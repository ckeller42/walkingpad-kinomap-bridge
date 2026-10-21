#pragma once
#include <stdint.h>

#include "devmenu.h"
#include "wizard.h"

// Main screen ("layout A") on the 320x240 CoreS3 panel:
//   y   0..27   link indicators (belt left half, phone right half)
//   y  28..149  instantaneous speed, very large
//   y  40..139  speed readout flanked by large "-" (x 0..71) and "+" (x 248..319)
//               touch targets; enabled only while armed (disarmed = dim)
//   y 150..175  distance (left) and elapsed time (right)
//   y 184..239  bottom bar: left = ARM / START / ARMED, right = STOP
// The left button is one control with three faces: ARM (disarmed), START
// (armed, belt stopped, belt linked), ARMED (armed and running; tap disarms).
//
// Wizard screens reuse the bottom bar as two buttons (left / right) under a
// title, an action (what to do next) and a few detail lines.
class Ui {
public:
    void begin();
    // Call once per loop, right after M5.update(): samples touch, latches presses.
    void poll();
    // centiKmh is what the belt reports; commandedCentiKmh is what was last
    // asked for. They differ for a second or two after a +/- press, because
    // the belt lags a command by a poll. Showing only the first makes the
    // buttons look dead and invites a second press that overshoots.
    // Pass commandedCentiKmh == centiKmh (or 0) when there is nothing pending.
    void render(uint16_t centiKmh, uint16_t commandedCentiKmh, uint32_t distance10m,
                uint32_t elapsedSec, bool beltUp, bool phoneUp, bool armed,
                bool running);  // armed also enables the +/- buttons
    // Emits the current frame now (console `shot`). False if not a screenshot build.
    bool shotNow();
    void renderWizard(const wizard::Screen& s, bool sim);

    // Developer menu. A list screen draws rows 0..rowCount-1 between the title
    // and the bottom bar; a keyboard screen draws the key grid instead. The
    // rendered screen decides which, so the caller never sets a second flag
    // that could disagree with what is on the glass.
    void renderDevMenu(const devmenu::Screen& s);
    void setMenuMode(bool on) { menuMode_ = on; }

    // Edge-triggered, like the other took*() accessors.
    int tookMenuRow();        // row index, or -1
    bool tookDevTap();        // main screen, tap on the PHONE indicator
    char tookKeyChar();       // typed character, or 0
    bool tookKeyShift();
    bool tookKeyBackspace();
    // Marks the belt indicator as "SIM BELT" (amber) while the simulated belt is bound.
    void setSim(bool sim) { sim_ = sim; }
    // Selects which screen family owns the touch zones.
    void setWizardMode(bool on) { wizardMode_ = on; }
    // Edge-triggered: true once per press, then cleared.
    bool tookArmButton();  // main screen, left button
    bool tookStop();
    bool tookSpeedUp();    // main screen, "+" beside the speed readout
    bool tookSpeedDown();  // main screen, "-"
    bool tookSetupTap();   // main screen, tap on the BELT indicator
    bool tookWizardLeft();
    bool tookWizardRight();

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
    int menuRow_ = -1;
    bool devTapped_ = false;
    char keyChar_ = 0;
    bool keyShift_ = false;
    bool keyBack_ = false;
    int shotPhase_ = -1;
};
