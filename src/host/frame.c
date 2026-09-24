/*
 * Minimalpad host module: frames in
 *
 * Finds frames in whatever bytes a transport hands over and dispatches the ones
 * this firmware understands. Nothing here knows which transport it serves, so
 * the USB channel can use it unchanged.
 *
 * The rules are host-protocol.md, "Frame": the three header bytes never move, a
 * length above 17 is not a frame, and a frame with an unknown version or
 * command is skipped by its length so the frame behind it still gets read. A
 * payload longer than the command's own is a newer Studio's, and the command
 * runs on the fields this firmware knows.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <minimalpad/host.h>

LOG_MODULE_DECLARE(minimalpad_host, CONFIG_MINIMALPAD_HOST_LOG_LEVEL);

typedef void (*mp_host_handler_t)(const struct mp_host_transport *from, const uint8_t *payload);

struct command {
    uint8_t id;
    /* The smallest payload this command may carry. The protocol grows by adding
     * fields to the end of one, so a longer payload is the same command from a
     * newer Studio and its extra bytes are ignored. */
    uint8_t min_payload_len;
    /* Changes the pad, so only the active host may send it. */
    bool changes_pad;
    mp_host_handler_t handle;
};

/*
 * host-protocol.md, "Commands, host to pad". A command added to the protocol
 * gets a line here and a handler in host.c.
 *
 * Payload sizes come from the header's structs, and the asserts below pin those
 * to the spec's numbers, so a header that drifts from the spec fails the build
 * instead of every frame on the pad.
 *
 * HELLO, GET_STATE and GET_DIAL change nothing, so any bonded host gets an answer
 * (host-protocol.md, "Which host is in charge").
 */
static const struct command commands[] = {
    {MP_CMD_HELLO, 0, false, mp_host_handle_hello},
    {MP_CMD_SET_PROFILE, sizeof(struct mp_set_profile), true, mp_host_handle_set_profile},
    {MP_CMD_HEARTBEAT, sizeof(struct mp_heartbeat), true, mp_host_handle_heartbeat},
    {MP_CMD_SET_LEDS, sizeof(struct mp_set_leds), true, mp_host_handle_set_leds},
    {MP_CMD_ENTER_BOOTLOADER, sizeof(struct mp_enter_bootloader), true,
     mp_host_handle_enter_bootloader},
    {MP_CMD_GET_STATE, 0, false, mp_host_handle_get_state},
#if IS_ENABLED(CONFIG_MINIMALPAD_HOST_DIAL)
    {MP_CMD_SET_DIAL, sizeof(struct mp_set_dial), true, mp_host_handle_set_dial},
    {MP_CMD_GET_DIAL, sizeof(struct mp_get_dial), false, mp_host_handle_get_dial},
#endif
};

BUILD_ASSERT(sizeof(struct mp_host_frame_header) == MP_HOST_HEADER_LEN);
BUILD_ASSERT(sizeof(struct mp_set_profile) == 14, "SET_PROFILE carries 14 payload bytes");
BUILD_ASSERT(sizeof(struct mp_heartbeat) == 1, "HEARTBEAT carries 1 payload byte");
BUILD_ASSERT(sizeof(struct mp_set_leds) == 4, "SET_LEDS carries 4 payload bytes");
BUILD_ASSERT(sizeof(struct mp_enter_bootloader) == 4, "ENTER_BOOTLOADER carries 4 payload bytes");

static const struct command *find_command(uint8_t id) {
    for (size_t i = 0; i < ARRAY_SIZE(commands); i++) {
        if (commands[i].id == id) {
            return &commands[i];
        }
    }

    return NULL;
}

static void dispatch(struct mp_host_reader *reader, const struct mp_host_transport *from) {
    struct mp_host_frame_header header;
    memcpy(&header, reader->buf, sizeof(header));

    if (header.version != MP_HOST_FRAME_VERSION) {
        // The version byte is frozen at 1, so anything else is not a frame of this protocol.
        // It has already been measured by its length, which is safe because the header is the
        // same whatever follows it.
        reader->skipped++;
        LOG_DBG("Skipping a version %d frame", header.version);
        return;
    }

    const struct command *command = find_command(header.command);
    if (!command) {
        // A command newer than this firmware, or an event sent the wrong way. Ignoring it is
        // what lets a newer Studio degrade on an older pad instead of breaking.
        reader->skipped++;
        LOG_DBG("Skipping unknown command 0x%02x", header.command);
        return;
    }

    if (header.length < command->min_payload_len) {
        // Well framed, still malformed: too short to hold this command. Too long is not an
        // error, because that is how the protocol grows; the handler reads the fields it knows
        // and leaves the rest.
        reader->rejected++;
        LOG_WRN("Rejecting command 0x%02x with %d payload bytes, expected at least %d",
                header.command, header.length, command->min_payload_len);
        mp_host_reject(from, header.command, MP_REJECTED_WRONG_LENGTH);
        return;
    }

    if (command->changes_pad && !from->sender_may_command()) {
        // After a profile change the previous host can stay connected, and its app switches
        // must not change layers under the new one (ADR 0005).
        reader->rejected++;
        LOG_DBG("Rejecting command 0x%02x from a host that is not the active one",
                header.command);
        mp_host_reject(from, header.command, MP_REJECTED_NOT_ACTIVE_HOST);
        return;
    }

    command->handle(from, reader->buf + MP_HOST_HEADER_LEN);
}

void mp_host_reader_feed(struct mp_host_reader *reader, const struct mp_host_transport *from,
                         const uint8_t *data, size_t len) {
    while (len > 0) {
        // Take the header first, then as much payload as the header declares.
        size_t want = reader->len < MP_HOST_HEADER_LEN
                          ? MP_HOST_HEADER_LEN - reader->len
                          : MP_HOST_HEADER_LEN + reader->buf[2] - reader->len;
        size_t take = MIN(want, len);

        memcpy(reader->buf + reader->len, data, take);
        reader->len += take;
        data += take;
        len -= take;

        if (reader->len < MP_HOST_HEADER_LEN) {
            continue;
        }

        uint8_t payload_len = reader->buf[2];

        if (payload_len > MP_HOST_PAYLOAD_MAX) {
            // "A length above 17 is not a frame": the reader has lost its place, and a length it
            // cannot trust will not find it again. Drop the rest of this read and start clean.
            reader->dropped++;
            LOG_WRN("Dropping a read that declares a %d byte payload", payload_len);
            reader->len = 0;
            return;
        }

        if (reader->len == MP_HOST_HEADER_LEN + payload_len) {
            dispatch(reader, from);
            reader->len = 0;
        }
    }
}

void mp_host_reader_reset(struct mp_host_reader *reader) {
    if (reader->len > 0) {
        reader->dropped++;
        LOG_WRN("Dropping %d bytes of an unfinished frame", reader->len);
        reader->len = 0;
    }
}
