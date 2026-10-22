#pragma once
/**
 * @file logsink.h
 * @brief Session logging to SD and to UDP, so a running session can be
 *        analysed without opening the serial port.
 *
 * Opening the serial port resets the ESP32-S3, which tears down both the belt
 * link and Kinomap's GATT link. That makes the console unusable during exactly
 * the sessions worth observing, which is why these sinks exist.
 *
 * @note This `@file` block is load-bearing for documentation coverage, not
 * decoration. Without it Doxygen extracts a header's free functions but never
 * reports them as undocumented, so they are missing from the docs *and*
 * invisible to tools/doxygen_ratchet.py. Any new header in this project needs
 * one.
 */
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

/**
 * @brief Start the sinks: mount the SD card if enabled, and begin associating
 *        if Wi-Fi credentials are stored.
 *
 * Never blocks on the network, and neither sink failing stops the other or the
 * bridge. SD probing is opt-in; see logsink::sdWantedOn().
 */
void begin();

/**
 * @brief Drain the ring buffer to the active sinks.
 * @param nowMs Current `millis()`.
 * @note Call from `loop()`. All I/O happens here and never in a BLE callback,
 *       where an SD write of tens of milliseconds would stall the very link it
 *       is trying to observe.
 */
void tick(uint32_t nowMs);

/**
 * @brief Write one timestamped line to the sinks.
 * @param fmt printf-style format; a line longer than the internal maximum is
 *        truncated rather than dropped.
 *
 * Safe from any task, including BLE callbacks: it only formats into a ring
 * buffer. The `format` attribute is deliberate — it is what makes the compiler
 * check these call sites, and it caught a mismatched argument list that had
 * put the firmware into a boot loop.
 */
void line(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

/**
 * @brief Log a wire frame as verbatim hex, e.g. `tx f7 a2 01 14 b7 fd`.
 * @param tag  Short prefix identifying the direction or kind.
 * @param data Bytes exactly as sent or received.
 * @param len  Number of bytes in @p data.
 *
 * Verbatim rather than decoded, deliberately: a decoder in the logging path
 * discards whatever it fails to understand, which is precisely the material
 * worth keeping. A status frame split across two notifications was only ever
 * visible because the raw bytes were written down.
 */
void hex(const char* tag, const uint8_t* data, size_t len);

/**
 * @brief Whether an SD card is mounted.
 * @return True when the card responded and mounted.
 */
bool sdOk();

/**
 * @brief Whether SD probing is enabled.
 *
 * Off by default. Mounting claims GPIO 35/36/37 for SPI, which on an ESP32-S3
 * are octal PSRAM pins, so a slot that has never answered is not worth probing
 * on every boot.
 *
 * @return True when SD logging is switched on.
 */
bool sdWantedOn();

/**
 * @brief Enable or disable SD probing. Takes effect after a restart.
 * @param on True to probe for a card at boot.
 */
void setSd(bool on);
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

/**
 * @brief Turn developer mode on or off, persisting it in NVS.
 * @param on True to enable the radio features.
 * @note Turning it off takes the radio down immediately, so a shipped bridge
 *       never sits on a network.
 */
void setDev(bool on);

// The UDP log broadcast itself, inside developer mode. Also persisted, also
// off by default; dev mode off means no broadcast whatever this says.
/**
 * @brief Whether the UDP log broadcast is enabled.
 * @return True when it is on; it still needs developer mode to take effect.
 */
bool udpOn();

/**
 * @brief Turn the UDP log broadcast on or off, persisting it in NVS.
 * @param on True to broadcast.
 * @note Stored either way, but nothing is broadcast outside developer mode.
 */
void setUdp(bool on);

/**
 * @brief Store Wi-Fi credentials in NVS and reconnect.
 * @param ssid Network name, or "" to forget the stored credentials.
 * @param pass Password; ignored when @p ssid is empty.
 * @note Credentials live on the device and never in the repository.
 */
void setWifi(const char* ssid, const char* pass);

/**
 * @brief Whether a connect started by setWifi() is still within its window.
 * @return True for up to 15 s after setWifi() while Wi-Fi is not yet associated.
 */
bool wifiConnecting();

/**
 * @brief Whether the last connect started by setWifi() has failed.
 * @return True when the stack reports a failed connect, or 15 s passed without
 *         association. False once connected; cleared by the next setWifi().
 */
bool wifiFailed();

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

/**
 * @brief Summarise the sinks for the console's `log` command.
 * @param out Buffer for one line of text.
 * @param cap Its capacity.
 *
 * Reports developer mode, the UDP flag, SD state and card type, the pins used,
 * the IP and the dropped-line count. `type=0` means no card responded at all,
 * which is a presence failure rather than a filesystem one.
 */
void describe(char* out, size_t cap);

}  // namespace logsink
