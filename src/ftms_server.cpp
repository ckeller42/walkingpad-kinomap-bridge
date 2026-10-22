#include "ftms_server.h"

#include "logsink.h"
#include <Arduino.h>
#include <NimBLEDevice.h>

namespace {

// Written against NimBLE-Arduino 2.x (callbacks take NimBLEConnInfo&).
constexpr uint16_t kSvcFitnessMachine = 0x1826;
constexpr uint16_t kChrFeature = 0x2ACC;
constexpr uint16_t kChrSpeedRange = 0x2AD4;
constexpr uint16_t kChrTreadmillData = 0x2ACD;
constexpr uint16_t kChrControlPoint = 0x2AD9;
constexpr uint16_t kChrStatus = 0x2ADA;
constexpr uint16_t kSvcDeviceInfo = 0x180A;
constexpr uint16_t kChrManufacturer = 0x2A29;
constexpr uint16_t kChrModel = 0x2A24;
// 2AD5 Supported Inclination Range is deliberately absent: no incline.

class ServerCb : public NimBLEServerCallbacks {
public:
    void onConnect(NimBLEServer*, NimBLEConnInfo&) override { connected = true; }
    void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
        connected = false;
        NimBLEDevice::startAdvertising();
    }
    volatile bool connected = false;
};

class CpCb : public NimBLECharacteristicCallbacks {
public:
    explicit CpCb(FtmsServer* s) : server(s) {}
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
        NimBLEAttValue v = c->getValue();
        server->handleControlPointWrite(v.data(), v.size());
    }
    FtmsServer* server;
};

ServerCb gServerCb;

}  // namespace

void FtmsServer::begin(const char* advName, const char* manufacturer, const char* model) {
    NimBLEDevice::init(advName);
    NimBLEServer* srv = NimBLEDevice::createServer();
    srv->setCallbacks(&gServerCb);

    NimBLEService* fms = srv->createService(NimBLEUUID(kSvcFitnessMachine));

    ftms::Buf f = ftms::featureValue();
    fms->createCharacteristic(NimBLEUUID(kChrFeature), NIMBLE_PROPERTY::READ)
        ->setValue(f.bytes, f.len);

    ftms::Buf r = ftms::supportedSpeedRange();
    fms->createCharacteristic(NimBLEUUID(kChrSpeedRange), NIMBLE_PROPERTY::READ)
        ->setValue(r.bytes, r.len);

    treadmillData_ =
        fms->createCharacteristic(NimBLEUUID(kChrTreadmillData), NIMBLE_PROPERTY::NOTIFY);

    controlPoint_ = fms->createCharacteristic(
        NimBLEUUID(kChrControlPoint), NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::INDICATE);
    controlPoint_->setCallbacks(new CpCb(this));

    status_ = fms->createCharacteristic(NimBLEUUID(kChrStatus), NIMBLE_PROPERTY::NOTIFY);

    NimBLEService* dis = srv->createService(NimBLEUUID(kSvcDeviceInfo));
    dis->createCharacteristic(NimBLEUUID(kChrManufacturer), NIMBLE_PROPERTY::READ)
        ->setValue(manufacturer);
    dis->createCharacteristic(NimBLEUUID(kChrModel), NIMBLE_PROPERTY::READ)->setValue(model);

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->setName(advName);
    adv->addServiceUUID(NimBLEUUID(kSvcFitnessMachine));
    adv->start();
}

void FtmsServer::notifySpeed(uint16_t centiKmh) {
    if (!treadmillData_ || !phoneConnected()) return;
    ftms::Buf b = ftms::treadmillData(centiKmh);
    treadmillData_->notify(b.bytes, b.len);
}

void FtmsServer::notifyStarted() {
    if (!status_ || !phoneConnected()) return;
    ftms::Buf b = ftms::statusStarted();  // 0x04 on 2ADA, not on the control point
    status_->notify(b.bytes, b.len);
}

bool FtmsServer::phoneConnected() const { return gServerCb.connected; }

void FtmsServer::handleControlPointWrite(const uint8_t* data, size_t len) {
    ftms::CpRequest req = ftms::parseControlPoint(data, len);

    // One line per write, raw bytes (not the mapped CpOp): "cp op=0x03 len=3 payload=2c 01".
    if (len == 0 || data == nullptr) {
        cpEmpty_ = cpEmpty_ + 1;
        Serial.println("cp op=none len=0");
    } else {
        cpCount_[data[0]] = cpCount_[data[0]] + 1;
        char pl[3 * 16 + 4];
        size_t n = 0;
        const size_t shown = (len - 1 < 16) ? len - 1 : 16;
        for (size_t i = 0; i < shown; ++i) n += snprintf(pl + n, sizeof pl - n, "%s%02x", i ? " " : "", data[1 + i]);
        if (len - 1 > shown) snprintf(pl + n, sizeof pl - n, "...");
        if (len > 1) Serial.printf("cp op=0x%02x len=%u payload=%s%s\n", data[0], (unsigned)len, pl, req.valid ? "" : " (invalid)");
        else Serial.printf("cp op=0x%02x len=1%s\n", data[0], req.valid ? "" : " (invalid)");
    }
    if (data && len) logsink::hex("cp", data, len);

    // An undecodable write changes nothing, but it is still answered below:
    // the client is owed an indication for every write it makes.
    if (!req.valid) logsink::line("cp invalid");

    if (req.valid) {
        if (req.op == ftms::CpOp::SetTargetSpeed) {
            if (onTarget_) onTarget_(req.centiKmh);
        } else if (onControlOp_) {
            onControlOp_(req.op);
        }
    }

    // Always Success, echoing the raw opcode: an FTMS error makes Kinomap
    // retry in a loop. A zero-length write has no opcode to echo, and
    // controlPointAck() returns an empty buffer for it.
    ftms::Buf resp = ftms::controlPointAck(req);
    if (controlPoint_ && resp.len) controlPoint_->indicate(resp.bytes, resp.len);
}
