#include "ui.h"

#include <M5Unified.h>
#include <stdio.h>

#include "shot.h"

namespace {
constexpr int W = 320, H = 240;
constexpr int BAR_Y = 184;  // bottom bar top edge
constexpr int HIT_Y = 180;  // just above the bar, clear of the distance/time row (<= y176)
constexpr int HALF = W / 2;
constexpr int SPD_BTN_W = 72;     // "-" at x 0..71, "+" at x 248..319
constexpr int SPD_BTN_Y0 = 44, SPD_BTN_Y1 = 140;
constexpr int SETUP_HIT_Y = 40;  // top strip: tap the BELT indicator to re-run setup
constexpr int DEV_HIT_Y = 40;    // top strip, right half: tap PHONE to open the dev menu

// Developer menu list: five rows between the title and the bottom bar.
constexpr int ROW_Y0 = 30, ROW_H = 30, ROW_N = 5;

// On-screen keyboard: ten columns of 32 px is exactly the 320 px panel, so the
// grid needs no centring and every key is the same width.
constexpr int KB_Y0 = 50, KB_ROW_H = 32, KB_COLS = 10, KB_COL_W = W / KB_COLS;
constexpr int KB_ROWS = 4;

// Row 2 is nine letters then backspace; row 3 is shift, eight letters, space.
// Keeping the letters in one string per row means the hit test and the drawing
// read from the same source and cannot disagree.
const char* const KB_LOWER[KB_ROWS] = {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm."};
const char* const KB_UPPER[KB_ROWS] = {"!@#$%^&*()", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM_"};

constexpr uint16_t BG = TFT_BLACK;
constexpr uint16_t DIM = 0x7BEF;       // mid grey
constexpr uint16_t ARM_ON = 0x03A0;    // dark green: armed
constexpr uint16_t ARM_GO = 0x05E0;    // bright green: START is available
constexpr uint16_t ARM_OFF = 0x2104;   // dark grey
constexpr uint16_t BTN_GREY = 0x4208;

M5Canvas canvas(&M5.Display);
bool canvasOk = false;

lgfx::LovyanGFX& target() {
    return canvasOk ? static_cast<lgfx::LovyanGFX&>(canvas)
                    : static_cast<lgfx::LovyanGFX&>(M5.Display);
}

void dot(lgfx::LovyanGFX& g, int cx, const char* label, bool up, bool sim = false) {
    g.fillCircle(cx - 40, 14, 9, sim ? TFT_ORANGE : (up ? TFT_GREEN : TFT_RED));
    g.setTextColor(TFT_WHITE, BG);
    g.setTextDatum(middle_left);
    g.setFont(&fonts::FreeSansBold12pt7b);
    g.drawString(label, cx - 24, 14);
}

// One half of the bottom bar. Falls back to a smaller font if the label is wide.
void barButton(lgfx::LovyanGFX& g, int x, uint16_t bg, const char* label) {
    const int bh = H - BAR_Y;
    g.fillRect(x, BAR_Y, HALF, bh, bg);
    g.setTextDatum(middle_center);
    g.setTextColor(TFT_WHITE, bg);
    g.setFont(&fonts::FreeSansBold18pt7b);
    if (g.textWidth(label) > HALF - 8) g.setFont(&fonts::FreeSansBold12pt7b);
    g.drawString(label, x + HALF / 2, BAR_Y + bh / 2);
}
void barDivider(lgfx::LovyanGFX& g) {
    const int bh = H - BAR_Y;
    g.drawFastVLine(HALF - 1, BAR_Y, bh, BG);
    g.drawFastVLine(HALF, BAR_Y, bh, BG);
}
}  // namespace

void Ui::begin() {
    M5.Display.setRotation(1);
    M5.Display.fillScreen(BG);
    canvas.setColorDepth(16);
    canvasOk = canvas.createSprite(W, H) != nullptr;
    if (!canvasOk) Serial.println("ui: no sprite memory, drawing direct");
}

void Ui::poll() {
    auto t = M5.Touch.getDetail();
    // wasPressed() is true only on the update that saw the finger land, so a
    // held finger yields exactly one event.
    if (!t.wasPressed()) return;
    const bool left = t.x < HALF;

    // The developer menu owns the whole screen while it is open, so it is
    // tested first: otherwise a row tap would also read as a speed button.
    if (menuMode_) {
        if (t.y >= HIT_Y) {
            (left ? wizL_ : wizR_) = true;  // the bottom bar is shared with the wizard
        } else if (menuKeyboard_) {
            pollKeyboard(t.x, t.y);
        } else if (t.y >= ROW_Y0 && t.y < ROW_Y0 + ROW_H * ROW_N) {
            menuRow_ = (t.y - ROW_Y0) / ROW_H;
        }
        return;
    }

    if (t.y >= HIT_Y) {
        if (wizardMode_) (left ? wizL_ : wizR_) = true;
        else (left ? armPressed_ : stopPressed_) = true;
    } else if (!wizardMode_ && t.y < SETUP_HIT_Y) {
        // Symmetric hidden taps on the two link indicators: BELT re-runs setup,
        // PHONE opens the developer menu.
        (left ? setupTapped_ : devTapped_) = true;
    } else if (!wizardMode_ && t.y >= SPD_BTN_Y0 && t.y < SPD_BTN_Y1) {
        if (t.x < SPD_BTN_W) speedDown_ = true;
        else if (t.x >= W - SPD_BTN_W) speedUp_ = true;
    }
}

// Maps a touch in the key grid to a character or an editing action. Mirrors
// the layout drawn by renderDevMenu(); both read KB_LOWER / KB_UPPER.
void Ui::pollKeyboard(int x, int y) {
    if (y < KB_Y0) return;
    const int row = (y - KB_Y0) / KB_ROW_H;
    const int col = x / KB_COL_W;
    if (row < 0 || row >= KB_ROWS || col < 0 || col >= KB_COLS) return;
    const char* const* map = menuShift_ ? KB_UPPER : KB_LOWER;

    switch (row) {
        case 0:
        case 1:
            keyChar_ = map[row][col];
            break;
        case 2:
            if (col == KB_COLS - 1) keyBack_ = true;
            else keyChar_ = map[2][col];
            break;
        case 3:
            if (col == 0) keyShift_ = true;
            else if (col == KB_COLS - 1) keyChar_ = ' ';
            else keyChar_ = map[3][col - 1];
            break;
        default:
            break;
    }
}

bool Ui::tookArmButton() { bool r = armPressed_; armPressed_ = false; return r; }
bool Ui::tookStop() { bool r = stopPressed_; stopPressed_ = false; return r; }
bool Ui::tookSpeedUp() { bool r = speedUp_; speedUp_ = false; return r; }
bool Ui::tookSpeedDown() { bool r = speedDown_; speedDown_ = false; return r; }
bool Ui::tookSetupTap() { bool r = setupTapped_; setupTapped_ = false; return r; }
bool Ui::tookWizardLeft() { bool r = wizL_; wizL_ = false; return r; }
bool Ui::tookWizardRight() { bool r = wizR_; wizR_ = false; return r; }
int Ui::tookMenuRow() { int r = menuRow_; menuRow_ = -1; return r; }
bool Ui::tookDevTap() { bool r = devTapped_; devTapped_ = false; return r; }
char Ui::tookKeyChar() { char r = keyChar_; keyChar_ = 0; return r; }
bool Ui::tookKeyShift() { bool r = keyShift_; keyShift_ = false; return r; }
bool Ui::tookKeyBackspace() { bool r = keyBack_; keyBack_ = false; return r; }

// Pushes the finished frame, and (screenshot builds) emits it once per phase.
void Ui::finishFrame(int phase) {
    if (!canvasOk) return;
    canvas.pushSprite(0, 0);
    if (UI_SCREENSHOT && phase != shotPhase_) {
        shotPhase_ = phase;
        shot::emit(static_cast<const uint8_t*>(canvas.getBuffer()), W, H);
    }
}

bool Ui::shotNow() {
    if (!UI_SCREENSHOT || !canvasOk) return false;
    shot::emit(static_cast<const uint8_t*>(canvas.getBuffer()), W, H);
    return true;
}

void Ui::render(uint16_t centiKmh, uint16_t commandedCentiKmh, uint32_t distance10m,
                uint32_t elapsedSec, bool beltUp, bool phoneUp, bool armed,
                bool running) {
    lgfx::LovyanGFX& g = target();
    g.fillScreen(BG);

    // A simulated belt must never look like a healthy real one: amber "SIM".
    if (sim_) dot(g, HALF / 2, "SIM BELT", true, true);
    else dot(g, HALF / 2, "BELT", beltUp);
    dot(g, HALF + HALF / 2, "PHONE", phoneUp);

    // The button below now names its action, so the armed state is shown here
    // instead of being inferred from a button label.
    if (armed) {
        g.setFont(&fonts::FreeSans9pt7b);
        g.setTextColor(ARM_GO, BG);
        g.setTextDatum(top_right);
        g.drawString("ARMED", W - 4, 30);
    }

    char buf[24];
    snprintf(buf, sizeof buf, "%u.%u", centiKmh / 100, (centiKmh % 100) / 10);
    g.setTextColor(TFT_WHITE, BG);
    g.setTextDatum(middle_center);
    g.setFont(&fonts::Font7);  // 7-segment, 48px tall
    // The -/+ buttons are drawn after this and would paint over anything that
    // strays under them, so the readout has to fit the gap between them
    // (W - 2 * SPD_BTN_W = 176 px) rather than merely look centred.
    g.setTextSize(2.0f);
    g.drawString(buf, HALF, 90);
    g.setTextSize(1);
    g.setFont(&fonts::FreeSansBold12pt7b);
    g.setTextColor(DIM, BG);
    // Clear of the + button (x >= W - SPD_BTN_W), which otherwise erases half
    // the unit label.
    g.setTextDatum(bottom_right);
    g.drawString("km/h", W - SPD_BTN_W - 6, 150);

    // While a commanded speed has not been reached yet, show it. Without this
    // a +/- press changes nothing on screen for a poll or two and reads as a
    // dead button. Drawn dim so it never competes with the actual speed.
    if (commandedCentiKmh != 0 && commandedCentiKmh != centiKmh) {
        char tbuf[20];
        snprintf(tbuf, sizeof tbuf, "-> %u.%u", commandedCentiKmh / 100,
                 (commandedCentiKmh % 100) / 10);
        g.setFont(&fonts::FreeSansBold12pt7b);
        g.setTextColor(ARM_GO, BG);
        g.setTextDatum(bottom_left);
        g.drawString(tbuf, SPD_BTN_W + 6, 150);
    }

    // Speed -/+ : dim and unmistakably inert while disarmed, like START.
    {
        const uint16_t bg = armed ? BTN_GREY : ARM_OFF;
        const uint16_t fg = armed ? TFT_WHITE : DIM;
        const int by = SPD_BTN_Y0, bh = SPD_BTN_Y1 - SPD_BTN_Y0, cy = by + bh / 2;
        g.fillRoundRect(2, by, SPD_BTN_W - 4, bh, 8, bg);
        g.fillRoundRect(W - SPD_BTN_W + 2, by, SPD_BTN_W - 4, bh, 8, bg);
        g.fillRect(14, cy - 3, SPD_BTN_W - 28, 6, fg);                   // minus
        g.fillRect(W - SPD_BTN_W + 14, cy - 3, SPD_BTN_W - 28, 6, fg);   // plus: bar
        g.fillRect(W - SPD_BTN_W / 2 - 3, cy - (SPD_BTN_W - 28) / 2, 6, SPD_BTN_W - 28, fg);  // plus: stem
        g.setTextColor(TFT_WHITE, BG);
    }

    g.setTextColor(TFT_WHITE, BG);
    g.setTextDatum(middle_center);
    snprintf(buf, sizeof buf, "%lu.%02lu km", (unsigned long)(distance10m / 100),
             (unsigned long)(distance10m % 100));
    g.drawString(buf, HALF / 2, 164);
    if (elapsedSec >= 3600)
        snprintf(buf, sizeof buf, "%lu:%02lu:%02lu", (unsigned long)(elapsedSec / 3600),
                 (unsigned long)(elapsedSec / 60 % 60), (unsigned long)(elapsedSec % 60));
    else
        snprintf(buf, sizeof buf, "%02lu:%02lu", (unsigned long)(elapsedSec / 60),
                 (unsigned long)(elapsedSec % 60));
    g.drawString(buf, HALF + HALF / 2, 164);

    // Bottom bar: two halves flush with the screen edges. STOP is always
    // saturated red. The left half is one control with three faces, and each
    // label names what tapping DOES:
    //   disarmed            -> ARM    (dark grey)
    //   armed, can start    -> START  (bright green: this is the call to action)
    //   armed otherwise     -> DISARM (dark green)
    // It used to read ARMED in the third case, which named the state rather
    // than the action, leaving no clue that tapping is how you undo it. The
    // state is still carried by the colour, and now also by the top bar.
    const bool canStart = armed && !running && beltUp;
    const char* label = !armed ? "ARM" : (canStart ? "START" : "DISARM");
    const uint16_t armBg = !armed ? ARM_OFF : (canStart ? ARM_GO : ARM_ON);
    barButton(g, 0, armBg, label);
    barButton(g, HALF, TFT_RED, "STOP");
    barDivider(g);

    finishFrame(100 + (armed ? 1 : 0) + (canStart ? 2 : 0) + (centiKmh > 0 ? 4 : 0) + (sim_ ? 8 : 0));
}

void Ui::renderWizard(const wizard::Screen& s, bool sim) {
    lgfx::LovyanGFX& g = target();
    g.fillScreen(BG);

    g.setTextDatum(middle_left);
    g.setFont(&fonts::FreeSansBold12pt7b);
    g.setTextColor(TFT_WHITE, BG);
    g.drawString(s.title, 8, 14);

    // Progress ("STEP 2 OF 5") and, when it applies, the amber SIM BELT tag.
    char buf[24];
    snprintf(buf, sizeof buf, "STEP %u OF %u", s.stepNo, s.stepTotal);
    g.setFont(&fonts::FreeSans9pt7b);
    g.setTextColor(DIM, BG);
    g.drawString(buf, 8, 38);
    if (sim) {
        g.fillCircle(HALF + 28, 38, 7, TFT_ORANGE);
        g.setTextColor(TFT_ORANGE, BG);
        g.drawString("SIM BELT", HALF + 42, 38);
    }

    // What the user has to do next: the largest, whitest text on the screen.
    g.setFont(&fonts::FreeSansBold12pt7b);
    g.setTextColor(TFT_WHITE, BG);
    g.drawString(s.action[0], 8, 68);
    g.drawString(s.action[1], 8, 94);

    g.setFont(&fonts::FreeSans9pt7b);
    g.setTextColor(DIM, BG);
    for (int i = 0; i < 4; ++i)
        if (s.detail[i][0]) g.drawString(s.detail[i], 8, 121 + 17 * i);

    barButton(g, 0, BTN_GREY, s.left);
    barButton(g, HALF, ARM_GO, s.right);
    barDivider(g);

    finishFrame(s.phase);
}


namespace {
// One key of the on-screen keyboard. Drawn slightly inset so neighbouring keys
// read as separate targets rather than one strip.
void drawKey(lgfx::LovyanGFX& g, int col, int row, const char* glyph, bool accent) {
    const int x = col * KB_COL_W, y = KB_Y0 + row * KB_ROW_H;
    g.fillRoundRect(x + 1, y + 1, KB_COL_W - 2, KB_ROW_H - 2, 3,
                    accent ? BTN_GREY : ARM_OFF);
    g.setTextDatum(middle_center);
    g.setTextColor(TFT_WHITE);
    g.setFont(&fonts::FreeSansBold12pt7b);
    if (g.textWidth(glyph) > KB_COL_W - 6) g.setFont(&fonts::FreeSans9pt7b);
    g.drawString(glyph, x + KB_COL_W / 2, y + KB_ROW_H / 2);
}
}  // namespace

void Ui::renderDevMenu(const devmenu::Screen& s) {
    // Remember what was drawn, so poll() hit-tests the screen actually on the
    // glass rather than a flag the caller has to keep in step.
    menuKeyboard_ = s.keyboard;
    menuShift_ = s.shift;

    lgfx::LovyanGFX& g = target();
    g.fillScreen(BG);

    g.setTextDatum(middle_left);
    g.setTextColor(TFT_WHITE, BG);
    g.setFont(&fonts::FreeSansBold12pt7b);
    g.drawString(s.title, 8, 15);
    g.drawFastHLine(0, 27, W, DIM);

    if (s.keyboard) {
        // What is being joined, and what has been typed. The password is shown
        // rather than masked: this is a 320x240 panel in the user's own home,
        // and typing a credential blind on a touch grid is how you end up with
        // a wrong one stored and no way to tell.
        g.setFont(&fonts::FreeSans9pt7b);
        g.setTextColor(DIM, BG);
        g.drawString(s.prompt, 8, 38);
        g.setTextColor(TFT_WHITE, BG);
        g.setFont(&fonts::FreeSansBold12pt7b);
        const char* shown = s.typed[0] ? s.typed : "_";
        g.drawString(shown, 8, 40 + 0);  // baseline tuned against the key grid

        const char* const* map = s.shift ? KB_UPPER : KB_LOWER;
        char one[2] = {0, 0};
        for (int col = 0; col < KB_COLS; ++col) {
            one[0] = map[0][col];
            drawKey(g, col, 0, one, false);
            one[0] = map[1][col];
            drawKey(g, col, 1, one, false);
        }
        for (int col = 0; col < KB_COLS - 1; ++col) {
            one[0] = map[2][col];
            drawKey(g, col, 2, one, false);
        }
        drawKey(g, KB_COLS - 1, 2, "DEL", true);
        drawKey(g, 0, 3, s.shift ? "SHF" : "shf", true);
        for (int col = 1; col < KB_COLS - 1; ++col) {
            one[0] = map[3][col - 1];
            drawKey(g, col, 3, one, false);
        }
        drawKey(g, KB_COLS - 1, 3, "SP", true);
    } else {
        for (int i = 0; i < s.rowCount && i < ROW_N; ++i) {
            const int y = ROW_Y0 + i * ROW_H;
            const bool on = s.rows[i].enabled;
            g.setFont(&fonts::FreeSans9pt7b);
            g.setTextDatum(middle_left);
            g.setTextColor(on ? TFT_WHITE : DIM, BG);
            g.drawString(s.rows[i].label, 8, y + ROW_H / 2);
            if (s.rows[i].value[0]) {
                g.setTextDatum(middle_right);
                g.setTextColor(on ? ARM_GO : DIM, BG);
                g.drawString(s.rows[i].value, W - 8, y + ROW_H / 2);
            }
            g.drawFastHLine(0, y + ROW_H - 1, W, ARM_OFF);
        }
    }

    barButton(g, 0, BTN_GREY, s.left);
    if (s.right[0]) barButton(g, HALF, ARM_GO, s.right);
    else g.fillRect(HALF, BAR_Y, HALF, H - BAR_Y, ARM_OFF);
    barDivider(g);
    finishFrame(s.phase);
}
