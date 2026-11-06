#include <cmath>
#include <cstdio>
#include <cstring>
#include <unity.h>

#include "app/RouteShelf.h"
#include "app/SceneBuilder.h"
#include "data/Shapes.h"
#include "model/Geo.h"
#include "screens/Scene.h"

void setUp() {}
void tearDown() {}

static app::RouteInfo zrhIad() {
    app::RouteInfo r;
    std::strcpy(r.callsign, "UAL53");
    std::strcpy(r.orig, "ZRH"), std::strcpy(r.dest, "IAD");
    std::strcpy(r.origCity, "Zurich"), std::strcpy(r.destCity, "Washington Dulles");
    r.origLat = 47.4647f, r.origLon = 8.5492f, r.destLat = 38.9445f, r.destLon = -77.4558f;
    return r;
}

static void test_progress_halfway() {
    app::RouteInfo r = zrhIad();
    double total = geo::distanceNm(r.origLat, r.origLon, r.destLat, r.destLon);
    double brg = geo::bearingDeg(r.origLat, r.origLon, r.destLat, r.destLon);
    double lat, lon;
    geo::project(r.origLat, r.origLon, total / 2, brg, lat, lon);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.5f, app::routeProgress(r, lat, lon));
}

static void test_progress_unknown_without_coordinates() {
    app::RouteInfo r = zrhIad();
    r.destLat = NAN;
    TEST_ASSERT_TRUE(app::routeProgress(r, 45, -30) < 0);
}

static void test_arrival_estimate() {
    app::RouteInfo r = zrhIad();
    // 487 nm short of Dulles along the inbound bearing, at 487 kt: about an hour out.
    double brg = geo::bearingDeg(r.destLat, r.destLon, r.origLat, r.origLon);
    double lat, lon;
    geo::project(r.destLat, r.destLon, 487, brg, lat, lon);
    int64_t eta = app::estimateArrival(r, lat, lon, 487, 34000, 1000000);
    TEST_ASSERT_INT_WITHIN(90, 1000000 + 3600, eta);
    TEST_ASSERT_EQUAL(0, app::estimateArrival(r, lat, lon, 487, 800, 1000000));  // on approach: no estimate
    TEST_ASSERT_EQUAL(0, app::estimateArrival(r, lat, lon, 40, 34000, 1000000));  // too slow to trust
    TEST_ASSERT_EQUAL(0, app::estimateArrival(r, lat, lon, NAN, 34000, 1000000));  // no speed reported
    TEST_ASSERT_TRUE(app::routeProgress(r, NAN, NAN) < 0);
}

static void test_fill_flight_without_route() {
    model::Aircraft a;
    a.hex = 0xA1B2C3;
    std::strcpy(a.callsign, "UAL53");
    std::strcpy(a.type, "B789");
    a.altFt = 34000, a.gsKt = 487, a.trackDeg = 52, a.distNm = 2.4f, a.bearingDeg = 40, a.elevDeg = 31;
    scr::SceneFlight f;
    app::fillFlight(f, a, nullptr, 0);
    TEST_ASSERT_EQUAL_STRING("UAL53", f.callsign);
    TEST_ASSERT_EQUAL_STRING("UAL", f.airline);
    TEST_ASSERT_FALSE(f.hasRoute);
    TEST_ASSERT_TRUE(f.progress < 0);
    TEST_ASSERT_EQUAL(0, f.etaEpoch);
}

// Callsigns are put in capitals when the scene is built, so every screen shows the same thing.
static void test_fill_flight_upper_cases_the_callsign() {
    model::Aircraft a;
    a.hex = 0xA1B2C3;
    std::strcpy(a.callsign, "ual53");
    scr::SceneFlight f;
    app::fillFlight(f, a, nullptr, 0);
    TEST_ASSERT_EQUAL_STRING("UAL53", f.callsign);
    TEST_ASSERT_EQUAL_STRING("UAL", f.airline);
    std::strcpy(a.callsign, ""), std::strcpy(a.reg, "n512ka");  // no callsign: the registration
    app::fillFlight(f, a, nullptr, 0);
    TEST_ASSERT_EQUAL_STRING("N512KA", f.callsign);
    TEST_ASSERT_EQUAL_STRING("n512ka", f.reg);
    TEST_ASSERT_EQUAL_STRING("", f.airline);
}

static void test_fill_flight_copies_squawk_and_emergency_status() {
    model::Aircraft a;
    a.hex = 0xA1B2C3;
    std::strcpy(a.squawk, "7700");
    a.flags = model::AC_EMERGENCY | model::AC_EMERG_STATUS;
    scr::SceneFlight f;
    app::fillFlight(f, a, nullptr, 0);
    TEST_ASSERT_EQUAL_STRING("7700", f.squawk);
    TEST_ASSERT_TRUE(f.emergency);
    TEST_ASSERT_TRUE(f.emergencyStatus);
    a.flags = model::AC_EMERGENCY;  // the squawk alone
    app::fillFlight(f, a, nullptr, 0);
    TEST_ASSERT_FALSE(f.emergencyStatus);
}

static void test_fill_flight_sets_the_shape_family() {
    model::Aircraft a;
    a.hex = 0xABCDEF;
    std::snprintf(a.callsign, sizeof a.callsign, "UAL53");
    std::snprintf(a.type, sizeof a.type, "B789");
    scr::SceneFlight f;
    app::fillFlight(f, a, nullptr, 0);
    TEST_ASSERT_EQUAL(int(data::Family::Widebody), int(f.shape));
    a.type[0] = 0;
    a.category = 0xA7;
    app::fillFlight(f, a, nullptr, 0);
    TEST_ASSERT_EQUAL(int(data::Family::Helicopter), int(f.shape));
}

static void test_shelf_round_trip() {
    app::RouteShelf shelf;
    shelf.put(zrhIad());
    app::RouteInfo out;
    TEST_ASSERT_TRUE(shelf.get("UAL53", out));
    TEST_ASSERT_EQUAL_STRING("Washington Dulles", out.destCity);
    TEST_ASSERT_FALSE(shelf.get("DAL22", out));
}

static void test_scene_lookup() {
    scr::Scene s;
    s.count = 1;
    s.flights[0].hex = 7;
    std::strcpy(s.routes[0].callsign, "UAL53");
    s.routeCount = 1;
    TEST_ASSERT_NOT_NULL(s.find(7));
    TEST_ASSERT_NULL(s.find(8));
    TEST_ASSERT_NOT_NULL(s.route("UAL53"));
    TEST_ASSERT_NULL(s.route(""));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_fill_flight_upper_cases_the_callsign);
    RUN_TEST(test_fill_flight_copies_squawk_and_emergency_status);
    RUN_TEST(test_progress_halfway);
    RUN_TEST(test_progress_unknown_without_coordinates);
    RUN_TEST(test_arrival_estimate);
    RUN_TEST(test_fill_flight_without_route);
    RUN_TEST(test_fill_flight_sets_the_shape_family);
    RUN_TEST(test_shelf_round_trip);
    RUN_TEST(test_scene_lookup);
    return UNITY_END();
}
