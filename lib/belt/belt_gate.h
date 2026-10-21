#pragma once
#include <stdint.h>

#include "belt.h"

// Paces every belt write at least `minGapMs` apart (the real belt silently
// drops a command that follows another too closely, and FE02 is
// write-without-response, so a dropped one is never reported). Wraps any
// belt::Belt, so a BLE-client belt slots in underneath unchanged.
//
// Queue depth is 1 per command type: a newer speed supersedes an older one.
// Send order is stop > mode > start > speed > status request: a command is a
// user intention, a status request is housekeeping. The request is an FE02
// write like any other, so it shares the gap. poll() owes one only when the
// reading is stale (> kStatusRefreshMs), which decouples the request cadence
// from the loop cadence; past kStatusUrgentMs it jumps ahead of commands
// (still below stop) so commands cannot starve status. stop() discards everything
// queued and is re-sent until a poll shows the belt stopped (speed 0, state
// 0 or 4), because a lost stop leaves the belt running with nobody holding it.
// stop() always writes: a belt that keeps running outranks one extra beep.
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
    uint32_t statusAgeMs() const override { return inner_.statusAgeMs(); }

    // A status refresh is owed once the reading is older than this...
    static constexpr uint32_t kStatusRefreshMs = 1500;
    // ...and outranks queued commands (never a stop) once older than this, so
    // a steady stream of commands cannot starve status forever.
    static constexpr uint32_t kStatusUrgentMs = 3000;

    // Sends at most one queued command if the gap since the last write has
    // elapsed, and ticks the wrapped belt. Call often (every loop pass is fine).
    void tick(uint32_t nowMs) override;

private:
    Belt& inner_;
    uint32_t gapMs_, retryMs_;
    uint32_t now_ = 0, lastWriteMs_ = 0, lastStopMs_ = 0;
    bool haveWritten_ = false;
    bool pollWanted_ = false;   // a status refresh is owed (paced like a command)
    bool pollUrgent_ = false;

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

    void sendStatusRequest();
    void cancelStop() { stopWanted_ = stopQueued_ = stopSent_ = false; }
    void wrote() { haveWritten_ = true; lastWriteMs_ = now_; }
};

}  // namespace belt
