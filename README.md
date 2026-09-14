![Imgur](https://i.imgur.com/PcZ9SqD.png)


# Customizing Your Keymap for MinimalPad (ZMK)

This guide will help you customize the keymap for your **MinimalPad**, a 4x4 macropad built as a shield for ZMK. By following these steps, you can create your own keymap and flash it onto your device.

## About MinimalPad
MinimalPad is a compact, customizable macropad designed to be used either wired or wirelessly. It is compatible with ZMK firmware and works seamlessly with microcontrollers such as the **NiceNano V2 clone**, which enables Bluetooth functionality. This allows users to configure and use the macropad in various setups, whether as a USB-powered device or as a fully wireless peripheral. 

## Prerequisites
- A GitHub account
- Basic knowledge of Git and YAML
- A MinimalPad with a compatible microcontroller (e.g., NiceNano V2 clone)

## 1. Fork the Repository

To customize your keymap, first fork the official MinimalPad configuration repository:

[MinimalPad ZMK Config Repository](https://github.com/TimoWielink/zmk-config-minimalpad)

Click the **Fork** button in the top right corner to create your own copy of the repository.

## 2. Clone Your Fork

Once you've forked the repository, clone it to your local machine:

```sh
# Replace "your-username" with your GitHub username
git clone https://github.com/your-username/zmk-config-minimalpad.git
cd zmk-config-minimalpad
```

## 3. Modify Your Keymap

The keymap is defined in the following file:

```plaintext
boards/shields/minimalpad/minimalpad.keymap
```

### Understanding the Keymap Structure

The keymap file is written in Devicetree format and consists of multiple layers. Here’s an example of a single row:

```c
        default_layer {
            bindings = <
                &kp N7    &kp N8    &kp N9    &kp BSPC
            >;
        };
```

Each key is defined using `&kp` (key press) followed by the desired keycode. For example, to change `N7` to `A`:

```c
&kp A
```

### Adding a Shortcut
To define a shortcut, use key combinations. For example, to add **Ctrl + C** (copy) to a key:

```c
&mt LCTRL C
```

You can refer to the [ZMK Keycode Documentation](https://zmk.dev/docs/features/keymaps) to find available keycodes.

### Saving and Pushing Changes
Once you've modified the keymap, save the file and push your changes:

```sh
git add boards/shields/minimalpad/minimalpad.keymap
git commit -m "Updated keymap"
git push origin main
```

## 4. Build Firmware Using GitHub Actions

Once you push your changes, GitHub Actions will automatically build your firmware. You can find the compiled `.uf2` file in the **Actions** tab of your forked repository. Download the latest build artifact.

## 5. Flash the Firmware

1. Connect your MinimalPad to your PC via USB.
2. Double-click the **reset** button on the PCB to enter **boot mode**.
3. Your device should appear as a mass storage device.
4. Drag and drop the `firmware.uf2` file onto the mounted drive.
5. The device will reboot automatically with the new keymap.

## 6. Testing and Debugging

After flashing, test your keymap by pressing the configured keys. If something isn’t working as expected, modify your keymap, push changes, and re-flash the new firmware.

## Minimalpad Studio support

This firmware includes the **host module**, which lets Minimalpad Studio, the Mac app, switch the pad to an app's profile layer and set its colour while you work. Without Studio running, the pad behaves exactly as its keymap says, and it returns to its Default layer on its own when Studio goes away. It is on by default and does not change how you build or flash. What it does, its settings and how to turn it off are in [docs/host-module.md](docs/host-module.md).

## Lighting, idle and battery

When you leave the pad alone, its lights go out but it stays connected.

- **Idle.** 30 seconds after your last key press or dial turn, the underglow fades out over 8 seconds, then switches off and cuts power to the LEDs. Press a key or turn the dial to bring it straight back. That press also works as normal.
- **Sleep.** On battery, the pad deep-sleeps after 12 hours without a key press or dial turn, for example overnight. Bluetooth disconnects while it sleeps. The key press that wakes it is used up waking it, and it reconnects within a few seconds. On USB it never sleeps.
- **Battery.** The LEDs are what drain the battery. By ZMK's own estimates, a nice!nano v2 with its LEDs off draws about 40 µA while connected and about 20 µA asleep, while 16 lit LEDs can draw over 100 mA at full brightness. Staying connected with the lights off costs very little.

To change this, edit `boards/shields/minimalpad/minimalpad.conf`:

| Setting | Now | What it does |
| --- | --- | --- |
| `CONFIG_ZMK_IDLE_TIMEOUT` | `30000` | Milliseconds without a key press or dial turn before the lights start to fade |
| `CONFIG_MINIMALPAD_LEDS_IDLE_FADE_MS` | `8000` | How long the fade takes. `0` switches the lights off at once |
| `CONFIG_ZMK_IDLE_SLEEP_TIMEOUT` | `43200000` | Milliseconds without a key press or dial turn before deep sleep, on battery (12 hours) |
| `CONFIG_ZMK_SLEEP` | `y` | Set to `n` and the pad never deep-sleeps |

Good to know:

- The breathe effect sets its own brightness, so it does not dim. It keeps pulsing until the fade time is up, then switches off.
- If the pad restarts while its lights are off for idle, for example waking from sleep, a reset or a flat battery, the lights come back on. Earlier firmware left them off. So after you flash this version for the first time, the lights may stay dark. Switch them on once: hold the bottom-right key and press the third key on the bottom row (LED on/off).
- The fade is in `src/leds/idle_fade.c`. It takes the place of ZMK's `CONFIG_ZMK_RGB_UNDERGLOW_AUTO_OFF_IDLE`, which switches the lights off at once.

## Versions

The firmware uses [semantic versioning](https://semver.org). Its version, such as `0.2.0`, is in the `VERSION` file at the root of this repo, the only place to change it. The firmware reports it to Minimalpad Studio, and each build on GitHub Actions is named after it:

| Build | Artifact to download |
| --- | --- |
| A push to a branch | `minimalpad-v0.2.0+18a503a`: the version, then the commit it was built from |
| A release tag | `minimalpad-v0.2.0`, and a GitHub Release with the `.uf2` files |

After a release, raise the version in the first commit that changes the firmware. Pick the part by the biggest change until the next release:

- **Patch** (`0.2.0` to `0.2.1`): fixes only.
- **Minor** (`0.2.1` to `0.3.0`): something new that works with your setup as it is, such as the idle fade.
- **Major** (`0.3.0` to `1.0.0`): a change that needs something redone, such as a new layer layout, pairing again, or updating Minimalpad Studio to reach the pad.

To release a version:

1. In [CHANGELOG.md](CHANGELOG.md), write what it changes under a heading with its number, such as `## 0.2.0`. This becomes the release's description, so write it for the people who will flash it.
2. Tag the commit whose `VERSION` has that version, and push the tag:

   ```sh
   git tag v0.2.0
   git push origin v0.2.0
   ```

GitHub Actions builds the firmware and publishes a release called "Minimalpad firmware v0.2.0". It holds `minimalpad_with_studio-v0.2.0.uf2` and `minimalpad-v0.2.0.uf2`. Its description is your changelog section, followed by which file to flash and how ([.github/release-notes.md](.github/release-notes.md)). Before building, it stops if the tag and `VERSION` disagree or the changelog has nothing for that version. Then delete the tag with `git tag -d v0.2.0` and `git push origin :v0.2.0`, fix what the error says, and tag again.

## Additional Resources
- [ZMK Documentation](https://zmk.dev/docs/)
- [ZMK Keymap Guide](https://zmk.dev/docs/features/keymaps)
- [MinimalPad GitHub Repository](https://github.com/TimoWielink/zmk-config-minimalpad)

---

By following this guide, you should be able to customize and flash your own keymap for the MinimalPad with ease. Happy hacking!
