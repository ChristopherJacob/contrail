#pragma once
#include "Color.h"

namespace gfx {

// 4x4 Bayer matrix, values 0..15.
constexpr uint8_t kBayer4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

// Quantise an 8-bit channel to `bits` with ordered dithering.
inline uint8_t ditherChannel(uint8_t v, int bits, uint8_t threshold16) {
    int step = 1 << (8 - bits);               // 8 for 5-bit, 4 for 6-bit
    int q = v + ((threshold16 * step) >> 4);  // truncation then rounds up for frac(v/step) of the cells
    if (q > 255) q = 255;
    return uint8_t(q);
}

// Convert to RGB565 with Bayer dithering at screen position (x, y). Removes the visible
// banding that 32-level red/blue channels produce on smooth sky gradients.
inline uint16_t dither565(Rgb c, int x, int y) {
    uint8_t t = kBayer4[y & 3][x & 3];
    return rgb565(ditherChannel(c.r, 5, t), ditherChannel(c.g, 6, t), ditherChannel(c.b, 5, t));
}

}  // namespace gfx
