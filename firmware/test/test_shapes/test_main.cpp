#include <unity.h>

#include <cmath>
#include <vector>

#include "data/Shapes.h"
#include "gfx/Canvas.h"

void setUp() {}
void tearDown() {}

using data::Family;

static void test_every_outline_is_closed_and_inside_its_box() {
    for (int i = 0; i < data::kFamilyCount; ++i) {
        const data::ShapeDef& s = data::kShapes[i];
        const uint8_t* p = s.data;
        TEST_ASSERT_TRUE(s.topN >= 4 && s.sideN >= 4);
        TEST_ASSERT_TRUE(s.topN <= data::kMaxOutline && s.sideN <= data::kMaxOutline);  // drawTop/drawSide point buffers
        TEST_ASSERT_EQUAL(0, p[0]);                    // starts on the centreline
        TEST_ASSERT_EQUAL(0, p[(s.topN - 1) * 2]);     // and ends on it
        for (int k = 0; k < s.topN; ++k) TEST_ASSERT_TRUE(p[2 * k] <= 100 && p[2 * k + 1] <= 200);
        const uint8_t* side = p + s.topN * 2 + s.topEngN * 4 + s.topLineN * 4;
        for (int k = 0; k < s.sideN; ++k) TEST_ASSERT_TRUE(side[2 * k] <= 240 && side[2 * k + 1] <= 80);
        TEST_ASSERT_TRUE(s.iconPx == 13 || s.iconPx == 16 || s.iconPx == 20 || s.iconPx == 22);
    }
}

static void test_type_table_is_sorted() {
    for (int i = 1; i < data::kTypeFamilyCount; ++i)
        TEST_ASSERT_TRUE((data::kTypeFamilies[i - 1] >> 4) < (data::kTypeFamilies[i] >> 4));
    TEST_ASSERT_TRUE(data::kTypeFamilyCount > 2500);
}

static void test_type_lookup() {
    TEST_ASSERT_EQUAL(int(Family::Widebody), int(data::familyForType("B789")));
    TEST_ASSERT_EQUAL(int(Family::Narrowbody), int(data::familyForType("A320")));
    TEST_ASSERT_EQUAL(int(Family::Jumbo), int(data::familyForType("B744")));
    TEST_ASSERT_EQUAL(int(Family::Superjumbo), int(data::familyForType("A388")));
    TEST_ASSERT_EQUAL(int(Family::Regional), int(data::familyForType("CRJ9")));
    TEST_ASSERT_EQUAL(int(Family::Bizjet), int(data::familyForType("C56X")));
    TEST_ASSERT_EQUAL(int(Family::Turboprop), int(data::familyForType("AT76")));
    TEST_ASSERT_EQUAL(int(Family::Single), int(data::familyForType("C172")));
    TEST_ASSERT_EQUAL(int(Family::Twin), int(data::familyForType("BE58")));
    TEST_ASSERT_EQUAL(int(Family::Helicopter), int(data::familyForType("R44")));
    TEST_ASSERT_EQUAL(int(Family::FastJet), int(data::familyForType("F16")));
    TEST_ASSERT_EQUAL(int(Family::Airlifter), int(data::familyForType("C17")));
    TEST_ASSERT_EQUAL(int(Family::Widebody), int(data::familyForType("b789")));  // case does not matter
    TEST_ASSERT_EQUAL(int(Family::None), int(data::familyForType("ZZZZ")));
    TEST_ASSERT_EQUAL(int(Family::None), int(data::familyForType("")));
    TEST_ASSERT_EQUAL(int(Family::None), int(data::familyForType(nullptr)));
    TEST_ASSERT_EQUAL(int(Family::None), int(data::familyForType("B78-")));
}

static void test_category_fallback() {
    TEST_ASSERT_EQUAL(int(Family::Single), int(data::familyForCategory(0xA1)));
    TEST_ASSERT_EQUAL(int(Family::Bizjet), int(data::familyForCategory(0xA2)));
    TEST_ASSERT_EQUAL(int(Family::Narrowbody), int(data::familyForCategory(0xA3)));
    TEST_ASSERT_EQUAL(int(Family::Narrowbody), int(data::familyForCategory(0xA4)));
    TEST_ASSERT_EQUAL(int(Family::Widebody), int(data::familyForCategory(0xA5)));
    TEST_ASSERT_EQUAL(int(Family::FastJet), int(data::familyForCategory(0xA6)));
    TEST_ASSERT_EQUAL(int(Family::Helicopter), int(data::familyForCategory(0xA7)));
    TEST_ASSERT_EQUAL(int(Family::None), int(data::familyForCategory(0)));
    TEST_ASSERT_EQUAL(int(Family::None), int(data::familyForCategory(0xB1)));
    TEST_ASSERT_EQUAL(int(Family::Widebody), int(data::familyFor("B789", 0xA1)));  // the type wins
    TEST_ASSERT_EQUAL(int(Family::Helicopter), int(data::familyFor("ZZZZ", 0xA7)));
}

static void test_draw_top_covers_the_middle_and_stays_in_its_box() {
    std::vector<uint16_t> px(64 * 64, 0);
    gfx::Canvas c(px.data(), 64, 0, 64);
    data::drawTop(c, Family::Widebody, 32, 32, 40, 0, {255, 255, 255});
    TEST_ASSERT_NOT_EQUAL(0, c.get(32, 32));  // fuselage
    TEST_ASSERT_NOT_EQUAL(0, c.get(32 + 15, 36));  // a wing
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            if (x < 10 || x > 54 || y < 10 || y > 54) TEST_ASSERT_EQUAL(0, c.get(x, y));
}

static void test_draw_side_and_none() {
    std::vector<uint16_t> px(64 * 32, 0);
    gfx::Canvas c(px.data(), 64, 0, 32);
    data::drawSide(c, Family::Narrowbody, 2, 2, 60, {255, 255, 255}, {128, 128, 128});
    TEST_ASSERT_NOT_EQUAL(0, c.get(32, 14));  // the fuselage (y 19..29 of 40), 120x40 scaled by 0.5, box at y = 2
    std::vector<uint16_t> blank(64 * 32, 0);
    gfx::Canvas d(blank.data(), 64, 0, 32);
    data::drawSide(d, Family::None, 2, 2, 60, {255, 255, 255}, {128, 128, 128});
    for (uint16_t v : blank) TEST_ASSERT_EQUAL(0, v);
}

static void test_heading_90_points_the_nose_right() {
    std::vector<uint16_t> px(64 * 64, 0);
    gfx::Canvas c(px.data(), 64, 0, 64);
    data::drawTop(c, Family::Narrowbody, 32, 32, 40, 90, {255, 255, 255});
    int right = 0, top = 0;  // `top`: pixels off the centreline
    for (int y = 29; y <= 35; ++y)
        for (int x = 46; x <= 54; ++x) right += c.get(x, y) != 0;  // the nose, 0.4 * 40 px right of centre
    for (int y = 10; y <= 22; ++y)
        for (int x = 46; x <= 54; ++x) top += c.get(x, y) != 0;  // beside the nose, where a wing tip would be if the nose were up
    TEST_ASSERT_TRUE(right > 0);
    TEST_ASSERT_EQUAL(0, top);
    TEST_ASSERT_NOT_EQUAL(0, c.get(15, 27));  // the tailplane, left of centre (it lands right of centre if flipped)
    TEST_ASSERT_EQUAL(0, c.get(49, 27));      // beside the narrow nose
}

static void test_no_family_draws_outside_its_culling_radius() {
    const float px = 40, reach = px * 0.71f + 1;
    for (int fam = 1; fam <= data::kFamilyCount; ++fam)
        for (int heading : {0, 45, 90}) {
            std::vector<uint16_t> buf(100 * 100, 0);
            gfx::Canvas c(buf.data(), 100, 0, 100);
            data::drawTop(c, Family(fam), 50, 50, px, float(heading), {255, 255, 255});
            int inside = 0;
            for (int y = 0; y < 100; ++y)
                for (int x = 0; x < 100; ++x)
                    if (c.get(x, y)) {
                        ++inside;
                        float dx = x + 0.5f - 50, dy = y + 0.5f - 50;
                        TEST_ASSERT_TRUE(std::sqrt(dx * dx + dy * dy) <= reach);
                    }
            TEST_ASSERT_TRUE(inside > 0);
        }
}

static void test_strips_match_one_tall_canvas() {
    for (int what = 0; what < 2; ++what) {  // 0 = top view, 1 = side view
        auto draw = [&](gfx::Canvas& c) {
            if (what == 0) data::drawTop(c, Family::Helicopter, 24, 16, 28, 37, {255, 200, 100}, 200);
            else data::drawSide(c, Family::Jumbo, 4, 8, 42, {255, 200, 100}, {90, 90, 90}, 200);
        };
        std::vector<uint16_t> whole(48 * 32, 0), top(48 * 16, 0), bottom(48 * 16, 0);
        gfx::Canvas w(whole.data(), 48, 0, 32), t(top.data(), 48, 0, 16), b(bottom.data(), 48, 16, 16);
        draw(w);
        draw(t);
        draw(b);
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 48; ++x)
                TEST_ASSERT_EQUAL_UINT16(w.get(x, y), y < 16 ? t.get(x, y) : b.get(x, y));
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_every_outline_is_closed_and_inside_its_box);
    RUN_TEST(test_type_table_is_sorted);
    RUN_TEST(test_type_lookup);
    RUN_TEST(test_category_fallback);
    RUN_TEST(test_draw_top_covers_the_middle_and_stays_in_its_box);
    RUN_TEST(test_draw_side_and_none);
    RUN_TEST(test_heading_90_points_the_nose_right);
    RUN_TEST(test_no_family_draws_outside_its_culling_radius);
    RUN_TEST(test_strips_match_one_tall_canvas);
    return UNITY_END();
}
