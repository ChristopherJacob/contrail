#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <unity.h>

#include "services/Astro.h"
#include "services/Sun.h"

void setUp() {}
void tearDown() {}

constexpr double LAT = 37.6189, LON = -122.375;  // SFO
constexpr int64_t kNew = 1704974220, kFirstQuarter = 1705549920, kFull = 1706205240;

static double angleDiff(double a, double b) {
    double d = std::fmod(a - b + 540.0, 360.0) - 180.0;
    return std::fabs(d);
}

static void test_polaris_altitude_is_latitude() {
    for (int64_t t : {int64_t(1793471700), int64_t(1793471700 + 6 * 3600), int64_t(1793471700 + 13 * 3600)}) {
        svc::HorizPos p = svc::equatorialToHorizontal(37.95, 89.264, t, LAT, LON);
        TEST_ASSERT_DOUBLE_WITHIN(1.2, LAT, p.elevationDeg);
        TEST_ASSERT_DOUBLE_WITHIN(1.5, 0, angleDiff(p.azimuthDeg, 0));
    }
}

static void test_moon_phases() {
    TEST_ASSERT_TRUE(svc::moonState(kNew, LAT, LON).illumination < 0.03);
    svc::MoonState q = svc::moonState(kFirstQuarter, LAT, LON);
    TEST_ASSERT_DOUBLE_WITHIN(0.06, 0.5, q.illumination);
    TEST_ASSERT_TRUE(q.waxing);
    TEST_ASSERT_TRUE(svc::moonState(kFull, LAT, LON).illumination > 0.97);
}

static void test_full_moon_opposite_sun() {
    svc::MoonState m = svc::moonState(kFull, LAT, LON);
    svc::SunPos s = svc::sunPosition(kFull, LAT, LON);
    TEST_ASSERT_DOUBLE_WITHIN(15, 0, angleDiff(m.azimuthDeg, s.azimuthDeg + 180));
    TEST_ASSERT_DOUBLE_WITHIN(15, -s.elevationDeg, m.elevationDeg);
}

// Reference positions from JPL Horizons (DE441), fetched 2026-10-31:
//   https://ssd.jpl.nasa.gov/api/horizons.api?format=text&COMMAND='301'&MAKE_EPHEM='YES'
//   &EPHEM_TYPE='OBSERVER'&CENTER='coord@399'&COORD_TYPE='GEODETIC'&SITE_COORD='-122.375,37.6189,0'
//   &TLIST_TYPE='JD'&TLIST='2460000.5','2460000.6','2460600.0','2461000.25'&QUANTITIES='4'
//   &APPARENT='AIRLESS'&ANG_FORMAT='DEG'&TIME_TYPE='UT'
// Quantity 4 is topocentric apparent azimuth (from north, through east) and elevation without
// refraction. The spec asks for the moon within 1 degree.
static void test_moon_matches_horizons() {
    struct Ref {
        int64_t epoch;
        double az, el;
    };
    const Ref refs[] = {
        {1677283200, 170.259532, 66.353001},  // 2023-02-25 00:00 UT
        {1677291840, 238.924499, 54.098214},  // 2023-02-25 02:24 UT
        {1729080000, 263.589414, 13.944666},  // 2024-10-16 12:00 UT
        {1763661600, 148.559072, 18.612484},  // 2025-11-20 18:00 UT
    };
    for (const Ref& r : refs) {
        svc::MoonState m = svc::moonState(r.epoch, LAT, LON);
        char msg[96];
        std::snprintf(msg, sizeof msg, "t=%lld az %.3f (ref %.3f) el %.3f (ref %.3f)", (long long)r.epoch,
                      m.azimuthDeg, r.az, m.elevationDeg, r.el);
        TEST_ASSERT_DOUBLE_WITHIN_MESSAGE(1.0, r.el, m.elevationDeg, msg);
        TEST_ASSERT_DOUBLE_WITHIN_MESSAGE(1.0, 0, angleDiff(m.azimuthDeg, r.az), msg);
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_polaris_altitude_is_latitude);
    RUN_TEST(test_moon_phases);
    RUN_TEST(test_full_moon_opposite_sun);
    RUN_TEST(test_moon_matches_horizons);
    return UNITY_END();
}
