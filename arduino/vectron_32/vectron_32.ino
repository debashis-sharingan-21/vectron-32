// ===========================================================================
// VECTRON-32 - Real-Time Embedded Instrumentation & SCADA Console
// Arduino IDE Standalone Sketch: vectron_32.ino
//
// Dual-Core FreeRTOS Architecture:
//   Core 0: Network Task (AsyncWebServer, WebSocket, DNS Captive Portal)
//   Core 1: Telemetry Task (Hardware polling, I2C discovery, 10Hz WS push)
//
// Board Settings in Arduino IDE:
//   - Board: "ESP32 Dev Module"
//   - Partition Scheme: "Huge APP (3MB No OTA/1MB SPIFFS)"
//   - Flash Frequency: 80MHz
//   - Upload Speed: 460800
// ===========================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <DNSServer.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "config.h"
#include "auth.h"
#include "hardware.h"
#include "api.h"
#include "storage.h"

// ---------------------------------------------------------------------------
// Globals shared between tasks
// ---------------------------------------------------------------------------
static AsyncWebServer server(80);
static DNSServer      dns;
static bool           fsOk = false;

// Task handles
static TaskHandle_t s_netTask = nullptr;
static TaskHandle_t s_telTask = nullptr;

// ---------------------------------------------------------------------------
// Forward declarations of tasks
// ---------------------------------------------------------------------------
static void vNetworkTask(void* pv);
static void vTelemetryTask(void* pv);

// ---------------------------------------------------------------------------
// Helper: list filesystem contents to Serial
// ---------------------------------------------------------------------------
static void listFiles(const char* dir, uint8_t depth = 0) {
    File root = LittleFS.open(dir);
    if (!root || !root.isDirectory()) return;
    File f = root.openNextFile();
    while (f) {
        for (uint8_t i = 0; i < depth; i++) Serial.print("  ");
        if (f.isDirectory()) {
            Serial.printf("  %s/\n", f.name());
            listFiles(f.path(), depth + 1);
        } else {
            Serial.printf("  %s (%u bytes)\n", f.name(), (unsigned)f.size());
        }
        f = root.openNextFile();
    }
}

// ---------------------------------------------------------------------------
// setup() — runs on Core 1 (Arduino default), then hands control to tasks.
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println("==================================================");
    Serial.println(" VECTRON-32 - Real-Time Instrumentation Console");
    Serial.println(" firmware " FW_VERSION " (Arduino IDE Build)");
    Serial.println("==================================================");
    Serial.printf("[SYS] chip %s rev %d, %d core(s) @ %u MHz, flash %u KB\n",
                  ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(),
                  ESP.getCpuFreqMHz(), (unsigned)(ESP.getFlashChipSize() / 1024));

    // ---- 1. NVS / Storage --------------------------------------------------
    if (!Storage::begin()) {
        Serial.println("[NVS] WARNING: NVS init failed; using defaults");
    }

    // ---- 2. Filesystem -----------------------------------------------------
    // Do NOT auto-format: a failed mount means "upload the FS image first".
    fsOk = LittleFS.begin(false);
    if (fsOk) {
        Serial.printf("[FS] LittleFS mounted (%u / %u bytes used)\n",
                      (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
        listFiles("/");
        if (!LittleFS.exists("/index.html"))
            Serial.println("[FS] WARNING: /index.html missing - upload the filesystem image");
    } else {
        Serial.println("[FS] MOUNT FAILED - please upload LittleFS filesystem data");
    }

    // ---- 3. Hardware init --------------------------------------------------
    Hardware::begin();

    // ---- 4. Auth init ------------------------------------------------------
    Auth::begin();

    // ---- 5. Wi-Fi access point ---------------------------------------------
    WiFi.mode(WIFI_AP);
    WiFi.setSleep(false);

    // Use stored credentials when available, fall back to compile-time defaults.
    String apSsid = Storage::getApSsid();
    String apPass = Storage::getApPassword();

    bool apOk = WiFi.softAP(
        apSsid.c_str(),
        apPass.length() >= 8 ? apPass.c_str() : nullptr,  // open AP if password too short
        VECTRON_AP_CHANNEL,
        VECTRON_AP_HIDDEN,
        VECTRON_AP_MAX_CLIENTS
    );
    if (!apOk) {
        Serial.println("[WIFI] softAP FAILED");
    } else {
        Hardware::setTxPowerLevel(2);
        Serial.printf("[WIFI] AP \"%s\" up on channel %u  IP %s\n",
                      apSsid.c_str(), VECTRON_AP_CHANNEL,
                      WiFi.softAPIP().toString().c_str());
    }

    // ---- 6. Captive-portal DNS (redirect all domains to 192.168.4.1) -------
    // TTL=0 so clients don't cache the hijacked response.
    dns.setErrorReplyCode(DNSReplyCode::NoError);
    dns.start(53, "*", WiFi.softAPIP());

    // ---- 7. Register web routes and start server ---------------------------
    Api::begin(server, fsOk);
    server.begin();
    Serial.println("[HTTP] server started on port 80");
    Serial.println("[SYS] launching dual-core FreeRTOS tasks...");

    // ---- 8. FreeRTOS tasks -------------------------------------------------
    // Network task — Core 0, priority 1 (lower than telemetry)
    xTaskCreatePinnedToCore(
        vNetworkTask,
        "netTask",
        TASK_NETWORK_STACK,
        nullptr,
        1,
        &s_netTask,
        0   // Core 0
    );

    // Telemetry task — Core 1, priority 2
    xTaskCreatePinnedToCore(
        vTelemetryTask,
        "telTask",
        TASK_TELEMETRY_STACK,
        nullptr,
        2,
        &s_telTask,
        1   // Core 1
    );

    // Register task handles with Hardware for stack watermark monitoring.
    Hardware_setNetworkTaskHandle(s_netTask);
    Hardware_setTelemetryTaskHandle(s_telTask);

    Serial.println("[SYS] READY");
}

// ---------------------------------------------------------------------------
// loop() — Arduino's main loop is no longer used; we delete this task so it
// does not waste stack and FreeRTOS idle cycles.
// ---------------------------------------------------------------------------
void loop() {
    vTaskDelete(nullptr);
}

// ---------------------------------------------------------------------------
// vNetworkTask — Core 0
// Drives the DNS captive-portal responder and cleans up WebSocket clients.
// ---------------------------------------------------------------------------
static void vNetworkTask(void* pv) {
    (void)pv;
    Serial.println("[NET] task started on core " + String(xPortGetCoreID()));
    for (;;) {
        dns.processNextRequest();
        Api::cleanupWsClients();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    // unreachable
    vTaskDelete(nullptr);
}

// ---------------------------------------------------------------------------
// vTelemetryTask — Core 1
// Reads hardware state at a fixed 100 ms interval and broadcasts telemetry
// to all connected WebSocket clients.
// ---------------------------------------------------------------------------
static void vTelemetryTask(void* pv) {
    (void)pv;
    Serial.println("[TEL] task started on core " + String(xPortGetCoreID()));

    TickType_t xLastWake = xTaskGetTickCount();
    for (;;) {
        // Run hardware service (BLE toggle, I2C scan, PWM fades, reboot).
        Hardware::service();

        // Push current telemetry to WebSocket clients.
        Api::broadcastTelemetry();

        // Wait for the next 100 ms window, accounting for execution time.
        vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(TELEMETRY_INTERVAL_MS));
    }
    // unreachable
    vTaskDelete(nullptr);
}
