# Changelog

All notable changes will be documented here.  
Format: [Keep a Changelog](https://keepachangelog.com). Versioning: [SemVer](https://semver.org).

---

## [1.0.0] — 2026-08-08

### Added
- 4-axis stepper control (M1–M4), each on dedicated ESP32 hardware timer
- State machine per motor: IDLE / MOVING / RUNNING / JOGGING / TESTING / ESTOPPED
- Trapezoidal ramp on all angle moves (15% accel + 15% decel)
- **Relative move** — `POST /api/motor/{i}/move` (signed degrees, +CW/−CCW)
- **Absolute Goto** — shortest-path routing, requires zero to be set first
- **Hold-to-jog** — 500 ms watchdog; auto-stops on network drop
- **Continuous rotation** — indefinite CW/CCW at set RPM
- **Repeatability test** — N-cycle ±angle test, per-cycle timing logged to CSV
- **E-Stop per motor + E-Stop All** — hard stop, driver disabled
- **Set Zero** — RAM-only position reference, cleared on reboot (safe-by-default)
- Interactive 360° SVG dial — tap to set Goto target
- Dark and light theme toggle, persisted in localStorage
- Captive portal — auto-redirects on Android/iOS/Windows/macOS WiFi connect
- Single admin session auth (cookie, 4 h sliding TTL)
- Browser time sync → real wall-clock timestamps in CSV log (POST /api/time)
- CSV activity log with 400 KB rotation and backup file
- PWA: installable, offline-capable, home screen icon
- Motor Library page — add/configure/remove motor slots M2–M4
- WebSocket telemetry (read-only, 150 ms active / 800 ms idle)
- REST-only command path (no WS commands, eliminates hallucination)
- GitHub Actions CI (PlatformIO compile check on every push)

---

## [Unreleased]

### Planned
- RMT-based 5th motor axis
- Encoder feedback / closed-loop correction
- Speed sweep test
- OTA firmware update
- GCODE-lite single-line parser
