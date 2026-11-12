#pragma once
// Waveshare ESP32-S3-Touch-LCD-2.8 pin map, from Waveshare's wiki and CircuitPython's board definition
// (ports/espressif/boards/waveshare_esp32_s3_touch_lcd_2_8/pins.c). ESP32-S3, 16 MB flash, 8 MB octal PSRAM.

namespace board {

// Display: ST7789, 240x320, on SPI2 (FSPI). Its MISO is not wired to anything we need: write only.
constexpr int kTftSclk = 40, kTftMosi = 45, kTftMiso = -1, kTftCs = 42, kTftDc = 41, kTftRst = 39, kTftBl = 5;
// Touch: CST3530 (V2 boards, from June 2026) or CST328 (V1) on its own I2C bus; found by address at boot.
constexpr int kTouchSda = 1, kTouchScl = 3, kTouchInt = 4, kTouchRst = 2;

// microSD, wired for SDMMC but used in SPI mode (D3 = CS) on SPI3, so the CYD's SdSpi driver runs unchanged.
constexpr int kSdSck = 14, kSdMosi = 17, kSdMiso = 16, kSdCs = 21;
// No RGB LED and no light sensor on this board.
constexpr bool kHasLed = false, kHasLightSensor = false;

// Settings' "auto" values. The panel is fixed on this board: ST7789 with inversion on (CircuitPython sends INVON).
enum class Panel { ILI9341, ST7789 };
constexpr Panel kDefaultPanel = Panel::ST7789;
constexpr bool kDefaultInvert = true;

}  // namespace board
