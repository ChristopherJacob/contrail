// MapPack over the fixture (map_small.pak, 40 nm around 39.4176, -77.3743) and corrupted copies of it.
#include <unity.h>

#include <cmath>
#include <cstring>

#include "Fixture.h"
#include "TestStorage.h"
#include "assets/Pack.h"
#include "map/Features.h"
#include "map/MapPack.h"

void setUp() {}
void tearDown() {}

using basemap::MapPack;

static MemStorage sd;

static MemFile& card(const std::string& bytes) { return sd.add(MapPack::kPath, bytes); }

// A card that refused command CRCs can be read but not written safely, and the detailed map writes its chart.
static void test_a_card_without_crcs_is_not_used() {
    card(loadFixture("map_small.pak"));
    sd.isWritable = false;
    MapPack p;
    TEST_ASSERT_FALSE(p.begin(&sd));
    TEST_ASSERT_EQUAL_STRING("the card can't be written safely: CRC off", p.error());
    sd.isWritable = true;
    TEST_ASSERT_TRUE(p.begin(&sd));
}

static void test_opens_the_fixture() {
    card(loadFixture("map_small.pak"));
    MapPack p;
    TEST_ASSERT_TRUE(p.begin(&sd));
    TEST_ASSERT_EQUAL_STRING("", p.error());
    TEST_ASSERT_EQUAL(3, p.levelCount());
    TEST_ASSERT_EQUAL(40, p.coverNm());
    TEST_ASSERT_INT32_WITHIN(1, 3941760, p.homeLatE5());
    TEST_ASSERT_EQUAL(135168u, p.cacheSize());
    TEST_ASSERT_EQUAL(0u, p.cacheOffset() % 4096);
    TEST_ASSERT_EQUAL(0, p.damagedTiles());
}

static void test_level_by_radius() {
    card(loadFixture("map_small.pak"));
    MapPack p;
    p.begin(&sd);
    TEST_ASSERT_EQUAL(0, p.levelFor(5));
    TEST_ASSERT_EQUAL(0, p.levelFor(25));
    TEST_ASSERT_EQUAL(1, p.levelFor(26));
    TEST_ASSERT_EQUAL(1, p.levelFor(60));
    TEST_ASSERT_EQUAL(2, p.levelFor(61));
    TEST_ASSERT_EQUAL(2, p.levelFor(100));
    TEST_ASSERT_EQUAL(2, p.levelFor(500));
}

static void test_covers() {
    card(loadFixture("map_small.pak"));
    MapPack p;
    p.begin(&sd);
    // The view spans the radius across half the width and 1.125 x it across half the height.
    TEST_ASSERT_TRUE(p.covers(39.4176, -77.3743, 25));
    TEST_ASSERT_TRUE(p.covers(39.4176, -77.3743, 35));
    TEST_ASSERT_FALSE(p.covers(39.4176, -77.3743, 36));  // 1.125 x 36 > 40
    TEST_ASSERT_FALSE(p.covers(40.2, -77.3743, 10));     // home moved 47 nm north
    TEST_ASSERT_FALSE(p.covers(39.4176, -76.5, 10));     // 40.5 nm east
    TEST_ASSERT_TRUE(p.covers(39.5, -77.3, 10));         // moved a little
    MapPack closed;
    TEST_ASSERT_FALSE(closed.covers(39.4176, -77.3743, 5));
}

// Reads every feature of a tile with the reader its kind needs; each must end exactly where the table says.
static void check_features_decode(MapPack& p, const basemap::TileInfo& t, int& towns, int& airports) {
    for (int i = 0; i < t.featureCount; ++i) {
        basemap::FeatureEntry e;
        TEST_ASSERT_TRUE(p.feature(t, i, e));
        basemap::ByteStream s(*p.file(), e.begin, e.end);
        if (e.kind == basemap::kRing) {
            basemap::StreamRingReader r(s);
            basemap::RingPoint pt;
            int n = 0;
            while (r.next(pt)) ++n;
            TEST_ASSERT_EQUAL(r.count() + 1, n);
        } else if (e.kind == basemap::kTown || e.kind == basemap::kAirport) {
            basemap::Point pt;
            TEST_ASSERT_TRUE(basemap::readPoint(s, pt));
            TEST_ASSERT_TRUE(pt.name[0] != 0);
            (e.kind == basemap::kTown ? towns : airports)++;
        } else {
            basemap::LineReader r(s);
            int32_t x, y;
            int n = 0;
            while (r.next(x, y)) ++n;
            TEST_ASSERT_EQUAL(r.count(), n);
        }
        TEST_ASSERT_TRUE(s.ok());
        TEST_ASSERT_EQUAL(e.end, s.tell());
    }
}

static void test_tile_at_home_has_features() {
    card(loadFixture("map_small.pak"));
    MapPack p;
    p.begin(&sd);
    const basemap::PackLevel& lv = p.level(0);
    int row, col;
    lv.cell(39.4176, -77.3743, row, col);
    basemap::TileInfo t;
    TEST_ASSERT_TRUE(p.tile(0, row, col, t));
    TEST_ASSERT_TRUE(t.offset != 0);
    TEST_ASSERT_TRUE(t.featureCount > 0);  // Frederick: roads, KFDK, towns
    int towns = 0, airports = 0;
    check_features_decode(p, t, towns, airports);
    TEST_ASSERT_TRUE(towns > 0 && airports > 0);
}

static void test_every_tile_of_every_level_decodes() {
    card(loadFixture("map_small.pak"));
    MapPack p;
    TEST_ASSERT_TRUE(p.begin(&sd));
    int tiles = 0, towns = 0, airports = 0;
    for (int l = 0; l < p.levelCount(); ++l)
        for (int r = 0; r < p.level(l).rows; ++r)
            for (int c = 0; c < p.level(l).cols; ++c) {
                basemap::TileInfo t;
                if (!p.tile(l, r, c, t)) continue;
                ++tiles;
                check_features_decode(p, t, towns, airports);
            }
    TEST_ASSERT_TRUE(tiles > 10);
    TEST_ASSERT_EQUAL(0, p.damagedTiles());
    TEST_ASSERT_TRUE(p.isOpen());
}

static void test_tile_out_of_range_and_empty() {
    card(loadFixture("map_small.pak"));
    MapPack p;
    p.begin(&sd);
    basemap::TileInfo t;
    TEST_ASSERT_FALSE(p.tile(0, -1, 0, t));
    TEST_ASSERT_FALSE(p.tile(0, 0, p.level(0).cols, t));
    TEST_ASSERT_FALSE(p.tile(3, 0, 0, t));
    TEST_ASSERT_TRUE(p.isOpen());
}

static uint32_t rd32(const std::string& b, size_t at) {
    uint32_t v = 0;
    for (int i = 3; i >= 0; --i) v = v << 8 | uint8_t(b[at + i]);
    return v;
}
static void wr16(std::string& b, size_t at, uint32_t v) { b[at] = char(v), b[at + 1] = char(v >> 8); }
static void wr32(std::string& b, size_t at, uint32_t v) { wr16(b, at, v), wr16(b, at + 2, v >> 16); }
static uint16_t crcOf(const std::string& b, size_t at, size_t n) {
    return assets::crc16(reinterpret_cast<const uint8_t*>(b.data()) + at, n);
}

// Redoes every index CRC, the level table CRC and the header CRC, so a test can change one field and have the
// check behind the CRCs show.
static void refreshCrcs(std::string& b) {
    const int levels = uint8_t(b[18]);
    for (int l = 0; l < levels; ++l) {
        const size_t e = 64 + 24 * l;
        const size_t bytes = size_t(uint8_t(b[e + 12]) | uint8_t(b[e + 13]) << 8) *
                             (uint8_t(b[e + 14]) | uint8_t(b[e + 15]) << 8) * 8;
        wr16(b, e + 20, crcOf(b, rd32(b, e + 16), bytes));
    }
    wr16(b, 32, crcOf(b, 64, 24 * levels));
    wr16(b, 62, crcOf(b, 0, 62));
}

// The fixture with tile (row, col) of level `l` flagged all land (and, with `empty`, stripped of its data), the
// CRCs redone.
static std::string withLandFlag(int l, int row, int col, bool empty = false) {
    std::string b = loadFixture("map_small.pak");
    MapPack probe;
    card(b);
    probe.begin(&sd);
    const basemap::PackLevel& lv = probe.level(l);
    const size_t e = lv.indexOffset + (size_t(row) * lv.cols + col) * 8;
    if (empty) std::memset(&b[e], 0, 6);
    b[e + 6] |= 1;
    refreshCrcs(b);
    return b;
}

static void test_all_land_is_reported_with_and_without_data() {
    card(loadFixture("map_small.pak"));
    MapPack probe;
    probe.begin(&sd);
    int row, col;
    probe.level(0).cell(39.4176, -77.3743, row, col);
    basemap::TileInfo t;
    TEST_ASSERT_TRUE(probe.tile(0, row, col, t));
    card(withLandFlag(0, row, col));
    MapPack p;
    TEST_ASSERT_TRUE(p.begin(&sd));
    TEST_ASSERT_TRUE(p.tile(0, row, col, t));  // has data (inland Frederick), and is all land
    TEST_ASSERT_TRUE(t.allLand);
    TEST_ASSERT_TRUE(t.offset != 0);
    TEST_ASSERT_TRUE(p.tile(0, row, col, t, false));
    TEST_ASSERT_TRUE(t.allLand);
    // An empty tile flagged all land: no data, but allLand set.
    card(withLandFlag(0, 1, 1, true));
    TEST_ASSERT_TRUE(p.begin(&sd));
    TEST_ASSERT_FALSE(p.tile(0, 1, 1, t));
    TEST_ASSERT_TRUE(t.allLand);
    TEST_ASSERT_EQUAL(0u, t.offset);
}

static void test_refuses_a_bad_header() {
    std::string b = loadFixture("map_small.pak");
    b[10] ^= 1;  // inside the CRC'd header
    card(b);
    MapPack p;
    TEST_ASSERT_FALSE(p.begin(&sd));
    TEST_ASSERT_EQUAL_STRING("map.pak is damaged or from another version; rebuild it", p.error());
    TEST_ASSERT_FALSE(p.isOpen());
}

static void test_refuses_a_newer_version() {
    std::string b = loadFixture("map_small.pak");
    b[4] = 2;
    card(b);
    MapPack p;
    TEST_ASSERT_FALSE(p.begin(&sd));
}

static void test_refuses_a_truncated_pack() {
    std::string b = loadFixture("map_small.pak");
    card(b.substr(0, 30));
    MapPack p;
    TEST_ASSERT_FALSE(p.begin(&sd));
    TEST_ASSERT_EQUAL_STRING("map.pak is damaged or from another version; rebuild it", p.error());
    card(b.substr(0, b.size() - 4096));  // the reserved area cut short
    TEST_ASSERT_FALSE(p.begin(&sd));
}

static void test_refuses_a_bad_index() {
    card(loadFixture("map_small.pak"));
    MapPack probe;
    probe.begin(&sd);
    const uint32_t at = probe.level(1).indexOffset;
    std::string b = loadFixture("map_small.pak");
    b[at + 3] ^= 0x40;
    card(b);
    MapPack p;
    TEST_ASSERT_FALSE(p.begin(&sd));
    TEST_ASSERT_EQUAL_STRING("map.pak is damaged or from another version; rebuild it", p.error());
}

static void test_bad_tile_is_skipped_and_counted() {
    card(loadFixture("map_small.pak"));
    MapPack probe;
    probe.begin(&sd);
    int row, col;
    probe.level(0).cell(39.4176, -77.3743, row, col);
    basemap::TileInfo t;
    probe.tile(0, row, col, t);
    std::string b = loadFixture("map_small.pak");
    b[t.offset + t.size - 1] ^= 0xFF;  // the tile's last byte
    card(b);
    MapPack p;
    TEST_ASSERT_TRUE(p.begin(&sd));
    TEST_ASSERT_FALSE(p.tile(0, row, col, t));
    TEST_ASSERT_EQUAL(1, p.damagedTiles());
    TEST_ASSERT_EQUAL(0, t.featureCount);
    TEST_ASSERT_FALSE(p.tile(0, row, col, t));
    TEST_ASSERT_EQUAL(1, p.damagedTiles());  // counted once per tile
    TEST_ASSERT_TRUE(p.tile(0, row, col, t, false));  // unverified: the header alone is fine
    TEST_ASSERT_TRUE(p.isOpen());
}

static void test_missing_pack_and_no_card() {
    MemStorage empty;
    MapPack p;
    TEST_ASSERT_FALSE(p.begin(&empty));
    TEST_ASSERT_EQUAL_STRING("no map.pak", p.error());
    empty.isMounted = false;
    TEST_ASSERT_FALSE(p.begin(&empty));
    TEST_ASSERT_EQUAL_STRING("", p.error());
    TEST_ASSERT_FALSE(p.begin(nullptr));
    TEST_ASSERT_EQUAL_STRING("", p.error());
}

static void test_read_failure_turns_the_pack_off() {
    MemFile& f = card(loadFixture("map_small.pak"));
    MapPack p;
    TEST_ASSERT_TRUE(p.begin(&sd));
    f.fail = true;
    int row, col;
    p.level(0).cell(39.4176, -77.3743, row, col);
    basemap::TileInfo t;
    TEST_ASSERT_FALSE(p.tile(0, row, col, t));
    TEST_ASSERT_FALSE(p.isOpen());
    TEST_ASSERT_EQUAL_STRING("card error", p.error());
    f.fail = false;
    TEST_ASSERT_FALSE(p.tile(0, row, col, t));  // stays off until restart
    TEST_ASSERT_TRUE(p.begin(&sd));             // begin starts over
    TEST_ASSERT_TRUE(p.isOpen());
    f.fail = true;
    TEST_ASSERT_FALSE(p.begin(&sd));
    TEST_ASSERT_EQUAL_STRING("card error", p.error());
}


// --- damage that gets past the CRCs: the checks behind them -------------------------------------------------

// Redoes a tile's own CRC.
static void refreshTile(std::string& b, const basemap::TileInfo& t) {
    wr16(b, t.offset, crcOf(b, t.offset + 2, t.size - 2));
}

static basemap::TileInfo homeTile(std::string* bytes = nullptr) {
    card(bytes ? *bytes : loadFixture("map_small.pak"));
    MapPack p;
    TEST_ASSERT_TRUE(p.begin(&sd));
    int row, col;
    p.level(0).cell(39.4176, -77.3743, row, col);
    basemap::TileInfo t;
    TEST_ASSERT_TRUE(p.tile(0, row, col, t));
    return t;
}

static void test_the_helpers_leave_the_fixture_valid() {
    std::string b = loadFixture("map_small.pak");
    refreshCrcs(b);
    TEST_ASSERT_TRUE(b == loadFixture("map_small.pak"));
}

static void test_level_table_is_checked() {
    for (size_t at : {size_t(64 + 2), size_t(64 + 4), size_t(64 + 24 + 8), size_t(64 + 12)}) {
        std::string b = loadFixture("map_small.pak");
        b[at] ^= 1;  // tileMilliDeg, originLatE5, a second level's originLonE5, rows
        card(b);
        MapPack p;
        TEST_ASSERT_FALSE(p.begin(&sd));
        TEST_ASSERT_EQUAL_STRING("map.pak is damaged or from another version; rebuild it", p.error());
    }
}

static void test_levels_are_published_only_by_a_good_begin() {
    std::string b = loadFixture("map_small.pak");
    MapPack probe;
    card(b);
    probe.begin(&sd);
    b[probe.level(1).indexOffset + 3] ^= 0x40;
    card(b);
    MapPack p;
    TEST_ASSERT_FALSE(p.begin(&sd));
    TEST_ASSERT_EQUAL(0, p.levelCount());
    TEST_ASSERT_EQUAL(0, p.levelFor(50));  // closed: level 0, never -1
    card(loadFixture("map_small.pak"));
    TEST_ASSERT_TRUE(p.begin(&sd));
    TEST_ASSERT_EQUAL(3, p.levelCount());
    card(b);
    TEST_ASSERT_FALSE(p.begin(&sd));
    TEST_ASSERT_EQUAL(0, p.levelCount());
}

static void test_an_index_must_lie_between_the_level_table_and_the_saved_chart() {
    std::string good = loadFixture("map_small.pak");
    const uint32_t cacheAt = rd32(good, 24);
    std::string b = good;
    wr32(b, 64 + 16, cacheAt);  // the index inside the saved chart's area (zeros, so its CRC can be made right)
    refreshCrcs(b);
    card(b);
    MapPack p;
    TEST_ASSERT_FALSE(p.begin(&sd));
    b = good;
    wr32(b, 64 + 16, 64);  // the index over the header and level table
    refreshCrcs(b);
    card(b);
    TEST_ASSERT_FALSE(p.begin(&sd));
    b = good;
    wr32(b, 64 + 16, cacheAt - 8);  // the last entry spills over
    refreshCrcs(b);
    card(b);
    TEST_ASSERT_FALSE(p.begin(&sd));
}

// A pack from before the chart was saved in tiles reserves 128 KB: too small, so it is from another version.
static void test_an_old_size_saved_chart_area_is_refused() {
    std::string b = loadFixture("map_small.pak");
    wr32(b, 28, 131072);
    refreshCrcs(b);
    card(b);
    MapPack p;
    TEST_ASSERT_FALSE(p.begin(&sd));
    TEST_ASSERT_EQUAL_STRING("map.pak is damaged or from another version; rebuild it", p.error());
}

static void test_a_tile_must_lie_inside_the_tile_data() {
    std::string good = loadFixture("map_small.pak");
    MapPack probe;
    card(good);
    probe.begin(&sd);
    const uint32_t cacheAt = probe.cacheOffset();
    const size_t entry = probe.level(0).indexOffset;  // tile (0, 0)'s index entry
    basemap::TileInfo t;
    std::string b = good;
    wr32(b, entry, cacheAt), wr16(b, entry + 4, 4);  // four zero bytes in the saved chart's area
    refreshCrcs(b);
    card(b);
    MapPack p;
    TEST_ASSERT_TRUE(p.begin(&sd));
    TEST_ASSERT_FALSE(p.tile(0, 0, 0, t, false));
    TEST_ASSERT_TRUE(t.offset != 0);
    b = good;
    wr16(b, entry + 4, 2);  // too small to hold its own header
    refreshCrcs(b);
    card(b);
    TEST_ASSERT_TRUE(p.begin(&sd));
    TEST_ASSERT_FALSE(p.tile(0, 0, 0, t, false));
}

static void test_a_feature_count_beyond_the_tile_is_damage() {
    basemap::TileInfo t = homeTile();
    std::string b = loadFixture("map_small.pak");
    wr16(b, t.offset + 2, 0xFFFF);
    refreshTile(b, t);
    card(b);
    MapPack p;
    TEST_ASSERT_TRUE(p.begin(&sd));
    TEST_ASSERT_FALSE(p.tile(0, t.row, t.col, t));
    TEST_ASSERT_EQUAL(1, p.damagedTiles());
    TEST_ASSERT_FALSE(p.tile(0, t.row, t.col, t, false));  // and without the CRC pass
}

static void test_features_must_lie_inside_the_tile_after_its_table() {
    const basemap::TileInfo want = homeTile();
    TEST_ASSERT_TRUE(want.featureCount >= 3);
    const size_t table = want.offset + 4;
    const int last = want.featureCount - 1;
    struct Case {
        int feature;       // whose dataOffset is changed
        uint32_t to;
        int asked;         // the feature feature() is asked for
    };
    const Case cases[] = {
        {0, 4, 0},                       // starts inside the feature table
        {last, want.size, last},         // the last one starts at the tile's end
        {0, uint32_t(want.size - 1), 0}, // starts after the next one does
        {1, 0xFFFF, 0},                  // the next one starts beyond the tile
    };
    for (const Case& c : cases) {
        std::string b = loadFixture("map_small.pak");
        wr16(b, table + 8 * c.feature + 2, c.to);
        refreshTile(b, want);
        card(b);
        MapPack p;
        TEST_ASSERT_TRUE(p.begin(&sd));
        basemap::TileInfo t;
        TEST_ASSERT_TRUE(p.tile(0, want.row, want.col, t));
        basemap::FeatureEntry e;
        TEST_ASSERT_FALSE(p.feature(t, c.asked, e));
    }
    // The unchanged tile is fine, and the index is bounded.
    card(loadFixture("map_small.pak"));
    MapPack p;
    p.begin(&sd);
    basemap::TileInfo t;
    TEST_ASSERT_TRUE(p.tile(0, want.row, want.col, t));
    basemap::FeatureEntry e;
    TEST_ASSERT_TRUE(p.feature(t, 0, e));
    TEST_ASSERT_TRUE(p.feature(t, t.featureCount - 1, e));
    TEST_ASSERT_EQUAL(t.offset + t.size, e.end);
    TEST_ASSERT_FALSE(p.feature(t, -1, e));
    TEST_ASSERT_FALSE(p.feature(t, t.featureCount, e));
}

static void test_damaged_tiles_stop_counting_when_the_set_is_full() {
    card(loadFixture("map_small.pak"));
    MapPack probe;
    probe.begin(&sd);
    std::string b = loadFixture("map_small.pak");
    int tiles = 0;
    for (int l = 0; l < probe.levelCount(); ++l)
        for (int r = 0; r < probe.level(l).rows; ++r)
            for (int c = 0; c < probe.level(l).cols; ++c) {
                basemap::TileInfo t;
                if (!probe.tile(l, r, c, t)) continue;
                b[t.offset + t.size - 1] ^= 0xFF;
                ++tiles;
            }
    TEST_ASSERT_TRUE(tiles > 16);
    card(b);
    MapPack p;
    TEST_ASSERT_TRUE(p.begin(&sd));
    for (int pass = 0; pass < 2; ++pass)
        for (int l = 0; l < p.levelCount(); ++l)
            for (int r = 0; r < p.level(l).rows; ++r)
                for (int c = 0; c < p.level(l).cols; ++c) {
                    basemap::TileInfo t;
                    p.tile(l, r, c, t);
                }
    TEST_ASSERT_EQUAL(16, p.damagedTiles());  // saturated, not recounted
    TEST_ASSERT_TRUE(p.isOpen());
}

static void test_covers_away_from_home() {
    card(loadFixture("map_small.pak"));
    MapPack p;
    p.begin(&sd);
    const double hl = 39.4176, hn = -77.3743, rad = 3.14159265358979 / 180;
    // A view that sits near the limit east of a home moved north or south: its half width in nautical miles
    // follows the cosine of its own latitude, not home's. radius 30 needs 33.75 nm of latitude.
    for (double dLat : {0.1, -0.1}) {
        const double lat = hl + dLat, r = 30;
        const double viewNm = r * std::cos(hl * rad) / std::cos(lat * rad);  // in nm along home's parallel
        const double lonNm = 40 - viewNm;                                    // the east edge exactly at the limit
        const double perDeg = 60 * std::cos(hl * rad);
        TEST_ASSERT_TRUE(p.covers(lat, hn + (lonNm - 0.05) / perDeg, float(r)));
        TEST_ASSERT_FALSE(p.covers(lat, hn + (lonNm + 0.05) / perDeg, float(r)));
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_opens_the_fixture);
    RUN_TEST(test_a_card_without_crcs_is_not_used);
    RUN_TEST(test_level_by_radius);
    RUN_TEST(test_covers);
    RUN_TEST(test_tile_at_home_has_features);
    RUN_TEST(test_every_tile_of_every_level_decodes);
    RUN_TEST(test_tile_out_of_range_and_empty);
    RUN_TEST(test_all_land_is_reported_with_and_without_data);
    RUN_TEST(test_refuses_a_bad_header);
    RUN_TEST(test_refuses_a_newer_version);
    RUN_TEST(test_refuses_a_truncated_pack);
    RUN_TEST(test_refuses_a_bad_index);
    RUN_TEST(test_bad_tile_is_skipped_and_counted);
    RUN_TEST(test_missing_pack_and_no_card);
    RUN_TEST(test_read_failure_turns_the_pack_off);
    RUN_TEST(test_the_helpers_leave_the_fixture_valid);
    RUN_TEST(test_level_table_is_checked);
    RUN_TEST(test_levels_are_published_only_by_a_good_begin);
    RUN_TEST(test_an_index_must_lie_between_the_level_table_and_the_saved_chart);
    RUN_TEST(test_an_old_size_saved_chart_area_is_refused);
    RUN_TEST(test_a_tile_must_lie_inside_the_tile_data);
    RUN_TEST(test_a_feature_count_beyond_the_tile_is_damage);
    RUN_TEST(test_features_must_lie_inside_the_tile_after_its_table);
    RUN_TEST(test_damaged_tiles_stop_counting_when_the_set_is_full);
    RUN_TEST(test_covers_away_from_home);
    return UNITY_END();
}
