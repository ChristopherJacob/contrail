// Desktop simulator: the same core and screens as the CYD, in an SDL window, with live data.
//   program                      interactive window
//   program --shot out.ppm [s]   headless: run s seconds of live data at 30 fps, write the frame
//   program --render <scene> <screen> <theme> out.ppm   headless: a fixed demo scene, no network, no
//                                settings file, drawn as the golden tests draw it (the site's screenshots)
//   CONTRAIL_THEME=editorial program   the same in Bold Editorial
// Settings persist in ~/.contrail/settings.json (same keys as the device).
#include <sys/stat.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "NativeHal.h"
#include "NativeStorage.h"
#include "SdlDisplay.h"
#include "alerts/AlertConfig.h"
#include "alerts/AlertRules.h"
#include "app/CoreApp.h"
#include "app/PanelBridge.h"
#include "assets/LogoStore.h"
#include "map/MapPack.h"
#include "gfx/StripRenderer.h"
#include "screens/DemoScenes.h"
#include "screens/ScreenApp.h"
#include "services/Log.h"

#ifndef CONTRAIL_VERSION
#define CONTRAIL_VERSION "dev"
#endif

// 16-row strips, as on the device (main_esp32.cpp), so damage and timing behave the same.
constexpr int kStripH = 16;
static uint16_t bufA[240 * kStripH], bufB[240 * kStripH];

static uint32_t microsNow() {
    using namespace std::chrono;
    return uint32_t(duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

class CaptureDisplay : public hal::IDisplay {
   public:
    std::vector<uint16_t> px = std::vector<uint16_t>(240 * 320);
    int width() const override { return 240; }
    int height() const override { return 320; }
    void pushRect(int x, int y, int w, int h, const uint16_t* p) override {
        for (int r = 0; r < h; ++r) std::copy(p + r * w, p + (r + 1) * w, px.begin() + (y + r) * 240 + x);
    }
};

static bool writePpm(const char* path, const std::vector<uint16_t>& px) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n240 320\n255\n");
    for (uint16_t v : px) {
        gfx::Rgb c = gfx::unpack565(v);
        std::fputc(c.r, f), std::fputc(c.g, f), std::fputc(c.b, f);
    }
    return std::fclose(f) == 0;
}

static std::string settingsPath() {
    const char* home = std::getenv("HOME");
    std::string dir = std::string(home ? home : ".") + "/.contrail";
    mkdir(dir.c_str(), 0755);
    return dir + "/settings.json";
}

// The busy scene with an emergency takeover on its first flight, set in the scene as the alert goldens do
// (test/test_alert_screen), so no alert engine or clock is involved.
static void sceneBusyEmergency(scr::Scene& s) {
    scr::sceneBusy(s);
    scr::SceneAlert& a = s.alert;
    a = scr::SceneAlert{};
    a.active = true, a.kind = scr::AlertKind::Emergency, a.live = true;
    a.flight = s.flights[0];
    a.startMs = 1000, a.endsMs = 31000;
    std::strcpy(a.flight.squawk, "7700"), a.flight.emergency = true;
}

// Every flight stepped `seconds` back along its track, as test_map_screen's backInTime.
static void backInTime(scr::Scene& s, float seconds) {
    for (int i = 0; i < s.count; ++i) {
        scr::SceneFlight& f = s.flights[i];
        float b = f.bearingDeg * 0.0174533f, t = f.trackDeg * 0.0174533f, d = f.gsKt * seconds / 3600.f;
        float e = f.distNm * std::sin(b) - d * std::sin(t), n = f.distNm * std::cos(b) - d * std::cos(t);
        f.distNm = std::sqrt(e * e + n * n);
        f.bearingDeg = std::fmod(std::atan2(e, n) / 0.0174533f + 360.f, 360.f);
    }
    s.epoch -= int64_t(seconds);
}

// --render: one demo scene on one screen in one theme, drawn in the golden tests' call order
// (test/common/GoldenRender.h: fill, sceneUpdated, showNow, 75 frames of 1/30 s, no tick), so the shot
// matches the goldens and is the same on every run. As their goldens do, the Map gets 70 s of trails
// (8 scene updates, as test_map_screen's map_busy) and the Sky 100 frames (sky_night: a beacon lit). No network, no settings file, no SDL window; an SD
// card (logos, map pack) only when CONTRAIL_SD names a folder.
static int renderShot(const char* sceneName, const char* screenName, const char* themeName, const char* out) {
    struct NamedScene { const char* name; void (*fill)(scr::Scene&); };
    static const NamedScene kScenes[] = {
        {"busy", scr::sceneBusy}, {"quiet", scr::sceneQuiet}, {"night", scr::sceneNight},
        {"empty", scr::sceneEmpty}, {"stats", scr::sceneStats},
    };
    struct NamedScreen { const char* name; scr::ScreenId id; };
    static const NamedScreen kScreens[] = {
        {"closest", scr::ScreenId::Closest}, {"radar", scr::ScreenId::Radar}, {"board", scr::ScreenId::Board},
        {"sky", scr::ScreenId::Sky},         {"map", scr::ScreenId::Map},     {"stats", scr::ScreenId::Stats},
        {"alert", scr::ScreenId::Alert},
    };
    void (*fill)(scr::Scene&) = nullptr;
    for (const auto& n : kScenes)
        if (!std::strcmp(n.name, sceneName)) fill = n.fill;
    const NamedScreen* screen = nullptr;
    for (const auto& n : kScreens)
        if (!std::strcmp(n.name, screenName)) screen = &n;
    bool themeOk = !std::strcmp(themeName, "cinematic") || !std::strcmp(themeName, "editorial");
    if (!fill || !screen || !themeOk) {
        std::fprintf(stderr, "usage: --render <scene> <screen> <theme> <out.ppm>\n  scenes:");
        for (const auto& n : kScenes) std::fprintf(stderr, " %s", n.name);
        std::fprintf(stderr, "\n  screens:");
        for (const auto& n : kScreens) std::fprintf(stderr, " %s", n.name);
        std::fprintf(stderr, "\n  themes: cinematic editorial\n");
        return 2;
    }
    if (screen->id == scr::ScreenId::Alert) fill = sceneBusyEmergency;
    setenv("TZ", "UTC0", 1);  // as the goldens: the scenes' clocks are UTC
    tzset();

    const char* sdEnv = std::getenv("CONTRAIL_SD");
    std::unique_ptr<hal::NativeStorage> sd;
    assets::LogoStore logos;
    basemap::MapPack mapPack;
    auto screens = std::make_unique<scr::ScreenApp>(scr::themeByName(themeName));
    if (sdEnv && *sdEnv) {
        sd = std::make_unique<hal::NativeStorage>(sdEnv);
        logos.begin(sd.get());
        mapPack.begin(sd.get());
        screens->setLogos(&logos);
        screens->setMap(&mapPack);
    }
    CaptureDisplay display;
    gfx::StripRenderer renderer(display, bufA, bufB, kStripH);
    renderer.setLayers(screens->layers(), screens->layerCount());
    const bool trails = screen->id == scr::ScreenId::Map;
    for (int k = trails ? 7 : 0; k >= 0; --k) {
        fill(screens->scene());
        if (trails) backInTime(screens->scene(), 10.f * k);  // k = 0 too, as withHistory (float round trip)
        screens->sceneUpdated();
    }
    screens->host().showNow(screen->id);
    const int frames = screen->id == scr::ScreenId::Sky ? 100 : 75;
    for (int i = 0; i < frames; ++i) renderer.frame(1 / 30.f);
    bool ok = writePpm(out, display.px);
    screens.reset();  // before the stores it points at
    return ok ? 0 : 1;
}

int main(int argc, char** argv) {
    // --render <scene> <screen> <theme> <out.ppm>: a fixed demo scene, no network, for the site's screenshots.
    if (argc >= 2 && std::strcmp(argv[1], "--render") == 0)
        return argc == 6 ? renderShot(argv[2], argv[3], argv[4], argv[5]) : renderShot("", "", "", "");
    bool shot = argc >= 3 && std::strcmp(argv[1], "--shot") == 0;
    float shotSeconds = argc >= 4 ? float(std::atof(argv[3])) : 8.f;

    svc::logSink() = [](const char* line) { std::fprintf(stderr, "%s\n", line); };
    hal::NativeClock clock;
    hal::FileStore kv(settingsPath());
    hal::CurlHttp http("Contrail-sim/" CONTRAIL_VERSION " (+https://github.com/ChristopherJacob/contrail)");
    app::CoreApp core(http, clock, kv);
    core.begin();
    if (!core.settings().getBool(svc::key::kLocSet)) {  // default home: San Francisco Intl
        core.settings().set(svc::key::kLat, "37.6189");
        core.settings().set(svc::key::kLon, "-122.375");
        core.settings().set(svc::key::kLocSet, "true");
    }

    std::string tz = core.settings().getString(svc::key::kTz);
    setenv("TZ", tz.empty() ? "UTC0" : tz.c_str(), 1);
    tzset();

    std::atomic<bool> running{true};
    std::thread net([&] {
        while (running) {
            core.networkTick();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });

    // The "SD card": a folder (CONTRAIL_SD, default sim_sd/ in the working directory) holding contrail/logos.pak and contrail/map.pak.
    // Declared before the screens, so the store outlives them; begun before the screens are given it.
    const char* sdEnv = std::getenv("CONTRAIL_SD");
    hal::NativeStorage sd(sdEnv && *sdEnv ? sdEnv : "sim_sd");
    assets::LogoStore logos;
    logos.begin(&sd);
    basemap::MapPack mapPack;
    mapPack.begin(&sd);
    if (!sd.mounted()) svc::logf("[sd] no card (no folder %s)", sdEnv && *sdEnv ? sdEnv : "sim_sd");
    auto screens = std::make_unique<scr::ScreenApp>();
    screens->configure(scr::parseDirectorMode(core.settings().getEnum(svc::key::kScreenMode).c_str()),
                       uint32_t(core.settings().getInt(svc::key::kCarouselS)) * 1000u);
#ifdef CONTRAIL_SOAK_CAROUSEL
    screens->configure(scr::DirectorMode::Carousel, CONTRAIL_SOAK_CAROUSEL * 1000u);
#endif
    // CONTRAIL_THEME=editorial overrides the setting, for trying the theme from the shell.
    const char* themeEnv = std::getenv("CONTRAIL_THEME");
    if (themeEnv && *themeEnv) screens->useTheme(scr::themeByName(themeEnv));
    else screens->useTheme(scr::themeByName(core.settings().getEnum(svc::key::kTheme).c_str()));
    screens->configureAlerts(alerts::configFromSettings(core.settings()));
    screens->setLogos(&logos);
    screens->setMap(&mapPack);
    screens->setMapClock(microsNow);
    // Panel edits save at once here; any settings change (this thread or the network thread's geolocation)
    // sets a flag, and the main loop re-applies theme, mode and panel values, as on the device.
    std::atomic<bool> displayDirty{true};
    core.settings().onChange([&](const char*) { displayDirty = true; });
    std::string themeName = core.settings().getEnum(svc::key::kTheme);
    auto applyDisplay = [&] {
        svc::Settings& s = core.settings();
        const std::string theme = s.getEnum(svc::key::kTheme);
        if (!(themeEnv && *themeEnv) && theme != themeName)
            themeName = theme, screens->useTheme(scr::themeByName(theme.c_str()));
#ifndef CONTRAIL_SOAK_CAROUSEL
        screens->configure(scr::parseDirectorMode(s.getEnum(svc::key::kScreenMode).c_str()),
                           uint32_t(s.getInt(svc::key::kCarouselS)) * 1000u);
#endif
        screens->setPanelValues(app::panelValues(s));
    };
    // CONTRAIL_ALERT=emerg|watch|mil|low shows that alert 3 s in, for trying the takeover from the shell.
    scr::AlertKind simAlert = scr::AlertKind::Emergency;
    const char* alertEnv = std::getenv("CONTRAIL_ALERT");
    bool alertPending = alertEnv && alerts::parseTestCommand((std::string("alert ") + alertEnv).c_str(), simAlert);
    const uint32_t simStart = clock.millis();
    auto maybeInject = [&] {
        if (alertPending && clock.millis() - simStart >= 3000)
            screens->injectAlert(simAlert, clock.millis()), alertPending = false;
    };
    int rc = 0;
    if (shot) {
        CaptureDisplay display;
        gfx::StripRenderer renderer(display, bufA, bufB, kStripH);
        renderer.setLayers(screens->layers(), screens->layerCount());
        uint32_t start = clock.millis();
        while (clock.millis() - start < shotSeconds * 1000) {
            maybeInject();
            if (displayDirty.exchange(false)) applyDisplay();
            if (core.renderTick(screens->scene())) screens->sceneUpdated();
            screens->tick(clock.millis());
            renderer.frame(1 / 30.f);
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }
        rc = writePpm(argv[2], display.px) ? 0 : 1;
    } else {
        hal::SdlDisplay display(2);
        gfx::StripRenderer renderer(display, bufA, bufB, kStripH);
        renderer.setLayers(screens->layers(), screens->layerCount());
        renderer.setMicrosSource(microsNow);
        uint32_t last = microsNow();
        while (display.pumpEvents()) {
            uint32_t now = microsNow();
            float dt = (now - last) / 1e6f;
            last = now;
            maybeInject();
            if (core.renderTick(screens->scene())) screens->sceneUpdated();
            screens->tick(clock.millis());
            int tx = 0, ty = 0;
            bool touched = display.read(tx, ty);
            screens->touch(touched, tx, ty, clock.millis());
            for (scr::SettingEdit e; screens->takeSettingEdit(e);) app::applyEdit(core.settings(), e);
            if (displayDirty.exchange(false)) applyDisplay();
            renderer.frame(dt);
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }
    running = false;
    net.join();
    return rc;
}
