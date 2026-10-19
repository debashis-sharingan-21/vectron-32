# VECTRON-32 Command Center v2.0 — Hardware Guide

> This guide covers everything needed to build and wire a VECTRON-32 hardware node: bill of materials, full ESP32 pinout reference, wiring diagrams for relay modules, LEDs, and I2C peripherals, plus power supply considerations and a complete breadboard layout.

---

## Table of Contents

1. [Bill of Materials](#1-bill-of-materials)
2. [ESP32 Pinout Safety Matrix](#2-esp32-pinout-safety-matrix)
3. [Wiring: Relay Module](#3-wiring-relay-module)
4. [Wiring: LED Output](#4-wiring-led-output)
5. [Wiring: I2C Peripherals](#5-wiring-i2c-peripherals)
6. [Power Considerations](#6-power-considerations)
7. [Breadboard Layout](#7-breadboard-layout)

---

## 1. Bill of Materials

| Component | Qty | Specification / Notes |
|-----------|:---:|-----------------------|
| ESP32-DevKitC (WROOM-32) | 1 | 38-pin or 30-pin variant; 4 MB flash minimum |
| 5V Single-Channel Relay Module | 1–4 | Optocoupler-isolated; active-LOW control; flyback diode included on most modules |
| 5 mm LED (Red or Green) | 2–4 | Forward voltage ~2.1 V; 20 mA maximum |
| 120 Ω resistor (1/4 W) | 2–4 | Current limiting for LED at 3.3 V GPIO output |
| I2C device (choose one or more) | 1+ | e.g., SSD1306 OLED (0x3C), BME280 sensor (0x76), MPU6050 IMU (0x68) |
| 4.7 kΩ resistor | 2 | I2C SDA and SCL pull-ups to 3.3 V |
| 100 nF ceramic capacitor | 2 | Decoupling, place between 3.3 V and GND near ESP32 VCC pins |
| 10 µF electrolytic capacitor | 1 | Bulk decoupling on 5 V rail, near USB connector or VIN |
| Half-size solderless breadboard | 1 | 400-tie point minimum |
| Micro-USB cable | 1 | Data + power; avoid charge-only cables |
| Jumper wires (M-M) | 20+ | Various lengths |
| External 5 V / 2 A power supply (optional) | 1 | Required if driving multiple relays from VIN |

---

## 2. ESP32 Pinout Safety Matrix

> Pins 6–11 are wired directly to the internal SPI flash chip. **Never** connect anything to these pins.

| Pin # | GPIO Name | Safe Output | Safe Input | ADC | Touch | Notes |
|------:|-----------|:-----------:|:----------:|:---:|:-----:|-------|
| 1  | 3V3       | —  | —  | —   | —  | 3.3 V power output (500 mA max from LDO) |
| 2  | EN        | —  | —  | —   | —  | Chip enable; pull HIGH (internal). Do not drive LOW in normal operation |
| 3  | GPIO36 (VP) | ⛔ | ✅ | ADC1 CH0 | — | Input only; no internal pull-up/down |
| 4  | GPIO39 (VN) | ⛔ | ✅ | ADC1 CH3 | — | Input only; no internal pull-up/down |
| 5  | GPIO34    | ⛔ | ✅ | ADC1 CH6 | — | Input only; no internal pull-up/down |
| 6  | GPIO35    | ⛔ | ✅ | ADC1 CH7 | — | Input only; no internal pull-up/down |
| 7  | GPIO32    | ✅ | ✅ | ADC1 CH4 | T9 | General purpose; touch sensor |
| 8  | GPIO33    | ✅ | ✅ | ADC1 CH5 | T8 | General purpose; touch sensor |
| 9  | GPIO25    | ✅ | ✅ | ADC2 CH8 | — | DAC1 output (8-bit) |
| 10 | GPIO26    | ✅ | ✅ | ADC2 CH9 | — | DAC2 output (8-bit) |
| 11 | GPIO27    | ✅ | ✅ | ADC2 CH7 | T7 | General purpose |
| 12 | GPIO14    | ✅ | ✅ | ADC2 CH6 | T6 | Outputs PWM signal during boot; JTAG TMS |
| 13 | GPIO12    | ⚠️ | ✅ | ADC2 CH5 | T5 | Strapping pin — must be LOW at boot to avoid flash voltage fault |
| 14 | GND       | —  | —  | —   | —  | Ground |
| 15 | GPIO13    | ✅ | ✅ | ADC2 CH4 | T4 | General purpose; JTAG TCK |
| 16 | GPIO9  (D2) | ⛔ | ⛔ | —  | — | Internal SPI flash — **DO NOT USE** |
| 17 | GPIO10 (D3) | ⛔ | ⛔ | —  | — | Internal SPI flash — **DO NOT USE** |
| 18 | GPIO11 (CMD) | ⛔ | ⛔ | — | — | Internal SPI flash — **DO NOT USE** |
| 19 | 5V (VIN)  | —  | —  | —   | —  | 5 V from USB or external supply via diode |
| 20 | GND       | —  | —  | —   | —  | Ground |
| 21 | GPIO6  (CLK) | ⛔ | ⛔ | — | — | Internal SPI flash — **DO NOT USE** |
| 22 | GPIO7  (D0) | ⛔ | ⛔ | — | — | Internal SPI flash — **DO NOT USE** |
| 23 | GPIO8  (D1) | ⛔ | ⛔ | — | — | Internal SPI flash — **DO NOT USE** |
| 24 | GPIO15    | ⚠️ | ✅ | ADC2 CH3 | T3 | Strapping pin — HIGH at boot enables boot log; JTAG TDO |
| 25 | GPIO2     | ✅ | ✅ | ADC2 CH2 | T2 | Onboard LED (most DevKit boards); must be LOW or floating at boot for download mode |
| 26 | GPIO0     | ⛔ | ✅ | ADC2 CH1 | T1 | Strapping pin — LOW at boot = download mode. Use as input only |
| 27 | GPIO4     | ✅ | ✅ | ADC2 CH0 | T0 | General purpose — recommended for relay |
| 28 | GPIO16    | ✅ | ✅ | —   | —  | UART2 RX; general purpose |
| 29 | GPIO17    | ✅ | ✅ | —   | —  | UART2 TX; general purpose |
| 30 | GPIO5     | ⛔ | ✅ | —   | —  | Strapping pin — affects SDIO slave timing; safe as input |
| 31 | GPIO18    | ✅ | ✅ | —   | —  | SPI SCK (safe if SPI peripheral not used) |
| 32 | GPIO19    | ✅ | ✅ | —   | —  | SPI MISO |
| 33 | GPIO21    | ✅ | ✅ | —   | —  | **I2C SDA** (default Wire assignment) |
| 34 | GPIO3 (RXD0) | ⚠️ | ✅ | — | — | UART0 RX — must be HIGH at boot. Avoid as output |
| 35 | GPIO1 (TXD0) | ⚠️ | ⛔ | — | — | UART0 TX — used by Serial.print(); avoid as input |
| 36 | GPIO22    | ✅ | ✅ | —   | —  | **I2C SCL** (default Wire assignment) |
| 37 | GPIO23    | ✅ | ✅ | —   | —  | SPI MOSI |
| 38 | GND       | —  | —  | —   | —  | Ground |

---

## 3. Wiring: Relay Module

### Components
- 1× ESP32 DevKit
- 1× 5 V optocoupler-isolated relay module (active-LOW control input)
- Flyback protection diode (1N4007) — usually pre-installed on module

### Wiring Diagram

```
ESP32 DevKit                  Relay Module
─────────────                 ────────────
  GPIO4 ──────── IN1 ─── (optocoupler LED anode, active LOW)
  GND   ──────── GND
  VIN(5V) ────── VCC  (relay coil requires 5 V; 3.3 V coils exist but are less common)

  Relay Module Terminals (high-voltage side):
  ┌─────────────────────────────────────┐
  │  COM ─── mains/load supply (+)      │
  │  NO  ─── load (+) when relay CLOSED │  ← use NO for normally-open switching
  │  NC  ─── load (+) when relay OPEN   │
  └─────────────────────────────────────┘
```

### How It Works

1. **Optocoupler isolation:** The ESP32 GPIO drives the LED side of an optocoupler. The phototransistor on the other side switches the relay coil driver transistor. This means the ESP32 3.3 V logic is completely galvanically isolated from the 5 V relay coil circuit.

2. **Flyback diode (1N4007):** Connected in reverse across the relay coil (`cathode → coil +`, `anode → coil −`). When the coil is de-energised, the collapsing magnetic field generates a voltage spike; the flyback diode clamps this spike and dissipates it harmlessly. Most relay modules include this diode on the PCB — verify before wiring.

3. **Active-LOW logic:** Drive GPIO4 LOW to energise the relay (close NO contact), HIGH to de-energise. VECTRON-32 firmware accounts for this inversion in the GPIO safety matrix driver layer.

> **Warning:** Never switch mains (AC) voltage on a breadboard. Use a properly enclosed relay board with terminal blocks rated for your load voltage and current.

---

## 4. Wiring: LED Output

### Resistor Calculation

```
V_supply = 3.3 V   (ESP32 GPIO HIGH output)
V_f      = 2.1 V   (typical red/green 5 mm LED forward voltage)
I_desired = 10 mA  (safe GPIO sink/source current; max 40 mA per pin but keep to 10–12 mA)

R = (V_supply − V_f) / I_desired
R = (3.3 − 2.1) / 0.010
R = 1.2 / 0.010
R = 120 Ω  ✓  (use 120 Ω or nearest standard value: 100 Ω or 150 Ω)
```

### Wiring Diagram

```
ESP32 GPIO2 ──── [120 Ω resistor] ──── LED anode (+, longer leg)
                                             │
                                        LED cathode (−, shorter leg, flat side)
                                             │
                                           GND
```

### Notes

- Always place the resistor **in series** with the LED (either before the anode or after the cathode — both work).
- Do not connect an LED directly from GPIO to GND without a resistor; the LED will draw excessive current and can damage the GPIO driver.
- For multiple LEDs from the same GPIO, place a separate resistor for each LED and connect anodes together. Do not share a single resistor.

---

## 5. Wiring: I2C Peripherals

### Default I2C Pin Assignment

| Signal | ESP32 Pin | GPIO |
|--------|-----------|:----:|
| SDA    | Pin 33    | 21   |
| SCL    | Pin 36    | 22   |

### Pull-Up Resistors

I2C is an open-drain bus. Both SDA and SCL must be pulled up to VCC (3.3 V) through resistors. Without pull-ups the bus will not function.

```
3.3V ──┬─── 4.7 kΩ ──── SDA (GPIO21) ──── I2C device SDA
       └─── 4.7 kΩ ──── SCL (GPIO22) ──── I2C device SCL

GND ───────────────────────────────────── I2C device GND
3.3V ──────────────────────────────────── I2C device VCC (if 3.3 V device)
```

### Wiring Multiple I2C Devices

All I2C devices share the same SDA and SCL lines (bus topology). Each device has a unique 7-bit address. A single pair of 4.7 kΩ pull-up resistors is sufficient for the entire bus (do not add pull-ups per device — this lowers effective resistance and can cause signal integrity issues).

```
3.3V ─── 4.7kΩ ─── SDA ─────┬──────────┬──────────┬────
                              │          │          │
                           SSD1306    BME280    MPU6050
                           (0x3C)     (0x76)    (0x68)
                              │          │          │
3.3V ─── 4.7kΩ ─── SCL ─────┴──────────┴──────────┴────

GND ────────────────────────────────────────────────────
```

### Bus Speed

VECTRON-32 configures I2C at **400 kHz (Fast Mode)**. All devices listed in the BOM support 400 kHz. If you add a device that only supports 100 kHz Standard Mode, change the Wire initialisation call in firmware:

```cpp
Wire.begin(21, 22, 100000UL);  // 100 kHz standard mode
```

---

## 6. Power Considerations

### USB Power (Development / Light Load)

- Most USB ports supply 500 mA at 5 V; USB 3.0 ports supply up to 900 mA.
- The ESP32's onboard LDO regulator steps 5 V → 3.3 V and can supply **~600 mA** total (shared between ESP32 core, GPIO outputs, and peripherals).
- **Safe USB load budget:**

  | Consumer | Current |
  |----------|--------:|
  | ESP32 core + Wi-Fi TX peak | ~240 mA |
  | I2C devices (sensors) | ~20 mA |
  | LEDs × 4 @ 10 mA | ~40 mA |
  | Relay coil (5 V, from VIN) | ~70 mA |
  | **Total** | **~370 mA** |

  This is within USB limits but leaves little headroom. Use a dedicated 5 V / 2 A supply if driving 2+ relays.

### External 5 V Supply

- Connect to the **VIN** pin (not 3.3 V directly).
- The onboard diode on DevKit boards prevents back-powering from USB when external supply is connected.
- Minimum 2 A rating recommended for 4-relay configurations.

### Decoupling Capacitors

Place decoupling capacitors **as close as possible** to the ESP32 power pins:

| Capacitor | Location | Purpose |
|-----------|----------|---------|
| 100 nF ceramic | Between 3.3 V and GND (×2, near each VCC pin) | High-frequency switching noise from GPIO transitions |
| 10 µF electrolytic | Between 5 V rail and GND (×1, near VIN) | Bulk charge reservoir for relay coil switching transients |

Failure to add decoupling capacitors can cause brownout resets when relay coils switch or Wi-Fi transmits at full power.

---

## 7. Breadboard Layout

```
         [ESP32-DevKitC WROOM-32]
         ┌──────────────────────┐
  3V3 ───┤ 3V3           GND   ├─── GND rail (─)
  EN  ───┤ EN            GPIO23├─── (SPI MOSI / spare)
  VP  ───┤ GPIO36        GPIO22├─── SCL ──── 4.7kΩ ──── 3V3
  VN  ───┤ GPIO39        GPIO21├─── SDA ──── 4.7kΩ ──── 3V3
  D34 ───┤ GPIO34        GPIO19├─── (SPI MISO / spare)
  D35 ───┤ GPIO35        GPIO18├─── (SPI SCK / spare)
  D32 ───┤ GPIO32        GPIO5 ├─── (strapping — leave float)
  D33 ───┤ GPIO33        GPIO17├─── (UART2 TX / spare)
  D25 ───┤ GPIO25        GPIO16├─── (UART2 RX / spare)
  D26 ───┤ GPIO26        GPIO4 ├─── Relay IN1 ──── Relay Module
  D27 ───┤ GPIO27        GPIO0 ├─── (strapping — BOOT BTN)
  D14 ───┤ GPIO14        GPIO2 ├─── 120Ω ─── LED(+) ─── LED(-) ─── GND
  D12 ───┤ GPIO12        GPIO15├─── (strapping — leave float)
  D13 ───┤ GPIO13        GPIO8 ├─── (flash — DO NOT CONNECT)
  D9  ───┤ GPIO9         GPIO7 ├─── (flash — DO NOT CONNECT)
  D10 ───┤ GPIO10        GPIO6 ├─── (flash — DO NOT CONNECT)
  D11 ───┤ GPIO11        GPIO11├─── (flash — DO NOT CONNECT)
  VIN ───┤ VIN           GPIO10├─── (flash — DO NOT CONNECT)
  GND ───┤ GND           GPIO9 ├─── (flash — DO NOT CONNECT)
         └──────────────────────┘

  Power Rails on Breadboard:
  (+) 3.3V rail  ─── ESP32 3V3 pin, I2C pull-ups, I2C device VCC
  (-) GND rail   ─── ESP32 GND pin, relay GND, LED cathodes

  I2C Bus (right side of breadboard):
  [SDA] ───── SSD1306 OLED (ADDR 0x3C)
         └─── BME280 Sensor (ADDR 0x76)

  Relay Module (separate 5V supply recommended):
  [GPIO4] ──── IN1
  [VIN 5V] ─── VCC
  [GND]    ─── GND
  [COM/NO] ─── Load circuit (low voltage DC only on breadboard!)

  LED (GPIO2):
  [GPIO2] ─── [120Ω] ─── LED(+) ─── LED(-) ─── GND
```

> **Tip:** Keep high-current relay wiring (the COM/NO/NC side) physically separate from the logic wiring on the breadboard. Even with optocoupler isolation, switching transients can couple into adjacent wires and cause spurious GPIO reads.
