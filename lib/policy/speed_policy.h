#pragma once
#include <stdint.h>

// Decides whether a speed target from the app becomes a belt command at all.
// The belt beeps on every accepted command and ignores commands closer than
// ~0.7 s, so suppressing needless commands is the purpose of this class.
// Plain C++, no Arduino types; time arrives as a parameter.
namespace policy {

constexpr uint16_t kStopSentinelCentiKmh = 100;  // exactly 1.00 km/h means "stop"
constexpr uint16_t kMaxCentiKmh = 600;           // advertised maximum, 6.00 km/h
constexpr uint8_t kDeadbandTenths = 3;           // drop changes smaller than 0.3 km/h
constexpr uint32_t kMinCommandIntervalMs = 1000; // at most one command per interval
constexpr uint8_t kResyncPolls = 3;              // consecutive divergent polls = manual override

enum class Action { None, SetSpeed, Stop };

struct Decision {
    Action action;
    uint8_t tenthKmh;  // valid for SetSpeed
};

class SpeedPolicy {
public:
    // sentinel -> convert to 0.1 km/h -> clamp -> deadband -> rate limit
    Decision onTarget(uint16_t centiKmh, uint32_t nowMs);

    // One sample per status poll; detects speed changes made by the remote.
    void onBeltSpeed(uint8_t tenthKmh);

    void reset();

private:
    bool hasRef_ = false;       // a speed has been sent (or resynced to)
    uint8_t refTenths_ = 0;
    bool stopped_ = false;      // a stop was sent and nothing has moved the belt since
    bool hasSent_ = false;      // lastSendMs_ is valid
    uint32_t lastSendMs_ = 0;
    uint8_t divergentPolls_ = 0;
};

}  // namespace policy
