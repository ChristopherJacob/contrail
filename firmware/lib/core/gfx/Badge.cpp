#include "Badge.h"

#include "Hot.h"

namespace gfx {

GFX_HOT void drawBadge(Canvas& c, const Badge& b, int x, int y, uint8_t a) {
    if (b.empty() || a == 0) return;
    if ((b.bpp != 8 && b.bpp != 4) || !b.pal || !b.alpha || !b.px) return;
    const Rect area = Rect{x, y, b.w, b.h}.intersect(c.clip());
    if (area.empty()) return;
    const int rowBytes = (b.w * b.bpp + 7) / 8;
    const uint16_t* pal = b.pal;
    const int colours = b.colours < 128 ? b.colours : 128;  // the guard and the alpha table agree
    // Palette alpha scaled by the overall opacity, once per call (at most 128 entries).
    uint8_t al[128];
    const uint8_t* alpha = b.alpha;
    if (a != 255) {
        for (int i = 0; i < colours; ++i) al[i] = uint8_t(div255(uint32_t(b.alpha[i]) * a));
        alpha = al;
    }
    // Forced inline: an out-of-line copy would be placed in flash and called from IRAM for every pixel.
    auto put = [&](uint16_t* d, int idx) __attribute__((always_inline)) {
        if (idx >= colours) return;  // a corrupt index: leave the pixel
        const uint32_t v = alpha[idx];
        if (!v) return;
        *d = v >= 255 ? pal[idx] : blend565(*d, pal[idx], uint8_t(v));
    };
    for (int py = area.y; py < area.bottom(); ++py) {
        const uint8_t* src = b.px + (py - y) * rowBytes;
        uint16_t* row = &c.at(area.x, py) - area.x;
        if (b.bpp == 8) {
            for (int px = area.x; px < area.right(); ++px) put(row + px, src[px - x]);
        } else {
            for (int px = area.x; px < area.right(); ++px) {
                const int i = px - x;
                put(row + px, i & 1 ? src[i >> 1] & 0x0F : src[i >> 1] >> 4);
            }
        }
    }
}

}  // namespace gfx
