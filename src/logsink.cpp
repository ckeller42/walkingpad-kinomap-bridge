#include "logsink.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiUdp.h>

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
bool udpStarted = false;
bool devMode = false;   // master: no radio at all unless this is on
bool udpWanted = false; // the broadcast itself, inside dev mode
uint32_t lastFlush = 0, lastWifiTry = 0;
char sdPath[32] = "";
uint32_t sdHz = 0;
File sdFile;
WiFiUDP udp;
Preferences prefs;

size_t pending() { return (head + kRing - tail) % kRing; }

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
}

void stopWifi() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    wifiTried = udpStarted = false;
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
    for (uint32_t hz : {25000000u, 10000000u, 4000000u, 1000000u}) {
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
    prefs.end();
    if (mountSd()) openSdFile();
    startWifi();
    line("logsink: dev=%d udp=%d sd=%d type=%d hz=%lu pins sck=%d miso=%d mosi=%d cs=%d path=%s wifi=%d",
         (int)devMode, (int)udpWanted, (int)sdReady, (int)SD.cardType(),
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
    if (n == 0) return;

    if (sdReady) {
        sdFile.write((const uint8_t*)out, n);
        sdFile.flush();
    }
    if (devMode && udpWanted && WiFi.status() == WL_CONNECTED) {
        // The socket has to exist before the first send, and the subnet
        // broadcast address is delivered where the 255.255.255.255 limited
        // broadcast often is not.
        if (!udpStarted) udpStarted = udp.begin(0) == 1;
        const IPAddress bcast = WiFi.broadcastIP();
        for (size_t off = 0; off < n; off += 1200) {
            const size_t chunk = (n - off < 1200) ? n - off : 1200;
            if (udp.beginPacket(bcast, LOG_UDP_PORT)) {
                udp.write((const uint8_t*)out + off, chunk);
                udp.endPacket();
            }
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
    } else {
        wifiTried = false;
    }
}

void describe(char* out, size_t cap) {
    uint32_t d;
    {
        std::lock_guard<std::mutex> g(mu);
        d = dropped;
    }
    snprintf(out, cap, "log dev=%d udp=%d sd=%d type=%d hz=%lu pins=%d/%d/%d/%d file=%s wifi=%d ip=%s port=%d dropped=%lu",
             (int)devMode, (int)udpWanted, (int)sdReady, (int)SD.cardType(),
             (unsigned long)sdHz,
             sdSck, sdMiso, sdMosi, sdCs, sdPath[0] ? sdPath : "-", (int)(WiFi.status() == WL_CONNECTED),
             WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "-",
             (int)LOG_UDP_PORT, (unsigned long)d);
}

}  // namespace logsink
