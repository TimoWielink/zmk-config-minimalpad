/*
 * Minimalpad host module: a light for each layer and each key, kept on the pad
 *
 * Every layer above Default can have a light, and every key on every layer,
 * Default included, a light of its own (host-protocol.md, "Layer and key
 * lights"). A light is Inherit, a colour or Off. Studio sets them with
 * SET_LAYER_LIGHT and SET_KEY_LIGHT and reads them back with GET_LAYER_LIGHT
 * and GET_KEY_LIGHTS, and finds out the pad has them with GET_FEATURES. They
 * are saved in the pad's settings by layer id, as dials are, so they last
 * through restarts and work without Studio.
 *
 * mp_lights_resolve() says which light a key shows: walking the active layers
 * from the top down to Default, the first key light or layer light that is not
 * Inherit, then Default's key light. Inherit there leaves the key to the
 * underglow.
 *
 * While the pad's own lighting is being shown (SHOW_PAD_LIGHTING, or
 * lighting.c after a key or the dial changed it), the renderer draws the
 * underglow as it is and ignores the lights. mp_lights_show_pad() sets the
 * deadline and mp_lights_showing_pad() says whether it has passed.
 *
 * Threading: commands run on the system work queue, and so does the save. The
 * underglow draws its frames on its own work queue, which asks
 * mp_lights_resolve(), and the settings load on the thread that loads them.
 * So every read and write of the tables below is under lights_lock, kept short:
 * nothing is saved or sent while it is held.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include <zmk/keymap.h>

#include <minimalpad/host.h>

LOG_MODULE_DECLARE(minimalpad_host, CONFIG_MINIMALPAD_HOST_LOG_LEVEL);

#define LAYER_SETTINGS_PREFIX "mp_light"
#define KEY_SETTINGS_PREFIX "mp_klight"

/* host-protocol.md, "Saving and clearing": saved once changes have stopped for this long. */
#define SAVE_DELAY_MS 2000

/* Key positions 0-15, the grid, and one bit each of SET_KEY_LIGHT's mask. */
#define KEY_COUNT 16

/* KEY_LIGHTS_STATE holds up to three lights, so a layer's keys are read in six
 * pages, named by their first key: 0, 3, 6, 9, 12 and 15. The last holds one. */
#define PAGE_LEN 3

BUILD_ASSERT(KEY_COUNT == SIZEOF_FIELD(struct mp_set_key_light, keys) * BITS_PER_BYTE,
             "a bit of SET_KEY_LIGHT's keys for every key");
BUILD_ASSERT(PAGE_LEN == SIZEOF_FIELD(struct mp_key_lights_state, lights) / sizeof(struct mp_light),
             "KEY_LIGHTS_STATE holds a page of lights");
BUILD_ASSERT(ZMK_KEYMAP_LAYERS_LEN <= NUM_BITS(uint32_t), "a dirty bit for every layer");

/*
 * The lights, by layer id, with the hue in CPU byte order. All Inherit (zero)
 * at start. Default's layer light stays Inherit: SET_LAYER_LIGHT refuses
 * anything else for it, and so does the load, so GET_LAYER_LIGHT answers
 * Inherit for Default.
 */

static struct k_spinlock lights_lock;
static struct mp_light layer_lights[ZMK_KEYMAP_LAYERS_LEN];
static struct mp_light key_lights[ZMK_KEYMAP_LAYERS_LEN][KEY_COUNT];

/* Layers whose layer light or key lights changed since the last save, by bit of layer id. */
static uint32_t dirty_layer_lights;
static uint32_t dirty_key_lights;

/*
 * Until when the pad's own lighting shows in place of layer and key lights, in
 * uptime milliseconds. Not saved: a restart shows layer and key lights again.
 * 64 bits, so the 32-bit core cannot read half of one being written, which is
 * why it is under lights_lock like the tables.
 */
static int64_t show_until;

static const struct mp_light inherit = {.kind = MP_LIGHT_INHERIT};

/*
 * host-protocol.md, "The light payload": a kind of 0-2, a colour's hue and
 * saturation in range, and Inherit and Off with a hue and saturation of 0.
 */
static bool light_valid(struct mp_light light) {
    switch (light.kind) {
    case MP_LIGHT_INHERIT:
    case MP_LIGHT_OFF:
        return light.hue == 0 && light.saturation == 0;
    case MP_LIGHT_COLOUR:
        return light.hue <= MP_HUE_MAX && light.saturation <= MP_SATURATION_MAX;
    default:
        return false;
    }
}

static bool lights_equal(struct mp_light a, struct mp_light b) {
    return a.kind == b.kind && a.hue == b.hue && a.saturation == b.saturation;
}

static struct mp_light light_from_wire(struct mp_light light) {
    light.hue = sys_le16_to_cpu(light.hue);
    return light;
}

static struct mp_light light_to_wire(struct mp_light light) {
    light.hue = sys_cpu_to_le16(light.hue);
    return light;
}

/*
 * Resolution
 */

/* Call with lights_lock held. */
static struct mp_light resolve(uint8_t position) {
    const zmk_keymap_layer_id_t base = zmk_keymap_layer_default();

    // As keys resolve: the highest active layer first, and on each layer its key light before
    // its layer light. An unused slot is ZMK_KEYMAP_LAYER_ID_INVAL, past the tables' end.
    for (int index = ZMK_KEYMAP_LAYERS_LEN - 1; index >= 0; index--) {
        const zmk_keymap_layer_id_t id = zmk_keymap_layer_index_to_id(index);
        // ZMK's key walk stops at Default too. A layer below it, where ZMK Studio's layer order
        // can move one, never decides a key, so it never lights one either.
        if (id == base) {
            break;
        }
        if (id >= ZMK_KEYMAP_LAYERS_LEN || !zmk_keymap_layer_active(id)) {
            continue;
        }
        if (key_lights[id][position].kind != MP_LIGHT_INHERIT) {
            return key_lights[id][position];
        }
        if (layer_lights[id].kind != MP_LIGHT_INHERIT) {
            return layer_lights[id];
        }
    }

    // Default is always active and has key lights but no layer light: its own is the
    // underglow's.
    return base < ZMK_KEYMAP_LAYERS_LEN ? key_lights[base][position] : inherit;
}

struct mp_light mp_lights_resolve(uint8_t position) {
    if (position >= KEY_COUNT) {
        return inherit;
    }

    k_spinlock_key_t key = k_spin_lock(&lights_lock);
    const struct mp_light light = resolve(position);
    k_spin_unlock(&lights_lock, key);

    return light;
}

/*
 * Showing the pad's own lighting
 */

void mp_lights_show_pad(uint32_t ms) {
    // Read before the lock, to keep what is held short. 0 puts the deadline in the past, which
    // ends a show at once.
    const int64_t until = ms == 0 ? 0 : k_uptime_get() + ms;

    // A call never shortens a show: the 3 seconds after a key must not cut short the ten that
    // Studio's popover asked for. Only 0 ends one early.
    k_spinlock_key_t key = k_spin_lock(&lights_lock);
    if (ms == 0 || until > show_until) {
        show_until = until;
    }
    k_spin_unlock(&lights_lock, key);
}

bool mp_lights_showing_pad(void) {
    // Asked by the underglow's work queue for every frame, 20 times a second.
    const int64_t now = k_uptime_get();

    k_spinlock_key_t key = k_spin_lock(&lights_lock);
    const bool showing = now < show_until;
    k_spin_unlock(&lights_lock, key);

    return showing;
}

/*
 * Saving
 */

#if IS_ENABLED(CONFIG_SETTINGS)

/*
 * Whether the saved lights have loaded, under lights_lock. Until then a change
 * is only marked dirty, and nothing is saved or scheduled: see
 * lights_settings_commit().
 */
static bool lights_loaded;

static void save_one(const char *prefix, uint8_t layer_id, const void *value, size_t len,
                     bool inherit_only) {
    char name[sizeof(KEY_SETTINGS_PREFIX "/255")];
    snprintf(name, sizeof(name), "%s/%d", prefix, layer_id);

    // A layer with nothing but Inherit has no entry.
    const int err = inherit_only ? settings_delete(name) : settings_save_one(name, value, len);
    if (err) {
        LOG_ERR("Could not save %s (%d)", name, err);
    }
}

static void save_layer(uint8_t layer_id, bool layer_light, bool key_light) {
    struct mp_light layer;
    struct mp_light keys[KEY_COUNT];

    k_spinlock_key_t key = k_spin_lock(&lights_lock);
    layer = layer_lights[layer_id];
    memcpy(keys, key_lights[layer_id], sizeof(keys));
    k_spin_unlock(&lights_lock, key);

    if (layer_light) {
        save_one(LAYER_SETTINGS_PREFIX, layer_id, &layer, sizeof(layer),
                 layer.kind == MP_LIGHT_INHERIT);
    }

    if (key_light) {
        bool inherit_only = true;
        for (int position = 0; position < KEY_COUNT; position++) {
            inherit_only &= keys[position].kind == MP_LIGHT_INHERIT;
        }
        save_one(KEY_SETTINGS_PREFIX, layer_id, keys, sizeof(keys), inherit_only);
    }
}

static void lights_save(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(lights_save_work, lights_save);

static void lights_save(struct k_work *work) {
    uint32_t layers = 0;
    uint32_t keys = 0;

    k_spinlock_key_t key = k_spin_lock(&lights_lock);
    const bool loaded = lights_loaded;
    if (loaded) {
        layers = dirty_layer_lights;
        keys = dirty_key_lights;
        dirty_layer_lights = 0;
        dirty_key_lights = 0;
    }
    k_spin_unlock(&lights_lock, key);

    // Only scheduled once the lights have loaded (schedule_save() and the commit). Were it ever
    // to run sooner, it leaves the marks for the commit and makes no settings call.
    if (!loaded) {
        return;
    }

    for (uint8_t id = 0; id < ZMK_KEYMAP_LAYERS_LEN; id++) {
        if ((layers | keys) & BIT(id)) {
            save_layer(id, layers & BIT(id), keys & BIT(id));
        }
    }
}

/*
 * A change was marked dirty: save it once changes have stopped for
 * SAVE_DELAY_MS. Until the saved lights have loaded the mark stays and nothing
 * is scheduled: the commit schedules the save. A handler marks before it calls
 * this, and the commit sets lights_loaded and reads the marks under one lock,
 * so one of the two always schedules it.
 */
static void schedule_save(void) {
    k_spinlock_key_t key = k_spin_lock(&lights_lock);
    const bool loaded = lights_loaded;
    k_spin_unlock(&lights_lock, key);

    if (loaded) {
        k_work_reschedule(&lights_save_work, K_MSEC(SAVE_DELAY_MS));
    }
}

#else

static void schedule_save(void) {}

#endif /* IS_ENABLED(CONFIG_SETTINGS) */

/*
 * Answers
 */

static void send_layer_light_state(const struct mp_host_transport *to, uint8_t layer_id) {
    struct mp_layer_light_state state = {.layer_id = layer_id};

    k_spinlock_key_t key = k_spin_lock(&lights_lock);
    state.light = light_to_wire(layer_lights[layer_id]);
    k_spin_unlock(&lights_lock, key);

    mp_host_reply(to, MP_EVT_LAYER_LIGHT_STATE, &state, sizeof(state));
}

/* One page of a layer's key lights. `first` is 0, 3, 6, 9, 12 or 15. */
static void send_key_lights_state(const struct mp_host_transport *to, uint8_t layer_id,
                                  uint8_t first) {
    struct mp_key_lights_state state = {
        .layer_id = layer_id,
        .first = first,
        .count = MIN(PAGE_LEN, KEY_COUNT - first),
    };

    k_spinlock_key_t key = k_spin_lock(&lights_lock);
    for (int i = 0; i < state.count; i++) {
        state.lights[i] = light_to_wire(key_lights[layer_id][first + i]);
    }
    k_spin_unlock(&lights_lock, key);

    // 3 + 4 * count bytes: a host reads count lights.
    mp_host_reply(to, MP_EVT_KEY_LIGHTS_STATE, &state,
                  offsetof(struct mp_key_lights_state, lights) +
                      state.count * sizeof(struct mp_light));
}

/*
 * Commands
 */

void mp_host_handle_get_features(const struct mp_host_transport *from, const uint8_t *payload) {
    ARG_UNUSED(payload);

    // One byte holds every feature this firmware has.
    const uint8_t features = BIT(MP_FEATURE_LAYER_LIGHTS) | BIT(MP_FEATURE_KEY_LIGHTS);
    mp_host_reply(from, MP_EVT_FEATURES, &features, sizeof(features));
}

void mp_host_handle_set_layer_light(const struct mp_host_transport *from,
                                    const uint8_t *payload) {
    struct mp_set_layer_light cmd;
    memcpy(&cmd, payload, sizeof(cmd));

    const struct mp_light light = light_from_wire(cmd.light);
    const bool clearing = light.kind == MP_LIGHT_INHERIT;

    // Clearing is allowed for a slot no layer holds, as for SET_DIAL, so a host can clear a
    // layer's light after removing the layer and before the slot is taken again.
    if (cmd.layer_id >= ZMK_KEYMAP_LAYERS_LEN ||
        (!clearing && !mp_host_layer_in_keymap(cmd.layer_id))) {
        LOG_WRN("Rejecting SET_LAYER_LIGHT for layer id %d, which the keymap does not hold",
                cmd.layer_id);
        mp_host_reject(from, MP_CMD_SET_LAYER_LIGHT, MP_REJECTED_UNKNOWN_LAYER);
        return;
    }

    // Default has no layer light: the underglow is Default's.
    if (!light_valid(light) || (!clearing && cmd.layer_id == zmk_keymap_layer_default())) {
        LOG_WRN("Rejecting SET_LAYER_LIGHT for layer %d with %d/%d/%d", cmd.layer_id, light.kind,
                light.hue, light.saturation);
        mp_host_reject(from, MP_CMD_SET_LAYER_LIGHT, MP_REJECTED_OUT_OF_RANGE);
        return;
    }

    k_spinlock_key_t key = k_spin_lock(&lights_lock);
    const bool was_changed = !lights_equal(layer_lights[cmd.layer_id], light);
    if (was_changed) {
        layer_lights[cmd.layer_id] = light;
        dirty_layer_lights |= BIT(cmd.layer_id);
    }
    k_spin_unlock(&lights_lock, key);

    if (was_changed) {
        schedule_save();
    }

    send_layer_light_state(from, cmd.layer_id);
}

void mp_host_handle_get_layer_light(const struct mp_host_transport *from,
                                    const uint8_t *payload) {
    struct mp_get_layer_light cmd;
    memcpy(&cmd, payload, sizeof(cmd));

    // A reserved slot is answered too, as Inherit, as GET_DIAL answers one.
    if (cmd.layer_id >= ZMK_KEYMAP_LAYERS_LEN) {
        mp_host_reject(from, MP_CMD_GET_LAYER_LIGHT, MP_REJECTED_UNKNOWN_LAYER);
        return;
    }

    send_layer_light_state(from, cmd.layer_id);
}

void mp_host_handle_show_pad_lighting(const struct mp_host_transport *from,
                                      const uint8_t *payload) {
    ARG_UNUSED(from);

    struct mp_show_pad_lighting cmd;
    memcpy(&cmd, payload, sizeof(cmd));

    // Counted from now, so sending it again renews the show, and 0 ends it. No answer: the
    // lights are the answer.
    mp_lights_show_pad((uint32_t)cmd.seconds * MSEC_PER_SEC);
}

void mp_host_handle_set_key_light(const struct mp_host_transport *from, const uint8_t *payload) {
    struct mp_set_key_light cmd;
    memcpy(&cmd, payload, sizeof(cmd));

    const uint16_t keys = sys_le16_to_cpu(cmd.keys);
    const struct mp_light light = light_from_wire(cmd.light);
    const bool clearing = light.kind == MP_LIGHT_INHERIT;

    // As SET_LAYER_LIGHT, with Default allowed.
    if (cmd.layer_id >= ZMK_KEYMAP_LAYERS_LEN ||
        (!clearing && !mp_host_layer_in_keymap(cmd.layer_id))) {
        LOG_WRN("Rejecting SET_KEY_LIGHT for layer id %d, which the keymap does not hold",
                cmd.layer_id);
        mp_host_reject(from, MP_CMD_SET_KEY_LIGHT, MP_REJECTED_UNKNOWN_LAYER);
        return;
    }

    if (keys == 0 || !light_valid(light)) {
        LOG_WRN("Rejecting SET_KEY_LIGHT for layer %d, keys 0x%04x, with %d/%d/%d",
                cmd.layer_id, keys, light.kind, light.hue, light.saturation);
        mp_host_reject(from, MP_CMD_SET_KEY_LIGHT, MP_REJECTED_OUT_OF_RANGE);
        return;
    }

    bool was_changed = false;

    k_spinlock_key_t key = k_spin_lock(&lights_lock);
    for (int position = 0; position < KEY_COUNT; position++) {
        if ((keys & BIT(position)) && !lights_equal(key_lights[cmd.layer_id][position], light)) {
            key_lights[cmd.layer_id][position] = light;
            was_changed = true;
        }
    }
    if (was_changed) {
        dirty_key_lights |= BIT(cmd.layer_id);
    }
    k_spin_unlock(&lights_lock, key);

    if (was_changed) {
        schedule_save();
    }

    // Every page that holds a key in keys, in page order, to the sender only.
    for (uint8_t first = 0; first < KEY_COUNT; first += PAGE_LEN) {
        const uint16_t page = BIT_MASK(MIN(PAGE_LEN, KEY_COUNT - first)) << first;
        if (keys & page) {
            send_key_lights_state(from, cmd.layer_id, first);
        }
    }
}

void mp_host_handle_get_key_lights(const struct mp_host_transport *from,
                                   const uint8_t *payload) {
    struct mp_get_key_lights cmd;
    memcpy(&cmd, payload, sizeof(cmd));

    // A reserved slot is answered too, as all Inherit.
    if (cmd.layer_id >= ZMK_KEYMAP_LAYERS_LEN) {
        mp_host_reject(from, MP_CMD_GET_KEY_LIGHTS, MP_REJECTED_UNKNOWN_LAYER);
        return;
    }

    if (cmd.first >= KEY_COUNT || cmd.first % PAGE_LEN != 0) {
        mp_host_reject(from, MP_CMD_GET_KEY_LIGHTS, MP_REJECTED_OUT_OF_RANGE);
        return;
    }

    send_key_lights_state(from, cmd.layer_id, cmd.first);
}

/*
 * Loading
 */

#if IS_ENABLED(CONFIG_SETTINGS)

/* The layer id a setting is saved under, or -EINVAL. */
static long layer_id_from_name(const char *name) {
    char *end;

    if (name == NULL) {
        return -EINVAL;
    }

    const long layer_id = strtol(name, &end, 10);
    if (end == name || *end != '\0' || layer_id < 0 || layer_id >= ZMK_KEYMAP_LAYERS_LEN) {
        return -EINVAL;
    }

    return layer_id;
}

static int layer_light_settings_set(const char *name, size_t len, settings_read_cb read_cb,
                                    void *cb_arg) {
    const long layer_id = layer_id_from_name(name);
    if (layer_id < 0 || len != sizeof(struct mp_light)) {
        return -EINVAL;
    }

    struct mp_light light;
    const int err = read_cb(cb_arg, &light, sizeof(light));
    if (err <= 0) {
        return err < 0 ? err : -EINVAL;
    }

    if (!light_valid(light) ||
        (light.kind != MP_LIGHT_INHERIT && layer_id == zmk_keymap_layer_default())) {
        LOG_WRN("Ignoring a saved light for layer %ld with %d/%d/%d", layer_id, light.kind,
                light.hue, light.saturation);
        return 0;
    }

    k_spinlock_key_t key = k_spin_lock(&lights_lock);
    layer_lights[layer_id] = light;
    k_spin_unlock(&lights_lock, key);
    return 0;
}

static int key_lights_settings_set(const char *name, size_t len, settings_read_cb read_cb,
                                   void *cb_arg) {
    const long layer_id = layer_id_from_name(name);
    if (layer_id < 0 || len != sizeof(key_lights[0])) {
        return -EINVAL;
    }

    struct mp_light lights[KEY_COUNT];
    const int err = read_cb(cb_arg, lights, sizeof(lights));
    if (err <= 0) {
        return err < 0 ? err : -EINVAL;
    }

    for (int position = 0; position < KEY_COUNT; position++) {
        if (!light_valid(lights[position])) {
            LOG_WRN("Ignoring the saved key lights for layer %ld, with %d/%d/%d at key %d",
                    layer_id, lights[position].kind, lights[position].hue,
                    lights[position].saturation, position);
            return 0;
        }
    }

    k_spinlock_key_t key = k_spin_lock(&lights_lock);
    memcpy(key_lights[layer_id], lights, sizeof(lights));
    k_spin_unlock(&lights_lock, key);
    return 0;
}

/*
 * Commit comes after every subtree has loaded, ours included, and from here on
 * a save cannot be overwritten by the load. No settings call is made here, nor
 * may the save run before this: this runs on the thread loading the settings,
 * which holds their lock until every commit has run, ZMK's Bluetooth start-up
 * among them (its commit is a dynamic handler, so it runs after this static
 * one), and Bluetooth sends its commands from the system work queue. A save
 * there waiting for the lock would hang the pad at start, as lighting.c's
 * `started` says.
 *
 * So a change from before the load was only marked, and the save for it is
 * scheduled here, SAVE_DELAY_MS from now. Starting that timer is not a settings
 * call. The first save then comes at least SAVE_DELAY_MS after this, long after
 * the remaining commits have run and the lock has been let go.
 */
static int lights_settings_commit(void) {
    k_spinlock_key_t key = k_spin_lock(&lights_lock);
    lights_loaded = true;
    const bool dirty = (dirty_layer_lights | dirty_key_lights) != 0;
    k_spin_unlock(&lights_lock, key);

    if (dirty) {
        k_work_reschedule(&lights_save_work, K_MSEC(SAVE_DELAY_MS));
    }
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(mp_light, LAYER_SETTINGS_PREFIX, NULL, layer_light_settings_set,
                               lights_settings_commit, NULL);
SETTINGS_STATIC_HANDLER_DEFINE(mp_klight, KEY_SETTINGS_PREFIX, NULL, key_lights_settings_set, NULL,
                               NULL);

#endif /* IS_ENABLED(CONFIG_SETTINGS) */
