#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
//  MotionOS — WebServer.h
//  REST API + WebSocket telemetry + Captive Portal + Time Sync
// ═══════════════════════════════════════════════════════════════════════════════
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include "MotorManager.h"
#include "ActivityLogger.h"
#include "AuthManager.h"
#include "Config.h"

class WebServerManager {
public:
    void begin();
    void update();      // call from loop() — WS broadcast + DNS + cleanup

private:
    AsyncWebServer  _server{80};
    AsyncWebSocket  _ws{"/ws"};
    DNSServer       _dns;
    unsigned long   _lastWsBroadcast = 0;

    // ── Route setup ───────────────────────────────────────────────────────────
    void _setupCaptivePortal();
    void _setupAuth();
    void _setupMotorAPI();
    void _setupConfigAPI();
    void _setupLogAPI();
    void _setupSystemAPI();
    void _setupWS();

    // ── Telemetry ─────────────────────────────────────────────────────────────
    String _buildTelemetry();

    // ── Helpers ───────────────────────────────────────────────────────────────
    bool   _requireAuth(AsyncWebServerRequest* req);
    void   _sendJSON(AsyncWebServerRequest* req, int code, const String& body);
    bool   _getCookie(AsyncWebServerRequest* req, const String& name, String& out);
};

extern WebServerManager webMgr;
