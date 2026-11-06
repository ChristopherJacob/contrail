#include <unity.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "screens/SkyProjection.h"
#include "screens/SkyViewScreen.h"
#include "screens/TreeProfile.h"
#include "data/Stars.h"
#include "services/Astro.h"

void setUp() {}
void tearDown() {}

static void test_zenith_is_the_centre() {
    gfx::Vec2 p = scr::domeProject(123, 90);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 120, p.x);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 140, p.y);
}

static void test_horizon_points() {
    scr::DomeGeom g;
    gfx::Vec2 n = scr::domeProject(0, 0), e = scr::domeProject(90, 0), w = scr::domeProject(270, 0);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, g.cy - g.ry, n.y);  // north at the top
    TEST_ASSERT_FLOAT_WITHIN(0.01f, g.cx - g.rx, e.x);  // east on the LEFT when looking up
    TEST_ASSERT_FLOAT_WITHIN(0.01f, g.cx + g.rx, w.x);
}

static void test_below_horizon_clamps_to_the_edge() {
    gfx::Vec2 a = scr::domeProject(180, -10), b = scr::domeProject(180, 0);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, b.y, a.y);
}

// Halfway up the sky is halfway out from the centre (azimuthal equidistant).
static void test_elevation_is_linear_in_radius() {
    scr::DomeGeom g;
    gfx::Vec2 s = scr::domeProject(180, 45);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, g.cx, s.x);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, g.cy + g.ry / 2, s.y);
}

// The unclamped projection keeps going past the horizon, for directions toward the set sun.
static void test_raw_projection_extends_below_the_horizon() {
    scr::DomeGeom g;
    gfx::Vec2 a = scr::domeProjectRaw(0, -9);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, g.cy - g.ry * 1.1f, a.y);
}

// The moon's bright limb points along the great circle toward the sun, as seen on the dome.
static void test_limb_points_toward_the_sun() {
    // Sun due west on the horizon, moon overhead-ish to the south: the limb faces west, which is
    // the RIGHT on the looking-up dome.
    gfx::Vec2 d = scr::domeDirectionToward(180, 60, 270, 0);
    TEST_ASSERT_TRUE(d.x > 0.5f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.f, std::sqrt(d.x * d.x + d.y * d.y));
    // Sun below the northern horizon, moon low in the south: the limb faces north (up), across the zenith.
    d = scr::domeDirectionToward(180, 20, 0, -20);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.f, d.x);
    TEST_ASSERT_TRUE(d.y < -0.9f);
}

// The treeline frames the dome: the zenith and the high sky are clear, the corners solid, and the
// rim mostly hidden behind trees (with a few gaps); the silhouette is smooth round the rim, not
// row-by-row noise.
static void test_treeline_frames_the_dome() {
    scr::SkyDome dome(scr::cinematicSky());
    scr::SkyTrees trees(dome);
    TEST_ASSERT_EQUAL(0, trees.coverage(120.5f, 140.5f));
    TEST_ASSERT_EQUAL(255, trees.coverage(1.5f, 1.5f));
    TEST_ASSERT_EQUAL(255, trees.coverage(238.5f, 318.5f));
    int horizonCovered = 0, jumps = 0, prev = -1;
    for (int az = 0; az < 360; ++az) {
        gfx::Vec2 hi = scr::domeProject(float(az), 25), lo = scr::domeProject(float(az), 0);
        TEST_ASSERT_EQUAL(0, trees.coverage(hi.x, hi.y));
        horizonCovered += trees.coverage(lo.x, lo.y) > 128;
        // Coverage a few degrees up, sampled every 0.25 degree of azimuth: neighbours differ only
        // where a tree's edge crosses, so large jumps are rare.
        for (int q = 0; q < 4; ++q) {
            gfx::Vec2 p = scr::domeProject(az + q * 0.25f, 6);
            int c = trees.coverage(p.x, p.y);
            if (prev >= 0 && std::abs(c - prev) > 200) ++jumps;
            prev = c;
        }
    }
    TEST_ASSERT_GREATER_THAN(300, horizonCovered);
    TEST_ASSERT_LESS_THAN(40, jumps);
}

// The dome keeps at most 180 stars (SkyDome::kMaxStars): more than are ever above the trees at
// once, anywhere.
static void test_star_capacity_is_enough() {
    int most = 0;
    for (double lat = -80; lat <= 80; lat += 20)
        for (int64_t t = 1793471700; t < 1793471700 + 86400; t += 3600) {
            int n = 0;
            for (int i = 0; i < data::kStarCount; ++i) {
                const data::Star& st = data::kStars[i];
                n += svc::equatorialToHorizontal(st.raDeg, st.decDeg, t, lat, -122).elevationDeg >= 2.5;
            }
            most = n > most ? n : most;
        }
    char msg[48];
    std::snprintf(msg, sizeof msg, "at most %d stars up at once", most);
    TEST_MESSAGE(msg);
    TEST_ASSERT_LESS_OR_EQUAL(180, most);
}

// The flash table is generated from buildTreeProfile(); rerun tools/gen_trees.py if this fails.
static void test_tree_table_matches_its_builder() {
    auto p = std::make_unique<scr::TreeProfile>();
    scr::buildTreeProfile(*p);
    TEST_ASSERT_EQUAL_MEMORY(scr::kTreeProfile.rho, p->rho, sizeof p->rho);
    TEST_ASSERT_EQUAL_MEMORY(scr::kTreeProfile.scale, p->scale, sizeof p->scale);
    TEST_ASSERT_EQUAL_FLOAT(scr::kTreeProfile.rhoMin, p->rhoMin);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_zenith_is_the_centre);
    RUN_TEST(test_horizon_points);
    RUN_TEST(test_below_horizon_clamps_to_the_edge);
    RUN_TEST(test_elevation_is_linear_in_radius);
    RUN_TEST(test_raw_projection_extends_below_the_horizon);
    RUN_TEST(test_limb_points_toward_the_sun);
    RUN_TEST(test_treeline_frames_the_dome);
    RUN_TEST(test_star_capacity_is_enough);
    RUN_TEST(test_tree_table_matches_its_builder);
    return UNITY_END();
}
