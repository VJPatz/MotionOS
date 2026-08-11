#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
//  MotionOS — ActivityLogger.h
//  Buffered CSV logger to LittleFS.
//  Timestamps are real wall-clock time once the browser syncs via POST /api/time.
//  Until synced, timestamps are uptime-relative (T+HH:MM:SS).
// ═══════════════════════════════════════════════════════════════════════════════
#include <Arduino.h>
#include <LittleFS.h>
#include "Config.h"

// ── Log event string constants ─────────────────────────────────────────────────
namespace LogEvent {
    constexpr const char* LOGIN         = "login";
    constexpr const char* LOGOUT        = "logout";
    constexpr const char* MOVE_START    = "move_start";
    constexpr const char* MOVE_DONE     = "move_done";
    constexpr const char* GOTO_START    = "goto_start";
    constexpr const char* GOTO_DONE     = "goto_done";
    constexpr const char* RUN_START     = "run_start";
    constexpr const char* RUN_STOP      = "run_stop";
    constexpr const char* JOG_START     = "jog_start";
    constexpr const char* JOG_STOP      = "jog_stop";
    constexpr const char* ESTOP         = "estop";
    constexpr const char* ZERO_SET      = "zero_set";
    constexpr const char* ENA_ON        = "driver_on";
    constexpr const char* ENA_OFF       = "driver_off";
    constexpr const char* TEST_START    = "test_start";
    constexpr const char* TEST_CYCLE    = "test_cycle";
    constexpr const char* TEST_DONE     = "test_done";
    constexpr const char* TEST_STOP     = "test_stop";
    constexpr const char* CONFIG_SAVED  = "config_saved";
    constexpr const char* MOTOR_ADDED   = "motor_added";
    constexpr const char* MOTOR_REMOVED = "motor_removed";
    constexpr const char* LOG_CLEARED   = "log_cleared";
    constexpr const char* RECORD_START  = "record_start";
    constexpr const char* RECORD_STOP   = "record_stop";
    constexpr const char* TIME_SYNC     = "time_sync";
}

class ActivityLogger {
public:
    bool recording = false;     // toggled by UI; when false, log() is a no-op

    void   begin();
    void   update();            // call from loop() — flushes buffer

    // action + optional motor label + details
    void   log(const char* action, const char* motor = "", const String& details = "");

    // Browser time sync (called by WebServer on POST /api/time)
    void   setTimeSync(uint64_t epochMs, uint32_t millisAtSync, int16_t tzOffsetMin);
    bool   isTimeSynced() const { return _timeSynced; }

    // Log management
    bool   clearLog();
    size_t fileSize() const;

    // Returns CSV text: header + last n data rows (for /api/log)
    String recentEntriesCSV(int n = 100);

private:
    struct Entry {
        unsigned long ms;
        String        action;
        String        motor;
        String        details;
    };

    Entry         _buf[LOG_BUF_SIZE];
    int           _bufCount  = 0;
    unsigned long _lastFlush = 0;

    // Time sync state
    bool     _timeSynced     = false;
    uint64_t _epochMs        = 0;
    uint32_t _millisAtSync   = 0;
    int16_t  _tzOffsetMin    = 0;

    String _getTimestamp() const;
    String _epochToDatetime(uint64_t epochSec) const;
    void   _flush();
    void   _rotateIfNeeded();
    String _escapeCsv(const String& s) const;
};

extern ActivityLogger actLog;
