// The radar damages only the band-by-band extent of its sweep, so check over a whole
// turn (at a frame rate that does not divide it evenly) that every incrementally drawn
// frame matches a full redraw of the same moment.
#include <unity.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#include "FrameCapture.h"
#include "data/Shapes.h"
#include "gfx/StripRenderer.h"
#include "screens/DemoScenes.h"
#include "screens/ScreenApp.h"

void setUp() {
    setenv("TZ", "UTC0", 1);
    tzset();
}
void tearDown() {}

static uint16_t bufA[240 * 32], bufB[240 * 32];

static void sweepMatchesFullRedraw(void (*fill)(scr::Scene&), float dt, int stripH = 32,
                                   const scr::Theme& theme = scr::cinematicSky()) {
    auto app = std::make_unique<scr::ScreenApp>(theme);
    fill(app->scene());
    app->sceneUpdated();
    app->host().showNow(scr::ScreenId::Radar);
    FrameCapture d;
    gfx::StripRenderer r(d, bufA, bufB, stripH);
    r.setLayers(app->layers(), app->layerCount());
    r.frame(dt);
    int frames = int(4.2f / dt);  // a little over one 4 s turn
    size_t pushed = 0;
    for (int i = 0; i < frames; ++i) {
        d.pushedRects.clear();
        r.frame(dt);
        for (const auto& p : d.pushedRects) pushed += size_t(p.w) * p.h;
        std::vector<uint16_t> incremental = d.px;
        r.invalidateAll();
        r.frame(0);
        int stale = 0;
        for (size_t k = 0; k < incremental.size(); ++k) stale += incremental[k] != d.px[k];
        if (stale) {
            char msg[64];
            std::snprintf(msg, sizeof msg, "frame %d: %d pixels left stale", i, stale);
            TEST_FAIL_MESSAGE(msg);
        }
    }
    // The sweep alone should redraw well under a third of the screen per frame.
    TEST_ASSERT_LESS_THAN(240 * 320 / 3, int(pushed / size_t(frames)));
}

static void test_busy_sweep_30fps() { sweepMatchesFullRedraw(scr::sceneBusy, 1 / 30.f); }
static void test_busy_sweep_23fps() { sweepMatchesFullRedraw(scr::sceneBusy, 1 / 23.f); }
static void test_empty_sweep_17fps() { sweepMatchesFullRedraw(scr::sceneEmpty, 1 / 17.f); }
// The firmware renders 16-row strips.
static void test_busy_sweep_16_row_strips() { sweepMatchesFullRedraw(scr::sceneBusy, 1 / 60.f, 16); }
// Bold Editorial: the flat, even sweep over a black scope.
static void test_editorial_sweep_30fps() { sweepMatchesFullRedraw(scr::sceneBusy, 1 / 30.f, 32, scr::editorial()); }
static void test_editorial_sweep_16_row_strips() {
    sweepMatchesFullRedraw(scr::sceneBusy, 1 / 60.f, 16, scr::editorial());
}

// One flight of `fam` drifting by fractions of a pixel, selected and deselected at every step: the
// selection ring reaches further than the icon, so the blip's box must cover it.
static void selectSweepLeavesNoStale(data::Family fam) {
    auto app = std::make_unique<scr::ScreenApp>();
    scr::sceneQuiet(app->scene());
    app->scene().flights[0].shape = uint8_t(fam);
    app->sceneUpdated();
    app->host().showNow(scr::ScreenId::Radar);
    FrameCapture d;
    gfx::StripRenderer r(d, bufA, bufB, 16);
    r.setLayers(app->layers(), app->layerCount());
    r.frame(1 / 30.f);
    for (int step = 0; step < 60; ++step) {
        scr::SceneFlight& f = app->scene().flights[0];
        f.bearingDeg += 2.9f, f.distNm += 0.11f;  // about half a pixel a step: every fraction of a pixel
        app->host().setSelection(step % 2 ? 0 : f.hex);
        app->sceneUpdated();
        r.frame(1 / 30.f);
        std::vector<uint16_t> incremental = d.px;
        r.invalidateAll();
        r.frame(0);
        int stale = 0;
        for (size_t k = 0; k < incremental.size(); ++k) stale += incremental[k] != d.px[k];
        if (stale) {
            char msg[64];
            std::snprintf(msg, sizeof msg, "step %d: %d pixels left stale", step, stale);
            TEST_FAIL_MESSAGE(msg);
        }
    }
}

static void test_select_sweep_small_icon() { selectSweepLeavesNoStale(data::Family::Bizjet); }  // 13 px
static void test_select_sweep_helicopter() { selectSweepLeavesNoStale(data::Family::Helicopter); }

// The selected flight changing family (bigger, smaller, a rotor, then no shape at all): the old icon and
// ring must go with it.
static void test_family_change_while_selected() {
    auto app = std::make_unique<scr::ScreenApp>();
    scr::sceneQuiet(app->scene());
    app->sceneUpdated();
    app->host().showNow(scr::ScreenId::Radar);
    app->host().setSelection(app->scene().flights[0].hex);
    FrameCapture d;
    gfx::StripRenderer r(d, bufA, bufB, 16);
    r.setLayers(app->layers(), app->layerCount());
    r.frame(1 / 30.f);
    for (data::Family fam : {data::Family::Superjumbo, data::Family::Single, data::Family::Helicopter,
                             data::Family::None, data::Family::Widebody}) {
        app->scene().flights[0].shape = uint8_t(fam);
        app->sceneUpdated();
        r.frame(1 / 30.f);
        std::vector<uint16_t> incremental = d.px;
        r.invalidateAll();
        r.frame(0);
        int stale = 0;
        for (size_t k = 0; k < incremental.size(); ++k) stale += incremental[k] != d.px[k];
        TEST_ASSERT_EQUAL_MESSAGE(0, stale, "pixels left stale by a family change");
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_busy_sweep_30fps);
    RUN_TEST(test_busy_sweep_23fps);
    RUN_TEST(test_empty_sweep_17fps);
    RUN_TEST(test_busy_sweep_16_row_strips);
    RUN_TEST(test_editorial_sweep_30fps);
    RUN_TEST(test_editorial_sweep_16_row_strips);
    RUN_TEST(test_select_sweep_small_icon);
    RUN_TEST(test_select_sweep_helicopter);
    RUN_TEST(test_family_change_while_selected);
    return UNITY_END();
}
