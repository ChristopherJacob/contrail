#include "DemoClip.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

#include "DemoRender.h"
#include "gfx/StripRenderer.h"
#include "screens/DemoScenes.h"
#include "screens/ScreenApp.h"

namespace app {
namespace {

constexpr float kDt = 1 / 30.f;
constexpr int kWarmup = 150;  // a full radar turn and the Closest contrail's 2.5 s
constexpr int kFade = 15;
constexpr float kLapse = 10;  // flight seconds per second of clip

class Capture : public hal::IDisplay {
   public:
    std::vector<uint16_t> px = std::vector<uint16_t>(240 * 320);
    int width() const override { return 240; }
    int height() const override { return 320; }
    void pushRect(int x, int y, int w, int h, const uint16_t* p) override {
        for (int r = 0; r < h; ++r) std::copy(p + r * w, p + (r + 1) * w, px.begin() + (y + r) * 240 + x);
    }
};

void sceneBusyAlert(scr::Scene& s) {
    scr::sceneBusy(s);
    addDemoEmergency(s);
}

struct Clip {
    const char* name;
    scr::ScreenId id;
    void (*fill)(scr::Scene&);
    int frames, fade;
    int stepEvery;  // frames between time-lapse steps; 0: the flights stay put
};
const Clip kClips[] = {
    {"closest", scr::ScreenId::Closest, scr::sceneBusy, 145, 0, 0},
    {"radar", scr::ScreenId::Radar, scr::sceneBusy, 120, 0, 0},
    {"sky", scr::ScreenId::Sky, scr::sceneNight, 72, 0, 0},
    {"alert", scr::ScreenId::Alert, sceneBusyAlert, 90, kFade, 0},
    {"map", scr::ScreenId::Map, scr::sceneBusy, 240, kFade, 3},
    {"board", scr::ScreenId::Board, scr::sceneBusy, 240, kFade, 15},
};

bool knownTheme(const char* theme) { return !std::strcmp(theme, "cinematic") || !std::strcmp(theme, "editorial"); }

// The scene `ahead` flight seconds on, nearest first as the device's model gives them.
void stepScene(scr::ScreenApp& app, void (*fill)(scr::Scene&), float ahead) {
    scr::Scene& s = app.scene();
    fill(s);
    backInTime(s, -ahead);
    std::stable_sort(s.flights, s.flights + s.count,
                     [](const scr::SceneFlight& a, const scr::SceneFlight& b) { return a.distNm < b.distNm; });
    app.sceneUpdated();
}

// 70 s of trails (renderDemo's history), or the plain scene.
void startScene(scr::ScreenApp& app, void (*fill)(scr::Scene&), bool trails) {
    for (int k = trails ? 7 : 0; k >= 0; --k) {
        fill(app.scene());
        if (trails) backInTime(app.scene(), 10.f * k);
        app.sceneUpdated();
    }
}

void sceneStatsAlert(scr::Scene& s) {
    scr::sceneStats(s);
    addDemoEmergency(s);
}

uint16_t blend565(uint16_t a, uint16_t b, int t, int n) {  // t/n of b
    auto mix = [&](int ca, int cb) { return (ca * (n - t) + cb * t + n / 2) / n; };
    return uint16_t(mix(a >> 11, b >> 11) << 11 | mix((a >> 5) & 63, (b >> 5) & 63) << 5 | mix(a & 31, b & 31));
}

}  // namespace

int renderClip(const char* screen, const char* theme, const ClipSink& sink, uint16_t* bufA, uint16_t* bufB, int stripH) {
    const Clip* c = nullptr;
    for (const auto& k : kClips)
        if (!std::strcmp(k.name, screen)) c = &k;
    if (!c || !knownTheme(theme)) return -1;

    auto app = std::make_unique<scr::ScreenApp>(scr::themeByName(theme));
    Capture display;
    gfx::StripRenderer renderer(display, bufA, bufB, stripH);
    renderer.setLayers(app->layers(), app->layerCount());
    startScene(*app, c->fill, c->id == scr::ScreenId::Map);
    app->host().showNow(c->id);
    for (int i = 0; i < kWarmup; ++i) renderer.frame(kDt);

    std::vector<std::vector<uint16_t>> held;
    const int drawn = c->frames + c->fade;
    std::vector<uint16_t> out(240 * 320);
    for (int i = 0; i < drawn; ++i) {
        if (c->stepEvery && i % c->stepEvery == 0)
            stepScene(*app, c->fill, (i / c->stepEvery + 1) * c->stepEvery * kDt * kLapse);
        renderer.frame(kDt);
        if (i < c->fade) held.push_back(display.px);
        else if (i < c->frames) sink(display.px.data());
        else {
            const int j = i - c->frames;
            for (size_t p = 0; p < out.size(); ++p) out[p] = blend565(display.px[p], held[j][p], j + 1, c->fade + 1);
            sink(out.data());
        }
    }
    return c->frames;
}

int renderTour(const char* theme, const ClipSink& sink, uint16_t* bufA, uint16_t* bufB, int stripH) {
    if (!knownTheme(theme)) return -1;
    static const scr::ScreenId kOrder[] = {scr::ScreenId::Closest, scr::ScreenId::Radar, scr::ScreenId::Map,
                                           scr::ScreenId::Board,   scr::ScreenId::Sky,   scr::ScreenId::Stats,
                                           scr::ScreenId::Alert};
    constexpr int kSteps = 3, kFadeFrames = 6, kDwell = 105;  // ScreenHost::kFadeS is 6 frames

    auto app = std::make_unique<scr::ScreenApp>(scr::themeByName(theme));
    Capture display;
    gfx::StripRenderer renderer(display, bufA, bufB, stripH);
    renderer.setLayers(app->layers(), app->layerCount());
    void (*fill)(scr::Scene&) = scr::sceneStats;
    startScene(*app, fill, true);
    scr::ScreenHost& host = app->host();
    host.showNow(scr::ScreenId::Stats);
    for (int i = 0; i < kWarmup; ++i) renderer.frame(kDt);

    int frames = 0, step = 0, tick = 0;
    auto draw = [&](bool keep) {
        if (tick++ % kSteps == 0) stepScene(*app, fill, ++step * kSteps * kDt * kLapse);
        renderer.frame(kDt);
        if (keep) sink(display.px.data()), ++frames;
    };
    host.show(scr::ScreenId::Closest);
    while (host.current() != scr::ScreenId::Closest) draw(false);
    // The switch's frame is all veil: it opens the file (and, drawn again at the end, closes it).
    sink(display.px.data()), ++frames;
    const int n = int(sizeof kOrder / sizeof kOrder[0]);
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < kFadeFrames + kDwell; ++j) draw(true);
        const scr::ScreenId next = kOrder[(i + 1) % n];
        if (next == scr::ScreenId::Alert) fill = sceneStatsAlert;
        host.show(next);
        while (host.current() != next) draw(true);
    }
    return frames;
}

}  // namespace app
