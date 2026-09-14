# Minimalpad host module

A small addition to the pad's firmware for Minimalpad Studio, the Mac app. Status: compiled for both targets, **not yet tested on a pad**.

## What it does

ZMK Studio already lets an app edit the keymap, but it cannot tell the pad which layer to use or what colour to show. The host module adds a second, private channel for that:

- **Profiles.** When you switch to an app that has a profile, such as Figma, Studio tells the pad to switch to that app's layer.
- **Colour.** A profile can set the underglow colour, so you can see which profile is active.
- **Safety net.** Studio sends a heartbeat every 2 seconds. If it stops (Studio quits, the Mac sleeps, the pad goes out of range), the pad returns to its Default layer and its own colours by itself within 6 seconds.

Without Studio running, nothing changes: the pad behaves exactly as its keymap says.

## What stays the same

- You still build with GitHub Actions: push, then download the `.uf2` from the Actions tab.
- You still flash the usual way: double-press reset, then copy the `.uf2` onto the drive that appears.
- Keymap editing still goes through ZMK Studio, from Minimalpad Studio or Minimalpad Configurator.

## What changed in this repo

| Path | What it is |
| --- | --- |
| `src/host/frame.c` | Reads command frames and checks them before anything runs |
| `src/host/host.c` | What each command does: layers, LEDs, heartbeat and fallback, state reports |
| `src/host/transport_gatt.c` | The Bluetooth GATT service the Mac talks to |
| `include/minimalpad/mp_host_protocol.h` | The wire format, copied byte for byte from Minimalpad Studio |
| `include/minimalpad/host.h` | How those three files fit together |
| `Kconfig`, `CMakeLists.txt`, `zephyr/module.yml` | Make this repo a Zephyr module, so ZMK compiles the code |
| `boards/shields/minimalpad/minimalpad.keymap` | 14 reserved layer slots instead of 3, so there is room for profiles |
| `.github/workflows/build.yml` | Builds also run when only module code changes |

The reserved slots only exist in the `minimalpad_with_studio` build, which has 16 layers: Media, BT + LED and 14 free for profiles. The plain `minimalpad` build keeps its two layers, so it carries the module but has no room for profiles.

## How it works

The Mac and the pad exchange frames of at most 20 bytes over an encrypted Bluetooth service, `00000000-e7cd-4a56-8a90-8fd4561e0b05`. Only a Mac the pad is bonded with can connect.

| Commands, Mac to pad | Events, pad to Mac |
| --- | --- |
| `HELLO`: who are you? | `HELLO_ACK`: firmware version, capabilities, free layers |
| `SET_PROFILE`: switch layer, and optionally colour and dial | `STATE`: active profile, top layer, connection, battery, LEDs |
| `HEARTBEAT`: still here; `0` means let go now | `FALLBACK`: back on Default, and why |
| `SET_LEDS`: colour preview | `REJECTED`: a command was refused, and why |
| `GET_STATE`: what are you doing? | `KEY_EVENT`, `DIAL_EVENT`, `ACTION_EVENT`: reserved for later |
| `ENTER_BOOTLOADER`: reboot into flash mode, guarded by a magic number | |

Rules the firmware keeps:

- **Default when alone.** It falls back when the heartbeat stops, when Studio lets go, when USB is unplugged while it carries the keys, and when you switch Bluetooth profile.
- **One Mac in charge.** Only the Mac on the active Bluetooth profile can change the pad. Other bonded Macs can still ask what it is doing.
- **All or nothing.** A command is checked in full before any part of it runs. A bad colour or unknown layer changes nothing and is answered with `REJECTED`.
- **Your colours are yours.** A profile colour is temporary. The pad keeps its own colour and effect aside, and puts them back on fallback, when a profile has no colour, and after sleep or power loss.

The full specification lives in the Minimalpad Studio repo, in `docs/protocol/host-protocol.md`.

## Settings

Add any of these to `boards/shields/minimalpad/minimalpad.conf`:

| Option | Default | What it does |
| --- | --- | --- |
| `CONFIG_MINIMALPAD_HOST` | `y` | The module itself. Set `n` to build without it |
| `CONFIG_MINIMALPAD_HOST_HEARTBEAT_DEFAULT_TIMEOUT` | `6` | Seconds before falling back, until Studio's first heartbeat sets its own |
| `CONFIG_MINIMALPAD_HOST_LEDS_FORCE_SOLID` | `y` | Show profile colours with the solid effect, since animated effects ignore colour |
| `CONFIG_MINIMALPAD_HOST_FW_VERSION_MAJOR`, `_MINOR`, `_PATCH` | `0.1.0` | The version Studio sees |
| `CONFIG_MINIMALPAD_HOST_LOG_LEVEL_DBG` | off | Log what the module decides, with the `zmk-usb-logging` snippet |

## Size

Measured on 14 Sep 2026 against ZMK `main` at `641514a`:

| Build | Before | With the host module |
| --- | --- | --- |
| `minimalpad` | 207,304 B flash, 48,776 B RAM | 210,512 B flash, 49,480 B RAM |
| `minimalpad_with_studio` | 234,048 B flash, 63,378 B RAM | 240,600 B flash, 67,786 B RAM |

The module itself is about 3.2 KB of flash and 0.7 KB of RAM. The rest of the Studio build's growth is the 11 extra reserved layers. The nice!nano has 1 MB of flash and 256 KB of RAM.

## Testing

Flash a spare pad first, since the Studio build changes the layer count. The checklist is in the Minimalpad Studio repo, `.scratch/host-module/issues/01-test-on-a-pad.md`. Most checks need Studio's Bluetooth connection, which is the next thing being built on the Mac side.

## Building locally

Optional: GitHub Actions is the normal build. To compile the same targets on a Mac with Docker (OrbStack or Docker Desktop):

```sh
REPO="$PWD"
docker volume create minimalpad-zmk-west
docker run --rm -v minimalpad-zmk-west:/tmp/zmk-config -v "$REPO":/zmk-config-repo:ro -w /tmp/zmk-config \
  zmkfirmware/zmk-build-arm:stable sh -c '
  rm -rf config && mkdir config && cp -R /zmk-config-repo/config/* config/
  [ -d .west ] || west init -l /tmp/zmk-config/config
  west update --fetch-opt=--filter=tree:0'

build() {
  docker run --rm -v minimalpad-zmk-west:/tmp/zmk-config -v "$REPO":/zmk-config-repo:ro -w /tmp/zmk-config \
    -e NAME="$1" -e SNIPPET="$2" -e EXTRA="$3" zmkfirmware/zmk-build-arm:stable sh -c '
    rm -rf config && mkdir config && cp -R /zmk-config-repo/config/* config/
    west zephyr-export; rm -rf "build/$NAME"
    west build -s zmk/app -d "build/$NAME" -b "nice_nano//zmk" -S "$SNIPPET" -- \
      -DZMK_CONFIG=/tmp/zmk-config/config -DSHIELD=minimalpad -DZMK_EXTRA_MODULES=/zmk-config-repo $EXTRA'
}
build minimalpad "nrf52840-nosd" ""
build minimalpad_with_studio "studio-rpc-usb-uart nrf52840-nosd" "-DCONFIG_ZMK_STUDIO=y"
```

The `.uf2` files end up in the volume at `build/<name>/zephyr/zmk.uf2`. Running `west update` again moves to a newer ZMK `main`.

## Changing the protocol

`include/minimalpad/mp_host_protocol.h` is a copy. Change the Minimalpad Studio repo's `docs/protocol/mp_host_protocol.h` and its spec first, copy the header here unchanged, then update the Swift side (`Core/HostLink`) to match. The `BUILD_ASSERT` lines in `frame.c` and `host.c` fail the build if the structs drift from the sizes the spec names.
