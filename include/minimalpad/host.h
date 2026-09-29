/*
 * Minimalpad host module
 *
 * What the frame reader (frame.c), the core (host.c, with dial.c and
 * lighting.c) and the transports share. The wire format itself lives in
 * mp_host_protocol.h, a verbatim copy of
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
 * GET_STATE and GET_LIGHTING). Replies go back through `from`.
 */
void mp_host_handle_hello(const struct mp_host_transport *from, const uint8_t *payload);
void mp_host_handle_set_profile(const struct mp_host_transport *from, const uint8_t *payload);
void mp_host_handle_heartbeat(const struct mp_host_transport *from, const uint8_t *payload);
void mp_host_handle_set_leds(const struct mp_host_transport *from, const uint8_t *payload);
void mp_host_handle_enter_bootloader(const struct mp_host_transport *from, const uint8_t *payload);
void mp_host_handle_get_state(const struct mp_host_transport *from, const uint8_t *payload);
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
