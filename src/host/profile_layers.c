/*
 * Minimalpad host module: the layers of the profile a host holds
 *
 * A profile can have several layers, and the pad moves between them by itself
 * (host-protocol.md, "Profile layers"). That needs no round trip to the Mac, so
 * it works over a flaky Bluetooth link, and a hold keeps its press and release
 * timing. SWITCH_PROFILE tells the pad which layers belong to the profile, its
 * group, and the &profile_layer key moves within it: Next layer, Go to and Hold
 * for.
 *
 * Moving within the group is a swap: the profile layer the pad is on goes off
 * and the target comes on, and every other active layer stays as it is, so a
 * pad layer held or toggled on above the profile survives a swap. An app
 * switch is not a swap: SWITCH_PROFILE goes through zmk_keymap_layer_to(), as
 * SET_PROFILE does (host.c).
 *
 * Nothing here is saved. The group belongs to the host holding the pad, and a
 * pad that restarts is held by no host.
 *
 * Threading: host commands run on the system work queue and &profile_layer in
 * ZMK's event context, which ZMK also runs from the system work queue on this
 * pad. Rather than lean on that, one spinlock guards the group and the hold, so
 * they are always read and changed whole. The keymap is called with it
 * released: a layer change raises an event, and its listeners must not run
 * under a spinlock.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stddef.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>

#include <zmk/keymap.h>

#include <minimalpad/host.h>

LOG_MODULE_DECLARE(minimalpad_host, CONFIG_MINIMALPAD_HOST_LOG_LEVEL);

static struct k_spinlock profile_layers_lock;

/* The held profile's layers, in the order Next goes through them. No group is held while
 * group_len is 0. */
static zmk_keymap_layer_id_t group[MP_SWITCH_PROFILE_GROUP_MAX];
static uint8_t group_len;

/* The current hold, 0 for none, and the layer its release goes back to. */
static uint32_t hold_token;
static zmk_keymap_layer_id_t hold_return;

/* The last token a hold was given. */
static uint32_t last_token;

/* Call with profile_layers_lock held. */
static bool in_group(zmk_keymap_layer_id_t id) {
    for (uint8_t i = 0; i < group_len; i++) {
        if (group[i] == id) {
            return true;
        }
    }

    return false;
}

/*
 * The swap (host-protocol.md, "The group, the swap and holds"). Locking on both
 * sides: a non-locking deactivate leaves a locked layer on, and every profile
 * layer the host set is locked, by zmk_keymap_layer_to() or an earlier swap.
 * Default is never deactivated, since ZMK keeps it on, nor activated, since
 * Default under the profile is what shows when the profile layer goes off.
 *
 * Call without profile_layers_lock: the keymap raises a layer event.
 */
static void swap_to(zmk_keymap_layer_id_t target, enum mp_layer_change change) {
    const zmk_keymap_layer_id_t current = mp_host_layer();
    if (target == current) {
        return;
    }

    const zmk_keymap_layer_id_t default_layer = zmk_keymap_layer_default();

    LOG_DBG("Swapping profile layer %d for %d (change %d)", current, target, change);

    if (current != default_layer) {
        zmk_keymap_layer_deactivate(current, true);
    }

    if (target != default_layer) {
        zmk_keymap_layer_activate(target, true);
    }

    mp_host_set_layer(target, change);
}

/*
 * The swap for Next and Go to. One that ends a hold always says so, even when it
 * lands on the layer the hold was showing: nothing in the keymap moves, but the
 * layer is now the key's choice rather than the hold's, and Studio learns that
 * from STATE's change, which sends a STATE on its own (host.c, state_update()).
 */
static void key_swap_to(zmk_keymap_layer_id_t target, bool ended_hold) {
    if (ended_hold && target == mp_host_layer()) {
        mp_host_set_layer(target, MP_LAYER_CHANGE_KEY);
        return;
    }

    swap_to(target, MP_LAYER_CHANGE_KEY);
}

int mp_profile_layer_next(void) {
    k_spinlock_key_t key = k_spin_lock(&profile_layers_lock);

    if (group_len == 0) {
        k_spin_unlock(&profile_layers_lock, key);
        return -ENOENT;
    }

    // The group's first when the pad is on none of its layers, as during Hold for Default.
    const zmk_keymap_layer_id_t current = mp_host_layer();
    zmk_keymap_layer_id_t target = group[0];

    for (uint8_t i = 0; i < group_len; i++) {
        if (group[i] == current) {
            target = group[(i + 1) % group_len];
            break;
        }
    }

    // A key that moves the profile layer ends a hold, so its release does nothing.
    const bool ended_hold = hold_token != 0;
    hold_token = 0;
    k_spin_unlock(&profile_layers_lock, key);

    key_swap_to(target, ended_hold);
    return 0;
}

int mp_profile_layer_go(zmk_keymap_layer_id_t target) {
    k_spinlock_key_t key = k_spin_lock(&profile_layers_lock);

    // Only the held profile's own layers: Default, or another profile's layer, is no target.
    if (!in_group(target)) {
        k_spin_unlock(&profile_layers_lock, key);
        return -ENOENT;
    }

    const bool ended_hold = hold_token != 0;
    hold_token = 0;
    k_spin_unlock(&profile_layers_lock, key);

    key_swap_to(target, ended_hold);
    return 0;
}

int mp_profile_layer_hold(zmk_keymap_layer_id_t target, uint32_t *token) {
    k_spinlock_key_t key = k_spin_lock(&profile_layers_lock);

    // Default is a target only with a group held, like the rest: with no profile there is no
    // profile layer to take off.
    if (group_len == 0 || (target != zmk_keymap_layer_default() && !in_group(target))) {
        k_spin_unlock(&profile_layers_lock, key);
        *token = 0;
        return -ENOENT;
    }

    // The newest hold wins but keeps the layer the first one left, so letting go of every hold
    // key, in any order, ends where the first went down.
    if (hold_token == 0) {
        hold_return = mp_host_layer();
    }

    // Never 0, which stands for no hold.
    last_token++;
    if (last_token == 0) {
        last_token = 1;
    }

    hold_token = last_token;
    *token = hold_token;
    k_spin_unlock(&profile_layers_lock, key);

    swap_to(target, MP_LAYER_CHANGE_HOLD_BEGAN);
    return 0;
}

void mp_profile_layer_release(uint32_t token) {
    k_spinlock_key_t key = k_spin_lock(&profile_layers_lock);

    // A hold that is no longer current was ended by something else that moved the profile
    // layer, and that move stands.
    const bool current = token != 0 && token == hold_token;
    const zmk_keymap_layer_id_t back = hold_return;

    if (current) {
        hold_token = 0;
    }

    k_spin_unlock(&profile_layers_lock, key);

    if (current) {
        swap_to(back, MP_LAYER_CHANGE_HOLD_ENDED);
    }
}

void mp_profile_layers_clear(void) {
    k_spinlock_key_t key = k_spin_lock(&profile_layers_lock);
    group_len = 0;
    hold_token = 0;
    k_spin_unlock(&profile_layers_lock, key);
}

/*
 * SWITCH_PROFILE
 */

/*
 * The ids are a group the pad can swap within (host-protocol.md,
 * "SWITCH_PROFILE"), each failure answered with reason 1. None comes twice, so
 * Next goes through each layer once. layer_id is among them, so the switch
 * lands inside the group. None is Default, which shows when the profile layer
 * goes off and so cannot be one of its layers.
 */
static bool group_valid(const struct mp_switch_profile *cmd) {
    bool has_layer_id = false;

    for (uint8_t i = 0; i < cmd->count; i++) {
        for (uint8_t j = 0; j < i; j++) {
            if (cmd->layer_ids[j] == cmd->layer_ids[i]) {
                LOG_WRN("Rejecting SWITCH_PROFILE with layer id %d twice", cmd->layer_ids[i]);
                return false;
            }
        }

        has_layer_id |= cmd->layer_ids[i] == cmd->layer_id;
    }

    if (!has_layer_id) {
        LOG_WRN("Rejecting SWITCH_PROFILE for layer id %d, which is not among its layers",
                cmd->layer_id);
        return false;
    }

    const zmk_keymap_layer_id_t default_layer = zmk_keymap_layer_default();

    for (uint8_t i = 0; i < cmd->count; i++) {
        if (cmd->layer_ids[i] == default_layer) {
            LOG_WRN("Rejecting SWITCH_PROFILE with Default, layer id %d, among its layers",
                    default_layer);
            return false;
        }
    }

    return true;
}

void mp_host_handle_switch_profile(const struct mp_host_transport *from, const uint8_t *payload) {
    struct mp_switch_profile cmd;
    memcpy(&cmd, payload, offsetof(struct mp_switch_profile, layer_ids));

    // frame.c has refused a count above the group's size as too short already, since no frame
    // holds that many ids. Checked again before the copy, which the group's size bounds.
    if (cmd.count == 0 || cmd.count > MP_SWITCH_PROFILE_GROUP_MAX) {
        LOG_WRN("Rejecting SWITCH_PROFILE with %d layers", cmd.count);
        mp_host_reject(from, MP_CMD_SWITCH_PROFILE, MP_REJECTED_OUT_OF_RANGE);
        return;
    }

    // Only the count ids: anything after them is a later revision's.
    memcpy(cmd.layer_ids, payload + offsetof(struct mp_switch_profile, layer_ids), cmd.count);

    // host-protocol.md, "Validation": the whole frame is checked before any part of it is
    // applied, so an app switch cannot half-apply, and every range check comes before the
    // keymap's.
    if (!group_valid(&cmd)) {
        mp_host_reject(from, MP_CMD_SWITCH_PROFILE, MP_REJECTED_OUT_OF_RANGE);
        return;
    }

    for (uint8_t i = 0; i < cmd.count; i++) {
        if (!mp_host_layer_in_keymap(cmd.layer_ids[i])) {
            LOG_WRN("Rejecting SWITCH_PROFILE with layer id %d, which the keymap does not hold",
                    cmd.layer_ids[i]);
            mp_host_reject(from, MP_CMD_SWITCH_PROFILE, MP_REJECTED_UNKNOWN_LAYER);
            return;
        }
    }

    // A hold from before the switch ends with it, so its key's release does nothing.
    k_spinlock_key_t key = k_spin_lock(&profile_layers_lock);
    memcpy(group, cmd.layer_ids, cmd.count);
    group_len = cmd.count;
    hold_token = 0;
    k_spin_unlock(&profile_layers_lock, key);

    mp_host_switch_profile(cmd.layer_id);
}
