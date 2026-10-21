#include "sim_belt.h"

namespace belt {

void SimBelt::setSpeed(uint8_t tenthKmh) {
    ++commands_;
    commanded_ = tenthKmh;
}

void SimBelt::setMode(wilink::Mode mode) {
    if (mode == wilink::Mode::Standby) { stop(); return; }
    // Leaving standby is only possible once the belt has fully stopped.
    if (state_ == kStandby) { state_ = kIdle; mode_ = mode; return; }
    if (state_ == kIdle) mode_ = mode;
}

void SimBelt::start() {
    if (state_ != kIdle || mode_ == wilink::Mode::Standby) return;
    state_ = kCountdown;
    stopping_ = false;
    countdownMs_ = 0;
    elapsedMs_ = 0;
    meters_ = 0.0f;
    steps_ = 0.0f;
}

void SimBelt::stop() {
    mode_ = wilink::Mode::Standby;
    if (state_ == kTransition || state_ == kRunning) {
        stopping_ = true;
        state_ = kTransition;
    } else {
        state_ = kStandby;
    }
}

void SimBelt::tick(uint32_t nowMs) {
    if (!haveLast_) { lastMs_ = nowMs; haveLast_ = true; return; }
    const uint32_t dtMs = nowMs - lastMs_;
    lastMs_ = nowMs;
    const float dt = dtMs / 1000.0f;

    if (state_ == kCountdown) {
        countdownMs_ += dtMs;
        if (countdownMs_ >= kCountdownMs) state_ = kTransition;
        return;
    }
    if (state_ != kTransition && state_ != kRunning) return;

    const float target = stopping_ ? 0.0f : commanded_ / 10.0f;
    if (speedKmh_ < target) {
        speedKmh_ += kRampUpKmhPerSec * dt;
        if (speedKmh_ > target) speedKmh_ = target;
    } else if (speedKmh_ > target) {
        speedKmh_ -= kRampDownKmhPerSec * dt;
        if (speedKmh_ < target) speedKmh_ = target;
    }

    const float m = speedKmh_ / 3.6f * dt;
    meters_ += m;
    steps_ += m * kStepsPerMeter;
    elapsedMs_ += dtMs;

    if (stopping_) {
        if (speedKmh_ <= 0.0f) { stopping_ = false; state_ = kStandby; }
    } else {
        state_ = (speedKmh_ == target) ? kRunning : kTransition;
    }
}

bool SimBelt::poll(wilink::Status& out) {
    out.beltState = state_;
    out.tenthKmh = static_cast<uint8_t>(speedKmh_ * 10.0f + 0.5f);
    out.mode = static_cast<uint8_t>(mode_);
    out.elapsedSec = elapsedMs_ / 1000;
    out.distance10m = static_cast<uint32_t>(meters_ / 10.0f);
    out.steps = static_cast<uint32_t>(steps_);
    return true;
}

}  // namespace belt
