# MinimalPad host module

A small addition to the pad's firmware for MinimalPad Studio for Mac. The Bluetooth channel and Profile switching have run on a pad. Firmware 0.3.0 adds USB Profile control; that new route and Profile colour still need their physical-pad check after flashing. Firmware 0.4.0 adds the Bluetooth device to the pad's state report, which needs the same check. Firmware 0.5.0 adds the Mac action key, which tells Studio when it goes down or up so Studio can open an app.

## What it does

ZMK Studio already lets an app edit the keymap, but it cannot tell the pad which layer to use or what colour to show. The host module adds a small private protocol for that:

- **Profiles.** When you switch to an app that has a profile, such as Figma, Studio tells the pad to switch to that app's layer.
- **Colour.** A Profile can set one temporary global Solid underglow colour, so you can see which Profile is active.
- **Bluetooth device.** The pad says which of its Bluetooth devices is selected, so Studio can show you whether the pad is typing to this Mac or to another one.
- **Safety net.** Studio sends a heartbeat every 2 seconds. If it stops (Studio quits, the Mac sleeps, the pad goes out of range), the pad returns to its Default layer and its own colours by itself within 6 seconds.

Without Studio running, nothing changes: the pad behaves exactly as its keymap says.

## What stays the same

- You still build with GitHub Actions: push, then download the `.uf2` from the Actions tab.
- You still flash the usual way: double-press reset, then copy the `.uf2` onto the drive that appears.
- Keymap editing still goes through ZMK Studio, from MinimalPad Studio on the web or on Mac.

## What changed in this repo

| Path | What it is |
| --- | --- |
| `src/host/frame.c` | Reads command frames and checks them before anything runs |
| `src/host/host.c` | What each command does: layers, LEDs, heartbeat and fallback, state reports |
| `src/host/transport_gatt.c` | The Bluetooth GATT service the Mac talks to |
| `src/host/transport_usb.c` | Shares ZMK Studio's one USB serial stream without adding another port |
| `src/behaviors/mac_action.c` | The `&mac_action` key: sends `ACTION_EVENT` when it goes down or up, and does nothing else |
| `dts/bindings/behaviors/minimalpad,behavior-mac-action.yaml` | Tells the keymap and ZMK Studio what `&mac_action` is and that it takes one number |
| `dts/bindings/vendor-prefixes.txt` | Registers the `minimalpad` vendor prefix, so the build does not warn about it |
| `include/minimalpad/mp_host_protocol.h` | The wire format, copied byte for byte from MinimalPad Studio for Mac |
| `include/minimalpad/host.h` | How those three files fit together |
| `Kconfig`, `CMakeLists.txt`, `zephyr/module.yml` | Make this repo a Zephyr module, so ZMK compiles the code |
| `boards/shields/minimalpad/minimalpad.keymap` | 14 reserved layer slots instead of 3, so there is room for profiles |
| `.github/workflows/build.yml` | Builds also run when only module code changes |

The firmware, `minimalpad_with_studio`, has 16 layers: Default, BT + LED and 14 free for profiles. The reserved slots are only compiled into builds with ZMK Studio, and since 0.3.0 that is the only build.

## How it works

The Mac and the pad exchange the same frames of at most 20 bytes over either connection:

- **USB:** the host frames share ZMK Studio's one CDC-ACM serial stream. ZMK Studio frames begin with `0xAB`; host frames begin with the frame version `0x01` and declare their length. The firmware and app separate them before either decoder sees them. There is still one serial port, so the pad identity verified by ZMK Studio and every Profile write are physically inseparable.
- **Bluetooth:** the frames use the encrypted service `00000000-e7cd-4a56-8a90-8fd4561e0b05`. Only a Mac the pad is bonded with can connect.

That version byte is frozen at `0x01` and will not change: raising it would make the pad look dead to every Studio already installed. The protocol grows instead by putting new fields on the end of a payload and announcing them as a capability in `HELLO_ACK`, so both ends read the fields they know and ignore whatever trails them. The pad therefore accepts a command that carries more bytes than it expects, and still refuses one that carries fewer.

| Commands, Mac to pad | Events, pad to Mac |
| --- | --- |
| `HELLO`: who are you? | `HELLO_ACK`: firmware version, capabilities, free layers |
| `SET_PROFILE`: switch layer, and optionally colour and dial | `STATE`: active profile, top layer, connection, battery, LEDs, Bluetooth device |
| `HEARTBEAT`: still here; `0` means let go now | `FALLBACK`: back on Default, and why |
| `SET_LEDS`: colour preview | `REJECTED`: a command was refused, and why |
| `GET_STATE`: what are you doing? | `ACTION_EVENT`: a Mac action key went down or up, for Studio to act on |
| `ENTER_BOOTLOADER`: reboot into flash mode, guarded by a magic number | `KEY_EVENT`, `DIAL_EVENT`: reserved for later |

Rules the firmware keeps:

- **Default when alone.** It falls back when the heartbeat stops, when Studio lets go, when USB is unplugged while it carries the keys, and when you switch Bluetooth profile.
- **One active connection in charge.** USB commands are accepted only while USB is ZMK's selected output. Bluetooth commands are accepted only while Bluetooth is selected and from the Mac on the active Bluetooth profile. Other bonded Macs can still ask what the pad is doing.
- **All or nothing.** A command is checked in full before any part of it runs. A bad colour or unknown layer changes nothing and is answered with `REJECTED`.
- **Your colours are yours.** A Profile colour is temporary. The pad keeps its own HSB and effect aside, and puts them back on fallback, when a Profile has no colour, and after deep sleep, restart or power loss. It never turns the lights on or changes animation speed.

## Lighting ownership

The physical pad has one 16-pixel WS2812 strip. Effects can draw different pixels, but Studio's current host protocol sets one HSB colour for the whole strip. It cannot address individual keys or segments.

| Effect | What it does with a chosen Profile HSB | Speed |
| --- | --- | --- |
| Solid | Uses hue, saturation and brightness exactly | No visible effect |
| Breathe | Keeps hue and saturation, generates brightness | Changes pulse rate |
| Spectrum | Generates hue, keeps saturation and brightness | Changes cycle rate |
| Swirl | Generates hue across the LEDs, keeps saturation and brightness | Changes movement rate |

The firmware therefore forces Solid when Studio applies a colour. The pad's own Effect key still acts immediately: it can select an animation until Studio sends the next Profile or preview colour, then Solid is forced again. When Studio releases the pad, the colour and effect from before host control return. Speed stays a global pad setting throughout, and on/off stays the person's choice. Heartbeats only keep the Profile active; they do not reapply its colour.

Ordinary ZMK lighting changes made from keys are saved to the pad after the settings debounce. Profile colour is instead saved with the Profile on the Mac and applied temporarily. The host keeps a safety copy of the pad's HSB and effect in flash, so a restart during host control restores the pad's own lighting rather than making an app colour permanent.

The complete control and interaction matrix, official ZMK source links and the Studio UI decision are in the MinimalPad Studio for Mac repo, in `docs/research/profile-lighting.md`.

The full specification lives in the MinimalPad Studio for Mac repo, in `docs/protocol/host-protocol.md`.

## Settings

Add any of these to `boards/shields/minimalpad/minimalpad.conf`:

| Option | Default | What it does |
| --- | --- | --- |
| `CONFIG_MINIMALPAD_HOST` | `y` | The module itself. Set `n` to build without it |
| `CONFIG_MINIMALPAD_HOST_USB` | `y` in the Studio USB build | Carry Profile control beside ZMK Studio on its one serial port |
| `CONFIG_MINIMALPAD_HOST_USB_TX_BUFFER_SIZE` | `512` | Bytes reserved while Studio and host replies share USB |
| `CONFIG_MINIMALPAD_HOST_HEARTBEAT_DEFAULT_TIMEOUT` | `6` | Seconds before falling back, until Studio's first heartbeat sets its own |
| `CONFIG_MINIMALPAD_MAC_ACTION` | `y` | The `&mac_action` key. Set `n` to build without it; the pad then no longer claims Mac actions in `HELLO_ACK` |
| `CONFIG_MINIMALPAD_HOST_LEDS_FORCE_SOLID` | `y` | Show Profile HSB exactly: Breathe generates brightness, while Spectrum and Swirl generate hue |
| `CONFIG_MINIMALPAD_HOST_LOG_LEVEL_DBG` | off | Log what the module decides, with the `zmk-usb-logging` snippet |

The firmware version Studio sees in `HELLO_ACK` is not a setting. It comes from the `VERSION` file, the same one the build is named after ([releasing.md](releasing.md)).

## Size

The earlier Bluetooth-only module was measured on 14 Sep 2026 against ZMK `main` at `641514a`:

| Build | Before | With the host module |
| --- | --- | --- |
| `minimalpad` | 207,304 B flash, 48,776 B RAM | 210,512 B flash, 49,480 B RAM |
| `minimalpad_with_studio` | 234,048 B flash, 63,378 B RAM | 240,600 B flash, 67,786 B RAM |

That module was about 3.2 KB of flash and 0.7 KB of RAM. Firmware 0.3.0 also reserves a 512-byte combined USB transmit buffer and an eight-by-64-byte receive queue. The rest of the Studio build's growth is the 11 extra reserved layers. The nice!nano has 1 MB of flash and 256 KB of RAM.

## Testing

Flash a spare pad first, since the Studio build changes the layer count. The checklist is in the MinimalPad Studio for Mac repo, `.scratch/host-module/issues/01-test-on-a-pad.md`. Bluetooth Profile switching has passed. USB switching, USB fallback and the lighting cases remain to be run on hardware.

## Building locally

Optional: GitHub Actions is the normal build. To compile the same firmware on a Mac with Docker (OrbStack or Docker Desktop):

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
build minimalpad_with_studio "studio-rpc-usb-uart nrf52840-nosd" "-DCONFIG_ZMK_STUDIO=y"
```

The `.uf2` file ends up in the volume at `build/minimalpad_with_studio/zephyr/zmk.uf2`. `config/west.yml` pins the ZMK revision used by the combined USB transport, and `.github/workflows/build.yml` builds with ZMK's workflow from the same commit. Change the two pins together, deliberately, and rebuild when adopting a newer ZMK revision.

## Changing the protocol

`include/minimalpad/mp_host_protocol.h` is a copy. Change the MinimalPad Studio for Mac repo's `docs/protocol/mp_host_protocol.h` and its spec first, copy the header here unchanged, then update the Swift side (`Core/HostLink`) to match. The `BUILD_ASSERT` lines in `frame.c` and `host.c` fail the build if the structs drift from the sizes the spec names.
