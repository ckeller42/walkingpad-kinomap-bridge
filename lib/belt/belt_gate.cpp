#include "belt_gate.h"

namespace belt {

void BeltGate::setSpeed(uint8_t tenthKmh) {
    cancelStop();
    speed_ = tenthKmh;
    speedQueued_ = true;
}

void BeltGate::setMode(wilink::Mode mode) {
    if (mode == wilink::Mode::Standby) { stop(); return; }  // same command as stop
    cancelStop();
    mode_ = mode;
    modeQueued_ = true;
}

void BeltGate::start() {
    cancelStop();
    startQueued_ = true;
}

void BeltGate::stop() {
    modeQueued_ = startQueued_ = speedQueued_ = false;
    stopWanted_ = stopQueued_ = true;
    stopSent_ = false;
}

bool BeltGate::poll(wilink::Status& out) {
    if (!inner_.poll(out)) return false;
    lastSpeed_ = out.tenthKmh;
    const bool idle = out.tenthKmh == 0 && (out.beltState == 0 || out.beltState == 4);
    if (stopWanted_ && stopSent_) {
        if (idle) {
            cancelStop();  // confirmed stopped
        } else if (now_ - lastStopMs_ >= retryMs_ && out.tenthKmh >= speedAtStop_) {
            stopQueued_ = true;  // no sign of deceleration: the stop was lost
        }
    }
    return true;
}

void BeltGate::tick(uint32_t nowMs) {
    inner_.tick(nowMs);
    now_ = nowMs;
    if (haveWritten_ && now_ - lastWriteMs_ < gapMs_) return;
    if (stopQueued_) {
        inner_.stop();
        stopQueued_ = false;
        stopSent_ = true;
        lastStopMs_ = now_;
        speedAtStop_ = lastSpeed_;
        wrote();
    } else if (modeQueued_) {
        inner_.setMode(mode_);
        modeQueued_ = false;
        wrote();
    } else if (startQueued_) {
        inner_.start();
        startQueued_ = false;
        wrote();
    } else if (speedQueued_) {
        inner_.setSpeed(speed_);
        speedQueued_ = false;
        wrote();
    }
}

}  // namespace belt
