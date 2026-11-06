#pragma once
// ESP32-2432S028R ("Cheap Yellow Display") pin map, from the board schematic.

namespace board {

// Display, HSPI
constexpr int kTftSclk = 14, kTftMosi = 13, kTftMiso = 12, kTftCs = 15, kTftDc = 2, kTftBl = 21;
// Touch XPT2046 on its own pins (software SPI)
constexpr int kTouchClk = 25, kTouchMosi = 32, kTouchMiso = 39, kTouchCs = 33, kTouchIrq = 36;
// microSD, VSPI
constexpr int kSdSck = 18, kSdMosi = 23, kSdMiso = 19, kSdCs = 5;
// RGB LED, active low
constexpr int kLedR = 4, kLedG = 16, kLedB = 17;
// Light-dependent resistor (ADC1)
constexpr int kLdr = 34;

// Panel defaults per variant; all can be overridden in settings at runtime.
enum class Panel { ILI9341, ST7789 };
#if defined(CONTRAIL_BOARD_CYD2USB)
// Verified on a CYD2USB (TPM408-2.8 panel): ST7789, no inversion, RGB order.
constexpr Panel kDefaultPanel = Panel::ST7789;
constexpr bool kDefaultInvert = false;
// Its touch film runs right-to-left relative to the panel, so raw X is mirrored.
constexpr int kTouchXMin = 3900, kTouchXMax = 300;
#else
constexpr Panel kDefaultPanel = Panel::ILI9341;
constexpr bool kDefaultInvert = false;
constexpr int kTouchXMin = 300, kTouchXMax = 3900;
#endif
// XPT2046 raw range mapped onto the panel; a min above max mirrors that axis.
constexpr int kTouchYMin = 200, kTouchYMax = 3800;

}  // namespace board
