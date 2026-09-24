# Minimalpad firmware additions: the host module, the device channel Minimalpad
# Studio uses for what ZMK Studio RPC cannot do, a corrected copy of ZMK Studio's
# Bluetooth transport, and the underglow's fade on idle. See Kconfig for what
# each is.
#
# The sources join ZMK's own `app` target rather than a library of their own,
# because ZMK keeps its headers (zmk/keymap.h and friends) private to `app`.

# The firmware's version, MAJOR.MINOR.PATCH, is kept in VERSION at the root of
# this repo. The firmware reports it and the GitHub Actions build is named after
# it (docs/releasing.md). A change to VERSION reconfigures the build.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/VERSION)
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/VERSION MINIMALPAD_VERSION LIMIT_COUNT 1)
string(STRIP "${MINIMALPAD_VERSION}" MINIMALPAD_VERSION)
string(REGEX MATCH "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$" version_parts
  "${MINIMALPAD_VERSION}")
if(NOT version_parts)
  message(FATAL_ERROR "VERSION must be MAJOR.MINOR.PATCH, such as 1.4.2, not '${MINIMALPAD_VERSION}'")
endif()
set(MINIMALPAD_VERSION_MAJOR ${CMAKE_MATCH_1})
set(MINIMALPAD_VERSION_MINOR ${CMAKE_MATCH_2})
set(MINIMALPAD_VERSION_PATCH ${CMAKE_MATCH_3})
foreach(part MAJOR MINOR PATCH)
  # HELLO_ACK carries each part in one byte.
  if(MINIMALPAD_VERSION_${part} GREATER 255)
    message(FATAL_ERROR "VERSION ${MINIMALPAD_VERSION}: each part must be 255 or less")
  endif()
endforeach()
message(STATUS "Minimalpad firmware ${MINIMALPAD_VERSION}")

if(CONFIG_MINIMALPAD_HOST OR CONFIG_MINIMALPAD_LEDS_IDLE_FADE)
  target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_compile_definitions(app PRIVATE
    MINIMALPAD_VERSION_MAJOR=${MINIMALPAD_VERSION_MAJOR}
    MINIMALPAD_VERSION_MINOR=${MINIMALPAD_VERSION_MINOR}
    MINIMALPAD_VERSION_PATCH=${MINIMALPAD_VERSION_PATCH}
  )
endif()

if(CONFIG_MINIMALPAD_HOST)
  target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host/frame.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host/host.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host/transport_gatt.c
  )
  target_sources_ifdef(CONFIG_MINIMALPAD_HOST_USB app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host/transport_usb.c
  )
  target_sources_ifdef(CONFIG_MINIMALPAD_MAC_ACTION app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/behaviors/mac_action.c
  )
  target_sources_ifdef(CONFIG_MINIMALPAD_HOST_DIAL app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/behaviors/host_dial.c
  )
endif()

target_sources_ifdef(CONFIG_MINIMALPAD_STUDIO_BLE app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/studio/gatt_rpc_transport.c
)

if(CONFIG_MINIMALPAD_LEDS_IDLE_FADE)
  target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/leds/idle_fade.c)
endif()
