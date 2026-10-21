#pragma once
#include "belt.h"

/**
 * @file sim_belt.h
 * @brief A belt with no hardware behind it, for host tests and bench work.
 *
 * Built so that **a simulated belt never looks like a healthy real one**: the
 * numbers it reports are invented, so anything that could be mistaken for
 * evidence has to be labelled. The wizard shows a dedicated screen and the main
 * screen an amber SIM BELT tag; a screenshot of this belt is never evidence
 * about the real one. Selected by the `cores3_sim` build, and the simulated
 * source is fixed at boot — `sim on`/`sim off` on the console answer
 * `err reboot-required`.
 */
namespace belt {

/**
 * @brief Simulated belt, modelled on measured WalkingPad R1 Pro behaviour.
 *
 * What is modelled, and why each part is there:
 * - start() enters countdown (state 7), then ramps (state 6) to running (1).
 * - stop() is "set mode standby": it decelerates, ends in state 4 / mode 2, and
 *   never returns to state 0 by itself.
 * - start() is refused until setMode(wilink::Mode::Manual) takes the belt out of
 *   standby, which is what makes the bridge's mode-first start sequence
 *   necessary rather than merely tidy.
 * - setSpeed() is *dropped*, not stored, unless the belt is in state 6 or 1;
 *   the real belt discards a speed sent during the countdown and uses its own
 *   preset.
 *
 * Known divergences from the real belt, so tests are not read as hardware
 * findings: there is no auto-stop when nobody walks on it (the real belt stops
 * about 15 s after starting unattended), the ramp after a bare start() goes to
 * 0.0 rather than the belt's own ~1.1 km/h preset, and the return to state 6 on
 * a speed change while running is an assumption that has not been measured.
 *
 * Command counters count **accepted** commands only, since each of those beeps
 * on the real belt; rejected or dropped commands are not counted.
 * setMode(Standby) is the same command as stop() and counts as a stop, and a
 * stop on an already stopped or stopping belt is rejected.
 */
class SimBelt : public Belt {
public:
    /**
     * @brief Accept a speed, but only while the belt is ramping or running.
     * @param tenthKmh Speed in 0.1 km/h units.
     * @note Silently dropped — not stored, not counted — in any other state,
     *       including the countdown and while stopping. That mirrors the real
     *       belt and is deliberately not an error: the caller has no way to
     *       learn it was dropped there either.
     */
    void setSpeed(uint8_t tenthKmh) override;

    /**
     * @brief Set the operating mode.
     * @param mode The mode to select.
     * @note wilink::Mode::Standby is routed to stop(), being the same command
     *       on the wire. Any other mode is accepted only from standby (which
     *       moves the belt to idle, the one way out of standby) or from idle;
     *       from a moving belt it is ignored.
     */
    void setMode(wilink::Mode mode) override;

    /**
     * @brief Start the belt from idle.
     * @note Ignored in any other state, exactly as the real belt refuses it.
     *       Clears the commanded speed and the session totals, so no stale
     *       speed survives a restart and a new session counts from zero.
     */
    void start() override;

    /**
     * @brief Stop the belt: decelerate and park in standby.
     * @note Rejected, and not counted, if the belt is already stopped or
     *       already stopping. The belt stays in standby afterwards and needs a
     *       mode change before it can start again.
     */
    void stop() override;

    /**
     * @brief Report the simulated state as a status frame.
     * @param out Filled in with the simulated state, speed, mode and session
     *            totals.
     * @return Always true — unlike a real belt, there is no link to lose and no
     *         reading to be missing, which is why nothing above may treat a
     *         successful poll here as evidence about hardware.
     */
    bool poll(wilink::Status& out) override;

    /**
     * @brief Whether the belt is reachable.
     * @return Always true. There is no link, so there is nothing to be down.
     */
    bool linkUp() const override { return true; }

    /**
     * @brief Advance the simulated physics.
     * @param nowMs Monotonic milliseconds. The first call only records the
     *              clock, so a large gap before the simulation starts cannot be
     *              integrated as one enormous step.
     */
    void tick(uint32_t nowMs) override;

    /**
     * @brief Every accepted command since construction.
     * @return The count. One beep per command on the real belt, so this is the
     *         number tests assert against when checking that the policy and the
     *         gate are not chattering.
     */
    uint32_t commandCount() const { return commands_; }

    /**
     * @brief Accepted speed commands.
     * @return The count, excluding speeds dropped because of the belt's state.
     */
    uint32_t setSpeedCount() const { return speedCmds_; }

    /**
     * @brief Accepted mode commands.
     * @return The count. Excludes setMode(Standby), which counts as a stop.
     */
    uint32_t modeCount() const { return modeCmds_; }

    /**
     * @brief Accepted starts.
     * @return The count, excluding starts refused because the belt was not idle.
     */
    uint32_t startCount() const { return startCmds_; }

    /**
     * @brief Accepted stops.
     * @return The count. Includes setMode(Standby) and excludes a stop on an
     *         already stopped or stopping belt, so a BeltGate stop retry shows
     *         up here only when it really went out.
     */
    uint32_t stopCount() const { return stopCmds_; }

private:
    static constexpr uint8_t kIdle = 0, kRunning = 1, kStandby = 4,
                             kTransition = 6, kCountdown = 7;
    static constexpr uint32_t kCountdownMs = 3000;
    static constexpr float kRampUpKmhPerSec = 0.7f;
    static constexpr float kRampDownKmhPerSec = 0.8f;
    static constexpr float kStepsPerMeter = 1.3f;

    uint8_t state_ = kIdle;
    wilink::Mode mode_ = wilink::Mode::Manual;
    uint8_t commanded_ = 0;     // tenths km/h, applied once running
    bool stopping_ = false;
    float speedKmh_ = 0.0f;
    float meters_ = 0.0f;
    float steps_ = 0.0f;
    uint32_t elapsedMs_ = 0;
    uint32_t countdownMs_ = 0;
    uint32_t lastMs_ = 0;
    bool haveLast_ = false;
    uint32_t commands_ = 0, speedCmds_ = 0, modeCmds_ = 0, startCmds_ = 0,
             stopCmds_ = 0;

    bool doStop();
    void accept(uint32_t& counter) { ++counter; ++commands_; }
};

}  // namespace belt
