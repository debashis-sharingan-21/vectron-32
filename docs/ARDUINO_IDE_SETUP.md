# VECTRON-32 // Arduino IDE Setup & Deployment Guide

This guide provides complete instructions for flashing **VECTRON-32** using the official **Arduino IDE** (supporting both **v2.x** and **v1.8.x**), without requiring PlatformIO or command-line tooling.

---

## Prerequisites & Supported Environments

| Environment | Supported Versions | Notes |
| :--- | :--- | :--- |
| **Arduino IDE 2.x** | v2.2.0 or newer | Modern interface, integrated board manager, terminal monitor. |
| **Arduino IDE 1.8.x** | v1.8.19 (Legacy) | Supports Java-based LittleFS GUI uploader plugin. |
| **Target Hardware** | ESP32 DevKit V1 / ESP32-WROOM-32 | 30-pin and 38-pin DevKit modules supported. |

---

## Step 1: Install ESP32 Board Package

1. Launch **Arduino IDE**.
2. Open **Preferences** (`Ctrl + ,` on Windows/Linux, `Cmd + ,` on macOS).
3. In **Additional boards manager URLs**, add the official Espressif package URL:
   ```text
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
   *(If you already have URLs listed, separate them with commas or newlines).*
4. Navigate to **Tools > Board > Boards Manager...** (or click the Board icon in the IDE 2.x left sidebar).
5. Search for `esp32` by **Espressif Systems** and install version **2.0.14** or newer (v3.x is also supported).

---

## Step 2: Install Required Libraries

VECTRON-32 relies on asynchronous networking libraries for real-time WebSocket telemetry and the captive portal web server:

1. **AsyncTCP** (`esp32async/AsyncTCP`):
   - In Arduino IDE, go to **Sketch > Include Library > Manage Libraries...**
   - Search for `AsyncTCP` and install the package by **ESP32Async** or **dvarrel**.
   - *Manual installation alternative:* Download the ZIP from [GitHub: esp32async/AsyncTCP](https://github.com/esp32async/AsyncTCP) and select **Sketch > Include Library > Add .ZIP Library...**.

2. **ESPAsyncWebServer** (`esp32async/ESPAsyncWebServer`):
   - In Library Manager, search for `ESPAsyncWebServer` and install the library by **ESP32Async** (v3.7.0 or newer).
   - *Manual installation alternative:* Download the ZIP from [GitHub: esp32async/ESPAsyncWebServer](https://github.com/esp32async/ESPAsyncWebServer) and select **Sketch > Include Library > Add .ZIP Library...**.

> [!NOTE]
> VECTRON-32 uses standard built-in ESP32 core libraries for WiFi (`WiFi.h`), Filesystem (`LittleFS.h`), DNS (`DNSServer.h`), and Cryptography (`mbedtls/sha256.h`). No external JSON library is needed—the firmware avoids ArduinoJson entirely to optimize heap allocation.

---

## Step 3: Critical Board Menu Settings

Open the sketch at `arduino/vectron_32/vectron_32.ino`. Then configure the board parameters under the **Tools** menu:

| Menu Item | Required Selection | Critical Rationale |
| :--- | :--- | :--- |
| **Board** | `ESP32 Arduino` &rarr; **ESP32 Dev Module** | Configures pinout & register maps for ESP32-WROOM-32. |
| **Partition Scheme** | **Huge APP (3MB No OTA/1MB SPIFFS)** | **MANDATORY.** Firmware binary is ~1.2MB and requires 3MB app flash partition. |
| **Flash Frequency** | **80MHz** | Ensures deterministic 10Hz WebSocket telemetry throughput. |
| **Upload Speed** | **460800** (or `921600`) | High-speed flashing. Drop to `115200` if your USB cable experiences drops. |
| **CPU Frequency** | **240MHz (WiFi/BT)** | Full clock frequency for dual-core FreeRTOS concurrency. |
| **Core Debug Level** | **None** (or `Error`) | Prevents UART debug spam from interfering with baud 115200. |
| **Port** | Select your ESP32 COM Port | e.g., `COM3`, `COM4` on Windows or `/dev/ttyUSB0` on Linux. |

> [!IMPORTANT]
> If you compile with the default partition scheme (*Default 4MB with spiffs (1.2MB APP)*), compilation will fail with `sketch too large` or flash memory overflow. Always select **Huge APP (3MB No OTA/1MB SPIFFS)**.

---

## Step 4: Flash LittleFS Web Console Assets

VECTRON-32's SCADA UI and 3D terminal assets reside in the `data/` directory inside `arduino/vectron_32/data/`. These must be flashed into the 1MB LittleFS partition:

### Method A: Arduino IDE 2.x (Recommended)
1. Download the **arduino-littlefs-upload** tool extension from [arduino-littlefs-upload GitHub](https://github.com/earlephilhower/arduino-littlefs-upload/releases).
2. Follow the plugin installation instructions: place the extracted folder in your Arduino IDE plugin directory:
   - Windows: `C:\Users\<YourUser>\.arduinoIDE\plugins\`
   - macOS: `~/Library/Application Support/arduino-ide/plugins/`
   - Linux: `~/.arduinoIDE/plugins/`
3. Restart Arduino IDE 2.x.
4. Press `Ctrl + Shift + P` (or `Cmd + Shift + P` on macOS) to open the Command Palette.
5. Type `Upload LittleFS to Pico/ESP8266/ESP32` and press Enter.

### Method B: Arduino IDE 1.8.x (Legacy Plugin)
1. Download `arduino-esp32-littlefs-plugin.jar` from [arduino-esp32-littlefs-plugin](https://github.com/lorol/arduino-esp32-littlefs-plugin/releases).
2. Place the `.jar` file into your Arduino sketchbook directory:
   `<sketchbook_location>/tools/ESP32LittleFS/tool/esp32littlefs.jar`
3. Restart Arduino IDE 1.8.x.
4. With `arduino/vectron_32/vectron_32.ino` open, click **Tools > ESP32 LittleFS Data Upload**.

---

## Step 5: Compile & Flash Firmware

1. Click the **Verify** button (`✓`) to compile the sketch and verify zero compilation errors.
2. Connect your ESP32 board to your computer via micro-USB.
3. Hold the **BOOT** (GPIO 0) button if your board does not have an automatic reset capacitor circuit.
4. Click the **Upload** button (`➔`).
5. Once uploading finishes, open the **Serial Monitor** at **115200 baud**.
6. Press the board **EN / RST** button. You should see the boot banner:
   ```text
   ==================================================
    VECTRON-32 - Real-Time Instrumentation Console
    firmware 2.0.0 (Arduino IDE Build)
   ==================================================
   [SYS] chip ESP32-D0WD-V3 rev 3, 2 core(s) @ 240 MHz, flash 4096 KB
   [NVS] opened, first-boot=no
   [FS] LittleFS mounted (1376256 / 1048576 bytes used)
   [WIFI] AP "VECTRON-32" up on channel 6  IP 192.168.4.1
   [HTTP] server started on port 80
   [SYS] launching dual-core FreeRTOS tasks...
   [NET] task started on core 0
   [TEL] task started on core 1
   [SYS] READY
   ```

---

## Step 6: Connect to the SoftAP & Authenticate

1. On your laptop, tablet, or smartphone, search for Wi-Fi networks and connect to:
   - **SSID**: `VECTRON-32`
   - **Password**: None (Open on initial boot; can be configured in `/setup`)
2. Open a browser and navigate to:
   ```text
   http://192.168.4.1/
   ```
   *(The captive portal will also trigger automatically on mobile devices).*
3. On `#screen-login`, enter default fallback credentials:
   - **Operator**: `demo`
   - **Passkey**: `demo`
4. Click **[ AUTHENTICATE & ENGAGE ]** to trigger the CRT Access Granted sequence, vector DevKit schematic trace, and SCADA dashboard.

---

## Troubleshooting Guide

### Issue: `Sketch too big` or `text section exceeds available space`
- **Fix:** In **Tools > Partition Scheme**, select **Huge APP (3MB No OTA/1MB SPIFFS)**.

### Issue: `LittleFS.h: No such file or directory`
- **Fix:** Ensure the ESP32 board package version in Boards Manager is updated to **v2.0.0 or higher**.

### Issue: `Failed to connect to ESP32: Timed out waiting for packet header`
- **Fix:** Press and hold the **BOOT** button on the ESP32 DevKit board as soon as `Connecting........_____` appears in the console.

### Issue: Web page shows 404 or raw white screen
- **Fix:** You forgot to flash the LittleFS partition. Run **Step 4** to upload the contents of the `data/` folder.
