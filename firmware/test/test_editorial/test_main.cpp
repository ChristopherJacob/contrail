#include <unity.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <utility>

#include "GoldenRender.h"
#include "app/SceneBuilder.h"
#include "model/Aircraft.h"
#include "screens/DemoScenes.h"

using scr::ScreenId;

void setUp() {
    setenv("TZ", "UTC0", 1);
    tzset();
}
void tearDown() {}

static const scr::Theme& ed() { return scr::editorial(); }

static void test_closest() {
    golden::render(ScreenId::Closest, scr::sceneBusy, "ed_closest_busy", ed());
    golden::render(ScreenId::Closest, scr::sceneEmpty, "ed_closest_empty", ed());
    // Southwest's yellow is too light for white (under 3:1): the plane, contrail and compass go dark.
    golden::render(ScreenId::Closest, scr::sceneBusy, "ed_closest_light", ed(), 0xA3D4E5);
    // JetBlue's navy is too dark to read on the card: the bar and percentage are lifted toward white.
    golden::render(ScreenId::Closest, scr::sceneBusy, "ed_closest_dark", ed(), 0xA60718);
    // With the fixture card: UAL's 30 px logo on its white tile at the band's top-left, "UNITED" after it.
    golden::render(ScreenId::Closest, scr::sceneBusy, "ed_closest_logo", ed(), 0, 75, true);
    // Delta's red logo on Delta's red band: the white tile under it keeps it visible.
    golden::render(ScreenId::Closest, scr::sceneBusy, "ed_closest_logo_dal", ed(), 0xA2C3D4, 75, true);
}

// The nearest flight rebuilt the way the app builds it, from an aircraft reporting `callsign`.
static void rebuildNearest(scr::Scene& s, const char* callsign) {
    const scr::SceneFlight f = s.flights[0];
    model::Aircraft a;
    a.hex = f.hex;
    std::snprintf(a.callsign, sizeof a.callsign, "%s", callsign);
    std::snprintf(a.reg, sizeof a.reg, "%s", f.reg);
    std::snprintf(a.type, sizeof a.type, "%s", f.type);
    a.altFt = f.altFt, a.vrateFpm = f.vrateFpm, a.gsKt = f.gsKt, a.trackDeg = f.trackDeg;
    a.distNm = f.distNm, a.bearingDeg = f.bearingDeg, a.elevDeg = f.elevDeg;
    app::fillFlight(s.flights[0], a, nullptr, s.epoch);
}

// The condensed hero face has capitals only: an aircraft reporting a lower-case callsign gets it in
// capitals when the scene is built, so Closest Flight looks the same as for the upper-case one.
static void test_closest_lower_case_callsign() {
    golden::Rig up(ed()), low(ed());
    scr::sceneBusy(up.app->scene()), rebuildNearest(up.app->scene(), "UAL53");
    scr::sceneBusy(low.app->scene()), rebuildNearest(low.app->scene(), "ual53");
    for (golden::Rig* g : {&up, &low}) {
        g->app->sceneUpdated();
        g->show(ScreenId::Closest);
        g->frames(75);
    }
    int diff = 0;
    for (size_t i = 0; i < up.d.px.size(); ++i) diff += up.d.px[i] != low.d.px[i];
    TEST_ASSERT_EQUAL(0, diff);
}

// Board rows filled with airline colours glide and fade over the flat list: a reorder, a reorder
// mid-glide, a row leaving and a new one arriving leave nothing stale, mid-animation or settled.
static void test_board_reorder_leaves_no_stale_pixels() {
    golden::Rig g(ed());
    scr::Scene& s = g.app->scene();
    scr::sceneBusy(s);
    g.app->sceneUpdated();
    g.show(ScreenId::Board);
    g.frames(20);
    std::swap(s.flights[0], s.flights[3]);
    g.app->sceneUpdated();
    g.frames(4);
    g.assertNoStale();
    std::swap(s.flights[0], s.flights[1]);  // mid-glide
    g.app->sceneUpdated();
    g.frames(3);
    g.assertNoStale();
    for (int i = 2; i < s.count - 1; ++i) s.flights[i] = s.flights[i + 1];  // one leaves, one arrives
    s.flights[4].hex = 0xC0FFEE;
    std::snprintf(s.flights[4].callsign, sizeof s.flights[4].callsign, "ASA1234");
    g.app->sceneUpdated();
    g.frames(5);
    g.assertNoStale();
    g.frames(10);
    g.assertNoStale();
}

// Night to dusk at once: the flat bands, the dusk discs and the plain moon redraw with nothing stale.
static void test_sky_sun_jump_leaves_no_stale_pixels() {
    golden::Rig g(ed());
    scr::Scene& s = g.app->scene();
    scr::sceneNight(s);
    g.app->sceneUpdated();
    g.show(ScreenId::Sky);
    g.frames(10);
    s.epoch += 1, s.sunElev = 3, s.sunAz = 250;
    g.app->sceneUpdated();
    g.frames(10);
    g.assertNoStale();
    s.epoch += 60, s.sunElev = -4;  // the glow cools a minute later
    g.app->sceneUpdated();
    g.frames(10);
    g.assertNoStale();
}

static void test_board() {
    golden::render(ScreenId::Board, scr::sceneBusy, "ed_board_busy", ed());
    // With a card holding UAL's and DAL's logos: those two logos, the other airlines' fins and N512KA's shape on
    // white tiles, in livery-coloured rows.
    golden::render(ScreenId::Board, scr::sceneBusy, "ed_board_logos", ed(), 0, 75, true);
    golden::render(ScreenId::Board, scr::sceneEmpty, "ed_board_empty", ed());
}

static void test_radar() {
    golden::render(ScreenId::Radar, scr::sceneBusy, "ed_radar_busy", ed());
    golden::render(ScreenId::Radar, scr::sceneEmpty, "ed_radar_empty", ed());
}

static void test_sky() {
    golden::render(ScreenId::Sky, scr::sceneBusy, "ed_sky_busy", ed());
    golden::render(ScreenId::Sky, scr::sceneEmpty, "ed_sky_empty", ed());
    golden::render(ScreenId::Sky, scr::sceneQuiet, "ed_sky_day", ed());
    golden::render(ScreenId::Sky, scr::sceneNight, "ed_sky_night", ed(), 0, 100);
}

// The busy Map with 70 s of trails behind it, recorded the way the app records them (as
// test_map_screen's map_busy), so the golden shows the trails in airline colours.
static void mapWithTrails() {
    golden::Rig g(ed());
    for (int k = 7; k >= 0; --k) {
        scr::Scene& s = g.app->scene();
        scr::sceneBusy(s);
        for (int i = 0; i < s.count; ++i) {  // every flight stepped 10 * k seconds back along its track
            scr::SceneFlight& f = s.flights[i];
            float b = f.bearingDeg * 0.0174533f, t = f.trackDeg * 0.0174533f, d = f.gsKt * 10.f * k / 3600.f;
            float e = f.distNm * std::sin(b) - d * std::sin(t), n = f.distNm * std::cos(b) - d * std::cos(t);
            f.distNm = std::sqrt(e * e + n * n);
            f.bearingDeg = std::fmod(std::atan2(e, n) / 0.0174533f + 360.f, 360.f);
        }
        s.epoch -= int64_t(10 * k);
        g.app->sceneUpdated();
    }
    g.show(ScreenId::Map);
    g.frames(75);
    assertGolden("ed_map_busy", g.d.px, 240, 320);
    g.assertNoStale();
}

static void test_map() {
    mapWithTrails();
    golden::render(ScreenId::Map, scr::sceneEmpty, "ed_map_empty", ed());
}

static void test_stats() {
    golden::render(ScreenId::Stats, scr::sceneStats, "ed_stats_busy", ed());
    golden::render(ScreenId::Stats, scr::sceneStatsEmpty, "ed_stats_empty", ed());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_closest);
    RUN_TEST(test_closest_lower_case_callsign);
    RUN_TEST(test_board);
    RUN_TEST(test_board_reorder_leaves_no_stale_pixels);
    RUN_TEST(test_radar);
    RUN_TEST(test_sky);
    RUN_TEST(test_sky_sun_jump_leaves_no_stale_pixels);
    RUN_TEST(test_map);
    RUN_TEST(test_stats);
    return UNITY_END();
}
