#include <Arduino.h>
#include <M5Unified.h>
#include <Preferences.h>

#include <mutex>

#include "belt_gate.h"
#include "belt_link.h"
#include "bridge_state.h"
#include "config.h"
#include "console_cmd.h"
#include "ftms_server.h"
#include "logsink.h"
#include "sim_belt.h"
#include "speed_policy.h"
#include "ui.h"
#include "wizard.h"

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

// Speed last commanded through applyOutcome: the base for +/- steps. The belt
// reading lags a command by a poll or two, so rapid presses must not step
// from it.
static uint8_t lastCmdTenths = 0;
static uint32_t lastCmdMs = 0;
static constexpr uint32_t kCmdBaseMs = 3000;

static const char* emitName(policy::Emit e) {
    switch (e) {
        case policy::Emit::SetSpeed:      return "SetSpeed";
        case policy::Emit::Stop:          return "Stop";
        case policy::Emit::StartSequence: return "StartSequence";
        case policy::Emit::NotifyStarted: return "NotifyStarted";
        case policy::Emit::ResetPolicy:   return "ResetPolicy";
        case policy::Emit::SyncPolicy:    return "SyncPolicy";
        case policy::Emit::None:          return "None";
    }
    return "?";
}

static void applyOutcome(const policy::Outcome& o) {
    for (size_t i = 0; i < o.count; ++i) {
        if (o.emits[i] != policy::Emit::None)
            logsink::line("emit %s tenths=%u", emitName(o.emits[i]), (unsigned)o.tenthKmh);
        switch (o.emits[i]) {
            case policy::Emit::SetSpeed:
                lastCmdTenths = o.tenthKmh; lastCmdMs = millis();
                if (startPending) pendingTenths = o.tenthKmh;  // not running yet
                else theBelt.setSpeed(o.tenthKmh);
                break;
            case policy::Emit::Stop:
                startPending = false;
                lastCmdTenths = 0;
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
                lastCmdTenths = o.tenthKmh; lastCmdMs = pendingSinceMs;
                break;
            case policy::Emit::NotifyStarted:
                // 2ADA 0x04: this is what makes Kinomap begin its session.
                Serial.printf("notify started (2ADA 04), phone=%d\n", (int)bleServer.phoneConnected());
                bleServer.notifyStarted();
                break;
            case policy::Emit::ResetPolicy:
                speedPolicy.reset();
                break;
            case policy::Emit::SyncPolicy:
                speedPolicy.adopt(o.tenthKmh, millis());
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

// ---- setup wizard -------------------------------------------------------
// First boot (no stored "setup_done" flag) opens the wizard; tapping the BELT
// indicator on the main screen re-opens it (only while the belt is stopped).
static Preferences prefs;
static wizard::Wizard wiz;
static bool inWizard = false;
static bool simSource = false;
static uint32_t scanEnterMs = 0;
static bool wizDirty = true;
static int lastWizPhase = -1;
static char foundAddrBuf[24] = "";
// Speed requested by the on-screen START: Kinomap sends no target until the
// belt moves, so the bridge has to get it moving. 2.0 km/h, a gentle start.
static constexpr uint8_t kUiStartTenths = 20;

#if UI_SCREENSHOT
// Screenshot builds script a tour of every screen: the wizard is driven with
// real button presses but SYNTHETIC belt inputs (there is no belt on the
// bench), then the main screen runs on the real simulated belt.
static constexpr uint32_t kDemoDwellMs = 4000;
static constexpr int kDemoLast = 13;
static uint32_t demoT0 = 0;
static int demoIdx = -1;
static wizard::Inputs demoIn;
#endif

static void enterWizard(uint32_t now) {
    applyOutcome(state.setArmed(false));  // nothing may start the belt mid-setup
    wiz.start(simSource);
    inWizard = true;
    wizDirty = true;
    scanEnterMs = now;
    ui.setWizardMode(true);
    (void)ui.tookArmButton(); (void)ui.tookStop(); (void)ui.tookSetupTap();
}

static void leaveWizard(bool completed) {
    inWizard = false;
    ui.setWizardMode(false);
    wizDirty = true;
    (void)ui.tookWizardLeft(); (void)ui.tookWizardRight();
    if (completed) {
#if !UI_SCREENSHOT
        prefs.putBool("setup_done", true);
#endif
        Serial.println("setup: complete");
    }
}

static wizard::Inputs wizInputs(uint32_t now) {
#if UI_SCREENSHOT
    if (demoIdx >= 0 && demoIdx < 8) {
        demoIn.scanSec = demoIdx == 2 ? 40 : 4;
        return demoIn;
    }
#endif
    wizard::Inputs in;
    in.sim = simSource;
    in.beltUp = theBelt.linkUp() && beltFresh();
    in.scanSec = (now - scanEnterMs) / 1000;
    if (!simSource) {
        switch (beltLink.phase()) {
            case BeltLink::Phase::Scanning: in.scan = wizard::ScanPhase::Scanning; break;
            case BeltLink::Phase::Found: in.scan = wizard::ScanPhase::Found; break;
            case BeltLink::Phase::Connecting: in.scan = wizard::ScanPhase::Connecting; break;
            default: break;
        }
        snprintf(foundAddrBuf, sizeof foundAddrBuf, "%s", beltLink.foundAddress().c_str());
        in.foundAddr = foundAddrBuf;
    }
    return in;
}

static void wizPress(wizard::Btn b, uint32_t now) {
    const wizard::Inputs in = wizInputs(now);
    const wizard::Step before = wiz.step();
    const wizard::Result r = wiz.press(b, in);
    if (wiz.step() == wizard::Step::Scan && before != wizard::Step::Scan) scanEnterMs = now;
    wizDirty = true;
    if (r == wizard::Result::Later) leaveWizard(false);
    else if (r == wizard::Result::Finished) leaveWizard(true);
}

#if UI_SCREENSHOT
// Any console command ends the scripted tour, so scripted captures are deterministic.
static void demoCancel() {
    if (demoIdx >= kDemoLast) return;
    demoIdx = kDemoLast;
    if (inWizard) leaveWizard(false);
}

static void demoAdvance(int i, uint32_t now) {
    switch (i) {
        case 0: demoIn = wizard::Inputs{}; wiz.start(false); inWizard = true; ui.setWizardMode(true); break;
        case 1: demoIn.scan = wizard::ScanPhase::Scanning; wizPress(wizard::Btn::Right, now); break;
        case 2: break;  // same screen, 40 s in: the "not found yet" hint
        case 3: demoIn.scan = wizard::ScanPhase::Found; demoIn.foundAddr = "AA:BB:CC:DD:EE:01"; break;
        case 4: demoIn.beltUp = true; break;  // update() advances to Linked
        case 5: wizPress(wizard::Btn::Right, now); break;
        case 6: wizPress(wizard::Btn::Right, now); break;
        case 7: demoIn = wizard::Inputs{}; demoIn.sim = true; wiz.start(true); break;
        case 8: leaveWizard(false); break;  // main screen, disarmed
        case 9: applyOutcome(state.setArmed(true)); break;
        case 10: applyOutcome(state.startFromUi(kUiStartTenths)); break;  // real START path
        case 11: case 12: break;  // belt counts down, then ramps: new frames land by themselves
        case 13: applyOutcome(state.stopFromUi()); break;
    }
    wizDirty = true;
}
#endif

// ---- serial console -----------------------------------------------------
// One command per line on the USB-CDC serial (the only remote control surface
// this board has). Every command ends with exactly one "ok" / "err <reason>"
// line. Commands run the same paths as the touch UI: BridgeState, applyOutcome
// and (through it) BeltGate. Nothing here talks to the belt directly.
static console::LineBuffer conLine;
static bool shotPending = false;  // "shot" replies after the next frame is drawn

static const char* stepName() {
    if (!inWizard) return "off";
    switch (wiz.step()) {
        case wizard::Step::Welcome: return "welcome";
        case wizard::Step::Scan: return "scan";
        case wizard::Step::Linked: return "linked";
        case wizard::Step::Arm: return "arm";
        case wizard::Step::Done: return "done";
        case wizard::Step::SimInfo: return "siminfo";
    }
    return "?";
}

// Manual speed: the ONE path for the on-screen +/- and the console `speed`
// commands. dir != 0 steps; dir == 0 sets absTenths. BridgeState decides,
// applyOutcome executes (BeltGate paces). Returns nullptr or an err reason.
static const char* manualSpeed(int dir, uint8_t absTenths) {
    if (inWizard) return "wizard-active";
    if (!state.armed()) return "not-armed";
    if (!(theBelt.linkUp() && beltFresh())) return "belt-down";
    uint8_t base = lastCmdTenths;
    if (millis() - lastCmdMs > kCmdBaseMs) base = lastStatus.tenthKmh;  // remote may have moved it
    applyOutcome(dir != 0 ? state.stepSpeedFromUi(dir, base) : state.setSpeedFromUi(absTenths));
    return nullptr;
}

static void consoleStatus() {
    const bool fresh = beltFresh();
    // speed in 0.1 km/h, target in 0.01 km/h (as Kinomap sends it), so every
    // value is an integer a host script can compare.
    Serial.printf(
        "status armed=%d running=%d belt_link=%d phone_link=%d belt_state=%d "
        "speed=%u target=%u wizard=%s src=%s setup_done=%d rx_join=%lu rx_lost=%lu\n",
        (int)state.armed(), (int)state.beltRunning(), (int)(theBelt.linkUp() && fresh),
        (int)bleServer.phoneConnected(), fresh ? (int)lastStatus.beltState : -1,
        fresh ? (unsigned)lastStatus.tenthKmh : 0u, (unsigned)targetCenti, stepName(),
        simSource ? "sim" : "real", (int)prefs.getBool("setup_done", false),
        // Frames rejoined from split notifications, and fragments that never
        // completed. Reported so the rate is measured rather than assumed.
        simSource ? 0ul : (unsigned long)beltLink.reassembled(),
        simSource ? 0ul : (unsigned long)beltLink.fragmentsLost());
}

static void consoleRun(const char* line, bool overflow, uint32_t now) {
    using console::Cmd;
#if UI_SCREENSHOT
    demoCancel();
#endif
    if (overflow) { Serial.println("err line-too-long"); return; }
    wizDirty = true;  // whatever happens, redraw
    switch (console::parse(line)) {
        case Cmd::Empty:
            return;
        case Cmd::Status:
            consoleStatus();
            break;
        case Cmd::Arm:
            if (inWizard) { Serial.println("err wizard-active"); return; }
            applyOutcome(state.setArmed(true));
            break;
        case Cmd::Disarm:
            applyOutcome(state.setArmed(false));
            break;
        case Cmd::Start:
            if (inWizard) { Serial.println("err wizard-active"); return; }
            if (!state.armed()) { Serial.println("err not-armed"); return; }
            if (state.beltRunning()) { Serial.println("err already-running"); return; }
            if (!(theBelt.linkUp() && beltFresh())) { Serial.println("err belt-down"); return; }
            applyOutcome(state.startFromUi(kUiStartTenths));
            break;
        case Cmd::Stop:
            applyOutcome(state.stopFromUi());  // also disarms, like the button
            break;
        case Cmd::SimOn:
        case Cmd::SimOff: {
            // The belt source (and the BeltGate wrapped around it) is bound at
            // boot; switching live would orphan a pending start. Honest answer.
            const bool want = console::parse(line) == Cmd::SimOn;
            if (want != simSource) { Serial.println("err reboot-required"); return; }
            break;
        }
        case Cmd::Wizard:
            if (state.beltRunning()) { Serial.println("err belt-running"); return; }
            enterWizard(now);
            break;
        case Cmd::WizardNext:
        case Cmd::WizardBack:
            if (!inWizard) { Serial.println("err not-in-wizard"); return; }
            wizPress(console::parse(line) == Cmd::WizardNext ? wizard::Btn::Right : wizard::Btn::Left, now);
            break;
        case Cmd::DevOn:
        case Cmd::DevOff:
            logsink::setDev(console::parse(line) == Cmd::DevOn);
            break;
        case Cmd::UdpOn:
        case Cmd::UdpOff: {
            const bool want = console::parse(line) == Cmd::UdpOn;
            // Saying "on" and silently getting nothing would be the worst
            // outcome here: the flag is stored either way, but the broadcast
            // only happens inside developer mode, so say so.
            logsink::setUdp(want);
            if (want && !logsink::devOn()) {
                Serial.println("err dev-mode-off");
                return;
            }
            break;
        }
        case Cmd::Log: {
            char buf[160];
            logsink::describe(buf, sizeof buf);
            Serial.println(buf);
            break;
        }
        case Cmd::Wifi: {
            char ssid[48], pass[64];
            if (!console::parseWifiArg(line, ssid, sizeof ssid, pass, sizeof pass)) {
                Serial.println("err bad-wifi-args");
                return;
            }
            logsink::setWifi(ssid, pass);
            // Never echo the password, not even to the local console: this log
            // is broadcast over UDP and written to a removable card.
            Serial.printf("wifi ssid=%s stored\n", ssid[0] ? ssid : "(cleared)");
            break;
        }
        case Cmd::SpeedUp:
        case Cmd::SpeedDown: {
            const char* e = manualSpeed(console::parse(line) == Cmd::SpeedUp ? 1 : -1, 0);
            if (e) { Serial.printf("err %s\n", e); return; }
            break;
        }
        case Cmd::SpeedSet: {
            unsigned char tenths = 0;
            switch (console::parseSpeedArg(line, policy::kMinCentiKmh, policy::kMaxCentiKmh, tenths)) {
                case console::SpeedArg::Bad: Serial.println("err bad-value"); return;
                case console::SpeedArg::OutOfRange: Serial.println("err out-of-range"); return;
                case console::SpeedArg::Ok: break;
            }
            const char* e = manualSpeed(0, tenths);
            if (e) { Serial.printf("err %s\n", e); return; }
            break;
        }
        case Cmd::Cp: {
            // Control Point writes since boot, by raw opcode byte; only seen ones.
            Serial.print("cp");
            for (int i = 0; i < 256; ++i)
                if (uint32_t c = bleServer.cpCount((uint8_t)i)) Serial.printf(" %02x=%u", i, (unsigned)c);
            Serial.printf(" empty=%u\n", (unsigned)bleServer.cpEmptyCount());
            break;
        }
        case Cmd::Shot:
            shotPending = true;  // answered once the frame has been drawn and emitted
            return;
        case Cmd::Help:
            Serial.println("commands: status arm disarm start stop | sim on|off | wizard [next|back] | speed up|down|<km/h> | cp | log | log udp on|off | dev on|off | wifi <ssid> <pass> | shot | help");
            break;
        case Cmd::Unknown:
        case Cmd::TooLong:
            Serial.println("err unknown-command");
            return;
    }
    Serial.println("ok");
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);
    logsink::begin();
    ui.begin();

    bleServer.setOnTarget([](uint16_t centi) {
        std::lock_guard<std::recursive_mutex> g(mtx);
        hasTarget = true;  // latest wins; applied by loop()
        targetCenti = centi;
    });
    bleServer.setOnControlOp([](ftms::CpOp op) {
        // (Raw-byte logging and per-opcode counts live in FtmsServer.)
        std::lock_guard<std::recursive_mutex> g(mtx);
        if (op == ftms::CpOp::StopPause) {
            stopRequested = true;
        } else if (op == ftms::CpOp::StartResume) {
            // Counted over a full hardware session, Kinomap writes 00=1 03=2
            // 07=1 and never 0x02 Set Target Speed: if {07} does not start the
            // belt, nothing Kinomap does ever will. So treat it as a start
            // request. Kinomap also sends {07} to acknowledge our own 2ADA
            // 0x04, but the belt is running by definition in that case, and
            // the beltRunning() guard drops it.
            //
            // This goes through hasTarget rather than straight to the belt so
            // that it passes the same gates every other target does: BridgeState
            // drops it unless the user armed the bridge, and SpeedPolicy owns
            // the start sequence and the command pacing.
            if (PHONE_START_CENTI_KMH > 0 && !state.beltRunning()) {
                hasTarget = true;
                targetCenti = PHONE_START_CENTI_KMH;
            }
        }
    });
    bleServer.begin(BRIDGE_ADV_NAME, BRIDGE_MANUFACTURER, BRIDGE_MODEL);

    // Belt source: config default, inverted if the screen is held during boot.
    M5.update();
    bool sim = USE_SIM_BELT;
    if (M5.Touch.getCount() > 0) sim = !sim;
    Serial.printf("belt source: %s\n", sim ? "SimBelt" : "BeltLink");
    simSource = sim;
    belt::Belt& chosen = sim ? static_cast<belt::Belt&>(simBelt)
                             : static_cast<belt::Belt&>(beltLink);
    ui.setSim(sim);
    gate = new belt::BeltGate(chosen, policy::kMinCommandIntervalMs);
    // BeltLink shares the NimBLE stack, so only start it after bleServer.begin().
    if (!sim && !beltLink.begin()) Serial.println("belt link task failed to start");

    prefs.begin("bridge", false);
    const bool setupDone = prefs.getBool("setup_done", false);
    Serial.printf("setup: %s\n", setupDone ? "already done" : "first boot, starting wizard");

    std::lock_guard<std::recursive_mutex> g(mtx);
#if UI_SCREENSHOT
    demoT0 = millis();  // the tour is started by demoAdvance(0) from loop()
    (void)setupDone;
    ui.render(0, 0, 0, false, false, state.armed(), false);
#else
    if (!setupDone) enterWizard(millis());
    else ui.render(0, 0, 0, false, false, state.armed(), false);  // boots disarmed
#endif
}

void loop() {
    logsink::tick(millis());
    static uint32_t lastTick = 0;
    M5.update();
    ui.poll();

    bool redraw = false;
    uint16_t centi;
    uint32_t dist, elapsed;
    bool beltUp, phoneUp, armed, running;
    wizard::Screen wscreen;
    // (console `shot` forces a redraw via wizDirty so the frame is current)
    bool wizSim = false;
    {
        std::lock_guard<std::recursive_mutex> g(mtx);
        trackPhone();
        trackBelt();
        const uint32_t now = millis();
        while (Serial.available()) {
            if (conLine.feed((char)Serial.read())) consoleRun(conLine.line(), conLine.overflowed(), now);
        }
#if UI_SCREENSHOT
        while (demoIdx < kDemoLast && demoIdx < (int)((now - demoT0) / kDemoDwellMs))
            demoAdvance(++demoIdx, now);
#endif
        if (inWizard) {
            // The wizard owns the touch zones. The bridge stays disarmed here
            // (see enterWizard); FTMS Stop/Pause below still works.
            if (ui.tookWizardLeft()) wizPress(wizard::Btn::Left, now);
            if (inWizard && ui.tookWizardRight()) wizPress(wizard::Btn::Right, now);
        } else {
            if (ui.tookArmButton()) {
                const bool canStart =
                    state.armed() && !state.beltRunning() && theBelt.linkUp() && beltFresh();
                if (!state.armed()) applyOutcome(state.setArmed(true));
                else if (canStart) applyOutcome(state.startFromUi(kUiStartTenths));
                else applyOutcome(state.setArmed(false));  // ARMED face: tap disarms
                redraw = true;
            }
            // Same path as console `speed up|down`; ignored (button is dim) unless armed.
            if (ui.tookSpeedUp()) { (void)manualSpeed(1, 0); redraw = true; }
            if (ui.tookSpeedDown()) { (void)manualSpeed(-1, 0); redraw = true; }
            if (ui.tookStop()) {
                applyOutcome(state.stopFromUi());  // also disarms
                redraw = true;
            }
            // Re-open setup, but never under a moving belt.
            if (ui.tookSetupTap() && !state.beltRunning()) enterWizard(now);
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
        running = state.beltRunning();
        if (inWizard) {
            const wizard::Inputs in = wizInputs(now);
            if (wiz.update(in)) {
                if (wiz.step() == wizard::Step::Scan) scanEnterMs = now;
                wizDirty = true;
            }
            wscreen = wiz.screen(in);
            wizSim = in.sim;
            if (wscreen.phase != lastWizPhase) { lastWizPhase = wscreen.phase; wizDirty = true; }
        } else {
            lastWizPhase = -1;
        }
    }
    // Drawing (~150 KB sprite push) happens outside the lock.
    if (inWizard) {
        if (wizDirty || redraw) { wizDirty = false; ui.renderWizard(wscreen, wizSim); }
    } else if (redraw || wizDirty) {
        wizDirty = false;
        ui.render(centi, dist, elapsed, beltUp, phoneUp, armed, running);
    }
    if (shotPending) {
        shotPending = false;
        Serial.println(ui.shotNow() ? "ok" : "err not-built");
    }
    delay(20);
}
