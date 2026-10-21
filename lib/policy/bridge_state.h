#pragma once
#include <stddef.h>
#include <stdint.h>
#include "speed_policy.h"

// Turns a SpeedPolicy decision into what the adapters must actually emit,
// given whether the user armed the bridge and whether the belt is running.
// Arming gates speed-driven starts (Kinomap never sends FTMS Start/Stop).
// Plain C++, no Arduino types.
namespace policy {

// ResetPolicy: the caller MUST call SpeedPolicy::reset() when it sees this.
// BridgeState dropped or bypassed a decision (or moved the belt itself), so
// SpeedPolicy's idea of the last-sent speed is stale; without a reset an
// unchanged app target would be deadbanded and never act on the new state.
//
// SyncPolicy: the caller MUST call SpeedPolicy::adopt(Outcome::tenthKmh, now).
// The user changed the speed by hand; the policy reference follows it.
enum class Emit { None, SetSpeed, Stop, StartSequence, NotifyStarted, ResetPolicy, SyncPolicy };

// Manual speed control (UI +/- and console `speed`): 0.5 km/h steps, clamped
// to the advertised range, in 0.1 km/h units.
constexpr uint8_t kSpeedStepTenths = 5;
constexpr uint8_t kSpeedMinTenths = POLICY_MIN_CENTI_KMH / 10;
constexpr uint8_t kSpeedMaxTenths = POLICY_MAX_CENTI_KMH / 10;
// Next step from base: up goes to the next multiple of 0.5 above base, down to
// the next one below it; the result is clamped to [min, max] (never a stop).
uint8_t stepTenths(uint8_t baseTenths, int dir);

struct [[nodiscard]] Outcome {
    Emit emits[3];
    size_t count;
    uint8_t tenthKmh;  // valid for SetSpeed / StartSequence
};

class BridgeState {
public:
    // Arming alone never starts the belt; only the next target does. Emits
    // ResetPolicy on false->true. Disarming mid-run deliberately leaves the
    // belt moving: disarm is not stop (Kinomap's 1.00 sentinel and UI STOP
    // still stop it).
    Outcome setArmed(bool armed);
    bool armed() const { return armed_; }

    // Link-up (down->up transition) also emits ResetPolicy: targets dropped
    // while a link was down must not be deadbanded once it returns.
    // Belt loss: running state is unknown, so it is cleared (emits ResetPolicy
    // if it was running). After reconnect the belt is treated as stopped until
    // a fresh armed target starts it.
    Outcome onBeltLink(bool up);
    // Phone loss on a running belt emits Stop, ResetPolicy; the bridge stays armed.
    // Nothing is emitted if the belt was not running.
    Outcome onPhoneLink(bool up);

    bool beltRunning() const { return running_; }
    // True while 2ACD Treadmill Data should be sent (both links up).
    bool notifying() const { return beltUp_ && phoneUp_; }

    // The caller saw the belt stopped (speed 0, state 0/4) while beltRunning()
    // was true and no start was in flight, or a start never took. apply()
    // sets running_ optimistically and Belt::start() cannot report refusal, so
    // without this a refused/lost start wedges running_ true and no later
    // target ever emits StartSequence. Clears running_, emits ResetPolicy.
    // Emits nothing if the belt was not marked running.
    Outcome onBeltObservedStopped();

    Outcome apply(const Decision& d);
    // UI START: begins a session from the bridge itself, because Kinomap sends
    // no target until the belt reports movement. Only while armed, belt link
    // up and not already running; otherwise emits nothing. Emits
    // StartSequence, NotifyStarted (the 2ADA 0x04 that makes Kinomap begin its
    // session) and ResetPolicy, so the first Kinomap target is not deadbanded.
    Outcome startFromUi(uint8_t tenthKmh);
    // Manual speed (UI +/- and console). Only while armed with the belt link
    // up; otherwise emits nothing. tenths is clamped to the advertised range.
    // Running: SetSpeed + SyncPolicy. Stopped: runs the normal start sequence
    // at that speed (StartSequence, NotifyStarted, SyncPolicy).
    Outcome setSpeedFromUi(uint8_t tenthKmh);
    // One 0.5 km/h step from baseTenths (the speed the user sees now). Stopped:
    // + starts at the minimum, - does nothing.
    Outcome stepSpeedFromUi(int dir, uint8_t baseTenths);
    // UI STOP: emits Stop, ResetPolicy and also disarms, so the app cannot
    // override the user's physical stop with its next target.
    Outcome stopFromUi();

private:
    bool armed_ = false;
    bool beltUp_ = false;
    bool phoneUp_ = false;
    bool running_ = false;
};

}  // namespace policy
