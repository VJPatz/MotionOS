# MotionOS — Wiring Guide

## ESP32 → TB6600 Per Motor

| Signal | ESP32 GPIO | TB6600 Terminal | Notes |
|--------|-----------|-----------------|-------|
| PUL+   | (see table) | PUL+          | 3.3 V logic OK |
| PUL-   | GND        | PUL-           | Common GND     |
| DIR+   | (see table) | DIR+          | 3.3 V logic OK |
| DIR-   | GND        | DIR-           | Common GND     |
| ENA+   | (see table) | ENA+          | LOW = enabled  |
| ENA-   | GND        | ENA-           | Common GND     |

### GPIO Assignments

| Motor | PUL | DIR | ENA |
|-------|-----|-----|-----|
| M1    | 18  | 19  | 21  |
| M2    | 25  | 26  | 27  |
| M3    | 32  | 33  | 14  |
| M4    | 13  | 12  |  4  |

> **M1 is the primary tested axis.** M2–M4 GPIOs are verified output-capable.

---

## TB6600 Driver Settings

| DIP   | Recommended (NEMA17) | Notes |
|-------|----------------------|-------|
| S1–S3 | Microstep: 1/16      | 3200 steps/rev → match `Config.h` |
| S4–S6 | Current: match motor | Set per motor datasheet |

Typical NEMA17 (42HS40): 1.5–2.0 A current limit.

---

## Power

```
PSU 24V DC ──┬── TB6600 VCC (V+)    ← driver power
             └── ESP32 VIN           ← via barrel jack or AMS1117 reg
GND ──────────── TB6600 GND + ESP32 GND (common ground essential)
```

> Use a **dedicated 24 V PSU** for the motors. Never power drivers from USB alone.

---

## Motor Wiring (NEMA17, 4-wire bipolar)

| TB6600 | NEMA17 |
|--------|--------|
| A+     | Black  |
| A-     | Green  |
| B+     | Red    |
| B-     | Blue   |

Coil pairs vary by manufacturer — check your motor datasheet. If motor vibrates but doesn't rotate, swap A+ and A-.

---

## ENA Polarity

MotionOS uses **ENA+ LOW = enabled** (TB6600 default). Configured in `Config.h`:

```cpp
#define DRIVER_ENA_ACTIVE LOW
#define DRIVER_ENA_IDLE   HIGH
```

If your driver uses the opposite polarity, flip these two defines.

---

## Wiring Checklist

- [ ] Common GND between ESP32 and all TB6600 drivers
- [ ] PSU rated for peak current × number of active motors
- [ ] Twisted pairs for PUL/DIR signals over 30 cm
- [ ] Ferrite bead on ENA signal if motor interference causes resets
- [ ] Capacitor (100 µF / 50 V) across PSU terminals near each driver
