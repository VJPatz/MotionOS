#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
//  MotionOS — MotorManager.h
//  State machine per motor:
//
//    IDLE ──────► MOVING   (angle / goto — trapezoidal RTOS task)
//    IDLE ──────► RUNNING  (continuous rotation — hw timer ISR)
//    IDLE ──────► JOGGING  (hold-to-jog — hw timer ISR + WS watchdog)
//    IDLE ──────► TESTING  (repeatability test — RTOS task)
//    any  ──────► ESTOPPED (hard stop, driver disabled)
//    ESTOPPED ──► IDLE     (only via re-enable toggle from UI)
//
//  ISR handles RUNNING + JOGGING (constant speed, timer-driven).
//  RTOS task on Core 1 handles MOVING + TESTING (variable-speed trapezoid).
//  ISR does nothing when mode == MOVING / TESTING / ESTOPPED.
// ═══════════════════════════════════════════════════════════════════════════════
#include <Arduino.h>
#include "Config.h"

enum class MotorMode : uint8_t {
    IDLE     = 0,
    MOVING   = 1,   // executing angle / goto move
    RUNNING  = 2,   // continuous rotation
    JOGGING  = 3,   // hold-to-jog
    TESTING  = 4,   // repeatability test
    ESTOPPED = 5,
};

// ── Per-motor persistent configuration (saved to LittleFS) ────────────────────
struct MotorConfig {
    char     label[16];       // "M1" … "M4"
    char     motorModel[32];  // "NEMA17" | "NEMA23" | "NEMA34" | "Custom"
    char     driverModel[32]; // "TB6600" | "DM542"  | "A4988"  | "DRV8825" | "Custom"
    uint32_t stepsPerRev;     // 200 – 6400
    float    currentA;        // informational; set on driver hardware
    bool     active;          // false = slot unused (not shown in UI)
};

// ── Per-motor live state ───────────────────────────────────────────────────────
struct MotorState {
    MotorMode mode;
    bool      enabled;      // driver energised
    bool      dirCW;        // current direction

    // Position (step-level; derived angle = (pos - zero) / spr × 360°)
    volatile int64_t stepPosition;   // absolute step counter (updated in ISR or task)
    int64_t          zeroOffset;     // stepPosition value at user-set zero
    bool             zeroSet;        // false until user taps Set Zero

    // Speed
    float    commandedRPM;
    float    appliedRPM;
    uint32_t stepIntervalUs;         // half-period fed to timer alarm

    // Jog watchdog
    uint32_t jogLastHeartbeat;

    // Move / task state
    volatile bool moveComplete;

    // Repeatability test
    bool     testRunning;
    uint32_t testCycles;
    uint32_t testTargetCycles;
    float    testAngle;
    float    testRpm;
};

// ── Task argument structs (heap-allocated, deleted inside task) ────────────────
struct MoveTaskArg { class Motor* motor; int64_t steps; float rpm; bool cw; };
struct TestTaskArg { class Motor* motor; float angle; float rpm; uint32_t cycles; };

// ── Motor object ──────────────────────────────────────────────────────────────
class Motor {
public:
    MotorConfig  cfg;
    MotorState   state;
    uint8_t      idx;           // 0-based

    // ISR-shared
    volatile bool  stepLevel   = false;
    volatile bool  stopRequest = false;

    hw_timer_t*    timer    = nullptr;
    TaskHandle_t   moveTask = nullptr;
    portMUX_TYPE   mux      = portMUX_INITIALIZER_UNLOCKED;

    // ── Init ─────────────────────────────────────────────────────────────────
    void begin();

    // ── Driver / direction ────────────────────────────────────────────────────
    void setEnable(bool en);
    void setDir(bool cw);

    // ── ISR speed (RUNNING / JOGGING only) ───────────────────────────────────
    void applySpeedRPM(float rpm);

    // ── Motion commands ───────────────────────────────────────────────────────
    void startMove(float degrees, float rpm);        // relative; +deg = CW
    void startGoto(float targetDeg, float rpm);      // absolute from zero (shortest path)
    void startContinuous(float rpm, bool cw);
    void startJog(bool cw, float rpm);
    void updateJogHeartbeat();
    void stopJog();
    void stopMotion();   // graceful stop (waits for task; kills ISR)
    void eStop();        // immediate, disables driver

    // ── Zero / position ───────────────────────────────────────────────────────
    void  setZero();
    float currentAngle() const;     // 0–360° relative to zero; 0 if zero not set

    // ── Test ─────────────────────────────────────────────────────────────────
    void startTest(float angle, float rpm, uint32_t cycles);
    void stopTest();

    // ── Helpers ───────────────────────────────────────────────────────────────
    uint32_t rpmToIntervalUs(float rpm) const {
        if (rpm <= 0.0f) return 0;
        float sps = (rpm * cfg.stepsPerRev) / 60.0f;
        float iv  = 500000.0f / sps;          // half-period in µs
        return (uint32_t)constrain(iv, (float)MIN_STEP_DELAY_US, 500000.0f);
    }
    int64_t degreesToSteps(float deg) const {
        return (int64_t)(fabsf(deg) / 360.0f * cfg.stepsPerRev + 0.5f);
    }

private:
    void _launchMoveTask(int64_t steps, float rpm, bool cw);
};

// ── Manager singleton ─────────────────────────────────────────────────────────
class MotorManager {
public:
    Motor motors[MAX_MOTORS];

    void   begin();
    void   update();          // call from loop() — jog watchdog
    void   loadConfig();
    void   saveConfig();

    Motor* get(int idx) {
        return (idx >= 0 && idx < MAX_MOTORS) ? &motors[idx] : nullptr;
    }
};

extern MotorManager motorMgr;
