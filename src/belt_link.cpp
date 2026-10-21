#include "belt_link.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "wilink.h"

namespace {
// The only GATT objects this file names. Deliberately no other UUIDs: the
// Telink OTA service is never referenced, let alone opened.
const NimBLEUUID kSvc((uint16_t)0xFE00);
const NimBLEUUID kStatusChr((uint16_t)0xFE01);
const NimBLEUUID kCmdChr((uint16_t)0xFE02);

constexpr const char* kAdvName = "RE";  // fallback match if FE00 leaves the advert
constexpr uint8_t kCmdByte = 0xA2;  // the only command group we ever emit
}  // namespace

// Callback glue, kept out of the header so NimBLE types stay private.
struct BeltLink::Impl : NimBLEScanCallbacks, NimBLEClientCallbacks {
    explicit Impl(BeltLink& o) : owner(o) {}
    BeltLink& owner;

    void onResult(const NimBLEAdvertisedDevice* d) override {
        const bool bySvc = d->isAdvertisingService(kSvc);
        const bool byName = d->getName() == kAdvName;
        if (!bySvc && !byName) return;
        if (!bySvc)
            Serial.printf("beltlink: %s matched by name only (no FE00 in advert)\n",
                          d->getAddress().toString().c_str());
        std::lock_guard<std::mutex> g(owner.foundMu_);
        owner.found_ = true;
        owner.foundAddr_ = d->getAddress().toString();
        owner.foundType_ = d->getAddressType();
    }
    void onDisconnect(NimBLEClient*, int) override { owner.onDown(); }
};

bool BeltLink::begin() {
    if (begun_) return true;
    begun_ = true;
    if (!NimBLEDevice::isInitialized()) NimBLEDevice::init("");
    impl_ = new Impl(*this);

    client_ = NimBLEDevice::createClient();
    client_->setClientCallbacks(impl_, false);
    client_->setConnectTimeout(8000);

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(impl_, false);
    scan->setActiveScan(true);  // FE00 may sit in the scan response

    return xTaskCreate(workerEntry, "beltlink", 6144, this, 1, nullptr) == pdPASS;
}

void BeltLink::workerEntry(void* self) {
    static_cast<BeltLink*>(self)->worker();
}

// Scan -> connect -> (link up) -> wait. onDisconnect clears linked_, which
// sends this loop back to scanning: that is the whole reconnect story.
void BeltLink::worker() {
    for (;;) {
        if (linked_.load()) {
            // A disconnect can land between subscribe() and linked_.store();
            // onDisconnect then ran too early to clear anything. Self-heal.
            if (!client_->isConnected()) onDown();
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        bool found;
        {
            std::lock_guard<std::mutex> g(foundMu_);
            found = found_;
        }
        NimBLEScan* scan = NimBLEDevice::getScan();
        if (!found) {
            if (!scan->isScanning()) {
                scan->clearResults();
                scan->start(0, false, true);  // until stopped
            }
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        scan->stop();
        if (!connectOnce()) vTaskDelay(pdMS_TO_TICKS(1000));  // then rescan
    }
}

bool BeltLink::connectOnce() {
    NimBLEAddress addr;
    {
        std::lock_guard<std::mutex> g(foundMu_);
        addr = NimBLEAddress(foundAddr_, foundType_);
        found_ = false;
    }
    if (!client_->connect(addr)) return false;

    NimBLERemoteService* svc = client_->getService(kSvc);
    NimBLERemoteCharacteristic* status = svc ? svc->getCharacteristic(kStatusChr) : nullptr;
    NimBLERemoteCharacteristic* cmd = svc ? svc->getCharacteristic(kCmdChr) : nullptr;
    if (!status || !cmd ||
        !status->subscribe(true, [this](NimBLERemoteCharacteristic*, uint8_t* data,
                                        size_t len, bool) { onStatus(data, len); })) {
        client_->disconnect();
        return false;
    }
    {
        std::lock_guard<std::mutex> g(statusMu_);
        haveStatus_ = false;
    }
    {
        std::lock_guard<std::mutex> g(gattMu_);
        cmd_ = cmd;
    }
    linked_.store(true);
    if (!client_->isConnected()) {  // dropped while we were publishing
        onDown();
        return false;
    }
    return true;
}

void BeltLink::onDown() {
    {
        std::lock_guard<std::mutex> g(gattMu_);
        cmd_ = nullptr;  // before linked_: nobody may see "up" with no handle
    }
    linked_.store(false);
    std::lock_guard<std::mutex> g(statusMu_);
    haveStatus_ = false;  // never serve a pre-disconnect reading
}

void BeltLink::onStatus(const uint8_t* data, size_t len) {
    wilink::Status st{};
    if (!wilink::parseStatus(data, len, st)) return;
    std::lock_guard<std::mutex> g(statusMu_);
    status_ = st;
    haveStatus_ = true;
    statusAtMs_ = millis();
}

// Single choke point for every write. Refuses anything that is not an 0xA2
// frame, so the OTA group (0xE8) cannot be emitted even by a future bug.
bool BeltLink::send(const wilink::Frame& f) {
    if (f.len < 2 || f.len > sizeof(f.bytes) || f.bytes[0] != 0xF7 || f.bytes[1] != kCmdByte) return false;
    std::lock_guard<std::mutex> g(gattMu_);
    if (!cmd_) return false;
    return cmd_->writeValue(f.bytes, f.len, false);  // without response
}

void BeltLink::setSpeed(uint8_t tenthKmh) { send(wilink::setSpeed(tenthKmh)); }
void BeltLink::setMode(wilink::Mode mode) { send(wilink::setMode(mode)); }
void BeltLink::start() { send(wilink::startBelt()); }
void BeltLink::stop() { send(wilink::setMode(wilink::Mode::Standby)); }

uint32_t BeltLink::statusAgeMs() const {
    std::lock_guard<std::mutex> g(statusMu_);
    return haveStatus_ ? millis() - statusAtMs_ : UINT32_MAX;
}

bool BeltLink::requestStatus() {
    return send(wilink::requestStatus());  // the reply arrives later on FE01
}

bool BeltLink::poll(wilink::Status& out) {
    std::lock_guard<std::mutex> g(statusMu_);
    if (!haveStatus_ || millis() - statusAtMs_ > kStatusStaleMs) return false;
    out = status_;
    return true;
}
