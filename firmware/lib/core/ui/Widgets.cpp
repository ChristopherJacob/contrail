#include "Widgets.h"

#ifdef CONTRAIL_ESP32
#pragma GCC optimize("O2")  // contrail() is a per-pixel loop
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#include "gfx/Hot.h"
#include "gfx/Raster.h"
#include "gfx/fonts/Fonts.h"

namespace ui {
using namespace gfx;

void shadowText(Canvas& c, const FontData& f, int x, int y, const char* s, Rgb col, Align al, const scr::Theme& t) {
    if (!t.flat) drawText(c, f, x + 1, y + 1, s, t.shadow, al, 110);
    drawText(c, f, x, y, s, col, al);
}

Rect textBox(const FontData& f, int x, int baseline, const char* s, Align al) {
    return textBox(f, x, baseline, textWidth(f, s), al);
}

Rect textBox(const FontData& f, int x, int baseline, int w, Align al) {
    int x0 = al == Align::Left ? x : al == Align::Center ? x - w / 2 : x - w;
    return {x0 - 1, baseline - f.ascent - 1, w + 3, f.ascent + f.descent + 3};
}

void card(Canvas& c, const Rect& r, float radius, Rgb fill, Rgb edge) {
    fillRoundRect(c, r, radius, fill);
    aaLine(c, r.x + radius, r.y + 0.5f, r.right() - radius, r.y + 0.5f, 1, edge, 200);
}

void progressBar(Canvas& c, int x, int y, int w, float frac, Rgb track, Rgb fill, bool flat, bool knob) {
    frac = frac < 0 ? 0 : frac > 1 ? 1 : frac;
    if (flat) {
        c.fill({x, y - 2, w, 6}, rgb565(track));
        int fw = int(w * frac + 0.5f);
        if (fw > 0) c.fill({x, y - 2, fw, 6}, rgb565(fill));
        return;
    }
    float fx = x + w * frac;
    aaLine(c, x, y + 0.5f, x + w, y + 0.5f, 2, track);
    if (fx > x + 1) aaLine(c, x, y + 0.5f, fx, y + 0.5f, 2, fill);
    if (knob) aaCircle(c, fx, y + 0.5f, 4.5f, fill);
}

void statTile(Canvas& c, const Rect& r, const scr::Theme& t, const char* label, const char* value, const char* sub,
              Rgb subCol) {
    if (!t.flat) fillRoundRect(c, r, 8, t.tile);
    drawText(c, kFontSmall, r.x + 7, r.y + 13, label, t.textDim);
    drawText(c, *t.fontLabel, r.x + 7, r.y + 32, value, t.text);
    if (sub && sub[0]) drawText(c, kFontSmall, r.x + 7, r.y + 46, sub, subCol);
}

void stripe(Canvas& c, int x, int y, int h, Rgb col) { fillRoundRect(c, {x, y, 4, h}, 2, col); }

void compass(Canvas& c, float cx, float cy, float r, float bearingDeg, const scr::Theme& t, Rgb field) {
    // Flat: a solid disc that reads on the field, marked in the field's colour (Bold Editorial); else a
    // dim disc with a ring.
    const bool flat = t.flat;
    const Rgb ink = flat ? field : t.textSoft, arrow = flat ? field : t.accent;
    aaCircle(c, cx, cy, r, flat ? textOn(field) : t.veil, flat ? 255 : 140);
    if (!flat) aaRing(c, cx, cy, r - 0.5f, 1, t.textSoft, 150);
    // Ticks at E, S and W; north gets its letter.
    for (int k = 1; k < 4; ++k) {
        float a = k * 90 * 3.14159265f / 180.f, s = std::sin(a), co = -std::cos(a);
        aaLine(c, cx + s * (r - 4), cy + co * (r - 4), cx + s * (r - 1.5f), cy + co * (r - 1.5f), 1, ink, 140);
    }
    drawText(c, kFontSmall, int(cx), int(cy - r + 11), "N", ink, Align::Center, 200);
    float a = bearingDeg * 3.14159265f / 180.f, s = std::sin(a), co = std::cos(a);
    auto rot = [&](float px, float py) { return Vec2{cx + px * co - py * s, cy + px * s + py * co}; };
    // Arrowhead pointing "up" in its own frame, rotated to the bearing; short enough to clear
    // the N unless it points almost straight at it.
    Vec2 pts[] = {rot(0, -(r - 6)), rot(4.5f, 4.5f), rot(0, 1.5f), rot(-4.5f, 4.5f)};
    aaPolygon(c, pts, 4, arrow);
}

void pageDots(Canvas& c, int count, int active, int y, const scr::Theme& t) {
    const int gap = 10;
    float x0 = 120 - (count - 1) * gap / 2.f;
    for (int i = 0; i < count; ++i) {
        if (i == active) aaCircle(c, x0 + i * gap, y, 2.6f, t.text, 230);
        else aaCircle(c, x0 + i * gap, y, 2.f, t.textDim, 150);
    }
}

void ellipsize(char* out, size_t n, const FontData& f, const char* s, int maxW) {
    std::strncpy(out, s, n - 1);
    out[n - 1] = 0;
    // A buffer-sized cut can land inside a multi-byte character: drop its leading bytes too.
    size_t cut = std::strlen(out);
    while (cut > 0 && (uint8_t(s[cut]) & 0xC0) == 0x80) --cut;
    out[cut] = 0;
    if (textWidth(f, out) <= maxW) return;
    static const char kDots[] = "…";  // 3 bytes in UTF-8
    size_t len = std::strlen(out);
    while (len > 0) {
        do --len;
        while (len > 0 && (uint8_t(out[len]) & 0xC0) == 0x80);  // back up one whole character
        if (len + sizeof kDots > n) continue;
        std::memcpy(out + len, kDots, sizeof kDots);
        if (textWidth(f, out) <= maxW) return;
    }
    out[0] = 0;
}

// Contrail behind a plane whose tail is at (tx, ty), trailing along unit vector (bx, by):
// two engine trails that spread and merge into one soft plume, fading out with distance.
// Drawn per pixel so the plume is one smooth field, without the bright joints overlapping
// line segments would leave.
namespace {
constexpr int kLut = 64;
// 1 / (1 + 1.6 f) and 1 / (0.8 + 4.6 f) for f = i / kLut: the plume's thinning and widening.
struct ContrailLut {
    float peak[kLut + 1], invHw[kLut + 1];
    constexpr ContrailLut() : peak(), invHw() {
        for (int i = 0; i <= kLut; ++i) {
            float f = float(i) / kLut;
            peak[i] = 1.f / (1.f + 1.6f * f), invHw[i] = 1.f / (0.8f + 4.6f * f);
        }
    }
};
constexpr ContrailLut kContrailLut{};
}  // namespace

GFX_HOT void contrail(Canvas& c, float tx, float ty, float bx, float by, float len, Rgb col, float scale, float peak) {
    if (len < 1) return;
    float ex = tx + bx * len, ey = ty + by * len, pad = 8 * scale;
    Rect bb{int(std::floor(std::fmin(tx, ex) - pad)), int(std::floor(std::fmin(ty, ey) - pad)), 0, 0};
    bb.w = int(std::ceil(std::fmax(tx, ex) + pad)) - bb.x, bb.h = int(std::ceil(std::fmax(ty, ey) + pad)) - bb.y;
    Rect area = bb.intersect(c.clip());
    if (area.empty()) return;

    // The two reciprocals of f, sampled once (they do not depend on the call), so the inner
    // loop interpolates instead of dividing.
    const float* peakLut = kContrailLut.peak;
    const float* invHwLut = kContrailLut.invHw;
    const float invScale = 1.f / scale;
    const float invLen = 1.f / len, invRamp = 1.f / (6.f * scale);
    const float kMaxV = 5.4f * scale;  // widest the plume ever gets: hw + off at f = 1 (and 4.2 at f = 0)
    const float eps = 1e-4f;

    for (int y = area.y; y < area.bottom(); ++y) {
        float py = y + 0.5f - ty, px0 = area.x + 0.5f - tx;
        float u = px0 * bx + py * by;              // along the plume, at the row's first pixel
        float vs = px0 * by - py * bx;             // signed distance from the axis
        // Columns where 0 < u < len and |v| < kMaxV; one extra pixel either side absorbs rounding.
        float lo = 0, hi = float(area.w);
        auto clipSpan = [&](float val, float step, float min, float max) {
            if (std::fabs(step) < eps) {
                if (val <= min || val >= max) hi = lo - 1;
                return;
            }
            float a = (min - val) / step, b = (max - val) / step;
            if (a > b) std::swap(a, b);
            lo = std::fmax(lo, a - 1), hi = std::fmin(hi, b + 1);
        };
        clipSpan(u, bx, 0.f, len);
        clipSpan(vs, by, -kMaxV, kMaxV);
        int x0 = int(std::floor(lo)), x1 = int(std::ceil(hi));
        if (x0 < 0) x0 = 0;
        if (x1 > area.w) x1 = area.w;
        u += x0 * bx, vs += x0 * by;
        for (int i = x0; i < x1; ++i, u += bx, vs += by) {
            if (u <= 0 || u >= len) continue;
            float v = std::fabs(vs);
            float f = u * invLen;
            float hw = (0.8f + f * 4.6f) * scale;                 // half width widens as it diffuses
            float off = 3.4f * scale * std::fmax(0.f, 1.f - f * 2.8f);  // engine trails merge by ~35 %
            if (v > off + hw) continue;
            float fl = f * kLut;
            int k = std::min(int(fl), kLut - 1);
            float fr = fl - k;
            float d = std::fmin(std::fabs(v - off), v + off) * invScale *
                      (invHwLut[k] + (invHwLut[k + 1] - invHwLut[k]) * fr);
            float g = 1.f - d * d;
            if (g <= 0) continue;
            float q = 1 - f, fade = q * (0.35f + 0.65f * q) * std::fmin(1.f, u * invRamp);  // forms just behind the engines
            float pk = peak * (peakLut[k] + (peakLut[k + 1] - peakLut[k]) * fr);  // spreading thins it
            int a = int(g * fade * pk + 0.5f);
            if (a > 0) c.blendPixel(area.x + i, y, col, uint8_t(std::min(a, 255)));
        }
    }
}

void vFill(Canvas& c, const Rect& r, const Rgb* stops, const float* pos, int n, const scr::Theme& t) {
    if (n <= 0) return;
    if (t.flat) c.fill(r, rgb565(stops[0]));
    else c.vGradient(r, stops, pos, n);
}

GFX_HOT void radialFill(Canvas& c, const Rect& r, float cx, float cy, float rx, float ry, const Rgb* stops,
                        const float* pos, int n, const scr::Theme& t) {
    if (n <= 0) return;
    if (!t.flat) return c.radialGradient(r, cx, cy, rx, ry, stops, pos, n);
    bool same = true;
    for (int i = 1; i < n; ++i)
        same = same && stops[i].r == stops[0].r && stops[i].g == stops[0].g && stops[i].b == stops[0].b;
    if (same) return c.fill(r, rgb565(stops[0]));  // Editorial's black radar scope: no per-pixel work
    Rect a = r.intersect(c.clip());
    if (a.empty()) return;
    constexpr int kMax = 8;
    n = std::min(n, kMax);
    uint16_t col[kMax];
    float edge2[kMax];  // squared normalised radius where each band starts
    for (int i = 0; i < n; ++i) col[i] = rgb565(stops[i]), edge2[i] = pos[i] * pos[i];
    const float irx = 1.f / rx, iry = 1.f / ry;
    for (int y = a.y; y < a.bottom(); ++y) {
        float dy = (y + 0.5f - cy) * iry, dy2 = dy * dy;
        uint16_t* p = &c.at(a.x, y);
        for (int x = a.x; x < a.right(); ++x) {
            float dx = (x + 0.5f - cx) * irx, d2 = dx * dx + dy2;
            int b = 0;
            while (b + 1 < n && d2 >= edge2[b + 1]) ++b;
            *p++ = col[b];
        }
    }
}

void glow(Canvas& c, float cx, float cy, float radius, Rgb col, uint8_t peak, const scr::Theme& t) {
    if (!t.flat) c.radialGlow(cx, cy, radius, col, peak);
}

void panel(Canvas& c, const Rect& r, float radius, Rgb fill, Rgb edge, const scr::Theme& t) {
    if (t.flat) c.fill(r, rgb565(fill));
    else card(c, r, radius, fill, edge);
}

namespace {
float linear(uint8_t v) {
    float s = v / 255.f;
    return s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
}
}  // namespace

float relativeLuminance(Rgb c) { return 0.2126f * linear(c.r) + 0.7152f * linear(c.g) + 0.0722f * linear(c.b); }

float contrastRatio(Rgb a, Rgb b) {
    float la = relativeLuminance(a), lb = relativeLuminance(b);
    if (la < lb) std::swap(la, lb);
    return (la + 0.05f) / (lb + 0.05f);
}

Rgb textOn(Rgb fill) {
    const Rgb white{255, 255, 255}, black{0, 0, 0};
    // Contrast with white is 1.05 / (L + 0.05), which is at least 3 exactly when L <= 0.3.
    return relativeLuminance(fill) <= 0.3f ? white : black;
}

Rgb readableOn(Rgb col, Rgb bg, float minContrast) {
    for (int i = 0; i <= 32; ++i) {
        Rgb c = lerp(col, {255, 255, 255}, i / 32.f);
        if (contrastRatio(c, bg) >= minContrast) return c;
    }
    return {255, 255, 255};
}

}  // namespace ui
