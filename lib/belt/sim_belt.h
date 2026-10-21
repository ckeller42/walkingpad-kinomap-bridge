#pragma once
#include "belt.h"

namespace belt {

// Simulated belt modelled on measured WalkingPad R1 Pro behaviour:
//  - start() enters countdown (state 7), then ramps (6) to running (1).
//  - stop() is "set mode standby": decelerates, ends in state 4 / mode 2,
//    and never returns to state 0 by itself.
//  - start() is refused until setMode(Manual) takes the belt out of standby.
//  - setSpeed() is dropped (not stored) unless the belt is in state 6 or 1;
//    the real belt discards it during countdown and uses its own preset.
//  - Command counters count ACCEPTED commands only (each beeps on the real
//    belt); rejected/dropped commands are not counted. setMode(Standby) is
//    the same command as stop() and counts as a stop; stop() on an already
//    stopped or stopping belt is rejected.
class SimBelt : public Belt {
public:
    void setSpeed(uint8_t tenthKmh) override;
    void setMode(wilink::Mode mode) override;
    void start() override;
    void stop() override;
    bool poll(wilink::Status& out) override;
    bool linkUp() const override { return true; }

    void tick(uint32_t nowMs);
    uint32_t commandCount() const { return commands_; }   // all accepted
    uint32_t setSpeedCount() const { return speedCmds_; }
    uint32_t modeCount() const { return modeCmds_; }
    uint32_t startCount() const { return startCmds_; }
    uint32_t stopCount() const { return stopCmds_; }

private:
    static constexpr uint8_t kIdle = 0, kRunning = 1, kStandby = 4,
                             kTransition = 6, kCountdown = 7;
    static constexpr uint32_t kCountdownMs = 3000;
    static constexpr float kRampUpKmhPerSec = 0.7f;
    static constexpr float kRampDownKmhPerSec = 0.8f;
    static constexpr float kStepsPerMeter = 1.3f;

    uint8_t state_ = kIdle;
    wilink::Mode mode_ = wilink::Mode::Manual;
    uint8_t commanded_ = 0;     // tenths km/h, applied once running
    bool stopping_ = false;
    float speedKmh_ = 0.0f;
    float meters_ = 0.0f;
    float steps_ = 0.0f;
    uint32_t elapsedMs_ = 0;
    uint32_t countdownMs_ = 0;
    uint32_t lastMs_ = 0;
    bool haveLast_ = false;
    uint32_t commands_ = 0, speedCmds_ = 0, modeCmds_ = 0, startCmds_ = 0,
             stopCmds_ = 0;

    bool doStop();
    void accept(uint32_t& counter) { ++counter; ++commands_; }
};

}  // namespace belt
