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
};

}  // namespace belt
