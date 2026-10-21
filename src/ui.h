#pragma once
#include <stdint.h>

// Layout A on the 320x240 CoreS3 panel:
//   y   0..27   link indicators (belt left half, phone right half)
//   y  28..149  instantaneous speed, very large
//   y 150..175  distance (left) and elapsed time (right)
//   y 184..239  bottom bar: ARM x 0..159, STOP x 160..319
class Ui {
public:
    void begin();
    // Call once per loop, right after M5.update(): samples touch, latches presses.
    void poll();
    void render(uint16_t centiKmh, uint32_t distance10m, uint32_t elapsedSec,
                bool beltUp, bool phoneUp, bool armed);
    // Edge-triggered: true once per press, then cleared.
    bool tookArmToggle();
    bool tookStop();

private:
    bool armPressed_ = false;
    bool stopPressed_ = false;
};
