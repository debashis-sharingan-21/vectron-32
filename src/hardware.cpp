#include "hardware.h"
#include "config.h"

#include <WiFi.h>
#include <Wire.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <esp_system.h>   // esp_reset_reason()
#include <esp32/rom/ets_sys.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

static_assert(OUTPUT_COUNT <= 8, "VECTRON-32 supports at most 8 configured outputs");

// ---------------------------------------------------------------------------
// Forward declaration for telemetry task handle (set by main.cpp via a weak
// reference mechanism; we provide a setter so main.cpp can register it).
// ---------------------------------------------------------------------------
static TaskHandle_t s_telTask = nullptr;
static TaskHandle_t s_netTask = nullptr;

// Call this from main.cpp after xTaskCreatePinnedToCore for the telemetry task.
extern "C" void Hardware_setTelemetryTaskHandle(TaskHandle_t h) {
    s_telTask = h;
}

// Call this from main.cpp after xTaskCreatePinnedToCore for the network task.
extern "C" void Hardware_setNetworkTaskHandle(TaskHandle_t h) {
    s_netTask = h;
}

namespace {

// ---------- Output state ----------------------------------------------------
bool outBlocked[8];   // OUTPUT_COUNT must be <= 8

// ---------- I2C -------------------------------------------------------------
static const uint8_t   MAX_I2C_FOUND = 24;
volatile bool          i2cRequest = false;
volatile Hardware::I2cState i2cSt = Hardware::I2C_IDLE;
uint8_t                i2cFound[MAX_I2C_FOUND];
uint8_t                i2cCount = 0;
static SemaphoreHandle_t s_i2cMutex = nullptr;

// ---------- BLE -------------------------------------------------------------
volatile int8_t bleRequest = -1;   // -1 = none, 0 = stop, 1 = start
bool            bleOn      = false;
BLEServer*      bleServer  = nullptr;

// ---------- Radio -----------------------------------------------------------
uint8_t txLevel = 2;

// ---------- Deferred reboot -------------------------------------------------
volatile uint32_t rebootAt      = 0;
volatile bool     rebootPending = false;

// ---------- PWM/LEDC fade state ---------------------------------------------
struct FadeState {
    bool     active;
    uint8_t  channel;
    uint32_t startDuty;
    uint32_t targetDuty;
    uint32_t startMs;
    uint32_t durationMs;
};
static FadeState s_fades[8] = {};  // up to 8 simultaneous fades

// ---------------------------------------------------------------------------
// I2C device lookup table
// ---------------------------------------------------------------------------
static const Hardware::I2cDevice k_i2cTable[] = {
    // OLED displays
    { 0x3C, "SSD1306",   "OLED Display 128x64" },
    { 0x3D, "SSD1306",   "OLED Display 128x64 (alt addr)" },
    // Environmental sensors
    { 0x76, "BME280",    "Temp/Humidity/Pressure Sensor" },
    { 0x77, "BME280",    "Temp/Humidity/Pressure Sensor (alt addr)" },
    // GPIO expanders (0x20-0x27)
    { 0x20, "PCF8574",   "I2C GPIO Expander" },
    { 0x21, "PCF8574",   "I2C GPIO Expander" },
    { 0x22, "PCF8574",   "I2C GPIO Expander" },
    { 0x23, "PCF8574",   "I2C GPIO Expander" },
    { 0x24, "PCF8574",   "I2C GPIO Expander" },
    { 0x25, "PCF8574",   "I2C GPIO Expander" },
    { 0x26, "PCF8574",   "I2C GPIO Expander" },
    { 0x27, "PCF8574",   "I2C GPIO Expander" },
    // ADC
    { 0x48, "ADS1115",   "16-bit ADC" },
    { 0x49, "ADS1115",   "16-bit ADC" },
    { 0x4A, "ADS1115",   "16-bit ADC" },
    { 0x4B, "ADS1115",   "16-bit ADC" },
    // IMU
    { 0x68, "MPU6050",   "6-axis IMU" },
    { 0x69, "MPU6050",   "6-axis IMU (alt addr)" },
    // Magnetometer
    { 0x1C, "HMC5883L",  "3-axis Magnetometer" },
    { 0x1E, "HMC5883L",  "3-axis Magnetometer (alt addr)" },
    // Current/Power monitor
    { 0x40, "INA219",    "Current/Power Monitor" },
    // EEPROM (0x50-0x57)
    { 0x50, "AT24Cxx",   "EEPROM" },
    { 0x51, "AT24Cxx",   "EEPROM" },
    { 0x52, "AT24Cxx",   "EEPROM" },
    { 0x53, "AT24Cxx",   "EEPROM" },
    { 0x54, "AT24Cxx",   "EEPROM" },
    { 0x55, "AT24Cxx",   "EEPROM" },
    { 0x56, "AT24Cxx",   "EEPROM" },
    { 0x57, "AT24Cxx",   "EEPROM" },
};
static const size_t k_i2cTableLen = sizeof(k_i2cTable) / sizeof(k_i2cTable[0]);

// ---------------------------------------------------------------------------
// BLE helpers
// ---------------------------------------------------------------------------
void startBle() {
    if (bleOn) return;
    BLEDevice::init(BLE_DEVICE_NAME);
    bleServer = BLEDevice::createServer();
    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->setScanResponse(true);
    BLEDevice::startAdvertising();
    bleOn = true;
    Serial.println("[BLE] advertising started");
}

void stopBle() {
    if (!bleOn) return;
    BLEDevice::getAdvertising()->stop();
    bleServer = nullptr;
    // deinit(false): keep controller memory so BLE can be started again later.
    BLEDevice::deinit(false);
    bleOn = false;
    Serial.println("[BLE] stopped");
}

// ---------------------------------------------------------------------------
// I2C scan (called from service() when a scan is requested)
// ---------------------------------------------------------------------------
void doI2cScan() {
    if (!s_i2cMutex) return;
    if (xSemaphoreTake(s_i2cMutex, portMAX_DELAY) != pdTRUE) return;

    i2cCount = 0;
    Wire.begin(I2C_SDA, I2C_SCL);
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0 && i2cCount < MAX_I2C_FOUND) {
            i2cFound[i2cCount++] = addr;
        }
    }
    Wire.end();

    xSemaphoreGive(s_i2cMutex);
    Serial.printf("[I2C] scan done, %u device(s)\n", i2cCount);
}

// ---------------------------------------------------------------------------
// PWM fade tick (called from service())
// ---------------------------------------------------------------------------
void tickFades() {
    uint32_t now = millis();
    for (size_t i = 0; i < 8; i++) {
        FadeState& f = s_fades[i];
        if (!f.active) continue;
        uint32_t elapsed = now - f.startMs;
        if (elapsed >= f.durationMs) {
            ledcWrite(f.channel, f.targetDuty);
            f.active = false;
        } else {
            // Linear interpolation
            int32_t delta = (int32_t)f.targetDuty - (int32_t)f.startDuty;
            uint32_t duty = (uint32_t)((int32_t)f.startDuty +
                            (int64_t)delta * elapsed / (int32_t)f.durationMs);
            ledcWrite(f.channel, duty);
        }
    }
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Hardware namespace — public API
// ---------------------------------------------------------------------------
namespace Hardware {

// ---------- lifecycle -------------------------------------------------------

bool isSafeOutputPin(uint8_t pin) {
    static const uint8_t ok[] = {2, 4, 5, 13, 14, 15, 16, 17, 18, 19,
                                  23, 25, 26, 27, 32, 33};
    for (size_t i = 0; i < sizeof(ok); i++) if (ok[i] == pin) return true;
    return false;
}

void begin() {
    // Create I2C mutex first so doI2cScan is safe.
    s_i2cMutex = xSemaphoreCreateMutex();

    for (size_t i = 0; i < OUTPUT_COUNT && i < 8; i++) {
        const OutputPin& o = OUTPUTS[i];
        if (!isSafeOutputPin(o.pin)) {
            outBlocked[i] = true;
            Serial.printf("[GPIO] BLOCKED unsafe output pin %u (%s)\n", o.pin, o.name);
            continue;
        }
        // Set the level BEFORE switching to output to avoid a glitch.
        digitalWrite(o.pin, (o.initialOn == o.activeHigh) ? HIGH : LOW);
        pinMode(o.pin, OUTPUT);
        digitalWrite(o.pin, (o.initialOn == o.activeHigh) ? HIGH : LOW);
        Serial.printf("[GPIO] output %-12s GPIO%-2u ready\n", o.name, o.pin);
    }
    for (size_t i = 0; i < INPUT_COUNT; i++) {
        pinMode(INPUTS[i].pin, INPUTS[i].pullup ? INPUT_PULLUP : INPUT);
        Serial.printf("[GPIO] input  %-12s GPIO%-2u ready\n", INPUTS[i].name, INPUTS[i].pin);
    }
}

void service() {
    // ---- BLE ----
    int8_t br = bleRequest;
    if (br >= 0) {
        bleRequest = -1;
        if (br == 1) startBle(); else stopBle();
    }

    // ---- I2C scan ----
    if (i2cRequest) {
        i2cRequest = false;
        i2cSt = I2C_SCANNING;
        doI2cScan();
        i2cSt = I2C_DONE;
    }

    // ---- PWM fades ----
    tickFades();

    // ---- Deferred reboot ----
    if (rebootPending && (int32_t)(millis() - rebootAt) >= 0) {
        Serial.println("[SYS] rebooting");
        delay(50);
        ESP.restart();
    }
}

// ---------- GPIO outputs ----------------------------------------------------

bool outputBlocked(size_t i)  { return i < 8 ? outBlocked[i] : true; }

bool outputLevelHigh(size_t i) {
    if (i >= OUTPUT_COUNT || outBlocked[i]) return false;
    return digitalRead(OUTPUTS[i].pin) == HIGH;
}

bool outputIsOn(size_t i) {
    if (i >= OUTPUT_COUNT || outBlocked[i]) return false;
    return outputLevelHigh(i) == OUTPUTS[i].activeHigh;
}

bool setOutput(size_t i, bool on) {
    if (i >= OUTPUT_COUNT || outBlocked[i]) return false;
    digitalWrite(OUTPUTS[i].pin, (on == OUTPUTS[i].activeHigh) ? HIGH : LOW);
    Serial.printf("[GPIO] %s -> %s\n", OUTPUTS[i].name, on ? "ON" : "OFF");
    return true;
}

// ---------- GPIO inputs -----------------------------------------------------

bool inputActive(size_t i) {
    if (i >= INPUT_COUNT) return false;
    bool high = digitalRead(INPUTS[i].pin) == HIGH;
    return INPUTS[i].activeLow ? !high : high;
}

// ---------- BLE -------------------------------------------------------------

void    requestBle(bool enable) { bleRequest = enable ? 1 : 0; }
bool    bleAdvertising()        { return bleOn && bleRequest != 0; }
uint8_t bleConnections() {
    if (!bleOn || !bleServer) return 0;
    return (uint8_t)bleServer->getConnectedCount();
}

// ---------- Wi-Fi radio power -----------------------------------------------

bool setTxPowerLevel(uint8_t level) {
    wifi_power_t p;
    switch (level) {
        case 0:  p = WIFI_POWER_8_5dBm;  break;
        case 1:  p = WIFI_POWER_13dBm;   break;
        case 2:  p = WIFI_POWER_19_5dBm; break;
        default: return false;
    }
    if (!WiFi.setTxPower(p)) return false;
    txLevel = level;
    Serial.printf("[WIFI] TX power level %u\n", level);
    return true;
}
uint8_t     txPowerLevel() { return txLevel; }
const char* txPowerName()  {
    static const char* names[] = {"LOW", "MED", "HIGH"};
    return names[txLevel > 2 ? 2 : txLevel];
}

// ---------- I2C scan --------------------------------------------------------

void requestI2cScan() {
    if (i2cSt != I2C_SCANNING) {
        i2cSt = I2C_SCANNING;
        i2cRequest = true;
    }
}
I2cState i2cState()               { return i2cSt; }
uint8_t  i2cFoundCount()           { return i2cCount; }
uint8_t  i2cFoundAddr(uint8_t idx) { return idx < i2cCount ? i2cFound[idx] : 0; }

// ---------- I2C device identification ---------------------------------------

const I2cDevice* identifyI2cDevice(uint8_t addr) {
    for (size_t i = 0; i < k_i2cTableLen; i++) {
        if (k_i2cTable[i].addr == addr) return &k_i2cTable[i];
    }
    return nullptr;
}

uint8_t i2cDeviceName(uint8_t idx, char* buf, size_t len) {
    if (idx >= i2cCount || !buf || len == 0) return 0;
    uint8_t addr = i2cFound[idx];
    const I2cDevice* dev = identifyI2cDevice(addr);
    const char* name = dev ? dev->name : "Unknown";
    size_t n = strnlen(name, len - 1);
    memcpy(buf, name, n);
    buf[n] = '\0';
    return (uint8_t)n;
}

// ---------- PWM / LEDC ------------------------------------------------------

bool pwmAttach(uint8_t pin, uint8_t channel, uint32_t freqHz, uint8_t resolution) {
    if (!ledcSetup(channel, freqHz, resolution)) return false;
    ledcAttachPin(pin, channel);
    Serial.printf("[PWM] ch%u pin%u freq%uHz res%ubit\n",
                  channel, pin, freqHz, resolution);
    return true;
}

void pwmSetDuty(uint8_t channel, uint32_t duty) {
    ledcWrite(channel, duty);
}

void pwmFade(uint8_t channel, uint32_t targetDuty, uint32_t durationMs) {
    // Find a free fade slot (or reuse same channel).
    for (size_t i = 0; i < 8; i++) {
        FadeState& f = s_fades[i];
        if (!f.active || f.channel == channel) {
            f.active      = true;
            f.channel     = channel;
            f.startMs     = millis();
            f.durationMs  = durationMs;
            f.startDuty   = ledcRead(channel);
            f.targetDuty  = targetDuty;
            return;
        }
    }
    // No free slot — just set immediately.
    ledcWrite(channel, targetDuty);
}

// ---------- System telemetry ------------------------------------------------

void getTelemetry(SysTelemetry& t) {
    t.uptimeSec    = millis() / 1000UL;
    t.heapFree     = esp_get_free_heap_size();
    t.heapMin      = esp_get_minimum_free_heap_size();
    t.heapMaxBlock = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    t.heapTotal    = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
    t.heapFragPct  = (t.heapFree > 0)
                   ? (1.0f - (float)t.heapMaxBlock / (float)t.heapFree) * 100.0f
                   : 0.0f;
    t.tempC        = chipTemperatureC();
    t.cpuMHz       = (uint32_t)ESP.getCpuFreqMHz();
    t.resetReason  = (uint8_t)esp_reset_reason();
    t.loopWatermark = s_telTask
                    ? (uint16_t)(uxTaskGetStackHighWaterMark(s_telTask) * sizeof(StackType_t))
                    : 0;
    t.netWatermark  = s_netTask
                    ? (uint16_t)(uxTaskGetStackHighWaterMark(s_netTask) * sizeof(StackType_t))
                    : 0;
    t.rssi         = 0;
    t.apClients    = (uint8_t)WiFi.softAPgetStationNum();
}

// ---------- I2C mutex -------------------------------------------------------

SemaphoreHandle_t i2cMutex() { return s_i2cMutex; }

// ---------- misc ------------------------------------------------------------

void requestReboot(uint32_t delayMs) {
    rebootAt      = millis() + delayMs;
    rebootPending = true;
}

float chipTemperatureC() {
    float t = temperatureRead();
    if (isnan(t) || t < -40 || t > 150) return NAN;
    return t;
}

}  // namespace Hardware
