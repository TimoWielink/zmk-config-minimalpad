/*
 * Minimalpad host module: a dial for each layer, kept on the pad
 *
 * Every layer can have its own dial action, set from MinimalPad Studio with
 * SET_DIAL and read back with GET_DIAL (host-protocol.md, "Dials"). A value
 * set here is saved in the pad's settings, so it lasts through restarts and
 * works without Studio, the way a key Studio changed does.
 *
 * A layer without a value set does what its keymap says: a layer bound to
 * &host_dial has the clockwise and counter-clockwise values written there,
 * and a layer with no sensor binding, such as a profile layer, passes the turn
 * to the layer below. src/behaviors/host_dial.c asks mp_host_dial_resolve()
 * which value a turn gets.
 *
 * Threading: commands and sensor events both run on the system work queue.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/byteorder.h>

#include <dt-bindings/minimalpad/dial.h>
#include <zmk/keymap.h>

#include <minimalpad/host.h>

LOG_MODULE_DECLARE(minimalpad_host, CONFIG_MINIMALPAD_HOST_LOG_LEVEL);

BUILD_ASSERT(sizeof(struct mp_set_dial) == 10, "SET_DIAL carries 10 payload bytes");
BUILD_ASSERT(sizeof(struct mp_get_dial) == 1, "GET_DIAL carries 1 payload byte");
BUILD_ASSERT(sizeof(struct mp_dial_state) == 11, "DIAL_STATE carries 11 payload bytes");
BUILD_ASSERT(DIAL_SCROLL_RIGHT == ((MP_DIAL_PAGE_SCROLL << 16) | MP_DIAL_SCROLL_RIGHT),
             "dt-bindings/minimalpad/dial.h matches the protocol");
BUILD_ASSERT(DIAL_LIGHTS_SLOWER == ((MP_DIAL_PAGE_LIGHTS << 16) | MP_DIAL_LIGHTS_SLOWER),
             "dt-bindings/minimalpad/dial.h matches the protocol");

#define SETTINGS_PREFIX "mp_dial"

/*
 * What each layer's keymap says, worked out at build time from the keymap the
 * way ZMK builds its own sensor table, so the index is the layer id.
 */

struct keymap_dial {
    uint8_t source; /* MP_DIAL_SOURCE_KEYMAP, _BELOW or _FIXED */
    uint32_t cw;
    uint32_t ccw;
};

#define SENSOR_BEHAVIOR(layer) DT_PHANDLE_BY_IDX(layer, sensor_bindings, 0)

#define KEYMAP_DIAL(layer)                                                                         \
    COND_CODE_1(                                                                                   \
        DT_NODE_HAS_PROP(layer, sensor_bindings),                                                  \
        (COND_CODE_1(DT_NODE_HAS_COMPAT(SENSOR_BEHAVIOR(layer), minimalpad_behavior_host_dial),   \
                     ({                                                                            \
                         .source = MP_DIAL_SOURCE_KEYMAP,                                          \
                         .cw = DT_PROP(SENSOR_BEHAVIOR(layer), clockwise),                         \
                         .ccw = DT_PROP(SENSOR_BEHAVIOR(layer), counter_clockwise),                \
                     }),                                                                           \
                     ({.source = MP_DIAL_SOURCE_FIXED}))),                                         \
        ({.source = MP_DIAL_SOURCE_BELOW}))

static const struct keymap_dial keymap_dials[] = {ZMK_KEYMAP_LAYERS_FOREACH_SEP(KEYMAP_DIAL, (, ))};

BUILD_ASSERT(ARRAY_SIZE(keymap_dials) == ZMK_KEYMAP_LAYERS_LEN, "one keymap dial per layer");

/*
 * Values set with SET_DIAL, by layer id.
 */

struct set_dial {
    uint32_t cw;
    uint32_t ccw;
};

static struct set_dial set_dials[ZMK_KEYMAP_LAYERS_LEN];
static bool is_set[ZMK_KEYMAP_LAYERS_LEN];

/* host-protocol.md, "Dial values": a key, a scroll, a lighting step, or nothing. */
static bool dial_value_valid(uint32_t value) {
    if (value == 0) {
        return true;
    }

    switch (MP_DIAL_PAGE(value)) {
    case MP_DIAL_PAGE_KEYBOARD:
    case MP_DIAL_PAGE_CONSUMER:
        return true;
    case MP_DIAL_PAGE_SCROLL:
        return MP_DIAL_USAGE(value) >= MP_DIAL_SCROLL_UP &&
               MP_DIAL_USAGE(value) <= MP_DIAL_SCROLL_RIGHT;
    case MP_DIAL_PAGE_LIGHTS:
        return MP_DIAL_USAGE(value) >= MP_DIAL_LIGHTS_BRIGHTER &&
               MP_DIAL_USAGE(value) <= MP_DIAL_LIGHTS_SLOWER;
    default:
        return false;
    }
}

bool mp_host_dial_resolve(uint8_t answering_layer, bool clockwise, uint32_t *value) {
    if (answering_layer >= ZMK_KEYMAP_LAYERS_LEN) {
        return false;
    }

    // ZMK gave the turn to the highest active layer with a sensor binding. A layer above it
    // with none, such as a profile layer, can still have a dial set, and the highest of those
    // wins, as its keys would.
    for (int index = ZMK_KEYMAP_LAYERS_LEN - 1; index >= 0; index--) {
        const zmk_keymap_layer_id_t id = zmk_keymap_layer_index_to_id(index);
        if (id == answering_layer) {
            break;
        }
        if (id < ZMK_KEYMAP_LAYERS_LEN && is_set[id] && zmk_keymap_layer_active(id)) {
            *value = clockwise ? set_dials[id].cw : set_dials[id].ccw;
            return true;
        }
    }

    if (is_set[answering_layer]) {
        *value = clockwise ? set_dials[answering_layer].cw : set_dials[answering_layer].ccw;
        return true;
    }

    const struct keymap_dial *own = &keymap_dials[answering_layer];
    if (own->source != MP_DIAL_SOURCE_KEYMAP) {
        return false;
    }

    *value = clockwise ? own->cw : own->ccw;
    return true;
}

static void send_dial_state(const struct mp_host_transport *to, uint8_t layer_id) {
    struct mp_dial_state state = {
        .layer_id = layer_id,
        .keymap_source = keymap_dials[layer_id].source,
    };

    if (is_set[layer_id]) {
        state.source = MP_DIAL_SOURCE_SET;
        state.cw = sys_cpu_to_le32(set_dials[layer_id].cw);
        state.ccw = sys_cpu_to_le32(set_dials[layer_id].ccw);
    } else {
        state.source = keymap_dials[layer_id].source;
        state.cw = sys_cpu_to_le32(keymap_dials[layer_id].cw);
        state.ccw = sys_cpu_to_le32(keymap_dials[layer_id].ccw);
    }

    mp_host_send(to, MP_EVT_DIAL_STATE, &state, sizeof(state));
}

static void save(uint8_t layer_id) {
#if IS_ENABLED(CONFIG_SETTINGS)
    char name[sizeof(SETTINGS_PREFIX "/255")];
    snprintf(name, sizeof(name), SETTINGS_PREFIX "/%d", layer_id);

    int err = is_set[layer_id]
                  ? settings_save_one(name, &set_dials[layer_id], sizeof(set_dials[layer_id]))
                  : settings_delete(name);
    if (err) {
        LOG_ERR("Could not save the dial for layer %d (%d)", layer_id, err);
    }
#endif
}

void mp_host_handle_set_dial(const struct mp_host_transport *from, const uint8_t *payload) {
    struct mp_set_dial cmd;
    memcpy(&cmd, payload, sizeof(cmd));

    const uint32_t cw = sys_le32_to_cpu(cmd.cw);
    const uint32_t ccw = sys_le32_to_cpu(cmd.ccw);
    const bool set = cmd.flags & MP_DIAL_FLAG_SET;

    // Clearing is allowed for a slot no layer holds, so a host can clear a layer's dial after
    // removing the layer and before the slot is taken again.
    if (cmd.layer_id >= ZMK_KEYMAP_LAYERS_LEN || (set && !mp_host_layer_in_keymap(cmd.layer_id))) {
        LOG_WRN("Rejecting SET_DIAL for layer id %d, which the keymap does not hold",
                cmd.layer_id);
        mp_host_reject(from, MP_CMD_SET_DIAL, MP_REJECTED_UNKNOWN_LAYER);
        return;
    }

    if (keymap_dials[cmd.layer_id].source == MP_DIAL_SOURCE_FIXED ||
        (set && !(dial_value_valid(cw) && dial_value_valid(ccw)))) {
        LOG_WRN("Rejecting SET_DIAL for layer %d with 0x%08x/0x%08x", cmd.layer_id, cw, ccw);
        mp_host_reject(from, MP_CMD_SET_DIAL, MP_REJECTED_OUT_OF_RANGE);
        return;
    }

    const bool changed = set != is_set[cmd.layer_id] ||
                         (set && (cw != set_dials[cmd.layer_id].cw ||
                                  ccw != set_dials[cmd.layer_id].ccw));
    is_set[cmd.layer_id] = set;
    set_dials[cmd.layer_id] = set ? (struct set_dial){.cw = cw, .ccw = ccw} : (struct set_dial){0};
    if (changed) {
        save(cmd.layer_id);
    }

    send_dial_state(from, cmd.layer_id);
}

void mp_host_handle_get_dial(const struct mp_host_transport *from, const uint8_t *payload) {
    struct mp_get_dial cmd;
    memcpy(&cmd, payload, sizeof(cmd));

    // A reserved slot is answered too, as having no dial of its own: GET_DIAL changes nothing,
    // and a host reading every layer id need not know which slots are taken.
    if (cmd.layer_id >= ZMK_KEYMAP_LAYERS_LEN) {
        mp_host_reject(from, MP_CMD_GET_DIAL, MP_REJECTED_UNKNOWN_LAYER);
        return;
    }

    send_dial_state(from, cmd.layer_id);
}

#if IS_ENABLED(CONFIG_SETTINGS)

static int dial_settings_set(const char *name, size_t len, settings_read_cb read_cb,
                             void *cb_arg) {
    char *end;
    const long layer_id = strtol(name, &end, 10);

    if (end == name || *end != '\0' || layer_id < 0 ||
        layer_id >= ZMK_KEYMAP_LAYERS_LEN || len != sizeof(struct set_dial)) {
        return -EINVAL;
    }

    struct set_dial value;
    const int err = read_cb(cb_arg, &value, sizeof(value));
    if (err <= 0) {
        return err < 0 ? err : -EINVAL;
    }

    if (!(dial_value_valid(value.cw) && dial_value_valid(value.ccw))) {
        LOG_WRN("Ignoring a saved dial for layer %ld with 0x%08x/0x%08x", layer_id, value.cw,
                value.ccw);
        return 0;
    }

    set_dials[layer_id] = value;
    is_set[layer_id] = true;
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(mp_dial, SETTINGS_PREFIX, NULL, dial_settings_set, NULL, NULL);

#endif
