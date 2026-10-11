/*
 * Minimalpad host module
 *
 * What the frame reader (frame.c), the core (host.c, with dial.c, lighting.c,
 * lights.c and profile_layers.c) and the transports share. The wire format
 * itself lives in mp_host_protocol.h, a verbatim copy of
 * minimalpad-studio-mac/docs/protocol/mp_host_protocol.h: change it there.
 *
 * Threading: the core runs on the system work queue, the queue ZMK processes
 * key presses on. Transports hand received bytes to that queue before calling
 * mp_host_reader_feed(), and only the calls below that say so may be made from
 * elsewhere.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zmk/keymap.h>

#include <minimalpad/mp_host_protocol.h>

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW)
#include <zmk/rgb_underglow.h>
#endif

/*
 * One way to reach a host. Bluetooth has its GATT adapter and USB has a
 * combined CDC-ACM adapter; both use the same frame reader and command core.
 */
struct mp_host_transport {
    const char *name;

    /* Sends one whole frame to every host listening. Returns 0, -ENOTCONN when
     * no host is listening, or another negative errno. */
    int (*send)(const uint8_t *frame, size_t len);

    /* Sends one whole frame to the host whose frame is being handled, for
     * answers (host-protocol.md, "Who hears what"). Only valid inside a
     * handler. */
    int (*reply)(const uint8_t *frame, size_t len);

    /* Whether the host whose frame is being handled may change the pad
     * (host-protocol.md, "Which host is in charge"). Only valid inside a
     * handler. */
    bool (*sender_may_command)(void);
};

extern const struct mp_host_transport mp_host_transport_gatt;
#if IS_ENABLED(CONFIG_MINIMALPAD_HOST_USB)
extern const struct mp_host_transport mp_host_transport_usb;
#endif

/*
 * Turns one transport's bytes into frames and dispatches them.
 *
 * A stream transport (a serial port) calls mp_host_reader_feed() for every
 * read and keeps any partial frame for the next one. A datagram transport
 * (Bluetooth, where every frame fits one packet) calls mp_host_reader_reset()
 * after each write, so a cut-off frame is dropped instead of being completed
 * by the next write's bytes.
 */
struct mp_host_reader {
    uint8_t buf[MP_HOST_FRAME_MAX];
    uint8_t len;

    /* Frames ignored for an unknown version or command. */
    uint32_t skipped;
    /* Frames whose length disagrees with their command's payload size. */
    uint32_t rejected;
    /* Reads abandoned for a length above MP_HOST_PAYLOAD_MAX, or cut off. */
    uint32_t dropped;
};

void mp_host_reader_feed(struct mp_host_reader *reader, const struct mp_host_transport *from,
                         const uint8_t *data, size_t len);

void mp_host_reader_reset(struct mp_host_reader *reader);

/*
 * Command handlers, host.c. frame.c calls them with a payload whose length
 * already matches the command's struct in mp_host_protocol.h (none for HELLO,
 * GET_STATE, GET_LIGHTING and GET_FEATURES). Replies go back through `from`.
 */
void mp_host_handle_hello(const struct mp_host_transport *from, const uint8_t *payload);
void mp_host_handle_set_profile(const struct mp_host_transport *from, const uint8_t *payload);
void mp_host_handle_heartbeat(const struct mp_host_transport *from, const uint8_t *payload);
void mp_host_handle_set_leds(const struct mp_host_transport *from, const uint8_t *payload);
void mp_host_handle_enter_bootloader(const struct mp_host_transport *from, const uint8_t *payload);
void mp_host_handle_get_state(const struct mp_host_transport *from, const uint8_t *payload);
void mp_host_handle_get_features(const struct mp_host_transport *from, const uint8_t *payload);
/* src/host/dial.c, built with CONFIG_MINIMALPAD_HOST_DIAL. */
/*
 * The dial value a turn gets when ZMK gave it to the &host_dial binding on
 * `answering_layer`: a value set for an active layer above it, then one set for
 * that layer, then its keymap's. Returns false when none applies. Call from the
 * system work queue.
 */
bool mp_host_dial_resolve(uint8_t answering_layer, bool clockwise, uint32_t *value);
void mp_host_handle_set_dial(const struct mp_host_transport *from, const uint8_t *payload);
void mp_host_handle_get_dial(const struct mp_host_transport *from, const uint8_t *payload);
/* src/host/lighting.c, built with CONFIG_MINIMALPAD_HOST_LIGHTING. */
void mp_host_handle_get_lighting(const struct mp_host_transport *from, const uint8_t *payload);
void mp_host_handle_set_lighting(const struct mp_host_transport *from, const uint8_t *payload);
/* src/host/lights.c, built with CONFIG_MINIMALPAD_HOST_LIGHTS. */
void mp_host_handle_set_layer_light(const struct mp_host_transport *from, const uint8_t *payload);
void mp_host_handle_get_layer_light(const struct mp_host_transport *from, const uint8_t *payload);
void mp_host_handle_show_pad_lighting(const struct mp_host_transport *from,
                                      const uint8_t *payload);
void mp_host_handle_set_key_light(const struct mp_host_transport *from, const uint8_t *payload);
void mp_host_handle_get_key_lights(const struct mp_host_transport *from, const uint8_t *payload);
/*
 * src/host/profile_layers.c, built with CONFIG_MINIMALPAD_PROFILE_LAYERS. The
 * payload's length is in the payload, 2 + count, and frame.c has checked the
 * frame holds that much, so the count layer ids are there to read.
 */
void mp_host_handle_switch_profile(const struct mp_host_transport *from, const uint8_t *payload);

/*
 * Answers a dropped command with REJECTED, to the host that sent it only. A
 * rejected command has changed nothing: every check runs before any part of it
 * is applied (host-protocol.md, "Validation").
 */
void mp_host_reject(const struct mp_host_transport *from, uint8_t command,
                    enum mp_rejected_reason reason);

/*
 * Answers the host whose frame is being handled, and only that host
 * (host-protocol.md, "Who hears what"). Only valid inside a handler.
 */
void mp_host_reply(const struct mp_host_transport *to, uint8_t event, const void *payload,
                   uint8_t len);

/*
 * Sends one event to one transport, or to every transport with a host
 * listening. ACTION_EVENT goes out through mp_host_broadcast() from the
 * &mac_action behavior (src/behaviors/mac_action.c). The events still to come,
 * KEY_EVENT and DIAL_EVENT, go the same way with their structs from
 * mp_host_protocol.h.
 */
int mp_host_send(const struct mp_host_transport *to, uint8_t event, const void *payload,
                 uint8_t len);
void mp_host_broadcast(uint8_t event, const void *payload, uint8_t len);

/*
 * A transport calls this when a host starts listening (on Bluetooth, when the
 * event characteristic is subscribed), so the pad volunteers a STATE
 * (host-protocol.md, "State"). Safe from any thread.
 */
void mp_host_host_arrived(const struct mp_host_transport *transport);

/*
 * The underglow switched on or off with no ZMK event to say so, as at the end
 * of the idle fade (src/leds/idle_fade.c), so the pad sends STATE if its LED
 * flag changed. Safe from any thread.
 */
void mp_host_leds_changed(void);

#if IS_ENABLED(CONFIG_MINIMALPAD_HOST_LIGHTING)

/*
 * The pad's own lighting may have changed with no ZMK event to say so: the
 * LEDs started or stopped resting for idle (src/leds/idle_fade.c), or a host
 * colour started or stopped showing. The pad sends LIGHTING_STATE if it
 * differs from the last one every host heard. Safe from any thread.
 */
void mp_host_lighting_changed(void);

/*
 * The dial stepped the underglow's speed, `steps` faster or, below 0, slower
 * (src/behaviors/host_dial.c). ZMK cannot report the speed, so the pad counts
 * it. Each step also schedules ZMK's save, which the idle fade waits for
 * (mp_leds_wait_for_save()). Call from the system work queue.
 */
void mp_host_lighting_speed_stepped(int steps);

#else

static inline void mp_host_lighting_changed(void) {}

static inline void mp_host_lighting_speed_stepped(int steps) { (void)steps; }

#endif /* IS_ENABLED(CONFIG_MINIMALPAD_HOST_LIGHTING) */

#if IS_ENABLED(CONFIG_MINIMALPAD_HOST_LIGHTS)

/*
 * The light a key shows, resolved top-down over the active layers; kind
 * MP_LIGHT_INHERIT means the underglow's own pixel. `position` is a key
 * position, 0-15; any other is Inherit. The hue is in CPU byte order. Safe from
 * any thread.
 */
struct mp_light mp_lights_resolve(uint8_t position);

/*
 * Shows the pad's own lighting in place of layer and key lights for `ms` from
 * now, so a change to it can be seen. A call with `ms` above 0 renews the show
 * to `ms` from then, but never shortens one that lasts longer, and 0 ends it at
 * once. SHOW_PAD_LIGHTING asks for it while Studio's Lights popover is open, and
 * lighting.c asks for it when a key or the dial changes the pad's colour,
 * effect or speed. Safe from any thread.
 */
void mp_lights_show_pad(uint32_t ms);

/*
 * Whether the pad's own lighting is being shown, so the renderer
 * (src/leds/light_layers.c) passes every pixel of the underglow through. Safe
 * from any thread.
 */
bool mp_lights_showing_pad(void);

#else

static inline void mp_lights_show_pad(uint32_t ms) { (void)ms; }

static inline struct mp_light mp_lights_resolve(uint8_t position) {
    (void)position;
    return (struct mp_light){.kind = MP_LIGHT_INHERIT};
}

static inline bool mp_lights_showing_pad(void) { return false; }

#endif /* IS_ENABLED(CONFIG_MINIMALPAD_HOST_LIGHTS) */

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW)

/*
 * The pad's own underglow colour and effect, host.c. While a host colour shows
 * they are the ones the pad keeps aside and puts back when it is released;
 * otherwise they are the live ones. Colours are the pad's awake ones, never a
 * step of the idle fade (leds.h). Call from the system work queue.
 */

/* Whether a host colour shows in place of the pad's own. */
bool mp_host_leds_held(void);

struct zmk_led_hsb mp_host_own_color(void);

uint8_t mp_host_own_effect(void);

/*
 * Set the pad's own colour or effect wherever it is kept. Kept aside, the
 * change shows when the host colour is released, and the copy in flash is
 * saved again once changes stop. Live, the colour saves nothing, as
 * zmk_rgb_underglow_set_hsb() does not (see mp_leds_save()), and the effect
 * schedules ZMK's save.
 */
int mp_host_set_own_color(struct zmk_led_hsb color);

int mp_host_set_own_effect(uint8_t effect);

#endif /* IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW) */

/*
 * Whether a layer id names a layer the keymap holds now, as opposed to a
 * reserved slot no layer has taken.
 */
bool mp_host_layer_in_keymap(uint8_t id);

/*
 * The features this build has, as the first byte of FEATURES: one bit for each
 * MP_FEATURE_* the build carries. GET_FEATURES answers with it, and a build
 * without any answers 0x00. Safe from any thread.
 */
uint8_t mp_host_features(void);

/*
 * The profile layer the pad is on for the host, STATE's layer_id, host.c: the
 * layer the host last set, or the one a &profile_layer key has swapped to
 * since. Default while nothing holds the pad, and during Hold for Default. Call
 * from the system work queue, where ZMK also runs key behaviors.
 */
zmk_keymap_layer_id_t mp_host_layer(void);

/*
 * Records that a &profile_layer key moved the profile layer to `id`, and why,
 * and sends STATE. The caller has already swapped the layers in the keymap.
 * Call from the system work queue.
 */
void mp_host_set_layer(zmk_keymap_layer_id_t id, enum mp_layer_change change);

/*
 * The switch SWITCH_PROFILE makes once profile_layers.c has checked the frame
 * and kept its group: SET_PROFILE with no flags, so through
 * zmk_keymap_layer_to(), with the pad's own colour back and the fallback window
 * open. Call from the system work queue.
 */
void mp_host_switch_profile(zmk_keymap_layer_id_t id);

#if IS_ENABLED(CONFIG_MINIMALPAD_PROFILE_LAYERS)

/*
 * The layers of the profile a host holds, src/host/profile_layers.c
 * (host-protocol.md, "Profile layers"). SWITCH_PROFILE sets the group;
 * &profile_layer keys move within it by swapping the profile layer, which
 * leaves every other active layer as it is. Call from a behavior or the system
 * work queue. One spinlock guards the group and the hold, and the keymap is
 * called with it released.
 *
 * Next, Go to and Hold for return 0, or -ENOENT when no group is held or the
 * target is not in it; then nothing changes. A swap to the layer the pad is
 * already on does nothing, except that a Next or Go to which ends a hold still
 * sends STATE with change MP_LAYER_CHANGE_KEY.
 */

/* Swaps to the layer after the current one in the group, wrapping round, or to
 * the group's first when the pad is on none of them, as during Hold for
 * Default. */
int mp_profile_layer_next(void);

/* Swaps to `target`, a layer in the group. */
int mp_profile_layer_go(zmk_keymap_layer_id_t target);

/*
 * Swaps to `target`, a layer in the group or Default, until the hold is
 * released. `token` names this hold for mp_profile_layer_release(); it is never
 * 0, and is set to 0 when the call fails. The newest hold is the current one
 * and keeps the layer the first one left.
 */
int mp_profile_layer_hold(zmk_keymap_layer_id_t target, uint32_t *token);

/* Swaps back to the layer the hold left, if `token` is still the current hold.
 * Otherwise, and for token 0, does nothing. */
void mp_profile_layer_release(uint32_t token);

/* Forgets the group and any hold, so a later release does nothing: SET_PROFILE,
 * a fallback and a release (HEARTBEAT 0, which falls back) call it. */
void mp_profile_layers_clear(void);

#else

static inline void mp_profile_layers_clear(void) {}

#endif /* IS_ENABLED(CONFIG_MINIMALPAD_PROFILE_LAYERS) */
