# MinimalPad host module

A small addition to the pad's firmware for MinimalPad Studio for Mac. The Bluetooth channel and Profile switching have run on a pad. Firmware 0.3.0 adds USB Profile control; that new route and Profile colour still need their physical-pad check after flashing. Firmware 0.4.0 adds the Bluetooth device to the pad's state report, which needs the same check. Firmware 0.5.0 adds the Mac action key, which tells Studio when it goes down or up so Studio can open an app. Firmware 0.6.0 gives every layer a dial Studio can set and the pad keeps (`SET_DIAL`, `GET_DIAL`): `&host_dial` taps a key, scrolls or steps the underglow. Firmware 0.7.0 lets Studio read and set the pad's own lighting, which the pad keeps: on or off, colour, brightness, effect and speed (`GET_LIGHTING`, `SET_LIGHTING`).

## What it does

ZMK Studio already lets an app edit the keymap, but it cannot tell the pad which layer to use or what colour to show. The host module adds a small private protocol for that:

- **Profiles.** When you switch to an app that has a profile, such as Figma, Studio tells the pad to switch to that app's layer.
- **Colour.** A Profile can set one temporary global Solid underglow colour, so you can see which Profile is active.
- **Lighting.** Studio can read and change the pad's own lighting, the one its Connections & LEDs keys change. The pad keeps it, so it stays without Studio.
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
| `src/behaviors/host_dial.c` | `&host_dial`, the dial on Default and Connections & LEDs: taps a key, scrolls or steps the underglow, with modifiers, as its layer's dial value says |
| `src/host/dial.c` | Each layer's dial: `SET_DIAL` and `GET_DIAL`, saving values in the pad's settings, and which value a turn gets |
| `src/host/lighting.c` | The pad's own lighting: `GET_LIGHTING` and `SET_LIGHTING`, reporting changes made on the pad, the speed count, and saving colours |
| `dts/bindings/behaviors/minimalpad,behavior-host-dial.yaml` | Tells the keymap what `&host_dial` is and that it takes clockwise and counter-clockwise values until Studio sets others |
| `include/dt-bindings/minimalpad/dial.h` | Names for dial values that are not keys, such as `DIAL_SCROLL_UP` and `DIAL_LIGHTS_BRIGHTER`, for the keymap |
| `dts/bindings/vendor-prefixes.txt` | Registers the `minimalpad` vendor prefix, so the build does not warn about it |
| `include/minimalpad/mp_host_protocol.h` | The wire format, copied byte for byte from MinimalPad Studio for Mac |
| `include/minimalpad/host.h` | How those three files fit together |
| `Kconfig`, `CMakeLists.txt`, `zephyr/module.yml` | Make this repo a Zephyr module, so ZMK compiles the code |
| `boards/shields/minimalpad/minimalpad.keymap` | 14 reserved layer slots instead of 3, so there is room for profiles |
| `.github/workflows/build.yml` | Builds also run when only module code changes |

The firmware, `minimalpad_with_studio`, has 16 layers: Default, Connections & LEDs and 14 free for profiles. The reserved slots are only compiled into builds with ZMK Studio, and since 0.3.0 that is the only build.

## How it works

The Mac and the pad exchange the same frames of at most 20 bytes over either connection:

- **USB:** the host frames share ZMK Studio's one CDC-ACM serial stream. ZMK Studio frames begin with `0xAB`; host frames begin with the frame version `0x01` and declare their length. The firmware and app separate them before either decoder sees them. There is still one serial port, so the pad identity verified by ZMK Studio and every Profile write are physically inseparable.
- **Bluetooth:** the frames use the encrypted service `00000000-e7cd-4a56-8a90-8fd4561e0b05`. Only a Mac the pad is bonded with can connect.

That version byte is frozen at `0x01` and will not change: raising it would make the pad look dead to every Studio already installed. The protocol grows instead by putting new fields on the end of a payload and announcing them as a capability in `HELLO_ACK`, so both ends read the fields they know and ignore whatever trails them. The pad therefore accepts a command that carries more bytes than it expects, and still refuses one that carries fewer.

| Commands, Mac to pad | Events, pad to Mac |
| --- | --- |
| `HELLO`: who are you? | `HELLO_ACK`: firmware version, capabilities, free layers |
| `SET_PROFILE`: switch layer, and optionally colour | `STATE`: active profile, top layer, connection, battery, LEDs, Bluetooth device |
| `HEARTBEAT`: still here; `0` means let go now | `FALLBACK`: back on Default, and why |
| `SET_LEDS`: colour preview | `REJECTED`: a command was refused, and why |
| `GET_STATE`: what are you doing? | `ACTION_EVENT`: a Mac action key went down or up, for Studio to act on |
| `ENTER_BOOTLOADER`: reboot into flash mode, guarded by a magic number | `KEY_EVENT`, `DIAL_EVENT`: reserved for later |
| `SET_DIAL`, `GET_DIAL`: one layer's dial, kept on the pad | `DIAL_STATE`: one layer's dial, and where it comes from |
| `GET_LIGHTING`, `SET_LIGHTING`: the pad's own lighting, kept on the pad | `LIGHTING_STATE`: the pad's own lighting, and whether it is resting |

Rules the firmware keeps:

- **Default when alone.** It falls back when the heartbeat stops, when Studio lets go, when USB is unplugged while it carries the keys, and when you switch Bluetooth profile.
- **One active connection in charge.** USB commands are accepted only while USB is ZMK's selected output. Bluetooth commands are accepted only while Bluetooth is selected and from the Mac on the active Bluetooth profile. Other bonded Macs can still ask what the pad is doing.
- **All or nothing.** A command is checked in full before any part of it runs. A bad colour or unknown layer changes nothing and is answered with `REJECTED`.
- **Your colours are yours.** A Profile colour is temporary. The pad keeps its own HSB and effect aside, and puts them back on fallback, when a Profile has no colour, and after deep sleep, restart or power loss. It never turns the lights on or changes animation speed. Only `SET_LIGHTING`, a change you make in Studio, changes the pad's own lighting ([Lighting](#lighting)).

## Lighting ownership

The physical pad has one 16-pixel WS2812 strip. Effects can draw different pixels, but Studio's current host protocol sets one HSB colour for the whole strip. It cannot address individual keys or segments.

| Effect | What it does with a chosen Profile HSB | Speed |
| --- | --- | --- |
| Solid | Uses hue, saturation and brightness exactly | No visible effect |
| Breathe | Keeps hue and saturation, generates brightness | Changes pulse rate |
| Spectrum | Generates hue, keeps saturation and brightness | Changes cycle rate |
| Swirl | Generates hue across the LEDs, keeps saturation and brightness | Changes movement rate |

The firmware therefore forces Solid when Studio applies a colour. The pad's own Effect key still acts immediately: it can select an animation until Studio sends the next Profile or preview colour, then Solid is forced again. When Studio releases the pad, the colour and effect from before host control return, with any change made with `SET_LIGHTING` meanwhile. Speed stays a global pad setting throughout, and on/off stays the person's choice. Heartbeats only keep the Profile active; they do not reapply its colour.

Lighting changes made with the pad's keys or dial are saved on the pad 60 seconds after the last change (ZMK's settings debounce), from 0.7.0 all of them. Up to 0.6.1 a colour or brightness was not saved by itself. ZMK does not save one set through `zmk_rgb_underglow_set_hsb()` (ZMK issue #1920), which is where the Colour and Brightness keys and the dial's lighting steps end up, so it waited for the next save of something else, usually the one the idle fade schedules when it switches the lights off, about a minute and a half after the last key. When the pad went idle, on/off, effect and speed waited as long, because switching off starts ZMK's minute again. Unplugging before then lost the change. 0.7.0 schedules the colour's save itself, and keeps the lights from fading until the save has landed ([Saving](#saving)). Profile colour is instead saved with the Profile on the Mac and applied temporarily. The host keeps a safety copy of the pad's HSB and effect in flash, so a restart during host control restores the pad's own lighting rather than making an app colour permanent.

The complete control and interaction matrix, official ZMK source links and the Studio UI decision are in the MinimalPad Studio for Mac repo, in `docs/research/profile-lighting.md`.

The full specification lives in the MinimalPad Studio for Mac repo, in `docs/protocol/host-protocol.md`.

## Lighting

Firmware 0.7.0 lets a host read and set the pad's own lighting: the underglow the pad shows without Studio, kept in its settings, the same lighting its Connections & LEDs keys and the dial change. This is not a Profile colour. A Profile colour stays temporary and still goes through `SET_PROFILE` and `SET_LEDS`, and [Lighting ownership](#lighting-ownership) holds for it as before. This is protocol revision 5, announced by capability bit 6 (`MP_CAP_LIGHTING`).

### Commands and event

| ID | Name | Direction | Payload | Who may send it | Answer |
| --- | --- | --- | --- | --- | --- |
| `0x09` | `GET_LIGHTING` | Mac to pad | none | Any host | `LIGHTING_STATE` to that host only |
| `0x0A` | `SET_LIGHTING` | Mac to pad | 8 bytes | The active host only | `LIGHTING_STATE` with flag bit 3 to every host listening, or `REJECTED` to the sender |
| `0x89` | `LIGHTING_STATE` | pad to Mac | 9 bytes | | |

Multi-byte fields are little-endian, as everywhere in this protocol. A payload longer than the one below is from a later revision: each end reads the fields it knows and ignores the rest.

### `SET_LIGHTING` payload

8 bytes, 11 with the header.

| Offset | Field | Type | Range | Notes |
| --- | --- | --- | --- | --- |
| 0 | `fields` | u8 | bits 0-4 | Which parts to apply, below |
| 1 | `on` | u8 | 0 or 1 | 0 off, 1 on |
| 2-3 | `hue` | u16 | 0-359 | Degrees |
| 4 | `saturation` | u8 | 0-100 | Percent |
| 5 | `brightness` | u8 | 0-100 | Percent. 0 is dark but not off: only `on` = 0 cuts the LEDs' power |
| 6 | `effect` | u8 | 0 to `effect_count` − 1 | [Effects](#effects) |
| 7 | `speed` | u8 | 1-5 | Slowest to fastest |

| `fields` bit | Applies | Name |
| --- | --- | --- |
| 0 | `on` | `MP_LIGHTING_FIELD_ON` |
| 1 | `hue` and `saturation` together | `MP_LIGHTING_FIELD_COLOR` |
| 2 | `brightness` | `MP_LIGHTING_FIELD_BRIGHTNESS` |
| 3 | `effect` | `MP_LIGHTING_FIELD_EFFECT` |
| 4 | `speed` | `MP_LIGHTING_FIELD_SPEED` |
| 5-7 | Reserved for fields a later revision adds on the end. A host sends them clear; a pad ignores the bits it does not know | |

A part left out stays as it is. Brightness has a bit of its own, so dimming never rewrites a colour the pad's keys changed a moment before. Every field is range-checked whatever `fields` says, as `SET_PROFILE`'s colour is checked whatever its flags say, so a host fills the parts it leaves out with the values from the last `LIGHTING_STATE`. `fields` of 0 is valid: it changes nothing, but like any `SET_LIGHTING` it wakes resting lights and restarts the hold ([Keeping the lights awake](#keeping-the-lights-awake)).

### `LIGHTING_STATE` payload

9 bytes, 12 with the header. Bytes 1 to 7 are laid out as in `SET_LIGHTING`.

| Offset | Field | Type | Range | Notes |
| --- | --- | --- | --- | --- |
| 0 | `flags` | u8 | bits 0-3 | Below |
| 1 | `on` | u8 | 0 or 1 | 1 while the lights are on, resting included. 0 when they are switched off, by a key, the dial or a host |
| 2-3 | `hue` | u16 | 0-359 | The pad's own colour |
| 4 | `saturation` | u8 | 0-100 | |
| 5 | `brightness` | u8 | 0-100 | The brightness when awake, never a step of the idle fade |
| 6 | `effect` | u8 | 0 to `effect_count` − 1 | |
| 7 | `speed` | u8 | 1-5 | Exact only with flag bit 2 |
| 8 | `effect_count` | u8 | 4 | How many effects this pad has |

Colour, brightness and effect are always the pad's own. While a host colour shows, they are the ones the pad keeps aside and will put back, not the ones on the LEDs.

| `flags` bit | Name | Set when |
| --- | --- | --- |
| 0 | `MP_LIGHTING_FLAG_HOST_COLOR` | A host colour, from `SET_PROFILE` with its LED flag or from `SET_LEDS`, shows in place of the pad's own colour and effect |
| 1 | `MP_LIGHTING_FLAG_RESTING` | The lights are on, but dimming or off because the pad is idle. A key, the dial or a `SET_LIGHTING` wakes them. Never set with `on` = 0 |
| 2 | `MP_LIGHTING_FLAG_SPEED_EXACT` | `speed` is the speed the lights run at ([Speed](#speed)) |
| 3 | `MP_LIGHTING_FLAG_SET_ANSWER` | This frame answers a `SET_LIGHTING` |
| 4-7 | Reserved, sent clear. A host ignores the bits it does not know | |

A host reads Off, Resting or On from two fields: `on` = 0 is Off, `on` = 1 with bit 1 is Resting, and `on` = 1 without it is On. `STATE`'s `leds_on` keeps its meaning for Studio versions that do not know `LIGHTING_STATE`: 0 both when switched off and at the end of the idle fade.

### Effects

The protocol names the effects, because ZMK keeps its own list private. The ZMK revision `config/west.yml` pins numbers them the same way.

| Value | Name | Hue | Saturation | Brightness | Speed |
| --- | --- | --- | --- | --- | --- |
| 0 | `MP_LIGHTING_EFFECT_SOLID` | used | used | used | not used |
| 1 | `MP_LIGHTING_EFFECT_BREATHE` | used | used | not used: it pulses | used |
| 2 | `MP_LIGHTING_EFFECT_SPECTRUM` | not used: it cycles | used | used | used |
| 3 | `MP_LIGHTING_EFFECT_SWIRL` | not used: it moves across the pad | used | used | used |

`effect_count` is 4 on this firmware. A pad accepts only an effect below its own count. A host offers only the effects it has a name for, and shows an effect it has no name for without offering it.

### Answers and rejections

The pad checks the whole frame before it applies any of it.

| Command | When | Answer |
| --- | --- | --- |
| `GET_LIGHTING` | Always | `LIGHTING_STATE`, flag bit 3 clear, to the host that asked |
| `SET_LIGHTING` | Payload shorter than 8 bytes | `REJECTED`, command `0x0A`, reason 0 |
| `SET_LIGHTING` | Not from the active host | `REJECTED`, reason 3 |
| `SET_LIGHTING` | Any field out of range, whatever `fields` says: `on` above 1, `hue` above 359, `saturation` or `brightness` above 100, `effect` not below `effect_count`, `speed` outside 1-5 | `REJECTED`, reason 1 |
| `SET_LIGHTING` | Otherwise | Applied, then `LIGHTING_STATE` with flag bit 3 to every host listening, the sender included, even when nothing changed |

Unlike `DIAL_STATE`, the answer to `SET_LIGHTING` goes to every host, because the change is news to every host showing the pad's lighting. Flag bit 3 is how the sender tells its answer from a `LIGHTING_STATE` the pad volunteered just before. `SET_LIGHTING` needs no heartbeat and arms no fallback: like pressing the pad's lighting keys, it is a saved change, not something a host holds.

### Applying `SET_LIGHTING`

After the checks, in this order:

1. **Wake or not.** If the lights will be on afterwards, the pad wakes them if they are resting and starts the hold. If they will be off, it ends resting without lighting them first.
2. **On/off**, as the pad's Toggle key does: switching off cuts the LEDs' power. A change here also brings a `STATE`, as any change of `leds_on` does.
3. **Colour and brightness**, into the pad's own colour.
4. **Effect.**
5. **Speed.**
6. **The answer**, `LIGHTING_STATE`.

A part equal to what the pad already has changes nothing: it restarts no animation and schedules no save. `SET_LIGHTING` switches the lights on only when its `on` part says so. A colour sent to lights that are off is the colour they come back in.

It is the one command that switches the lights on. The rule that a host never does is about Profile colours and still holds for them. `SET_LIGHTING` is a change the person asked for, like pressing Toggle on the Connections & LEDs layer.

### While a host colour shows

A Profile colour or a preview replaces the pad's colour and effect while it shows, and the pad keeps its own aside (`host.c`'s snapshot). `SET_LIGHTING` changes the pad's own lighting wherever it is kept at the time:

| Part | Goes to | Seen |
| --- | --- | --- |
| On/off | The lights | At once |
| Colour | The pad's own, kept aside | When the host colour is released |
| Brightness | The pad's own, kept aside | When the host colour is released. A host colour keeps its own brightness |
| Effect | The pad's own, kept aside | When the host colour is released. The host colour stays Solid |
| Speed | The lights | In an animated effect, so once the host colour is released |

The copy kept aside in flash (`mp_host/leds`) is saved again 60 seconds after the last change to it, not at once, so dragging a slider costs one write. Its first save, when a host colour first replaces the pad's own, stays immediate. When the host colour is released, the pad's own lighting comes back with the changes, and ZMK saves it. The copy in flash stays until that save has landed, also when a fade moves the save later, so a power cut in between still brings back the pad's own lighting and never makes the host colour its own.

The pad's own keys work as before: while a host colour shows, their colour, brightness and effect changes touch the host colour and are lost when it is released, so they never appear in `LIGHTING_STATE`. On/off, and speed from the dial, apply to the pad's own and do appear.

With `CONFIG_MINIMALPAD_HOST_LEDS_FORCE_SOLID=n` a host colour shows in the pad's own effect, so an effect change applies to the lights as well.

### When the pad sends `LIGHTING_STATE`

Besides its answers, the pad volunteers a `LIGHTING_STATE` to every host listening whenever any field, flag bit 3 aside, differs from the last one it sent to every host:

- **After a key or the dial.** ZMK raises no event when lighting changes, so 75 ms after a key goes down or up, or the dial turns, the pad compares its lighting. When something changed it sends, then compares again 75 ms later, and it also compares again while the last key or turn is less than 75 ms old, until a comparison finds nothing new. The dial's lighting steps run through ZMK's behavior queue, so they can still be landing after the turn; this keeps looking until they have. Turning the dial therefore updates a host about every 75 ms.
- **When the lights start resting and when they wake.** Resting starts with the idle fade, not at its end, so switching off at the end of the fade sends no `LIGHTING_STATE`. It still sends `STATE`, as `leds_on` changes.
- **When a host colour starts or stops showing** (flag bit 0): the first `SET_PROFILE` with its LED flag or `SET_LEDS`, and the release on fallback, on a `SET_PROFILE` without the flag, or at start-up.

A change from anything else shows at the next of these, or when a host asks. The pad does not volunteer one when a host arrives: Studio asks with `GET_LIGHTING` once the link is ready. The answer to `GET_LIGHTING` goes to one host, so it does not count as sent to every host.

### Keeping the lights awake

ZMK counts only keys, the dial and other input as activity. Without help the lights would start fading 30 seconds into an edit in Studio and be off at 38.

A `SET_LIGHTING` that leaves the lights on wakes them if they are resting, and holds off the idle fade until 30 seconds (`MP_LIGHTING_HOLD_SECONDS`) after the last `SET_LIGHTING`. When the hold ends, the pad's own rule applies again: if no key or dial has been used for 30 seconds (`CONFIG_ZMK_IDLE_TIMEOUT`), the fade starts at once; otherwise it starts when the pad next goes idle.

- The hold always runs out. A host sends `SET_LIGHTING` only for a change the person makes, never on a timer.
- A change to the pad's own lighting, whatever made it, also holds off the fade until ZMK has saved it ([Saving](#saving)), at most 60 seconds after the last change. After a change the lights therefore stay on up to a minute rather than 30 seconds.
- A `SET_LIGHTING` that leaves the lights off starts no hold, but a change it makes to the colour, effect or speed still delays a later fade until it has been saved.
- `GET_LIGHTING` never wakes or holds the lights.
- Lighting commands are activity for nothing else, and do not put off deep sleep.
- A host colour still does not wake resting lights ([Lighting ownership](#lighting-ownership)).
- Without `CONFIG_MINIMALPAD_LEDS_IDLE_FADE` the lights never rest: flag bit 1 stays clear and there is nothing to hold.

### Saving

The pad saves its own lighting through ZMK's settings: one write, 60 seconds (`CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE`) after the last change, whatever made it, `SET_LIGHTING`, a key or the dial. Dragging a slider across its range costs one write. A restart or power cut inside that minute loses the change, so a host does not say "saved" when the pad answers. The pad has no preview mode: a host undoes a change by sending back the values it had before.

ZMK does not save a colour or brightness set through `zmk_rgb_underglow_set_hsb()` on its own (ZMK issue #1920), and that is where the Colour and Brightness keys and the dial's lighting steps end up. The pad schedules ZMK's save itself with `zmk_rgb_underglow_change_hue(0)`, which changes no colour anyone can see:

- after a `SET_LIGHTING` changes the colour or brightness of the lights, with no host colour showing;
- when its comparison after a key or the dial finds the pad's own colour or brightness changed.

Changes made while a host colour shows are left out: they belong to the host colour, and ZMK must not save it as the pad's.

The comparison, and the waits for ZMK's save after a speed key or the dial's speed steps, start once the start-up read of the speed has been taken up. Before that the settings are still loading, and a change is ZMK's saved lighting arriving, not a key's. Acting on it could delete the idle flag from the system work queue while the loading thread holds the settings lock and waits on that queue for Bluetooth, so a pad restarted with its lights off for idle and pressed as it starts would hang.

A save must never record a step of the idle fade: ZMK saves whatever its state holds when the minute runs out, and mid-fade that is a dimmed colour. So the fade does not start while a save of a change to the pad's own lighting is pending. It waits until the save has landed, a minute after the last change, and only then do the lights start resting. Moving the save past the fade instead would cost another minute, and switching off at the end of the fade another on top, so a change would be saved about a minute and a half after the last key, as up to 0.6.1. A save pending for anything else, such as a Profile colour going on or coming off, is moved past the fade when the fade starts. Switching off at the end of the fade saves again anyway, once the pad's colour is back in place.

### Speed

ZMK cannot report the animation speed, so the pad keeps its own count.

- At start-up it reads the speed from ZMK's saved lighting: the byte at offset 4 of the 10-byte `rgb/underglow/state` record, read with `settings_load_subtree_direct()` in the module's settings commit, once every subtree has loaded. The read runs on the thread loading the settings, never on the system work queue: that thread holds the settings lock through ZMK's Bluetooth commit, which needs the work queue to send its commands, so a read from the queue would hang the pad at every start. A second handler for that key would take over ZMK's own load. With no record saved, the speed is `CONFIG_ZMK_RGB_UNDERGLOW_SPD_START`.
- `SET_LIGHTING` sets a speed by stepping down four times, then up `speed` − 1 times, one step at a time with `zmk_rgb_underglow_change_spd()`. A larger step overflows ZMK's counter. This lands on the speed asked for whatever the count said.
- The dial's Faster and Slower steps move the count one step each, stopping at 1 and 5 as ZMK does.

Flag bit 2 is set after a good read at start-up and after a `SET_LIGHTING` that sets the speed. It is cleared when that read fails, and when a key goes down at a position where any layer holds `&rgb_ug RGB_SPI` or `&rgb_ug RGB_SPD`, since the pad does not count those. The default keymap has neither.

### Compatibility

- **A host turns lighting on from capability bit 6, and from nothing else.** Firmware without it skips `0x09` and `0x0A` silently, so a host that sent one and waited would only time out.
- **Older Studio on this firmware** is unaffected. `HELLO_ACK` is still 7 bytes, bit 6 means nothing to it, and it skips `0x89` by its length.
- **Bit 7 is the last capability bit.** Studio 0.1.0 to 0.3.0 read `HELLO_ACK` as exactly 7 bytes and fail the handshake on a longer one, so `HELLO_ACK` cannot grow a second capability byte. Plan another way to announce a feature before spending bit 7.
- **`LIGHTING_STATE` grows by trailing fields.** A host reads the 9 bytes it knows and ignores the rest, from its first version: a decoder that refused a longer one would fix the event at 9 bytes for good. A new `SET_LIGHTING` part goes on the end, with a new `fields` bit.
- **`STATE` does not change.** `leds_on` still reads 0 at the end of the idle fade, for Studio versions that do not know `LIGHTING_STATE`.
- **USB and Bluetooth behave the same**: both transports share the frame reader and the handlers.

### How Studio uses it

These are Studio's rules. They sit here too, so both ends implement the same thing.

- Read with `GET_LIGHTING` once the link is ready, then follow the pad's `LIGHTING_STATE`s.
- Show Off, Resting or On from `on` and flag bit 1, not from `STATE`'s `leds_on`.
- Keep one `SET_LIGHTING` in flight. While a slider moves, send the newest values when the answer comes back, and no more often than every 33 ms. The answer is the first `LIGHTING_STATE` with flag bit 3 after sending; a `REJECTED` for `0x0A` fails it.
- Fill the parts left out of `fields` from the last `LIGHTING_STATE`.
- While flag bit 0 is set, a colour, brightness or effect change shows only once the host colour is released. Studio lifts a Profile colour while the Lights popover is open, by sending the current `SET_PROFILE` again without its LED flag, and puts it back when the popover closes. Anywhere else, such as the menu bar's brightness slider, a change made under a host colour shows only once that colour is released.
- Never send on a timer, and never say "saved" when the pad answers.

## Settings

Add any of these to `boards/shields/minimalpad/minimalpad.conf`:

| Option | Default | What it does |
| --- | --- | --- |
| `CONFIG_MINIMALPAD_HOST` | `y` | The module itself. Set `n` to build without it |
| `CONFIG_MINIMALPAD_HOST_USB` | `y` in the Studio USB build | Carry Profile control beside ZMK Studio on its one serial port |
| `CONFIG_MINIMALPAD_HOST_USB_TX_BUFFER_SIZE` | `512` | Bytes reserved while Studio and host replies share USB |
| `CONFIG_MINIMALPAD_HOST_HEARTBEAT_DEFAULT_TIMEOUT` | `6` | Seconds before falling back, until Studio's first heartbeat sets its own |
| `CONFIG_MINIMALPAD_MAC_ACTION` | `y` | The `&mac_action` key. Set `n` to build without it; the pad then no longer claims Mac actions in `HELLO_ACK` |
| `CONFIG_MINIMALPAD_HOST_DIAL` | `y` | `&host_dial` and each layer's dial. Set `n` to build without it; the pad then no longer claims layer dials in `HELLO_ACK` |
| `CONFIG_MINIMALPAD_HOST_DIAL_SCROLL_STEP` | `1` | Wheel steps per step of the dial when a profile's dial scrolls |
| `CONFIG_MINIMALPAD_HOST_LIGHTING` | `y` | Studio reads and sets the pad's own lighting. Needs `CONFIG_ZMK_RGB_UNDERGLOW`. Set `n` to build without it; the pad then no longer claims lighting in `HELLO_ACK`, and colours changed with keys go back to waiting for ZMK's next save |
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

Firmware 0.7.0's lighting, measured on 29 Sep 2026 at the ZMK revision `config/west.yml` pins, adds 2,536 B of flash and 224 B of RAM to `minimalpad_with_studio`: 251,996 B flash and 79,718 B RAM, against 249,460 B and 79,494 B for 0.6.1.

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
