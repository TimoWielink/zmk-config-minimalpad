![MinimalPad Firmware](https://cdn.prod.website-files.com/67a8fe2ace8968bc2e48ba6c/6aaae79f281e8ecf0c5c70f7_minimalpad-firmware-github-banner.png)

# MinimalPad firmware

ZMK firmware for the MinimalPad macropad: 16 keys, a push-button dial and RGB underglow, on a nice!nano v2 or a compatible clone, over USB or Bluetooth.

**Firmware version: 0.3.0.** What changed in each version is in [CHANGELOG.md](CHANGELOG.md).

- **Website:** [minimalmacropad.com](https://minimalmacropad.com)
- **MinimalPad Studio, web version:** [studio.minimalmacropad.com](https://studio.minimalmacropad.com), in Chrome or Edge
- **MinimalPad Studio for Mac:** coming soon. The download link will be added here.
- **Docs:** [docs.minimalmacropad.com](https://docs.minimalmacropad.com), which are being updated

You don't need to build anything to change what a key does: open MinimalPad Studio and connect your pad. Build this firmware yourself to change what Studio can't, such as what turning the dial does, or when the lights go out and the pad sleeps.

## Which file to flash

Every build makes two files:

| File | Flash it to |
| --- | --- |
| `minimalpad_with_studio` | Change your keys with MinimalPad Studio, on the web or on Mac. Profiles in Studio for Mac need it too. |
| `minimalpad` | Keep the keys set in the keymap, without ZMK Studio |

On GitHub Actions, a build is one download, such as `minimalpad-v0.3.0+abcdef0`: a zip that holds `minimalpad_with_studio.uf2` and `minimalpad.uf2`. A GitHub Release holds the same files named with their version, such as `minimalpad_with_studio-v0.3.0.uf2`. Until the first release is published, download a build from Actions.

## Flash the firmware

1. Download a build. In the **Actions** tab of this repo, or of your fork, open the latest run with a green tick and download the file under **Artifacts**. Unzip it.
2. Connect the MinimalPad to your computer with a USB cable that carries data. A charge-only cable won't work.
3. Press the pad's reset button twice, quickly. A drive called NICENANO appears.
4. Copy the `.uf2` file onto that drive. The pad restarts with the new firmware.

If the drive doesn't appear, try another cable or USB port, or press reset a little faster or slower.

## Build your own keymap

1. **Fork this repo.** Click **Fork** at the top of [its GitHub page](https://github.com/TimoWielink/zmk-config-minimalpad).
2. **Turn on Actions in your fork.** Open your fork's **Actions** tab and enable its workflows. GitHub keeps them off in a new fork.
3. **Edit** `boards/shields/minimalpad/minimalpad.keymap`, in GitHub's web editor or in a clone of your fork:

   ```sh
   # Replace YOUR-USERNAME with your GitHub username
   git clone https://github.com/YOUR-USERNAME/zmk-config-minimalpad.git
   cd zmk-config-minimalpad
   ```

4. **Commit and push.** A push that changes the firmware starts a build; one that only changes the README or docs doesn't. To start a build yourself, open **Actions**, pick **Build ZMK firmware** and click **Run workflow**.
5. **Flash** the build, as above.

To build on your Mac instead, see [Building locally](docs/host-module.md#building-locally).

### The keymap

The keymap has two layers. Each lists 17 bindings: the 16 keys, row by row from the top left, then the dial's button. `sensor-bindings` sets what turning the dial does. This is the first layer:

```c
media_layer {
    display-name = "Media";
    bindings = <
        &kp C_PREV      &kp C_PP        &kp C_NEXT      &kp C_MUTE
        &kp C_VOL_DN    &kp C_VOL_UP    &kp C_STOP      &kp C_EJECT
        &kp C_RW        &kp C_FF        &kp C_BRI_DN    &kp C_BRI_UP
        &kp C_VOL_DN    &kp C_VOL_UP    &kp C_PP        &mo BTLED
        &kp C_PP
    >;
    sensor-bindings = <&inc_dec_kp C_VOL_UP C_VOL_DN>;
};
```

**Media.** Turning the dial changes the volume, and pressing it plays or pauses.

| | Key 1 | Key 2 | Key 3 | Key 4 |
| --- | --- | --- | --- | --- |
| **Row 1** | Previous track | Play/pause | Next track | Mute |
| **Row 2** | Volume down | Volume up | Stop | Eject |
| **Row 3** | Rewind | Fast-forward | Screen brightness down | Screen brightness up |
| **Row 4** | Volume down | Volume up | Play/pause | Hold for BT + LED |

**BT + LED**, while you hold the bottom-right key (`&mo BTLED`). Turning the dial changes the lights' brightness.

| | Key 1 | Key 2 | Key 3 | Key 4 |
| --- | --- | --- | --- | --- |
| **Row 1** | Bluetooth slot 1 | Bluetooth slot 2 | Bluetooth slot 3 | Bluetooth slot 4 |
| **Row 2** | Bluetooth slot 5 | Next slot | Previous slot | Switch between USB and Bluetooth |
| **Row 3** | Lights brighter | Lights dimmer | Animation faster | Animation slower |
| **Row 4** | Hue up | Next effect: Solid, Breathe, Spectrum or Swirl | Lights on/off | Held |

To change a key, replace its binding. `&kp` sends a key: `&kp A` types A, `&kp LC(C)` sends Ctrl+C and `&kp LG(C)` sends Cmd+C on a Mac. The keycodes are in ZMK's [list of keycodes](https://zmk.dev/docs/keymaps/list-of-keycodes), and everything else a binding can do is in its [keymap docs](https://zmk.dev/docs/keymaps).

After the two layers come 14 empty ones, marked `status = "reserved"`. Only the `minimalpad_with_studio` build includes them, as room for profiles in MinimalPad Studio for Mac.

## MinimalPad Studio support

The firmware includes the **host module**, which MinimalPad Studio for Mac uses to follow the app you're working in:

- **Profiles.** When you switch to an app that has a profile, such as Figma, Studio for Mac switches the pad to that app's layer.
- **Colour.** A profile can light the pad in one colour while it's active.
- **Safety net.** Studio for Mac sends a heartbeat every 2 seconds. If it stops, because Studio quits, the Mac sleeps or the pad goes out of range, the pad returns to its Default layer (the first one in the keymap) and its own colours within 6 seconds.

This works over USB and Bluetooth. Without Studio for Mac running, the pad does exactly what its keymap says. Profiles need the `minimalpad_with_studio` build: the plain `minimalpad` build carries the module but has no room for them. The module is on by default and doesn't change how you build or flash. How it works, its settings and how to turn it off are in [docs/host-module.md](docs/host-module.md).

## Lighting, idle and battery

The lighting keys are on the BT + LED layer, in the table above. The pad saves what you set a moment after you change it. Speed only shows in the animated effects: Breathe keeps the hue and changes the brightness, while Spectrum and Swirl change the hue.

A profile's colour in Studio for Mac is separate and temporary. While it shows, the pad uses Solid, and when the profile ends, the pad goes back to its own colour and effect. The details are in [Lighting ownership](docs/host-module.md#lighting-ownership).

- **Idle.** 30 seconds after your last key press or dial turn, the lights fade out over 8 seconds, then switch off and their power is cut. The pad stays connected. A key press or dial turn brings the lights straight back and still does its job.
- **Sleep.** On battery, the pad deep-sleeps after 12 hours without a key press or dial turn, for example overnight. Bluetooth disconnects while it sleeps. The key press that wakes it only wakes it, and it reconnects within a few seconds. On USB it never sleeps.
- **Battery.** The LEDs are what drain the battery. By ZMK's own estimates, a nice!nano v2 with its LEDs off draws about 40 µA while connected and about 20 µA asleep, while 16 lit LEDs can draw over 100 mA at full brightness. Staying connected with the lights off costs very little.

To change this, edit `boards/shields/minimalpad/minimalpad.conf`:

| Setting | Now | What it does |
| --- | --- | --- |
| `CONFIG_ZMK_IDLE_TIMEOUT` | `30000` | Milliseconds without a key press or dial turn before the lights start to fade |
| `CONFIG_MINIMALPAD_LEDS_IDLE_FADE_MS` | `8000` | How long the fade takes. `0` switches the lights off at once |
| `CONFIG_ZMK_IDLE_SLEEP_TIMEOUT` | `43200000` | Milliseconds without a key press or dial turn before deep sleep, on battery (12 hours) |
| `CONFIG_ZMK_SLEEP` | `y` | Set to `n` and the pad never deep-sleeps |

Good to know:

- Breathe sets its own brightness, so it doesn't dim. It keeps pulsing until the fade time is up, then switches off.
- If the pad restarts while its lights are off for idle, for example waking from sleep, a reset or a flat battery, the lights come back on. Firmware before 0.2.0 left them off, so the first time you flash this version, the lights may stay dark. Switch them on once: hold the bottom-right key and press the third key on the bottom row.
- The fade is in `src/leds/idle_fade.c`. It takes the place of ZMK's `CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE`, which switches the lights off at once.

## Versions

The firmware uses [semantic versioning](https://semver.org). Its version, now `0.3.0`, is in the `VERSION` file at the root of this repo, the only place to change it. The firmware reports it to MinimalPad Studio for Mac, and each build on GitHub Actions is named after it:

| Build | What you download |
| --- | --- |
| A push to a branch | `minimalpad-v0.3.0+abcdef0`: the version, then the commit it was built from |
| A release tag | `minimalpad-v0.3.0`, and a GitHub Release with the `.uf2` files |

After a release, raise the version in the first commit that changes the firmware. Changes to the README or docs don't count. Pick the part by the biggest change until the next release:

- **Patch** (`0.3.0` to `0.3.1`): fixes only.
- **Minor** (`0.3.1` to `0.4.0`): something new that works with your setup as it is, such as the idle fade.
- **Major** (`0.4.0` to `1.0.0`): a change that needs something redone, such as a new layer layout, pairing again, or updating MinimalPad Studio to reach the pad.

To release a version:

1. In [CHANGELOG.md](CHANGELOG.md), write what it changes under a heading with its number, such as `## 0.3.0`. This becomes the release's description, so write it for the people who will flash it.
2. Tag the commit whose `VERSION` has that version, and push the tag:

   ```sh
   git tag v0.3.0
   git push origin v0.3.0
   ```

GitHub Actions builds the firmware and publishes a release called "MinimalPad firmware v0.3.0". It holds `minimalpad_with_studio-v0.3.0.uf2` and `minimalpad-v0.3.0.uf2`. Its description is your changelog section, followed by which file to flash and how ([.github/release-notes.md](.github/release-notes.md)). Before building, it stops if the tag and `VERSION` disagree or the changelog has nothing for that version. Then delete the tag with `git tag -d v0.3.0` and `git push origin :v0.3.0`, fix what the error says, and tag again.

## More

- [MinimalPad docs](https://docs.minimalmacropad.com)
- [ZMK documentation](https://zmk.dev/docs)
- [ZMK keymaps](https://zmk.dev/docs/keymaps) and [list of keycodes](https://zmk.dev/docs/keymaps/list-of-keycodes)
