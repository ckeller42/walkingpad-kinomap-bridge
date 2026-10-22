#pragma once
#include <stddef.h>
#include <stdint.h>

#include <atomic>
#include <mutex>
#include <string>

#include "belt.h"
#include "config.h"

/**
 * @file belt_link.h
 * @brief The BLE central that talks to the real WalkingPad, and the single
 *        choke point for every command written to it.
 */

class NimBLEClient;
class NimBLERemoteCharacteristic;

/**
 * @brief NimBLE central for the KingSmith WalkingPad R1 Pro.
 *
 * Scans for service `FE00`, connects, subscribes to `FE01` (status) and writes
 * commands to `FE02` (write-without-response, so there is no ack and no flow
 * control). If `FE00` is missing from the advertisement it falls back to
 * matching the advertised name `RE`, and says so in the log, because a match by
 * name is weaker evidence that this is a belt.
 *
 * ## The safety property
 *
 * send() is the **one** path from this bridge to the belt's command
 * characteristic, and it refuses any frame whose command group is not `0xA2`.
 * Group `0xE8` is the WiLink OTA group and **can brick the belt**. Because
 * every public method here goes through send(), "the bridge never emits `0xE8`"
 * is a structural property of the code rather than a convention someone has to
 * remember - a future bug can produce a wrong frame, but it cannot produce an
 * OTA frame. Keep it that way: do not add a second write path, and do not relax
 * the check in send() to cover a new group without understanding that this is
 * the guarantee being traded away.
 *
 * In the same spirit, this file deliberately names no GATT UUID other than
 * `FE00`, `FE01` and `FE02`. The belt's Telink OTA service is never discovered
 * by UUID, connected to, subscribed to or written to - it is not referenced at
 * all, so there is nothing to misfire.
 *
 * ## No pacing here
 *
 * This class keeps no command queue and does no rate limiting: every method is
 * one immediate write. It sits underneath belt::BeltGate, which owns the
 * >= 0.7 s spacing the belt needs. That matters because the belt does not
 * reject a command sent too soon - it *silently ignores* it, with no error and
 * no NAK, and with write-without-response there is nothing to notice. Bypassing
 * the gate therefore looks like a belt that intermittently disobeys.
 *
 * ## Status is pulled, never pushed
 *
 * The belt sends nothing unsolicited; subscribing to `FE01` and waiting yields
 * zero notifications. requestStatus() writes the request (BeltGate calls it, so
 * it shares the pacing of every other `FE02` write) and poll() returns the most
 * recently *received* reply - the answer to an earlier request. poll() returns
 * false when nothing fresh has arrived, so a silent link is never mistaken for
 * a live belt.
 *
 * ## Threading
 *
 * Connect, discover and subscribe run on a private FreeRTOS worker task,
 * because they block on the NimBLE host task and so must not run under
 * `main.cpp`'s mutex. The belt::Belt methods run on the Arduino loop task.
 * onStatus() and the disconnect handler run on the NimBLE host task - status
 * arrives from a BLE callback, not from loop() - and touch only mutex-guarded
 * or atomic state.
 */
class BeltLink : public belt::Belt {
public:
    /**
     * @brief Status frames recovered from split notifications.
     *
     * The belt sometimes splits one status frame across two notifications
     * (issue #3); wilink::StatusReassembler rejoins them. This counter and
     * fragmentsLost() exist so that the rate is **measured rather than
     * assumed**: the loss rate is the difference between a harmless quirk and a
     * belt that reports itself down mid-run, and it cannot be guessed from a
     * log that only shows what parsed.
     *
     * @return Frames rejoined from two or more fragments since boot.
     */
    uint32_t reassembled() const { return reasm_.reassembled(); }

    /**
     * @brief Status fragments abandoned without completing.
     *
     * One lost reading is harmless: the poll runs at about 1 Hz and
     * kStatusStaleMs is 5000 ms, so the next reply arrives long before the
     * status goes stale. A *run* of them is not - roughly five consecutive
     * losses is enough for poll() to start refusing, which declares a belt that
     * is actually RUNNING to be down. This counter is how a rare event is told
     * apart from one that is about to matter.
     *
     * @return Fragments discarded without a frame completing, since boot.
     */
    uint32_t fragmentsLost() const { return reasm_.discarded(); }

    /**
     * @brief How old a belt reading may be before the belt counts as down.
     *
     * From `STATUS_STALE_MS` in config.h. poll() refuses anything older, which
     * is what stops a dropped link from presenting the last known speed
     * forever.
     */
    static constexpr uint32_t kStatusStaleMs = STATUS_STALE_MS;

    /**
     * @brief Start the radio and the worker task that looks for the belt.
     *
     * Initialises NimBLE if nothing has yet, which makes it safe to call after
     * FtmsServer::begin(), creates the client, turns on active scanning
     * (`FE00` may sit in the scan response rather than the advertisement) and
     * spawns the worker. Idempotent: a second call does nothing and succeeds.
     *
     * @return False only if the worker task could not be created. True means
     *         the search has *started*, not that a belt was found - use
     *         phase() for that and linkUp() for whether it is usable.
     */
    bool begin();

    /**
     * @brief Set the belt's target speed. One immediate `FE02` write.
     *
     * @param tenthKmh Speed in 0.1 km/h units.
     *
     * @note Measured, and both surprises have cost time here: **a speed of 0 is
     *       not a stop** - the belt carries on ramping to its own preset - and a
     *       speed sent during the start countdown is ignored in favour of that
     *       preset, so speed has to be set once the belt reports state 1
     *       (running). Use stop() to stop.
     */
    void setSpeed(uint8_t tenthKmh) override;

    /**
     * @brief Set the belt's operating mode. One immediate `FE02` write.
     *
     * @param mode Manual to accept speed commands, Standby to stop.
     *
     * @note A stop leaves the belt parked in standby, and it will not start
     *       again until the mode is set back to manual. That is why the start
     *       sequence sets mode first: it is necessary, not tidy.
     */
    void setMode(wilink::Mode mode) override;

    /**
     * @brief Ask the belt to start moving. One immediate `FE02` write.
     *
     * @note The belt **auto-stops about 15 s after starting with nobody on
     *       it**, returning to state 0, and the window shortens on repeated
     *       starts. Nearly every historical "the bridge cannot start the belt"
     *       result was this interlock rather than a bridge fault, so a start
     *       has to be verified inside that window or with a walker aboard.
     */
    void start() override;

    /**
     * @brief Stop the belt, by setting mode Standby.
     *
     * The only stop the belt honours - confirmed against a moving belt, where
     * set-speed-0 and start-with-0 both failed to stop it. STOP must work
     * whether or not the bridge is armed, so nothing here is gated on arming.
     */
    void stop() override;

    /**
     * @brief The most recent belt reading, if it is still fresh.
     *
     * Read-only: this never writes to the belt. What it returns is the reply to
     * an earlier requestStatus(), not a fresh read.
     *
     * @param out Filled in only when this returns true; untouched otherwise.
     * @return False when no status has arrived since the link came up, or the
     *         newest one is older than kStatusStaleMs. A false here means "I do
     *         not know", which is why callers must not fall back to the last
     *         value they saw.
     */
    bool poll(wilink::Status& out) override;

    /**
     * @brief How long since the newest accepted status frame.
     * @return Milliseconds, or `UINT32_MAX` when there is no reading at all.
     *         A disconnect discards the stored reading, so this never reports
     *         an age measured across a reconnect.
     */
    uint32_t statusAgeMs() const override;

    /**
     * @brief Write the status request to `FE02`.
     *
     * @return Whether the write went out - not whether the belt answered. The
     *         reply, if any, arrives later as an `FE01` notification and is
     *         collected by poll(). With write-without-response there is no ack,
     *         so true here is weaker than it looks.
     *
     * @note Called by belt::BeltGate so that it is paced like every other
     *       `FE02` write; a status request that jumps the queue costs a command.
     */
    bool requestStatus() override;

    /**
     * @brief Whether the belt link is up.
     * @return True while connected with the status subscription established.
     * @note Up and silent is a real state: this can be true while poll()
     *       returns false. That is why the UI tests both - the link is the
     *       transport, poll() is the evidence.
     */
    bool linkUp() const override { return linked_.load(); }

    /**
     * @brief What the worker task is doing right now; the setup wizard shows it.
     *
     * Progress of the search, not the health of the link - for that use
     * linkUp(), which is cleared on disconnect while the phase is on its way
     * back round to Scanning.
     */
    enum class Phase : uint8_t {
        Idle,        ///< begin() has not run, or the worker has not started.
        Scanning,    ///< Scanning; no matching advertisement yet.
        Found,       ///< An advert matched `FE00` or the name `RE`.
        Connecting,  ///< Connecting, discovering and subscribing.
        Linked,      ///< Connected and subscribed to `FE01`.
    };

    /**
     * @brief Where the search has got to.
     * @return The current phase; atomic, so this is safe from any task.
     */
    Phase phase() const { return phase_.load(); }

    /**
     * @brief Address of the most recent belt seen.
     *
     * @return The BLE address as text, or "" if no advertisement has matched
     *         since boot.
     *
     * @note The last belt *seen*, not the one connected, and it survives a
     *       disconnect. That is the point: it lets the wizard distinguish "no
     *       belt is advertising" from "a belt is there and the connection
     *       failed", which look identical from linkUp() alone.
     */
    std::string foundAddress() const;

private:
    struct Impl;  // callback glue, defined in the .cpp
    bool send(const wilink::Frame& f);
    void onStatus(const uint8_t* data, size_t len);
    void onDown();
    static void workerEntry(void* self);
    void worker();
    bool connectOnce();

    std::atomic<bool> linked_{false};
    std::atomic<Phase> phase_{Phase::Idle};
    std::string lastFoundAddr_;  // under foundMu_

    std::mutex gattMu_;  // guards cmd_ (the FE02 handle) for the write path
    NimBLERemoteCharacteristic* cmd_ = nullptr;

    // Rejoins status frames the belt splits across two notifications.
    wilink::StatusReassembler reasm_;
    mutable std::mutex statusMu_;
    wilink::Status status_{};
    bool haveStatus_ = false;
    uint32_t statusAtMs_ = 0;

    NimBLEClient* client_ = nullptr;
    Impl* impl_ = nullptr;
    // Set by the scan callback (host task), consumed by the worker.
    mutable std::mutex foundMu_;
    bool found_ = false;
    std::string foundAddr_;
    uint8_t foundType_ = 0;
    bool begun_ = false;
};
