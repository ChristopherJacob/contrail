// Display probe: cycles through panel driver / inversion combinations so you can see which
// one your CYD needs. Each option shows a big number, an arrow that should point up (away
// from the USB connector) and red/green/blue/white bars that should read in that order.
#include <Arduino.h>

#include "Esp32Hal.h"

namespace {

struct Option {
    const char* name;
    board::Panel panel;
    bool invert;
};

const Option kOptions[] = {
    {"ILI9341 invert on", board::Panel::ILI9341, true},
    {"ILI9341 invert off", board::Panel::ILI9341, false},
    {"ST7789 invert on", board::Panel::ST7789, true},
    {"ST7789 invert off", board::Panel::ST7789, false},
};

void drawPattern(hal::CydLgfx& lcd, int n, const Option& o) {
    lcd.fillScreen(TFT_BLACK);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setTextDatum(textdatum_t::top_center);
    lcd.setFont(&fonts::Font4);
    lcd.drawString("UP", 120, 8);
    lcd.fillTriangle(120, 40, 100, 70, 140, 70, TFT_WHITE);
    lcd.setFont(&fonts::Font7);
    lcd.drawNumber(n, 120, 90);
    lcd.setFont(&fonts::Font2);
    lcd.drawString(o.name, 120, 150);
    // Keep these 16-bit: LovyanGFX reads uint32_t colours as RGB888.
    const uint16_t bars[] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_WHITE};
    const char* labels[] = {"R", "G", "B", "W"};
    for (int i = 0; i < 4; ++i) {
        lcd.fillRect(10 + i * 56, 190, 52, 90, bars[i]);
        lcd.setTextColor(TFT_BLACK, bars[i]);
        lcd.drawString(labels[i], 36 + i * 56, 228);
    }
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.drawString("bottom edge", 120, 300);
}

}  // namespace

void setup() {
    Serial.begin(115200);
    pinMode(board::kTftBl, OUTPUT);
    digitalWrite(board::kTftBl, HIGH);
}

void loop() {
    static int i = 0;
    const Option& o = kOptions[i];
    hal::DisplayOptions d;
    d.panel = o.panel;
    d.invert = o.invert;
    auto* lcd = new hal::CydLgfx(d);
    lcd->init();
    drawPattern(*lcd, i + 1, o);
    Serial.printf("[probe] %d: %s\n", i + 1, o.name);
    delay(8000);
    delete lcd;
    i = (i + 1) % 4;
}
