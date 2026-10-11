/*
 * Minimalpad LEDs: layer and key lights over the underglow
 *
 * A LED strip between ZMK's underglow and the real WS2812 chain. The underglow
 * keeps computing its effect every 50 ms and hands each frame here; this strip
 * replaces the pixels a layer or key light covers and passes the frame on.
 * Drawing over the underglow's frame, rather than into the chain beside it,
 * means the underglow's lights-off, brightness and idle fade keep working, and
 * a frame is written to the chain once, so nothing flickers.
 *
 * Which light a key shows is mp_lights_resolve()'s to say (src/host/lights.c):
 * Inherit leaves the underglow's own pixel, a colour is drawn at the
 * underglow's current brightness, and Off is dark. While the underglow is off
 * the frame goes through as it is: the host never turns lights on. While the
 * pad is showing its own lighting (mp_lights_showing_pad()), the underglow
 * shows as it is, in every pixel.
 *
 * The strip is built whether or not the host module's lights are, because the
 * shield's devicetree makes it the underglow's. Without them (host.h) every
 * key resolves to Inherit and every frame goes through unchanged.
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT minimalpad_light_layers

#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/sys/util.h>

#include <zmk/rgb_underglow.h>

#include <minimalpad/host.h>

#define CHAIN_LENGTH DT_INST_PROP(0, chain_length)

static const struct device *const chain = DEVICE_DT_GET(DT_INST_PHANDLE(0, led_strip));
static const uint8_t key_positions[] = DT_INST_PROP(0, key_positions);
BUILD_ASSERT(ARRAY_SIZE(key_positions) == CHAIN_LENGTH, "one key position per LED");

/* The same conversion as ZMK's own (rgb_underglow.c keeps it private). */
static struct led_rgb hsb_to_rgb(uint16_t hue, uint8_t sat, uint8_t bri) {
    float h = (float)hue / 60.0f;
    float s = (float)sat / 100.0f;
    float v = (float)bri / 100.0f;
    int i = (int)h;
    float f = h - i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));
    float r = 0, g = 0, b = 0;
    switch (i % 6) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    case 5: r = v; g = p; b = q; break;
    }
    return (struct led_rgb){.r = (uint8_t)(r * 255), .g = (uint8_t)(g * 255), .b = (uint8_t)(b * 255)};
}

/* The brightness the underglow draws at now, scaled the way it scales its own. */
static uint8_t current_brightness(void) {
    uint8_t b = zmk_rgb_underglow_calc_brt(0).b;
    return CONFIG_ZMK_RGB_UNDERGLOW_BRT_MIN +
           (CONFIG_ZMK_RGB_UNDERGLOW_BRT_MAX - CONFIG_ZMK_RGB_UNDERGLOW_BRT_MIN) * b / 100;
}

static int light_layers_update_rgb(const struct device *dev, struct led_rgb *pixels,
                                   size_t num_pixels) {
    bool on = false;
    if (zmk_rgb_underglow_get_state(&on) != 0 || !on || mp_lights_showing_pad()) {
        // Off is the person's: the dark frame ZMK sends goes through as it is.
        // So does the underglow's own frame while the pad is showing its own lighting.
        return led_strip_update_rgb(chain, pixels, num_pixels);
    }

    uint8_t bri = current_brightness();
    for (size_t i = 0; i < num_pixels && i < CHAIN_LENGTH; i++) {
        struct mp_light light = mp_lights_resolve(key_positions[i]);
        switch (light.kind) {
        case MP_LIGHT_COLOUR:
            pixels[i] = hsb_to_rgb(light.hue, light.saturation, bri);
            break;
        case MP_LIGHT_OFF:
            pixels[i] = (struct led_rgb){0};
            break;
        default: // MP_LIGHT_INHERIT: the underglow's own pixel stays.
            break;
        }
    }
    return led_strip_update_rgb(chain, pixels, num_pixels);
}

static size_t light_layers_length(const struct device *dev) { return CHAIN_LENGTH; }

static int light_layers_init(const struct device *dev) {
    return device_is_ready(chain) ? 0 : -ENODEV;
}

static const struct led_strip_driver_api light_layers_api = {
    .update_rgb = light_layers_update_rgb,
    .length = light_layers_length,
};

// After the WS2812 chain (CONFIG_LED_STRIP_INIT_PRIORITY), before ZMK's
// underglow (APPLICATION).
DEVICE_DT_INST_DEFINE(0, light_layers_init, NULL, NULL, NULL, POST_KERNEL, 99, &light_layers_api);
