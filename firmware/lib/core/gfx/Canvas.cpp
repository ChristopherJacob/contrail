#include "Canvas.h"

#ifdef CONTRAIL_ESP32
// Per-pixel loops: the firmware is built with -Os, which roughly halves their speed.
#pragma GCC optimize("O2")
#endif

#include <algorithm>
#include <cmath>
#include <cstring>

#include "Dither.h"
#include "Hot.h"

namespace gfx {

Rect Rect::intersect(const Rect& o) const {
    int x0 = std::max(x, o.x), y0 = std::max(y, o.y);
    int x1 = std::min(right(), o.right()), y1 = std::min(bottom(), o.bottom());
    if (x1 <= x0 || y1 <= y0) return {};
    return {x0, y0, x1 - x0, y1 - y0};
}

Rect Rect::unite(const Rect& o) const {
    if (empty()) return o;
    if (o.empty()) return *this;
    int x0 = std::min(x, o.x), y0 = std::min(y, o.y);
    int x1 = std::max(right(), o.right()), y1 = std::max(bottom(), o.bottom());
    return {x0, y0, x1 - x0, y1 - y0};
}

Canvas::Canvas(uint16_t* buf, int bandX, int width, int bandY, int bandH)
    : buf_(buf), bandX_(bandX), width_(width), bandY_(bandY), bandH_(bandH), clip_(band()) {}

void Canvas::setClip(const Rect& r) { clip_ = r.intersect(band()); }
void Canvas::resetClip() { clip_ = band(); }

GFX_HOT void Canvas::fill(const Rect& r, uint16_t c) {
    Rect a = r.intersect(clip_);
    for (int y = a.y; y < a.bottom(); ++y) {
        uint16_t* row = &at(a.x, y);
        for (int i = 0; i < a.w; ++i) row[i] = c;
    }
}

GFX_HOT void Canvas::fillBlend(const Rect& r, Rgb c, uint8_t alpha) {
    uint32_t a5 = (uint32_t(alpha) + 4) >> 3;  // same quantisation as blend565
    if (a5 == 0) return;
    if (a5 >= 32) return fill(r, rgb565(c));
    Rect a = r.intersect(clip_);
    uint32_t s = spread565(rgb565(c));
    for (int y = a.y; y < a.bottom(); ++y) {
        uint16_t* row = &at(a.x, y);
        for (int i = 0; i < a.w; ++i) {
            uint32_t d = spread565(row[i]);
            row[i] = pack565((d + (((s - d) * a5) >> 5)) & 0x07E0F81Fu);
        }
    }
}

Rgb sampleStops(const Rgb* stops, const float* pos, int n, float t) {
    if (n <= 0) return {0, 0, 0};
    if (t <= pos[0]) return stops[0];
    for (int i = 1; i < n; ++i) {
        if (t <= pos[i]) {
            float span = pos[i] - pos[i - 1];
            float u = span > 0 ? (t - pos[i - 1]) / span : 1.f;
            return lerp(stops[i - 1], stops[i], u);
        }
    }
    return stops[n - 1];
}

GFX_HOT void Canvas::vGradient(const Rect& r, const Rgb* stops, const float* pos, int n) {
    Rect a = r.intersect(clip_);
    if (a.empty()) return;
    for (int y = a.y; y < a.bottom(); ++y) {
        float t = r.h > 1 ? float(y - r.y) / float(r.h - 1) : 0.f;
        Rgb c = sampleStops(stops, pos, n, t);
        // The colour is constant along the row, so the dither repeats every 4 pixels.
        uint16_t pattern[4];
        for (int k = 0; k < 4; ++k) pattern[k] = dither565(c, k, y);
        uint16_t* row = &at(a.x, y);
        for (int x = a.x; x < a.right(); ++x) row[x - a.x] = pattern[x & 3];
    }
}

GFX_HOT void Canvas::radialGradient(const Rect& r, float cx, float cy, float rx, float ry, const Rgb* stops,
                            const float* pos, int n) {
    Rect a = r.intersect(clip_);
    if (a.empty()) return;
    // Indexed by the squared normalised radius so the inner loop needs no square root. Entries
    // are spaced evenly in r^2, which near the centre is coarse in r, so each pixel interpolates
    // between two entries (in 1/16ths of an entry) and dithers from that finer colour; picking
    // the nearest entry left visible rings round the centre.
    //
    // Each entry holds a channel in 1/16ths of its output step (5- or 6-bit), red and blue
    // packed in one word 16 bits apart: interpolating is then two multiply-adds (packed
    // arithmetic is exact while every field stays in range), and with the dither threshold
    // added the output bits fall straight out of the fields. Values are capped at the top
    // output level, where dithering stops anyway, so no field overflows into the next.
    // A screen draws the same gradient into every damaged strip, so the table is kept.
    constexpr int kSteps = 128, kMaxStops = 6;
    static uint32_t lutRB[kSteps + 1];  // red << 16 | blue
    static uint16_t lutG[kSteps + 1];
    static Rgb keyStops[kMaxStops];
    static float keyPos[kMaxStops];
    static int keyN = -1;
    bool same = n == keyN && n <= kMaxStops;
    for (int i = 0; same && i < n; ++i)
        same = stops[i].r == keyStops[i].r && stops[i].g == keyStops[i].g && stops[i].b == keyStops[i].b &&
               pos[i] == keyPos[i];
    if (!same) {
        for (int i = 0; i < kSteps; ++i) {
            float t = std::sqrt(float(i) / (kSteps - 1));
            // sampleStops rounds to 8 bits; interpolate the stops here at full precision instead.
            int k = 1;
            while (k < n - 1 && t > pos[k]) ++k;
            float span = n > 1 ? pos[k] - pos[k - 1] : 0, u = span > 0 ? (t - pos[k - 1]) / span : 1.f;
            u = u < 0 ? 0 : u > 1 ? 1 : u;
            const Rgb &s0 = stops[n > 1 ? k - 1 : 0], &s1 = stops[n > 1 ? k : 0];
            // 8-bit level x 16, then 1/16ths of a 5-bit step (128 sixteenths) or a 6-bit one (64).
            int r16 = int((s0.r + (s1.r - s0.r) * u) * 16 + 0.5f), g16 = int((s0.g + (s1.g - s0.g) * u) * 16 + 0.5f),
                b16 = int((s0.b + (s1.b - s0.b) * u) * 16 + 0.5f);
            uint32_t r = std::min((r16 + 4) >> 3, 31 * 16), g = std::min((g16 + 2) >> 2, 63 * 16),
                     b = std::min((b16 + 4) >> 3, 31 * 16);
            lutRB[i] = (r << 16) | b, lutG[i] = uint16_t(g);
        }
        lutRB[kSteps] = lutRB[kSteps - 1], lutG[kSteps] = lutG[kSteps - 1];
        keyN = n <= kMaxStops ? n : -1;
        for (int i = 0; i < n && i < kMaxStops; ++i) keyStops[i] = stops[i], keyPos[i] = pos[i];
    }
    // Per pixel this is still too slow on the ESP32 (~70 cycles with the interpolation), so
    // the colour is computed exactly every 8 screen columns and stepped linearly between: two
    // packed adds a pixel. The chord is within a fraction of an 8-bit level of the curve except
    // right at the centre, and it is continuous, so it cannot draw rings. Block ends sit on
    // fixed screen columns and are computed from their own x, so a pixel's colour depends
    // only on (x, y): partial redraws match full ones whatever the strip height or clip.
    //
    // Fields are in 1/2048ths of an output step (an end's table position has 1/16 entry
    // resolution, so ends are multiples of 8 and the per-pixel step is exact); red tops out
    // at 31 * 2048 plus the dither, inside its 16 bits.
    constexpr int kSub = 4, kMaxQ = (kSteps - 1) << kSub, kBlk = 8;
    const float sx = float(kMaxQ) / (rx * rx), sy = float(kMaxQ) / (ry * ry);
    auto colourAt = [&](float dx, float qy, uint32_t& rb, uint32_t& g) {
        float qf = dx * dx * sx + qy;
        int q = qf >= float(kMaxQ) ? kMaxQ : int(qf);
        int i = q >> kSub, f = q & ((1 << kSub) - 1);
        rb = ((lutRB[i] << kSub) + (lutRB[i + 1] - lutRB[i]) * f) << 3;
        g = ((uint32_t(lutG[i]) << kSub) + (uint32_t(lutG[i + 1]) - lutG[i]) * f) << 3;
    };
    for (int y = a.y; y < a.bottom(); ++y) {
        float dy = y + 0.5f - cy, qy = dy * dy * sy;
        uint32_t ditRB[4], ditG[4];  // the Bayer threshold in the same units
        for (int k = 0; k < 4; ++k) {
            uint32_t t = kBayer4[y & 3][k] << 7;
            ditRB[k] = (t << 16) | t, ditG[k] = t;
        }
        uint16_t* p = &at(a.x, y);
        int bx = a.x & ~(kBlk - 1);
        uint32_t rb0, g0, rb1, g1;
        colourAt(float(bx) + 0.5f - cx, qy, rb0, g0);
        for (; bx < a.right(); bx += kBlk) {
            colourAt(float(bx + kBlk) + 0.5f - cx, qy, rb1, g1);
            // Per-channel steps (a packed difference could overflow), repacked: adding the
            // packed step is exact because every field stays between its two ends.
            // Ends are multiples of 8, so the shifts divide exactly.
            static_assert(kBlk == 8, "the shifts below divide by kBlk");
            int32_t dR = (int32_t(rb1 >> 16) - int32_t(rb0 >> 16)) >> 3,
                    dB = (int32_t(rb1 & 0xFFFF) - int32_t(rb0 & 0xFFFF)) >> 3, dG = (int32_t(g1) - int32_t(g0)) >> 3;
            uint32_t drb = (uint32_t(dR) << 16) + uint32_t(dB);
            int x = std::max(bx, a.x), end = std::min(bx + kBlk, a.right());
            uint32_t rb = rb0 + drb * uint32_t(x - bx), g = g0 + uint32_t(dG) * uint32_t(x - bx);
            auto pack = [](uint32_t vrb, uint32_t vg) {
                return uint16_t(((vrb >> 16) & 0xF800) | ((vg >> 6) & 0x07E0) | ((vrb >> 11) & 0x001F));
            };
            if (end - x == kBlk) {  // a whole block: unrolled, the dither cells known
                for (int k = 0; k < kBlk; ++k, rb += drb, g += uint32_t(dG)) p[k] = pack(rb + ditRB[k & 3], g + ditG[k & 3]);
                p += kBlk;
            } else {
                for (; x < end; ++x, rb += drb, g += uint32_t(dG)) *p++ = pack(rb + ditRB[x & 3], g + ditG[x & 3]);
            }
            rb0 = rb1, g0 = g1;
        }
    }
}

namespace {
// Glow falloff (1 - d)^2 indexed by squared normalised distance d^2, scaled to 0..32767.
// Indexing by d^2 keeps the per-pixel work to a multiply-add, with no square root.
constexpr int kGlowLut = 1024;
// sqrt for the constant table below: Newton's method in double, then rounded to float (a
// correctly rounded double square root rounds to the correctly rounded float one).
constexpr float constSqrt(float v) {
    double x = v, r = v > 1 ? v : 1.0;
    for (int i = 0; i < 64; ++i) {
        double n = 0.5 * (r + x / r);
        if (n == r) break;
        r = n;
    }
    return float(r);
}

// The falloff, (1 - sqrt(r^2))^2 in 1/32767ths: a const table, so it lives in flash.
struct GlowLut {
    uint16_t v[kGlowLut];
    constexpr GlowLut() : v() {
        for (int i = 0; i < kGlowLut; ++i) {
            float f = 1.f - constSqrt((i + 0.5f) / kGlowLut);
            v[i] = uint16_t(f * f * 32767.f + 0.5f);
        }
    }
};
constexpr GlowLut kGlow{};
}  // namespace

GFX_HOT void Canvas::radialGlow(float cx, float cy, float radius, Rgb c, uint8_t peakAlpha) {
    if (radius <= 0 || peakAlpha == 0) return;
    Rect box{int(fastFloor(cx - radius)), int(fastFloor(cy - radius)), int(fastCeil(2 * radius)) + 1,
             int(fastCeil(2 * radius)) + 1};
    Rect a = box.intersect(clip_);
    if (a.empty()) return;
    // Each pixel adds the colour times the falloff, as per-channel increments in 1/16ths of an
    // RGB565 step, rounded with an ordered dither (the same 4x4 Bayer pattern as the sky, so
    // the glow fades smoothly instead of in rings) and added with saturation. The increments
    // are the shared falloff table times a per-call multiplier, so no per-colour table has to
    // be rebuilt when glows of different colours or radii alternate (the sky draws four big
    // ones per strip). The falloff wants about one entry per pixel of r^2.
    const uint16_t* lut = kGlow.v;
    int n = 32;
    while (n < kGlowLut && n < radius * radius) n *= 2;
    const int lutStep = kGlowLut / n;
    // incR = c.r * alpha / 255 * 2 (1/16ths of a 5-bit step) with alpha = peak * lut / 32767:
    // a 15-bit table value times these stays within 32 bits.
    const uint32_t mR = uint32_t(c.r) * peakAlpha * 512u / 255u, mG = uint32_t(c.g) * peakAlpha * 512u / 255u,
                   mB = uint32_t(c.b) * peakAlpha * 512u / 255u;
    const float r2 = radius * radius, scale = n / r2;
    // The table index is a quadratic along the row, stepped in 16.16 fixed point; stepping
    // restarts from exact values every 16 screen columns, so a pixel's value never depends on
    // where the clip starts (partial redraws must match full ones).
    constexpr float kOne = 65536.f;
    const float sx = scale * kOne;
    const int32_t step2 = int32_t(2.f * sx + 0.5f), nFix = n << 16;
    for (int y = a.y; y < a.bottom(); ++y) {
        float dy = y + 0.5f - cy, dy2 = dy * dy;
        if (dy2 >= r2) continue;
        float half = std::sqrt(r2 - dy2);
        int x0 = std::max(a.x, int(fastFloor(cx - half))), x1 = std::min(a.right(), int(fastCeil(cx + half)) + 1);
        if (x0 >= x1) continue;
        const uint8_t* bayer = kBayer4[y & 3];
        const float base = dy2 * sx;
        uint16_t* row = &at(x0, y);
        for (int bx = x0 & ~15; bx < x1; bx += 16) {
            float dx = bx + 0.5f - cx;
            int32_t acc = int32_t(dx * dx * sx + base), step = int32_t((2.f * dx + 1.f) * sx);
            int x = bx, end = std::min(bx + 16, x1);
            for (; x < x0; ++x) acc += step, step += step2;
            for (; x < end; ++x, acc += step, step += step2) {
                if (acc >= nFix) continue;
                uint32_t f = lut[(acc >> 16) * lutStep], t = bayer[x & 3];
                uint32_t r = (((f * mR) >> 23) + t) >> 4, g = (((f * mG) >> 22) + t) >> 4, b = (((f * mB) >> 23) + t) >> 4;
                if (!(r | g | b)) continue;
                uint32_t inc = (g << 21) | (r << 11) | b;  // already in spread565 layout
                uint16_t& p = row[x - x0];
                p = pack565(addSat565(spread565(p), inc));
            }
        }
    }
}

}  // namespace gfx
