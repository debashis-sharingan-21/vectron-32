#pragma once
#include <Arduino.h>
// NOTE: credentials are stored in NVS (see Storage namespace). No secrets.h needed.

// ---------------------------------------------------------------------------
// VECTRON-32 - Central Configuration & Hardware Definition
// Real-Time Embedded Instrumentation & SCADA Console
// ---------------------------------------------------------------------------

#define FW_NAME      "VECTRON-32"
#define FW_VERSION   "2.0.0"

// ---- Wi-Fi Access Point ---------------------------------------------------
static const uint8_t  VECTRON_AP_CHANNEL     = 6;
static const uint8_t  VECTRON_AP_MAX_CLIENTS = 4;       // ESP32 limit: 10
static const bool     VECTRON_AP_HIDDEN      = false;

// Default credentials (used only when NVS has no provisioned credentials)
#define AP_DEFAULT_SSID     "VECTRON-32"
#define AP_DEFAULT_IP       "192.168.4.1"

// ---- Bluetooth Low Energy -------------------------------------------------
#define BLE_DEVICE_NAME     "VECTRON-32"

// ---- Authentication -------------------------------------------------------
static const uint32_t SESSION_IDLE_TIMEOUT_MS = 30UL * 60UL * 1000UL;  // 30 min
static const uint8_t  MAX_SESSIONS            = 3;
static const uint8_t  LOGIN_MAX_FAILS         = 5;       // wrong tries before lockout
static const uint32_t LOGIN_LOCK_MS           = 60000;   // 60 s (doubles per repeat, max ×8)

// ---- I2C Bus used by the Peripherals Scanner ------------------------------
static const uint8_t I2C_SDA = 21;
static const uint8_t I2C_SCL = 22;

// ---- Telemetry / WebSocket Broadcast Timing -------------------------------
static const uint32_t TELEMETRY_INTERVAL_MS = 100;   // 10 Hz telemetry task cycle
static const uint32_t WS_BROADCAST_MS       = 100;   // 10 Hz WS clients update rate

// ---- FreeRTOS Task Stack Sizes (bytes) ------------------------------------
static const uint32_t TASK_TELEMETRY_STACK = 8192;
static const uint32_t TASK_NETWORK_STACK   = 8192;

// ---------------------------------------------------------------------------
// GPIO OUTPUTS the console may control.
// Only pins listed here exist as far as the web API is concerned.
//
// Allowed output pins (enforced at boot, see Hardware::isSafeOutputPin):
//   2 4 5 13 14 15 16 17 18 19 23 25 26 27 32 33
// Strapping pins (0/12), UART (1/3), flash (6-11), and input-only (34-39) are BLOCKED.
// ---------------------------------------------------------------------------
struct OutputPin {
  uint8_t     pin;
  const char* name;       // shown in UI, short A-Z 0-9 _
  bool        activeHigh; // true: HIGH = ON. false: LOW = ON (e.g. relay modules)
  bool        initialOn;  // state applied at boot
};

static const OutputPin OUTPUTS[] = {
  { 2, "ONBOARD_LED", true, false },   // blue LED on most ESP32 DevKit boards
  // { 4,  "LED_EXT_1", true,  false }, // external LED + resistor
  // { 16, "RELAY_1",   false, false }, // active-LOW relay module
};
static const size_t OUTPUT_COUNT = sizeof(OUTPUTS) / sizeof(OUTPUTS[0]);

// ---------------------------------------------------------------------------
// GPIO INPUTS shown read-only on the dashboard.
// ---------------------------------------------------------------------------
struct InputPin {
  uint8_t     pin;
  const char* name;
  bool        pullup;     // enable internal pull-up
  bool        activeLow;  // true: LOW = "active"
};

static const InputPin INPUTS[] = {
  { 0, "BOOT_BUTTON", true, true },    // DevKit onboard BOOT button
};
static const size_t INPUT_COUNT = sizeof(INPUTS) / sizeof(INPUTS[0]);
