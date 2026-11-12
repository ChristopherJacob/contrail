#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "board.h"
#include "hal.h"

namespace hal {

struct DisplayOptions {
    board::Panel panel = board::kDefaultPanel;
    bool invert = board::kDefaultInvert;
    bool bgr = false;
    uint8_t rotation = 0;  // 0 or 2 (portrait, flipped)
};

#if defined(CONTRAIL_BOARD_WS28)
// LovyanGFX device for the Waveshare 2.8: one ST7789 on SPI2. Touch (CST328) is read by Ws28Touch, not LovyanGFX,
// which has no driver for it.
class CydLgfx : public lgfx::LGFX_Device {
   public:
    explicit CydLgfx(const DisplayOptions& o);

   private:
    lgfx::Bus_SPI bus_;
    lgfx::Panel_ST7789 st_;
};

// The board's capacitive touch over I2C (Wire, on its own pins): a CST3530 (V2 boards) or CST328 (V1), whichever
// answers at boot. Coordinates come in panel pixels, portrait.
class Ws28Touch {
   public:
    bool begin();
    bool read(int& x, int& y);  // true while a finger is down

   private:
    enum class Chip { None, Cst328, Cst3530 };
    Chip chip_ = Chip::None;
    bool down_ = false;
    int lastX_ = 0, lastY_ = 0;
    uint32_t lastPollMs_ = 0;
};
#else
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
#endif

class Esp32Display : public IDisplay, public ITouch {
   public:
    explicit Esp32Display(const DisplayOptions& o) : lcd_(o), rotation_(o.rotation) {}
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
    uint8_t rotation_;
#if defined(CONTRAIL_BOARD_WS28)
    Ws28Touch touch_;
#endif
};

class Esp32Backlight : public IBacklight {
   public:
    void begin();
    void set(uint8_t level) override;
};

// The CYD's RGB LED; does nothing on a board without one (board::kHasLed).
class Esp32Led : public ILed {
   public:
    void begin();
    void set(uint8_t r, uint8_t g, uint8_t b) override;
};

// The CYD's light-dependent resistor. On a board without one (board::kHasLightSensor) read() is a fixed, fairly
// bright level, so "auto" brightness holds steady.
class Esp32Light : public ILightSensor {
   public:
    void begin();
    int read() override;
    int raw();  // ADC counts, 0..4095, low = bright; -1 without a sensor
};

}  // namespace hal
