#include <unity.h>

#include "data/Airlines.h"
#include "gfx/Canvas.h"
#include "screens/Theme.h"
#include "ui/Widgets.h"

using gfx::Rgb;

void setUp() {}
void tearDown() {}

static uint16_t buf[240 * 16];

static scr::Theme flatTheme() {
    scr::Theme t = scr::cinematicSky();
    t.flat = true;
    return t;
}
static bool same(Rgb a, Rgb b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

static void test_contrast_ratio_matches_wcag() {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 21.f, ui::contrastRatio({255, 255, 255}, {0, 0, 0}));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.f, ui::contrastRatio({120, 30, 200}, {120, 30, 200}));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.2126f, ui::relativeLuminance({255, 0, 0}));
}

// Bold Editorial's Board rows: black on the light brand colours, white on the rest.
static void test_text_on_brand_colours() {
    const Rgb white{255, 255, 255}, black{0, 0, 0};
    TEST_ASSERT_TRUE(same(black, ui::textOn({255, 236, 0})));   // Spirit yellow
    TEST_ASSERT_TRUE(same(black, ui::textOn({249, 186, 0})));   // Lufthansa
    TEST_ASSERT_TRUE(same(white, ui::textOn({59, 123, 255})));  // United: black contrasts more, white still passes 3:1
    TEST_ASSERT_TRUE(same(white, ui::textOn({227, 50, 75})));   // Delta
    TEST_ASSERT_TRUE(same(white, ui::textOn({96, 100, 110})));  // the neutral fill
}

// Area fills use the real livery colour, and every one of them gets a text colour that reads (3:1).
static void test_airline_fills_are_readable() {
    const data::Airline* ual = data::airlineByCode("UAL");
    TEST_ASSERT_TRUE(same({31, 79, 216}, data::airlineFill(ual, {1, 2, 3})));
    TEST_ASSERT_TRUE(same({200, 16, 46}, data::airlineFill(data::airlineByCode("DAL"), {1, 2, 3})));
    TEST_ASSERT_TRUE(same({1, 2, 3}, data::airlineFill(nullptr, {1, 2, 3})));  // no airline: the neutral
    for (int i = 0; i < data::kAirlineCount; ++i) {
        const Rgb f = data::airlineFill(&data::kAirlines[i], {0, 0, 0});
        TEST_ASSERT_TRUE_MESSAGE(f.r || f.g || f.b, data::kAirlines[i].icao);
        TEST_ASSERT_TRUE_MESSAGE(ui::contrastRatio(ui::textOn(f), f) >= 3.f, data::kAirlines[i].icao);
    }
}

// A brand colour lightened just enough to read on a dark card (the Closest percentage, mockup 7A).
static void test_readable_on_lightens_only_as_needed() {
    const Rgb card{14, 14, 16}, ual{31, 79, 216};
    const Rgb t = ui::readableOn(ual, card, 4.5f);
    TEST_ASSERT_TRUE(ui::contrastRatio(t, card) >= 4.5f);
    TEST_ASSERT_TRUE(t.b > t.r && t.b > t.g);           // still blue
    TEST_ASSERT_TRUE(ui::contrastRatio(t, card) < 7.f);  // not washed out to white
    TEST_ASSERT_TRUE(same({242, 185, 15}, ui::readableOn({242, 185, 15}, card, 4.5f)));  // already reads
}

static void test_flat_vfill_is_solid() {
    gfx::Canvas c(buf, 240, 0, 16);
    Rgb stops[] = {{10, 20, 30}, {200, 100, 50}};
    float pos[] = {0, 1};
    ui::vFill(c, {0, 0, 240, 16}, stops, pos, 2, flatTheme());
    for (int i = 0; i < 240 * 16; ++i) TEST_ASSERT_EQUAL_HEX16(gfx::rgb565(stops[0]), buf[i]);
}

static void test_flat_radial_fill_has_hard_bands() {
    gfx::Canvas c(buf, 240, 0, 16);
    Rgb stops[] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}};
    float pos[] = {0, 0.25f, 0.5f, 0.75f};
    // Centre (0, 8), radius 100: x = 10 is 0.1 out (band 0), 30 → 0.3, 60 → 0.6, 90 → 0.9, 200 → beyond.
    ui::radialFill(c, {0, 0, 240, 16}, 0, 8, 100, 100, stops, pos, 4, flatTheme());
    TEST_ASSERT_EQUAL_HEX16(gfx::rgb565(stops[0]), c.get(10, 7));
    TEST_ASSERT_EQUAL_HEX16(gfx::rgb565(stops[1]), c.get(30, 7));
    TEST_ASSERT_EQUAL_HEX16(gfx::rgb565(stops[2]), c.get(60, 7));
    TEST_ASSERT_EQUAL_HEX16(gfx::rgb565(stops[3]), c.get(90, 7));
    TEST_ASSERT_EQUAL_HEX16(gfx::rgb565(stops[3]), c.get(200, 7));
}

static void test_flat_glow_draws_nothing() {
    gfx::Canvas c(buf, 240, 0, 16);
    c.fill({0, 0, 240, 16}, 0x1234);
    ui::glow(c, 120, 8, 30, {255, 255, 255}, 200, flatTheme());
    for (int i = 0; i < 240 * 16; ++i) TEST_ASSERT_EQUAL_HEX16(0x1234, buf[i]);
    ui::glow(c, 120, 8, 30, {255, 255, 255}, 200, scr::cinematicSky());
    TEST_ASSERT_TRUE(c.get(120, 8) != 0x1234);
}

static void test_flat_panel_is_square() {
    gfx::Canvas c(buf, 240, 0, 16);
    c.fill({0, 0, 240, 16}, 0);
    ui::panel(c, {10, 2, 100, 12}, 6, {200, 200, 200}, {255, 255, 255}, scr::cinematicSky());
    TEST_ASSERT_EQUAL_HEX16(0, c.get(10, 2));  // the rounded corner leaves it empty
    ui::panel(c, {10, 2, 100, 12}, 6, {200, 200, 200}, {255, 255, 255}, flatTheme());
    TEST_ASSERT_EQUAL_HEX16(gfx::rgb565(200, 200, 200), c.get(10, 2));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_contrast_ratio_matches_wcag);
    RUN_TEST(test_text_on_brand_colours);
    RUN_TEST(test_airline_fills_are_readable);
    RUN_TEST(test_readable_on_lightens_only_as_needed);
    RUN_TEST(test_flat_vfill_is_solid);
    RUN_TEST(test_flat_radial_fill_has_hard_bands);
    RUN_TEST(test_flat_glow_draws_nothing);
    RUN_TEST(test_flat_panel_is_square);
    return UNITY_END();
}
