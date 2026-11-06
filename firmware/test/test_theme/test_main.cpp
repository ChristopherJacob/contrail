#include <unity.h>

#include <memory>

#include "gfx/fonts/Fonts.h"
#include "screens/ScreenApp.h"
#include "screens/Theme.h"

void setUp() {}
void tearDown() {}

static void test_cinematic_roles_are_inter() {
    const scr::Theme& t = scr::cinematicSky();
    TEST_ASSERT_EQUAL_PTR(&gfx::kFontHero, t.fontDisplay);
    TEST_ASSERT_EQUAL_PTR(&gfx::kFontTitle, t.fontTitle);
    TEST_ASSERT_EQUAL_PTR(&gfx::kFontBold, t.fontLabel);
    TEST_ASSERT_FALSE(t.flat);
    TEST_ASSERT_FALSE(t.brandFill);
    TEST_ASSERT_FALSE(t.upperHeads);
}

static void test_theme_by_name() {
    TEST_ASSERT_EQUAL_PTR(&scr::editorial(), &scr::themeByName("editorial"));
    TEST_ASSERT_EQUAL_PTR(&scr::cinematicSky(), &scr::themeByName("cinematic"));
    TEST_ASSERT_EQUAL_PTR(&scr::cinematicSky(), &scr::themeByName("bogus"));
    TEST_ASSERT_EQUAL_PTR(&scr::cinematicSky(), &scr::themeByName(nullptr));
    TEST_ASSERT_TRUE(scr::editorial().flat);
    TEST_ASSERT_TRUE(scr::editorial().brandFill);
    TEST_ASSERT_TRUE(scr::editorial().upperHeads);
    // Every Stats colour is set (a palette that forgets one draws the chart in black).
    const gfx::Rgb past = scr::editorial().statsPast;
    TEST_ASSERT_TRUE(past.r || past.g || past.b);
}

static void test_editorial_uses_the_condensed_display_face() {
    const scr::Theme& t = scr::editorial();
    TEST_ASSERT_EQUAL_PTR(&gfx::kFontCondHero, t.fontDisplay);
    TEST_ASSERT_EQUAL_PTR(&gfx::kFontCondTitle, t.fontTitle);
    TEST_ASSERT_EQUAL_PTR(&gfx::kFontCondLabel, t.fontLabel);
    TEST_ASSERT_EQUAL_UINT8(0, t.radarBg[0].r);  // black scope
    TEST_ASSERT_EQUAL_UINT8(0, t.veil.g);
}

// Both palettes are written out in full: an initialiser list one colour short leaves the last
// members zero (black), so the last one and the trailing map/stats colours must be set.
static void test_every_theme_sets_its_last_colours() {
    for (const scr::Theme* t : {&scr::cinematicSky(), &scr::editorial()}) {
        for (gfx::Rgb c : {t->brandNeutral, t->mapWater, t->mapCardEdge, t->statsBar, t->statsTrack, t->statsCard,
                           t->statsCardEdge})
            TEST_ASSERT_TRUE(c.r || c.g || c.b);
    }
}

// The app draws from its own copy, so a theme can be picked at boot (or swapped by the soak).
// Map pack colours (5b): set in both themes, and roads, rivers and towns can be told from land.
static void test_map_pack_colours() {
    for (const scr::Theme* t : {&scr::cinematicSky(), &scr::editorial()}) {
        for (const gfx::Rgb& c : {t->mapRoad, t->mapRoadMinor, t->mapRiver, t->mapTown}) {
            TEST_ASSERT_TRUE(c.r || c.g || c.b);
            TEST_ASSERT_NOT_EQUAL(gfx::rgb565(t->mapLand), gfx::rgb565(c));
        }
    }
}

static void test_app_uses_a_copy_of_the_theme() {
    auto app = std::make_unique<scr::ScreenApp>();
    TEST_ASSERT_FALSE(app->theme().flat);
    app->useTheme(scr::editorial());
    TEST_ASSERT_TRUE(app->theme().flat);
    TEST_ASSERT_TRUE(&scr::editorial() != &app->theme());
    auto ed = std::make_unique<scr::ScreenApp>(scr::editorial());
    TEST_ASSERT_TRUE(ed->theme().brandFill);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_cinematic_roles_are_inter);
    RUN_TEST(test_theme_by_name);
    RUN_TEST(test_map_pack_colours);
    RUN_TEST(test_app_uses_a_copy_of_the_theme);
    RUN_TEST(test_editorial_uses_the_condensed_display_face);
    RUN_TEST(test_every_theme_sets_its_last_colours);
    return UNITY_END();
}
