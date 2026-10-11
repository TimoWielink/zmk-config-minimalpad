/*
 * The first parameter of &profile_layer in a keymap: what the key does within
 * the layers of the profile MinimalPad Studio holds the pad on. The second is
 * a layer id, such as `&profile_layer PL_GO 5`, and 0 for PL_NEXT. These are
 * the values src/behaviors/profile_layer.c names to ZMK Studio, and Studio for
 * Mac reads them from there.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

/* Swap to the next layer of the profile, wrapping round. */
#define PL_NEXT 0

/* Swap to a layer of the profile. */
#define PL_GO 1

/* Swap to a layer of the profile, or Default, until the key is released. */
#define PL_HOLD 2
