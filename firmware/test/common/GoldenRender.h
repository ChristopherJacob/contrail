#pragma once
// Shared by the pass 2 screen suites: draw a ScreenApp the way the device does (16-row strips), and
// check the incremental redraw against a full one.
#include <unity.h>

#include <memory>
#include <vector>

#include "Fixture.h"
#include "FrameCapture.h"
#include "TestStorage.h"
#include "assets/LogoStore.h"
#include "gfx/StripRenderer.h"
#include "map/MapPack.h"
#include "screens/ScreenApp.h"

namespace golden {

constexpr int kStripH = 16;  // as main_esp32.cpp
inline uint16_t bufA[240 * kStripH], bufB[240 * kStripH];

struct Rig {
    MemStorage sd;  // before app, so the store the app points at outlives it
    assets::LogoStore logos;
    basemap::MapPack map;
    std::unique_ptr<scr::ScreenApp> app;
    FrameCapture d;
    gfx::StripRenderer r{d, bufA, bufB, kStripH};
    explicit Rig(const scr::Theme& theme = scr::cinematicSky()) : app(std::make_unique<scr::ScreenApp>(theme)) {
        r.setLayers(app->layers(), app->layerCount());
    }
    // A card holding the three-logo fixture pack (UAL, DAL, KLM).
    void useLogos() {
        sd.add(assets::LogoStore::kPath, loadFixture("logos_tiny.pak"));
        logos.begin(&sd);
        app->setLogos(&logos);
    }
    // A card holding the 40 nm fixture map pack.
    void useMap() {
        sd.add(basemap::MapPack::kPath, loadFixture("map_small.pak"));
        map.begin(&sd);
        app->setMap(&map);
    }
    void show(scr::ScreenId id) { app->host().showNow(id); }
    void frames(int n) {
        for (int i = 0; i < n; ++i) r.frame(1 / 30.f);
    }
    // Pixels one frame pushes.
    int pushed(float dt = 1 / 30.f) {
        d.pushedRects.clear();
        r.frame(dt);
        int px = 0;
        for (const auto& p : d.pushedRects) px += p.w * p.h;
        return px;
    }
    void assertNoStale() {
        std::vector<uint16_t> inc = d.px;
        r.invalidateAll();
        r.frame(0);
        int stale = 0;
        for (size_t i = 0; i < inc.size(); ++i) stale += inc[i] != d.px[i];
        TEST_ASSERT_EQUAL_MESSAGE(0, stale, "pixels left stale by incremental redraw");
    }
};

// Scene, screen, 2.5 s of animation, the golden, then a full redraw that must match. `logos`: with the
// fixture pack on a card. `mapPack`: with the fixture map pack on a card.
inline void render(scr::ScreenId id, void (*fill)(scr::Scene&), const char* name,
                   const scr::Theme& theme = scr::cinematicSky(), uint32_t selectHex = 0, int frames = 75,
                   bool logos = false, bool mapPack = false) {
    Rig g(theme);
    if (logos) g.useLogos();
    if (mapPack) g.useMap();
    fill(g.app->scene());
    g.app->sceneUpdated();
    g.show(id);
    if (selectHex) g.app->host().setSelection(selectHex);
    g.frames(frames);
    assertGolden(name, g.d.px, 240, 320);
    g.assertNoStale();
}

}  // namespace golden
