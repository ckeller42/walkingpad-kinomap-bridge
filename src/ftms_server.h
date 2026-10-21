#pragma once
#include <stddef.h>
#include <stdint.h>

#include <functional>

#include "ftms.h"

class NimBLECharacteristic;

// Transport shell: exposes the FTMS GATT table over NimBLE and forwards
// decoded Control Point requests to callbacks. Holds no policy.
class FtmsServer {
public:
    void begin(const char* advName);
    void notifySpeed(uint16_t centiKmh);  // 2ACD Treadmill Data
    void notifyStarted();                 // 2ADA Fitness Machine Status: 0x04
    bool phoneConnected() const;
    void setOnTarget(std::function<void(uint16_t)> cb) { onTarget_ = std::move(cb); }
    void setOnControlOp(std::function<void(ftms::CpOp)> cb) { onControlOp_ = std::move(cb); }

    // Called from the NimBLE write callback on 2AD9.
    void handleControlPointWrite(const uint8_t* data, size_t len);

private:
    NimBLECharacteristic* treadmillData_ = nullptr;
    NimBLECharacteristic* controlPoint_ = nullptr;
    NimBLECharacteristic* status_ = nullptr;
    std::function<void(uint16_t)> onTarget_;
    std::function<void(ftms::CpOp)> onControlOp_;
};
