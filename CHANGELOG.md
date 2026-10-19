# Changelog

All notable changes to this project are documented here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [2.0.0] — VECTRON-32 Release

### Added
- **FreeRTOS Dual-Core SMP Architecture:** Pinned network, WebSocket, and DNS routines to Core 0 (`vNetworkTask`); pinned hardware telemetry and peripheral sampling to Core 1 (`vTelemetryTask`, 100 ms period).
- **Zero-Dependency 10 Hz WebSocket Broadcast:** High-frequency compact JSON telemetry streamed to clients via `/ws`.
- **First-Boot Setup Wizard & Cryptographic Flash Provisioning:** `/setup` endpoint with salted SHA-256 password hashing (16-byte random salt generated via ESP32 hardware RNG) stored in NVS under `"vectron"`.
- **Brownout-Resilient RTC Memory Defense:** Brute-force lockout counters and lockout durations stored in `RTC_DATA_ATTR` memory.
- **I2C Auto-Discovery Engine:** Dynamic peripheral registration and signature identification with `i2cMutex` thread safety.
- **LEDC PWM Hardware Driver:** Dedicated duty and smooth fading API.
- **Cyber-Industrial SCADA Console:** Multi-trace oscilloscope canvas with dark green reticle, neon trace glow, live HUD numerical metrics, FreeRTOS stack high-watermark monitors, and mobile-responsive CSS Grid layout.
- **Creator Attribution:** Developed by `debashis-sharingan-21` (GitHub: [@debashis-sharingan-21](https://github.com/debashis-sharingan-21)).
