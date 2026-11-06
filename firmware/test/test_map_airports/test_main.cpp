#include <unity.h>

#include <cstdio>
#include <cstring>

#include "gfx/fonts/Fonts.h"
#include "map/Airports.h"
#include "map/MapData.h"

using basemap::AirportMark;

void setUp() {}
void tearDown() {}

static void mark(AirportMark& a, int x, int y, bool large, const char* id) {
    a = AirportMark{};
    a.x = int16_t(x), a.y = int16_t(y), a.large = large;
    std::snprintf(a.ident, sizeof a.ident, "%s", id);
}

// Largest first; a label that would sit on one already placed is skipped.
static void test_labels_skip_overlaps() {
    AirportMark m[3];
    mark(m[0], 100, 100, true, "KBOS");
    mark(m[1], 104, 103, false, "KBED");
    mark(m[2], 40, 40, false, "KOWD");
    basemap::placeLabels(m, 3, gfx::kFontSmall, 240);
    TEST_ASSERT_TRUE(m[0].labelled);
    TEST_ASSERT_FALSE(m[1].labelled);
    TEST_ASSERT_TRUE(m[2].labelled);
    TEST_ASSERT_TRUE(m[2].box().x >= 44);  // to the right of its dot
}

static void test_labels_near_the_right_edge_go_left() {
    AirportMark m[1];
    mark(m[0], 230, 50, true, "KJFK");
    basemap::placeLabels(m, 1, gfx::kFontSmall, 240);
    TEST_ASSERT_TRUE(m[0].labelled && m[0].alignRight);
    TEST_ASSERT_TRUE(m[0].box().right() <= 227);
}

static void test_airports_around_sfo() {
    AirportMark m[20];
    int n = basemap::airportsInView(basemap::viewFor(37.6189, -122.375, 25), m, 20);
    TEST_ASSERT_GREATER_THAN(3, n);
    TEST_ASSERT_EQUAL_STRING("KSFO", m[0].ident);  // large, and right at home
    TEST_ASSERT_TRUE(m[0].large);
    TEST_ASSERT_GREATER_OR_EQUAL(4, basemap::gen::kAirports[m[0].index].runways());
    bool oak = false;
    for (int i = 0; i < n; ++i) oak |= !std::strcmp(m[i].ident, "KOAK");
    TEST_ASSERT_TRUE(oak);
    for (int i = 1; i < n; ++i) TEST_ASSERT_TRUE(m[i - 1].large >= m[i].large);  // large ones first
}

static bool has(const AirportMark* m, int n, const char* id) {
    for (int i = 0; i < n; ++i)
        if (!std::strcmp(m[i].ident, id)) return true;
    return false;
}

// Nadi (NFFN, 177.44° E) from home just east of the dateline, and from close by at 25 nm.
static void test_airports_across_the_dateline() {
    AirportMark m[20];
    int n = basemap::airportsInView(basemap::viewFor(-17.75, -179.5, 250), m, 20);
    TEST_ASSERT_TRUE(has(m, n, "NFFN"));
    for (int i = 0; i < n; ++i) TEST_ASSERT_TRUE(m[i].x >= 0 && m[i].x < 240);
    n = basemap::airportsInView(basemap::viewFor(-17.6, 177.6, 25), m, 20);
    TEST_ASSERT_TRUE(has(m, n, "NFFN"));
}

// With more airports in view than room, the ones kept are the best by rank: the same as the head
// of the full list.
static void test_max_keeps_the_best() {
    static AirportMark all[200];
    AirportMark few[5];
    const basemap::MapView v = basemap::viewFor(37.6189, -122.375, 250);
    const int n = basemap::airportsInView(v, all, 200);
    TEST_ASSERT_GREATER_THAN(5, n);
    TEST_ASSERT_EQUAL(5, basemap::airportsInView(v, few, 5));
    for (int i = 0; i < 5; ++i) TEST_ASSERT_EQUAL_STRING(all[i].ident, few[i].ident);
}

static void test_max_zero() {
    AirportMark m[1];
    TEST_ASSERT_EQUAL(0, basemap::airportsInView(basemap::viewFor(37.6189, -122.375, 25), m, 0));
}

// A label steps out past a keep-clear rect on its own side (home's halo), and is left off when
// that would take it too far (under the header).
static void test_labels_keep_clear() {
    AirportMark m[2];
    mark(m[0], 120, 135, true, "KSFO");
    mark(m[1], 30, 14, false, "KXYZ");
    const gfx::Rect keep[] = {{111, 126, 19, 19}, {11, 8, 100, 15}};
    basemap::placeLabels(m, 2, gfx::kFontSmall, 240, keep, 2);
    TEST_ASSERT_TRUE(m[0].labelled);
    const gfx::Rect b = m[0].box(), ink{b.x + 1, b.y + 1, b.w - 3, b.h - 3};  // the box less its margin
    TEST_ASSERT_FALSE(ink.intersects(keep[0]));
    TEST_ASSERT_TRUE(m[0].box().x <= 120 + 5 + basemap::kKeepShift);
    TEST_ASSERT_FALSE(m[1].labelled);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_labels_skip_overlaps);
    RUN_TEST(test_labels_near_the_right_edge_go_left);
    RUN_TEST(test_airports_around_sfo);
    RUN_TEST(test_airports_across_the_dateline);
    RUN_TEST(test_max_keeps_the_best);
    RUN_TEST(test_max_zero);
    RUN_TEST(test_labels_keep_clear);
    return UNITY_END();
}
