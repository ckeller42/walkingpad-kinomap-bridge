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
    // Returns the latest known status. Must NOT write to the belt: any write
    // belongs in requestStatus() so the pacer can see it.
    virtual bool poll(wilink::Status& out) = 0;
    // Asks the belt to produce a fresh status (BLE: a write to FE02, which is
    // subject to the same ~0.7 s rule as commands). Returns true only if a
    // write actually happened. Default: nothing to ask (SimBelt).
    virtual bool requestStatus() { return false; }
    // Age of the status poll() would return, in ms (UINT32_MAX if none yet).
    // Lets the pacer ask for a refresh only when the reading is actually
    // stale. Default 0: a belt with no request/reply round trip is always fresh.
    virtual uint32_t statusAgeMs() const { return 0; }
    virtual bool linkUp() const = 0;
    // Time-driven housekeeping (SimBelt's physics, BeltGate's pacing). Call
    // every loop pass. A no-op by default, so a belt with no clock-driven
    // state (the BLE link, which is event-driven) needs no override.
    virtual void tick(uint32_t /*nowMs*/) {}
};

}  // namespace belt
