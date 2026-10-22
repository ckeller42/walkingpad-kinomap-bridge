#include "battery.h"

namespace battery {

State classify(int pct, bool charging) {
    if (pct < 0) return State::Unknown;        // no cell / unreadable wins over everything
    if (charging) return State::Charging;      // recovering: don't warn while it climbs
    if (pct <= kCriticalPct) return State::Critical;
    if (pct <= kLowPct) return State::Low;
    return State::Ok;
}

}  // namespace battery
