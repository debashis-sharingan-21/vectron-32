#include "api.h"
#include "auth.h"
#include "hardware.h"
#include "storage.h"
#include "config.h"
#include <WiFi.h>
#include <LittleFS.h>
#include <esp_wifi.h>
#include <esp_timer.h>
#include <DNSServer.h>

// ---------------------------------------------------------------------------
// Module-level state
// ---------------------------------------------------------------------------
namespace {

static AsyncWebSocket  ws("/ws");
static AsyncWebSocket* s_ws           = &ws;
static DNSServer       dnsServer;
static uint32_t        s_lastBroadcast = 0;

// ============================================================ helpers ======

String jsonEscape(const String& in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if      (c == '"')  { out += "\\\""; }
    else if (c == '\\') { out += "\\\\"; }
    else if ((uint8_t)c < 0x20) { out += ' '; }
    else { out += c; }
  }
  return out;
}

void sendJson(AsyncWebServerRequest* r, int code, const String& body) {
  AsyncWebServerResponse* resp = r->beginResponse(code, "application/json", body);
  resp->addHeader("Cache-Control", "no-store");
  r->send(resp);
}

void sendErr(AsyncWebServerRequest* r, int code, const char* msg) {
  sendJson(r, code, String("{\"ok\":false,\"error\":\"") + msg + "\"}");
}

bool sameOrigin(AsyncWebServerRequest* r) {
  if (!r->hasHeader("Origin")) return true;  // same-origin fetches may omit it
  String o = r->header("Origin");
  int i = o.indexOf("://");
  if (i >= 0) o = o.substring(i + 3);
  return o == r->header("Host");
}

bool getParam(AsyncWebServerRequest* r, const char* key, String& out) {
  if (!r->hasParam(key, true)) return false;
  out = r->getParam(key, true)->value();
  return true;
}

// Guards — each returns false after sending the error response.
bool requireAuth(AsyncWebServerRequest* r) {
  if (Auth::check(r)) return true;
  sendErr(r, 401, "UNAUTHORIZED");
  return false;
}
bool requireAuthPost(AsyncWebServerRequest* r) {
  if (!sameOrigin(r)) { sendErr(r, 403, "BAD_ORIGIN"); return false; }
  return requireAuth(r);
}

String macToString(const uint8_t* m) {
  char b[18];
  snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X",
           m[0], m[1], m[2], m[3], m[4], m[5]);
  return String(b);
}

// =========================================================== builders ======

String buildInfo() {
  String j;
  j.reserve(512);
  j += "{\"ok\":true";
  j += ",\"device\":\"" FW_NAME "\"";
  j += ",\"firmware\":\"" FW_VERSION "\"";
  j += ",\"chip\":\""    + jsonEscape(ESP.getChipModel()) + "\"";
  j += ",\"revision\":"  + String(ESP.getChipRevision());
  j += ",\"cores\":"     + String(ESP.getChipCores());
  j += ",\"cpuMHz\":"    + String(ESP.getCpuFreqMHz());
  j += ",\"flashBytes\":" + String(ESP.getFlashChipSize());
  j += ",\"sketchBytes\":" + String(ESP.getSketchSize());
  j += ",\"sdk\":\""     + jsonEscape(ESP.getSdkVersion()) + "\"";
  j += ",\"mac\":\""     + WiFi.softAPmacAddress() + "\"";
  j += ",\"fsUsed\":"    + String((unsigned)LittleFS.usedBytes());
  j += ",\"fsTotal\":"   + String((unsigned)LittleFS.totalBytes());
  j += ",\"outputs\":"   + String((unsigned)OUTPUT_COUNT);
  j += ",\"inputs\":"    + String((unsigned)INPUT_COUNT);
  j += "}";
  return j;
}

// Legacy status blob — kept for backward compat with /api/status.
String buildStatus() {
  String j;
  j.reserve(1400);
  j += "{\"ok\":true";
  j += ",\"uptime\":"  + String((unsigned long)(esp_timer_get_time() / 1000000ULL));
  j += ",\"heap\":{\"free\":"    + String(ESP.getFreeHeap()) +
       ",\"min\":"               + String(ESP.getMinFreeHeap()) +
       ",\"maxBlock\":"          + String(ESP.getMaxAllocHeap()) + "}";

  float t = Hardware::chipTemperatureC();
  j += ",\"tempC\":"  + (isnan(t) ? String("null") : String(t, 1));

  // Wi-Fi access point
  j += ",\"wifi\":{\"mode\":\"AP\"";
  j += ",\"ssid\":\""     + jsonEscape(WiFi.softAPSSID()) + "\"";
  j += ",\"ip\":\""       + WiFi.softAPIP().toString() + "\"";
  j += ",\"mac\":\""      + WiFi.softAPmacAddress() + "\"";
  j += ",\"channel\":"    + String(WiFi.channel());
  j += ",\"clients\":"    + String(WiFi.softAPgetStationNum());
  j += ",\"maxClients\":" + String(VECTRON_AP_MAX_CLIENTS);
  j += ",\"txPower\":\""  + String(Hardware::txPowerName()) + "\"";
  j += ",\"txLevel\":"    + String(Hardware::txPowerLevel());
  j += ",\"stations\":[";
  wifi_sta_list_t list;
  if (esp_wifi_ap_get_sta_list(&list) == ESP_OK) {
    for (int i = 0; i < list.num; i++) {
      if (i) j += ",";
      j += "{\"mac\":\"" + macToString(list.sta[i].mac) +
           "\",\"rssi\":" + String(list.sta[i].rssi) + "}";
    }
  }
  j += "]}";

  // Bluetooth
  j += ",\"bluetooth\":{";
  j += "\"classic\":{\"hardware\":true,\"firmware\":false}";
  j += ",\"ble\":{\"hardware\":true,\"firmware\":true";
  j += ",\"advertising\":" + String(Hardware::bleAdvertising() ? "true" : "false");
  j += ",\"name\":\"" BLE_DEVICE_NAME "\"";
  j += ",\"connections\":" + String(Hardware::bleConnections()) + "}}";

  // GPIO outputs
  j += ",\"gpio\":{\"outputs\":[";
  for (size_t i = 0; i < OUTPUT_COUNT; i++) {
    if (i) j += ",";
    j += "{\"id\":"        + String((unsigned)i);
    j += ",\"name\":\""    + jsonEscape(OUTPUTS[i].name) + "\"";
    j += ",\"pin\":"       + String(OUTPUTS[i].pin);
    j += ",\"activeHigh\":" + String(OUTPUTS[i].activeHigh ? "true" : "false");
    j += ",\"blocked\":"   + String(Hardware::outputBlocked(i) ? "true" : "false");
    j += ",\"on\":"        + String(Hardware::outputIsOn(i) ? "true" : "false");
    j += ",\"level\":"     + String(Hardware::outputLevelHigh(i) ? 1 : 0) + "}";
  }
  j += "],\"inputs\":[";
  for (size_t i = 0; i < INPUT_COUNT; i++) {
    if (i) j += ",";
    j += "{\"name\":\""  + jsonEscape(INPUTS[i].name) + "\"";
    j += ",\"pin\":"     + String(INPUTS[i].pin);
    j += ",\"active\":"  + String(Hardware::inputActive(i) ? "true" : "false") + "}";
  }
  j += "]}";

  // I2C
  const char* st = Hardware::i2cState() == Hardware::I2C_SCANNING ? "scanning"
                 : Hardware::i2cState() == Hardware::I2C_DONE      ? "done"
                 : "idle";
  j += ",\"i2c\":{\"state\":\"" + String(st) +
       "\",\"sda\":"             + String(I2C_SDA) +
       ",\"scl\":"               + String(I2C_SCL) +
       ",\"found\":[";
  for (uint8_t i = 0; i < Hardware::i2cFoundCount(); i++) {
    if (i) j += ",";
    j += String(Hardware::i2cFoundAddr(i));
  }
  j += "]}";

  j += "}";
  return j;
}

// 10 Hz telemetry frame — compact JSON sent over WebSocket.
String buildTelemetryFrame() {
  Hardware::SysTelemetry t;
  Hardware::getTelemetry(t);

  String j;
  j.reserve(512);
  j += "{\"t\":\"telem\"";
  j += ",\"up\":"       + String(t.uptimeSec);
  j += ",\"heap\":"     + String(t.heapFree);
  j += ",\"heapMin\":"  + String(t.heapMin);

  // Heap fragmentation as a percentage: (1 - maxBlock/free) * 100
  float frag = 0.0f;
  if (t.heapFree > 0 && t.heapMaxBlock <= t.heapFree) {
    frag = (1.0f - (float)t.heapMaxBlock / (float)t.heapFree) * 100.0f;
  }
  char fragBuf[8];
  snprintf(fragBuf, sizeof(fragBuf), "%.1f", frag);
  j += ",\"heapFrag\":"; j += fragBuf;

  // Chip temperature
  if (isnan(t.tempC)) {
    j += ",\"temp\":null";
  } else {
    char tb[10];
    snprintf(tb, sizeof(tb), "%.1f", t.tempC);
    j += ",\"temp\":"; j += tb;
  }

  j += ",\"rssi\":"    + String(t.rssi);
  j += ",\"clients\":" + String(t.apClients);
  j += ",\"cpuMHz\":"  + String(t.cpuMHz);

  // GPIO outputs
  j += ",\"gpio\":{\"out\":[";
  for (size_t i = 0; i < OUTPUT_COUNT; i++) {
    if (i) j += ",";
    j += "{\"id\":"  + String((unsigned)i);
    j += ",\"on\":"  + String(Hardware::outputIsOn(i) ? "true" : "false") + "}";
  }
  j += "],\"in\":[";
  for (size_t i = 0; i < INPUT_COUNT; i++) {
    if (i) j += ",";
    j += "{\"id\":"      + String((unsigned)i);
    j += ",\"active\":"  + String(Hardware::inputActive(i) ? "true" : "false") + "}";
  }
  j += "]}";

  // I2C
  const char* ist = Hardware::i2cState() == Hardware::I2C_SCANNING ? "scanning"
                  : Hardware::i2cState() == Hardware::I2C_DONE      ? "done"
                  : "idle";
  j += ",\"i2c\":{\"state\":\"";
  j += ist;
  j += "\",\"found\":[";
  for (uint8_t i = 0; i < Hardware::i2cFoundCount(); i++) {
    if (i) j += ",";
    uint8_t addr = Hardware::i2cFoundAddr(i);
    j += "{\"addr\":" + String(addr);
    // Well-known I2C device names for display convenience.
    const char* name = "?";
    const char* desc = "";
    if      (addr == 0x3C || addr == 0x3D) { name = "SSD1306"; desc = "OLED 128x64"; }
    else if (addr == 0x48)                  { name = "ADS1115"; desc = "ADC 16-bit";  }
    else if (addr == 0x68 || addr == 0x69) { name = "MPU6050"; desc = "IMU 6-axis";  }
    else if (addr == 0x76 || addr == 0x77) { name = "BME280";  desc = "Env sensor";  }
    else if (addr == 0x27 || addr == 0x3F) { name = "PCF8574"; desc = "I2C expander";}
    j += ",\"name\":\""; j += name;
    j += "\",\"desc\":\""; j += desc;
    j += "\"}";
  }
  j += "]}";

  // BLE
  j += ",\"ble\":{\"adv\":";
  j += Hardware::bleAdvertising() ? "true" : "false";
  j += ",\"conn\":" + String(Hardware::bleConnections()) + "}";

  // Wi-Fi AP stats
  j += ",\"wifi\":{\"clients\":" + String(WiFi.softAPgetStationNum());
  j += ",\"ch\":"                + String(WiFi.channel());
  j += ",\"txLevel\":"           + String(Hardware::txPowerLevel()) + "}";

  // Task stack watermarks (bytes) & LittleFS metrics
  j += ",\"watermark\":"    + String(t.loopWatermark);
  j += ",\"telWatermark\":" + String(t.loopWatermark);
  j += ",\"netWatermark\":" + String(t.netWatermark);
  j += ",\"fsUsed\":"       + String((unsigned)LittleFS.usedBytes());
  j += ",\"fsTotal\":"      + String((unsigned)LittleFS.totalBytes());
  j += ",\"heapTotal\":"    + String(t.heapTotal);
  j += ",\"heapMax\":"      + String(t.heapMaxBlock);
  j += ",\"sketchBytes\":"  + String((unsigned)ESP.getSketchSize());
  j += ",\"flashBytes\":"   + String((unsigned)ESP.getFlashChipSize());

  j += "}";
  return j;
}

// =========================================================== setup page ====

// Minimal but functional first-boot setup form served inline.
// Dark VECTRON cyber theme: dark bg, green mono text, subtle glows.
static const char SETUP_PAGE_HTML[] PROGMEM = R"rawhtml(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1"/>
<title>VECTRON-32 – First Boot Setup</title>
<style>
  :root{--green:#00ff88;--dim:#00cc66;--bg:#0a0a0a;--card:#111;--border:#00ff8844;}
  *{box-sizing:border-box;margin:0;padding:0;}
  body{background:var(--bg);color:var(--green);font-family:'Courier New',Courier,monospace;
       min-height:100vh;display:flex;align-items:center;justify-content:center;padding:16px;}
  .card{background:var(--card);border:1px solid var(--border);border-radius:6px;
        padding:32px 28px;width:100%;max-width:440px;
        box-shadow:0 0 32px #00ff8820,0 0 8px #00ff8808;}
  h1{font-size:1.4rem;letter-spacing:4px;text-align:center;margin-bottom:4px;
     text-shadow:0 0 12px var(--green);}
  .sub{text-align:center;color:var(--dim);font-size:.78rem;margin-bottom:28px;letter-spacing:2px;}
  label{display:block;font-size:.75rem;letter-spacing:2px;color:var(--dim);margin-bottom:4px;margin-top:16px;}
  input{width:100%;padding:8px 10px;background:#000;border:1px solid var(--border);
        border-radius:3px;color:var(--green);font-family:inherit;font-size:.9rem;outline:none;
        transition:border-color .2s;}
  input:focus{border-color:var(--green);box-shadow:0 0 6px #00ff8840;}
  hr{border:none;border-top:1px solid var(--border);margin:24px 0;}
  button{width:100%;margin-top:20px;padding:11px;background:transparent;
         border:1px solid var(--green);border-radius:3px;color:var(--green);
         font-family:inherit;font-size:1rem;letter-spacing:3px;cursor:pointer;
         transition:background .2s,box-shadow .2s;}
  button:hover{background:#00ff8818;box-shadow:0 0 12px #00ff8840;}
  .warn{margin-top:16px;font-size:.72rem;color:#ff6644;text-align:center;letter-spacing:1px;}
  .section{font-size:.7rem;letter-spacing:3px;color:#00ff8888;margin-top:8px;}
</style>
</head>
<body>
<div class="card">
  <h1>VECTRON-32</h1>
  <div class="sub">FIRST BOOT &mdash; CREDENTIAL SETUP</div>
  <form method="POST" action="/api/setup" autocomplete="off">
    <div class="section">&#x25B8; ACCESS POINT</div>
    <label for="ap_ssid">AP SSID</label>
    <input type="text" id="ap_ssid" name="ap_ssid" maxlength="32" required
           placeholder="VECTRON-32" value="VECTRON-32"/>
    <label for="ap_pass">AP PASSWORD <span style="color:#ff664488">(8&ndash;63 chars)</span></label>
    <input type="password" id="ap_pass" name="ap_pass" minlength="8" maxlength="63"
           required placeholder="Min 8 characters"/>
    <hr/>
    <div class="section">&#x25B8; ADMIN ACCOUNT</div>
    <label for="admin_user">USERNAME</label>
    <input type="text" id="admin_user" name="admin_user" maxlength="63"
           required placeholder="admin"/>
    <label for="admin_pass">PASSWORD <span style="color:#ff664488">(min 8 chars)</span></label>
    <input type="password" id="admin_pass" name="admin_pass" minlength="8" maxlength="63"
           required placeholder="Min 8 characters"/>
    <button type="submit">&#x25B6; &nbsp; INITIALISE SYSTEM</button>
  </form>
  <div class="warn">&#x26A0; This page is only available once. Reboot to return here
  only if credentials were never saved.</div>
</div>
</body>
</html>)rawhtml";

// =========================================================== WS handler ====

void handleWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                   AwsEventType type, void* arg, uint8_t* data, size_t len) {

  if (type == WS_EVT_CONNECT) {
    // Authenticate the upgrade request via VECTRON_SID cookie.
    // ESPAsyncWebServer provides the upgrade HTTP headers via client->request().
    AsyncWebServerRequest* req = client->request();
    String sid;
    if (req && req->hasHeader("Cookie")) {
      String c   = req->header("Cookie");
      String key = "VECTRON_SID=";
      int    p   = c.indexOf(key);
      if (p >= 0) {
        if (p == 0 || c[p - 1] == ' ' || c[p - 1] == ';') {
          sid = c.substring(p + key.length(), p + key.length() + 32);
        }
      }
    }
    if (!Auth::checkWsToken(sid)) {
      Serial.printf("[WS]  client %u rejected – no valid VECTRON_SID\n", client->id());
      client->close(1008, "Unauthorized");
      return;
    }
    Serial.printf("[WS]  client %u connected from %s\n",
                  client->id(),
                  client->remoteIP().toString().c_str());

  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("[WS]  client %u disconnected\n", client->id());

  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo* info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len &&
        info->opcode == WS_TEXT) {

      // Parse a single-frame text command: {"cmd":"gpio","id":0,"state":1}
      // We use a minimal hand-rolled parse to avoid heap-heavy JSON allocator
      // inside the WS callback.
      String msg((char*)data, len);
      msg.trim();

      // Extract command: check "cmd" or "action"
      String cmd;
      int cmdIdx = msg.indexOf("\"cmd\"");
      if (cmdIdx < 0) cmdIdx = msg.indexOf("\"action\"");
      if (cmdIdx >= 0) {
        int colon = msg.indexOf(':', cmdIdx);
        if (colon >= 0) {
          int q1 = msg.indexOf('"', colon + 1);
          int q2 = (q1 >= 0) ? msg.indexOf('"', q1 + 1) : -1;
          if (q1 >= 0 && q2 >= 0) {
            cmd = msg.substring(q1 + 1, q2);
          }
        }
      }

      auto extractInt = [&](const char* field) -> int {
        int fi = msg.indexOf(String("\"") + field + "\"");
        if (fi < 0) return -1;
        int cl = msg.indexOf(':', fi);
        if (cl < 0) return -1;
        int vi = cl + 1;
        while (vi < (int)msg.length() && (msg[vi] == ' ' || msg[vi] == '\t')) vi++;
        if (vi >= (int)msg.length()) return -1;
        return msg.substring(vi).toInt();
      };

      if (cmd == "gpio") {
        int id    = extractInt("id");
        int pin   = extractInt("pin");
        int state = extractInt("state");

        // If pin was provided instead of id, resolve to output index
        if (id < 0 && pin >= 0) {
          for (size_t k = 0; k < OUTPUT_COUNT; k++) {
            if (OUTPUTS[k].pin == (uint8_t)pin) {
              id = (int)k;
              break;
            }
          }
        }

        if (id < 0 || (size_t)id >= OUTPUT_COUNT || state < 0) return;
        Hardware::setOutput((size_t)id, state != 0);

        // Echo back updated state.
        String ack = "{\"t\":\"gpio_ack\",\"id\":";
        ack += id;
        ack += ",\"pin\":";
        ack += OUTPUTS[id].pin;
        ack += ",\"on\":";
        ack += Hardware::outputIsOn((size_t)id) ? "true" : "false";
        ack += "}";
        client->text(ack);

      } else if (cmd == "reboot") {
        client->text("{\"t\":\"reboot_ack\"}");
        Hardware::requestReboot(800);

      } else if (cmd == "ping") {
        client->text("{\"t\":\"pong\"}");

      } else if (cmd == "i2c_scan") {
        Hardware::requestI2cScan();
        client->text("{\"t\":\"i2c_scan_ack\"}");

      } else if (cmd == "txpower") {
        int lvl = extractInt("level");
        if (lvl >= 0 && lvl <= 2) {
          Hardware::setTxPowerLevel((uint8_t)lvl);
          client->text("{\"t\":\"txpower_ack\"}");
        }
      }
    }
  } else if (type == WS_EVT_ERROR) {
    Serial.printf("[WS]  client %u error %u: %s\n",
                  client->id(), *((uint16_t*)arg), (char*)data);
  }
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Public Api namespace
// ---------------------------------------------------------------------------
namespace Api {

void begin(AsyncWebServer& server, bool filesystemOk) {

  s_ws->onEvent(handleWsEvent);
  server.addHandler(s_ws);

  // ================================================================ public ==

  // Session probe – used by the SPA to decide which screen to show.
  server.on("/api/session", HTTP_GET, [](AsyncWebServerRequest* r) {
    bool auth      = Auth::check(r);
    bool firstBoot = Auth::isFirstBoot();
    String j = String("{\"ok\":true,\"authenticated\":") +
               (auth ? "true" : "false") +
               ",\"firstBoot\":"  + (firstBoot ? "true" : "false") +
               ",\"device\":\"" FW_NAME "\"}";
    sendJson(r, 200, j);
  });

  // Login
  server.on("/api/login", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (!sameOrigin(r)) { sendErr(r, 403, "BAD_ORIGIN"); return; }

    String user, pass;
    if (!getParam(r, "user", user) || !getParam(r, "pass", pass)) {
      sendErr(r, 400, "MISSING_FIELDS");
      return;
    }

    const uint32_t ip = (uint32_t)r->client()->remoteIP();
    String   token;
    uint32_t retry = 0;
    Auth::Result res = Auth::login(ip, user, pass, token, retry);

    if (res == Auth::Result::OK) {
      AsyncWebServerResponse* resp =
        r->beginResponse(200, "application/json", "{\"ok\":true}");
      resp->addHeader("Cache-Control", "no-store");
      resp->addHeader("Set-Cookie",    Auth::cookieHeader(token).c_str());
      r->send(resp);
      Serial.println("[AUTH] login OK");
    } else if (res == Auth::Result::LOCKED) {
      sendJson(r, 429,
               String("{\"ok\":false,\"error\":\"LOCKED\",\"retryAfter\":") + retry + "}");
    } else if (res == Auth::Result::FIRST_BOOT) {
      sendErr(r, 403, "FIRST_BOOT_REQUIRED");
    } else {
      Serial.println("[AUTH] login DENIED");
      sendJson(r, 401,
               String("{\"ok\":false,\"error\":\"ACCESS DENIED\",\"attemptsLeft\":") +
               Auth::attemptsLeft(ip) + "}");
    }
  });

  // First-boot setup – GET serves the inline HTML form.
  server.on("/setup", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (!Auth::isFirstBoot()) {
      // Already configured – redirect to root.
      AsyncWebServerResponse* resp = r->beginResponse(302, "text/plain", "");
      resp->addHeader("Location", "/");
      r->send(resp);
      return;
    }
    r->send_P(200, "text/html", SETUP_PAGE_HTML);
  });

  // First-boot setup / provisioning – POST applies credentials and reboots.
  auto handleProvision = [](AsyncWebServerRequest* r) {
    if (!sameOrigin(r)) { sendErr(r, 403, "BAD_ORIGIN"); return; }

    if (!Auth::isFirstBoot()) {
      sendErr(r, 403, "ALREADY_CONFIGURED");
      return;
    }

    String ap_ssid, ap_pass, admin_user, admin_pass;
    if (!getParam(r, "ap_ssid", ap_ssid)) getParam(r, "ssid", ap_ssid);
    if (!getParam(r, "ap_pass", ap_pass)) getParam(r, "pass", ap_pass);
    if (!getParam(r, "admin_user", admin_user)) getParam(r, "user", admin_user);
    if (!getParam(r, "admin_pass", admin_pass)) getParam(r, "password", admin_pass);

    if (ap_ssid.length() < 1     || ap_pass.length()  < 8    ||
        admin_user.length() < 1  || admin_pass.length() < 8) {
      sendErr(r, 400, "INVALID_FIELDS");
      return;
    }

    if (ap_ssid.length() > 32   || ap_pass.length()  > 63 ||
        admin_user.length() > 63 || admin_pass.length() > 63) {
      sendErr(r, 400, "FIELDS_TOO_LONG");
      return;
    }

    if (!Storage::setCredentials(ap_ssid.c_str(), ap_pass.c_str(), admin_user.c_str(), admin_pass.c_str())) {
      sendErr(r, 500, "STORAGE_ERROR");
      return;
    }

    sendJson(r, 200, "{\"ok\":true,\"rebooting\":true}");
    Hardware::requestReboot(1500);
  };

  server.on("/api/setup",     HTTP_POST, handleProvision);
  server.on("/api/provision", HTTP_POST, handleProvision);

  // Logout
  server.on("/api/logout", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (!sameOrigin(r)) { sendErr(r, 403, "BAD_ORIGIN"); return; }
    Auth::logout(r);
    AsyncWebServerResponse* resp =
      r->beginResponse(200, "application/json", "{\"ok\":true}");
    resp->addHeader("Set-Cookie", Auth::clearCookieHeader().c_str());
    r->send(resp);
  });

  // ============================================================ protected ==

  server.on("/api/info", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (!requireAuth(r)) return;
    sendJson(r, 200, buildInfo());
  });

  // DEPRECATED – kept for backward compat. Prefer WebSocket telemetry frames.
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (!requireAuth(r)) return;
    AsyncWebServerResponse* resp =
      r->beginResponse(200, "application/json", buildStatus());
    resp->addHeader("Cache-Control", "no-store");
    resp->addHeader("Deprecation",   "true");
    resp->addHeader("X-Deprecated-By", "ws://host/ws");
    r->send(resp);
  });

  server.on("/api/gpio", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (!requireAuthPost(r)) return;
    String id, state;
    if (!getParam(r, "id", id) || !getParam(r, "state", state)) {
      sendErr(r, 400, "MISSING_FIELDS");
      return;
    }
    long i = id.toInt();
    if (i < 0 || (size_t)i >= OUTPUT_COUNT || (id != "0" && i == 0)) {
      sendErr(r, 404, "NO_SUCH_OUTPUT");
      return;
    }
    if (state != "1" && state != "0") { sendErr(r, 400, "BAD_STATE"); return; }
    if (!Hardware::setOutput((size_t)i, state == "1")) {
      sendErr(r, 409, "PIN_BLOCKED");
      return;
    }
    sendJson(r, 200,
             String("{\"ok\":true,\"id\":") + i +
             ",\"on\":"  + (Hardware::outputIsOn((size_t)i) ? "true" : "false") + "}");
  });

  server.on("/api/bluetooth", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (!requireAuthPost(r)) return;
    String state;
    if (!getParam(r, "state", state) || (state != "1" && state != "0")) {
      sendErr(r, 400, "BAD_STATE");
      return;
    }
    Hardware::requestBle(state == "1");
    sendJson(r, 202, "{\"ok\":true,\"pending\":true}");
  });

  server.on("/api/wifi/txpower", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (!requireAuthPost(r)) return;
    String level;
    if (!getParam(r, "level", level) ||
        level.length() != 1 || level[0] < '0' || level[0] > '2') {
      sendErr(r, 400, "BAD_LEVEL");
      return;
    }
    if (!Hardware::setTxPowerLevel((uint8_t)(level[0] - '0'))) {
      sendErr(r, 500, "TXPOWER_FAILED");
      return;
    }
    sendJson(r, 200, "{\"ok\":true}");
  });

  server.on("/api/i2c/scan", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (!requireAuthPost(r)) return;
    Hardware::requestI2cScan();
    sendJson(r, 202, "{\"ok\":true,\"pending\":true}");
  });

  server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (!requireAuthPost(r)) return;
    Hardware::requestReboot(800);
    sendJson(r, 200, "{\"ok\":true}");
  });

  // ======================================================= captive portal ==
  // Android / iOS / Windows captive-portal probes.
  auto captiveRedirect = [](AsyncWebServerRequest* r) {
    AsyncWebServerResponse* resp = r->beginResponse(302, "text/plain", "");
    resp->addHeader("Location", "http://192.168.4.1/");
    r->send(resp);
  };
  server.on("/generate_204",        HTTP_GET, captiveRedirect);
  server.on("/hotspot-detect.html", HTTP_GET, captiveRedirect);
  server.on("/fwlink",              HTTP_GET, captiveRedirect);
  server.on("/connecttest.txt",     HTTP_GET, captiveRedirect);
  server.on("/ncsi.txt",            HTTP_GET, captiveRedirect);

  // =========================================================== frontend ==
  if (filesystemOk) {
    // Explicit MIME for woff2 (not all library versions know it).
    server.on("/fonts/*", HTTP_GET, [](AsyncWebServerRequest* r) {
      String path = r->url();
      if (path.indexOf("..") >= 0 || !path.endsWith(".woff2") ||
          !LittleFS.exists(path)) {
        r->send(404, "text/plain", "NOT FOUND");
        return;
      }
      AsyncWebServerResponse* resp =
        r->beginResponse(LittleFS, path, "font/woff2");
      resp->addHeader("Cache-Control", "public, max-age=86400");
      r->send(resp);
    });
    server.serveStatic("/", LittleFS, "/")
          .setDefaultFile("index.html")
          .setCacheControl("no-cache");
  } else {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
      r->send(200, "text/plain",
              "VECTRON-32: filesystem not mounted.\n"
              "Run: PlatformIO > Upload Filesystem Image, then reset the board.\n");
    });
  }

  // 404
  server.onNotFound([](AsyncWebServerRequest* r) {
    if (r->url().startsWith("/api/")) sendErr(r, 404, "NOT_FOUND");
    else r->send(404, "text/plain", "404 NOT FOUND");
  });

  Serial.println("[API]  routes registered");
}

// ---------------------------------------------------------------------------
void Api::broadcastTelemetry() {
  if (!s_ws || s_ws->count() == 0) return;
  String frame = buildTelemetryFrame();
  s_ws->textAll(frame);
}

// ---------------------------------------------------------------------------
void Api::broadcastTelemetry(const char* payload) {
  if (!s_ws || s_ws->count() == 0) return;
  if (payload) {
    s_ws->textAll(payload);
  } else {
    broadcastTelemetry();
  }
}

// ---------------------------------------------------------------------------
void Api::cleanupWsClients() {
  if (s_ws) s_ws->cleanupClients();
}

}  // namespace Api
