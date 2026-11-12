#include "Esp32Hal.h"

#include <Arduino.h>
#if defined(CONTRAIL_BOARD_WS28)
#include <Wire.h>
#endif

namespace hal {
namespace {
constexpr int kBlChannel = 7, kLedChR = 4, kLedChG = 5, kLedChB = 6;
}

#if !defined(CONTRAIL_BOARD_WS28)
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

bool Esp32Display::read(int& x, int& y) {
    // The controller pulls IRQ low while touched; a full (bit-banged) read costs ~12 ms.
    if (digitalRead(board::kTouchIrq) == HIGH) return false;
    lgfx::touch_point_t tp;
    if (lcd_.getTouch(&tp, 1) == 0) return false;
    x = tp.x, y = tp.y;
    return true;
}
#else  // CONTRAIL_BOARD_WS28
CydLgfx::CydLgfx(const DisplayOptions& o) {
    auto b = bus_.config();
    b.spi_host = SPI2_HOST;
    b.spi_mode = 0;
    b.freq_write = 80000000;  // the S3 drives SPI2 from the IO_MUX pins at up to 80 MHz
    b.freq_read = 16000000;
    b.spi_3wire = false;
    b.use_lock = true;
    b.dma_channel = SPI_DMA_CH_AUTO;
    b.pin_sclk = board::kTftSclk;
    b.pin_mosi = board::kTftMosi;
    b.pin_miso = board::kTftMiso;
    b.pin_dc = board::kTftDc;
    bus_.config(b);

    st_.setBus(&bus_);
    auto p = st_.config();
    p.pin_cs = board::kTftCs;
    p.pin_rst = board::kTftRst;
    p.pin_busy = -1;
    p.panel_width = 240;
    p.panel_height = 320;
    p.offset_rotation = 0;
    p.readable = false;
    p.invert = o.invert;
    p.rgb_order = o.bgr;
    p.dlen_16bit = false;
    p.bus_shared = false;
    st_.config(p);

    setPanel(&st_);
    setRotation(o.rotation);
}

void Esp32Display::begin() {
    lcd_.init();
    lcd_.setColorDepth(16);
    lcd_.fillScreen(0);
    if (!touch_.begin()) Serial.println("[touch] no touch controller found");
}

bool Esp32Display::read(int& x, int& y) {
    if (!touch_.read(x, y)) return false;
    if (rotation_ == 2) x = 239 - x, y = 319 - y;
    return true;
}

namespace {
// Two touch controllers have shipped on this board: the CST328 (V1, at 0x1A, 16-bit register addresses) and, since
// June 2026, the CST3530 (V2, at 0x58, 32-bit commands). Both are found by address at boot.
constexpr uint8_t kCst328Addr = 0x1A, kCst3530Addr = 0x58;
constexpr uint32_t kPollMs = 10;  // both report at ~100 Hz; reading faster only repeats the last report

bool i2cWrite(uint8_t addr, const uint8_t* bytes, size_t n) {
    Wire.beginTransmission(addr);
    Wire.write(bytes, n);
    return Wire.endTransmission() == 0;
}

bool i2cRead(uint8_t addr, const uint8_t* cmd, size_t cmdLen, uint8_t* out, size_t n) {
    Wire.beginTransmission(addr);
    Wire.write(cmd, cmdLen);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(addr, uint8_t(n)) != n) return false;
    for (size_t i = 0; i < n; ++i) out[i] = uint8_t(Wire.read());
    return true;
}

bool present(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

// CST3530 (Waveshare's esp_lcd_touch_cst3530 component): read 32 bytes after D0 07 00 00; byte 2 is 0xFF when a
// report is ready, byte 3 holds the key and finger counts, bytes 0..1 a checksum (0x55 plus the point bytes), and
// each point is 5 bytes from byte 4 (after any keys): x low, y low, pressure, x/y high nibbles, event << 4 | id.
// D0 00 02 AB hands the buffer back.
bool read3530(int& x, int& y) {
    static const uint8_t kRead[4] = {0xD0, 0x07, 0x00, 0x00}, kClear[4] = {0xD0, 0x00, 0x02, 0xAB};
    uint8_t b[32];
    const bool ok = i2cRead(kCst3530Addr, kRead, 4, b, sizeof b);
    i2cWrite(kCst3530Addr, kClear, 4);
    if (!ok || b[2] != 0xFF) return false;
    const int fingers = b[3] & 0x0F, keys = b[3] >> 4;
    if (fingers == 0 || fingers > 5 || 4 + (keys + fingers) * 5 > int(sizeof b)) return false;
    uint16_t sum = 0x55;
    for (int i = 4; i < 4 + (keys + fingers) * 5; ++i) sum += b[i];
    if (sum != uint16_t(b[0] | (b[1] << 8))) return false;
    const uint8_t* p = b + 4 + keys * 5;  // the first finger
    if ((p[4] >> 4) == 0) return false;  // event 0: lifted
    x = p[0] | ((p[3] & 0x0F) << 8);
    y = p[1] | ((p[3] & 0xF0) << 4);
    return true;
}

// CST328 (ESPHome's cst328 driver): read 7 bytes at 0xD000; byte 5's low nibble is the finger count, byte 0's
// low nibble point 1's state (0x06 pressed); x = b1 << 4 | b3 >> 4, y = b2 << 4 | (b3 & 15). Then 0 to 0xD005 and
// 0xAB to 0xD000 hand the buffer back.
bool read328(int& x, int& y) {
    static const uint8_t kInfo[2] = {0xD0, 0x00}, kZeroCount[3] = {0xD0, 0x05, 0x00}, kSync[3] = {0xD0, 0x00, 0xAB};
    uint8_t d[7];
    const bool ok = i2cRead(kCst328Addr, kInfo, 2, d, sizeof d);
    i2cWrite(kCst328Addr, kZeroCount, 3);
    i2cWrite(kCst328Addr, kSync, 3);
    const int fingers = d[5] & 0x0F;
    if (!ok || fingers == 0 || fingers > 5 || (d[0] & 0x0F) != 0x06) return false;
    x = (d[1] << 4) | (d[3] >> 4);
    y = (d[2] << 4) | (d[3] & 0x0F);
    return true;
}
}  // namespace

bool Ws28Touch::begin() {
    pinMode(board::kTouchInt, INPUT_PULLUP);
    pinMode(board::kTouchRst, OUTPUT);
    digitalWrite(board::kTouchRst, LOW);
    delay(10);
    digitalWrite(board::kTouchRst, HIGH);
    delay(300);  // the controller boots its firmware after reset
    Wire.begin(board::kTouchSda, board::kTouchScl, 400000);
    if (present(kCst3530Addr)) {
        chip_ = Chip::Cst3530;
        // Its info block (D0 03 00 00): bytes 2..3 read CA CA, then the resolution at 28..31.
        static const uint8_t kInfo[4] = {0xD0, 0x03, 0x00, 0x00};
        uint8_t b[50] = {};
        if (i2cRead(kCst3530Addr, kInfo, 4, b, sizeof b) && b[2] == 0xCA && b[3] == 0xCA)
            Serial.printf("[touch] CST3530, %dx%d\n", b[28] | (b[29] << 8), b[30] | (b[31] << 8));
        else Serial.println("[touch] CST3530 (no info block)");
    } else if (present(kCst328Addr)) {
        chip_ = Chip::Cst328;
        static const uint8_t kNormal[2] = {0xD1, 0x09};
        i2cWrite(kCst328Addr, kNormal, 2);
        Serial.println("[touch] CST328");
    }
    return chip_ != Chip::None;
}

bool Ws28Touch::read(int& x, int& y) {
    if (chip_ == Chip::None) return false;
    const uint32_t now = millis();
    if (now - lastPollMs_ < kPollMs) {
        x = lastX_, y = lastY_;
        return down_;
    }
    lastPollMs_ = now;
    int rx = 0, ry = 0;
    down_ = chip_ == Chip::Cst3530 ? read3530(rx, ry) : read328(rx, ry);
    if (!down_) return false;
    lastX_ = std::min(239, std::max(0, rx));
    lastY_ = std::min(319, std::max(0, ry));
    x = lastX_, y = lastY_;
    return true;
}
#endif  // CONTRAIL_BOARD_WS28

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

void Esp32Backlight::begin() {
    ledcSetup(kBlChannel, 5000, 8);
    ledcAttachPin(board::kTftBl, kBlChannel);
    set(255);
}

void Esp32Backlight::set(uint8_t level) { ledcWrite(kBlChannel, level); }

void Esp32Led::begin() {
    if (!board::kHasLed) return;
#if !defined(CONTRAIL_BOARD_WS28)
    const int pins[] = {board::kLedR, board::kLedG, board::kLedB};
    const int chans[] = {kLedChR, kLedChG, kLedChB};
    for (int i = 0; i < 3; ++i) {
        ledcSetup(chans[i], 5000, 8);
        ledcAttachPin(pins[i], chans[i]);
    }
    set(0, 0, 0);
#endif
}

void Esp32Led::set(uint8_t r, uint8_t g, uint8_t b) {  // active low
#if !defined(CONTRAIL_BOARD_WS28)
    ledcWrite(kLedChR, 255 - r);
    ledcWrite(kLedChG, 255 - g);
    ledcWrite(kLedChB, 255 - b);
#else
    (void)r, (void)g, (void)b;
#endif
}

#if defined(CONTRAIL_BOARD_WS28)
void Esp32Light::begin() {}
int Esp32Light::raw() { return -1; }
int Esp32Light::read() { return 800; }  // no sensor: "auto" brightness settles at ~206 of 255
#else
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
#endif

}  // namespace hal
