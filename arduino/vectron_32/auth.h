#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// Session-cookie authentication with per-IP lockout and RTC-backed brute-force
// counters. Credentials are stored in NVS via Storage (storage.h).
namespace Auth {

enum class Result { OK, BAD, LOCKED, FIRST_BOOT };

// Initialise sessions and attempt tracking. Call once before server.begin().
void begin();

// Verify credentials against NVS-stored hash. On OK, 'token' holds a new
// session token. On LOCKED, 'retrySeconds' says how long to wait.
Result login(uint32_t ip, const String& user, const String& pass,
             String& token, uint32_t& retrySeconds);

// How many bad attempts remain before the next lockout.
uint8_t attemptsLeft(uint32_t ip);

// True if the request carries a valid, non-expired VECTRON_SID cookie.
// A successful check refreshes the idle timer.
bool check(AsyncWebServerRequest* request);

// For WebSocket upgrade: look up raw token string in session table.
bool checkWsToken(const String& token);

// Invalidate the session carried by this request (if any).
void logout(AsyncWebServerRequest* request);

// Values for Set-Cookie headers.
String cookieHeader(const String& token);
String clearCookieHeader();

// True when no credentials have been provisioned yet (delegates to Storage).
bool isFirstBoot();

}  // namespace Auth
