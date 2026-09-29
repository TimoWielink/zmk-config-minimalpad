![MinimalPad Firmware](https://cdn.prod.website-files.com/67a8fe2ace8968bc2e48ba6c/6aaae79f281e8ecf0c5c70f7_minimalpad-firmware-github-banner.png)

# MinimalPad firmware

ZMK firmware for the MinimalPad macropad. Get the latest version for your pad here.

**Firmware version: 0.7.0.** What changed is in [CHANGELOG.md](CHANGELOG.md).

- **Website:** [minimalmacropad.com](https://minimalmacropad.com)
- **MinimalPad Studio, web version:** [studio.minimalmacropad.com](https://studio.minimalmacropad.com), in Chrome or Edge
- **MinimalPad Studio for Mac:** [download it](https://downloads.minimalmacropad.com/mac/latest), for macOS 14 or newer
- **Docs:** [docs.minimalmacropad.com](https://docs.minimalmacropad.com)

## Change your keys

You don't need to build anything. Connect your pad to MinimalPad Studio, on the web or on your Mac, and change your keys there. Studio for Mac also sets the dial and the lights, and switches the pad to a profile for the app you're using.

## Install the latest firmware

On a Mac, MinimalPad Studio for Mac installs it for you: plug the pad in, and follow the update message in its sidebar.

By hand, on any computer:

1. **Download it.** Get the `.uf2` file from the [latest release](https://github.com/TimoWielink/zmk-config-minimalpad/releases/latest).
2. **Connect the pad** to your computer with a USB cable that carries data. A charge-only cable won't work.
3. **Press the pad's reset button twice**, quickly. A drive called NICENANO appears.
4. **Copy the `.uf2` file** onto that drive. The pad restarts with the new firmware.

If the drive doesn't appear, try another cable or USB port, or press reset a little faster or slower. If you pressed reset twice by accident, press it once to restart the pad. If the lights stay off after the update, switch them on once: hold the bottom-right key and press the knob.

If the pad still doesn't work after you install the firmware, see [Update the firmware](https://docs.minimalmacropad.com/firmware/update) in the docs.

## For developers

- [The host module](docs/host-module.md): how MinimalPad Studio for Mac switches profiles on the pad
- [Versions and releases](docs/releasing.md)
