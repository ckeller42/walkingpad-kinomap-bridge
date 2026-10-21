#include "shot.h"

#if UI_SCREENSHOT
#include <Arduino.h>

namespace shot {
namespace {
const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Collects payload bytes and writes them as "S:" + base64 lines. 96 bytes
// encode to exactly 128 characters, so only the final line is padded.
struct Sink {
    uint8_t buf[96];
    size_t n = 0;
    void put(uint8_t b) {
        buf[n++] = b;
        if (n == sizeof buf) flush();
    }
    void flush() {
        if (!n) return;
        char line[2 + 128 + 1 + 1];
        size_t o = 0;
        line[o++] = 'S';
        line[o++] = ':';
        for (size_t i = 0; i < n; i += 3) {
            uint32_t v = buf[i] << 16;
            if (i + 1 < n) v |= buf[i + 1] << 8;
            if (i + 2 < n) v |= buf[i + 2];
            line[o++] = kB64[(v >> 18) & 63];
            line[o++] = kB64[(v >> 12) & 63];
            line[o++] = i + 1 < n ? kB64[(v >> 6) & 63] : '=';
            line[o++] = i + 2 < n ? kB64[v & 63] : '=';
        }
        line[o++] = '\n';
        Serial.write(reinterpret_cast<const uint8_t*>(line), o);  // one write per line
        n = 0;
    }
};

inline uint16_t px16(const uint8_t* px, uint32_t i) { return (uint16_t)(px[2 * i] << 8 | px[2 * i + 1]); }
}  // namespace

void emit(const uint8_t* px, int w, int h) {
    const uint32_t total = (uint32_t)w * h;
    uint32_t runs = 0;
    for (uint32_t i = 0; i < total;) {
        uint32_t j = i + 1;
        while (j < total && j - i < 255 && px16(px, j) == px16(px, i)) ++j;
        ++runs;
        i = j;
    }
    char head[64];
    int hl = snprintf(head, sizeof head, "\n[SHOT %d %d RLE16 %u]\n", w, h, (unsigned)runs);
    Serial.write(reinterpret_cast<const uint8_t*>(head), hl);
    Sink sink;
    for (uint32_t i = 0; i < total;) {
        const uint16_t c = px16(px, i);
        uint32_t j = i + 1;
        while (j < total && j - i < 255 && px16(px, j) == c) ++j;
        sink.put((uint8_t)(j - i));
        sink.put((uint8_t)(c >> 8));
        sink.put((uint8_t)(c & 0xFF));
        i = j;
    }
    sink.flush();
    Serial.write(reinterpret_cast<const uint8_t*>("[/SHOT]\n"), 8);
}
}  // namespace shot
#endif
