#include <Arduino.h>
#include <M5Unified.h>

#include <mutex>

#include "config.h"  // must precede speed_policy.h
#include "bridge_state.h"
#include "ftms_server.h"
#include "sim_belt.h"
#include "speed_policy.h"
#include "ui.h"

static belt::SimBelt simBelt;
static belt::Belt& theBelt = simBelt;
static policy::SpeedPolicy speedPolicy;
static policy::BridgeState state;
static FtmsServer bleServer;
static Ui ui;

// BLE callbacks run on the NimBLE task; loop() runs on the Arduino task.
static std::recursive_mutex mtx;

// Pending start: mode manual -> start -> wait for beltState 1/6 -> setSpeed.
// A setSpeed during the countdown (state 7) is discarded by the belt.
static bool startPending = false;
static uint8_t pendingTenths = 0;
static uint32_t pendingSinceMs = 0;

static void applyOutcome(const policy::Outcome& o) {
    for (size_t i = 0; i < o.count; ++i) {
        switch (o.emits[i]) {
            case policy::Emit::SetSpeed:
                if (startPending) pendingTenths = o.tenthKmh;  // not running yet
                else theBelt.setSpeed(o.tenthKmh);
                break;
            case policy::Emit::Stop:
                startPending = false;
                theBelt.stop();
                break;
            case policy::Emit::StartSequence:
                theBelt.setMode(wilink::Mode::Manual);
                theBelt.start();
                startPending = true;
                pendingTenths = o.tenthKmh;
                pendingSinceMs = millis();
                break;
            case policy::Emit::NotifyStarted:
                bleServer.notifyStarted();
                break;
            case policy::Emit::ResetPolicy:
                speedPolicy.reset();
                break;
            case policy::Emit::None:
                break;
        }
    }
}

static void driveStart(const wilink::Status& st, uint32_t now) {
    if (!startPending) return;
    if (st.beltState == 1 || st.beltState == 6) {
        theBelt.setSpeed(pendingTenths);
        startPending = false;
    } else if (now - pendingSinceMs > START_TIMEOUT_MS) {
        startPending = false;
        speedPolicy.reset();  // belt never started; let the next target retry
        Serial.println("start sequence timed out");
    }
}

// Latest belt readings, so the UI can be redrawn immediately after a tap.
static wilink::Status lastStatus{};

static void renderUi() {
    ui.render((uint16_t)lastStatus.tenthKmh * 10, lastStatus.distance10m, lastStatus.elapsedSec,
              theBelt.linkUp(), bleServer.phoneConnected(), state.armed());
}

static bool lastPhone = false;
static void trackPhone() {
    bool now = bleServer.phoneConnected();
    if (now != lastPhone) {
        lastPhone = now;
        applyOutcome(state.onPhoneLink(now));
    }
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);
    ui.begin();

    bleServer.setOnTarget([](uint16_t centi) {
        std::lock_guard<std::recursive_mutex> g(mtx);
        auto d = speedPolicy.onTarget(centi, millis());
        applyOutcome(state.apply(d));
    });
    bleServer.setOnControlOp([](ftms::CpOp op) {
        // Kinomap's {07} in reply to our 2ADA 0x04 is just an acknowledgement.
        Serial.printf("control op 0x%02x\n", (unsigned)op);
    });
    bleServer.begin(BRIDGE_ADV_NAME);

    std::lock_guard<std::recursive_mutex> g(mtx);
    applyOutcome(state.onBeltLink(true));
    renderUi();  // boots disarmed; ARM is a deliberate tap
}

void loop() {
    static uint32_t lastTick = 0;
    M5.update();
    ui.poll();
    {
        std::lock_guard<std::recursive_mutex> g(mtx);
        trackPhone();
        bool redraw = false;
        if (ui.tookArmToggle()) {
            applyOutcome(state.setArmed(!state.armed()));
            redraw = true;
        }
        if (ui.tookStop()) {
            applyOutcome(state.stopFromUi());  // also disarms
            redraw = true;
        }
        uint32_t now = millis();
        if (now - lastTick >= 1000) {
            lastTick = now;
            simBelt.tick(now);
            wilink::Status st{};
            if (theBelt.poll(st)) {
                lastStatus = st;
                speedPolicy.onBeltSpeed(st.tenthKmh);
                driveStart(st, now);
                if (state.notifying()) bleServer.notifySpeed((uint16_t)st.tenthKmh * 10);
            }
            redraw = true;
        }
        if (redraw) renderUi();
    }
    delay(20);
}
