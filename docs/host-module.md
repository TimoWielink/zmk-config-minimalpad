# MinimalPad host module

A small addition to the pad's firmware for MinimalPad Studio for Mac. The Bluetooth channel and Profile switching have run on a pad. Firmware 0.3.0 adds USB Profile control; that new route and Profile colour still need their physical-pad check after flashing. Firmware 0.4.0 adds the Bluetooth device to the pad's state report, which needs the same check. Firmware 0.5.0 adds the Mac action key, which tells Studio when it goes down or up so Studio can open an app. Firmware 0.6.0 gives every layer a dial Studio can set and the pad keeps (`SET_DIAL`, `GET_DIAL`): `&host_dial` taps a key, scrolls or steps the underglow. Firmware 0.7.0 lets Studio read and set the pad's own lighting, which the pad keeps: on or off, colour, brightness, effect and speed (`GET_LIGHTING`, `SET_LIGHTING`). Firmware 0.8.0 lets Studio give every layer above Default a light and every key on every layer one of its own, which the pad keeps and draws over its own lighting (`SET_LAYER_LIGHT`, `SET_KEY_LIGHT`, announced by `GET_FEATURES`). Firmware 0.9.0 lets a profile have several layers that the pad switches between by itself (`SWITCH_PROFILE`, the `&profile_layer` key, announced by `GET_FEATURES`), and saves a layer's dial the way it saves lights, after the settings have loaded, instead of from its command handler.

## What it does

ZMK Studio already lets an app edit the keymap, but it cannot tell the pad which layer to use or what colour to show. The host module adds a small private protocol for that:

- **Profiles.** When you switch to an app that has a profile, such as Figma, Studio tells the pad to switch to that app's layer.
- **Colour.** A Profile can set one temporary global Solid underglow colour, so you can see which Profile is active.
- **Lighting.** Studio can read and change the pad's own lighting, the one its Connections & LEDs keys change. The pad keeps it, so it stays without Studio.
- **Layer and key lights.** Studio can give a layer, and any key on any layer, a colour of its own or switch its light off. The pad keeps them and draws them over its own lighting, so a layer or a key looks the same without Studio, and the pad shows its own lighting for a few seconds while you change it.
- **Several layers for one app.** A profile can have several layers. Studio tells the pad which ones belong to the app in front, and keys on the pad switch between them with no round trip to the Mac: Next layer, Go to, and Hold for, Default included ([Profile layers](#profile-layers)).
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
| `src/host/host.c` | What each command does: layers, LEDs, heartbeat and fallback, state reports, and `GET_FEATURES`, which it answers on every build |
| `src/host/transport_gatt.c` | The Bluetooth GATT service the Mac talks to |
| `src/host/transport_usb.c` | Shares ZMK Studio's one USB serial stream without adding another port |
| `src/behaviors/mac_action.c` | The `&mac_action` key: sends `ACTION_EVENT` when it goes down or up, and does nothing else |
| `dts/bindings/behaviors/minimalpad,behavior-mac-action.yaml` | Tells the keymap and ZMK Studio what `&mac_action` is and that it takes one number |
| `src/behaviors/host_dial.c` | `&host_dial`, the dial on Default and Connections & LEDs: taps a key, scrolls or steps the underglow, with modifiers, as its layer's dial value says |
| `src/host/dial.c` | Each layer's dial: `SET_DIAL` and `GET_DIAL`, saving values in the pad's settings 2 seconds after the last change, and which value a turn gets |
| `src/host/lighting.c` | The pad's own lighting: `GET_LIGHTING` and `SET_LIGHTING`, reporting changes made on the pad, the speed count, and saving colours |
| `src/host/lights.c` | Each layer's and key's light: `SET_LAYER_LIGHT`, `GET_LAYER_LIGHT`, `SHOW_PAD_LIGHTING`, `SET_KEY_LIGHT` and `GET_KEY_LIGHTS`, saving them in the pad's settings, which light a key shows, and when the pad shows its own lighting instead |
| `src/host/profile_layers.c` | `SWITCH_PROFILE`, the layers of the profile the pad holds (its group), and the swap and the holds that `&profile_layer` makes |
| `src/behaviors/profile_layer.c` | `&profile_layer`: Next layer, Go to and Hold for, with what ZMK Studio needs to put it on a key |
| `src/leds/light_layers.c` | The LED strip between ZMK's underglow and the WS2812 chain that paints those lights over the underglow's frame |
| `dts/bindings/led_strip/minimalpad,light-layers.yaml` | Tells the overlay what that strip is: the chain it feeds, its length and the key under each LED |
| `dts/bindings/behaviors/minimalpad,behavior-profile-layer.yaml` | Tells the keymap what `&profile_layer` is and that it takes a mode and a layer id |
| `dts/bindings/behaviors/minimalpad,behavior-host-dial.yaml` | Tells the keymap what `&host_dial` is and that it takes clockwise and counter-clockwise values until Studio sets others |
| `include/dt-bindings/minimalpad/dial.h` | Names for dial values that are not keys, such as `DIAL_SCROLL_UP` and `DIAL_LIGHTS_BRIGHTER`, for the keymap |
| `include/dt-bindings/minimalpad/profile_layer.h` | Names for `&profile_layer`'s modes, `PL_NEXT`, `PL_GO` and `PL_HOLD`, for the keymap |
| `dts/bindings/vendor-prefixes.txt` | Registers the `minimalpad` vendor prefix, so the build does not warn about it |
| `include/minimalpad/mp_host_protocol.h` | The wire format, copied byte for byte from MinimalPad Studio for Mac |
| `include/minimalpad/host.h` | How those three files fit together |
| `Kconfig`, `CMakeLists.txt`, `zephyr/module.yml` | Make this repo a Zephyr module, so ZMK compiles the code |
| `boards/shields/minimalpad/minimalpad.keymap` | 14 reserved layer slots instead of 3, so there is room for profiles, and the `profile_layer` behavior, which no key uses until Studio assigns it |
| `boards/shields/minimalpad/minimalpad.overlay` | The `light_layers` node, which is the underglow's strip, and the key under each LED in chain order |
| `boards/shields/minimalpad/minimalpad.conf` | `CONFIG_BT_ATT_TX_COUNT=10`, so a whole layer's light answers are not lost over Bluetooth |
| `.github/workflows/build.yml` | Builds also run when only module code or a devicetree binding changes |

The firmware, `minimalpad_with_studio`, has 16 layers: Default, Connections & LEDs and 14 free for profiles. The reserved slots are only compiled into builds with ZMK Studio, and since 0.3.0 that is the only build.

## How it works

The Mac and the pad exchange the same frames of at most 20 bytes over either connection:

- **USB:** the host frames share ZMK Studio's one CDC-ACM serial stream. ZMK Studio frames begin with `0xAB`; host frames begin with the frame version `0x01` and declare their length. The firmware and app separate them before either decoder sees them. There is still one serial port, so the pad identity verified by ZMK Studio and every Profile write are physically inseparable.
- **Bluetooth:** the frames use the encrypted service `00000000-e7cd-4a56-8a90-8fd4561e0b05`. Only a Mac the pad is bonded with can connect.

That version byte is frozen at `0x01` and will not change: raising it would make the pad look dead to every Studio already installed. The protocol grows instead by putting new fields on the end of a payload and announcing them as a capability in `HELLO_ACK`, so both ends read the fields they know and ignore whatever trails them. The pad therefore accepts a command that carries more bytes than it expects, and still refuses one that carries fewer.

| Commands, Mac to pad | Events, pad to Mac |
| --- | --- |
| `HELLO`: who are you? | `HELLO_ACK`: firmware version, capabilities, free layers |
| `SET_PROFILE`: switch layer, and optionally colour. `SWITCH_PROFILE`: switch layer, and say which layers the profile has | `STATE`: active profile, top layer, connection, battery, LEDs, Bluetooth device, and why the layer changed |
| `HEARTBEAT`: still here; `0` means let go now | `FALLBACK`: back on Default, and why |
| `SET_LEDS`: colour preview | `REJECTED`: a command was refused, and why |
| `GET_STATE`: what are you doing? | `ACTION_EVENT`: a Mac action key went down or up, for Studio to act on |
| `ENTER_BOOTLOADER`: reboot into flash mode, guarded by a magic number | `KEY_EVENT`, `DIAL_EVENT`: reserved for later |
| `SET_DIAL`, `GET_DIAL`: one layer's dial, kept on the pad | `DIAL_STATE`: one layer's dial, and where it comes from |
| `GET_LIGHTING`, `SET_LIGHTING`: the pad's own lighting, kept on the pad | `LIGHTING_STATE`: the pad's own lighting, and whether it is resting |
| `GET_FEATURES`: which features does this pad have? The host core answers, on every build | `FEATURES`: a bitmap of features: profile layers, layer lights, key lights |
| `SET_LAYER_LIGHT`, `GET_LAYER_LIGHT`: a layer's light, kept on the pad | `LAYER_LIGHT_STATE`: a layer's light |
| `SET_KEY_LIGHT`, `GET_KEY_LIGHTS`: the lights of keys on a layer, kept on the pad | `KEY_LIGHTS_STATE`: the lights of up to three keys |
| `SHOW_PAD_LIGHTING`: show the pad's own lighting for some seconds | None: the lights are the answer |

Rules the firmware keeps:

- **Default when alone.** It falls back when the heartbeat stops, when Studio lets go, when USB is unplugged while it carries the keys, and when you switch Bluetooth profile. A fallback also forgets the profile's layers, so `&profile_layer` keys do nothing again ([Profile layers](#profile-layers)).
- **One active connection in charge.** USB commands are accepted only while USB is ZMK's selected output. Bluetooth commands are accepted only while Bluetooth is selected and from the Mac on the active Bluetooth profile. Other bonded Macs can still ask what the pad is doing.
- **All or nothing.** A command is checked in full before any part of it runs. A bad colour or unknown layer changes nothing and is answered with `REJECTED`.
- **Your colours are yours.** A Profile colour is temporary. The pad keeps its own HSB and effect aside, and puts them back on fallback, when a Profile has no colour, and after deep sleep, restart or power loss. It never turns the lights on or changes animation speed. Only `SET_LIGHTING`, a change you make in Studio, changes the pad's own lighting ([Lighting](#lighting)).

## Lighting ownership

The physical pad has one 16-pixel WS2812 strip. Effects can draw different pixels, but a Profile colour, or a preview, is one HSB colour for the whole strip. It cannot address individual keys or segments. Layer and key lights, from firmware 0.8.0, can, and are drawn over it ([Layer and key lights](#layer-and-key-lights)).

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
- **Bit 7 was the last capability bit.** Studio 0.1.0 to 0.3.0 read `HELLO_ACK` as exactly 7 bytes and fail the handshake on a longer one, so `HELLO_ACK` cannot grow a second capability byte. Revision 6 spent bit 7 on `GET_FEATURES`: every feature after it is a bit of `FEATURES` instead ([Layer and key lights](#layer-and-key-lights)).
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

## Layer and key lights

Firmware 0.8.0 lets a host give every layer above Default a light, and every key on every layer, Default included, a light of its own. A light is what the LEDs show: Inherit, a colour (hue and saturation) or Off. The pad keeps them in its settings, per layer id and like each layer's dial, so they work without Studio, and draws them over its own lighting. This is not a Profile colour: a Profile colour stays temporary and goes through `SET_PROFILE` and `SET_LEDS` ([Lighting ownership](#lighting-ownership)), and the pad's own lighting is still changed only with `SET_LIGHTING` ([Lighting](#lighting)). This is protocol revision 6, announced by capability bit 7 (`MP_CAP_FEATURES`) and then by features 1 and 2 of `FEATURES`.

### Commands and events

| ID | Name | Direction | Payload | Who may send it | Answer |
| --- | --- | --- | --- | --- | --- |
| `0x0B` | `GET_FEATURES` | Mac to pad | none | Any host | `FEATURES` to that host only. Answered by `host.c` |
| `0x0D` | `SET_LAYER_LIGHT` | Mac to pad | 5 bytes | The active host only | `LAYER_LIGHT_STATE` to the sender, or `REJECTED` |
| `0x0E` | `GET_LAYER_LIGHT` | Mac to pad | 1 byte | Any host | `LAYER_LIGHT_STATE` to that host only |
| `0x0F` | `SHOW_PAD_LIGHTING` | Mac to pad | 1 byte | The active host only | None but `REJECTED`: the lights are the answer |
| `0x10` | `SET_KEY_LIGHT` | Mac to pad | 7 bytes | The active host only | A `KEY_LIGHTS_STATE` for each page that holds a key in `keys`, to the sender, or `REJECTED` |
| `0x11` | `GET_KEY_LIGHTS` | Mac to pad | 2 bytes | Any host | One `KEY_LIGHTS_STATE` to that host only |
| `0x8A` | `FEATURES` | pad to Mac | 1 to 16 bytes | | Sent by `host.c` |
| `0x8B` | `LAYER_LIGHT_STATE` | pad to Mac | 5 bytes | | |
| `0x8C` | `KEY_LIGHTS_STATE` | pad to Mac | 7 to 15 bytes | | |

`GET_FEATURES` and `FEATURES` belong to the host core (`host.c`) and not to the lights: they are answered on every build, so they are there whatever `CONFIG_MINIMALPAD_HOST_LIGHTS` says ([Features](#features)). `0x0C` is `SWITCH_PROFILE` ([Profile layers](#profile-layers)). Multi-byte fields are little-endian, as everywhere in this protocol. A payload longer than the one named here is from a later revision: each end reads the fields it knows and ignores the rest. Every answer goes only to the host that asked, as `DIAL_STATE` does. Unlike `LIGHTING_STATE`, nothing here is volunteered to other hosts.

### Features

`HELLO_ACK` has no capability bit left to give, so bit 7 announces `GET_FEATURES` instead, and `FEATURES` is a bitmap of up to 128 features: bit n is bit n mod 8 of byte n / 8. A pad sends as many bytes as its highest feature needs, so a feature past the end of the payload is one the pad does not have. This firmware sends one byte, `0x07`.

| Feature | Name | Commands and events | Built with |
| --- | --- | --- | --- |
| 0 | Profile layers | `SWITCH_PROFILE`, the `&profile_layer` key and `STATE`'s `change` byte ([Profile layers](#profile-layers)) | `CONFIG_MINIMALPAD_PROFILE_LAYERS` and `CONFIG_MINIMALPAD_PROFILE_LAYER_BEHAVIOR` |
| 1 | Layer lights | `SET_LAYER_LIGHT`, `GET_LAYER_LIGHT`, `LAYER_LIGHT_STATE`, `SHOW_PAD_LIGHTING` | `CONFIG_MINIMALPAD_HOST_LIGHTS` |
| 2 | Key lights | `SET_KEY_LIGHT`, `GET_KEY_LIGHTS`, `KEY_LIGHTS_STATE` | `CONFIG_MINIMALPAD_HOST_LIGHTS` |
| 3-127 | | Reserved | |

The host core answers `GET_FEATURES` and sets `MP_CAP_FEATURES` in `HELLO_ACK`, on every build. They do not depend on the lights, so a build with `CONFIG_MINIMALPAD_HOST_LIGHTS=n` still announces features and answers with only the bits it has: `0x01`, with profile layers on. A build with neither answers with one byte, `0x00`, so a host can tell "no features" from a pad that does not know the question. Bit 0 promises the `&profile_layer` key as well as `SWITCH_PROFILE`, so a build with `CONFIG_MINIMALPAD_PROFILE_LAYER_BEHAVIOR=n` leaves it clear, though it still handles `SWITCH_PROFILE`.

### The light payload

Every light is four bytes, in the same order in every command and event.

| Offset | Field | Type | Range | Notes |
| --- | --- | --- | --- | --- |
| 0 | `kind` | u8 | 0-2 | 0 Inherit (`MP_LIGHT_INHERIT`), 1 colour (`MP_LIGHT_COLOUR`), 2 Off (`MP_LIGHT_OFF`) |
| 1-2 | `hue` | u16 | 0-359 | Degrees. 0 unless `kind` is 1 |
| 3 | `saturation` | u8 | 0-100 | Percent. 0 unless `kind` is 1 |

- **Inherit** passes the decision on: a key to its layer's light, a layer to the layer below. **Off** is dark, whatever lies below.
- Inherit and Off carry hue 0 and saturation 0, in what a host sends and in every answer.
- There is no brightness. A colour is drawn at the pad's live brightness, so the brightness keys, the dial, the idle fade and Off act on every light at once.
- Key positions 0 to 15 are the grid, as ZMK's matrix numbers them, and bit n of a `keys` mask is position n. They are not the order of the LEDs along the chain, which the pad maps itself ([The renderer](#the-renderer)). The dial button, position 16, has no LED and is never in a mask.

### Command payloads

`SET_LAYER_LIGHT` is 5 bytes, 8 with the header: `layer_id` u8, then the light. `layer_id` is ZMK's stable layer id, not Default (0), which has no layer light: the pad's own lighting is Default's.

`GET_LAYER_LIGHT` is 1 byte: `layer_id` u8.

`SHOW_PAD_LIGHTING` is 1 byte: `seconds` u8 ([Showing the pad's lighting](#showing-the-pads-lighting)).

`SET_KEY_LIGHT` is 7 bytes, 10 with the header:

| Offset | Field | Type | Range | Notes |
| --- | --- | --- | --- | --- |
| 0 | `layer_id` | u8 | A layer in the keymap | Default may have key lights |
| 1-2 | `keys` | u16 | 1-`0xFFFF` | Bit n is key position n. At least one bit |
| 3-6 | light | | | As above |

`GET_KEY_LIGHTS` is 2 bytes: `layer_id` u8 and `first` u8 (0, 3, 6, 9, 12 or 15).

Every change a person makes is "these keys get this light": one key, a selection, or clearing a layer. So one `SET_KEY_LIGHT` carries it, and clearing a layer's key lights is `keys` = `0xFFFF` with Inherit.

### Event payloads

`LAYER_LIGHT_STATE` is 5 bytes, 8 with the header: `layer_id` u8, then the light. Default answers Inherit. A reserved layer slot answers what the pad has for that id: Inherit, unless a layer the browser Configurator removed left its light behind.

`KEY_LIGHTS_STATE` is 3 + 4 × `count` bytes, 7 to 15, so up to 18 with the header:

| Offset | Field | Type | Range | Notes |
| --- | --- | --- | --- | --- |
| 0 | `layer_id` | u8 | | |
| 1 | `first` | u8 | 0, 3, 6, 9, 12, 15 | The key position of the first light |
| 2 | `count` | u8 | 1-3 | How many lights follow. `first` + `count` never passes 16 |
| 3... | lights | 4 bytes each | | Key `first`, then `first` + 1, and so on |

A layer's 16 keys are six pages: keys 0-2, 3-5, 6-8, 9-11, 12-14 and key 15 alone. A page is named by its first key.

### Answers and rejections

The pad checks the whole frame before it applies any of it. A light equal to what the pad has changes nothing and schedules no save, and is still answered.

| Command | When | Answer |
| --- | --- | --- |
| `SET_LAYER_LIGHT` | Payload shorter than 5 bytes | `REJECTED`, command `0x0D`, reason 0 |
| `SET_LAYER_LIGHT` | Not from the active host | `REJECTED`, reason 3 |
| `SET_LAYER_LIGHT` | `layer_id` not below the layer count, or not a layer in the keymap while the light is anything but Inherit | `REJECTED`, reason 2 |
| `SET_LAYER_LIGHT` | `kind` above 2, `hue` above 359, `saturation` above 100, an Inherit or Off whose `hue` or `saturation` is not 0, or Default (`layer_id` 0) with anything but Inherit | `REJECTED`, reason 1 |
| `SET_LAYER_LIGHT` | Otherwise | Applied, then `LAYER_LIGHT_STATE` to the sender |
| `GET_LAYER_LIGHT` | Payload shorter than 1 byte | `REJECTED`, command `0x0E`, reason 0 |
| `GET_LAYER_LIGHT` | `layer_id` not below the layer count | `REJECTED`, reason 2 |
| `GET_LAYER_LIGHT` | Otherwise | `LAYER_LIGHT_STATE` to the host that asked |
| `SHOW_PAD_LIGHTING` | Payload shorter than 1 byte | `REJECTED`, command `0x0F`, reason 0 |
| `SHOW_PAD_LIGHTING` | Not from the active host | `REJECTED`, reason 3 |
| `SHOW_PAD_LIGHTING` | Otherwise | Applied. No answer |
| `SET_KEY_LIGHT` | Payload shorter than 7 bytes | `REJECTED`, command `0x10`, reason 0 |
| `SET_KEY_LIGHT` | Not from the active host | `REJECTED`, reason 3 |
| `SET_KEY_LIGHT` | `layer_id` not below the layer count, or not a layer in the keymap while the light is anything but Inherit | `REJECTED`, reason 2 |
| `SET_KEY_LIGHT` | `keys` 0, `kind` above 2, `hue` above 359, `saturation` above 100, or an Inherit or Off whose `hue` or `saturation` is not 0 | `REJECTED`, reason 1 |
| `SET_KEY_LIGHT` | Otherwise | Applied, then a `KEY_LIGHTS_STATE` for every page that holds a key in `keys`, in page order, up to six frames, to the sender only |
| `GET_KEY_LIGHTS` | Payload shorter than 2 bytes | `REJECTED`, command `0x11`, reason 0 |
| `GET_KEY_LIGHTS` | `layer_id` not below the layer count | `REJECTED`, reason 2 |
| `GET_KEY_LIGHTS` | `first` not 0, 3, 6, 9, 12 or 15 | `REJECTED`, reason 1 |
| `GET_KEY_LIGHTS` | Otherwise | One `KEY_LIGHTS_STATE` to the host that asked |

A reserved layer slot, one no layer has taken, can be cleared but not given a colour or Off: a host can clear a layer's lights after removing the layer and before the slot is taken again, as with `SET_DIAL`. Both `GET` commands answer for a reserved slot, with what the pad has for that id, which can be a light left by a layer the browser Configurator removed.

### The light rule

For each LED, from the highest level down. The first level that has something to say decides.

| Level | What | Notes |
| --- | --- | --- |
| 0 | Lights off | All LEDs dark, whatever else is set. Layer and key lights never switch the lights on |
| 1 | Pad status | Reserved for short, timed signals such as pairing or low battery. None exist yet |
| 2 | Showing the pad's lighting | While on, levels 3 and 4 step aside ([Showing the pad's lighting](#showing-the-pads-lighting)) |
| 3 | Key light on the highest active layer that sets one | Inherit, a colour or Off |
| 4 | Layer light of the highest active layer that sets one | A colour or Off. Inherit passes down a layer |
| 5 | The pad's own lighting | Colour or effect, brightness and speed, always running underneath |

- **Walking down.** Levels 3 and 4 are walked together, highest active layer first: on each layer the key's light, then the layer's. A layer's key light beats its own layer light, and both beat anything below that layer. Inherit means "keep walking". An unused layer slot is skipped. The walk ends at Default, as ZMK's walk for a key's binding does, so a layer ordered below Default, where the browser Configurator can move one, never decides a light.
- **Default.** It has no layer light, so the pad's own lighting is Default's. Its key lights come last in the walk, above the pad's own lighting: they show on any key that no active layer's key light or layer light decides, with or without Studio.
- **Host colour.** A Profile colour (`SET_PROFILE` with its LED flag, `SET_LEDS`) replaces the pad's own colour and effect at level 5. Layer and key lights are drawn over it.

### The renderer

An LED strip, `light_layers` (`src/leds/light_layers.c`, binding `minimalpad,light-layers`), sits between ZMK's underglow and the WS2812 chain. The overlay makes it the chosen `zmk,underglow`, so the underglow draws into it, and it passes the frame on to the real chain. The node says which chain it feeds (`led-strip`), how long it is (`chain-length`, 16), and which key each LED sits under, in chain order (`key-positions`): `0 1 2 3 7 6 5 4 8 9 10 11 15 14 13 12`. The chain snakes along the rows, so the order of the LEDs is not the order of the keys.

The underglow keeps computing its effect every 50 ms and hands each frame to the strip, which then, for each LED:

- asks `mp_lights_resolve()` for the key's light ([The light rule](#the-light-rule));
- draws a colour at the brightness the underglow draws at now, scaled between `CONFIG_ZMK_RGB_UNDERGLOW_BRT_MIN` and `CONFIG_ZMK_RGB_UNDERGLOW_BRT_MAX` the way ZMK scales its own;
- makes Off dark;
- leaves the underglow's own pixel for Inherit.

While the underglow is off, or the pad is showing its own lighting, the frame goes through as it is. Drawing over the underglow's frame, rather than into the chain beside it, keeps the underglow's lights off, brightness and idle fade working, and writes the chain once per frame, so nothing flickers.

The strip is built whether or not the host lights are (`CONFIG_MINIMALPAD_LIGHT_LAYERS`, on by default), because the shield's devicetree makes it the underglow's. Without `CONFIG_MINIMALPAD_HOST_LIGHTS` every key resolves to Inherit and every frame goes through unchanged.

The strip asks from the underglow's work queue, 16 times per frame, while commands write the lights from the system work queue and the settings load on their own thread. Every access to the lights is under one short spinlock, and nothing is saved or sent while it is held.

### Saving

- A change applies at once, and the pad saves it once changes have stopped for 2 seconds. Dragging a colour across its range costs one write. A restart or power cut inside those 2 seconds loses the last change, so a host does not say "saved" when the pad answers.
- A layer's light is the setting `mp_light/<layer id>`, 4 bytes. Its key lights are `mp_klight/<layer id>`, 64 bytes: 16 lights in key position order. A layer whose light is Inherit, or whose key lights are all Inherit, has no entry.
- Nothing is saved before the settings have loaded. The save runs on the system work queue, never from the settings commit: the commit runs on the thread loading the settings, which holds their lock until every commit has run, ZMK's Bluetooth start-up among them, and Bluetooth sends its commands from the system work queue. A save waiting for the lock there would hang the pad at start, as `lighting.c`'s `started` explains. So a change made before the load has finished is only marked, and nothing is scheduled for it. The commit then schedules the save for 2 seconds later. Starting that timer is not a settings call, and the first save comes at least 2 seconds after the commit, long after ZMK's Bluetooth commit has run and the lock has been let go.
- **A layer's dial is saved the same way**, from 0.9.0. `SET_DIAL` changes the dial at once and marks the layer, then answers with `DIAL_STATE` at once, without waiting for the save, so the answer no longer means the dial is saved. The save comes 2 seconds after the last change, from the system work queue, and never before the settings have loaded, for the reason above. Before 0.9.0 the command handler wrote the setting before it answered. A layer's dial is the setting `mp_dial/<layer id>`, 8 bytes: `cw`, then `ccw`, u32 each. A layer whose dial is cleared has no entry. A restart or power cut inside the 2 seconds loses the last change, so a host does not say "saved" when the pad answers.
- A saved light is ignored, with a warning, when it is not a light the pad would accept: a kind above 2, a hue or saturation out of range, an Inherit or Off with a hue or saturation, a layer light on Default that is not Inherit, or any of a layer's 16 key lights. That layer then starts as Inherit. An entry of the wrong length is refused.
- Studio clears a layer's light and key lights when it makes or removes the layer, as it does the layer's dial, so a reused layer id starts clean. Clearing an empty slot is accepted.

### Showing the pad's lighting

Layer and key lights are drawn over the pad's own lighting, so a change to its colour, effect or speed would be hidden under them. While the pad shows its own lighting, level 2 of the rule, the renderer passes the underglow through in every pixel, key lights set to Off included.

- **From a host.** `SHOW_PAD_LIGHTING` shows it for `seconds` from when the pad gets the command. Studio asks while its Lights popover is open and renews it every few seconds. `0` stops it at once.
- **From the pad.** When a key or the dial changes the pad's own colour (hue or saturation), effect or speed, the pad shows its own lighting for 3 seconds. Brightness is not one of them, because it shows through the layer and key lights, and neither is switching off, which is dark anyway. A host's `SET_LIGHTING` and the lights starting or stopping their rest for idle do not show it either, nor does a host colour starting or stopping.
- **A show never shortens a longer one.** A call moves the end of a show only when it makes it later, so the 3 seconds after a key do not cut short the ten that Studio asked for. `0` is the only call that ends one early.
- It is not saved, and it needs no heartbeat: a restart shows the layer and key lights again, and a show ends by itself, so a host that goes away leaves nothing behind.

### Compatibility

- **A host turns layer lights on from feature 1 and key lights from feature 2, after `HELLO_ACK`'s capability bit 7, and from nothing else.** Firmware without bit 7 skips `0x0B` silently, and firmware without a feature skips that feature's commands the same way, so a host that sent one and waited would only time out.
- **Older Studio on this firmware** is unaffected. `HELLO_ACK` is still 7 bytes, bit 7 means nothing to it, and it skips `0x8A` to `0x8C` by their length.
- **Lights under an older Studio's Profile colour.** Studio 0.4 and earlier never set layer or key lights, but a pad can still have some, set by a newer Studio. While an older Studio holds a Profile colour (`SET_PROFILE` with its LED flag), those lights are drawn at that colour's brightness, because they follow the underglow's live brightness. The Colour keys then change the Profile colour, not the pad's own, so they don't start the 3-second show ([Showing the pad's lighting](#showing-the-pads-lighting)). Both are as intended, and only someone who set lights with a newer Studio and went back to an older one sees them.
- **`LAYER_LIGHT_STATE` and `FEATURES` grow by trailing fields.** A host reads the five bytes of a layer light, and as many of the 16 bytes of `FEATURES` as it has features for, and ignores the rest. A host reads `count` lights of a `KEY_LIGHTS_STATE`.
- **`STATE` and `LIGHTING_STATE` do not change.**
- **USB and Bluetooth behave the same**: both transports share the frame reader and the handlers. A `SET_KEY_LIGHT` for a whole layer answers with six `KEY_LIGHTS_STATE` frames from the system work queue. There Zephyr cannot wait for an ATT buffer, and sends nothing until the handler returns, because its transmit work runs on the same queue. With Zephyr's 3 buffers the 4th answer onwards would be lost over Bluetooth, so `minimalpad.conf` sets `CONFIG_BT_ATT_TX_COUNT=10`, which costs 1,176 B of RAM. Key reports and ZMK Studio's answers draw on the same buffers.

### How Studio uses it

These are Studio's rules. They sit here too, so both ends implement the same thing.

- Ask `GET_FEATURES` once the link is ready, and only when `HELLO_ACK` has bit 7.
- Read a layer's key lights with six `GET_KEY_LIGHTS`, one for each `first`. A change for several keys is one `SET_KEY_LIGHT`, and the pages it answers with are the ones that hold those keys.
- Check the ranges before sending and never clamp.
- Never say "saved" when the pad answers. Nothing of the pad's lighting is stored on the Mac.

## Profile layers

Firmware 0.9.0 lets a profile have several layers, and the pad switches between them by itself. That is instant and needs no round trip to the Mac, so it works over a flaky Bluetooth link, and a hold keeps its press and release timing. Studio tells the pad which layers belong to the profile it holds, with `SWITCH_PROFILE`, and `&profile_layer` keys on the pad move between them. This is protocol revision 7, announced by feature 0 of `FEATURES` ([Features](#features)). It covers `SWITCH_PROFILE`, the `&profile_layer` key and the `change` byte on the end of `STATE`.

### Command and event

| ID | Name | Direction | Payload | Who may send it | Answer |
| --- | --- | --- | --- | --- | --- |
| `0x0C` | `SWITCH_PROFILE` | Mac to pad | 3 to 17 bytes | The active host only | `STATE` with `change` 0, to every host listening, or `REJECTED` to the sender |
| `0x85` | `STATE` | pad to Mac | 7 bytes | | Grows by `change`, below |

A payload longer than the one named here is from a later revision: each end reads the fields it knows and ignores the rest.

### `SWITCH_PROFILE`

`SET_PROFILE` with no flags, plus the profile's layers, in one frame, so a switch cannot half-apply. 2 + `count` bytes, 3 to 17, so at most 20 with the header.

| Offset | Field | Type | Range | Notes |
| --- | --- | --- | --- | --- |
| 0 | `layer_id` | u8 | One of the ids | ZMK's stable layer id of the layer to switch to |
| 1 | `count` | u8 | 1-15 | How many ids follow |
| 2... | layer ids | u8 each | A layer in the keymap, not Default, none twice | The profile's layers, in the order Next goes through them |

A pad reads `count` ids and ignores what trails them, as a later revision adds fields there.

- **An app switch.** Like `SET_PROFILE`, it switches layers as `&to` does: the layer it names is the one profile layer left on, and a layer toggled on by hand ends there. Only the swaps inside a profile keep other layers ([The swap](#the-swap)).
- **The answer.** `STATE` with `change` 0. The pad sends a `STATE` when anything in it differs from the last it sent, `change` included, so a switch to the layer a key had chosen still brings one: that key's `STATE` said `change` 1.
- **A host colour.** If an older Studio's host colour was showing, it is put back, as a `SET_PROFILE` with no flags puts it back.
- **The heartbeat.** Like `SET_PROFILE`, it opens the heartbeat window if none is running, and only the active host may send it. A hold still down ends with the switch, and its key's release does nothing.

The pad checks the whole frame before it applies any of it, so a bad one changes nothing. The rows are checked in order, so a frame wrong in two ways gets the first row's reason.

| When | Answer |
| --- | --- |
| Payload shorter than 3 bytes, or than 2 + `count`. Every `count` above 15 is here: no frame holds that many ids | `REJECTED`, command `0x0C`, reason 0 |
| Not from the active host | `REJECTED`, reason 3 |
| `count` 0, `layer_id` not among the ids, Default among the ids, or an id twice | `REJECTED`, reason 1 |
| An id that is not a layer in the keymap | `REJECTED`, reason 2 |
| Otherwise | Applied, then `STATE` with `change` 0 |

### The group

The group is the profile's layers as the pad knows them: an ordered list of up to 15 layer ids, kept in RAM.

- `SWITCH_PROFILE` sets it.
- `SET_PROFILE`, a fallback and a release (`HEARTBEAT` with `timeout` 0) clear it.
- It is never saved. It belongs to the host that holds the pad, and a pad that restarts is held by no host.

### The `&profile_layer` key

A key behavior, not a frame. It takes two parameters, a mode and a layer id, and ZMK Studio shows it as "Profile Layer":

| Parameter | Meaning |
| --- | --- |
| `param1` | The mode, below: `PL_NEXT`, `PL_GO` or `PL_HOLD` from `dt-bindings/minimalpad/profile_layer.h`. ZMK Studio shows them as "Next layer", "Go to" and "Hold for" |
| `param2` | A layer id: the layer to go to, or to hold for. 0 for Next |

In a keymap that is `&profile_layer PL_NEXT 0` or `&profile_layer PL_GO 5`. No key of the default keymap uses it: Studio puts it on a key with ZMK Studio's `set_layer_binding`. ZMK checks the binding against the behavior's metadata first, so that has two sets, as `&bt` has: Next alone (`param2` 0), and Go to or Hold for with a layer id.

| Mode | Value | Press | Release |
| --- | --- | --- | --- |
| Next | 0 | Swap to the next layer in the group, wrapping round, and skipping a layer the keymap no longer holds. The group's first when the pad is on none of its layers, as it is during Hold for Default | — |
| Go to | 1 | Swap to the layer, if it is in the group | — |
| Hold for | 2 | Remember the layer the pad is on, then swap to the target: a layer in the group, or Default, which means the profile layer goes off | If the hold is still current, swap back to the remembered layer |

The key does nothing, and the press goes no further, when:

- no host holds a profile with a group;
- Go to or Hold for names a layer that is not in the group. Default is a target for Hold for only, and only with a group held, and a layer of another profile is never a target;
- Go to or Hold for names a layer the keymap no longer holds. `SWITCH_PROFILE` checks the group's layers once, and ZMK Studio can remove one afterwards. Next skips such a layer;
- it is a Next with no other layer in the group, in the keymap, to go to, as in an app with one layer;
- it is a Hold for on a key position past the keys, such as a combo's: it has nowhere to keep the hold. Next and Go to work there.

A key that does nothing leaves a hold that is current as it is.

### The swap

A swap switches the profile layer and leaves everything else as it was: it turns off the profile layer the pad is on, turns on the target, sets the layer the host last set to the target, and sends `STATE`.

- It does not depend on the order of the layers, so Hold for Default and Hold for a sibling both work, and a pad layer held or toggled on above the profile survives. An app switch is not a swap.
- Default is never turned off or on. ZMK keeps it on, and it is what shows when the profile layer goes off.
- Lights and dials follow without work of their own. A profile layer is an ordinary layer to the light rule ([The light rule](#the-light-rule)) and to the dial, so the next frame draws the new layer's lights and a turn gets its dial.
- The group and the hold are under one short spinlock, so they are always read and changed whole. The swaps themselves rest on every caller running on the system work queue: both transports' commands, the matrix scan and the sensors, and so the behaviors ZMK raises from them, all do at the ZMK revision `config/west.yml` pins.

### Holds

- **Press.** The pad remembers the layer it is on, unless a hold is already current, and swaps to the target. `STATE` carries `change` 2.
- **Release.** If the hold is still current, the pad swaps back to the layer it remembered. `STATE` carries `change` 3.
- **A hold stops being current** when anything else moves the profile layer: Next, Go to, another hold, `SWITCH_PROFILE`, `SET_PROFILE`, a fallback or a release. The release of a hold that is no longer current does nothing, and the move that ended it stands.
- **Two holds.** The newest hold wins and keeps the layer the first one left. Letting go of the older key does nothing, and letting go of the newest, or of every hold key in any order, ends where the first went down.
- **A Hold for the layer already shown** begins silently: nothing moves and no `STATE` goes out. Its release reports `change` 3 (hold ended).
- **A Go to that ends a hold on the layer already shown** reports `change` 1, though nothing moves, so a host learns the layer is now the key's choice and not the hold's. With no hold current, a Go to the layer already shown does nothing. A Next never lands on the layer already shown: with no other layer to go to it does nothing, and a hold stays current.
- **During Hold for Default** `STATE`'s `layer_id` is Default's id. A Next then ends the hold and goes to the group's first layer.
- The pad does not volunteer a `STATE` that would repeat the last it sent in `layer_id`, `top_layer_id`, `endpoint`, `leds_on`, `bt_profile` and `change`, so a change to `battery` alone never sends one. Three kinds always go out, repeat or not: the reply to `GET_STATE`, the `STATE` sent when a host arrives (the event characteristic subscribed, or a `HELLO` answered), and the one sent when the active Bluetooth profile reports a change, including connecting or pairing. So when the last `STATE` already says the same layer and the same `change`, as it does for a Hold for the layer already shown, released right after another hold ended there (`change` 3), nothing goes out: a host already has what it would say.

### The `change` byte

`change` is the seventh byte of `STATE`, after `bt_profile`, and explains a change of `layer_id`. Firmware with feature 0 sends it with every `STATE`, and always sends `bt_profile` before it, `0xFF` if the pad has no slot.

| Value | Name | Meaning |
| --- | --- | --- |
| 0 | `MP_LAYER_CHANGE_HOST` | The host, or anything else: `SET_PROFILE`, `SWITCH_PROFILE`, a fallback or a release, and the pad's start |
| 1 | `MP_LAYER_CHANGE_KEY` | A Next layer or Go to key |
| 2 | `MP_LAYER_CHANGE_HOLD_BEGAN` | A Hold for key went down |
| 3 | `MP_LAYER_CHANGE_HOLD_ENDED` | A Hold for key came up |

`layer_id` keeps its meaning, the profile layer the pad is on for the host. `top_layer_id` is still the highest active layer, so the two differ while a pad layer is held above the profile. `change` stays until the layer next moves, so a `STATE` sent for another reason, such as a Bluetooth slot changing, repeats the last: it says why `layer_id` last moved, not that a key was just pressed. A host reads a value above 3 as 0. A `STATE` of six bytes or fewer says nothing about why the layer changed, which is not the same answer as 0.

### Compatibility

- **A host turns profile layers on from feature 0, and from nothing else.** Firmware without it skips `0x0C` silently, as firmware before 0.9.0 does and a build with `CONFIG_MINIMALPAD_PROFILE_LAYERS=n` does, so a host that sent one and waited would only time out. A build with `CONFIG_MINIMALPAD_PROFILE_LAYER_BEHAVIOR=n` has no `&profile_layer` key, so it leaves the bit clear too.
- **Studio 0.5.0 and older on this firmware** are unaffected. `HELLO_ACK` is still 7 bytes, with `proto` 7 and the same capability bits. They read the bytes of `STATE` they know and ignore the seventh. Studio 0.5.0, the first to ask `GET_FEATURES`, uses features 1 and 2 and does nothing with bit 0. They switch with `SET_PROFILE` as they always did, which clears the group, so a `&profile_layer` key set by a newer Studio does nothing under them.
- **Studio 0.6.0 on firmware 0.8.0.** `FEATURES` is `0x06`, so feature 0 is clear. Studio sends no `SWITCH_PROFILE`, offers no `&profile_layer` key and switches with `SET_PROFILE`, to the profile's last layer, as before. A profile that already has several layers shows the others need firmware 0.9.0 to switch on the pad, and adding a layer offers the update.
- **`STATE` grows by a trailing field.** A host reads the bytes it knows and ignores the rest, from its first version: a decoder that refused a longer `STATE` would fix it at its size for good.
- **USB and Bluetooth behave the same**: both transports share the frame reader and the handlers.

### How Studio uses it

These are Studio's rules. They sit here too, so both ends implement the same thing.

- Turn profile layers on from feature 0, and from nothing else. On a pad without it Studio switches with `SET_PROFILE` as before, to the last layer chosen.
- Send `SWITCH_PROFILE` with each app switch: the profile's last layer, or its first (Home) when none is remembered, and the profile's healthy layers in order as the group.
- Count the switch once a `STATE` names its layer (ADR 0016 in the MinimalPad Studio for Mac repo). `REJECTED` for `0x0C` fails the switch, as it does for `0x02`.
- A `STATE` read after the newest confirmation that names a layer in the held group means the pad changed layer by itself. With `change` 1 the layer is confirmed and wanted, and the profile remembers it as its last layer. No new switch is sent. With `change` 2 the held layer is confirmed for display only: what Studio wants, and the last layer, stay. With `change` 3 the confirmed layer goes back.
- A `STATE` numbered before the newest confirmation never confirms anything. With `change` 1 it still updates the last layer of the profile that owns its layer.
- After a fallback caused by a lost heartbeat, apply the wanted layer again, which is the last one chosen.
- When the held profile's layers change, a layer added, deleted, moved, or healthy again, send `SWITCH_PROFILE` again with the new group, so the pad never cycles through a stale list. It stays on the layer the pad is on, or goes Home if that layer was deleted.

## Settings

Add any of these to `boards/shields/minimalpad/minimalpad.conf`:

| Option | Default | What it does |
| --- | --- | --- |
| `CONFIG_MINIMALPAD_HOST` | `y` | The module itself. Set `n` to build without it |
| `CONFIG_MINIMALPAD_HOST_USB` | `y` in the Studio USB build | Carry Profile control beside ZMK Studio on its one serial port |
| `CONFIG_MINIMALPAD_HOST_USB_TX_BUFFER_SIZE` | `512` | Bytes reserved while Studio and host replies share USB |
| `CONFIG_MINIMALPAD_HOST_HEARTBEAT_DEFAULT_TIMEOUT` | `6` | Seconds before falling back, until Studio's first heartbeat sets its own |
| `CONFIG_MINIMALPAD_MAC_ACTION` | `y` | The `&mac_action` key. Set `n` to build without it; the pad then no longer claims Mac actions in `HELLO_ACK` |
| `CONFIG_MINIMALPAD_HOST_DIAL` | `y` | `&host_dial` and each layer's dial, saved 2 seconds after the last change. Set `n` to build without it; the pad then no longer claims layer dials in `HELLO_ACK` |
| `CONFIG_MINIMALPAD_HOST_DIAL_SCROLL_STEP` | `1` | Wheel steps per step of the dial when a profile's dial scrolls |
| `CONFIG_MINIMALPAD_HOST_LIGHTING` | `y` | Studio reads and sets the pad's own lighting. Needs `CONFIG_ZMK_RGB_UNDERGLOW`. Set `n` to build without it; the pad then no longer claims lighting in `HELLO_ACK`, and colours changed with keys go back to waiting for ZMK's next save |
| `CONFIG_MINIMALPAD_HOST_LIGHTS` | `y` | Layer and key lights, kept on the pad and set from Studio. Needs `CONFIG_ZMK_RGB_UNDERGLOW`. Set `n` to build without it; `FEATURES` then lacks bits 1 and 2, and every key shows the underglow. `GET_FEATURES` stays, because the host core answers it |
| `CONFIG_MINIMALPAD_PROFILE_LAYERS` | `y` | Several layers for one profile: `SWITCH_PROFILE`, the group the pad keeps while Studio holds it, and `STATE`'s `change` byte. Set `n` to build without it; `FEATURES` then lacks bit 0 and the pad skips `SWITCH_PROFILE` as an unknown command. The bit also needs `CONFIG_MINIMALPAD_PROFILE_LAYER_BEHAVIOR` |
| `CONFIG_MINIMALPAD_PROFILE_LAYER_BEHAVIOR` | `y` | The `&profile_layer` key. Needs `CONFIG_MINIMALPAD_PROFILE_LAYERS`. Set `n` to build without it; `FEATURES` then lacks bit 0, because the bit promises this key |
| `CONFIG_MINIMALPAD_LIGHT_LAYERS` | `y` | The LED strip that draws those lights over the underglow. Built with or without the host module, because the shield's devicetree makes it the underglow's. Set `n` only after pointing the overlay's `zmk,underglow` back at the chain |
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

Firmware 0.8.0's layer and key lights, measured on 11 Oct 2026 the same way, take `minimalpad_with_studio` to 255,008 B flash and 82,054 B RAM, against 252,220 B and about 79.7 KB for 0.7.1: about 2.8 KB more flash and 2.3 KB more RAM. 1,176 B of that RAM is `CONFIG_BT_ATT_TX_COUNT=10`.

Firmware 0.9.0's profile layers and the dial's save, measured on 12 Oct 2026 the same way (the Docker build of `minimalpad_with_studio`, with ZMK at the revision `config/west.yml` pins), take `minimalpad_with_studio` to 257,096 B flash and 82,214 B RAM. The v0.8.0 tag built the same way gives 255,056 B and 82,054 B, 48 B more flash than the figure above, which was taken before the last pairing fix. So 0.9.0 is 2,040 B more flash and 160 B more RAM. A build with `CONFIG_MINIMALPAD_HOST_LIGHTS=n` also links: 254,500 B flash and 81,062 B RAM. So does one with `CONFIG_MINIMALPAD_PROFILE_LAYER_BEHAVIOR=n`: 256,012 B flash and 82,134 B RAM.

## Testing

Flash a spare pad first, since the Studio build changes the layer count. The checklist is in the MinimalPad Studio for Mac repo, `.scratch/host-module/issues/01-test-on-a-pad.md`. Bluetooth Profile switching has passed. USB switching, USB fallback and the lighting cases remain to be run on hardware.

The 0.8.0 checks for layer and key lights passed over USB on 11 Oct 2026, on the CI build of `54d402a`: the handshake (revision 6, layer and key lights), setting and reading back a key light and a layer light, each lighting the right LEDs, a key light on Default lasting through a restart, brightness, the idle fade, lights off, a host's `SHOW_PAD_LIGHTING` and the 3-second show after a Colour key. Two Bluetooth checks are still to run: that a whole-layer `SET_KEY_LIGHT` brings all six `KEY_LIGHTS_STATE` pages, and switching apps quickly while a colour is dragged. Their results go in the 0.8.0 pull request.

The 0.9.0 checks passed on 12 Oct 2026. Over USB, on the CI build of `c074022`, with `minimalpadctl host`: the handshake (revision 7, profile layers, layer and key lights); `&profile_layer` bindings accepted and a bad Next refused; `SWITCH_PROFILE` confirmed with `change` host; Next and Go to swapping with `change` key; Hold for Default beginning and ending a hold; a toggled pad layer surviving Next and Go to; a hold released after another switch doing nothing; refusals with reason 1 (Default in a group) and 2 (no such layer); and coloured layers swapping with no flash of the pad's own lighting. Over Bluetooth, on the CI build of `868a936` (the code this release is built from), with MinimalPad Studio for Mac 0.6.0: Next layer on the dial press stepping through an app's layers, each in its own colour; the pad coming back to the layer used last, also after Studio restarts; adding, renaming and deleting layers with long names, which froze the pad before `aa2036b`; and Hold for Default. Still to run: a dial set right after a restart, and a hold interrupted by an app switch.

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
