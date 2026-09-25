/*
 * Dial values for &host_dial's clockwise and counter-clockwise properties in a
 * keymap. The same numbers as MP_DIAL_PAGE_* in mp_host_protocol.h, which the
 * firmware checks against these at build time. A key is any &kp keycode, such
 * as C_VOL_UP or LG(EQUAL); these name what is not a key.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#define DIAL_NOTHING 0

#define DIAL_SCROLL_UP 0x00010001
#define DIAL_SCROLL_DOWN 0x00010002
#define DIAL_SCROLL_LEFT 0x00010003
#define DIAL_SCROLL_RIGHT 0x00010004

#define DIAL_LIGHTS_BRIGHTER 0x00080001
#define DIAL_LIGHTS_DIMMER 0x00080002
#define DIAL_LIGHTS_HUE_UP 0x00080003
#define DIAL_LIGHTS_HUE_DOWN 0x00080004
#define DIAL_LIGHTS_SATURATION_UP 0x00080005
#define DIAL_LIGHTS_SATURATION_DOWN 0x00080006
#define DIAL_LIGHTS_FASTER 0x00080007
#define DIAL_LIGHTS_SLOWER 0x00080008
