/*
 * Minimalpad host module: what the commands do
 *
 * host-protocol.md is the contract with Minimalpad Studio. ADR 0005 is the rule
 * this file keeps: when nothing is driving the pad, it goes back to Default and
 * its own LEDs, the pad it is out of the box.
 *
 * Everything here runs on the system work queue, where ZMK also processes key
 * presses, so a profile switch never lands between a key's press and its
 * release. ZMK event listeners and transports only note what happened and
 * submit work.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/reboot.h>

#if IS_ENABLED(CONFIG_RETENTION_BOOT_MODE)
#include <zephyr/retention/bootmode.h>
#else
#include <dt-bindings/zmk/reset.h>
#endif

#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
#include <zmk/battery.h>
#endif

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW)
#include <zmk/rgb_underglow.h>

#include <minimalpad/leds.h>
#endif

#if IS_ENABLED(CONFIG_ZMK_USB)
#include <zmk/usb.h>
#endif

#include <minimalpad/host.h>
#include <minimalpad/version.h>

LOG_MODULE_REGISTER(minimalpad_host, CONFIG_MINIMALPAD_HOST_LOG_LEVEL);

BUILD_ASSERT(sizeof(struct mp_hello_ack) == 7, "HELLO_ACK carries 7 payload bytes");
BUILD_ASSERT(sizeof(struct mp_state_event) == 5, "STATE carries 5 payload bytes");
BUILD_ASSERT(sizeof(struct mp_fallback_event) == 1, "FALLBACK carries 1 payload byte");
BUILD_ASSERT(sizeof(struct mp_rejected_event) == 2, "REJECTED carries 2 payload bytes");

/* STATE's battery byte when the pad does not know. */
#define STATE_BATTERY_UNKNOWN 0xFF

/* Lets the ATT write response leave before the pad resets, so the host sees its
 * ENTER_BOOTLOADER succeed rather than fail with the link. */
#define BOOTLOADER_REBOOT_DELAY K_MSEC(100)

/* Every way a host can reach the pad. The USB channel joins this list. */
static const struct mp_host_transport *const transports[] = {
    &mp_host_transport_gatt,
#if IS_ENABLED(CONFIG_MINIMALPAD_HOST_USB)
    &mp_host_transport_usb,
#endif
};

/* The layer the host last set: STATE's layer_id, the profile. */
static zmk_keymap_layer_id_t host_layer;

/* SET_PROFILE's dial keycodes, kept for the dial swap still to come. */
static bool dial_set;
static uint32_t dial_cw;
static uint32_t dial_ccw;

/*
 * Sending
 */

/* Frames an event and hands it to one of a transport's senders. */
static int send_frame(int (*send)(const uint8_t *frame, size_t len), uint8_t event,
                      const void *payload, uint8_t len) {
    if (len > MP_HOST_PAYLOAD_MAX) {
        return -EMSGSIZE;
    }

    const struct mp_host_frame_header header = {
        .version = MP_HOST_PROTOCOL_VERSION,
        .command = event,
        .length = len,
    };
    uint8_t frame[MP_HOST_FRAME_MAX];

    memcpy(frame, &header, sizeof(header));
    if (len > 0) {
        memcpy(frame + sizeof(header), payload, len);
    }

    return send(frame, sizeof(header) + len);
}

int mp_host_send(const struct mp_host_transport *to, uint8_t event, const void *payload,
                 uint8_t len) {
    return send_frame(to->send, event, payload, len);
}

void mp_host_broadcast(uint8_t event, const void *payload, uint8_t len) {
    for (size_t i = 0; i < ARRAY_SIZE(transports); i++) {
        int err = mp_host_send(transports[i], event, payload, len);

        // No host listening is the usual case, not a failure.
        if (err < 0 && err != -ENOTCONN) {
            LOG_WRN("Failed to send event 0x%02x over %s (%d)", event, transports[i]->name, err);
        }
    }
}

/* Answers the host whose frame is being handled, and only that host
 * (host-protocol.md, "Who hears what"). */
static void reply(const struct mp_host_transport *to, uint8_t event, const void *payload,
                  uint8_t len) {
    int err = send_frame(to->reply, event, payload, len);
    if (err < 0) {
        LOG_WRN("Failed to answer with event 0x%02x over %s (%d)", event, to->name, err);
    }
}

void mp_host_reject(const struct mp_host_transport *from, uint8_t command,
                    enum mp_rejected_reason reason) {
    const struct mp_rejected_event event = {.command = command, .reason = reason};
    reply(from, MP_EVT_REJECTED, &event, sizeof(event));
}

/*
 * Layers
 */

/*
 * host-protocol.md, "Layer identity": the host names a layer by ZMK's stable id,
 * not its position. An id counts only while some position in the layer order
 * holds it: a reserved slot nobody has filled, or a layer Studio removed, has
 * none, and zmk_keymap_layer_to() would quietly accept it anyway.
 */
static bool layer_in_keymap(zmk_keymap_layer_id_t id) {
    if (id >= ZMK_KEYMAP_LAYERS_LEN) {
        return false;
    }

    for (zmk_keymap_layer_index_t index = 0; index < ZMK_KEYMAP_LAYERS_LEN; index++) {
        if (zmk_keymap_layer_index_to_id(index) == id) {
            return true;
        }
    }

    return false;
}

/*
 * Reserved slots still unused, counted as ZMK Studio counts available_layers
 * (studio/keymap_subsystem.c), so both channels report the same budget. Unused
 * slots always sit at the end of the order.
 */
static uint8_t free_layer_slots(void) {
    for (zmk_keymap_layer_index_t index = 0; index < ZMK_KEYMAP_LAYERS_LEN; index++) {
        if (zmk_keymap_layer_index_to_id(index) == ZMK_KEYMAP_LAYER_ID_INVAL) {
            return ZMK_KEYMAP_LAYERS_LEN - index;
        }
    }

    return 0;
}

/*
 * LEDs
 */

/* host-protocol.md, "Validation": checked on every frame, whatever its flags say
 * and whether or not this build has underglow, so a host bug is loud everywhere. */
static bool leds_in_range(uint16_t hue, uint8_t saturation, uint8_t brightness) {
    return hue <= MP_HUE_MAX && saturation <= MP_SATURATION_MAX && brightness <= MP_BRIGHTNESS_MAX;
}

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW)

/* rgb_underglow.c numbers its effects in an enum it does not export; solid is 0. */
#define UNDERGLOW_EFFECT_SOLID 0

/*
 * The pad's own underglow, taken just before the first host colour replaces it
 * and put back on fallback. zmk_rgb_underglow_set_hsb() takes hue in degrees and
 * saturation and brightness in percent, the protocol's own units, so colours
 * pass through unconverted. Colours go through leds.h, so one taken or set
 * while the LEDs fade out for idle is the pad's and not a dimmed step.
 */
struct leds_snapshot {
    struct zmk_led_hsb color;
    uint8_t effect;
};

static struct leds_snapshot leds_snapshot;
static bool leds_snapshot_held;

#if IS_ENABLED(CONFIG_SETTINGS)

#define LEDS_SNAPSHOT_SETTING "mp_host/leds"

/* How long past ZMK's own save debounce the copy in flash is kept after a restore. */
#define LEDS_SNAPSHOT_FORGET_MARGIN_MS 5000

/*
 * The snapshot is kept in flash as well as in RAM. ZMK saves the whole underglow
 * state, colour included, whenever the LEDs go off for idle, and waking from
 * deep sleep is a reboot. Without its own copy, a pad that sleeps or loses power
 * on a profile would wake in the profile's colour, and the next snapshot would
 * take that colour for the pad's. It costs a write per stretch of host control,
 * not per switch.
 */
static void leds_snapshot_save(struct k_work *work) {
    int err = settings_save_one(LEDS_SNAPSHOT_SETTING, &leds_snapshot, sizeof(leds_snapshot));
    if (err < 0) {
        LOG_WRN("Failed to save the underglow snapshot (%d)", err);
    }
}

static K_WORK_DEFINE(leds_snapshot_save_work, leds_snapshot_save);

static void leds_snapshot_forget(struct k_work *work) {
    int err = settings_delete(LEDS_SNAPSHOT_SETTING);
    if (err < 0) {
        LOG_WRN("Failed to delete the underglow snapshot (%d)", err);
    }
}

static K_WORK_DELAYABLE_DEFINE(leds_snapshot_forget_work, leds_snapshot_forget);

#endif /* IS_ENABLED(CONFIG_SETTINGS) */

static void leds_apply(uint16_t hue, uint8_t saturation, uint8_t brightness) {
    if (!leds_snapshot_held) {
        // With a direction of 0 this returns the current effect unchanged.
        leds_snapshot = (struct leds_snapshot){
            .color = mp_leds_color(),
            .effect = zmk_rgb_underglow_calc_effect(0),
        };
        leds_snapshot_held = true;

#if IS_ENABLED(CONFIG_SETTINGS)
        k_work_cancel_delayable(&leds_snapshot_forget_work);
        k_work_submit(&leds_snapshot_save_work);
#endif
    }

    // Only the colour changes. LEDs that are off, by hand or for idle, stay off and wake in
    // this colour, and setting it saves nothing.
    int err = mp_leds_set_color((struct zmk_led_hsb){
        .h = hue,
        .s = saturation,
        .b = brightness,
    });
    if (err < 0) {
        LOG_WRN("Failed to set the underglow colour (%d)", err);
    }

    if (IS_ENABLED(CONFIG_MINIMALPAD_HOST_LEDS_FORCE_SOLID) &&
        zmk_rgb_underglow_calc_effect(0) != UNDERGLOW_EFFECT_SOLID) {
        zmk_rgb_underglow_select_effect(UNDERGLOW_EFFECT_SOLID);
    }
}

static void leds_restore(void) {
    if (!leds_snapshot_held) {
        return;
    }

    mp_leds_set_color(leds_snapshot.color);
    // Selecting the effect also schedules ZMK's own save of the underglow state, which holds
    // the pad's colour again by the time it runs.
    zmk_rgb_underglow_select_effect(leds_snapshot.effect);
    leds_snapshot_held = false;

#if IS_ENABLED(CONFIG_SETTINGS)
    // Keep the copy in flash until that save has had its chance, so a power cut in between
    // still wakes the pad in its own colours.
    k_work_reschedule(&leds_snapshot_forget_work,
                      K_MSEC(CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE + LEDS_SNAPSHOT_FORGET_MARGIN_MS));
#endif
}

static bool leds_on(void) {
    bool on = false;
    return zmk_rgb_underglow_get_state(&on) == 0 && on;
}

#else /* IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW) */

static void leds_apply(uint16_t hue, uint8_t saturation, uint8_t brightness) {}

static void leds_restore(void) {}

static bool leds_on(void) { return false; }

#endif /* IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW) */

/*
 * STATE
 */

static uint8_t battery_percent(void) {
#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING) && DT_HAS_CHOSEN(zmk_battery)
    // Without a ready sensor ZMK never takes a reading and would report 0% for good.
    if (device_is_ready(DEVICE_DT_GET(DT_CHOSEN(zmk_battery)))) {
        uint8_t percent = zmk_battery_state_of_charge();
        return MIN(percent, 100);
    }
#endif

    return STATE_BATTERY_UNKNOWN;
}

static uint8_t state_endpoint(enum zmk_transport transport) {
    switch (transport) {
    case ZMK_TRANSPORT_USB:
        return MP_ENDPOINT_USB;
    case ZMK_TRANSPORT_BLE:
        return MP_ENDPOINT_BLUETOOTH;
    default:
        // Nothing is ready to take keys, although the host hearing this still reaches the pad.
        return MP_ENDPOINT_NONE;
    }
}

static struct mp_state_event current_state(void) {
    return (struct mp_state_event){
        .layer_id = host_layer,
        .top_layer_id = zmk_keymap_layer_index_to_id(zmk_keymap_highest_layer_active()),
        .endpoint = state_endpoint(zmk_endpoint_get_selected().transport),
        .battery = battery_percent(),
        .leds_on = leds_on(),
    };
}

/* The STATE last broadcast, to tell a change from a repeat. */
static struct mp_state_event last_state;
static bool last_state_valid;
static atomic_t state_forced;

static void state_update(struct k_work *work) {
    const bool forced = atomic_set(&state_forced, 0);
    const struct mp_state_event state = current_state();

    // host-protocol.md, "State": sent whenever the layer, endpoint or LED state changes.
    // Battery rides along but does not send one by itself.
    const bool changed = !last_state_valid || state.layer_id != last_state.layer_id ||
                         state.top_layer_id != last_state.top_layer_id ||
                         state.endpoint != last_state.endpoint ||
                         state.leds_on != last_state.leds_on;

    if (!forced && !changed) {
        return;
    }

    last_state = state;
    last_state_valid = true;
    mp_host_broadcast(MP_EVT_STATE, &state, sizeof(state));
}

static K_WORK_DEFINE(state_work, state_update);

/*
 * Sends STATE once the work queue gets to it, which coalesces:
 * zmk_keymap_layer_to() switches layers one at a time and the host needs only
 * where they end up. Forced sends go out even when nothing changed, for a host
 * that has just started listening.
 */
static void request_state(bool forced) {
    if (forced) {
        atomic_set(&state_forced, 1);
    }

    k_work_submit(&state_work);
}

void mp_host_host_arrived(const struct mp_host_transport *transport) {
    LOG_DBG("A host is listening over %s", transport->name);
    request_state(true);
}

void mp_host_leds_changed(void) { request_state(false); }

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW) && IS_ENABLED(CONFIG_SETTINGS)

static void leds_boot_restore(struct k_work *work) {
    LOG_INF("Restoring the underglow a host colour replaced before the pad went down");
    leds_restore();
    request_state(false);
}

static K_WORK_DEFINE(leds_boot_restore_work, leds_boot_restore);

static int host_settings_set(const char *name, size_t len, settings_read_cb read_cb,
                             void *cb_arg) {
    const char *next;

    if (settings_name_steq(name, "leds", &next) && !next) {
        if (len != sizeof(leds_snapshot)) {
            return -EINVAL;
        }

        int rc = read_cb(cb_arg, &leds_snapshot, sizeof(leds_snapshot));
        if (rc < 0) {
            return rc;
        }

        leds_snapshot_held = true;
        return 0;
    }

    return -ENOENT;
}

static int host_settings_commit(void) {
    // A snapshot still in flash means the pad went down while a host colour showed, so no
    // fallback ran. Commit comes after every subtree has loaded, so the colour this replaces
    // is the one ZMK just restored.
    if (leds_snapshot_held) {
        k_work_submit(&leds_boot_restore_work);
    }

    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(minimalpad_host, "mp_host", NULL, host_settings_set,
                               host_settings_commit, NULL);

#endif /* IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW) && IS_ENABLED(CONFIG_SETTINGS) */

/*
 * Fallback, ADR 0005
 */

/* The window the last HEARTBEAT named. SET_PROFILE and SET_LEDS arm it too. */
static uint8_t heartbeat_window = CONFIG_MINIMALPAD_HOST_HEARTBEAT_DEFAULT_TIMEOUT;

static void heartbeat_expired(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(heartbeat_work, heartbeat_expired);

static void fall_back(enum mp_fallback_reason reason) {
    LOG_INF("Falling back to Default (reason %d)", reason);

    // Nothing left to watch until a host drives the pad again.
    k_work_cancel_delayable(&heartbeat_work);

    // Locking, as for SET_PROFILE, so a layer toggled on with &tog goes as well: Default means
    // the pad as it is out of the box.
    host_layer = zmk_keymap_layer_default();
    zmk_keymap_layer_to(host_layer, true);

    leds_restore();

    dial_set = false;
    dial_cw = 0;
    dial_ccw = 0;

    // Only a host still listening hears this. A host whose link dropped learns it from the
    // STATE it asks for when it comes back.
    const struct mp_fallback_event event = {.reason = reason};
    mp_host_broadcast(MP_EVT_FALLBACK, &event, sizeof(event));

    request_state(false);
}

static void heartbeat_expired(struct k_work *work) { fall_back(MP_FALLBACK_HEARTBEAT_LOST); }

/* The reason, plus one, of a fallback an event listener asked for; 0 when none is pending. */
static atomic_t fallback_pending;

static void fallback_requested(struct k_work *work) {
    const atomic_val_t pending = atomic_set(&fallback_pending, 0);

    if (pending > 0) {
        fall_back((enum mp_fallback_reason)(pending - 1));
    }
}

static K_WORK_DEFINE(fallback_work, fallback_requested);

static void request_fallback(enum mp_fallback_reason reason) {
    // The first reason stands until the work runs, because that one is the cause.
    atomic_cas(&fallback_pending, 0, (atomic_val_t)reason + 1);
    k_work_submit(&fallback_work);
}

/*
 * Starts the fallback window unless it is already running. Only HEARTBEAT moves
 * a running window, so a host that switches profiles but has stopped beating
 * still loses the pad on time.
 */
static void arm_fallback(void) { k_work_schedule(&heartbeat_work, K_SECONDS(heartbeat_window)); }

/*
 * Commands
 */

void mp_host_handle_hello(const struct mp_host_transport *from, const uint8_t *payload) {
    ARG_UNUSED(payload);

    const struct mp_hello_ack ack = {
        .protocol = MP_HOST_PROTOCOL_VERSION,
        .fw_major = MINIMALPAD_VERSION_MAJOR,
        .fw_minor = MINIMALPAD_VERSION_MINOR,
        .fw_patch = MINIMALPAD_VERSION_PATCH,
        // Dial swap stays clear until the dial keycodes SET_PROFILE carries drive the dial.
        .caps = MP_CAP_PROFILES | (IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW) ? MP_CAP_LEDS : 0),
        .layer_count = ZMK_KEYMAP_LAYERS_LEN,
        .free_layers = free_layer_slots(),
    };

    reply(from, MP_EVT_HELLO_ACK, &ack, sizeof(ack));

    // USB has no subscribe step, so HELLO is when that host arrives. Bluetooth
    // already volunteered STATE on subscribe; forcing a current one here is
    // harmless and makes both transports follow one handshake rule.
    mp_host_host_arrived(from);
}

void mp_host_handle_set_profile(const struct mp_host_transport *from, const uint8_t *payload) {
    struct mp_set_profile cmd;
    memcpy(&cmd, payload, sizeof(cmd));

    const uint16_t hue = sys_le16_to_cpu(cmd.hue);
    const bool set_leds = cmd.flags & MP_PROFILE_FLAG_SET_LEDS;
    const bool set_dial = cmd.flags & MP_PROFILE_FLAG_SET_DIAL;

    // host-protocol.md, "Validation": the whole frame is checked before any part of it is
    // applied, so an app switch cannot half-apply, and a bad one is answered with REJECTED.
    if (!leds_in_range(hue, cmd.saturation, cmd.brightness)) {
        LOG_WRN("Rejecting SET_PROFILE with colour %d/%d/%d out of range", hue, cmd.saturation,
                cmd.brightness);
        mp_host_reject(from, MP_CMD_SET_PROFILE, MP_REJECTED_OUT_OF_RANGE);
        return;
    }

    if (!layer_in_keymap(cmd.layer_id)) {
        LOG_WRN("Rejecting SET_PROFILE for layer id %d, which the keymap does not hold",
                cmd.layer_id);
        mp_host_reject(from, MP_CMD_SET_PROFILE, MP_REJECTED_UNKNOWN_LAYER);
        return;
    }

    // Locking, as &to switches layers, so releasing a &mo for the same layer cannot drop the
    // profile.
    host_layer = cmd.layer_id;
    zmk_keymap_layer_to(cmd.layer_id, true);

    // host-protocol.md, "Switching": every SET_PROFILE describes the whole switch. A flag set
    // takes that part from the frame; a flag clear puts back the pad's own, so switching to an
    // app without a profile leaves the pad as it is without Studio.
    if (set_leds) {
        leds_apply(hue, cmd.saturation, cmd.brightness);
    } else {
        leds_restore();
    }

    if (set_dial) {
        dial_cw = sys_le32_to_cpu(cmd.dial_cw);
        dial_ccw = sys_le32_to_cpu(cmd.dial_ccw);
        dial_set = true;
    } else {
        dial_set = false;
        dial_cw = 0;
        dial_ccw = 0;
    }

    arm_fallback();
    request_state(false);
}

void mp_host_handle_heartbeat(const struct mp_host_transport *from, const uint8_t *payload) {
    ARG_UNUSED(from);

    struct mp_heartbeat cmd;
    memcpy(&cmd, payload, sizeof(cmd));

    if (cmd.timeout_seconds == MP_HEARTBEAT_RELEASE) {
        // host-protocol.md, "Heartbeat": a host that quits or pauses switching lets go at once.
        // The window is not set to zero, which would make the next host's first SET_PROFILE
        // fall back as soon as it lands; the next host starts from the default instead.
        heartbeat_window = CONFIG_MINIMALPAD_HOST_HEARTBEAT_DEFAULT_TIMEOUT;
        fall_back(MP_FALLBACK_RELEASED);
        return;
    }

    // Every HEARTBEAT restarts the window with its own timeout.
    heartbeat_window = cmd.timeout_seconds;
    k_work_reschedule(&heartbeat_work, K_SECONDS(cmd.timeout_seconds));
}

void mp_host_handle_set_leds(const struct mp_host_transport *from, const uint8_t *payload) {
    struct mp_set_leds cmd;
    memcpy(&cmd, payload, sizeof(cmd));

    const uint16_t hue = sys_le16_to_cpu(cmd.hue);

    if (!leds_in_range(hue, cmd.saturation, cmd.brightness)) {
        LOG_WRN("Rejecting SET_LEDS with colour %d/%d/%d out of range", hue, cmd.saturation,
                cmd.brightness);
        mp_host_reject(from, MP_CMD_SET_LEDS, MP_REJECTED_OUT_OF_RANGE);
        return;
    }

    if (!IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW)) {
        // HELLO_ACK does not claim LEDs on this build.
        return;
    }

    leds_apply(hue, cmd.saturation, cmd.brightness);

    // A preview left showing is the host driving the pad too, so it falls back like a profile.
    arm_fallback();
    request_state(false);
}

static void reboot_into_bootloader(struct k_work *work) {
    // The same steps as ZMK's &bootloader behavior (behaviors/behavior_reset.c).
#if IS_ENABLED(CONFIG_RETENTION_BOOT_MODE)
    int err = bootmode_set(BOOT_MODE_TYPE_BOOTLOADER);
    if (err < 0) {
        LOG_ERR("Failed to set the bootloader boot mode (%d)", err);
        return;
    }

    sys_reboot(SYS_REBOOT_WARM);
#else
    sys_reboot(RST_UF2);
#endif
}

static K_WORK_DELAYABLE_DEFINE(bootloader_work, reboot_into_bootloader);

void mp_host_handle_enter_bootloader(const struct mp_host_transport *from,
                                     const uint8_t *payload) {
    struct mp_enter_bootloader cmd;
    memcpy(&cmd, payload, sizeof(cmd));

    // The magic word is what keeps a stray frame from rebooting the pad.
    const uint32_t magic = sys_le32_to_cpu(cmd.magic);
    if (magic != MP_BOOTLOADER_MAGIC) {
        LOG_WRN("Rejecting ENTER_BOOTLOADER with magic 0x%08x", magic);
        mp_host_reject(from, MP_CMD_ENTER_BOOTLOADER, MP_REJECTED_WRONG_MAGIC);
        return;
    }

    LOG_INF("Rebooting into the bootloader");
    k_work_schedule(&bootloader_work, BOOTLOADER_REBOOT_DELAY);
}

void mp_host_handle_get_state(const struct mp_host_transport *from, const uint8_t *payload) {
    ARG_UNUSED(payload);

    const struct mp_state_event state = current_state();
    reply(from, MP_EVT_STATE, &state, sizeof(state));
}

/*
 * ZMK events
 */

/* The endpoint and Bluetooth profile as they were before the event in hand. */
static enum zmk_transport last_transport = ZMK_TRANSPORT_NONE;
static int last_ble_profile = -1;

static bool usb_powered(void) {
#if IS_ENABLED(CONFIG_ZMK_USB)
    return zmk_usb_is_powered();
#else
    return false;
#endif
}

static int host_event_listener(const zmk_event_t *eh) {
    const struct zmk_endpoint_changed *endpoint_ev = as_zmk_endpoint_changed(eh);
    if (endpoint_ev) {
        // USB unplugged, but only while USB carried the keys: a pad charging over USB while it
        // types over Bluetooth has lost nothing when the cable comes out.
        if (last_transport == ZMK_TRANSPORT_USB &&
            endpoint_ev->endpoint.transport != ZMK_TRANSPORT_USB && !usb_powered()) {
            request_fallback(MP_FALLBACK_USB_UNPLUGGED);
        }

        last_transport = endpoint_ev->endpoint.transport;
        request_state(false);
        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_ble_active_profile_changed *profile_ev =
        as_zmk_ble_active_profile_changed(eh);
    if (profile_ev) {
        // Bluetooth profile changed. ble.c also raises this when the active profile connects,
        // disconnects or pairs, so only a different index is a change.
        if (last_ble_profile >= 0 && profile_ev->index != last_ble_profile) {
            request_fallback(MP_FALLBACK_BT_PROFILE_CHANGED);
        }

        last_ble_profile = profile_ev->index;

        // The active profile's host may have just connected with its subscription restored
        // from the bond. The CCC callback only reports the first subscriber of all connections,
        // so this is a second chance to volunteer STATE.
        request_state(true);
        return ZMK_EV_EVENT_BUBBLE;
    }

    // A layer change, or an activity change, since waking switches the underglow back on. The
    // idle fade reports switching off itself, through mp_host_leds_changed().
    request_state(false);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(minimalpad_host, host_event_listener);
ZMK_SUBSCRIPTION(minimalpad_host, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(minimalpad_host, zmk_endpoint_changed);
ZMK_SUBSCRIPTION(minimalpad_host, zmk_ble_active_profile_changed);
ZMK_SUBSCRIPTION(minimalpad_host, zmk_activity_state_changed);

bool mp_host_dial_keycodes(uint32_t *clockwise, uint32_t *counter_clockwise) {
    if (!dial_set) {
        return false;
    }

    *clockwise = dial_cw;
    *counter_clockwise = dial_ccw;
    return true;
}

static int host_init(void) {
    LOG_INF("Minimalpad firmware %d.%d.%d", MINIMALPAD_VERSION_MAJOR, MINIMALPAD_VERSION_MINOR,
            MINIMALPAD_VERSION_PATCH);

    host_layer = zmk_keymap_layer_default();
    return 0;
}

SYS_INIT(host_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
