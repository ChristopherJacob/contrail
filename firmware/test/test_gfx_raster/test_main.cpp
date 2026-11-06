#include <unity.h>

#include <cmath>
#include <functional>
#include <vector>

#include "gfx/Raster.h"

using namespace gfx;

void setUp() {}
void tearDown() {}

static const Rgb kWhite{255, 255, 255};

struct Frame {
    int w, h;
    std::vector<uint16_t> px;
    Frame(int w_, int h_) : w(w_), h(h_), px(w_ * h_, 0) {}
    uint16_t at(int x, int y) const { return px[y * w + x]; }
};

// Render `draw` once on a full canvas and once split into bands, return both.
static void renderBoth(int w, int h, int bandH, const std::function<void(Canvas&)>& draw, Frame& whole,
                       Frame& banded) {
    Canvas full(whole.px.data(), w, 0, h);
    draw(full);
    std::vector<uint16_t> strip(w * bandH);
    for (int y0 = 0; y0 < h; y0 += bandH) {
        int bh = std::min(bandH, h - y0);
        std::fill(strip.begin(), strip.end(), 0);
        Canvas c(strip.data(), w, y0, bh);
        draw(c);
        std::copy(strip.begin(), strip.begin() + w * bh, banded.px.begin() + y0 * w);
    }
}

static void test_line_spans_two_rows_partially() {
    Frame f(32, 32);
    Canvas c(f.px.data(), 32, 0, 32);
    aaLine(c, 2, 11.0f, 28, 11.0f, 1.0f, kWhite);  // centre on the boundary between rows 10 and 11
    uint16_t r10 = f.at(15, 10), r11 = f.at(15, 11);
    TEST_ASSERT_NOT_EQUAL(0, r10);
    TEST_ASSERT_NOT_EQUAL(0, r11);
    TEST_ASSERT_NOT_EQUAL(0xFFFF, r10);
    TEST_ASSERT_EQUAL_HEX16(0, f.at(15, 14));
}

static void test_circle_centre_edge_outside() {
    Frame f(48, 48);
    Canvas c(f.px.data(), 48, 0, 48);
    aaCircle(c, 20, 20, 5, kWhite);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, f.at(19, 19));
    TEST_ASSERT_EQUAL_HEX16(0, f.at(30, 20));
    uint16_t edge = f.at(24, 22);
    TEST_ASSERT_TRUE(edge != 0 && edge != 0xFFFF);
}

static void test_wedge_only_covers_its_sweep() {
    Frame f(64, 64);
    Canvas c(f.px.data(), 64, 0, 64);
    aaWedge(c, 32, 32, 20, 0, 90, kWhite, 255, 255);  // up -> right quadrant
    TEST_ASSERT_NOT_EQUAL(0, f.at(40, 24));            // upper right
    TEST_ASSERT_EQUAL_HEX16(0, f.at(24, 40));          // lower left
    TEST_ASSERT_EQUAL_HEX16(0, f.at(40, 40));          // lower right
}

static void test_polygon_square_exact() {
    Frame f(16, 16);
    Canvas c(f.px.data(), 16, 0, 16);
    Vec2 sq[] = {{4, 4}, {12, 4}, {12, 12}, {4, 12}};
    aaPolygon(c, sq, 4, kWhite);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, f.at(4, 4));
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, f.at(11, 11));
    TEST_ASSERT_EQUAL_HEX16(0, f.at(12, 8));
    TEST_ASSERT_EQUAL_HEX16(0, f.at(3, 8));
}

static Rect inkBox(const Frame& f) {
    int x0 = f.w, y0 = f.h, x1 = -1, y1 = -1;
    for (int y = 0; y < f.h; ++y)
        for (int x = 0; x < f.w; ++x)
            if (f.at(x, y)) x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
    return {x0, y0, x1 - x0 + 1, y1 - y0 + 1};
}

static void test_plane_glyph_rotates() {
    Frame up(64, 64), east(64, 64);
    Canvas cu(up.px.data(), 64, 0, 64), ce(east.px.data(), 64, 0, 64);
    planeGlyph(cu, 32, 32, 40, 0, kWhite);
    planeGlyph(ce, 32, 32, 40, 90, kWhite);
    Rect bu = inkBox(up), be = inkBox(east);
    TEST_ASSERT_TRUE(bu.w > bu.h * 0.95f || bu.w >= 38);  // wingspan horizontal
    TEST_ASSERT_TRUE(be.h >= 38);                           // wingspan vertical after turning east
    // Nose points east: more ink right of centre than left in the middle row.
    TEST_ASSERT_NOT_EQUAL(0, east.at(50, 32));
}

static void test_strip_rendering_matches_full_canvas() {
    auto scene = [](Canvas& c) {
        aaCircle(c, 60, 30, 25, {200, 120, 40});
        aaLine(c, 5, 5, 110, 70, 3, kWhite, 200);
        aaArc(c, 60, 40, 30, 2, 30, 200, {40, 255, 160});
        aaWedge(c, 60, 40, 35, 300, 350, {40, 255, 160}, 0, 160);
        planeGlyph(c, 80, 50, 24, 37, kWhite);
        c.radialGlow(30, 40, 15, {255, 220, 150}, 180);
    };
    Frame whole(120, 80), banded(120, 80);
    renderBoth(120, 80, 32, scene, whole, banded);
    TEST_ASSERT_EQUAL_MEMORY(whole.px.data(), banded.px.data(), whole.px.size() * 2);
}

static void test_round_rect_corners_are_cut() {
    std::vector<uint16_t> buf(240 * 32, 0);
    Canvas c(buf.data(), 240, 0, 32);
    fillRoundRect(c, {10, 2, 100, 28}, 8, kWhite);
    TEST_ASSERT_EQUAL_HEX16(0, c.get(10, 2));        // corner stays background
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, c.get(60, 16));  // interior is filled
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, c.get(10, 16));  // left edge mid-height is filled
    TEST_ASSERT_EQUAL_HEX16(0, c.get(110, 16));      // right of the rect untouched
}

// The straightforward per-pixel form of fillRoundRect: coverage for every pixel of the row.
static void referenceRoundRect(Canvas& c, const Rect& r, float radius, Rgb col, uint8_t a) {
    radius = std::fmin(radius, std::fmin(r.w, r.h) * 0.5f);
    uint16_t solid = rgb565(col);
    Rect area = r.intersect(c.clip());
    for (int y = area.y; y < area.bottom(); ++y) {
        float yc = y + 0.5f, dy = 0;
        if (yc < r.y + radius) dy = r.y + radius - yc;
        else if (yc > r.bottom() - radius) dy = yc - (r.bottom() - radius);
        float inset = dy > 0 ? radius - std::sqrt(std::fmax(0.f, radius * radius - dy * dy)) : 0.f;
        float xl = r.x + inset, xr = r.right() - inset;
        for (int x = area.x; x < area.right(); ++x) {
            float cov = std::fmin(x + 1.f, xr) - std::fmax(float(x), xl);
            if (cov <= 0) continue;
            uint8_t pa = uint8_t(a * std::fmin(cov, 1.f) + 0.5f);
            c.at(x, y) = pa == 255 ? solid : blend565(c.at(x, y), solid, pa);
        }
    }
}

static void test_round_rect_matches_reference_over_background() {
    struct Case { Rect r; float radius; uint8_t a; };
    const Case cases[] = {{{10, 2, 100, 28}, 8, 170}, {{7, 3, 61, 25}, 9, 215}, {{20, 5, 4, 14}, 2, 255},
                          {{0, 0, 30, 30}, 15, 90},   {{-5, -4, 50, 40}, 9, 128}, {{30, 6, 40, 12}, 20, 40},
                          {{12, 4, 80, 20}, 6, 3},    {{15, 1, 2, 2}, 1, 200}};
    for (const Case& k : cases) {
        std::vector<uint16_t> fast(120 * 40), ref(120 * 40);
        for (size_t i = 0; i < fast.size(); ++i) fast[i] = ref[i] = uint16_t(i * 2654435761u >> 11);  // busy background
        Canvas cf(fast.data(), 120, 0, 40), cr(ref.data(), 120, 0, 40);
        fillRoundRect(cf, k.r, k.radius, {200, 90, 30}, k.a);
        referenceRoundRect(cr, k.r, k.radius, {200, 90, 30}, k.a);
        TEST_ASSERT_EQUAL_MEMORY(ref.data(), fast.data(), fast.size() * 2);
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_line_spans_two_rows_partially);
    RUN_TEST(test_circle_centre_edge_outside);
    RUN_TEST(test_wedge_only_covers_its_sweep);
    RUN_TEST(test_polygon_square_exact);
    RUN_TEST(test_plane_glyph_rotates);
    RUN_TEST(test_strip_rendering_matches_full_canvas);
    RUN_TEST(test_round_rect_corners_are_cut);
    RUN_TEST(test_round_rect_matches_reference_over_background);
    return UNITY_END();
}
