#include <unity.h>

#include <cstdlib>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include "FrameCapture.h"
#include "gfx/StripRenderer.h"
#include "screens/DemoScenes.h"
#include "screens/ScreenApp.h"

// The page dots and the offline badge drawn over each screen: they must stay inside their
// boxes, leave nothing behind when they go, and (goldens) look right. Set OVERLAY_DUMP=<dir>
// to also write every frame as <dir>/overlay_<screen>.ppm for review.

void setUp() {
    setenv("TZ", "UTC0", 1);
    tzset();
}
void tearDown() {}

static constexpr int kStripH = 16, kFrames = 75;
static uint16_t bufA[240 * kStripH], bufB[240 * kStripH];

struct Rig {
    std::unique_ptr<scr::ScreenApp> app = std::make_unique<scr::ScreenApp>();
    FrameCapture d;
    gfx::StripRenderer r{d, bufA, bufB, kStripH};
    Rig(scr::ScreenId id, void (*fill)(scr::Scene&)) {
        fill(app->scene());
        app->sceneUpdated();
        app->configure(scr::DirectorMode::Stay, 30000);  // keep the screen whatever the scene
        app->host().showNow(id);
        r.setLayers(app->layers(), app->layerCount());
    }
    void frames(int n) {
        for (int i = 0; i < n; ++i) r.frame(1 / 30.f);
    }
};

static bool inside(const gfx::Rect& b, int x, int y) { return x >= b.x && x < b.right() && y >= b.y && y < b.y + b.h; }

static void check(scr::ScreenId id, void (*fill)(scr::Scene&), const char* name, bool golden) {
    Rig plain(id, fill), over(id, fill);
    // Offline, and the user touched the screen a moment ago: both overlays show.
    over.app->scene().online = false;
    over.app->director().noteTouch(1000);
    over.app->tick(1500);
    TEST_ASSERT_TRUE(over.app->pageDots().visible());
    TEST_ASSERT_TRUE(over.app->offlineBadge().on());
    plain.frames(kFrames);
    over.frames(kFrames);

    if (const char* dir = std::getenv("OVERLAY_DUMP"))
        writePpm(std::string(dir) + "/overlay_" + name + ".ppm", over.d.px, 240, 320);
    if (golden) assertGolden((std::string("overlay_") + name).c_str(), over.d.px, 240, 320);

    const gfx::Rect dots = scr::PageDots::kBox, badge = scr::OfflineBadge::kBox;
    int outside = 0, inDots = 0, inBadge = 0;
    for (int y = 0; y < 320; ++y)
        for (int x = 0; x < 240; ++x) {
            if (plain.d.px[y * 240 + x] == over.d.px[y * 240 + x]) continue;
            if (inside(dots, x, y)) ++inDots;
            else if (inside(badge, x, y)) ++inBadge;
            else ++outside;
        }
    TEST_ASSERT_EQUAL_MESSAGE(0, outside, "overlay drew outside its box");
    TEST_ASSERT_GREATER_THAN_MESSAGE(20, inDots, "page dots not drawn");
    TEST_ASSERT_GREATER_THAN_MESSAGE(200, inBadge, "offline badge not drawn");

    // Back online and the minute is up: both go, and the screen underneath is restored.
    over.app->scene().online = true;
    over.app->tick(1000 + scr::Director::kExploreMs);
    TEST_ASSERT_FALSE(over.app->pageDots().visible());
    TEST_ASSERT_FALSE(over.app->offlineBadge().on());
    plain.frames(1);
    over.frames(1);
    int differ = 0;
    for (size_t i = 0; i < plain.d.px.size(); ++i) differ += plain.d.px[i] != over.d.px[i];
    TEST_ASSERT_EQUAL_MESSAGE(0, differ, "overlay left pixels behind");
}

static void test_radar() { check(scr::ScreenId::Radar, scr::sceneBusy, "radar", true); }
static void test_board() { check(scr::ScreenId::Board, scr::sceneBusy, "board", true); }
static void test_closest() { check(scr::ScreenId::Closest, scr::sceneBusy, "closest", true); }
static void test_sky() { check(scr::ScreenId::Sky, scr::sceneNight, "sky", true); }
// Offline before the clock is set: the badge sits between the title and the "No clock" note.
static void test_stats() { check(scr::ScreenId::Stats, scr::sceneStatsNoClock, "stats", false); }

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_radar);
    RUN_TEST(test_board);
    RUN_TEST(test_closest);
    RUN_TEST(test_sky);
    RUN_TEST(test_stats);
    return UNITY_END();
}
