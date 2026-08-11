// ═══════════════════════════════════════════════════════════════════════════════
//  MotionOS — WebServer.cpp
//  REST API  |  WebSocket telemetry (read-only push)  |  Captive Portal
//  All motor commands arrive as HTTP POST — no WS command pathway.
// ═══════════════════════════════════════════════════════════════════════════════
#include "WebServer.h"
#include <WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

WebServerManager webMgr;

// ─── Helpers ─────────────────────────────────────────────────────────────────
bool WebServerManager::_getCookie(AsyncWebServerRequest* req, const String& name, String& out) {
    if (!req->hasHeader("Cookie")) return false;
    const AsyncWebHeader* h = req->getHeader("Cookie");
    if (!h) return false;
    String c = h->value(); int p = 0;
    while (p < (int)c.length()) {
        while (p < (int)c.length() && (c[p]==' '||c[p]==';')) p++;
        int eq = c.indexOf('=', p); if (eq < 0) break;
        String k = c.substring(p, eq); k.trim();
        int sc = c.indexOf(';', eq+1); if (sc < 0) sc = c.length();
        String v = c.substring(eq+1, sc); v.trim();
        if (k == name) { out = v; return true; }
        p = sc + 1;
    }
    return false;
}

bool WebServerManager::_requireAuth(AsyncWebServerRequest* req) {
    String sid;
    if (!_getCookie(req, "SID", sid) || !authMgr.validate(sid)) {
        req->redirect("/login"); return false;
    }
    return true;
}

void WebServerManager::_sendJSON(AsyncWebServerRequest* req, int code, const String& body) {
    auto* res = req->beginResponse(code, "application/json", body);
    res->addHeader("Cache-Control", "no-cache");
    req->send(res);
}

// ─── Telemetry ───────────────────────────────────────────────────────────────
String WebServerManager::_buildTelemetry() {
    String out = F("{\"motors\":[");
    for (int i = 0; i < MAX_MOTORS; i++) {
        Motor& m = motorMgr.motors[i];
        if (i > 0) out += ',';
        out += F("{\"idx\":");         out += i;
        out += F(",\"label\":\"");     out += m.cfg.label;           out += '"';
        out += F(",\"motor\":\"");     out += m.cfg.motorModel;      out += '"';
        out += F(",\"driver\":\"");    out += m.cfg.driverModel;     out += '"';
        out += F(",\"steps\":");       out += (int)m.cfg.stepsPerRev;
        out += F(",\"current\":");     out += String(m.cfg.currentA, 1);
        out += F(",\"active\":");      out += m.cfg.active   ? "true" : "false";
        out += F(",\"enabled\":");     out += m.state.enabled? "true" : "false";
        out += F(",\"mode\":");        out += (int)m.state.mode;
        out += F(",\"dir\":");         out += m.state.dirCW  ? "1" : "0";
        out += F(",\"rpm\":");         out += String(m.state.appliedRPM, 1);
        out += F(",\"angle\":");       out += String(m.currentAngle(), 2);
        out += F(",\"zero_set\":");    out += m.state.zeroSet? "true" : "false";
        out += F(",\"test_running\":"); out += m.state.testRunning ? "true" : "false";
        out += F(",\"test_cycles\":"); out += m.state.testCycles;
        out += F(",\"test_target\":"); out += m.state.testTargetCycles;
        out += '}';
    }
    out += F("],\"recording\":");
    out += actLog.recording ? "true" : "false";
    out += F(",\"log_size\":");
    out += actLog.fileSize();
    out += F(",\"time_synced\":");
    out += actLog.isTimeSynced() ? "true" : "false";
    out += '}';
    return out;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  begin()
// ═══════════════════════════════════════════════════════════════════════════════
void WebServerManager::begin() {
    _setupCaptivePortal();
    _setupAuth();
    _setupMotorAPI();
    _setupConfigAPI();
    _setupLogAPI();
    _setupSystemAPI();
    _setupWS();

    // ── Dashboard (auth-gated) ────────────────────────────────────────────────
    _server.on("/", HTTP_GET, [this](AsyncWebServerRequest* req){
        if (!_requireAuth(req)) return;
        if (LittleFS.exists("/index.html.gz")) {
            auto* r = req->beginResponse(LittleFS, "/index.html.gz", "text/html");
            r->addHeader("Content-Encoding", "gzip"); req->send(r);
        } else if (LittleFS.exists("/index.html")) {
            req->send(LittleFS, "/index.html", "text/html");
        } else {
            req->send(200, "text/html", "<h2>Upload LittleFS: pio run -t uploadfs</h2>");
        }
    });

    _server.on("/sw.js",       HTTP_GET, [](AsyncWebServerRequest* r){ r->send(LittleFS, "/sw.js",       "application/javascript"); });
    _server.on("/manifest.json",HTTP_GET,[](AsyncWebServerRequest* r){ r->send(LittleFS, "/manifest.json","application/json"); });
    _server.on("/icon-192.png",HTTP_GET, [](AsyncWebServerRequest* r){ r->send(LittleFS, "/icon-192.png","image/png"); });
    _server.on("/icon-512.png",HTTP_GET, [](AsyncWebServerRequest* r){ r->send(LittleFS, "/icon-512.png","image/png"); });

    // ── API: full status ──────────────────────────────────────────────────────
    _server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* req){
        if (!_requireAuth(req)) return;
        _sendJSON(req, 200, _buildTelemetry());
    });

    // ── Catch-all → captive portal ────────────────────────────────────────────
    _server.onNotFound([](AsyncWebServerRequest* req){
        req->redirect("http://" AP_IP_ADDR "/login");
    });

    _server.addHandler(&_ws);
    _server.begin();
    Serial.println("[Web] Server started");
}

// ─── Captive Portal ──────────────────────────────────────────────────────────
void WebServerManager::_setupCaptivePortal() {
    IPAddress apIP; apIP.fromString(AP_IP_ADDR);
    _dns.setTTL(300);
    _dns.start(53, "*", apIP);

    auto captive = [](AsyncWebServerRequest* req){ req->redirect("http://" AP_IP_ADDR "/login"); };
    _server.on("/generate_204",              HTTP_GET, captive);
    _server.on("/gen_204",                   HTTP_GET, captive);
    _server.on("/hotspot-detect.html",       HTTP_GET, captive);
    _server.on("/library/test/success.html", HTTP_GET, captive);
    _server.on("/ncsi.txt",                  HTTP_GET, captive);
    _server.on("/connecttest.txt",           HTTP_GET, captive);
    _server.on("/redirect",                  HTTP_GET, captive);
    _server.on("/canonical.html",            HTTP_GET, captive);
    _server.on("/success.txt",               HTTP_GET, captive);
    _server.on("/connectivity-check",        HTTP_GET, captive);
}

// ─── Auth routes ─────────────────────────────────────────────────────────────
void WebServerManager::_setupAuth() {
    _server.on("/login", HTTP_GET, [](AsyncWebServerRequest* req){
        LittleFS.exists("/login.html")
            ? req->send(LittleFS, "/login.html", "text/html")
            : req->send(200, "text/html", "<h2>Upload LittleFS</h2>");
    });

    _server.on("/login", HTTP_POST, [](AsyncWebServerRequest*){}, nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
            JsonDocument doc;
            if (deserializeJson(doc, data, len) != DeserializationError::Ok)
                { req->send(400, "application/json", "{\"error\":\"bad json\"}"); return; }
            const char* u = doc["u"] | "";
            const char* p = doc["p"] | "";
            if (strcmp(u, ADMIN_USER) == 0 && strcmp(p, ADMIN_PASS) == 0) {
                String sid = authMgr.createSession();
                actLog.log(LogEvent::LOGIN, "sys", "admin");
                auto* res = req->beginResponse(200, "application/json", "{\"ok\":true}");
                res->addHeader("Set-Cookie", "SID=" + sid + "; Path=/; HttpOnly; SameSite=Lax");
                req->send(res);
            } else {
                req->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
            }
        }
    );

    _server.on("/logout", HTTP_POST, [this](AsyncWebServerRequest* req){
        String sid;
        if (_getCookie(req, "SID", sid)) {
            authMgr.destroySession(sid);
            actLog.log(LogEvent::LOGOUT, "sys", "admin");
        }
        auto* res = req->beginResponse(200, "application/json", "{\"ok\":true}");
        res->addHeader("Set-Cookie", "SID=; Max-Age=0; Path=/; HttpOnly; SameSite=Lax");
        req->send(res);
    });
}

// ─── Motor API ───────────────────────────────────────────────────────────────
void WebServerManager::_setupMotorAPI() {

    // POST /api/motor/{0-3}/move  { angle, rpm }  — relative move; +deg=CW
    _server.on("^\\/api\\/motor\\/([0-3])\\/move$", HTTP_POST,
        [](AsyncWebServerRequest*){}, nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
            if (!_requireAuth(req)) return;
            Motor* m = motorMgr.get(req->pathArg(0).toInt());
            if (!m || !m->cfg.active) { _sendJSON(req, 404, "{\"error\":\"not found\"}"); return; }
            JsonDocument doc; deserializeJson(doc, data, len);
            float angle = constrain((float)(doc["angle"] | 0.0f), -360.0f, 360.0f);
            float rpm   = constrain((float)(doc["rpm"]   | (float)DEFAULT_RPM), 1.0f, (float)MAX_RPM);
            m->startMove(angle, rpm);
            _sendJSON(req, 200, "{\"ok\":true}");
        }
    );

    // POST /api/motor/{0-3}/goto  { angle, rpm }  — absolute from zero
    _server.on("^\\/api\\/motor\\/([0-3])\\/goto$", HTTP_POST,
        [](AsyncWebServerRequest*){}, nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
            if (!_requireAuth(req)) return;
            Motor* m = motorMgr.get(req->pathArg(0).toInt());
            if (!m || !m->cfg.active) { _sendJSON(req, 404, "{\"error\":\"not found\"}"); return; }
            if (!m->state.zeroSet)    { _sendJSON(req, 400, "{\"error\":\"zero not set\"}"); return; }
            JsonDocument doc; deserializeJson(doc, data, len);
            float angle = constrain((float)(doc["angle"] | 0.0f), 0.0f, 360.0f);
            float rpm   = constrain((float)(doc["rpm"]   | (float)DEFAULT_RPM), 1.0f, (float)MAX_RPM);
            m->startGoto(angle, rpm);
            _sendJSON(req, 200, "{\"ok\":true}");
        }
    );

    // POST /api/motor/{0-3}/run   { rpm, dir }    — continuous
    _server.on("^\\/api\\/motor\\/([0-3])\\/run$", HTTP_POST,
        [](AsyncWebServerRequest*){}, nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
            if (!_requireAuth(req)) return;
            Motor* m = motorMgr.get(req->pathArg(0).toInt());
            if (!m || !m->cfg.active) { _sendJSON(req, 404, "{\"error\":\"not found\"}"); return; }
            JsonDocument doc; deserializeJson(doc, data, len);
            float rpm = constrain((float)(doc["rpm"] | (float)DEFAULT_RPM), 1.0f, (float)MAX_RPM);
            bool  cw  = (int)(doc["dir"] | 1) != 0;
            m->startContinuous(rpm, cw);
            _sendJSON(req, 200, "{\"ok\":true}");
        }
    );

    // POST /api/motor/{0-3}/jog  { action:"start"|"stop"|"heartbeat", dir, rpm }
    _server.on("^\\/api\\/motor\\/([0-3])\\/jog$", HTTP_POST,
        [](AsyncWebServerRequest*){}, nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
            if (!_requireAuth(req)) return;
            Motor* m = motorMgr.get(req->pathArg(0).toInt());
            if (!m || !m->cfg.active) { _sendJSON(req, 404, "{\"error\":\"not found\"}"); return; }
            JsonDocument doc; deserializeJson(doc, data, len);
            const char* action = doc["action"] | "heartbeat";
            if (strcmp(action, "start") == 0) {
                bool  cw  = (int)(doc["dir"] | 1) != 0;
                float rpm = constrain((float)(doc["rpm"] | (float)DEFAULT_JOG_RPM), 1.0f, (float)MAX_RPM);
                m->startJog(cw, rpm);
            } else if (strcmp(action, "stop") == 0) {
                if (m->state.mode == MotorMode::JOGGING) m->stopJog();
            } else {
                // heartbeat — keep watchdog alive
                if (m->state.mode == MotorMode::JOGGING) m->updateJogHeartbeat();
            }
            _sendJSON(req, 200, "{\"ok\":true}");
        }
    );

    // POST /api/motor/{0-3}/stop
    _server.on("^\\/api\\/motor\\/([0-3])\\/stop$", HTTP_POST,
        [this](AsyncWebServerRequest* req){
            if (!_requireAuth(req)) return;
            Motor* m = motorMgr.get(req->pathArg(0).toInt());
            if (!m) { _sendJSON(req, 404, "{\"error\":\"not found\"}"); return; }
            m->stopMotion();
            _sendJSON(req, 200, "{\"ok\":true}");
        }
    );

    // POST /api/motor/{0-3}/estop
    _server.on("^\\/api\\/motor\\/([0-3])\\/estop$", HTTP_POST,
        [this](AsyncWebServerRequest* req){
            if (!_requireAuth(req)) return;
            Motor* m = motorMgr.get(req->pathArg(0).toInt());
            if (!m) { _sendJSON(req, 404, "{\"error\":\"not found\"}"); return; }
            m->eStop();
            _sendJSON(req, 200, "{\"ok\":true}");
        }
    );

    // POST /api/motor/{0-3}/zero
    _server.on("^\\/api\\/motor\\/([0-3])\\/zero$", HTTP_POST,
        [this](AsyncWebServerRequest* req){
            if (!_requireAuth(req)) return;
            Motor* m = motorMgr.get(req->pathArg(0).toInt());
            if (!m || !m->cfg.active) { _sendJSON(req, 404, "{\"error\":\"not found\"}"); return; }
            m->setZero();
            _sendJSON(req, 200, "{\"ok\":true}");
        }
    );

    // POST /api/motor/{0-3}/ena  { enabled: true|false }
    _server.on("^\\/api\\/motor\\/([0-3])\\/ena$", HTTP_POST,
        [](AsyncWebServerRequest*){}, nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
            if (!_requireAuth(req)) return;
            Motor* m = motorMgr.get(req->pathArg(0).toInt());
            if (!m || !m->cfg.active) { _sendJSON(req, 404, "{\"error\":\"not found\"}"); return; }
            // Cannot re-enable while ESTOPPED via this route; use explicit re-enable
            if (m->state.mode == MotorMode::ESTOPPED) {
                // Allow re-enable after estop via UI toggle
                m->state.mode = MotorMode::IDLE;
            }
            JsonDocument doc; deserializeJson(doc, data, len);
            bool en = (bool)doc["enabled"];
            m->setEnable(en);
            actLog.log(en ? LogEvent::ENA_ON : LogEvent::ENA_OFF, m->cfg.label, "");
            _sendJSON(req, 200, "{\"ok\":true}");
        }
    );

    // POST /api/motor/{0-3}/test  { action:"start"|"stop", angle, rpm, cycles }
    _server.on("^\\/api\\/motor\\/([0-3])\\/test$", HTTP_POST,
        [](AsyncWebServerRequest*){}, nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
            if (!_requireAuth(req)) return;
            Motor* m = motorMgr.get(req->pathArg(0).toInt());
            if (!m || !m->cfg.active) { _sendJSON(req, 404, "{\"error\":\"not found\"}"); return; }
            JsonDocument doc; deserializeJson(doc, data, len);
            const char* action = doc["action"] | "stop";
            if (strcmp(action, "start") == 0) {
                float    angle  = constrain((float)(doc["angle"]  | 45.0f), 1.0f, 360.0f);
                float    rpm    = constrain((float)(doc["rpm"]    | (float)DEFAULT_RPM), 1.0f, (float)MAX_RPM);
                uint32_t cycles = constrain((int)(doc["cycles"] | 10), 1, 10000);
                m->startTest(angle, rpm, cycles);
            } else {
                m->stopTest();
            }
            _sendJSON(req, 200, "{\"ok\":true}");
        }
    );

    // POST /api/estop_all
    _server.on("/api/estop_all", HTTP_POST, [this](AsyncWebServerRequest* req){
        if (!_requireAuth(req)) return;
        for (int i = 0; i < MAX_MOTORS; i++) {
            if (motorMgr.motors[i].cfg.active) motorMgr.motors[i].eStop();
        }
        actLog.log(LogEvent::ESTOP, "sys", "ALL");
        _sendJSON(req, 200, "{\"ok\":true}");
    });
}

// ─── Config API ──────────────────────────────────────────────────────────────
void WebServerManager::_setupConfigAPI() {
    _server.on("/api/config", HTTP_GET, [this](AsyncWebServerRequest* req){
        if (!_requireAuth(req)) return;
        _sendJSON(req, 200, _buildTelemetry());
    });

    _server.on("^\\/api\\/config\\/([0-3])$", HTTP_POST,
        [](AsyncWebServerRequest*){}, nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
            if (!_requireAuth(req)) return;
            int idx = req->pathArg(0).toInt();
            Motor* m = motorMgr.get(idx);
            if (!m) { _sendJSON(req, 404, "{\"error\":\"invalid index\"}"); return; }
            JsonDocument doc; deserializeJson(doc, data, len);
            bool wasActive = m->cfg.active;
            if (doc.containsKey("label"))   strncpy(m->cfg.label,       doc["label"]   | m->cfg.label,       sizeof(m->cfg.label)-1);
            if (doc.containsKey("motor"))   strncpy(m->cfg.motorModel,  doc["motor"]   | m->cfg.motorModel,  sizeof(m->cfg.motorModel)-1);
            if (doc.containsKey("driver"))  strncpy(m->cfg.driverModel, doc["driver"]  | m->cfg.driverModel, sizeof(m->cfg.driverModel)-1);
            if (doc.containsKey("steps"))   m->cfg.stepsPerRev = (uint32_t)(int)doc["steps"];
            if (doc.containsKey("current")) m->cfg.currentA    = (float)doc["current"];
            if (doc.containsKey("active"))  m->cfg.active      = (bool)doc["active"];
            motorMgr.saveConfig();
            String det = String(m->cfg.label) + " " + m->cfg.motorModel + "/" + m->cfg.driverModel;
            actLog.log(!wasActive && m->cfg.active ? LogEvent::MOTOR_ADDED : LogEvent::CONFIG_SAVED, m->cfg.label, det);
            _sendJSON(req, 200, "{\"ok\":true}");
        }
    );

    _server.on("^\\/api\\/config\\/([0-3])\\/remove$", HTTP_POST,
        [this](AsyncWebServerRequest* req){
            if (!_requireAuth(req)) return;
            int idx = req->pathArg(0).toInt();
            if (idx == 0) { _sendJSON(req, 400, "{\"error\":\"M1 cannot be removed\"}"); return; }
            Motor* m = motorMgr.get(idx);
            if (!m) { _sendJSON(req, 404, "{\"error\":\"not found\"}"); return; }
            m->stopMotion(); m->setEnable(false);
            actLog.log(LogEvent::MOTOR_REMOVED, m->cfg.label, "");
            m->cfg.active = false;
            motorMgr.saveConfig();
            _sendJSON(req, 200, "{\"ok\":true}");
        }
    );
}

// ─── Log API ─────────────────────────────────────────────────────────────────
void WebServerManager::_setupLogAPI() {
    // Returns CSV text — header + last n rows
    _server.on("/api/log", HTTP_GET, [this](AsyncWebServerRequest* req){
        if (!_requireAuth(req)) return;
        int n = 100;
        if (req->hasParam("n")) n = constrain(req->getParam("n")->value().toInt(), 1, 200);
        String csv = actLog.recentEntriesCSV(n);
        auto* res = req->beginResponse(200, "text/csv", csv);
        res->addHeader("Cache-Control", "no-cache");
        req->send(res);
    });

    // Full file download
    _server.on("/api/log/download", HTTP_GET, [this](AsyncWebServerRequest* req){
        if (!_requireAuth(req)) return;
        if (!LittleFS.exists(LOG_FILE)) { req->send(404, "text/plain", "No log"); return; }
        auto* res = req->beginResponse(LittleFS, LOG_FILE, "text/csv");
        res->addHeader("Content-Disposition", "attachment; filename=\"motionos_log.csv\"");
        req->send(res);
    });

    _server.on("/api/log/clear", HTTP_POST, [this](AsyncWebServerRequest* req){
        if (!_requireAuth(req)) return;
        actLog.clearLog();
        actLog.log(LogEvent::LOG_CLEARED, "sys", "by admin");
        _sendJSON(req, 200, "{\"ok\":true}");
    });

    _server.on("/api/log/record", HTTP_POST,
        [](AsyncWebServerRequest*){}, nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
            if (!_requireAuth(req)) return;
            JsonDocument doc; deserializeJson(doc, data, len);
            bool rec = (bool)doc["recording"];
            actLog.recording = rec;
            actLog.log(rec ? LogEvent::RECORD_START : LogEvent::RECORD_STOP, "sys", "");
            _sendJSON(req, 200, "{\"ok\":true}");
        }
    );
}

// ─── System API ──────────────────────────────────────────────────────────────
void WebServerManager::_setupSystemAPI() {
    // Browser posts Date.now() + tzOffset on login — gives us real timestamps
    _server.on("/api/time", HTTP_POST,
        [](AsyncWebServerRequest*){}, nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
            if (!_requireAuth(req)) return;
            JsonDocument doc; deserializeJson(doc, data, len);
            double   epochD = doc["epoch"]    | 0.0;
            int16_t  tz     = doc["tzOffset"] | (int16_t)0;
            if (epochD > 0) {
                actLog.setTimeSync((uint64_t)epochD, millis(), tz);
                actLog.log(LogEvent::TIME_SYNC, "sys", "tz=" + String(tz));
            }
            _sendJSON(req, 200, "{\"ok\":true}");
        }
    );
}

// ─── WebSocket — read-only telemetry push ────────────────────────────────────
void WebServerManager::_setupWS() {
    _ws.onEvent([this](AsyncWebSocket*, AsyncWebSocketClient* client,
                       AwsEventType type, void*, uint8_t*, size_t){
        if (type == WS_EVT_CONNECT)
            client->text(_buildTelemetry()); // full state on connect
        // WS_EVT_DATA ignored — WS is telemetry-only; commands go via REST
    });
}

// ─── update() ────────────────────────────────────────────────────────────────
void WebServerManager::update() {
    _dns.processNextRequest();
    _ws.cleanupClients();
    actLog.update();
    authMgr.cleanup();

    uint32_t now = millis();
    bool anyActive = false;
    for (int i = 0; i < MAX_MOTORS; i++) {
        Motor& m = motorMgr.motors[i];
        if (m.cfg.active && (m.state.enabled || m.state.mode != MotorMode::IDLE))
            { anyActive = true; break; }
    }
    uint32_t interval = anyActive ? 150 : 800;

    if (now - _lastWsBroadcast >= interval) {
        _lastWsBroadcast = now;
        _ws.textAll(_buildTelemetry());
    }
}
