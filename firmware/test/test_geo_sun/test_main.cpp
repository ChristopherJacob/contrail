#include <cmath>
#include <initializer_list>
#include <unity.h>

#include "model/Geo.h"
#include "services/Sun.h"

void setUp() {}
void tearDown() {}

// Airport coordinates.
constexpr double SFO_LAT = 37.6189, SFO_LON = -122.3750;
constexpr double ORD_LAT = 41.9786, ORD_LON = -87.9048;
constexpr double JFK_LAT = 40.6398, JFK_LON = -73.7789;

static void test_distance_sfo_ord() {
    TEST_ASSERT_DOUBLE_WITHIN(5, 1600, geo::distanceNm(SFO_LAT, SFO_LON, ORD_LAT, ORD_LON));
}

static void test_bearing_sfo_jfk() {
    TEST_ASSERT_DOUBLE_WITHIN(1.5, 69.9, geo::bearingDeg(SFO_LAT, SFO_LON, JFK_LAT, JFK_LON));
}

static void test_project_round_trip() {
    double lat, lon;
    geo::project(SFO_LAT, SFO_LON, 12.5, 233, lat, lon);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 12.5, geo::distanceNm(SFO_LAT, SFO_LON, lat, lon));
    TEST_ASSERT_DOUBLE_WITHIN(0.1, 233, geo::bearingDeg(SFO_LAT, SFO_LON, lat, lon));
}

static void test_elevation() {
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 90, geo::elevationDeg(0, 35000));
    // 35,000 ft is 5.76 nm up; at 10 nm that is atan(5.76/10) = 29.9° minus a little curvature.
    TEST_ASSERT_DOUBLE_WITHIN(1, 29.7, geo::elevationDeg(10, 35000));
    TEST_ASSERT_TRUE(geo::elevationDeg(200, 1000) < 0);  // below the horizon far away
}

static void test_bounding_box() {
    double a, b, c, d;
    geo::boundingBox(37.62, -122.38, 25, a, b, c, d);
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 0.8333, c - a);  // 25 nm = 0.4167° each side
    TEST_ASSERT_TRUE(d - b > c - a);  // longitude span wider than latitude away from equator
}

static void test_sun_summer_midday_sfo() {
    // 2024-06-20 20:00 UTC = 13:00 PDT. Solar elevation ~75.6°, sun to the south.
    svc::SunPos s = svc::sunPosition(1718913600, 37.62, -122.38);
    TEST_ASSERT_DOUBLE_WITHIN(0.6, 75.6, s.elevationDeg);
    TEST_ASSERT_TRUE(s.azimuthDeg > 150 && s.azimuthDeg < 230);
}

static void test_sun_winter_night_sfo() {
    // 2024-12-21 08:00 UTC = 00:00 PST.
    svc::SunPos s = svc::sunPosition(1734768000, 37.62, -122.38);
    TEST_ASSERT_TRUE(s.elevationDeg < -50);
}

static void test_sun_equinox_sunrise_equator() {
    // 2024-03-20 06:00 UTC at 0°,0°: the sun is near the horizon in the east.
    svc::SunPos s = svc::sunPosition(1710914400, 0, 0);
    TEST_ASSERT_DOUBLE_WITHIN(2.5, 0, s.elevationDeg);
    TEST_ASSERT_DOUBLE_WITHIN(3, 90, s.azimuthDeg);
}

// The fast local maths used per frame must agree with the exact great-circle versions
// across the tracking radius.
static void test_local_frame_matches_exact() {
    const double lat0 = 39.42, lon0 = -77.37;
    geo::LocalFrame f;
    f.set(lat0, lon0);
    for (double brg = 0; brg < 360; brg += 30) {
        for (double d : {0.5, 5.0, 25.0, 60.0}) {
            double lat, lon;
            geo::project(lat0, lon0, d, brg, lat, lon);
            float fd, fb;
            f.distBearing(float(lat), float(lon), fd, fb);
            TEST_ASSERT_FLOAT_WITHIN(float(d * 0.003 + 0.01), float(d), fd);
            float db = std::fabs(fb - float(brg));
            TEST_ASSERT_TRUE(std::fmin(db, 360.f - db) < 0.5f);
            if (d > 5) continue;  // dead reckoning projects at most ~5 nm
            float plat, plon;
            geo::projectFast(float(lat0), float(lon0), float(d), float(brg), plat, plon);
            TEST_ASSERT_TRUE(geo::distanceNm(lat, lon, plat, plon) < 0.01);
        }
    }
    TEST_ASSERT_FLOAT_WITHIN(0.2f, float(geo::elevationDeg(10, 35000)), geo::elevationDegFast(10, 35000));
    TEST_ASSERT_FLOAT_WITHIN(0.2f, float(geo::elevationDeg(40, 2000)), geo::elevationDegFast(40, 2000));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_local_frame_matches_exact);
    RUN_TEST(test_distance_sfo_ord);
    RUN_TEST(test_bearing_sfo_jfk);
    RUN_TEST(test_project_round_trip);
    RUN_TEST(test_elevation);
    RUN_TEST(test_bounding_box);
    RUN_TEST(test_sun_summer_midday_sfo);
    RUN_TEST(test_sun_winter_night_sfo);
    RUN_TEST(test_sun_equinox_sunrise_equator);
    return UNITY_END();
}
