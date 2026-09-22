/*
 * Minimalpad host module: the &mac_action key
 *
 * A key bound to `&mac_action N` does nothing on the pad. On the way down and
 * on the way up it sends ACTION_EVENT to every host listening, and MinimalPad
 * Studio does whatever it saved under action id N, such as opening an app. The
 * pad cannot do that work itself, so without Studio running the key is inert.
 *
 * Threading: ZMK runs behaviors on the system work queue, the queue the host
 * core (host.c) runs on, so calling mp_host_broadcast() directly is right.
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT minimalpad_behavior_mac_action

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>

#include <minimalpad/host.h>

LOG_MODULE_DECLARE(minimalpad_host, CONFIG_MINIMALPAD_HOST_LOG_LEVEL);

BUILD_ASSERT(sizeof(struct mp_action_event) == 2, "ACTION_EVENT carries 2 payload bytes");

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

/* One parameter, the action id, 0 to 255: what lets ZMK Studio's
 * set_layer_binding accept the binding, since it checks the parameters against
 * this. */
static const struct behavior_parameter_value_metadata param_values[] = {
    {
        .display_name = "Action",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_RANGE,
        .range = {.min = 0, .max = 255},
    },
};

static const struct behavior_parameter_metadata_set param_metadata_set[] = {{
    .param1_values = param_values,
    .param1_values_len = ARRAY_SIZE(param_values),
}};

static const struct behavior_parameter_metadata metadata = {
    .sets_len = ARRAY_SIZE(param_metadata_set),
    .sets = param_metadata_set,
};

#endif

static void send_action(uint32_t action_id, bool pressed) {
    const struct mp_action_event event = {
        .action_id = (uint8_t)action_id,
        .pressed = pressed ? 1 : 0,
    };

    mp_host_broadcast(MP_EVT_ACTION, &event, sizeof(event));
}

static int mac_action_binding_pressed(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    LOG_DBG("position %d action %d down", event.position, binding->param1);
    send_action(binding->param1, true);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int mac_action_binding_released(struct zmk_behavior_binding *binding,
                                       struct zmk_behavior_binding_event event) {
    LOG_DBG("position %d action %d up", event.position, binding->param1);
    send_action(binding->param1, false);
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api mac_action_driver_api = {
    .binding_pressed = mac_action_binding_pressed,
    .binding_released = mac_action_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#endif
};

#define MAC_ACTION_INST(n)                                                                         \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                                \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &mac_action_driver_api);

DT_INST_FOREACH_STATUS_OKAY(MAC_ACTION_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
