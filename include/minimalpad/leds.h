/*
 * Minimalpad LEDs
 *
 * The underglow colour, for code that reads or sets it while the pad may be
 * fading its LEDs out for idle (src/leds/idle_fade.c). ZMK keeps brightness
 * inside the colour, so mid-fade zmk_rgb_underglow_calc_hue(0) returns a
 * dimmed step and the next step undoes zmk_rgb_underglow_set_hsb(). These
 * read and set the pad's own colour instead.
 *
 * The rest is for the host module's lighting commands (src/host/lighting.c):
 * whether the LEDs are resting for idle, keeping them awake while a host
 * changes them, switching them off, and ZMK's save of the underglow state.
 *
 * Call them from the system work queue, where the fade runs.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include <zmk/rgb_underglow.h>

#if IS_ENABLED(CONFIG_MINIMALPAD_LEDS_IDLE_FADE)

/* The underglow colour, at the brightness the pad shows when it is awake. */
struct zmk_led_hsb mp_leds_color(void);

/*
 * Sets the underglow colour. Mid-fade it shows at the fade's brightness, and
 * at its own when the pad wakes. Like zmk_rgb_underglow_set_hsb() it saves
 * nothing, and returns -ENOTSUP for a value out of range.
 */
int mp_leds_set_color(struct zmk_led_hsb color);

/*
 * Whether the underglow is resting: on, but dimming or off because the pad is
 * idle. It starts resting when the fade starts, and a key, the dial or
 * mp_leds_hold_awake() wakes it. Underglow switched off by hand never rests.
 */
bool mp_leds_resting(void);

/*
 * Wakes the underglow if it is resting, and holds off the idle fade until
 * `duration_ms` after the last call. Host commands are not activity to ZMK, so
 * without this the LEDs would fade while someone changes them from a host.
 * The hold always runs out: then the fade starts at once if the pad has gone
 * idle meanwhile, and otherwise when it next does. It is not activity for
 * anything else, so it does not put off deep sleep.
 */
void mp_leds_hold_awake(uint32_t duration_ms);

/*
 * Switches the underglow off, as the Toggle key does, cutting the LEDs'
 * power. Resting LEDs stop resting without lighting up first, and stay off
 * after a restart. Does nothing to LEDs already switched off by hand.
 */
int mp_leds_switch_off(void);

/*
 * Saves a change to the pad's own colour or brightness: schedules ZMK's save
 * of the underglow state, which ZMK does not do for a colour set with
 * zmk_rgb_underglow_set_hsb() (ZMK issue #1920), and waits for it as
 * mp_leds_wait_for_save() does. Changes no colour anyone can see.
 */
void mp_leds_save(void);

/*
 * Says ZMK has scheduled its own save of a change to the pad's own lighting,
 * as it does for on, off, effect and speed. The idle fade does not start until
 * that save has landed, a debounce after the change, so the save holds the
 * pad's colour and not a dimmed step, and lands as soon as ZMK's debounce lets
 * it. The LEDs therefore stay lit up to a debounce after the last change,
 * rather than ZMK's idle timeout after the last key.
 */
void mp_leds_wait_for_save(void);

/*
 * Says ZMK has scheduled its own save of the underglow state for anything
 * else, such as putting a host colour on or taking it off. A fade that starts
 * before it lands moves it to after the fade instead, so ZMK never saves a
 * dimmed step as the pad's colour.
 */
void mp_leds_save_scheduled(void);

/*
 * When ZMK's pending save of the underglow state lands at the latest, in
 * k_uptime_get() milliseconds, counting the saves said to be scheduled here and
 * the fade's moves of them. In the past when none is pending.
 */
int64_t mp_leds_save_due(void);

#else

static inline struct zmk_led_hsb mp_leds_color(void) { return zmk_rgb_underglow_calc_hue(0); }

static inline int mp_leds_set_color(struct zmk_led_hsb color) {
    return zmk_rgb_underglow_set_hsb(color);
}

static inline bool mp_leds_resting(void) { return false; }

static inline void mp_leds_hold_awake(uint32_t duration_ms) { ARG_UNUSED(duration_ms); }

static inline int mp_leds_switch_off(void) {
    bool on = false;
    if (zmk_rgb_underglow_get_state(&on) < 0 || !on) {
        return 0;
    }

    return zmk_rgb_underglow_off();
}

static inline void mp_leds_save(void) { zmk_rgb_underglow_change_hue(0); }

static inline void mp_leds_wait_for_save(void) {}

static inline void mp_leds_save_scheduled(void) {}

static inline int64_t mp_leds_save_due(void) { return 0; }

#endif /* IS_ENABLED(CONFIG_MINIMALPAD_LEDS_IDLE_FADE) */
