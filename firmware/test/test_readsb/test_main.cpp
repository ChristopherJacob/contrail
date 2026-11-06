#include <unity.h>

#include <ArduinoJson.h>

#include "Fixture.h"
#include "model/Geo.h"
#include "providers/ReadsbParser.h"

using namespace model;

void setUp() {}
void tearDown() {}

constexpr double HOME_LAT = 37.62, HOME_LON = -122.38;
static AircraftStore store;

static prov::ParseResult parse(const std::string& json, uint32_t now = 100000) {
    store.configure(HOME_LAT, HOME_LON, 25);
    store.setKeepGround(true);  // these tests check ground flags and raw counts
    hal::StringSource src(json.data(), json.size());
    return prov::parseReadsb(src, store, now, Source::Community);
}

// Independent count of positioned aircraft within 25 nm, using a full parse.
static int expectedWithin(const std::string& json) {
    JsonDocument d;
    deserializeJson(d, json);
    int n = 0;
    for (JsonVariant e : d["ac"].as<JsonArray>()) {
        if (e["lat"].isNull() && e["lastPosition"].isNull()) continue;
        double lat = e["lat"].isNull() ? e["lastPosition"]["lat"].as<double>() : e["lat"].as<double>();
        double lon = e["lon"].isNull() ? e["lastPosition"]["lon"].as<double>() : e["lon"].as<double>();
        if (geo::distanceNm(HOME_LAT, HOME_LON, lat, lon) <= 25) ++n;
    }
    return n > AircraftStore::kCapacity ? AircraftStore::kCapacity : n;
}

static void test_adsblol_fixture() {
    std::string json = loadFixture("adsblol_point.json");
    auto r = parse(json);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_TRUE(r.seen > 0);
    TEST_ASSERT_EQUAL(expectedWithin(json), store.size());
}

static void test_adsbfi_fixture() {
    std::string json = loadFixture("adsbfi_v3.json");
    auto r = parse(json);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL(expectedWithin(json), store.size());
}

static void test_military_flag_from_dbflags() {
    parse(loadFixture("adsblol_point.json"));
    Aircraft out[AircraftStore::kCapacity];
    int n = store.snapshot(out, AircraftStore::kCapacity, 100000);
    bool found = false;
    for (int i = 0; i < n; ++i)
        if (out[i].hex == 0xae6827) {
            found = true;
            TEST_ASSERT_TRUE(out[i].has(AC_MILITARY));
        }
    TEST_ASSERT_TRUE_MESSAGE(found, "fixture aircraft ae6827 not stored");
}

static void test_local_receiver_format() {
    const char* json = R"({"now":1793373942.1,"messages":12345,"aircraft":[
      {"hex":"a1b2c3","flight":"SWA2210 ","alt_baro":"ground","gs":12.0,"track":90,"lat":37.62,"lon":-122.37,"seen":0.5},
      {"hex":"~0c0ffe","flight":"N512FX","alt_baro":4500,"gs":120,"track":270,"lat":37.70,"lon":-122.30,"squawk":"7700","category":"A1","seen":1.0},
      {"hex":"abcdef","alt_baro":10000,"seen":2.0}
    ]})";
    auto r = parse(json, 50000);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL(3, r.seen);
    TEST_ASSERT_EQUAL(2, r.accepted);  // the third has no position
    Aircraft out[4];
    int n = store.snapshot(out, 4, 50000);
    TEST_ASSERT_EQUAL(2, n);
    const Aircraft& g = out[0].hex == 0xa1b2c3 ? out[0] : out[1];
    const Aircraft& e = out[0].hex == 0xa1b2c3 ? out[1] : out[0];
    TEST_ASSERT_TRUE(g.has(AC_GROUND));
    TEST_ASSERT_EQUAL_STRING("SWA2210", g.callsign);
    TEST_ASSERT_EQUAL_UINT32(49500, g.lastSeenMs);
    TEST_ASSERT_TRUE(e.has(AC_EMERGENCY));
    // A squawk alone is not a declared emergency: the alert engine debounces squawks (alerts spec §4).
    TEST_ASSERT_FALSE(e.has(AC_EMERG_STATUS));
    TEST_ASSERT_EQUAL_STRING("7700", e.squawk);
    TEST_ASSERT_EQUAL_HEX32((1u << 24) | 0x0c0ffe, e.hex);
    TEST_ASSERT_EQUAL_HEX8(0xA1, e.category);
    TEST_ASSERT_EQUAL(4500, e.altFt);
}

static void test_emergency_status_sets_its_own_flag() {
    parse(R"({"ac":[{"hex":"a1b2c3","lat":37.62,"lon":-122.37,"alt_baro":9000,"squawk":"2000","emergency":"general"}]})");
    Aircraft out[1];
    TEST_ASSERT_EQUAL(1, store.snapshot(out, 1, 100000));
    TEST_ASSERT_TRUE(out[0].has(AC_EMERG_STATUS));
    TEST_ASSERT_TRUE(out[0].has(AC_EMERGENCY));
}

static void test_garbage_leaves_store_unchanged() {
    parse(R"({"ac":[{"hex":"a1b2c3","lat":37.62,"lon":-122.37,"alt_baro":1000}]})");
    TEST_ASSERT_EQUAL(1, store.size());
    hal::StringSource src("<html>502 Bad Gateway</html>", 28);
    auto r = prov::parseReadsb(src, store, 100000, Source::Community);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL(1, store.size());
}

static void test_empty_array_ok() {
    auto r = parse(R"({"ac":[],"msg":"No error"})");
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL(0, r.seen);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_adsblol_fixture);
    RUN_TEST(test_adsbfi_fixture);
    RUN_TEST(test_military_flag_from_dbflags);
    RUN_TEST(test_local_receiver_format);
    RUN_TEST(test_emergency_status_sets_its_own_flag);
    RUN_TEST(test_garbage_leaves_store_unchanged);
    RUN_TEST(test_empty_array_ok);
    return UNITY_END();
}
