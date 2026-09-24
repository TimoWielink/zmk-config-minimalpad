/*
 * Minimalpad host module
 *
 * What the frame reader (frame.c), the core (host.c) and the transports share.
 * The wire format itself lives in mp_host_protocol.h, a verbatim copy of
 * minimalpad-studio-mac/docs/protocol/mp_host_protocol.h: change it there.
 *
 * Threading: the core runs on the system work queue, the queue ZMK processes
 * key presses on. Transports hand received bytes to that queue before calling
 * mp_host_reader_feed(), and only mp_host_host_arrived() may be called from
 * elsewhere.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <minimalpad/mp_host_protocol.h>

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
 * already matches the command's struct in mp_host_protocol.h (none for HELLO
 * and GET_STATE). Replies go back through `from`.
 */
void mp_host_handle_hello(const struct mp_host_transport *from, const uint8_t *payload);
void mp_host_handle_set_profile(const struct mp_host_transport *from, const uint8_t *payload);
void mp_host_handle_heartbeat(const struct mp_host_transport *from, const uint8_t *payload);
void mp_host_handle_set_leds(const struct mp_host_transport *from, const uint8_t *payload);
void mp_host_handle_enter_bootloader(const struct mp_host_transport *from, const uint8_t *payload);
void mp_host_handle_get_state(const struct mp_host_transport *from, const uint8_t *payload);

/*
 * Answers a dropped command with REJECTED, to the host that sent it only. A
 * rejected command has changed nothing: every check runs before any part of it
 * is applied (host-protocol.md, "Validation").
 */
void mp_host_reject(const struct mp_host_transport *from, uint8_t command,
                    enum mp_rejected_reason reason);

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

/*
 * The dial values the last SET_PROFILE carried with its dial flag set, which
 * &host_dial uses (src/behaviors/host_dial.c). Returns false when the pad's
 * own dial binding applies. Call from the system work queue.
 */
bool mp_host_dial_keycodes(uint32_t *clockwise, uint32_t *counter_clockwise);
