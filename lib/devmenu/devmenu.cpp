#include "devmenu.h"

#include <stdio.h>
#include <string.h>

namespace devmenu {
namespace {

void set(char* dst, size_t cap, const char* src) {
    if (!src) src = "";
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

template <size_t N>
void set(char (&dst)[N], const char* src) {
    set(dst, N, src);
}

const char* onOff(bool b) { return b ? "ON" : "OFF"; }

}  // namespace

void DevMenu::open() {
    step_ = Step::Root;
    ssid_[0] = typed_[0] = 0;
    shift_ = false;
    numeric_ = false;
}

Screen DevMenu::screen(const Inputs& in) const {
    Screen s;
    switch (step_) {
        case Step::Root: {
            set(s.title, "DEVELOPER");
            set(s.left, "CLOSE");
            set(s.right, "");
            s.phase = 60;

            set(s.rows[0].label, "Developer mode");
            set(s.rows[0].value, onOff(in.devOn));

            set(s.rows[1].label, "UDP log");
            // Saying ON while the radio is down would be a lie; the flag is
            // stored but nothing is broadcast outside developer mode.
            set(s.rows[1].value, in.devOn ? onOff(in.udpOn) : "needs dev");
            s.rows[1].enabled = in.devOn;

            set(s.rows[2].label, "Wi-Fi");
            if (in.wifiUp) set(s.rows[2].value, in.ip);
            else if (in.storedSsid && in.storedSsid[0]) set(s.rows[2].value, "offline");
            else set(s.rows[2].value, "not set");
            s.rows[2].enabled = in.devOn;

            set(s.rows[3].label, in.storedSsid && in.storedSsid[0] ? in.storedSsid : "(no network)");
            set(s.rows[3].value, in.storedSsid && in.storedSsid[0] ? "forget" : "");
            s.rows[3].enabled = in.devOn && in.storedSsid && in.storedSsid[0];

            set(s.rows[4].label, "Protocol");
            set(s.rows[4].value, in.protocol == Protocol::Ftms ? "FTMS" : "Domyos");

            s.rowCount = 5;
            break;
        }
        case Step::WifiScan: {
            set(s.title, "CHOOSE NETWORK");
            set(s.left, "BACK");
            set(s.right, "RESCAN");
            s.phase = in.scanning ? 61 : 62;
            if (in.scanning) {
                set(s.rows[0].label, "Scanning...");
                s.rows[0].enabled = false;
                s.rowCount = 1;
                break;
            }
            if (in.networkCount == 0) {
                set(s.rows[0].label, "No networks found");
                s.rows[0].enabled = false;
                set(s.rows[1].label, "Move closer, RESCAN");
                s.rows[1].enabled = false;
                s.rowCount = 2;
                s.phase = 63;
                break;
            }
            const uint8_t n = in.networkCount < kMaxRows ? in.networkCount : kMaxRows;
            for (uint8_t i = 0; i < n; ++i) set(s.rows[i].label, in.networks[i]);
            s.rowCount = n;
            break;
        }
        case Step::WifiPass: {
            set(s.title, "PASSWORD");
            set(s.left, "BACK");
            set(s.right, "CONNECT");
            s.phase = 64;
            s.keyboard = true;
            s.shift = shift_;
            s.numeric = numeric_;
            // set() already truncates to the field's capacity; the extra
            // buffer only added a second, narrower limit and a warning.
            set(s.prompt, ssid_);
            set(s.typed, typed_);
            break;
        }
    }
    return s;
}

Action DevMenu::tapRow(uint8_t row, const Inputs& in) {
    const Screen s = screen(in);
    if (row >= s.rowCount || !s.rows[row].enabled) return Action::None;

    switch (step_) {
        case Step::Root:
            switch (row) {
                case 0: return Action::ToggleDev;
                case 1: return Action::ToggleUdp;
                case 2: step_ = Step::WifiScan; return Action::StartScan;
                case 3: return Action::ForgetWifi;
                case 4: return Action::CycleProtocol;
                default: return Action::None;
            }
        case Step::WifiScan:
            if (row < in.networkCount) {
                set(ssid_, in.networks[row]);
                typed_[0] = 0;
                shift_ = false;
                numeric_ = false;
                step_ = Step::WifiPass;
            }
            return Action::None;
        case Step::WifiPass:
            return Action::None;  // the keyboard owns its own hit testing
    }
    return Action::None;
}

Action DevMenu::press(bool right, const Inputs& in) {
    (void)in;
    switch (step_) {
        case Step::Root:
            return right ? Action::None : Action::Close;
        case Step::WifiScan:
            if (right) return Action::StartScan;
            step_ = Step::Root;
            return Action::None;
        case Step::WifiPass:
            if (!right) {
                step_ = Step::WifiScan;
                return Action::None;
            }
            step_ = Step::Root;
            return Action::SubmitPassword;
    }
    return Action::None;
}

void DevMenu::append(char c) {
    const size_t n = strlen(typed_);
    if (n + 1 >= kMaxPassword) return;  // refuse rather than truncate
    typed_[n] = c;
    typed_[n + 1] = 0;
    shift_ = false;  // one shifted character, like a phone keyboard
}

void DevMenu::backspace() {
    const size_t n = strlen(typed_);
    if (n) typed_[n - 1] = 0;
}

void DevMenu::toggleShift() { shift_ = !shift_; }

void DevMenu::toggleNumeric() { numeric_ = !numeric_; shift_ = false; }

}  // namespace devmenu
