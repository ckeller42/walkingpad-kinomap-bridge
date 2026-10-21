#include "speed_policy.h"

namespace policy {

namespace {
uint8_t absDiff(uint8_t a, uint8_t b) { return static_cast<uint8_t>(a > b ? a - b : b - a); }
}  // namespace

Decision SpeedPolicy::onTarget(uint16_t centiKmh, uint32_t nowMs) {
    // The sentinel and anything slower than the advertised minimum are stops.
    if (centiKmh == kStopSentinelCentiKmh || centiKmh < kMinCentiKmh) {
        if (stopped_) return {Action::None, 0};  // redundant command = redundant beep
        stopped_ = true;
        hasRef_ = false;                         // belt is at rest; next target is fresh
        divergentPolls_ = 0;
        prevTenths_ = 0;
        hasSent_ = true;
        lastSendMs_ = nowMs;
        return {Action::Stop, 0};
    }

    if (centiKmh > kMaxCentiKmh) centiKmh = kMaxCentiKmh;
    const uint8_t tenths = static_cast<uint8_t>(centiKmh / 10);

    if (hasRef_ && absDiff(tenths, refTenths_) < kDeadbandTenths) return {Action::None, 0};
    if (hasSent_ && nowMs - lastSendMs_ < kMinCommandIntervalMs) return {Action::None, 0};

    hasRef_ = true;
    refTenths_ = tenths;
    stopped_ = false;
    divergentPolls_ = 0;
    hasSent_ = true;
    lastSendMs_ = nowMs;
    return {Action::SetSpeed, tenths};
}

void SpeedPolicy::onBeltSpeed(uint8_t tenthKmh) {
    if (!hasRef_) {
        // No reference (after a Stop, or never commanded): a belt that is moving
        // was started by the remote. A falling sample restarts the run so the
        // deceleration ramp after our own Stop is not adopted. Flat runs count,
        // since a belt already at speed has no rise to observe.
        if (tenthKmh == 0) {
            divergentPolls_ = 0;
            prevTenths_ = 0;
            return;
        }
        divergentPolls_ = (tenthKmh < prevTenths_) ? 1 : static_cast<uint8_t>(divergentPolls_ + 1);
        prevTenths_ = tenthKmh;
        if (divergentPolls_ >= kResyncPolls) {
            refTenths_ = tenthKmh;
            hasRef_ = true;
            stopped_ = false;
            divergentPolls_ = 0;
        }
        return;
    }

    // Agreement is the exact complement of what onTarget would send.
    if (absDiff(tenthKmh, refTenths_) < kDeadbandTenths) {
        divergentPolls_ = 0;
        return;
    }
    if (++divergentPolls_ >= kResyncPolls) {
        refTenths_ = tenthKmh;
        stopped_ = (tenthKmh == 0);
        divergentPolls_ = 0;
    }
}

void SpeedPolicy::reset() { *this = SpeedPolicy(); }

}  // namespace policy
