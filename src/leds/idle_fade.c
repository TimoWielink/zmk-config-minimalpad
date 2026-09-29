/*
 * Minimalpad LEDs: fade the underglow out when the pad goes idle
 *
 * ZMK's CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE switches the underglow off the
 * moment the pad goes idle. This dims it slowly instead, then switches it off
 * the same way, which cuts the LEDs' power. A key or the dial brings it
 * straight back.
 *
 * ZMK keeps brightness inside the underglow colour, so the fade dims the colour
 * a step at a time and keeps the pad's own here meanwhile (leds.h). Before the
 * underglow is left off, the pad's colour goes back in, so ZMK's saved state
 * never holds a dimmed one. For the same reason no fade starts while ZMK has a
 * save of a change to the pad's lighting pending, and any other save it has
 * pending when a fade starts is moved to after it.
 *
 * From the start of the fade until a key, the dial or a host wakes it, the
 * underglow is resting: still on as far as the person is concerned, only dark
 * for idle. The host module's lighting commands read that, keep the LEDs awake
 * while a host changes them, and switch them off without lighting them first
 * (host-module.md, "Keeping the lights awake").
 *
 * Everything here runs on the system work queue. ZMK raises activity changes
 * there, from its idle timer and from the key and dial listeners, so the
 * listener and the fade never overlap.
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

#include <zmk/activity.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/rgb_underglow.h>

#include <minimalpad/leds.h>

#if IS_ENABLED(CONFIG_MINIMALPAD_HOST)
#include <minimalpad/host.h>
#endif

LOG_MODULE_REGISTER(minimalpad_leds, CONFIG_MINIMALPAD_LEDS_LOG_LEVEL);

#if IS_ENABLED(CONFIG_ZMK_SLEEP)
BUILD_ASSERT(CONFIG_ZMK_IDLE_SLEEP_TIMEOUT >
                 CONFIG_ZMK_IDLE_TIMEOUT + CONFIG_MINIMALPAD_LEDS_IDLE_FADE_MS,
             "The pad would deep-sleep before the underglow finished fading");
#endif

/* The largest values zmk_rgb_underglow_set_hsb() takes, which rgb_underglow.c keeps private. */
#define HUE_MAX 360
#define SAT_MAX 100
#define BRT_MAX 100

/* ZMK redraws the underglow every 50 ms, so a faster step would not be seen. */
#define FADE_STEP K_MSEC(50)

/*
 * The wait between switching off and putting the pad's colour back. A redraw
 * ZMK queued just before the switch still runs once, and should draw the dark
 * last step rather than the full colour.
 */
#define SETTLE_TIME K_MSEC(100)

enum phase {
    /* The underglow is as the pad's settings say, on or off. Nothing here holds it. */
    PHASE_AWAKE,
    /* Idle, and dimming towards off. */
    PHASE_FADING,
    /* Switched off, at the end of a fade or by hand during one, still holding the step it
     * showed. Then PHASE_OFF, or PHASE_AWAKE when it was switched off by hand. */
    PHASE_SETTLING,
    /* Switched off for idle, with the pad's colour back in place. */
    PHASE_OFF,
};

static enum phase phase = PHASE_AWAKE;

/* Where PHASE_SETTLING goes: PHASE_OFF after the fade, PHASE_AWAKE after a switch-off by hand. */
static enum phase settled_phase = PHASE_OFF;

/* The pad's own colour outside PHASE_AWAKE, while ZMK's may hold a dimmed step. */
static struct zmk_led_hsb awake_color;

static int64_t fade_started;

/*
 * No fade starts before this uptime, in milliseconds: a host is changing the
 * lights (mp_leds_hold_awake()), or ZMK has yet to save a change to them
 * (mp_leds_wait_for_save()).
 */
static int64_t awake_until;

/*
 * The brightness `elapsed` milliseconds into the fade. It falls along a square
 * rather than a straight line: eyes judge light on a curve, and a straight fade
 * seems to hang near full and then drop out at the end.
 */
static uint8_t faded_brightness(int64_t elapsed) {
    if (elapsed >= CONFIG_MINIMALPAD_LEDS_IDLE_FADE_MS) {
        return 0;
    }

    // At least 1, so a 0 ms fade, which returns above, still compiles without a division by zero.
    const uint64_t total = MAX(CONFIG_MINIMALPAD_LEDS_IDLE_FADE_MS, 1);
    const uint64_t left = total - MAX(elapsed, 0);
    return (uint8_t)((awake_color.b * left * left + total * total / 2) / (total * total));
}

/* Tells the host module the underglow started or stopped resting, which ZMK has no event for. */
static void report_resting_changed(void) {
#if IS_ENABLED(CONFIG_MINIMALPAD_HOST)
    mp_host_lighting_changed();
#endif
}

/*
 * Whether the pad went down while switched off for idle, kept in flash
 */

#if IS_ENABLED(CONFIG_SETTINGS)

#define IDLE_OFF_SETTING "mp_leds/idle_off"

/* How long past ZMK's pending save of the underglow the flag stays in flash after waking. */
#define IDLE_OFF_FORGET_MARGIN_MS 5000

/* How long past ZMK's own save debounce the fade waits for that save to have landed. */
#define SAVE_MARGIN_MS 100

/*
 * When ZMK's save of the underglow state lands, at the latest, for a save
 * someone said it scheduled (leds.h). ZMK saves whatever its state holds when
 * its debounce runs out, and mid-fade that is a dimmed step.
 */
static int64_t save_due;

/*
 * Switching off saves the underglow as off, like any other switch, and a
 * restart loads it that way: waking from deep sleep, a reset, flashing, a flat
 * battery. This flag tells a restart apart from LEDs switched off by hand, so
 * they come back on. It is written when ZMK's own save is, so a pad woken within
 * that debounce writes neither.
 */
static bool idle_off_saved;

static void idle_off_save(struct k_work *work) {
    const uint8_t value = 1;
    int err = settings_save_one(IDLE_OFF_SETTING, &value, sizeof(value));
    if (err < 0) {
        LOG_WRN("Failed to save that the underglow is off for idle (%d)", err);
        return;
    }

    idle_off_saved = true;
}

static K_WORK_DELAYABLE_DEFINE(idle_off_save_work, idle_off_save);

static void idle_off_delete(void) {
    int err = settings_delete(IDLE_OFF_SETTING);
    if (err < 0) {
        LOG_WRN("Failed to delete the idle flag (%d)", err);
        return;
    }

    idle_off_saved = false;
}

static void idle_off_forget(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(idle_off_forget_work, idle_off_forget);

/*
 * After waking, flash still says off until ZMK's save of the underglow as on
 * lands, so the flag stays until then. That is usually a debounce after the
 * wake, but a fade that starts meanwhile moves the save a full debounce later
 * (move_pending_save()), and a key during the fade wakes the LEDs without
 * saving again. Deleted before that save, the flag would leave a restart in
 * between loading the underglow as off, with nothing to switch it back on.
 */
static void idle_off_forget(struct k_work *work) {
    const int64_t left = save_due + IDLE_OFF_FORGET_MARGIN_MS - k_uptime_get();
    if (left > 0) {
        k_work_reschedule(&idle_off_forget_work, K_MSEC(left));
        return;
    }

    idle_off_delete();
}

static void remember_idle_off(void) {
    k_work_cancel_delayable(&idle_off_forget_work);
    if (!idle_off_saved) {
        // Scheduled just before zmk_rgb_underglow_off() schedules its own save, so it lands first.
        k_work_reschedule(&idle_off_save_work, K_MSEC(CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE));
    }
}

static void forget_idle_off(void) {
    k_work_cancel_delayable(&idle_off_save_work);
    if (idle_off_saved) {
        // Kept until ZMK has saved the underglow as on again, so a restart in between still
        // brings the LEDs back. idle_off_forget() waits longer if that save moves.
        k_work_reschedule(&idle_off_forget_work,
                          K_MSEC(CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE + IDLE_OFF_FORGET_MARGIN_MS));
    }
}

/*
 * For LEDs switched off by hand while the flag may still be in flash: off for
 * idle, or woken from it before ZMK saved them as on. ZMK's state now says off,
 * and will not be saved as on again, so the flag goes now: kept, it would
 * switch them back on at the next restart. Deleted here rather than by
 * idle_off_forget(), which would wait for ZMK's pending save, now of off.
 */
static void forget_idle_off_now(void) {
    k_work_cancel_delayable(&idle_off_save_work);
    k_work_cancel_delayable(&idle_off_forget_work);
    if (idle_off_saved) {
        idle_off_delete();
    }
}

static void note_save(void) {
    save_due = k_uptime_get() + CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE;
}

static void keep_awake_until(int64_t when);

/*
 * For a save that holds a change to the pad's own lighting. The fade waits for
 * it: moving it past the fade would push it a full debounce further, and the
 * switch-off at the end of the fade further still, so a change would be saved
 * a minute and a half after the last key rather than a minute.
 *
 * A change that left the LEDs switched off by hand, such as the Toggle key's,
 * lets go of the flag now, as mp_leds_switch_off() does. Left to
 * idle_off_forget(), it would wait for this save, and a restart in between
 * would switch them back on.
 */
static void wait_for_save(void) {
    note_save();
    keep_awake_until(save_due + SAVE_MARGIN_MS);

    bool on = false;
    if (phase == PHASE_AWAKE && zmk_rgb_underglow_get_state(&on) == 0 && !on) {
        forget_idle_off_now();
    }
}

/*
 * Called as the fade starts, while ZMK still holds the pad's colour, for a
 * save that holds no change of the person's, such as a host colour's. Any
 * change starts ZMK's debounce again, so an unseen one moves its save to a full
 * debounce from now, past the end of the fade. By then the pad is awake, or
 * switched off with its colour back in place, and switching off saves again
 * anyway.
 */
static void move_pending_save(void) {
    if (k_uptime_get() < save_due) {
        LOG_DBG("Moving ZMK's pending save of the underglow past the fade");
        zmk_rgb_underglow_change_hue(0);
        note_save();
    }
}

int64_t mp_leds_save_due(void) { return save_due; }

static void boot_wake(struct k_work *work) {
    bool on = false;
    if (phase == PHASE_AWAKE && zmk_rgb_underglow_get_state(&on) == 0 && !on) {
        LOG_INF("Switching the underglow back on: the pad restarted while it was off for idle");
        zmk_rgb_underglow_on();
        note_save();
    }

    forget_idle_off();
}

static K_WORK_DEFINE(boot_wake_work, boot_wake);

static int leds_settings_set(const char *name, size_t len, settings_read_cb read_cb,
                             void *cb_arg) {
    const char *next;

    if (settings_name_steq(name, "idle_off", &next) && !next) {
        uint8_t value = 0;
        if (len != sizeof(value)) {
            return -EINVAL;
        }

        int rc = read_cb(cb_arg, &value, sizeof(value));
        if (rc < 0) {
            return rc;
        }

        idle_off_saved = value == 1;
        return 0;
    }

    return -ENOENT;
}

static int leds_settings_commit(void) {
    // Commit comes after every subtree has loaded, so the underglow state is the saved one.
    if (idle_off_saved) {
        k_work_submit(&boot_wake_work);
    }

    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(minimalpad_leds, "mp_leds", NULL, leds_settings_set,
                               leds_settings_commit, NULL);

#else /* IS_ENABLED(CONFIG_SETTINGS) */

static void remember_idle_off(void) {}

static void forget_idle_off(void) {}

static void forget_idle_off_now(void) {}

static void note_save(void) {}

static void wait_for_save(void) {}

static void move_pending_save(void) {}

int64_t mp_leds_save_due(void) { return 0; }

#endif /* IS_ENABLED(CONFIG_SETTINGS) */

/*
 * The fade
 */

static void fade_step(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(fade_work, fade_step);

static void switch_off(void) {
    struct zmk_led_hsb dark = awake_color;
    dark.b = 0;
    zmk_rgb_underglow_set_hsb(dark);

    remember_idle_off();
    int err = zmk_rgb_underglow_off();
    if (err < 0) {
        LOG_WRN("Failed to switch the underglow off (%d)", err);
    }
    note_save();

    phase = PHASE_SETTLING;
    settled_phase = PHASE_OFF;
    k_work_reschedule(&fade_work, SETTLE_TIME);

#if IS_ENABLED(CONFIG_MINIMALPAD_HOST)
    // No ZMK event says the underglow went off, so STATE would not follow by itself.
    mp_host_leds_changed();
#endif
}

static void fade_step(struct k_work *work) {
    switch (phase) {
    case PHASE_FADING: {
        const int64_t elapsed = k_uptime_get() - fade_started;
        if (elapsed >= CONFIG_MINIMALPAD_LEDS_IDLE_FADE_MS) {
            switch_off();
            return;
        }

        struct zmk_led_hsb dimmed = awake_color;
        dimmed.b = faded_brightness(elapsed);
        zmk_rgb_underglow_set_hsb(dimmed);
        k_work_reschedule(&fade_work, FADE_STEP);
        return;
    }

    case PHASE_SETTLING:
        // Not drawn while off. It is what ZMK saves, and what the LEDs wake in.
        zmk_rgb_underglow_set_hsb(awake_color);
        phase = settled_phase;
        return;

    default:
        return;
    }
}

static void fade_out(void) {
    bool on = false;
    if (k_uptime_get() < awake_until || phase != PHASE_AWAKE ||
        zmk_rgb_underglow_get_state(&on) < 0 || !on) {
        // Kept awake for now, already fading or off, or switched off by hand: nothing to fade.
        return;
    }

    LOG_DBG("Idle: fading the underglow out over %d ms", CONFIG_MINIMALPAD_LEDS_IDLE_FADE_MS);

    // With a direction of 0 this returns the current colour unchanged.
    awake_color = zmk_rgb_underglow_calc_hue(0);
    move_pending_save();
    fade_started = k_uptime_get();
    phase = PHASE_FADING;
    k_work_reschedule(&fade_work, K_NO_WAIT);

    report_resting_changed();
}

static void wake(void) {
    if (!mp_leds_resting()) {
        // Awake already, or settling after a switch-off by hand, which ends awake by itself.
        return;
    }

    LOG_DBG("Active: the underglow is back");

    k_work_cancel_delayable(&fade_work);
    zmk_rgb_underglow_set_hsb(awake_color);

    if (phase != PHASE_FADING) {
        int err = zmk_rgb_underglow_on();
        if (err < 0) {
            LOG_WRN("Failed to switch the underglow back on (%d)", err);
        }
        note_save();

        forget_idle_off();
    }

    phase = PHASE_AWAKE;
    report_resting_changed();
}

static void awake_until_passed(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(awake_until_work, awake_until_passed);

/* Nothing keeps the underglow awake any more: the pad's own rule again. */
static void awake_until_passed(struct k_work *work) {
    const int64_t left = awake_until - k_uptime_get();
    if (left > 0) {
        // Woken a tick early by rounding. Without another go the fade would wait for a key.
        k_work_reschedule(&awake_until_work, K_MSEC(left));
        return;
    }

    // Going idle meanwhile raised the only idle change ZMK makes, so the fade starts here.
    if (zmk_activity_get_state() != ZMK_ACTIVITY_ACTIVE) {
        fade_out();
    }
}

static void keep_awake_until(int64_t when) {
    if (when <= awake_until) {
        return;
    }

    awake_until = when;
    k_work_reschedule(&awake_until_work, K_MSEC(MAX(when - k_uptime_get(), 0)));
}

struct zmk_led_hsb mp_leds_color(void) {
    return phase == PHASE_AWAKE ? zmk_rgb_underglow_calc_hue(0) : awake_color;
}

int mp_leds_set_color(struct zmk_led_hsb color) {
    if (phase == PHASE_AWAKE) {
        return zmk_rgb_underglow_set_hsb(color);
    }

    if (color.h > HUE_MAX || color.s > SAT_MAX || color.b > BRT_MAX) {
        return -ENOTSUP;
    }

    awake_color = color;

    switch (phase) {
    case PHASE_FADING:
        // Shown at the fade's current step now, rather than at the next one.
        color.b = faded_brightness(k_uptime_get() - fade_started);
        return zmk_rgb_underglow_set_hsb(color);

    case PHASE_OFF:
        return zmk_rgb_underglow_set_hsb(color);

    default:
        // Settling puts awake_color back itself.
        return 0;
    }
}

bool mp_leds_resting(void) {
    switch (phase) {
    case PHASE_FADING:
    case PHASE_OFF:
        return true;
    case PHASE_SETTLING:
        return settled_phase == PHASE_OFF;
    default:
        return false;
    }
}

void mp_leds_hold_awake(uint32_t duration_ms) {
    keep_awake_until(k_uptime_get() + duration_ms);
    wake();
}

int mp_leds_switch_off(void) {
    int err = 0;

    switch (phase) {
    case PHASE_AWAKE: {
        bool on = false;
        if (zmk_rgb_underglow_get_state(&on) < 0 || !on) {
            return 0;
        }

        err = zmk_rgb_underglow_off();
        note_save();
        forget_idle_off_now();
        return err;
    }

    case PHASE_FADING:
        // Off from the step it shows, then the pad's colour goes back in, as at the end of the
        // fade, so the redraw ZMK may have queued draws that step and not the full colour.
        err = zmk_rgb_underglow_off();
        note_save();
        forget_idle_off_now();
        phase = PHASE_SETTLING;
        settled_phase = PHASE_AWAKE;
        k_work_reschedule(&fade_work, SETTLE_TIME);
        break;

    case PHASE_SETTLING:
        if (settled_phase == PHASE_AWAKE) {
            return 0;
        }

        settled_phase = PHASE_AWAKE;
        forget_idle_off_now();
        break;

    case PHASE_OFF:
        phase = PHASE_AWAKE;
        forget_idle_off_now();
        break;
    }

    LOG_DBG("Switched off by hand while resting");
    report_resting_changed();
    return err;
}

void mp_leds_save(void) {
    zmk_rgb_underglow_change_hue(0);
    wait_for_save();
}

void mp_leds_wait_for_save(void) { wait_for_save(); }

void mp_leds_save_scheduled(void) { note_save(); }

static int leds_event_listener(const zmk_event_t *eh) {
    const struct zmk_activity_state_changed *ev = as_zmk_activity_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    // Handled here rather than on work: ZMK's activity listener raises this before the keymap
    // sees the key press that woke the pad, so a key that changes the underglow, such as
    // brightness on the Connections & LEDs layer, changes the pad's colour and not a dimmed step.
    if (ev->state == ZMK_ACTIVITY_ACTIVE) {
        wake();
    } else {
        fade_out();
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(minimalpad_leds, leds_event_listener);
ZMK_SUBSCRIPTION(minimalpad_leds, zmk_activity_state_changed);
