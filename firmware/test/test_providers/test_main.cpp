#include <unity.h>

#include <cstring>

#include <ArduinoJson.h>

#include "FakeHttp.h"
#include "Fixture.h"
#include "providers/Providers.h"

using namespace model;
using namespace prov;

void setUp() {}
void tearDown() {}

static AircraftStore store;
static PollContext ctx() { return {100000, 1793373950, 37.62, -122.38, 25}; }

static void test_opensky_fixture() {
    std::string json = loadFixture("opensky_states.json");
    store.configure(37.62, -122.38, 60);  // the recorded box is ~25 nm half-width; allow its corners
    store.setKeepGround(true);
    hal::StringSource src(json.data(), json.size());
    ParseResult r = parseOpenSkyStates(src, store, 100000, 1793373950);
    TEST_ASSERT_TRUE(r.ok);
    JsonDocument d;
    deserializeJson(d, json);
    TEST_ASSERT_EQUAL(int(d["states"].size()), r.seen);
    TEST_ASSERT_TRUE(r.accepted > 0);
    // First record: a9c6ff "N729WR  " on the ground.
    Aircraft out[AircraftStore::kCapacity];
    int n = store.snapshot(out, AircraftStore::kCapacity, 100000);
    bool found = false;
    for (int i = 0; i < n; ++i)
        if (out[i].hex == 0xa9c6ff) {
            found = true;
            TEST_ASSERT_EQUAL_STRING("N729WR", out[i].callsign);
            TEST_ASSERT_TRUE(out[i].has(AC_GROUND));
        }
    TEST_ASSERT_TRUE(found);
}

static void test_opensky_unit_conversion() {
    const char* json = R"({"time":1000,"states":[["abc123","DAL884  ","United States",995,999,-122.30,37.70,10000.0,false,250.0,135.5,-5.08,null,10100.0,"7600",false,0]]})";
    store.configure(37.62, -122.38, 25);
    hal::StringSource src(json, std::strlen(json));
    ParseResult r = parseOpenSkyStates(src, store, 100000, 1000);
    TEST_ASSERT_EQUAL(1, r.accepted);
    Aircraft a;
    store.snapshot(&a, 1, 100000 - 5000);  // the position is 5 s old; snapshot at the fix time
    TEST_ASSERT_INT_WITHIN(2, 32808, a.altFt);
    TEST_ASSERT_FLOAT_WITHIN(0.5, 486, a.gsKt);
    TEST_ASSERT_INT_WITHIN(2, -1000, a.vrateFpm);
    TEST_ASSERT_TRUE(a.has(AC_EMERGENCY));
    TEST_ASSERT_EQUAL_UINT32(95000, a.lastSeenMs);
}

static void test_opensky_null_states_ok() {
    const char* json = R"({"time":1000,"states":null})";
    store.configure(37.62, -122.38, 25);
    hal::StringSource src(json, std::strlen(json));
    ParseResult r = parseOpenSkyStates(src, store, 1000, 1000);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL(0, r.seen);
}

static void test_opensky_token_reused_then_refreshed() {
    store.configure(37.62, -122.38, 25);
    OpenSkyProvider p;
    p.setCredentials("me-api-client", "s3cret");
    FakeHttp http;
    http.responses = {{200, R"({"access_token":"tok1","expires_in":1800,"token_type":"Bearer"})"},
                      {200, R"({"time":1,"states":[]})"},
                      {200, R"({"time":1,"states":[]})"},
                      {200, R"({"access_token":"tok2","expires_in":1800})"},
                      {200, R"({"time":1,"states":[]})"}};
    PollContext c = ctx();
    c.nowMs = 1000;
    TEST_ASSERT_TRUE(p.poll(http, store, c));
    c.nowMs = 600000;  // 10 min later: token still valid
    TEST_ASSERT_TRUE(p.poll(http, store, c));
    c.nowMs = 1000 + 1750000;  // inside the 60 s refresh margin
    TEST_ASSERT_TRUE(p.poll(http, store, c));
    TEST_ASSERT_EQUAL(5, http.requests.size());
    TEST_ASSERT_EQUAL_STRING("POST", http.requests[0].method.c_str());
    TEST_ASSERT_TRUE(http.requests[0].payload.find("client_id=me-api-client") != std::string::npos);
    TEST_ASSERT_EQUAL_STRING("Bearer tok1", http.requests[1].headers[0].second.c_str());
    TEST_ASSERT_EQUAL_STRING("POST", http.requests[3].method.c_str());
    TEST_ASSERT_EQUAL_STRING("Bearer tok2", http.requests[4].headers[0].second.c_str());
}

static void test_opensky_anonymous_no_auth_header() {
    OpenSkyProvider p;
    FakeHttp http;
    http.responses = {{200, R"({"time":1,"states":[]})"}};
    TEST_ASSERT_TRUE(p.poll(http, store, ctx()));
    TEST_ASSERT_EQUAL(1, http.requests.size());
    TEST_ASSERT_TRUE(http.requests[0].headers.empty());
    TEST_ASSERT_TRUE(http.requests[0].url.find("lamin=37.2") != std::string::npos);
}

static void test_community_rotates_hosts_on_failure() {
    CommunityProvider p;
    FakeHttp http;
    http.responses = {{503, ""}, {200, R"({"ac":[]})"}};
    TEST_ASSERT_EQUAL_STRING("adsb.lol", p.name());
    TEST_ASSERT_FALSE(p.poll(http, store, ctx()));
    TEST_ASSERT_EQUAL_STRING("adsb.fi", p.name());
    TEST_ASSERT_TRUE(p.poll(http, store, ctx()));
    TEST_ASSERT_TRUE(http.requests[0].url.find("api.adsb.lol/v2/point/37.6200/-122.3800/25") != std::string::npos);
    TEST_ASSERT_TRUE(http.requests[1].url.find("opendata.adsb.fi/api/v3/lat/37.6200/lon/-122.3800/dist/25") !=
                     std::string::npos);
}

static void test_local_receiver_requires_url() {
    LocalReceiverProvider p;
    TEST_ASSERT_FALSE(p.configured());
    p.setUrl("http://192.168.1.20/tar1090/data/aircraft.json");
    TEST_ASSERT_TRUE(p.configured());
    FakeHttp http;
    http.responses = {{200, R"({"now":1,"aircraft":[]})"}};
    TEST_ASSERT_TRUE(p.poll(http, store, ctx()));
    TEST_ASSERT_EQUAL_STRING("http://192.168.1.20/tar1090/data/aircraft.json", http.requests[0].url.c_str());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_opensky_fixture);
    RUN_TEST(test_opensky_unit_conversion);
    RUN_TEST(test_opensky_null_states_ok);
    RUN_TEST(test_opensky_token_reused_then_refreshed);
    RUN_TEST(test_opensky_anonymous_no_auth_header);
    RUN_TEST(test_community_rotates_hosts_on_failure);
    RUN_TEST(test_local_receiver_requires_url);
    return UNITY_END();
}
