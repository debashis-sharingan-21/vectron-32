#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

// ---------------------------------------------------------------------------
// Storage — NVS-backed credential store + RTC stats.
//
// Call Storage::begin() once in setup() before reading any credential.
// On first boot (no credentials in NVS) the caller is responsible for
// running the provisioning flow (captive portal, BLE, etc.).
// ---------------------------------------------------------------------------
namespace Storage {

// ---------- lifecycle -------------------------------------------------------

/// Mount NVS ("vectron" namespace).  Returns true on success.
bool begin();

/// True if no credentials have ever been written to NVS.
bool isFirstBoot();

/// True if AP SSID + hashed password are present in NVS.
bool hasCredentials();

// ---------- credential management -------------------------------------------

/// Persist all credentials.
/// @param ssid        Wi-Fi AP SSID to advertise.
/// @param pass        Wi-Fi AP password (plaintext, stored as-is for AP use).
/// @param user        Login username for the web UI.
/// @param passHash    SHA-256 hex string of (salt || plainPassword).
/// @param salt        Raw salt bytes used when computing passHash.
/// @param saltLen     Number of bytes in salt (recommended: 16).
/// Low-level: caller supplies pre-computed hash + raw salt bytes.
void setCredentials(const String& ssid,
                    const String& pass,
                    const String& user,
                    const String& passHash,
                    const uint8_t* salt,
                    size_t         saltLen);

/// Convenience overload: generates a 16-byte random salt using esp_fill_random,
/// computes SHA-256(salt || plainAdminPass), and stores everything in NVS.
/// Returns true on success.
bool setCredentials(const char* ssid,
                    const char* apPass,
                    const char* user,
                    const char* plainAdminPass);

/// String overload for convenience
bool setCredentials(const String& ssid,
                    const String& apPass,
                    const String& user,
                    const String& adminPass);

/// Compute SHA-256(storedSalt || plaintext) and compare with stored hash.
/// Comparison is constant-time to resist timing attacks.
/// Returns true when the password matches.
bool checkPassword(const String& plaintext);

// ---------- getters ---------------------------------------------------------

String getApSsid();        ///< Stored AP SSID, or AP_DEFAULT_SSID if none.
String getApPassword();    ///< Stored AP password, or "" if none.
String getUsername();      ///< Stored username, or "" if none.

/// Stored password hash as a lowercase hex string (64 chars), or "" if none.
const char* getPasswordHash();

// ---------- RTC statistics (survive deep-sleep, cleared by power cycle) -----

/// Read fail / lock counters from RTC RAM.
void getRtcStats(uint32_t& failCount, uint32_t& lockUntil);

/// Write fail / lock counters to RTC RAM.
void setRtcStats(uint32_t failCount, uint32_t lockUntil);

/// Reset both counters to zero.
void clearRtcStats();

}  // namespace Storage
