#pragma once
#include <stddef.h>
#include <stdint.h>

namespace wilink {

// Frames are F7 <cmd> <payload...> <crc> FD. The CRC is the low byte of the
// sum of every byte between the header (F7) and the CRC itself.
uint8_t crc(const uint8_t* body, size_t len);

struct Frame {
    uint8_t bytes[8];
    size_t len;
};

enum class Mode : uint8_t { Auto = 0, Manual = 1, Standby = 2 };

Frame requestStatus();
Frame setSpeed(uint8_t tenthKmh);  // speed in 0.1 km/h units
Frame setMode(Mode m);
Frame startBelt();

// Decoded F8 A2 status frame. Speed is in 0.1 km/h units.
struct Status {
    uint8_t beltState;
    uint8_t tenthKmh;
    uint8_t mode;
    uint32_t elapsedSec;
    uint32_t distance10m;
    uint32_t steps;
};

// Returns false and leaves `out` untouched unless `buf` is a well-formed
// F8 A2 status frame (header and minimum length checked; no CRC check).
bool parseStatus(const uint8_t* buf, size_t len, Status& out);

}  // namespace wilink
