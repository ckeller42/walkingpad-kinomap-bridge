#pragma once
#include <stddef.h>
#include <stdint.h>

/**
 * @file ftms.h
 * @brief Bluetooth Fitness Machine Service payloads, as served to the phone.
 *
 * Plain C++ with no Arduino types, so it is unit-tested on the host. See
 * `protocol/ftms.yaml` for the single source of truth.
 *
 * Two of these values are load-bearing in a way that is invisible at runtime,
 * because Kinomap fails *silently* when they are wrong: a `2ACC` shorter than
 * 8 bytes or without the speed-target bit means it never commands a speed, and
 * a missing `2AD4` means every target clamps to zero. Both are documented in
 * the Kinomap findings and both are satisfied here.
 */
namespace ftms {

/** @brief A small fixed-capacity byte payload for one characteristic. */
struct Buf {
    uint8_t bytes[8];  ///< Payload bytes.
    size_t len;        ///< Number of valid bytes in @ref bytes.
};

/**
 * @brief `2ACC` Fitness Machine Feature, 8 bytes.
 *
 * Bytes 0-3 are the Fitness Machine Features bitfield (none reported); bytes
 * 4-7 are the Target Setting Features bitfield (speed target only). Both are
 * uint32 little-endian.
 *
 * @return The 8-byte characteristic value.
 */
Buf featureValue();

/**
 * @brief `2AD4` Supported Speed Range.
 *
 * Minimum 0.50, maximum 6.00, increment 0.10 km/h, each a uint16
 * little-endian in 0.01 km/h units.
 *
 * @return The 6-byte characteristic value.
 */
Buf supportedSpeedRange();

/**
 * @brief `2ACD` Treadmill Data.
 * @param centiKmh Instantaneous speed in 0.01 km/h units.
 * @return Flags `0x0000` followed by the speed as uint16 little-endian.
 * @note Flags are deliberately zero and only the speed is sent. Kinomap parses
 *       the flags field big-endian and multiplies the high byte by 255 rather
 *       than 256, so any additional field risks corrupting every later offset.
 */
Buf treadmillData(uint16_t centiKmh);

/**
 * @brief `2ADA` status payload for opcode `0x04`, Start or Resume.
 * @return The notification payload.
 */
Buf statusStarted();

/**
 * @brief Control Point opcodes the bridge recognises.
 *
 * Not all are acted on. Anything not listed decodes as CpOp::Unknown.
 */
enum class CpOp : uint8_t {
    RequestControl = 0x00,  ///< Client claims control. Acknowledged.
    SetTargetSpeed = 0x02,  ///< Target speed, uint16 LE in 0.01 km/h units.
    /// Known but deliberately not acted on: this machine has no incline, and
    /// the feature bits say so. Acknowledged with Success; nothing happens.
    SetTargetInclination = 0x03,
    StartResume = 0x07,     ///< Start or resume.
    StopPause = 0x08,       ///< Stop (param 1) or pause (param 2).
    Unknown = 0xFF,         ///< Any opcode not listed above.
};

/** @brief A decoded write to the Control Point (`2AD9`). */
struct CpRequest {
    CpOp op;            ///< Decoded opcode.
    uint16_t centiKmh;  ///< Only meaningful for @ref CpOp::SetTargetSpeed.
    uint8_t param;      ///< Only meaningful for @ref CpOp::StopPause.
    bool valid;         ///< False when the buffer is too short for the opcode.
    /// The first byte exactly as written, or 0 when there was none. Kept so a
    /// response can echo the request opcode even when op is Unknown.
    uint8_t rawOp;
    /// True when the write carried at least one byte. Distinguishes "an
    /// operation we could not decode" from "no operation at all": only the
    /// first can be answered, because only it has an opcode to echo.
    bool hasOp;
};

/**
 * @brief Decode a Control Point write.
 *
 * @param buf Bytes written by the client; may be null.
 * @param len Number of bytes in @p buf.
 * @return The decoded request. Never reads past @p len, and a null @p buf is
 *         treated as empty. Unrecognised opcodes return CpOp::Unknown with
 *         `valid == true`, so the caller can still acknowledge them.
 */
CpRequest parseControlPoint(const uint8_t* buf, size_t len);

/**
 * @brief Success indication for a Control Point request.
 * @param rawOp The request opcode exactly as written.
 * @return `0x80`, @p rawOp, then `0x01` (Success).
 * @note Always Success, even for unrecognised opcodes. An FTMS error makes
 *       Kinomap retry in a loop.
 */
Buf controlPointSuccess(uint8_t rawOp);

/**
 * @brief The indication to send back for a Control Point write.
 *
 * @param req A request from parseControlPoint().
 * @return A Success indication echoing @ref CpRequest::rawOp, or a Buf with
 *         `len == 0` when there is nothing to answer.
 *
 * Every write that named an operation is answered, **including one the parser
 * rejected** - a truncated `SetTargetSpeed`, say. The client is entitled to an
 * indication for every write it makes, and silence leaves it waiting for one
 * while every other write gets Success. A zero-length write named no
 * operation, so it has no opcode to echo and gets nothing back.
 */
Buf controlPointAck(const CpRequest& req);

}  // namespace ftms
