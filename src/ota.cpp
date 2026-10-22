#include "ota.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <mbedtls/sha256.h>

#include "logsink.h"

#ifndef FW_VERSION
#define FW_VERSION ""
#endif

// An unset FW_VERSION expands to an empty string rather than being undefined,
// and an empty version would compare equal to an empty manifest field - i.e.
// an untagged local build could report itself "up to date" against a broken
// manifest. Fall back to a name that can never match a release tag.
static const char* kVersion = FW_VERSION[0] ? FW_VERSION : "dev-local";

namespace ota {
namespace {

State st = State::Idle;
char msg[96] = "idle";
char availVer[32] = "";
char manifestUrl[160] = "";
char imageUrl[200] = "";
char wantSha[65] = "";
Preferences prefs;

void say(State s, const char* fmt, ...) {
    st = s;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    logsink::line("ota: %s", msg);
}

// Minimal extractor for the flat manifest this project publishes. A full JSON
// parser would be a dependency for four fields that CI writes itself.
bool field(const char* body, const char* key, char* out, size_t cap) {
    char pat[40];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char* p = strstr(body, pat);
    if (!p) return false;
    p = strchr(p + strlen(pat), ':');
    if (!p) return false;
    while (*p && *p != '"') ++p;       // opening quote of the value
    if (*p != '"') return false;
    ++p;
    size_t n = 0;
    while (*p && *p != '"' && n + 1 < cap) out[n++] = *p++;
    out[n] = 0;
    return n > 0;
}

// One HTTP(S) GET. TLS certificates are not verified; see the header's trust
// model note. The SHA-256 check is what makes a bad image unusable.
bool get(const char* u, HTTPClient& http, WiFiClientSecure& tls, WiFiClient& plain) {
    if (strncmp(u, "https://", 8) == 0) {
        tls.setInsecure();
        return http.begin(tls, u);
    }
    return http.begin(plain, u);
}

void hex32(const uint8_t* d, char* out) {
    for (int i = 0; i < 32; ++i) snprintf(out + i * 2, 3, "%02x", d[i]);
    out[64] = 0;
}

}  // namespace

void begin() {
    prefs.begin("bridge", true);
    prefs.getString("ota_url", manifestUrl, sizeof manifestUrl);
    prefs.end();
    snprintf(msg, sizeof msg, "idle (running %s)", kVersion);
}

const char* version() { return kVersion; }
State state() { return st; }
const char* message() { return msg; }
const char* availableVersion() { return availVer; }
const char* url() { return manifestUrl; }

void setUrl(const char* u) {
    snprintf(manifestUrl, sizeof manifestUrl, "%s", u ? u : "");
    prefs.begin("bridge", false);
    prefs.putString("ota_url", manifestUrl);
    prefs.end();
    say(State::Idle, "manifest url %s", manifestUrl[0] ? manifestUrl : "(cleared)");
}

void check() {
    if (WiFi.status() != WL_CONNECTED) { say(State::Failed, "no network"); return; }
    if (!manifestUrl[0]) { say(State::Failed, "no manifest url set"); return; }

    say(State::Checking, "fetching manifest");
    HTTPClient http;
    WiFiClientSecure tls;
    WiFiClient plain;
    http.setTimeout(10000);
    if (!get(manifestUrl, http, tls, plain)) { say(State::Failed, "bad url"); return; }
    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        say(State::Failed, "manifest http %d", code);
        return;
    }
    const String body = http.getString();
    http.end();

    availVer[0] = imageUrl[0] = wantSha[0] = 0;
    if (!field(body.c_str(), "version", availVer, sizeof availVer) ||
        !field(body.c_str(), "url", imageUrl, sizeof imageUrl) ||
        !field(body.c_str(), "sha256", wantSha, sizeof wantSha)) {
        say(State::Failed, "manifest missing version/url/sha256");
        return;
    }
    if (strcmp(availVer, kVersion) == 0) {
        say(State::UpToDate, "up to date (%s)", kVersion);
        return;
    }
    say(State::Available, "%s available (running %s)", availVer, kVersion);
}

void apply() {
    if (st != State::Available) { say(State::Failed, "nothing to install; check first"); return; }
    if (WiFi.status() != WL_CONNECTED) { say(State::Failed, "no network"); return; }

    say(State::Installing, "downloading %s", availVer);
    HTTPClient http;
    WiFiClientSecure tls;
    WiFiClient plain;
    http.setTimeout(20000);
    if (!get(imageUrl, http, tls, plain)) { say(State::Failed, "bad image url"); return; }
    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        say(State::Failed, "image http %d", code);
        return;
    }
    const int total = http.getSize();
    if (total <= 0) { http.end(); say(State::Failed, "image has no length"); return; }
    if (!Update.begin((size_t)total)) {
        http.end();
        say(State::Failed, "no room: %s", Update.errorString());
        return;
    }

    // Hash while streaming, so the image is never held in RAM and a corrupted
    // download is caught before it is committed.
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buf[1024];
    int left = total;
    uint32_t lastLog = 0;
    while (left > 0 && http.connected()) {
        const size_t avail = stream->available();
        if (!avail) { delay(1); continue; }
        const int n = stream->readBytes(buf, avail > sizeof buf ? sizeof buf : avail);
        if (n <= 0) break;
        mbedtls_sha256_update(&sha, buf, (size_t)n);
        if (Update.write(buf, (size_t)n) != (size_t)n) {
            mbedtls_sha256_free(&sha);
            Update.abort();
            http.end();
            say(State::Failed, "write failed: %s", Update.errorString());
            return;
        }
        left -= n;
        if (millis() - lastLog > 2000) {
            lastLog = millis();
            logsink::line("ota: %d%% (%d bytes left)", (total - left) * 100 / total, left);
        }
    }
    http.end();

    uint8_t digest[32];
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);
    char gotSha[65];
    hex32(digest, gotSha);

    if (left != 0) { Update.abort(); say(State::Failed, "short download (%d left)", left); return; }
    if (strcasecmp(gotSha, wantSha) != 0) {
        Update.abort();  // never commit an image that does not match the manifest
        say(State::Failed, "sha256 mismatch");
        logsink::line("ota: wanted %s", wantSha);
        logsink::line("ota: got    %s", gotSha);
        return;
    }
    if (!Update.end(true)) { say(State::Failed, "finalise failed: %s", Update.errorString()); return; }

    say(State::Installing, "installed %s, rebooting", availVer);
    delay(400);  // let the log line reach the sinks before the reset
    ESP.restart();
}

}  // namespace ota
