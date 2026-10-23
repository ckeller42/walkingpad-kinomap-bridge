#pragma once
/**
 * @file devmenu.h
 * @brief The developer menu: the radio switches and Wi-Fi pairing.
 */
// The developer menu: the radio switches, Wi-Fi pairing and the firmware
// Update screen (check, install). The serial console's `ota` commands remain.
//
// Arduino-free and unit-tested, like lib/wizard. It decides *what* should
// happen and never does it: every press returns an Action for the host to
// carry out, so the state machine can be tested on the host with no BLE, no
// Wi-Fi and no screen.
//
// Reached by tapping the gear in the top-right corner of the main screen.

#include <stddef.h>
#include <stdint.h>

#include "keyboard_layout.h"

namespace devmenu {

/** @brief Which screen of the developer menu is showing. */
enum class Step : uint8_t {
    Root,      ///< The switches.
    WifiScan,  ///< Networks found nearby.
    WifiPass,  ///< On-screen keyboard for the password.
    Update,    ///< Firmware update: running version, check and install.
};

/** @brief Where the firmware update stands. Mirrors `ota::State`, in the same order. */
enum class OtaState : uint8_t {
    Idle,        ///< Nothing checked yet.
    Checking,    ///< Asking the update server what is current.
    UpToDate,    ///< The server's image is the one already running.
    Available,   ///< A different image is on offer.
    Installing,  ///< Downloading and writing the image.
    Failed,      ///< The last check or install failed; see Inputs::otaMessage.
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
    RerunSetup,      ///< Close the menu and open the setup wizard.
    OpenUpdate,      ///< The Update screen was opened; the host may refresh its state.
    CheckOta,        ///< Ask the update server whether a newer image exists.
    InstallOta,      ///< Download and install the available image.
};

constexpr uint8_t kMaxRows = 6;  ///< Rows that fit on the 320x240 panel.
constexpr uint8_t kMaxNetworks = 5;  ///< Scan results kept; the list screen shows at most this many.
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
    bool wifiConnecting = false;  ///< A connect was requested and is still within its window.
    bool wifiFailed = false;      ///< The last connect was rejected or timed out.
    bool scanning = false;        ///< A Wi-Fi scan is in progress.
    uint8_t networkCount = 0;     ///< Networks in @ref networks.
    const char* networks[kMaxNetworks] = {};  ///< Scan results, strongest first.
    const char* otaVersion = "";    ///< Running firmware version, "" when unknown.
    const char* otaAvailable = "";  ///< Version on offer from the server, "" when none.
    OtaState otaState = OtaState::Idle;  ///< Where the update stands.
    const char* otaMessage = "";    ///< One line of detail for the Update screen, e.g. a failure reason.
    const char* otaHost = "";       ///< Update server host, shown as the source.
    const char* otaImageEnv = "";   ///< Build environment the offered image targets.
    bool otaEnvMismatch = false;    ///< The offered image targets a different build; installing is refused.
    bool beltRunning = false;       ///< The belt is moving; the Update screen does nothing that blocks.
};

/**
 * @brief The host part of a URL, for the Update screen's Source row.
 *
 * Skips the scheme and any `user:password@`, keeps a bracketed IPv6 literal
 * whole, and stops at the port, path, query or fragment.
 *
 * @param url Any URL, or a bare host; nullptr reads as "".
 * @param out Receives the host, always NUL-terminated.
 * @param cap Size of @p out; the host is cut to fit.
 * @return The length written, not counting the NUL.
 */
size_t urlHost(const char* url, char* out, size_t cap);

/** @brief One line of a list screen. */
struct Row {
    char label[40] = {};  ///< Left-hand text; 39 characters, enough for the Update screen's message row.
    char value[24] = {};  ///< Right-hand text, often a state.
    bool enabled = true;  ///< A disabled row is drawn dim and ignores taps.
};

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
    Page page = Page::Letters;  ///< Keyboard screen: which page is showing.
};

/**
 * @brief The developer menu's screen flow.
 *
 * Reached by tapping the gear in the top-right corner of the main screen.
 */
class DevMenu {
public:
    /** @brief Reset to devmenu::Step::Root and clear any half-typed password. */
    void open();

    /**
     * @brief Which screen is showing.
     * @return The current devmenu::Step.
     */
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
     *
     * The forget row needs two taps in a row: the first turns its value into
     * `tap again`, the second returns devmenu::Action::ForgetWifi. Any other
     * tap or press in between disarms it. Rows are 25 px apart, and a stored
     * network should not vanish on a finger that slipped off Wi-Fi or Update.
     */
    Action tapRow(uint8_t row, const Inputs& in);

    /**
     * @brief Whether the forget row is waiting for its confirming tap.
     * @return True after one tap on the forget row, until anything else is touched.
     */
    bool forgetArmed() const { return forgetArmed_; }

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
     * @param c The character to add.
     *
     * A character that would overflow is **refused**, not truncated: a cut-off
     * credential looks accepted and then never connects.
     */
    void append(char c);

    /** @brief Delete the last character, if any. */
    void backspace();

    /** @brief Flip shift. It applies to one character, like a phone keyboard. */
    void toggleShift();

    /**
     * @brief Show another keyboard page. Clears a pending shift.
     * @param p The page a Key::Kind::ToLetters / ToSymbols1 / ToSymbols2 key names.
     */
    void setPage(Page p);

    /**
     * @brief Which keyboard page is showing.
     * @return The current devmenu::Page.
     */
    Page page() const { return page_; }

    /**
     * @brief Whether the next character will be shifted.
     * @return True while shift is pending.
     */
    bool shift() const { return shift_; }

    /**
     * @brief The network chosen on the scan screen.
     * @return Its SSID, or "" when none has been picked.
     */
    const char* ssid() const { return ssid_; }

    /**
     * @brief The password typed so far.
     * @return The characters entered. Never logged by this module.
     */
    const char* password() const { return typed_; }

private:
    Step step_ = Step::Root;
    bool forgetArmed_ = false;
    char ssid_[kMaxSsid] = {};
    char typed_[kMaxPassword] = {};
    bool shift_ = false;
    Page page_ = Page::Letters;
};

}  // namespace devmenu
