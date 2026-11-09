#include "DemoRender.h"

#include <cmath>
#include <cstring>
#include <memory>

#include "assets/LogoStore.h"
#include "gfx/StripRenderer.h"
#include "map/MapPack.h"
#include "screens/DemoScenes.h"
#include "screens/ScreenApp.h"

namespace app {
namespace {

struct NamedScene {
    const char* name;
    void (*fill)(scr::Scene&);
};
const NamedScene kScenes[] = {
    {"busy", scr::sceneBusy}, {"quiet", scr::sceneQuiet}, {"night", scr::sceneNight},
    {"empty", scr::sceneEmpty}, {"stats", scr::sceneStats},
};
struct NamedScreen {
    const char* name;
    scr::ScreenId id;
};
const NamedScreen kScreens[] = {
    {"closest", scr::ScreenId::Closest}, {"radar", scr::ScreenId::Radar}, {"board", scr::ScreenId::Board},
    {"sky", scr::ScreenId::Sky},         {"map", scr::ScreenId::Map},     {"stats", scr::ScreenId::Stats},
    {"alert", scr::ScreenId::Alert},
};

// The busy scene with an emergency takeover on its first flight, set in the scene as the alert goldens do
// (test/test_alert_screen), so no alert engine or clock is involved.
void sceneBusyEmergency(scr::Scene& s) {
    scr::sceneBusy(s);
    addDemoEmergency(s);
}

}  // namespace

void addDemoEmergency(scr::Scene& s) {
    scr::SceneAlert& a = s.alert;
    a = scr::SceneAlert{};
    a.active = true, a.kind = scr::AlertKind::Emergency, a.live = true;
    a.flight = s.flights[0];
    a.startMs = 1000, a.endsMs = 31000;
    std::strcpy(a.flight.squawk, "7700"), a.flight.emergency = true;
}

// As test_map_screen's backInTime.
void backInTime(scr::Scene& s, float seconds) {
    for (int i = 0; i < s.count; ++i) {
        scr::SceneFlight& f = s.flights[i];
        float b = f.bearingDeg * 0.0174533f, t = f.trackDeg * 0.0174533f, d = f.gsKt * seconds / 3600.f;
        float e = f.distNm * std::sin(b) - d * std::sin(t), n = f.distNm * std::cos(b) - d * std::cos(t);
        f.distNm = std::sqrt(e * e + n * n);
        f.bearingDeg = std::fmod(std::atan2(e, n) / 0.0174533f + 360.f, 360.f);
    }
    s.epoch -= int64_t(seconds);
}

bool renderDemo(const char* scene, const char* screen, const char* theme, hal::IDisplay& display, uint16_t* bufA,
                uint16_t* bufB, int stripH, assets::LogoStore* logos, basemap::MapPack* map) {
    void (*fill)(scr::Scene&) = nullptr;
    for (const auto& n : kScenes)
        if (!std::strcmp(n.name, scene)) fill = n.fill;
    const NamedScreen* named = nullptr;
    for (const auto& n : kScreens)
        if (!std::strcmp(n.name, screen)) named = &n;
    if (!fill || !named || (std::strcmp(theme, "cinematic") && std::strcmp(theme, "editorial"))) return false;
    if (named->id == scr::ScreenId::Alert) fill = sceneBusyEmergency;

    auto screens = std::make_unique<scr::ScreenApp>(scr::themeByName(theme));
    if (logos) screens->setLogos(logos);
    if (map) screens->setMap(map);
    gfx::StripRenderer renderer(display, bufA, bufB, stripH);
    renderer.setLayers(screens->layers(), screens->layerCount());
    const bool trails = named->id == scr::ScreenId::Map;
    for (int k = trails ? 7 : 0; k >= 0; --k) {
        fill(screens->scene());
        if (trails) backInTime(screens->scene(), 10.f * k);  // k = 0 too, as withHistory (float round trip)
        screens->sceneUpdated();
    }
    screens->host().showNow(named->id);
    const int frames = named->id == scr::ScreenId::Sky ? 100 : 75;
    for (int i = 0; i < frames; ++i) renderer.frame(1 / 30.f);
    return true;
}

void printDemoNames(std::FILE* out) {
    std::fprintf(out, "  scenes:");
    for (const auto& n : kScenes) std::fprintf(out, " %s", n.name);
    std::fprintf(out, "\n  screens:");
    for (const auto& n : kScreens) std::fprintf(out, " %s", n.name);
    std::fprintf(out, "\n  themes: cinematic editorial\n");
}

}  // namespace app
