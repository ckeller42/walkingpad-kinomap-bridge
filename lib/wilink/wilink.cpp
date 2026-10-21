#include "wilink.h"

namespace wilink {

uint8_t crc(const uint8_t* body, size_t len) {
    uint8_t sum = 0;
    for (size_t i = 0; i < len; ++i) {
        sum = static_cast<uint8_t>(sum + body[i]);
    }
    return sum;
}

namespace {

constexpr uint8_t kHeader = 0xF7;
constexpr uint8_t kCommand = 0xA2;
constexpr uint8_t kTrailer = 0xFD;

// Builds F7 A2 <p0> <p1> <crc> FD.
Frame build(uint8_t p0, uint8_t p1) {
    Frame f{};
    f.bytes[0] = kHeader;
    f.bytes[1] = kCommand;
    f.bytes[2] = p0;
    f.bytes[3] = p1;
    f.bytes[4] = crc(&f.bytes[1], 3);
    f.bytes[5] = kTrailer;
    f.len = 6;
    return f;
}

}  // namespace

Frame requestStatus() { return build(0x00, 0x00); }

Frame setSpeed(uint8_t tenthKmh) { return build(0x01, tenthKmh); }

Frame setMode(Mode m) { return build(0x02, static_cast<uint8_t>(m)); }

Frame startBelt() { return build(0x04, 0x01); }

}  // namespace wilink
