// Minimal M5Unified touch probe: nothing of this project in it.
//
// The bridge's own firmware reports the board correctly detected
// (board_M5StackCoreS3), touch "enabled", the sampler running at ~47 Hz, and
// zero contacts ever. That leaves two possibilities - something in the
// bridge's firmware, or the hardware - and no amount of reading the bridge's
// code separates them.
//
// This does. It is M5.begin(), M5.update(), and a count, with no BLE, no
// Wi-Fi, no logging, no sprite, no custom partition table.
//
//   pio run -e cores3_touchtest -t upload --upload-port /dev/cu.usbmodem101
//   tools/console.py is not needed; just read the serial output.
//
// Touch the screen. If counts appear here but not in the bridge, the fault is
// in the bridge. If nothing appears here either, it is the hardware.

#include <M5Unified.h>

static unsigned maxSeen = 0;
static unsigned long samples = 0;

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);
    delay(500);
    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setTextSize(2);
    M5.Display.drawString("TOUCH ME", 60, 100);
    Serial.printf("\n[touchtest] board=%d touch_enabled=%d\n", (int)M5.getBoard(),
                  (int)M5.Touch.isEnabled());
}

void loop() {
    M5.update();
    ++samples;
    const unsigned n = (unsigned)M5.Touch.getCount();
    if (n > maxSeen) maxSeen = n;
    if (n > 0) {
        auto t = M5.Touch.getDetail();
        Serial.printf("[touchtest] CONTACT n=%u x=%d y=%d pressed=%d\n", n, (int)t.x,
                      (int)t.y, (int)t.isPressed());
        M5.Display.fillCircle(t.x, t.y, 10, TFT_GREEN);
    }
    static uint32_t last = 0;
    if (millis() - last > 2000) {
        last = millis();
        Serial.printf("[touchtest] hb n=%u max=%u samples=%lu\n", n, maxSeen, samples);
    }
    delay(5);
}
