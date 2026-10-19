#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// ---------------------------------------------------------------------------
// Hardware — GPIO, BLE, I2C, Wi-Fi radio power, PWM/LEDC, system telemetry.
// All direct hardware access lives here.
// ---------------------------------------------------------------------------
namespace Hardware {

// ---------- lifecycle -------------------------------------------------------
void begin();
void service();   ///< Call from the telemetry task (or loop() in non-RTOS builds).

// ---------- GPIO outputs ----------------------------------------------------
bool isSafeOutputPin(uint8_t pin);
bool outputBlocked(size_t i);
bool outputLevelHigh(size_t i);   ///< Real pin level read back from the chip.
bool outputIsOn(size_t i);        ///< Level interpreted with activeHigh.
bool setOutput(size_t i, bool on);

// ---------- GPIO inputs -----------------------------------------------------
bool inputActive(size_t i);

// ---------- Bluetooth Low Energy --------------------------------------------
void    requestBle(bool enable);  ///< Applied from service().
bool    bleAdvertising();
uint8_t bleConnections();

// ---------- Wi-Fi radio power -----------------------------------------------
bool        setTxPowerLevel(uint8_t level);  ///< 0=LOW 1=MED 2=HIGH
uint8_t     txPowerLevel();
const char* txPowerName();

// ---------- I2C scan --------------------------------------------------------
enum I2cState : uint8_t { I2C_IDLE = 0, I2C_SCANNING = 1, I2C_DONE = 2 };
void      requestI2cScan();
I2cState  i2cState();
uint8_t   i2cFoundCount();
uint8_t   i2cFoundAddr(uint8_t idx);

// ---------- I2C device identification ---------------------------------------
struct I2cDevice {
    uint8_t     addr;
    const char* name;   ///< Short chip part-number, e.g. "SSD1306"
    const char* desc;   ///< Human-readable description
};

/// Look up a well-known I2C address.
/// @return Pointer to a static I2cDevice, or nullptr if unknown.
const I2cDevice* identifyI2cDevice(uint8_t addr);

/// Write the name of the i2c device found at scan index `idx` into `buf`.
/// @return Number of characters written (not counting NUL), 0 if idx out of range.
uint8_t i2cDeviceName(uint8_t idx, char* buf, size_t len);

// ---------- PWM / LEDC ------------------------------------------------------
/// Attach a LEDC channel to a pin with the given frequency and resolution.
/// @param resolution  Bit depth (1-16). Use 8 for 0-255 duty.
bool pwmAttach(uint8_t pin, uint8_t channel, uint32_t freqHz, uint8_t resolution);

/// Set the raw duty value for an attached LEDC channel.
void pwmSetDuty(uint8_t channel, uint32_t duty);

/// Smoothly fade a LEDC channel to `targetDuty` over `durationMs` milliseconds.
void pwmFade(uint8_t channel, uint32_t targetDuty, uint32_t durationMs);

// ---------- System telemetry ------------------------------------------------
struct SysTelemetry {
    uint32_t uptimeSec;       ///< Seconds since boot.
    uint32_t heapFree;        ///< Current free heap bytes.
    uint32_t heapMin;         ///< All-time minimum free heap bytes.
    uint32_t heapMaxBlock;    ///< Largest contiguous free block.
    uint32_t heapTotal;       ///< Total heap size.
    float    heapFragPct;     ///< Fragmentation: (1 - maxBlock/freeHeap) * 100.
    float    tempC;           ///< Chip temperature in °C (NAN if unavailable).
    uint32_t cpuMHz;          ///< CPU frequency in MHz.
    uint8_t  resetReason;     ///< esp_reset_reason() cast to uint8_t.
    uint16_t loopWatermark;   ///< Stack high-watermark of the telemetry task (words).
    uint16_t netWatermark;    ///< Stack high-watermark of the network task (words).
    int8_t   rssi;            ///< Wi-Fi RSSI (0 in AP mode).
    uint8_t  apClients;       ///< Connected station count.
};

/// Fill `t` with current telemetry values.
void getTelemetry(SysTelemetry& t);

// ---------- I2C mutex (shared with telemetry task) --------------------------
/// Returns the FreeRTOS mutex guarding the I2C bus.
/// Created in begin(); callers must take/give it around Wire operations.
SemaphoreHandle_t i2cMutex();

// ---------- misc ------------------------------------------------------------
void  requestReboot(uint32_t delayMs);
float chipTemperatureC();   ///< NAN if unavailable.

}  // namespace Hardware

extern "C" {
void Hardware_setTelemetryTaskHandle(TaskHandle_t h);
void Hardware_setNetworkTaskHandle(TaskHandle_t h);
}
