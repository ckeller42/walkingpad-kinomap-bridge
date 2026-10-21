#pragma once
#include <stdint.h>

// Serial screenshots (build flag UI_SCREENSHOT=1, env cores3_shot).
//
// Frame format, one per UI phase change, on the USB serial console:
//
//   [SHOT 320 240 RLE16 <n>]
//   S:<base64 line>        (many lines, 128 chars each, last one padded)
//   [/SHOT]
//
// The base64 decodes to <n> runs of 3 bytes: count (1..255), then the RGB565
// pixel value as hi byte, lo byte. Runs cover the frame row-major. Every
// payload line carries the "S:" prefix and is written with a single write, so
// an interleaved log line cannot corrupt a frame. tools/decode_shots.py turns
// a capture into PNGs.
#ifndef UI_SCREENSHOT
#define UI_SCREENSHOT 0
#endif

namespace shot {
#if UI_SCREENSHOT
// px: w*h RGB565 pixels as stored by an M5GFX 16-bit sprite (big-endian bytes).
void emit(const uint8_t* px, int w, int h);
#else
inline void emit(const uint8_t*, int, int) {}
#endif
}  // namespace shot
