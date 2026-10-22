#pragma once

/**
 * @file battery.h
 * @brief Classify the bridge's own (CoreS3) battery into a display state.
 *
 * Pure policy: no Arduino, no M5Unified. `src/main.cpp` reads the AXP2101 PMIC
 * through `M5.Power` and passes the raw percent plus the charging flag here;
 * `src/ui.cpp` turns the returned State into a colour and a glyph. Keeping the
 * thresholds here makes them host-testable with no hardware, the same way the
 * speed and arming policy live in `lib/policy`.
 */

namespace battery {

/// Where the charge sits, for the top-bar indicator.
enum class State {
    Unknown,   ///< No battery / unreadable (running on USB with no cell): draw nothing.
    Charging,  ///< On external power and charging; dominates the level states.
    Ok,        ///< Comfortably above the low mark.
    Low,       ///< At or below kLowPct: warn (amber).
    Critical,  ///< At or below kCriticalPct: the BLE session may drop soon (red).
};

/// At or below this percent is Low.
constexpr int kLowPct = 20;
/// At or below this percent is Critical.
constexpr int kCriticalPct = 10;

/**
 * @brief Classify a battery reading.
 * @param pct      Charge 0..100, or < 0 when unknown / no battery present.
 * @param charging True while on external power and charging.
 * @return The display State. A negative @p pct is always Unknown; otherwise a
 *         charging cell is Charging regardless of level, and a discharging cell
 *         is Critical (<= kCriticalPct), Low (<= kLowPct), or Ok.
 */
State classify(int pct, bool charging);

}  // namespace battery
