#include <unity.h>

#include "FrameCapture.h"
#include "gfx/Raster.h"
#include "gfx/StripRenderer.h"
#include "gfx/fonts/Fonts.h"

using namespace gfx;

void setUp() {}
void tearDown() {}

static uint16_t bufA[240 * 32], bufB[240 * 32];

// Reports a fixed damage rect once, then nothing.
class OneShotLayer : public Layer {
   public:
    Rect dmg;
    bool fired = false;
    int draws = 0;
    void update(float, DamageList& d) override {
        if (!fired) d.add(dmg), fired = true;
    }
    void draw(Canvas&) override { ++draws; }
};

class SceneLayer : public Layer {
   public:
    void update(float, DamageList&) override {}
    void draw(Canvas& c) override {
        Rgb stops[] = {{8, 16, 48}, {40, 70, 140}, {230, 130, 80}};
        float pos[] = {0.f, 0.6f, 1.f};
        c.vGradient({0, 0, 240, 320}, stops, pos, 3);
        c.radialGlow(170, 120, 40, {255, 230, 180}, 160);
        planeGlyph(c, 170, 120, 44, 70, {255, 255, 255});
        aaRing(c, 120, 250, 50, 1.2f, {120, 255, 180}, 180);
        drawText(c, kFontHero, 16, 60, "UAL1423", {255, 255, 255});
        drawText(c, kFontBody, 16, 80, "SFO → ORD · 35,000 ft", {210, 225, 255});
    }
};

static void test_first_frame_pushes_all_strips() {
    FrameCapture d;
    StripRenderer r(d, bufA, bufB, 32);
    OneShotLayer l;
    Layer* layers[] = {&l};
    r.setLayers(layers, 1);
    TEST_ASSERT_EQUAL(10, r.frame(0.016f));
}

static void test_only_damaged_strips_pushed() {
    FrameCapture d;
    StripRenderer r(d, bufA, bufB, 32);
    OneShotLayer l;
    Layer* layers[] = {&l};
    r.setLayers(layers, 1);
    r.frame(0.016f);  // initial full frame consumes forceFull and the one-shot damage
    l.fired = false;
    l.dmg = {10, 40, 20, 10};
    d.pushedRects.clear();
    TEST_ASSERT_EQUAL(1, r.frame(0.016f));
    TEST_ASSERT_EQUAL(40, d.pushedRects[0].y);  // the damaged rows of the strip, not all of it
    TEST_ASSERT_EQUAL(10, d.pushedRects[0].h);
}

static void test_no_damage_pushes_nothing() {
    FrameCapture d;
    StripRenderer r(d, bufA, bufB, 32);
    OneShotLayer l;
    l.fired = true;
    Layer* layers[] = {&l};
    r.setLayers(layers, 1);
    r.frame(0.016f);
    TEST_ASSERT_EQUAL(0, r.frame(0.016f));
}

static void test_damage_pushes_only_its_columns() {
    FrameCapture d;
    StripRenderer r(d, bufA, bufB, 32);
    OneShotLayer l;
    Layer* layers[] = {&l};
    r.setLayers(layers, 1);
    r.frame(0.016f);
    l.fired = false;
    l.dmg = {100, 40, 30, 10};
    d.pushedRects.clear();
    TEST_ASSERT_EQUAL(1, r.frame(0.016f));
    TEST_ASSERT_EQUAL(100, d.pushedRects[0].x);
    TEST_ASSERT_EQUAL(40, d.pushedRects[0].y);
    TEST_ASSERT_EQUAL(30, d.pushedRects[0].w);
    TEST_ASSERT_EQUAL(10, d.pushedRects[0].h);
}

// Scene whose content moves; a partial redraw must leave the same pixels as a full one.
class MovingLayer : public Layer {
   public:
    float x = 60;
    Rect last{0, 0, 0, 0};
    void update(float, DamageList& d) override {
        Rect now{int(x) - 30, 100, 60, 60};
        d.add(now.unite(last));
        last = now;
    }
    void draw(Canvas& c) override {
        Rgb stops[] = {{8, 16, 48}, {230, 130, 80}};
        float pos[] = {0.f, 1.f};
        c.vGradient({0, 0, 240, 320}, stops, pos, 2);
        c.radialGlow(x, 130, 26, {255, 230, 180}, 160);
        planeGlyph(c, x, 130, 30, 90, {255, 255, 255});
    }
};

static void test_partial_redraw_matches_full_redraw() {
    FrameCapture partial, full;
    StripRenderer rp(partial, bufA, bufB, 32);
    MovingLayer lp;
    Layer* lps[] = {&lp};
    rp.setLayers(lps, 1);
    rp.frame(0.016f);
    lp.x = 140;
    rp.frame(0.016f);

    static uint16_t cA[240 * 32], cB[240 * 32];
    StripRenderer rf(full, cA, cB, 32);
    MovingLayer lf;
    lf.x = 140;
    Layer* lfs[] = {&lf};
    rf.setLayers(lfs, 1);
    rf.frame(0.016f);
    TEST_ASSERT_TRUE(partial.px == full.px);
}

static void test_golden_basic_scene() {
    FrameCapture d;
    StripRenderer r(d, bufA, bufB, 32);
    SceneLayer s;
    Layer* layers[] = {&s};
    r.setLayers(layers, 1);
    r.frame(0.016f);
    assertGolden("renderer_basic", d.px, 240, 320);
}

// Two far-apart blocks of damage in one strip are drawn and pushed separately.
static void test_separate_columns_push_separate_blocks() {
    DamageList d;
    d.add({10, 0, 20, 10});
    d.add({200, 4, 20, 10});
    d.add({35, 2, 5, 5});  // within the join gap of the first
    int x0[4], x1[4], y0[4], y1[4];
    int n = d.spansInBand(0, 16, 240, 16, 4, x0, x1, y0, y1);
    TEST_ASSERT_EQUAL(2, n);
    TEST_ASSERT_EQUAL(10, x0[0]);
    TEST_ASSERT_EQUAL(40, x1[0]);
    TEST_ASSERT_EQUAL(0, y0[0]);  // the rows of the two rects it joins
    TEST_ASSERT_EQUAL(10, y1[0]);
    TEST_ASSERT_EQUAL(200, x0[1]);
    TEST_ASSERT_EQUAL(220, x1[1]);
    TEST_ASSERT_EQUAL(4, y0[1]);
    TEST_ASSERT_EQUAL(14, y1[1]);
    TEST_ASSERT_EQUAL(1, d.spansInBand(0, 16, 240, 16, 1, x0, x1, y0, y1));  // capped: joined
    TEST_ASSERT_EQUAL(10, x0[0]);
    TEST_ASSERT_EQUAL(220, x1[0]);
    TEST_ASSERT_EQUAL(0, y0[0]);
    TEST_ASSERT_EQUAL(14, y1[0]);
    TEST_ASSERT_EQUAL(1, d.spansInBand(8, 16, 240, 16, 1, x0, x1, y0, y1));  // clamped to the band
    TEST_ASSERT_EQUAL(8, y0[0]);
    TEST_ASSERT_EQUAL(14, y1[0]);
}

// Overlapping old and new boxes of moved items merge first, and merging never joins far
// rects while closer ones remain (negative waste once seeded the search with a bad pair).
static void test_merged_damage_stays_local() {
    DamageList d;
    Rect r[8] = {{0, 0, 10, 10},   {2, 1, 10, 10},   {100, 100, 10, 10}, {101, 100, 10, 10},
                 {200, 0, 10, 10}, {201, 0, 10, 10}, {0, 300, 10, 10},   {1, 301, 10, 10}};
    d.addMerged(r, 8, 4);
    int x0[4], x1[4];
    TEST_ASSERT_EQUAL(2, d.spansInBand(0, 16, 240, 16, 4, x0, x1));
    TEST_ASSERT_EQUAL(0, x0[0]);
    TEST_ASSERT_EQUAL(12, x1[0]);
    TEST_ASSERT_EQUAL(1, d.spansInBand(96, 16, 240, 16, 4, x0, x1));
    TEST_ASSERT_EQUAL(100, x0[0]);
    TEST_ASSERT_EQUAL(111, x1[0]);
    TEST_ASSERT_FALSE(d.touchesBand(150, 16));
    TEST_ASSERT_FALSE(d.all());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_first_frame_pushes_all_strips);
    RUN_TEST(test_only_damaged_strips_pushed);
    RUN_TEST(test_no_damage_pushes_nothing);
    RUN_TEST(test_damage_pushes_only_its_columns);
    RUN_TEST(test_partial_redraw_matches_full_redraw);
    RUN_TEST(test_golden_basic_scene);
    RUN_TEST(test_separate_columns_push_separate_blocks);
    RUN_TEST(test_merged_damage_stays_local);
    return UNITY_END();
}
