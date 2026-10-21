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
    if (t.y >= HIT_Y) {
        if (wizardMode_) (left ? wizL_ : wizR_) = true;
        else (left ? armPressed_ : stopPressed_) = true;
    } else if (!wizardMode_ && t.y < SETUP_HIT_Y && left) {
        setupTapped_ = true;
    } else if (!wizardMode_ && t.y >= SPD_BTN_Y0 && t.y < SPD_BTN_Y1) {
        if (t.x < SPD_BTN_W) speedDown_ = true;
        else if (t.x >= W - SPD_BTN_W) speedUp_ = true;
    }
}

bool Ui::tookArmButton() { bool r = armPressed_; armPressed_ = false; return r; }
bool Ui::tookStop() { bool r = stopPressed_; stopPressed_ = false; return r; }
bool Ui::tookSpeedUp() { bool r = speedUp_; speedUp_ = false; return r; }
bool Ui::tookSpeedDown() { bool r = speedDown_; speedDown_ = false; return r; }
bool Ui::tookSetupTap() { bool r = setupTapped_; setupTapped_ = false; return r; }
bool Ui::tookWizardLeft() { bool r = wizL_; wizL_ = false; return r; }
bool Ui::tookWizardRight() { bool r = wizR_; wizR_ = false; return r; }

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

void Ui::render(uint16_t centiKmh, uint32_t distance10m, uint32_t elapsedSec,
                bool beltUp, bool phoneUp, bool armed, bool running) {
    lgfx::LovyanGFX& g = target();
    g.fillScreen(BG);

    // A simulated belt must never look like a healthy real one: amber "SIM".
    if (sim_) dot(g, HALF / 2, "SIM BELT", true, true);
    else dot(g, HALF / 2, "BELT", beltUp);
    dot(g, HALF + HALF / 2, "PHONE", phoneUp);

    char buf[24];
    snprintf(buf, sizeof buf, "%u.%u", centiKmh / 100, (centiKmh % 100) / 10);
    g.setTextColor(TFT_WHITE, BG);
    g.setTextDatum(middle_center);
    g.setFont(&fonts::Font7);  // 7-segment, 48px tall
    g.setTextSize(2.3f);
    g.drawString(buf, HALF, 90);
    g.setTextSize(1);
    g.setFont(&fonts::FreeSansBold12pt7b);
    g.setTextColor(DIM, BG);
    g.setTextDatum(bottom_right);
    g.drawString("km/h", W - 6, 152);

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
    // saturated red. The left half is one control with three faces:
    //   disarmed            -> ARM   (dark grey)
    //   armed, can start    -> START (bright green: this is the call to action)
    //   armed otherwise     -> ARMED (dark green; tap disarms)
    const bool canStart = armed && !running && beltUp;
    const char* label = !armed ? "ARM" : (canStart ? "START" : "ARMED");
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
