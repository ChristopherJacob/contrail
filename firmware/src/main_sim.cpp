// Desktop simulator: the same core and screens as the CYD, in an SDL window, with live data.
//   program                      interactive window
//   program --shot out.ppm [s]   headless: run s seconds of live data at 30 fps, write the frame
//   program --render <scene> <screen> <theme> out.ppm   headless: a fixed demo scene, no network, no
//                                settings file, drawn as the golden tests draw it (the site's screenshots)
//   program --clip <screen> <theme>   headless: a preview loop as raw 240x320 RGB24 frames on stdout (the site's clips)
//   program --tour               the same for the tour of the screens
//   CONTRAIL_THEME=editorial program   the same in Bold Editorial
// Settings persist in ~/.contrail/settings.json (same keys as the device).
#include <sys/stat.h>

#include <atomic>
#include <chrono>
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
#include "app/DemoClip.h"
#include "app/DemoRender.h"
#include "app/PanelBridge.h"
#include "assets/LogoStore.h"
#include "map/MapPack.h"
#include "gfx/StripRenderer.h"
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

// --render: one demo scene on one screen in one theme (app::renderDemo), with no network, no settings file and no
// SDL window; an SD card (logos, map pack) only when CONTRAIL_SD names a folder.
static int renderShot(const char* sceneName, const char* screenName, const char* themeName, const char* out) {
    setenv("TZ", "UTC0", 1);  // as the goldens: the scenes' clocks are UTC
    tzset();
    const char* sdEnv = std::getenv("CONTRAIL_SD");
    std::unique_ptr<hal::NativeStorage> sd;
    assets::LogoStore logos;
    basemap::MapPack mapPack;
    if (sdEnv && *sdEnv) {
        sd = std::make_unique<hal::NativeStorage>(sdEnv);
        logos.begin(sd.get());
        mapPack.begin(sd.get());
    }
    CaptureDisplay display;
    if (!app::renderDemo(sceneName, screenName, themeName, display, bufA, bufB, kStripH, sd ? &logos : nullptr,
                         sd ? &mapPack : nullptr)) {
        std::fprintf(stderr, "usage: --render <scene> <screen> <theme> <out.ppm>\n");
        app::printDemoNames(stderr);
        return 2;
    }
    return writePpm(out, display.px) ? 0 : 1;
}

// --clip and --tour: raw RGB24 frames on stdout, for tools/previews.sh.
static void writeRgb(const uint16_t* px) {
    static std::vector<uint8_t> rgb(240 * 320 * 3);
    for (int i = 0; i < 240 * 320; ++i) {
        gfx::Rgb c = gfx::unpack565(px[i]);
        rgb[i * 3] = c.r, rgb[i * 3 + 1] = c.g, rgb[i * 3 + 2] = c.b;
    }
    std::fwrite(rgb.data(), 1, rgb.size(), stdout);
}

static int renderFrames(const char* screen, const char* theme) {
    setenv("TZ", "UTC0", 1);  // as the goldens: the scenes' clocks are UTC
    tzset();
    int n = screen ? app::renderClip(screen, theme, writeRgb, bufA, bufB, kStripH)
                   : app::renderTour(theme, writeRgb, bufA, bufB, kStripH);
    if (n < 0) {
        std::fprintf(stderr, "usage: --clip <closest|radar|sky|alert|map|board> <cinematic|editorial>, or --tour\n");
        return 2;
    }
    return std::fflush(stdout) == 0 ? 0 : 1;
}

int main(int argc, char** argv) {
    // --render <scene> <screen> <theme> <out.ppm>: a fixed demo scene, no network, for the site's screenshots.
    if (argc >= 2 && std::strcmp(argv[1], "--render") == 0)
        return argc == 6 ? renderShot(argv[2], argv[3], argv[4], argv[5]) : renderShot("", "", "", "");
    if (argc >= 2 && std::strcmp(argv[1], "--clip") == 0)
        return argc == 4 ? renderFrames(argv[2], argv[3]) : renderFrames("", "");
    if (argc == 2 && std::strcmp(argv[1], "--tour") == 0) return renderFrames(nullptr, "cinematic");
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
