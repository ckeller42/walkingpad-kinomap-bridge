#pragma once
#include <stddef.h>
#include <stdint.h>

/**
 * @file wilink.h
 * @brief KingSmith "WiLink" protocol: the belt's own wire format.
 *
 * Measured against a WalkingPad R1 Pro; see `protocol/wilink.yaml` for the
 * single source of truth and the provenance of every field. Pure encoding and
 * decoding with no I/O, so it is unit-tested on the host.
 *
 * @warning Command group `0xE8` is the WiLink OTA group and **can brick the
 * belt**. Everything here emits group `0xA2` only, and the single write choke
 * point in `BeltLink::send()` refuses anything else.
 */
namespace wilink {

/**
 * @brief Checksum for a frame body.
 *
 * Frames are `F7 <cmd> <payload...> <crc> FD`. The CRC is the low byte of the
 * sum of every byte between the header (`F7`) and the CRC itself.
 *
 * @param body First byte after the `0xF7` header.
 * @param len  Number of bytes to sum.
 * @return The checksum byte.
 */
uint8_t crc(const uint8_t* body, size_t len);

/** @brief An encoded command frame, ready to write to characteristic FE02. */
struct Frame {
    uint8_t bytes[8];  ///< Frame bytes, starting with the `0xF7` header.
    size_t len;        ///< Number of valid bytes in @ref bytes.
};

/** @brief Belt operating mode, as carried by the Set mode command. */
enum class Mode : uint8_t {
    Auto = 0,     ///< Settable, but never observed in a status frame.
    Manual = 1,   ///< Speed is commanded. The mode the bridge uses.
    Standby = 2,  ///< Parked. Writing this is also the only working stop.
};

/**
 * @brief Ask the belt for a status frame; the reply arrives on FE01.
 * @return The encoded request frame.
 */
Frame requestStatus();

/**
 * @brief Set the target speed.
 * @param tenthKmh Speed in 0.1 km/h units, so 25 means 2.5 km/h.
 * @return The encoded command frame.
 * @note Only takes effect in Manual mode on a running belt. On a stopped belt
 *       the console displays the value without moving.
 */
Frame setSpeed(uint8_t tenthKmh);

/**
 * @brief Set the operating mode.
 * @param m The mode to select. Mode::Standby doubles as the only working stop.
 * @return The encoded command frame.
 */
Frame setMode(Mode m);

/**
 * @brief Start the belt, after which it counts down and runs.
 *
 * Confirmed on an R1 Pro from both standby and idle. Left alone it ramps to
 * the belt's own preset speed (~1.1 km/h) rather than a commanded one.
 *
 * @return The encoded command frame.
 */
Frame startBelt();

/** @brief A decoded `F8 A2` status frame. */
struct Status {
    uint8_t beltState;     ///< 0 idle, 1 running, 3 waking, 4 standby, 6 starting, 7 countdown, 9 pre-countdown.
    uint8_t tenthKmh;      ///< Current speed in 0.1 km/h units.
    uint8_t mode;          ///< Current @ref Mode.
    uint32_t elapsedSec;   ///< Session time in seconds.
    uint32_t distance10m;  ///< Session distance in 10 m units.
    uint32_t steps;        ///< Session step count.
};

/**
 * @brief Decode a status frame.
 *
 * @param buf Bytes received on FE01.
 * @param len Number of bytes in @p buf.
 * @param out Filled in only on success.
 * @return False, leaving @p out untouched, unless @p buf is a well-formed
 *         `F8 A2` status frame. Header and minimum length are checked; the
 *         CRC is not.
 */
bool parseStatus(const uint8_t* buf, size_t len, Status& out);

}  // namespace wilink
