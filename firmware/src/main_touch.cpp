// Touch probe: draws a dot where the touch controller says the finger is, so a mirrored or
// swapped axis is obvious at a glance. Corner labels mark where the dot should land.
#include <Arduino.h>

#include "Esp32Hal.h"

namespace {

hal::Esp32Display* display = nullptr;

void drawLabels(hal::CydLgfx& lcd) {
    lcd.fillScreen(TFT_BLACK);
    lcd.setFont(&fonts::Font4);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setTextDatum(textdatum_t::top_left);
    lcd.drawString("TL", 6, 6);
    lcd.setTextDatum(textdatum_t::top_right);
    lcd.drawString("TR", 234, 6);
    lcd.setTextDatum(textdatum_t::bottom_left);
    lcd.drawString("BL", 6, 314);
    lcd.setTextDatum(textdatum_t::bottom_right);
    lcd.drawString("BR", 234, 314);
    lcd.setTextDatum(textdatum_t::middle_center);
    lcd.setFont(&fonts::Font2);
    lcd.drawString("dot follows your finger?", 120, 150);
    lcd.drawString("USB at the bottom", 120, 170);
}

}  // namespace

void setup() {
    Serial.begin(115200);
    pinMode(board::kTftBl, OUTPUT);
    digitalWrite(board::kTftBl, HIGH);
    display = new hal::Esp32Display(hal::DisplayOptions{});
    display->begin();
    drawLabels(display->lcd());
}

void loop() {
    static bool was = false;
    int x, y;
    bool touched = display->read(x, y);
    if (touched) {
        lgfx::touch_point_t raw;
        display->lcd().getTouchRaw(&raw, 1);
        display->lcd().fillCircle(x, y, 4, TFT_CYAN);
        Serial.printf("[touch] %d,%d raw %d,%d\n", x, y, raw.x, raw.y);
    } else if (was) {
        delay(800);
        drawLabels(display->lcd());
    }
    was = touched;
    delay(20);
}
