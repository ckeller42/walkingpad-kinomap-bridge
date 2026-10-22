#pragma once
#include <stddef.h>
#include <stdint.h>

#include "bridge_state.h"  // policy::LeftFace only; the UI decides nothing
#include "devmenu.h"
#include "wizard.h"

/**
 * @file ui.h
 * @brief The 320x240 touchscreen: drawing, and latching what was touched.
 *
 * Main screen layout:
 *
 * | y        | content |
 * |----------|---------|
 * | 0..24    | link indicators (BELT left, PHONE right), mode badge (centre) and the settings gear (x >= 276) |
 * | 25..27   | armed stripe, full width (green; cyan in Auto) |
 * | 28..43   | `ARMED`/`AUTO` tag (right); battery gauge (left, y 31..43) |
 * | 28..149  | instantaneous speed, very large |
 * | 44..139  | the readout flanked by `-` (x 0..71) and `+` (x 248..319), enabled only while armed and not in Auto |
 * | 150..175 | distance (left), steps (centre) and elapsed time (right); a refusal reason replaces the "-> commanded" text beside `km/h` |
 * | 184..239 | bottom bar: left = ARM / START / STARTING / DISARM, right = STOP; split at the middle, or at x 106 (STOP two thirds wide) while the belt moves or starts |
 *
 * The left button is one control with four faces (ARM, START, STARTING, DISARM; chosen by policy::leftButton()), and each label names what
 * tapping *does* rather than what the bridge *is*; the armed state is shown in
 * the top bar instead.
 *
 * The wizard and the developer menu reuse the bottom bar as two buttons. Which
 * screen family owns the touch zones is selected by setWizardMode() and
 * setMenuMode(); the menu is tested first, so a row tap cannot also read as a
 * speed button.
 */

/**
 * @brief What the top-bar mode badge shows.
 *
 * The UI only draws this; deciding it (and what a tap on it means) is the
 * caller's policy.
 */
enum class ModeBadge : uint8_t {
    None,         ///< Mode unknown or no belt: nothing is drawn.
    Manual,       ///< Belt is in manual mode (`M`, green).
    Auto,         ///< Belt is in auto mode (`A`, cyan).
    AutoPending,  ///< Auto requested but not yet confirmed by the belt (`A?`, amber).
};

/**
 * @brief The bridge's screen and its latched touch input.
 *
 * Owns the panel: draws the main screen, the setup wizard and the developer
 * menu, and records what was touched for `loop()` to act on. Holds no policy —
 * every press becomes a flag that main.cpp turns into a BridgeState call.
 */
class Ui {
public:
    /** @brief Set up the panel and the off-screen canvas. Call once. */
    void begin();

    /**
     * @brief Report what the touch panel says about itself.
     * @param out Buffer for one line of text.
     * @param cap Its capacity.
     */
    void describeTouch(char* out, size_t cap);

    /**
     * @brief Sample touch and latch any press.
     *
     * Call once per loop, right after `M5.update()`. A held finger yields
     * exactly one event.
     */
    void poll();

    /**
     * @brief Draw the main screen.
     *
     * @param centiKmh          What the belt reports, in 0.01 km/h units.
     * @param commandedCentiKmh What was last asked for. The two differ for a
     *        second or two after a `+`/`-` press, because the belt lags a
     *        command by a poll; showing only the first makes the buttons look
     *        dead and invites a second press that overshoots. Pass the same
     *        value as @p centiKmh (or 0) when nothing is pending.
     * @param distance10m       Session distance in 10 m units.
     * @param elapsedSec        Session time in seconds.
     * @param steps             Session step count, from the belt's status frame.
     * @param beltUp            Belt linked and delivering fresh status.
     * @param phoneUp           A phone is connected.
     * @param armed             The bridge is armed; enables `+`/`-` unless @p autoMode,
     *        and draws the full-width armed stripe.
     * @param running           The belt is moving.
     * @param battPct           Bridge battery charge 0..100, or < 0 if unknown / no cell.
     * @param charging          True while the bridge is on external power and charging.
     * @param badge             What the top-bar mode badge shows; drawn dim
     *        while disarmed.
     * @param autoMode          Auto is observed or pending. Mutes `+`/`-`,
     *        turns the `ARMED` tag into `AUTO` and removes the `START` face.
     * @param face              The left button's face from policy::leftButton();
     *        the label and colour are drawn from it and nothing else
     *        (`STARTING` is the inert armed colour).
     * @param reason            Short refusal text to show for about a second,
     *        or nullptr. Drawn in orange beside the readout in place of the
     *        "-> commanded" text.
     * @param moving            The belt is believed or observed moving. With
     *        @p face == Wait it widens STOP to two thirds of the bar (and
     *        narrows the left tap zone to match).
     */
    void render(uint16_t centiKmh, uint16_t commandedCentiKmh, uint32_t distance10m,
                uint32_t elapsedSec, uint32_t steps, bool beltUp, bool phoneUp,
                bool armed, bool running, int battPct, bool charging,
                ModeBadge badge, bool autoMode, policy::LeftFace face, const char* reason,
                bool moving);

    /**
     * @brief Emit the current frame over serial (the console's `shot`).
     * @return False when this is not a screenshot build.
     */
    bool shotNow();

    /**
     * @brief Draw one wizard screen.
     * @param s   The screen to draw.
     * @param sim True while a simulated belt is bound.
     */
    void renderWizard(const wizard::Screen& s, bool sim);

    /**
     * @brief Draw one developer-menu screen.
     *
     * A list screen draws its rows between the title and the bottom bar; a
     * keyboard screen draws the key grid instead. The screen itself decides
     * which, so the caller never sets a second flag that could disagree with
     * what is on the glass.
     *
     * @param s The screen to draw.
     */
    void renderDevMenu(const devmenu::Screen& s);

    /**
     * @brief Hand the touch zones to the developer menu, or take them back.
     * @param on True while the menu is open.
     */
    void setMenuMode(bool on) { menuMode_ = on; }

    /**
     * @brief Mark the belt indicator as amber "SIM".
     * @param sim True while a simulated belt is bound.
     * @note A simulated belt must never read as a healthy real one.
     */
    void setSim(bool sim) { sim_ = sim; }

    /**
     * @brief Hand the touch zones to the setup wizard, or take them back.
     * @param on True while the wizard is running.
     */
    void setWizardMode(bool on) { wizardMode_ = on; }

    /// @name Latched input
    /// Edge-triggered: each returns true (or a value) once per press, then
    /// clears, so a caller cannot process the same press twice.
    /// @{

    /** @brief A tapped developer-menu row. @return Row index, or -1. */
    int tookMenuRow();

    /** @brief The settings gear. @return True once per tap. */
    bool tookDevTap();

    /** @brief A keyboard character. @return The character, or 0. */
    char tookKeyChar();

    /** @brief The keyboard's shift key. @return True once per tap. */
    bool tookKeyShift();

    /** @brief The keyboard's DEL key. @return True once per tap. */
    bool tookKeyBackspace();

    /**
     * @brief The keyboard's page keys (ABC / 123 / #+=).
     * @return 0 when none was tapped, else the devmenu::Page index plus one.
     */
    int tookKeyPage();

    /** @brief The main screen's left button. @return True once per tap. */
    bool tookArmButton();

    /** @brief The STOP button. @return True once per tap. */
    bool tookStop();

    /** @brief The `+` beside the readout. @return True once per tap. */
    bool tookSpeedUp();

    /** @brief The `-` beside the readout. @return True once per tap. */
    bool tookSpeedDown();

    /** @brief The mode badge in the top bar. @return True once per tap. */
    bool tookModeTap();

    /** @brief The bottom bar's left button. @return True once per tap. */
    bool tookWizardLeft();

    /** @brief The bottom bar's right button. @return True once per tap. */
    bool tookWizardRight();

    /// @}

private:
    void finishFrame(int phase);
    void pollKeyboard(int x, int y);
    bool sim_ = false;
    bool wizardMode_ = false;
    bool armPressed_ = false;
    bool stopPressed_ = false;
    bool speedUp_ = false;
    bool speedDown_ = false;
    bool modeTapped_ = false;
    bool wizL_ = false;
    bool wizR_ = false;
    bool menuMode_ = false;
    bool menuKeyboard_ = false;  // set by renderDevMenu from the screen it drew
    bool menuShift_ = false;
    devmenu::Page menuPage_ = devmenu::Page::Letters;
    int menuRow_ = -1;
    bool devTapped_ = false;
    char keyChar_ = 0;
    bool keyShift_ = false;
    bool keyBack_ = false;
    int keyPage_ = 0;  // 0 = none, else page + 1
    // Highest contact count and sample total since boot. Reported by
    // describeTouch(): max == 0 with samples climbing means the panel is
    // being polled correctly and answering nothing, which is the signature
    // of the controller latching up rather than a bug in the zones.
    unsigned touchMax_ = 0;
    unsigned long touchSamples_ = 0;
    int shotPhase_ = -1;
    /// Bottom-bar split x: `W / 3` while the belt moves or a start is in flight (wide STOP),
    /// else `W / 2`. Set by render(), read by poll(), so the drawn button and
    /// its tap zone cannot disagree.
    int barSplit_ = 160;
};
