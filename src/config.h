#pragma once
// Single place for the bridge's tunables. Include before speed_policy.h.

#define BRIDGE_ADV_NAME "WalkingPad Bridge"
#define BRIDGE_MANUFACTURER "WalkingPad Kinomap Bridge"
#define BRIDGE_MODEL "R1 Pro FTMS Bridge"

#ifndef USE_SIM_BELT
#define USE_SIM_BELT 1
#endif

// Policy overrides (consumed by lib/policy/speed_policy.h).
#define POLICY_DEADBAND_TENTHS 3
#define POLICY_MIN_COMMAND_INTERVAL_MS 1000

// Give up on a pending belt start if it never reaches state 1/6.
#define START_TIMEOUT_MS 15000
