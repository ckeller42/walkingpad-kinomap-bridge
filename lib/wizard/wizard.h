#pragma once
#include <stdint.h>

/**
 * @file wizard.h
 * @brief First-time-setup wizard: a pure state machine plus the on-screen text.
 *
 * No Arduino or drawing types, so it is unit-tested natively; `src/ui.cpp` only
 * paints what Wizard::screen() returns.
 *
 * Rule for every screen: `action` says what the USER has to do next, in plain
 * words (never a state name), and both buttons always lead somewhere, so the
 * wizard can never strand anyone.
 *
 * It opens on first boot (no stored `setup_done` flag) and again whenever the
 * BELT indicator is tapped on the main screen, and it always leaves the bridge
 * disarmed — finishing setup must not leave the belt commandable.
 */
namespace wizard {

/**
 * @brief Which screen of the wizard is showing.
 *
 * Not a screen number: the simulated path visits SimInfo instead of Welcome,
 * Scan and Linked, so Screen::stepNo is derived per step rather than taken
 * from the enumerator value.
 */
enum class Step : uint8_t {
    Welcome,  ///< Switch the belt on and close its phone app.
    Scan,     ///< Searching for the belt; also carries the "not found yet" advice.
    Linked,   ///< The belt is delivering status, so the link really works.
    Arm,      ///< Explains arming, once, before it matters.
    Done,     ///< Last screen; its right button finishes setup.
    SimInfo,  ///< Simulated belt: says so plainly and skips pairing entirely.
};

/**
 * @brief Mirror of `BeltLink::Phase`, duplicated to keep this library Arduino-free.
 *
 * Kept in step with the real phase enum by hand; nothing checks that it is.
 */
enum class ScanPhase : uint8_t {
    Idle,        ///< Not scanning.
    Scanning,    ///< Looking for a belt.
    Found,       ///< Seen in an advertisement, not connected yet.
    Connecting,  ///< Connecting to the address that was found.
};

/** @brief Which bottom-bar button was pressed. */
enum class Btn : uint8_t {
    Left,   ///< BACK, or LATER on the first screen of either path.
    Right,  ///< NEXT, SKIP on the scan screen, FINISH on the last one.
};

/** @brief What the host should do after Wizard::press(). */
enum class Result : uint8_t {
    Continue,  ///< Stay in the wizard and redraw.
    Later,     ///< The user left without finishing: setup is NOT marked complete.
    Finished,  ///< FINISH on the last screen: mark setup complete.
};

/**
 * @brief Everything the wizard needs to know about the world to draw itself.
 *
 * Supplied fresh by the host on every call; the wizard keeps no copy, which is
 * why screen() and press() both take it.
 */
struct Inputs {
    /// A simulated belt is bound. Sends the user down the three-step path:
    /// the wizard must never pretend to pair when there is nothing to pair with.
    bool sim = false;
    /// The real belt is linked **and** delivering fresh status. A sighting is
    /// not enough — the Linked screen exists to confirm a working link rather
    /// than an advertisement, so a half-open connection cannot be mistaken for
    /// success.
    bool beltUp = false;
    ScanPhase scan = ScanPhase::Idle;  ///< Scan phase, which picks the Scan screen's wording.
    /// Seconds spent on the Scan screen. Past @ref kScanHintSec the screen
    /// switches from reassurance to instructions.
    uint32_t scanSec = 0;
    /// Address of the belt found, if any. May be null; the Scan screen renders
    /// a null as "" rather than dereferencing it.
    const char* foundAddr = "";
};

/**
 * @brief How long to search before offering advice instead of a bare "not found".
 *
 * A real belt is normally seen in under 10 s, so 30 s means something the user
 * can fix: the belt is off, its own phone app holds the single connection the
 * belt allows, or the bridge is too far away.
 */
constexpr uint32_t kScanHintSec = 30;

/**
 * @brief A screen description for the host to draw.
 *
 * Plain character arrays with no pointers back into the wizard, so the host may
 * hold it across frames. Text is pre-fitted to the 320x240 panel; the host does
 * no layout and no wrapping of its own.
 */
struct Screen {
    uint8_t stepNo = 1;     ///< 1-based position in the flow, for "step N of M".
    uint8_t stepTotal = 5;  ///< Steps in this path: 5 for a real belt, 3 for the simulated one.
    /// Distinct per visually-distinct screen, and the trigger the screenshot
    /// build keys off. Simulated variants add 1000, so they are captured as
    /// separate frames instead of being deduplicated against the real ones.
    int phase = 0;
    char title[28] = {};      ///< Heading.
    /// What the user has to do next, drawn big and white. Plain words only,
    /// never a state name.
    char action[2][28] = {};
    char detail[4][36] = {};  ///< Why, and extra context, drawn small and grey.
    /// Left button label: BACK, or LATER on a first screen where going back
    /// would mean leaving.
    char left[10] = {};
    char right[10] = {};      ///< Right button label: NEXT, SKIP or FINISH.
};

/**
 * @brief The setup wizard's screen flow.
 *
 * Decides *what* should happen and never does it: it paints nothing, touches no
 * radio, and takes the belt's state as an argument, so the whole flow — including
 * a belt that drops out mid-setup — is testable on the host.
 */
class Wizard {
public:
    /**
     * @brief Begin at the first screen of the appropriate path.
     * @param sim True when a simulated belt is bound, which starts at
     *            Step::SimInfo and skips pairing; false starts at Step::Welcome.
     * @note Always restarts from the beginning, even if the wizard was part-way
     *       through, so re-entering from the BELT indicator is predictable.
     */
    void start(bool sim);

    /**
     * @brief Which screen is showing.
     * @return The current @ref Step.
     */
    Step step() const { return step_; }

    /**
     * @brief Fold the belt's current state into the current step.
     *
     * Scan advances to Linked once the belt delivers status, and Linked falls
     * back to Scan if it is lost, so a belt switched off mid-setup returns to
     * searching by itself instead of showing a link that is gone. Does nothing
     * on the simulated path, which never pairs.
     *
     * @param in Current world state.
     * @return True when the step changed, so the host knows to redraw.
     */
    bool update(const Inputs& in);

    /**
     * @brief Handle a bottom-bar press.
     * @param b  Which button was pressed.
     * @param in Current world state; needed only to decide where BACK from the
     *           Arm screen returns to.
     * @return Result::Finished only from the last screen's right button;
     *         Result::Later when the user left a first screen by the left
     *         button, which must NOT mark setup complete; otherwise
     *         Result::Continue.
     * @note Every press leads somewhere. The Scan screen's right button is SKIP
     *       rather than NEXT, so a belt that cannot be found never traps anyone
     *       on a screen with no way forward.
     */
    Result press(Btn b, const Inputs& in);

    /**
     * @brief Render the current screen.
     * @param in Current world state.
     * @return A description the host draws; the wizard never touches a display.
     * @note The last screen has two forms. With a real belt that is not up it
     *       says the belt is off, because Kinomap verifies it is receiving
     *       treadmill data before it will pair and the bridge only notifies
     *       while the belt delivers fresh status — so a belt left off makes
     *       Kinomap fail with a generic "could not pair" that blames the
     *       bridge. Measured: that is exactly how a real pairing attempt failed.
     */
    Screen screen(const Inputs& in) const;

private:
    Step step_ = Step::Welcome;
    bool sim_ = false;
    /// Where BACK from the Arm screen goes: to the link state the user is
    /// actually in now, not the screen they arrived from.
    Step afterBack(const Inputs& in) const;
};

}  // namespace wizard
