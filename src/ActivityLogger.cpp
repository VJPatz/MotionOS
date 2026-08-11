// ═══════════════════════════════════════════════════════════════════════════════
//  MotionOS — ActivityLogger.cpp
// ═══════════════════════════════════════════════════════════════════════════════
#include "ActivityLogger.h"

ActivityLogger actLog;

static const char* CSV_HEADER = "timestamp,uptime,action,motor,details";

// ─── begin ───────────────────────────────────────────────────────────────────
void ActivityLogger::begin() {
    if (!LittleFS.exists(LOG_FILE)) {
        File f = LittleFS.open(LOG_FILE, "w");
        if (f) { f.println(CSV_HEADER); f.close(); }
    }
    Serial.println("[Log] ActivityLogger ready — " LOG_FILE);
}

// ─── setTimeSync ─────────────────────────────────────────────────────────────
void ActivityLogger::setTimeSync(uint64_t epochMs, uint32_t millisAtSync, int16_t tzOffsetMin) {
    _epochMs      = epochMs;
    _millisAtSync = millisAtSync;
    _tzOffsetMin  = tzOffsetMin;
    _timeSynced   = true;
    Serial.printf("[Log] Time synced — epoch=%llu tz=%+d\n", epochMs, tzOffsetMin);
}

// ─── log ─────────────────────────────────────────────────────────────────────
void ActivityLogger::log(const char* action, const char* motor, const String& details) {
    if (!recording) return;
    if (_bufCount >= LOG_BUF_SIZE) _flush();
    Entry& e  = _buf[_bufCount++];
    e.ms      = millis();
    e.action  = action;
    e.motor   = motor;
    e.details = details;
}

// ─── update ──────────────────────────────────────────────────────────────────
void ActivityLogger::update() {
    if (_bufCount == 0) return;
    if (millis() - _lastFlush >= LOG_FLUSH_MS || _bufCount >= LOG_BUF_SIZE)
        _flush();
}

// ─── _flush ──────────────────────────────────────────────────────────────────
void ActivityLogger::_flush() {
    if (_bufCount == 0) return;
    _rotateIfNeeded();

    File f = LittleFS.open(LOG_FILE, "a");
    if (!f) { Serial.println("[Log] ERROR: cannot open log"); _bufCount = 0; return; }

    for (int i = 0; i < _bufCount; i++) {
        Entry& e = _buf[i];

        // Uptime string (always written regardless of sync)
        unsigned long s = e.ms / 1000;
        char uptime[12];
        snprintf(uptime, sizeof(uptime), "%02lu:%02lu:%02lu", s/3600, (s%3600)/60, s%60);

        // Timestamp (wall clock if synced, uptime-relative if not)
        String ts = _getTimestamp();

        f.print(_escapeCsv(ts));      f.print(',');
        f.print(uptime);              f.print(',');
        f.print(_escapeCsv(e.action)); f.print(',');
        f.print(_escapeCsv(e.motor)); f.print(',');
        f.println(_escapeCsv(e.details));
    }

    f.close();
    _bufCount  = 0;
    _lastFlush = millis();
}

// ─── _rotateIfNeeded ─────────────────────────────────────────────────────────
void ActivityLogger::_rotateIfNeeded() {
    File f = LittleFS.open(LOG_FILE, "r");
    if (!f) return;
    size_t sz = f.size(); f.close();
    if (sz < LOG_MAX_BYTES) return;

    Serial.printf("[Log] Rotating (%u bytes)\n", sz);
    if (LittleFS.exists(LOG_BAK_FILE)) LittleFS.remove(LOG_BAK_FILE);
    LittleFS.rename(LOG_FILE, LOG_BAK_FILE);

    File nf = LittleFS.open(LOG_FILE, "w");
    if (nf) { nf.println(CSV_HEADER); nf.close(); }
}

// ─── clearLog ────────────────────────────────────────────────────────────────
bool ActivityLogger::clearLog() {
    _bufCount = 0;
    if (LittleFS.exists(LOG_BAK_FILE)) LittleFS.remove(LOG_BAK_FILE);
    File f = LittleFS.open(LOG_FILE, "w");
    if (!f) return false;
    f.println(CSV_HEADER);
    f.close();
    Serial.println("[Log] Cleared");
    return true;
}

// ─── fileSize ────────────────────────────────────────────────────────────────
size_t ActivityLogger::fileSize() const {
    File f = LittleFS.open(LOG_FILE, "r");
    if (!f) return 0;
    size_t sz = f.size(); f.close();
    return sz;
}

// ─── recentEntriesCSV ────────────────────────────────────────────────────────
//  Returns CSV header + last n data rows as a String.
//  Client JS parses and renders the table.
String ActivityLogger::recentEntriesCSV(int n) {
    const int MAX_N = 200;
    if (n < 1)   n = 1;
    if (n > MAX_N) n = MAX_N;

    File f = LittleFS.open(LOG_FILE, "r");
    if (!f) return String(CSV_HEADER) + "\n";

    // Ring-buffer of last n lines
    String lines[MAX_N];
    int    count = 0;
    bool   first = true;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.isEmpty()) continue;
        if (first) { first = false; continue; }  // skip header
        lines[count % n] = line;
        count++;
    }
    f.close();

    int total = (count < n) ? count : n;
    int start = (count < n) ? 0 : (count % n);

    String out = String(CSV_HEADER) + "\n";
    for (int i = 0; i < total; i++) {
        out += lines[(start + i) % n] + "\n";
    }
    return out;
}

// ─── _getTimestamp ────────────────────────────────────────────────────────────
String ActivityLogger::_getTimestamp() const {
    if (!_timeSynced) {
        uint32_t s = millis() / 1000;
        char buf[14];
        snprintf(buf, sizeof(buf), "T+%02lu:%02lu:%02lu", s/3600, (s%3600)/60, s%60);
        return String(buf);
    }
    uint32_t elapsed = millis() - _millisAtSync;
    uint64_t nowMs   = _epochMs + (uint64_t)elapsed;
    uint64_t nowSec  = nowMs / 1000 + (int64_t)_tzOffsetMin * 60;
    return _epochToDatetime(nowSec);
}

// ─── _epochToDatetime ─────────────────────────────────────────────────────────
String ActivityLogger::_epochToDatetime(uint64_t epochSec) const {
    uint32_t sec  = (uint32_t)(epochSec % 60);
    uint32_t min  = (uint32_t)((epochSec / 60) % 60);
    uint32_t hr   = (uint32_t)((epochSec / 3600) % 24);
    uint64_t days = epochSec / 86400;

    uint32_t year = 1970;
    while (true) {
        bool     leap = (year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0));
        uint32_t diy  = leap ? 366 : 365;
        if (days < diy) break;
        days -= diy;
        year++;
    }

    static const uint8_t dpm[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    bool     leap  = (year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0));
    uint32_t month = 1;
    for (int m = 0; m < 12; m++) {
        uint32_t dim = dpm[m] + (m == 1 && leap ? 1 : 0);
        if (days < dim) { month = m + 1; break; }
        days -= dim;
    }
    uint32_t day = (uint32_t)days + 1;

    char buf[20];
    snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u",
             year, month, day, hr, min, sec);
    return String(buf);
}

// ─── _escapeCsv ───────────────────────────────────────────────────────────────
String ActivityLogger::_escapeCsv(const String& s) const {
    if (s.indexOf(',') < 0 && s.indexOf('"') < 0 && s.indexOf('\n') < 0)
        return s;
    String out = "\"";
    for (unsigned int i = 0; i < s.length(); i++) {
        if (s[i] == '"') out += '"';
        out += s[i];
    }
    out += '"';
    return out;
}
