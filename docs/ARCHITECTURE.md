# VECTRON-32 Command Center v2.0 — Architecture Reference

> **Audience:** firmware contributors and technical reviewers.  
> This document describes the internal design of the VECTRON-32 ESP32 firmware: task scheduling, state machines, concurrency model, WebSocket flow, authentication, I2C discovery, and the security threat model.

---

## Table of Contents

1. [FreeRTOS Task Schedule](#1-freertos-task-schedule)
2. [Boot State Machine](#2-boot-state-machine)
3. [Memory Layout](#3-memory-layout)
4. [Concurrency Model](#4-concurrency-model)
5. [WebSocket Message Flow](#5-websocket-message-flow)
6. [Authentication Flow](#6-authentication-flow)
7. [I2C Discovery Engine](#7-i2c-discovery-engine)
8. [Security Threat Model](#8-security-threat-model)

---

## 1. FreeRTOS Task Schedule

VECTRON-32 runs two primary application tasks. The Arduino `loop()` task is intentionally kept idle (or deletes itself) — all real work happens in the two explicitly created tasks below.

| Task Name | Core | Priority | Stack | Period | Responsibilities |
|-----------|:----:|:--------:|------:|-------:|------------------|
| `vNetworkTask` | 0 | 5 | 8 192 B | Event-driven | AsyncWebServer request handling, AsyncWebSocket dispatch, DNSServer query processing, captive portal redirects, session validation |
| `vTelemetryTask` | 1 | 4 | 6 144 B | 100 ms | Call `Hardware::service()`, read GPIO / I2C / ADC state, build telemetry frame, call `Api::broadcastTelemetry()` |
| `loopTask` (Arduino) | 1 | 1 | 8 192 B | 1 000 ms | Idle / deleted via `vTaskDelete(NULL)` |

**Priority rationale:** `vNetworkTask` runs at a higher priority (5) than `vTelemetryTask` (4) so that incoming HTTP and WS frames are serviced promptly. Because `vTelemetryTask` uses `vTaskDelayUntil`, it yields the CPU for the vast majority of each 100 ms window.

---

## 2. Boot State Machine

VECTRON-32 implements a three-state finite state machine (FSM) that governs which services are started and which HTTP routes are active.

```
  ┌─────────────┐   NVS credentials     ┌──────────────────┐
  │  FIRST_BOOT │──── not found ────────►│  SETUP_WIZARD    │
  └─────────────┘                        └────────┬─────────┘
                                                  │
                                     POST /api/setup (valid form)
                                                  │
                                         NVS write + reboot
                                                  │
                                                  ▼
                                         ┌────────────────┐
                                         │  OPERATIONAL   │
                                         └────────────────┘
```

### State Descriptions

| State | Active Services | HTTP Routes Exposed |
|-------|----------------|---------------------|
| `FIRST_BOOT` | AP mode only, DNSServer, AsyncWebServer (minimal) | `GET /`, `GET /setup`, `POST /api/setup` |
| `SETUP_WIZARD` | AP mode, captive portal, full DNS redirect | `GET /setup`, `POST /api/setup`, static assets |
| `OPERATIONAL` | AP + STA (if configured), all routes, WebSocket, telemetry task | All routes; `/setup` returns 403 |

**Transition guards:**
- `FIRST_BOOT → SETUP_WIZARD`: automatic on boot if NVS namespace `vectron` is empty.
- `SETUP_WIZARD → OPERATIONAL`: `POST /api/setup` validates input (username ≥ 4 chars, password ≥ 8 chars, SHA-256 + salt hash), writes to NVS, calls `ESP.restart()`.
- `OPERATIONAL` is terminal until a factory reset (long-press BOOT button or resetting NVS with valid session).

---

## 3. Memory Layout

### NVS Namespaces

| Namespace | Key | Type | Description |
|-----------|-----|------|-------------|
| `vectron` | `user` | String | Plaintext username (≤ 32 chars) |
| `vectron` | `hash` | String | Hex SHA-256 digest of `salt + plainAdminPass` |
| `vectron` | `salt` | String | Hex representation of 16-byte cryptographic salt |
| `vectron` | `ssid` | String | AP SSID override (default: `VECTRON-32`) |
| `vectron` | `pass` | String | AP passphrase |

### RTC_DATA_ATTR Variables (Survive Deep-Sleep & Brownout)

| Variable | Type | Description |
|----------|------|-------------|
| `rtc_fail_count` | `uint32_t` | Consecutive failed login attempts |
| `rtc_lock_until` | `uint32_t` | Millis timestamp; logins rejected before this time |

### Heap Usage Breakdown (Steady-State, ~140 KB Free)

| Segment | Approximate Usage | Notes |
|---------|-----------------:|-------|
| Wi-Fi stack (lwIP + WPA) | ~80 KB | Allocated by IDF at `WiFi.begin()` |
| AsyncWebServer + routes | ~12 KB | Static allocation; scales with route count |
| AsyncWebSocket + clients | ~6 KB per client | Headers, TX/RX buffers |
| FreeRTOS task stacks | ~16 KB | Sum of all task stacks (see §1) |
| Telemetry frame buffer | ~1 KB | String serialization buffer (stack-allocated per call) |
| Application globals | ~4 KB | Session tokens, config structs, misc |

---

## 4. Concurrency Model

VECTRON-32 uses FreeRTOS primitives to protect shared data between `vNetworkTask` (Core 0) and `vTelemetryTask` (Core 1):

### `i2cMutex` — `SemaphoreHandle_t` (Mutex)

The I2C bus is shared between `vTelemetryTask` (periodic sensor polling) and the on-demand `POST /api/i2c/scan` handler (runs in `vNetworkTask` context via AsyncWebServer callbacks).

```
Any I2C access:
  xSemaphoreTake(Hardware::i2cMutex(), portMAX_DELAY)
    Wire.beginTransmission(addr);
    Wire.endTransmission();
  xSemaphoreGive(Hardware::i2cMutex());
```

---

## 5. WebSocket Message Flow

```
Client                          vNetworkTask (Core 0)           vTelemetryTask (Core 1)
──────                          ─────────────────────           ──────────────────────
HTTP GET /ws ──────────────────►
                                Upgrade: WebSocket
                                Check VECTRON_SID cookie
                                ◄──── 101 Switching Protocols ──
                                      (or 1008 if token invalid)

                                                                Hardware::service()
                                                                Build Telemetry Frame
                                                                Api::broadcastTelemetry()
                                ◄───────────────────────────────
◄──────────────────────────── ws.textAll(telemetryJSON)

────── {"cmd":"gpio","id":0,"state":1} ────────────────────────►
                                Validate VECTRON_SID session
                                Execute GPIO set
                                Immediate state broadcast

────── disconnect ──────────────►
                                onDisconnect(client)
                                Cleanup client slot
```

---

## 6. Authentication Flow

```
First Boot                        NVS                         Client
──────────                        ───                         ──────
POST /api/setup {user, pass}
  generate salt (16 B random)
  hash = SHA256(salt + pass)
  nvsWrite("vectron", "hash", hash)
  nvsWrite("vectron", "salt", salt)
  nvsWrite("vectron", "user", user)
  ESP.restart()

                                                POST /api/login {user, pass}
                                  ◄── nvsRead salt, hash ───────────────────
                                  check rtc_fail_count ≥ 5 → 429 Locked
                                  recompute SHA256(salt + pass)
                                  constant-time compare with stored hash
                                  match? → generate 64-char hex session token
                                           store token in RAM session table
                                           rtc_fail_count = 0
                                  ◄── Set-Cookie: VECTRON_SID=...; 200 OK ──
                                  no match?
                                           rtc_fail_count++
                                           if ≥ 5: rtc_lock_until = millis() + 60000
                                  ◄── 401 Unauthorized ────────────────────
```

---

## 7. I2C Discovery Engine

1. `xSemaphoreTake(Hardware::i2cMutex(), portMAX_DELAY)`
2. Iterate addresses `0x01` through `0x7E`
3. For each address: `Wire.beginTransmission(addr)` → `Wire.endTransmission()`
4. On ACK (return code == 0): match address against peripheral signature database:
   - `0x3C`, `0x3D`: SSD1306 (OLED 128x64)
   - `0x48`, `0x49`: ADS1115 (16-bit ADC)
   - `0x68`, `0x69`: MPU6050 (6-axis IMU)
   - `0x76`, `0x77`: BME280 (Temperature/Humidity/Pressure)
   - `0x20..0x27`: PCF8574 (GPIO Expander)
5. `xSemaphoreGive(Hardware::i2cMutex())`
6. Expose structured JSON peripheral telemetry frame to WebSocket clients.

---

## 8. Security Threat Model

VECTRON-32 is designed for use on a **local, instrumentation network**.

### What VECTRON-32 Defends Against

| Threat | Mitigation |
|--------|-----------|
| **Credential brute force** | `RTC_DATA_ATTR rtc_fail_count`: lockout after 5 failures; survives reboots and brownouts |
| **Credential storage leak** | Passwords are never stored plaintext; only `SHA-256(salt + plainAdminPass)` written to NVS; 16-byte random salt is unique per device |
| **Replay attacks on session tokens** | Ephemeral 64-character hex tokens generated via ESP32 hardware RNG; invalidated on logout or idle timeout |
| **Cross-Site Request Forgery (CSRF)** | `SameSite=Strict`, `HttpOnly` cookie enforcement and Origin header validation |
| **GPIO pin sabotage** | Runtime GPIO safety matrix rejects writes to strapping / UART / SPI-flash pins |
| **Firmware bricking via setup re-run** | `/api/setup` returns 403 in OPERATIONAL state |
| **I2C bus race conditions** | `i2cMutex` guards all Wire transactions |

### What VECTRON-32 Does Not Defend Against

| Gap | Explanation |
|-----|-------------|
| **No TLS / HTTPS** | ESP32 TLS adds substantial RAM overhead and requires certificate authority infrastructure. VECTRON-32 operates over plaintext HTTP/WS on the local AP. |
| **Physical access to flash** | Unencrypted flash can be read with a chip programmer unless ESP32 Flash Encryption is enabled in production fuses. |
