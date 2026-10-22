#pragma once
#include <stdint.h>

/**
 * @file shot.h
 * @brief Serial screenshots: the panel's framebuffer, RLE-encoded, over USB.
 *
 * Frame format, on the USB serial console:
 *
 *     [SHOT 320 240 RLE16 <n>]
 *     S:<base64 line>        (many lines, 128 chars each, last one padded)
 *     [/SHOT]
 *
 * The base64 decodes to `<n>` runs of 3 bytes: count (1..255), then the RGB565
 * pixel value as hi byte, lo byte. Runs cover the frame row-major.
 * `tools/decode_shots.py` turns a capture into PNGs.
 *
 * Two details of that format are there to survive a *shared* console. The
 * bridge logs from several places while a frame is going out, so every payload
 * line carries the `S:` prefix and is written with a single `Serial.write()`:
 * an interleaved log line can land between two payload lines, where the
 * decoder skips it, but cannot appear inside one. And the run count is in the
 * header because the encoder walks the frame twice - once to count, once to
 * emit - so a truncated capture is detectable rather than silently short.
 *
 * `UI_SCREENSHOT` (build flag; environment `cores3_shot`) gates only the
 * *automatic* emission of one frame per UI phase change, which is what the
 * screens in `docs/screens` are built from. emit() itself is unconditional -
 * see there for why.
 */

/**
 * @def UI_SCREENSHOT
 * @brief Non-zero in a capture build, where every UI phase change emits a frame.
 *
 * Defaults to 0 so the ordinary firmware emits only on request. It does not
 * compile emit() out; nothing does.
 */
#ifndef UI_SCREENSHOT
#define UI_SCREENSHOT 0
#endif

namespace shot {

/**
 * @brief Write one frame to the serial console as an `[SHOT ...]` block.
 *
 * Built into every firmware, not just the capture build: the console's `shot`
 * is the only way to see what a user is looking at when they report that the
 * screen is not responding, and a support tool that is compiled out of the
 * build people actually run is no support tool at all. It costs nothing until
 * it is called.
 *
 * Synchronous and blocking: the whole frame is written from the calling task
 * before this returns. That is deliberate - drawing happens on the same task,
 * so no redraw can land in the middle of a frame - but it does mean a 320x240
 * capture occupies the console for as long as the bytes take at 115200 baud.
 *
 * Note the one thing this cannot rescue: the serial port has to be open
 * *already*. Opening it reboots the ESP32-S3, and a reboot replaces the screen
 * you were trying to photograph with a fresh boot. Capture a wedged screen from
 * a port that was open before it wedged.
 *
 * @param px Pointer to @p w * @p h RGB565 pixels as an M5GFX 16-bit sprite
 *           stores them: two bytes per pixel, high byte first. Must address
 *           `2 * w * h` bytes - neither the length nor the pointer is checked.
 * @param w  Frame width in pixels.
 * @param h  Frame height in pixels.
 */
void emit(const uint8_t* px, int w, int h);

}  // namespace shot
