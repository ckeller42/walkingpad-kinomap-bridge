#pragma once
#include <stddef.h>
#include <stdint.h>

// Byte payloads for the Bluetooth Fitness Machine Service characteristics the
// bridge serves to the phone. Plain C++, no Arduino types.
namespace ftms {

struct Buf {
    uint8_t bytes[8];
    size_t len;
};

// 2ACC Fitness Machine Feature: 8 bytes. Bytes 0-3 are the Fitness Machine
// Features bitfield (none reported); bytes 4-7 are the Target Setting Features
// bitfield (speed target only). All uint32 little-endian.
Buf featureValue();

// 2AD4 Supported Speed Range: min 0.50, max 6.00, increment 0.10 km/h,
// each uint16 little-endian in 0.01 km/h units.
Buf supportedSpeedRange();

// 2ACD Treadmill Data: flags 0x0000 followed by instantaneous speed as uint16
// little-endian in 0.01 km/h units.
Buf treadmillData(uint16_t centiKmh);

// Fitness Machine Control Point response payload for opcode 0x04 (Start or Resume).
Buf statusStarted();

// Fitness Machine Control Point opcodes the bridge handles. Unknown is the
// result for any opcode not listed here.
enum class CpOp : uint8_t {
    RequestControl = 0x00,
    SetTargetSpeed = 0x02,
    StartResume = 0x07,
    StopPause = 0x08,
    Unknown = 0xFF,
};

// A decoded write to the Control Point (2AD9). centiKmh is only meaningful for
// SetTargetSpeed; param is only meaningful for StopPause. valid is false when
// the buffer is too short for the opcode's required payload.
struct CpRequest {
    CpOp op;
    uint16_t centiKmh;
    uint8_t param;
    bool valid;
};

// Decodes a Control Point write. Never reads past len; a null buf is treated
// as empty. Unrecognised opcodes return op = Unknown with valid = true so the
// caller can still acknowledge them.
CpRequest parseControlPoint(const uint8_t* buf, size_t len);

// Success indication payload for a Control Point request: 0x80, the request
// opcode, then 0x01 (Success).
Buf controlPointSuccess(CpOp op);

}  // namespace ftms
