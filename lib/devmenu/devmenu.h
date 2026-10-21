#pragma once
// The developer menu: the radio switches, Wi-Fi pairing and OTA.
//
// Arduino-free and unit-tested, like lib/wizard. It decides *what* should
// happen and never does it: every press returns an Action for the host to
// carry out, so the state machine can be tested on the host with no BLE, no
// Wi-Fi and no screen.
//
// Reached by tapping the PHONE indicator on the main screen, mirroring the
// BELT indicator that re-opens the setup wizard.

#include <stddef.h>
#include <stdint.h>

namespace devmenu {

/** @brief Which screen of the developer menu is showing. */
enum class Step : uint8_t {
    Root,      ///< The switches.
    WifiScan,  ///< Networks found nearby.
    WifiPass,  ///< On-screen keyboard for the password.
};

/**
 * @brief What the host should do in response to a press.
 *
 * The menu performs nothing itself, so that it stays Arduino-free and
 * host-testable; every effect is carried out by the caller.
 */
enum class Action : uint8_t {
    None,            ///< Nothing to do.
    Close,           ///< Leave the menu and return to the main screen.
    ToggleDev,       ///< Flip developer mode (the radio master switch).
    ToggleUdp,       ///< Flip the UDP log broadcast.
    StartScan,       ///< Begin a Wi-Fi scan.
    SubmitPassword,  ///< Connect using ssid() and password().
    ForgetWifi,      ///< Clear the stored credentials.
    CycleProtocol,   ///< Ask for the next protocol mode (see @ref Protocol).
};

/**
 * @brief Which BLE profile the bridge presents to the phone.
 *
 * Kinomap only commands treadmill speed for Domyos machines, so a future
 * Domyos profile would be the way to obtain app-driven speed. That work is
 * tracked in issue #2 and is **not implemented**; the enumerator exists so the
 * setting is real and selectable rather than invented later, and the host
 * refuses to switch to it.
 */
enum class Protocol : uint8_t {
    Ftms = 0,   ///< Fitness Machine Service. The only working mode.
    Domyos = 1, ///< Proprietary Decathlon/Domyos profile. Not implemented.
};

constexpr uint8_t kMaxNetworks = 6;  ///< Scan results kept.
constexpr size_t kMaxSsid = 33;      ///< 32 characters plus NUL.
constexpr size_t kMaxPassword = 64;  ///< WPA2 allows 63 characters, plus NUL.

/**
 * @brief Everything the menu needs to know about the world to draw itself.
 *
 * Supplied fresh by the host on every call; the menu keeps no copy.
 */
struct Inputs {
    bool devOn = false;           ///< Developer mode is on.
    bool udpOn = false;           ///< UDP log broadcast is on.
    bool wifiUp = false;          ///< Wi-Fi is associated.
    const char* storedSsid = "";  ///< Stored SSID, or "" when none.
    const char* ip = "";          ///< Current IP, or "" when offline.
    bool scanning = false;        ///< A Wi-Fi scan is in progress.
    uint8_t networkCount = 0;     ///< Networks in @ref networks.
    const char* networks[kMaxNetworks] = {};  ///< Scan results, strongest first.
    Protocol protocol = Protocol::Ftms;       ///< Current BLE profile.
};

/** @brief One line of a list screen. */
struct Row {
    char label[24] = {};  ///< Left-hand text.
    char value[18] = {};  ///< Right-hand text, often a state.
    bool enabled = true;  ///< A disabled row is drawn dim and ignores taps.
};

constexpr uint8_t kMaxRows = 5;  ///< Rows that fit on the 320x240 panel.

/**
 * @brief A screen description for the host to draw.
 *
 * Plain data with no pointers into the menu, so the host may hold it across
 * frames.
 */
struct Screen {
    char title[20] = {};   ///< Heading.
    Row rows[kMaxRows];    ///< List rows; only the first @ref rowCount are valid.
    uint8_t rowCount = 0;  ///< Number of valid rows.
    char left[10] = {};    ///< Bottom-bar left button label.
    char right[10] = {};   ///< Bottom-bar right button label, "" if unused.
    int phase = 0;         ///< Distinct per visually-distinct screen, for screenshots.
    bool keyboard = false; ///< Draw the key grid instead of rows.
    char prompt[28] = {};  ///< Keyboard screen: which network is being joined.
    char typed[kMaxPassword] = {};  ///< Keyboard screen: the password so far.
    bool shift = false;    ///< Keyboard screen: next character is shifted.
    bool numeric = false;  ///< Keyboard screen: digits/symbols page is showing.
};

/**
 * @brief The developer menu's screen flow.
 *
 * Reached by tapping the PHONE indicator on the main screen, mirroring the
 * BELT indicator that re-opens the setup wizard.
 */
class DevMenu {
public:
    /** @brief Reset to @ref Step::Root and clear any half-typed password. */
    void open();

    /** @brief The screen currently shown. */
    Step step() const { return step_; }

    /**
     * @brief Render the current screen.
     * @param in Current world state.
     * @return A description the host draws; the menu never touches a display.
     */
    Screen screen(const Inputs& in) const;

    /**
     * @brief Handle a tap on a list row.
     * @param row Zero-based row index; out-of-range and disabled rows are ignored.
     * @param in  Current world state.
     * @return The action the host should carry out.
     */
    Action tapRow(uint8_t row, const Inputs& in);

    /**
     * @brief Handle a bottom-bar press.
     * @param right True for the right button, false for the left.
     * @param in    Current world state.
     * @return The action the host should carry out.
     */
    Action press(bool right, const Inputs& in);

    /**
     * @brief Append one character to the password being typed.
     *
     * A character that would overflow is **refused**, not truncated: a cut-off
     * credential looks accepted and then never connects.
     */
    void append(char c);

    /** @brief Delete the last character, if any. */
    void backspace();

    /** @brief Flip shift. It applies to one character, like a phone keyboard. */
    void toggleShift();

    /** @brief Switch between the letter page and the digits/symbols page. */
    void toggleNumeric();

    /** @brief True while the digits/symbols page is showing. */
    bool numeric() const { return numeric_; }

    /** @brief Whether the next character should be shifted. */
    bool shift() const { return shift_; }

    /** @brief The network chosen on the scan screen. */
    const char* ssid() const { return ssid_; }

    /** @brief The password typed so far. Never logged by this module. */
    const char* password() const { return typed_; }

private:
    Step step_ = Step::Root;
    char ssid_[kMaxSsid] = {};
    char typed_[kMaxPassword] = {};
    bool shift_ = false;
    bool numeric_ = false;
};

}  // namespace devmenu
