#pragma once
#include <stdint.h>

#include "belt.h"

// Paces every belt write at least `minGapMs` apart (the real belt silently
// drops a command that follows another too closely, and FE02 is
// write-without-response, so a dropped one is never reported). Wraps any
// belt::Belt, so a BLE-client belt slots in underneath unchanged.
//
// Queue depth is 1 per command type: a newer speed supersedes an older one.
// Send order is stop > mode > start > speed. stop() discards everything
// queued and is re-sent until a poll shows the belt stopped (speed 0, state
// 0 or 4), because a lost stop leaves the belt running with nobody holding it.
// Plain C++, no Arduino types; time arrives through tick().
namespace belt {

class BeltGate : public Belt {
public:
    BeltGate(Belt& inner, uint32_t minGapMs, uint32_t stopRetryMs = 3000)
        : inner_(inner), gapMs_(minGapMs), retryMs_(stopRetryMs) {}

    void setSpeed(uint8_t tenthKmh) override;
    void setMode(wilink::Mode mode) override;
    void start() override;
    void stop() override;
    bool poll(wilink::Status& out) override;
    bool linkUp() const override { return inner_.linkUp(); }

    // Sends at most one queued command if the gap since the last write has
    // elapsed. Call often (every loop pass is fine).
    void tick(uint32_t nowMs);

    bool stopOutstanding() const { return stopWanted_; }

private:
    Belt& inner_;
    uint32_t gapMs_, retryMs_;
    uint32_t now_ = 0, lastWriteMs_ = 0, lastStopMs_ = 0;
    bool haveWritten_ = false;

    bool stopWanted_ = false;   // stop queued or sent but not yet confirmed
    bool stopQueued_ = false;   // needs (re)sending
    bool stopSent_ = false;
    uint8_t speedAtStop_ = 0;

    bool modeQueued_ = false;
    wilink::Mode mode_ = wilink::Mode::Manual;
    bool startQueued_ = false;
    bool speedQueued_ = false;
    uint8_t speed_ = 0;
    uint8_t lastSpeed_ = 0;
    bool haveStatus_ = false;

    void cancelStop() { stopWanted_ = stopQueued_ = stopSent_ = false; }
    void wrote() { haveWritten_ = true; lastWriteMs_ = now_; }
};

}  // namespace belt
