#include "ui.h"

#include <M5Unified.h>
#include <stdio.h>

namespace {
constexpr int W = 320, H = 240;
constexpr int BAR_Y = 184;  // bottom bar top edge
constexpr int HIT_Y = 168;  // touch zone starts above the bar (forgiving)
constexpr int HALF = W / 2;

constexpr uint16_t BG = TFT_BLACK;
constexpr uint16_t DIM = 0x7BEF;       // mid grey
constexpr uint16_t ARM_ON = 0x03A0;    // dark green
constexpr uint16_t ARM_OFF = 0x2104;   // dark grey

M5Canvas canvas(&M5.Display);
bool canvasOk = false;

void dot(lgfx::LovyanGFX& g, int cx, const char* label, bool up) {
    g.fillCircle(cx - 40, 14, 9, up ? TFT_GREEN : TFT_RED);
    g.setTextColor(TFT_WHITE, BG);
    g.setTextDatum(middle_left);
    g.setFont(&fonts::FreeSansBold12pt7b);
    g.drawString(label, cx - 24, 14);
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
    if (!t.wasPressed() || t.y < HIT_Y) return;
    if (t.x < HALF) armPressed_ = true;
    else stopPressed_ = true;
}

bool Ui::tookArmToggle() { bool r = armPressed_; armPressed_ = false; return r; }
bool Ui::tookStop() { bool r = stopPressed_; stopPressed_ = false; return r; }

void Ui::render(uint16_t centiKmh, uint32_t distance10m, uint32_t elapsedSec,
                bool beltUp, bool phoneUp, bool armed) {
    lgfx::LovyanGFX& g = canvasOk ? static_cast<lgfx::LovyanGFX&>(canvas)
                                  : static_cast<lgfx::LovyanGFX&>(M5.Display);
    g.fillScreen(BG);

    dot(g, HALF / 2, "BELT", beltUp);
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
    // saturated red; ARM is green when armed, dark grey when not.
    const int bh = H - BAR_Y;
    const uint16_t armBg = armed ? ARM_ON : ARM_OFF;
    g.fillRect(0, BAR_Y, HALF, bh, armBg);
    g.fillRect(HALF, BAR_Y, HALF, bh, TFT_RED);
    g.drawFastVLine(HALF - 1, BAR_Y, bh, BG);
    g.drawFastVLine(HALF, BAR_Y, bh, BG);
    g.setFont(&fonts::FreeSansBold18pt7b);
    g.setTextDatum(middle_center);
    g.setTextColor(TFT_WHITE, armBg);
    g.drawString(armed ? "ARMED" : "ARM", HALF / 2, BAR_Y + bh / 2);
    g.setTextColor(TFT_WHITE, TFT_RED);
    g.drawString("STOP", HALF + HALF / 2, BAR_Y + bh / 2);

    if (canvasOk) canvas.pushSprite(0, 0);
}
