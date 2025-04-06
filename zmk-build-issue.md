# ZMK Bluetooth Build Issue: `ticker_update` Undefined Reference

I'm building a custom minimalpad shield for ZMK with a Nice!Nano v2 controller and running into Bluetooth-related build errors. The minimalpad has:

- 4x4 key matrix
- EC11 encoder
- RGB underglow (WS2812 LEDs)
- USB and Bluetooth connectivity

### Error Description

When building with Bluetooth enabled, I consistently get this linker error:

```
/opt/zephyr-sdk-0.16.3/arm-zephyr-eabi/bin/../lib/gcc/arm-zephyr-eabi/12.2.0/../../../../arm-zephyr-eabi/bin/ld.bfd: zephyr/subsys/bluetooth/controller/libsubsys__bluetooth__controller.a(ull_adv.c.obj): in function `ticker_update_rand':
/tmp/zmk-config/zephyr/subsys/bluetooth/controller/ll_sw/ull_adv.c:1932: undefined reference to `ticker_update'
```

The error appears in four functions that all reference the missing `ticker_update` function:
- `ticker_update_rand()` in ull_adv.c
- `ull_adv_time_update()` in ull_adv.c
- `ull_conn_done()` in ull_conn.c
- `ull_periph_latency_cancel()` in ull_peripheral.c

### What I've Tried

1. **Basic Bluetooth configuration:**
```
CONFIG_ZMK_USB=y
CONFIG_ZMK_BLE=y
CONFIG_BT_CTLR_TX_PWR_PLUS_8=y
```

2. **Added Bluetooth controller and ticker settings:**
```
CONFIG_BT=y  
CONFIG_BT_CTLR=y
CONFIG_BT_LL_SW_SPLIT=y
CONFIG_BT_TICKER_UPDATE=y  # Attempted to address the missing ticker_update
```

3. **Attempted HCI transport approach:**
```
CONFIG_BT_HCI=y
CONFIG_BT_HCI_ACL_FLOW_CONTROL=y
CONFIG_BT_HCI_VS=y
```
When using the HCI approach, I received this warning which may indicate a dependency issue:
```
warning: BT_HCI_VS (defined at subsys/bluetooth/common/Kconfig:199) was assigned the value 'y' but got the value 'n'. Check these unsatisfied dependencies: (BT_HAS_HCI_VS || !BT_CTLR) (=n).
```

4. **Working USB-only configuration:**
Finally, I disabled Bluetooth entirely to get a working build:
```
CONFIG_ZMK_USB=y
CONFIG_ZMK_BLE=n
CONFIG_BT=n
```

### System Details
- ZMK firmware with Zephyr 3.5.0
- Using the nrf52840-nosd snippet
- Building with GitHub Actions
- Using west build with Nice!Nano v2 board target

### Questions
1. What is causing the missing `ticker_update` function reference?
2. Is there a specific configuration needed for the Bluetooth ticker system in this version of ZMK/Zephyr?
3. Are there known compatibility issues with the Nice!Nano v2 and the current ZMK Bluetooth implementation?
4. How can I enable Bluetooth while ensuring the ticker functions are properly linked?

Any assistance with resolving this Bluetooth build error would be greatly appreciated! 