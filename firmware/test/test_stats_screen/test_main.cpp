#include <unity.h>

#include <cstdlib>
#include <ctime>

#include "GoldenRender.h"
#include "gfx/fonts/Fonts.h"
#include "screens/DemoScenes.h"
#include "screens/StatsScreen.h"

using golden::Rig;
using scr::ScreenId;

void setUp() {
    setenv("TZ", "UTC0", 1);
    tzset();
}
void tearDown() {}

static void test_stats_today() { golden::render(ScreenId::Stats, scr::sceneStats, "stats_today"); }
static void test_stats_empty() { golden::render(ScreenId::Stats, scr::sceneStatsEmpty, "stats_empty"); }

// Before the clock is set the loaded counts may be another day's: the screen shows the all-time
// logbook (no hour chart, nothing called "today") and says it is waiting for the clock.
static void test_stats_no_clock() { golden::render(ScreenId::Stats, scr::sceneStatsNoClock, "stats_noclock"); }

static void swipeUp(Rig& g, uint32_t t0) {
    for (uint32_t t = 0; t <= 240; t += 20) g.app->touch(true, 120, 250 - int(t) * 150 / 240, t0 + t);
    g.app->touch(false, 0, 0, t0 + 260);
}

// A real upward swipe switches to all-time; another one switches back.
static void test_stats_all_time() {
    Rig g;
    scr::sceneStats(g.app->scene());
    g.app->sceneUpdated();
    g.show(ScreenId::Stats);
    g.frames(5);
    swipeUp(g, 1000);
    g.frames(70);
    assertGolden("stats_alltime", g.d.px, 240, 320);
    g.assertNoStale();
    swipeUp(g, 3000);
    g.frames(5);
    assertGolden("stats_today", g.d.px, 240, 320);
    g.assertNoStale();
}

// Nothing on one line runs into the next thing on it: the header's count and right-hand value, the
// card's left text and the newest type's lines, and every row's bar and count.
static void checkLayout(Rig& g) {
    using gfx::textWidth;
    using V = scr::StatsView;
    const auto* screen = static_cast<const scr::StatsScreen*>(g.app->host().screen(ScreenId::Stats));
    const V::Text* t = screen->view().text();
    TEST_ASSERT_NOT_NULL(t);
    const scr::Theme& th = g.app->theme();
    if (t->rightValue[0])
        TEST_ASSERT_LESS_OR_EQUAL(V::kRight - textWidth(gfx::kFontBody, t->rightValue),
                                  14 + textWidth(*th.fontDisplay, t->count) + V::kGap);
    const int countEnd = V::kCardLeft + textWidth(*th.fontTitle, t->cardCount);
    const int leftEnd = countEnd + (t->cardNew[0] ? 5 + textWidth(gfx::kFontSmall, t->cardNew) : 0);
    TEST_ASSERT_LESS_OR_EQUAL(V::kCardRight - textWidth(gfx::kFontBody, t->cardType), countEnd + V::kGap);
    TEST_ASSERT_LESS_OR_EQUAL(V::kCardRight - textWidth(gfx::kFontSmall, t->cardSub), leftEnd + V::kGap);
    auto rows = [&](const V::Row* r, int n) {
        for (int i = 0; i < n; ++i)
            TEST_ASSERT_LESS_OR_EQUAL(V::kRight - textWidth(gfx::kFontSmall, r[i].count),
                                      V::kBarX + r[i].barW + V::kGap);
    };
    rows(t->chartRow, t->chartRows);
    rows(t->listRow, t->listRows);
}

// Years of logbook: a seven-digit total drops the "Since" date rather than run into it, long counts
// shorten their bars, and the card's newest sighting keeps clear of "123 none new today".
static void test_stats_big_numbers() {
    Rig g;
    scr::sceneStatsBig(g.app->scene());
    g.app->sceneUpdated();
    g.show(ScreenId::Stats);
    g.frames(3);
    checkLayout(g);
    swipeUp(g, 1000);
    g.frames(5);
    checkLayout(g);
    const auto* screen = static_cast<const scr::StatsScreen*>(g.app->host().screen(ScreenId::Stats));
    TEST_ASSERT_EQUAL_STRING("", screen->view().text()->rightValue);  // "1,000,000" leaves no room for it
    assertGolden("stats_big", g.d.px, 240, 320);
    g.assertNoStale();
}

// Before the clock is set a swipe does nothing (there is only the all-time view); once it is set
// the screen shows today.
static void test_stats_swipe_waits_for_the_clock() {
    Rig g;
    scr::sceneStatsNoClock(g.app->scene());
    g.app->sceneUpdated();
    g.show(ScreenId::Stats);
    g.frames(3);
    swipeUp(g, 1000);
    const auto* screen = static_cast<const scr::StatsScreen*>(g.app->host().screen(ScreenId::Stats));
    TEST_ASSERT_FALSE(screen->allTime());
    scr::sceneStats(g.app->scene());
    g.app->sceneUpdated();
    g.frames(3);
    TEST_ASSERT_EQUAL_STRING("TODAY", screen->view().text()->title);
    checkLayout(g);
}

// Text-hash damage: a settled screen draws nothing; a new count redraws the header; a new bar the chart.
static void test_stats_redraws_only_what_changed() {
    Rig g;
    scr::sceneStats(g.app->scene());
    g.app->sceneUpdated();
    g.show(ScreenId::Stats);
    g.frames(3);
    TEST_ASSERT_EQUAL(0, g.pushed());
    g.app->scene().stats.flightsToday += 1;
    g.app->sceneUpdated();
    int px = g.pushed();
    TEST_ASSERT_TRUE(px > 0 && px <= 240 * 80);  // the header, rounded out to 16-row strips
    g.app->scene().stats.hourly[18] += 1;
    g.app->sceneUpdated();
    px = g.pushed();
    TEST_ASSERT_TRUE(px > 0 && px <= 240 * 112);  // the chart
    g.assertNoStale();
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_stats_today);
    RUN_TEST(test_stats_empty);
    RUN_TEST(test_stats_no_clock);
    RUN_TEST(test_stats_all_time);
    RUN_TEST(test_stats_redraws_only_what_changed);
    RUN_TEST(test_stats_big_numbers);
    RUN_TEST(test_stats_swipe_waits_for_the_clock);
    return UNITY_END();
}
