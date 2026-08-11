# Contributing to MotionOS

## Quick setup

```bash
git clone https://github.com/yourname/MotionOS
cd MotionOS
pip install platformio
pio run            # verify compile
```

## Branch strategy

- `main` — stable releases only
- `dev`  — integration branch
- Feature branches: `feat/your-feature`
- Bug branches: `fix/issue-number`

## Code style

- **C++**: follow existing style — 4-space indent, `camelCase` methods, `PascalCase` classes
- **HTML/JS**: 2-space indent, `camelCase` functions
- No external JS frameworks — vanilla only (keeps LittleFS footprint small)

## Testing checklist before PR

- [ ] `pio run` passes zero errors, zero new warnings
- [ ] Flashed to actual ESP32 hardware
- [ ] Tested on mobile browser (Chrome/Safari)
- [ ] Motors move correctly, position tracking correct
- [ ] E-Stop works immediately from all states
- [ ] Jog watchdog fires correctly (disconnect WiFi mid-jog)
- [ ] Log CSV parses correctly in spreadsheet

## Pin changes

If you modify `MOTOR_PIN_MAP` in `Config.h`, document:
- Why the new pins are safe (no strapping/flash/input-only conflict)
- Which hardware you tested on

## Firmware size budget

LittleFS partition: 1.5 MB  
Keep `data/` total under 800 KB to leave headroom for logs.  
Current footprint: ~90 KB.

## Issues

File issues with the template. Include serial output and browser version.
