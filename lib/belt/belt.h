#pragma once
#include <stdint.h>
#include "wilink.h"

/**
 * @file belt.h
 * @brief The treadmill abstraction everything above the wire talks to.
 *
 * Implemented by SimBelt (hardware-free), by the real BLE link in `src/`, and
 * by BeltGate, which wraps either of them to pace the writes. Because BeltGate
 * is itself a @ref belt::Belt, nothing above it can tell whether it is talking
 * to a paced belt, a real one or a simulated one — which is what lets the whole
 * policy stack run in host tests.
 */
namespace belt {

/**
 * @brief A treadmill belt: commands in, status out.
 *
 * Commands are fire-and-forget. The real belt's FE02 is
 * write-without-response, and it silently ignores anything sent less than
 * ~0.7 s after the previous write, so no method here can report that a command
 * landed. The only way to know is to poll() and watch the speed change; that is
 * the only backpressure there is.
 */
class Belt {
public:
    virtual ~Belt() = default;

    /**
     * @brief Set the target speed.
     * @param tenthKmh Speed in 0.1 km/h units, so 25 means 2.5 km/h.
     * @note Takes effect only on a belt already running in manual mode. The
     *       real belt discards a speed sent during the countdown and uses its
     *       own preset instead: an early probe written to hold 1.0 km/h let the
     *       belt reach 4.0 km/h unattended this way. Set the speed after the
     *       belt reports running, not during the start.
     */
    virtual void setSpeed(uint8_t tenthKmh) = 0;

    /**
     * @brief Set the operating mode.
     * @param mode The mode to select.
     * @note wilink::Mode::Standby is the same command as stop() on the wire, so
     *       implementations treat it as one. Setting Manual is also how a
     *       stopped belt is made startable again, since a stop parks it in
     *       standby and it refuses to start from there.
     */
    virtual void setMode(wilink::Mode mode) = 0;

    /**
     * @brief Start the belt, after which it counts down and runs.
     * @note Cannot report refusal, and the belt refuses a start unless it is
     *       idle. Callers confirm by polling — see
     *       `policy::BridgeState::onBeltObservedStopped()`, which exists
     *       precisely because a lost or refused start is otherwise invisible.
     */
    virtual void start() = 0;

    /**
     * @brief Stop the belt.
     * @note On the wire this is set-mode-standby, the **only** thing that
     *       stops the belt. Measured: both set-speed-0 and start-with-0 left it
     *       ramping on up to 4.0 km/h. The belt ends in standby (state 4,
     *       mode 2) and will not start again until the mode is set back to
     *       manual.
     */
    virtual void stop() = 0;

    /**
     * @brief The latest known status.
     * @param out Filled in only on success.
     * @return False, leaving @p out untouched, when no status is available.
     * @note Must **not** write to the belt. Any write belongs in
     *       requestStatus(), so the pacer can see it and charge it against the
     *       minimum gap; a write hidden inside poll() would be the one command
     *       BeltGate cannot account for.
     */
    virtual bool poll(wilink::Status& out) = 0;

    /**
     * @brief Ask the belt to produce a fresh status.
     * @return True only when a write actually happened.
     * @note On the BLE belt this is a write to FE02, subject to the same ~0.7 s
     *       rule as a command, which is why it is a separate call rather than
     *       part of poll(). The belt pushes nothing unsolicited — subscribing to
     *       FE01 and waiting 10 s yielded zero notifications — so status has to
     *       be asked for, making the poll required rather than a convenience.
     *       The default reports that there was nothing to ask, as for SimBelt.
     */
    virtual bool requestStatus() { return false; }

    /**
     * @brief Age of the status that poll() would return.
     * @return Milliseconds since that reading, or `UINT32_MAX` if there is none
     *         yet.
     * @note Lets the pacer ask for a refresh only when the reading is really
     *       stale, which decouples the request cadence from the loop cadence.
     *       The default of 0 says "always fresh", which is correct for a belt
     *       with no request/reply round trip.
     */
    virtual uint32_t statusAgeMs() const { return 0; }

    /**
     * @brief Whether the belt is reachable.
     * @return True while the link is up.
     * @note A belt already connected to KS Fit or to Kinomap on another phone
     *       reports down here, indistinguishably from one that is switched off:
     *       the belt accepts exactly one BLE connection.
     */
    virtual bool linkUp() const = 0;

    /**
     * @brief Time-driven housekeeping: SimBelt's physics, BeltGate's pacing.
     *
     * Call every loop pass. Monotonic milliseconds are passed in; only
     * differences are used, so any epoch will do. A no-op by default, because
     * the BLE link is event-driven and has no clock-driven state of its own.
     */
    virtual void tick(uint32_t /*nowMs*/) {}
};

}  // namespace belt
