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

namespace {

constexpr uint8_t kStatusHeader0 = 0xF8;
constexpr uint8_t kStatusHeader1 = 0xA2;
constexpr size_t kStatusLen = 14;

uint32_t readBe24(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 16) |
           (static_cast<uint32_t>(p[1]) << 8) |
           static_cast<uint32_t>(p[2]);
}

}  // namespace

bool parseStatus(const uint8_t* buf, size_t len, Status& out) {
    if (buf == nullptr || len < kStatusLen) return false;
    if (buf[0] != kStatusHeader0 || buf[1] != kStatusHeader1) return false;

    out.beltState = buf[2];
    out.tenthKmh = buf[3];
    out.mode = buf[4];
    out.elapsedSec = readBe24(&buf[5]);
    out.distance10m = readBe24(&buf[8]);
    out.steps = readBe24(&buf[11]);
    return true;
}

namespace {
// A chunk starts a frame when it carries the F8 A2 header. Both bytes are
// checked when both are present, so a continuation that happens to begin 0xF8
// is not mistaken for a new frame.
bool looksLikeStart(const uint8_t* data, size_t len) {
    if (len == 0 || data[0] != kStatusHeader0) return false;
    return len < 2 || data[1] == kStatusHeader1;
}
}  // namespace

void StatusReassembler::reset() { len_ = 0; }

StatusReassembler::Result StatusReassembler::feed(const uint8_t* data, size_t len,
                                                  Status& out) {
    if (data == nullptr || len == 0) return Result::Ignored;

    if (looksLikeStart(data, len)) {
        // A new frame abandons whatever was pending: the old one lost its tail
        // and will never complete.
        if (len_ != 0) ++discarded_;
        len_ = 0;
        if (parseStatus(data, len, out)) return Result::Parsed;  // whole frame
        if (len > kCap) {
            ++discarded_;
            return Result::Ignored;
        }
        for (size_t i = 0; i < len; ++i) buf_[i] = data[i];
        len_ = len;
        return Result::Buffered;
    }

    if (len_ == 0) return Result::Ignored;  // stray continuation, nothing to join

    if (len_ + len > kCap) {  // cannot be a status frame; give up on it
        ++discarded_;
        len_ = 0;
        return Result::Ignored;
    }
    for (size_t i = 0; i < len; ++i) buf_[len_ + i] = data[i];
    len_ += len;

    if (parseStatus(buf_, len_, out)) {
        ++reassembled_;
        len_ = 0;
        return Result::Parsed;
    }
    return Result::Buffered;
}

}  // namespace wilink
