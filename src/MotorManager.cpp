// ═══════════════════════════════════════════════════════════════════════════════
//  MotionOS — MotorManager.cpp
//
//  ISR   → handles RUNNING + JOGGING (constant speed, timer-driven)
//  Task  → handles MOVING + TESTING  (trapezoidal, Core 1, RTOS)
//  ISR does nothing when mode == MOVING / TESTING / ESTOPPED.
// ═══════════════════════════════════════════════════════════════════════════════
#include "MotorManager.h"
#include "ActivityLogger.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

MotorManager motorMgr;

// ═══════════════════════════════════════════════════════════════════════════════
//  ISR trampolines
// ═══════════════════════════════════════════════════════════════════════════════
static Motor* _isrMotors[MAX_MOTORS] = {};

static void IRAM_ATTR _motorISR(uint8_t idx) {
    Motor* m = _isrMotors[idx];
    if (!m) return;
    portENTER_CRITICAL_ISR(&m->mux);

    MotorMode mode = m->state.mode;
    if ((mode == MotorMode::RUNNING || mode == MotorMode::JOGGING)
            && m->state.enabled
            && m->state.stepIntervalUs > 0) {
        m->stepLevel = !m->stepLevel;
        if (m->stepLevel) {
            // Rising edge = one physical step
            if (m->state.dirCW) m->state.stepPosition++;
            else                m->state.stepPosition--;
        }
        digitalWrite(MOTOR_PIN_MAP[idx].pul, m->stepLevel ? HIGH : LOW);
    } else {
        if (m->stepLevel) {
            m->stepLevel = false;
            digitalWrite(MOTOR_PIN_MAP[idx].pul, LOW);
        }
    }
    portEXIT_CRITICAL_ISR(&m->mux);
}

void IRAM_ATTR _isr0() { _motorISR(0); }
void IRAM_ATTR _isr1() { _motorISR(1); }
void IRAM_ATTR _isr2() { _motorISR(2); }
void IRAM_ATTR _isr3() { _motorISR(3); }
static void (*_isrFuncs[MAX_MOTORS])() = { _isr0, _isr1, _isr2, _isr3 };

// ═══════════════════════════════════════════════════════════════════════════════
//  Internal helper — trapezoidal move, blocks calling task
//  Returns true = completed, false = aborted (stopRequest)
// ═══════════════════════════════════════════════════════════════════════════════
static bool _trapMove(Motor* m, int64_t totalSteps, float rpm, bool cw) {
    if (totalSteps <= 0 || rpm <= 0.0f) return true;

    const MotorPins& pins = MOTOR_PIN_MAP[m->idx];

    m->setDir(cw);
    delayMicroseconds(5);   // DIR propagation delay for TB6600

    float    sps       = (rpm * m->cfg.stepsPerRev) / 60.0f;
    int      runUs     = max((int)MIN_STEP_DELAY_US, (int)(500000.0f / sps));
    int      rampSteps = max(5, (int)(totalSteps * RAMP_FRACTION));
    int      startUs   = min(runUs * 4, (int)MAX_START_DELAY_US);

    for (int64_t i = 0; i < totalSteps; i++) {
        if (m->stopRequest) return false;

        int d;
        if      (i < rampSteps)              d = startUs - (int)((float)(startUs - runUs) * i / rampSteps);
        else if (i >= totalSteps - rampSteps) d = startUs - (int)((float)(startUs - runUs) * (totalSteps - 1 - i) / rampSteps);
        else                                 d = runUs;
        d = max(d, (int)MIN_STEP_DELAY_US);

        // Update step count (task context — ISR is dormant in MOVING/TESTING)
        if (cw) m->state.stepPosition++;
        else    m->state.stepPosition--;

        digitalWrite(pins.pul, HIGH);
        delayMicroseconds(d);
        digitalWrite(pins.pul, LOW);
        delayMicroseconds(d);
    }
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Move task — single angle move (relative or goto)
// ═══════════════════════════════════════════════════════════════════════════════
static void _moveTask(void* arg) {
    auto* a   = static_cast<MoveTaskArg*>(arg);
    Motor* m  = a->motor;
    int64_t steps = a->steps;
    float   rpm   = a->rpm;
    bool    cw    = a->cw;
    delete a;

    bool ok = _trapMove(m, steps, rpm, cw);

    m->state.mode         = MotorMode::IDLE;
    m->state.moveComplete = true;
    m->stopRequest        = false;
    m->moveTask           = nullptr;

    if (ok) actLog.log(LogEvent::MOVE_DONE, m->cfg.label,
                       "steps=" + String((long)steps));

    vTaskDelete(nullptr);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Test task — repeatability N-cycle test
// ═══════════════════════════════════════════════════════════════════════════════
static void _testTask(void* arg) {
    auto* a = static_cast<TestTaskArg*>(arg);
    Motor* m  = a->motor;
    float  angle   = a->angle;
    float  rpm     = a->rpm;
    uint32_t total = a->cycles;
    delete a;

    m->state.testCycles = 0;
    m->setEnable(true);

    int64_t steps = m->degreesToSteps(angle);

    for (uint32_t c = 0; c < total && !m->stopRequest; c++) {
        uint32_t t0 = millis();

        if (!_trapMove(m, steps, rpm, true))  break;
        vTaskDelay(120 / portTICK_PERIOD_MS); // settle
        if (!_trapMove(m, steps, rpm, false)) break;
        vTaskDelay(120 / portTICK_PERIOD_MS);

        m->state.testCycles++;
        uint32_t ms = millis() - t0;
        actLog.log(LogEvent::TEST_CYCLE, m->cfg.label,
                   "c=" + String(m->state.testCycles) + " ms=" + String(ms));
    }

    m->state.testRunning = false;
    m->state.mode        = MotorMode::IDLE;
    m->setEnable(false); // release coils after test
    m->stopRequest       = false;
    m->moveTask          = nullptr;

    actLog.log(LogEvent::TEST_DONE, m->cfg.label,
               "cycles=" + String(m->state.testCycles) + "/" + String(total));
    vTaskDelete(nullptr);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Motor::begin
// ═══════════════════════════════════════════════════════════════════════════════
void Motor::begin() {
    const MotorPins& p = MOTOR_PIN_MAP[idx];

    pinMode(p.pul, OUTPUT); digitalWrite(p.pul, LOW);
    pinMode(p.dir, OUTPUT); digitalWrite(p.dir, HIGH);
    pinMode(p.ena, OUTPUT); digitalWrite(p.ena, DRIVER_ENA_IDLE); // disabled at boot

    _isrMotors[idx] = this;

    timer = timerBegin(MOTOR_TIMER_INDEX[idx], TIMER_PRESCALER, true);
    timerAttachInterrupt(timer, _isrFuncs[idx], true);
    timerAlarmWrite(timer, 100000, true); // idle: 100 ms
    timerAlarmEnable(timer);

    Serial.printf("[Motor] M%d init — PUL:%d DIR:%d ENA:%d Timer:%d\n",
                  idx+1, p.pul, p.dir, p.ena, MOTOR_TIMER_INDEX[idx]);
}

// ─── setEnable ───────────────────────────────────────────────────────────────
void Motor::setEnable(bool en) {
    state.enabled = en;
    digitalWrite(MOTOR_PIN_MAP[idx].ena, en ? DRIVER_ENA_ACTIVE : DRIVER_ENA_IDLE);
}

// ─── setDir ──────────────────────────────────────────────────────────────────
void Motor::setDir(bool cw) {
    state.dirCW = cw;
    digitalWrite(MOTOR_PIN_MAP[idx].dir, cw ? HIGH : LOW);
}

// ─── applySpeedRPM ───────────────────────────────────────────────────────────
void Motor::applySpeedRPM(float rpm) {
    state.commandedRPM = rpm;
    state.appliedRPM   = rpm;
    if (rpm <= 0.0f) {
        state.stepIntervalUs = 0;
        timerAlarmWrite(timer, 100000, true);
        return;
    }
    uint32_t iv = rpmToIntervalUs(rpm);
    state.stepIntervalUs = iv;
    timerAlarmWrite(timer, iv, true);
}

// ─── _launchMoveTask ─────────────────────────────────────────────────────────
void Motor::_launchMoveTask(int64_t steps, float rpm, bool cw) {
    if (state.mode != MotorMode::IDLE) stopMotion();
    setEnable(true);
    state.mode         = MotorMode::MOVING;
    state.moveComplete = false;
    stopRequest        = false;
    timerAlarmWrite(timer, 100000, true); // ISR idle while task runs

    auto* arg = new MoveTaskArg{this, steps, rpm, cw};
    char  name[14]; snprintf(name, sizeof(name), "move_m%d", idx+1);
    xTaskCreatePinnedToCore(_moveTask, name, 4096, arg, 3, &moveTask, 1);
}

// ─── startMove (relative) ────────────────────────────────────────────────────
void Motor::startMove(float degrees, float rpm) {
    if (state.mode == MotorMode::ESTOPPED) return;
    bool    cw    = degrees >= 0.0f;
    int64_t steps = degreesToSteps(degrees);
    if (steps == 0) return;
    actLog.log(LogEvent::MOVE_START, cfg.label,
               "deg=" + String(degrees,1) + " rpm=" + String(rpm,1));
    _launchMoveTask(steps, rpm, cw);
}

// ─── startGoto (absolute, shortest path) ────────────────────────────────────
void Motor::startGoto(float targetDeg, float rpm) {
    if (state.mode == MotorMode::ESTOPPED || !state.zeroSet) return;

    while (targetDeg <    0) targetDeg += 360.0f;
    while (targetDeg >= 360) targetDeg -= 360.0f;

    float delta = targetDeg - currentAngle();
    if (delta >  180.0f) delta -= 360.0f;
    if (delta < -180.0f) delta += 360.0f;
    if (fabsf(delta) < 0.5f) return;

    bool    cw    = delta > 0;
    int64_t steps = degreesToSteps(delta);
    if (steps == 0) return;

    actLog.log(LogEvent::GOTO_START, cfg.label,
               "target=" + String(targetDeg,1) + " rpm=" + String(rpm,1));
    _launchMoveTask(steps, rpm, cw);
}

// ─── startContinuous ─────────────────────────────────────────────────────────
void Motor::startContinuous(float rpm, bool cw) {
    if (state.mode == MotorMode::ESTOPPED) return;
    if (state.mode != MotorMode::IDLE) stopMotion();
    stopRequest = false;
    setDir(cw);
    setEnable(true);
    state.mode = MotorMode::RUNNING;
    applySpeedRPM(rpm);
    actLog.log(LogEvent::RUN_START, cfg.label,
               "rpm=" + String(rpm,1) + " dir=" + (cw?"CW":"CCW"));
}

// ─── startJog ────────────────────────────────────────────────────────────────
void Motor::startJog(bool cw, float rpm) {
    if (state.mode == MotorMode::ESTOPPED) return;
    if (state.mode != MotorMode::IDLE && state.mode != MotorMode::JOGGING) return;
    stopRequest = false;
    setDir(cw);
    setEnable(true);
    state.mode = MotorMode::JOGGING;
    state.jogLastHeartbeat = millis();
    applySpeedRPM(rpm);
    actLog.log(LogEvent::JOG_START, cfg.label, String(cw?"CW":"CCW"));
}

void Motor::updateJogHeartbeat() {
    state.jogLastHeartbeat = millis();
}

void Motor::stopJog() {
    timerAlarmWrite(timer, 100000, true);
    state.stepIntervalUs = 0;
    state.commandedRPM   = 0;
    state.appliedRPM     = 0;
    state.mode           = MotorMode::IDLE;
    actLog.log(LogEvent::JOG_STOP, cfg.label, "");
}

// ─── stopMotion ──────────────────────────────────────────────────────────────
void Motor::stopMotion() {
    stopRequest = true;
    // Wait for any task to exit (max 2 s)
    uint32_t deadline = millis() + 2000;
    while (moveTask != nullptr && millis() < deadline)
        vTaskDelay(10 / portTICK_PERIOD_MS);

    timerAlarmWrite(timer, 100000, true);
    state.stepIntervalUs = 0;
    state.commandedRPM   = 0;
    state.appliedRPM     = 0;
    state.mode           = MotorMode::IDLE;
    stopRequest          = false;
    actLog.log(LogEvent::RUN_STOP, cfg.label, "");
}

// ─── eStop ───────────────────────────────────────────────────────────────────
void Motor::eStop() {
    stopRequest = true;                          // signals task to exit
    timerAlarmWrite(timer, 100000, true);        // silence ISR immediately
    state.stepIntervalUs = 0;
    state.commandedRPM   = 0;
    state.appliedRPM     = 0;
    setEnable(false);                            // release coils
    state.mode = MotorMode::ESTOPPED;
    actLog.log(LogEvent::ESTOP, cfg.label, "");
}

// ─── setZero ─────────────────────────────────────────────────────────────────
void Motor::setZero() {
    state.zeroOffset = state.stepPosition;
    state.zeroSet    = true;
    actLog.log(LogEvent::ZERO_SET, cfg.label,
               "pos=" + String((long)state.stepPosition));
}

// ─── currentAngle ────────────────────────────────────────────────────────────
float Motor::currentAngle() const {
    if (!state.zeroSet) return 0.0f;
    int64_t rel = state.stepPosition - state.zeroOffset;
    float   deg = (float)(rel % (int64_t)cfg.stepsPerRev) / cfg.stepsPerRev * 360.0f;
    if (deg < 0.0f) deg += 360.0f;
    return deg;
}

// ─── startTest ───────────────────────────────────────────────────────────────
void Motor::startTest(float angle, float rpm, uint32_t cycles) {
    if (state.mode == MotorMode::ESTOPPED) return;
    if (state.mode != MotorMode::IDLE) stopMotion();

    state.mode           = MotorMode::TESTING;
    state.testRunning    = true;
    state.testCycles     = 0;
    state.testTargetCycles = cycles;
    state.testAngle      = angle;
    state.testRpm        = rpm;
    stopRequest          = false;

    timerAlarmWrite(timer, 100000, true);

    auto* arg = new TestTaskArg{this, angle, rpm, cycles};
    char  name[14]; snprintf(name, sizeof(name), "test_m%d", idx+1);
    xTaskCreatePinnedToCore(_testTask, name, 6144, arg, 3, &moveTask, 1);
    actLog.log(LogEvent::TEST_START, cfg.label,
               "angle=" + String(angle,1) + " rpm=" + String(rpm,1) + " n=" + String(cycles));
}

void Motor::stopTest() {
    stopRequest = true;
    actLog.log(LogEvent::TEST_STOP, cfg.label, "aborted");
}

// ═══════════════════════════════════════════════════════════════════════════════
//  MotorManager
// ═══════════════════════════════════════════════════════════════════════════════

void MotorManager::begin() {
    for (int i = 0; i < MAX_MOTORS; i++) {
        Motor& m = motors[i];
        m.idx = i;
        snprintf(m.cfg.label,       sizeof(m.cfg.label),       "M%d", i+1);
        m.cfg.motorModel[0]  = '\0';   // filled by user via Motor Library
        m.cfg.driverModel[0] = '\0';
        m.cfg.stepsPerRev = DEFAULT_STEPS_PER_REV;
        m.cfg.currentA    = 1.5f;
        m.cfg.active      = (i == 0); // M1 always on; others added via UI

        m.state.mode             = MotorMode::IDLE;
        m.state.enabled          = false;
        m.state.dirCW            = true;
        m.state.stepPosition     = 0;
        m.state.zeroOffset       = 0;
        m.state.zeroSet          = false;
        m.state.commandedRPM     = 0;
        m.state.appliedRPM       = 0;
        m.state.stepIntervalUs   = 0;
        m.state.jogLastHeartbeat = 0;
        m.state.moveComplete     = false;
        m.state.testRunning      = false;
        m.state.testCycles       = 0;

        m.begin();
    }
    loadConfig();
}

// ─── update — jog watchdog ───────────────────────────────────────────────────
void MotorManager::update() {
    uint32_t now = millis();
    for (int i = 0; i < MAX_MOTORS; i++) {
        Motor& m = motors[i];
        if (!m.cfg.active) continue;
        if (m.state.mode == MotorMode::JOGGING &&
            (now - m.state.jogLastHeartbeat) > JOG_WATCHDOG_MS) {
            Serial.printf("[Motor] M%d jog watchdog\n", i+1);
            m.stopJog();
        }
    }
}

// ─── loadConfig ──────────────────────────────────────────────────────────────
void MotorManager::loadConfig() {
    File f = LittleFS.open(CONFIG_PATH, "r");
    if (!f) { Serial.println("[Cfg] No config.json — using defaults"); return; }

    JsonDocument doc;
    if (deserializeJson(doc, f) != DeserializationError::Ok)
        { f.close(); Serial.println("[Cfg] Parse error"); return; }
    f.close();

    JsonArray arr = doc["motors"].as<JsonArray>();
    if (!arr) return;
    int i = 0;
    for (JsonObject mo : arr) {
        if (i >= MAX_MOTORS) break;
        Motor& m = motors[i++];
        strncpy(m.cfg.label,       mo["label"]   | m.cfg.label,       sizeof(m.cfg.label)-1);
        strncpy(m.cfg.motorModel,  mo["motor"]   | m.cfg.motorModel,  sizeof(m.cfg.motorModel)-1);
        strncpy(m.cfg.driverModel, mo["driver"]  | m.cfg.driverModel, sizeof(m.cfg.driverModel)-1);
        m.cfg.stepsPerRev = mo["steps"]   | (int)DEFAULT_STEPS_PER_REV;
        m.cfg.currentA    = mo["current"] | 1.5f;
        m.cfg.active      = mo["active"]  | (i == 1);
    }
    Serial.printf("[Cfg] Loaded %d motor configs\n", i);
}

// ─── saveConfig ──────────────────────────────────────────────────────────────
void MotorManager::saveConfig() {
    JsonDocument doc;
    JsonArray arr = doc["motors"].to<JsonArray>();
    for (int i = 0; i < MAX_MOTORS; i++) {
        Motor& m   = motors[i];
        JsonObject mo = arr.add<JsonObject>();
        mo["label"]   = m.cfg.label;
        mo["motor"]   = m.cfg.motorModel;
        mo["driver"]  = m.cfg.driverModel;
        mo["steps"]   = (int)m.cfg.stepsPerRev;
        mo["current"] = m.cfg.currentA;
        mo["active"]  = m.cfg.active;
    }
    File f = LittleFS.open(CONFIG_PATH, "w");
    if (!f) { Serial.println("[Cfg] ERROR writing config"); return; }
    serializeJson(doc, f);
    f.close();
    Serial.println("[Cfg] Saved config.json");
}