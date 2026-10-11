# Changelog

What changed in each version of the MinimalPad firmware. How versions are numbered and released is in [docs/releasing.md](docs/releasing.md). A version's section here becomes the description of its GitHub Release, so write it for the people who will flash it.

## 0.9.0

- An app can have several layers, and the pad switches between them by itself. In MinimalPad Studio for Mac, from its next release, give an app a second layer, such as one for rating and one for editing in Lightroom, then put a key on each to move between them: **Next layer** steps through the app's layers and wraps round, **Go to** jumps to one of them, and **Hold for** shows one for as long as you hold the key. Studio tells the pad which layers belong to the app in front, and the keys do the rest on the pad, so a switch is instant and doesn't wait for the Mac or a slow Bluetooth link.
- **Hold for Default** on an app's layer comes with Studio's next release, which puts a real Hold for Default on a key and offers to fix keys that held Default and did nothing. Hold the key and the pad shows Default's keys; let go and it's back on the layer you were on. Flashing this firmware alone doesn't change a key that held Default before: it still does nothing, because Default was already there underneath.
- Moving between an app's layers leaves any other layer you've switched on yourself, such as a numbers layer, as it was. These keys only act while Studio has the pad on an app. Without Studio, or in an app without a profile, they do nothing.
- The pad tells Studio when a key changed its layer, so Studio can bring you back to the layer you last used when you return to the app.
- The pad now saves a dial you set about 2 seconds after your last change, as it saves a light, and never while it is starting up. Before, it saved at once.
- Nothing else changes: your keys, layers, profiles, dial and lights are as you left them, and you don't need to pair the pad again. An older Studio works as it always did.

## 0.8.0

- Each layer, and each key, can have a light of its own. In MinimalPad Studio for Mac, from its next release, give a layer a colour and the pad turns that colour while you're on that layer, or give a key its own colour, or switch its light off. The pad keeps them, so they work without Studio running, on any computer.
- That includes the Default layer's keys. A key such as Play / Pause can stay one colour on Default, even with Studio quit. Default has no light for the whole layer, because the pad's own lighting is Default's.
- A key without a light of its own shows its layer's, and a layer without one shows the layer below it, down to the pad's own lighting.
- The colours follow the pad's brightness, the idle fade and lights off. The brightness keys and the dial dim and brighten them; they fade out when the pad is idle and a key wakes them. If you've switched the lights off, they stay dark: a layer or key light never switches them on.
- While you change the pad's own colour, effect or speed with its keys or the dial, the pad shows its own lighting for about 3 seconds, so you can see the change even where a layer or key light would cover it. Studio, from its next release, does the same while **Lights** is open.
- The pad saves a light about 2 seconds after your last change.
- A computer that forgot the pad in its Bluetooth settings can pair with it again. Before, the pad kept that computer's old pairing and refused the new one, and clearing the pad's Bluetooth slots didn't always help.
- Nothing else changes: your keys, layers, profiles, dial and lights are as you left them, and you don't need to pair the pad again. An older Studio works as it always did.

## 0.7.1

- One click of the dial is now exactly one step: one volume step, one scroll, one key. Before, a click sometimes sent two, because the pad counted each click as one and two thirds of a step. A full turn is now 20 steps, one per click, where it was about 33.
- Nothing else changes: your keys, layers, profiles, dial and lights are as you left them, and you don't need to pair the pad again.

## 0.7.0

- MinimalPad Studio for Mac, from its next release, can change the pad's own lights: switch them on or off, and set their brightness, colour, effect and speed. The pad keeps what you choose, as it keeps changes made with the lights keys, so the lights stay that way without Studio, on any computer. While you're changing them they stay on, and they fade out once the pad has saved your change, about a minute after the last one.
- Studio can also tell lights that are resting, faded out because you haven't touched the pad for a while, from lights you switched off.
- A colour or brightness you set with the pad's keys or the dial is now saved a minute after your last change. Before, it was saved only once the lights had faded out and another minute had passed, so unplugging or restarting the pad sooner lost it. After any change to the lights made with the keys or the dial, they don't fade out until the pad has saved it, up to a minute after your last change.
- On the Connections & LEDs layer, the empty key on the bottom row, next to **Brightness up**, is now **Previous light effect**. A key you changed there yourself stays as you set it.
- Nothing else changes: your keys, layers, profiles, dial and lights are as you left them, and you don't need to pair the pad again.

## 0.6.1

- The lights now show the colour you pick. Red and green were swapped on the way to the LEDs, so an orange profile lit the pad lime green, violet showed as blue and red as green. Blue was right.
- The pad's own colour shows as chosen too, so it may look different after flashing. Set it again from the Connections & LEDs layer if you'd like it back as it was.
- Nothing else changes: your keys, layers, profiles and dial are as you left them, and you don't need to pair the pad again.

## 0.6.0

- You choose what the dial does, on every layer. In MinimalPad Studio for Mac, from its next release, pick a layer or an app profile and choose what turning the dial does: volume, scroll up and down, scroll left and right, zoom, light brightness or colour, or any two keys, one for each direction. A scroll can hold modifier keys, which is how many apps pan, zoom or orbit with a wheel. The pad keeps your choice, so it works without Studio running, on any computer.
- An app profile you haven't given a dial of its own uses Default's.
- The BT + LED layer is now called **Connections & LEDs**. It's still opened by holding the bottom-right key.
- On that layer, **Clear pairing** has moved to the end of the second row, after **Use USB** and **Use Bluetooth**, so it's not pressed by mistake when you reach for Bluetooth device 5. A key you changed there yourself stays as you set it.
- The pad now also reports itself as a mouse wheel, which is how the dial scrolls. Nothing moves the pointer.
- Nothing else changes: your keys, layers, profiles and lights are as you left them.

## 0.5.0

- A key can now open a Mac app. MinimalPad Studio for Mac, from its next release, offers "Open app" for any key. The pad tells Studio which key was pressed and Studio opens the app, or brings it forward if it's already open. It works only while Studio is running: without it, the key does nothing.
- Nothing else changes: your keys, layers, profiles and lights are as you left them, and you don't need to pair the pad again.

## 0.4.2

- The pad keeps working over Bluetooth after your computer reconnects to it. Before this, once MinimalPad Studio had read the layout over Bluetooth, the next reconnect (restarting the Mac, turning Bluetooth off and on, the Mac waking up) left the pad showing as connected while no key, dial turn or Studio edit got through. Restarting the pad brought it back, until the next reconnect.
- The cause is a fault in ZMK's own Studio code, which this firmware now replaces with a corrected copy. You don't need to pair the pad again, and forgetting it in your Bluetooth settings was never the cure.
- Nothing else changes: your keys, layers, profiles and lights are as you left them.

## 0.4.1

- MinimalPad Studio for Mac can now read your layout and switch profiles over Bluetooth. Before this, a wireless edit got part of the way through and then stopped, and the pad had to be on a cable to work on it.
- Nothing else changes: your keys, layers, profiles and lights are as you left them, and you don't need to pair the pad again.

## 0.4.0

- MinimalPad Studio for Mac can show which Bluetooth device the pad is on. The pad now reports which of the five devices on the BT + LED layer is selected, and tells Studio again whenever you change it with one of those keys. An older Studio ignores this and works as it always did.
- Nothing else changes: your keys, layers, profiles and lights are as you left them, and you don't need to pair the pad again.

## 0.3.0

The first published release. It also includes 0.1.0 and 0.2.0, which were not released on their own: profiles and profile colours for MinimalPad Studio for Mac, the underglow's fade when the pad goes idle, deep sleep after 12 hours on battery, and the firmware version.

- Profiles and their LED colours now switch over USB as well as Bluetooth.
- Over USB, ZMK Studio editing and profile control share one verified connection, so Studio for Mac cannot write a layout to one pad while it controls another.
- USB stays automatic: the pad is still one serial device, so there is no port to pick.
- There is one firmware file, `minimalpad_with_studio`, with ZMK Studio built in. The build without ZMK Studio is no longer made.
- New default keys. The first layer, now called Default instead of Media, has macOS shortcuts: Spotlight, Mission Control, emoji, lock screen, copy, paste, undo, redo and two screenshot keys. It also has screen brightness and track controls. Pressing the knob mutes, and turning it still changes the volume.
- A simpler BT + LED layer, still opened by holding the bottom-right key. It has Bluetooth devices 1 to 5, a key that clears the current device's pairing, keys that choose USB or Bluetooth, and lights on/off, effect, colour and brightness. Pressing the knob there switches the lights on or off.
- Keys you changed and saved in MinimalPad Studio keep your setting.

If the lights stay off after you flash it, switch them on once: hold the bottom-right key and press the knob.

## 0.2.0

- The underglow fades out over 8 seconds when the pad goes idle, instead of switching off at once.
- On battery, deep sleep waits 12 hours instead of 10 minutes, so Bluetooth stays connected through a working day.
- Lights that were off for idle come back on when the pad restarts.
- The firmware has a version, kept in `VERSION`. Builds are named after it, and MinimalPad Studio for Mac reads it.

## 0.1.0

- The host module for MinimalPad Studio for Mac: profile layers, profile colours, and a return to Default when Studio goes away.
- 16 layers in the Studio build, 14 of them free for profiles.
