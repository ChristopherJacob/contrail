#include <unity.h>

#include <algorithm>
#include <functional>
#include <vector>

#include "data/Airlines.h"
#include "gfx/Color.h"
#include "ui/Identity.h"
#include "ui/TailFin.h"

void setUp() {}
void tearDown() {}

using namespace gfx;

static void test_stripe_contrast_rule() {
    // Lufthansa: navy fill, yellow stripe - far apart, kept.
    Rgb s = ui::finStripe({5, 22, 77}, {249, 186, 0});
    TEST_ASSERT_EQUAL(249, s.r);
    TEST_ASSERT_EQUAL(186, s.g);
    TEST_ASSERT_EQUAL(0, s.b);
    // United: blue on blue - too alike, so white (the fill is dark).
    s = ui::finStripe({31, 79, 216}, {59, 123, 255});
    TEST_ASSERT_EQUAL(255, s.r);
    TEST_ASSERT_EQUAL(255, s.g);
    TEST_ASSERT_EQUAL(255, s.b);
    // Southwest yellow on yellow: near-black.
    s = ui::finStripe({242, 185, 15}, {249, 182, 18});
    TEST_ASSERT_TRUE(s.r < 40);
    TEST_ASSERT_TRUE(s.g < 40);
    TEST_ASSERT_TRUE(s.b < 40);
}

static int countColour(const std::vector<uint16_t>& px, Rgb col) {
    int n = 0;
    for (uint16_t v : px) n += v == rgb565(col);
    return n;
}

static void test_fin_uses_livery_and_stripe() {
    const data::Airline* dlh = data::airlineByCode("DLH");
    TEST_ASSERT_NOT_NULL(dlh);
    std::vector<uint16_t> px(40 * 40, 0);
    Canvas c(px.data(), 40, 0, 40);
    ui::drawFin(c, 0, 0, 40, dlh, scr::cinematicSky());
    TEST_ASSERT_TRUE(countColour(px, dlh->fill) > 200);   // the navy fin
    TEST_ASSERT_TRUE(countColour(px, dlh->color) > 40);   // the yellow stripe
    TEST_ASSERT_TRUE(countColour(px, {0x26, 0x30, 0x6A}) > 200);  // the Cinematic tile
}

static void test_neutral_fin_and_editorial_tile() {
    std::vector<uint16_t> px(25 * 25, 0);
    Canvas c(px.data(), 25, 0, 25);
    const scr::Theme& ed = scr::editorial();
    ui::drawFin(c, 0, 0, 25, nullptr, ed);
    TEST_ASSERT_TRUE(countColour(px, ed.brandNeutral) > 60);
    TEST_ASSERT_TRUE(countColour(px, {255, 255, 255}) > 100);  // the white tile
}

static void test_identity_prefers_logo_then_fin_then_shape() {
    const uint16_t pal[1] = {rgb565(255, 0, 255)};
    const uint8_t alpha[1] = {255}, idx[1] = {0};
    Badge logo{1, 1, 8, 1, pal, alpha, idx};
    const data::Airline* ual = data::airlineByCode("UAL");
    std::vector<uint16_t> px(25 * 25, 0);
    Canvas c(px.data(), 25, 0, 25);
    ui::Identity id = ui::identityOf("UAL53", uint8_t(data::Family::Widebody));
    TEST_ASSERT_TRUE(id.airline);
    TEST_ASSERT_TRUE(id.colours == ual);
    id.logo = &logo;
    ui::drawIdentity(c, 0, 0, 25, id, scr::cinematicSky());
    TEST_ASSERT_EQUAL_HEX16(rgb565(255, 0, 255), c.get(12, 12));  // centred
    TEST_ASSERT_EQUAL_HEX16(0, c.get(0, 0));                      // no tile under a logo
    // A logo larger than the slot is skipped: the fin is drawn instead.
    Badge big{30, 30, 8, 1, pal, alpha, idx};
    id.logo = &big;
    std::fill(px.begin(), px.end(), 0);
    ui::drawIdentity(c, 0, 0, 25, id, scr::cinematicSky());
    TEST_ASSERT_EQUAL(0, countColour(px, {255, 0, 255}));
    id.logo = nullptr;
    std::fill(px.begin(), px.end(), 0);
    ui::drawIdentity(c, 0, 0, 25, id, scr::cinematicSky());
    TEST_ASSERT_TRUE(countColour(px, ual->fill) > 40);
    ui::Identity reg = ui::identityOf("N512KA", uint8_t(data::Family::Single));
    TEST_ASSERT_FALSE(reg.airline);
    std::fill(px.begin(), px.end(), 0);
    ui::drawIdentity(c, 0, 0, 25, reg, scr::cinematicSky());
    TEST_ASSERT_TRUE(countColour(px, scr::cinematicSky().text) > 20);  // the shape in the text colour
}

static void test_identity_of_unknown_airline_and_bad_shape() {
    ui::Identity id = ui::identityOf("ZZZ12", 13);
    TEST_ASSERT_TRUE(id.airline);
    TEST_ASSERT_NULL(id.colours);
    TEST_ASSERT_TRUE(id.family == data::Family::None);
    TEST_ASSERT_TRUE(ui::identityOf("N1", 12).family == data::Family::Airlifter);
    TEST_ASSERT_TRUE(ui::identityOf("N1", 13).family == data::Family::None);
}

static void test_fin_strip_equivalence() {
    const data::Airline* dlh = data::airlineByCode("DLH");
    const int w = 60, h = 60;
    std::vector<uint16_t> whole(w * h, 0), banded(w * h, 0);
    auto draw = [&](Canvas& c) { ui::drawFin(c, 7, 9, 40, dlh, scr::cinematicSky(), 200); };
    Canvas full(whole.data(), w, 0, h);
    draw(full);
    const int bandH = 16;
    std::vector<uint16_t> strip(w * bandH);
    for (int y0 = 0; y0 < h; y0 += bandH) {
        int bh = std::min(bandH, h - y0);
        std::fill(strip.begin(), strip.end(), 0);
        Canvas c(strip.data(), w, y0, bh);
        draw(c);
        std::copy(strip.begin(), strip.begin() + w * bh, banded.begin() + y0 * w);
    }
    TEST_ASSERT_TRUE(whole == banded);
}

static void test_tiny_side_stays_in_tile() {
    std::vector<uint16_t> px(20 * 20, 0);
    Canvas c(px.data(), 20, 0, 20);
    ui::drawFin(c, 8, 8, 3, data::airlineByCode("UAL"), scr::cinematicSky());
    ui::drawFamilyTile(c, 8, 8, 3, data::Family::Widebody, scr::editorial());
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 20; ++x)
            if (x < 8 || x >= 11 || y < 8 || y >= 11) TEST_ASSERT_EQUAL_HEX16(0, c.get(x, y));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_stripe_contrast_rule);
    RUN_TEST(test_fin_uses_livery_and_stripe);
    RUN_TEST(test_neutral_fin_and_editorial_tile);
    RUN_TEST(test_identity_prefers_logo_then_fin_then_shape);
    RUN_TEST(test_identity_of_unknown_airline_and_bad_shape);
    RUN_TEST(test_fin_strip_equivalence);
    RUN_TEST(test_tiny_side_stays_in_tile);
    return UNITY_END();
}
