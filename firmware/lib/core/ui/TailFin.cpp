#include "TailFin.h"

#include <cassert>
#include <cmath>

#include "gfx/Raster.h"
#include "ui/Widgets.h"

namespace ui {
using namespace gfx;

namespace {
const Vec2 kFin[4] = {{10, 94}, {56, 8}, {84, 8}, {92, 94}};
const Vec2 kBand[4] = {{0, 74}, {100, 42}, {100, 60}, {0, 92}};
constexpr Rgb kCinematicTile{0x26, 0x30, 0x6A};
constexpr float kInset = 0.8f;  // the fin or shape fills 80 % of the tile

// Clip `in` (n points) to the convex, clockwise-on-screen polygon `clip`; returns the count in `out`.
int clipConvex(const Vec2* in, int n, const Vec2* clip, int m, Vec2* out) {
    assert(n + m <= 16);  // each clip edge adds at most one vertex
    Vec2 buf[16];
    Vec2* src = buf;
    for (int i = 0; i < n; ++i) src[i] = in[i];
    for (int e = 0; e < m && n; ++e) {
        const Vec2 a = clip[e], b = clip[(e + 1) % m];
        auto inside = [&](Vec2 p) { return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x) >= 0; };
        Vec2 next[16];
        int k = 0;
        for (int i = 0; i < n; ++i) {
            Vec2 p = src[i], q = src[(i + 1) % n];
            bool ip = inside(p), iq = inside(q);
            if (ip) next[k++] = p;
            if (ip != iq) {
                float d1 = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
                float d2 = (b.x - a.x) * (q.y - a.y) - (b.y - a.y) * (q.x - a.x);
                float t = d1 / (d1 - d2);
                next[k++] = {p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t};
            }
        }
        n = k;
        for (int i = 0; i < n; ++i) src[i] = next[i];
    }
    for (int i = 0; i < n; ++i) out[i] = src[i];
    return n;
}

float dist(Rgb a, Rgb b) {
    float dr = float(a.r) - b.r, dg = float(a.g) - b.g, db = float(a.b) - b.b;
    return std::sqrt(dr * dr + dg * dg + db * db);
}
}  // namespace

Rgb finStripe(Rgb fill, Rgb color) {
    if (dist(fill, color) >= 80) return color;
    const Rgb white{255, 255, 255}, ink{20, 20, 24};
    return contrastRatio(fill, white) >= contrastRatio(fill, ink) ? white : ink;
}

void identityTile(Canvas& c, int x, int y, int side, const scr::Theme& t, uint8_t a) {
    if (t.flat) fillRoundRect(c, {x, y, side, side}, 3, {255, 255, 255}, a);
    else fillRoundRect(c, {x, y, side, side}, side / 5.f, kCinematicTile, a);
}

void drawFin(Canvas& c, int x, int y, int side, const data::Airline* al, const scr::Theme& t, uint8_t a) {
    if (!c.clip().intersects({x, y, side, side})) return;
    identityTile(c, x, y, side, t, a);
    Rgb fill, stripe;
    if (al) {
        const bool livery = al->fill.r || al->fill.g || al->fill.b;
        fill = livery ? al->fill : al->color;
        stripe = finStripe(fill, al->color);
    } else {
        fill = t.brandNeutral;
        stripe = lerp(fill, {255, 255, 255}, 0.45f);
    }
    const float k = side * kInset / 100.f, ox = x + side * (1 - kInset) / 2, oy = y + side * (1 - kInset) / 2;
    Vec2 fin[4], band[16];
    for (int i = 0; i < 4; ++i) fin[i] = {ox + kFin[i].x * k, oy + kFin[i].y * k};
    Vec2 raw[4];
    for (int i = 0; i < 4; ++i) raw[i] = {ox + kBand[i].x * k, oy + kBand[i].y * k};
    // Tile, fin and stripe are drawn as separate layers, so lower ones show through during a fade (fades are short).
    aaPolygon(c, fin, 4, fill, a);
    const int n = clipConvex(raw, 4, fin, 4, band);
    if (n >= 3) aaPolygon(c, band, n, stripe, a);
}

void drawFamilyTile(Canvas& c, int x, int y, int side, data::Family f, const scr::Theme& t, uint8_t a) {
    if (!c.clip().intersects({x, y, side, side})) return;
    identityTile(c, x, y, side, t, a);
    const Rgb ink = t.flat ? Rgb{0x2C, 0x2C, 0x32} : t.text;
    data::drawTop(c, f, x + side / 2.f, y + side / 2.f, side * kInset, 0, ink, a);
}

}  // namespace ui
