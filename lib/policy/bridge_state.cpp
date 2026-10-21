#include "bridge_state.h"

namespace policy {

void BridgeState::onBeltLink(bool up) {
    beltUp_ = up;
    if (!up) running_ = false;
}

void BridgeState::onPhoneLink(bool up) {
    phoneUp_ = up;
    if (!up) running_ = false;
}

Outcome BridgeState::apply(const Decision& d) {
    Outcome o = {{Emit::None, Emit::None, Emit::None}, 0, d.tenthKmh};
    switch (d.action) {
    case Action::None:
        break;
    case Action::Stop:
        // Stopping is always safe, armed or not.
        o.emits[o.count++] = Emit::Stop;
        running_ = false;
        break;
    case Action::SetSpeed:
        // Disarmed (or belt unreachable): accepted and ignored.
        if (!armed_ || !beltUp_) break;
        if (running_) {
            o.emits[o.count++] = Emit::SetSpeed;
        } else {
            o.emits[o.count++] = Emit::StartSequence;
            o.emits[o.count++] = Emit::NotifyStarted;
            running_ = true;
        }
        break;
    }
    return o;
}

Outcome BridgeState::stopFromUi() {
    Outcome o = {{Emit::Stop, Emit::None, Emit::None}, 1, 0};
    running_ = false;
    return o;
}

}  // namespace policy
