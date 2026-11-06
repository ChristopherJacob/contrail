#include "Font.h"

#ifdef CONTRAIL_ESP32
#pragma GCC optimize("O2")  // drawText is a per-pixel loop
#endif

namespace gfx {
namespace {

const GlyphInfo* findGlyph(const FontData& f, uint32_t cp) {
    // Every font starts with a run of ASCII from the space: there a code point indexes directly.
    if (!f.count) return nullptr;
    const uint32_t k = cp - f.codepoints[0];
    if (k < f.count && f.codepoints[k] == cp) return &f.glyphs[k];
    int lo = 0, hi = f.count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint16_t v = f.codepoints[mid];
        if (v == cp) return &f.glyphs[mid];
        if (v < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return nullptr;
}

}  // namespace

uint32_t nextCodepoint(const char*& s) {
    auto& u = reinterpret_cast<const uint8_t*&>(s);
    uint8_t b = *u;
    if (!b) return 0;
    uint32_t cp;
    int extra;
    if (b < 0x80) cp = b, extra = 0;
    else if ((b & 0xE0) == 0xC0) cp = b & 0x1F, extra = 1;
    else if ((b & 0xF0) == 0xE0) cp = b & 0x0F, extra = 2;
    else cp = b & 0x07, extra = 3;
    ++u;
    for (int i = 0; i < extra && (*u & 0xC0) == 0x80; ++i, ++u) cp = (cp << 6) | (*u & 0x3F);
    return cp;
}

int textWidth(const FontData& f, const char* s) {
    int w = 0;
    while (uint32_t cp = nextCodepoint(s)) {
        const GlyphInfo* g = findGlyph(f, cp);
        w += g ? g->adv : f.spaceAdv;
    }
    return w;
}

int drawText(Canvas& c, const FontData& f, int x, int baselineY, const char* s, Rgb col, uint8_t a) {
    const Rect& clip = c.clip();
    // Whole line outside this strip: only advance the pen.
    bool rowsVisible = baselineY + f.descent > clip.y && baselineY - f.ascent < clip.bottom();
    uint8_t alpha[16];
    bool alphaReady = false;
    while (uint32_t cp = nextCodepoint(s)) {
        const GlyphInfo* g = findGlyph(f, cp);
        if (!g) {
            x += f.spaceAdv;
            continue;
        }
        if (rowsVisible && g->w) {
            if (!alphaReady) {  // each 4-bit coverage's alpha at this opacity, once per call
                for (int n = 0; n < 16; ++n) alpha[n] = uint8_t((n * 17 * a + 127) / 255);
                alphaReady = true;
            }
            int gx = x + g->xoff, gy = baselineY + g->yoff;
            Rect box = Rect{gx, gy, g->w, g->h}.intersect(clip);
            const uint8_t* bits = f.bitmap + g->offset;
            for (int y = box.y; y < box.bottom(); ++y) {
                for (int px = box.x; px < box.right(); ++px) {
                    int i = (y - gy) * g->w + (px - gx);
                    uint8_t nib = (i & 1) ? (bits[i >> 1] & 0x0F) : (bits[i >> 1] >> 4);
                    if (!nib) continue;
                    c.at(px, y) = blend565(c.at(px, y), col, alpha[nib]);
                }
            }
        }
        x += g->adv;
    }
    return x;
}

int drawText(Canvas& c, const FontData& f, int x, int baselineY, const char* s, Rgb col, Align align, uint8_t a) {
    if (align != Align::Left) {
        int w = textWidth(f, s);
        x -= align == Align::Center ? w / 2 : w;
    }
    return drawText(c, f, x, baselineY, s, col, a);
}

}  // namespace gfx
