// ═══════════════════════════════════════════════════════════════════════════════
//  MotionOS — AuthManager.cpp
// ═══════════════════════════════════════════════════════════════════════════════
#include "AuthManager.h"

AuthManager authMgr;

String AuthManager::_randomHex(size_t bytes) {
    String out; out.reserve(bytes * 2);
    const char* h = "0123456789abcdef";
    for (size_t i = 0; i < bytes; i++) {
        uint8_t b = (uint8_t)(esp_random() & 0xFF);
        out += h[(b >> 4) & 0xF];
        out += h[b & 0xF];
    }
    return out;
}

String AuthManager::createSession() {
    String sid = _randomHex(16);
    _sessions[sid] = { millis() + SESSION_TTL_MS };
    return sid;
}

bool AuthManager::validate(const String& sid) {
    cleanup();
    auto it = _sessions.find(sid);
    if (it == _sessions.end()) return false;
    if ((int32_t)(it->second.expiresAt - millis()) <= 0) {
        _sessions.erase(it); return false;
    }
    it->second.expiresAt = millis() + SESSION_TTL_MS; // sliding window
    return true;
}

void AuthManager::destroySession(const String& sid) {
    _sessions.erase(sid);
}

void AuthManager::cleanup() {
    uint32_t now = millis();
    for (auto it = _sessions.begin(); it != _sessions.end(); ) {
        if ((int32_t)(it->second.expiresAt - now) <= 0) it = _sessions.erase(it);
        else ++it;
    }
}
