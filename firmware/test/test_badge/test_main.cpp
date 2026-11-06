#include <unity.h>

#include <vector>

#include "gfx/Badge.h"

void setUp() {}
void tearDown() {}

using namespace gfx;

static void test_8bpp_draws_opaque_and_skips_transparent() {
    const uint16_t pal[3] = {rgb565(255, 0, 0), rgb565(0, 255, 0), 0};
    const uint8_t alpha[3] = {255, 255, 0};
    const uint8_t px[4] = {0, 1, 2, 0};  // 2 x 2
    Badge b{2, 2, 8, 3, pal, alpha, px};
    std::vector<uint16_t> buf(8 * 8, 0x1234);
    Canvas c(buf.data(), 8, 0, 8);
    drawBadge(c, b, 3, 3);
    TEST_ASSERT_EQUAL_HEX16(rgb565(255, 0, 0), c.get(3, 3));
    TEST_ASSERT_EQUAL_HEX16(rgb565(0, 255, 0), c.get(4, 3));
    TEST_ASSERT_EQUAL_HEX16(0x1234, c.get(3, 4));  // alpha 0
    TEST_ASSERT_EQUAL_HEX16(0x1234, c.get(2, 3));
}

static void test_4bpp_nibbles_and_odd_width() {
    const uint16_t pal[2] = {rgb565(0, 0, 0), rgb565(255, 255, 255)};
    const uint8_t alpha[2] = {255, 255};
    const uint8_t px[4] = {0x01, 0x00, 0x10, 0x10};  // 3 x 2: rows "0 1 0" and "1 0 1", each padded to 2 bytes
    Badge b{3, 2, 4, 2, pal, alpha, px};
    std::vector<uint16_t> buf(4 * 2, 0x1234);
    Canvas c(buf.data(), 4, 0, 2);
    drawBadge(c, b, 0, 0);
    TEST_ASSERT_EQUAL_HEX16(0x0000, c.get(0, 0));
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, c.get(1, 0));
    TEST_ASSERT_EQUAL_HEX16(0x0000, c.get(2, 0));
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, c.get(0, 1));
    TEST_ASSERT_EQUAL_HEX16(0x0000, c.get(1, 1));
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, c.get(2, 1));
    TEST_ASSERT_EQUAL_HEX16(0x1234, c.get(3, 1));
}

static void test_partial_alpha_and_clip() {
    const uint16_t pal[1] = {0xFFFF};
    const uint8_t alpha[1] = {128};
    const uint8_t px[1] = {0};
    Badge b{1, 1, 8, 1, pal, alpha, px};
    std::vector<uint16_t> buf(2 * 2, 0);
    Canvas c(buf.data(), 2, 0, 2);
    drawBadge(c, b, 0, 0);
    TEST_ASSERT_EQUAL_HEX16(blend565(0, uint16_t(0xFFFF), 128), c.get(0, 0));
    const std::vector<uint16_t> snap = buf;
    drawBadge(c, b, -1, 5);  // entirely outside: nothing happens
    TEST_ASSERT_TRUE(buf == snap);
    drawBadge(c, Badge{}, 0, 0);  // empty: nothing happens
    TEST_ASSERT_TRUE(buf == snap);
}

static void test_corrupt_index_leaves_pixel() {
    const uint16_t pal[2] = {0xF800, 0x07E0};
    const uint8_t alpha[2] = {255, 255};
    const uint8_t px[2] = {0xFF, 1};
    Badge b{2, 1, 8, 2, pal, alpha, px};
    std::vector<uint16_t> buf(4, 0x1234);
    Canvas c(buf.data(), 4, 0, 1);
    drawBadge(c, b, 0, 0);
    TEST_ASSERT_EQUAL_HEX16(0x1234, c.get(0, 0));
    TEST_ASSERT_EQUAL_HEX16(0x07E0, c.get(1, 0));
}

static void test_overall_opacity() {
    const uint16_t pal[2] = {0xFFFF, 0xFFFF};
    const uint8_t alpha[2] = {255, 128};
    const uint8_t px[2] = {0, 1};
    Badge b{2, 1, 8, 2, pal, alpha, px};
    std::vector<uint16_t> buf(2, 0x1234);
    Canvas c(buf.data(), 2, 0, 1);
    drawBadge(c, b, 0, 0, 128);
    TEST_ASSERT_EQUAL_HEX16(blend565(0x1234, uint16_t(0xFFFF), 128), c.get(0, 0));
    TEST_ASSERT_EQUAL_HEX16(blend565(0x1234, uint16_t(0xFFFF), uint8_t(div255(128 * 128))), c.get(1, 0));
}

static void test_4bpp_clipped_at_odd_column() {
    const uint16_t pal[3] = {0x0001, 0x0002, 0x0003};
    const uint8_t alpha[3] = {255, 255, 255};
    const uint8_t px[2] = {0x01, 0x20};  // 4 x 1: 0 1 2 0
    Badge b{4, 1, 4, 3, pal, alpha, px};
    std::vector<uint16_t> buf(4, 0x1234);
    Canvas c(buf.data(), 4, 0, 1);
    c.setClip({1, 0, 3, 1});  // starts on the odd pixel
    drawBadge(c, b, 0, 0);
    TEST_ASSERT_EQUAL_HEX16(0x1234, c.get(0, 0));
    TEST_ASSERT_EQUAL_HEX16(0x0002, c.get(1, 0));
    TEST_ASSERT_EQUAL_HEX16(0x0003, c.get(2, 0));
    TEST_ASSERT_EQUAL_HEX16(0x0001, c.get(3, 0));
}

static void test_band_with_offset_y() {
    const uint16_t pal[1] = {0xFFFF};
    const uint8_t alpha[1] = {255};
    const uint8_t px[4] = {0, 0, 0, 0};
    Badge b{2, 2, 8, 1, pal, alpha, px};
    std::vector<uint16_t> buf(4 * 2, 0);
    Canvas c(buf.data(), 4, 10, 2);  // rows 10..11
    drawBadge(c, b, 1, 9);           // badge rows 9..10: only 10 is in the band
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, c.get(1, 10));
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, c.get(2, 10));
    TEST_ASSERT_EQUAL_HEX16(0, c.get(1, 11));
}

static void test_bad_bpp_draws_nothing() {
    const uint16_t pal[1] = {0xFFFF};
    const uint8_t alpha[1] = {255}, px[8] = {0};
    std::vector<uint16_t> buf(4, 0x1234);
    Canvas c(buf.data(), 2, 0, 2);
    for (uint8_t bpp : {0, 1, 2}) {
        Badge b{2, 2, bpp, 1, pal, alpha, px};
        drawBadge(c, b, 0, 0);
    }
    Badge nul{2, 2, 8, 1, nullptr, alpha, px};
    drawBadge(c, nul, 0, 0);
    for (uint16_t v : buf) TEST_ASSERT_EQUAL_HEX16(0x1234, v);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_8bpp_draws_opaque_and_skips_transparent);
    RUN_TEST(test_4bpp_nibbles_and_odd_width);
    RUN_TEST(test_partial_alpha_and_clip);
    RUN_TEST(test_corrupt_index_leaves_pixel);
    RUN_TEST(test_overall_opacity);
    RUN_TEST(test_4bpp_clipped_at_odd_column);
    RUN_TEST(test_band_with_offset_y);
    RUN_TEST(test_bad_bpp_draws_nothing);
    return UNITY_END();
}
