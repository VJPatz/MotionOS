# MotionOS — REST API Reference

Base URL: `http://192.168.4.1`  
Auth: cookie `SID` set on login. All `/api/*` routes require it.  
Command pattern: **HTTP POST only** — no commands via WebSocket.

---

## Authentication

### `POST /login`
```json
{ "u": "admin", "p": "motion1234" }
```
Returns `Set-Cookie: SID=...` on success. Browser time is also synced via `POST /api/time` by the login page automatically.

### `POST /logout`
Invalidates the session cookie.

---

## Motor Control

Motor index `{i}` is 0–3.

### `POST /api/motor/{i}/move`
Relative move from current position.
```json
{ "angle": 90.0, "rpm": 30 }
```
- `angle`: degrees; positive = CW, negative = CCW
- `rpm`: 1–200

### `POST /api/motor/{i}/goto`
Absolute move to target angle. Motor must have zero set. Shortest-path routing.
```json
{ "angle": 90.0, "rpm": 30 }
```
- `angle`: 0–360 degrees from zero

### `POST /api/motor/{i}/run`
Start continuous rotation.
```json
{ "rpm": 60, "dir": 1 }
```
- `dir`: 1 = CW, 0 = CCW

### `POST /api/motor/{i}/jog`
Hold-to-jog. Browser sends heartbeat every 200 ms while held.
```json
{ "action": "start",     "dir": 1, "rpm": 10 }
{ "action": "heartbeat" }
{ "action": "stop" }
```
Motor auto-stops if no heartbeat within 500 ms (configurable via `JOG_WATCHDOG_MS` in `Config.h`).

### `POST /api/motor/{i}/stop`
Graceful stop (waits for any running task to exit).

### `POST /api/motor/{i}/estop`
Hard stop + disable driver. Motor moves to `ESTOPPED` state.  
Re-enable via `POST /api/motor/{i}/ena { "enabled": true }`.

### `POST /api/motor/{i}/zero`
Sets current shaft position as 0°. Stored in RAM only — clears on reboot.

### `POST /api/motor/{i}/ena`
Enable/disable driver (coils energised).
```json
{ "enabled": true }
```

### `POST /api/motor/{i}/test`
Repeatability test: N cycles of ±angle° moves with timing logged per cycle.
```json
{ "action": "start", "angle": 90, "rpm": 30, "cycles": 20 }
{ "action": "stop" }
```

### `POST /api/estop_all`
Hard-stops all active motors simultaneously.

---

## Configuration

### `GET /api/config`
Returns same JSON as `/api/status`.

### `POST /api/config/{i}`
Update motor slot configuration (persisted to LittleFS).
```json
{ "label": "X-axis", "motor": "NEMA23", "driver": "DM542", "steps": 1600, "current": 2.5, "active": true }
```

### `POST /api/config/{i}/remove`
Deactivates motor slot (M1 cannot be removed). Stops motor first.

---

## Activity Log

### `GET /api/log?n=200`
Returns `text/csv`: header + last n entries.  
Default n = 100, max = 200.

### `GET /api/log/download`
Downloads the full `activity.csv` as an attachment.

### `POST /api/log/clear`
Wipes all log rows (keeps header).

### `POST /api/log/record`
Toggle recording.
```json
{ "recording": true }
```
When `false`, `log()` calls are no-ops.

---

## System

### `GET /api/status`
Full telemetry snapshot (same data as WebSocket push):
```json
{
  "motors": [
    {
      "idx": 0, "label": "M1", "motor": "NEMA17", "driver": "TB6600",
      "steps": 3200, "current": 1.5, "active": true,
      "enabled": true, "mode": 0, "dir": 1,
      "rpm": 0.0, "angle": 47.3, "zero_set": true,
      "test_running": false, "test_cycles": 0, "test_target": 0
    }
  ],
  "recording": false,
  "log_size": 4096,
  "time_synced": true
}
```

**Mode values:** 0=IDLE, 1=MOVING, 2=RUNNING, 3=JOGGING, 4=TESTING, 5=ESTOPPED

### `POST /api/time`
Sync browser time (called automatically by login page).
```json
{ "epoch": 1723045931000, "tzOffset": 120 }
```
- `epoch`: `Date.now()` (UTC ms since epoch)
- `tzOffset`: `-(new Date().getTimezoneOffset())` in minutes

---

## WebSocket `/ws`

Read-only telemetry stream. The firmware pushes `_buildTelemetry()` JSON:
- Every **150 ms** when any motor is active
- Every **800 ms** at idle

**No commands are accepted via WebSocket.** All motor commands use HTTP POST.

---

## Error Responses

```json
{ "error": "motor not found" }
{ "error": "zero not set" }
{ "error": "M1 cannot be removed" }
{ "error": "bad json" }
```

HTTP 401 → redirect to `/login`.
