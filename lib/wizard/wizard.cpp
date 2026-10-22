#include "wizard.h"

#include <stdio.h>
#include <string.h>

namespace wizard {
namespace {
template <size_t N> void set(char (&dst)[N], const char* s) { snprintf(dst, N, "%s", s); }
}  // namespace

void Wizard::start(bool sim) {
    sim_ = sim;
    step_ = sim ? Step::SimInfo : Step::Welcome;
}

bool Wizard::update(const Inputs& in) {
    const Step before = step_;
    if (!sim_) {
        if (step_ == Step::Scan && in.beltUp) step_ = Step::Linked;
        else if (step_ == Step::Linked && !in.beltUp) step_ = Step::Scan;
    }
    return step_ != before;
}

// Where BACK from the Arm screen goes.
Step Wizard::afterBack(const Inputs& in) const {
    if (sim_) return Step::SimInfo;
    return in.beltUp ? Step::Linked : Step::Scan;
}

Result Wizard::press(Btn b, const Inputs& in) {
    const bool next = (b == Btn::Right);
    switch (step_) {
        case Step::Welcome:
            if (!next) return Result::Later;
            step_ = Step::Scan;
            break;
        case Step::SimInfo:
            if (!next) return Result::Later;
            step_ = Step::Arm;
            break;
        case Step::Scan:
            step_ = next ? Step::Arm : Step::Welcome;  // right = SKIP
            break;
        case Step::Linked:
            step_ = next ? Step::Arm : Step::Welcome;
            break;
        case Step::Arm:
            step_ = next ? Step::Done : afterBack(in);
            break;
        case Step::Done:
            if (next) return Result::Finished;
            step_ = Step::Arm;
            break;
    }
    return Result::Continue;
}

Screen Wizard::screen(const Inputs& in) const {
    Screen s;
    s.stepTotal = sim_ ? 3 : 5;
    set(s.left, "BACK");
    set(s.right, "NEXT");
    switch (step_) {
        case Step::Welcome:
            s.stepNo = 1;
            s.phase = 10;
            set(s.title, "Connect your belt");
            set(s.action[0], "Switch the belt ON.");
            set(s.action[1], "Close its phone app.");
            set(s.detail[0], "The belt talks to only one");
            set(s.detail[1], "device at a time. Stay within");
            set(s.detail[2], "2 m of it, then tap NEXT.");
            set(s.left, "LATER");
            break;
        case Step::SimInfo:
            s.stepNo = 1;
            s.phase = 15;
            set(s.title, "Simulated belt");
            set(s.action[0], "No real belt needed.");
            set(s.action[1], "Tap NEXT to go on.");
            set(s.detail[0], "SIM BELT is active: speeds are");
            set(s.detail[1], "made up and nothing is paired.");
            set(s.detail[2], "To switch, restart the bridge;");
            set(s.detail[3], "touch at boot flips the source.");
            set(s.left, "LATER");
            break;
        case Step::Scan: {
            s.stepNo = 2;
            set(s.right, "SKIP");
            char buf[40];
            if (in.scan == ScanPhase::Found || in.scan == ScanPhase::Connecting) {
                s.phase = 22;
                set(s.title, "Belt found");
                set(s.action[0], "Wait, connecting...");
                set(s.action[1], "Keep the belt ON.");
                snprintf(buf, sizeof buf, "Found 'RE' %s", in.foundAddr ? in.foundAddr : "");
                set(s.detail[0], buf);
                set(s.detail[1], "This takes a few seconds.");
            } else if (in.scanSec >= kScanHintSec) {
                s.phase = 21;
                set(s.title, "Belt not found yet");
                set(s.action[0], "Switch belt OFF and ON,");
                set(s.action[1], "close its phone app.");
                set(s.detail[0], "Move the bridge next to it.");
                snprintf(buf, sizeof buf, "Still searching... %us", (unsigned)in.scanSec);
                set(s.detail[1], buf);
                set(s.detail[2], "SKIP to set up the belt later.");
            } else {
                s.phase = 20;
                set(s.title, "Looking for the belt");
                set(s.action[0], "Keep the belt ON.");
                set(s.action[1], "Nothing else to do.");
                set(s.detail[0], "Searching for 'RE' (FE00)");
                snprintf(buf, sizeof buf, "Searching... %us", (unsigned)in.scanSec);
                set(s.detail[1], buf);
                set(s.detail[2], "Usually takes under 10 s.");
            }
            break;
        }
        case Step::Linked:
            s.stepNo = 3;
            s.phase = 30;
            set(s.title, "Belt connected");
            set(s.action[0], "Pairing worked.");
            set(s.action[1], "Tap NEXT.");
            set(s.detail[0], "The belt answers and reports");
            set(s.detail[1], "its status. You won't need");
            set(s.detail[2], "to do this again.");
            break;
        case Step::Arm:
            s.stepNo = sim_ ? 2 : 4;
            s.phase = 40;
            set(s.title, "About ARM");
            set(s.action[0], "ARM before you walk.");
            set(s.action[1], "Then press START.");
            set(s.detail[0], "The bridge moves the belt");
            set(s.detail[1], "only while ARMED (green).");
            set(s.detail[2], "STOP (red) always works,");
            set(s.detail[3], "armed or not.");
            break;
        case Step::Done:
            s.stepNo = sim_ ? 3 : 5;
            set(s.right, "FINISH");
            // Kinomap verifies it receives treadmill data before it will pair,
            // and the bridge only notifies while the belt delivers fresh
            // status. So a belt left off makes Kinomap fail with a generic
            // "could not pair" error that says nothing about the real cause.
            // Measured: this is exactly how a real pairing attempt failed.
            if (!sim_ && !in.beltUp) {
                s.phase = 51;
                set(s.title, "Belt is OFF");
                set(s.action[0], "Switch the belt ON");
                set(s.action[1], "before pairing Kinomap.");
                set(s.detail[0], "Kinomap checks for data before");
                set(s.detail[1], "it pairs. With the belt off");
                set(s.detail[2], "it fails with a connection");
                set(s.detail[3], "error that blames the bridge.");
                break;
            }
            s.phase = 50;
            set(s.title, "Last step");
            set(s.action[0], "Open Kinomap, ARM here,");
            set(s.action[1], "then press START.");
            // Measured during a real pairing: Kinomap's confirmation step reads
            // live values, and a belt sitting at 0.00 km/h looks the same to it
            // as no data at all. Belt merely ON is necessary but not
            // sufficient - it has to be moving while the app checks.
            set(s.detail[0], "ARM and START first: pairing");
            set(s.detail[1], "needs it moving. Stand on it:");
            set(s.detail[2], "empty, it stops after 15 s.");
            set(s.detail[3], "Re-run setup: gear menu.");
            break;
    }
    if (sim_) s.phase += 1000;  // sim variants are different screens
    return s;
}

}  // namespace wizard
