# VECTRON-32: Industrial Command Center & Instrumentation Console (v2.0)

[![PlatformIO CI](https://github.com/debashis-sharingan-21/vectron-32/actions/workflows/build.yml/badge.svg)](https://github.com/debashis-sharingan-21/vectron-32/actions/workflows/build.yml)
[![SoC](https://img.shields.io/badge/SoC-ESP32--WROOM--32-red.svg?logo=espressif)](https://www.espressif.com/)
[![RTOS](https://img.shields.io/badge/RTOS-FreeRTOS%20SMP-blue.svg)](https://www.freertos.org/)
[![C++](https://img.shields.io/badge/Standard-C%2B%2B17-blue.svg)](https://isocpp.org/)
[![Filesystem](https://img.shields.io/badge/Storage-LittleFS-orange.svg)](https://github.com/ARMmbed/littlefs)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

**VECTRON-32** is a cyber-industrial SCADA instrumentation console, real-time silicon memory profiler, and hardware telemetry engine running entirely on a dual-core **ESP32-WROOM-32** microcontroller. Designed with deterministic FreeRTOS task separation, non-blocking asynchronous WebSocket transport, hardware-accelerated SHA-256 cryptographic storage, and a single-page virtual tab telemetry dashboard.

---

## Dual-Core Architecture

VECTRON-32 enforces strict symmetric multiprocessing (SMP) core affinity across the ESP32's dual Tensilica Xtensa LX6 cores, decoupling real-time hardware sampling from asynchronous network operations:

```
┌────────────────────────────────────────────────────────────────────────┐
│                   ESP32-WROOM-32 (Dual-Core 240 MHz)                   │
├───────────────────────────────────┬────────────────────────────────────┤
│              CORE 0               │               CORE 1               │
│  [Network & Transport Engine]     │    [Instrumentation & HAL Engine]  │
│                                   │                                    │
│  ┌─────────────────────────────┐  │  ┌──────────────────────────────┐  │
│  │   vNetworkTask              │  │  │   vTelemetryTask (100ms)     │  │
│  │   - AsyncWebServer (:80)    │  │  │   - Hardware::service()      │  │
│  │   - AsyncWebSocket (/ws)    │◄─┼──│   - 10 Hz Telemetry Broadcast│  │
│  │   - Captive Portal DNS (:53)│  │  │   - I2C Auto-Discovery Engine│  │
│  │   - Session Cookie Auth     │  │  │   - LEDC PWM & GPIO Matrix   │  │
│  │   - Static Asset Streaming  │  │  │   - RTC Lockout Diagnostics  │  │
│  └─────────────────────────────┘  │  └──────────────────────────────┘  │
│                 │                 │                 │                  │
│                 ▼                 │                 ▼                  │
│     AsyncTCP Non-Blocking Stack   │     Hardware Abstraction Layer     │
│     LittleFS Flash File Server    │     I2C Mutex (xSemaphoreMutex)    │
└───────────────────────────────────┴────────────────────────────────────┘
```

> [!NOTE]
> **Default SoftAP Parameters**:
> - **SSID**: `VECTRON-32`
> - **Access IP**: `http://192.168.4.1/`
> - **Captive Portal**: Automatic redirect to `/setup` on first boot
> - **UART Baud Rate**: `115200 8-N-1`

> [!TIP]
> **Zero-Hardware Simulation**:
> You can preview, develop, and benchmark the complete web dashboard locally without an ESP32 by running the bundled mock server:
> ```bash
> node tools/mock-server.js
> ```
> Navigate to `http://localhost:8099/` (Default credentials: `demo` / `demo`). Includes full RFC 6455 WebSocket streaming.

> [!IMPORTANT]
> **Partition Scheme & LittleFS Provisioning**:
> VECTRON-32 requires the `huge_app.csv` partition layout (`3.1MB App / 1MB SPIFFS/LittleFS`). You **must** flash both the LittleFS filesystem image (`pio run --target uploadfs`) and the firmware binary (`pio run --target upload`) on first setup.

---

## Virtual Tab Architecture (SPA)

The web dashboard is structured as a client-side Single-Page Application (SPA) with a top navigation bar, eliminating vertical scrolling while maintaining a continuous, uninterrupted WebSocket connection:

1. **`[ TAB 1: SCADA CONSOLE ]`** (`#view-scada`):
   - CRT Phosphor-Green Oscilloscope (`<canvas id="telemetry-chart">`) with dark-green dashed 8x10 reticle, neon glow waveforms (Free Heap, Chip Temperature, Clients), and live in-canvas HUD legend.
   - GPIO output toggles, input pulse monitors, dynamic I2C bus auto-discovery card, and filterable diagnostics terminal (`[INFO]`, `[WARN]`, `[ERROR]`, `[BUS]`).
   - Radio TX power controls (`wifi --txpower`) and 2-step reboot confirmation (`[y/n]`).

2. **`[ TAB 2: SILICON MEMORY MAP ]`** (`#view-memory`):
   - **Internal SRAM (DRAM / Heap)**: Real-time gauges for Total Heap (320 KB), Free Heap, Historic Minimum Watermark, Heap Fragmentation %, and Largest Allocable Block.
   - **NOR Flash Map**: Segmented partition visual strip covering Bootloader, NVS (`"vectron"` namespace), `huge_app` binary utilization (3.1 MB max), and LittleFS asset storage (1.0 MB max).
   - **FreeRTOS Task Headroom**: Real-time stack headroom telemetry (`uxTaskGetStackHighWaterMark`) for `vNetworkTask` (Core 0), `vTelemetryTask` (Core 1), and `RTC_DATA_ATTR` lockout diagnostics.

3. **`[ TAB 3: BOARD & PINOUT INSPECTOR ]`** (`#view-pinout`):
   - Full vector SVG rendering of the ESP32-WROOM-32 DevKit board with interactive pin pads and traces.
   - Status-coded lighting:
     - **Neon Green (`#00ff66`)**: Active allowlisted outputs (GPIO 2 on-board LED, relays).
     - **Cyan Pulse (`#00e5ff`)**: Active inputs (GPIO 0 BOOT switch).
     - **Deep Blue (`#0088cc`)**: Guarded I2C bus lines (GPIO 21 SDA, GPIO 22 SCL).
     - **Dark Amber / Red (`#ff3333` with line-through)**: Hardware-blocked pins (strapping 12/15, flash lines 6-11, UART0 1/3).
   - Interactive hover/click cross-linking between SVG pins and the live Pinout Matrix Table.

---

## Safety Matrix & Pin Assignment

The firmware implements a strict GPIO safety matrix preventing bus contention, boot-strapping interference, and internal SPI flash corruption:

| GPIO | Default Function | Input / Output | Safety Rating | Behavioral Notes |
|:---:|:---:|:---:|:---:|:---|
| **0** | Boot Mode / Input | Input (Pullup) | ⚠️ STRAPPING | Strapping pin (must be HIGH during reset). |
| **2** | Onboard Status LED | Output | ⚠️ STRAPPING | Connected to internal LED; strapped to GND. |
| **4** | Relay Output 1 | Output | ✅ SAFE | Active-LOW relay channel. |
| **5** | Relay Output 2 | Output | ✅ SAFE | General output channel. |
| **6-11**| Integrated Flash Bus | Forbidden | ⛔ SYSTEM CRITICAL | Connected directly to SPI Flash. Never touch. |
| **12-15**| JTAG / MTDI / Strapping| Reserved | ⚠️ STRAPPING | Boot strapping pins. Excluded from runtime matrix. |
| **16** | Auxiliary Output | Output | ✅ SAFE | Safe GPIO for external load switching. |
| **17** | Auxiliary Output | Output | ✅ SAFE | Safe GPIO for external load switching. |
| **21** | I2C SDA | Bidirectional | ✅ BUS | Guarded by FreeRTOS `i2cMutex`. |
| **22** | I2C SCL | Output | ✅ BUS | Fast-mode 400 kHz clock with hardware pull-ups. |
| **34-39**| Analog / GPI | Input-Only | ⚠️ INPUT ONLY | No internal pull-up/pull-down resistors. |

> [!WARNING]
> **Relay Active-Low Logic & Inrush Current**:
> Relays wired to GPIO4 and GPIO16 typically operate in active-LOW configuration. VECTRON-32 configures pins HIGH immediately at boot to prevent relay chattering. Always ensure optocoupler isolation is powered from external 5V, not the 3.3V LDO rail.

> [!CAUTION]
> **Hardware Wiring Best Practices**:
> - Never drive inductive relay coils directly from ESP32 pins; always use optocoupled driver stages with reverse flyback protection diodes (1N4007).
> - Never exceed 3.3V on any GPIO. When switching AC mains (110V/230V), maintain physical creepage and clearance distances of at least 6mm.

---

## Non-Volatile Security & RTC Persistence

1. **SHA-256 + 16-Byte Cryptographic Salt**:
   Passphrases are salted with 16 random bytes sourced from ESP32 hardware entropy (`esp_fill_random`) and hashed using `mbedtls_sha256`. Hashes are committed to non-volatile flash (`Preferences.h`) under the `"vectron"` namespace.
2. **Brownout & Reset-Resilient Defense**:
   Brute-force lockout counters and lockout expiration timestamps reside in `RTC_DATA_ATTR static uint32_t` ultra-low-power SRAM. An attacker cannot bypass rate-limiting or lockouts by cycling power or triggering watchdog brownouts.
3. **Session Token Hardening**:
   HTTP endpoints and WebSocket upgrade requests authenticate against ephemeral 64-character hex tokens stored in memory and transported via `HttpOnly`, `SameSite=Strict` `VECTRON_SID` cookies.

---

## WebSocket Protocol Specification

The WebSocket endpoint is mounted at `ws://192.168.4.1/ws`.

### 1. Telemetry Broadcast Frame (ESP32 -> Client, 10 Hz)
```json
{
  "t": "telem",
  "up": 1420,
  "heap": 182340,
  "heapMin": 142100,
  "heapMax": 110000,
  "heapTotal": 327680,
  "heapFrag": 8.4,
  "temp": 42.6,
  "rssi": 0,
  "clients": 1,
  "cpuMHz": 240,
  "gpio": {
    "out": [
      {"id": 0, "on": true},
      {"id": 1, "on": false}
    ],
    "in": [
      {"id": 0, "active": false}
    ]
  },
  "i2c": {
    "state": "done",
    "found": [
      {"addr": 60, "name": "SSD1306", "desc": "OLED 128x64"},
      {"addr": 118, "name": "BME280", "desc": "Env sensor"}
    ]
  },
  "ble": {"adv": false, "conn": 0},
  "wifi": {"clients": 1, "ch": 6, "txLevel": 2},
  "watermark": 4280,
  "telWatermark": 4280,
  "netWatermark": 3950,
  "fsUsed": 102400,
  "fsTotal": 1048576,
  "sketchBytes": 1228800,
  "flashBytes": 4194304
}
```

### 2. Inbound Command Payloads (Client -> ESP32)
- **Actuate GPIO**:
  ```json
  {"cmd": "gpio", "id": 0, "state": 1}
  ```
- **Initiate I2C Bus Scan**:
  ```json
  {"cmd": "i2c_scan"}
  ```
- **Request Graceful Reboot**:
  ```json
  {"cmd": "reboot"}
  ```

---

## Quickstart Guide

### Prerequisites
- [PlatformIO Core](https://platformio.org/) or [VS Code PlatformIO Extension](https://marketplace.visualstudio.com/items?itemName=platformio.platformio-ide)
- ESP32-WROOM-32 development board (CP2102, CH340, or FTDI interface)
- USB data cable

### 1. Clone & Environment Setup
```bash
git clone https://github.com/debashis-sharingan-21/vectron-32.git
cd vectron-32
```

### 2. Build & Flash Filesystem (LittleFS)
Compile and upload the web dashboard, fonts, and stylesheets to flash:
```bash
pio run --target uploadfs
```

### 3. Build & Flash Firmware
Compile and upload the C++17 FreeRTOS firmware:
```bash
pio run --target upload
```

### 4. Serial Telemetry Monitor
Open the serial console at 115200 baud to monitor the FreeRTOS boot process:
```bash
pio device monitor -b 115200
```

### 5. Access the Console
1. Connect your workstation or mobile device to the Wi-Fi network: **`VECTRON-32`**.
2. The captive portal will automatically prompt login, or navigate to **`http://192.168.4.1/`**.
3. On first boot, the `/setup` wizard allows setting persistent administrator credentials and AP passphrase.

---

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

Developed with precision by **debashis-sharingan-21**  
GitHub: [@debashis-sharingan-21](https://github.com/debashis-sharingan-21)
