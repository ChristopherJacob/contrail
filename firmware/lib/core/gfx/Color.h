#pragma once
#include <cstdint>

namespace gfx {

struct Rgb {
    uint8_t r, g, b;
};

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return uint16_t(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
constexpr uint16_t rgb565(Rgb c) { return rgb565(c.r, c.g, c.b); }

inline Rgb unpack565(uint16_t c) {
    uint8_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
    return {uint8_t((r << 3) | (r >> 2)), uint8_t((g << 2) | (g >> 4)), uint8_t((b << 3) | (b >> 2))};
}

inline uint8_t lerp8(uint8_t a, uint8_t b, float t) {
    float v = a + (float(b) - float(a)) * t;
    return uint8_t(v < 0 ? 0 : v > 255 ? 255 : v + 0.5f);
}

inline Rgb lerp(Rgb a, Rgb b, float t) { return {lerp8(a.r, b.r, t), lerp8(a.g, b.g, t), lerp8(a.b, b.b, t)}; }

// Exact round(x / 255) for x in 0..65535, without a division.
inline uint32_t div255(uint32_t x) { return (x + 128 + ((x + 128) >> 8)) >> 8; }

// RGB565 spread over 32 bits as 00000GGGGGG00000RRRRR000000BBBBB, leaving room above
// each field for a 5-bit multiply, so all three channels blend in one integer operation.
inline uint32_t spread565(uint16_t c) { return (c | (uint32_t(c) << 16)) & 0x07E0F81Fu; }
inline uint16_t pack565(uint32_t s) { return uint16_t(s | (s >> 16)); }

// Blend src over the 565 dst pixel. alpha 0..255, applied in 33 steps.
// Always inlined: drawBadge (IRAM) otherwise gets a flash copy and calls it per blended pixel.
__attribute__((always_inline)) inline uint16_t blend565(uint16_t dst, uint16_t src, uint8_t alpha) {
    uint32_t a = (uint32_t(alpha) + 4) >> 3;
    if (a == 0) return dst;
    if (a >= 32) return src;
    uint32_t d = spread565(dst), s = spread565(src);
    return pack565((d + (((s - d) * a) >> 5)) & 0x07E0F81Fu);
}
inline uint16_t blend565(uint16_t dst, Rgb src, uint8_t alpha) { return blend565(dst, rgb565(src), alpha); }

// Per-channel saturating add of two spread565 values. Each field's carry lands in the
// free bits above it (B -> bit 5, R -> bit 16, G -> bit 27) and is widened to a full mask.
inline uint32_t addSat565(uint32_t a, uint32_t b) {
    uint32_t s = a + b;
    uint32_t cb = s & 0x20u, cr = s & 0x10000u, cg = s & 0x8000000u;
    uint32_t sat = (cb - (cb >> 5)) | (cr - (cr >> 5)) | (cg - (cg >> 6));
    return (s | sat) & 0x07E0F81Fu;
}

// Additive blend, used for glows so overlapping lights brighten instead of occlude.
inline uint16_t add565(uint16_t dst, Rgb src, uint8_t alpha) {
    Rgb d = unpack565(dst);
    auto ch = [alpha](uint8_t dc, uint8_t sc) {
        uint32_t v = dc + div255(uint32_t(sc) * alpha);
        return uint8_t(v > 255 ? 255 : v);
    };
    return rgb565(ch(d.r, src.r), ch(d.g, src.g), ch(d.b, src.b));
}

}  // namespace gfx
