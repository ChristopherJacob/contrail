#include <unity.h>

#include <cstring>

#include "model/AircraftStore.h"
#include "model/Geo.h"

using namespace model;

void setUp() {}
void tearDown() {}

constexpr double HOME_LAT = 37.62, HOME_LON = -122.38;

static Aircraft at(uint32_t hex, double distNm, double brg, uint32_t seenMs, const char* cs = "TEST1") {
    Aircraft a;
    a.hex = hex;
    std::strncpy(a.callsign, cs, 8);
    double lat, lon;
    geo::project(HOME_LAT, HOME_LON, distNm, brg, lat, lon);
    a.lat = float(lat), a.lon = float(lon);
    a.altFt = 30000;
    a.flags = AC_HAS_POS;
    a.lastSeenMs = seenMs;
    return a;
}

static AircraftStore store;

static void reset() { store.configure(HOME_LAT, HOME_LON, 25); }

static void test_upsert_same_hex_updates() {
    reset();
    store.beginUpdate(1000);
    store.upsert(at(0xABC, 5, 90, 1000, "OLD"));
    store.upsert(at(0xABC, 6, 90, 1000, "NEW"));
    store.endUpdate();
    Aircraft out[4];
    TEST_ASSERT_EQUAL(1, store.snapshot(out, 4, 1000));
    TEST_ASSERT_EQUAL_STRING("NEW", out[0].callsign);
    TEST_ASSERT_FLOAT_WITHIN(0.05, 6, out[0].distNm);
}

static void test_beyond_radius_and_no_position_dropped() {
    reset();
    store.beginUpdate(1000);
    TEST_ASSERT_FALSE(store.upsert(at(1, 30, 0, 1000)));
    Aircraft nopos = at(2, 3, 0, 1000);
    nopos.flags = 0;
    TEST_ASSERT_FALSE(store.upsert(nopos));
    store.endUpdate();
    TEST_ASSERT_EQUAL(0, store.size());
}

static void test_ground_dropped_unless_kept() {
    reset();
    Aircraft g = at(7, 1, 0, 1000);
    g.flags |= AC_GROUND;
    store.beginUpdate(1000);
    TEST_ASSERT_FALSE(store.upsert(g));
    store.setKeepGround(true);
    TEST_ASSERT_TRUE(store.upsert(g));
    store.setKeepGround(false);
    store.endUpdate();
}

static void test_keeps_nearest_when_full() {
    reset();
    store.beginUpdate(1000);
    for (int i = 0; i < 70; ++i) store.upsert(at(100 + i, 24.0 - i * 0.3, i * 5, 1000));
    store.endUpdate();
    TEST_ASSERT_EQUAL(AircraftStore::kCapacity, store.size());
    Aircraft out[AircraftStore::kCapacity];
    int n = store.snapshot(out, AircraftStore::kCapacity, 1000);
    // The 6 farthest (i = 0..5) must have been displaced.
    for (int i = 0; i < n; ++i) TEST_ASSERT_TRUE(out[i].hex >= 106);
}

static void test_stale_evicted_after_60s() {
    reset();
    store.beginUpdate(1000);
    store.upsert(at(1, 5, 0, 1000));
    store.endUpdate();
    store.beginUpdate(1000 + 61000);
    store.upsert(at(2, 5, 0, 1000 + 61000));
    store.endUpdate();
    Aircraft out[4];
    TEST_ASSERT_EQUAL(1, store.snapshot(out, 4, 62000));
    TEST_ASSERT_EQUAL_HEX32(2, out[0].hex);
}

static void test_snapshot_sorted_and_limited() {
    reset();
    store.beginUpdate(1000);
    store.upsert(at(1, 20, 0, 1000));
    store.upsert(at(2, 2, 90, 1000));
    store.upsert(at(3, 10, 180, 1000));
    store.endUpdate();
    Aircraft out[2];
    TEST_ASSERT_EQUAL(2, store.snapshot(out, 2, 1000));
    TEST_ASSERT_EQUAL_HEX32(2, out[0].hex);
    TEST_ASSERT_EQUAL_HEX32(3, out[1].hex);
}

static void test_dead_reckoning() {
    Aircraft a = at(1, 0.001, 0, 0);
    a.gsKt = 480, a.trackDeg = 90;
    double lat0 = a.lat, lon0 = a.lon;
    deadReckon(a, 15);  // 480 kt for 15 s = 2 nm
    TEST_ASSERT_DOUBLE_WITHIN(0.02, 2.0, geo::distanceNm(lat0, lon0, a.lat, a.lon));
    TEST_ASSERT_DOUBLE_WITHIN(1, 90, geo::bearingDeg(lat0, lon0, a.lat, a.lon));
}

static void test_snapshot_reckoning_capped_at_30s() {
    reset();
    Aircraft a = at(1, 0.001, 0, 0);
    a.gsKt = 360, a.trackDeg = 0;  // 0.1 nm/s
    store.beginUpdate(0);
    store.upsert(a);
    store.endUpdate();
    Aircraft out[1];
    store.snapshot(out, 1, 50000);  // 50 s later, capped to 30 s -> 3 nm
    TEST_ASSERT_FLOAT_WITHIN(0.05, 3.0, out[0].distNm);
}

static void test_generation_bumps_once_per_update() {
    reset();
    Aircraft out[2];
    uint32_t gen = 99;
    store.snapshot(out, 2, 0, &gen);
    const uint32_t g0 = gen;
    store.beginUpdate(1000);
    store.upsert(at(1, 5, 0, 1000));
    store.upsert(at(2, 6, 0, 1000));
    store.endUpdate();
    TEST_ASSERT_EQUAL_UINT32(g0 + 1, store.generation());
    TEST_ASSERT_EQUAL(2, store.snapshot(out, 2, 1000, &gen));
    TEST_ASSERT_EQUAL_UINT32(g0 + 1, gen);
    store.beginUpdate(2000);
    store.endUpdate();  // an update with nothing in it is still a poll's data
    store.snapshot(out, 2, 2000, &gen);
    TEST_ASSERT_EQUAL_UINT32(g0 + 2, gen);
    store.snapshot(out, 2, 2100, &gen);
    TEST_ASSERT_EQUAL_UINT32(g0 + 2, gen);  // reading does not bump it
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_upsert_same_hex_updates);
    RUN_TEST(test_beyond_radius_and_no_position_dropped);
    RUN_TEST(test_ground_dropped_unless_kept);
    RUN_TEST(test_keeps_nearest_when_full);
    RUN_TEST(test_stale_evicted_after_60s);
    RUN_TEST(test_snapshot_sorted_and_limited);
    RUN_TEST(test_dead_reckoning);
    RUN_TEST(test_snapshot_reckoning_capped_at_30s);
    RUN_TEST(test_generation_bumps_once_per_update);
    return UNITY_END();
}
