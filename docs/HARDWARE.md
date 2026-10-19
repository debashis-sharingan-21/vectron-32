# Hardware Guide

Back to the [README](../README.md).

## Target Board

- **ESP32-WROOM-32** development board, PlatformIO board id `esp32dev`, Arduino framework.
- Other ESP32 families (S2, S3, C3, …) are not targeted.
- No extra hardware is required to run the dashboard. LEDs, relays and I2C devices are optional.

## Default Pin Map

| Function | Pin | Direction | Defined in |
|---|---|---|---|
| `ONBOARD_LED` | GPIO 2 | output, active-HIGH | `OUTPUTS[]` in `include/config.h` |
| `BOOT_BUTTON` | GPIO 0 | input, internal pull-up, active-LOW | `INPUTS[]` in `include/config.h` |
| I2C SDA / SCL | GPIO 21 / GPIO 22 | scan only | `I2C_SDA`, `I2C_SCL` in `include/config.h` |
| Wi-Fi | — | SoftAP, channel 6, max 4 clients | `AP_DEFAULT_*` in `include/config.h` |
| BLE | — | advertising only, name `VECTRON-32` | `BLE_DEVICE_NAME` in `include/config.h` |

## Output Pin Allow-List

Only pins listed in `OUTPUTS[]` **and** on this allow-list can be driven (enforced in `Hardware::isSafeOutputPin`):

```text
2  4  5  13  14  15  16  17  18  19  23  25  26  27  32  33
```

Anything else is shown in the dashboard as `BLOCKED` and never touched. Reasons:

| Pins | Why blocked |
|---|---|
| 6–11 | Connected to the SPI flash — driving them crashes the chip |
| 1, 3 | UART0 (serial monitor / flashing) |
| 0, 12 | Strapping pins (boot configuration) |
| 34–39 | Input-only pins — cannot output |
| 21, 22 | Used by the I2C scanner |

**Allowed, but boot-sensitive:** GPIO 2 and GPIO 15 are strapping pins too. A circuit that pulls them to the wrong level during reset can change boot behaviour (GPIO 2 must be low or floating to enter download mode; GPIO 15 affects boot logging). Prefer 4, 5, 13, 14, 16–19, 23, 25–27, 32, 33 for external hardware.

## Adding an Output

1. Wire it (see below).
2. Add a row to `OUTPUTS[]` in `include/config.h`:
   ```cpp
   { 4, "RELAY_1", false, false },    // pin, name, activeHigh, initialOn
   ```
   Up to **8** outputs are supported (compile-time check).
3. Rebuild and upload the **firmware** (`pio run -t upload`). No filesystem upload is needed.

`activeHigh = false` is for modules where LOW means ON (many relay boards). `initialOn` is applied at boot — the level is written both before and after switching the pin to output mode to avoid a glitch.

## Wiring Examples

**External LED on GPIO 4**

```text
GPIO 4 ── 220–330 Ω resistor ── LED anode (+)
                                 LED cathode (–) ── GND
```

**Relay module.** A relay board must be wired with the supply and driver it needs (many need 5 V and/or a transistor stage; ESP32 pins are 3.3 V and source only a few mA). Never switch mains voltage unless you know how to do it safely.

**I2C device (OLED, BME280, …)**

```text
Device SDA ── GPIO 21        Device VCC ── 3V3 (or as the module requires)
Device SCL ── GPIO 22        Device GND ── GND
```

I2C needs pull-up resistors; most breakout modules include them. The scan covers addresses 1–126 and lists detected peripherals.
