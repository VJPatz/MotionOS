// ═══════════════════════════════════════════════════════════════════════════════
//  MotionOS — main.cpp
//  Boot sequence: LittleFS → WiFi AP → Motors → Logger → WebServer
// ═══════════════════════════════════════════════════════════════════════════════
#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include "Config.h"
#include "MotorManager.h"
#include "ActivityLogger.h"
#include "AuthManager.h"
#include "WebServer.h"

// ─── setup ───────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(400);
    Serial.println(F("\n\n╔══════════════════════════════════╗"));
    Serial.println(F(  "║      MotionOS v1.0.0 booting     ║"));
    Serial.println(F(  "╚══════════════════════════════════╝"));

    // ── LittleFS ─────────────────────────────────────────────────────────────
    if (!LittleFS.begin(true)) {
        Serial.println(F("[FATAL] LittleFS mount failed — flash OK?"));
        ESP.restart();
    }
    Serial.printf("[FS] LittleFS mounted — %u KB used / %u KB total\n",
                  (unsigned)(LittleFS.usedBytes()  / 1024),
                  (unsigned)(LittleFS.totalBytes() / 1024));

    // ── WiFi Access Point ─────────────────────────────────────────────────────
    IPAddress apIP, gw, sn;
    apIP.fromString(AP_IP_ADDR); gw = apIP; sn.fromString("255.255.255.0");

    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, gw, sn);
    WiFi.softAP(AP_SSID, AP_PASS, 6, 0, 4);   // ch 6, visible, max 4 clients
    Serial.printf("[WiFi] AP → SSID: %s  IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

    // ── Subsystems ────────────────────────────────────────────────────────────
    actLog.begin();          // LittleFS CSV log
    actLog.recording = true; // log everything from boot

    motorMgr.begin();        // init pins, timers, load config

    webMgr.begin();          // HTTP server + DNS + WS

    Serial.println(F("[Boot] MotionOS ready\n"));
    Serial.printf("  Connect to WiFi: %s  /  Password: %s\n", AP_SSID, AP_PASS);
    Serial.printf("  Open browser → http://%s\n", AP_IP_ADDR);
    Serial.println(F("  Login: admin / motion1234\n"));
}

// ─── loop ────────────────────────────────────────────────────────────────────
void loop() {
    motorMgr.update();   // jog watchdog
    webMgr.update();     // WS broadcast + DNS + log flush
}
