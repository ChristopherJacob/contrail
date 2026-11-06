#include <unity.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <vector>

#include "gfx/Canvas.h"
#include "gfx/Dither.h"

using namespace gfx;

void setUp() {}
void tearDown() {}

static void test_rgb565_packing() {
    TEST_ASSERT_EQUAL_HEX16(0xF800, rgb565(255, 0, 0));
    TEST_ASSERT_EQUAL_HEX16(0x07E0, rgb565(0, 255, 0));
    TEST_ASSERT_EQUAL_HEX16(0x001F, rgb565(0, 0, 255));
    Rgb w = unpack565(0xFFFF);
    TEST_ASSERT_EQUAL(255, w.r);
    TEST_ASSERT_EQUAL(255, w.g);
}

static void test_pixel_outside_band_ignored() {
    std::vector<uint16_t> buf(240 * 32, 0);
    Canvas c(buf.data(), 240, 32, 32);
    c.pixel(10, 10, 0xFFFF);  // above band
    c.pixel(10, 70, 0xFFFF);  // below band
    for (auto p : buf) TEST_ASSERT_EQUAL_HEX16(0, p);
    c.pixel(10, 40, 0xFFFF);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, c.get(10, 40));
}

static void test_fill_clipped_to_band() {
    std::vector<uint16_t> buf(240 * 32, 0);
    Canvas c(buf.data(), 240, 32, 32);
    c.fill({0, 50, 240, 100}, 0x1234);
    TEST_ASSERT_EQUAL_HEX16(0, c.get(0, 49));
    TEST_ASSERT_EQUAL_HEX16(0x1234, c.get(0, 50));
    TEST_ASSERT_EQUAL_HEX16(0x1234, c.get(239, 63));
}

static void test_blend_extremes() {
    Rgb red{255, 0, 0};
    TEST_ASSERT_EQUAL_HEX16(0x1234, blend565(0x1234, red, 0));
    TEST_ASSERT_EQUAL_HEX16(rgb565(red), blend565(0x1234, red, 255));
}

static void test_dithered_gradient_removes_banding() {
    std::vector<uint16_t> buf(32 * 32, 0);
    Canvas c(buf.data(), 32, 0, 32);
    Rgb stops[] = {{0, 0, 0}, {0, 0, 40}};
    float pos[] = {0.f, 1.f};
    c.vGradient({0, 0, 32, 32}, stops, pos, 2);
    float prev = -1;
    for (int y = 0; y < 32; y += 4) {
        float sum = 0;
        for (int yy = y; yy < y + 4; ++yy)
            for (int x = 0; x < 32; ++x) sum += c.get(x, yy) & 0x1F;
        TEST_ASSERT_TRUE(sum >= prev);
        prev = sum;
    }
    // A mid-gradient 4x4 cell must mix at least two quantised levels.
    std::set<uint16_t> levels;
    for (int y = 12; y < 16; ++y)
        for (int x = 0; x < 4; ++x) levels.insert(c.get(x, y));
    TEST_ASSERT_TRUE(levels.size() >= 2);
}

static void test_radial_glow_falloff() {
    std::vector<uint16_t> buf(64 * 64, 0);
    Canvas c(buf.data(), 64, 0, 64);
    c.radialGlow(32, 32, 10, {255, 255, 255}, 255);
    Rgb centre = unpack565(c.get(32, 32));
    Rgb edge = unpack565(c.get(40, 32));
    TEST_ASSERT_TRUE(centre.r > edge.r);
    TEST_ASSERT_EQUAL_HEX16(0, c.get(32 + 12, 32));
}

static void test_radial_gradient_centre_and_edge() {
    std::vector<uint16_t> buf(240 * 32, 0);
    Canvas c(buf.data(), 240, 0, 32);
    Rgb stops[] = {{200, 40, 40}, {40, 40, 200}};
    float pos[] = {0, 1};
    c.radialGradient({0, 0, 240, 32}, 120, 16, 100, 100, stops, pos, 2);
    Rgb mid = unpack565(c.get(120, 16));
    TEST_ASSERT_INT_WITHIN(12, 200, mid.r);
    Rgb edge = unpack565(c.get(0, 0));  // beyond the radius: last stop
    TEST_ASSERT_INT_WITHIN(12, 200, edge.b);
    TEST_ASSERT_INT_WITHIN(12, 40, edge.r);
}

// Screens redraw damaged strips of any height and column span; a pixel must come out the same
// as in a full redraw. The off-centre gradient reaches far outside its ellipse in the corners.
static void test_radial_gradient_partial_matches_full() {
    Rgb stops[] = {{12, 30, 70}, {40, 90, 150}, {230, 140, 90}};
    float pos[] = {0, 0.58f, 1};
    const float cxs[] = {120, 60}, cys[] = {140, 40}, rxs[] = {116, 50}, rys[] = {136, 60};
    for (int g = 0; g < 2; ++g) {
        std::vector<uint16_t> full(240 * 320, 0);
        Canvas f(full.data(), 240, 0, 320);
        f.radialGradient({0, 0, 240, 320}, cxs[g], cys[g], rxs[g], rys[g], stops, pos, 3);
        const int heights[] = {1, 7, 16, 40};
        unsigned seed = 1;
        for (int h : heights) {
            for (int y0 = 0; y0 < 320; y0 += h) {
                int bh = std::min(h, 320 - y0);
                std::vector<uint16_t> strip(240 * bh, 0);
                Canvas c(strip.data(), 240, y0, bh);
                seed = seed * 1103515245u + 12345u;
                int x0 = (seed >> 8) % 200, w = 1 + (seed >> 16) % (240 - x0);
                // The rect passed differs per call too (the sky passes per-chunk rects).
                c.radialGradient({x0, y0, w, bh}, cxs[g], cys[g], rxs[g], rys[g], stops, pos, 3);
                for (int y = y0; y < y0 + bh; ++y)
                    for (int x = x0; x < x0 + w; ++x)
                        if (c.get(x, y) != full[y * 240 + x]) {
                            char msg[64];
                            std::snprintf(msg, sizeof msg, "g%d h%d (%d,%d)", g, h, x, y);
                            TEST_FAIL_MESSAGE(msg);
                        }
            }
        }
    }
}

// Averaged over the dither, the colour must follow the stops smoothly out from the centre: a
// table indexed by r^2 is coarse in r there, and picking the nearest entry drew rings.
static void test_radial_gradient_no_rings_at_centre() {
    std::vector<uint16_t> buf(240 * 320, 0);
    Canvas c(buf.data(), 240, 0, 320);
    Rgb stops[] = {{0, 0, 0}, {160, 160, 160}};
    float pos[] = {0, 1};
    const float cx = 120, cy = 160, rx = 116;
    c.radialGradient({0, 0, 240, 320}, cx, cy, rx, rx, stops, pos, 2);
    float worst = 0;
    for (int bx = 120; bx < 120 + 100; bx += 4) {  // 4x4 blocks (one dither tile) along +x
        float sum = 0, rsum = 0;
        for (int y = 160; y < 164; ++y)
            for (int x = bx; x < bx + 4; ++x) {
                Rgb p = unpack565(c.get(x, y));
                sum += p.g;
                float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
                rsum += std::sqrt(dx * dx + dy * dy) / rx * 160.f;
            }
        worst = std::fmax(worst, std::fabs(sum - rsum) / 16.f);
    }
    char msg[48];
    std::snprintf(msg, sizeof msg, "worst block error %.2f levels", worst);
    TEST_ASSERT_TRUE_MESSAGE(worst < 4.f, msg);  // under one 6-bit step (nearest entry: ~9)
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_rgb565_packing);
    RUN_TEST(test_pixel_outside_band_ignored);
    RUN_TEST(test_fill_clipped_to_band);
    RUN_TEST(test_blend_extremes);
    RUN_TEST(test_dithered_gradient_removes_banding);
    RUN_TEST(test_radial_glow_falloff);
    RUN_TEST(test_radial_gradient_centre_and_edge);
    RUN_TEST(test_radial_gradient_partial_matches_full);
    RUN_TEST(test_radial_gradient_no_rings_at_centre);
    return UNITY_END();
}
