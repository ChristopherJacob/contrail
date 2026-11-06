#pragma once
// Small drawing pieces the screens share. All take global coordinates and draw only
// inside the canvas clip, like the gfx primitives they are built on.
#include <cstddef>
#include <cstdint>

#include "gfx/Canvas.h"
#include "gfx/Font.h"
#include "screens/Theme.h"

namespace ui {

// Text with a soft drop shadow so it stays legible over any sky.
void shadowText(gfx::Canvas& c, const gfx::FontData& f, int x, int baseline, const char* s, gfx::Rgb col,
                gfx::Align al, const scr::Theme& t);
// Box a text run covers (shadow included), for damage.
gfx::Rect textBox(const gfx::FontData& f, int x, int baseline, const char* s, gfx::Align al = gfx::Align::Left);
// The same for a run whose textWidth() is already known.
gfx::Rect textBox(const gfx::FontData& f, int x, int baseline, int width, gfx::Align al = gfx::Align::Left);
// Rounded panel with a one-pixel highlight along its top edge.
void card(gfx::Canvas& c, const gfx::Rect& r, float radius, gfx::Rgb fill, gfx::Rgb edge);
// Thin track with a filled portion and a dot at `frac` (0..1); flat: a 6 px bar centred on `y`, no dot.
// knob = false leaves the dot off, for a caller that marks the progress point itself.
void progressBar(gfx::Canvas& c, int x, int y, int w, float frac, gfx::Rgb track, gfx::Rgb fill, bool flat = false,
                 bool knob = true);
// Label / value / sub-line tile (Closest Flight stats).
void statTile(gfx::Canvas& c, const gfx::Rect& r, const scr::Theme& t, const char* label, const char* value,
              const char* sub, gfx::Rgb subCol);
// Four-pixel rounded airline colour stripe.
void stripe(gfx::Canvas& c, int x, int y, int h, gfx::Rgb col);
// Small compass rose with an arrow pointing along `bearingDeg`. Flat: a solid disc in textOn(field)
// with its marks in the colour of the field it sits on (by default a white disc, black marks).
void compass(gfx::Canvas& c, float cx, float cy, float r, float bearingDeg, const scr::Theme& t,
             gfx::Rgb field = {0, 0, 0});
// Row of dots centred horizontally at `y`; `active` is highlighted.
void pageDots(gfx::Canvas& c, int count, int active, int y, const scr::Theme& t);
// Contrail behind a plane whose tail is at (tx, ty), trailing `len` px along the unit vector
// (bx, by): two engine trails that spread and merge into one soft plume, fading with distance.
// `scale` sizes the plume (1 = a 34 px airliner); `peak` is the opacity where it forms.
void contrail(gfx::Canvas& c, float tx, float ty, float bx, float by, float len, gfx::Rgb col, float scale = 1,
              float peak = 240);
// Copies `s`, trimming characters and appending "…" until it fits `maxW` pixels.
void ellipsize(char* out, size_t n, const gfx::FontData& f, const char* s, int maxW);

// Fills that honour Theme::flat. A flat theme gets solid colour (vFill: the first stop) or
// hard-edged bands (radialFill: each stop from its position out to the next, at most 8 stops), never
// dithering. n <= 0 draws nothing.
void vFill(gfx::Canvas& c, const gfx::Rect& r, const gfx::Rgb* stops, const float* pos, int n, const scr::Theme& t);
void radialFill(gfx::Canvas& c, const gfx::Rect& r, float cx, float cy, float rx, float ry, const gfx::Rgb* stops,
                const float* pos, int n, const scr::Theme& t);
// Additive glow; nothing at all in a flat theme.
void glow(gfx::Canvas& c, float cx, float cy, float radius, gfx::Rgb col, uint8_t peak, const scr::Theme& t);
// card() in a rounded theme; a plain square fill without the highlight in a flat one.
void panel(gfx::Canvas& c, const gfx::Rect& r, float radius, gfx::Rgb fill, gfx::Rgb edge, const scr::Theme& t);
// WCAG 2 relative luminance (0..1) and contrast ratio (1..21).
float relativeLuminance(gfx::Rgb c);
float contrastRatio(gfx::Rgb a, gfx::Rgb b);
// Text colour for a fill: white unless white falls under 3:1 against it (the WCAG minimum for
// large text), then black.
gfx::Rgb textOn(gfx::Rgb fill);
// `col` lightened toward white just far enough to reach `minContrast` against `bg` (unchanged when it
// already does): a brand colour that still reads as itself on a dark card.
gfx::Rgb readableOn(gfx::Rgb col, gfx::Rgb bg, float minContrast);

}  // namespace ui
