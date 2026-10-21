#pragma once
#include <stdint.h>

/**
 * @file speed_policy.h
 * @brief Decides whether a speed target from the app becomes a belt command at all.
 *
 * The belt beeps on every accepted command and silently ignores commands sent
 * closer together than ~0.7 s, so suppressing needless commands is the whole
 * purpose of this class: a change not worth a beep is not worth sending, and a
 * command sent too soon simply does not happen.
 *
 * FE02 is write-without-response, so there is no acknowledgement and no flow
 * control — the only way to learn what the belt really did is to poll FE01 and
 * watch the speed, which is what onBeltSpeed() is for.
 *
 * Plain C++, no Arduino types; time arrives as a parameter, so every deadband,
 * rate-limit and resync case is unit-tested on the host.
 */
namespace policy {

// Each default can be overridden with -D or an earlier #define (e.g. from
// config.h); the constants below are what the code actually reads.
#ifndef POLICY_STOP_SENTINEL_CENTI_KMH
/** @brief Default for @ref policy::kStopSentinelCentiKmh — exactly 1.00 km/h means "stop". */
#define POLICY_STOP_SENTINEL_CENTI_KMH 100
#endif
#ifndef POLICY_MIN_CENTI_KMH
/** @brief Default for @ref policy::kMinCentiKmh — 0.50 km/h, below which a target is a stop. */
#define POLICY_MIN_CENTI_KMH 50
#endif
#ifndef POLICY_MAX_CENTI_KMH
/** @brief Default for @ref policy::kMaxCentiKmh — 6.00 km/h, the advertised maximum. */
#define POLICY_MAX_CENTI_KMH 600
#endif
#ifndef POLICY_DEADBAND_TENTHS
/** @brief Default for @ref policy::kDeadbandTenths — changes under 0.3 km/h are dropped. */
#define POLICY_DEADBAND_TENTHS 3
#endif
#ifndef POLICY_MIN_COMMAND_INTERVAL_MS
/** @brief Default for @ref policy::kMinCommandIntervalMs — at most one command per second. */
#define POLICY_MIN_COMMAND_INTERVAL_MS 1000
#endif
#ifndef POLICY_RESYNC_POLLS
/** @brief Default for @ref policy::kResyncPolls — divergent polls that mean a manual override. */
#define POLICY_RESYNC_POLLS 3
#endif

/// Target that means "stop" rather than a speed, in 0.01 km/h units. Kinomap
/// has no stop opcode for a generic FTMS treadmill, so it winds the target down
/// to exactly 1.00 km/h; that makes 1.00 unusable as a real target.
constexpr uint16_t kStopSentinelCentiKmh = POLICY_STOP_SENTINEL_CENTI_KMH;
/// Advertised minimum, 0.50 km/h. Anything below is treated as a stop rather
/// than clamped up, so an app that winds down towards zero really does stop the
/// belt instead of parking it at a crawl.
constexpr uint16_t kMinCentiKmh = POLICY_MIN_CENTI_KMH;
/// Advertised maximum, 6.00 km/h. Targets above it are clamped, not rejected:
/// an over-range target is still a request to go as fast as the machine can.
constexpr uint16_t kMaxCentiKmh = POLICY_MAX_CENTI_KMH;
/// Smallest change worth a command, in 0.1 km/h units. Every accepted command
/// beeps and nudges the motor, which is not worth a difference the walker
/// cannot feel.
constexpr uint8_t kDeadbandTenths = POLICY_DEADBAND_TENTHS;
/// Minimum gap between commands. Comfortably above the belt's own ~0.7 s
/// silent-drop window, so the policy never has to rely on BeltGate to save it.
constexpr uint32_t kMinCommandIntervalMs = POLICY_MIN_COMMAND_INTERVAL_MS;
/// Consecutive divergent status samples before the belt's own speed is adopted
/// as the reference. More than one on purpose: the deceleration ramp after our
/// own stop would otherwise look like somebody starting the belt by remote.
constexpr uint8_t kResyncPolls = POLICY_RESYNC_POLLS;

/** @brief What the policy decided to do with a target. */
enum class Action {
    None,      ///< Send nothing: deadbanded, rate-limited, or a redundant stop.
    SetSpeed,  ///< Command Decision::tenthKmh.
    /// Stop the belt. On the wire this is set-mode-standby; set-speed-0 does
    /// **not** stop the belt.
    Stop,
};

/** @brief One decision, consumed by BridgeState::apply(). */
struct Decision {
    Action action;     ///< What to do.
    uint8_t tenthKmh;  ///< Speed in 0.1 km/h units. Only valid for Action::SetSpeed.
};

/**
 * @brief The deadband, rate limit and resync between app targets and the belt.
 *
 * Holds a *reference* speed: what the belt is believed to be doing. The
 * reference moves when a command is sent, when the user sets a speed through
 * the bridge (adopt()), or when the belt disagrees for @ref kResyncPolls polls
 * in a row (onBeltSpeed()).
 *
 * The reference can go stale in ways this class cannot see — a dropped command,
 * a reconnect, a start the belt refused — so BridgeState says so with
 * `Emit::ResetPolicy` and `Emit::SyncPolicy`. Honouring those is not optional:
 * an unchanged app target judged against a reference that no longer describes
 * the belt is deadbanded, and nothing ever moves again.
 */
class SpeedPolicy {
public:
    /**
     * @brief Judge one speed target from the app.
     *
     * In order: stop sentinel, convert to 0.1 km/h, clamp to the advertised
     * range, deadband, rate limit.
     *
     * @param centiKmh Target in 0.01 km/h units, as written to the Control Point.
     * @param nowMs    Monotonic milliseconds; only differences are used, so any
     *                 epoch will do.
     * @return What to do. A stop while already stopped returns Action::None,
     *         because a redundant stop is a redundant beep. A stop also drops
     *         the reference, so the next target after it is judged fresh rather
     *         than against the speed the belt used to be doing.
     */
    Decision onTarget(uint16_t centiKmh, uint32_t nowMs);

    /**
     * @brief Offer one status sample, once per status poll.
     *
     * Detects speed changes the bridge did not make — the belt's own remote or
     * its panel. After @ref kResyncPolls consecutive divergent samples the
     * belt's speed becomes the reference, so a later app target is judged
     * against what the belt is really doing rather than what we last asked for.
     *
     * @param tenthKmh Speed from the status frame, in 0.1 km/h units.
     * @note With no reference (just after a stop, or before anything was ever
     *       commanded) a *falling* sample restarts the count, and a zero sample
     *       clears it. Measured, the ramp down after a stop is strictly
     *       decreasing — 2.8, 2.3, 0.4, 0.0 over about 3.6 s — so without that
     *       rule the bridge's own stop would be misread as a remote start. A
     *       flat run still counts, because a belt already at speed has no rise
     *       left to observe.
     */
    void onBeltSpeed(uint8_t tenthKmh);

    /**
     * @brief Adopt a speed the user set by hand through the bridge (UI +/- or console).
     *
     * Does immediately what the remote resync would do after @ref kResyncPolls
     * samples. Without it the next app target is weighed against a stale
     * reference, so a target the user has just stepped away from would be
     * deadbanded and never act.
     *
     * @param tenthKmh The speed that was commanded, in 0.1 km/h units.
     * @param nowMs    Monotonic milliseconds. Also counts as a command for rate
     *                 limiting, because one really was sent.
     */
    void adopt(uint8_t tenthKmh, uint32_t nowMs);

    /**
     * @brief Forget the reference, the rate-limit timestamp and the resync count.
     *
     * The caller must do this whenever a decision was dropped or bypassed, or
     * the belt was moved by something other than onTarget(); BridgeState signals
     * it with `Emit::ResetPolicy`.
     *
     * @note Deliberately leaves the "stopped" flag false rather than true. The
     *       belt may already be running from its own remote when the phone
     *       connects, so the first stop sentinel after a reset has to emit.
     */
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
