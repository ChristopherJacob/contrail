#include <unity.h>

#include <algorithm>
#include <vector>

#include "app/DemoClip.h"

void setUp() {}
void tearDown() {}

using Frame = std::vector<uint16_t>;
using Frames = std::vector<Frame>;
static uint16_t bufA[240 * 16], bufB[240 * 16];

static Frames clip(const char* screen, const char* theme = "cinematic") {
    Frames out;
    int n = app::renderClip(screen, theme, [&](const uint16_t* px) { out.emplace_back(px, px + 240 * 320); }, bufA, bufB, 16);
    TEST_ASSERT_EQUAL(int(out.size()), n);
    return out;
}

static int changed(const Frame& a, const Frame& b) {
    int n = 0;
    for (size_t i = 0; i < a.size(); ++i) n += a[i] != b[i];
    return n;
}

// The step from the last frame back to the first looks like any other step of the clip.
static void assertLoopCloses(const Frames& f) {
    int biggest = 0;
    for (size_t i = 0; i + 1 < f.size(); ++i) biggest = std::max(biggest, changed(f[i], f[i + 1]));
    TEST_ASSERT_GREATER_THAN(0, biggest);
    TEST_ASSERT_LESS_OR_EQUAL(2 * biggest, changed(f.back(), f.front()));
}

void test_radar_is_one_turn_and_closes() {
    Frames f = clip("radar");
    TEST_ASSERT_EQUAL(120, int(f.size()));
    assertLoopCloses(f);
}

void test_alert_closes_through_the_crossfade() {
    Frames f = clip("alert");
    TEST_ASSERT_EQUAL(90, int(f.size()));
    assertLoopCloses(f);
}

void test_map_closes_through_the_crossfade() {
    Frames f = clip("map");
    TEST_ASSERT_EQUAL(240, int(f.size()));
    assertLoopCloses(f);
}

// A callsign's pixels on the top row: the rows re-sort as the nearest flight flies away.
void test_board_rows_re_sort() {
    Frames f = clip("board");
    TEST_ASSERT_EQUAL(240, int(f.size()));
    assertLoopCloses(f);
    int diff = 0;
    for (int y = 80; y < 97; ++y)
        for (int x = 45; x < 100; ++x) diff += f[0][y * 240 + x] != f[200][y * 240 + x];
    TEST_ASSERT_GREATER_THAN(50, diff);
}

void test_two_runs_are_identical() {
    Frames a = clip("radar"), b = clip("radar");
    for (size_t i = 0; i < a.size(); ++i) TEST_ASSERT_EQUAL(0, changed(a[i], b[i]));
}

void test_stats_and_unknown_names_are_refused() {
    auto none = [](const uint16_t*) {};
    TEST_ASSERT_EQUAL(-1, app::renderClip("stats", "cinematic", none, bufA, bufB, 16));
    TEST_ASSERT_EQUAL(-1, app::renderClip("nope", "cinematic", none, bufA, bufB, 16));
    TEST_ASSERT_EQUAL(-1, app::renderClip("radar", "nope", none, bufA, bufB, 16));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_radar_is_one_turn_and_closes);
    RUN_TEST(test_alert_closes_through_the_crossfade);
    RUN_TEST(test_map_closes_through_the_crossfade);
    RUN_TEST(test_board_rows_re_sort);
    RUN_TEST(test_two_runs_are_identical);
    RUN_TEST(test_stats_and_unknown_names_are_refused);
    return UNITY_END();
}
