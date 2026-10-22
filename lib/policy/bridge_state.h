#pragma once
#include <stddef.h>
#include <stdint.h>
#include "speed_policy.h"

/**
 * @file bridge_state.h
 * @brief Turns a SpeedPolicy decision into what the adapters must actually emit.
 *
 * The arming gate lives here: **nothing moves the belt unless the user armed
 * the bridge on the device**, and STOP works armed or not.
 *
 * Every state-changing entry point returns a `[[nodiscard]]` @ref policy::Outcome
 * listing what should happen, and exactly one `applyOutcome()` in `src/main.cpp`
 * drains it. That is what makes "no code path can quietly move the belt"
 * structural rather than a convention, and it is why all of this is testable
 * with no belt attached.
 *
 * Kinomap does send FTMS StartResume (`0x07`) but never SetTargetSpeed (`0x02`)
 * to a generic treadmill, so `main.cpp` converts its `0x07` into a target at
 * `PHONE_START_CENTI_KMH` and it arrives here as an ordinary target. Do not
 * treat a missing `0x02` as a bridge bug.
 *
 * Plain C++, no Arduino types.
 */
namespace policy {

/**
 * @brief One thing the host must do; a @ref policy::Outcome carries up to three, in order.
 *
 * Two of these are not belt commands at all but obligations on the caller:
 * Emit::ResetPolicy and Emit::SyncPolicy repair SpeedPolicy's reference, which
 * BridgeState can invalidate but cannot see.
 */
enum class Emit {
    None,      ///< Padding past Outcome::count. Never acted on.
    SetSpeed,  ///< Command Outcome::tenthKmh on an already-running belt.
    Stop,      ///< Stop the belt: set mode standby. Set-speed-0 does not stop it.
    /// The whole start sequence at Outcome::tenthKmh — mode manual, start, then
    /// speed. Mode first is necessary, not tidy: a stop parks the belt in
    /// standby (mode 2) and it refuses to start again until mode is set back to
    /// manual.
    StartSequence,
    /// Notify FTMS `2ADA` with `0x04`, which is what makes Kinomap begin its
    /// session. Kinomap never starts the belt itself, so if this is skipped the
    /// belt walks and the app records nothing.
    NotifyStarted,
    /// The caller **MUST** call SpeedPolicy::reset(). BridgeState dropped or
    /// bypassed a decision, or moved the belt itself, so the policy's idea of
    /// the last-sent speed is stale; without a reset an unchanged app target
    /// would be deadbanded and never act on the new state.
    ResetPolicy,
    /// The caller **MUST** call `SpeedPolicy::adopt(Outcome::tenthKmh, now)`.
    /// The user changed the speed by hand, so the policy reference follows it.
    SyncPolicy,
    /// Write `set_mode Outcome::mode`. Only ever Auto, Manual or Standby —
    /// never BeltMode::Unknown.
    SetMode,
};

/**
 * @brief The belt's mode as reported in status byte 4.
 *
 * Auto is the belt following the walker on its own; Manual is the mode a start
 * needs; Standby is where a stop parks it.
 */
enum class BeltMode : uint8_t {
    Unknown,  ///< No status seen yet, or the belt link is down. A sentinel, not a wire value.
    Auto,     ///< The belt adjusts its own speed.
    Manual,   ///< The app or the bridge sets the speed.
    Standby,  ///< Stopped; refuses to start until set back to manual.
};

/// Status frames an Auto request may wait for the belt to confirm it. A frame
/// that still shows the old mode counts as "not applied yet"; the
/// kModeConfirmFrames-th such frame gives the request up.
constexpr uint8_t kModeConfirmFrames = 5;

/// Manual speed step for the UI +/- buttons and the console `speed up|down`,
/// in 0.1 km/h units: 0.5 km/h.
constexpr uint8_t kSpeedStepTenths = 5;
/// Lowest manual speed, in 0.1 km/h units. Tied to the advertised FTMS minimum
/// so the device UI cannot ask for a speed the bridge told the phone it has not
/// got.
constexpr uint8_t kSpeedMinTenths = POLICY_MIN_CENTI_KMH / 10;
/// Highest manual speed, in 0.1 km/h units. Tied to the advertised FTMS maximum.
constexpr uint8_t kSpeedMaxTenths = POLICY_MAX_CENTI_KMH / 10;

/**
 * @brief The next 0.5 km/h step from a base speed.
 *
 * Snaps to the grid rather than adding: up goes to the next multiple of 0.5
 * above @p baseTenths, down to the next one below it. A belt left at 2.3 km/h
 * by the app therefore steps to 2.5, not 2.8, so the UI shows round numbers
 * however the speed got there.
 *
 * @param baseTenths Speed to step from, in 0.1 km/h units — the speed the user
 *                   can see, which is not necessarily what the bridge last
 *                   commanded.
 * @param dir Positive to step up; anything else steps down.
 * @return The stepped speed in 0.1 km/h units, clamped to
 *         [@ref policy::kSpeedMinTenths, @ref policy::kSpeedMaxTenths]. Never zero,
 *         so stepping down can never turn into a stop — stopping is STOP's job alone.
 */
uint8_t stepTenths(uint8_t baseTenths, int dir);

/**
 * @brief What the host must do, as a value.
 *
 * `[[nodiscard]]` on purpose: an Outcome that is not drained is a belt command
 * that silently did not happen, which from the outside is indistinguishable
 * from a bug in the policy above it.
 */
struct [[nodiscard]] Outcome {
    /// Capacity of the emit array. Three is the longest sequence any entry
    /// point produces — start sequence, notify, and the policy fix-up — and
    /// naming it lets a test pin the bound, so a fourth added later fails
    /// instead of overrunning silently.
    static constexpr size_t kMaxEmits = 3;

    /// What to do, in the order given. Only the first #count entries are
    /// valid; the rest are Emit::None.
    Emit emits[kMaxEmits];

    size_t count;      ///< Number of valid entries in #emits.
    /// Speed in 0.1 km/h units. Valid for Emit::SetSpeed, Emit::StartSequence
    /// and Emit::SyncPolicy. Entry points that clamp report the clamped value
    /// here, so the UI can show what was actually asked for.
    uint8_t tenthKmh;
    /// Mode to write. Valid for Emit::SetMode; BeltMode::Unknown otherwise.
    BeltMode mode;
};

/**
 * @brief The arming gate and the belt's believed running state.
 *
 * Knows four bits of world — armed, belt link, phone link, belt running — and
 * the mode the belt last reported (plus any mode request awaiting confirmation),
 * and nothing else. It issues no commands and keeps no clock; every effect is
 * described by the @ref Outcome it returns.
 *
 * The running flag is *optimistic*: it is set the moment a start is emitted,
 * because `Belt::start()` cannot report refusal. onBeltObservedStopped() is how
 * that optimism is corrected from what the belt actually reports.
 */
class BridgeState {
public:
    /**
     * @brief Arm or disarm the bridge.
     *
     * Arming alone never starts the belt; only the next target does.
     *
     * @param armed True to arm.
     * @return Emit::ResetPolicy on a false->true transition, so the first
     *         target after arming is not deadbanded against a reference from
     *         before. Nothing otherwise.
     * @note Disarming mid-run deliberately leaves the belt moving. Disarm is
     *       not stop: a control that sometimes stops the belt and sometimes
     *       does not is worse than one that never does. Kinomap's 1.00 km/h
     *       sentinel and stopFromUi() both still stop it.
     */
    Outcome setArmed(bool armed);

    /**
     * @brief Whether the bridge is armed.
     * @return True while armed. Note that stopFromUi() clears this.
     */
    bool armed() const { return armed_; }

    /**
     * @brief Record the belt link coming up or going down.
     *
     * @param up True when the belt is linked and delivering status.
     * @return Emit::ResetPolicy on a down->up transition, because targets
     *         dropped while the link was down must not be deadbanded once it
     *         returns. On loss the running state is unknowable, so it is
     *         cleared, with another Emit::ResetPolicy if the belt was running;
     *         after a reconnect the belt is treated as stopped until a fresh
     *         armed target starts it.
     *         The observed mode goes back to BeltMode::Unknown and any pending
     *         mode request is dropped; a reconnect never re-sends it.
     */
    Outcome onBeltLink(bool up);

    /**
     * @brief Record the phone link coming up or going down.
     *
     * @param up True when the phone is connected.
     * @return On loss with the belt running, Emit::Stop then Emit::ResetPolicy:
     *         nobody is recording the session any more, so the belt must not
     *         keep going. Nothing at all if the belt was not running. On a
     *         down->up transition, Emit::ResetPolicy.
     * @note The bridge stays armed across phone loss. The user armed it
     *       physically and only they take that back; a dropped phone must not
     *       silently require a walk back to the device.
     */
    Outcome onPhoneLink(bool up);

    /**
     * @brief Whether the belt is believed to be running.
     * @return True while a start has been issued and nothing has stopped it.
     * @note Optimistic — see the class note. Not a measurement of the belt.
     */
    bool beltRunning() const { return running_; }

    /**
     * @brief Whether FTMS `2ACD` Treadmill Data should be sent.
     * @return True while both links are up.
     * @note Deliberately not gated on arming or on the belt running. Kinomap
     *       verifies it is receiving data before it will pair, so notifying has
     *       to work on a stopped, disarmed belt or pairing fails with a generic
     *       error that blames the bridge.
     */
    bool notifying() const { return beltUp_ && phoneUp_; }

    /**
     * @brief Correct @ref beltRunning() after seeing the belt stopped.
     *
     * Call when a poll shows the belt stopped (speed 0, state 0 or 4) while
     * beltRunning() was true and no start was in flight, or when a start never
     * took. apply() sets the running flag optimistically and `Belt::start()`
     * cannot report refusal, so without this a refused or lost start wedges the
     * flag true, no later target ever emits Emit::StartSequence, and the bridge
     * goes quiet while looking perfectly healthy.
     *
     * @return Emit::ResetPolicy when the belt was marked running; nothing
     *         otherwise, so it is safe to call on every poll.
     */
    Outcome onBeltObservedStopped();

    /**
     * @brief Apply a SpeedPolicy decision, subject to the arming gate.
     *
     * @param d The decision from SpeedPolicy::onTarget().
     * @return For Action::Stop, Emit::Stop — always, armed or not, because
     *         stopping is always safe. For Action::SetSpeed while armed with
     *         **both** links up: Emit::SetSpeed on a running belt, or
     *         Emit::StartSequence plus Emit::NotifyStarted on a stopped one.
     *         Disarmed, or with either link down, a speed target is accepted and
     *         ignored: nothing is emitted and no error is reported, because the
     *         app has no business learning whether the user has armed.
     * @note A belt reconnect can re-emit the start sequence and a duplicate
     *       Emit::NotifyStarted into a Kinomap session that already started.
     *       That is accepted deliberately — a latch to suppress it could swallow
     *       a genuine restart, and a redundant notify costs nothing.
     */
    Outcome apply(const Decision& d);

    /**
     * @brief START pressed on the device, or `start` on the console.
     *
     * The bridge has to be able to begin a session itself: Kinomap sends no
     * target until the belt already reports movement, so waiting for the app
     * would deadlock.
     *
     * @param tenthKmh Speed to start at, in 0.1 km/h units.
     * @return Emit::StartSequence, Emit::NotifyStarted and Emit::ResetPolicy, so
     *         the first Kinomap target afterwards is not deadbanded. Emits
     *         nothing unless the bridge is armed, the belt link is up, and the
     *         belt is not already running.
     */
    Outcome startFromUi(uint8_t tenthKmh);

    /**
     * @brief Set an absolute speed from the device UI or the console `speed <km/h>`.
     *
     * @param tenthKmh Wanted speed in 0.1 km/h units. Clamped into
     *        [@ref kSpeedMinTenths, @ref kSpeedMaxTenths] and reported back in
     *        Outcome::tenthKmh.
     * @return On a running belt, Emit::SetSpeed then Emit::SyncPolicy. On a
     *         stopped one, the normal start sequence at that speed —
     *         Emit::StartSequence, Emit::NotifyStarted, Emit::SyncPolicy —
     *         because asking for a speed on a stopped belt means "walk at this
     *         speed", not "remember this setpoint". Emits nothing unless armed
     *         with the belt link up.
     * @note Requires no phone link, unlike apply(). Somebody standing at the
     *       device outranks the app, and the belt has to be drivable with no
     *       phone connected at all.
     */
    Outcome setSpeedFromUi(uint8_t tenthKmh);

    /**
     * @brief One 0.5 km/h step up or down from the speed the user can see.
     *
     * @param dir Positive to step up; anything else steps down.
     * @param baseTenths The speed shown on screen now, in 0.1 km/h units.
     *        Passed in rather than remembered, because the belt may have been
     *        moved by its own remote since the bridge last commanded anything.
     * @return The same emissions as setSpeedFromUi(). On a stopped belt, up
     *         starts the belt at @ref kSpeedMinTenths and down emits nothing at
     *         all — a belt that is not moving has no speed to reduce, and `-`
     *         must never become a way to issue a stop.
     */
    Outcome stepSpeedFromUi(int dir, uint8_t baseTenths);

    /**
     * @brief STOP pressed on the device, or `stop` on the console.
     *
     * @return Emit::Stop and Emit::ResetPolicy, unconditionally — STOP works
     *         armed or not, linked or not, running or not.
     * @note Also disarms. Otherwise the app's next target would override the
     *       user's physical stop, which is the one thing a stop control must
     *       never permit.
     */
    Outcome stopFromUi();

    /**
     * @brief Ask the belt to change mode.
     *
     * @param m The wanted mode: Auto, Manual or Standby.
     * @param observedRunning Whether the belt is *observed* moving, from its own
     *        status rather than @ref beltRunning(). A mode change under a
     *        moving belt is not ours to make.
     * @return Emit::SetMode with Outcome::mode = @p m, and the request becomes
     *         pending until observeMode() confirms or rejects it. Emits nothing
     *         unless the bridge is armed, the belt link is up, and the belt is
     *         not observed running.
     */
    Outcome requestMode(BeltMode m, bool observedRunning);

    /**
     * @brief Record the mode from one status frame.
     *
     * Call once per status frame. With a request pending, a frame showing the
     * requested mode confirms it; a frame still showing the mode seen when it
     * was made means "not applied yet" and counts toward
     * @ref kModeConfirmFrames; any other mode is a rejection. Both clear the
     * pending request.
     *
     * @param m The mode the frame reports.
     * @return Emit::ResetPolicy when the belt has left Auto, whoever moved it:
     *         the policy's reference was made under Auto and is stale. Nothing
     *         otherwise.
     */
    Outcome observeMode(BeltMode m);

    /**
     * @brief The mode the belt last reported.
     * @return BeltMode::Unknown before the first frame and after a belt link loss.
     */
    BeltMode observedMode() const { return observedMode_; }

    /**
     * @brief Whether the belt is in Auto.
     * @return True when @ref observedMode() is BeltMode::Auto, however it got
     *         there — an Auto set from the belt's own remote counts.
     */
    bool autoActive() const { return observedMode_ == BeltMode::Auto; }

    /**
     * @brief Whether an Auto request is waiting for confirmation.
     * @return True from the Emit::SetMode for Auto until the belt confirms,
     *         rejects or times it out.
     */
    bool autoPending() const { return pendingMode_ == BeltMode::Auto; }

private:
    bool armed_ = false;
    bool beltUp_ = false;
    bool phoneUp_ = false;
    bool running_ = false;
    BeltMode observedMode_ = BeltMode::Unknown;
    BeltMode pendingMode_ = BeltMode::Unknown;  ///< Unknown when nothing is pending.
    BeltMode pendingFrom_ = BeltMode::Unknown;  ///< Mode observed when the request was made.
    uint8_t pendingFrames_ = 0;                 ///< Unchanged frames seen since the request.
};

}  // namespace policy
