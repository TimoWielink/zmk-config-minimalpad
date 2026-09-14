# Minimalpad host module: the device channel Minimalpad Studio uses for what
# ZMK Studio RPC cannot do. See Kconfig for what it is.
#
# The sources join ZMK's own `app` target rather than a library of their own,
# because ZMK keeps its headers (zmk/keymap.h and friends) private to `app`.

if(CONFIG_MINIMALPAD_HOST)
  target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host/frame.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host/host.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/host/transport_gatt.c
  )
endif()
