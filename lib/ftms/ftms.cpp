#include "ftms.h"

namespace ftms {

Buf featureValue() {
    // Fitness Machine Features = 0x00000000, Target Setting Features = 0x00000001.
    return Buf{{0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00}, 8};
}

Buf supportedSpeedRange() {
    // 0.50 km/h = 50 = 0x0032, 6.00 km/h = 600 = 0x0258, 0.10 km/h = 10 = 0x000A.
    return Buf{{0x32, 0x00, 0x58, 0x02, 0x0A, 0x00, 0x00, 0x00}, 6};
}

Buf treadmillData(uint16_t centiKmh) {
    Buf b{{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 4};
    b.bytes[2] = static_cast<uint8_t>(centiKmh & 0xFF);
    b.bytes[3] = static_cast<uint8_t>(centiKmh >> 8);
    return b;
}

Buf statusStarted() {
    return Buf{{0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 1};
}

CpRequest parseControlPoint(const uint8_t* buf, size_t len) {
    CpRequest r{CpOp::Unknown, 0, 0, false, 0, false};
    if (buf == nullptr || len == 0) {
        return r;
    }
    r.hasOp = true;
    r.rawOp = buf[0];
    switch (buf[0]) {
        case static_cast<uint8_t>(CpOp::RequestControl):
            r.op = CpOp::RequestControl;
            r.valid = true;
            break;
        case static_cast<uint8_t>(CpOp::SetTargetSpeed):
            // Opcode plus uint16 little-endian speed in 0.01 km/h.
            if (len < 3) {
                return r;
            }
            r.op = CpOp::SetTargetSpeed;
            r.centiKmh = static_cast<uint16_t>(buf[1] | (buf[2] << 8));
            r.valid = true;
            break;
        case static_cast<uint8_t>(CpOp::SetTargetInclination):
            // Named so it is logged as itself; deliberately not acted on.
            r.op = CpOp::SetTargetInclination;
            r.valid = true;
            break;
        case static_cast<uint8_t>(CpOp::StartResume):
            r.op = CpOp::StartResume;
            r.valid = true;
            break;
        case static_cast<uint8_t>(CpOp::StopPause):
            // Optional stop/pause parameter byte.
            r.op = CpOp::StopPause;
            r.param = (len >= 2) ? buf[1] : 0;
            r.valid = true;
            break;
        default:
            // Unrecognised: keep Unknown but valid so it can be acknowledged.
            r.valid = true;
            break;
    }
    return r;
}

Buf controlPointSuccess(uint8_t rawOp) {
    return Buf{{0x80, rawOp, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00}, 3};
}

Buf controlPointAck(const CpRequest& req) {
    if (!req.hasOp) return Buf{{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 0};
    return controlPointSuccess(req.rawOp);
}

}  // namespace ftms
