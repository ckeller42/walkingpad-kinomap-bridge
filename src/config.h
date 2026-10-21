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

// Belt source. 0 (default) = the real WalkingPad over BLE (BeltLink);
// 1 = SimBelt, for stage-1 testing with no treadmill. Override with
// -DUSE_SIM_BELT=1 (see the cores3_sim environment in platformio.ini).
#ifndef USE_SIM_BELT
#define USE_SIM_BELT 0
#endif

// Give up on a pending belt start if it never reaches state 1/6.
#define START_TIMEOUT_MS 15000
