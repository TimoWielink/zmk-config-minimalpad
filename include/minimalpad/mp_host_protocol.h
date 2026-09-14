/*
 * Minimalpad host protocol v1 (draft)
 *
 * The contract between Minimalpad Studio on the Mac and the host module in the
 * pad's firmware. The prose spec is host-protocol.md next to this file; this
 * header is the firmware-side copy of it, and the Swift side in
 * Core/HostLink mirrors the same layout.
 *
 * Copy this file into zmk-config-minimalpad when the host module is written.
 * Keep the three copies in step: a change here is a change to the wire format.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

/* BIT(), __packed and BT_UUID_128_ENCODE() come from Zephyr, so the header
 * stands on its own wherever it is included first. */
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/util.h>
#include <zephyr/toolchain.h>

/* Protocol version carried in byte 0 of every frame. A receiver ignores a
 * frame whose version it does not know, and ignores an unknown command, so a
 * newer app talking to an older pad degrades instead of breaking. */
#define MP_HOST_PROTOCOL_VERSION 1

/* A frame never exceeds this, so it fits the smallest negotiated ATT MTU
 * (23 bytes, less 3 for the ATT header) without chunking. */
#define MP_HOST_FRAME_MAX 20

/* version, command, length */
#define MP_HOST_HEADER_LEN 3
#define MP_HOST_PAYLOAD_MAX (MP_HOST_FRAME_MAX - MP_HOST_HEADER_LEN)

/* Bluetooth: our own GATT service. In Zephyr, following ZMK's own pattern:
 *   BT_UUID_DECLARE_128(MP_HOST_UUID(0x00000000))
 */
#define MP_HOST_UUID(num) BT_UUID_128_ENCODE(num, 0xe7cd, 0x4a56, 0x8a90, 0x8fd4561e0b05)
#define MP_HOST_SERVICE_UUID MP_HOST_UUID(0x00000000)
#define MP_HOST_CMD_CHRC_UUID MP_HOST_UUID(0x00000001) /* host writes */
#define MP_HOST_EVT_CHRC_UUID MP_HOST_UUID(0x00000002) /* pad notifies */

/* Commands, host to pad. */
enum mp_host_command {
	MP_CMD_HELLO = 0x01,
	MP_CMD_SET_PROFILE = 0x02,
	MP_CMD_HEARTBEAT = 0x03,
	MP_CMD_SET_LEDS = 0x04,
	MP_CMD_ENTER_BOOTLOADER = 0x05,
	MP_CMD_GET_STATE = 0x06,
};

/* Events, pad to host. */
enum mp_host_event {
	MP_EVT_HELLO_ACK = 0x81,
	MP_EVT_KEY = 0x82,
	MP_EVT_DIAL = 0x83,
	MP_EVT_ACTION = 0x84,
	MP_EVT_STATE = 0x85,
	MP_EVT_FALLBACK = 0x86,
	MP_EVT_REJECTED = 0x87,
};

/* Guards MP_CMD_ENTER_BOOTLOADER, so a stray frame cannot reboot a pad. */
#define MP_BOOTLOADER_MAGIC 0xB0071E00u

/* Flags in mp_set_profile.flags. A flag set takes that part from the frame; a
 * flag clear puts back the pad's own colour and effect, or the keymap's own
 * dial bindings. Every SET_PROFILE describes the whole switch. */
#define MP_PROFILE_FLAG_SET_LEDS BIT(0)
#define MP_PROFILE_FLAG_SET_DIAL BIT(1)

/* Value ranges, checked on every frame whatever its flags say. */
#define MP_HUE_MAX 359
#define MP_SATURATION_MAX 100
#define MP_BRIGHTNESS_MAX 100

/* mp_heartbeat.timeout_seconds of 0: fall back to Default now, and report
 * MP_FALLBACK_RELEASED. */
#define MP_HEARTBEAT_RELEASE 0

/* Capability bits in mp_hello_ack.caps. */
#define MP_CAP_PROFILES BIT(0)
#define MP_CAP_DIAL_SWAP BIT(1)
#define MP_CAP_LEDS BIT(2)

/* Why the pad returned to Default, in mp_fallback.reason. */
enum mp_fallback_reason {
	MP_FALLBACK_HEARTBEAT_LOST = 0,
	MP_FALLBACK_USB_UNPLUGGED = 1,
	MP_FALLBACK_BT_PROFILE_CHANGED = 2,
	MP_FALLBACK_RELEASED = 3,
};

/* Where keys go, in mp_state_event.endpoint. */
enum mp_endpoint {
	MP_ENDPOINT_USB = 0,
	MP_ENDPOINT_BLUETOOTH = 1,
	MP_ENDPOINT_NONE = 0xFF,
};

/* Why the pad dropped a command, in mp_rejected_event.reason. The pad checks a
 * whole frame before applying any of it, so a rejected command changed nothing.
 * Unknown command ids are skipped silently, not rejected. */
enum mp_rejected_reason {
	MP_REJECTED_WRONG_LENGTH = 0,
	MP_REJECTED_OUT_OF_RANGE = 1,
	MP_REJECTED_UNKNOWN_LAYER = 2,
	MP_REJECTED_NOT_ACTIVE_HOST = 3,
	MP_REJECTED_WRONG_MAGIC = 4,
};

/* Every frame: version, command, payload length, then the payload.
 * Multi-byte fields are little-endian, which is native on both ends.
 *
 * The length byte is what lets a receiver skip a command it does not know
 * without losing whatever frame sits behind it in the same read. */
struct mp_host_frame_header {
	uint8_t version;
	uint8_t command;
	uint8_t length;
} __packed;

/* MP_CMD_SET_PROFILE: layer, LEDs and dial in one frame, so an app switch
 * cannot half-apply. 14 bytes of payload, 17 with the header. */
struct mp_set_profile {
	uint8_t layer_id; /* ZMK stable layer id, not the index */
	uint8_t flags;
	uint16_t hue; /* 0-359 */
	uint8_t saturation; /* 0-100 */
	uint8_t brightness; /* 0-100 */
	uint32_t dial_cw; /* ZMK keycode */
	uint32_t dial_ccw; /* ZMK keycode */
} __packed;

/* MP_CMD_HEARTBEAT: the pad returns to Default when this window passes. */
struct mp_heartbeat {
	uint8_t timeout_seconds;
} __packed;

/* MP_CMD_SET_LEDS: live preview while a colour is being picked. */
struct mp_set_leds {
	uint16_t hue;
	uint8_t saturation;
	uint8_t brightness;
} __packed;

/* MP_CMD_ENTER_BOOTLOADER */
struct mp_enter_bootloader {
	uint32_t magic; /* MP_BOOTLOADER_MAGIC */
} __packed;

/* MP_EVT_HELLO_ACK: what this pad is and what it can do. */
struct mp_hello_ack {
	uint8_t protocol;
	uint8_t fw_major;
	uint8_t fw_minor;
	uint8_t fw_patch;
	uint8_t caps;
	uint8_t layer_count; /* layers compiled in, including reserved slots */
	uint8_t free_layers; /* reserved slots still unused */
} __packed;

/* MP_EVT_KEY: drives "press a key to select it" in the configurator, so
 * layer_id is the layer whose binding answered this press, not whatever the
 * host last set. */
struct mp_key_event {
	uint8_t position;
	uint8_t pressed;
	uint8_t layer_id;
} __packed;

/* MP_EVT_DIAL */
struct mp_dial_event {
	int8_t direction;
	uint8_t detents;
} __packed;

/* MP_EVT_ACTION: sent by the &mac_action behavior. */
struct mp_action_event {
	uint8_t action_id;
	uint8_t pressed;
} __packed;

/* MP_EVT_STATE: feeds the menu bar pop-out.
 *
 * Two layer fields on purpose: the pop-out keeps saying "Figma" while a system
 * layer's hold key is down, and something still has to report the layer that
 * is actually winning. They differ only while a hold key is down. */
struct mp_state_event {
	uint8_t layer_id; /* the layer the host last set, so the profile */
	uint8_t top_layer_id; /* the highest active layer */
	uint8_t endpoint; /* enum mp_endpoint */
	uint8_t battery; /* percent, 0xFF when unknown */
	uint8_t leds_on;
} __packed;

/* MP_EVT_FALLBACK: the pad has returned to Default. */
struct mp_fallback_event {
	uint8_t reason; /* enum mp_fallback_reason */
} __packed;

/* MP_EVT_REJECTED: sent only to the host whose frame was dropped. */
struct mp_rejected_event {
	uint8_t command; /* the command id that was dropped */
	uint8_t reason; /* enum mp_rejected_reason */
} __packed;
