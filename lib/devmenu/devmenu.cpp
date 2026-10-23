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

const char* otaWord(OtaState st) {
    switch (st) {
        case OtaState::Idle: return "not checked";
        case OtaState::Checking: return "checking...";
        case OtaState::UpToDate: return "up to date";
        case OtaState::Available: return "update available";
        case OtaState::Installing: return "installing...";
        case OtaState::Failed: return "failed";
    }
    return "";
}

/// Why the Update screen cannot act right now, or nullptr when it can.
const char* updateBlocker(const Inputs& in, char* buf, size_t cap) {
    if (!in.devOn) return "needs dev mode";
    if (!in.wifiUp) return "needs Wi-Fi";
    if (in.otaState == OtaState::Available && in.otaEnvMismatch) {
        snprintf(buf, cap, "image is for %s", in.otaImageEnv ? in.otaImageEnv : "");
        return buf;
    }
    return nullptr;
}

/// The Update screen's right-hand button label: "" when nothing can be done.
const char* updateButton(const Inputs& in, bool blocked) {
    if (blocked) return "";
    switch (in.otaState) {
        case OtaState::Idle:
        case OtaState::UpToDate:
        case OtaState::Failed: return "CHECK";
        case OtaState::Available: return "INSTALL";
        case OtaState::Checking:
        case OtaState::Installing: return "";
    }
    return "";
}

}  // namespace

void DevMenu::open() {
    step_ = Step::Root;
    ssid_[0] = typed_[0] = 0;
    shift_ = false;
    page_ = Page::Letters;
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
            else if (in.wifiConnecting) set(s.rows[2].value, "connecting...");
            else if (in.wifiFailed) set(s.rows[2].value, "wrong password?");
            else if (in.storedSsid && in.storedSsid[0]) set(s.rows[2].value, "offline");
            else set(s.rows[2].value, "not set");
            s.rows[2].enabled = in.devOn;

            set(s.rows[3].label, in.storedSsid && in.storedSsid[0] ? in.storedSsid : "(no network)");
            set(s.rows[3].value, in.storedSsid && in.storedSsid[0] ? "forget" : "");
            s.rows[3].enabled = in.devOn && in.storedSsid && in.storedSsid[0];

            set(s.rows[4].label, "Update");  // opens the screen even with dev mode off
            set(s.rows[4].value, in.otaVersion);

            set(s.rows[5].label, "Re-run setup");  // works with dev mode off

            s.rowCount = 6;
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
            const uint8_t n = in.networkCount < kMaxNetworks ? in.networkCount : kMaxNetworks;
            for (uint8_t i = 0; i < n; ++i) set(s.rows[i].label, in.networks[i]);
            s.rowCount = n;
            break;
        }
        case Step::WifiPass: {
            set(s.title, "PASSWORD");
            set(s.left, "BACK");
            set(s.right, "CONNECT");
            s.phase = 64 + (int)page_;  // one phase per keyboard page: each is its own screen
            s.keyboard = true;
            s.shift = shift_;
            s.page = page_;
            // set() already truncates to the field's capacity; the extra
            // buffer only added a second, narrower limit and a warning.
            set(s.prompt, ssid_);
            set(s.typed, typed_);
            break;
        }
        case Step::Update: {
            set(s.title, "UPDATE");
            set(s.left, "BACK");
            s.phase = 70 + (int)in.otaState;

            char why[sizeof(Row::value)];
            const char* blocker = updateBlocker(in, why, sizeof why);
            set(s.right, updateButton(in, blocker != nullptr));

            set(s.rows[0].label, "Running");
            set(s.rows[0].value, in.otaVersion);
            set(s.rows[1].label, "Available");
            set(s.rows[1].value, in.otaAvailable);
            set(s.rows[2].label, "Status");
            set(s.rows[2].value, blocker ? blocker : otaWord(in.otaState));
            set(s.rows[3].label, in.otaMessage);
            set(s.rows[4].label, "Source");
            set(s.rows[4].value, in.otaHost);
            // Informational: the buttons act, the rows do not.
            for (uint8_t i = 0; i < 5; ++i) s.rows[i].enabled = false;
            s.rowCount = 5;
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
                case 4: step_ = Step::Update; return Action::OpenUpdate;
                case 5: return Action::RerunSetup;
                default: return Action::None;
            }
        case Step::WifiScan:
            if (row < in.networkCount) {
                set(ssid_, in.networks[row]);
                typed_[0] = 0;
                shift_ = false;
                page_ = Page::Letters;
                step_ = Step::WifiPass;
            }
            return Action::None;
        case Step::WifiPass:
            return Action::None;  // the keyboard owns its own hit testing
        case Step::Update:
            return Action::None;  // informational rows
    }
    return Action::None;
}

Action DevMenu::press(bool right, const Inputs& in) {
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
        case Step::Update: {
            if (!right) {
                step_ = Step::Root;
                return Action::None;
            }
            // The button label is the single source of truth for what a press does.
            const Screen s = screen(in);
            if (strcmp(s.right, "CHECK") == 0) return Action::CheckOta;
            if (strcmp(s.right, "INSTALL") == 0) return Action::InstallOta;
            return Action::None;
        }
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

void DevMenu::setPage(Page p) { page_ = p; shift_ = false; }

}  // namespace devmenu
