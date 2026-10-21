#pragma once
#include <stdint.h>
#include "wilink.h"

namespace belt {

// Treadmill belt abstraction. Implemented by SimBelt (hardware-free) and,
// later, by a BLE link to the real WalkingPad.
class Belt {
public:
    virtual ~Belt() = default;
    virtual void setSpeed(uint8_t tenthKmh) = 0;
    virtual void setMode(wilink::Mode mode) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool poll(wilink::Status& out) = 0;
    virtual bool linkUp() const = 0;
    // Time-driven housekeeping (SimBelt's physics, BeltGate's pacing). Call
    // every loop pass. A no-op by default, so a belt with no clock-driven
    // state (the BLE link, which is event-driven) needs no override.
    virtual void tick(uint32_t /*nowMs*/) {}
};

}  // namespace belt
