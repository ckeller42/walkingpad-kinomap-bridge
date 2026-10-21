#pragma once
#include <stddef.h>
#include <stdint.h>

namespace wilink {

// Frames are F7 <cmd> <payload...> <crc> FD. The CRC is the low byte of the
// sum of every byte between the header (F7) and the CRC itself.
uint8_t crc(const uint8_t* body, size_t len);

}  // namespace wilink
