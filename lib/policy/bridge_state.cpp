#include "bridge_state.h"

namespace policy {

namespace {
Outcome empty(uint8_t tenth = 0) {
    return Outcome{{Emit::None, Emit::None, Emit::None}, 0, tenth};
}
}  // namespace

Outcome BridgeState::setArmed(bool armed) {
    Outcome o = empty();
    if (armed && !armed_) o.emits[o.count++] = Emit::ResetPolicy;
    armed_ = armed;
    return o;
}

Outcome BridgeState::onBeltLink(bool up) {
    Outcome o = empty();
    if (up && !beltUp_) o.emits[o.count++] = Emit::ResetPolicy;
    beltUp_ = up;
    if (!up && running_) {
        running_ = false;
        o.emits[o.count++] = Emit::ResetPolicy;
    }
    return o;
}

Outcome BridgeState::onPhoneLink(bool up) {
    Outcome o = empty();
    if (up && !phoneUp_) o.emits[o.count++] = Emit::ResetPolicy;
    phoneUp_ = up;
    if (!up && running_) {  // build the outcome before clearing running_
        o.emits[o.count++] = Emit::Stop;
        o.emits[o.count++] = Emit::ResetPolicy;
        running_ = false;
    }
    return o;
}

Outcome BridgeState::onBeltObservedStopped() {
    Outcome o = empty();
    if (running_) {
        running_ = false;
        o.emits[o.count++] = Emit::ResetPolicy;
    }
    return o;
}

Outcome BridgeState::apply(const Decision& d) {
    Outcome o = empty(d.tenthKmh);
    switch (d.action) {
    case Action::None:
        break;
    case Action::Stop:
        // Stopping is always safe, armed or not.
        o.emits[o.count++] = Emit::Stop;
        running_ = false;
        break;
    case Action::SetSpeed:
        // Disarmed or either link down: accepted and ignored.
        if (!armed_ || !beltUp_ || !phoneUp_) break;
        if (running_) {
            o.emits[o.count++] = Emit::SetSpeed;
        } else {
            // A belt reconnect may re-emit StartSequence and a duplicate
            // NotifyStarted to a Kinomap session that already started. This
            // is accepted; do not add a latch, it could swallow a real restart.
            o.emits[o.count++] = Emit::StartSequence;
            o.emits[o.count++] = Emit::NotifyStarted;
            running_ = true;
        }
        break;
    }
    return o;
}

Outcome BridgeState::startFromUi(uint8_t tenthKmh) {
    Outcome o = empty(tenthKmh);
    if (!armed_ || !beltUp_ || running_) return o;
    o.emits[o.count++] = Emit::StartSequence;
    o.emits[o.count++] = Emit::NotifyStarted;
    o.emits[o.count++] = Emit::ResetPolicy;
    running_ = true;
    return o;
}

uint8_t stepTenths(uint8_t base, int dir) {
    int next;
    if (dir > 0) next = (base / kSpeedStepTenths + 1) * kSpeedStepTenths;
    else next = (base % kSpeedStepTenths) ? (base / kSpeedStepTenths) * kSpeedStepTenths
                                          : base - kSpeedStepTenths;
    if (next < kSpeedMinTenths) next = kSpeedMinTenths;
    if (next > kSpeedMaxTenths) next = kSpeedMaxTenths;
    return static_cast<uint8_t>(next);
}

Outcome BridgeState::setSpeedFromUi(uint8_t tenthKmh) {
    if (tenthKmh < kSpeedMinTenths) tenthKmh = kSpeedMinTenths;
    if (tenthKmh > kSpeedMaxTenths) tenthKmh = kSpeedMaxTenths;
    Outcome o = empty(tenthKmh);
    if (!armed_ || !beltUp_) return o;
    if (running_) {
        o.emits[o.count++] = Emit::SetSpeed;
    } else {
        o.emits[o.count++] = Emit::StartSequence;
        o.emits[o.count++] = Emit::NotifyStarted;
        running_ = true;
    }
    o.emits[o.count++] = Emit::SyncPolicy;
    return o;
}

Outcome BridgeState::stepSpeedFromUi(int dir, uint8_t baseTenths) {
    if (!running_) {
        if (dir > 0) return setSpeedFromUi(kSpeedMinTenths);
        return empty();
    }
    return setSpeedFromUi(stepTenths(baseTenths, dir));
}

Outcome BridgeState::stopFromUi() {
    Outcome o = empty();
    o.emits[o.count++] = Emit::Stop;
    o.emits[o.count++] = Emit::ResetPolicy;
    running_ = false;
    armed_ = false;
    return o;
}

}  // namespace policy
