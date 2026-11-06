#pragma once
#include <cstdint>

#include "Canvas.h"

namespace gfx {

struct GlyphInfo {
    uint32_t offset;  // into bitmap, in bytes; 2 px per byte, high nibble first
    uint8_t w, h;
    int8_t xoff, yoff;  // from pen position on the baseline to the bitmap's top-left
    uint8_t adv;
};

struct FontData {
    const uint8_t* bitmap;
    const GlyphInfo* glyphs;
    const uint16_t* codepoints;  // ascending, parallel to glyphs
    uint16_t count;
    uint8_t ascent, descent, spaceAdv;
};

enum class Align { Left, Center, Right };

// Decode one UTF-8 code point and advance `s`. Returns 0 at end of string.
uint32_t nextCodepoint(const char*& s);

int textWidth(const FontData& f, const char* utf8);
// Draws with the baseline at `baselineY`. Returns the x after the last glyph.
int drawText(Canvas& c, const FontData& f, int x, int baselineY, const char* utf8, Rgb col, uint8_t a = 255);
int drawText(Canvas& c, const FontData& f, int x, int baselineY, const char* utf8, Rgb col, Align align,
             uint8_t a = 255);

}  // namespace gfx
