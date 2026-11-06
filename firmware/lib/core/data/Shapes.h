#pragma once
// Aircraft shape families (5a spec §5): 12 outlines, each a top view (nose up, 100-unit box) and a side
// view (nose right, 120 x 40 box), and the ICAO type -> family table. Outlines and table are generated
// by tools/gen_shapes.py into ShapeData.cpp.
#include <cstdint>

#include "gfx/Canvas.h"

namespace data {

enum class Family : uint8_t {
    None = 0,  // unknown: the screens draw gfx::planeGlyph
    Narrowbody, Widebody, Jumbo, Superjumbo, Regional, Bizjet, Turboprop, Single, Twin, Helicopter, FastJet,
    Airlifter,
};
constexpr int kFamilyCount = 12;
constexpr int kMaxOutline = 16;  // most points in one top half or side outline (gen_shapes.py checks it too)

// One family's outlines, as uint8 half-units, in this order: top half outline (x, y) * topN, top engines
// (dx, y, rx, ry) * topEngN (mirrored), top lines (x0, y0, x1, y1) * topLineN (mirrored), side outline
// (x, y) * sideN, side engines (cx, cy, rx, ry) * sideEngN, side lines * sideLineN.
struct ShapeDef {
    const uint8_t* data;
    uint8_t topN, topEngN, topLineN, sideN, sideEngN, sideLineN;
    bool rotor;    // a rotor disc over the top view
    uint8_t iconPx;  // Radar and Map icon size, from the family's typical wake class
};
extern const ShapeDef kShapes[kFamilyCount];
// Sorted: the type code in base 37 (21 bits) << 4 | family.
extern const uint32_t kTypeFamilies[];
extern const int kTypeFamilyCount;

// The family for an ICAO type designator ("B789"), or None when the code is unknown or empty.
Family familyForType(const char* icaoType);
// The family for an ADS-B emitter category (0xA1 .. 0xA7), or None.
Family familyForCategory(uint8_t category);
// The type, else the category.
Family familyFor(const char* icaoType, uint8_t category);

// Radar and Map icon size in pixels (planeGlyph's 12 px for None).
int iconPx(Family f);
// The Radar and Map selection ring's radius: just clear of the icon's box.
inline float selectRingR(Family f) { return iconPx(f) / 2.f + 3; }
// Half the side of a square, centred on the icon's whole-pixel centre, that covers everything the icon
// draws: drawTop's reach (px * 0.71) and, when selected, the ring (1.2 px wide, plus AA). None: planeGlyph
// at 12 px and its ring.
inline int iconReach(Family f, bool selected) {
    if (f == Family::None) return selected ? 11 : 8;
    const int px = iconPx(f), body = int(px * 0.71f) + 1;
    const int ring = (px + 1) / 2 + 5;
    return selected && ring > body ? ring : body;
}
// How far a label moves out from the spots set for a 12 px mark: by how much the icon is bigger, and
// 3 px more to clear a selection ring.
inline int labelPushOut(Family f, bool selected) {
    const int extra = iconPx(f) / 2 - 6;
    return (extra > 0 ? extra : 0) + (selected ? 3 : 0);
}
// Where a label's text starts for a spot (dx, dy) set for a 12 px mark at (x, y), `left` when the text extends
// left of the anchor, pushed out by `out` (labelPushOut): always sideways, and up or down for the spots above
// (dy < 0) or below (dy > 1) the mark.
inline void labelAnchor(int x, int y, int dx, int dy, bool left, int out, int& lx, int& ly) {
    lx = x + (left ? -dx - out : dx + out);
    ly = y + dy + (dy < 0 ? -out : dy > 1 ? out : 0);
}

// The top view centred on (cx, cy), `px` pixels across the box, nose along headingDeg. Engines are drawn in
// the body colour and overlap the body, so a translucent `a` shows the overlaps. Prop and rotor lines are
// drawn at about 55% of `a`. The rotor and the box diagonals reach past px / 2: size damage boxes by
// px * 0.71. Family::None draws gfx::planeGlyph at px.
void drawTop(gfx::Canvas& c, Family f, float cx, float cy, float px, float headingDeg, gfx::Rgb col, uint8_t a = 255);
// The side view with its box's top-left at (x, y), `w` pixels wide (height w / 3), nose right; engines in
// `engine`. Side lines are drawn at the full `a`. Nothing for Family::None.
void drawSide(gfx::Canvas& c, Family f, float x, float y, float w, gfx::Rgb body, gfx::Rgb engine, uint8_t a = 255);

}  // namespace data
