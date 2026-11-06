#include <unity.h>

#include <vector>

#include "gfx/fonts/Fonts.h"
#include "ui/Widgets.h"

using namespace gfx;

void setUp() {}
void tearDown() {}

static const Rgb kWhite{255, 255, 255};

static void test_empty_width_zero() { TEST_ASSERT_EQUAL(0, textWidth(kFontBody, "")); }

static void test_width_is_sum_of_advances() {
    TEST_ASSERT_EQUAL(2 * textWidth(kFontBody, "A"), textWidth(kFontBody, "AA"));
    TEST_ASSERT_TRUE(textWidth(kFontHero, "W") > textWidth(kFontSmall, "W"));
}

static void test_utf8_degree_sign_decodes() {
    const char* s = "°";
    TEST_ASSERT_EQUAL_HEX32(0xB0, nextCodepoint(s));
    TEST_ASSERT_EQUAL(0, nextCodepoint(s));
    TEST_ASSERT_TRUE(textWidth(kFontBody, "°") > 0);
}

static void test_unknown_codepoint_advances_space() {
    TEST_ASSERT_EQUAL(kFontBody.spaceAdv, textWidth(kFontBody, "\xE2\x9C\x88"));  // ✈ not in table
}

static void test_glyph_ink_stays_in_line_box() {
    std::vector<uint16_t> buf(64 * 40, 0);
    Canvas c(buf.data(), 64, 0, 40);
    int base = 30;
    drawText(c, kFontBold, 10, base, "I", kWhite);
    int inked = 0;
    for (int y = 0; y < 40; ++y)
        for (int x = 0; x < 64; ++x)
            if (buf[y * 64 + x]) {
                ++inked;
                TEST_ASSERT_TRUE(y >= base - kFontBold.ascent && y <= base);
                TEST_ASSERT_TRUE(x >= 10 && x < 10 + textWidth(kFontBold, "I") + 2);
            }
    TEST_ASSERT_TRUE(inked > 10);
}

static void test_text_across_band_split_matches() {
    const int W = 200, H = 64;
    std::vector<uint16_t> whole(W * H, 0), banded(W * H, 0), strip(W * 32);
    Canvas full(whole.data(), W, 0, H);
    drawText(full, kFontHero, 4, 44, "SFO 1423", kWhite);
    for (int y0 = 0; y0 < H; y0 += 32) {
        std::fill(strip.begin(), strip.end(), 0);
        Canvas c(strip.data(), W, y0, 32);
        drawText(c, kFontHero, 4, 44, "SFO 1423", kWhite);
        std::copy(strip.begin(), strip.end(), banded.begin() + y0 * W);
    }
    TEST_ASSERT_EQUAL_MEMORY(whole.data(), banded.data(), whole.size() * 2);
}

static void test_center_alignment() {
    std::vector<uint16_t> buf(100 * 30, 0);
    Canvas c(buf.data(), 100, 0, 30);
    int w = textWidth(kFontBody, "ORD");
    int end = drawText(c, kFontBody, 50, 20, "ORD", kWhite, Align::Center);
    TEST_ASSERT_EQUAL(50 - w / 2 + w, end);
}

// A buffer too small for the text must not keep half of a multi-byte character.
static void test_ellipsize_cuts_on_a_character_boundary() {
    char out[5];
    ui::ellipsize(out, sizeof out, kFontBody, "ab°cd", 1000);  // ° is 2 bytes; the copy keeps 4
    TEST_ASSERT_EQUAL_STRING("ab°", out);
    ui::ellipsize(out, sizeof out, kFontBody, "abc°d", 1000);  // the cut falls inside the °
    TEST_ASSERT_EQUAL_STRING("abc", out);
}

// Bold Editorial's display face: condensed (narrower than Inter even at a larger size), taller.
static void test_condensed_display_fonts() {
    TEST_ASSERT_TRUE(textWidth(kFontCondHero, "UAL1234") < textWidth(kFontHero, "UAL1234"));
    TEST_ASSERT_TRUE(kFontCondHero.ascent > kFontHero.ascent);
    TEST_ASSERT_TRUE(textWidth(kFontCondTitle, "6 FLIGHTS") > 0);
    TEST_ASSERT_TRUE(textWidth(kFontCondLabel, "Quiet skies") > 0);
    TEST_ASSERT_TRUE(textWidth(kFontCondLabel, "UAL53") < textWidth(kFontBold, "UAL53") + 6);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_empty_width_zero);
    RUN_TEST(test_width_is_sum_of_advances);
    RUN_TEST(test_utf8_degree_sign_decodes);
    RUN_TEST(test_unknown_codepoint_advances_space);
    RUN_TEST(test_glyph_ink_stays_in_line_box);
    RUN_TEST(test_text_across_band_split_matches);
    RUN_TEST(test_center_alignment);
    RUN_TEST(test_ellipsize_cuts_on_a_character_boundary);
    RUN_TEST(test_condensed_display_fonts);
    return UNITY_END();
}
