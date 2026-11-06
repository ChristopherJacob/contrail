#include "Raster.h"

#ifdef CONTRAIL_ESP32
// Per-pixel loops: the firmware is built with -Os, which roughly halves their speed.
#pragma GCC optimize("O2")
#endif

#include <algorithm>
#include <climits>
#include <cmath>

#include "Dither.h"
#include "Hot.h"
#include "Math.h"

namespace gfx {
namespace {

constexpr int kMaxPolyWidth = 320;

Rect boundsOf(float x0, float y0, float x1, float y1, float pad) {
    int ix0 = int(fastFloor(std::min(x0, x1) - pad)), iy0 = int(fastFloor(std::min(y0, y1) - pad));
    int ix1 = int(fastCeil(std::max(x0, x1) + pad)), iy1 = int(fastCeil(std::max(y0, y1) + pad));
    return {ix0, iy0, ix1 - ix0 + 1, iy1 - iy0 + 1};
}

inline float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

inline void put(Canvas& c, int x, int y, Rgb col, uint8_t a, float cov) {
    if (cov <= 0) return;
    uint8_t al = cov >= 1 ? a : uint8_t(a * cov + 0.5f);
    if (al) c.at(x, y) = blend565(c.at(x, y), col, al);
}

// atan2 via a minimax polynomial (error < 1e-5 rad): several times cheaper than libm,
// which matters in per-pixel loops.
inline float fastAtan2(float y, float x) {
    float ax = std::fabs(x), ay = std::fabs(y);
    float mx = std::max(ax, ay), mn = std::min(ax, ay);
    if (mx == 0) return 0;
    float a = mn / mx, s = a * a;
    float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax) r = 1.57079637f - r;
    if (x < 0) r = 3.14159274f - r;
    return y < 0 ? -r : r;
}

// Compass angle (clockwise from up) of a vector, in 0..360.
inline float compass(float dx, float dy) {
    float d = fastAtan2(dx, -dy) / kDeg;
    return d < 0 ? d + 360.f : d;
}

// Pixel columns of row y (centre py) that can lie within `outer` of (cx, cy), clipped to
// b. Returns false if none do.
inline bool diskSpan(const Rect& b, float cx, float py, float cy, float outer, int& x0, int& x1) {
    float dy = py - cy, rem = outer * outer - dy * dy;
    if (rem <= 0) return false;
    float half = std::sqrt(rem);
    x0 = std::max(b.x, int(fastFloor(cx - half)));
    x1 = std::min(b.right(), int(fastCeil(cx + half)) + 1);
    return x0 < x1;
}

// Signed angular distance of `ang` inside the [a0, a1] sweep (positive inside), in degrees.
inline float insideSweep(float ang, float a0, float a1) {
    float span = a1 - a0;
    if (span >= 360.f) return 360.f;
    float rel = wrap360(ang - a0);
    if (rel <= span) return std::min(rel, span - rel);
    return -std::min(rel - span, 360.f - rel);
}

}  // namespace

GFX_HOT void aaLine(Canvas& c, float x0, float y0, float x1, float y1, float width, Rgb col, uint8_t a) {
    float hw = width * 0.5f;
    Rect r = boundsOf(x0, y0, x1, y1, hw + 1).intersect(c.clip());
    if (r.empty()) return;  // outside this block: skip the square root and divisions below
    float dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
    float invLen2 = len2 > 0 ? 1.f / len2 : 0.f, invLen = len2 > 0 ? 1.f / std::sqrt(len2) : 0.f;
    // A pixel is covered when its centre is nearer the segment than reach = hw + 0.5. The rows
    // and columns visited, and the square roots taken, keep to that reach (plus a hair for
    // rounding); everything skipped would have had no coverage.
    const float reach = hw + 0.5f, m = reach + 0.01f, far2 = (reach + 0.01f) * (reach + 0.01f);
    for (int y = r.y; y < r.bottom(); ++y) {
        float py = y + 0.5f;
        // Columns this row can touch: the nearest point of the segment is within m rows of py,
        // and the pixel's centre within m columns of it.
        int xa = r.x, xb = r.right();
        if (dy != 0) {
            float ta = std::max(0.f, std::min(1.f, (py - m - y0) / dy));
            float tb = std::max(0.f, std::min(1.f, (py + m - y0) / dy));
            float ex0 = x0 + dx * ta, ex1 = x0 + dx * tb;
            xa = std::max(r.x, int(fastFloor(std::min(ex0, ex1) - m - 0.5f)));
            xb = std::min(r.right(), int(fastCeil(std::max(ex0, ex1) + m - 0.5f)) + 1);
        }
        for (int x = xa; x < xb; ++x) {
            float px = x + 0.5f;
            float t = ((px - x0) * dx + (py - y0) * dy) * invLen2;
            float d;
            if (t > 0 && t < 1) {
                // Beside the segment the distance is the perpendicular one: no square root.
                d = std::fabs((px - x0) * dy - (py - y0) * dx) * invLen;
            } else {
                t = clamp01(t);
                float ex = px - (x0 + t * dx), ey = py - (y0 + t * dy), e2 = ex * ex + ey * ey;
                if (e2 > far2) continue;  // beyond the end cap
                d = std::sqrt(e2);
            }
            put(c, x, y, col, a, hw + 0.5f - d);
        }
    }
}

GFX_HOT void aaCircle(Canvas& c, float cx, float cy, float r, Rgb col, uint8_t a) {
    Rect b = boundsOf(cx, cy, cx, cy, r + 1).intersect(c.clip());
    for (int y = b.y; y < b.bottom(); ++y) {
        float dy = y + 0.5f - cy;
        int x0, x1;
        if (!diskSpan(b, cx, y + 0.5f, cy, r + 1.f, x0, x1)) continue;
        for (int x = x0; x < x1; ++x) {
            float dx = x + 0.5f - cx;
            put(c, x, y, col, a, r + 0.5f - std::sqrt(dx * dx + dy * dy));
        }
    }
}

void aaRing(Canvas& c, float cx, float cy, float r, float thickness, Rgb col, uint8_t a) {
    aaArc(c, cx, cy, r, thickness, 0, 360, col, a);
}

GFX_HOT void aaArc(Canvas& c, float cx, float cy, float r, float thickness, float a0, float a1, Rgb col, uint8_t a) {
    float ht = thickness * 0.5f;
    Rect b = boundsOf(cx, cy, cx, cy, r + ht + 1).intersect(c.clip());
    bool full = a1 - a0 >= 360.f;
    float inner = r - ht - 1.f;
    // Every pixel visited lies within ht + 2 of the ring, so its distance from the centre
    // comes from a series about r instead of a square root (~100 cycles on the ESP32);
    // the error is below 0.01 px for rings of radius 16 and up.
    const bool series = r >= 16.f;
    const float ir = 1.f / r, ir3 = ir * ir * ir, r2 = r * r;
    for (int y = b.y; y < b.bottom(); ++y) {
        float dy = y + 0.5f - cy;
        int x0, x1;
        if (!diskSpan(b, cx, y + 0.5f, cy, r + ht + 1.f, x0, x1)) continue;
        // Skip the hollow middle of the ring on rows that cross it.
        int h0 = x1, h1 = x1;
        if (inner > 0 && dy * dy < inner * inner) {
            float half = std::sqrt(inner * inner - dy * dy);
            h0 = int(fastCeil(cx - half)), h1 = int(fastFloor(cx + half));
        }
        for (int x = x0; x < x1; ++x) {
            if (x >= h0 && x < h1) x = h1;
            float dx = x + 0.5f - cx;
            float d2 = dx * dx + dy * dy, e = d2 - r2;
            float d = series ? r + e * 0.5f * ir - e * e * 0.125f * ir3 : std::sqrt(d2);
            float cov = ht + 0.5f - std::fabs(d - r);
            if (cov <= 0) continue;
            if (!full) {
                float inside = insideSweep(compass(dx, dy), a0, a1) * kDeg * d;  // arc length in px
                cov = std::min(cov, inside + 0.5f);
            }
            put(c, x, y, col, a, cov);
        }
    }
}

GFX_HOT void aaWedge(Canvas& c, float cx, float cy, float r, float a0, float a1, Rgb col, uint8_t aStart,
             uint8_t aEnd) {
    Rect b = boundsOf(cx, cy, cx, cy, r + 1).intersect(c.clip());
    float span = a1 - a0;
    if (span <= 0 || b.empty()) return;
    // Edges of the sweep as unit vectors. Their cross products with a pixel's offset are
    // its perpendicular distances to the edge lines, which give edge coverage directly;
    // the angle (for the alpha ramp) comes from the offset relative to the a0 edge.
    float e0x = std::sin(a0 * kDeg), e0y = -std::cos(a0 * kDeg);
    float e1x = std::sin(a1 * kDeg), e1y = -std::cos(a1 * kDeg);
    bool narrow = span <= 180.f;
    float invSpan = 1.f / (span * kDeg);
    float inner2 = (r - 0.5f) * (r - 0.5f);
    const uint16_t solid = rgb565(col);
    const uint32_t src = spread565(solid);
    // blend565 keeps 32 alpha levels, which bands a slow ramp into visible steps, so the
    // level is chosen with an ordered dither (alpha 255 still maps to fully opaque).
    auto plot = [&](int x, int y, int level) {
        if (level <= 0) return;
        uint16_t& p = c.at(x, y);
        if (level >= 32) {
            p = solid;
            return;
        }
        uint32_t d = spread565(p);
        p = pack565((d + (((src - d) * uint32_t(level)) >> 5)) & 0x07E0F81Fu);
    };
    // Raw angle from the a0 edge (radians, clockwise), continuous across both edges.
    auto angleAt = [&](float dx, float dy) { return fastAtan2(e0x * dy - e0y * dx, e0x * dx + e0y * dy); };
    // The full per-pixel evaluation: edges, rim and ramp.
    auto exact = [&](int x, int y, float dx, float dy) {
        float s0 = e0x * dy - e0y * dx;  // > 0: clockwise of the a0 edge
        float s1 = e1y * dx - e1x * dy;  // > 0: anticlockwise of the a1 edge
        if (narrow && (s0 < -0.5f || s1 < -0.5f)) return;
        float d2 = dx * dx + dy * dy;
        float cov = d2 <= inner2 ? 1.f : r + 0.5f - std::sqrt(d2);
        if (cov <= 0) return;
        // Angle from the a0 edge, clockwise, in 0..2pi.
        float ang = fastAtan2(s0, e0x * dx + e0y * dy);
        // Narrow sweeps: a negative angle is the anti-aliased fringe just outside the
        // a0 edge, which belongs to the aStart end (wrapping it would light it at aEnd).
        if (ang < 0) ang = narrow ? 0.f : ang + 2 * 3.14159265f;
        float t = ang * invSpan;
        if (narrow) {
            cov = std::min(cov, std::min(s0, s1) + 0.5f);
        } else if (t > 1.f) {
            return;  // wide sweeps: outside by angle (edges left hard)
        }
        if (cov <= 0) return;
        float al = (aStart + (float(aEnd) - aStart) * std::min(t, 1.f)) * std::min(cov, 1.f);
        plot(x, y, int(al * (32.f / 255.f) + kBayer4[y & 3][x & 3] * (1.f / 16.f)));
    };

    // Distance from the centre for pixels at the rim, without a square root (which costs
    // ~100 cycles on the ESP32): a series about r, accurate to ~0.01 px within 2 px of
    // the rim for r >= 16.
    const float ir = 1.f / r, ir3 = ir * ir * ir, r2 = r * r;
    auto nearRadius = [&](float d2) {
        if (r < 16.f) return std::sqrt(d2);
        float e = d2 - r2;
        return r + e * 0.5f * ir - e * e * 0.125f * ir3;
    };
    // Fully covered pixels of a narrow sweep (inside both edges and the rim) take a fast
    // path: the angle is computed exactly every kBlock screen columns and interpolated in
    // between, and the alpha level is stepped in 16.16 fixed point. Anchoring the blocks
    // to screen columns keeps strips and partial redraws identical to a full draw. Near
    // the apex the angle curves too fast to interpolate, so those pixels stay exact.
    constexpr int kBlock = 8;
    constexpr float kNear = 16.f;
    const bool fast = narrow && span <= 150.f;
    // Alpha in 1/16ths of a blend level (level = (F + bayer) >> 4), and its clamp range.
    const float fScale = 512.f / 255.f * 65536.f;
    const int32_t fLo = int32_t(std::min(aStart, aEnd) * fScale), fHi = int32_t(std::max(aStart, aEnd) * fScale);
    const float fA = aStart * fScale, fD = (float(aEnd) - aStart) * invSpan * fScale;
    for (int y = b.y; y < b.bottom(); ++y) {
        float dy = y + 0.5f - cy;
        int x0, x1;
        if (!diskSpan(b, cx, y + 0.5f, cy, r + 1.f, x0, x1)) continue;
        // Narrow sweeps are convex: each edge's half-plane bounds the row's columns, so
        // only the pixels between the edges are visited (one spare column either side
        // keeps the per-pixel test the one that decides).
        int ia = x1, ib = x1, na = x1, nb = x1;  // fast columns [ia, ib) minus [na, nb)
        if (narrow) {
            auto bound = [&](float a, float k, float th, int& lo, int& hi) {  // a + k * dx >= th
                if (k == 0) {
                    if (a < th) hi = lo;
                    return;
                }
                float xe = cx - 0.5f + (th - a) / k;
                if (k > 0) lo = std::max(lo, int(fastFloor(xe)) - 1);
                else hi = std::min(hi, int(fastCeil(xe)) + 2);
            };
            bound(e0x * dy, -e0y, -0.5f, x0, x1);
            bound(-e1x * dy, e1y, -0.5f, x0, x1);
            float rim = inner2 - dy * dy;
            if (fast && rim > 0) {
                float h = std::sqrt(rim);
                ia = std::max(x0, int(fastCeil(cx - 0.5f - h)) + 1);
                ib = std::min(x1, int(fastFloor(cx - 0.5f + h)));
                // Same bounds, pulled in: columns where both edge distances are >= 0.5.
                bound(e0x * dy, -e0y, 0.5f, ia, ib);
                bound(-e1x * dy, e1y, 0.5f, ia, ib);
                ia += 2, ib -= 2;  // the bound pads outwards by up to two columns
                if (dy * dy < kNear * kNear) {
                    float hn = std::sqrt(kNear * kNear - dy * dy);
                    na = int(fastFloor(cx - 0.5f - hn)) - 1, nb = int(fastCeil(cx - 0.5f + hn)) + 2;
                }
            }
        }
        if (ib <= ia) ia = ib = x1;
        const uint8_t* bayer = kBayer4[y & 3];
        int cached = INT32_MIN;  // block whose right anchor angle is in angB
        float angB = 0;
        for (int x = x0; x < x1; ++x) {
            if (!fast || (x >= na && x < nb)) {
                exact(x, y, x + 0.5f - cx, dy);
                continue;
            }
            // A run to the end of this block (or the apex exclusion): the angle comes from
            // the block's anchors, the coverage is 1 inside [ia, ib) and exact elsewhere.
            int bx = x & ~(kBlock - 1);
            int end = std::min(bx + kBlock, x1);
            if (x < na) end = std::min(end, na);
            float angA = bx == cached ? angB : angleAt(bx + 0.5f - cx, dy);
            angB = angleAt(bx + kBlock + 0.5f - cx, dy), cached = bx + kBlock;
            int32_t f = int32_t(fA + fD * angA), df = int32_t(fD * (angB - angA) * (1.f / kBlock));
            f += df * (x - bx);
            for (; x < end; ++x, f += df) {
                int32_t fc = f < fLo ? fLo : f > fHi ? fHi : f;
                if (x >= ia && x < ib) {
                    plot(x, y, ((fc >> 16) + bayer[x & 3]) >> 4);
                    continue;
                }
                float dx = x + 0.5f - cx;
                float cov = std::min(e0x * dy - e0y * dx, e1y * dx - e1x * dy) + 0.5f;
                if (cov <= 0) continue;
                float d2 = dx * dx + dy * dy;
                if (d2 > inner2) cov = std::min(cov, r + 0.5f - nearRadius(d2));
                if (cov <= 0) continue;
                if (cov < 1.f) fc = int32_t(float(fc) * cov);
                plot(x, y, ((fc >> 16) + bayer[x & 3]) >> 4);
            }
            --x;  // the outer loop's ++x
        }
    }
}

GFX_HOT void aaPolygon(Canvas& c, const Vec2* pts, int n, Rgb col, uint8_t a) {
    if (n < 3) return;
    float minx = pts[0].x, maxx = pts[0].x, miny = pts[0].y, maxy = pts[0].y;
    for (int i = 1; i < n; ++i) {
        minx = std::min(minx, pts[i].x), maxx = std::max(maxx, pts[i].x);
        miny = std::min(miny, pts[i].y), maxy = std::max(maxy, pts[i].y);
    }
    Rect b = boundsOf(minx, miny, maxx, maxy, 1).intersect(c.clip());
    if (b.empty() || b.w > kMaxPolyWidth) return;
    // Coverage in 1/kFull units per pixel, accumulated as a difference array so a span
    // costs O(1) per sub-scanline: whole pixels go in `run`, partial ends in `edge`.
    constexpr int kSub = 4, kUnit = 256, kFull = kSub * kUnit;
    int32_t run[kMaxPolyWidth + 2];
    int32_t edge[kMaxPolyWidth + 2];
    float xs[64];
    uint16_t near[64];  // the edges that reach this row (shapes here have a few dozen at most)
    const uint16_t solid = rgb565(col);
    for (int y = b.y; y < b.bottom(); ++y) {
        std::fill(run, run + b.w + 2, 0);
        std::fill(edge, edge + b.w + 2, 0);
        // An edge crosses sub-scanline sy when it spans it (min y <= sy < max y): only the edges
        // spanning some of this row's four are tested below, in the same order, so the crossings
        // and their x come out as if every edge were tested.
        const float sy0 = y + 0.5f / kSub, sy1 = y + (kSub - 0.5f) / kSub;
        int ne = 0;
        for (int i = 0, j = n - 1; i < n && ne < 64; j = i++) {
            const float lo = std::min(pts[i].y, pts[j].y), hi = std::max(pts[i].y, pts[j].y);
            if (lo <= sy1 && hi > sy0) near[ne++] = uint16_t(i);
        }
        for (int s = 0; s < kSub; ++s) {
            float sy = y + (s + 0.5f) / kSub;
            int nx = 0;
            for (int e = 0; e < ne && nx < 64; ++e) {
                const int i = near[e], j = i ? i - 1 : n - 1;
                const Vec2 &p = pts[i], &q = pts[j];
                if ((p.y <= sy) != (q.y <= sy)) xs[nx++] = p.x + (sy - p.y) * (q.x - p.x) / (q.y - p.y);
            }
            for (int k = 1; k < nx; ++k)  // insertion sort: a handful of crossings
                for (int m = k; m > 0 && xs[m] < xs[m - 1]; --m) std::swap(xs[m], xs[m - 1]);
            for (int k = 0; k + 1 < nx; k += 2) {
                float xa = std::max(xs[k], float(b.x)), xb = std::min(xs[k + 1], float(b.right()));
                if (xb <= xa) continue;
                int ia = int(fastFloor(xa)), ib = int(fastFloor(xb));
                if (ia == ib) {
                    edge[ia - b.x] += int32_t((xb - xa) * kUnit + 0.5f);
                    continue;
                }
                edge[ia - b.x] += int32_t((ia + 1 - xa) * kUnit + 0.5f);
                run[ia + 1 - b.x] += kUnit;
                run[ib - b.x] -= kUnit;
                if (ib < b.right()) edge[ib - b.x] += int32_t((xb - ib) * kUnit + 0.5f);
            }
        }
        int32_t acc = 0;
        uint16_t* row = &c.at(b.x, y);
        for (int i = 0; i < b.w; ++i) {
            acc += run[i];
            int32_t cov = acc + edge[i];
            if (cov <= 0) continue;
            if (cov >= kFull && a == 255) row[i] = solid;
            else row[i] = blend565(row[i], solid, uint8_t(uint32_t(a) * std::min(cov, int32_t(kFull)) / kFull));
        }
    }
}

void planeGlyph(Canvas& c, float cx, float cy, float size, float headingDeg, Rgb col, uint8_t a) {
    // Top-down airliner, nose at -y, unit wingspan 1.0 (x in -0.5..0.5).
    static const Vec2 kShape[] = {
        {0.00f, -0.50f}, {0.04f, -0.44f}, {0.05f, -0.14f}, {0.50f, 0.06f},  {0.50f, 0.12f},
        {0.05f, 0.02f},  {0.04f, 0.30f},  {0.18f, 0.42f},  {0.18f, 0.47f},  {0.02f, 0.42f},
        {0.00f, 0.48f},  {-0.02f, 0.42f}, {-0.18f, 0.47f}, {-0.18f, 0.42f}, {-0.04f, 0.30f},
        {-0.05f, 0.02f}, {-0.50f, 0.12f}, {-0.50f, 0.06f}, {-0.05f, -0.14f}, {-0.04f, -0.44f},
    };
    constexpr int kN = sizeof(kShape) / sizeof(kShape[0]);
    float s = std::sin(headingDeg * kDeg), co = std::cos(headingDeg * kDeg);
    Vec2 pts[kN];
    for (int i = 0; i < kN; ++i) {
        float x = kShape[i].x * size, y = kShape[i].y * size;
        pts[i] = {cx + x * co - y * s, cy + x * s + y * co};
    }
    aaPolygon(c, pts, kN, col, a);
}

GFX_HOT void fillRoundRect(Canvas& c, const Rect& r, float radius, Rgb col, uint8_t a) {
    Rect area = r.intersect(c.clip());
    if (area.empty() || a == 0) return;
    radius = std::fmin(radius, std::fmin(r.w, r.h) * 0.5f);  // larger radii would overlap top and bottom
    uint16_t solid = rgb565(col);
    for (int y = area.y; y < area.bottom(); ++y) {
        float yc = y + 0.5f, dy = 0;
        if (yc < r.y + radius) dy = r.y + radius - yc;
        else if (yc > r.bottom() - radius) dy = yc - (r.bottom() - radius);
        float inset = dy > 0 ? radius - std::sqrt(std::fmax(0.f, radius * radius - dy * dy)) : 0.f;
        float xl = r.x + inset, xr = r.right() - inset;
        // Only the two end pixels of a row have fractional coverage; the span between is solid.
        const int e0 = int(fastFloor(xl)), e1 = int(fastCeil(xr)) - 1;
        uint16_t* row = &c.at(area.x, y) - area.x;
        auto edge = [&](int x) {
            if (x < area.x || x >= area.right()) return;
            float cov = std::fmin(x + 1.f, xr) - std::fmax(float(x), xl);
            if (cov <= 0) return;
            uint8_t pa = uint8_t(a * std::fmin(cov, 1.f) + 0.5f);
            row[x] = pa == 255 ? solid : blend565(row[x], solid, pa);
        };
        edge(e0);
        if (e1 > e0) edge(e1);
        int x0 = std::max(area.x, e0 + 1), x1 = std::min(area.right(), e1);
        if (x0 >= x1) continue;
        if (a == 255) {
            for (int x = x0; x < x1; ++x) row[x] = solid;
        } else {
            uint32_t a5 = (uint32_t(a) + 4) >> 3;  // same quantisation as blend565
            if (a5 >= 32) {
                for (int x = x0; x < x1; ++x) row[x] = solid;
            } else if (a5) {
                uint32_t sp = spread565(solid);
                for (int x = x0; x < x1; ++x) {
                    uint32_t d = spread565(row[x]);
                    row[x] = pack565((d + (((sp - d) * a5) >> 5)) & 0x07E0F81Fu);
                }
            }
        }
    }
}

}  // namespace gfx
