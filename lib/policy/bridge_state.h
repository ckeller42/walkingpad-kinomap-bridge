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
enum class Emit { None, SetSpeed, Stop, StartSequence, NotifyStarted, ResetPolicy };

struct Outcome {
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

    Outcome apply(const Decision& d);
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
