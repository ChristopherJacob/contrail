#include <unity.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <utility>
#include <vector>

#include "FrameCapture.h"
#include "GoldenRender.h"
#include "data/Shapes.h"
#include "gfx/StripRenderer.h"
#include "screens/DemoScenes.h"
#include "screens/ScreenApp.h"
#include "screens/SkyProjection.h"
#include "services/Astro.h"

void setUp() {
    setenv("TZ", "UTC0", 1);
    tzset();
}
void tearDown() {}

// The device draws in 16-row strips (main_esp32.cpp kStripH).
static constexpr int kStripH = 16;
static uint16_t bufA[240 * kStripH], bufB[240 * kStripH];

// Fixed scene and a fixed animation time, so any rendering change shows up as a diff.
static void render(scr::ScreenId id, void (*fill)(scr::Scene&), const char* golden, uint32_t selectHex = 0,
                   int frames = 75) {
    auto app = std::make_unique<scr::ScreenApp>();
    fill(app->scene());
    app->sceneUpdated();
    app->host().showNow(id);
    if (selectHex) app->host().setSelection(selectHex);
    FrameCapture d;
    gfx::StripRenderer r(d, bufA, bufB, kStripH);
    r.setLayers(app->layers(), app->layerCount());
    for (int i = 0; i < frames; ++i) r.frame(1 / 30.f);  // 2.5 s of animation by default
    assertGolden(golden, d.px, 240, 320);
    // Damage tracking: a full redraw of the same moment must match the incrementally drawn frame.
    std::vector<uint16_t> incremental = d.px;
    r.invalidateAll();
    r.frame(0);
    int stale = 0;
    for (size_t i = 0; i < incremental.size(); ++i) stale += incremental[i] != d.px[i];
    TEST_ASSERT_EQUAL_MESSAGE(0, stale, "pixels left stale by incremental redraw");
}

// Moving traffic (labels included), a new selection and a departing flight: the incremental
// redraw must still match a full one.
static void test_radar_updates_leave_no_stale_pixels() {
    auto app = std::make_unique<scr::ScreenApp>();
    scr::sceneBusy(app->scene());
    app->sceneUpdated();
    app->host().showNow(scr::ScreenId::Radar);
    FrameCapture d;
    gfx::StripRenderer r(d, bufA, bufB, kStripH);
    r.setLayers(app->layers(), app->layerCount());
    for (int step = 0; step < 4; ++step) {
        for (int i = 0; i < 10; ++i) r.frame(1 / 30.f);
        scr::Scene& s = app->scene();
        // A few flights at a time, so the damage stays in rects rather than overflowing to a full redraw.
        for (int i = step; i < s.count; i += 6) s.flights[i].bearingDeg += 2.5f, s.flights[i].distNm *= 0.97f;
        // Select a flight, and drop the farthest one (the last in the nearest-first list).
        if (step == 1) app->host().setSelection(s.flights[7].hex), --s.count;
        app->sceneUpdated();
        d.pushedRects.clear();
        r.frame(1 / 30.f);
        int pushed = 0;
        for (const auto& p : d.pushedRects) pushed += p.w * p.h;
        TEST_ASSERT_LESS_THAN_MESSAGE(240 * 320 * 2 / 3, pushed, "scene update redrew most of the screen");
    }
    r.frame(1 / 30.f);
    std::vector<uint16_t> incremental = d.px;
    r.invalidateAll();
    r.frame(0);
    int stale = 0;
    for (size_t i = 0; i < incremental.size(); ++i) stale += incremental[i] != d.px[i];
    TEST_ASSERT_EQUAL_MESSAGE(0, stale, "pixels left stale by incremental redraw");
}

// Closest Flight: a new selection, then the featured plane turning and moving across the sky
// and its data changing; the incremental redraw must match a full one.
static void test_closest_updates_leave_no_stale_pixels() {
    auto app = std::make_unique<scr::ScreenApp>();
    scr::sceneBusy(app->scene());
    app->sceneUpdated();
    app->host().showNow(scr::ScreenId::Closest);
    FrameCapture d;
    gfx::StripRenderer r(d, bufA, bufB, kStripH);
    r.setLayers(app->layers(), app->layerCount());
    for (int i = 0; i < 20; ++i) r.frame(1 / 30.f);  // contrail half grown
    app->host().setSelection(app->scene().flights[2].hex);
    for (int step = 0; step < 4; ++step) {
        for (int i = 0; i < 7; ++i) r.frame(1 / 30.f);
        scr::SceneFlight& f = app->scene().flights[2];
        f.trackDeg += 23.f, f.bearingDeg += 3.3f, f.distNm *= 0.96f, f.altFt += 250, f.progress += 0.013f;
        app->sceneUpdated();
        d.pushedRects.clear();
        r.frame(1 / 30.f);
        int pushed = 0;
        for (const auto& p : d.pushedRects) pushed += p.w * p.h;
        TEST_ASSERT_LESS_THAN_MESSAGE(240 * 320, pushed, "scene update fell back to a full-screen redraw");
    }
    r.frame(1 / 30.f);
    std::vector<uint16_t> incremental = d.px;
    r.invalidateAll();
    r.frame(0);
    int stale = 0;
    for (size_t i = 0; i < incremental.size(); ++i) stale += incremental[i] != d.px[i];
    TEST_ASSERT_EQUAL_MESSAGE(0, stale, "pixels left stale by incremental redraw");
}

// The featured flight changing family redraws its sky plane and the side view on the route bar, in
// both themes (the bar is drawn differently in each), including to and from no shape (the bar's dot).
static void test_closest_family_change_leaves_no_stale_pixels() {
    for (const scr::Theme* t : {&scr::cinematicSky(), &scr::editorial()}) {
        auto app = std::make_unique<scr::ScreenApp>(*t);
        scr::sceneQuiet(app->scene());
        app->sceneUpdated();
        app->host().showNow(scr::ScreenId::Closest);
        FrameCapture d;
        gfx::StripRenderer r(d, bufA, bufB, kStripH);
        r.setLayers(app->layers(), app->layerCount());
        for (int i = 0; i < 10; ++i) r.frame(1 / 30.f);
        for (data::Family fam : {data::Family::Helicopter, data::Family::Superjumbo, data::Family::None,
                                 data::Family::Single}) {
            app->scene().flights[0].shape = uint8_t(fam);
            app->sceneUpdated();
            r.frame(1 / 30.f);
            std::vector<uint16_t> incremental = d.px;
            r.invalidateAll();
            r.frame(0);
            int stale = 0;
            for (size_t i = 0; i < incremental.size(); ++i) stale += incremental[i] != d.px[i];
            TEST_ASSERT_EQUAL_MESSAGE(0, stale, "pixels left stale by a family change");
        }
    }
}

static void test_closest_busy() { render(scr::ScreenId::Closest, scr::sceneBusy, "closest_busy"); }
static void test_closest_no_route() { render(scr::ScreenId::Closest, scr::sceneNoRoute, "closest_no_route"); }
static void test_closest_empty() { render(scr::ScreenId::Closest, scr::sceneEmpty, "closest_empty"); }

// With a card holding UAL's and DAL's logos: United's logo left of UAL53, the callsign moved right.
static void test_closest_logo() {
    golden::render(scr::ScreenId::Closest, scr::sceneBusy, "closest_logo", scr::cinematicSky(), 0, 75, true);
}
// The card is in but has no logo for American: its fin in the logo's place (and a six-character callsign
// beside the badge). Without a card at all, closest_busy shows United's fin.
static void test_closest_fin() {
    golden::render(scr::ScreenId::Closest, scr::sceneBusy, "closest_fin", scr::cinematicSky(), 0xA5F607, 75, true);
}

// The badge's square (Cinematic: the card; Bold Editorial: the band), copied out of the frame.
static std::vector<uint16_t> badgePixels(const golden::Rig& g, bool flat) {
    const gfx::Rect r = flat ? gfx::Rect{10, 8, 30, 30} : gfx::Rect{12, 145, 40, 40};
    std::vector<uint16_t> px;
    for (int y = r.y; y < r.y + r.h; ++y)
        for (int x = r.x; x < r.x + r.w; ++x) px.push_back(g.d.px[size_t(y) * 240 + x]);
    return px;
}

// The featured flight moving between airlines with logos (UAL53, DAL22: the same store reloaded), one with
// only a fin (AAL101) and a registration (N512KA, its shape); leaving for the Board (which takes the arena)
// and coming back; and a theme change while Closest is up. Each redraws the badge, leaves no stale pixels, and
// a logo is really (re)loaded: UAL's and DAL's badges differ, and UAL's comes back the same.
static void test_closest_identity_changes_leave_no_stale_pixels() {
    constexpr uint32_t kUal = 0xA1B2C3, kDal = 0xA2C3D4, kAal = 0xA5F607, kReg = 0xA4E5F6;
    for (const scr::Theme* t : {&scr::cinematicSky(), &scr::editorial()}) {
        const scr::Theme& other = t->flat ? scr::cinematicSky() : scr::editorial();
        golden::Rig g(*t);
        g.useLogos();
        scr::sceneBusy(g.app->scene());
        g.app->sceneUpdated();
        g.show(scr::ScreenId::Closest);
        auto select = [&g](uint32_t hex) {
            g.app->host().setSelection(hex);
            g.frames(3);
            g.assertNoStale();
        };
        select(kUal);
        const std::vector<uint16_t> ual = badgePixels(g, t->flat);
        select(kDal);
        TEST_ASSERT_TRUE_MESSAGE(badgePixels(g, t->flat) != ual, "DAL's logo was not loaded over UAL's");
        for (uint32_t hex : {kAal, kReg, kUal, kReg, kAal, kUal}) select(hex);
        TEST_ASSERT_TRUE_MESSAGE(badgePixels(g, t->flat) == ual, "UAL's logo did not come back");
        // The Board claims the arena; coming back reclaims it and must load the logo again.
        g.show(scr::ScreenId::Board);
        g.frames(3);
        g.show(scr::ScreenId::Closest);
        g.frames(3);
        g.assertNoStale();
        TEST_ASSERT_TRUE_MESSAGE(badgePixels(g, t->flat) == ual, "logo not reloaded after the Board");
        select(kDal);
        TEST_ASSERT_TRUE(badgePixels(g, t->flat) != ual);
        select(kUal);
        // A theme change re-enters: the other theme's store, then back.
        g.app->useTheme(other);
        g.frames(3);
        g.assertNoStale();
        g.app->useTheme(*t);
        g.frames(3);
        g.assertNoStale();
        TEST_ASSERT_TRUE_MESSAGE(badgePixels(g, t->flat) == ual, "logo not reloaded after a theme change");
    }
}

// Night sky, a descending flight and a three-line airline / type / registration block.
static void test_closest_night() { render(scr::ScreenId::Closest, scr::sceneNight, "closest_night"); }

static void test_radar_busy() { render(scr::ScreenId::Radar, scr::sceneBusy, "radar_busy"); }
static void test_radar_quiet_selected() { render(scr::ScreenId::Radar, scr::sceneQuiet, "radar_quiet", 0xA1B2C3); }
static void test_radar_empty() { render(scr::ScreenId::Radar, scr::sceneEmpty, "radar_empty"); }

// Incremental redraw of the current moment must match a full one.
static void assertNoStalePixels(FrameCapture& d, gfx::StripRenderer& r) {
    std::vector<uint16_t> incremental = d.px;
    r.invalidateAll();
    r.frame(0);
    int stale = 0;
    for (size_t i = 0; i < incremental.size(); ++i) stale += incremental[i] != d.px[i];
    TEST_ASSERT_EQUAL_MESSAGE(0, stale, "pixels left stale by incremental redraw");
}

// Closest Flight edge cases: the sun crossing SkyLayer's redraw threshold and the horizon, the
// selected flight vanishing (falls back to the nearest), and flight -> empty -> flight.
static void test_closest_scene_changes_leave_no_stale_pixels() {
    auto app = std::make_unique<scr::ScreenApp>();
    scr::sceneBusy(app->scene());
    app->sceneUpdated();
    app->host().showNow(scr::ScreenId::Closest);
    FrameCapture d;
    gfx::StripRenderer r(d, bufA, bufB, kStripH);
    r.setLayers(app->layers(), app->layerCount());
    for (int i = 0; i < 80; ++i) r.frame(1 / 30.f);
    scr::Scene& s = app->scene();
    auto settle = [&](int n) {
        for (int i = 0; i < n; ++i) r.frame(1 / 30.f);
        assertNoStalePixels(d, r);
    };

    // A drift under the sky's threshold changes nothing; then the sun sinks through the horizon
    // (the disc and the plane's avoidance of it go away) and on into the star field.
    float e0 = s.sunElev;
    s.sunElev = e0 + 0.1f;
    app->sceneUpdated();
    settle(3);
    for (float e : {2.f, 0.4f, -0.3f, -5.f, -14.f, 6.f}) {
        s.sunElev = e;
        app->sceneUpdated();
        settle(4);
    }

    // The selected flight disappears: the card falls back to the nearest.
    app->host().setSelection(s.flights[2].hex);
    settle(10);
    for (int i = 2; i < s.count - 1; ++i) s.flights[i] = s.flights[i + 1];
    --s.count;
    app->sceneUpdated();
    settle(10);

    // Everything leaves, then a flight comes back.
    scr::SceneFlight keep = s.flights[0];
    int n = s.count;
    s.count = 0;
    app->sceneUpdated();
    settle(5);
    s.count = n;
    s.flights[0] = keep;
    app->sceneUpdated();
    settle(20);
}

// A bearing-only change moves the compass needle and nothing else: no sky, plane or card redraw.
static void test_closest_bearing_only_damages_compass() {
    auto app = std::make_unique<scr::ScreenApp>();
    scr::sceneBusy(app->scene());
    app->sceneUpdated();
    app->host().showNow(scr::ScreenId::Closest);
    FrameCapture d;
    gfx::StripRenderer r(d, bufA, bufB, kStripH);
    r.setLayers(app->layers(), app->layerCount());
    for (int i = 0; i < 80; ++i) r.frame(1 / 30.f);  // contrail fully grown
    scr::SceneFlight& f = app->scene().flights[0];
    for (float db : {4.f, 7.f, 5.f}) {
        f.bearingDeg = std::fmod(f.bearingDeg + db, 360.f);
        app->sceneUpdated();
        d.pushedRects.clear();
        r.frame(0);  // no time passes, so the bob and the contrail hold still
        TEST_ASSERT_TRUE_MESSAGE(!d.pushedRects.empty(), "a bearing change redrew nothing");
        for (const auto& p : d.pushedRects) {
            // The renderer pushes whole strips of the damaged columns, so allow the rounding to 32 rows.
            bool inside = p.x >= 190 && p.x + p.w <= 232 && p.y + p.h <= 64;
            char msg[64];
            std::snprintf(msg, sizeof msg, "redrew %d,%d %dx%d", p.x, p.y, p.w, p.h);
            TEST_ASSERT_TRUE_MESSAGE(inside, msg);
        }
        assertNoStalePixels(d, r);
    }
}

// One frame that must not fall back to a full-screen redraw; returns the pixels pushed.
static int partialFrame(FrameCapture& d, gfx::StripRenderer& r) {
    d.pushedRects.clear();
    r.frame(1 / 30.f);
    int pushed = 0;
    for (const auto& p : d.pushedRects) pushed += p.w * p.h;
    TEST_ASSERT_LESS_THAN_MESSAGE(240 * 320, pushed, "board update fell back to a full-screen redraw");
    return pushed;
}

static void test_board_rows_settle_after_reorder() {
    auto app = std::make_unique<scr::ScreenApp>();
    scr::sceneBusy(app->scene());
    app->sceneUpdated();
    app->host().showNow(scr::ScreenId::Board);
    FrameCapture d;
    gfx::StripRenderer r(d, bufA, bufB, kStripH);
    r.setLayers(app->layers(), app->layerCount());
    for (int i = 0; i < 30; ++i) r.frame(1 / 30.f);
    std::swap(app->scene().flights[0], app->scene().flights[1]);  // the two nearest swap places
    app->sceneUpdated();
    for (int i = 0; i < 30; ++i) r.frame(1 / 30.f);
    assertGolden("board_reordered", d.px, 240, 320);
    assertNoStalePixels(d, r);
}

// A reorder mid-glide, a row leaving, a new one fading in and changing text: every frame stays a
// partial redraw, and the incremental result matches a full redraw both mid-animation and settled.
static void test_board_updates_leave_no_stale_pixels() {
    auto app = std::make_unique<scr::ScreenApp>();
    scr::sceneBusy(app->scene());
    app->sceneUpdated();
    app->host().showNow(scr::ScreenId::Board);
    FrameCapture d;
    gfx::StripRenderer r(d, bufA, bufB, kStripH);
    r.setLayers(app->layers(), app->layerCount());
    for (int i = 0; i < 20; ++i) r.frame(1 / 30.f);
    scr::Scene& s = app->scene();

    // The nearest and the fourth swap: two rows glide past the two between them.
    std::swap(s.flights[0], s.flights[3]);
    app->sceneUpdated();
    for (int i = 0; i < 4; ++i) partialFrame(d, r);
    assertNoStalePixels(d, r);

    // Mid-glide: the order changes again, so rows turn around from where they are.
    std::swap(s.flights[0], s.flights[1]);
    app->sceneUpdated();
    for (int i = 0; i < 3; ++i) partialFrame(d, r);

    // The third flight leaves (the rest move up) and a new aircraft arrives in fifth place.
    for (int i = 2; i < s.count - 1; ++i) s.flights[i] = s.flights[i + 1];
    s.flights[4].hex = 0xC0FFEE;
    std::snprintf(s.flights[4].callsign, sizeof s.flights[4].callsign, "ASA1234");
    app->sceneUpdated();
    for (int i = 0; i < 5; ++i) partialFrame(d, r);
    assertNoStalePixels(d, r);
    for (int i = 0; i < 10; ++i) partialFrame(d, r);  // settle

    // Values change in place: distance and climb on the nearest card.
    s.flights[0].distNm += 1.3f, s.flights[0].vrateFpm = -1500, s.flights[0].altFt -= 400;
    app->sceneUpdated();
    int pushed = partialFrame(d, r);
    TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(240 * 64, pushed, "a text change redrew more than its row");
    for (int i = 0; i < 20; ++i) partialFrame(d, r);
    assertNoStalePixels(d, r);

    // At rest nothing is redrawn.
    d.pushedRects.clear();
    r.frame(1 / 30.f);
    TEST_ASSERT_EQUAL_MESSAGE(0, int(d.pushedRects.size()), "a settled board kept redrawing");

    // Down to two flights: four rows fade out.
    s.count = 2;
    app->sceneUpdated();
    for (int i = 0; i < 12; ++i) partialFrame(d, r);
    assertNoStalePixels(d, r);
}

// Tapping a card opens that flight in Closest Flight.
static void test_board_tap_opens_closest() {
    auto app = std::make_unique<scr::ScreenApp>();
    scr::sceneBusy(app->scene());
    app->sceneUpdated();
    app->host().showNow(scr::ScreenId::Board);
    FrameCapture d;
    gfx::StripRenderer r(d, bufA, bufB, kStripH);
    r.setLayers(app->layers(), app->layerCount());
    for (int i = 0; i < 10; ++i) r.frame(1 / 30.f);
    int y = scr::BoardList::kFirstY + 2 * scr::BoardList::kPitch + 10;  // third card
    app->touch(true, 120, y, 1000);
    app->touch(true, 120, y, 1030);
    app->touch(false, 120, y, 1080);
    TEST_ASSERT_EQUAL(int(scr::ScreenId::Closest), int(app->host().target()));
    TEST_ASSERT_EQUAL_HEX32(app->scene().flights[2].hex, app->host().selection());
}

// What a tap at the middle of board row `i` selects (0 = nothing), straight from the screen.
static uint32_t boardTap(scr::ScreenApp& app, int y) {
    return app.host().screen(scr::ScreenId::Board)->onTap(120, y).selectHex;
}
static int rowY(int i) { return scr::BoardList::kFirstY + i * scr::BoardList::kPitch + 10; }

static void setBoardFlights(scr::Scene& s, int n, uint32_t hexBase, int from = 0) {
    TEST_ASSERT_GREATER_OR_EQUAL(n, s.count);
    s.count = n;
    for (int i = from; i < n; ++i) {
        s.flights[i].hex = hexBase + i;
        std::snprintf(s.flights[i].callsign, sizeof s.flights[i].callsign, "TST%d", int(hexBase + i) & 0xFFF);
    }
}

// New flights arriving every few frames while others fade out must never take the row of a flight
// that is still listed, even with every slot busy.
static void test_board_new_flights_never_steal_listed_rows() {
    auto app = std::make_unique<scr::ScreenApp>();
    scr::sceneBusy(app->scene());
    scr::Scene& s = app->scene();
    setBoardFlights(s, 6, 0x100);
    app->sceneUpdated();
    app->host().showNow(scr::ScreenId::Board);
    FrameCapture d;
    gfx::StripRenderer r(d, bufA, bufB, kStripH);
    r.setLayers(app->layers(), app->layerCount());
    for (int i = 0; i < 20; ++i) r.frame(1 / 30.f);
    for (int round = 1; round <= 8; ++round) {
        // The nearest two stay; the other four are replaced, so up to 10 rows exist at once.
        setBoardFlights(s, 6, 0x1000 + 0x100 * round, 2);
        app->sceneUpdated();
        for (int i = 0; i < 2; ++i) r.frame(1 / 30.f);
        TEST_ASSERT_EQUAL_HEX32(0x100, boardTap(*app, rowY(0)));
        TEST_ASSERT_EQUAL_HEX32(0x101, boardTap(*app, rowY(1)));
    }
    for (int i = 0; i < 30; ++i) r.frame(1 / 30.f);
    for (int i = 0; i < 6; ++i) TEST_ASSERT_EQUAL_HEX32(s.flights[i].hex, boardTap(*app, rowY(i)));
    assertNoStalePixels(d, r);

    // A flight with hex 0 is never treated as an empty slot's match: it gets no card.
    s.flights[3].hex = 0;
    app->sceneUpdated();
    for (int i = 0; i < 30; ++i) r.frame(1 / 30.f);
    TEST_ASSERT_EQUAL_HEX32(0, boardTap(*app, rowY(3)));
    TEST_ASSERT_EQUAL_HEX32(s.flights[2].hex, boardTap(*app, rowY(2)));
}

// Leaving the board and coming back while rows glide shows a settled list, with the right cards.
static void test_board_enter_mid_glide() {
    auto app = std::make_unique<scr::ScreenApp>();
    scr::sceneBusy(app->scene());
    app->sceneUpdated();
    app->host().showNow(scr::ScreenId::Board);
    FrameCapture d;
    gfx::StripRenderer r(d, bufA, bufB, kStripH);
    r.setLayers(app->layers(), app->layerCount());
    for (int i = 0; i < 20; ++i) r.frame(1 / 30.f);
    scr::Scene& s = app->scene();
    std::swap(s.flights[0], s.flights[3]);
    app->sceneUpdated();
    for (int i = 0; i < 4; ++i) r.frame(1 / 30.f);  // mid-glide
    app->host().showNow(scr::ScreenId::Radar);
    r.setLayers(app->layers(), app->layerCount());
    for (int i = 0; i < 3; ++i) r.frame(1 / 30.f);
    app->host().showNow(scr::ScreenId::Board);
    r.setLayers(app->layers(), app->layerCount());
    r.frame(1 / 30.f);
    // Rows appear in place, in the new order, with no leftover glide.
    for (int i = 0; i < 6; ++i) TEST_ASSERT_EQUAL_HEX32(s.flights[i].hex, boardTap(*app, rowY(i)));
    std::vector<uint16_t> first = d.px;
    for (int i = 0; i < 20; ++i) r.frame(1 / 30.f);
    assertNoStalePixels(d, r);
    TEST_ASSERT_TRUE(first == d.px);
    d.pushedRects.clear();
    r.frame(1 / 30.f);
    TEST_ASSERT_EQUAL_MESSAGE(0, int(d.pushedRects.size()), "a settled board kept redrawing");
}

// The screen arena hands its scratch from screen to screen. Going round the other arena users
// (Radar, Board, Sky) through the veil and back must draw exactly what a fresh app draws on that
// screen. The fresh app gets as many frames on the screen as the round trip gave it (the host
// updates only the current screen), so time kept outside the arena (the radar's sweep, the sky's
// beacon clock) matches and only the hand-off differs.
static void checkArenaRoundTrip(void (*fill)(scr::Scene&), scr::ScreenId end) {
    using scr::ScreenId;
    const ScreenId users[] = {ScreenId::Radar, ScreenId::Board, ScreenId::Sky};
    ScreenId trip[4];
    trip[0] = trip[3] = end;
    int n = 1;
    for (ScreenId u : users)
        if (u != end) trip[n++] = u;
    TEST_ASSERT_EQUAL(3, n);

    auto app = std::make_unique<scr::ScreenApp>();
    fill(app->scene());
    app->sceneUpdated();
    app->host().showNow(end);
    FrameCapture d;
    gfx::StripRenderer r(d, bufA, bufB, kStripH);
    r.setLayers(app->layers(), app->layerCount());
    int onEnd = 0;  // frames in which `end` was the screen updated
    auto run = [&](int frames) {
        for (int i = 0; i < frames; ++i) {
            r.frame(1 / 30.f);
            onEnd += app->host().current() == end;
        }
    };
    run(30);
    for (int k = 1; k < 4; ++k) {
        app->host().show(trip[k]);
        run(30);  // 1 s: the 0.4 s veil and some time on the screen
        TEST_ASSERT_FALSE(app->host().transitioning());
        TEST_ASSERT_EQUAL(int(trip[k]), int(app->host().current()));
    }
    run(60);  // settle
    assertNoStalePixels(d, r);

    auto fresh = std::make_unique<scr::ScreenApp>();
    fill(fresh->scene());
    fresh->sceneUpdated();
    fresh->host().showNow(end);
    FrameCapture fd;
    gfx::StripRenderer fr(fd, bufA, bufB, kStripH);
    fr.setLayers(fresh->layers(), fresh->layerCount());
    for (int i = 0; i < onEnd; ++i) fr.frame(1 / 30.f);
    int differ = 0;
    for (size_t i = 0; i < d.px.size(); ++i) differ += d.px[i] != fd.px[i];
    TEST_ASSERT_EQUAL_MESSAGE(0, differ, "pixels differ from a fresh app after the arena round trip");
}

static void test_arena_round_trip_sky() { checkArenaRoundTrip(scr::sceneNight, scr::ScreenId::Sky); }
static void test_arena_round_trip_radar() { checkArenaRoundTrip(scr::sceneBusy, scr::ScreenId::Radar); }

// Two cards crossing mid-glide overlap; a tap there opens the one drawn on top (the later slot).
static void test_board_tap_overlap_hits_topmost() {
    auto app = std::make_unique<scr::ScreenApp>();
    scr::sceneBusy(app->scene());
    app->sceneUpdated();
    app->host().showNow(scr::ScreenId::Board);
    FrameCapture d;
    gfx::StripRenderer r(d, bufA, bufB, kStripH);
    r.setLayers(app->layers(), app->layerCount());
    for (int i = 0; i < 20; ++i) r.frame(1 / 30.f);
    scr::Scene& s = app->scene();
    uint32_t lower = s.flights[1].hex;  // second slot: drawn after (above) the first
    std::swap(s.flights[0], s.flights[1]);
    app->sceneUpdated();
    for (int i = 0; i < 4; ++i) r.frame(1 / 30.f);
    int y = scr::BoardList::kFirstY + scr::BoardList::kPitch + 12;  // inside both cards
    TEST_ASSERT_EQUAL_HEX32(lower, boardTap(*app, y));
}

static void test_board_busy() { render(scr::ScreenId::Board, scr::sceneBusy, "board_busy"); }
// With the fixture card (UAL, DAL and KLM logos): UAL's and DAL's logos on the first two rows, the other airlines' fins and
// N512KA's shape tile.
static void test_board_logos() {
    golden::render(scr::ScreenId::Board, scr::sceneBusy, "board_logos", scr::cinematicSky(), 0, 75, true);
}

// Board row `i`'s badge square, copied out of the frame.
static std::vector<uint16_t> boardBadge(const golden::Rig& g, int i) {
    const int top = scr::BoardList::kFirstY + i * scr::BoardList::kPitch + 6;
    std::vector<uint16_t> px;
    for (int y = top; y < top + 25; ++y)
        for (int x = 14; x < 14 + 25; ++x) px.push_back(g.d.px[size_t(y) * 240 + x]);
    return px;
}

// The nearest row keeps its aircraft while its flight changes airline (UAL53 to DAL22, whose logo is already in
// a slot for the second row, so the row only points at another slot) and then reports only its registration
// (the shape tile): each change redraws the badge exactly. Then, with six airlines listed (every slot needed),
// the nearest row turns into a seventh with a logo (KLM): UAL's slot is the only one free, so the same slot index
// is reloaded with new contents, and the row must still redraw.
static void test_board_airline_change_in_place() {
    for (const scr::Theme* t : {&scr::cinematicSky(), &scr::editorial()}) {
        golden::Rig g(*t);
        g.useLogos();
        scr::sceneBusy(g.app->scene());
        g.app->sceneUpdated();
        g.show(scr::ScreenId::Board);
        g.frames(15);
        scr::SceneFlight& f = g.app->scene().flights[0];
        TEST_ASSERT_EQUAL_STRING("UAL53", f.callsign);
        const std::vector<uint16_t> ual = boardBadge(g, 0);
        std::vector<std::vector<uint16_t>> seen{ual};
        for (const char* cs : {"DAL22", "N24974", "UAL53"}) {
            std::snprintf(f.callsign, sizeof f.callsign, "%s", cs);
            g.app->sceneUpdated();
            g.frames(2);
            g.assertNoStale();
            seen.push_back(boardBadge(g, 0));
        }
        TEST_ASSERT_TRUE_MESSAGE(seen[1] != ual, "DAL's logo did not replace UAL's");
        TEST_ASSERT_TRUE_MESSAGE(seen[2] != ual && seen[2] != seen[1], "the registration kept a logo");
        TEST_ASSERT_TRUE_MESSAGE(seen[3] == ual, "UAL's logo did not come back");

        // Six airlines listed: UAL, DAL, SWA, ASA (in N512KA's place), AAL, JBU.
        scr::SceneFlight& reg = g.app->scene().flights[3];
        TEST_ASSERT_EQUAL_STRING("N512KA", reg.callsign);
        std::snprintf(reg.callsign, sizeof reg.callsign, "ASA312");
        g.app->sceneUpdated();
        g.frames(15);
        g.assertNoStale();
        TEST_ASSERT_TRUE(boardBadge(g, 0) == ual);
        std::snprintf(f.callsign, sizeof f.callsign, "KLM601");
        g.app->sceneUpdated();
        g.frames(2);
        g.assertNoStale();
        const std::vector<uint16_t> klm = boardBadge(g, 0);
        TEST_ASSERT_TRUE_MESSAGE(klm != ual, "KLM's logo did not replace UAL's in the reused slot");
        TEST_ASSERT_TRUE_MESSAGE(klm != boardBadge(g, 1), "KLM's row shows DAL's badge");
    }
}

// Logos are read from the card only when an airline new to the board is listed: a settled board re-sent the same
// scene reads nothing, and a second row of an airline the pack lacks (SWA: its slot holds an empty badge) does
// not look it up again.
static void test_board_logo_read_budget() {
    for (const scr::Theme* t : {&scr::cinematicSky(), &scr::editorial()}) {
        golden::Rig g(*t);
        g.useLogos();
        scr::sceneBusy(g.app->scene());
        g.app->sceneUpdated();
        g.show(scr::ScreenId::Board);
        g.frames(15);
        const MemFile& pak = *g.sd.files.at(assets::LogoStore::kPath);
        const int settled = pak.reads;
        TEST_ASSERT_GREATER_THAN(0, settled);
        for (int i = 0; i < 5; ++i) {
            g.app->sceneUpdated();
            g.frames(3);
        }
        TEST_ASSERT_EQUAL_MESSAGE(settled, pak.reads, "a settled board read the card again");
        scr::SceneFlight& f = g.app->scene().flights[4];  // AAL101 becomes a second Southwest flight
        std::snprintf(f.callsign, sizeof f.callsign, "SWA417");
        g.app->sceneUpdated();
        g.frames(5);
        g.assertNoStale();
        TEST_ASSERT_EQUAL_MESSAGE(settled, pak.reads, "a second row of an airline without a logo read the card");
    }
}

// Airlines passing through the board, more of them over time than there are logo slots: each wave replaces
// every row (so the new rows' slots are taken from rows still fading out), mid-fade and settled frames match a
// full redraw, and at the end UAL and DAL (back after their slots were reused) show their own, different logos.
static void test_board_logo_slots_recycle() {
    static const char* kAirlines[] = {"UAL", "DAL", "AAL", "SWA", "JBU", "BAW", "AFR", "DLH", "KLM", "ASA", "UAE",
                                      "QTR", "ACA", "DAL", "UAL", "FFT"};
    constexpr int kN = int(sizeof kAirlines / sizeof *kAirlines);
    for (const scr::Theme* t : {&scr::cinematicSky(), &scr::editorial()}) {
        golden::Rig g(*t);
        g.useLogos();
        scr::Scene& s = g.app->scene();
        scr::sceneBusy(s);
        g.app->sceneUpdated();
        g.show(scr::ScreenId::Board);
        g.frames(15);
        const std::vector<uint16_t> ual = boardBadge(g, 0), dal = boardBadge(g, 1);
        TEST_ASSERT_TRUE(ual != dal);
        s.count = 6;
        int next = 0;
        for (int wave = 0; wave < 8; ++wave) {
            for (int i = 0; i < 6; ++i) {
                scr::SceneFlight& f = s.flights[i];
                f.hex = 0x300000 + uint32_t(wave * 16 + i);
                std::snprintf(f.callsign, sizeof f.callsign, "%s%d", kAirlines[next++ % kN], 100 + i);
            }
            g.app->sceneUpdated();
            g.frames(4);  // the old rows fading out, the new fading in
            g.assertNoStale();
            g.frames(12);
            g.assertNoStale();
        }
        // UAL and DAL back on the first two rows.
        std::snprintf(s.flights[0].callsign, sizeof s.flights[0].callsign, "UAL53");
        std::snprintf(s.flights[1].callsign, sizeof s.flights[1].callsign, "DAL22");
        s.flights[0].hex = 0x3F0000, s.flights[1].hex = 0x3F0001;
        g.app->sceneUpdated();
        g.frames(15);
        g.assertNoStale();
        TEST_ASSERT_TRUE_MESSAGE(boardBadge(g, 0) != boardBadge(g, 1), "UAL and DAL rows show the same badge");
        TEST_ASSERT_TRUE_MESSAGE(boardBadge(g, 0) == ual, "UAL's logo was not reloaded into a recycled slot");
        TEST_ASSERT_TRUE_MESSAGE(boardBadge(g, 1) == dal, "DAL's logo was not reloaded into a recycled slot");
    }
}
static void test_board_quiet() { render(scr::ScreenId::Board, scr::sceneQuiet, "board_quiet"); }
static void test_board_empty() { render(scr::ScreenId::Board, scr::sceneEmpty, "board_empty"); }
// Long callsigns, airline names and type names, and two-digit distances: routes that would run
// into the distance column are dropped and long "airline · type" lines are shortened.
static void sceneLongNames(scr::Scene& s) {
    scr::sceneBusy(s);
    auto set = [](scr::SceneFlight& f, const char* cs, const char* type, float dist) {
        std::snprintf(f.callsign, sizeof f.callsign, "%s", cs);
        std::snprintf(f.type, sizeof f.type, "%s", type);
        f.distNm = dist;
    };
    set(s.flights[0], "VIR1234", "B77W", 12.f);    // fits with the route only when it is short
    set(s.flights[1], "BAW2276", "A35K", 13.f);
    set(s.flights[2], "TAP1347", "A21N", 14.f);    // trimmed with an ellipsis
    set(s.flights[3], "N9876QZ", "PC12", 15.f);    // no airline: the type alone
    set(s.flights[4], "QTR7311", "B788", 21.f);
    set(s.flights[5], "VIR12345", "C700", 24.f);  // eight characters and a long type: trimmed
    std::snprintf(s.flights[5].orig, 4, "MWM"), std::snprintf(s.flights[5].dest, 4, "WMW");  // too wide: dropped
}
static void test_board_long_names() { render(scr::ScreenId::Board, sceneLongNames, "board_long_names"); }
static void test_board_night() { render(scr::ScreenId::Board, scr::sceneNight, "board_night"); }

// 3.33 s in: a moment when DAL22's beacon is lit.
static void test_sky_night() { render(scr::ScreenId::Sky, scr::sceneNight, "sky_night", 0, 100); }
static void test_sky_dusk_busy() { render(scr::ScreenId::Sky, scr::sceneBusy, "sky_dusk"); }
static void test_sky_empty() { render(scr::ScreenId::Sky, scr::sceneEmpty, "sky_empty"); }
static void test_sky_day() { render(scr::ScreenId::Sky, scr::sceneQuiet, "sky_day"); }
// By day the moon shows only its lit part, faintly: a waxing crescent east of the afternoon sun.
static void sceneDayMoon(scr::Scene& s) {
    scr::sceneQuiet(s);
    s.moonElev = 40, s.moonAz = 140, s.moonIllum = 0.22f, s.moonWaxing = true;
}
static void test_sky_day_moon() { render(scr::ScreenId::Sky, sceneDayMoon, "sky_day_moon"); }
// In the twilight the earthshine side is only starting to show: a first-quarter moon in the south.
static void sceneDuskMoon(scr::Scene& s) {
    scr::sceneQuiet(s);
    s.sunElev = -5, s.sunAz = 255;
    s.moonElev = 35, s.moonAz = 165, s.moonIllum = 0.5f, s.moonWaxing = true;
}
static void test_sky_dusk_moon() { render(scr::ScreenId::Sky, sceneDuskMoon, "sky_dusk_moon"); }
// A plane low at the rim, a degree or two up, is drawn over the treetops, label and all.
static void sceneRimPlane(scr::Scene& s) {
    scr::sceneQuiet(s);
    s.count = 0;
    scr::SceneFlight& f = s.flights[s.count++];
    f = scr::SceneFlight{};
    f.hex = 0xABCDEF, f.altFt = 2500, f.distNm = 18, f.bearingDeg = 200, f.trackDeg = 300, f.elevDeg = 1.3f;
    std::snprintf(f.callsign, sizeof f.callsign, "SKW5259");
    std::snprintf(f.type, sizeof f.type, "E75L");
}
static void test_sky_plane_over_trees() {
    auto app = std::make_unique<scr::ScreenApp>();
    sceneRimPlane(app->scene());
    app->sceneUpdated();
    app->host().showNow(scr::ScreenId::Sky);
    FrameCapture d;
    gfx::StripRenderer r(d, bufA, bufB, kStripH);
    r.setLayers(app->layers(), app->layerCount());
    for (int i = 0; i < 5; ++i) r.frame(1 / 30.f);
    assertGolden("sky_rim_plane", d.px, 240, 320);
    std::vector<uint16_t> with = d.px;
    // The same frame without the plane: the plane's pixels differ, on the treetops too.
    app->scene().count = 0;
    app->sceneUpdated();
    for (int i = 0; i < 5; ++i) r.frame(1 / 30.f);
    gfx::Vec2 p = scr::domeProject(200, 1.3f);
    int changed = 0;
    for (int y = int(p.y) - 6; y <= int(p.y) + 6; ++y)
        for (int x = int(p.x) - 6; x <= int(p.x) + 6; ++x) changed += with[y * 240 + x] != d.px[y * 240 + x];
    TEST_ASSERT_GREATER_THAN_MESSAGE(30, changed, "the low plane is hidden behind the trees");
    assertNoStalePixels(d, r);
}

// ------------------------------------------------------------------ Sky View

// Pixels one frame pushes, and whether that was the whole screen.
static int pushedPx(FrameCapture& d, gfx::StripRenderer& r, float dt = 1 / 30.f) {
    d.pushedRects.clear();
    r.frame(dt);
    int pushed = 0;
    for (const auto& p : d.pushedRects) pushed += p.w * p.h;
    return pushed;
}
static void assertPartial(FrameCapture& d, gfx::StripRenderer& r, const char* what) {
    TEST_ASSERT_LESS_THAN_MESSAGE(240 * 320, pushedPx(d, r), what);
}

// Planes moving and turning, the highest one leaving (the tag and caption move to the next), a
// new one appearing, beacons blinking throughout: every frame is a partial redraw, and the
// incremental result always matches a full one.
static void test_sky_planes_update_without_stale_pixels() {
    auto app = std::make_unique<scr::ScreenApp>();
    scr::sceneBusy(app->scene());
    app->sceneUpdated();
    app->host().showNow(scr::ScreenId::Sky);
    FrameCapture d;
    gfx::StripRenderer r(d, bufA, bufB, kStripH);
    r.setLayers(app->layers(), app->layerCount());
    for (int i = 0; i < 20; ++i) r.frame(1 / 30.f);
    scr::Scene& s = app->scene();
    for (int step = 0; step < 6; ++step) {
        for (int i = 0; i < 7; ++i) assertPartial(d, r, "a beacon blink fell back to a full-screen redraw");
        for (int i = step % 3; i < s.count; i += 3)
            s.flights[i].bearingDeg += 1.7f, s.flights[i].elevDeg += 0.9f, s.flights[i].trackDeg += 11.f;
        if (step == 2) {  // the highest plane leaves
            int top = 0;
            for (int i = 1; i < s.count; ++i)
                if (s.flights[i].elevDeg > s.flights[top].elevDeg) top = i;
            for (int i = top; i < s.count - 1; ++i) s.flights[i] = s.flights[i + 1];
            --s.count;
        }
        if (step == 3) {  // a new plane appears high overhead
            scr::SceneFlight& f = s.flights[s.count++];
            f = s.flights[0];
            f.hex = 0xC0FFEE, f.elevDeg = 71, f.bearingDeg = 135, f.altFt = 36000;
            std::snprintf(f.callsign, sizeof f.callsign, "ASA1234");
        }
        app->sceneUpdated();
        int pushed = pushedPx(d, r);
        char msg[64];
        std::snprintf(msg, sizeof msg, "sky scene update step %d: %d px", step, pushed);
        TEST_MESSAGE(msg);
        TEST_ASSERT_LESS_THAN_MESSAGE(240 * 320 / 2, pushed, "a plane update redrew half the screen or more");
        assertNoStalePixels(d, r);
    }
}

// Idle, only the beacons change: a few small boxes, never the dome.
static void test_sky_idle_frames_are_cheap() {
    auto app = std::make_unique<scr::ScreenApp>();
    scr::sceneBusy(app->scene());
    app->sceneUpdated();
    app->host().showNow(scr::ScreenId::Sky);
    FrameCapture d;
    gfx::StripRenderer r(d, bufA, bufB, kStripH);
    r.setLayers(app->layers(), app->layerCount());
    for (int i = 0; i < 5; ++i) r.frame(1 / 30.f);
    long total = 0;
    int most = 0;
    for (int i = 0; i < 72; ++i) {  // two beacon periods
        int p = pushedPx(d, r);
        total += p, most = std::max(most, p);
    }
    char msg[96];
    std::snprintf(msg, sizeof msg, "idle sky: %ld px/frame on average, %d at most", total / 72, most);
    TEST_MESSAGE(msg);
    TEST_ASSERT_LESS_THAN_MESSAGE(240 * 64, most, msg);
    assertNoStalePixels(d, r);
}

// The sky drifts: small sun and moon moves within a minute wait (no full redraw); the next minute
// recomputes the stars, sun and moon in one full redraw; a jump (night to dusk) redraws at once.
static void test_sky_recomputes_once_a_minute() {
    auto app = std::make_unique<scr::ScreenApp>();
    scr::sceneNight(app->scene());
    app->sceneUpdated();
    app->host().showNow(scr::ScreenId::Sky);
    FrameCapture d;
    gfx::StripRenderer r(d, bufA, bufB, kStripH);
    r.setLayers(app->layers(), app->layerCount());
    for (int i = 0; i < 10; ++i) r.frame(1 / 30.f);
    scr::Scene& s = app->scene();

    for (int k = 0; k < 4; ++k) {  // 5 Hz rebuilds within the same minute, the moon and sun creeping
        s.epoch += 1, s.moonAz += 0.2f, s.sunElev -= 0.1f;
        app->sceneUpdated();
        assertPartial(d, r, "a drift within the minute redrew the whole dome");
    }
    assertNoStalePixels(d, r);
    std::vector<uint16_t> before = d.px;

    s.epoch += 60;  // the next minute: everything is recomputed together
    app->sceneUpdated();
    TEST_ASSERT_EQUAL_MESSAGE(240 * 320, pushedPx(d, r), "the minute rollover did not redraw the sky");
    int changed = 0;
    for (size_t i = 0; i < before.size(); ++i) changed += before[i] != d.px[i];
    TEST_ASSERT_GREATER_THAN_MESSAGE(50, changed, "stars and moon did not move at the minute rollover");
    assertNoStalePixels(d, r);

    s.epoch += 1;  // nothing new: nothing but beacons
    app->sceneUpdated();
    assertPartial(d, r, "an unchanged sky redrew the whole dome");

    s.epoch += 1, s.sunElev = 3, s.sunAz = 250;  // a jump: at once
    app->sceneUpdated();
    TEST_ASSERT_EQUAL(240 * 320, pushedPx(d, r));
    for (int i = 0; i < 10; ++i) r.frame(1 / 30.f);
    assertNoStalePixels(d, r);

    s.epoch += 1, s.sunElev += 0.6f;  // without stars, a drift still waits for the minute...
    app->sceneUpdated();
    assertPartial(d, r, "a sun drift redrew the dome before the minute was up");
    s.epoch += 60;  // ...and then goes
    app->sceneUpdated();
    TEST_ASSERT_EQUAL(240 * 320, pushedPx(d, r));
    s.epoch += 60;  // a new minute with nothing to change redraws nothing
    app->sceneUpdated();
    assertPartial(d, r, "a quiet minute redrew the whole dome");
    assertNoStalePixels(d, r);
}

// Everything leaves ("Quiet skies"), then traffic returns.
static void test_sky_empty_and_back() {
    auto app = std::make_unique<scr::ScreenApp>();
    scr::sceneNight(app->scene());
    app->sceneUpdated();
    app->host().showNow(scr::ScreenId::Sky);
    FrameCapture d;
    gfx::StripRenderer r(d, bufA, bufB, kStripH);
    r.setLayers(app->layers(), app->layerCount());
    for (int i = 0; i < 10; ++i) r.frame(1 / 30.f);
    scr::Scene& s = app->scene();
    int n = s.count;
    s.count = 0;
    app->sceneUpdated();
    assertPartial(d, r, "planes leaving redrew the whole screen");
    for (int i = 0; i < 40; ++i) r.frame(1 / 30.f);
    assertNoStalePixels(d, r);
    s.count = n;
    app->sceneUpdated();
    assertPartial(d, r, "planes returning redrew the whole screen");
    for (int i = 0; i < 40; ++i) r.frame(1 / 30.f);
    assertNoStalePixels(d, r);
}

// Tapping a plane opens it in Closest Flight; tapping open sky opens the highest one.
static void test_sky_tap_opens_closest() {
    for (int which = 0; which < 2; ++which) {
        auto app = std::make_unique<scr::ScreenApp>();
        scr::sceneNight(app->scene());
        app->sceneUpdated();
        app->host().showNow(scr::ScreenId::Sky);
        FrameCapture d;
        gfx::StripRenderer r(d, bufA, bufB, kStripH);
        r.setLayers(app->layers(), app->layerCount());
        for (int i = 0; i < 5; ++i) r.frame(1 / 30.f);
        const scr::SceneFlight& ual = *app->scene().find(0xA1B2C3);  // not the highest (DAL22)
        gfx::Vec2 p = which ? gfx::Vec2{200, 240} : scr::domeProject(ual.bearingDeg, ual.elevDeg);
        int x = int(p.x), y = int(p.y);
        app->touch(true, x, y, 1000), app->touch(true, x, y, 1030), app->touch(false, x, y, 1080);
        TEST_ASSERT_EQUAL(int(scr::ScreenId::Closest), int(app->host().target()));
        TEST_ASSERT_EQUAL_HEX32(which ? 0xA2C3D4 : 0xA1B2C3, app->host().selection());
    }
}

// Real stars in their real places: Polaris sits due north at the observer's latitude, and Vega
// where the catalogue puts it at this instant; each is brighter than the sky round it.
static void test_sky_stars_are_where_they_should_be() {
    auto app = std::make_unique<scr::ScreenApp>();
    scr::sceneEmpty(app->scene());
    app->sceneUpdated();
    app->host().showNow(scr::ScreenId::Sky);
    FrameCapture d;
    gfx::StripRenderer r(d, bufA, bufB, kStripH);
    r.setLayers(app->layers(), app->layerCount());
    r.frame(1 / 30.f);
    const scr::Scene& s = app->scene();
    auto lum = [&](int x, int y) {
        gfx::Rgb c = gfx::unpack565(d.px[y * 240 + x]);
        return c.r + c.g + c.b;
    };
    struct {
        double ra, dec;
    } stars[] = {{37.95, 89.26}, {279.23, 38.78}};  // Polaris, Vega
    for (auto st : stars) {
        svc::HorizPos h = svc::equatorialToHorizontal(st.ra, st.dec, s.epoch, s.homeLat, s.homeLon);
        TEST_ASSERT_TRUE(h.elevationDeg > 10);
        gfx::Vec2 p = scr::domeProject(float(h.azimuthDeg), float(h.elevationDeg));
        int peak = 0, ring = 0;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) peak = std::max(peak, lum(int(p.x) + dx, int(p.y) + dy));
        for (int k = 0; k < 8; ++k)
            ring = std::max(ring, lum(int(p.x + 6 * std::cos(k * 0.785f)), int(p.y + 6 * std::sin(k * 0.785f))));
        TEST_ASSERT_GREATER_THAN(ring + 40, peak);
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_radar_busy);
    RUN_TEST(test_radar_quiet_selected);
    RUN_TEST(test_radar_empty);
    RUN_TEST(test_radar_updates_leave_no_stale_pixels);
    RUN_TEST(test_closest_busy);
    RUN_TEST(test_closest_no_route);
    RUN_TEST(test_closest_empty);
    RUN_TEST(test_closest_night);
    RUN_TEST(test_closest_logo);
    RUN_TEST(test_closest_fin);
    RUN_TEST(test_closest_identity_changes_leave_no_stale_pixels);
    RUN_TEST(test_closest_updates_leave_no_stale_pixels);
    RUN_TEST(test_closest_family_change_leaves_no_stale_pixels);
    RUN_TEST(test_closest_scene_changes_leave_no_stale_pixels);
    RUN_TEST(test_closest_bearing_only_damages_compass);
    RUN_TEST(test_board_busy);
    RUN_TEST(test_board_logos);
    RUN_TEST(test_board_airline_change_in_place);
    RUN_TEST(test_board_logo_read_budget);
    RUN_TEST(test_board_logo_slots_recycle);
    RUN_TEST(test_board_quiet);
    RUN_TEST(test_board_empty);
    RUN_TEST(test_board_night);
    RUN_TEST(test_board_long_names);
    RUN_TEST(test_board_rows_settle_after_reorder);
    RUN_TEST(test_board_updates_leave_no_stale_pixels);
    RUN_TEST(test_board_tap_opens_closest);
    RUN_TEST(test_board_new_flights_never_steal_listed_rows);
    RUN_TEST(test_board_enter_mid_glide);
    RUN_TEST(test_arena_round_trip_sky);
    RUN_TEST(test_arena_round_trip_radar);
    RUN_TEST(test_board_tap_overlap_hits_topmost);
    RUN_TEST(test_sky_night);
    RUN_TEST(test_sky_dusk_busy);
    RUN_TEST(test_sky_empty);
    RUN_TEST(test_sky_day);
    RUN_TEST(test_sky_day_moon);
    RUN_TEST(test_sky_dusk_moon);
    RUN_TEST(test_sky_plane_over_trees);
    RUN_TEST(test_sky_planes_update_without_stale_pixels);
    RUN_TEST(test_sky_idle_frames_are_cheap);
    RUN_TEST(test_sky_recomputes_once_a_minute);
    RUN_TEST(test_sky_empty_and_back);
    RUN_TEST(test_sky_tap_opens_closest);
    RUN_TEST(test_sky_stars_are_where_they_should_be);
    return UNITY_END();
}
