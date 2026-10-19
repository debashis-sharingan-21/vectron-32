# Installation & Flashing Guide

Back to the [README](../README.md).

## Prerequisites

- [PlatformIO Core](https://platformio.org/) or VS Code PlatformIO IDE extension.
- ESP32-WROOM-32 development board.
- USB data cable.

## Step-by-Step Setup

### 1. Open the Project
Open the folder containing `platformio.ini` in VS Code or navigate to the terminal.

### 2. Build & Upload LittleFS
Upload the frontend web assets to flash:
```bash
pio run --target uploadfs
```

### 3. Build & Upload Firmware
Compile and flash the FreeRTOS dual-core firmware:
```bash
pio run --target upload
```

### 4. Monitor Serial Output
Connect to the serial console at 115200 baud:
```bash
pio device monitor -b 115200
```

Expected boot log:
```text
[SYS] VECTRON-32 Command Center v2.0
[RTOS] Tasks spawned: vNetworkTask (Core 0), vTelemetryTask (Core 1)
[FS] LittleFS mounted successfully
[GPIO] Matrix initialized
[WIFI] SoftAP "VECTRON-32" active on 192.168.4.1
[HTTP] Server & WebSocket mounted at /ws
```

### 5. First-Boot Credential Setup
1. Join the Wi-Fi network: **`VECTRON-32`**.
2. Open your browser and navigate to `http://192.168.4.1/`.
3. If credentials have not been configured in NVS, the captive portal will automatically redirect to `/setup`.
4. Enter your desired administrator credentials and AP passphrase, then submit.
5. The board will securely commit salted SHA-256 hashes to NVS and reboot into operational mode.

### 6. Hardware Verification Checks

| Check | What it proves |
|---|---|
| `ONBOARD_LED` toggle | GPIO 2 level changes (blue LED lights; WS state broadcast). |
| Hold the **BOOT** button | Dashboard shows `[ ACTIVE ]` (GPIO 0 input). |
| BLE Toggle | BLE scanner detects advertising name `VECTRON-32`. |
| I2C Scan | Discovers attached peripherals (SSD1306, BME280, etc.). |
| FreeRTOS Health | Stack high-water mark metrics update in real-time. |
