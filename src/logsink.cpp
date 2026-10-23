#include "logsink.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#include <stdlib.h>
#include <string.h>

#include <mutex>

#include "config.h"

namespace logsink {
namespace {

constexpr size_t kRing = 8192;      // bytes of pending log text
constexpr size_t kMaxLine = 200;
constexpr uint32_t kFlushMs = 1000;  // SD flush cadence
constexpr uint32_t kWifiRetryMs = 15000;

// SD pins come from M5Unified's own per-board table rather than being
// hardcoded: a wrong guess here is indistinguishable from "no card inserted",
// which cost real time once already.
int sdSck = -1, sdMiso = -1, sdMosi = -1, sdCs = -1;

std::mutex mu;
char ring[kRing];
size_t head = 0, tail = 0;  // tail..head is pending; empty when equal
uint32_t dropped = 0;

bool sdReady = false;
bool wifiTried = false;
uint32_t wifiBeginMs = 0;      // when the last setWifi() began a connect
bool wifiRequested = false;    // a connect is outstanding since wifiBeginMs
constexpr uint32_t kWifiConnectMs = 15000;
bool udpStarted = false;
constexpr uint8_t kMaxScan = 5;
char scanNames[kMaxScan][33] = {};
uint8_t scanFound = 0;
bool scanBusy = false;
char ssidBuf[48] = {};
char ipBuf[20] = {};
bool devMode = false;   // master: no radio at all unless this is on
bool sdWanted = false;  // SD probing is opt-in; see begin()
bool udpWanted = false; // the broadcast itself, inside dev mode
uint32_t lastFlush = 0, lastWifiTry = 0;
char sdPath[32] = "";
uint32_t sdHz = 0;
File sdFile;
WiFiUDP udp;
Preferences prefs;

// Every datagram opens with "<millis> udp-seq <n>". Broadcast frames are not
// acknowledged on Wi-Fi, so a lost one silently deletes a second of evidence;
// with a counter the gap is at least visible to the reader.
uint32_t udpSeq = 0;
constexpr size_t kChunk = 1200;  // payload per datagram, below one Ethernet MTU
// A host that sends "hello" to LOG_UDP_PORT gets every datagram unicast as
// well, for this long. Unicast frames are acknowledged and retried by the
// Wi-Fi MAC, so on a quiet network loss becomes rare instead of routine.
constexpr uint32_t kUnicastMs = 120000;
IPAddress unicastIp;           // 0.0.0.0 when nobody has said hello
uint32_t unicastUntilMs = 0;

bool unicastLive(uint32_t nowMs) {
    return unicastIp != IPAddress((uint32_t)0) && (int32_t)(unicastUntilMs - nowMs) > 0;
}

// Drains the socket without blocking. Anything but "hello" is ignored.
void readHello(uint32_t nowMs) {
    char buf[16];
    for (int i = 0; i < 8; ++i) {  // bounded: loop() must not stall on a flood
        if (udp.parsePacket() <= 0) return;
        const int n = udp.read(buf, sizeof buf);
        if (n >= 5 && memcmp(buf, "hello", 5) == 0) {
            const IPAddress from = udp.remoteIP();
            if (from != unicastIp) line("udp unicast to %s", from.toString().c_str());
            unicastIp = from;
            unicastUntilMs = nowMs + kUnicastMs;
        }
        udp.clear();  // discard the rest of this datagram
    }
}

// One datagram: the header line, then `chunk`. Sent broadcast, and unicast
// too while a hello sender is live.
void sendChunk(const char* chunk, size_t len, uint32_t nowMs) {
    static char pkt[kChunk + 40];
    // The header carries the first line's own millis rather than now: lines in
    // the chunk are older than now, and a header newer than the line after it
    // would read as a reboot to anything that splits a log on millis going
    // backwards (logparse, assert_capture).
    char* end = nullptr;
    const unsigned long first = strtoul(chunk, &end, 10);
    const unsigned long ms = (end != chunk && end && *end == ' ') ? first : (unsigned long)nowMs;
    const int h = snprintf(pkt, sizeof pkt, "%lu udp-seq %lu\n", ms, (unsigned long)++udpSeq);
    if (h <= 0 || (size_t)h + len > sizeof pkt) return;
    memcpy(pkt + h, chunk, len);
    const size_t total = (size_t)h + len;
    // The subnet broadcast address is delivered where the 255.255.255.255
    // limited broadcast often is not.
    if (udp.beginPacket(WiFi.broadcastIP(), LOG_UDP_PORT)) {
        udp.write((const uint8_t*)pkt, total);
        udp.endPacket();
    }
    if (unicastLive(nowMs) && udp.beginPacket(unicastIp, LOG_UDP_PORT)) {
        udp.write((const uint8_t*)pkt, total);
        udp.endPacket();
    }
}

// A flush with nothing to send still tells a hello sender how far the log has
// got: a header-only datagram `<now> udp-seq <n> idle`, unicast only, meaning
// "datagrams up to n have been sent, nothing since". Without it a quiet log is
// silent, and a listener cannot tell "nothing was logged" from "the datagram
// was lost". It repeats n rather than taking a new number, which keeps
// broadcast-only listeners (logcat.py) free of gaps for heartbeats they never
// receive, and it says `idle` so a reader never mistakes it for datagram n
// itself: if datagram n was lost, the heartbeat proves it was sent.
void sendHeartbeat(uint32_t nowMs) {
    if (udpSeq == 0 || !unicastLive(nowMs)) return;
    char pkt[40];
    const int h = snprintf(pkt, sizeof pkt, "%lu udp-seq %lu idle\n", (unsigned long)nowMs, (unsigned long)udpSeq);
    if (h <= 0 || (size_t)h >= sizeof pkt) return;
    if (udp.beginPacket(unicastIp, LOG_UDP_PORT)) {
        udp.write((const uint8_t*)pkt, (size_t)h);
        udp.endPacket();
    }
}

size_t pending() { return (head + kRing - tail) % kRing; }

// SD.cardType() reaches into the card object, which is only meaningful once
// SD.begin() has been attempted. With SD probing off that never happens, so
// asking anyway hangs the boot before a single log line is written.
int cardType() { return sdReady ? (int)SD.cardType() : 0; }

// Pushes raw text into the ring. Caller holds mu. A line that will not fit is
// dropped whole and counted, rather than half-written: a truncated log line is
// worse than a missing one, because it reads as real.
void push(const char* s, size_t n) {
    if (n + 1 >= kRing - pending()) {
        ++dropped;
        return;
    }
    for (size_t i = 0; i < n; ++i) {
        ring[head] = s[i];
        head = (head + 1) % kRing;
    }
}

void openSdFile() {
    for (int i = 0; i < 1000; ++i) {
        snprintf(sdPath, sizeof sdPath, "/bridge-%03d.log", i);
        if (!SD.exists(sdPath)) break;
    }
    sdFile = SD.open(sdPath, FILE_APPEND);
    sdReady = (bool)sdFile;
}

void startWifi() {
    if (!devMode) return;  // shipped firmware never brings the radio up
    char ssid[48] = "", pass[64] = "";
    prefs.begin("bridge", true);
    prefs.getString("wifi_ssid", ssid, sizeof ssid);
    prefs.getString("wifi_pass", pass, sizeof pass);
    prefs.end();
    if (ssid[0] == 0) return;
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    wifiTried = true;
    // Same bookkeeping as setWifi(): the row reads "connecting..." rather
    // than "offline" while this attempt is outstanding.
    wifiBeginMs = millis();
    wifiRequested = true;
}

void stopWifi() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    wifiTried = udpStarted = false;
    wifiRequested = false;  // nothing outstanding; a later drop is not a failed attempt
}

}  // namespace

// Mount is retried down the speed range: a card that refuses 25 MHz often
// takes 4, and "no card" and "card too slow for this bus" look identical from
// a single failed begin().
bool mountSd() {
    sdSck  = M5.getPin(m5::pin_name_t::sd_spi_sclk);
    sdMiso = M5.getPin(m5::pin_name_t::sd_spi_miso);
    sdMosi = M5.getPin(m5::pin_name_t::sd_spi_mosi);
    sdCs   = M5.getPin(m5::pin_name_t::sd_spi_cs);
    if (sdSck < 0 || sdMiso < 0 || sdMosi < 0 || sdCs < 0) return false;
    SPI.begin(sdSck, sdMiso, sdMosi, sdCs);
    // Two attempts, not four: each failure makes the SD library log an error,
    // and with no card inserted that is four lines of noise on every boot for
    // no extra diagnostic value.
    for (uint32_t hz : {25000000u, 4000000u}) {
        SD.end();
        if (SD.begin((uint8_t)sdCs, SPI, hz)) {
            sdHz = hz;
            return true;
        }
        delay(50);
    }
    return false;
}

void begin() {
    prefs.begin("bridge", true);
    devMode = prefs.getBool("dev_mode", false);
    udpWanted = prefs.getBool("log_udp", false);
    sdWanted = prefs.getBool("log_sd", false);
    prefs.end();
    // SD probing is opt-in and off by default. Mounting claims GPIO 35/36/37
    // for SPI, and on an ESP32-S3 those are octal PSRAM pins; the CoreS3 has
    // 8 MB of PSRAM. Probing a slot that has never answered (cardType reports
    // 0, i.e. no card) is not worth touching them for. Turn it on with
    // "log sd on" when a card is actually present.
    if (sdWanted && mountSd()) openSdFile();
    startWifi();
    line("logsink: dev=%d udp=%d sd=%d want_sd=%d type=%d hz=%lu pins sck=%d miso=%d mosi=%d cs=%d path=%s wifi=%d",
         (int)devMode, (int)udpWanted, (int)sdReady, (int)sdWanted, cardType(),
         (unsigned long)sdHz, sdSck, sdMiso, sdMosi, sdCs,
         sdPath[0] ? sdPath : "-", (int)wifiTried);
}

void line(const char* fmt, ...) {
    char buf[kMaxLine];
    va_list ap;
    va_start(ap, fmt);
    int n = snprintf(buf, sizeof buf, "%lu ", (unsigned long)millis());
    if (n < 0) n = 0;
    int m = vsnprintf(buf + n, sizeof buf - (size_t)n - 2, fmt, ap);
    va_end(ap);
    size_t len = (size_t)n + (m > 0 ? (size_t)m : 0);
    if (len > sizeof buf - 2) len = sizeof buf - 2;
    buf[len++] = '\n';
    std::lock_guard<std::mutex> g(mu);
    push(buf, len);
}

void hex(const char* tag, const uint8_t* data, size_t len) {
    char buf[kMaxLine];
    size_t n = 0;
    for (size_t i = 0; i < len && n + 3 < sizeof buf; ++i)
        n += (size_t)snprintf(buf + n, sizeof buf - n, "%s%02x", i ? " " : "", data[i]);
    buf[n] = 0;
    line("%s %s", tag, buf);
}

void tick(uint32_t nowMs) {
    if (devMode && wifiTried && WiFi.status() != WL_CONNECTED &&
        nowMs - lastWifiTry >= kWifiRetryMs) {
        lastWifiTry = nowMs;
        WiFi.reconnect();
    }
    const bool udpUp = devMode && udpWanted && WiFi.status() == WL_CONNECTED;
    if (udpUp) {
        // Bound to LOG_UDP_PORT rather than an ephemeral port, so a listener
        // can say hello to <bridge ip>:9999. The socket has to exist before
        // the first send.
        if (!udpStarted) udpStarted = udp.begin(LOG_UDP_PORT) == 1;
        if (udpStarted) readHello(nowMs);
    }
    if (nowMs - lastFlush < kFlushMs) return;
    lastFlush = nowMs;

    // Copy out under the lock, then do I/O without it: an SD write can take
    // tens of ms and must not stall a BLE callback trying to log.
    static char out[kRing];
    size_t n = 0;
    {
        std::lock_guard<std::mutex> g(mu);
        while (tail != head && n < sizeof out) {
            out[n++] = ring[tail];
            tail = (tail + 1) % kRing;
        }
    }
    if (n == 0) {
        if (udpUp && udpStarted) sendHeartbeat(nowMs);
        return;
    }

    if (sdReady) {
        sdFile.write((const uint8_t*)out, n);
        sdFile.flush();
    }
    if (udpUp && udpStarted) {
        // Chunks end on a line boundary, so each datagram starts with a whole
        // line and a lost one takes whole lines with it, never half of one.
        for (size_t off = 0; off < n;) {
            size_t chunk = (n - off < kChunk) ? n - off : kChunk;
            if (off + chunk < n) {
                size_t cut = chunk;
                while (cut > 0 && out[off + cut - 1] != '\n') --cut;
                if (cut > 0) chunk = cut;  // lines are < kMaxLine, so one always fits
            }
            sendChunk(out + off, chunk, nowMs);
            off += chunk;
        }
    }
}

bool sdOk() { return sdReady; }
bool netOk() { return devMode && WiFi.status() == WL_CONNECTED; }
bool devOn() { return devMode; }
bool udpOn() { return udpWanted; }

void setDev(bool on) {
    if (on == devMode) return;
    devMode = on;
    prefs.begin("bridge", false);
    prefs.putBool("dev_mode", on);
    prefs.end();
    line("dev mode %s", on ? "ON" : "OFF");
    if (on) startWifi();
    else stopWifi();  // leaving dev mode takes the radio down with it
}

void setSd(bool on) {
    sdWanted = on;
    prefs.begin("bridge", false);
    prefs.putBool("log_sd", on);
    prefs.end();
    line("sd logging %s (takes effect after a restart)", on ? "ON" : "OFF");
}

bool sdWantedOn() { return sdWanted; }

void setUdp(bool on) {
    udpWanted = on;
    prefs.begin("bridge", false);
    prefs.putBool("log_udp", on);
    prefs.end();
    line("udp log %s", on ? "ON" : "OFF");
}

void setWifi(const char* ssid, const char* pass) {
    prefs.begin("bridge", false);
    prefs.putString("wifi_ssid", ssid);
    prefs.putString("wifi_pass", pass);
    prefs.end();
    WiFi.disconnect();
    if (ssid[0] && devMode) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid, pass);
        wifiTried = true;
        wifiBeginMs = millis();
        wifiRequested = true;
    } else {
        wifiTried = false;
        wifiRequested = false;
    }
}

bool wifiConnecting() {
    // A connect that succeeded is finished: a later AP drop is not a failure
    // of that request, so it must not read as "wrong password?".
    if (WiFi.status() == WL_CONNECTED) wifiRequested = false;
    return wifiRequested && WiFi.status() != WL_CONNECTED &&
           WiFi.status() != WL_CONNECT_FAILED && millis() - wifiBeginMs < kWifiConnectMs;
}

bool wifiFailed() {
    if (WiFi.status() == WL_CONNECTED) wifiRequested = false;
    if (!wifiRequested) return false;
    return WiFi.status() == WL_CONNECT_FAILED || millis() - wifiBeginMs >= kWifiConnectMs;
}

const char* storedSsid() {
    prefs.begin("bridge", true);
    prefs.getString("wifi_ssid", ssidBuf, sizeof ssidBuf);
    prefs.end();
    return ssidBuf;
}

const char* ipText() {
    if (WiFi.status() != WL_CONNECTED) { ipBuf[0] = 0; return ipBuf; }
    snprintf(ipBuf, sizeof ipBuf, "%s", WiFi.localIP().toString().c_str());
    return ipBuf;
}

void startScan() {
    if (!devMode) return;  // the radio is down outside developer mode
    WiFi.mode(WIFI_STA);
    WiFi.scanDelete();
    scanFound = 0;
    scanBusy = WiFi.scanNetworks(/*async=*/true) == WIFI_SCAN_RUNNING;
    line("wifi scan started");
}

bool scanning() {
    if (!scanBusy) return false;
    const int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return true;
    scanBusy = false;
    if (n < 0) {  // failed; report as "none found" rather than hanging
        scanFound = 0;
        line("wifi scan failed");
        return false;
    }
    scanFound = (uint8_t)(n < (int)kMaxScan ? n : (int)kMaxScan);
    for (uint8_t i = 0; i < scanFound; ++i)
        snprintf(scanNames[i], sizeof scanNames[i], "%s", WiFi.SSID(i).c_str());
    line("wifi scan: %d found", n);
    WiFi.scanDelete();
    return false;
}

uint8_t scanCount() { return scanFound; }

const char* scanSsid(uint8_t i) { return i < scanFound ? scanNames[i] : ""; }

void describe(char* out, size_t cap) {
    uint32_t d;
    {
        std::lock_guard<std::mutex> g(mu);
        d = dropped;
    }
    char uniBuf[20] = "-";
    if (unicastLive(millis())) snprintf(uniBuf, sizeof uniBuf, "%s", unicastIp.toString().c_str());
    snprintf(out, cap, "log dev=%d udp=%d sd=%d want_sd=%d type=%d hz=%lu pins=%d/%d/%d/%d file=%s wifi=%d ip=%s port=%d dropped=%lu unicast=%s",
             (int)devMode, (int)udpWanted, (int)sdReady, (int)sdWanted, cardType(),
             (unsigned long)sdHz,
             sdSck, sdMiso, sdMosi, sdCs, sdPath[0] ? sdPath : "-", (int)(WiFi.status() == WL_CONNECTED),
             WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "-",
             (int)LOG_UDP_PORT, (unsigned long)d, uniBuf);
}

}  // namespace logsink
