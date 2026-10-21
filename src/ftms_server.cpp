#include "ftms_server.h"

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

// Never "decathlon": that string selects a different path inside Kinomap.
constexpr const char* kManufacturer = "WalkingPad Bridge";
constexpr const char* kModel = "R1 Pro FTMS Bridge";

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

void FtmsServer::begin(const char* advName) {
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
        ->setValue(kManufacturer);
    dis->createCharacteristic(NimBLEUUID(kChrModel), NIMBLE_PROPERTY::READ)->setValue(kModel);

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
    if (!req.valid) return;

    if (req.op == ftms::CpOp::SetTargetSpeed) {
        if (onTarget_) onTarget_(req.centiKmh);
    } else if (onControlOp_) {
        onControlOp_(req.op);
    }

    // Always Success, echoing the raw opcode: an FTMS error makes Kinomap retry.
    ftms::Buf resp = ftms::controlPointSuccess(req.rawOp);
    controlPoint_->indicate(resp.bytes, resp.len);
}
