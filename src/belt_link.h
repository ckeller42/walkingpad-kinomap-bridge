#pragma once
#include <stddef.h>
#include <stdint.h>

#include <atomic>
#include <mutex>
#include <string>

#include "belt.h"
#include "config.h"

class NimBLEClient;
class NimBLERemoteCharacteristic;

// NimBLE *central* that talks to the real WalkingPad: scans for service FE00,
// connects, subscribes to FE01 (status) and writes commands to FE02
// (write-without-response, so there is no ack and no flow control).
//
// This class does NO pacing and keeps NO command queue: it sits underneath
// belt::BeltGate, which owns the >= 0.7 s spacing. Every method here is one
// immediate write.
//
// The belt never pushes status. requestStatus() writes the request (BeltGate
// calls it, so it shares the pacing of every other FE02 write); poll() only
// returns the most recently *received* reply, i.e. the answer to an earlier
// request. It returns false when nothing fresh has arrived (never, or older
// than kStatusStaleMs), so a silent link is never mistaken for a live belt.
//
// Only 0xA2 frames from lib/wilink are ever written (enforced in send()).
// The belt's Telink OTA service is never discovered by UUID, connected to or
// written to.
//
// Threading: connect/discover/subscribe run on a private worker task (they
// block on the NimBLE host task, so they must not run under main.cpp's mutex).
// Belt methods run on the Arduino loop task. Notification and disconnect
// callbacks run on the NimBLE host task and only touch mutex-guarded state.
class BeltLink : public belt::Belt {
public:
    static constexpr uint32_t kStatusStaleMs = STATUS_STALE_MS;

    // Initialises NimBLE if needed (safe after FtmsServer::begin) and starts
    // the worker. Call once. Returns false if the worker could not be created.
    bool begin();

    void setSpeed(uint8_t tenthKmh) override;
    void setMode(wilink::Mode mode) override;
    void start() override;
    void stop() override;  // setMode(Standby): the only stop the belt honours
    bool poll(wilink::Status& out) override;  // read-only: never writes
    uint32_t statusAgeMs() const override;
    bool requestStatus() override;            // the FE02 write; paced by BeltGate
    bool linkUp() const override { return linked_.load(); }

private:
    struct Impl;  // callback glue, defined in the .cpp
    bool send(const wilink::Frame& f);
    void onStatus(const uint8_t* data, size_t len);
    void onDown();
    static void workerEntry(void* self);
    void worker();
    bool connectOnce();

    std::atomic<bool> linked_{false};

    std::mutex gattMu_;  // guards cmd_ (the FE02 handle) for the write path
    NimBLERemoteCharacteristic* cmd_ = nullptr;

    mutable std::mutex statusMu_;
    wilink::Status status_{};
    bool haveStatus_ = false;
    uint32_t statusAtMs_ = 0;

    NimBLEClient* client_ = nullptr;
    Impl* impl_ = nullptr;
    // Set by the scan callback (host task), consumed by the worker.
    std::mutex foundMu_;
    bool found_ = false;
    std::string foundAddr_;
    uint8_t foundType_ = 0;
    bool begun_ = false;
};
