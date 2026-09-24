/*
 * Minimalpad host protocol v1
 *
 * The contract between Minimalpad Studio on the Mac and the host module in the
 * pad's firmware. The prose spec is host-protocol.md next to this file; this
 * header is the firmware-side copy of it, and the Swift side in
 * Core/HostLink mirrors the same layout.
 *
 * This file is mirrored in zmk-config-minimalpad for the firmware build.
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

/* The version byte in byte 0 of every frame. It is frozen at 1 and must never
 * be raised. Both ends compare it for equality and ignore a frame whose version
 * they do not know, so a pad that shipped a 2 here would go silent to every
 * Studio already installed, and a newer Studio would look like a dead pad to an
 * older one. There is no version of this protocol in which raising it helps.
 *
 * The protocol grows instead by adding fields to the end of a payload and
 * announcing them with a capability bit, which both ends already tolerate: a
 * decoder reads the fields it knows and ignores whatever trails them. */
#define MP_HOST_FRAME_VERSION 1

/* Which revision of this contract the firmware implements, reported in
 * mp_hello_ack.proto and nowhere else. This is the number to raise when a field
 * or a command is added, and Studio compares it with >= rather than ==, so a
 * newer pad still talks to an older Mac.
 *
 * 1: the original six commands and seven events.
 * 2: STATE grew mp_state_event.bt_profile, behind MP_CAP_BT_PROFILE.
 * 3: the pad sends ACTION_EVENT for &mac_action keys, behind MP_CAP_MAC_ACTIONS.
 * 4: SET_DIAL, GET_DIAL and DIAL_STATE, a dial for each layer kept on the pad,
 *    behind MP_CAP_LAYER_DIALS. */
#define MP_HOST_PROTOCOL_VERSION 4

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
	MP_CMD_SET_DIAL = 0x07,
	MP_CMD_GET_DIAL = 0x08,
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
	MP_EVT_DIAL_STATE = 0x88,
};

/* Guards MP_CMD_ENTER_BOOTLOADER, so a stray frame cannot reboot a pad. */
#define MP_BOOTLOADER_MAGIC 0xB0071E00u

/* Flags in mp_set_profile.flags. A flag set takes that part from the frame; a
 * flag clear puts back the pad's own colour and effect. Every SET_PROFILE
 * describes the whole switch. The dial flag was reserved for a dial set with
 * the switch; no firmware acts on it, since each layer keeps its own dial
 * (SET_DIAL). */
#define MP_PROFILE_FLAG_SET_LEDS BIT(0)
#define MP_PROFILE_FLAG_SET_DIAL BIT(1)

/* Value ranges, checked on every frame whatever its flags say. */
#define MP_HUE_MAX 359
#define MP_SATURATION_MAX 100
#define MP_BRIGHTNESS_MAX 100

/* mp_heartbeat.timeout_seconds of 0: fall back to Default now, and report
 * MP_FALLBACK_RELEASED. */
#define MP_HEARTBEAT_RELEASE 0

/* Capability bits in mp_hello_ack.caps. A bit is how a field or a feature added
 * after revision 1 announces itself, so a host knows whether a pad will send it
 * rather than guessing from a version number. */
#define MP_CAP_PROFILES BIT(0)
#define MP_CAP_DIAL_SWAP BIT(1) /* never set: see MP_CAP_LAYER_DIALS */
#define MP_CAP_LEDS BIT(2)
#define MP_CAP_BT_PROFILE BIT(3) /* STATE carries bt_profile */
#define MP_CAP_MAC_ACTIONS BIT(4) /* &mac_action keys send ACTION_EVENT */
#define MP_CAP_LAYER_DIALS BIT(5) /* SET_DIAL, GET_DIAL and DIAL_STATE */

/* What one dial value does, in SET_DIAL and DIAL_STATE, one per direction of
 * turn. It is a ZMK keycode, so modifier flags in bits 31-24, a HID usage page
 * in bits 23-16 and a usage id in bits 15-0, and the page says which kind:
 *
 * - The keyboard page (0x07) or the consumer page (0x0C): the pad taps that
 *   key with those modifiers, as &kp would, once per step of the dial.
 * - MP_DIAL_PAGE_SCROLL: the pad scrolls one wheel step per step of the dial,
 *   in the direction the usage id names, with those modifiers held.
 * - MP_DIAL_PAGE_LIGHTS: the pad changes its underglow one step, as the
 *   &rgb_ug key the usage id names would. Modifiers are ignored.
 * - 0: the dial does nothing that way.
 *
 * Page 0x01 is HID's Generic Desktop, where the wheel lives, and 0x08 is HID's
 * LED page. &kp never sends either, so no key collides with them.
 * include/dt-bindings/minimalpad/dial.h spells the same values for a keymap. */
#define MP_DIAL_PAGE_KEYBOARD 0x07
#define MP_DIAL_PAGE_CONSUMER 0x0C
#define MP_DIAL_PAGE_SCROLL 0x01
#define MP_DIAL_PAGE_LIGHTS 0x08

enum mp_dial_scroll {
	MP_DIAL_SCROLL_UP = 1,
	MP_DIAL_SCROLL_DOWN = 2,
	MP_DIAL_SCROLL_LEFT = 3,
	MP_DIAL_SCROLL_RIGHT = 4,
};

enum mp_dial_lights {
	MP_DIAL_LIGHTS_BRIGHTER = 1,
	MP_DIAL_LIGHTS_DIMMER = 2,
	MP_DIAL_LIGHTS_HUE_UP = 3,
	MP_DIAL_LIGHTS_HUE_DOWN = 4,
	MP_DIAL_LIGHTS_SATURATION_UP = 5,
	MP_DIAL_LIGHTS_SATURATION_DOWN = 6,
	MP_DIAL_LIGHTS_FASTER = 7,
	MP_DIAL_LIGHTS_SLOWER = 8,
};

#define MP_DIAL_MODIFIERS(value) ((uint8_t)((value) >> 24))
#define MP_DIAL_PAGE(value) ((uint8_t)((value) >> 16))
#define MP_DIAL_USAGE(value) ((uint16_t)(value))

/* mp_set_dial.flags: set, the layer's dial is the frame's values; clear, the
 * layer's dial goes back to its keymap's, or to the layer below's. */
#define MP_DIAL_FLAG_SET BIT(0)

/* Where a layer's dial comes from, in mp_dial_state.source. */
enum mp_dial_source {
	MP_DIAL_SOURCE_SET = 0, /* set with SET_DIAL, kept on the pad */
	MP_DIAL_SOURCE_KEYMAP = 1, /* the keymap's own &host_dial values */
	MP_DIAL_SOURCE_BELOW = 2, /* none: a turn goes to the layer below */
	MP_DIAL_SOURCE_FIXED = 3, /* another behavior the host cannot change */
};

/* Why the pad returned to Default, in mp_fallback.reason. */
enum mp_fallback_reason {
	MP_FALLBACK_HEARTBEAT_LOST = 0,
	MP_FALLBACK_USB_UNPLUGGED = 1,
	MP_FALLBACK_BT_PROFILE_CHANGED = 2,
	MP_FALLBACK_RELEASED = 3,
};

/* No Bluetooth slot is selected, or the firmware cannot say which is, in
 * mp_state_event.bt_profile. Slots themselves are 0-based, the same numbering
 * &bt BT_SEL takes in the keymap. */
#define MP_BT_PROFILE_NONE 0xFF

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
	uint32_t dial_cw; /* ignored: a layer's dial is set with SET_DIAL */
	uint32_t dial_ccw; /* ignored */
} __packed;

/* MP_CMD_SET_DIAL: what turning the dial does on one layer, kept on the pad
 * until changed. The pad answers with DIAL_STATE. 10 bytes of payload. */
struct mp_set_dial {
	uint8_t layer_id;
	uint8_t flags; /* MP_DIAL_FLAG_SET */
	uint32_t cw; /* a dial value */
	uint32_t ccw; /* a dial value */
} __packed;

/* MP_CMD_GET_DIAL: the pad answers with DIAL_STATE. */
struct mp_get_dial {
	uint8_t layer_id;
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

/* MP_EVT_ACTION: sent by the &mac_action behavior on the way down and on the
 * way up, to every host listening. The pad cannot do the work: action_id is the
 * key's parameter, and Studio does whatever it saved under that id for this pad.
 * Sent only by firmware that advertises MP_CAP_MAC_ACTIONS. */
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
	/* Added in revision 2, so it is sent only when caps has MP_CAP_BT_PROFILE.
	 * A host that predates it reads the five bytes it knows and ignores this
	 * one, which is why the field goes on the end and not beside endpoint. */
	uint8_t bt_profile; /* 0-4, the &bt BT_SEL slot, or MP_BT_PROFILE_NONE */
} __packed;

/* MP_EVT_DIAL_STATE: one layer's dial, the answer to SET_DIAL and GET_DIAL,
 * sent only to the host that asked. */
struct mp_dial_state {
	uint8_t layer_id;
	uint8_t source; /* enum mp_dial_source */
	uint32_t cw; /* the values that apply, 0 for MP_DIAL_SOURCE_BELOW and _FIXED */
	uint32_t ccw;
	uint8_t keymap_source; /* where it would come from with nothing set: _KEYMAP,
				* _BELOW or _FIXED, so a host knows what clearing does */
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
