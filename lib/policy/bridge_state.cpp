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

Outcome BridgeState::stopFromUi() {
    Outcome o = empty();
    o.emits[o.count++] = Emit::Stop;
    o.emits[o.count++] = Emit::ResetPolicy;
    running_ = false;
    armed_ = false;
    return o;
}

}  // namespace policy
