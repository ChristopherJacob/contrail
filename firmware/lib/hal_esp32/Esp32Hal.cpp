#include "Esp32Hal.h"

#include <Arduino.h>

namespace hal {
namespace {
constexpr int kBlChannel = 7, kLedChR = 4, kLedChG = 5, kLedChB = 6;
}

CydLgfx::CydLgfx(const DisplayOptions& o) {
    auto b = bus_.config();
    b.spi_host = HSPI_HOST;
    b.spi_mode = 0;
    b.freq_write = 40000000;
    b.freq_read = 16000000;
    b.spi_3wire = false;
    b.use_lock = true;
    b.dma_channel = SPI_DMA_CH_AUTO;
    b.pin_sclk = board::kTftSclk;
    b.pin_mosi = board::kTftMosi;
    b.pin_miso = board::kTftMiso;
    b.pin_dc = board::kTftDc;
    bus_.config(b);

    lgfx::Panel_Device* panel = o.panel == board::Panel::ST7789 ? static_cast<lgfx::Panel_Device*>(&st_) : &ili_;
    panel->setBus(&bus_);
    auto p = panel->config();
    p.pin_cs = board::kTftCs;
    p.pin_rst = -1;
    p.pin_busy = -1;
    p.panel_width = 240;
    p.panel_height = 320;
    p.offset_rotation = 0;
    p.readable = true;
    p.invert = o.invert;
    p.rgb_order = o.bgr;
    p.dlen_16bit = false;
    p.bus_shared = false;
    panel->config(p);

    auto t = touch_.config();
    t.x_min = board::kTouchXMin;
    t.x_max = board::kTouchXMax;
    t.y_min = board::kTouchYMin;
    t.y_max = board::kTouchYMax;
    t.pin_int = board::kTouchIrq;
    t.bus_shared = false;
    t.offset_rotation = 0;
    t.spi_host = -1;  // software SPI: touch has its own pins
    t.freq = 1000000;
    t.pin_sclk = board::kTouchClk;
    t.pin_mosi = board::kTouchMosi;
    t.pin_miso = board::kTouchMiso;
    t.pin_cs = board::kTouchCs;
    touch_.config(t);
    panel->setTouch(&touch_);

    setPanel(panel);
    setRotation(o.rotation);
}

void Esp32Display::begin() {
    pinMode(board::kTouchIrq, INPUT);  // GPIO36 is input-only and has no pull-up; the board has one
    lcd_.init();
    lcd_.setColorDepth(16);
    lcd_.fillScreen(0);
}

void Esp32Display::pushRect(int x, int y, int w, int h, const uint16_t* px) {
    // The panel expects big-endian RGB565. The buffer is fully redrawn before reuse, so
    // swapping in place is safe and lets the transfer run as pure DMA.
    auto* p = const_cast<uint16_t*>(px);
    // Two pixels per word (the strip buffers are word-aligned), then an odd last one.
    const int n = w * h;
    auto* q = reinterpret_cast<uint32_t*>(p);
    for (int i = 0; i < n / 2; ++i) q[i] = ((q[i] & 0x00FF00FFu) << 8) | ((q[i] >> 8) & 0x00FF00FFu);
    if (n & 1) p[n - 1] = uint16_t((p[n - 1] << 8) | (p[n - 1] >> 8));
    lcd_.pushImageDMA(x, y, w, h, reinterpret_cast<const lgfx::swap565_t*>(p));
}

bool Esp32Display::read(int& x, int& y) {
    // The controller pulls IRQ low while touched; a full (bit-banged) read costs ~12 ms.
    if (digitalRead(board::kTouchIrq) == HIGH) return false;
    lgfx::touch_point_t tp;
    if (lcd_.getTouch(&tp, 1) == 0) return false;
    x = tp.x, y = tp.y;
    return true;
}

void Esp32Backlight::begin() {
    ledcSetup(kBlChannel, 5000, 8);
    ledcAttachPin(board::kTftBl, kBlChannel);
    set(255);
}

void Esp32Backlight::set(uint8_t level) { ledcWrite(kBlChannel, level); }

void Esp32Led::begin() {
    const int pins[] = {board::kLedR, board::kLedG, board::kLedB};
    const int chans[] = {kLedChR, kLedChG, kLedChB};
    for (int i = 0; i < 3; ++i) {
        ledcSetup(chans[i], 5000, 8);
        ledcAttachPin(pins[i], chans[i]);
    }
    set(0, 0, 0);
}

void Esp32Led::set(uint8_t r, uint8_t g, uint8_t b) {  // active low
    ledcWrite(kLedChR, 255 - r);
    ledcWrite(kLedChG, 255 - g);
    ledcWrite(kLedChB, 255 - b);
}

void Esp32Light::begin() {
    // The LDR divider only swings a few hundred millivolts; at the default 11 dB
    // attenuation it reads 0 in any room light.
    analogSetPinAttenuation(board::kLdr, ADC_0db);
}

int Esp32Light::raw() { return analogRead(board::kLdr); }

int Esp32Light::read() {
    // The LDR pulls the ADC low in bright light. Measured on a CYD2USB at 0 dB: 0 in room
    // light, ~45 under a finger, so the useful range is tiny; treat kDarkRaw and above
    // as full dark and invert so higher means brighter.
    constexpr int kDarkRaw = 150;
    return 1023 - std::min(raw(), kDarkRaw) * 1023 / kDarkRaw;
}

}  // namespace hal
