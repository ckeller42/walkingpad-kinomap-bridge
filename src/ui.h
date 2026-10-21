#pragma once
#include <stddef.h>
#include <stdint.h>

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
 * | 0..27    | link indicators (BELT left, PHONE right) and the settings gear |
 * | 28..149  | instantaneous speed, very large |
 * | 44..139  | the readout flanked by `-` (x 0..71) and `+` (x 248..319), enabled only while armed |
 * | 150..175 | distance (left) and elapsed time (right) |
 * | 184..239 | bottom bar: left = ARM / START / DISARM, right = STOP |
 *
 * The left button is one control with three faces, and each label names what
 * tapping *does* rather than what the bridge *is*; the armed state is shown in
 * the top bar instead.
 *
 * The wizard and the developer menu reuse the bottom bar as two buttons. Which
 * screen family owns the touch zones is selected by setWizardMode() and
 * setMenuMode(); the menu is tested first, so a row tap cannot also read as a
 * speed button.
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
     * @param beltUp            Belt linked and delivering fresh status.
     * @param phoneUp           A phone is connected.
     * @param armed             The bridge is armed; also enables `+`/`-`.
     * @param running           The belt is moving.
     */
    void render(uint16_t centiKmh, uint16_t commandedCentiKmh, uint32_t distance10m,
                uint32_t elapsedSec, bool beltUp, bool phoneUp, bool armed,
                bool running);

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
     * @brief Mark the belt indicator as amber "SIM BELT".
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

    /** @brief The keyboard's 123 / ABC page toggle. @return True once per tap. */
    bool tookKeyNumeric();

    /** @brief The main screen's left button. @return True once per tap. */
    bool tookArmButton();

    /** @brief The STOP button. @return True once per tap. */
    bool tookStop();

    /** @brief The `+` beside the readout. @return True once per tap. */
    bool tookSpeedUp();

    /** @brief The `-` beside the readout. @return True once per tap. */
    bool tookSpeedDown();

    /** @brief The BELT indicator, which re-runs setup. @return True once per tap. */
    bool tookSetupTap();

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
    bool setupTapped_ = false;
    bool wizL_ = false;
    bool wizR_ = false;
    bool menuMode_ = false;
    bool menuKeyboard_ = false;  // set by renderDevMenu from the screen it drew
    bool menuShift_ = false;
    bool menuNumeric_ = false;
    int menuRow_ = -1;
    bool devTapped_ = false;
    char keyChar_ = 0;
    bool keyShift_ = false;
    bool keyBack_ = false;
    bool keyNum_ = false;
    // Highest contact count and sample total since boot. Reported by
    // describeTouch(): max == 0 with samples climbing means the panel is
    // being polled correctly and answering nothing, which is the signature
    // of the controller latching up rather than a bug in the zones.
    unsigned touchMax_ = 0;
    unsigned long touchSamples_ = 0;
    int shotPhase_ = -1;
};
