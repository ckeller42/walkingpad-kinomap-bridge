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
    // manufacturer/model go to the Device Information service (2A29/2A24). The
    // manufacturer string selects a code path inside Kinomap (never "decathlon").
    void begin(const char* advName, const char* manufacturer, const char* model);
    void notifySpeed(uint16_t centiKmh);  // 2ACD Treadmill Data
    void notifyStarted();                 // 2ADA Fitness Machine Status: 0x04
    bool phoneConnected() const;
    // Writes seen on the Control Point since boot, by raw first byte.
    uint32_t cpCount(uint8_t rawOp) const { return cpCount_[rawOp]; }
    uint32_t cpEmptyCount() const { return cpEmpty_; }
    void setOnTarget(std::function<void(uint16_t)> cb) { onTarget_ = std::move(cb); }
    void setOnControlOp(std::function<void(ftms::CpOp)> cb) { onControlOp_ = std::move(cb); }

    // Called from the NimBLE write callback on 2AD9.
    void handleControlPointWrite(const uint8_t* data, size_t len);

private:
    NimBLECharacteristic* treadmillData_ = nullptr;
    NimBLECharacteristic* controlPoint_ = nullptr;
    NimBLECharacteristic* status_ = nullptr;
    volatile uint32_t cpCount_[256] = {};
    volatile uint32_t cpEmpty_ = 0;
    std::function<void(uint16_t)> onTarget_;
    std::function<void(ftms::CpOp)> onControlOp_;
};
