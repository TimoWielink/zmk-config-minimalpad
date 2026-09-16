# Changelog

What changed in each version of the MinimalPad firmware. How versions are numbered and released is in the README, under "Versions". A version's section here becomes the description of its GitHub Release, so write it for the people who will flash it.

## 0.3.0

The first published release. It also includes 0.1.0 and 0.2.0, which were not released on their own: profiles and profile colours for MinimalPad Studio for Mac, the underglow's fade when the pad goes idle, deep sleep after 12 hours on battery, and the firmware version.

- Profiles and their LED colours now switch over USB as well as Bluetooth.
- Over USB, ZMK Studio editing and profile control share one verified connection, so Studio for Mac cannot write a layout to one pad while it controls another.
- USB stays automatic: the pad is still one serial device, so there is no port to pick.
- There is one firmware file, `minimalpad_with_studio`, with ZMK Studio built in. The build without ZMK Studio is no longer made.

If the lights stay off after you flash it, switch them on once: hold the bottom-right key and press the third key on the bottom row.

## 0.2.0

- The underglow fades out over 8 seconds when the pad goes idle, instead of switching off at once.
- On battery, deep sleep waits 12 hours instead of 10 minutes, so Bluetooth stays connected through a working day.
- Lights that were off for idle come back on when the pad restarts.
- The firmware has a version, kept in `VERSION`. Builds are named after it, and MinimalPad Studio for Mac reads it.

## 0.1.0

- The host module for MinimalPad Studio for Mac: profile layers, profile colours, and a return to Default when Studio goes away.
- 16 layers in the Studio build, 14 of them free for profiles.
