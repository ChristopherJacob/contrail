#include <unity.h>

#include <cstring>

#include "map/MapData.h"
#include "map/Tiles.h"

using namespace basemap;

void setUp() {}
void tearDown() {}

static int popcount(const uint8_t* bits) {
    int n = 0;
    for (int i = 0; i < gen::kBitBytes; ++i) n += __builtin_popcount(bits[i]);
    return n;
}

static void test_index_is_consistent() {
    const int ringTiles = popcount(gen::kHasRings);
    TEST_ASSERT_GREATER_THAN(500, ringTiles);
    TEST_ASSERT_EQUAL(ringTiles, gen::kRowRank[kRows]);
    for (int r = 0; r < kRows; ++r) TEST_ASSERT_TRUE(gen::kRowRank[r] <= gen::kRowRank[r + 1]);
    for (int k = 0; k < ringTiles; ++k) TEST_ASSERT_TRUE(gen::kTileOffset[k] < gen::kTileOffset[k + 1]);
    TEST_ASSERT_EQUAL_UINT32(gen::kRingDataSize, gen::kTileOffset[ringTiles]);
    TEST_ASSERT_GREATER_THAN(1000, popcount(gen::kFullLand));
    for (int i = 0; i < gen::kBitBytes; ++i) TEST_ASSERT_EQUAL_HEX8(0, gen::kFullLand[i] & gen::kHasRings[i]);
}

// Every ring tile decodes to its exact end, with every point inside the tile.
static void test_every_tile_decodes() {
    int rings = 0;
    for (int r = 0; r < kRows; ++r)
        for (int c = 0; c < kCols; ++c) {
            TileRef t = builtinTiles().tile(r, c);
            if (t.kind != TileRef::Rings) continue;
            const uint8_t* p = t.data + 2;
            const uint8_t* end = t.data + t.size;
            const int n = t.data[0] | t.data[1] << 8;
            TEST_ASSERT_GREATER_THAN(0, n);
            for (int i = 0; i < n; ++i) {
                RingReader rr(p, end);
                RingPoint pt;
                int pts = 0;
                while (rr.next(pt)) {
                    TEST_ASSERT_TRUE(pt.x >= 0 && pt.x <= kUnits && pt.y >= 0 && pt.y <= kUnits);
                    ++pts;
                }
                TEST_ASSERT_TRUE(rr.ok());
                TEST_ASSERT_EQUAL(rr.count() + 1, pts);
                p = rr.end();
                ++rings;
            }
            TEST_ASSERT_EQUAL_PTR(end, p);
        }
    TEST_ASSERT_GREATER_THAN(1000, rings);
}

static void test_known_tiles() {
    TEST_ASSERT_EQUAL(int(TileRef::Land), int(builtinTiles().tile(tileRow(24.5), tileCol(45.0)).kind));    // Najd
    TEST_ASSERT_EQUAL(int(TileRef::Water), int(builtinTiles().tile(tileRow(30.5), tileCol(-140.5)).kind)); // Pacific
    TEST_ASSERT_EQUAL(int(TileRef::Rings), int(builtinTiles().tile(tileRow(37.6), tileCol(-122.4)).kind)); // SF Bay
    // Antarctica is left out: no tile south of 60° S has data.
    for (int r = 0; r < kFirstDataRow; ++r)
        for (int c = 0; c < kCols; ++c) TEST_ASSERT_EQUAL(int(TileRef::Water), int(builtinTiles().tile(r, c).kind));
    TEST_ASSERT_TRUE(gen::kRowRank[kFirstDataRow] == 0);
}

static void test_airports_sorted_with_runways() {
    TEST_ASSERT_EQUAL(10, int(sizeof(gen::Airport)));
    TEST_ASSERT_GREATER_THAN(2000, gen::kAirportCount);
    int runways = 0;
    for (int i = 0; i < gen::kAirportCount; ++i) {
        const gen::Airport& a = gen::kAirports[i];
        if (i) TEST_ASSERT_TRUE(gen::kAirports[i - 1].latE5() <= a.latE5());
        TEST_ASSERT_TRUE(a.latE5() >= -9000000 && a.latE5() <= 9000000);
        TEST_ASSERT_TRUE(a.lonE5() >= -18000000 && a.lonE5() <= 18000000);
        char id[5];
        a.ident(id);
        TEST_ASSERT_TRUE(std::strlen(id) >= 3);
        if (!a.large()) TEST_ASSERT_EQUAL(0, a.runways());
        TEST_ASSERT_EQUAL(runways, a.firstRunway());
        runways += a.runways();
    }
    TEST_ASSERT_EQUAL(gen::kRunwayCount, runways);
}

static const gen::Airport* find(const char* code) {
    char id[5];
    for (int i = 0; i < gen::kAirportCount; ++i)
        if (gen::kAirports[i].ident(id), !std::strcmp(id, code)) return &gen::kAirports[i];
    return nullptr;
}

// The packed records decode to the real positions; runway ends land near the airport.
static void test_known_airports() {
    const gen::Airport* sfo = find("KSFO");
    TEST_ASSERT_NOT_NULL(sfo);
    TEST_ASSERT_TRUE(sfo->large());
    TEST_ASSERT_INT32_WITHIN(3, 3761981, sfo->latE5());  // 37.619806, -122.374821 in airports.csv
    TEST_ASSERT_INT32_WITHIN(3, -12237482, sfo->lonE5());
    TEST_ASSERT_GREATER_OR_EQUAL(4, sfo->runways());
    for (int k = 0; k < sfo->runways(); ++k) {
        const gen::Runway& r = gen::kRunways[sfo->firstRunway() + k];
        TEST_ASSERT_INT32_WITHIN(5000, 0, r.dLat1);  // within ~5 km of the reference point
        TEST_ASSERT_INT32_WITHIN(5000, 0, r.dLon2);
    }
    const gen::Airport* bos = find("KBOS");
    TEST_ASSERT_NOT_NULL(bos);
    TEST_ASSERT_INT32_WITHIN(3, 4236197, bos->latE5());  // 42.36197, -71.0079 in airports.csv
    TEST_ASSERT_INT32_WITHIN(3, -7100790, bos->lonE5());
    TEST_ASSERT_NOT_NULL(find("EGLL"));
}

static void test_flash_budget() {
    const int ringTiles = gen::kRowRank[kRows];
    const int runwayStarts = (gen::kAirportCount + gen::kRunwayBlock - 1) / gen::kRunwayBlock;
    size_t bytes = gen::kRingDataSize + (ringTiles + 1) * sizeof(uint32_t) + 2 * gen::kBitBytes + sizeof gen::kRowRank +
                   gen::kAirportCount * sizeof(gen::Airport) + runwayStarts * sizeof(uint16_t) +
                   gen::kRunwayCount * sizeof(gen::Runway);
    TEST_ASSERT_LESS_OR_EQUAL(360u * 1024u, bytes);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_index_is_consistent);
    RUN_TEST(test_every_tile_decodes);
    RUN_TEST(test_known_tiles);
    RUN_TEST(test_airports_sorted_with_runways);
    RUN_TEST(test_known_airports);
    RUN_TEST(test_flash_budget);
    return UNITY_END();
}
