#pragma once
#include <stddef.h>
#include <stdint.h>

#include <functional>

#include "ftms.h"

/**
 * @file ftms_server.h
 * @brief The phone-facing GATT table: FTMS over NimBLE, and nothing else.
 */

class NimBLECharacteristic;

/**
 * @brief Transport shell for the Fitness Machine Service.
 *
 * Exposes the GATT table Kinomap reads and forwards decoded Control Point
 * writes to callbacks. It holds **no policy**: nothing here decides whether to
 * move the belt, what speed is reasonable, or whether the bridge is armed.
 * That is not tidiness - this class cannot be tested without a radio, so a
 * decision taken here would be a decision no host test can reach. The payload
 * encoding lives in `lib/ftms` and the decisions in `lib/policy`, both
 * Arduino-free and both tested; this class is the part that is left over.
 *
 * Call begin() once, before BeltLink::begin(): begin() is what calls
 * `NimBLEDevice::init()`, and BeltLink only initialises NimBLE if nobody has.
 *
 * Threading: every notify is called from the Arduino loop task; the Control
 * Point callback and phoneConnected() are driven from the NimBLE host task.
 *
 * @note `2AD5` Supported Inclination Range is deliberately absent, because the
 *       R1 Pro has no incline. Kinomap sends inclination targets anyway and
 *       clamps them against the range it never read, so they arrive as 0.0%
 *       and are acknowledged and ignored. Adding `2AD5` would make them real.
 */
class FtmsServer {
public:
    /**
     * @brief Build the GATT table, publish Device Information and advertise.
     *
     * Initialises NimBLE, creates `1826` (Feature, Supported Speed Range,
     * Treadmill Data, Control Point, Status) and `180A`, then starts
     * advertising `1826` under @p advName. Call once.
     *
     * @param advName      Advertised and NimBLE device name; what the user
     *                     picks out of Kinomap's FOUND EQUIPMENT list. The app
     *                     recognises the machine from the advertised `1826`
     *                     UUID, not from this string, so it is for humans.
     * @param manufacturer `2A29` Manufacturer Name String. **Never
     *                     "decathlon".** Kinomap compares this string
     *                     case-insensitively and a match selects a different
     *                     pause/resume code path - one meant for a Domyos
     *                     machine speaking a proprietary non-FTMS UUID set.
     * @param model        `2A24` Model Number String. Cosmetic; nothing in
     *                     Kinomap is known to read it.
     *
     * @note The two values that decide whether this works at all are not
     *       parameters: `2ACC` must be 8 bytes with the speed-target bit set,
     *       and `2AD4` must exist, or Kinomap connects, displays data and
     *       either never commands a speed or clamps every target to zero. Both
     *       come from `lib/ftms` and both fail *silently*, which is why
     *       `docs/test-log.md` checks them against a real GATT dump rather
     *       than trusting a workout to reveal them.
     */
    void begin(const char* advName, const char* manufacturer, const char* model);

    /**
     * @brief Notify `2ACD` Treadmill Data with the belt's current speed.
     *
     * Does nothing when no phone is connected or begin() has not run, so the
     * caller can notify unconditionally from loop().
     *
     * @param centiKmh Instantaneous speed in 0.01 km/h units, as measured from
     *                 the belt's status frame - not the setpoint. Kinomap
     *                 integrates its distance and power from this stream, so a
     *                 commanded value sent here would make up a workout that
     *                 did not happen.
     */
    void notifySpeed(uint16_t centiKmh);

    /**
     * @brief Notify `2ADA` Fitness Machine Status, opcode `0x04` Start/Resume.
     *
     * This is the notification that gets a session going. Kinomap is passive:
     * it waits for the machine to report motion and answers this with a `0x07`
     * Start/Resume write of its own - it never initiates one. Does nothing
     * when no phone is connected.
     */
    void notifyStarted();

    /**
     * @brief Whether a central is connected.
     * @return True between a connect and the matching disconnect.
     * @note Connected is not subscribed, and neither is "has a live session":
     *       Kinomap only starts counting once it sees the belt move. Tracked by
     *       a single file-scope callback object in the .cpp, so the answer is
     *       the same for every instance - there is exactly one server.
     */
    bool phoneConnected() const;

    /**
     * @brief How many Control Point writes have arrived with this first byte.
     *
     * Counted by **raw** first byte rather than by decoded ftms::CpOp, which is
     * the whole point: an unrecognised opcode decodes to CpOp::Unknown and
     * would be indistinguishable from every other unrecognised opcode. Counting
     * the byte is what identified an earlier mystery `0xff` log line as `0x03`
     * Set Target Inclination.
     *
     * These counters settled a question that three other approaches could not.
     * A full session with a walker aboard and three speed changes produced
     * `00=1 03=2 07=1` and no `0x02` at all, which is counted evidence that
     * Kinomap does not command treadmill speed - not an inference from a screen
     * that happened to hold steady. See `docs/test-log.md`.
     *
     * @param rawOp The first byte as written; all 256 values are valid indices.
     * @return Writes seen since boot. Never reset; the console prints them.
     */
    uint32_t cpCount(uint8_t rawOp) const { return cpCount_[rawOp]; }

    /**
     * @brief How many zero-length Control Point writes have arrived.
     * @return The count since boot.
     * @note Counted apart from cpCount() because an empty write has no first
     *       byte to count under, and "nothing arrived" and "an empty write
     *       arrived" are different diagnoses.
     */
    uint32_t cpEmptyCount() const { return cpEmpty_; }

    /**
     * @brief Install the handler for `0x02` Set Target Speed.
     * @param cb Called with the requested speed in 0.01 km/h units. Runs on the
     *           NimBLE host task, not from loop(), so it must latch the request
     *           and return rather than command the belt itself - which is also
     *           what makes the arming gate unavoidable, since the gate lives in
     *           loop().
     */
    void setOnTarget(std::function<void(uint16_t)> cb) { onTarget_ = std::move(cb); }

    /**
     * @brief Install the handler for every other recognised opcode.
     * @param cb Called with the decoded opcode, including ftms::CpOp::Unknown.
     *           Runs on the NimBLE host task. `0x02` never arrives here - it
     *           goes to setOnTarget() alone - so a handler that wants every
     *           write has to register both.
     */
    void setOnControlOp(std::function<void(ftms::CpOp)> cb) { onControlOp_ = std::move(cb); }

    /**
     * @brief Handle one write to the Control Point (`2AD9`).
     *
     * Public only so the NimBLE characteristic callback in the .cpp can reach
     * it; it is not meant to be called from the bridge.
     *
     * Counts the write, logs the raw bytes, dispatches to a callback and then
     * indicates **Success, echoing the raw opcode, even for an opcode it does
     * not recognise**. That is not politeness: an FTMS error makes Kinomap
     * retry in a loop, so a correct refusal costs more than a meaningless
     * acknowledgement. Nothing acts on the acknowledgement either way.
     *
     * The one write that goes unanswered is a write too short for its own
     * opcode: it is counted and logged, then dropped without an indication.
     *
     * @param data Bytes as written; may be null.
     * @param len  Number of bytes in @p data; may be zero.
     */
    void handleControlPointWrite(const uint8_t* data, size_t len);

private:
    NimBLECharacteristic* treadmillData_ = nullptr;
    NimBLECharacteristic* controlPoint_ = nullptr;
    NimBLECharacteristic* status_ = nullptr;
    /// Indexed by the raw first byte, so an unknown opcode keeps its identity.
    volatile uint32_t cpCount_[256] = {};
    volatile uint32_t cpEmpty_ = 0;
    std::function<void(uint16_t)> onTarget_;
    std::function<void(ftms::CpOp)> onControlOp_;
};
