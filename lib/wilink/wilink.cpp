#include "wilink.h"

namespace wilink {

uint8_t crc(const uint8_t* body, size_t len) {
    uint8_t sum = 0;
    for (size_t i = 0; i < len; ++i) {
        sum = static_cast<uint8_t>(sum + body[i]);
    }
    return sum;
}

}  // namespace wilink
