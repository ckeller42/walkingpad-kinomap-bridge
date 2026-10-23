#include <Arduino.h>
#include <M5Unified.h>
#include <Preferences.h>

#include <mutex>

#include "belt_gate.h"
#include "belt_link.h"
#include "bridge_state.h"
#include "config.h"
#include "console_cmd.h"
#include "devmenu.h"
#include "ftms_server.h"
#include "logsink.h"
#include "ota.h"
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

// Developer menu. Entered by tapping the gear in the top-right corner of the
// main screen; its "Re-run setup" row re-opens the wizard. It owns the screen
// while open, opening it disarms, and it is refused while a linked belt is not
// parked or is in Auto: it can take the radio up and down, which is not
// something to do underfoot.
static devmenu::DevMenu devMenu;
static bool inDevMenu = false;
static bool devDirty = true;

// Pending start: mode manual -> start -> wait for the belt to actually run ->
// setSpeed. A setSpeed during the countdown (state 7) is discarded by the belt.
// State 6 means "ramping up" only if a countdown was seen first; it also means
// "decelerating", when the belt has refused our start.
static bool startPending = false;
static bool sawCountdown = false;
static uint8_t pendingTenths = 0;
static uint32_t pendingSinceMs = 0;
/// Short refusal text for the screen, valid until #reasonUntilMs.
static const char* reasonText = nullptr;
static uint32_t reasonUntilMs = 0;
/// Show @p s on the screen for 1200 ms (a tap was refused).
static void showReason(const char* s) { reasonText = s; reasonUntilMs = millis() + 1200; }

// Latest belt readings (loop thread only).
static wilink::Status lastStatus{};

// Speed last commanded through applyOutcome: the base for +/- steps. The belt
// reading lags a command by a poll or two, so rapid presses must not step
// from it.
static uint8_t lastCmdTenths = 0;
static uint32_t lastCmdMs = 0;
static constexpr uint32_t kCmdBaseMs = 3000;
// After a Stop the belt keeps reporting motion while it decelerates; do not
// treat that as a fresh start (it would re-notify Kinomap "started").
static constexpr uint32_t kPostStopGraceMs = 5000;
// Starts one grace period in the past so boot does not mask Auto detection.
static uint32_t lastStopMs = 0 - kPostStopGraceMs;

static policy::BeltMode toBeltMode(uint8_t wireMode) {
    switch (wireMode) {
        case 0: return policy::BeltMode::Auto;
        case 1: return policy::BeltMode::Manual;
        case 2: return policy::BeltMode::Standby;
        default: return policy::BeltMode::Unknown;
    }
}

// Never called with Unknown (applyOutcome skips that case).
static wilink::Mode toWireMode(policy::BeltMode m) {
    switch (m) {
        case policy::BeltMode::Auto:    return wilink::Mode::Auto;
        case policy::BeltMode::Standby: return wilink::Mode::Standby;
        default:                        return wilink::Mode::Manual;
    }
}

static const char* emitName(policy::Emit e) {
    switch (e) {
        case policy::Emit::SetSpeed:      return "SetSpeed";
        case policy::Emit::Stop:          return "Stop";
        case policy::Emit::SetMode:       return "SetMode";
        case policy::Emit::StartSequence: return "StartSequence";
        case policy::Emit::NotifyStarted: return "NotifyStarted";
        case policy::Emit::ResetPolicy:   return "ResetPolicy";
        case policy::Emit::SyncPolicy:    return "SyncPolicy";
        case policy::Emit::None:          return "None";
    }
    return "?";
}

#if UI_SCREENSHOT
// Tour phase 25 sets this so its SetMode never reaches the (sim) belt; phase 26
// clears it. Screenshot builds only.
static bool demoHoldSetMode = false;
#endif

static void applyOutcome(const policy::Outcome& o) {
    for (size_t i = 0; i < o.count; ++i) {
        if (o.emits[i] != policy::Emit::None) {
            if (o.emits[i] == policy::Emit::SetMode)
                logsink::line("emit SetMode mode=%u", (unsigned)o.mode);
            else
                logsink::line("emit %s tenths=%u", emitName(o.emits[i]), (unsigned)o.tenthKmh);
        }
        switch (o.emits[i]) {
            case policy::Emit::SetSpeed:
                lastCmdTenths = o.tenthKmh; lastCmdMs = millis();
                if (startPending) pendingTenths = o.tenthKmh;  // not running yet
                else theBelt.setSpeed(o.tenthKmh);
                break;
            case policy::Emit::Stop:
                startPending = false;
                lastCmdTenths = 0;
                lastStopMs = millis();
                theBelt.stop();
                break;
            case policy::Emit::SetMode:
                if (o.mode == policy::BeltMode::Unknown) {
                    logsink::line("emit SetMode skipped: unknown");
                    break;
                }
#if UI_SCREENSHOT
                if (demoHoldSetMode) {  // tour phase 25: keep the request pending for the A? shot
                    logsink::line("emit SetMode held (shot tour)");
                    break;
                }
#endif
                theBelt.setMode(toWireMode(o.mode));
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
    // Auto owns the speed: the belt starts and paces itself, so a pending
    // start must never write set_speed under it.
    if (state.autoActive() || state.autoPending()) { startPending = false; return; }
    // Disarmed while the start was pending: drop it rather than write under a
    // bridge that no longer owns the belt.
    if (!state.armed()) { startPending = false; logsink::line("start dropped: disarmed"); return; }
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
// First boot (no stored "setup_done" flag) opens the wizard; the gear menu's
// "Re-run setup" row re-opens it (the menu itself is refused while the belt
// moves).
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
static constexpr int kDemoLast = 27;
static uint32_t demoT0 = 0;
static int demoIdx = -1;
static wizard::Inputs demoIn;
// The scan list and the keyboard depend on what is on the air, which makes
// them unscreenshottable on a bench with no network. The tour substitutes a
// fixed set so the captured pages are stable.
static bool demoDev = false;
static devmenu::Inputs demoDevIn;
#endif

static void enterWizard(uint32_t now) {
    applyOutcome(state.setArmed(false));  // nothing may start the belt mid-setup
    wiz.start(simSource);
    inWizard = true;
    wizDirty = true;
    scanEnterMs = now;
    ui.setWizardMode(true);
    (void)ui.tookArmButton(); (void)ui.tookStop();
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

static void speedTapReason();  // defined with the console; the tour drives the real refusal path
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
        case 9: speedTapReason(); break;    // a refused + tap: the reason line ("ARM first")
        case 10: applyOutcome(state.setArmed(true)); break;
        case 11: applyOutcome(state.startFromUi(kUiStartTenths)); break;  // real START path
        case 12: case 13: break;  // belt counts down, then ramps: new frames land by themselves
        case 14: applyOutcome(state.stopFromUi()); break;
        case 15:  // developer menu, root
            demoDev = true;
            demoDevIn = devmenu::Inputs{};
            demoDevIn.devOn = true;
            demoDevIn.udpOn = true;
            demoDevIn.wifiUp = true;
            demoDevIn.storedSsid = "Insel";
            demoDevIn.ip = "192.168.178.96";
            demoDevIn.otaVersion = "bd3819b";  // the root's Update row shows it
            demoDevIn.otaHost = "ckeller42.github.io";
            devMenu.open();
            inDevMenu = true;
            ui.setMenuMode(true);
            break;
        case 16:  // the scan list
            demoDevIn.networkCount = 3;
            demoDevIn.networks[0] = "Insel";
            demoDevIn.networks[1] = "Nachbar-WLAN";
            demoDevIn.networks[2] = "Gast";
            devMenu.tapRow(2, demoDevIn);
            break;
        case 17:  // the on-screen keyboard
            devMenu.tapRow(0, demoDevIn);
            devMenu.append('s');
            devMenu.append('e');
            devMenu.append('c');
            break;
        case 18:  // the digits and symbols page
            devMenu.setPage(devmenu::Page::Symbols1);
            break;
        case 19:  // the remaining punctuation
            devMenu.setPage(devmenu::Page::Symbols2);
            break;
        case 20:  // the Update screen, not checked yet
            devMenu.open();  // back to the root from the keyboard
            demoDevIn.otaVersion = "bd3819b";
            demoDevIn.otaHost = "ckeller42.github.io";
            demoDevIn.otaState = devmenu::OtaState::Idle;
            demoDevIn.otaMessage = "idle (running bd3819b)";
            devMenu.tapRow(4, demoDevIn);
            break;
        case 21:  // a newer build is published
            demoDevIn.otaState = devmenu::OtaState::Available;
            demoDevIn.otaAvailable = "v0.2.0";
            demoDevIn.otaMessage = "v0.2.0 available";  // what ota::check() really says
            break;
        case 22:  // installing, mid-download
            demoDevIn.otaState = devmenu::OtaState::Installing;
            demoDevIn.otaMessage = "downloading 37%";
            break;
        case 23:  // the failure a private repository produces
            demoDevIn.otaState = devmenu::OtaState::Failed;
            demoDevIn.otaMessage = "manifest http 404";
            break;
        case 24:
            // End on the main screen. Leaving the tour inside the menu means a
            // flashed screenshot build answers no touch but CLOSE, which reads
            // as a dead touchscreen. leaveDevMenu() is defined further down, so
            // the two lines it would run are inlined here.
            demoDev = false;
            inDevMenu = false;
            ui.setMenuMode(false);
            break;
        // The three badge states on the main screen: A? (requested), A, M.
        // The state comes from the sim belt itself, never from a synthetic
        // observe. Gated on simSource: a real belt must not be sent set_mode.
        case 25:  // request held back, so the sim keeps its old mode: A?
            if (!simSource) break;
            inDevMenu = false;
            ui.setMenuMode(false);
            applyOutcome(state.setArmed(true));
            demoHoldSetMode = true;
            applyOutcome(state.requestMode(policy::BeltMode::Auto, false));
            break;
        case 26:  // release: the sim accepts Auto and reports it on its next poll: A
            if (!simSource) break;
            demoHoldSetMode = false;
            applyOutcome(state.requestMode(policy::BeltMode::Auto, false));
            break;
        case 27:  // sim reports Manual: M
            if (!simSource) break;
            applyOutcome(state.requestMode(policy::BeltMode::Manual, false));
            break;
    }
    devDirty = true;
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
/// Explain a `+`/`-` tap that will be refused (disarmed, or Auto owns speed).
static void speedTapReason() {
    if (!state.armed()) showReason("ARM first");
    else if (state.autoActive() || state.autoPending()) showReason("AUTO sets speed");
}

static const char* manualSpeed(int dir, uint8_t absTenths) {
    if (inWizard) return "wizard-active";
    if (!state.armed()) return "not-armed";
    if (state.autoActive() || state.autoPending()) return "auto-mode";
    if (!(theBelt.linkUp() && beltFresh())) return "belt-down";
    uint8_t base = lastCmdTenths;
    if (millis() - lastCmdMs > kCmdBaseMs) base = lastStatus.tenthKmh;  // remote may have moved it
    applyOutcome(dir != 0 ? state.stepSpeedFromUi(dir, base) : state.setSpeedFromUi(absTenths));
    return nullptr;
}

// devmenu::OtaState mirrors ota::State so lib/ need not include ota.h; the
// cast in devInputs() is only valid while the two stay in the same order.
static_assert((int)devmenu::OtaState::Idle == (int)ota::State::Idle &&
                  (int)devmenu::OtaState::Checking == (int)ota::State::Checking &&
                  (int)devmenu::OtaState::UpToDate == (int)ota::State::UpToDate &&
                  (int)devmenu::OtaState::Available == (int)ota::State::Available &&
                  (int)devmenu::OtaState::Installing == (int)ota::State::Installing &&
                  (int)devmenu::OtaState::Failed == (int)ota::State::Failed,
              "devmenu::OtaState must mirror ota::State");

// "downloading NN%", written by otaProgress() while ota::apply() blocks; shown
// in place of ota::message() while the state is Installing.
static char otaProgressMsg[48] = "";  // "downloading " + a 31-char version

/// The host part of the manifest URL, for the Update screen's Source row.
static const char* otaHostText() {
    static char host[64];
    devmenu::urlHost(ota::url(), host, sizeof host);
    return host;
}

// Builds the menu's view of the world fresh each frame; the menu keeps no copy.
static devmenu::Inputs devInputs() {
#if UI_SCREENSHOT
    if (demoDev) return demoDevIn;
#endif
    devmenu::Inputs in;
    in.devOn = logsink::devOn();
    in.udpOn = logsink::udpOn();
    in.wifiUp = logsink::netOk();
    in.wifiConnecting = logsink::wifiConnecting();
    in.wifiFailed = logsink::wifiFailed();
    in.storedSsid = logsink::storedSsid();
    in.ip = logsink::ipText();
    in.scanning = logsink::scanning();
    const uint8_t n = logsink::scanCount();
    in.networkCount = n < devmenu::kMaxNetworks ? n : devmenu::kMaxNetworks;
    for (uint8_t i = 0; i < in.networkCount && i < devmenu::kMaxNetworks; ++i)
        in.networks[i] = logsink::scanSsid(i);
    in.otaVersion = ota::version();
    in.otaAvailable = ota::availableVersion();
    in.otaState = static_cast<devmenu::OtaState>(ota::state());
    in.otaMessage = (ota::state() == ota::State::Installing && otaProgressMsg[0])
                        ? otaProgressMsg : ota::message();
    in.otaHost = otaHostText();
    in.otaImageEnv = ota::imageEnv();
    in.otaEnvMismatch = ota::state() == ota::State::Available && !ota::envMatches();
    in.beltRunning = state.beltRunning();
    return in;
}

/// Put the menu as it stands on the glass now.
static void drawDevMenu() {
    ui.renderDevMenu(devMenu.screen(devInputs()));
}

/// Draw the menu as it will look once a blocking ota:: call has started.
/// ota::check() and ota::apply() block loop() and only set their state from
/// inside, so without this the glass would show the state from before the tap
/// for the whole call.
static void drawDevMenuOta(devmenu::OtaState st, const char* msg) {
    devmenu::Inputs in = devInputs();
    in.otaState = st;
    in.otaMessage = msg;
    ui.renderDevMenu(devMenu.screen(in));
}

/// ota::apply()'s progress callback: same task, called from its download loop.
static void otaProgress(int percent) {
    snprintf(otaProgressMsg, sizeof otaProgressMsg, "downloading %d%%", percent);
    drawDevMenu();
}

static void leaveDevMenu() {
    inDevMenu = false;
    ui.setMenuMode(false);
    wizDirty = true;  // force the main screen to redraw over the menu
}

static void devMenuAct(devmenu::Action a) {
    using A = devmenu::Action;
    switch (a) {
        case A::Close:
            leaveDevMenu();
            break;
        case A::ToggleDev:
            logsink::setDev(!logsink::devOn());
            break;
        case A::ToggleUdp:
            logsink::setUdp(!logsink::udpOn());
            break;
        case A::StartScan:
            logsink::startScan();
            break;
        case A::SubmitPassword:
            logsink::setWifi(devMenu.ssid(), devMenu.password());
            break;
        case A::ForgetWifi:
            logsink::setWifi("", "");
            break;
        case A::RerunSetup:
            leaveDevMenu();
            enterWizard(millis());
            break;
        case A::OpenUpdate:
            break;  // the Update screen reads ota:: fresh; the redraw below is enough
        case A::CheckOta:
            // The menu blanks its button while the belt runs (Inputs::beltRunning),
            // so a press that got here was offered; the guard is belt and braces.
            if (state.beltRunning()) { logsink::line("ota: check refused, belt running"); break; }
            drawDevMenuOta(devmenu::OtaState::Checking, "fetching manifest");
            ota::check();
            break;
        case A::InstallOta:
            // Same guards as the console's `ota apply`: the download blocks for
            // seconds and the belt link goes stale meanwhile. The menu cannot
            // be open over a moving belt and blanks INSTALL if one starts, so
            // this is belt and braces.
            if (state.beltRunning()) { logsink::line("ota: refused, belt running"); break; }
            applyOutcome(state.setArmed(false));
            // The same words ota::apply() will log first, so the glass and the
            // log agree from the first frame.
            snprintf(otaProgressMsg, sizeof otaProgressMsg, "downloading %s", ota::availableVersion());
            drawDevMenuOta(devmenu::OtaState::Installing, otaProgressMsg);
            ota::apply(otaProgress);  // reboots on success; returns on failure
            otaProgressMsg[0] = 0;
            break;
        case A::None:
            break;
    }
    devDirty = true;
}

// One frame of the developer menu, plus whatever the user just touched.
static void devMenuTick() {
    const devmenu::Inputs in = devInputs();

    const int row = ui.tookMenuRow();
    if (row >= 0) devMenuAct(devMenu.tapRow((uint8_t)row, in));
    if (ui.tookWizardLeft()) devMenuAct(devMenu.press(false, in));
    if (ui.tookWizardRight()) devMenuAct(devMenu.press(true, in));

    if (const char c = ui.tookKeyChar()) { devMenu.append(c); devDirty = true; }
    if (ui.tookKeyBackspace()) { devMenu.backspace(); devDirty = true; }
    if (ui.tookKeyShift()) { devMenu.toggleShift(); devDirty = true; }
    if (const int p = ui.tookKeyPage()) { devMenu.setPage((devmenu::Page)(p - 1)); devDirty = true; }

    if (!inDevMenu) return;  // Close was handled above

    // A scan finishing changes the screen with no touch at all, so redraw
    // while one is running rather than only on input.
    if (in.scanning) devDirty = true;
    // Wi-Fi state (connecting, IP, wrong password) also changes with no
    // touch, so refresh at least once a second while the menu is open.
    static uint32_t lastDevRedrawMs = 0;
    if (millis() - lastDevRedrawMs >= 1000) { devDirty = true; lastDevRedrawMs = millis(); }
    if (devDirty) {
        devDirty = false;
        ui.renderDevMenu(devMenu.screen(devInputs()));
    }
}

static void consoleStatus() {
    const bool fresh = beltFresh();
    // speed in 0.1 km/h, target in 0.01 km/h (as Kinomap sends it), so every
    // value is an integer a host script can compare.
    Serial.printf(
        "status armed=%d running=%d belt_link=%d phone_link=%d belt_state=%d "
        "speed=%u target=%u wizard=%s menu=%s src=%s setup_done=%d rx_join=%lu rx_lost=%lu\n",
        (int)state.armed(), (int)state.beltRunning(), (int)(theBelt.linkUp() && fresh),
        (int)bleServer.phoneConnected(), fresh ? (int)lastStatus.beltState : -1,
        fresh ? (unsigned)lastStatus.tenthKmh : 0u, (unsigned)targetCenti, stepName(),
        // Whether the developer menu owns the screen. Without this, "the touch
        // screen does nothing" is undiagnosable from here: the main screen's
        // controls are correctly inert while the menu is open.
        inDevMenu ? "on" : "off", simSource ? "sim" : "real", (int)prefs.getBool("setup_done", false),
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
    const console::Cmd cmd = console::parse(line);
    switch (cmd) {
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
        case Cmd::Menu:
            // A way out of the developer menu that does not need the touch
            // screen, for exactly the case where the screen seems dead.
            if (!inDevMenu) { Serial.println("err not-in-menu"); return; }
            leaveDevMenu();
            break;
        case Cmd::Touch: {
            char buf[160];
            ui.describeTouch(buf, sizeof buf);
            Serial.println(buf);
            break;
        }
        case Cmd::Ota:
            Serial.printf("ota running=%s env=%s state=%d url=%s%s msg=%s\n", ota::version(),
                          ota::env(), (int)ota::state(), ota::url()[0] ? ota::url() : "-",
                          ota::usingDefaultUrl() ? " (default)" : "", ota::message());
            break;
        case Cmd::OtaUrl: {
            char u[160];  // matches ota.cpp's manifestUrl[160]
            static_assert(console::LineBuffer::kMax >= 8 + sizeof u - 1,
                          "the console must accept \"ota url \" + the longest storable URL");
            if (!console::parseOtaUrlArg(line, u, sizeof u)) {
                Serial.println("err bad-ota-url");
                return;
            }
            ota::setUrl(u);  // "" restores OTA_MANIFEST_URL
            Serial.printf("ota url=%s%s\n", ota::url(), u[0] ? "" : " (default)");
            break;
        }
        case Cmd::OtaCheck:
            if (!logsink::devOn()) { Serial.println("err dev-mode-off"); return; }
            ota::check();
            Serial.println(ota::message());
            break;
        case Cmd::OtaApply:
            // The download blocks for seconds and the belt link goes stale
            // meanwhile, so never under a moving belt.
            if (!logsink::devOn()) { Serial.println("err dev-mode-off"); return; }
            if (state.beltRunning()) { Serial.println("err belt-running"); return; }
            applyOutcome(state.setArmed(false));
            ota::apply();
            Serial.println(ota::message());
            break;
        case Cmd::SdOn:
        case Cmd::SdOff:
            logsink::setSd(console::parse(line) == Cmd::SdOn);
            break;
        case Cmd::Log: {
            char buf[224];  // describe() runs to ~170 chars; unicast= is last and must not be cut
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
        case Cmd::ModeAuto:
        case Cmd::ModeManual: {
            // Mirrors the badge tap: set_mode must not precede start_belt, and
            // lastStatus lags a poll, so a pending or running start also counts.
            if (inWizard) { Serial.println("err wizard-active"); return; }
            if (!state.armed()) { Serial.println("err not-armed"); return; }
            if (!(theBelt.linkUp() && beltFresh())) { Serial.println("err belt-down"); return; }
            if (!wilink::parked(lastStatus) || startPending || state.beltRunning()) {
                Serial.println("err belt-moving");
                return;
            }
            const auto want = cmd == Cmd::ModeAuto ? policy::BeltMode::Auto
                                                     : policy::BeltMode::Manual;
            const auto o = state.requestMode(want, false);
            if (o.count == 0) {
                // Leave evidence in the log like the badge tap does: a replayed
                // capture must show why nothing moved.
                logsink::line("mode console refused: want=%u state=%u speed=%u armed=%d",
                              (unsigned)want, (unsigned)lastStatus.beltState,
                              (unsigned)lastStatus.tenthKmh, (int)state.armed());
                Serial.println("err refused");
                return;
            }
            applyOutcome(o);
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
            Serial.println("commands: status arm disarm start stop | sim on|off | wizard [next|back] | menu close | touch | speed up|down|<km/h> | mode auto|manual | cp | log | log udp on|off | log sd on|off | dev on|off | wifi <ssid> <pass> | ota [check|apply|url [<u>]] | shot | help");
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
    ota::begin();
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
    ui.render(0, 0, 0, 0, 0, false, false, state.armed(), false, -1, false,
                  ModeBadge::None, false,
                  policy::leftButton(state.armed(), false, false), nullptr, false);
#else
    if (!setupDone) enterWizard(millis());
    else ui.render(0, 0, 0, 0, 0, false, false, state.armed(), false, -1, false,
                  ModeBadge::None, false,
                  policy::leftButton(state.armed(), false, false), nullptr, false);  // boots disarmed
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
    bool beltUp, phoneUp, armed, running, moving = false, autoMode = false;
    ModeBadge badge = ModeBadge::None;
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
        if (inDevMenu) {
            // The menu owns the screen and the touch zones while it is open.
            devMenuTick();
        } else if (inWizard) {
            // The wizard owns the touch zones. The bridge stays disarmed here
            // (see enterWizard); FTMS Stop/Pause below still works.
            if (ui.tookWizardLeft()) wizPress(wizard::Btn::Left, now);
            if (inWizard && ui.tookWizardRight()) wizPress(wizard::Btn::Right, now);
        } else {
            if (ui.tookModeTap()) {
                const bool fresh = beltFresh();
                const bool stationary = fresh && wilink::parked(lastStatus);
                const auto want = (state.autoActive() || state.autoPending())
                                      ? policy::BeltMode::Manual : policy::BeltMode::Auto;
                // lastStatus lags a poll, so also fold in a start the bridge knows is
                // pending or running: a set_mode must not precede start_belt.
                const auto o = state.requestMode(
                    want, !stationary || startPending || state.beltRunning());
                // A refused tap must leave evidence: the first badge test on
                // hardware was refused four times in silence (belt in state 3).
                if (o.count == 0) {
                    logsink::line("mode tap refused: want=%u fresh=%d state=%u speed=%u armed=%d "
                                  "pending=%d running=%d",
                                  (unsigned)want, (int)fresh, (unsigned)lastStatus.beltState,
                                  (unsigned)lastStatus.tenthKmh, (int)state.armed(),
                                  (int)startPending, (int)state.beltRunning());
                    if (!state.armed()) showReason("ARM first");
                    else if (!fresh) showReason("Belt offline");
                    else if (startPending) showReason("Starting - wait");
                    else showReason("STOP first");
                }
                applyOutcome(o);
                redraw = true;
            }
            if (ui.tookArmButton()) {
                // The face comes from policy::leftButton(), the same call the
                // screen draws from, so the tap does what the picture says.
                // Wait refuses, so an impatient second tap cannot disarm
                // under a starting belt.
                switch (policy::leftButton(state.armed(), startPending,
                                           state.startAllowed(beltFresh()))) {
                case policy::LeftFace::Arm:
                    applyOutcome(state.setArmed(true));
                    break;
                case policy::LeftFace::Wait:
                    showReason("Starting - wait");
                    break;
                case policy::LeftFace::Start:
                    applyOutcome(state.startFromUi(kUiStartTenths));
                    break;
                case policy::LeftFace::Disarm:
                    // Say why START was not on offer; the tap still disarms.
                    if (!(theBelt.linkUp() && beltFresh())) showReason("Belt offline");
                    else if (state.autoActive() || state.autoPending())
                        showReason("AUTO: step on to start");
                    applyOutcome(state.setArmed(false));
                    break;
                }
                redraw = true;
            }
            // Same path as console `speed up|down`; ignored (button is dim) unless armed.
            if (ui.tookSpeedUp()) {
                speedTapReason();
                (void)manualSpeed(1, 0);
                redraw = true;
            }
            if (ui.tookSpeedDown()) {
                speedTapReason();
                (void)manualSpeed(-1, 0);
                redraw = true;
            }
            if (ui.tookStop()) {
                applyOutcome(state.stopFromUi());  // also disarms
                redraw = true;
            }
            // The developer menu can take the radio up and down, re-pair
            // Wi-Fi and re-run setup, so it is refused unless the belt is
            // known to be stopped, and AUTO must be left first. Opening it
            // disarms: the menu has no STOP of its own.
            if (ui.tookDevTap()) {
                // With the link down a bridge cannot move the belt, and setup
                // and Wi-Fi must stay reachable, so only a linked belt that
                // is not known to be parked refuses.
                if (theBelt.linkUp() && (!beltFresh() || !wilink::parked(lastStatus))) {
                    showReason("Belt not ready");
                    redraw = true;
                } else if (state.autoActive() || state.autoPending()) {
                    showReason("Leave AUTO first");
                    redraw = true;
                } else {
                    applyOutcome(state.setArmed(false));
                    devMenu.open();
                    inDevMenu = true;
                    devDirty = true;
                    ui.setMenuMode(true);
                    logsink::line("dev menu opened");
                }
            }
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
                const auto prevMode = state.observedMode();
                applyOutcome(state.observeMode(toBeltMode(st.mode)));
                if (state.observedMode() != prevMode)
                    logsink::line("belt mode %u -> %u raw=%u pending=%d active=%d", (unsigned)prevMode,
                                  (unsigned)state.observedMode(), (unsigned)st.mode,
                                  (int)state.autoPending(), (int)state.autoActive());
                // Fresh millis(), not `now`: a Stop applied earlier this iteration
                // sets lastStopMs > now, and the unsigned delta would wrap.
                if (state.autoActive() && !state.beltRunning() && st.beltState == 1 &&
                    millis() - lastStopMs >= kPostStopGraceMs)
                    applyOutcome(state.onBeltObservedRunning());
                speedPolicy.onBeltSpeed(st.tenthKmh);
                driveStart(st, prevTenths, now);
                if (state.beltRunning() && !startPending && wilink::parked(st))
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
        // The wide STOP follows observed motion too, not only the bridge's belief
        // (a belt started from KS Fit or by stepping on in Auto).
        moving = running || (fresh && !wilink::parked(lastStatus));
        autoMode = state.autoActive() || state.autoPending();
        badge = state.autoPending() ? ModeBadge::AutoPending
              : state.autoActive()  ? ModeBadge::Auto
              : state.observedMode() == policy::BeltMode::Unknown ? ModeBadge::None
              : ModeBadge::Manual;
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
        // lastCmdTenths is what we last asked the belt for. It goes stale as a
        // "pending" indicator once the belt has had time to obey, so it is only
        // offered while recent; after that the readout stands alone.
        const uint16_t commanded =
            (millis() - lastCmdMs <= kCmdBaseMs) ? (uint16_t)lastCmdTenths * 10 : centi;
        // Bridge's own battery (AXP2101 via M5Unified); < 0 when no cell.
        const int battPct = M5.Power.getBatteryLevel();
        const bool charging = M5.Power.isCharging();
        if (!inDevMenu)
            ui.render(centi, commanded, dist, elapsed, lastStatus.steps, beltUp, phoneUp,
                      armed, running, battPct, charging,
                      badge, autoMode,
                      policy::leftButton(armed, startPending, state.startAllowed(beltFresh())),
                      (int32_t)(reasonUntilMs - millis()) > 0 ? reasonText : nullptr,
                      moving);
    }
    if (shotPending) {
        shotPending = false;
        Serial.println(ui.shotNow() ? "ok" : "err not-built");
    }
    delay(20);
}
