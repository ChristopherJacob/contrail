// ChartCache: the key's write order and each field, tiles in and out.
#include <unity.h>

#include <vector>

#include "TestStorage.h"
#include "map/ChartCache.h"

void setUp() {}
void tearDown() {}

using basemap::ChartCache;
using basemap::ChartKey;

static MemFile pack() {
    MemFile f;
    f.bytes.assign(4096 + ChartCache::kAreaBytes, 0);
    return f;
}

static ChartKey key() {
    ChartKey k;
    k.latE5 = 3941760, k.lonE5 = -7737430, k.radiusNm10 = 250, k.theme = 0x1234, k.level = 0, k.packId = 0xABCD,
    k.format = basemap::kChartFormat;
    return k;
}

// A pixel's value: unique over the chart and its padding rows (272 x 240 < 65,536).
static uint16_t pix(int x, int y) { return uint16_t(y * 240 + x); }
static std::vector<uint16_t> band(int by) {
    std::vector<uint16_t> px(240 * 16);
    for (int r = 0; r < 16; ++r)
        for (int x = 0; x < 240; ++x) px[r * 240 + x] = pix(x, by * 16 + r);
    return px;
}
static uint16_t at(const MemFile& f, uint32_t off) { return uint16_t(f.bytes[off] | f.bytes[off + 1] << 8); }

// Tiles first, from the area's start: tile (tx, ty) is sector ty * 15 + tx, its pixel rows 32 bytes apart. A band is
// 15 consecutive sectors. Then the key, then the extra bytes, and all of it inside the 33 x 4 KB area.
static void test_layout_of_the_area() {
    MemFile f = pack();
    ChartCache c(f, 4096, ChartCache::kAreaBytes);
    for (int by : {0, 3, 16}) {
        std::vector<uint16_t> px = band(by);
        TEST_ASSERT_TRUE(c.writeBand(by, px.data()));
        TEST_ASSERT_TRUE(px == band(by));  // given back as it came
    }
    for (int by : {0, 3, 16})
        for (int tx : {0, 7, 14})
            for (int r : {0, 9, 15})
                for (int col : {0, 5, 15})
                    TEST_ASSERT_EQUAL_HEX16(pix(tx * 16 + col, by * 16 + r),
                                            at(f, 4096 + (by * 15 + tx) * 512 + r * 32 + col * 2));
    TEST_ASSERT_EQUAL(130560u, ChartCache::kKeyAt);
    TEST_ASSERT_TRUE(c.finish(key()));
    TEST_ASSERT_EQUAL_MEMORY("CHR1", &f.bytes[4096 + 130560], 4);
    TEST_ASSERT_EQUAL_HEX16(pix(239, 271), at(f, 4096 + 130560 - 2));  // the last tile's last pixel, before the key
    const uint8_t first = 0x5A, last = 0xA5;
    TEST_ASSERT_TRUE(c.writeExtra(0, &first, 1));
    TEST_ASSERT_TRUE(c.writeExtra(ChartCache::kExtraBytes - 1, &last, 1));
    TEST_ASSERT_EQUAL_HEX8(first, f.bytes[4096 + 130592]);
    TEST_ASSERT_EQUAL_HEX8(last, f.bytes[4096 + 132031]);
    TEST_ASSERT_TRUE(ChartCache::kExtraBytes >= 1284);  // 4 + 32 label marks of 40 B
    TEST_ASSERT_EQUAL(132032u, ChartCache::kBytes);
    TEST_ASSERT_EQUAL(135168u, ChartCache::kAreaBytes);
    TEST_ASSERT_TRUE(ChartCache::kBytes <= ChartCache::kAreaBytes);
    // An area one byte short does not count.
    ChartCache small(f, 4096, ChartCache::kBytes - 1);
    TEST_ASSERT_FALSE(small.matches(key()));
}

// Rows to tiles and back, for every shape a read or a band uses.
static void test_transpose_round_trips() {
    for (int n = 1; n <= 15; ++n) {
        std::vector<uint16_t> p(16 * n * 16), orig;
        for (size_t i = 0; i < p.size(); ++i) p[i] = uint16_t(i);
        orig = p;
        ChartCache::transpose(p.data(), 16, n);
        // Chunk (row r, column t) now sits at t * 16 + r.
        for (int r = 0; r < 16; ++r)
            for (int t = 0; t < n; ++t) TEST_ASSERT_EQUAL(orig[(r * n + t) * 16], p[(t * 16 + r) * 16]);
        ChartCache::transpose(p.data(), n, 16);
        TEST_ASSERT_TRUE(p == orig);
    }
}

static void test_tiles_round_trip_and_key_is_written_last() {
    MemFile f = pack();
    ChartCache c(f, 4096, ChartCache::kAreaBytes);
    TEST_ASSERT_TRUE(c.begin(key()));
    TEST_ASSERT_FALSE(c.matches(key()));  // begun, not finished
    for (int by = 0; by < ChartCache::kTilesY; ++by) {
        std::vector<uint16_t> px = band(by);
        TEST_ASSERT_TRUE(c.writeBand(by, px.data()));
    }
    TEST_ASSERT_FALSE(c.matches(key()));  // tiles in, key not yet
    TEST_ASSERT_TRUE(c.finish(key()));
    TEST_ASSERT_TRUE(c.matches(key()));
    // A whole band in one read, a band of whole tiles, then boxes that are not whole tiles, across tile and band
    // borders, and the padding rows.
    const int rects[][4] = {{0, 32, 240, 16}, {48, 160, 32, 16}, {10, 40, 30, 1},  {5, 13, 40, 22},
                            {0, 0, 240, 272}, {239, 269, 1, 3},  {100, 254, 37, 9}, {16, 16, 16, 15}};
    for (const auto& q : rects) {
        const int x = q[0], y = q[1], w = q[2], h = q[3], stride = w + 3;
        std::vector<uint16_t> out(size_t(stride) * h, 0xEEEE);
        TEST_ASSERT_TRUE(c.readRect(x, y, w, h, out.data(), stride));
        for (int r = 0; r < h; ++r)
            for (int i = 0; i < stride; ++i)
                TEST_ASSERT_EQUAL_HEX16(i < w ? pix(x + i, y + r) : 0xEEEE, out[r * stride + i]);
        std::vector<uint16_t> tight(size_t(w) * h);  // stride == w: the one-read path where it applies
        TEST_ASSERT_TRUE(c.readRect(x, y, w, h, tight.data(), w));
        for (int r = 0; r < h; ++r)
            for (int i = 0; i < w; ++i) TEST_ASSERT_EQUAL_HEX16(pix(x + i, y + r), tight[r * w + i]);
    }
    TEST_ASSERT_TRUE(c.matches(key()));  // tiles never touch the key
}

static void test_empty_area_is_not_valid() {
    MemFile f = pack();
    ChartCache c(f, 4096, ChartCache::kAreaBytes);
    TEST_ASSERT_FALSE(c.matches(key()));
}

static void test_restart_mid_rebuild_reads_as_invalid() {
    MemFile f = pack();
    {
        ChartCache c(f, 4096, ChartCache::kAreaBytes);
        c.begin(key());
        c.finish(key());
        c.begin(key());  // a second rebuild starts and the board restarts
    }
    ChartCache again(f, 4096, ChartCache::kAreaBytes);
    TEST_ASSERT_FALSE(again.matches(key()));
}

static void test_each_key_field_matters() {
    MemFile f = pack();
    ChartCache c(f, 4096, ChartCache::kAreaBytes);
    c.begin(key());
    c.finish(key());
    ChartKey k = key();
    k.latE5 += 1;
    TEST_ASSERT_FALSE(c.matches(k));
    k = key(), k.lonE5 += 1;
    TEST_ASSERT_FALSE(c.matches(k));
    k = key(), k.radiusNm10 += 1;
    TEST_ASSERT_FALSE(c.matches(k));
    k = key(), k.theme += 1;
    TEST_ASSERT_FALSE(c.matches(k));
    k = key(), k.level = 1;
    TEST_ASSERT_FALSE(c.matches(k));
    k = key(), k.packId += 1;
    TEST_ASSERT_FALSE(c.matches(k));
    k = key(), k.format += 1;
    TEST_ASSERT_FALSE(c.matches(k));
    TEST_ASSERT_TRUE(c.matches(key()));
}

static void test_extra_round_trip_and_bounds() {
    MemFile f = pack();
    ChartCache c(f, 4096, ChartCache::kAreaBytes);
    const char msg[] = "marks";
    TEST_ASSERT_TRUE(c.writeExtra(10, msg, sizeof msg));
    char back[sizeof msg];
    TEST_ASSERT_TRUE(c.readExtra(10, back, sizeof back));
    TEST_ASSERT_EQUAL_STRING(msg, back);
    TEST_ASSERT_FALSE(c.writeExtra(ChartCache::kExtraBytes - 2, msg, sizeof msg));
    TEST_ASSERT_FALSE(c.readExtra(ChartCache::kExtraBytes + 1, back, 1));
    TEST_ASSERT_TRUE(c.writeExtra(ChartCache::kExtraBytes - sizeof msg, msg, sizeof msg));  // the last bytes fit
    // Extra bytes do not disturb the key or the first row.
    c.begin(key());
    c.finish(key());
    TEST_ASSERT_TRUE(c.writeExtra(0, msg, sizeof msg));
    TEST_ASSERT_TRUE(c.matches(key()));
    uint16_t px = 1;
    TEST_ASSERT_TRUE(c.readRect(239, 271, 1, 1, &px, 1));  // the pixel next to the key
    TEST_ASSERT_EQUAL(0, px);
}

static void test_extra_and_tiles_do_not_disturb_each_other() {
    MemFile f = pack();
    ChartCache c(f, 4096, ChartCache::kAreaBytes);
    std::vector<uint16_t> px(240 * 16, 0xFFFF), back(240 * 16);
    TEST_ASSERT_TRUE(c.writeBand(16, px.data()));  // the last band, just before the key
    TEST_ASSERT_TRUE(c.finish(key()));
    TEST_ASSERT_TRUE(c.matches(key()));  // the band left the key alone
    const uint8_t first = 0x5A;
    TEST_ASSERT_TRUE(c.writeExtra(0, &first, 1));  // the extra bytes' first, just after the key
    TEST_ASSERT_TRUE(c.matches(key()));
    TEST_ASSERT_TRUE(c.readRect(0, 256, 240, 16, back.data(), 240));
    TEST_ASSERT_TRUE(px == back);  // the band is whole
    // And the other way: the band rewritten leaves key and extra alone.
    for (auto& p : px) p = 0x1234;
    TEST_ASSERT_TRUE(c.writeBand(16, px.data()));
    uint8_t got = 0;
    TEST_ASSERT_TRUE(c.readExtra(0, &got, 1));
    TEST_ASSERT_EQUAL_HEX8(first, got);
    TEST_ASSERT_TRUE(c.matches(key()));
    TEST_ASSERT_TRUE(c.readRect(0, 256, 240, 16, back.data(), 240));
    TEST_ASSERT_TRUE(px == back);
}

static void test_write_failure_is_reported() {
    MemFile f = pack();
    ChartCache c(f, 4096, ChartCache::kAreaBytes);
    f.writeFail = true;
    TEST_ASSERT_FALSE(c.begin(key()));
    TEST_ASSERT_FALSE(c.finish(key()));
    std::vector<uint16_t> px = band(2);
    TEST_ASSERT_FALSE(c.writeBand(2, px.data()));
    TEST_ASSERT_TRUE(px == band(2));  // given back in row order all the same
    TEST_ASSERT_FALSE(c.writeExtra(0, px.data(), 4));
}

static void test_a_too_small_area_never_matches() {
    MemFile f = pack();
    ChartCache c(f, 4096, 65536);
    TEST_ASSERT_TRUE(c.finish(key()));
    TEST_ASSERT_FALSE(c.matches(key()));
}

static void test_tiles_outside_the_chart_are_refused() {
    MemFile f = pack();
    ChartCache c(f, 4096, ChartCache::kAreaBytes);
    std::vector<uint16_t> px(240 * 272);
    TEST_ASSERT_FALSE(c.writeBand(17, px.data()));
    TEST_ASSERT_FALSE(c.writeBand(-1, px.data()));
    TEST_ASSERT_FALSE(c.readRect(0, 271, 1, 2, px.data(), 1));  // past the padding
    TEST_ASSERT_FALSE(c.readRect(230, 0, 11, 1, px.data(), 11));
    TEST_ASSERT_FALSE(c.readRect(-1, 0, 1, 1, px.data(), 1));
    TEST_ASSERT_FALSE(c.readRect(0, 0, 10, 1, px.data(), 9));  // a stride narrower than the rows
    TEST_ASSERT_FALSE(c.readRect(0, 0, 0, 1, px.data(), 1));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_layout_of_the_area);
    RUN_TEST(test_empty_area_is_not_valid);
    RUN_TEST(test_transpose_round_trips);
    RUN_TEST(test_tiles_round_trip_and_key_is_written_last);
    RUN_TEST(test_restart_mid_rebuild_reads_as_invalid);
    RUN_TEST(test_each_key_field_matters);
    RUN_TEST(test_extra_round_trip_and_bounds);
    RUN_TEST(test_extra_and_tiles_do_not_disturb_each_other);
    RUN_TEST(test_write_failure_is_reported);
    RUN_TEST(test_a_too_small_area_never_matches);
    RUN_TEST(test_tiles_outside_the_chart_are_refused);
    return UNITY_END();
}
