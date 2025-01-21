# Layer LED Indicators Implementation Guide Needed

## Goal
Implement simple LED indicators to show which layer is currently active on a custom keyboard.

## Hardware Setup
- Board: nice_nano_v2
- 3 GPIO LEDs connected to:
  - Layer 0 LED: P1.13 (GPIO1_13)
  - Layer 1 LED: P0.09 (GPIO0_09)
  - Layer 2 LED: P1.11 (GPIO1_11)

## Current Implementation

### LED Node Definition (.overlay)
dts
layer_leds {
compatible = "gpio-leds";
layer0_led: layer0_led {
gpios = <&gpio1 13 GPIO_ACTIVE_HIGH>;
status = "okay";
};
layer1_led: layer1_led {
gpios = <&gpio0 9 GPIO_ACTIVE_HIGH>;
status = "okay";
};
layer2_led: layer2_led {
gpios = <&gpio1 11 GPIO_ACTIVE_HIGH>;
status = "okay";
};
};


### Behavior Attempt (.keymap)

dts
behaviors {
layer0_led_on: layer0_led_on {
compatible = "zmk,behavior-output";
#binding-cells = <0>;
gpios = <&layer0_led>;
output-high;
};
// ... similar definitions for other LEDs ...
};
macros {
to_layer0: to_layer0 {
compatible = "zmk,behavior-macro";
#binding-cells = <0>;
bindings = <&to DEFAULT>, <&layer0_led_on>, <&layer1_led_off>, <&layer2_led_off>;
};
// ... similar definitions for other layers ...
};


CONFIG_GPIO=y
CONFIG_LED=y
CONFIG_LED_GPIO=y
CONFIG_ZMK_MACRO=y

## Current Error

devicetree error: binding controller <Node /behaviors/layer0_led_on in '/tmp/zmk-config/zephyr/misc/empty_file.c'> for <Node /macros/to_layer0 in '/tmp/zmk-config/zephyr/misc/empty_file.c'> lacks binding


## Questions
1. What is the correct way to implement LED layer indicators in ZMK?
2. Is there a specific behavior type we should use for GPIO LED control?
3. Are there any example implementations we can reference?

## Additional Context
We've tried various approaches including:
- `zmk,behavior-gpio`
- `zmk,behavior-output`
- `zmk,behavior-led-control`
- Direct GPIO references and LED node references

Any guidance on the proper way to implement this functionality would be greatly appreciated!