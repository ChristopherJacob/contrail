#pragma once
#include "Canvas.h"

// Anti-aliased primitives. Each one walks only its bounding box ∩ the canvas clip, so
// drawing into a 32-row strip costs only the rows that strip actually contains.
// Angles are degrees clockwise from "up" (screen north), matching compass bearings.
namespace gfx {

void aaLine(Canvas& c, float x0, float y0, float x1, float y1, float width, Rgb col, uint8_t a = 255);
void aaCircle(Canvas& c, float cx, float cy, float r, Rgb col, uint8_t a = 255);
void aaRing(Canvas& c, float cx, float cy, float r, float thickness, Rgb col, uint8_t a = 255);
void aaArc(Canvas& c, float cx, float cy, float r, float thickness, float a0Deg, float a1Deg, Rgb col,
           uint8_t a = 255);
// Filled rectangle with anti-aliased rounded corners (horizontal coverage only).
void fillRoundRect(Canvas& c, const Rect& r, float radius, Rgb col, uint8_t a = 255);
// Filled pie slice whose alpha ramps linearly from aStart (at a0) to aEnd (at a1).
void aaWedge(Canvas& c, float cx, float cy, float r, float a0Deg, float a1Deg, Rgb col, uint8_t aStart,
             uint8_t aEnd);
// Even-odd fill with 4 sub-scanlines per row and exact horizontal coverage.
void aaPolygon(Canvas& c, const Vec2* pts, int n, Rgb col, uint8_t a = 255);
// Airliner silhouette, nose pointing along headingDeg, `size` = wingspan in pixels.
void planeGlyph(Canvas& c, float cx, float cy, float size, float headingDeg, Rgb col, uint8_t a = 255);

}  // namespace gfx
