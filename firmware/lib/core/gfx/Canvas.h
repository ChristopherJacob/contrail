#pragma once
#include <cstdint>

#include "Color.h"

namespace gfx {

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool empty() const { return w <= 0 || h <= 0; }
    int right() const { return x + w; }
    int bottom() const { return y + h; }
    Rect intersect(const Rect& o) const;
    bool intersects(const Rect& o) const { return !intersect(o).empty(); }
    Rect unite(const Rect& o) const;
    bool operator==(const Rect& o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
    bool operator!=(const Rect& o) const { return !(*this == o); }
};

struct Vec2 {
    float x, y;
};

// A window onto the global screen space that owns the block of columns
// [bandX, bandX + width) and rows [bandY, bandY + bandH), stored packed in buf.
// All coordinates are global; anything outside the band or the clip is discarded.
class Canvas {
   public:
    Canvas(uint16_t* buf, int width, int bandY, int bandH) : Canvas(buf, 0, width, bandY, bandH) {}
    Canvas(uint16_t* buf, int bandX, int width, int bandY, int bandH);

    int width() const { return width_; }
    int bandX() const { return bandX_; }
    int bandY() const { return bandY_; }
    int bandH() const { return bandH_; }
    Rect band() const { return {bandX_, bandY_, width_, bandH_}; }

    void setClip(const Rect& r);
    void resetClip();
    // Effective drawable area: clip ∩ band, in global coordinates.
    const Rect& clip() const { return clip_; }

    uint16_t* buffer() { return buf_; }
    uint16_t get(int x, int y) const { return buf_[(y - bandY_) * width_ + (x - bandX_)]; }
    bool contains(int x, int y) const {
        return x >= clip_.x && x < clip_.right() && y >= clip_.y && y < clip_.bottom();
    }

    void pixel(int x, int y, uint16_t c) {
        if (contains(x, y)) at(x, y) = c;
    }
    void blendPixel(int x, int y, Rgb c, uint8_t a) {
        if (a && contains(x, y)) at(x, y) = blend565(at(x, y), c, a);
    }
    void addPixel(int x, int y, Rgb c, uint8_t a) {
        if (a && contains(x, y)) at(x, y) = add565(at(x, y), c, a);
    }
    // Unchecked access for primitives that have already clipped.
    uint16_t& at(int x, int y) { return buf_[(y - bandY_) * width_ + (x - bandX_)]; }

    void fill(const Rect& r, uint16_t c);
    void fillBlend(const Rect& r, Rgb c, uint8_t a);
    // Dithered vertical gradient through n stops at positions pos[i] in 0..1 (ascending).
    void vGradient(const Rect& r, const Rgb* stops, const float* pos, int n);
    // Dithered elliptical gradient centred on (cx, cy): stops run from the centre (0) to
    // the ellipse with radii rx, ry (1); everything beyond takes the last stop.
    void radialGradient(const Rect& r, float cx, float cy, float rx, float ry, const Rgb* stops, const float* pos,
                        int n);
    // Additive glow with quadratic falloff to zero at `radius`.
    void radialGlow(float cx, float cy, float radius, Rgb c, uint8_t peakAlpha);

   private:
    uint16_t* buf_;
    int bandX_, width_, bandY_, bandH_;
    Rect clip_;
};

// Colour at t (0..1) through gradient stops.
Rgb sampleStops(const Rgb* stops, const float* pos, int n, float t);

}  // namespace gfx
