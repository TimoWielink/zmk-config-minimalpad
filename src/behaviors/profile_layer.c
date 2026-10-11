/*
 * Minimalpad host module: the &profile_layer key
 *
 * Moves between the layers of the profile MinimalPad Studio holds the pad on,
 * its group (profile_layers.c, host-protocol.md "Profile layers"), with no
 * round trip to the Mac:
 *
 *   &profile_layer PL_NEXT 0       the next layer in the group, wrapping round
 *   &profile_layer PL_GO layer     that layer, if it is in the group
 *   &profile_layer PL_HOLD layer   that layer, or Default, until the key is released
 *
 * Without a group, which is without a host holding a profile, the key does
 * nothing, and the press goes no further.
 *
 * Threading: ZMK runs behaviors on the system work queue, the queue the host
 * core runs on, so hold_tokens needs no lock of its own.
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT minimalpad_behavior_profile_layer

#include <errno.h>

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>

#include <dt-bindings/minimalpad/profile_layer.h>
#include <zmk/behavior.h>
#include <zmk/matrix.h>

#include <minimalpad/host.h>

LOG_MODULE_DECLARE(minimalpad_host, CONFIG_MINIMALPAD_HOST_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

/*
 * What lets ZMK Studio's set_layer_binding accept the binding: ZMK checks the
 * parameters against these sets (zmk_behavior_check_params_match_metadata()),
 * and a binding fits if any one set fits both parameters. Two sets, as &bt has
 * them. Next takes no second parameter, which a set with no values for it
 * allows as 0. Go to and Hold for take a layer id.
 *
 * Studio for Mac reads the sets, so PL_* in profile_layer.h and the names here
 * are what it shows.
 */
static const struct behavior_parameter_value_metadata next_param1_values[] = {
    {
        .display_name = "Next layer",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE,
        .value = PL_NEXT,
    },
};

static const struct behavior_parameter_metadata_set next_set = {
    .param1_values = next_param1_values,
    .param1_values_len = ARRAY_SIZE(next_param1_values),
};

static const struct behavior_parameter_value_metadata layer_param1_values[] = {
    {
        .display_name = "Go to",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE,
        .value = PL_GO,
    },
    {
        .display_name = "Hold for",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE,
        .value = PL_HOLD,
    },
};

static const struct behavior_parameter_value_metadata layer_param2_values[] = {
    {
        .display_name = "Layer",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_LAYER_ID,
    },
};

static const struct behavior_parameter_metadata_set layer_set = {
    .param1_values = layer_param1_values,
    .param1_values_len = ARRAY_SIZE(layer_param1_values),
    .param2_values = layer_param2_values,
    .param2_values_len = ARRAY_SIZE(layer_param2_values),
};

static const struct behavior_parameter_metadata_set metadata_sets[] = {next_set, layer_set};

static const struct behavior_parameter_metadata metadata = {
    .sets_len = ARRAY_SIZE(metadata_sets),
    .sets = metadata_sets,
};

#endif /* IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA) */

/*
 * The hold each key position started, 0 for none, for its release to end
 * (mp_profile_layer_hold() hands the token out). A position past the keys is a
 * virtual one, such as a combo's: it has no slot, so it cannot hold.
 */
static uint32_t hold_tokens[ZMK_KEYMAP_LEN];

static int profile_layer_pressed(struct zmk_behavior_binding *binding,
                                 struct zmk_behavior_binding_event event) {
    int ret;

    switch (binding->param1) {
    case PL_NEXT:
        LOG_DBG("position %d next layer", event.position);
        ret = mp_profile_layer_next();
        break;
    case PL_GO:
        LOG_DBG("position %d go to layer %d", event.position, binding->param2);

        if (binding->param2 > UINT8_MAX) {
            LOG_ERR("Layer id %d is no layer", binding->param2);
            return ZMK_BEHAVIOR_OPAQUE;
        }

        ret = mp_profile_layer_go(binding->param2);
        break;
    case PL_HOLD:
        LOG_DBG("position %d hold for layer %d", event.position, binding->param2);

        if (binding->param2 > UINT8_MAX) {
            LOG_ERR("Layer id %d is no layer", binding->param2);
            return ZMK_BEHAVIOR_OPAQUE;
        }

        if (event.position >= ZMK_KEYMAP_LEN) {
            LOG_WRN("Position %d has nowhere to keep a hold, so it cannot hold a layer",
                    event.position);
            return ZMK_BEHAVIOR_OPAQUE;
        }

        ret = mp_profile_layer_hold(binding->param2, &hold_tokens[event.position]);
        break;
    default:
        LOG_ERR("Unknown profile layer mode %d", binding->param1);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    if (ret < 0) {
        // No group is held, or the layer is not in it: the key does nothing.
        LOG_DBG("Profile layer key did nothing (%d)", ret);
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int profile_layer_released(struct zmk_behavior_binding *binding,
                                  struct zmk_behavior_binding_event event) {
    if (binding->param1 == PL_HOLD && event.position < ZMK_KEYMAP_LEN) {
        LOG_DBG("position %d hold for layer %d ends", event.position, binding->param2);

        mp_profile_layer_release(hold_tokens[event.position]);
        hold_tokens[event.position] = 0;
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api profile_layer_driver_api = {
    .binding_pressed = profile_layer_pressed,
    .binding_released = profile_layer_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#endif
};

#define PROFILE_LAYER_INST(n)                                                                      \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                                \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &profile_layer_driver_api);

DT_INST_FOREACH_STATUS_OKAY(PROFILE_LAYER_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
