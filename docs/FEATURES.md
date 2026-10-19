# Features and Implementation Status

Back to the [README](../README.md).

| Feature | Implemented | Architecture / Engine |
|---|:--:|---|
| FreeRTOS Dual-Core Task Separation | yes | Core 0: `vNetworkTask`, Core 1: `vTelemetryTask` |
| Real-Time WebSocket Telemetry | yes | `/ws` broadcasting compact JSON at 10 Hz |
| Captive Portal DNS Server | yes | Port 53 resolving to 192.168.4.1 |
| First-Boot Setup Wizard | yes | `/setup` wizard for NVS credential initialization |
| Cryptographic NVS Storage | yes | SHA-256 + 16-byte random salt via ESP32 hardware RNG |
| RTC Memory Lockout Defense | yes | `RTC_DATA_ATTR` counters survive brownouts/resets |
| I2C Auto-Discovery Engine | yes | `i2cMutex` protected scanner matching peripheral signatures |
| LEDC PWM Hardware Engine | yes | Dedicated fading and duty cycle control |
| Strict GPIO Safety Matrix | yes | Strapping (0/2/12/15), UART (1/3), SPI Flash (6-11) protected |
| Cyber-Industrial SCADA UI | yes | Oscilloscope canvas with live HUD, RTOS metrics, mobile responsive |
| Offline Asset Serving | yes | Embedded LittleFS serving woff2 fonts and styling |

## Feature Notes

- **Dual-Core SMP:** Real-time hardware telemetry runs deterministically on Core 1 (`vTelemetryTask`, 100 ms period) while asynchronous network requests and WebSockets are serviced on Core 0 (`vNetworkTask`).
- **BLE Advertising:** Advertises the device name `VECTRON-32`.
- **GPIO Matrix:** Hardware matrix actively forbids invalid pin configurations and handles active-low relay inversion safely.
- **Responsive Telemetry Canvas:** 60 FPS rolling oscilloscope displaying Heap, Chip Temperature, and Client connection metrics.
