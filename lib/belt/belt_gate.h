#pragma once
#include <stdint.h>

#include "belt.h"

/**
 * @file belt_gate.h
 * @brief Paces and prioritises every write to the belt.
 *
 * The belt silently ignores a command that follows another too closely — not an
 * error, not a NAK, the command simply does not happen — and FE02 is
 * write-without-response, so a dropped one is never reported. The phone,
 * meanwhile, can ask for anything at any rate. BeltGate is what sits between
 * those two facts.
 *
 * Plain C++, no Arduino types; time arrives through tick(), so the whole pacing
 * and stop-retry behaviour is unit-tested on the host.
 */
namespace belt {

/**
 * @brief Releases belt writes no faster than the belt accepts them.
 *
 * Wraps any @ref Belt and is itself a @ref Belt, so a BLE-client belt slots in
 * underneath unchanged and nothing above notices the gate is there.
 *
 * **Queue depth is 1 per command type**: a newer speed supersedes an older one,
 * because the walker wants the latest speed, not every speed they passed
 * through on the way to it.
 *
 * **Send order is stop > urgent status > mode > start > speed > status.** A
 * command is a user intention and a status request is housekeeping, so commands
 * normally win. But the request is an FE02 write like any other and shares the
 * gap, so poll() only owes one when the reading is older than
 * @ref kStatusRefreshMs, and past @ref kStatusUrgentMs it jumps ahead of
 * commands (never ahead of a stop) so a steady stream of commands cannot starve
 * status forever.
 *
 * **A stop outranks everything and is retried.** stop() discards everything
 * queued and is re-sent until a poll shows the belt actually stopped (speed 0,
 * state 0 or 4), because a lost stop leaves the belt running with nobody
 * holding it — the one failure here that matters. stop() also always writes,
 * even if it looks redundant: a belt that keeps running outranks one extra beep.
 */
class BeltGate : public Belt {
public:
    /**
     * @brief Wrap a belt in the pacer.
     * @param inner The belt to write to. Held by reference and must outlive the
     *              gate; the gate forwards tick(), poll(), linkUp() and
     *              statusAgeMs() straight through.
     * @param minGapMs Minimum milliseconds between any two writes. Must be
     *              comfortably above the belt's own ~0.7 s silent-drop window,
     *              since there is no way to detect a drop.
     * @param stopRetryMs How long to wait for a sign of deceleration before
     *              concluding the stop was lost and re-sending it.
     */
    BeltGate(Belt& inner, uint32_t minGapMs, uint32_t stopRetryMs = 3000)
        : inner_(inner), gapMs_(minGapMs), retryMs_(stopRetryMs) {}

    /**
     * @brief Queue a speed, replacing any speed not yet sent.
     * @param tenthKmh Speed in 0.1 km/h units.
     * @note Cancels a pending stop. Asking for a speed is asking the belt to
     *       run, so it would be contradictory to leave the stop armed.
     */
    void setSpeed(uint8_t tenthKmh) override;

    /**
     * @brief Queue a mode change, replacing any mode not yet sent.
     * @param mode The mode to select.
     * @note wilink::Mode::Standby is redirected straight to stop(): it is the
     *       same command on the wire, so routing it anywhere else would give the
     *       belt a stop with none of the retry or priority that a stop needs.
     *       Any other mode cancels a pending stop.
     */
    void setMode(wilink::Mode mode) override;

    /**
     * @brief Queue a start.
     * @note Cancels a pending stop. Starts are not counted or coalesced beyond
     *       the single slot, and the gate cannot tell whether the belt accepted
     *       one — the belt refuses a start unless it is idle.
     */
    void start() override;

    /**
     * @brief Stop the belt, at the highest priority, and keep trying.
     *
     * Discards every other queued command and arms the stop, which is then sent
     * on the next tick() the gap allows and re-sent until poll() confirms the
     * belt is stopped. Always writes, even if a stop was just sent.
     */
    void stop() override;

    /**
     * @brief Read the wrapped belt's status and drive the stop confirmation.
     * @param out Filled in only on success, by the wrapped belt.
     * @return Whatever the wrapped belt returned.
     * @note This is also where a status refresh is *owed* (not sent — tick()
     *       sends it when the gap allows) and where a lost stop is detected. A
     *       stop counts as lost only if the retry window has passed **and** the
     *       speed has not fallen below what it was when the stop went out:
     *       measured, the ramp down is strictly decreasing, so any decrease at
     *       all means the stop landed and only needs time.
     */
    bool poll(wilink::Status& out) override;

    /**
     * @brief Whether the wrapped belt is reachable.
     * @return The wrapped belt's answer, unmodified.
     */
    bool linkUp() const override { return inner_.linkUp(); }

    /**
     * @brief Age of the wrapped belt's status.
     * @return The wrapped belt's answer, unmodified. The gate adds no latency
     *         of its own here, so the staleness thresholds below describe the
     *         real reading.
     */
    uint32_t statusAgeMs() const override { return inner_.statusAgeMs(); }

    /// A status refresh is owed once the reading is older than this. Below the
    /// 5 s staleness limit that makes the bridge declare a running belt down,
    /// with room for a retry.
    static constexpr uint32_t kStatusRefreshMs = 1500;
    /// Past this age a refresh outranks queued commands (never a stop), so a
    /// steady stream of commands cannot starve status forever and leave the
    /// bridge commanding a belt it can no longer see.
    static constexpr uint32_t kStatusUrgentMs = 3000;

    /**
     * @brief Release at most one queued write, if the gap has elapsed.
     *
     * Ticks the wrapped belt first, then sends one item in priority order.
     * Call often; every loop pass is fine, since the gap and not the call rate
     * decides when anything goes out.
     *
     * @param nowMs Monotonic milliseconds. Only differences are used, so any
     *              epoch will do, but it must be the same clock poll() is
     *              interleaved with — the stop retry window is measured against
     *              the value last seen here.
     */
    void tick(uint32_t nowMs) override;

private:
    Belt& inner_;
    uint32_t gapMs_, retryMs_;
    uint32_t now_ = 0, lastWriteMs_ = 0, lastStopMs_ = 0;
    bool haveWritten_ = false;
    bool pollWanted_ = false;   // a status refresh is owed (paced like a command)
    bool pollUrgent_ = false;

    bool stopWanted_ = false;   // stop queued or sent but not yet confirmed
    bool stopQueued_ = false;   // needs (re)sending
    bool stopSent_ = false;
    uint8_t speedAtStop_ = 0;

    bool modeQueued_ = false;
    wilink::Mode mode_ = wilink::Mode::Manual;
    bool startQueued_ = false;
    bool speedQueued_ = false;
    uint8_t speed_ = 0;
    uint8_t lastSpeed_ = 0;

    void sendStatusRequest();
    void cancelStop() { stopWanted_ = stopQueued_ = stopSent_ = false; }
    void wrote() { haveWritten_ = true; lastWriteMs_ = now_; }
};

}  // namespace belt
