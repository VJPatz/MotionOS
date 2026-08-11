# MotionOS

**ESP32-based 4-axis stepper motor controller with a mobile PWA interface.**  
No app. No cloud. Connect to WiFi, open browser, done.

---

## Demo

📹 *Video coming soon*  
<!-- Replace the line below with your YouTube link when ready -->
<!-- [![Watch the demo](https://img.youtube.com/vi/YOUR_VIDEO_ID/0.jpg)](https://www.youtube.com/watch?v=YOUR_VIDEO_ID) -->

---

## What it does

- Control up to 4 stepper motors independently from your phone or laptop
- Move by angle, go to absolute position, jog, or run continuously
- Trapezoidal speed ramp on all moves — no missed steps
- E-Stop per motor or all axes at once
- Repeatability test with cycle logging to CSV
- Jog watchdog — motor stops automatically if connection drops
- PWA installable — works offline once loaded, home screen icon

---

## Hardware

| | |
|---|---|
| **Board** | ESP32 (38-pin) |
| **Driver** | Step/Dir driver |
| **Interface** | Any browser on the same WiFi |

| Motor | PUL | DIR | ENA |
|-------|-----|-----|-----|
| M1 | 18 | 19 | 21 |
| M2 | 25 | 26 | 27 |
| M3 | 32 | 33 | 14 |
| M4 | 13 | 12 | 4 |

Full wiring guide: [`docs/wiring.md`](docs/wiring.md)

---

## Setup

```bash
# 1. Install PlatformIO
pip install platformio

# 2. Clone
git clone https://github.com/yourname/MotionOS
cd MotionOS

# 3. Set your credentials
#    Open include/Config.h — change AP_PASS and ADMIN_PASS before flashing

# 4. Flash firmware
pio run --target upload

# 5. Flash web files
pio run --target uploadfs

# 6. Connect to WiFi "MotionOS" → browser opens automatically
```

---

## First use

1. Power on → coils are off by default
2. Rotate shaft manually to your home position
3. Enable motor via toggle
4. Tap **Set Zero**
5. Now all angle commands are relative to that zero

Zero is RAM-only by design — a stale saved zero after manual movement would cause wrong positioning on physical hardware.

---

## Configure motors

Go to **Motor Library** in the app. Enter your motor model, driver, and steps/rev — these become labels throughout the UI.

---

## License

GPL-3.0 — open source, derivative works must stay open.  
See [LICENSE](LICENSE).
