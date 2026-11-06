#include <unity.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "GoldenRender.h"
#include "data/Shapes.h"
#include "map/Airports.h"
#include "screens/DemoScenes.h"

using golden::Rig;
using scr::ScreenId;

void setUp() {
    setenv("TZ", "UTC0", 1);
    tzset();
}
void tearDown() {}

static void test_map_empty() { golden::render(ScreenId::Map, scr::sceneEmpty, "map_empty"); }
static void test_map_ocean() { golden::render(ScreenId::Map, scr::sceneOcean, "map_ocean"); }
static void test_map_no_location() { golden::render(ScreenId::Map, scr::sceneNoLocation, "map_no_location"); }

// The chart is static: scene updates that keep home and the radius redraw nothing of it; a new
// radius rebuilds the mask and redraws the whole screen once.
static void test_map_base_redraws_only_on_a_new_view() {
    Rig g;
    scr::sceneEmpty(g.app->scene());
    g.app->sceneUpdated();
    g.show(ScreenId::Map);
    g.frames(3);
    g.app->scene().epoch += 1;
    g.app->sceneUpdated();
    TEST_ASSERT_EQUAL(0, g.pushed());
    g.app->scene().radiusNm = 40;
    g.app->sceneUpdated();
    TEST_ASSERT_EQUAL(240 * 320, g.pushed());
    TEST_ASSERT_EQUAL(0, g.pushed());
    g.assertNoStale();
}

// Step every flight back along its track, `seconds` into the past.
static void backInTime(scr::Scene& s, float seconds) {
    for (int i = 0; i < s.count; ++i) {
        scr::SceneFlight& f = s.flights[i];
        float b = f.bearingDeg * 0.0174533f, t = f.trackDeg * 0.0174533f, d = f.gsKt * seconds / 3600.f;
        float e = f.distNm * std::sin(b) - d * std::sin(t), n = f.distNm * std::cos(b) - d * std::cos(t);
        f.distNm = std::sqrt(e * e + n * n);
        f.bearingDeg = std::fmod(std::atan2(e, n) / 0.0174533f + 360.f, 360.f);
    }
    s.epoch -= int64_t(seconds);
}

// The scene with 70 s of history behind it, recorded the way the app records it.
static void withHistory(scr::ScreenApp& app, void (*fill)(scr::Scene&)) {
    for (int k = 7; k >= 0; --k) {
        fill(app.scene());
        backInTime(app.scene(), 10.f * k);
        app.sceneUpdated();
    }
}

static void test_map_busy() {
    Rig g;
    withHistory(*g.app, scr::sceneBusy);
    g.show(ScreenId::Map);
    g.frames(75);
    assertGolden("map_busy", g.d.px, 240, 320);
    g.assertNoStale();
}

static void test_map_quiet_selected() {
    golden::render(ScreenId::Map, scr::sceneQuiet, "map_quiet", scr::cinematicSky(), 0xA1B2C3);
}

// Planes moving and turning, a selection, one leaving, a new trail point: every frame stays a
// partial redraw, the incremental result matches a full one, and a settled map draws nothing.
static void test_map_updates_leave_no_stale_pixels() {
    Rig g;
    withHistory(*g.app, scr::sceneBusy);
    g.show(ScreenId::Map);
    g.frames(10);
    scr::Scene& s = g.app->scene();
    for (int step = 0; step < 4; ++step) {
        for (int i = step; i < s.count; i += 5)
            s.flights[i].bearingDeg += 2.f, s.flights[i].distNm *= 0.97f, s.flights[i].trackDeg += 9.f;
        if (step == 1) g.app->host().setSelection(s.flights[3].hex), --s.count;
        if (step == 2) s.epoch += 10;  // every plane gets a new trail point
        g.app->sceneUpdated();
        TEST_ASSERT_LESS_THAN_MESSAGE(240 * 320, g.pushed(), "a plane update redrew the whole map");
        g.assertNoStale();
    }
    g.frames(2);
    TEST_ASSERT_EQUAL_MESSAGE(0, g.pushed(), "a settled map kept redrawing");
}

// A plane crossing an altitude band recolours its whole trail, though the trail itself is unchanged.
static void test_map_band_change_recolours_the_trail() {
    Rig g;
    withHistory(*g.app, scr::sceneBusy);
    g.show(ScreenId::Map);
    g.frames(10);
    scr::Scene& s = g.app->scene();
    for (int i = 0; i < s.count; ++i) s.flights[i].altFt = s.flights[i].altFt >= 10000 ? 5000 : 30000;
    g.app->sceneUpdated();
    g.frames(2);
    g.assertNoStale();
}

// The selected plane changing family: its icon, ring and label (pushed out by the icon's size) all move.
static void test_map_family_change_while_selected() {
    Rig g;
    scr::sceneQuiet(g.app->scene());
    g.app->sceneUpdated();
    g.show(ScreenId::Map);
    g.app->host().setSelection(g.app->scene().flights[0].hex);
    g.frames(3);
    for (data::Family fam : {data::Family::Superjumbo, data::Family::Single, data::Family::Helicopter,
                             data::Family::None, data::Family::Widebody}) {
        g.app->scene().flights[0].shape = uint8_t(fam);
        g.app->sceneUpdated();
        g.frames(2);
        g.assertNoStale();
    }
}

static void test_map_taps() {
    Rig g;
    scr::sceneQuiet(g.app->scene());
    g.app->sceneUpdated();
    g.show(ScreenId::Map);
    g.frames(3);
    scr::Screen* m = g.app->host().screen(ScreenId::Map);
    // UAL53: 2.4 nm at bearing 40, 4.8 px per nm at the 25 nm radius.
    float px = 120 + std::sin(40 * 0.0174533f) * 2.4f * 4.8f, py = 135 - std::cos(40 * 0.0174533f) * 2.4f * 4.8f;
    TEST_ASSERT_EQUAL_HEX32(0xA1B2C3, m->onTap(int(px) + 6, int(py)).selectHex);
    TEST_ASSERT_EQUAL_HEX32(0, m->onTap(20, 30).selectHex);
    scr::NavRequest r = m->onTap(120, 290);
    TEST_ASSERT_TRUE(r.open);
    TEST_ASSERT_EQUAL(int(ScreenId::Closest), int(r.to));
    TEST_ASSERT_EQUAL_HEX32(0xA1B2C3, r.selectHex);
}

// Home ~27.7 nm north of KSFO, which (25 nm radius, 4.8 px a nm) sits just above the strip at
// y ≈ 268: its dot, runways and label must stop at the chart's bottom edge.
static void sceneSfoAtBottom(scr::Scene& s) {
    scr::sceneEmpty(s);
    s.homeLat = 37.6189 + 27.7 / 60.0;
}
static void sceneOceanEmpty(scr::Scene& s) {
    scr::sceneEmpty(s);
    s.homeLat = 30.0, s.homeLon = -140.0;
}

static void test_map_nothing_below_the_chart() {
    basemap::AirportMark m[20];
    const int n = basemap::airportsInView(basemap::viewFor(37.6189 + 27.7 / 60.0, -122.375, 25), m, 20);
    bool sfo = false;
    for (int i = 0; i < n; ++i)
        if (!std::strcmp(m[i].ident, "KSFO")) sfo = m[i].y >= 262 && m[i].y < 270;
    TEST_ASSERT_TRUE_MESSAGE(sfo, "fixture: KSFO should sit just above the strip");
    Rig edge, ocean;
    for (Rig* g : {&edge, &ocean}) {
        (g == &edge ? sceneSfoAtBottom : sceneOceanEmpty)(g->app->scene());
        g->app->sceneUpdated();
        g->show(ScreenId::Map);
        g->frames(75);
    }
    int differ = 0;
    for (int i = 240 * scr::MapBase::kMapH; i < 240 * 320; ++i) differ += edge.d.px[i] != ocean.d.px[i];
    TEST_ASSERT_EQUAL_MESSAGE(0, differ, "the chart painted below y = 270");
    edge.assertNoStale();
}

// The quiet scene with UAL53 due north at 2.5 nm: on a whole pixel, (120, 123).
static void sceneNorth(scr::Scene& s) {
    scr::sceneQuiet(s);
    s.flights[0].bearingDeg = 0, s.flights[0].distNm = 2.5f;
}

static scr::Screen* mapOf(Rig& g, void (*fill)(scr::Scene&)) {
    fill(g.app->scene());
    g.app->sceneUpdated();
    g.show(ScreenId::Map);
    g.frames(3);
    return g.app->host().screen(ScreenId::Map);
}

static void test_map_tap_reach() {
    Rig g;
    scr::Screen* m = mapOf(g, sceneNorth);
    TEST_ASSERT_EQUAL_HEX32(0xA1B2C3, m->onTap(120, 123 + 20).selectHex);  // 20 px: in reach
    TEST_ASSERT_EQUAL_HEX32(0, m->onTap(120, 123 + 21).selectHex);         // 21 px: not
    // Below the chart only the strip's card opens the plane, not the margins around it.
    TEST_ASSERT_TRUE(m->onTap(120, 290).open);
    TEST_ASSERT_FALSE(m->onTap(4, 290).open);
    TEST_ASSERT_FALSE(m->onTap(120, 315).open);
}

static void sceneQuietNowhere(scr::Scene& s) {
    sceneNorth(s);
    s.locationSet = false;
}

static void test_map_taps_without_a_location() {
    Rig g;
    scr::Screen* m = mapOf(g, sceneQuietNowhere);
    scr::NavRequest plane = m->onTap(120, 123), strip = m->onTap(120, 290);
    TEST_ASSERT_EQUAL_HEX32(0, plane.selectHex);
    TEST_ASSERT_FALSE(plane.open);
    TEST_ASSERT_FALSE(strip.open);
    TEST_ASSERT_EQUAL_HEX32(0, strip.selectHex);
}

// A busy map, settled.
static void busyMap(Rig& g) {
    withHistory(*g.app, scr::sceneBusy);
    g.show(ScreenId::Map);
    g.frames(10);
}

static void test_map_new_radius_and_home_with_planes() {
    Rig g;
    busyMap(g);
    g.app->scene().radiusNm = 40;
    g.app->sceneUpdated();
    TEST_ASSERT_EQUAL(240 * 320, g.pushed());
    g.assertNoStale();
    g.app->scene().homeLat += 0.1;  // home moves: a new chart, and the trails start again
    g.app->sceneUpdated();
    TEST_ASSERT_EQUAL(240 * 320, g.pushed());
    g.assertNoStale();
    TEST_ASSERT_EQUAL(0, g.pushed());
}

static void test_map_plane_leaving_the_chart() {
    Rig g;
    busyMap(g);
    scr::Scene& s = g.app->scene();
    const uint32_t hex = s.flights[0].hex;
    s.flights[0].distNm = 60, s.flights[0].bearingDeg = 90;  // still in the scene, off the chart
    g.app->sceneUpdated();
    const int px = g.pushed();
    TEST_ASSERT_TRUE(px > 0 && px < 240 * 320);
    g.assertNoStale();
    scr::Screen* m = g.app->host().screen(ScreenId::Map);
    TEST_ASSERT_NOT_EQUAL(hex, m->onTap(239, 135).selectHex);
}

// Only the strip's text changes (the closest plane's route): only the strip redraws.
static void test_map_strip_only_change() {
    Rig g;
    busyMap(g);
    std::snprintf(g.app->scene().flights[0].dest, sizeof g.app->scene().flights[0].dest, "SFO");
    g.app->sceneUpdated();
    g.d.pushedRects.clear();
    g.frames(1);
    TEST_ASSERT_FALSE(g.d.pushedRects.empty());
    for (const auto& r : g.d.pushedRects) TEST_ASSERT_TRUE(r.y + r.h > scr::MapStrip::kBox.y - 2);
    g.assertNoStale();
}

// A labelled plane moves onto another's label: labels move or drop, and nothing is left behind.
static void test_map_labels_move_and_drop() {
    Rig g;
    busyMap(g);
    scr::Scene& s = g.app->scene();
    for (int k = 1; k < 4; ++k)
        s.flights[k].distNm = s.flights[0].distNm, s.flights[k].bearingDeg = s.flights[0].bearingDeg + 3.f * k;
    g.app->sceneUpdated();
    const int px = g.pushed();
    TEST_ASSERT_TRUE(px > 0 && px < 240 * 320);
    g.assertNoStale();
    for (int k = 1; k < 4; ++k) s.flights[k].distNm += 4;  // and away again
    g.app->sceneUpdated();
    TEST_ASSERT_TRUE(g.pushed() > 0);
    g.assertNoStale();
}

// ------------------------------------------------------------------ the detailed map (5b)

// The board's home with the detailed map at three radii (levels 0, 0 and 1), both themes.
static void sceneHome(scr::Scene& s, float radius) {
    scr::sceneQuiet(s);  // the quiet scene's plane
    s.homeLat = 39.4176, s.homeLon = -77.3743, s.radiusNm = radius, s.locationSet = true;
}
static void home10(scr::Scene& s) { sceneHome(s, 10); }
static void home25(scr::Scene& s) { sceneHome(s, 25); }
static void home35(scr::Scene& s) { sceneHome(s, 35); }
static void busyHome(scr::Scene& s) {
    scr::sceneBusy(s);
    s.homeLat = 39.4176, s.homeLon = -77.3743, s.radiusNm = 25, s.locationSet = true;
}

// The Rig up on the Map with the fixture pack and `fill`'s scene, the chart finished and saved.
static MemFile& detailMap(Rig& g, void (*fill)(scr::Scene&) = home25) {
    g.useMap();
    fill(g.app->scene());
    g.app->sceneUpdated();
    g.show(ScreenId::Map);
    g.frames(30);
    return *static_cast<MemFile*>(g.map.file());
}

static void test_detail_goldens() {
    golden::render(ScreenId::Map, home10, "map_detail_10", scr::cinematicSky(), 0, 75, false, true);
    golden::render(ScreenId::Map, home25, "map_detail_25", scr::cinematicSky(), 0, 75, false, true);
    golden::render(ScreenId::Map, home35, "map_detail_35", scr::cinematicSky(), 0, 75, false, true);
    golden::render(ScreenId::Map, home25, "map_detail_25_ed", scr::editorial(), 0, 75, false, true);
}

// Part way through: the chart's top rows, water below, no plane, the note in the strip.
static void test_detail_golden_while_drawing() {
    Rig g;
    g.useMap();
    home25(g.app->scene());
    g.app->sceneUpdated();
    g.show(ScreenId::Map);
    g.frames(4);
    assertGolden("map_detail_drawing", g.d.px, 240, 320);
}

static void test_detail_reopens_from_the_saved_chart() {
    Rig g;
    MemFile& f = detailMap(g);
    const int writes = f.writes;
    TEST_ASSERT_TRUE(writes > 0);
    const std::vector<uint16_t> shown = g.d.px;
    g.show(ScreenId::Radar), g.frames(5), g.show(ScreenId::Map), g.frames(2);
    TEST_ASSERT_EQUAL(writes, f.writes);  // no rebuild
    TEST_ASSERT_EQUAL_MEMORY(shown.data(), g.d.px.data(), shown.size() * 2);  // and the marks came back too
    g.assertNoStale();
}

// One write per 16 rows on a multiple of 16, two a frame, each frame pushing only those rows. The first frame
// clears what was up. A full redraw part way through (the screen's fade, say) takes no more of the job.
static void test_detail_job_takes_two_strips_a_frame() {
    Rig g;
    g.useMap();
    home25(g.app->scene());
    g.app->sceneUpdated();
    g.show(ScreenId::Map);
    auto& f = *static_cast<MemFile*>(g.map.file());
    const int before = f.writes;
    TEST_ASSERT_EQUAL(240 * 320, g.pushed());
    TEST_ASSERT_EQUAL(before + 2 + 1, f.writes);  // the cleared key too
    TEST_ASSERT_EQUAL(240 * 32, g.pushed());
    TEST_ASSERT_EQUAL(before + 4 + 1, f.writes);
    g.r.invalidateAll();
    TEST_ASSERT_EQUAL(240 * 320, g.pushed());
    TEST_ASSERT_EQUAL(before + 6 + 1, f.writes);
    g.frames(30);
    g.assertNoStale();
}

static void test_detail_settled_map_is_still_and_reads_no_tiles() {
    Rig g;
    MemFile& f = detailMap(g);
    const int reads = f.reads;
    TEST_ASSERT_EQUAL(0, g.pushed());
    TEST_ASSERT_EQUAL(reads, f.reads);
}

static void test_detail_falls_back_when_home_is_outside() {
    Rig g, plain;
    g.useMap();
    for (Rig* r : {&g, &plain}) {
        home25(r->app->scene());
        r->app->scene().homeLat = 45.0;  // far outside the 40 nm pack
        r->app->sceneUpdated();
        r->show(ScreenId::Map);
        r->frames(30);
    }
    TEST_ASSERT_TRUE(g.map.isOpen());
    TEST_ASSERT_FALSE(g.map.covers(45.0, -77.3743, 25));
    TEST_ASSERT_EQUAL_MEMORY(plain.d.px.data(), g.d.px.data(), plain.d.px.size() * 2);
}

static void test_detail_radius_past_the_pack_uses_the_built_in_map() {
    Rig g, plain;
    g.useMap();
    for (Rig* r : {&g, &plain}) {
        home25(r->app->scene());
        r->app->scene().radiusNm = 50;
        r->app->sceneUpdated();
        r->show(ScreenId::Map);
        r->frames(30);
    }
    TEST_ASSERT_FALSE(g.map.covers(39.4176, -77.3743, 50));
    TEST_ASSERT_EQUAL_MEMORY(plain.d.px.data(), g.d.px.data(), plain.d.px.size() * 2);
    g.app->scene().radiusNm = 25;  // back inside: no settling from the built-in map
    g.app->sceneUpdated();
    g.frames(30);
    TEST_ASSERT_TRUE(g.map.covers(39.4176, -77.3743, 25));
    TEST_ASSERT_TRUE(static_cast<MemFile*>(g.map.file())->writes > 0);
}

// Without a card, or with a card and no pack, the same picture as an app never given a pack.
static void test_detail_no_card_or_no_pack_is_the_built_in_map() {
    Rig plain, noCard, noPack;
    noCard.sd.isMounted = false;
    noCard.map.begin(&noCard.sd), noCard.app->setMap(&noCard.map);
    noPack.map.begin(&noPack.sd), noPack.app->setMap(&noPack.map);
    for (Rig* r : {&plain, &noCard, &noPack}) {
        home25(r->app->scene());
        r->app->sceneUpdated();
        r->show(ScreenId::Map);
        r->frames(10);
    }
    TEST_ASSERT_EQUAL_MEMORY(plain.d.px.data(), noCard.d.px.data(), plain.d.px.size() * 2);
    TEST_ASSERT_EQUAL_MEMORY(plain.d.px.data(), noPack.d.px.data(), plain.d.px.size() * 2);
}

static void test_detail_card_pulled_falls_back_without_a_crash() {
    Rig g, plain;
    MemFile& f = detailMap(g);
    f.fail = true;
    for (Rig* r : {&g, &plain}) {
        if (r == &plain) home25(r->app->scene()), r->app->sceneUpdated(), r->show(ScreenId::Map);
        // The plane moves: the chart is read back. To where its label's best spot is the same with the chart's
        // marks and without them: labels keep their spot (MapPlanes::layout), so one chosen around a mark
        // before the fallback would stay, a difference from the plain map that is not the fallback's.
        r->app->scene().flights[0].bearingDeg += 140;
        r->app->sceneUpdated();
        r->frames(10);
    }
    TEST_ASSERT_FALSE(g.map.isOpen());
    TEST_ASSERT_EQUAL_STRING("card error", g.map.error());
    g.assertNoStale();
    TEST_ASSERT_EQUAL_MEMORY(plain.d.px.data(), g.d.px.data(), plain.d.px.size() * 2);
}

// The card goes while the job runs, on the job's write, then on its reads: the built-in map, whole, with its plane.
static void test_detail_failure_during_the_job_falls_back() {
    for (int mode = 0; mode < 3; ++mode) {
        Rig g, plain;
        g.useMap();
        for (Rig* r : {&g, &plain}) {
            home25(r->app->scene());
            r->app->sceneUpdated();
            r->show(ScreenId::Map);
        }
        auto& f = *static_cast<MemFile*>(g.map.file());
        g.frames(3);
        if (mode == 0) f.writeFail = true;
        if (mode == 1) f.fail = true;
        if (mode == 2) f.failOnRead = f.reads + 1;
        g.frames(12);
        plain.frames(15);
        TEST_ASSERT_FALSE(g.map.isOpen());
        g.assertNoStale();
        TEST_ASSERT_EQUAL_MEMORY(plain.d.px.data(), g.d.px.data(), plain.d.px.size() * 2);
    }
}

// Every read failing from the start: rebuild() itself falls back, within the frame.
static void test_detail_failure_while_starting_falls_back() {
    Rig g, plain;
    g.useMap();
    static_cast<MemFile*>(g.map.file())->fail = true;
    for (Rig* r : {&g, &plain}) {
        home25(r->app->scene());
        r->app->sceneUpdated();
        r->show(ScreenId::Map);
        r->frames(1);
    }
    TEST_ASSERT_FALSE(g.map.isOpen());
    TEST_ASSERT_EQUAL_MEMORY(plain.d.px.data(), g.d.px.data(), plain.d.px.size() * 2);
}

static void test_detail_radius_settles_before_rebuilding() {
    Rig g;
    MemFile& f = detailMap(g);
    const int writes = f.writes;
    const std::vector<uint16_t> shown = g.d.px;
    for (float r : {24.f, 23.f, 22.f}) {
        g.app->scene().radiusNm = r;
        g.app->sceneUpdated();
        g.frames(10);  // 1/3 s each
    }
    TEST_ASSERT_EQUAL(writes, f.writes);
    TEST_ASSERT_EQUAL_MEMORY(shown.data(), g.d.px.data(), shown.size() * 2);  // the old chart stays up
    g.frames(60);  // 2 s steady
    TEST_ASSERT_TRUE(f.writes > writes);
    g.assertNoStale();
}

// Back to the radius it started from before the time is up: nothing to rebuild, and the next change waits in full.
static void test_detail_radius_back_again_cancels_the_settle() {
    Rig g;
    MemFile& f = detailMap(g);
    const int writes = f.writes;
    auto setRadius = [&](float r) {
        g.app->scene().radiusNm = r;
        g.app->sceneUpdated();
    };
    setRadius(20), g.frames(10);
    setRadius(25), g.frames(60);
    TEST_ASSERT_EQUAL(writes, f.writes);
    setRadius(20), g.frames(30);  // 1 s: the same radius as before, and the full wait again
    TEST_ASSERT_EQUAL(writes, f.writes);
    g.frames(30);
    TEST_ASSERT_TRUE(f.writes > writes);
}

// Home moves inside the pack with planes up: no settling, planes off until the chart is done, then back with their
// labels, and nothing left where they were.
static void test_detail_new_home_with_planes_leaves_no_stale_pixels() {
    Rig g;
    g.useMap();
    withHistory(*g.app, busyHome);
    g.show(ScreenId::Map);
    g.frames(30);
    scr::Screen* m = g.app->host().screen(ScreenId::Map);
    scr::Scene& s = g.app->scene();
    const float a = s.flights[0].bearingDeg * 0.0174533f, d = s.flights[0].distNm * 4.8f;
    const int px = int(120 + std::sin(a) * d), py = int(135 - std::cos(a) * d);
    TEST_ASSERT_EQUAL_HEX32(s.flights[0].hex, m->onTap(px, py).selectHex);
    g.assertNoStale();

    s.homeLat += 0.02;
    g.app->sceneUpdated();
    g.frames(3);
    TEST_ASSERT_EQUAL_HEX32(0, m->onTap(px, py).selectHex);   // hidden while drawing
    TEST_ASSERT_FALSE(m->onTap(120, 290).open);               // and the strip names no plane
    s.flights[1].distNm *= 0.9f;                              // the scene goes on changing meanwhile
    g.app->sceneUpdated();
    g.frames(30);
    TEST_ASSERT_EQUAL_HEX32(s.flights[0].hex, m->onTap(px, py).selectHex);  // back
    TEST_ASSERT_TRUE(m->onTap(120, 290).open);
    g.assertNoStale();

    // Planes on a new radius, after the settle: the same.
    s.radiusNm = 20;
    g.app->sceneUpdated();
    g.frames(45 + 2);
    TEST_ASSERT_EQUAL_HEX32(0, m->onTap(px, py).selectHex);
    g.frames(30);
    g.assertNoStale();
    TEST_ASSERT_EQUAL(0, g.pushed());
}

// Planes moving over the finished chart: only their boxes redraw, and the picture equals a full redraw.
static void test_detail_planes_move_over_the_saved_chart() {
    Rig g;
    g.useMap();
    withHistory(*g.app, busyHome);
    g.show(ScreenId::Map);
    g.frames(30);
    scr::Scene& s = g.app->scene();
    for (int step = 0; step < 4; ++step) {
        for (int i = step; i < s.count; i += 5)
            s.flights[i].bearingDeg += 2.f, s.flights[i].distNm *= 0.97f, s.flights[i].trackDeg += 9.f;
        g.app->sceneUpdated();
        TEST_ASSERT_LESS_THAN(240 * 320, g.pushed());
        g.assertNoStale();
    }
}

// Away mid-job, another screen takes the arena, back: the job starts over and finishes.
static void test_detail_leaving_during_the_job() {
    Rig g;
    g.useMap();
    home25(g.app->scene());
    g.app->sceneUpdated();
    g.show(ScreenId::Map);
    g.frames(3);
    g.show(ScreenId::Radar), g.frames(3), g.show(ScreenId::Map), g.frames(30);
    g.assertNoStale();
    auto& f = *static_cast<MemFile*>(g.map.file());
    const int writes = f.writes;
    g.show(ScreenId::Radar), g.frames(3), g.show(ScreenId::Map), g.frames(3);
    TEST_ASSERT_EQUAL(writes, f.writes);
}

// The veil's fades redraw the whole screen every frame; none of it reaches the saved chart.
static void test_detail_job_through_a_fade() {
    Rig g, direct;
    g.useMap(), direct.useMap();
    for (Rig* r : {&g, &direct}) {
        home25(r->app->scene());
        r->app->sceneUpdated();
    }
    g.show(ScreenId::Radar), g.frames(2);
    g.app->host().show(ScreenId::Map);
    g.frames(45);
    direct.show(ScreenId::Map), direct.frames(30);
    TEST_ASSERT_EQUAL_MEMORY(direct.d.px.data(), g.d.px.data(), direct.d.px.size() * 2);
}

// A chart saved for one theme is not shown in the other.
static void test_detail_theme_change_redraws_the_chart() {
    Rig g;
    MemFile& f = detailMap(g);
    const int writes = f.writes;
    g.app->useTheme(scr::editorial());
    g.frames(30);
    TEST_ASSERT_TRUE(f.writes > writes);
    g.assertNoStale();
}

// The key is written only once every row is saved: until then the chart reads as not saved.
static void test_detail_key_comes_after_the_last_rows() {
    Rig g;
    g.useMap();
    auto& f = *static_cast<MemFile*>(g.map.file());
    const uint32_t area = g.map.cacheOffset();
    std::fill(f.bytes.begin() + area, f.bytes.begin() + area + basemap::ChartCache::kKeyAt, uint8_t(0xA5));
    home25(g.app->scene());
    g.app->sceneUpdated();
    g.show(ScreenId::Map);
    bool saved = false;
    for (int frame = 0; frame < 40 && !saved; ++frame) {
        g.frames(1);
        const uint8_t* key = &f.bytes[area + basemap::ChartCache::kKeyAt];
        saved = std::any_of(key, key + basemap::ChartCache::kKeyBytes, [](uint8_t b) { return b != 0; });
        // Saved: no 32-byte run of the chart still holds the fill. A run of 16 pixels of 0xA5A5 does not occur.
        int unwritten = 0;
        for (uint32_t at = 0; saved && at < basemap::ChartCache::kKeyAt; at += 32)
            unwritten += std::all_of(&f.bytes[area + at], &f.bytes[area + at + 32], [](uint8_t b) { return b == 0xA5; });
        TEST_ASSERT_EQUAL(0, unwritten);
    }
    TEST_ASSERT_TRUE(saved);
}

// A card read failing part way through a frame's read-back: the rest of that frame is water, never the built-in map's
// mask (the arena holds the detailed map's scratch).
static void test_detail_drop_frame_is_water() {
    Rig g;
    MemFile& f = detailMap(g, [](scr::Scene& s) { sceneHome(s, 25), s.count = 0; });
    f.fail = true;
    g.r.invalidateAll();
    g.r.frame(1 / 30.f);
    TEST_ASSERT_FALSE(g.map.isOpen());
    const uint16_t water = gfx::rgb565(scr::cinematicSky().mapWater);
    int other = 0;
    for (int i = 0; i < 240 * scr::MapBase::kMapH; ++i) other += g.d.px[i] != water;
    TEST_ASSERT_EQUAL(0, other);
}

// One failed read is enough: the built-in map until restart, though the card answers again.
static void test_detail_transient_read_error_stays_built_in() {
    Rig g, plain;
    MemFile& f = detailMap(g);
    f.failOnRead = f.reads + 1;
    home25(plain.app->scene()), plain.app->sceneUpdated(), plain.show(ScreenId::Map);
    for (Rig* r : {&g, &plain}) {
        r->app->scene().flights[0].bearingDeg += 140;  // as test_detail_card_pulled_falls_back_without_a_crash
        r->app->sceneUpdated();
        r->frames(30);
    }
    TEST_ASSERT_FALSE(g.map.isOpen());
    TEST_ASSERT_EQUAL_MEMORY(plain.d.px.data(), g.d.px.data(), plain.d.px.size() * 2);
}

// A saved chart whose mark count makes no sense is redrawn, not a card error.
static void test_detail_bad_saved_mark_count_redraws() {
    Rig g;
    MemFile& f = detailMap(g);
    const uint32_t at = g.map.cacheOffset() + basemap::ChartCache::kExtraAt;
    f.bytes[at] = 99, f.bytes[at + 1] = f.bytes[at + 2] = f.bytes[at + 3] = 0;
    const int writes = f.writes;
    g.show(ScreenId::Radar), g.frames(3), g.show(ScreenId::Map), g.frames(30);
    TEST_ASSERT_TRUE(g.map.isOpen());
    TEST_ASSERT_TRUE(f.writes > writes);
    g.assertNoStale();
}

// Radius steps that each restart the wait, 2 s of them in all: still no rebuild until one has held for 1.5 s.
static void test_detail_radius_steps_restart_the_settle() {
    Rig g;
    MemFile& f = detailMap(g);
    const int writes = f.writes;
    for (float r : {24.f, 23.f, 22.f}) {
        g.app->scene().radiusNm = r;
        g.app->sceneUpdated();
        g.frames(20);  // 2/3 s each
    }
    TEST_ASSERT_EQUAL(writes, f.writes);
    g.frames(30);
    TEST_ASSERT_TRUE(f.writes > writes);
}

// The home marker's colour is drawn into the saved chart, so it is part of the key.
static void test_detail_home_colour_is_in_the_key() {
    Rig g;
    MemFile& f = detailMap(g);
    const int writes = f.writes;
    scr::Theme t = scr::cinematicSky();
    t.home = {1, 2, 3};
    g.app->useTheme(t);
    g.frames(30);
    TEST_ASSERT_TRUE(f.writes > writes);
}

// With a clock the job paints until the frame has spent 60 ms, at least a band: a slow card or CPU gets a band a
// frame, a fast one takes more (the first frame's full damage, all of it here), and its damage follows.
static uint32_t gClockUs = 0, gClockStepUs = 0;
static uint32_t fakeClock() { return gClockUs += gClockStepUs; }

static void test_detail_job_paces_by_the_clock() {
    for (uint32_t step : {70000u, 0u}) {
        Rig g;
        g.useMap();
        gClockStepUs = step;
        g.app->setMapClock(fakeClock);
        home25(g.app->scene());
        g.app->sceneUpdated();
        g.show(ScreenId::Map);
        auto& f = *static_cast<MemFile*>(g.map.file());
        const int before = f.writes;
        g.frames(1);  // the first frame damages everything
        if (step) {
            TEST_ASSERT_EQUAL(before + 1 + 1, f.writes);  // the cleared key and one band
            TEST_ASSERT_EQUAL(240 * 16, g.pushed());       // then a band a frame, and only that band damaged
            TEST_ASSERT_EQUAL(before + 1 + 2, f.writes);
            TEST_ASSERT_EQUAL(240 * 16, g.pushed());
            TEST_ASSERT_EQUAL(before + 1 + 3, f.writes);
            g.frames(16);
        } else {
            TEST_ASSERT_EQUAL(before + 1 + 17, f.writes);  // no time passes: all of it
            g.frames(1);
        }
        g.assertNoStale();
        TEST_ASSERT_EQUAL(0, g.pushed());
        TEST_ASSERT_TRUE(g.app->host().screen(ScreenId::Map)->onTap(120, 290).open);  // finished: the plane shows
    }
}

// A slow start (a band a frame), then frames with time to spare: the damage asked for grows a band a frame.
static void test_detail_job_damage_grows_with_spare_time() {
    Rig g;
    g.useMap();
    gClockStepUs = 70000;
    g.app->setMapClock(fakeClock);
    home25(g.app->scene());
    g.app->sceneUpdated();
    g.show(ScreenId::Map);
    auto& f = *static_cast<MemFile*>(g.map.file());
    g.frames(3);
    TEST_ASSERT_EQUAL(240 * 16, g.pushed());  // one band a frame
    gClockStepUs = 2000;
    std::vector<int> bands;
    scr::Screen* m = g.app->host().screen(ScreenId::Map);
    for (int i = 0; i < 20 && !m->onTap(120, 290).open; ++i) {
        const int before = f.writes;
        g.frames(1);
        if (!m->onTap(120, 290).open) bands.push_back(f.writes - before);  // not the frame that saves the marks
    }
    TEST_ASSERT_TRUE(m->onTap(120, 290).open);
    TEST_ASSERT_TRUE(bands.size() >= 2);
    TEST_ASSERT_EQUAL(1, bands[0]);  // asked for one, then for one more each time it fits
    TEST_ASSERT_EQUAL(2, bands[1]);
    g.assertNoStale();
}

constexpr double kHalfPixelReads = 51.0;  // measured here with half-pixel steps

// Card reads a frame on a busy detailed map: 30 planes over 60 s at 5 frames a second, a second ticking every 5.
static double detailReadsPerFrame() {
    Rig g;
    g.useMap();
    withHistory(*g.app, [](scr::Scene& s) { busyHome(s), s.count = std::min(s.count, 30); });
    g.show(ScreenId::Map);
    g.frames(30);
    auto& f = *static_cast<MemFile*>(g.map.file());
    scr::Scene& s = g.app->scene();
    const int before = f.reads;
    for (int t = 0; t < 300; ++t) {
        backInTime(s, -0.2f);  // 0.2 s on (the epoch moves in whole seconds, below)
        if (t % 5 == 4) s.epoch += 1;
        g.app->sceneUpdated();
        g.r.frame(0.2f);
    }
    g.assertNoStale();
    return double(f.reads - before) / 300;
}

// Planes on the detailed chart move a whole pixel at a time: they redraw less often, and each redraw reads the chart's
// tiles from the card. The model measured 49 tiles a frame before, 29 after, for 30 planes at 25 nm.
static void test_detail_planes_move_in_whole_pixels() {
    const double reads = detailReadsPerFrame();
    printf("detailed map: %.1f tile reads a frame\n", reads);
    TEST_ASSERT_TRUE(reads < 0.7 * kHalfPixelReads);
}

// The plane's position is the same whole pixel for drawing, hit-testing and its label: 2.375 nm due north is
// y = 123.6, drawn at 123.5 on the built-in map and at 124 on the detailed one; taps reach 20 px from it.
static void test_detail_taps_find_the_plane_where_it_is_drawn() {
    Rig plain, detail;
    auto north = [](scr::Scene& s) { sceneHome(s, 25), s.flights[0].bearingDeg = 0, s.flights[0].distNm = 2.375f; };
    detailMap(detail, north);
    north(plain.app->scene()), plain.app->sceneUpdated(), plain.show(ScreenId::Map), plain.frames(3);
    scr::Screen* d = detail.app->host().screen(ScreenId::Map);
    scr::Screen* b = plain.app->host().screen(ScreenId::Map);
    TEST_ASSERT_EQUAL_HEX32(0xA1B2C3, d->onTap(120, 124 + 20).selectHex);
    TEST_ASSERT_EQUAL_HEX32(0, b->onTap(120, 124 + 20).selectHex);  // 20.5 px from 123.5
    TEST_ASSERT_EQUAL_HEX32(0, d->onTap(120, 124 - 21).selectHex);
    TEST_ASSERT_EQUAL_HEX32(0xA1B2C3, b->onTap(120, 124 - 20).selectHex);  // 19.5 px
    detail.app->host().setSelection(0xA1B2C3);  // the ring and label move with it, and leave nothing behind
    detail.frames(2);
    detail.assertNoStale();
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_map_empty);
    RUN_TEST(test_map_ocean);
    RUN_TEST(test_map_no_location);
    RUN_TEST(test_map_base_redraws_only_on_a_new_view);
    RUN_TEST(test_map_busy);
    RUN_TEST(test_map_quiet_selected);
    RUN_TEST(test_map_updates_leave_no_stale_pixels);
    RUN_TEST(test_map_band_change_recolours_the_trail);
    RUN_TEST(test_map_family_change_while_selected);
    RUN_TEST(test_map_taps);
    RUN_TEST(test_map_nothing_below_the_chart);
    RUN_TEST(test_map_tap_reach);
    RUN_TEST(test_map_taps_without_a_location);
    RUN_TEST(test_map_new_radius_and_home_with_planes);
    RUN_TEST(test_map_plane_leaving_the_chart);
    RUN_TEST(test_map_strip_only_change);
    RUN_TEST(test_map_labels_move_and_drop);
    RUN_TEST(test_detail_goldens);
    RUN_TEST(test_detail_golden_while_drawing);
    RUN_TEST(test_detail_reopens_from_the_saved_chart);
    RUN_TEST(test_detail_job_takes_two_strips_a_frame);
    RUN_TEST(test_detail_settled_map_is_still_and_reads_no_tiles);
    RUN_TEST(test_detail_falls_back_when_home_is_outside);
    RUN_TEST(test_detail_radius_past_the_pack_uses_the_built_in_map);
    RUN_TEST(test_detail_no_card_or_no_pack_is_the_built_in_map);
    RUN_TEST(test_detail_card_pulled_falls_back_without_a_crash);
    RUN_TEST(test_detail_failure_during_the_job_falls_back);
    RUN_TEST(test_detail_failure_while_starting_falls_back);
    RUN_TEST(test_detail_radius_settles_before_rebuilding);
    RUN_TEST(test_detail_radius_back_again_cancels_the_settle);
    RUN_TEST(test_detail_new_home_with_planes_leaves_no_stale_pixels);
    RUN_TEST(test_detail_planes_move_over_the_saved_chart);
    RUN_TEST(test_detail_leaving_during_the_job);
    RUN_TEST(test_detail_job_through_a_fade);
    RUN_TEST(test_detail_theme_change_redraws_the_chart);
    RUN_TEST(test_detail_key_comes_after_the_last_rows);
    RUN_TEST(test_detail_drop_frame_is_water);
    RUN_TEST(test_detail_transient_read_error_stays_built_in);
    RUN_TEST(test_detail_bad_saved_mark_count_redraws);
    RUN_TEST(test_detail_radius_steps_restart_the_settle);
    RUN_TEST(test_detail_home_colour_is_in_the_key);
    RUN_TEST(test_detail_job_paces_by_the_clock);
    RUN_TEST(test_detail_job_damage_grows_with_spare_time);
    RUN_TEST(test_detail_planes_move_in_whole_pixels);
    RUN_TEST(test_detail_taps_find_the_plane_where_it_is_drawn);
    return UNITY_END();
}
