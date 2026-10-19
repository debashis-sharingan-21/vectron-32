#include "storage.h"
#include "config.h"

#include <Preferences.h>
#include <esp_random.h>
#include <mbedtls/sha256.h>
#include <string.h>

// ---------------------------------------------------------------------------
// RTC RAM — survives deep sleep, reset on power cycle.
// ---------------------------------------------------------------------------
RTC_DATA_ATTR static uint32_t rtc_fail_count = 0;
RTC_DATA_ATTR static uint32_t rtc_lock_until  = 0;

// ---------------------------------------------------------------------------
// File-scope state
// ---------------------------------------------------------------------------
static Preferences prefs;
static bool        s_begun      = false;
static bool        s_firstBoot  = false;

// Cached hash string (64 hex chars + NUL).
static char        s_hashCache[65] = {0};

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------
namespace {

/// Convert a byte array to a lowercase hex string.
/// @param dst   Output buffer; must be at least 2*len+1 bytes.
void bytesToHex(const uint8_t* src, size_t len, char* dst) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        dst[i * 2]     = hex[src[i] >> 4];
        dst[i * 2 + 1] = hex[src[i] & 0x0F];
    }
    dst[len * 2] = '\0';
}

/// Convert a lowercase hex string back to bytes.
/// @return true on success, false if hexStr length is odd or contains invalid chars.
bool hexToBytes(const char* hexStr, uint8_t* dst, size_t dstLen) {
    size_t hexLen = strlen(hexStr);
    if (hexLen % 2 != 0 || hexLen / 2 > dstLen) return false;
    for (size_t i = 0; i < hexLen / 2; i++) {
        auto hexNibble = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int hi = hexNibble(hexStr[i * 2]);
        int lo = hexNibble(hexStr[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        dst[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

/// Compute SHA-256 of (salt || plaintext) and write 32 raw bytes to `out`.
void computeHash(const uint8_t* salt, size_t saltLen,
                 const char* plaintext, size_t plainLen,
                 uint8_t out[32]) {
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0 /* SHA-256, not SHA-224 */);
    mbedtls_sha256_update(&ctx, salt, saltLen);
    mbedtls_sha256_update(&ctx, reinterpret_cast<const uint8_t*>(plaintext), plainLen);
    mbedtls_sha256_finish(&ctx, out);
    mbedtls_sha256_free(&ctx);
}

/// Constant-time comparison of two byte buffers.
bool constTimeEqual(const uint8_t* a, const uint8_t* b, size_t len) {
    uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) diff |= a[i] ^ b[i];
    return diff == 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// Storage namespace implementation
// ---------------------------------------------------------------------------
namespace Storage {

bool begin() {
    if (s_begun) return true;
    bool ok = prefs.begin("vectron", false /* read-write */);
    if (!ok) {
        Serial.println("[NVS] ERROR: failed to open 'vectron' namespace");
        return false;
    }
    s_firstBoot = !prefs.isKey("ap_ssid");
    s_begun = true;
    Serial.printf("[NVS] opened, first-boot=%s\n", s_firstBoot ? "yes" : "no");
    return true;
}

bool isFirstBoot() {
    return s_firstBoot;
}

bool hasCredentials() {
    if (!s_begun) return false;
    return prefs.isKey("ap_ssid") && prefs.isKey("pw_hash");
}

void setCredentials(const String& ssid,
                    const String& pass,
                    const String& user,
                    const String& passHash,
                    const uint8_t* salt,
                    size_t         saltLen) {
    if (!s_begun) return;

    // Convert salt to hex for storage.
    // Max salt size we support: 32 bytes → 64 hex chars + NUL.
    char saltHex[65] = {0};
    if (saltLen > 32) saltLen = 32;
    bytesToHex(salt, saltLen, saltHex);

    prefs.putString("ap_ssid",  ssid);
    prefs.putString("ap_pass",  pass);
    prefs.putString("username", user);
    prefs.putString("pw_hash",  passHash);
    prefs.putString("pw_salt",  saltHex);

    // Cache the hash so getPasswordHash() can return a stable pointer.
    passHash.toCharArray(s_hashCache, sizeof(s_hashCache));

    s_firstBoot = false;
    Serial.println("[NVS] credentials saved");
}

bool setCredentials(const char* ssid,
                    const char* apPass,
                    const char* user,
                    const char* plainAdminPass) {
    if (!s_begun) return false;
    if (!ssid || !apPass || !user || !plainAdminPass) return false;

    uint8_t salt[16];
    esp_fill_random(salt, sizeof(salt));

    uint8_t hash[32];
    computeHash(salt, sizeof(salt), plainAdminPass, strlen(plainAdminPass), hash);

    char hashHex[65] = {0};
    bytesToHex(hash, sizeof(hash), hashHex);

    char saltHex[33] = {0};
    bytesToHex(salt, sizeof(salt), saltHex);

    prefs.putString("ap_ssid",  ssid);
    prefs.putString("ap_pass",  apPass);
    prefs.putString("username", user);
    prefs.putString("pw_hash",  hashHex);
    prefs.putString("pw_salt",  saltHex);

    strncpy(s_hashCache, hashHex, sizeof(s_hashCache) - 1);
    s_hashCache[sizeof(s_hashCache) - 1] = '\0';

    s_firstBoot = false;
    Serial.println("[NVS] credentials saved with 16-byte random salt");
    return true;
}

bool setCredentials(const String& ssid,
                    const String& apPass,
                    const String& user,
                    const String& adminPass) {
    return setCredentials(ssid.c_str(), apPass.c_str(), user.c_str(), adminPass.c_str());
}

bool checkPassword(const String& plaintext) {
    if (!s_begun || !hasCredentials()) return false;

    // Retrieve stored salt (hex).
    String saltHex = prefs.getString("pw_salt", "");
    if (saltHex.length() == 0 || saltHex.length() % 2 != 0) {
        Serial.println("[NVS] checkPassword: no salt stored");
        return false;
    }

    // Retrieve stored hash (hex, 64 chars = 32 bytes).
    String hashHex = prefs.getString("pw_hash", "");
    if (hashHex.length() != 64) {
        Serial.println("[NVS] checkPassword: invalid hash length");
        return false;
    }

    // Decode stored salt.
    uint8_t saltBytes[32] = {0};
    size_t  saltLen = saltHex.length() / 2;
    if (!hexToBytes(saltHex.c_str(), saltBytes, sizeof(saltBytes))) {
        Serial.println("[NVS] checkPassword: bad salt hex");
        return false;
    }

    // Decode stored hash.
    uint8_t storedHash[32] = {0};
    if (!hexToBytes(hashHex.c_str(), storedHash, sizeof(storedHash))) {
        Serial.println("[NVS] checkPassword: bad hash hex");
        return false;
    }

    // Compute SHA-256(salt || plaintext).
    uint8_t computed[32] = {0};
    computeHash(saltBytes, saltLen,
                plaintext.c_str(), plaintext.length(),
                computed);

    return constTimeEqual(computed, storedHash, 32);
}

String getApSsid() {
    if (!s_begun) return String(AP_DEFAULT_SSID);
    return prefs.getString("ap_ssid", AP_DEFAULT_SSID);
}

String getApPassword() {
    if (!s_begun) return String("");
    return prefs.getString("ap_pass", "");
}

String getUsername() {
    if (!s_begun) return String("");
    return prefs.getString("username", "");
}

const char* getPasswordHash() {
    if (!s_begun) return "";
    // Populate cache if empty.
    if (s_hashCache[0] == '\0') {
        String h = prefs.getString("pw_hash", "");
        h.toCharArray(s_hashCache, sizeof(s_hashCache));
    }
    return s_hashCache;
}

void getRtcStats(uint32_t& failCount, uint32_t& lockUntil) {
    failCount = rtc_fail_count;
    lockUntil = rtc_lock_until;
}

void setRtcStats(uint32_t failCount, uint32_t lockUntil) {
    rtc_fail_count = failCount;
    rtc_lock_until = lockUntil;
}

void clearRtcStats() {
    rtc_fail_count = 0;
    rtc_lock_until = 0;
}

}  // namespace Storage
