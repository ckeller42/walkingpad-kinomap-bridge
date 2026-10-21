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
//   speed up | down       one 0.5 km/h step (armed only: "err not-armed")
//   speed <km/h>         absolute, e.g. "speed 3.5" (0.1 steps; "err out-of-range", "err bad-value")
//   cp                    Control Point writes since boot: "cp 00=1 03=7 empty=0" (hex opcode=count), then ok
//   shot                  emit one screenshot frame now (screenshot builds)
//   help
namespace console {

enum class Cmd {
    Empty,  // blank line: no reply at all
    Status, Arm, Disarm, Start, Stop,
    SimOn, SimOff,
    Wizard, WizardNext, WizardBack,
    SpeedUp, SpeedDown, SpeedSet,
    Cp, Shot, Help,
    Log, Wifi,
    DevOn, DevOff,       // developer mode: the radio master switch
    UdpOn, UdpOff,       // the UDP log broadcast, inside developer mode
    SdOn, SdOff,         // SD probing, which claims PSRAM-adjacent pins
    Ota, OtaCheck, OtaApply, OtaUrl,  // firmware update
    Menu,                // leave the developer menu without the touch screen
    Touch,               // report the touch panel's own state
    Unknown,   // -> "err unknown-command"
    TooLong,   // -> "err line-too-long"
};

Cmd parse(const char* line);

enum class SpeedArg { Bad, OutOfRange, Ok };
// For Cmd::SpeedSet: reads "speed <km/h>" (at most 2 decimals, a multiple of
// 0.1) into tenthKmh (0.1 km/h units) if it is within [minCenti, maxCenti].
SpeedArg parseSpeedArg(const char* line, unsigned minCenti, unsigned maxCenti, unsigned char& tenthKmh);

// For Cmd::Wifi: reads "wifi <ssid> <password>" from the RAW line, not the
// normalised one, because both fields are case-sensitive and a password may
// contain runs of spaces that normalising would collapse. The password is
// everything after the single space that follows the ssid, trailing CR/LF
// removed. "wifi" alone (no arguments) clears both and returns true.
bool parseWifiArg(const char* rawLine, char* ssid, size_t ssidCap, char* pass, size_t passCap);

// For Cmd::OtaUrl: reads "ota url <absolute-url>" from the RAW line, because a
// URL is case-sensitive and normalising would destroy it. "ota url" alone
// clears the stored value and returns true.
bool parseOtaUrlArg(const char* rawLine, char* url, size_t cap);

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
