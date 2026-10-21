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

}  // namespace ftms
