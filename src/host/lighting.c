/*
 * Minimalpad host module: the pad's own lighting, read and set from a host
 *
 * GET_LIGHTING and SET_LIGHTING read and change the underglow the pad keeps as
 * its own: on or off, colour, brightness, effect and speed, the lighting its
 * Connections & LEDs keys and the dial change (host-module.md, "Lighting").
 * A change is saved the way those keys' changes are, through ZMK's settings a
 * minute after the last one, so it lasts without Studio.
 *
 * ZMK raises no event when the underglow changes, so after a key or the dial
 * the pad looks for itself and tells every host listening with LIGHTING_STATE.
 * The same look works around ZMK issue #1920: a colour or brightness from the
 * keys or the dial is set without a save, so the pad schedules one.
 *
 * ZMK cannot report the animation speed either, so the pad counts it: read
 * from ZMK's saved state at start-up, set exactly by SET_LIGHTING, and moved
 * by the dial's Faster and Slower steps.
 *
 * Where the pad keeps its own colour and effect while a host colour shows is
 * host.c's business (mp_host_own_color() and the rest), and whether the LEDs
 * are resting for idle is idle_fade.c's (leds.h).
 *
 * Threading: commands, key and dial events and the idle fade all run on the
 * system work queue. The saved speed is read on the thread loading the
 * settings, at their commit, and lighting_settings_commit() says why.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include <dt-bindings/zmk/rgb.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/sensor_event.h>
#include <zmk/keymap.h>
#include <zmk/matrix.h>
#include <zmk/rgb_underglow.h>

#include <minimalpad/host.h>
#include <minimalpad/leds.h>

LOG_MODULE_DECLARE(minimalpad_host, CONFIG_MINIMALPAD_HOST_LOG_LEVEL);

/*
 * How long after a key or the dial the pad looks at its lighting, and again
 * after a look that found a change. The dial's lighting steps run through ZMK's
 * behavior queue a few milliseconds apart, so they can still be landing after
 * the turn.
 */
#define LOOK_DELAY_MS 75

/* The &rgb_ug behavior, by the name a keymap binding gives it. */
#define UNDERGLOW_BEHAVIOR DEVICE_DT_NAME(DT_NODELABEL(rgb_ug))

/*
 * The speed
 */

/*
 * ZMK's saved underglow state, rgb/underglow/state: struct rgb_underglow_state
 * in rgb_underglow.c at the revision config/west.yml pins, which ZMK keeps
 * private. The colour takes bytes 0-3 and the animation speed byte 4.
 */
#define UNDERGLOW_STATE_SETTING "rgb/underglow/state"
#define UNDERGLOW_STATE_LEN 10
#define UNDERGLOW_STATE_SPEED 4

static uint8_t speed = CONFIG_ZMK_RGB_UNDERGLOW_SPD_START;

/*
 * Whether `speed` is the one the lights run at: not until the saved one has
 * been read, and not after a key the pad does not count changed it.
 */
static bool speed_exact = !IS_ENABLED(CONFIG_SETTINGS);

/*
 * Whether lighting_boot() has run. Until then the settings are still loading,
 * and a change a look sees is ZMK's saved lighting arriving, not a key's. A
 * save for it can reach settings_delete() through the idle flag (leds.h), on
 * the system work queue, while the thread loading the settings holds their
 * lock and goes on to need that queue for Bluetooth: the start-up hang again,
 * on a pad restarted with its lights off for idle and pressed as it starts.
 */
static bool started = !IS_ENABLED(CONFIG_SETTINGS);

static void set_speed(uint8_t value) {
    // ZMK only steps the speed, and its count is a uint8_t that a larger step can wrap. Four
    // steps down reach the slowest from anywhere, then up to the one asked for.
    for (int i = MP_LIGHTING_SPEED_MIN; i < MP_LIGHTING_SPEED_MAX; i++) {
        zmk_rgb_underglow_change_spd(-1);
    }
    for (int i = MP_LIGHTING_SPEED_MIN; i < value; i++) {
        zmk_rgb_underglow_change_spd(1);
    }
    mp_leds_wait_for_save();

    speed = value;
    speed_exact = true;
}

void mp_host_lighting_speed_stepped(int steps) {
    if (steps == 0) {
        return;
    }

    // ZMK stops at the slowest and the fastest one step at a time, and every step of one turn
    // goes the same way, so stopping the sum there comes to the same.
    speed = CLAMP(speed + steps, MP_LIGHTING_SPEED_MIN, MP_LIGHTING_SPEED_MAX);

    // ZMK schedules its save even for a Faster step at the fastest, which leaves the count as
    // it was, so the look would see no change to wait for. A Slower step at the slowest saves
    // nothing, and waiting for it only keeps the lights on a little longer. Not before
    // start-up is done: see `started`.
    if (started) {
        mp_leds_wait_for_save();
    }
}

/*
 * Whether a key at `position`, on any layer, changes the speed. The pad counts
 * the dial's steps but not those keys; the default keymap has none.
 */
static bool speed_key_at(uint32_t position) {
    if (position >= ZMK_KEYMAP_LEN) {
        return false;
    }

    for (zmk_keymap_layer_id_t id = 0; id < ZMK_KEYMAP_LAYERS_LEN; id++) {
        const struct zmk_behavior_binding *binding =
            zmk_keymap_get_layer_binding_at_idx(id, (uint16_t)position);

        // A reserved slot no layer has taken has no behavior.
        if (binding != NULL && binding->behavior_dev != NULL &&
            strcmp(binding->behavior_dev, UNDERGLOW_BEHAVIOR) == 0 &&
            (binding->param1 == RGB_SPI_CMD || binding->param1 == RGB_SPD_CMD)) {
            return true;
        }
    }

    return false;
}

/*
 * LIGHTING_STATE
 */

/* Resting LEDs are on as far as the person is concerned, whatever ZMK says at the end of the
 * fade. */
static bool lights_on(void) {
    bool on = false;
    return (zmk_rgb_underglow_get_state(&on) == 0 && on) || mp_leds_resting();
}

static struct mp_lighting_state current_lighting(void) {
    const struct zmk_led_hsb color = mp_host_own_color();
    const bool resting = mp_leds_resting();
    uint8_t flags = 0;

    if (mp_host_leds_held()) {
        flags |= MP_LIGHTING_FLAG_HOST_COLOR;
    }
    if (resting) {
        flags |= MP_LIGHTING_FLAG_RESTING;
    }
    if (speed_exact) {
        flags |= MP_LIGHTING_FLAG_SPEED_EXACT;
    }

    return (struct mp_lighting_state){
        .flags = flags,
        .on = lights_on(),
        // ZMK also takes 360, which is 0 again.
        .hue = sys_cpu_to_le16(color.h % (MP_HUE_MAX + 1)),
        .saturation = MIN(color.s, MP_SATURATION_MAX),
        .brightness = MIN(color.b, MP_BRIGHTNESS_MAX),
        .effect = mp_host_own_effect(),
        .speed = speed,
        .effect_count = MP_LIGHTING_EFFECT_COUNT,
    };
}

/*
 * The LIGHTING_STATE every host listening heard last, flag bit 3 aside. An
 * answer to GET_LIGHTING goes to one host, so it does not count.
 */
static struct mp_lighting_state last_sent;
static bool last_sent_valid;

/* The lighting at the last look, to tell what a key or the dial changed. */
static struct mp_lighting_state seen;
static bool seen_valid;

static void remember_seen(const struct mp_lighting_state *state) {
    seen = *state;
    seen_valid = true;
}

static void send_to_every_host(struct mp_lighting_state state, bool answer) {
    last_sent = state;
    last_sent_valid = true;

    if (answer) {
        state.flags |= MP_LIGHTING_FLAG_SET_ANSWER;
    }
    mp_host_broadcast(MP_EVT_LIGHTING_STATE, &state, sizeof(state));

    // STATE's leds_on moves with on or off, which ZMK raises no event for either.
    mp_host_leds_changed();
}

/*
 * Compares the lighting with the last look, to save what ZMK leaves unsaved,
 * and with the last report, to tell every host of a change. Returns whether
 * it sent one.
 */
static bool look(void) {
    // Before start-up is done there is nothing to compare with yet (`started`). lighting_boot()
    // takes the lighting as it then is, key and dial changes included.
    if (!started) {
        return false;
    }

    const struct mp_lighting_state now = current_lighting();

    if (seen_valid) {
        const bool color_changed = now.hue != seen.hue || now.saturation != seen.saturation ||
                                   now.brightness != seen.brightness;

        if (color_changed && !mp_host_leds_held()) {
            // ZMK issue #1920: the Colour and Brightness keys and the dial's lighting steps
            // set a colour without scheduling a save. Under a host colour they change the
            // host's, which the pad's own never takes.
            mp_leds_save();
        } else if (now.on != seen.on || now.effect != seen.effect || now.speed != seen.speed) {
            // ZMK scheduled its own save for these.
            mp_leds_wait_for_save();
        }
    }
    remember_seen(&now);

    if (last_sent_valid && memcmp(&now, &last_sent, sizeof(now)) == 0) {
        return false;
    }

    send_to_every_host(now, false);
    return true;
}

static int64_t last_input;

static void look_after_input(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(look_after_input_work, look_after_input);

static void look_after_input(struct k_work *work) {
    const bool changed = look();

    // Looking again while things keep changing, or while the last key or turn may still be
    // landing, catches the dial's steps as they run through the behavior queue.
    if (changed || k_uptime_get() - last_input < LOOK_DELAY_MS) {
        k_work_schedule(&look_after_input_work, K_MSEC(LOOK_DELAY_MS));
    }
}

static void look_now(struct k_work *work) { look(); }

static K_WORK_DEFINE(look_now_work, look_now);

void mp_host_lighting_changed(void) { k_work_submit(&look_now_work); }

/*
 * Commands
 */

void mp_host_handle_get_lighting(const struct mp_host_transport *from, const uint8_t *payload) {
    ARG_UNUSED(payload);

    // Changes nothing, resting lights included: only SET_LIGHTING wakes them.
    const struct mp_lighting_state state = current_lighting();
    mp_host_reply(from, MP_EVT_LIGHTING_STATE, &state, sizeof(state));
}

void mp_host_handle_set_lighting(const struct mp_host_transport *from, const uint8_t *payload) {
    struct mp_set_lighting cmd;
    memcpy(&cmd, payload, sizeof(cmd));

    const uint16_t hue = sys_le16_to_cpu(cmd.hue);

    // host-module.md, "Answers and rejections": every field is checked whatever `fields` says,
    // before any part is applied, so a host bug is loud and nothing half-applies.
    if (cmd.on > 1 || hue > MP_HUE_MAX || cmd.saturation > MP_SATURATION_MAX ||
        cmd.brightness > MP_BRIGHTNESS_MAX || cmd.effect >= MP_LIGHTING_EFFECT_COUNT ||
        cmd.speed < MP_LIGHTING_SPEED_MIN || cmd.speed > MP_LIGHTING_SPEED_MAX) {
        LOG_WRN("Rejecting SET_LIGHTING: on %d, colour %d/%d/%d, effect %d, speed %d", cmd.on,
                hue, cmd.saturation, cmd.brightness, cmd.effect, cmd.speed);
        mp_host_reject(from, MP_CMD_SET_LIGHTING, MP_REJECTED_OUT_OF_RANGE);
        return;
    }

    // The fields bits a later revision adds are left alone, so its hosts still work here.
    const uint8_t fields = cmd.fields;

    // host-module.md, "Applying SET_LIGHTING". First wake or not, then on or off: the one
    // command that switches the lights on, because the person asked for it. Lights left off
    // are not held awake, and resting ones go dark without lighting up first.
    const bool on = (fields & MP_LIGHTING_FIELD_ON) ? cmd.on : lights_on();
    if (on) {
        mp_leds_hold_awake(MP_LIGHTING_HOLD_SECONDS * MSEC_PER_SEC);

        bool zmk_on = false;
        if (zmk_rgb_underglow_get_state(&zmk_on) == 0 && !zmk_on) {
            zmk_rgb_underglow_on();
            mp_leds_wait_for_save();
        }
    } else if (fields & MP_LIGHTING_FIELD_ON) {
        mp_leds_switch_off();
    }

    // Colour and brightness, into the pad's own wherever it is kept. Lights that are off come
    // back in it.
    const struct zmk_led_hsb before = mp_host_own_color();
    struct zmk_led_hsb color = before;
    if (fields & MP_LIGHTING_FIELD_COLOR) {
        color.h = hue;
        color.s = cmd.saturation;
    }
    if (fields & MP_LIGHTING_FIELD_BRIGHTNESS) {
        color.b = cmd.brightness;
    }
    if (color.h != before.h || color.s != before.s || color.b != before.b) {
        int err = mp_host_set_own_color(color);
        if (err < 0) {
            LOG_WRN("Failed to set the pad's colour (%d)", err);
        } else if (!mp_host_leds_held()) {
            mp_leds_save();
        }
    }

    if ((fields & MP_LIGHTING_FIELD_EFFECT) && cmd.effect != mp_host_own_effect()) {
        int err = mp_host_set_own_effect(cmd.effect);
        if (err < 0) {
            LOG_WRN("Failed to set the pad's effect (%d)", err);
        }
    }

    // A count that may be wrong is put right even when it agrees.
    if ((fields & MP_LIGHTING_FIELD_SPEED) && (cmd.speed != speed || !speed_exact)) {
        set_speed(cmd.speed);
    }

    // To every host, sender included: the change is news to all of them, and the flag tells
    // the sender its answer from a report the pad volunteered just before.
    const struct mp_lighting_state state = current_lighting();
    remember_seen(&state);
    send_to_every_host(state, true);
}

/*
 * Keys and the dial
 */

static int lighting_event_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *position = as_zmk_position_state_changed(eh);
    if (position != NULL && position->state && speed_key_at(position->position)) {
        // A speed the pad does not count. The next SET_LIGHTING that sets one puts it right.
        speed_exact = false;

        // ZMK schedules its save for the key, and the look cannot see the speed change. Not
        // before start-up is done: see `started`.
        if (started) {
            mp_leds_wait_for_save();
        }
    }

    // The look waits for the key's behavior, and for the dial's steps, to have run. A look
    // already waiting is not put off, so turning the dial updates a host as it goes.
    last_input = k_uptime_get();
    k_work_schedule(&look_after_input_work, K_MSEC(LOOK_DELAY_MS));
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(minimalpad_lighting, lighting_event_listener);
ZMK_SUBSCRIPTION(minimalpad_lighting, zmk_position_state_changed);
ZMK_SUBSCRIPTION(minimalpad_lighting, zmk_sensor_event);

/*
 * Start-up
 */

#if IS_ENABLED(CONFIG_SETTINGS)

struct saved_speed {
    bool read;
    uint8_t value;
};

static int read_saved_speed(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg,
                            void *param) {
    struct saved_speed *saved = param;
    uint8_t record[UNDERGLOW_STATE_LEN];

    if (key != NULL) {
        // Saved below ZMK's record, not the record itself.
        return 0;
    }

    // A record of another length is another ZMK's, whose layout this does not know.
    if (len != sizeof(record) ||
        read_cb(cb_arg, record, sizeof(record)) != (ssize_t)sizeof(record)) {
        saved->read = false;
        return 0;
    }

    saved->value = record[UNDERGLOW_STATE_SPEED];
    saved->read =
        saved->value >= MP_LIGHTING_SPEED_MIN && saved->value <= MP_LIGHTING_SPEED_MAX;
    return 0;
}

/* The speed ZMK saved, read at commit and taken up by lighting_boot(). */
static struct saved_speed boot_speed;
static int boot_speed_err;

static void lighting_boot(struct k_work *work) {
    if (boot_speed_err < 0 || !boot_speed.read) {
        LOG_WRN("Could not read the underglow's speed from ZMK's saved state (%d)",
                boot_speed_err);
        speed_exact = false;
    } else {
        speed = boot_speed.value;
        speed_exact = true;
    }

    // The lighting the pad starts with, so the first look reports only a change.
    const struct mp_lighting_state state = current_lighting();
    remember_seen(&state);
    started = true;
    last_sent = state;
    last_sent_valid = true;
}

static K_WORK_DEFINE(lighting_boot_work, lighting_boot);

static int lighting_settings_set(const char *name, size_t len, settings_read_cb read_cb,
                                 void *cb_arg) {
    // Nothing is saved under this name: the lighting is ZMK's.
    return -ENOENT;
}

static int lighting_settings_commit(void) {
    // Commit comes after every subtree has loaded, ZMK's underglow state included. A second
    // settings handler for ZMK's key would take over ZMK's own load, so the record is read
    // directly. With none saved, ZMK starts at its configured speed.
    //
    // It is read here, on the thread loading the settings, and never from the system work
    // queue. That thread holds the settings lock until every commit has run, ZMK's Bluetooth
    // start-up among them, and Bluetooth sends its commands from the work queue. A read there
    // would wait for the lock while the lock's holder waited for the work queue, and the pad
    // would hang at every start. The thread holding the lock can take it again, so the read
    // here goes straight through.
    boot_speed = (struct saved_speed){.read = true, .value = CONFIG_ZMK_RGB_UNDERGLOW_SPD_START};
    boot_speed_err =
        settings_load_subtree_direct(UNDERGLOW_STATE_SETTING, read_saved_speed, &boot_speed);

    k_work_submit(&lighting_boot_work);
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(minimalpad_lighting, "mp_lighting", NULL, lighting_settings_set,
                               lighting_settings_commit, NULL);

#endif /* IS_ENABLED(CONFIG_SETTINGS) */
