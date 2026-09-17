/*
 * Minimalpad LEDs
 *
 * The underglow colour, for code that reads or sets it while the pad may be
 * fading its LEDs out for idle (src/leds/idle_fade.c). ZMK keeps brightness
 * inside the colour, so mid-fade zmk_rgb_underglow_calc_hue(0) returns a
 * dimmed step and the next step undoes zmk_rgb_underglow_set_hsb(). These
 * read and set the pad's own colour instead.
 *
 * Call them from the system work queue, where the fade runs.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

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

#else

static inline struct zmk_led_hsb mp_leds_color(void) { return zmk_rgb_underglow_calc_hue(0); }

static inline int mp_leds_set_color(struct zmk_led_hsb color) {
    return zmk_rgb_underglow_set_hsb(color);
}

#endif /* IS_ENABLED(CONFIG_MINIMALPAD_LEDS_IDLE_FADE) */
