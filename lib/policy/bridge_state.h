#pragma once
#include <stddef.h>
#include <stdint.h>
#include "speed_policy.h"

// Turns a SpeedPolicy decision into what the adapters must actually emit,
// given whether the user armed the bridge and whether the belt is running.
// Arming gates speed-driven starts (Kinomap never sends FTMS Start/Stop).
// Plain C++, no Arduino types.
namespace policy {

enum class Emit { None, SetSpeed, Stop, StartSequence, NotifyStarted };

struct Outcome {
    Emit emits[3];
    size_t count;
    uint8_t tenthKmh;  // valid for SetSpeed / StartSequence
};

class BridgeState {
public:
    void setArmed(bool armed) { armed_ = armed; }
    bool armed() const { return armed_; }

    // Belt loss: running state is unknown, so it is cleared. After reconnect the
    // belt is treated as stopped until a fresh armed target starts it.
    void onBeltLink(bool up);
    // Phone loss stops a running belt (caller: check beltRunning() BEFORE calling
    // to know whether a belt Stop must be sent) and leaves the bridge armed.
    void onPhoneLink(bool up);

    bool beltRunning() const { return running_; }
    // True while 2ACD Treadmill Data should be sent (both links up).
    bool notifying() const { return beltUp_ && phoneUp_; }

    Outcome apply(const Decision& d);
    Outcome stopFromUi();

private:
    bool armed_ = false;
    bool beltUp_ = false;
    bool phoneUp_ = false;
    bool running_ = false;
};

}  // namespace policy
