#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
//  MotionOS — AuthManager.h
//  Single-admin cookie-session auth. Sessions expire after SESSION_TTL_MS.
//  SID is a 32-char random hex string generated from esp_random().
// ═══════════════════════════════════════════════════════════════════════════════
#include <Arduino.h>
#include <map>
#include "Config.h"

class AuthManager {
public:
    String createSession();
    bool   validate(const String& sid);     // also refreshes TTL
    void   destroySession(const String& sid);
    void   cleanup();                       // evict expired sessions; call from loop

private:
    struct Session { uint32_t expiresAt; };
    std::map<String, Session> _sessions;
    String _randomHex(size_t bytes);
};

extern AuthManager authMgr;
