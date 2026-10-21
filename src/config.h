#pragma once
// Single place for the bridge's tunables.
//
// The POLICY_* constants (POLICY_DEADBAND_TENTHS, POLICY_MIN_COMMAND_INTERVAL_MS,
// ...) live in platformio.ini build_flags, NOT here: lib/policy is its own
// translation unit and never sees this header. Defaults are in speed_policy.h.

#define BRIDGE_ADV_NAME "WalkingPad Bridge"
// Never "decathlon": that string selects a different path inside Kinomap.
#define BRIDGE_MANUFACTURER "WalkingPad Bridge"
#define BRIDGE_MODEL "R1 Pro FTMS Bridge"

// Default belt source, chosen at boot (both are always compiled in, so one
// binary serves stage 1 and stage 2): 0 = the real WalkingPad over BLE
// (BeltLink), 1 = SimBelt. Touching the screen while booting selects the
// other one. -DUSE_SIM_BELT=1 flips the default (cores3_sim environment).
#ifndef USE_SIM_BELT
#define USE_SIM_BELT 0
#endif

// A belt reading older than this is no longer trusted: the belt counts as down.
#define STATUS_STALE_MS 5000

// Give up on a pending belt start if it never reaches state 1/6.
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
#define PHONE_START_CENTI_KMH 200  // 2.0 km/h
