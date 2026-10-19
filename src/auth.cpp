#include "auth.h"
#include "config.h"
#include "storage.h"
#include <esp_system.h>

// MAX_SESSIONS comes from config.h (default 3). Hard cap enforced below.
static_assert(MAX_SESSIONS <= 8, "MAX_SESSIONS must be <= 8");

// ---------------------------------------------------------------------------
// Internal types
// ---------------------------------------------------------------------------
namespace {

const char*  COOKIE_NAME = "VECTRON_SID";
const size_t TOKEN_LEN   = 32;   // 128-bit entropy, hex encoded

struct Session {
  bool     used;
  char     token[TOKEN_LEN + 1];
  uint32_t lastSeen;   // millis()
};

struct Attempt {
  bool     used;
  uint32_t ip;
  uint8_t  fails;     // bad tries in the current window
  uint8_t  strikes;   // completed lockout periods (drives exponential back-off)
  bool     locked;
  uint32_t lockUntil; // millis() after which lock expires
  uint32_t touched;   // millis() of last interaction
};

const uint8_t ATTEMPT_SLOTS = 6;

Session sessions[8];              // indices 0..MAX_SESSIONS-1 are active
Attempt attempts[ATTEMPT_SLOTS];

// ---------------------------------------------------------------------------
// RTC stats are global (not per-IP) as per Storage contract.
// We load them once at begin() and track whether we already loaded them so
// we only seed the first login attempt, not every attempt.
static bool s_rtcLoaded = false;

// ---------------------------------------------------------------------------
// Constant-time string compare (prevent timing oracle on passwords).
// Compares up to 64 bytes; anything longer is immediately rejected.
// ---------------------------------------------------------------------------
bool ctEqual(const String& a, const char* b) {
  const size_t lb = strlen(b);
  if (a.length() > 64) return false;
  uint8_t diff = (a.length() != lb) ? 1 : 0;
  for (size_t i = 0; i < 64; i++) {
    uint8_t x = (i < a.length()) ? (uint8_t)a[i] : 0;
    uint8_t y = (i < lb)         ? (uint8_t)b[i]  : 0;
    diff |= (x ^ y);
  }
  return diff == 0;
}

// ---------------------------------------------------------------------------
// Attempt-slot management
// ---------------------------------------------------------------------------
Attempt* findAttempt(uint32_t ip) {
  Attempt* oldest = &attempts[0];
  for (uint8_t i = 0; i < ATTEMPT_SLOTS; i++) {
    if (attempts[i].used && attempts[i].ip == ip) return &attempts[i];
    if (!attempts[i].used) { oldest = &attempts[i]; break; }
    if ((int32_t)(oldest->touched - attempts[i].touched) > 0) oldest = &attempts[i];
  }
  memset(oldest, 0, sizeof(Attempt));
  oldest->used = true;
  oldest->ip   = ip;
  return oldest;
}

// ---------------------------------------------------------------------------
// Token generation using hardware RNG (true random while Wi-Fi is active).
// ---------------------------------------------------------------------------
String newToken() {
  static const char* hex = "0123456789abcdef";
  char buf[TOKEN_LEN + 1];
  for (size_t i = 0; i < TOKEN_LEN; i += 8) {
    uint32_t r = esp_random();
    for (int n = 0; n < 8; n++) { buf[i + n] = hex[r & 0xF]; r >>= 4; }
  }
  buf[TOKEN_LEN] = '\0';
  return String(buf);
}

// ---------------------------------------------------------------------------
// Session-slot management
// ---------------------------------------------------------------------------
String createSession() {
  uint32_t now  = millis();
  Session* slot = nullptr;

  // Prefer an unused slot or an idle-expired one.
  for (uint8_t i = 0; i < MAX_SESSIONS; i++) {
    if (!sessions[i].used ||
        (now - sessions[i].lastSeen) > SESSION_IDLE_TIMEOUT_MS) {
      slot = &sessions[i];
      break;
    }
  }
  // Fall back to LRU eviction.
  if (!slot) {
    slot = &sessions[0];
    for (uint8_t i = 1; i < MAX_SESSIONS; i++) {
      if ((now - sessions[i].lastSeen) > (now - slot->lastSeen))
        slot = &sessions[i];
    }
  }

  String t = newToken();
  strncpy(slot->token, t.c_str(), TOKEN_LEN);
  slot->token[TOKEN_LEN] = '\0';
  slot->used     = true;
  slot->lastSeen = now;
  return t;
}

// ---------------------------------------------------------------------------
// Extract the VECTRON_SID cookie value from a Cookie header.
// Returns empty String on failure.
// ---------------------------------------------------------------------------
String tokenFromRequest(AsyncWebServerRequest* request) {
  if (!request->hasHeader("Cookie")) return String();
  String c   = request->header("Cookie");
  String key = String(COOKIE_NAME) + "=";
  int    p   = c.indexOf(key);
  if (p < 0) return String();
  // Guard against substring matches inside other cookie names.
  if (p > 0 && c[p - 1] != ' ' && c[p - 1] != ';') return String();
  p += key.length();
  String v = c.substring(p, p + TOKEN_LEN);
  if (v.length() != TOKEN_LEN) return String();
  return v;
}

// ---------------------------------------------------------------------------
// Locate a session by its token string. Returns nullptr on miss or expiry.
// ---------------------------------------------------------------------------
Session* findSession(const String& token) {
  if (token.length() != TOKEN_LEN) return nullptr;
  uint32_t now = millis();
  for (uint8_t i = 0; i < MAX_SESSIONS; i++) {
    if (!sessions[i].used) continue;
    // Expire idle sessions on access.
    if ((now - sessions[i].lastSeen) > SESSION_IDLE_TIMEOUT_MS) {
      sessions[i].used = false;
      continue;
    }
    if (strcmp(sessions[i].token, token.c_str()) == 0)
      return &sessions[i];
  }
  return nullptr;
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Public Auth namespace
// ---------------------------------------------------------------------------
namespace Auth {

// ---------------------------------------------------------------------------
void begin() {
  memset(sessions, 0, sizeof(sessions));
  memset(attempts, 0, sizeof(attempts));
  s_rtcLoaded = false;
  Serial.println("[AUTH] begin() ok");
}

// ---------------------------------------------------------------------------
// Login
// ---------------------------------------------------------------------------
Result login(uint32_t ip, const String& user, const String& pass,
             String& token, uint32_t& retrySeconds) {

  // First-boot guard: no credentials provisioned yet.
  if (Storage::isFirstBoot()) return Result::FIRST_BOOT;

  const uint32_t now = millis();
  Attempt* a = findAttempt(ip);
  a->touched  = now;

  // -----------  RTC-backed lock persistence  --------------------------------
  // Load RTC stats once per boot so a brownout/watchdog reset doesn't wipe
  // the brute-force state. Storage::getRtcStats / setRtcStats operate on a
  // single global counter (not per-IP), which is sufficient for the AP's
  // single-uplink topology.
  if (!s_rtcLoaded) {
    s_rtcLoaded = true;
    uint32_t rtcFails    = 0;
    uint32_t rtcLockUntil = 0;   // seconds since boot (RTC time)
    Storage::getRtcStats(rtcFails, rtcLockUntil);
    if (rtcFails > 0 || rtcLockUntil > 0) {
      a->fails = (rtcFails < LOGIN_MAX_FAILS) ? (uint8_t)rtcFails : LOGIN_MAX_FAILS;
      if (rtcLockUntil > 0) {
        // rtcLockUntil is in seconds-since-boot; compare to current millis.
        uint32_t lockUntilMs = rtcLockUntil * 1000UL;
        if ((int32_t)(lockUntilMs - now) > 0) {
          a->locked    = true;
          a->lockUntil = lockUntilMs;
        }
      }
    }
  }

  // -----------  lockout check  ---------------------------------------------
  if (a->locked) {
    if ((int32_t)(a->lockUntil - now) > 0) {
      retrySeconds = (a->lockUntil - now + 999) / 1000;
      return Result::LOCKED;
    }
    // Lock has expired.
    a->locked = false;
    Storage::clearRtcStats();
  }

  // -----------  credential check  ------------------------------------------
  // Evaluate both fields before branching to prevent timing oracle.
  String storedUser = Storage::getUsername();
  const bool userOk = ctEqual(user, storedUser.c_str());
  const bool passOk = Storage::checkPassword(pass);

  if (userOk && passOk) {
    a->fails   = 0;
    a->strikes = 0;
    Storage::clearRtcStats();
    token = createSession();
    return Result::OK;
  }

  // -----------  failure accounting  ----------------------------------------
  a->fails++;
  // Persist failure count to RTC storage so brownout doesn't reset it.
  Storage::setRtcStats(a->fails, 0);

  if (a->fails >= LOGIN_MAX_FAILS) {
    a->fails = 0;
    if (a->strikes < 4) a->strikes++;
    uint32_t mult = 1UL << (a->strikes - 1);  // 1, 2, 4, 8...
    if (mult > 8) mult = 8;
    a->locked    = true;
    a->lockUntil = now + LOGIN_LOCK_MS * mult;
    retrySeconds = (LOGIN_LOCK_MS * mult) / 1000;

    // Write lock-until as seconds-since-boot for RTC survival.
    uint32_t lockUntilSec = a->lockUntil / 1000;
    Storage::setRtcStats(0, lockUntilSec);

    Serial.printf("[AUTH] too many failures from %u.%u.%u.%u – locked %us\n",
                  ip & 255, (ip >> 8) & 255, (ip >> 16) & 255, (ip >> 24) & 255,
                  (unsigned)retrySeconds);
    return Result::LOCKED;
  }

  return Result::BAD;
}

// ---------------------------------------------------------------------------
uint8_t attemptsLeft(uint32_t ip) {
  Attempt* a = findAttempt(ip);
  return (a->fails < LOGIN_MAX_FAILS) ? (LOGIN_MAX_FAILS - a->fails) : 0;
}

// ---------------------------------------------------------------------------
bool check(AsyncWebServerRequest* request) {
  Session* s = findSession(tokenFromRequest(request));
  if (!s) return false;
  s->lastSeen = millis();
  return true;
}

// ---------------------------------------------------------------------------
bool checkWsToken(const String& token) {
  Session* s = findSession(token);
  if (!s) return false;
  s->lastSeen = millis();
  return true;
}

// ---------------------------------------------------------------------------
void logout(AsyncWebServerRequest* request) {
  Session* s = findSession(tokenFromRequest(request));
  if (s) { s->used = false; memset(s->token, 0, sizeof(s->token)); }
}

// ---------------------------------------------------------------------------
String cookieHeader(const String& token) {
  return String(COOKIE_NAME) + "=" + token +
         "; Path=/; HttpOnly; SameSite=Strict; Max-Age=" +
         String(SESSION_IDLE_TIMEOUT_MS / 1000);
}

String clearCookieHeader() {
  return String(COOKIE_NAME) + "=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0";
}

// ---------------------------------------------------------------------------
bool isFirstBoot() {
  return Storage::isFirstBoot();
}

}  // namespace Auth
