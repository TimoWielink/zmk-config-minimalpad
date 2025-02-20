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

## Additional Resources
- [ZMK Documentation](https://zmk.dev/docs/)
- [ZMK Keymap Guide](https://zmk.dev/docs/features/keymaps)
- [MinimalPad GitHub Repository](https://github.com/TimoWielink/zmk-config-minimalpad)

---

By following this guide, you should be able to customize and flash your own keymap for the MinimalPad with ease. Happy hacking!
