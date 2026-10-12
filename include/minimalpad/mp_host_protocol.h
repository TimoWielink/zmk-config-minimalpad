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
 *    behind MP_CAP_LAYER_DIALS.
 * 5: GET_LIGHTING, SET_LIGHTING and LIGHTING_STATE, the pad's own lighting read
 *    and set from a host, behind MP_CAP_LIGHTING.
 * 6: GET_FEATURES and FEATURES, behind MP_CAP_FEATURES, and a light for each
 *    layer and for each key, kept on the pad: SET_LAYER_LIGHT, GET_LAYER_LIGHT,
 *    SHOW_PAD_LIGHTING, SET_KEY_LIGHT, GET_KEY_LIGHTS, LAYER_LIGHT_STATE and
 *    KEY_LIGHTS_STATE, each behind a bit of FEATURES (MP_FEATURE_*).
 * 7: several layers for one profile, behind MP_FEATURE_PROFILE_LAYERS:
 *    SWITCH_PROFILE, which tells the pad the layers of the profile it holds, and
 *    STATE grew mp_state_event.change. The &profile_layer behavior moves between
 *    the layers on the pad itself. */
#define MP_HOST_PROTOCOL_VERSION 7

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
	MP_CMD_GET_LIGHTING = 0x09,
	MP_CMD_SET_LIGHTING = 0x0A,
	MP_CMD_GET_FEATURES = 0x0B,
	MP_CMD_SWITCH_PROFILE = 0x0C,
	MP_CMD_SET_LAYER_LIGHT = 0x0D,
	MP_CMD_GET_LAYER_LIGHT = 0x0E,
	MP_CMD_SHOW_PAD_LIGHTING = 0x0F,
	MP_CMD_SET_KEY_LIGHT = 0x10,
	MP_CMD_GET_KEY_LIGHTS = 0x11,
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
	MP_EVT_LIGHTING_STATE = 0x89,
	MP_EVT_FEATURES = 0x8A,
	MP_EVT_LAYER_LIGHT_STATE = 0x8B,
	MP_EVT_KEY_LIGHTS_STATE = 0x8C,
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
#define MP_CAP_LIGHTING BIT(6) /* GET_LIGHTING, SET_LIGHTING and LIGHTING_STATE */
#define MP_CAP_FEATURES BIT(7) /* GET_FEATURES and FEATURES */
/* Bit 7 is the last one, and HELLO_ACK cannot grow a second caps byte: Studio
 * 0.1.0 to 0.3.0 read HELLO_ACK as exactly seven bytes and fail the handshake on
 * a longer one. So it announces GET_FEATURES, which answers with a bitmap of up
 * to 128 features, and every feature after it is a bit of that bitmap. */

/* Feature bits in FEATURES: 1-16 bytes of bitmap, bit n of byte n/8 is feature
 * n. A pad sends as many bytes as its highest feature needs, and a feature past
 * the end of the bitmap is one the pad does not have.
 *
 * - PROFILE_LAYERS: SWITCH_PROFILE, the &profile_layer behavior, and the change
 *   byte on the end of STATE. Set by firmware 0.9.0, and only by a build with
 *   the &profile_layer behavior too: the bit promises the key.
 * - LAYER_LIGHTS: SET_LAYER_LIGHT, GET_LAYER_LIGHT, LAYER_LIGHT_STATE and
 *   SHOW_PAD_LIGHTING.
 * - KEY_LIGHTS: SET_KEY_LIGHT, GET_KEY_LIGHTS and KEY_LIGHTS_STATE. */
#define MP_FEATURE_PROFILE_LAYERS 0 /* SWITCH_PROFILE, &profile_layer and STATE's change */
#define MP_FEATURE_LAYER_LIGHTS 1
#define MP_FEATURE_KEY_LIGHTS 2

/* Profile layers (host-protocol.md, "Profile layers"): a profile can have several
 * layers, and the pad moves between them itself. SWITCH_PROFILE hands the pad the
 * profile's layers, its group, in the order Next goes through them. The pad keeps
 * the group in RAM only while it holds the profile, and a group is at most this
 * many layer ids. */
#define MP_SWITCH_PROFILE_GROUP_MAX 15

/* Why the layer changed, in mp_state_event.change: it explains a change of
 * layer_id. A value above MP_LAYER_CHANGE_HOLD_ENDED is one a later revision adds,
 * and a host reads it as MP_LAYER_CHANGE_HOST. */
enum mp_layer_change {
	MP_LAYER_CHANGE_HOST = 0, /* the host, or anything else */
	MP_LAYER_CHANGE_KEY = 1, /* a Next layer or Go to key */
	MP_LAYER_CHANGE_HOLD_BEGAN = 2, /* a Hold for key went down */
	MP_LAYER_CHANGE_HOLD_ENDED = 3, /* a Hold for key came up */
};

/* What a light does, in mp_light.kind. A light is what the LEDs of a layer, or of
 * one key on a layer, show (host-protocol.md, "Layer and key lights"). There is
 * no brightness: a colour is drawn at the pad's own brightness. Inherit and Off
 * carry a hue and a saturation of 0, in what a host sends and in every answer. */
#define MP_LIGHT_INHERIT 0 /* passes on: a key to its layer, a layer to the one below */
#define MP_LIGHT_COLOUR 1 /* a hue and a saturation */
#define MP_LIGHT_OFF 2 /* dark, whatever lies below */

/* The keys on the grid are positions 0-15. The dial button, position 16, has no
 * LED and is never in a mask. A layer's key lights are read in six pages, named
 * by the position of their first key: 0, 3, 6, 9, 12 and 15. The last holds key
 * 15 alone. */

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

/* The pad's own lighting, read with GET_LIGHTING and set with SET_LIGHTING
 * (host-protocol.md, "Lighting"): the underglow the pad shows without Studio,
 * kept in its settings like its lighting keys' changes. Not a profile's colour,
 * which stays temporary and goes through SET_PROFILE and SET_LEDS. */

/* mp_set_lighting.fields: which parts of the frame the pad applies. A part left
 * out stays as it is. Every field is range-checked whatever these say, so a host
 * fills the parts it leaves out from the last LIGHTING_STATE. Bits 5-7 are for
 * fields a later revision adds on the end: a host sends them clear, and a pad
 * ignores the bits it does not know. */
#define MP_LIGHTING_FIELD_ON BIT(0)
#define MP_LIGHTING_FIELD_COLOR BIT(1) /* hue and saturation together */
#define MP_LIGHTING_FIELD_BRIGHTNESS BIT(2)
#define MP_LIGHTING_FIELD_EFFECT BIT(3)
#define MP_LIGHTING_FIELD_SPEED BIT(4)

/* The effects, in mp_set_lighting.effect and mp_lighting_state.effect. The
 * protocol names them because ZMK keeps its own list private; the ZMK revision
 * config/west.yml pins numbers them the same way. A pad reports how many it has
 * in mp_lighting_state.effect_count and accepts only an effect below that. */
enum mp_lighting_effect {
	MP_LIGHTING_EFFECT_SOLID = 0, /* hue, saturation and brightness as set */
	MP_LIGHTING_EFFECT_BREATHE = 1, /* hue and saturation; brightness pulses */
	MP_LIGHTING_EFFECT_SPECTRUM = 2, /* saturation and brightness; hue cycles */
	MP_LIGHTING_EFFECT_SWIRL = 3, /* saturation and brightness; hue moves across */
};

#define MP_LIGHTING_EFFECT_COUNT 4

/* Animation speed, slowest to fastest. Solid does not use it. */
#define MP_LIGHTING_SPEED_MIN 1
#define MP_LIGHTING_SPEED_MAX 5

/* mp_lighting_state.flags. Bits 4-7 are sent clear, and a host ignores the bits
 * it does not know. */
#define MP_LIGHTING_FLAG_HOST_COLOR BIT(0) /* a host colour shows in place of the pad's own */
#define MP_LIGHTING_FLAG_RESTING BIT(1) /* on, but dimming or off because the pad is idle */
#define MP_LIGHTING_FLAG_SPEED_EXACT BIT(2) /* speed is the one the lights run at */
#define MP_LIGHTING_FLAG_SET_ANSWER BIT(3) /* this frame answers a SET_LIGHTING */

/* A SET_LIGHTING that leaves the lights on wakes them from resting and holds
 * off the idle fade until this many seconds after the last one. Host commands
 * are not activity to ZMK, so without it the lights would fade mid-edit. */
#define MP_LIGHTING_HOLD_SECONDS 30

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

/* MP_CMD_SWITCH_PROFILE: SET_PROFILE with no flags, plus the profile's layers, in
 * one frame, so a switch cannot half-apply. layer_id is the layer to switch to and
 * layer_ids are all of the profile's layers in the order Next goes through them,
 * layer_id among them; none may be Default or appear twice, and each must be a
 * layer in the keymap. Only count ids are sent, so the payload is 2 + count bytes:
 * 3 at the least, and sizeof(struct mp_switch_profile), 17, at the most. A later
 * revision may add fields after the ids. The pad answers with STATE, or with
 * REJECTED: a count above MP_SWITCH_PROFILE_GROUP_MAX needs more than a frame
 * holds, so it is MP_REJECTED_WRONG_LENGTH, as a frame short of its count is.
 * Only the active host may send it, and only to a pad with
 * MP_FEATURE_PROFILE_LAYERS. */
struct mp_switch_profile {
	uint8_t layer_id; /* ZMK stable layer id, not the index */
	uint8_t count; /* 1-MP_SWITCH_PROFILE_GROUP_MAX */
	uint8_t layer_ids[MP_SWITCH_PROFILE_GROUP_MAX]; /* the first count are sent */
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

/* MP_CMD_SET_LIGHTING: changes the pad's own lighting, which it saves as it
 * saves a change made with its lighting keys. Only the active host may send it,
 * and it arms no fallback. The pad answers every host listening with
 * LIGHTING_STATE, MP_LIGHTING_FLAG_SET_ANSWER set. GET_LIGHTING has no payload.
 * 8 bytes of payload. */
struct mp_set_lighting {
	uint8_t fields; /* MP_LIGHTING_FIELD_* */
	uint8_t on; /* 0 off, 1 on */
	uint16_t hue; /* 0-359 */
	uint8_t saturation; /* 0-100 */
	uint8_t brightness; /* 0-100; 0 is dark, not off */
	uint8_t effect; /* enum mp_lighting_effect, below effect_count */
	uint8_t speed; /* MP_LIGHTING_SPEED_MIN-MP_LIGHTING_SPEED_MAX */
} __packed;

/* One light, in four bytes. Every light on the wire has this layout. */
struct mp_light {
	uint8_t kind; /* MP_LIGHT_* */
	uint16_t hue; /* 0-359, 0 unless MP_LIGHT_COLOUR */
	uint8_t saturation; /* 0-100, 0 unless MP_LIGHT_COLOUR */
} __packed;

/* MP_CMD_SET_LAYER_LIGHT: a layer's light, kept on the pad until changed. Default
 * cannot have one. The pad answers the sender with LAYER_LIGHT_STATE. 5 bytes of
 * payload. */
struct mp_set_layer_light {
	uint8_t layer_id;
	struct mp_light light;
} __packed;

/* MP_CMD_GET_LAYER_LIGHT: the pad answers with LAYER_LIGHT_STATE. */
struct mp_get_layer_light {
	uint8_t layer_id;
} __packed;

/* MP_CMD_SHOW_PAD_LIGHTING: shows the pad's own lighting in place of layer and
 * key lights for this many seconds from now. 0 stops. Nothing answers it but a
 * REJECTED. */
struct mp_show_pad_lighting {
	uint8_t seconds;
} __packed;

/* MP_CMD_SET_KEY_LIGHT: one light for every key in keys, on one layer, in one
 * frame. Bit n of keys is key position n, 0-15; 0 is out of range. The pad
 * answers the sender with a KEY_LIGHTS_STATE for every page that holds a key in
 * keys. 7 bytes of payload. */
struct mp_set_key_light {
	uint8_t layer_id;
	uint16_t keys;
	struct mp_light light;
} __packed;

/* MP_CMD_GET_KEY_LIGHTS: the lights of one page of a layer's keys. The pad
 * answers with one KEY_LIGHTS_STATE. */
struct mp_get_key_lights {
	uint8_t layer_id;
	uint8_t first; /* 0, 3, 6, 9, 12 or 15 */
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
	/* 0 when switched off, by hand or at the end of the idle fade. LIGHTING_STATE
	 * tells the two apart. */
	uint8_t leds_on;
	/* Added in revision 2, so it is sent only when caps has MP_CAP_BT_PROFILE.
	 * A host that predates it reads the five bytes it knows and ignores this
	 * one, which is why the field goes on the end and not beside endpoint. */
	uint8_t bt_profile; /* 0-4, the &bt BT_SEL slot, or MP_BT_PROFILE_NONE */
	/* Added in revision 7. Every build of firmware 0.9.0 and later sends it,
	 * MP_LAYER_CHANGE_HOST throughout on one without MP_FEATURE_PROFILE_LAYERS,
	 * and after bt_profile: a pad that sends it always sends bt_profile before it,
	 * MP_BT_PROFILE_NONE if it has no slot. It explains a change of layer_id, and
	 * stays until layer_id next moves, so a STATE sent for something else repeats
	 * it. Older firmware sends six bytes or five, and a host reads that as nothing
	 * known about why, not as MP_LAYER_CHANGE_HOST. */
	uint8_t change; /* enum mp_layer_change */
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

/* MP_EVT_LIGHTING_STATE: the pad's own lighting. The answer to GET_LIGHTING,
 * sent to the host that asked, and to SET_LIGHTING, sent to every host
 * listening; the pad also volunteers it to every host listening whenever its
 * lighting changes. Colour, brightness and effect are the pad's own even while
 * a host colour shows in their place. Bytes 1-7 are laid out as in
 * mp_set_lighting. 9 bytes of payload; a later revision adds fields to the end,
 * so a host reads these nine and ignores what trails them. */
struct mp_lighting_state {
	uint8_t flags; /* MP_LIGHTING_FLAG_* */
	uint8_t on; /* 1 on, resting included; 0 switched off */
	uint16_t hue; /* 0-359 */
	uint8_t saturation; /* 0-100 */
	uint8_t brightness; /* 0-100, as when awake, never a step of the idle fade */
	uint8_t effect; /* enum mp_lighting_effect */
	uint8_t speed; /* 1-5, exact only with MP_LIGHTING_FLAG_SPEED_EXACT */
	uint8_t effect_count; /* MP_LIGHTING_EFFECT_COUNT on this firmware */
} __packed;

/* MP_EVT_FEATURES: which features the pad has, the answer to GET_FEATURES, sent
 * only to the host that asked. It is 1-16 bytes of bitmap with no struct: bit n
 * of byte n/8 is feature n (MP_FEATURE_*), and the payload is as long as the pad's
 * highest feature needs. A host reads up to 16 bytes and ignores what trails. */

/* MP_EVT_LAYER_LIGHT_STATE: one layer's light, the answer to SET_LAYER_LIGHT and
 * GET_LAYER_LIGHT, sent only to the host that asked. Default answers
 * MP_LIGHT_INHERIT. 5 bytes of payload; a later revision adds fields to the end,
 * so a host reads these five and ignores what trails them. */
struct mp_layer_light_state {
	uint8_t layer_id;
	struct mp_light light;
} __packed;

/* MP_EVT_KEY_LIGHTS_STATE: the lights of up to three keys on one layer, the
 * answer to SET_KEY_LIGHT and GET_KEY_LIGHTS, sent only to the host that asked.
 * lights[0] is key position first, lights[1] is first + 1, and so on; count is
 * 1-3 and first + count never passes 16. 3 + 4 * count bytes of payload, so 15
 * at most. A host reads count lights and ignores what trails them. */
struct mp_key_lights_state {
	uint8_t layer_id;
	uint8_t first;
	uint8_t count;
	struct mp_light lights[3]; /* the first count are sent */
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
