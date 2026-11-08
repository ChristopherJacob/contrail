// Browser simulator (6b): the CYD's core and screens in a Web Worker (site/sim-worker.js), on a place's recorded
// flights. The worker calls sim_start for a place, then sim_touch and sim_step about 30 times a second; sim_show,
// sim_theme and sim_alert come from the page's controls. sim_render draws a demo frame as the desktop's --render
// does, for the parity test (tools/sim_node_test.mjs). Built by tools/build_wasm.sh.
#include <emscripten/emscripten.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "RangeStorage.h"
#include "Replay.h"
#include "WasmGlue.h"
#include "alerts/AlertConfig.h"
#include "app/CoreApp.h"
#include "app/DemoRender.h"
#include "app/PanelBridge.h"
#include "assets/LogoStore.h"
#include "gfx/StripRenderer.h"
#include "map/MapPack.h"
#include "screens/ScreenApp.h"
#include "services/Log.h"

namespace {

// 16-row strips, as on the device (main_esp32.cpp), so damage and timing behave the same.
constexpr int kStripH = 16;
uint16_t bufA[240 * kStripH], bufB[240 * kStripH];

// The screen: strips into one frame, handed to the worker after a frame that pushed anything (post), or kept for
// sim_render.
class FrameDisplay : public hal::IDisplay {
   public:
    explicit FrameDisplay(bool post) : post_(post) {}
    std::vector<uint16_t> px = std::vector<uint16_t>(240 * 320);
    int width() const override { return 240; }
    int height() const override { return 320; }
    void pushRect(int x, int y, int w, int h, const uint16_t* p) override {
        for (int r = 0; r < h; ++r) std::copy(p + r * w, p + (r + 1) * w, px.begin() + (y + r) * 240 + x);
        dirty_ = true;
    }
    void endFrame() override {
        if (post_ && dirty_) hal::wasm::postFrame(px.data());
        dirty_ = false;
    }

   private:
    bool post_;
    bool dirty_ = false;
};

struct Place {
    std::string dir, mapUrl, logosUrl, theme;
    double lat = 0, lon = 0;
    int64_t start = 0;
    int replies = 0;
    int interval = 10;  // the recording's seconds between replies
};

struct Sim {
    Sim(const Place& p, const std::map<std::string, std::string>* keep);
    void apply();
    void step(float dt);

    Place place;
    hal::MemStore kv;
    hal::ReplayClock clock;
    hal::ReplayHttp http;
    app::CoreApp core;
    hal::RangeStorage sd;
    assets::LogoStore logos;
    basemap::MapPack map;
    std::unique_ptr<scr::ScreenApp> screens;  // after the stores it points at
    FrameDisplay display{true};
    gfx::StripRenderer renderer;
    std::string themeName;
    uint32_t lastNet = 0;
    uint32_t led = 0xFFFFFFFF;  // the LED colour last posted (r << 16 | g << 8 | b)
    bool dirty = true;          // a setting changed: theme, mode and panel values are applied again
    bool pressed = false;
    int tx = 0, ty = 0;
};

// keep: the settings of the place's previous run (the recording looped), else none.
Sim::Sim(const Place& p, const std::map<std::string, std::string>* keep)
    : place(p),
      clock(hal::wasm::nowMs, p.start),
      http(hal::wasm::fetchFile, p.dir, p.replies),
      core(http, clock, kv),
      sd({{assets::LogoStore::kPath, p.logosUrl}, {basemap::MapPack::kPath, p.mapUrl}}, hal::wasm::fileSize,
         hal::wasm::fetchRange),
      renderer(display, bufA, bufB, kStripH) {
    if (keep) kv.load(*keep);
    core.begin();
    svc::Settings& s = core.settings();
    char v[24];
    std::snprintf(v, sizeof v, "%.4f", p.lat);
    s.set(svc::key::kLat, v);
    std::snprintf(v, sizeof v, "%.4f", p.lon);
    s.set(svc::key::kLon, v);
    s.set(svc::key::kLocSet, "true");
    // Polls as the recording was made, so the replay runs in real time; set on every start, so a loop keeps it.
    s.set(svc::key::kPollComm, std::to_string(std::clamp(p.interval, 2, 60)).c_str());  // the setting's range
    if (!keep) s.set(svc::key::kTheme, p.theme.c_str());
    themeName = s.getEnum(svc::key::kTheme);
    logos.begin(&sd);
    map.begin(&sd);
    screens = std::make_unique<scr::ScreenApp>(scr::themeByName(themeName.c_str()));
    screens->configureAlerts(alerts::configFromSettings(s));
    screens->setLogos(&logos);
    screens->setMap(&map);
    screens->setMapClock(hal::wasm::nowUs);
    s.onChange([this](const char*) { dirty = true; });
    renderer.setLayers(screens->layers(), screens->layerCount());
    renderer.setMicrosSource(hal::wasm::nowUs);
}

// As main_sim.cpp's applyDisplay: a settings change (the page's theme switch, a panel edit) re-applies theme, mode
// and panel values.
void Sim::apply() {
    svc::Settings& s = core.settings();
    const std::string theme = s.getEnum(svc::key::kTheme);
    if (theme != themeName) themeName = theme, screens->useTheme(scr::themeByName(theme.c_str()));
    screens->configure(scr::parseDirectorMode(s.getEnum(svc::key::kScreenMode).c_str()),
                       uint32_t(s.getInt(svc::key::kCarouselS)) * 1000u);
    screens->configureAlerts(alerts::configFromSettings(s));
    screens->setPanelValues(app::panelValues(s));
}

// main_sim.cpp's interactive loop, one frame: the network every 100 ms (its thread's pace), the scene, touch,
// panel edits, the frame and the LED.
void Sim::step(float dt) {
    const uint32_t now = clock.millis();
    if (now - lastNet >= 100) core.networkTick(), lastNet = now;
    if (core.renderTick(screens->scene())) screens->sceneUpdated();
    screens->tick(now);
    screens->touch(pressed, tx, ty, now);
    for (scr::SettingEdit e; screens->takeSettingEdit(e);) app::applyEdit(core.settings(), e);
    if (dirty) dirty = false, apply();
    renderer.frame(dt);
    const alerts::LightOut lo = screens->alertLights(now);
    const uint32_t c = lo.on ? uint32_t(lo.led.r) << 16 | lo.led.g << 8 | lo.led.b : 0;
    if (c != led) led = c, hal::wasm::postLed(c >> 16, (c >> 8) & 0xFF, c & 0xFF);
}

std::unique_ptr<Sim> sim;

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE int sim_start(const char* dir, double lat, double lon, double start, int replies,
                                   int interval, const char* theme, const char* mapUrl, const char* logosUrl) {
    svc::logSink() = [](const char* line) { hal::wasm::postLog(line); };
    Place p;
    p.dir = dir, p.mapUrl = mapUrl, p.logosUrl = logosUrl, p.theme = theme;
    p.lat = lat, p.lon = lon, p.start = int64_t(start), p.replies = replies, p.interval = interval;
    sim.reset();  // before the next is made: one place's stores at a time
    sim = std::make_unique<Sim>(p, nullptr);
    return 0;
}

EMSCRIPTEN_KEEPALIVE void sim_step(double dt) {
    if (!sim) return;
    // The recording's end: the place again from its start, on the same screen, same settings. Not for a recording
    // that served nothing, which would be rebuilt every poll.
    if (sim->http.ended() && sim->http.served() > 0) {
        const Place p = sim->place;
        // The settings only: the stats (StatsBook's st_* keys) start afresh (spec §4), else every loop would count
        // the same flights again and no first sighting would come up twice.
        std::map<std::string, std::string> keep;
        for (const auto& [k, v] : sim->kv.all())
            if (sim->core.settings().find(k.c_str())) keep.emplace(k, v);
        const scr::ScreenId at = sim->screens->host().target();
        // A screen the user picked would be lost to the new director's own choice: its touch hold carries over
        // (restarted at full length), so the director adopts the screen as on the old run.
        const bool held = sim->screens->director().exploring(sim->clock.millis());
        sim.reset();
        sim = std::make_unique<Sim>(p, &keep);
        if (at < scr::ScreenId::Alert) sim->screens->host().showNow(at);  // an alert's takeover isn't restored
        if (held) sim->screens->director().noteTouch(sim->clock.millis());
    }
    sim->step(float(dt));
}

EMSCRIPTEN_KEEPALIVE void sim_touch(int pressed, int x, int y) {
    if (sim) sim->pressed = pressed != 0, sim->tx = x, sim->ty = y;
}

// The page's screen buttons: as a tap that opens the screen (the director hands control to the user for a minute).
EMSCRIPTEN_KEEPALIVE void sim_show(int screen) {
    if (!sim || screen < 0 || screen >= int(scr::ScreenId::Alert)) return;
    sim->screens->director().noteTouch(sim->clock.millis());
    sim->screens->host().show(scr::ScreenId(screen));
}

EMSCRIPTEN_KEEPALIVE void sim_theme(const char* name) {
    if (sim) sim->core.settings().set(svc::key::kTheme, name);
}

EMSCRIPTEN_KEEPALIVE void sim_alert() {
    if (sim) sim->screens->injectAlert(scr::AlertKind::Emergency, sim->clock.millis());
}

// A demo frame (app::renderDemo) for the parity test: the 240 x 320 RGB565 pixels, or null for an unknown name.
// The worker's Module.timeZone must be "UTC" (the default), as the desktop's --render sets TZ=UTC0.
EMSCRIPTEN_KEEPALIVE const uint16_t* sim_render(const char* scene, const char* screen, const char* theme) {
    static FrameDisplay shot(false);
    return app::renderDemo(scene, screen, theme, shot, bufA, bufB, kStripH) ? shot.px.data() : nullptr;
}

}  // extern "C"
