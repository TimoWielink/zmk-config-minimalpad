/*
 * Minimalpad host module: &host_dial, the dial Studio can change per profile
 *
 * Bound as a sensor binding on Default. Profile layers carry no dial binding
 * of their own, so ZMK looks down to Default's while a profile is active, and
 * BT + LED keeps its own dial because it defines one.
 *
 * While the last SET_PROFILE carried dial values (mp_host_dial_keycodes()), a
 * turn does what they say: tap a key, or scroll, with the modifiers they name
 * (host-protocol.md, "Dial values"). Otherwise, and whenever the pad falls back
 * to Default, it does what its own bindings say, as ZMK's sensor-rotate would.
 *
 * Threading: ZMK raises sensor events on the system work queue, the queue the
 * host core (host.c) runs on, so the dial values cannot change under a turn.
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT minimalpad_behavior_host_dial

#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>

#include <dt-bindings/zmk/hid_usage_pages.h>
#include <zmk/behavior.h>
#include <zmk/behavior_queue.h>
#include <zmk/endpoints.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>
#include <zmk/sensors.h>
#include <zmk/virtual_key_position.h>

#include <minimalpad/host.h>

LOG_MODULE_DECLARE(minimalpad_host, CONFIG_MINIMALPAD_HOST_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct host_dial_config {
    struct zmk_behavior_binding cw_binding;
    struct zmk_behavior_binding ccw_binding;
    int tap_ms;
};

/* The same bookkeeping as ZMK's sensor-rotate: the part of a turn too small to
 * be a step yet, and the steps the last reading made, per sensor and layer. */
struct host_dial_data {
    struct sensor_value remainder[ZMK_KEYMAP_SENSORS_LEN][ZMK_KEYMAP_LAYERS_LEN];
    int triggers[ZMK_KEYMAP_SENSORS_LEN][ZMK_KEYMAP_LAYERS_LEN];
};

static int host_dial_accept_data(struct zmk_behavior_binding *binding,
                                 struct zmk_behavior_binding_event event,
                                 const struct zmk_sensor_config *sensor_config,
                                 size_t channel_data_size,
                                 const struct zmk_sensor_channel_data *channel_data) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct host_dial_data *data = dev->data;
    const int sensor_index = ZMK_SENSOR_POSITION_FROM_VIRTUAL_KEY_POSITION(event.position);
    const struct sensor_value value = channel_data[0].value;
    int triggers;

    // As behavior_sensor_rotate_common.c: an EC11 reports degrees in val1, and older sensor
    // drivers report steps in val2 alone.
    if (value.val1 == 0) {
        triggers = value.val2;
    } else {
        struct sensor_value remainder = data->remainder[sensor_index][event.layer];

        remainder.val1 += value.val1;
        remainder.val2 += value.val2;

        if (abs(remainder.val2) >= 1000000) {
            remainder.val1 += remainder.val2 / 1000000;
            remainder.val2 %= 1000000;
        }

        const int trigger_degrees = 360 / sensor_config->triggers_per_rotation;
        triggers = remainder.val1 / trigger_degrees;
        remainder.val1 %= trigger_degrees;

        data->remainder[sensor_index][event.layer] = remainder;
    }

    data->triggers[sensor_index][event.layer] = triggers;
    return 0;
}

/* Scrolls `steps` wheel steps with `modifiers` held, all at once: one report
 * for the modifiers, one for the wheel, one to let the modifiers go. */
static void scroll(uint32_t value, int steps) {
#if IS_ENABLED(CONFIG_ZMK_POINTING)
    const int16_t amount = (int16_t)(steps * CONFIG_MINIMALPAD_HOST_DIAL_SCROLL_STEP);
    int16_t x = 0;
    int16_t y = 0;

    switch (MP_DIAL_USAGE(value)) {
    case MP_DIAL_SCROLL_UP:
        y = amount;
        break;
    case MP_DIAL_SCROLL_DOWN:
        y = -amount;
        break;
    case MP_DIAL_SCROLL_LEFT:
        x = -amount;
        break;
    case MP_DIAL_SCROLL_RIGHT:
        x = amount;
        break;
    default:
        return;
    }

    const zmk_mod_flags_t modifiers = MP_DIAL_MODIFIERS(value);
    if (modifiers) {
        zmk_hid_register_mods(modifiers);
        zmk_endpoint_send_report(HID_USAGE_KEY);
    }

    zmk_hid_mouse_scroll_set(x, y);
    zmk_endpoint_send_mouse_report();
    // A wheel report is a change, not a state: clear it so the next mouse report does not
    // scroll again.
    zmk_hid_mouse_scroll_set(0, 0);

    if (modifiers) {
        zmk_hid_unregister_mods(modifiers);
        zmk_endpoint_send_report(HID_USAGE_KEY);
    }
#else
    ARG_UNUSED(value);
    ARG_UNUSED(steps);
    LOG_WRN("A scroll on the dial needs CONFIG_ZMK_POINTING");
#endif
}

/* Taps a keycode `steps` times through ZMK's behavior queue, as sensor-rotate
 * does, so the taps are spaced and ordered like any other dial turn. */
static void tap(struct zmk_behavior_binding_event *event, struct zmk_behavior_binding binding,
                int steps, int tap_ms) {
    for (int i = 0; i < steps; i++) {
        zmk_behavior_queue_add(event, binding, true, tap_ms);
        zmk_behavior_queue_add(event, binding, false, 0);
    }
}

static int host_dial_process(struct zmk_behavior_binding *binding,
                             struct zmk_behavior_binding_event event,
                             enum behavior_sensor_binding_process_mode mode) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct host_dial_config *cfg = dev->config;
    struct host_dial_data *data = dev->data;
    const int sensor_index = ZMK_SENSOR_POSITION_FROM_VIRTUAL_KEY_POSITION(event.position);

    if (mode != BEHAVIOR_SENSOR_BINDING_PROCESS_MODE_TRIGGER) {
        data->triggers[sensor_index][event.layer] = 0;
        return ZMK_BEHAVIOR_TRANSPARENT;
    }

    int steps = data->triggers[sensor_index][event.layer];
    if (steps == 0) {
        return ZMK_BEHAVIOR_TRANSPARENT;
    }

    const bool clockwise = steps > 0;
    steps = abs(steps);

    uint32_t host_cw;
    uint32_t host_ccw;
    if (!mp_host_dial_keycodes(&host_cw, &host_ccw)) {
        tap(&event, clockwise ? cfg->cw_binding : cfg->ccw_binding, steps, cfg->tap_ms);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    const uint32_t value = clockwise ? host_cw : host_ccw;
    LOG_DBG("dial %s %d steps: 0x%08x", clockwise ? "cw" : "ccw", steps, value);

    switch (MP_DIAL_PAGE(value)) {
    case MP_DIAL_PAGE_SCROLL:
        scroll(value, steps);
        break;
    case MP_DIAL_PAGE_KEYBOARD:
    case MP_DIAL_PAGE_CONSUMER: {
        const struct zmk_behavior_binding key = {
            .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(kp)),
            .param1 = value,
        };
        tap(&event, key, steps, cfg->tap_ms);
        break;
    }
    default:
        // 0, the dial doing nothing this way. host.c rejects anything else.
        break;
    }

    // Opaque even when doing nothing: the profile set the dial, so no layer below answers.
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api host_dial_driver_api = {
    .sensor_binding_accept_data = host_dial_accept_data,
    .sensor_binding_process = host_dial_process,
};

#define _TRANSFORM_ENTRY(idx, node)                                                                \
    {                                                                                              \
        .behavior_dev = DEVICE_DT_NAME(DT_INST_PHANDLE_BY_IDX(node, bindings, idx)),               \
        .param1 = COND_CODE_0(DT_INST_PHA_HAS_CELL_AT_IDX(node, bindings, idx, param1), (0),       \
                              (DT_INST_PHA_BY_IDX(node, bindings, idx, param1))),                  \
        .param2 = COND_CODE_0(DT_INST_PHA_HAS_CELL_AT_IDX(node, bindings, idx, param2), (0),       \
                              (DT_INST_PHA_BY_IDX(node, bindings, idx, param2))),                  \
    }

#define HOST_DIAL_INST(n)                                                                          \
    static const struct host_dial_config host_dial_config_##n = {                                  \
        .cw_binding = _TRANSFORM_ENTRY(0, n),                                                      \
        .ccw_binding = _TRANSFORM_ENTRY(1, n),                                                     \
        .tap_ms = DT_INST_PROP(n, tap_ms),                                                         \
    };                                                                                             \
    static struct host_dial_data host_dial_data_##n = {};                                          \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, &host_dial_data_##n, &host_dial_config_##n,             \
                            POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                      \
                            &host_dial_driver_api);

DT_INST_FOREACH_STATUS_OKAY(HOST_DIAL_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
