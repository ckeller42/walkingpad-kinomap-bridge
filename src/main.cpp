#include <Arduino.h>
#include <M5Unified.h>

#include <mutex>

#include "belt_gate.h"
#include "belt_link.h"
#include "bridge_state.h"
#include "config.h"
#include "ftms_server.h"
#include "sim_belt.h"
#include "speed_policy.h"
#include "ui.h"

// Both belts are always built; setup() binds one at boot, so the binary that
// passes stage 1 (simulated) is the binary that meets the real belt.
static belt::SimBelt simBelt;
static BeltLink beltLink;
// Every belt write (commands AND the status request) goes through the gate,
// which paces them >= the command interval apart and retries a stop until the
// belt is seen stopped. Created in setup() once the source is chosen.
static belt::BeltGate* gate = nullptr;
#define theBelt (*gate)
static policy::SpeedPolicy speedPolicy;
static policy::BridgeState state;
static FtmsServer bleServer;
static Ui ui;

// BLE callbacks run on the NimBLE task; loop() runs on the Arduino task. The
// callbacks only record requests under mtx; loop() applies them, so no belt
// write ever happens inside a NimBLE host-task callback.
static std::recursive_mutex mtx;
static bool hasTarget = false;
static uint16_t targetCenti = 0;
static bool stopRequested = false;

// Pending start: mode manual -> start -> wait for the belt to actually run ->
// setSpeed. A setSpeed during the countdown (state 7) is discarded by the belt.
// State 6 means "ramping up" only if a countdown was seen first; it also means
// "decelerating", when the belt has refused our start.
static bool startPending = false;
static bool sawCountdown = false;
static uint8_t pendingTenths = 0;
static uint32_t pendingSinceMs = 0;

// Latest belt readings (loop thread only).
static wilink::Status lastStatus{};

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
                // Skip the mode write (and its beep) if already manual.
                if (lastStatus.mode != (uint8_t)wilink::Mode::Manual)
                    theBelt.setMode(wilink::Mode::Manual);
                theBelt.start();
                startPending = true;
                sawCountdown = false;
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

static void driveStart(const wilink::Status& st, uint8_t prevTenths, uint32_t now) {
    if (!startPending) return;
    if (st.beltState == 7) sawCountdown = true;
    // State 6 is also "decelerating". Accept it as ramping up if a countdown
    // was seen, or (the 1 Hz poll can miss the 3 s countdown) speed is rising.
    const bool rising = st.tenthKmh > prevTenths;
    if (st.beltState == 1 || (st.beltState == 6 && (sawCountdown || rising))) {
        theBelt.setSpeed(pendingTenths);
        startPending = false;
    }
}

// Runs every second whether or not a fresh status arrived: a silent belt must
// not keep a start pending (and "running") forever.
static void checkStartTimeout(uint32_t now) {
    if (startPending && now - pendingSinceMs > START_TIMEOUT_MS) {
        // The belt never started (start refused or lost). Don't stay "running".
        startPending = false;
        applyOutcome(state.onBeltObservedStopped());
        Serial.println("start sequence timed out");
    }
}

// A reading is only trusted while recent. Past STATUS_STALE_MS the belt counts
// as down (UI, policy), however connected the link claims to be.
// Age comes from the belt itself (when the frame was actually received), not
// from when poll() last returned true: a cached reading must not look fresh.
static bool haveFresh = false;
static bool beltFresh() { return haveFresh && theBelt.statusAgeMs() <= STATUS_STALE_MS; }
// Do not push a reading to Kinomap that is older than this.
#define NOTIFY_MAX_AGE_MS 2500

static bool lastPhone = false;
static void trackPhone() {
    bool now = bleServer.phoneConnected();
    if (now != lastPhone) {
        lastPhone = now;
        applyOutcome(state.onPhoneLink(now));
    }
}

// The belt link can drop and return (BeltLink); the policy must hear of it.
static bool lastBelt = false;
static void trackBelt() {
    bool now = theBelt.linkUp() && beltFresh();
    if (now != lastBelt) {
        lastBelt = now;
        if (!now) startPending = false;  // a pending start died with the link
        applyOutcome(state.onBeltLink(now));
    }
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);
    ui.begin();

    bleServer.setOnTarget([](uint16_t centi) {
        std::lock_guard<std::recursive_mutex> g(mtx);
        hasTarget = true;  // latest wins; applied by loop()
        targetCenti = centi;
    });
    bleServer.setOnControlOp([](ftms::CpOp op) {
        // Kinomap's {07} in reply to our 2ADA 0x04 is just an acknowledgement.
        Serial.printf("control op 0x%02x\n", (unsigned)op);
        if (op == ftms::CpOp::StopPause) {
            std::lock_guard<std::recursive_mutex> g(mtx);
            stopRequested = true;
        }
    });
    bleServer.begin(BRIDGE_ADV_NAME, BRIDGE_MANUFACTURER, BRIDGE_MODEL);

    // Belt source: config default, inverted if the screen is held during boot.
    M5.update();
    bool sim = USE_SIM_BELT;
    if (M5.Touch.getCount() > 0) sim = !sim;
    Serial.printf("belt source: %s\n", sim ? "SimBelt" : "BeltLink");
    belt::Belt& chosen = sim ? static_cast<belt::Belt&>(simBelt)
                             : static_cast<belt::Belt&>(beltLink);
    ui.setSim(sim);
    gate = new belt::BeltGate(chosen, policy::kMinCommandIntervalMs);
    // BeltLink shares the NimBLE stack, so only start it after bleServer.begin().
    if (!sim && !beltLink.begin()) Serial.println("belt link task failed to start");

    std::lock_guard<std::recursive_mutex> g(mtx);
    ui.render(0, 0, 0, false, false, state.armed());  // boots disarmed
}

void loop() {
    static uint32_t lastTick = 0;
    M5.update();
    ui.poll();

    bool redraw = false;
    uint16_t centi;
    uint32_t dist, elapsed;
    bool beltUp, phoneUp, armed;
    {
        std::lock_guard<std::recursive_mutex> g(mtx);
        trackPhone();
        trackBelt();
        if (ui.tookArmToggle()) {
            applyOutcome(state.setArmed(!state.armed()));
            redraw = true;
        }
        if (ui.tookStop()) {
            applyOutcome(state.stopFromUi());  // also disarms
            redraw = true;
        }
        if (stopRequested) {  // FTMS Stop/Pause: honour it
            stopRequested = false;
            applyOutcome(state.apply({policy::Action::Stop, 0}));
            speedPolicy.reset();
        }
        if (hasTarget) {
            hasTarget = false;
            auto d = speedPolicy.onTarget(targetCenti, millis());
            applyOutcome(state.apply(d));
        }
        uint32_t now = millis();
        theBelt.tick(now);  // paced belt writes
        if (now - lastTick >= 1000) {
            lastTick = now;
            wilink::Status st{};
            if (theBelt.poll(st)) {
                haveFresh = true;
                const uint8_t prevTenths = lastStatus.tenthKmh;
                lastStatus = st;
                speedPolicy.onBeltSpeed(st.tenthKmh);
                driveStart(st, prevTenths, now);
                if (state.beltRunning() && !startPending && st.tenthKmh == 0 &&
                    (st.beltState == 0 || st.beltState == 4))
                    applyOutcome(state.onBeltObservedStopped());
                if (state.notifying() && theBelt.statusAgeMs() <= NOTIFY_MAX_AGE_MS) bleServer.notifySpeed((uint16_t)st.tenthKmh * 10);
            }
            checkStartTimeout(now);
            redraw = true;
        }
        // Never show a stale reading as if it were live.
        const bool fresh = beltFresh();
        centi = fresh ? (uint16_t)lastStatus.tenthKmh * 10 : 0;
        dist = lastStatus.distance10m;
        elapsed = lastStatus.elapsedSec;
        beltUp = theBelt.linkUp() && fresh;
        phoneUp = bleServer.phoneConnected();
        armed = state.armed();
    }
    // Drawing (~150 KB sprite push) happens outside the lock.
    if (redraw) ui.render(centi, dist, elapsed, beltUp, phoneUp, armed);
    delay(20);
}
