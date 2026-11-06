#include <unity.h>

#include <memory>

#include "screens/Director.h"
#include "screens/ScreenApp.h"

using scr::Director;
using scr::ScreenId;

void setUp() {}
void tearDown() {}

static scr::Scene scene(int n, float dist = 8, int32_t alt = 30000, float elev = 20) {
    scr::Scene s;
    s.timeValid = s.locationSet = true;
    s.sunElev = 30, s.moonElev = -10;
    s.count = n;
    for (int i = 0; i < n; ++i) {
        s.flights[i].hex = uint32_t(i + 1);
        s.flights[i].distNm = dist + i, s.flights[i].altFt = alt, s.flights[i].elevDeg = elev;
    }
    return s;
}

// Runs the director like the app does: whatever it asks for is shown at once.
static ScreenId run(Director& d, const scr::Scene& s, ScreenId& cur, uint32_t now) {
    cur = d.decide(s, cur, now);
    return cur;
}

static void test_close_plane_shows_closest_after_dwell() {
    Director d;
    ScreenId cur = ScreenId::Radar;
    TEST_ASSERT_EQUAL(int(ScreenId::Radar), int(run(d, scene(3), cur, 0)));
    // A plane comes close 5 s later, but the radar has not had its 20 s yet.
    TEST_ASSERT_EQUAL(int(ScreenId::Radar), int(run(d, scene(3, 2, 8000), cur, 5000)));
    TEST_ASSERT_EQUAL(int(ScreenId::Closest), int(run(d, scene(3, 2, 8000), cur, 20001)));
}

static void test_hysteresis_keeps_closest_near_the_threshold() {
    Director d;
    ScreenId cur = ScreenId::Radar;
    run(d, scene(1, 2.5f, 8000), cur, 0);
    TEST_ASSERT_EQUAL(int(ScreenId::Closest), int(cur));
    // 3.3 nm is past the 3 nm entry threshold but inside the 3.6 nm release.
    TEST_ASSERT_EQUAL(int(ScreenId::Closest), int(run(d, scene(1, 3.3f, 8000), cur, 30000)));
    TEST_ASSERT_EQUAL(int(ScreenId::Radar), int(run(d, scene(1, 4.0f, 8000), cur, 60000)));
}

static void test_empty_sky_shows_sky_view() {
    Director d;
    ScreenId cur = ScreenId::Radar;
    TEST_ASSERT_EQUAL(int(ScreenId::Sky), int(run(d, scene(0), cur, 0)));
}

static void test_high_plane_at_night_shows_sky_view() {
    Director d;
    ScreenId cur = ScreenId::Radar;
    scr::Scene s = scene(2, 6, 30000, 50);
    s.sunElev = -20;
    TEST_ASSERT_EQUAL(int(ScreenId::Sky), int(run(d, s, cur, 0)));
    s.sunElev = 30;  // same plane in daylight: not "interesting"
    TEST_ASSERT_EQUAL(int(ScreenId::Radar), int(run(d, s, cur, 30000)));
}

static void test_planes_rotate_radar_map_board() {
    Director d;
    ScreenId cur = ScreenId::Radar;
    run(d, scene(5), cur, 0);
    TEST_ASSERT_EQUAL(int(ScreenId::Radar), int(run(d, scene(5), cur, 44000)));
    TEST_ASSERT_EQUAL(int(ScreenId::Map), int(run(d, scene(5), cur, 45000)));
    TEST_ASSERT_EQUAL(int(ScreenId::Board), int(run(d, scene(5), cur, 90000)));
    TEST_ASSERT_EQUAL(int(ScreenId::Radar), int(run(d, scene(5), cur, 135000)));
}

static void test_no_planes_alternates_sky_and_stats() {
    Director d;
    ScreenId cur = ScreenId::Radar;
    TEST_ASSERT_EQUAL(int(ScreenId::Sky), int(run(d, scene(0), cur, 0)));
    TEST_ASSERT_EQUAL(int(ScreenId::Sky), int(run(d, scene(0), cur, 44000)));
    TEST_ASSERT_EQUAL(int(ScreenId::Stats), int(run(d, scene(0), cur, 45000)));
    TEST_ASSERT_EQUAL(int(ScreenId::Sky), int(run(d, scene(0), cur, 90000)));
}

static scr::Scene sighted(int n, uint32_t seq) {
    scr::Scene s = scene(n);
    s.stats.firstSeq = seq;
    return s;
}

static void test_first_sighting_shows_stats_for_twenty_seconds() {
    Director d;
    ScreenId cur = ScreenId::Radar;
    run(d, sighted(3, 5), cur, 0);  // the count at boot is not news
    TEST_ASSERT_EQUAL(int(ScreenId::Radar), int(run(d, sighted(3, 5), cur, 25000)));
    TEST_ASSERT_EQUAL(int(ScreenId::Stats), int(run(d, sighted(3, 6), cur, 26000)));  // a new type
    TEST_ASSERT_EQUAL(int(ScreenId::Stats), int(run(d, sighted(3, 6), cur, 45000)));
    // The radar had its dwell before Stats interrupted it: the rotation moves on.
    TEST_ASSERT_EQUAL(int(ScreenId::Map), int(run(d, sighted(3, 6), cur, 46001)));
}

static void test_first_sighting_waits_for_the_dwell() {
    Director d;
    ScreenId cur = ScreenId::Radar;
    run(d, sighted(3, 1), cur, 0);
    TEST_ASSERT_EQUAL(int(ScreenId::Radar), int(run(d, sighted(3, 2), cur, 5000)));  // the radar's 20 s first
    TEST_ASSERT_EQUAL(int(ScreenId::Stats), int(run(d, sighted(3, 2), cur, 20000)));
    TEST_ASSERT_EQUAL(int(ScreenId::Stats), int(run(d, sighted(3, 2), cur, 39999)));  // 20 s from when it came up
    TEST_ASSERT_EQUAL(int(ScreenId::Map), int(run(d, sighted(3, 2), cur, 40000)));
}

static void test_one_sighting_takeover_per_five_minutes() {
    Director d;
    ScreenId cur = ScreenId::Radar;
    run(d, sighted(3, 1), cur, 0);
    TEST_ASSERT_EQUAL(int(ScreenId::Stats), int(run(d, sighted(3, 2), cur, 20000)));
    run(d, sighted(3, 2), cur, 40000);  // back to the rotation
    TEST_ASSERT_TRUE(run(d, sighted(3, 3), cur, 100000) != ScreenId::Stats);  // too soon after the last
    TEST_ASSERT_EQUAL(int(ScreenId::Stats), int(run(d, sighted(3, 4), cur, 341000)));
}

static void test_a_close_plane_beats_a_sighting() {
    Director d;
    ScreenId cur = ScreenId::Radar;
    run(d, sighted(3, 1), cur, 0);
    scr::Scene s = sighted(3, 2);
    s.flights[0].distNm = 2, s.flights[0].altFt = 8000;
    TEST_ASSERT_EQUAL(int(ScreenId::Closest), int(run(d, s, cur, 20000)));
}

// A close plane every minute must not pin the rotation on its first screen.
static void test_rotation_resumes_after_interruptions() {
    Director d;
    ScreenId cur = ScreenId::Radar;
    bool sawMap = false, sawBoard = false;
    for (uint32_t t = 0; t <= 180000; t += 1000) {
        bool close = t % 60000 >= 40000;
        ScreenId got = run(d, close ? scene(3, 2, 8000) : scene(3), cur, t);
        sawMap |= got == ScreenId::Map, sawBoard |= got == ScreenId::Board;
    }
    TEST_ASSERT_TRUE(sawMap);
    TEST_ASSERT_TRUE(sawBoard);
}

// An interruption before the dwell (only a user's swipe can cut a screen that short) resumes the
// same screen, with the time it already had.
static void test_short_interruption_resumes_the_same_screen() {
    Director d;
    ScreenId cur = ScreenId::Radar;
    run(d, scene(3), cur, 0);
    TEST_ASSERT_EQUAL(int(ScreenId::Map), int(run(d, scene(3), cur, 45000)));
    d.noteTouch(50000);
    cur = ScreenId::Closest;  // the user swiped away after 5 s of Map
    TEST_ASSERT_EQUAL(int(ScreenId::Closest), int(run(d, scene(3), cur, 50000)));
    TEST_ASSERT_EQUAL(int(ScreenId::Map), int(run(d, scene(3), cur, 110001)));
    TEST_ASSERT_EQUAL(int(ScreenId::Map), int(run(d, scene(3), cur, 150000)));  // 5 s + 39,999 ms
    TEST_ASSERT_EQUAL(int(ScreenId::Board), int(run(d, scene(3), cur, 150001)));
}

// The first rotation screen gets its full 45 s from when it actually came up.
static void test_rotation_clock_starts_when_shown() {
    Director d;
    ScreenId cur = ScreenId::Radar;
    run(d, scene(3, 2, 8000), cur, 0);  // Closest first
    TEST_ASSERT_EQUAL(int(ScreenId::Closest), int(run(d, scene(3), cur, 10000)));  // dwell holds it
    TEST_ASSERT_EQUAL(int(ScreenId::Radar), int(run(d, scene(3), cur, 20000)));
    TEST_ASSERT_EQUAL(int(ScreenId::Radar), int(run(d, scene(3), cur, 64999)));
    TEST_ASSERT_EQUAL(int(ScreenId::Map), int(run(d, scene(3), cur, 65000)));
}

// A plane flickering at the edge of range does not flip to the idle screens.
static void test_planes_must_be_gone_a_while_before_idle() {
    Director d;
    ScreenId cur = ScreenId::Radar;
    run(d, scene(1), cur, 0);
    TEST_ASSERT_EQUAL(int(ScreenId::Radar), int(run(d, scene(0), cur, 21000)));  // past the dwell, but too soon
    run(d, scene(1), cur, 25000);
    run(d, scene(0), cur, 26000);
    TEST_ASSERT_EQUAL(int(ScreenId::Radar), int(run(d, scene(0), cur, 40999)));
    TEST_ASSERT_EQUAL(int(ScreenId::Sky), int(run(d, scene(0), cur, 41000)));  // 15 s without planes
}

static scr::Scene closeSighted(uint32_t seq) {
    scr::Scene s = sighted(3, seq);
    s.flights[0].distNm = 2, s.flights[0].altFt = 8000;
    return s;
}

static void test_sighting_after_a_close_plane_releases() {
    Director d;
    ScreenId cur = ScreenId::Radar;
    run(d, sighted(3, 1), cur, 0);
    TEST_ASSERT_EQUAL(int(ScreenId::Closest), int(run(d, closeSighted(2), cur, 20000)));
    TEST_ASSERT_EQUAL(int(ScreenId::Stats), int(run(d, sighted(3, 2), cur, 40000)));  // within the 60 s
}

static void test_stale_sighting_is_dropped() {
    Director d;
    ScreenId cur = ScreenId::Radar;
    run(d, sighted(3, 1), cur, 0);
    TEST_ASSERT_EQUAL(int(ScreenId::Closest), int(run(d, closeSighted(2), cur, 20000)));
    for (uint32_t t = 21000; t <= 80000; t += 1000) run(d, closeSighted(2), cur, t);
    TEST_ASSERT_EQUAL(int(ScreenId::Map), int(run(d, sighted(3, 2), cur, 81000)));  // not Stats: too late
    // Dropped, not shown: the next sighting is not held back five minutes.
    TEST_ASSERT_EQUAL(int(ScreenId::Stats), int(run(d, sighted(3, 3), cur, 101000)));
}

static void test_sighting_while_exploring_on_stats_counts_as_shown() {
    Director d;
    ScreenId cur = ScreenId::Radar;
    run(d, sighted(3, 1), cur, 0);
    d.noteTouch(5000);
    cur = ScreenId::Stats;  // the user swiped to Stats
    for (uint32_t t = 10000; t <= 64000; t += 1000) run(d, sighted(3, 2), cur, t);
    TEST_ASSERT_EQUAL(int(ScreenId::Stats), int(cur));
    // Explore ends at 65000; the sighting was seen, so the rotation takes over.
    TEST_ASSERT_EQUAL(int(ScreenId::Radar), int(run(d, sighted(3, 2), cur, 65001)));
    // And it counted as a takeover: another within five minutes is held back.
    TEST_ASSERT_TRUE(run(d, sighted(3, 3), cur, 90000) != ScreenId::Stats);
}

static void test_sighting_while_exploring_elsewhere_shows_after() {
    Director d;
    ScreenId cur = ScreenId::Radar;
    run(d, sighted(3, 1), cur, 0);
    d.noteTouch(5000);
    cur = ScreenId::Board;
    TEST_ASSERT_EQUAL(int(ScreenId::Board), int(run(d, sighted(3, 2), cur, 30000)));
    TEST_ASSERT_EQUAL(int(ScreenId::Stats), int(run(d, sighted(3, 2), cur, 65001)));  // 35 s after it arrived
}

// With no planes, the idle rotation's Stats is the takeover.
static void test_sighting_with_no_planes_uses_idle_stats() {
    Director d;
    ScreenId cur = ScreenId::Radar;
    run(d, sighted(0, 1), cur, 0);
    TEST_ASSERT_EQUAL(int(ScreenId::Sky), int(run(d, sighted(0, 2), cur, 10000)));
    TEST_ASSERT_EQUAL(int(ScreenId::Stats), int(run(d, sighted(0, 2), cur, 45000)));
    for (uint32_t t = 46000; t <= 70000; t += 1000) run(d, sighted(3, 2), cur, t);  // planes again
    TEST_ASSERT_EQUAL(int(ScreenId::Radar), int(cur));
    TEST_ASSERT_TRUE(run(d, sighted(3, 3), cur, 100000) != ScreenId::Stats);  // the gap applies
}

static void test_rotation_across_the_millis_wrap() {
    Director d;
    ScreenId cur = ScreenId::Radar;
    const uint32_t t0 = 0xFFFFFFFFu - 20000;
    run(d, scene(3), cur, t0);
    TEST_ASSERT_EQUAL(int(ScreenId::Radar), int(run(d, scene(3), cur, t0 + 44999)));
    TEST_ASSERT_EQUAL(int(ScreenId::Map), int(run(d, scene(3), cur, t0 + 45000)));
    TEST_ASSERT_EQUAL(int(ScreenId::Board), int(run(d, scene(3), cur, t0 + 90000)));
    TEST_ASSERT_EQUAL(int(ScreenId::Radar), int(run(d, scene(3), cur, t0 + 135000)));
}

static void test_touch_holds_the_screen_for_a_minute() {
    Director d;
    ScreenId cur = ScreenId::Radar;
    run(d, scene(5), cur, 0);
    d.noteTouch(30000);
    cur = ScreenId::Board;  // the user swiped
    TEST_ASSERT_EQUAL(int(ScreenId::Board), int(run(d, scene(1, 1, 5000), cur, 60000)));
    TEST_ASSERT_TRUE(d.exploring(60000));
    // Explore mode ends at 90000; the dwell clock dates from the director's own switch at 0.
    TEST_ASSERT_FALSE(d.exploring(90000));
    TEST_ASSERT_EQUAL(int(ScreenId::Closest), int(run(d, scene(1, 1, 5000), cur, 90001)));
}

static void test_no_location_shows_radar() {
    Director d;
    ScreenId cur = ScreenId::Sky;
    scr::Scene s = scene(0);
    s.locationSet = false;
    TEST_ASSERT_EQUAL(int(ScreenId::Radar), int(run(d, s, cur, 0)));
}

static void test_carousel_and_stay() {
    Director d;
    d.configure(scr::DirectorMode::Carousel, 30000);
    ScreenId cur = ScreenId::Closest;
    TEST_ASSERT_EQUAL(int(ScreenId::Closest), int(run(d, scene(3), cur, 0)));
    TEST_ASSERT_EQUAL(int(ScreenId::Radar), int(run(d, scene(3), cur, 30000)));
    TEST_ASSERT_EQUAL(int(ScreenId::Map), int(run(d, scene(3), cur, 60000)));
    Director stay;
    stay.configure(scr::DirectorMode::Stay, 30000);
    cur = ScreenId::Board;
    TEST_ASSERT_EQUAL(int(ScreenId::Board), int(run(stay, scene(1, 1, 5000), cur, 100000)));
}

static void test_parse_director_mode() {
    TEST_ASSERT_EQUAL(int(scr::DirectorMode::Carousel), int(scr::parseDirectorMode("carousel")));
    TEST_ASSERT_EQUAL(int(scr::DirectorMode::Stay), int(scr::parseDirectorMode("stay")));
    TEST_ASSERT_EQUAL(int(scr::DirectorMode::Auto), int(scr::parseDirectorMode("auto")));
    TEST_ASSERT_EQUAL(int(scr::DirectorMode::Auto), int(scr::parseDirectorMode("bogus")));
    TEST_ASSERT_EQUAL(int(scr::DirectorMode::Auto), int(scr::parseDirectorMode(nullptr)));
}

// --- ScreenApp wiring ------------------------------------------------------------------

// A horizontal swipe from (x0, y) to (x1, y), sampled every 20 ms, released at t0 + 260.
static void swipe(scr::ScreenApp& app, int x0, int x1, int y, uint32_t t0) {
    for (uint32_t t = 0; t <= 240; t += 20) app.touch(true, x0 + (x1 - x0) * int(t) / 240, y, t0 + t);
    app.touch(false, 0, 0, t0 + 260);
}

// The swipe starts a fade; the director must follow where the host is heading, not the
// screen it is still fading out of, or its next tick would turn the fade back.
static void test_swipe_mid_fade_is_not_undone() {
    auto app = std::make_unique<scr::ScreenApp>();
    app->scene() = scene(5);
    app->sceneUpdated();
    app->tick(0);
    TEST_ASSERT_EQUAL(int(ScreenId::Radar), int(app->host().current()));
    swipe(*app, 200, 60, 160, 1000);  // left: Radar -> Map
    TEST_ASSERT_EQUAL(int(ScreenId::Map), int(app->host().target()));
    TEST_ASSERT_EQUAL(int(ScreenId::Radar), int(app->host().current()));  // still fading out
    app->tick(1300);
    TEST_ASSERT_EQUAL(int(ScreenId::Map), int(app->host().target()));
    TEST_ASSERT_TRUE(app->director().exploring(1300));
    // A minute after the swipe the director takes over again (planes in range: Radar first).
    app->tick(1300 + Director::kExploreMs);
    TEST_ASSERT_FALSE(app->director().exploring(1300 + Director::kExploreMs));
}

static void test_first_tick_cuts_to_the_directors_choice() {
    auto app = std::make_unique<scr::ScreenApp>();
    app->scene() = scene(0);  // empty sky
    app->sceneUpdated();
    app->tick(0);
    TEST_ASSERT_EQUAL(int(ScreenId::Sky), int(app->host().current()));
    TEST_ASSERT_FALSE(app->host().transitioning());
}

// A tap's selection lasts for explore mode, then every screen falls back to its default.
static void test_selection_expires_with_explore_mode() {
    auto app = std::make_unique<scr::ScreenApp>();
    app->scene() = scene(3, 2, 8000);  // a close, low plane: Closest Flight
    app->sceneUpdated();
    app->tick(0);
    TEST_ASSERT_EQUAL(int(ScreenId::Closest), int(app->host().current()));
    // Tap the card: select the next plane.
    for (uint32_t t = 0; t <= 60; t += 20) app->touch(true, 120, 250, 1000 + t);
    app->touch(false, 0, 0, 1080);
    TEST_ASSERT_EQUAL_UINT32(2, app->host().selection());
    app->tick(1100);
    app->tick(1080 + Director::kExploreMs - 100);
    TEST_ASSERT_EQUAL_UINT32(2, app->host().selection());
    app->tick(1080 + Director::kExploreMs + 100);
    TEST_ASSERT_EQUAL_UINT32(0, app->host().selection());
}

// Explore mode stops its clock while an alert is up, so the selection outlasts it.
static void test_selection_survives_an_alert() {
    auto app = std::make_unique<scr::ScreenApp>();
    app->scene() = scene(3, 2, 8000);
    app->sceneUpdated();
    app->tick(0);
    for (uint32_t t = 0; t <= 60; t += 20) app->touch(true, 120, 250, 1000 + t);
    app->touch(false, 0, 0, 1080);
    TEST_ASSERT_EQUAL_UINT32(2, app->host().selection());
    app->tick(1100);
    scr::SceneAlert& a = app->scene().alert;
    a.active = true, a.startMs = 50000, a.endsMs = 80000;
    app->tick(50000);
    TEST_ASSERT_EQUAL(int(ScreenId::Alert), int(app->host().target()));
    app->tick(70000);  // a minute after the tap, but 49 s of it explored
    TEST_ASSERT_EQUAL_UINT32(2, app->host().selection());
    a.active = false;
    app->tick(75000);  // 11 s of explore mode left
    TEST_ASSERT_TRUE(app->director().exploring(75000));
    TEST_ASSERT_EQUAL_UINT32(2, app->host().selection());
    app->tick(85000);
    TEST_ASSERT_EQUAL_UINT32(2, app->host().selection());
    app->tick(87000);
    TEST_ASSERT_EQUAL_UINT32(0, app->host().selection());
}

// An alert outranks every rule in every mode, and the screen it interrupted comes back (alerts spec §7).
static void test_alert_interrupts_every_mode_and_the_screen_comes_back() {
    const scr::DirectorMode modes[] = {scr::DirectorMode::Auto, scr::DirectorMode::Carousel,
                                       scr::DirectorMode::Stay};
    for (scr::DirectorMode mode : modes) {
        Director d;
        d.configure(mode, 30000);
        ScreenId cur = ScreenId::Board;
        scr::Scene s = scene(3);
        run(d, s, cur, 0);  // Auto: Radar (the busy rotation); Carousel and Stay: Board
        const ScreenId before = cur;
        s.alert.active = true;
        TEST_ASSERT_EQUAL(int(ScreenId::Alert), int(run(d, s, cur, 1000)));
        TEST_ASSERT_EQUAL(int(ScreenId::Alert), int(run(d, s, cur, 20000)));
        s.alert.active = false;
        TEST_ASSERT_EQUAL(int(before), int(run(d, s, cur, 31000)));
        // The 30 s of alert did not count against the carousel's interval or a rotation's 45 s.
        TEST_ASSERT_EQUAL(int(before), int(run(d, s, cur, 40000)));
    }
}

static void test_alert_outranks_a_close_plane() {
    Director d;
    ScreenId cur = ScreenId::Radar;
    scr::Scene s = scene(1, 2, 8000);
    TEST_ASSERT_EQUAL(int(ScreenId::Closest), int(run(d, s, cur, 0)));
    s.alert.active = true;
    TEST_ASSERT_EQUAL(int(ScreenId::Alert), int(run(d, s, cur, 1000)));  // no dwell for an alert
    s.alert.active = false;
    TEST_ASSERT_EQUAL(int(ScreenId::Closest), int(run(d, s, cur, 31000)));
}

static void test_alert_pauses_explore_mode() {
    Director d;
    ScreenId cur = ScreenId::Radar;
    scr::Scene s = scene(3);
    run(d, s, cur, 0);
    d.noteTouch(1000);
    cur = ScreenId::Stats;  // the user swiped to Stats
    run(d, s, cur, 1100);
    s.alert.active = true;
    TEST_ASSERT_EQUAL(int(ScreenId::Alert), int(run(d, s, cur, 5000)));
    s.alert.active = false;
    TEST_ASSERT_EQUAL(int(ScreenId::Stats), int(run(d, s, cur, 50000)));
    // 4 s of exploring before the alert, 56 s after it: until 106 s.
    TEST_ASSERT_TRUE(d.exploring(100000));
    TEST_ASSERT_FALSE(d.exploring(107000));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_close_plane_shows_closest_after_dwell);
    RUN_TEST(test_hysteresis_keeps_closest_near_the_threshold);
    RUN_TEST(test_empty_sky_shows_sky_view);
    RUN_TEST(test_high_plane_at_night_shows_sky_view);
    RUN_TEST(test_planes_rotate_radar_map_board);
    RUN_TEST(test_no_planes_alternates_sky_and_stats);
    RUN_TEST(test_first_sighting_shows_stats_for_twenty_seconds);
    RUN_TEST(test_first_sighting_waits_for_the_dwell);
    RUN_TEST(test_one_sighting_takeover_per_five_minutes);
    RUN_TEST(test_a_close_plane_beats_a_sighting);
    RUN_TEST(test_rotation_resumes_after_interruptions);
    RUN_TEST(test_short_interruption_resumes_the_same_screen);
    RUN_TEST(test_rotation_clock_starts_when_shown);
    RUN_TEST(test_planes_must_be_gone_a_while_before_idle);
    RUN_TEST(test_sighting_after_a_close_plane_releases);
    RUN_TEST(test_stale_sighting_is_dropped);
    RUN_TEST(test_sighting_while_exploring_on_stats_counts_as_shown);
    RUN_TEST(test_sighting_while_exploring_elsewhere_shows_after);
    RUN_TEST(test_sighting_with_no_planes_uses_idle_stats);
    RUN_TEST(test_rotation_across_the_millis_wrap);
    RUN_TEST(test_touch_holds_the_screen_for_a_minute);
    RUN_TEST(test_no_location_shows_radar);
    RUN_TEST(test_carousel_and_stay);
    RUN_TEST(test_parse_director_mode);
    RUN_TEST(test_swipe_mid_fade_is_not_undone);
    RUN_TEST(test_first_tick_cuts_to_the_directors_choice);
    RUN_TEST(test_selection_expires_with_explore_mode);
    RUN_TEST(test_selection_survives_an_alert);
    RUN_TEST(test_alert_interrupts_every_mode_and_the_screen_comes_back);
    RUN_TEST(test_alert_outranks_a_close_plane);
    RUN_TEST(test_alert_pauses_explore_mode);
    return UNITY_END();
}
