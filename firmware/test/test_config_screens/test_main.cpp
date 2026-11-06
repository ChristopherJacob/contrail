#include <unity.h>

#include <cstdlib>
#include <ctime>

#include "GoldenRender.h"
#include "screens/DemoScenes.h"

using scr::ScreenId;

void setUp() {
    setenv("TZ", "UTC0", 1);
    tzset();
}
void tearDown() {}

static void panelGolden(const scr::Theme& t, bool editorial, const char* name) {
    golden::Rig g(t);
    scr::sceneQuiet(g.app->scene());
    g.app->sceneUpdated();
    scr::PanelValues v;
    v.editorial = editorial;
    g.app->setPanelValues(v);
    g.show(ScreenId::Settings);
    g.frames(10);
    assertGolden(name, g.d.px, 240, 320);
    g.assertNoStale();
}

static void test_panel_goldens() {
    panelGolden(scr::cinematicSky(), false, "settings_panel");
    panelGolden(scr::editorial(), true, "ed_settings_panel");
}

static void updateGolden(const scr::Theme& t, const char* name) {
    golden::Rig g(t);
    scr::sceneQuiet(g.app->scene());
    g.app->sceneUpdated();
    g.app->setUpdateProgress(62);
    g.app->tick(1000);
    g.frames(30);  // through the fade
    TEST_ASSERT_EQUAL(int(ScreenId::Update), int(g.app->host().current()));
    assertGolden(name, g.d.px, 240, 320);
    g.assertNoStale();
}

static void test_update_goldens() {
    updateGolden(scr::cinematicSky(), "update_screen");
    updateGolden(scr::editorial(), "ed_update_screen");
}

// Only the percentage and bar redraw as the upload climbs; ending it returns to the screen before.
static void test_update_progress_redraws_little_and_returns() {
    golden::Rig g;
    scr::sceneQuiet(g.app->scene());
    g.app->sceneUpdated();
    g.show(ScreenId::Board);
    g.app->configure(scr::DirectorMode::Stay, 30000);
    g.app->tick(1000);
    g.app->setUpdateProgress(10);
    g.app->tick(1033);
    g.frames(30);
    g.app->setUpdateProgress(11);
    TEST_ASSERT_LESS_OR_EQUAL(240 * 80, g.pushed());
    g.app->touch(true, 120, 160, 2000);  // ignored
    g.app->setUpdateProgress(-1);
    g.app->tick(2100);
    g.frames(30);
    TEST_ASSERT_EQUAL(int(ScreenId::Board), int(g.app->host().current()));
}

#include <cstring>

// Drives the app the way main_esp32's loop does, 33 ms per frame.
struct Drive {
    golden::Rig g;
    uint32_t t = 1000;
    explicit Drive(const scr::Theme& theme = scr::cinematicSky()) : g(theme) {
        scr::sceneQuiet(g.app->scene());
        g.app->sceneUpdated();
        g.app->configure(scr::DirectorMode::Stay, 30000);
        g.show(ScreenId::Board);
        frame(false);
    }
    void frame(bool pressed, int x = 0, int y = 0) {
        t += 33;
        g.app->touch(pressed, x, y, t);
        g.app->tick(t);
        g.r.frame(0.033f);
    }
    void idle(uint32_t ms) {
        for (uint32_t e = 0; e < ms; e += 33) frame(false);
    }
    void hold(int x, int y, uint32_t ms) {
        for (uint32_t e = 0; e < ms; e += 33) frame(true, x, y);
    }
    void tap(int x, int y) {
        hold(x, y, 66);
        frame(false);
    }
    void openPanel() {
        hold(120, 200, 1100);
        frame(false);
        idle(500);  // the fade
    }
    ScreenId current() const { return g.app->host().current(); }
};

static int centreX(const gfx::Rect& r) { return r.x + r.w / 2; }
static int centreY(const gfx::Rect& r) { return r.y + r.h / 2; }

static void test_hold_ring_golden() {
    Drive d;
    d.hold(120, 160, 600);
    TEST_ASSERT_TRUE(d.g.app->holdRing().visible());
    assertGolden("hold_ring", d.g.d.px, 240, 320);
    d.g.assertNoStale();
}

static void test_hold_opens_the_panel_and_done_returns() {
    Drive d;
    d.openPanel();
    TEST_ASSERT_TRUE(d.g.app->panelOpen());
    TEST_ASSERT_EQUAL(int(ScreenId::Settings), int(d.current()));
    TEST_ASSERT_FALSE(d.g.app->holdRing().visible());
    d.tap(centreX(scr::SettingsView::kDone), centreY(scr::SettingsView::kDone));
    d.idle(500);
    TEST_ASSERT_FALSE(d.g.app->panelOpen());
    TEST_ASSERT_EQUAL(int(ScreenId::Board), int(d.current()));
}

static void test_taps_queue_edits() {
    Drive d;
    d.openPanel();
    d.tap(centreX(scr::SettingsView::kTheme[1]), centreY(scr::SettingsView::kTheme[1]));
    d.tap(centreX(scr::SettingsView::kPlus), centreY(scr::SettingsView::kPlus));
    d.tap(centreX(scr::SettingsView::kMode[2]), centreY(scr::SettingsView::kMode[2]));
    scr::SettingEdit e;
    TEST_ASSERT_TRUE(d.g.app->takeSettingEdit(e));
    TEST_ASSERT_EQUAL_STRING("theme", e.key);
    TEST_ASSERT_EQUAL_STRING("editorial", e.value);
    TEST_ASSERT_TRUE(d.g.app->takeSettingEdit(e));
    TEST_ASSERT_EQUAL_STRING("radius_nm", e.key);
    TEST_ASSERT_EQUAL_STRING("30", e.value);
    TEST_ASSERT_TRUE(d.g.app->takeSettingEdit(e));
    TEST_ASSERT_EQUAL_STRING("screen_mode", e.key);
    TEST_ASSERT_EQUAL_STRING("stay", e.value);
    TEST_ASSERT_FALSE(d.g.app->takeSettingEdit(e));
    d.g.assertNoStale();
}

static void test_radius_steps_and_clamps() {
    Drive d;
    scr::PanelValues v;
    v.radiusNm = 7;
    d.g.app->setPanelValues(v);
    d.openPanel();
    const gfx::Rect& m = scr::SettingsView::kMinus;
    d.tap(centreX(m), centreY(m));  // 7 -> 5
    d.tap(centreX(m), centreY(m));  // stays 5
    scr::SettingEdit e;
    TEST_ASSERT_TRUE(d.g.app->takeSettingEdit(e));
    TEST_ASSERT_EQUAL_STRING("5", e.value);  // one queued entry per key, the newest value
    TEST_ASSERT_FALSE(d.g.app->takeSettingEdit(e));
}

static void test_slider_previews_then_saves_on_release() {
    Drive d;
    d.openPanel();
    const int y = scr::SettingsView::kSliderY;
    d.hold(60, y, 99);
    for (int x = 60; x <= 200; x += 20) d.frame(true, x, y);
    TEST_ASSERT_EQUAL(scr::SettingsView::brightnessAt(200), d.g.app->previewBrightness());
    scr::SettingEdit e;
    TEST_ASSERT_FALSE(d.g.app->takeSettingEdit(e));  // nothing saved while dragging
    d.frame(false);
    TEST_ASSERT_EQUAL(-1, d.g.app->previewBrightness());
    TEST_ASSERT_TRUE(d.g.app->takeSettingEdit(e));
    TEST_ASSERT_EQUAL_STRING("bright_mode", e.key);
    TEST_ASSERT_EQUAL_STRING("manual", e.value);
    TEST_ASSERT_TRUE(d.g.app->takeSettingEdit(e));
    TEST_ASSERT_EQUAL_STRING("brightness", e.key);
    char want[12];
    std::snprintf(want, sizeof want, "%d", scr::SettingsView::brightnessAt(200));
    TEST_ASSERT_EQUAL_STRING(want, e.value);
    d.g.assertNoStale();
}

static void test_the_opening_hold_does_not_grab_the_slider() {
    Drive d;
    d.hold(120, scr::SettingsView::kSliderY, 1100);  // opens over where the slider will be
    d.idle(500);
    TEST_ASSERT_TRUE(d.g.app->panelOpen());
    scr::SettingEdit e;
    TEST_ASSERT_FALSE(d.g.app->takeSettingEdit(e));
}

static void test_panel_closes_after_30_s_idle() {
    Drive d;
    d.openPanel();
    d.idle(29000);
    TEST_ASSERT_TRUE(d.g.app->panelOpen());
    d.idle(1500);
    TEST_ASSERT_FALSE(d.g.app->panelOpen());
}

static void test_emergency_closes_the_panel() {
    Drive d;
    d.openPanel();
    scr::SceneAlert& a = d.g.app->scene().alert;
    a.active = true, a.kind = scr::AlertKind::Emergency, a.live = true;
    a.flight = d.g.app->scene().flights[0];
    a.startMs = d.t, a.endsMs = d.t + 30000;
    d.idle(500);
    TEST_ASSERT_FALSE(d.g.app->panelOpen());
    TEST_ASSERT_EQUAL(int(ScreenId::Alert), int(d.current()));
}

static void test_other_alerts_wait_for_the_panel() {
    Drive d;
    d.openPanel();
    scr::SceneAlert& a = d.g.app->scene().alert;
    a.active = true, a.kind = scr::AlertKind::Military, a.live = true;
    a.flight = d.g.app->scene().flights[0];
    a.startMs = d.t, a.endsMs = d.t + 30000;
    d.idle(500);
    TEST_ASSERT_TRUE(d.g.app->panelOpen());
    d.tap(centreX(scr::SettingsView::kDone), centreY(scr::SettingsView::kDone));
    d.idle(500);
    TEST_ASSERT_EQUAL(int(ScreenId::Alert), int(d.current()));
}

static void test_upload_replaces_the_panel() {
    Drive d;
    d.openPanel();
    d.g.app->setUpdateProgress(5);
    d.idle(500);
    TEST_ASSERT_FALSE(d.g.app->panelOpen());
    TEST_ASSERT_EQUAL(int(ScreenId::Update), int(d.current()));
    d.g.app->setUpdateProgress(-1);
    d.idle(500);
    TEST_ASSERT_EQUAL(int(ScreenId::Board), int(d.current()));
}

static void test_waiting_alert_does_not_light_the_panel() {
    Drive d;
    d.openPanel();
    scr::SceneAlert& a = d.g.app->scene().alert;
    a.active = true, a.kind = scr::AlertKind::Military, a.live = true;
    a.flight = d.g.app->scene().flights[0];
    a.startMs = d.t, a.endsMs = d.t + 30000;
    d.idle(500);
    TEST_ASSERT_TRUE(d.g.app->panelOpen());
    const uint32_t at = d.t + 250;  // a quarter into a cycle, where a pulsing alert is lit
    const alerts::LightOut off = d.g.app->alertLights(at);
    TEST_ASSERT_FALSE(off.on);
    TEST_ASSERT_EQUAL_FLOAT(1, off.backlight);
    TEST_ASSERT_EQUAL_FLOAT(0, off.border);
    d.tap(centreX(scr::SettingsView::kDone), centreY(scr::SettingsView::kDone));
    d.idle(500);
    TEST_ASSERT_TRUE(d.g.app->alertLights(d.t).on);
}

static void test_emergency_mid_drag_does_not_strand_the_slider() {
    Drive d;
    d.openPanel();
    const int y = scr::SettingsView::kSliderY;
    d.hold(60, y, 99);
    for (int x = 60; x <= 200; x += 20) d.frame(true, x, y);
    TEST_ASSERT_GREATER_THAN(-1, d.g.app->previewBrightness());
    scr::SceneAlert& a = d.g.app->scene().alert;
    a.active = true, a.kind = scr::AlertKind::Emergency, a.live = true;
    a.flight = d.g.app->scene().flights[0];
    a.startMs = d.t, a.endsMs = d.t + 30000;
    d.frame(true, 200, y);
    TEST_ASSERT_FALSE(d.g.app->panelOpen());
    d.frame(false);
    scr::PanelValues v;
    v.radiusNm = 40;
    d.g.app->setPanelValues(v);
    a.active = false;
    d.idle(1000);
    d.openPanel();
    TEST_ASSERT_TRUE(d.g.app->panelOpen());
    TEST_ASSERT_EQUAL(40, d.g.app->panelValues().radiusNm);
    TEST_ASSERT_EQUAL(v.brightness, d.g.app->panelValues().brightness);
    TEST_ASSERT_EQUAL(-1, d.g.app->previewBrightness());
    scr::SettingEdit e;
    TEST_ASSERT_FALSE(d.g.app->takeSettingEdit(e));  // the cut-off drag saved nothing
}

static void test_director_switch_waits_for_a_hold() {
    Drive d;
    d.g.app->configure(scr::DirectorMode::Carousel, 1500);
    d.idle(1000);
    const ScreenId first = d.g.app->host().target();
    int guard = 0;
    while (d.g.app->host().target() == first && ++guard < 3000) d.frame(false);  // until a switch
    const ScreenId start = d.g.app->host().target();
    TEST_ASSERT_NOT_EQUAL(int(first), int(start));
    d.idle(500);
    d.hold(120, 200, 1100);  // the next switch falls inside this hold
    d.frame(false);
    d.idle(500);
    TEST_ASSERT_TRUE(d.g.app->panelOpen());
    d.tap(centreX(scr::SettingsView::kDone), centreY(scr::SettingsView::kDone));
    d.idle(500);
    TEST_ASSERT_EQUAL(int(start), int(d.current()));
}

static void test_slow_tap_on_done_closes_the_panel() {
    Drive d;
    d.openPanel();
    d.hold(centreX(scr::SettingsView::kDone), centreY(scr::SettingsView::kDone), 450);
    d.frame(false);
    d.idle(500);
    TEST_ASSERT_FALSE(d.g.app->panelOpen());
}

static void test_slow_tap_dismisses_an_alert() {
    Drive d;
    scr::SceneAlert& a = d.g.app->scene().alert;
    a.active = true, a.kind = scr::AlertKind::Military, a.live = true;
    a.flight = d.g.app->scene().flights[0];
    a.startMs = d.t, a.endsMs = d.t + 30000;
    d.idle(500);
    TEST_ASSERT_EQUAL(int(ScreenId::Alert), int(d.current()));
    d.hold(120, 160, 450);
    d.frame(false);
    TEST_ASSERT_FALSE(d.g.app->scene().alert.active);
}

static void test_editorial_hold_hint_leaves_nothing_stale() {
    Drive d(scr::editorial());
    d.g.assertNoStale();
    for (int i = 0; i < 40; ++i) {  // through the ring's fill, at the screen's middle and near the bottom
        d.frame(true, 120, i < 20 ? 160 : 300);
        if (i % 4 == 0) d.g.assertNoStale();
    }
    d.frame(false);
    d.g.assertNoStale();
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_panel_goldens);
    RUN_TEST(test_update_goldens);
    RUN_TEST(test_update_progress_redraws_little_and_returns);
    RUN_TEST(test_hold_ring_golden);
    RUN_TEST(test_hold_opens_the_panel_and_done_returns);
    RUN_TEST(test_taps_queue_edits);
    RUN_TEST(test_radius_steps_and_clamps);
    RUN_TEST(test_slider_previews_then_saves_on_release);
    RUN_TEST(test_the_opening_hold_does_not_grab_the_slider);
    RUN_TEST(test_panel_closes_after_30_s_idle);
    RUN_TEST(test_emergency_closes_the_panel);
    RUN_TEST(test_other_alerts_wait_for_the_panel);
    RUN_TEST(test_upload_replaces_the_panel);
    RUN_TEST(test_waiting_alert_does_not_light_the_panel);
    RUN_TEST(test_emergency_mid_drag_does_not_strand_the_slider);
    RUN_TEST(test_director_switch_waits_for_a_hold);
    RUN_TEST(test_slow_tap_on_done_closes_the_panel);
    RUN_TEST(test_slow_tap_dismisses_an_alert);
    RUN_TEST(test_editorial_hold_hint_leaves_nothing_stale);
    return UNITY_END();
}
