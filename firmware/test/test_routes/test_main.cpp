#include <unity.h>

#include <cstdio>
#include <cstring>

#include "FakeHttp.h"
#include "Fixture.h"
#include "providers/Routes.h"

using namespace prov;

void setUp() {}
void tearDown() {}

static Route parseFixture(const char* name, bool (*fn)(hal::IByteSource&, Route&), bool& ok) {
    std::string json = loadFixture(name);
    hal::StringSource src(json.data(), json.size());
    Route r;
    ok = fn(src, r);
    return r;
}

static void test_adsbdb_known_route() {
    bool ok;
    Route r = parseFixture("adsbdb_route.json", parseAdsbdb, ok);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(r.known);
    TEST_ASSERT_EQUAL_STRING("YVR", r.origIata);
    TEST_ASSERT_EQUAL_STRING("CYVR", r.origIcao);
    TEST_ASSERT_EQUAL_STRING("Vancouver", r.origCity);
    TEST_ASSERT_EQUAL_STRING("United Airlines", r.airline);
    TEST_ASSERT_EQUAL_STRING("UAL", r.airlineIcao);
    TEST_ASSERT_EQUAL(3, std::strlen(r.destIata));
}

static void test_route_plausibility() {
    bool ok;
    Route r = parseFixture("adsbdb_route.json", parseAdsbdb, ok);  // YVR -> SFO
    TEST_ASSERT_TRUE(routePlausible(r, 45.6, -122.6));    // over Portland, on the way
    TEST_ASSERT_TRUE(routePlausible(r, 37.70, -122.30));  // arriving at SFO
    TEST_ASSERT_FALSE(routePlausible(r, 39.87, -75.24));  // over Philadelphia: stale route
    Route unknownCoords;
    TEST_ASSERT_TRUE(routePlausible(unknownCoords, 0, 0));
}

static void test_adsbdb_unknown_callsign() {
    bool ok;
    Route r = parseFixture("adsbdb_unknown.json", parseAdsbdb, ok);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FALSE(r.known);
}

static void test_aeroapi_picks_airborne_leg() {
    bool ok;
    Route r = parseFixture("aeroapi_flights.json", parseAeroApiFlights, ok);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(r.known);
    TEST_ASSERT_EQUAL_STRING("SFO", r.origIata);
    TEST_ASSERT_EQUAL_STRING("ORD", r.destIata);
    TEST_ASSERT_EQUAL_STRING("Chicago", r.destCity);
    TEST_ASSERT_EQUAL(62, r.progressPct);
}

static Route named(const char* cs, uint32_t t) {
    Route r;
    std::strcpy(r.callsign, cs);
    r.known = true;
    r.fetchedMs = t;
    return r;
}

static void test_cache_lru_eviction() {
    RouteCache c;
    char cs[9];
    for (int i = 0; i < RouteCache::kCapacity; ++i) {
        std::snprintf(cs, sizeof cs, "CS%d", i);
        c.put(named(cs, 0));
    }
    TEST_ASSERT_NOT_NULL(c.find("CS0", 10));  // touch CS0 so CS1 becomes least recent
    c.put(named("NEW", 0));
    TEST_ASSERT_EQUAL(RouteCache::kCapacity, c.size());
    TEST_ASSERT_NOT_NULL(c.find("CS0", 10));
    TEST_ASSERT_NULL(c.find("CS1", 10));
    TEST_ASSERT_NOT_NULL(c.find("NEW", 10));
}

static void test_cache_expires() {
    RouteCache c;
    c.put(named("UAL1", 1000));
    TEST_ASSERT_NOT_NULL(c.find("UAL1", 1000 + RouteCache::kKnownTtlMs));
    TEST_ASSERT_NULL(c.find("UAL1", 1001 + RouteCache::kKnownTtlMs));
}

static void test_adsbdb_404_is_negative_answer() {
    FakeHttp http;
    http.responses = {{404, R"({"response":"unknown callsign"})"}};
    AdsbdbClient c;
    Route r;
    TEST_ASSERT_TRUE(c.lookup(http, "ZZZ999", 5, r));
    TEST_ASSERT_FALSE(r.known);
    TEST_ASSERT_EQUAL_STRING("ZZZ999", r.callsign);
    TEST_ASSERT_EQUAL_STRING("https://api.adsbdb.com/v0/callsign/ZZZ999", http.requests[0].url.c_str());
}

static void test_aeroapi_budget() {
    FakeHttp http;
    http.responses = {{200, R"({"flights":[]})"}};
    AeroApiClient c;
    Route r;
    TEST_ASSERT_FALSE(c.lookup(http, "UAL1", 0, 86400 * 100, r));  // no key
    c.setKey("k");
    c.setDailyCap(20);
    for (int i = 0; i < 20; ++i) TEST_ASSERT_TRUE(c.lookup(http, "UAL1", 0, 86400 * 100 + i, r));
    TEST_ASSERT_FALSE(c.lookup(http, "UAL1", 0, 86400 * 100 + 50, r));  // 21st today
    TEST_ASSERT_TRUE(c.lookup(http, "UAL1", 0, 86400 * 101 + 1, r));     // next UTC day
    TEST_ASSERT_EQUAL_STRING("x-apikey", http.requests[0].headers[0].first.c_str());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_adsbdb_known_route);
    RUN_TEST(test_route_plausibility);
    RUN_TEST(test_adsbdb_unknown_callsign);
    RUN_TEST(test_aeroapi_picks_airborne_leg);
    RUN_TEST(test_cache_lru_eviction);
    RUN_TEST(test_cache_expires);
    RUN_TEST(test_adsbdb_404_is_negative_answer);
    RUN_TEST(test_aeroapi_budget);
    return UNITY_END();
}
