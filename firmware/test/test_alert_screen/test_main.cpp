#include <unity.h>

#include <cstdlib>
#include <cstring>
#include <ctime>

#include "GoldenRender.h"
#include "data/Shapes.h"
#include "gfx/Badge.h"
#include "gfx/Font.h"
#include "screens/DemoScenes.h"

using scr::AlertKind;
using scr::ScreenId;

void setUp() {
    setenv("TZ", "UTC0", 1);
    tzset();
}
void tearDown() {}

// sceneQuiet's one flight as the alert's plane.
static void alertOn(scr::Scene& s, AlertKind k) {
    scr::sceneQuiet(s);
    scr::SceneAlert& a = s.alert;
    a = scr::SceneAlert{};
    a.active = true, a.kind = k, a.live = true;
    a.flight = s.flights[0];
    a.startMs = 1000, a.endsMs = 31000;
    if (k == AlertKind::Emergency) std::strcpy(a.flight.squawk, "7700"), a.flight.emergency = true;
    if (k == AlertKind::Watchlist) std::strcpy(a.matched, "N512*");
    if (k == AlertKind::Military) a.flight.military = true;
    if (k == AlertKind::LowPass) a.flight.altFt = 2500, a.flight.distNm = 1.2f;
}
static void emergency(scr::Scene& s) { alertOn(s, AlertKind::Emergency); }
static void watchlist(scr::Scene& s) { alertOn(s, AlertKind::Watchlist); }
static void military(scr::Scene& s) { alertOn(s, AlertKind::Military); }
static void lowPass(scr::Scene& s) { alertOn(s, AlertKind::LowPass); }

static void test_cinematic_goldens() {
    golden::render(ScreenId::Alert, emergency, "alert_emergency");
    golden::render(ScreenId::Alert, watchlist, "alert_watchlist");
    golden::render(ScreenId::Alert, military, "alert_military");
    golden::render(ScreenId::Alert, lowPass, "alert_lowpass");
}

static void test_editorial_goldens() {
    golden::render(ScreenId::Alert, emergency, "ed_alert_emergency", scr::editorial());
    golden::render(ScreenId::Alert, watchlist, "ed_alert_watchlist", scr::editorial());
    golden::render(ScreenId::Alert, military, "ed_alert_military", scr::editorial());
    golden::render(ScreenId::Alert, lowPass, "ed_alert_lowpass", scr::editorial());
}

// With the fixture pack on a card: UAL's logo left of the hero.
static void test_logo_goldens() {
    golden::render(ScreenId::Alert, emergency, "alert_logo", scr::cinematicSky(), 0, 75, true);
    golden::render(ScreenId::Alert, emergency, "ed_alert_logo", scr::editorial(), 0, 75, true);
}

// The alert's 40 px badge square, copied out of the frame: Cinematic centres it with the hero, so it moves
// with the callsign's width (in the display face: these callsigns all fit beside the badge).
static std::vector<uint16_t> alertBadge(const golden::Rig& g, const scr::Theme& t, const char* callsign) {
    const int x = t.flat ? 14 : 120 - (49 + gfx::textWidth(*t.fontDisplay, callsign)) / 2;
    const int y = t.flat ? 206 - 34 : 218 - 34;
    std::vector<uint16_t> px;
    for (int r = y; r < y + 40; ++r)
        for (int c = x; c < x + 40; ++c) px.push_back(g.d.px[size_t(r) * 240 + c]);
    return px;
}

// The emergency alert for `callsign` without logos, settled: its fin.
static std::vector<uint16_t> finBadge(const scr::Theme& t, const char* callsign) {
    golden::Rig g(t);
    scr::Scene& s = g.app->scene();
    emergency(s);
    std::strcpy(s.alert.flight.callsign, callsign);
    s.alert.soft = true;
    g.app->sceneUpdated();
    g.show(ScreenId::Alert);
    g.frames(3);
    return alertBadge(g, t, callsign);
}

// The alerting flight moving between airlines with logos (UAL, DAL: the scratch reloaded), to a registration
// (its shape tile) and to another shape: each redraws the badge with no stale pixels, and DAL's logo really
// replaces UAL's (and is not DAL's fin). Then the fixture's logos go away (the screen is entered again): UAL's fin.
static void test_identity_changes_leave_no_stale_pixels() {
    for (const scr::Theme* t : {&scr::cinematicSky(), &scr::editorial()}) {
        golden::Rig g(*t);
        g.useLogos();
        scr::Scene& s = g.app->scene();
        emergency(s);
        s.alert.soft = true;  // the border and rings hold still: only the changes redraw
        g.app->sceneUpdated();
        g.show(ScreenId::Alert);
        g.frames(3);
        g.assertNoStale();
        const std::vector<uint16_t> ual = alertBadge(g, *t, "UAL53");
        auto to = [&](const char* callsign) {
            std::strcpy(s.alert.flight.callsign, callsign);
            g.app->sceneUpdated();
            g.frames(2);
            g.assertNoStale();
        };
        to("DAL22");
        TEST_ASSERT_TRUE_MESSAGE(alertBadge(g, *t, "DAL22") != ual, "DAL's logo was not loaded over UAL's");
        TEST_ASSERT_TRUE_MESSAGE(alertBadge(g, *t, "DAL22") != finBadge(*t, "DAL22"), "DAL shows its fin, not its logo");
        to("UAL53");
        TEST_ASSERT_TRUE_MESSAGE(alertBadge(g, *t, "UAL53") == ual, "UAL's logo did not come back");
        to("N24974");  // a registration: the family tile
        const std::vector<uint16_t> tile = alertBadge(g, *t, "N24974");
        TEST_ASSERT_TRUE(tile != ual);
        s.alert.flight.shape = uint8_t(data::Family::Helicopter);
        g.app->sceneUpdated();
        g.frames(2);
        g.assertNoStale();
        TEST_ASSERT_TRUE_MESSAGE(alertBadge(g, *t, "N24974") != tile, "the shape tile did not follow the family");
        to("UAL53");
        TEST_ASSERT_TRUE(alertBadge(g, *t, "UAL53") == ual);
        g.app->setLogos(nullptr);  // re-enters: UAL's fin
        g.frames(2);
        g.assertNoStale();
        TEST_ASSERT_TRUE_MESSAGE(alertBadge(g, *t, "UAL53") != ual, "the logo outlived its store");
    }
}

// An eight-character callsign with the badge: the Cinematic group stays inside the border, the Editorial hero
// inside the margin (both take the title face).
static void test_long_callsign_fits() {
    for (const scr::Theme* t : {&scr::cinematicSky(), &scr::editorial()}) {
        golden::Rig g(*t);
        scr::Scene& s = g.app->scene();
        emergency(s);
        std::strcpy(s.alert.flight.callsign, "WWWW8888");
        s.alert.soft = true;
        g.app->sceneUpdated();
        g.show(ScreenId::Alert);
        g.frames(3);
        g.assertNoStale();
        // Columns 0..11 and 228..239 of the hero's rows hold only the background and the border.
        const int y0 = t->flat ? 170 : 184, y1 = t->flat ? 212 : 226;
        auto edge = [&](int x) {
            std::vector<uint16_t> v;
            for (int y = y0; y < y1; ++y) v.push_back(g.d.px[size_t(y) * 240 + x]);
            return v;
        };
        golden::Rig ref(*t);  // the same alert with a short callsign
        emergency(ref.app->scene());
        ref.app->scene().alert.soft = true;
        ref.app->sceneUpdated();
        ref.show(ScreenId::Alert);
        ref.frames(3);
        auto refEdge = [&](int x) {
            std::vector<uint16_t> v;
            for (int y = y0; y < y1; ++y) v.push_back(ref.d.px[size_t(y) * 240 + x]);
            return v;
        };
        // Editorial: the hero ends by x 226 (the margin), and the gap between the badge and the hero (x 54..61)
        // holds only background, so the hero does not run into the badge.
        std::vector<int> clear = {11, 228};
        if (t->flat) {
            clear.push_back(226), clear.push_back(227);
            for (int x = 14 + 40; x < 62; ++x) clear.push_back(x);
        }
        for (int x : clear) TEST_ASSERT_TRUE_MESSAGE(edge(x) == refEdge(x), "the long callsign reached a margin or gap");
        if (t->flat) {  // the gap is the plain background, row by row
            const uint16_t bg = g.d.px[size_t(y0) * 240 + 58];
            for (int x = 54; x < 62; ++x)
                for (int y = y0; y < y1; ++y) TEST_ASSERT_EQUAL_HEX16(bg, g.d.px[size_t(y) * 240 + x]);
        }
    }
}

// One logo load for one airline: 30 frames and scene updates that change the alert's flight but not its
// airline (its numbers, then another UAL callsign) read the card no more than the first load did.
static void test_same_airline_loads_the_logo_once() {
    for (const scr::Theme* t : {&scr::cinematicSky(), &scr::editorial()}) {
        golden::Rig g(*t);
        g.useLogos();
        const MemFile& pak = *g.sd.files.at(assets::LogoStore::kPath);
        // What one 40 px load reads, measured on a store of the test's own.
        auto scratch = std::make_unique<gfx::Badge40>();
        int before = pak.reads;
        TEST_ASSERT_TRUE(g.logos.load("UAL", *scratch));
        const int oneLoad = pak.reads - before;
        TEST_ASSERT_GREATER_THAN(0, oneLoad);
        scr::Scene& s = g.app->scene();
        emergency(s);  // UAL53
        before = pak.reads;
        g.show(ScreenId::Alert);  // the screen up before (Closest) loaded nothing yet: the scene came after
        g.app->sceneUpdated();
        g.frames(30);
        TEST_ASSERT_EQUAL_MESSAGE(oneLoad, pak.reads - before, "the alert's logo was not loaded exactly once");
        for (int i = 0; i < 5; ++i) {
            s.alert.flight.altFt += 500, s.alert.flight.distNm += 0.1f;
            g.app->sceneUpdated();
            g.frames(3);
        }
        std::strcpy(s.alert.flight.callsign, "UAL1234");
        g.app->sceneUpdated();
        g.frames(3);
        g.assertNoStale();
        TEST_ASSERT_EQUAL_MESSAGE(oneLoad, pak.reads - before, "the same airline's logo was loaded again");
    }
}

// Closest Flight's 40 px (Cinematic) or 30 px (Editorial) badge square.
static std::vector<uint16_t> closestBadge(const golden::Rig& g, bool flat) {
    const gfx::Rect r = flat ? gfx::Rect{10, 8, 30, 30} : gfx::Rect{12, 145, 40, 40};
    std::vector<uint16_t> px;
    for (int y = r.y; y < r.y + r.h; ++y)
        for (int x = r.x; x < r.x + r.w; ++x) px.push_back(g.d.px[size_t(y) * 240 + x]);
    return px;
}

// An alert over Closest Flight claims the arena for its own logo (UAL's, over Closest's DAL); when it times
// out the host enters Closest again, which claims the arena back and reloads DAL's logo.
static void test_closest_reclaims_the_arena_after_an_alert() {
    for (const scr::Theme* t : {&scr::cinematicSky(), &scr::editorial()}) {
        golden::Rig g(*t);
        g.useLogos();
        scr::ScreenApp& app = *g.app;
        alerts::AlertConfig c;
        c.military = true;
        app.configureAlerts(c);
        scr::sceneNight(app.scene());  // DAL22 nearest, then UAL53 and N512KA
        ++app.scene().version;
        app.sceneUpdated();
        app.tick(0);
        TEST_ASSERT_EQUAL(int(ScreenId::Closest), int(app.host().target()));
        g.frames(30);
        g.assertNoStale();
        const std::vector<uint16_t> dal = closestBadge(g, t->flat);
        app.scene().flights[1].military = true;  // UAL53
        ++app.scene().version;
        app.sceneUpdated();
        app.tick(1000);
        TEST_ASSERT_TRUE(app.scene().alert.active);
        TEST_ASSERT_EQUAL_STRING("UAL53", app.scene().alert.flight.callsign);
        g.frames(30);
        TEST_ASSERT_EQUAL(int(ScreenId::Alert), int(app.host().current()));
        ++app.scene().version;
        app.tick(31000);  // timed out
        TEST_ASSERT_FALSE(app.scene().alert.active);
        g.frames(30);
        app.tick(32000);
        g.frames(30);
        TEST_ASSERT_EQUAL(int(ScreenId::Closest), int(app.host().current()));
        TEST_ASSERT_FALSE(app.host().transitioning());
        g.assertNoStale();
        TEST_ASSERT_TRUE_MESSAGE(closestBadge(g, t->flat) == dal, "Closest's logo not reloaded after the alert");
    }
}

// The border, rings and countdown move every frame: what is redrawn must match a full redraw, after
// every frame of a whole ring cycle (the rings damage only the annulus they cover), at a steady frame
// rate and a slow one that skips ring steps.
static void test_pulse_leaves_no_stale_pixels() {
    for (const scr::Theme* t : {&scr::cinematicSky(), &scr::editorial()})
        for (float dt : {1 / 30.f, 1 / 13.f}) {
            golden::Rig g(*t);
            emergency(g.app->scene());
            g.app->sceneUpdated();
            g.show(ScreenId::Alert);
            g.frames(1);
            for (int i = 0; i < 48; ++i) {
                g.r.frame(dt);
                g.assertNoStale();
            }
        }
}

// Quiet hours: the border and rings hold still, so frames only redraw the countdown.
static void test_soft_alert_redraws_only_the_countdown() {
    golden::Rig g;
    military(g.app->scene());
    g.app->scene().alert.soft = true;
    g.app->sceneUpdated();
    g.show(ScreenId::Alert);
    g.frames(3);
    for (int i = 0; i < 10; ++i) TEST_ASSERT_LESS_THAN(240 * 16, g.pushed());
}

// A new number from the feed redraws only its own field; a plane that has left redraws the screen, dimmed.
static void test_flight_changes_redraw() {
    for (const scr::Theme* t : {&scr::cinematicSky(), &scr::editorial()}) {
        golden::Rig g(*t);
        scr::Scene& s = g.app->scene();
        watchlist(s);
        s.alert.soft = true;  // the border and rings hold still: only the changes redraw
        g.app->sceneUpdated();
        g.show(ScreenId::Alert);
        g.frames(3);
        s.alert.flight.altFt += 1000;  // the stats
        g.app->sceneUpdated();
        TEST_ASSERT_LESS_THAN(240 * 48, g.pushed());
        g.assertNoStale();
        std::strcpy(s.alert.flight.callsign, "RCH472");  // the hero (and the sub, which may show the reg)
        g.app->sceneUpdated();
        TEST_ASSERT_LESS_THAN(240 * 80, g.pushed());
        g.assertNoStale();
        std::strcpy(s.alert.matched, "RCH*");  // the reason
        g.app->sceneUpdated();
        TEST_ASSERT_LESS_THAN(240 * 40, g.pushed());
        g.assertNoStale();
        s.alert.flight.trackDeg += 90;  // Cinematic: the plane; Editorial does not show it
        g.app->sceneUpdated();
        TEST_ASSERT_LESS_THAN(240 * (t->flat ? 16 : 120), g.pushed());
        g.assertNoStale();
        s.alert.flight.shape = uint8_t(data::Family::Helicopter);  // the target's shape, like its heading
        g.app->sceneUpdated();
        TEST_ASSERT_LESS_THAN(240 * (t->flat ? 16 : 120), g.pushed());
        g.assertNoStale();
        s.alert.live = false;
        g.app->sceneUpdated();
        TEST_ASSERT_GREATER_THAN(240 * 300, g.pushed());
        g.assertNoStale();
    }
}

// While an alert is up a tap ends it and does not reach the screen or start explore mode.
static void test_tap_dismisses() {
    golden::Rig g;
    scr::ScreenApp& app = *g.app;
    alerts::AlertConfig c;
    c.military = true;
    app.configureAlerts(c);
    scr::sceneQuiet(app.scene());
    app.scene().flights[0].military = true;
    ++app.scene().version;
    app.sceneUpdated();
    app.tick(1000);
    TEST_ASSERT_TRUE(app.scene().alert.active);
    TEST_ASSERT_EQUAL(int(ScreenId::Alert), int(app.host().target()));
    app.touch(true, 120, 160, 2000);  // two samples: a lone one is taken for panel noise
    app.touch(true, 120, 160, 2040);
    app.touch(false, 120, 160, 2080);
    TEST_ASSERT_FALSE(app.scene().alert.active);
    TEST_ASSERT_FALSE(app.director().exploring(2100));
    app.tick(2100);
    TEST_ASSERT_TRUE(app.host().target() != ScreenId::Alert);
}

// A tap with another alert queued goes straight to it, without fading back to the screen underneath.
static void test_tap_goes_straight_to_the_next_alert() {
    golden::Rig g;
    scr::ScreenApp& app = *g.app;
    alerts::AlertConfig c;
    c.military = true;
    app.configureAlerts(c);
    scr::sceneNoRoute(app.scene());
    for (int i = 0; i < app.scene().count; ++i) app.scene().flights[i].military = true;
    ++app.scene().version;
    app.sceneUpdated();
    app.tick(1000);
    const uint32_t first = app.scene().alert.flight.hex;
    app.touch(true, 120, 160, 2000);
    app.touch(true, 120, 160, 2040);
    app.touch(false, 120, 160, 2080);
    TEST_ASSERT_TRUE(app.scene().alert.active);
    TEST_ASSERT_TRUE(app.scene().alert.flight.hex != first);
    TEST_ASSERT_EQUAL_UINT32(2080, app.scene().alert.startMs);
    app.tick(2100);
    TEST_ASSERT_EQUAL(int(ScreenId::Alert), int(app.host().target()));
    TEST_ASSERT_FALSE(app.host().transitioning());
}

// An alert that times out fades back to the screen underneath; a tap in the fade is still the alert's.
static void test_tap_in_the_fade_out_is_swallowed() {
    golden::Rig g;
    scr::ScreenApp& app = *g.app;
    alerts::AlertConfig c;
    c.military = true;
    app.configureAlerts(c);
    scr::sceneQuiet(app.scene());
    app.scene().flights[0].military = true;
    ++app.scene().version;
    app.sceneUpdated();
    app.tick(1000);
    g.frames(30);  // the fade in is done
    ++app.scene().version;
    app.tick(31000);  // timed out
    TEST_ASSERT_FALSE(app.scene().alert.active);
    TEST_ASSERT_TRUE(app.host().transitioning());
    TEST_ASSERT_EQUAL(int(ScreenId::Alert), int(app.host().current()));
    app.touch(true, 120, 160, 31100);
    app.touch(true, 120, 160, 31140);
    app.touch(false, 120, 160, 31180);
    TEST_ASSERT_FALSE(app.director().exploring(31200));
    TEST_ASSERT_EQUAL_UINT32(0, app.host().selection());
}

static void test_lights_follow_the_alert() {
    scr::ScreenApp app;
    TEST_ASSERT_FALSE(app.alertLights(0).on);
    emergency(app.scene());
    alerts::LightOut o = app.alertLights(1500);  // 500 ms in: the top of the pulse
    TEST_ASSERT_TRUE(o.on);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1, o.backlight);
    TEST_ASSERT_EQUAL_UINT8(255, o.led.r);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_cinematic_goldens);
    RUN_TEST(test_editorial_goldens);
    RUN_TEST(test_logo_goldens);
    RUN_TEST(test_identity_changes_leave_no_stale_pixels);
    RUN_TEST(test_long_callsign_fits);
    RUN_TEST(test_same_airline_loads_the_logo_once);
    RUN_TEST(test_closest_reclaims_the_arena_after_an_alert);
    RUN_TEST(test_pulse_leaves_no_stale_pixels);
    RUN_TEST(test_soft_alert_redraws_only_the_countdown);
    RUN_TEST(test_flight_changes_redraw);
    RUN_TEST(test_tap_dismisses);
    RUN_TEST(test_tap_goes_straight_to_the_next_alert);
    RUN_TEST(test_tap_in_the_fade_out_is_swallowed);
    RUN_TEST(test_lights_follow_the_alert);
    return UNITY_END();
}
