# Changelog

What changed in each version of the Minimalpad firmware. How versions are numbered and released is in the README, under "Versions". A version's section here becomes the description of its GitHub Release, so write it for the people who will flash it.

## 0.2.0

- The underglow fades out over 8 seconds when the pad goes idle, instead of switching off at once.
- On battery, deep sleep waits 12 hours instead of 10 minutes, so Bluetooth stays connected through a working day.
- Lights that were off for idle come back on when the pad restarts.
- The firmware has a version, kept in `VERSION`. Builds are named after it, and Minimalpad Studio reads it.

## 0.1.0

- The host module for Minimalpad Studio: profile layers, profile colours, and a return to Default when Studio goes away.
- 16 layers in the Studio build, 14 of them free for profiles.
