#include <unity.h>

#include <cmath>

#include "map/Tiles.h"

using namespace basemap;

void setUp() {}
void tearDown() {}

// Four points: (100,100) → (220,100) short step → (220,2000) long step → (100,2000) short step
// marked as a tile cut; the closing edge back to (100,100) is a cut too (bit 15 of the header).
static const uint8_t kRing[] = {
    4, 0x80,                    // count 4 | closing cut
    100, 0, 100, 0,             // x0, y0
    120, 0,                     // +120, +0
    0x80, 0x00, 0x00, 0x6C, 0x07,  // long: +0, +1900
    0x81, 0x88, 0x00,           // cut, then -120, +0
};

// Short, medium and long steps, each with and without a tile cut, and each step kind's boundaries.
// tools/gen_map.py --selftest encodes the same points and checks it gets exactly these bytes
// (STEP_RING), so encoder and reader agree.
static const uint8_t kSteps[] = {
    13, 0x80,                        // count 13 | closing cut
    0, 0, 0, 0,                      // (0, 0)
    0x80, 0x00, 0x08, 0x00, 0x00,    // long: +2048, +0
    0x81, 0x80, 0x00, 0x00, 0x00, 0x08,  // cut, long: +0, +2048
    0x81, 0xD0, 0xF8,                // cut, short: -48, -8
    0xF6, 0xF6,                      // short: -10, -10
    0x81, 0x82, 0x22, 0xAC, 0xBF,    // cut, medium: -990, -1030
    0x82, 0x0C, 0x0E, 0xE7,          // medium: -500, -400
    0x83, 0x00,                      // short: -125 (the most negative short dx), 0
    0x82, 0x82, 0x0F, 0x00,          // medium: -126, 0
    0x00, 0x80,                      // short: 0, -128
    0x82, 0x08, 0x0F, 0x00,          // medium: -248, 0
    0x82, 0xFF, 0x07, 0x00,          // medium: +2047, 0
    0x81, 0x82, 0x00, 0x08, 0x00,    // cut, medium: -2048, 0
};

static void test_ring_reader_decodes_steps_and_cuts() {
    RingReader rr(kRing, kRing + sizeof kRing);
    TEST_ASSERT_EQUAL(4, rr.count());
    const RingPoint want[] = {{100, 100, false}, {220, 100, false}, {220, 2000, false}, {100, 2000, true},
                              {100, 100, true}};
    RingPoint p;
    for (const RingPoint& w : want) {
        TEST_ASSERT_TRUE(rr.next(p));
        TEST_ASSERT_EQUAL(w.x, p.x);
        TEST_ASSERT_EQUAL(w.y, p.y);
        TEST_ASSERT_EQUAL(w.cut, p.cut);
    }
    TEST_ASSERT_FALSE(rr.next(p));
    TEST_ASSERT_TRUE(rr.ok());
    TEST_ASSERT_EQUAL_PTR(kRing + sizeof kRing, rr.end());
}

static void test_short_medium_and_long_steps_with_cuts() {
    RingReader rr(kSteps, kSteps + sizeof kSteps);
    TEST_ASSERT_EQUAL(13, rr.count());
    const RingPoint want[] = {{0, 0, false},       {2048, 0, false},    {2048, 2048, true}, {2000, 2040, true},
                              {1990, 2030, false}, {1000, 1000, true}, {500, 600, false},  {375, 600, false},
                              {249, 600, false},   {249, 472, false},   {1, 472, false},    {2048, 472, false},
                              {0, 472, true},      {0, 0, true}};
    RingPoint p;
    for (const RingPoint& w : want) {
        TEST_ASSERT_TRUE(rr.next(p));
        TEST_ASSERT_EQUAL(w.x, p.x);
        TEST_ASSERT_EQUAL(w.y, p.y);
        TEST_ASSERT_EQUAL(w.cut, p.cut);
    }
    TEST_ASSERT_FALSE(rr.next(p));
    TEST_ASSERT_TRUE(rr.ok());
    TEST_ASSERT_EQUAL_PTR(kSteps + sizeof kSteps, rr.end());
    // Cut short inside the last (medium) step.
    RingReader cut(kSteps, kSteps + sizeof kSteps - 1);
    int n = 0;
    while (cut.next(p)) ++n;
    TEST_ASSERT_EQUAL(12, n);
    TEST_ASSERT_FALSE(cut.ok());
}

static void test_truncated_ring_is_not_ok() {
    RingReader rr(kRing, kRing + 9);  // the long step is cut short
    RingPoint p;
    int n = 0;
    while (rr.next(p)) ++n;
    TEST_ASSERT_EQUAL(2, n);
    TEST_ASSERT_FALSE(rr.ok());
    RingReader tiny(kRing, kRing + 3);
    TEST_ASSERT_FALSE(tiny.next(p));
    TEST_ASSERT_FALSE(tiny.ok());
}

static void test_tile_cells() {
    TEST_ASSERT_EQUAL(63, tileRow(37.6));   // 36..38
    TEST_ASSERT_EQUAL(28, tileCol(-122.4));  // -124..-122
    TEST_ASSERT_EQUAL(0, tileRow(-90));
    TEST_ASSERT_EQUAL(89, tileRow(90));
    TEST_ASSERT_EQUAL(0, tileCol(180));      // wraps
    TEST_ASSERT_EQUAL(179, tileCol(-180.5));
    TEST_ASSERT_EQUAL(int(TileRef::Water), int(builtinTiles().tile(-1, 0).kind));
    TEST_ASSERT_EQUAL(int(TileRef::Water), int(builtinTiles().tile(tileRow(-75.0), tileCol(0.0)).kind));  // cropped
    TEST_ASSERT_EQUAL(kFirstDataRow, tileRow(-60.0));
    TEST_ASSERT_EQUAL(0, tileRow(std::nan("")));
    TEST_ASSERT_EQUAL(0, tileCol(std::nan("")));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_ring_reader_decodes_steps_and_cuts);
    RUN_TEST(test_short_medium_and_long_steps_with_cuts);
    RUN_TEST(test_truncated_ring_is_not_ok);
    RUN_TEST(test_tile_cells);
    return UNITY_END();
}
