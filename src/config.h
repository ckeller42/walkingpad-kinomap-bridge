#pragma once
/**
 * @file config.h
 * @brief Single place for the bridge's compile-time tunables.
 *
 * The POLICY_* constants live in platformio.ini build flags rather than here,
 * because lib/policy is its own translation unit and never sees this header.
 */
// Single place for the bridge's tunables.
//
// The POLICY_* constants (POLICY_DEADBAND_TENTHS, POLICY_MIN_COMMAND_INTERVAL_MS,
// ...) live in platformio.ini build_flags, NOT here: lib/policy is its own
// translation unit and never sees this header. Defaults are in speed_policy.h.

/** @def BRIDGE_ADV_NAME
 * @brief The name the bridge advertises to the phone.
 */
#define BRIDGE_ADV_NAME "WalkingPad Bridge"
// Never "decathlon": that string selects a different path inside Kinomap.
/** @def BRIDGE_MANUFACTURER
 * @brief Device Information manufacturer (2A29).
 *
 * Never "decathlon": that string selects a different code path inside
 * Kinomap.
 */
#define BRIDGE_MANUFACTURER "WalkingPad Bridge"
/** @def BRIDGE_MODEL
 * @brief Device Information model (2A24).
 */
#define BRIDGE_MODEL "R1 Pro FTMS Bridge"

// Default belt source, chosen at boot (both are always compiled in, so one
// binary serves stage 1 and stage 2): 0 = the real WalkingPad over BLE
// (BeltLink), 1 = SimBelt. Touching the screen while booting selects the
// other one. -DUSE_SIM_BELT=1 flips the default (cores3_sim environment).
#ifndef USE_SIM_BELT
/** @def USE_SIM_BELT
 * @brief Default belt source: 0 the real belt over BLE, 1 the simulated one.
 *
 * Both are always compiled in, so one binary serves bench and real
 * testing. Touching the screen during boot selects the other.
 */
#define USE_SIM_BELT 0
#endif

// A belt reading older than this is no longer trusted: the belt counts as down.
/** @def STATUS_STALE_MS
 * @brief A belt reading older than this is no longer trusted; the belt counts
 *        as down.
 *
 * Load-bearing: a run of lost status frames would declare a RUNNING belt
 * down, which is why wilink::StatusReassembler exists.
 */
#define STATUS_STALE_MS 5000

// Give up on a pending belt start if it never reaches state 1/6.
/** @def START_TIMEOUT_MS
 * @brief Give up on a pending belt start if it never reaches a running state.
 */
#define START_TIMEOUT_MS 15000

// Speed the belt starts at when the phone writes FTMS 0x07 StartResume.
// 0 disables app-initiated starts entirely.
//
// ON, at the project owner's instruction, for use with someone standing on the
// belt. Understand what it means before lowering it back to 0 or raising it:
// a write from the phone can set this belt moving. The arming gate still
// applies, so the bridge must be armed first, but once armed no further human
// action is needed between the phone's 0x07 and the motor turning.
//
// Measured on hardware: Kinomap never *initiates* 0x07 - it waits for the belt
// to report motion and otherwise only replies to our own 2ADA 0x04 - so this
// may well never fire for Kinomap. It is here for FTMS apps that do drive the
// machine, and for the Kinomap paths not yet exercised with a walker aboard.
/** @def PHONE_START_CENTI_KMH
 * @brief Speed the belt starts at when the phone writes FTMS 0x07, or 0 to
 *        ignore it.
 *
 * Measured: Kinomap never initiates 0x07, so for Kinomap this does
 * nothing either way. It is here for FTMS apps that do drive the
 * machine.
 */
#define PHONE_START_CENTI_KMH 200  // 2.0 km/h

// UDP port the session log is broadcast to. Listen with:  nc -ul 9999
// Broadcast rather than a fixed host, so the log needs no address configured
// on this side. Wi-Fi credentials are NOT here: they live in NVS, set with the
// console's "wifi <ssid> <password>", so they never reach the repository.
/** @def LOG_UDP_PORT
 * @brief UDP port the session log is broadcast to. Listen with `nc -ul 9999`.
 */
#define LOG_UDP_PORT 9999

#ifndef OTA_MANIFEST_URL
/** @def OTA_MANIFEST_URL
 * @brief Where OTA looks for the manifest when NVS holds no `ota_url`.
 *
 * The GitHub Pages deployment CI publishes on every push to main. Override
 * at build time with `-DOTA_MANIFEST_URL=\"...\"`, or per device with the
 * console's `ota url <url>`; `ota url` with no argument returns here.
 */
#define OTA_MANIFEST_URL "https://ckeller42.github.io/walkingpad-kinomap-bridge/firmware/manifest.json"
#endif
