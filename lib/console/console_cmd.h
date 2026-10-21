#pragma once
#include <stddef.h>

// Serial command console: parsing only (no Arduino types, unit-tested
// natively). src/main.cpp executes the commands through the same paths as the
// touch UI (BridgeState / applyOutcome / BeltGate).
//
// Protocol, one command per line. Every command prints zero or more
// information lines and then EXACTLY ONE terminating line, "ok" or
// "err <reason>", so a host waits for a known token instead of a timeout.
//
//   status                one line of key=value pairs, then ok
//   arm | disarm
//   start | stop
//   sim on | sim off      ok if already so, else "err reboot-required"
//   wizard [next|back]    re-enter setup (next/back press the right/left button)
//   shot                  emit one screenshot frame now (screenshot builds)
//   help
namespace console {

enum class Cmd {
    Empty,  // blank line: no reply at all
    Status, Arm, Disarm, Start, Stop,
    SimOn, SimOff,
    Wizard, WizardNext, WizardBack,
    Shot, Help,
    Unknown,   // -> "err unknown-command"
    TooLong,   // -> "err line-too-long"
};

Cmd parse(const char* line);

// Accumulates bytes into lines. feed() returns true when a full line is ready
// (take it with line()); a line longer than the buffer is reported as TooLong
// once its newline arrives and is never executed.
class LineBuffer {
public:
    static constexpr size_t kMax = 63;
    bool feed(char c);
    const char* line() const { return buf_; }
    bool overflowed() const { return over_; }

private:
    char buf_[kMax + 1] = {};
    size_t n_ = 0;
    bool over_ = false;
    bool done_ = false;  // previous call completed a line: reset on next feed
};

}  // namespace console
