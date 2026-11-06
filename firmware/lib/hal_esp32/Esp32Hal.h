#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "board_cyd.h"
#include "hal.h"

namespace hal {

struct DisplayOptions {
    board::Panel panel = board::kDefaultPanel;
    bool invert = board::kDefaultInvert;
    bool bgr = false;
    uint8_t rotation = 0;  // 0 or 2 (portrait, flipped)
};

// LovyanGFX device for the CYD. Both panel drivers are instantiated so the choice can be
// made at runtime from settings (CYD revisions ship with either controller).
class CydLgfx : public lgfx::LGFX_Device {
   public:
    explicit CydLgfx(const DisplayOptions& o);

   private:
    lgfx::Bus_SPI bus_;
    lgfx::Panel_ILI9341 ili_;
    lgfx::Panel_ST7789 st_;
    lgfx::Touch_XPT2046 touch_;
};

class Esp32Display : public IDisplay, public ITouch {
   public:
    explicit Esp32Display(const DisplayOptions& o) : lcd_(o) {}
    void begin();
    int width() const override { return 240; }
    int height() const override { return 320; }
    void beginFrame() override { lcd_.startWrite(); }
    void pushRect(int x, int y, int w, int h, const uint16_t* px) override;
    void waitDma() override { lcd_.waitDMA(); }
    void endFrame() override { lcd_.endWrite(); }
    bool read(int& x, int& y) override;
    CydLgfx& lcd() { return lcd_; }

   private:
    CydLgfx lcd_;
};

class Esp32Backlight : public IBacklight {
   public:
    void begin();
    void set(uint8_t level) override;
};

class Esp32Led : public ILed {
   public:
    void begin();
    void set(uint8_t r, uint8_t g, uint8_t b) override;
};

class Esp32Light : public ILightSensor {
   public:
    void begin();
    int read() override;
    int raw();  // ADC counts, 0..4095, low = bright
};

}  // namespace hal
