#pragma once
// Session logging to SD and to UDP, so a running session can be analysed
// without opening the serial port.
//
// Opening the serial port resets the ESP32-S3, which tears down both the belt
// link and Kinomap's GATT link. That makes the console unusable during exactly
// the sessions worth observing: every finding from a live run has to be
// inferred from the phone's screen instead of counted. These two sinks exist to
// close that gap - SD survives a reboot and answers "what happened before it
// died", UDP is live and answers "what is happening right now".
//
// line() is safe to call from any task, including BLE callbacks: it only
// formats into a ring buffer. All real I/O happens in tick(), from loop().

#include <stddef.h>
#include <stdint.h>

namespace logsink {

// Mounts the SD card and, if Wi-Fi credentials are stored, starts connecting.
// Never blocks on the network; neither sink failing stops the other or the
// bridge.
void begin();

// Drains the ring buffer to SD and UDP. Call from loop().
void tick(uint32_t nowMs);

// printf-style, one line, timestamped with millis(). A line longer than
// kMaxLine is truncated rather than dropped.
void line(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// "tag f7 a2 01 14 b7 fd" - for wire frames, the thing worth having verbatim.
void hex(const char* tag, const uint8_t* data, size_t len);

/**
 * @brief Whether an SD card is mounted.
 * @return True when the card responded and mounted.
 */
bool sdOk();
/**
 * @brief Whether Wi-Fi is usable.
 * @return True when developer mode is on and the bridge is associated.
 */
bool netOk();

// Developer mode is the master switch for everything with a radio: the Wi-Fi
// pairing UI, the UDP log broadcast and OTA. A shipped firmware has it OFF, so
// the bridge never brings up Wi-Fi at all, and it is persisted in NVS so it
// survives a reboot. Turning it off takes the radio down with it.
/**
 * @brief Whether developer mode is on.
 * @return True when the radio features are enabled.
 */
bool devOn();
void setDev(bool on);

// The UDP log broadcast itself, inside developer mode. Also persisted, also
// off by default; dev mode off means no broadcast whatever this says.
/**
 * @brief Whether the UDP log broadcast is enabled.
 * @return True when it is on; it still needs developer mode to take effect.
 */
bool udpOn();
void setUdp(bool on);

// Stores Wi-Fi credentials in NVS and reconnects. Credentials live on the
// device, never in the repo. Pass an empty ssid to forget them.
void setWifi(const char* ssid, const char* pass);

/**
 * @brief The network the bridge will associate with.
 * @return Its SSID, or "" when none is stored.
 */
const char* storedSsid();

/**
 * @brief The bridge's address on the network.
 * @return The IP as text, or "" when not associated.
 */
const char* ipText();

/**
 * @brief Begin an asynchronous Wi-Fi scan.
 *
 * Does nothing outside developer mode, since the radio is down. Asynchronous
 * because a blocking scan takes seconds, and the UI has to keep drawing - a
 * frozen screen during setup reads as a crash.
 */
void startScan();

/**
 * @brief Whether a Wi-Fi scan is still running.
 * @return True while one is in progress.
 */
bool scanning();

/**
 * @brief How many networks the last scan found.
 * @return The count, strongest first, capped at the stored maximum.
 */
uint8_t scanCount();

/**
 * @brief One scan result.
 * @param i Zero-based index.
 * @return Its SSID, or "" when @p i is out of range.
 */
const char* scanSsid(uint8_t i);

// One line for the console: sinks, the SD file in use, dropped-line count.
void describe(char* out, size_t cap);

}  // namespace logsink
