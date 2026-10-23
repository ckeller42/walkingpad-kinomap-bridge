#pragma once
#include <stddef.h>

/**
 * @file console_cmd.h
 * @brief Serial command console: parsing only.
 *
 * No Arduino types, so it is unit-tested natively. `src/main.cpp` executes the
 * commands through the same paths as the touch UI (`BridgeState` /
 * `applyOutcome` / `BeltGate`), so the console can never move the belt by a
 * route the arming gate does not see.
 *
 * Protocol, one command per line. Every command prints zero or more information
 * lines and then **exactly one** terminating line, `ok` or `err <reason>`, so a
 * host script waits for a known token instead of a timeout.
 *
 *     status                one line of key=value pairs, then ok
 *     arm | disarm
 *     start | stop
 *     sim on | sim off      ok if already so, else "err reboot-required"
 *     wizard [next|back]    re-enter setup (next/back press the right/left button)
 *     speed up | down       one 0.5 km/h step (armed only: "err not-armed")
 *     speed <km/h>          absolute, e.g. "speed 3.5" (0.1 steps; "err out-of-range", "err bad-value")
 *     cp                    Control Point writes since boot: "cp 00=1 03=7 empty=0" (hex opcode=count), then ok
 *     shot                  emit one screenshot frame now (screenshot builds)
 *     help
 *
 * @warning Opening the serial port **reboots the ESP32-S3**, which drops the
 * belt link and the phone's GATT link and puts Kinomap into a retry loop a
 * stale session never recovers from. So the console cannot be attached to watch
 * a live session (that is what the SD and UDP log sinks are for), and device
 * state does not survive between `tools/console.py` invocations — chain a whole
 * scenario into one call.
 */
namespace console {

/**
 * @brief A recognised command line.
 *
 * Matching is done on a normalised line: lower-cased with runs of whitespace
 * collapsed, so `"  SIM   On "` is `sim on`.
 */
enum class Cmd {
    Empty,   ///< Blank line. Prints no reply at all, not even `ok`.
    Status,  ///< Report the bridge's state as key=value pairs.
    Arm,     ///< Arm the bridge. Arming alone never starts the belt.
    Disarm,  ///< Disarm. Deliberately does not stop a running belt.
    Start,   ///< Run the start sequence, as the UI START button does.
    Stop,    ///< Stop the belt. Works armed or not, and also disarms.
    SimOn,   ///< Ask for the simulated belt. The source is fixed at boot.
    SimOff,  ///< Ask for the real belt. The source is fixed at boot.
    Wizard,      ///< Re-enter first-time setup.
    WizardNext,  ///< Press the wizard's right button (NEXT / SKIP / FINISH).
    WizardBack,  ///< Press the wizard's left button (BACK / LATER).
    SpeedUp,     ///< One 0.5 km/h step up.
    SpeedDown,   ///< One 0.5 km/h step down. Does nothing on a stopped belt.
    /// Absolute speed; the value still has to be read with parseSpeedArg().
    SpeedSet,
    ModeAuto,    ///< Ask the belt for Auto mode, as tapping the badge does.
    ModeManual,  ///< Ask the belt for Manual mode, as tapping the badge does.
    /// Control Point writes counted since boot. The evidence that settled
    /// whether Kinomap ever commands speed: it sends `0x07` and never `0x02`.
    Cp,
    Shot,  ///< Emit one screenshot frame now. Screenshot builds only.
    Help,  ///< List the commands.
    Log,   ///< Report the log sinks.
    /// Wi-Fi credentials; the values still have to be read with
    /// parseWifiArg(), and from the raw line rather than the normalised one.
    Wifi,
    DevOn,   ///< Developer mode on: the radio master switch.
    DevOff,  ///< Developer mode off.
    UdpOn,   ///< UDP log broadcast on. Lives inside developer mode.
    UdpOff,  ///< UDP log broadcast off.
    SdOn,    ///< SD probing on. It claims PSRAM-adjacent pins, hence the switch.
    SdOff,   ///< SD probing off.
    Ota,       ///< Report the firmware-update state.
    OtaCheck,  ///< Check the configured URL for a newer build.
    OtaApply,  ///< Install what the last check found.
    /// Set or clear the update URL; the value still has to be read with
    /// parseOtaUrlArg(), from the raw line.
    OtaUrl,
    Menu,   ///< Leave the developer menu without the touch screen. Typed as `menu close`.
    Touch,  ///< Report the touch panel's own state.
    Unknown,  ///< Not a command. The caller replies `err unknown-command`.
    /// The line was longer than LineBuffer::kMax. The caller replies
    /// `err line-too-long` and must not execute anything, since the tail that
    /// would have changed the meaning is gone.
    TooLong,
};

/**
 * @brief Identify a command line.
 * @param line The line as received, without its newline. Case and extra
 *             whitespace do not matter.
 * @return The matching @ref console::Cmd, Cmd::Empty for a blank line, or Cmd::Unknown.
 * @note Only identifies the command. Commands that carry a value return the
 *       Cmd and leave reading it to parseSpeedArg(), parseWifiArg() or
 *       parseOtaUrlArg(); the last two need the **raw** line, so keep it.
 */
Cmd parse(const char* line);

/** @brief Outcome of reading the argument of `speed <km/h>`. */
enum class SpeedArg {
    /// Not a number the belt could use: no digits, more than two decimals, more
    /// than three whole digits, trailing rubbish, or finer than 0.1 km/h. The
    /// caller replies `err bad-value`.
    Bad,
    /// A valid number outside the advertised range. Kept distinct from Bad so
    /// the reply can say `err out-of-range` and the user learns the number was
    /// understood but refused.
    OutOfRange,
    Ok,  ///< Accepted; the speed was written out.
};

/**
 * @brief Read the argument of `speed <km/h>`.
 *
 * @param line     The command line; normalised internally, so case and spacing
 *                 do not matter.
 * @param minCenti Lowest acceptable speed in 0.01 km/h units.
 * @param maxCenti Highest acceptable speed in 0.01 km/h units.
 * @param tenthKmh Set to the speed in 0.1 km/h units only on SpeedArg::Ok.
 * @return Whether the value was usable; see @ref console::SpeedArg.
 * @note Accepts at most two decimals but insists the result be a whole
 *       multiple of 0.1 km/h, so `3.50` is fine and `3.55` is rejected rather
 *       than quietly rounded — the belt's resolution really is 0.1, and
 *       rounding would report back a speed the user did not ask for.
 */
SpeedArg parseSpeedArg(const char* line, unsigned minCenti, unsigned maxCenti, unsigned char& tenthKmh);

/**
 * @brief Read the arguments of `wifi <ssid> <password>`.
 *
 * @param rawLine The **raw** line, not the normalised one: both fields are
 *                case-sensitive and a password may contain runs of spaces that
 *                normalising would collapse.
 * @param ssid    Receives the SSID, NUL-terminated. Cleared on entry.
 * @param ssidCap Capacity of @p ssid including the NUL.
 * @param pass    Receives the password, NUL-terminated. Cleared on entry.
 * @param passCap Capacity of @p pass including the NUL.
 * @return False when the line is not a `wifi` line or a field does not fit.
 *         An over-long field is **refused, not truncated**: a cut-off
 *         credential looks accepted and then never connects. `wifi` alone, with
 *         no arguments, clears both and returns true.
 * @note The password is everything after the single space that follows the
 *       SSID, with trailing CR/LF removed — so leading spaces in a password are
 *       preserved and a password may contain spaces.
 */
bool parseWifiArg(const char* rawLine, char* ssid, size_t ssidCap, char* pass, size_t passCap);

/**
 * @brief Read the argument of `ota url <absolute-url>`.
 *
 * @param rawLine The **raw** line, because a URL is case-sensitive and
 *                normalising would destroy it.
 * @param url     Receives the URL, NUL-terminated. Cleared on entry.
 * @param cap     Capacity of @p url including the NUL.
 * @return False when the line is not an `ota url` line, or the URL is empty or
 *         does not fit — refused rather than truncated, since half a URL would
 *         fetch the wrong thing. `ota url` alone clears the stored value and
 *         returns true.
 */
bool parseOtaUrlArg(const char* rawLine, char* url, size_t cap);

/**
 * @brief Accumulates incoming bytes into whole lines.
 *
 * One line at a time: the completed line stays available until the next feed(),
 * which starts a fresh one. CR is discarded, so CRLF and LF hosts behave alike.
 */
class LineBuffer {
public:
    /// Longest line kept, excluding the NUL. Sized by `ota url <url>`: the
    /// 8-character prefix plus the longest URL the firmware can store (159
    /// characters, a 160-byte buffer). At 63 the compiled-in 76-character
    /// default URL itself was rejected as `err line-too-long`. src/main.cpp
    /// static_asserts the relationship where the URL buffer is declared.
    static constexpr size_t kMax = 167;

    /**
     * @brief Offer one received byte.
     * @param c The byte. CR is ignored; LF terminates the line.
     * @return True exactly once per line, when the line is complete and
     *         available from line().
     * @note A line longer than @ref kMax keeps its first kMax characters and
     *       sets overflowed(). The overflow is reported only when the newline
     *       arrives, so the caller sees one `err line-too-long` for the whole
     *       line rather than a truncated command it might execute.
     */
    bool feed(char c);

    /**
     * @brief The line most recently completed.
     * @return A NUL-terminated pointer into the buffer, valid until the next
     *         feed(). This is the **raw** line the `wifi` and `ota url` parsers
     *         need.
     */
    const char* line() const { return buf_; }

    /**
     * @brief Whether the completed line had been truncated.
     * @return True when characters were dropped, in which case the line must
     *         not be executed — the part that was lost could have changed what
     *         it means.
     */
    bool overflowed() const { return over_; }

private:
    char buf_[kMax + 1] = {};
    size_t n_ = 0;
    bool over_ = false;
    bool done_ = false;  // previous call completed a line: reset on next feed
};

}  // namespace console
