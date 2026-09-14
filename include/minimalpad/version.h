/*
 * Minimalpad firmware version
 *
 * MINIMALPAD_VERSION_MAJOR, _MINOR and _PATCH come from VERSION at the root of
 * this repo, which CMakeLists.txt reads at build time. The GitHub Actions build
 * is named after the same file, so the firmware reports the version its build
 * carries.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#if !defined(MINIMALPAD_VERSION_MAJOR) || !defined(MINIMALPAD_VERSION_MINOR) ||                    \
    !defined(MINIMALPAD_VERSION_PATCH)
#error "The MINIMALPAD_VERSION_* definitions come from VERSION, through CMakeLists.txt"
#endif
