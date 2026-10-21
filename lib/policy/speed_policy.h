#pragma once
#include <stdint.h>

// Decides whether a speed target from the app becomes a belt command at all.
// The belt beeps on every accepted command and ignores commands closer than
// ~0.7 s, so suppressing needless commands is the purpose of this class.
// Plain C++, no Arduino types; time arrives as a parameter.
namespace policy {

// Each default can be overridden with -D or an earlier #define (e.g. from config.h).
#ifndef POLICY_STOP_SENTINEL_CENTI_KMH
#define POLICY_STOP_SENTINEL_CENTI_KMH 100  // exactly 1.00 km/h means "stop"
#endif
#ifndef POLICY_MIN_CENTI_KMH
#define POLICY_MIN_CENTI_KMH 50             // advertised minimum (0.50 km/h); below it = stop
#endif
#ifndef POLICY_MAX_CENTI_KMH
#define POLICY_MAX_CENTI_KMH 600            // advertised maximum, 6.00 km/h
#endif
#ifndef POLICY_DEADBAND_TENTHS
#define POLICY_DEADBAND_TENTHS 3            // changes smaller than 0.3 km/h are dropped
#endif
#ifndef POLICY_MIN_COMMAND_INTERVAL_MS
#define POLICY_MIN_COMMAND_INTERVAL_MS 1000 // at most one command per interval
#endif
#ifndef POLICY_RESYNC_POLLS
#define POLICY_RESYNC_POLLS 3               // consecutive divergent polls = manual override
#endif

constexpr uint16_t kStopSentinelCentiKmh = POLICY_STOP_SENTINEL_CENTI_KMH;
constexpr uint16_t kMinCentiKmh = POLICY_MIN_CENTI_KMH;
constexpr uint16_t kMaxCentiKmh = POLICY_MAX_CENTI_KMH;
constexpr uint8_t kDeadbandTenths = POLICY_DEADBAND_TENTHS;
constexpr uint32_t kMinCommandIntervalMs = POLICY_MIN_COMMAND_INTERVAL_MS;
constexpr uint8_t kResyncPolls = POLICY_RESYNC_POLLS;

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

    // The user set the belt speed by hand through the bridge (UI +/- or
    // console). Adopts it as the reference, as the remote-resync would after
    // kResyncPolls samples, but immediately: a later app target is then judged
    // against what the belt really does (deadband / override) instead of a
    // stale reference. Also counts as a command for rate limiting.
    void adopt(uint8_t tenthKmh, uint32_t nowMs);

    void reset();

private:
    bool hasRef_ = false;       // a speed has been sent (or resynced to)
    uint8_t refTenths_ = 0;
    // Starts false: the belt may already be running from the remote at connect,
    // so the first stop sentinel must emit.
    bool stopped_ = false;      // a stop was sent and nothing has moved the belt since
    bool hasSent_ = false;      // lastSendMs_ is valid
    uint32_t lastSendMs_ = 0;
    uint8_t divergentPolls_ = 0;
    uint8_t prevTenths_ = 0;    // previous sample while there is no reference
};

}  // namespace policy
