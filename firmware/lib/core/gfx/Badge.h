#pragma once
// A palette image (5a spec §3): up to 128 colours at 8 bpp or 16 at 4 bpp, each palette entry RGB565 with
// its own alpha (the rounded corners and anti-aliased edges). Logos load into fixed-size stores in the
// screen arena; a Badge is the view drawBadge reads.
#include <cstdint>

#include "Canvas.h"

namespace gfx {

// drawBadge trusts a Badge's sizes: `colours` must not exceed the pal/alpha arrays and w x h must fit px
// (rows of (w * bpp + 7) / 8 bytes). It only rejects a bad bpp or null pointers. The loader (LogoStore) is
// the single place that validates SD-card headers against the store sizes.
struct Badge {
    uint8_t w = 0, h = 0, bpp = 0, colours = 0;  // bpp 8 or 4 (two pixels a byte, high nibble first)
    const uint16_t* pal = nullptr;
    const uint8_t* alpha = nullptr;
    const uint8_t* px = nullptr;  // row-major; a 4 bpp row starts on a byte
    bool empty() const { return w == 0; }
};

// The badge's top-left at (x, y), at `a` overall opacity, clipped to the canvas.
void drawBadge(Canvas& c, const Badge& b, int x, int y, uint8_t a = 255);

// Storage for one badge of up to Side x Side pixels.
template <int Side, int Bpp, int MaxColours>
struct BadgeStore {
    static constexpr int kSide = Side, kBpp = Bpp, kMaxColours = MaxColours;
    static constexpr int kRowBytes = (Side * Bpp + 7) / 8;
    uint16_t pal[MaxColours];
    uint8_t alpha[MaxColours];
    uint8_t px[kRowBytes * Side];
    Badge badge;  // empty until loaded
};
using Badge40 = BadgeStore<40, 8, 128>;  // Closest Flight (Cinematic), Alert
using Badge30 = BadgeStore<30, 8, 128>;  // Closest Flight (Editorial) band
using Badge25 = BadgeStore<25, 4, 16>;   // Board rows

}  // namespace gfx
