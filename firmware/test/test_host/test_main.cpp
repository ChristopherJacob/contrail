#include <unity.h>

#include "screens/ScreenHost.h"

void setUp() {}
void tearDown() {}

namespace {

struct CountLayer : gfx::Layer {
    int draws = 0;
    void update(float, gfx::DamageList&) override {}
    void draw(gfx::Canvas&) override { ++draws; }
};

struct FakeScreen : scr::Screen {
    explicit FakeScreen(scr::ScreenId id) : id_(id) {}
    scr::ScreenId id() const override { return id_; }
    void setScene(const scr::Scene* s) override { scene = s; }
    void enter() override { ++enters; }
    scr::NavRequest onTap(int, int) override { ++taps; return {}; }
    void onVerticalSwipe(int d) override { swipes += d; }
    int layerCount() const override { return 1; }
    gfx::Layer* const* layers() override { return layers_; }
    scr::ScreenId id_;
    const scr::Scene* scene = nullptr;
    int enters = 0, taps = 0;
    int swipes = 0;
    CountLayer layer;
    gfx::Layer* layers_[1] = {&layer};
};

}  // namespace

static void test_transition_dips_through_the_veil() {
    FakeScreen a(scr::ScreenId::Radar), b(scr::ScreenId::Board);
    scr::Screen* list[] = {&a, &b};
    scr::ScreenHost host(list, 2, scr::cinematicSky());
    host.showNow(scr::ScreenId::Radar);
    TEST_ASSERT_EQUAL(int(scr::ScreenId::Radar), int(host.current()));
    gfx::DamageList d;
    host.update(0.01f, d);
    TEST_ASSERT_TRUE(d.all());  // first frame after a cut redraws everything
    d.clear();
    host.update(0.01f, d);
    TEST_ASSERT_FALSE(d.any());

    host.show(scr::ScreenId::Board);
    TEST_ASSERT_TRUE(host.transitioning());
    host.update(scr::ScreenHost::kFadeS / 2, d);
    TEST_ASSERT_TRUE(d.all());
    TEST_ASSERT_INT_WITHIN(20, 128, host.veilAlpha());
    TEST_ASSERT_EQUAL(int(scr::ScreenId::Radar), int(host.current()));
    host.update(scr::ScreenHost::kFadeS / 2 + 0.001f, d);
    TEST_ASSERT_EQUAL(int(scr::ScreenId::Board), int(host.current()));
    TEST_ASSERT_EQUAL(1, b.enters);
    host.update(scr::ScreenHost::kFadeS + 0.001f, d);
    TEST_ASSERT_FALSE(host.transitioning());
    TEST_ASSERT_EQUAL(0, host.veilAlpha());
}

static void test_turning_back_mid_fade_reverses() {
    FakeScreen a(scr::ScreenId::Radar), b(scr::ScreenId::Board);
    scr::Screen* list[] = {&a, &b};
    scr::ScreenHost host(list, 2, scr::cinematicSky());
    host.showNow(scr::ScreenId::Radar);
    gfx::DamageList d;
    host.show(scr::ScreenId::Board);
    host.update(scr::ScreenHost::kFadeS * 0.75f, d);
    uint8_t before = host.veilAlpha();
    host.show(scr::ScreenId::Radar);
    TEST_ASSERT_EQUAL(before, host.veilAlpha());  // no jump in brightness
    host.update(scr::ScreenHost::kFadeS, d);
    TEST_ASSERT_FALSE(host.transitioning());
    TEST_ASSERT_EQUAL(int(scr::ScreenId::Radar), int(host.current()));
    TEST_ASSERT_EQUAL(1, a.enters);  // never left, so never re-entered
}

static void test_taps_are_ignored_mid_transition() {
    FakeScreen a(scr::ScreenId::Radar), b(scr::ScreenId::Board);
    scr::Screen* list[] = {&a, &b};
    scr::ScreenHost host(list, 2, scr::cinematicSky());
    host.showNow(scr::ScreenId::Radar);
    host.tap(10, 10);
    TEST_ASSERT_EQUAL(1, a.taps);
    host.show(scr::ScreenId::Board);
    host.tap(10, 10);
    TEST_ASSERT_EQUAL(1, a.taps);
}

static void test_scene_reaches_the_new_screen() {
    FakeScreen a(scr::ScreenId::Radar), b(scr::ScreenId::Board);
    scr::Screen* list[] = {&a, &b};
    scr::ScreenHost host(list, 2, scr::cinematicSky());
    scr::Scene scene;
    host.setScene(&scene);
    host.showNow(scr::ScreenId::Board);
    TEST_ASSERT_EQUAL_PTR(&scene, b.scene);
}

static void test_vertical_swipes_reach_the_screen_when_idle() {
    FakeScreen a(scr::ScreenId::Radar), b(scr::ScreenId::Board);
    scr::Screen* list[] = {&a, &b};
    scr::ScreenHost host(list, 2, scr::cinematicSky());
    host.showNow(scr::ScreenId::Radar);
    host.verticalSwipe(-1);
    TEST_ASSERT_EQUAL(-1, a.swipes);
    host.show(scr::ScreenId::Board);
    host.verticalSwipe(1);  // mid-transition: ignored
    TEST_ASSERT_EQUAL(-1, a.swipes);
    TEST_ASSERT_EQUAL(0, b.swipes);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_transition_dips_through_the_veil);
    RUN_TEST(test_turning_back_mid_fade_reverses);
    RUN_TEST(test_taps_are_ignored_mid_transition);
    RUN_TEST(test_scene_reaches_the_new_screen);
    RUN_TEST(test_vertical_swipes_reach_the_screen_when_idle);
    return UNITY_END();
}
