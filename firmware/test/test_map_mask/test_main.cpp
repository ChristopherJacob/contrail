#include <unity.h>

#include <cmath>

#include "map/LandMask.h"

using namespace basemap;

void setUp() {}
void tearDown() {}

static uint8_t bits[LandMask::kBytes];

// Tile (45, 90) = lat 0..2, lon 0..2: an island (0.5..1.5°) holding a lake (~0.88..1.12°);
// its eastern neighbour (45, 91) is solid land.
static const uint8_t kIsland[] = {
    2, 0,                                                  // two rings
    4, 0, 0x00, 0x02, 0x00, 0x02,                          // island: 4 points from (512, 512)
    0x82, 0x00, 0x04, 0x00,                                // medium: +1024, 0
    0x82, 0x00, 0x00, 0x40,                                // medium: 0, +1024
    0x82, 0x00, 0x0C, 0x00,                                // medium: -1024, 0
    4, 0, 0x84, 0x03, 0x84, 0x03,                          // lake: 4 points from (900, 900)
    0x82, 0xFA, 0x00, 0x00,                                // medium: +250, 0
    0x82, 0x00, 0xA0, 0x0F,                                // medium: 0, +250
    0x82, 0x06, 0x0F, 0x00,                                // medium: -250, 0
};

class TestTiles : public TileSource {
   public:
    TileRef tile(int row, int col) const override {
        TileRef t;
        if (row == 45 && col == 90) t.kind = TileRef::Rings, t.data = kIsland, t.size = sizeof kIsland;
        if (row == 45 && col == 91) t.kind = TileRef::Land;
        return t;
    }
};

// Home at (1, 1), radius 120 nm: 1 px per nm, 60 px per degree.
static void test_island_lake_and_full_tile() {
    LandMask m(bits);
    MapView v = viewFor(1.0, 1.0, 120);
    TEST_ASSERT_EQUAL(2, m.build(v, TestTiles()));
    TEST_ASSERT_FALSE(m.landAtScreen(120, 135));  // home: in the lake
    TEST_ASSERT_TRUE(m.landAtScreen(100, 135));   // the island, west of the lake
    TEST_ASSERT_FALSE(m.landAtScreen(70, 135));   // sea west of the island
    TEST_ASSERT_FALSE(m.landAtScreen(176, 135));  // sea, just inside the home tile
    TEST_ASSERT_TRUE(m.landAtScreen(184, 135));   // the full tile east of it
    TEST_ASSERT_FALSE(m.landAtScreen(184, 16));   // north of the full tile (no data there)
}

static void test_coverage_is_smoothed_at_the_coast() {
    LandMask m(bits);
    m.build(viewFor(1.0, 1.0, 120), TestTiles());
    TEST_ASSERT_EQUAL(0, m.coverage16(70, 135));
    TEST_ASSERT_EQUAL(16, m.coverage16(100, 135));
    int edge = m.coverage16(89, 135);  // the island's west shore is at x = 90
    TEST_ASSERT_TRUE(edge > 0 && edge < 16);
}

// A ring header with no points has no edges but must not hide the rings after it.
static const uint8_t kEmptyThenIsland[] = {
    3, 0,                                                  // three rings
    0, 0, 0, 0, 0, 0,                                      // no points
    4, 0, 0x00, 0x02, 0x00, 0x02,                          // the island again
    0x82, 0x00, 0x04, 0x00, 0x82, 0x00, 0x00, 0x40, 0x82, 0x00, 0x0C, 0x00,
};

// The island, then a ring cut short after one long step up the tile at x = 100: drawn, that open
// edge would flip everything east of it.
static const uint8_t kIslandThenTruncated[] = {
    2, 0,
    4, 0, 0x00, 0x02, 0x00, 0x02,
    0x82, 0x00, 0x04, 0x00, 0x82, 0x00, 0x00, 0x40, 0x82, 0x00, 0x0C, 0x00,
    4, 0, 100, 0, 100, 0,                                  // 4 points from (100, 100)
    0x80, 0x00, 0x00, 0x6C, 0x07,                          // long: 0, +1900; then the data ends
};

class OneTile : public TileSource {
   public:
    OneTile(const uint8_t* d, uint32_t n) : d_(d), n_(n) {}
    TileRef tile(int row, int col) const override {
        TileRef t;
        if (row == 45 && col == 90) t.kind = TileRef::Rings, t.data = d_, t.size = n_;
        return t;
    }

   private:
    const uint8_t* d_;
    uint32_t n_;
};

static void test_empty_ring_does_not_stop_the_tile() {
    LandMask m(bits);
    m.build(viewFor(1.0, 1.0, 120), OneTile(kEmptyThenIsland, sizeof kEmptyThenIsland));
    TEST_ASSERT_TRUE(m.landAtScreen(100, 135));
    TEST_ASSERT_TRUE(m.landAtScreen(120, 135));  // no lake this time
    TEST_ASSERT_FALSE(m.landAtScreen(70, 135));
    TEST_ASSERT_FALSE(m.landAtScreen(176, 135));
}

static void test_truncated_ring_is_left_out() {
    LandMask m(bits);
    m.build(viewFor(1.0, 1.0, 120), OneTile(kIslandThenTruncated, sizeof kIslandThenTruncated));
    TEST_ASSERT_TRUE(m.landAtScreen(100, 135));
    TEST_ASSERT_FALSE(m.landAtScreen(70, 135));
    TEST_ASSERT_FALSE(m.landAtScreen(176, 135));
}

// Land tiles in rows [r0, r0 + rows) and columns [c0, c0 + cols); with ringCol >= 0, that column's
// tiles are a ring covering the whole tile instead of full land (the seam between the two kinds).
static const uint8_t kWholeTileRing[] = {
    1, 0,
    4, 0, 0, 0, 0, 0,                // 4 points from (0, 0)
    0x80, 0x00, 0x08, 0x00, 0x00,    // long: +2048, 0
    0x80, 0x00, 0x00, 0x00, 0x08,    // long: 0, +2048
    0x80, 0x00, 0xF8, 0x00, 0x00,    // long: -2048, 0
};

class Block : public TileSource {
   public:
    Block(int r0, int c0, int rows, int cols, int ringCol = -1)
        : r0_(r0), c0_(c0), rows_(rows), cols_(cols), ringCol_(ringCol) {}
    TileRef tile(int row, int col) const override {
        TileRef t;
        const int dc = ((col - c0_) % kCols + kCols) % kCols;
        if (row < r0_ || row >= r0_ + rows_ || dc >= cols_) return t;
        if (col == ringCol_) t.kind = TileRef::Rings, t.data = kWholeTileRing, t.size = sizeof kWholeTileRing;
        else t.kind = TileRef::Land;
        return t;
    }

   private:
    int r0_, c0_, rows_, cols_, ringCol_;
};

// Every mask pixel centre more than a pixel inside the block is land, and more than a pixel
// outside it is water: no hairline along a seam between tiles.
static int sweepBlock(bool withRing) {
    int bad = 0;
    for (int i = 0; i < 600; ++i) {
        const double lat = -55 + std::fmod(i * 7.31, 128.0), lon = -170 + std::fmod(i * 13.7, 340.0);
        const float radius = 5 + (i % 50) * 5;
        // The block's corner is the tile corner nearest home, so its seams cross the view.
        const int r = int(std::lround((lat + 90) / kTileDeg)), c = int(std::lround((lon + 180) / kTileDeg));
        const Block src(r - 1, c - 1, 2, 2, withRing ? c - 1 : -1);
        LandMask m(bits);
        const MapView v = viewFor(lat, lon, radius);
        m.build(v, src);
        const double k = 60.0 * v.pxPerNm, kx = k * v.cosLat;
        const double latLo = -90.0 + (r - 1) * kTileDeg, latHi = latLo + 2 * kTileDeg;
        const double lonLo = -180.0 + (c - 1) * kTileDeg, lonHi = lonLo + 2 * kTileDeg;
        const double eLat = 2 / k, eLon = 2 / kx;  // a mask pixel
        for (int my = 0; my < LandMask::kH; ++my)
            for (int mx = 0; mx < LandMask::kW; ++mx) {
                const double plat = v.lat - (2 * my + 1 - v.cy) / k, plon = v.lon + (2 * mx + 1 - v.cx) / kx;
                const bool in = plat > latLo + eLat && plat < latHi - eLat && plon > lonLo + eLon && plon < lonHi - eLon;
                const bool out = plat < latLo - eLat || plat > latHi + eLat || plon < lonLo - eLon || plon > lonHi + eLon;
                if ((in && !m.land(mx, my)) || (out && m.land(mx, my))) ++bad;
            }
    }
    return bad;
}

static void test_no_hairlines_between_full_tiles() { TEST_ASSERT_EQUAL(0, sweepBlock(false)); }

static void test_no_hairlines_between_full_and_ring_tiles() { TEST_ASSERT_EQUAL(0, sweepBlock(true)); }

// Across 180°: a tile in column 0 lies just east of home at 179.9°, and column 179 just west of
// home at -179.9°.
static void test_dateline() {
    LandMask m(bits);
    m.build(viewFor(1.0, 179.9, 120), Block(45, 0, 1, 1));
    TEST_ASSERT_TRUE(m.landAtScreen(140, 135));
    TEST_ASSERT_FALSE(m.landAtScreen(110, 135));
    m.build(viewFor(1.0, -179.9, 120), Block(45, 179, 1, 1));
    TEST_ASSERT_TRUE(m.landAtScreen(100, 135));
    TEST_ASSERT_FALSE(m.landAtScreen(130, 135));
}

// A ring spanning the tile's width (lon 0..2, lat 0.5..1.5), seen past the mask's left edge (its
// west side clamps to column 0) and past the right edge (its east side toggles nothing).
static const uint8_t kWideRing[] = {
    1, 0,
    4, 0, 0, 0, 0x00, 0x02,          // 4 points from (0, 512)
    0x80, 0x00, 0x08, 0x00, 0x00,    // long: +2048, 0
    0x82, 0x00, 0x00, 0x40,          // medium: 0, +1024
    0x80, 0x00, 0xF8, 0x00, 0x00,    // long: -2048, 0
};

static void test_rings_past_the_mask_edges() {
    LandMask m(bits);
    const OneTile src(kWideRing, sizeof kWideRing);
    m.build(viewFor(1.0, 2.5, 120), src);  // the tile spans x = -30..90
    TEST_ASSERT_TRUE(m.landAtScreen(0, 135));
    TEST_ASSERT_TRUE(m.landAtScreen(60, 135));
    TEST_ASSERT_FALSE(m.landAtScreen(100, 135));
    TEST_ASSERT_FALSE(m.landAtScreen(0, 60));  // north of the ring
    m.build(viewFor(1.0, -0.5, 120), src);  // the tile spans x = 150..270
    TEST_ASSERT_TRUE(m.landAtScreen(200, 135));
    TEST_ASSERT_TRUE(m.landAtScreen(239, 135));
    TEST_ASSERT_FALSE(m.landAtScreen(140, 135));
    TEST_ASSERT_FALSE(m.landAtScreen(239, 60));
}

// At 65.5° N a degree of longitude is 0.41 of a degree of latitude, so the view spans more columns
// and a full tile is a tall rectangle. Tiles in columns 93, 95 and 97 of row 77 (64..66° N).
class HighTiles : public TileSource {
   public:
    TileRef tile(int row, int col) const override {
        TileRef t;
        if (row == 77 && (col == 93 || col == 95 || col == 97)) t.kind = TileRef::Land;
        return t;
    }
};

static void test_high_latitude() {
    LandMask m(bits);
    m.build(viewFor(65.5, 11.0, 120), HighTiles());  // col 93 spans x -4..45, 95 95..145, 97 195..244
    TEST_ASSERT_TRUE(m.landAtScreen(2, 135));
    TEST_ASSERT_TRUE(m.landAtScreen(238, 135));
    TEST_ASSERT_FALSE(m.landAtScreen(60, 135));
    TEST_ASSERT_FALSE(m.landAtScreen(170, 135));
    int w = 0, h = 0;
    for (int x = 50; x < 190; ++x) w += m.landAtScreen(x, 135);
    for (int y = 0; y < 270; ++y) h += m.landAtScreen(120, y);
    TEST_ASSERT_INT_WITHIN(2, 50, w);   // 2° × 60 px × cos 65.5° = 49.8 px, in 2-px mask pixels
    TEST_ASSERT_INT_WITHIN(2, 120, h);  // 2° × 60 px
}

static void test_projection() {
    MapView v = viewFor(42.36, -71.06, 10);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.f, v.pxPerNm);
    gfx::Vec2 home = v.project(42.36, -71.06);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.f, home.x);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 135.f, home.y);
    gfx::Vec2 north = v.project(42.36 + 1 / 60.0, -71.06);  // one nautical mile north
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 123.f, north.y);
}

static constexpr double kHarborLon = -70.955;

// The real tables: Boston Harbor is water, Cambridge across the river is land.
static void test_boston_harbor_water_and_cambridge_land() {
    LandMask m(bits);
    MapView v = viewFor(42.36, -71.06, 10);
    TEST_ASSERT_GREATER_THAN(0, m.build(v));
    gfx::Vec2 harbor = v.project(42.335, kHarborLon), cambridge = v.project(42.373, -71.119);
    TEST_ASSERT_FALSE(m.landAtScreen(int(harbor.x), int(harbor.y)));
    TEST_ASSERT_TRUE(m.landAtScreen(int(cambridge.x), int(cambridge.y)));
}

static void test_open_ocean_has_no_land() {
    LandMask m(bits);
    TEST_ASSERT_EQUAL(0, m.build(viewFor(30.0, -140.0, 25)));
    for (int i = 0; i < LandMask::kBytes; ++i) TEST_ASSERT_EQUAL_HEX8(0, bits[i]);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_island_lake_and_full_tile);
    RUN_TEST(test_coverage_is_smoothed_at_the_coast);
    RUN_TEST(test_empty_ring_does_not_stop_the_tile);
    RUN_TEST(test_truncated_ring_is_left_out);
    RUN_TEST(test_no_hairlines_between_full_tiles);
    RUN_TEST(test_no_hairlines_between_full_and_ring_tiles);
    RUN_TEST(test_dateline);
    RUN_TEST(test_rings_past_the_mask_edges);
    RUN_TEST(test_high_latitude);
    RUN_TEST(test_projection);
    RUN_TEST(test_boston_harbor_water_and_cambridge_land);
    RUN_TEST(test_open_ocean_has_no_land);
    return UNITY_END();
}
