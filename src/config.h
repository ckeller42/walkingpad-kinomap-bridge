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
// 0 disables app-initiated starts entirely, which is the default.
//
// Measured on hardware: Kinomap never initiates 0x07. It waits for the belt to
// report motion and only ever sends 0x07 in reply to our own 2ADA 0x04, so for
// Kinomap this does nothing either way. It is off by default because of what it
// would cost if it were wrong: the belt auto-stops when nobody walks on it,
// which clears running_, and a later 0x07 would then start a treadmill with
// nobody standing on it. Set it to e.g. 200 (2.0 km/h) for an FTMS app that
// does drive the machine, and only with someone on the belt.
#define PHONE_START_CENTI_KMH 0  // disabled
