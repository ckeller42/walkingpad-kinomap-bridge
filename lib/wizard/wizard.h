#pragma once
#include <stdint.h>

// First-time-setup wizard: pure state machine plus the on-screen text. No
// Arduino or drawing types, so it is unit-tested natively; src/ui.cpp only
// paints what screen() returns.
//
// Rule for every screen: `action` says what the USER has to do next, in plain
// words (never a state name), and both buttons always lead somewhere, so the
// wizard can never strand anyone.
namespace wizard {

enum class Step : uint8_t { Welcome, Scan, Linked, Arm, Done, SimInfo };

// Mirrors BeltLink::Phase (kept separate so this library stays Arduino-free).
enum class ScanPhase : uint8_t { Idle, Scanning, Found, Connecting };

enum class Btn : uint8_t { Left, Right };

enum class Result : uint8_t {
    Continue,
    Later,     // user left without finishing: setup is NOT marked complete
    Finished,  // user pressed FINISH on the last screen: mark setup complete
};

struct Inputs {
    bool sim = false;         // simulated belt is bound: never pretend to pair
    bool beltUp = false;      // real belt linked AND delivering fresh status
    ScanPhase scan = ScanPhase::Idle;
    uint32_t scanSec = 0;     // seconds spent on the Scan screen
    const char* foundAddr = "";  // address of the belt found, if any
};

constexpr uint32_t kScanHintSec = 30;  // after this, tell the user how to unstick it

struct Screen {
    uint8_t stepNo = 1, stepTotal = 5;
    int phase = 0;  // distinct per visually-distinct screen (screenshot trigger)
    char title[28] = {};
    char action[2][28] = {};  // what to do next: big, white
    char detail[4][36] = {};  // why / extra: small, grey
    char left[10] = {};       // button labels
    char right[10] = {};
};

class Wizard {
public:
    void start(bool sim);  // always begins at the first screen
    Step step() const { return step_; }
    // Applies belt state: Scan -> Linked once the belt delivers status, and back
    // to Scan if it is lost. Returns true if the step changed.
    bool update(const Inputs& in);
    Result press(Btn b, const Inputs& in);
    Screen screen(const Inputs& in) const;

private:
    Step step_ = Step::Welcome;
    bool sim_ = false;
    Step afterBack(const Inputs& in) const;
};

}  // namespace wizard
