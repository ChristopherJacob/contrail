// The browser simulator's recorded data (hal_wasm/Replay): replies in order then the end, any community host,
// recorded routes and 404s, the clock from the recording's start, settings in memory. The env ignores hal_wasm (its
// glue needs emscripten), so this suite compiles the source itself.
#include <unity.h>

#include <map>
#include <string>

#include "hal.h"  // names the hal lib for the dependency finder, which does not follow the include below
#include "../../lib/hal_wasm/Replay.cpp"

void setUp() {}
void tearDown() {}

static std::map<std::string, hal::Fetched> files;
static int fetches = 0;
static hal::Fetched fakeFetch(const std::string& path) {
    ++fetches;
    auto it = files.find(path);
    return it == files.end() ? hal::Fetched{404, ""} : it->second;
}

static std::string body;
static hal::HttpStatus get(hal::IHttp& http, const char* url) {
    body.clear();
    return http.get(url, nullptr, 0, [](hal::IByteSource& src) {
        for (int c; (c = src.read()) >= 0;) body += char(c);
    });
}

static void reset() {
    files = {{"sim/kfdk/000.json", {200, "r0"}},
             {"sim/kfdk/001.json", {200, "r1"}},
             {"sim/kfdk/routes.json", {200, R"({"UAL53":{"response":{"flightroute":{"callsign":"UAL53"}}},"N1":null})"}}};
    fetches = 0;
}

static void test_replies_in_order_then_the_end() {
    reset();
    hal::ReplayHttp http(fakeFetch, "sim/kfdk", 2);
    TEST_ASSERT_EQUAL(200, get(http, "https://api.adsb.lol/v2/point/39.4176/-77.3743/25").code);
    TEST_ASSERT_EQUAL_STRING("r0", body.c_str());
    TEST_ASSERT_FALSE(http.ended());
    TEST_ASSERT_EQUAL(200, get(http, "https://api.adsb.lol/v2/point/39.4176/-77.3743/25").code);
    TEST_ASSERT_EQUAL_STRING("r1", body.c_str());
    TEST_ASSERT_FALSE(http.ended());
    TEST_ASSERT_TRUE(get(http, "https://api.adsb.lol/v2/point/39.4176/-77.3743/25").code < 0);
    TEST_ASSERT_TRUE(http.ended());
    TEST_ASSERT_EQUAL(2, http.served());
}

static void test_every_community_host_gets_the_recording() {
    reset();
    hal::ReplayHttp http(fakeFetch, "sim/kfdk", 2);
    TEST_ASSERT_EQUAL(200, get(http, "https://opendata.adsb.fi/api/v3/lat/39.4/lon/-77.4/dist/25").code);
    TEST_ASSERT_EQUAL_STRING("r0", body.c_str());
    TEST_ASSERT_EQUAL(200, get(http, "https://api.airplanes.live/v2/point/39.4/-77.4/25").code);
    TEST_ASSERT_EQUAL_STRING("r1", body.c_str());
}

static void test_a_missing_reply_fails_the_poll() {
    reset();
    files.erase("sim/kfdk/001.json");
    hal::ReplayHttp http(fakeFetch, "sim/kfdk", 2);
    get(http, "https://api.adsb.lol/v2/point/1/2/25");
    TEST_ASSERT_EQUAL(404, get(http, "https://api.adsb.lol/v2/point/1/2/25").code);
    TEST_ASSERT_EQUAL_STRING("", body.c_str());  // no body handler for a non-2xx reply
}

static void test_routes_recorded_unknown_and_missing() {
    reset();
    hal::ReplayHttp http(fakeFetch, "sim/kfdk", 2);
    TEST_ASSERT_EQUAL(200, get(http, "https://api.adsbdb.com/v0/callsign/UAL53").code);
    TEST_ASSERT_EQUAL_STRING(R"({"response":{"flightroute":{"callsign":"UAL53"}}})", body.c_str());
    TEST_ASSERT_EQUAL(404, get(http, "https://api.adsbdb.com/v0/callsign/N1").code);
    TEST_ASSERT_EQUAL(404, get(http, "https://api.adsbdb.com/v0/callsign/DAL1").code);
    TEST_ASSERT_EQUAL(1, fetches);  // routes.json once
    TEST_ASSERT_EQUAL(0, http.served());  // routes are not polls
}

static void test_no_routes_file_means_every_route_is_unknown() {
    reset();
    files.erase("sim/kfdk/routes.json");
    hal::ReplayHttp http(fakeFetch, "sim/kfdk", 2);
    TEST_ASSERT_EQUAL(404, get(http, "https://api.adsbdb.com/v0/callsign/UAL53").code);
}

static void test_other_urls_fail_without_a_request() {
    reset();
    hal::ReplayHttp http(fakeFetch, "sim/kfdk", 2);
    TEST_ASSERT_TRUE(get(http, "https://opensky-network.org/api/states/all").code < 0);
    TEST_ASSERT_TRUE(http.post("https://x/", nullptr, 0, "text/plain", "", [](hal::IByteSource&) {}).code < 0);
    TEST_ASSERT_EQUAL(0, fetches);
}

static uint32_t fakeMs = 0;
static void test_clock_runs_from_the_recording_start() {
    fakeMs = 5000;
    hal::ReplayClock clock([] { return fakeMs; }, 1762500000);
    TEST_ASSERT_EQUAL_UINT32(0, clock.millis());
    TEST_ASSERT_EQUAL_INT64(1762500000, clock.epochSeconds());
    fakeMs = 5000 + 61500;
    TEST_ASSERT_EQUAL_UINT32(61500, clock.millis());
    TEST_ASSERT_EQUAL_INT64(1762500061, clock.epochSeconds());
    TEST_ASSERT_TRUE(clock.timeValid());
}

static void test_memstore() {
    hal::MemStore kv;
    char out[8];
    TEST_ASSERT_FALSE(kv.getString("theme", out, sizeof out));
    TEST_ASSERT_TRUE(kv.putString("theme", "editorial"));
    TEST_ASSERT_TRUE(kv.getString("theme", out, sizeof out));  // truncates, as hal::FileStore does
    TEST_ASSERT_EQUAL_STRING("editori", out);
    TEST_ASSERT_FALSE(kv.getString("theme", out, 0));
    char big[16];
    TEST_ASSERT_TRUE(kv.getString("theme", big, sizeof big));
    TEST_ASSERT_EQUAL_STRING("editorial", big);
    hal::MemStore copy;
    copy.load(kv.all());
    TEST_ASSERT_TRUE(copy.getString("theme", big, sizeof big));
    TEST_ASSERT_TRUE(kv.erase("theme"));
    TEST_ASSERT_FALSE(kv.getString("theme", big, sizeof big));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_replies_in_order_then_the_end);
    RUN_TEST(test_every_community_host_gets_the_recording);
    RUN_TEST(test_a_missing_reply_fails_the_poll);
    RUN_TEST(test_routes_recorded_unknown_and_missing);
    RUN_TEST(test_no_routes_file_means_every_route_is_unknown);
    RUN_TEST(test_other_urls_fail_without_a_request);
    RUN_TEST(test_clock_runs_from_the_recording_start);
    RUN_TEST(test_memstore);
    return UNITY_END();
}
