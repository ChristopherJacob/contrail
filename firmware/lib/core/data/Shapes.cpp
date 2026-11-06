#include "Shapes.h"

#include <cmath>
#include <initializer_list>

#include "gfx/Math.h"
#include "gfx/Raster.h"

namespace data {
using namespace gfx;

namespace {
constexpr int kEllipseN = 14;  // points on an engine ellipse: round at 22 px, cheap at 13
constexpr uint8_t kLineAlpha = 140;  // prop and rotor lines: about 55% of the body alpha
// The rotor disc, in half-units: hub (50, 38) of the 100-unit box, radius 44. It reaches 0.56 * px from the
// centre of the box, inside drawTop's culling radius of px * 0.71.
constexpr float kRotorHubX = 100, kRotorHubY = 76, kRotorR = 88;

int digit37(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0' + 1;
    if (ch >= 'A' && ch <= 'Z') return ch - 'A' + 11;
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 11;
    return -1;
}

// The unit circle at kEllipseN points, cos and sin of i * 2 pi / 14 as the float std::cos and std::sin give
// them (printed with %.9g, which round-trips a float), so drawing an engine costs no trig. constexpr, so it
// lives in flash, not in RAM with a guard and an initialiser.
constexpr float kUnitCos[kEllipseN] = {1, 0.90096885f, 0.623489797f, 0.222520858f, -0.222520947f, -0.623489797f,
                                       -0.900968909f, -1, -0.90096885f, -0.623489618f, -0.222521007f, 0.222521037f,
                                       0.623490036f, 0.90096885f};
constexpr float kUnitSin[kEllipseN] = {0, 0.433883756f, 0.781831503f, 0.974927902f, 0.974927902f, 0.781831503f,
                                       0.433883607f, -8.74227766e-08f, -0.433883756f, -0.781831622f, -0.974927902f,
                                       -0.974927902f, -0.781831324f, -0.433883756f};

// An ellipse centred on (ex, ey) in half-units, mapped through `map`.
template <class Map>
void ellipse(Canvas& c, float ex, float ey, float rx, float ry, const Map& map, Rgb col, uint8_t a) {
    Vec2 pts[kEllipseN];
    for (int i = 0; i < kEllipseN; ++i) pts[i] = map(ex + rx * kUnitCos[i], ey + ry * kUnitSin[i]);
    aaPolygon(c, pts, kEllipseN, col, a);
}
}  // namespace

Family familyForType(const char* t) {
    if (!t || !t[0]) return Family::None;
    uint32_t v = 0;
    int n = 0;
    for (; t[n]; ++n) {
        int d = digit37(t[n]);
        if (n >= 4 || d < 0) return Family::None;
        v = v * 37 + uint32_t(d);
    }
    for (; n < 4; ++n) v *= 37;
    int lo = 0, hi = kTypeFamilyCount - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint32_t k = kTypeFamilies[mid] >> 4;
        if (k == v) return Family(kTypeFamilies[mid] & 0xF);
        if (k < v) lo = mid + 1;
        else hi = mid - 1;
    }
    return Family::None;
}

Family familyForCategory(uint8_t cat) {
    switch (cat) {
        case 0xA1: return Family::Single;
        case 0xA2: return Family::Bizjet;
        case 0xA3:
        case 0xA4: return Family::Narrowbody;
        case 0xA5: return Family::Widebody;
        case 0xA6: return Family::FastJet;
        case 0xA7: return Family::Helicopter;
        default: return Family::None;
    }
}

Family familyFor(const char* type, uint8_t cat) {
    Family f = familyForType(type);
    return f != Family::None ? f : familyForCategory(cat);
}

int iconPx(Family f) { return f == Family::None ? 12 : kShapes[int(f) - 1].iconPx; }

void drawTop(Canvas& c, Family f, float cx, float cy, float px, float headingDeg, Rgb col, uint8_t a) {
    if (f == Family::None) return planeGlyph(c, cx, cy, px, headingDeg, col, a);
    // The circumscribed square: the box diagonal and the rotor disc both stay inside px * 0.71.
    const float reach = px * 0.71f + 2;
    const Rect box{int(std::floor(cx - reach)), int(std::floor(cy - reach)), int(std::ceil(2 * reach)) + 1,
                   int(std::ceil(2 * reach)) + 1};
    if (!c.clip().intersects(box)) return;
    const ShapeDef& s = kShapes[int(f) - 1];
    const float k = px / 200.f;  // half-units to pixels
    const float sn = std::sin(headingDeg * kDeg), cs = std::cos(headingDeg * kDeg);
    // Half-units (0..200, centre 100, 100) to screen, turned to the heading.
    auto map = [&](float hx, float hy) {
        float dx = (hx - 100) * k, dy = (hy - 100) * k;
        return Vec2{cx + dx * cs - dy * sn, cy + dx * sn + dy * cs};
    };
    const uint8_t* p = s.data;
    Vec2 pts[2 * kMaxOutline];
    int n = 0;
    for (int i = 0; i < s.topN; ++i) pts[n++] = map(100 + p[2 * i], p[2 * i + 1]);
    for (int i = s.topN - 2; i >= 1; --i) pts[n++] = map(100 - p[2 * i], p[2 * i + 1]);  // interior points only
    aaPolygon(c, pts, n, col, a);
    p += s.topN * 2;
    for (int i = 0; i < s.topEngN; ++i, p += 4)
        for (int side : {1, -1}) ellipse(c, 100 + side * p[0], p[1], p[2], p[3], map, col, a);
    const uint8_t la = uint8_t((a * kLineAlpha + 127) / 255);
    const float lw = std::fmax(1.f, px / 60.f);
    for (int i = 0; i < s.topLineN; ++i, p += 4)
        for (int side : {1, -1}) {
            Vec2 a0 = map(100 + side * p[0], p[1]), a1 = map(100 + side * p[2], p[3]);
            aaLine(c, a0.x, a0.y, a1.x, a1.y, lw, col, la);
        }
    if (s.rotor) {
        Vec2 hub = map(kRotorHubX, kRotorHubY);
        aaRing(c, hub.x, hub.y, kRotorR * k, lw, col, la);
    }
}

void drawSide(Canvas& c, Family f, float x, float y, float w, Rgb body, Rgb engine, uint8_t a) {
    if (f == Family::None) return;
    const Rect box{int(std::floor(x)) - 2, int(std::floor(y)) - 2, int(std::ceil(w)) + 5, int(std::ceil(w / 3)) + 5};
    if (!c.clip().intersects(box)) return;
    const ShapeDef& s = kShapes[int(f) - 1];
    const float k = w / 240.f;  // half-units to pixels
    auto map = [&](float hx, float hy) { return Vec2{x + hx * k, y + hy * k}; };
    const uint8_t* p = s.data + s.topN * 2 + s.topEngN * 4 + s.topLineN * 4;
    Vec2 pts[kMaxOutline];
    for (int i = 0; i < s.sideN; ++i) pts[i] = map(p[2 * i], p[2 * i + 1]);
    aaPolygon(c, pts, s.sideN, body, a);
    p += s.sideN * 2;
    for (int i = 0; i < s.sideEngN; ++i, p += 4) ellipse(c, p[0], p[1], p[2], p[3], map, engine, a);
    const float lw = std::fmax(1.f, w / 90.f);
    for (int i = 0; i < s.sideLineN; ++i, p += 4) {
        Vec2 a0 = map(p[0], p[1]), a1 = map(p[2], p[3]);
        aaLine(c, a0.x, a0.y, a1.x, a1.y, lw, body, a);
    }
}

}  // namespace data
