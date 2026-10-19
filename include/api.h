#pragma once
#include <ESPAsyncWebServer.h>

namespace Api {
  // Registers all /api routes, WebSocket handler, static file serving and the
  // 404 handler. Must be called before server.begin().
  void begin(AsyncWebServer& server, bool filesystemOk);

  // Collect telemetry and broadcast a JSON frame to all connected WS clients.
  // Call from the telemetry task (vTelemetryTask) every 100ms.
  void broadcastTelemetry();

  // Overload: broadcast an explicit JSON payload to all connected WS clients.
  void broadcastTelemetry(const char* payload);

  // Call cleanupClients() on the internal WebSocket; use from network task.
  void cleanupWsClients();
}
