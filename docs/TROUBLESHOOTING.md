# Troubleshooting Guide

Back to the [README](../README.md). Always check the serial monitor first (`pio device monitor -b 115200`).

## Build and Upload

| Symptom | Likely cause | Fix |
|---|---|---|
| Build error mentioning ESPAsyncWebServer / AsyncTCP | Stale or duplicate library copies | Run `pio run -t clean`, delete `.pio\libdeps`, build again. |
| Toolchain download fails | Network proxy or firewall | Check internet connection / proxy and retry. |
| `Failed to connect to ESP32: Timed out waiting for packet header` | Not in bootloader mode, bad cable, port busy | Hold **BOOT** button while uploading; use a reliable data cable; close any open serial monitor. |
| Windows shows no COM port | Missing USB-serial driver | Install CP210x or CH340 drivers matching your board. |

## Filesystem (LittleFS) & Dashboard

| Symptom | Likely cause | Fix |
|---|---|---|
| Page says **"Filesystem not mounted"** | LittleFS image not uploaded | Run `pio run --target uploadfs` and reset board. |
| Page loads unstyled / missing fonts | Cache or incomplete upload | Re-run `pio run --target uploadfs`, then Ctrl+F5 in browser. |

## Wi-Fi and Connecting

| Symptom | Likely cause | Fix |
|---|---|---|
| Can't see Wi-Fi network | AP initialization delayed | Check serial log for `[WIFI] SoftAP "VECTRON-32"`. |
| Browser can't reach `192.168.4.1` | Connected to wrong network, or `https://` prefix | Connect to `VECTRON-32` and enter `http://192.168.4.1/`. |

## Authentication & Sessions

| Symptom | Likely cause | Fix |
|---|---|---|
| Stuck on `/setup` page | First boot provisioning | Complete the setup wizard to initialize credentials in NVS. |
| Button shows `LOCKED` | Rate limit lockout after 5 failed tries | Wait for the cooldown timer. Lockout persists across brownouts via `RTC_DATA_ATTR`. |
