# Browser Simulator (6b) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Simulator page on the site runs the Contrail firmware, compiled to WebAssembly, on hourly recordings of the flights around five places.

**Architecture:** The firmware's `lib/core` is compiled with emscripten together with a new `lib/hal_wasm` and `src/main_wasm.cpp`, and runs in a Web Worker. There synchronous XHR lets the firmware's blocking `IHttp` and `IFile` calls work unchanged. `ReplayHttp` answers the community feed URLs with recorded adsb.lol replies and adsbdb URLs with recorded routes. `RangeStorage` reads `logos.pak` and a per-place `map.pak` by HTTP Range requests. The Pages workflow records the flights hourly with `tools/record_sky.py`, then publishes the build and the recordings with `tools/sim_site.py`.

**Tech Stack:** C++17, emscripten 7.0.0, PlatformIO native tests (Unity), Python 3.12 (stdlib only), plain JS (no build step), GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-11-07-contrail-browser-simulator-design.md`.

**Feasibility, checked 2026-11-07 on this Mac (emscripten 7.0.0 from Homebrew):**
- **Compiling:** all 78 `lib/core` sources compile unchanged.
- **Size:** a test build of the demo render is 680 KB of WASM.
- **Pixels:** under Node it drew all 14 site screenshots byte-identical to `program --render`, once one override was in. Emscripten's `localtime_r` takes the zone from JS `Date` (the host's zone) and ignores `TZ`. Overriding `_localtime_js` with a JS library that uses `Intl.DateTimeFormat` and a zone chosen by the page fixes it (Task 4). Do not use `setenv("TZ")` in the WASM build: it has no effect.
- **Include paths:** use `-I lib/core` only, not every subfolder: macOS's case-insensitive file system makes a folder-level `-I` shadow system headers.

**Conventions:**
- Comments state why, as in the surrounding code.
- Commit messages are "6b: …" and end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Work on the `simulator` branch.
- Run commands from the repository root unless a step says otherwise.
- In zsh, `$var` holding several words is not word-split: use arrays, or write the commands in bash scripts.

---

## File structure

| File | Responsibility |
|---|---|
| `firmware/lib/core/app/DemoRender.{h,cpp}` (new) | the demo frames behind `--render`, shared by the desktop and WASM builds |
| `firmware/src/main_sim.cpp` (modify) | `--render` calls `app::renderDemo` |
| `firmware/lib/hal_wasm/Replay.{h,cpp}` (new) | `ReplayHttp`, `ReplayClock`, `MemStore`: no emscripten, unit-tested natively |
| `firmware/lib/hal_wasm/RangeStorage.{h,cpp}` (new) | `RangeFile`, `RangeStorage`: no emscripten, unit-tested natively |
| `firmware/lib/hal_wasm/WasmGlue.{h,cpp}` (new) | emscripten only: sync XHR, Range reads, frame/LED/log to the worker |
| `firmware/lib/hal_wasm/localtime.js` (new) | emscripten JS library: `localtime_r` in the page's chosen IANA zone |
| `firmware/src/main_wasm.cpp` (new) | the `sim_*` exports the worker calls |
| `firmware/platformio.ini` (modify) | `hal_wasm` ignored by every env (the tests compile its sources themselves) |
| `firmware/test/test_replay/test_main.cpp` (new) | Replay tests |
| `firmware/test/test_range_storage/test_main.cpp` (new) | RangeStorage tests |
| `tools/build_wasm.sh` (new) | emscripten build to `<out>/contrail.js` + `contrail.wasm` |
| `tools/sim_node_test.mjs` (new) | pixel parity: WASM vs desktop `--render`, every site screenshot |
| `tools/sim_places.json` (new) | the five places |
| `tools/record_sky.py` (new) | the recorder, with `--selftest` |
| `tools/sim_site.py` (new) | `site/sim/`: the build, recordings, per-place maps, `places.json` |
| `site/sim.html`, `site/sim.js`, `site/sim-worker.js` (new) | the page |
| `site/style.css`, `site/index.html`, `site/install.html`, `site/sd.html` (modify) | styles, nav link, showcase button |
| `.github/workflows/ci.yml`, `.github/workflows/pages.yml` (modify) | the WASM job; hourly recording and publishing |
| `.gitignore`, `README.md`, the spec (modify) | ignores, docs, as built |

---

### Task 1: Share the demo render

`--render` in `main_sim.cpp` draws the site's screenshots. The WASM parity test must run *the same code*, so it moves into core as `app::renderDemo`. Then the parity test checks the compiler and libc, not two copies of the code.

**Files:**
- Create: `firmware/lib/core/app/DemoRender.h`, `firmware/lib/core/app/DemoRender.cpp`
- Modify: `firmware/src/main_sim.cpp` (remove `sceneBusyEmergency`, `backInTime` and `renderShot`'s body; lines ~76-165)

- [ ] **Step 1: Record the screenshots before the change**

```bash
cd firmware && pio run -e sim && cd ..
mkdir -p /tmp/contrail-before
for t in cinematic editorial; do for p in closest:busy radar:busy board:busy sky:night map:busy stats:stats alert:busy; do
  firmware/.pio/build/sim/program --render "${p##*:}" "${p%%:*}" "$t" "/tmp/contrail-before/${p%%:*}-$t.ppm"; done; done
ls /tmp/contrail-before | wc -l
```
Expected: `14`.

- [ ] **Step 2: Write `firmware/lib/core/app/DemoRender.h`**

```cpp
#pragma once
// The demo frames behind the simulator's --render (the site's screenshots) and the browser build's parity test
// (tools/sim_node_test.mjs): one demo scene on one screen in one theme, drawn in the golden tests' call order
// (test/common/GoldenRender.h: fill, sceneUpdated, showNow, 75 frames of 1/30 s, no tick), so a frame matches the
// goldens and is the same on every run. As their goldens do, the Map gets 70 s of trails (8 scene updates, as
// test_map_screen's map_busy) and the Sky 100 frames (sky_night: a beacon lit). The caller sets the time zone to
// UTC first: the scenes' clocks are UTC.
#include <cstdint>
#include <cstdio>

#include "hal.h"

namespace assets {
class LogoStore;
}
namespace basemap {
class MapPack;
}

namespace app {

// False, drawing nothing, when a name is unknown. bufA and bufB hold 240 x stripH pixels each. logos and map may be
// null (no card); they must outlive the call.
bool renderDemo(const char* scene, const char* screen, const char* theme, hal::IDisplay& display, uint16_t* bufA,
                uint16_t* bufB, int stripH, assets::LogoStore* logos = nullptr, basemap::MapPack* map = nullptr);
// The names renderDemo accepts, for a usage message.
void printDemoNames(std::FILE* out);

}  // namespace app
```

- [ ] **Step 3: Write `firmware/lib/core/app/DemoRender.cpp`**

The bodies are moved from `main_sim.cpp`, unchanged except that the names are checked here.

```cpp
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
    scr::SceneAlert& a = s.alert;
    a = scr::SceneAlert{};
    a.active = true, a.kind = scr::AlertKind::Emergency, a.live = true;
    a.flight = s.flights[0];
    a.startMs = 1000, a.endsMs = 31000;
    std::strcpy(a.flight.squawk, "7700"), a.flight.emergency = true;
}

// Every flight stepped `seconds` back along its track, as test_map_screen's backInTime.
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

}  // namespace

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
```

- [ ] **Step 4: Replace `renderShot` in `firmware/src/main_sim.cpp`**

Delete `sceneBusyEmergency`, `backInTime` and the old `renderShot`, including its comment block. Keep `CaptureDisplay`, `writePpm` and `settingsPath`. Put in its place:

```cpp
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
```

Add `#include "app/DemoRender.h"` to the includes. Remove `#include "screens/DemoScenes.h"` if nothing else uses it: check with `grep -n "scene[A-Z]" firmware/src/main_sim.cpp`.

- [ ] **Step 5: Build, and compare the screenshots with the ones from before**

```bash
cd firmware && pio run -e sim && cd ..
for t in cinematic editorial; do for p in closest:busy radar:busy board:busy sky:night map:busy stats:stats alert:busy; do
  firmware/.pio/build/sim/program --render "${p##*:}" "${p%%:*}" "$t" /tmp/contrail-after.ppm
  cmp -s /tmp/contrail-after.ppm "/tmp/contrail-before/${p%%:*}-$t.ppm" && echo same || echo "DIFF ${p%%:*} $t"; done; done | sort | uniq -c
firmware/.pio/build/sim/program --render nope closest cinematic /tmp/x.ppm; echo "exit $?"
```
Expected: `14 same`. Then the usage text listing the scenes, screens and themes, and `exit 2`.

- [ ] **Step 6: Make sure the device build still links**

Run: `cd firmware && pio run -e cyd2usb 2>&1 | tail -3`
Expected: `SUCCESS`. The new file is in core, so every env compiles it, and unused code is dropped at link.

- [ ] **Step 7: Commit**

```bash
git add firmware/lib/core/app/DemoRender.h firmware/lib/core/app/DemoRender.cpp firmware/src/main_sim.cpp
git commit -m "6b: the demo render moves into core, for the browser build's parity test

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: `ReplayHttp`, `ReplayClock`, `MemStore`

**Files:**
- Create: `firmware/lib/hal_wasm/Replay.h`, `firmware/lib/hal_wasm/Replay.cpp`, `firmware/test/test_replay/test_main.cpp`
- Modify: `firmware/platformio.ini` (`lib_ignore` in `[esp32_base]`, `[env:sim]` and `[env:test]`)

**Background:**
- **Feed URLs:** `CommunityProvider::poll` (`lib/core/providers/Providers.cpp:21-37`) GETs one of three host URLs. On a failed reply or parse it moves to the next host, so any of the three must get the recording.
- **Route URLs:** `AdsbdbClient::lookup` (`lib/core/providers/Routes.cpp:97-106`) GETs `https://api.adsbdb.com/v0/callsign/<cs>`, and treats a 404 as "unknown callsign".
- **Body handler:** it runs only for 2xx replies (`hal.h`).
- **Test pattern:** tests in this repo `#include` a HAL source directly, because the test env ignores the HAL libraries; see `test/test_native_storage/test_main.cpp`.

- [ ] **Step 1: Write the failing test `firmware/test/test_replay/test_main.cpp`**

```cpp
// The browser simulator's recorded data (hal_wasm/Replay): replies in order then the end, any community host,
// recorded routes and 404s, the clock from the recording's start, settings in memory. The env ignores hal_wasm (its
// glue needs emscripten), so this suite compiles the source itself.
#include <unity.h>

#include <map>
#include <string>

#include "../../lib/hal_wasm/Replay.cpp"

void setUp() {}
void tearDown() {}

static std::map<std::string, hal::Fetched> files;
static int fetches = 0;
static hal::Fetched fakeFetch(const std::string& path) {
    ++fetches;
    auto it = files.find(path);
    return it == files.end() ? hal::Fetched{404, ""} : it->second;
}

static std::string body;
static hal::HttpStatus get(hal::IHttp& http, const char* url) {
    body.clear();
    return http.get(url, nullptr, 0, [](hal::IByteSource& src) {
        for (int c; (c = src.read()) >= 0;) body += char(c);
    });
}

static void reset() {
    files = {{"sim/kfdk/000.json", {200, "r0"}},
             {"sim/kfdk/001.json", {200, "r1"}},
             {"sim/kfdk/routes.json", {200, R"({"UAL53":{"response":{"flightroute":{"callsign":"UAL53"}}},"N1":null})"}}};
    fetches = 0;
}

static void test_replies_in_order_then_the_end() {
    reset();
    hal::ReplayHttp http(fakeFetch, "sim/kfdk", 2);
    TEST_ASSERT_EQUAL(200, get(http, "https://api.adsb.lol/v2/point/39.4176/-77.3743/25").code);
    TEST_ASSERT_EQUAL_STRING("r0", body.c_str());
    TEST_ASSERT_FALSE(http.ended());
    TEST_ASSERT_EQUAL(200, get(http, "https://api.adsb.lol/v2/point/39.4176/-77.3743/25").code);
    TEST_ASSERT_EQUAL_STRING("r1", body.c_str());
    TEST_ASSERT_FALSE(http.ended());
    TEST_ASSERT_TRUE(get(http, "https://api.adsb.lol/v2/point/39.4176/-77.3743/25").code < 0);
    TEST_ASSERT_TRUE(http.ended());
    TEST_ASSERT_EQUAL(2, http.served());
}

static void test_every_community_host_gets_the_recording() {
    reset();
    hal::ReplayHttp http(fakeFetch, "sim/kfdk", 2);
    TEST_ASSERT_EQUAL(200, get(http, "https://opendata.adsb.fi/api/v3/lat/39.4/lon/-77.4/dist/25").code);
    TEST_ASSERT_EQUAL_STRING("r0", body.c_str());
    TEST_ASSERT_EQUAL(200, get(http, "https://api.airplanes.live/v2/point/39.4/-77.4/25").code);
    TEST_ASSERT_EQUAL_STRING("r1", body.c_str());
}

static void test_a_missing_reply_fails_the_poll() {
    reset();
    files.erase("sim/kfdk/001.json");
    hal::ReplayHttp http(fakeFetch, "sim/kfdk", 2);
    get(http, "https://api.adsb.lol/v2/point/1/2/25");
    TEST_ASSERT_EQUAL(404, get(http, "https://api.adsb.lol/v2/point/1/2/25").code);
    TEST_ASSERT_EQUAL_STRING("", body.c_str());  // no body handler for a non-2xx reply
}

static void test_routes_recorded_unknown_and_missing() {
    reset();
    hal::ReplayHttp http(fakeFetch, "sim/kfdk", 2);
    TEST_ASSERT_EQUAL(200, get(http, "https://api.adsbdb.com/v0/callsign/UAL53").code);
    TEST_ASSERT_EQUAL_STRING(R"({"response":{"flightroute":{"callsign":"UAL53"}}})", body.c_str());
    TEST_ASSERT_EQUAL(404, get(http, "https://api.adsbdb.com/v0/callsign/N1").code);
    TEST_ASSERT_EQUAL(404, get(http, "https://api.adsbdb.com/v0/callsign/DAL1").code);
    TEST_ASSERT_EQUAL(1, fetches);  // routes.json once
    TEST_ASSERT_EQUAL(0, http.served());  // routes are not polls
}

static void test_no_routes_file_means_every_route_is_unknown() {
    reset();
    files.erase("sim/kfdk/routes.json");
    hal::ReplayHttp http(fakeFetch, "sim/kfdk", 2);
    TEST_ASSERT_EQUAL(404, get(http, "https://api.adsbdb.com/v0/callsign/UAL53").code);
}

static void test_other_urls_fail_without_a_request() {
    reset();
    hal::ReplayHttp http(fakeFetch, "sim/kfdk", 2);
    TEST_ASSERT_TRUE(get(http, "https://opensky-network.org/api/states/all").code < 0);
    TEST_ASSERT_TRUE(http.post("https://x/", nullptr, 0, "text/plain", "", [](hal::IByteSource&) {}).code < 0);
    TEST_ASSERT_EQUAL(0, fetches);
}

static uint32_t fakeMs = 0;
static void test_clock_runs_from_the_recording_start() {
    fakeMs = 5000;
    hal::ReplayClock clock([] { return fakeMs; }, 1762500000);
    TEST_ASSERT_EQUAL_UINT32(0, clock.millis());
    TEST_ASSERT_EQUAL_INT64(1762500000, clock.epochSeconds());
    fakeMs = 5000 + 61500;
    TEST_ASSERT_EQUAL_UINT32(61500, clock.millis());
    TEST_ASSERT_EQUAL_INT64(1762500061, clock.epochSeconds());
    TEST_ASSERT_TRUE(clock.timeValid());
}

static void test_memstore() {
    hal::MemStore kv;
    char out[8];
    TEST_ASSERT_FALSE(kv.getString("theme", out, sizeof out));
    TEST_ASSERT_TRUE(kv.putString("theme", "editorial"));
    TEST_ASSERT_FALSE(kv.getString("theme", out, sizeof out));  // does not fit in 8 bytes
    char big[16];
    TEST_ASSERT_TRUE(kv.getString("theme", big, sizeof big));
    TEST_ASSERT_EQUAL_STRING("editorial", big);
    hal::MemStore copy;
    copy.load(kv.all());
    TEST_ASSERT_TRUE(copy.getString("theme", big, sizeof big));
    TEST_ASSERT_TRUE(kv.erase("theme"));
    TEST_ASSERT_FALSE(kv.getString("theme", big, sizeof big));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_replies_in_order_then_the_end);
    RUN_TEST(test_every_community_host_gets_the_recording);
    RUN_TEST(test_a_missing_reply_fails_the_poll);
    RUN_TEST(test_routes_recorded_unknown_and_missing);
    RUN_TEST(test_no_routes_file_means_every_route_is_unknown);
    RUN_TEST(test_other_urls_fail_without_a_request);
    RUN_TEST(test_clock_runs_from_the_recording_start);
    RUN_TEST(test_memstore);
    return UNITY_END();
}
```

Before writing the MemStore part, read how `hal::FileStore::getString` (`lib/hal_native/NativeHal.cpp`) handles a value too long for `out`. If it truncates rather than returning false, make `MemStore` do the same, and change the "does not fit" assertion to match. The two stores must behave alike.

- [ ] **Step 2: Ignore `hal_wasm` in every env, and run the test to see it fail**

In `firmware/platformio.ini`:
- `[esp32_base]`: `lib_ignore = hal_native` becomes `lib_ignore = hal_native, hal_wasm`.
- `[env:sim]`: `lib_ignore = hal_esp32` becomes `lib_ignore = hal_esp32, hal_wasm`.
- `[env:test]`: `lib_ignore = hal_esp32, hal_native` becomes `lib_ignore = hal_esp32, hal_native, hal_wasm`.

Run: `cd firmware && pio test -e test -f test_replay`
Expected: FAIL, because `Replay.cpp` doesn't exist.

- [ ] **Step 3: Write `firmware/lib/hal_wasm/Replay.h`**

```cpp
#pragma once
// The browser simulator's recorded data (6b spec §3-4), free of emscripten so the native tests build it:
// ReplayHttp answers the community feeds with a place's recorded adsb.lol replies and adsbdb with its recorded
// routes, ReplayClock runs from the recording's start, MemStore keeps the settings in memory.
#include <cstdint>
#include <functional>
#include <map>
#include <string>

#include "hal.h"

namespace hal {

struct Fetched {
    int status = 0;  // HTTP status, or negative when the request failed
    std::string body;
};
// A GET of a file of the site, relative to the worker ("sim/kfdk/000.json").
using Fetch = std::function<Fetched(const std::string& path)>;

class ReplayHttp : public IHttp {
   public:
    // dir: the place's folder ("sim/kfdk"); replies: how many NNN.json it holds.
    ReplayHttp(Fetch fetch, std::string dir, int replies);
    // A community feed URL (any of the three hosts, Providers.cpp) gets the next reply in order. After the last,
    // the next poll fails and ended() turns true: the caller loads the place again. An adsbdb callsign URL
    // (Routes.cpp) gets the recorded route, or 404 as adsbdb answers an unknown callsign. Anything else fails
    // without a request.
    HttpStatus get(const char* url, const Header* headers, int nHeaders, const BodyHandler& body) override;
    HttpStatus post(const char*, const Header*, int, const char*, const char*, const BodyHandler&) override {
        return {-1};
    }
    bool ended() const { return ended_; }
    int served() const { return served_; }

   private:
    void loadRoutes();
    Fetch fetch_;
    std::string dir_;
    int replies_;
    int served_ = 0;
    bool ended_ = false;
    bool routesLoaded_ = false;
    std::map<std::string, std::string> routes_;  // callsign → adsbdb's reply body; absent or null: 404
};

class ReplayClock : public IClock {
   public:
    // ms: a millisecond counter (performance.now() in the worker); the clock starts at 0 and at startEpoch.
    ReplayClock(std::function<uint32_t()> ms, int64_t startEpoch);
    uint32_t millis() const override { return ms_() - base_; }
    int64_t epochSeconds() const override { return start_ + millis() / 1000; }
    bool timeValid() const override { return true; }

   private:
    std::function<uint32_t()> ms_;
    uint32_t base_;
    int64_t start_;
};

class MemStore : public IKeyValueStore {
   public:
    bool getString(const char* key, char* out, size_t outLen) override;
    bool putString(const char* key, const char* value) override;
    bool erase(const char* key) override;
    // Kept across a reload of the place, so panel edits survive the recording's loop.
    const std::map<std::string, std::string>& all() const { return kv_; }
    void load(const std::map<std::string, std::string>& kv) { kv_ = kv; }

   private:
    std::map<std::string, std::string> kv_;
};

}  // namespace hal
```

- [ ] **Step 4: Write `firmware/lib/hal_wasm/Replay.cpp`**

```cpp
#include "Replay.h"

#include <ArduinoJson.h>

#include <cstdio>
#include <cstring>

namespace hal {
namespace {

// The community provider's hosts (Providers.cpp): on a failed poll it tries the next, so all of them replay.
const char* const kFeeds[] = {"https://api.adsb.lol/", "https://opendata.adsb.fi/", "https://api.airplanes.live/"};
const char kRoutes[] = "https://api.adsbdb.com/v0/callsign/";  // AdsbdbClient (Routes.cpp)

bool startsWith(const char* s, const char* prefix) { return std::strncmp(s, prefix, std::strlen(prefix)) == 0; }

HttpStatus answer(int status, const std::string& text, const BodyHandler& body) {
    HttpStatus st{status};
    if (st.ok()) {
        StringSource src(text.data(), text.size());
        body(src);
    }
    return st;
}

}  // namespace

ReplayHttp::ReplayHttp(Fetch fetch, std::string dir, int replies)
    : fetch_(std::move(fetch)), dir_(std::move(dir)), replies_(replies) {}

HttpStatus ReplayHttp::get(const char* url, const Header*, int, const BodyHandler& body) {
    for (const char* feed : kFeeds) {
        if (!startsWith(url, feed)) continue;
        if (served_ >= replies_) return ended_ = true, HttpStatus{-1};
        char path[32];
        std::snprintf(path, sizeof path, "/%03d.json", served_++);
        Fetched f = fetch_(dir_ + path);
        return answer(f.status, f.body, body);
    }
    if (startsWith(url, kRoutes)) {
        loadRoutes();
        auto it = routes_.find(url + sizeof kRoutes - 1);
        return it == routes_.end() ? HttpStatus{404} : answer(200, it->second, body);
    }
    return {-1};
}

void ReplayHttp::loadRoutes() {
    if (routesLoaded_) return;
    routesLoaded_ = true;
    Fetched f = fetch_(dir_ + "/routes.json");
    JsonDocument doc;
    if (f.status != 200 || deserializeJson(doc, f.body)) return;  // no routes: every lookup is unknown
    for (JsonPair p : doc.as<JsonObject>()) {
        if (p.value().isNull()) continue;
        std::string text;
        serializeJson(p.value(), text);
        routes_[p.key().c_str()] = std::move(text);
    }
}

ReplayClock::ReplayClock(std::function<uint32_t()> ms, int64_t startEpoch)
    : ms_(std::move(ms)), base_(ms_()), start_(startEpoch) {}

bool MemStore::getString(const char* key, char* out, size_t outLen) {
    auto it = kv_.find(key);
    if (it == kv_.end() || it->second.size() + 1 > outLen) return false;
    std::memcpy(out, it->second.c_str(), it->second.size() + 1);
    return true;
}

bool MemStore::putString(const char* key, const char* value) { return kv_[key] = value, true; }

bool MemStore::erase(const char* key) { return kv_.erase(key) > 0; }

}  // namespace hal
```

- [ ] **Step 5: Run the test to see it pass**

Run: `cd firmware && pio test -e test -f test_replay`
Expected: 8 tests, 0 failures.

- [ ] **Step 6: Commit**

```bash
git add firmware/lib/hal_wasm/Replay.h firmware/lib/hal_wasm/Replay.cpp firmware/test/test_replay firmware/platformio.ini
git commit -m "6b: ReplayHttp, ReplayClock and MemStore for the browser build

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: `RangeStorage`

**Files:**
- Create: `firmware/lib/hal_wasm/RangeStorage.h`, `firmware/lib/hal_wasm/RangeStorage.cpp`, `firmware/test/test_range_storage/test_main.cpp`

**Background:**
- **Open each path once:** `IStorage::open` (`hal.h`) returns a file the storage owns. `LogoStore::begin` and `MapPack::begin` each open their path once.
- **Small reads:** `readAt` is called with small reads (index entries, tiles, logo blobs), so reads go through a cache of 16 KB blocks, and a logo or a tile normally costs one request.

- [ ] **Step 1: Write the failing test `firmware/test/test_range_storage/test_main.cpp`**

```cpp
// The browser simulator's card (hal_wasm/RangeStorage): files read from the site in cached blocks by Range requests.
// The env ignores hal_wasm, so this suite compiles the source itself.
#include <unity.h>

#include <map>
#include <string>

#include "../../lib/hal_wasm/RangeStorage.cpp"

void setUp() {}
void tearDown() {}

static std::string data;  // the site's sim/map-kfdk.pak
static int requests = 0;
static bool failNext = false;

static int64_t fakeSize(const std::string& url) { return url == "sim/map-kfdk.pak" ? int64_t(data.size()) : -1; }
static bool fakeRange(const std::string& url, uint32_t offset, uint32_t n, void* out) {
    ++requests;
    if (failNext || url != "sim/map-kfdk.pak" || offset + n > data.size()) return failNext = false, false;
    std::memcpy(out, data.data() + offset, n);
    return true;
}

static hal::RangeStorage make() {
    data.resize(40000);
    for (size_t i = 0; i < data.size(); ++i) data[i] = char(i * 7 + i / 251);
    requests = 0;
    return hal::RangeStorage({{"/contrail/map.pak", "sim/map-kfdk.pak"}, {"/contrail/logos.pak", ""}}, fakeSize,
                             fakeRange);
}

static void test_reads_across_blocks() {
    hal::RangeStorage sd = make();
    hal::IFile* f = sd.open("/contrail/map.pak");
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_EQUAL_UINT32(40000, f->size());
    char buf[100];
    TEST_ASSERT_TRUE(f->readAt(16384 - 50, buf, sizeof buf));  // the end of block 0 and the start of block 1
    TEST_ASSERT_EQUAL_MEMORY(data.data() + 16384 - 50, buf, sizeof buf);
    TEST_ASSERT_EQUAL(2, requests);
}

static void test_blocks_are_cached() {
    hal::RangeStorage sd = make();
    hal::IFile* f = sd.open("/contrail/map.pak");
    char buf[10];
    TEST_ASSERT_TRUE(f->readAt(10, buf, sizeof buf));
    TEST_ASSERT_TRUE(f->readAt(1000, buf, sizeof buf));
    TEST_ASSERT_EQUAL(1, requests);
}

static void test_the_last_block_is_short() {
    hal::RangeStorage sd = make();
    hal::IFile* f = sd.open("/contrail/map.pak");
    char buf[16];
    TEST_ASSERT_TRUE(f->readAt(40000 - 16, buf, sizeof buf));
    TEST_ASSERT_EQUAL_MEMORY(data.data() + 40000 - 16, buf, sizeof buf);
    TEST_ASSERT_FALSE(f->readAt(40000 - 8, buf, sizeof buf));  // past the end
}

static void test_a_failed_request_fails_the_read_and_is_not_cached() {
    hal::RangeStorage sd = make();
    hal::IFile* f = sd.open("/contrail/map.pak");
    char buf[10];
    failNext = true;
    TEST_ASSERT_FALSE(f->readAt(10, buf, sizeof buf));
    TEST_ASSERT_TRUE(f->readAt(10, buf, sizeof buf));
    TEST_ASSERT_EQUAL_MEMORY(data.data() + 10, buf, sizeof buf);
}

static void test_missing_files() {
    hal::RangeStorage sd = make();
    TEST_ASSERT_TRUE(sd.mounted());
    TEST_ASSERT_FALSE(sd.writable());
    TEST_ASSERT_NULL(sd.open("/contrail/logos.pak"));  // no URL: no release's logos
    TEST_ASSERT_NULL(sd.open("/contrail/other.pak"));
    TEST_ASSERT_EQUAL_PTR(sd.open("/contrail/map.pak"), sd.open("/contrail/map.pak"));
}

static void test_the_cache_is_bounded() {
    hal::RangeStorage sd = make();
    data.resize(hal::RangeFile::kBlock * (hal::RangeFile::kBlocks + 2));
    hal::IFile* f = sd.open("/contrail/map.pak");
    char c;
    for (int b = 0; b < hal::RangeFile::kBlocks + 2; ++b) TEST_ASSERT_TRUE(f->readAt(b * hal::RangeFile::kBlock, &c, 1));
    requests = 0;
    TEST_ASSERT_TRUE(f->readAt(0, &c, 1));  // block 0 was the least recently used: fetched again
    TEST_ASSERT_EQUAL(1, requests);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_reads_across_blocks);
    RUN_TEST(test_blocks_are_cached);
    RUN_TEST(test_the_last_block_is_short);
    RUN_TEST(test_a_failed_request_fails_the_read_and_is_not_cached);
    RUN_TEST(test_missing_files);
    RUN_TEST(test_the_cache_is_bounded);
    return UNITY_END();
}
```

- [ ] **Step 2: Run it to see it fail**

Run: `cd firmware && pio test -e test -f test_range_storage`
Expected: FAIL, because `RangeStorage.cpp` doesn't exist.

- [ ] **Step 3: Write `firmware/lib/hal_wasm/RangeStorage.h`**

```cpp
#pragma once
// The browser simulator's "SD card" (6b spec §3): logos.pak and the place's map.pak read from the site by HTTP
// Range requests in 16 KB blocks, so a visitor downloads only what is drawn. Read-only. Free of emscripten: the
// worker's requests come in as functions (WasmGlue), the native tests pass fakes.
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "hal.h"

namespace hal {

// Reads n bytes at offset of the file at url into out; false when that failed.
using FetchRange = std::function<bool(const std::string& url, uint32_t offset, uint32_t n, void* out)>;
// The file's size, or -1 when it is missing.
using FetchSize = std::function<int64_t(const std::string& url)>;

class RangeFile : public IFile {
   public:
    static constexpr uint32_t kBlock = 16384;
    static constexpr int kBlocks = 64;  // 1 MB kept per file
    RangeFile(std::string url, uint32_t size, FetchRange fetch);
    uint32_t size() const override { return size_; }
    bool readAt(uint32_t offset, void* buf, uint32_t n) override;

   private:
    const uint8_t* block(uint32_t index);  // nullptr when the request failed
    struct Slot {
        int64_t index = -1;
        uint32_t used = 0;  // for least-recently-used replacement
        std::vector<uint8_t> data;
    };
    std::string url_;
    uint32_t size_;
    FetchRange fetch_;
    Slot slots_[kBlocks];
    uint32_t tick_ = 0;
};

class RangeStorage : public IStorage {
   public:
    // urls: card path → the site's URL ("/contrail/map.pak" → "sim/map-kfdk.pak"); an empty URL leaves that file
    // missing (no release, so no logos: the board's drawn fins).
    RangeStorage(std::map<std::string, std::string> urls, FetchSize size, FetchRange fetch);
    bool mounted() const override { return true; }
    // Opening a path that is already open returns the same file.
    IFile* open(const char* path) override;
    bool writable() const override { return false; }

   private:
    std::map<std::string, std::string> urls_;
    FetchSize size_;
    FetchRange fetch_;
    std::map<std::string, std::unique_ptr<RangeFile>> open_;
};

}  // namespace hal
```

- [ ] **Step 4: Write `firmware/lib/hal_wasm/RangeStorage.cpp`**

```cpp
#include "RangeStorage.h"

#include <algorithm>
#include <cstring>

namespace hal {

RangeFile::RangeFile(std::string url, uint32_t size, FetchRange fetch)
    : url_(std::move(url)), size_(size), fetch_(std::move(fetch)) {}

bool RangeFile::readAt(uint32_t offset, void* buf, uint32_t n) {
    if (uint64_t(offset) + n > size_) return false;
    uint8_t* out = static_cast<uint8_t*>(buf);
    while (n) {
        const uint8_t* b = block(offset / kBlock);
        if (!b) return false;
        uint32_t at = offset % kBlock, k = std::min(n, kBlock - at);
        std::memcpy(out, b + at, k);
        out += k, offset += k, n -= k;
    }
    return true;
}

const uint8_t* RangeFile::block(uint32_t index) {
    Slot* victim = &slots_[0];
    for (Slot& s : slots_) {
        if (s.index == index) return s.used = ++tick_, s.data.data();
        if (s.used < victim->used) victim = &s;
    }
    uint32_t start = index * kBlock, n = std::min(kBlock, size_ - start);
    victim->data.resize(n);
    victim->index = -1;
    if (!fetch_(url_, start, n, victim->data.data())) return nullptr;
    victim->index = index, victim->used = ++tick_;
    return victim->data.data();
}

RangeStorage::RangeStorage(std::map<std::string, std::string> urls, FetchSize size, FetchRange fetch)
    : urls_(std::move(urls)), size_(std::move(size)), fetch_(std::move(fetch)) {}

IFile* RangeStorage::open(const char* path) {
    auto o = open_.find(path);
    if (o != open_.end()) return o->second.get();
    auto u = urls_.find(path);
    if (u == urls_.end() || u->second.empty()) return nullptr;
    int64_t n = size_(u->second);
    if (n < 0 || n > int64_t(UINT32_MAX)) return nullptr;
    auto& f = open_[path];
    f = std::make_unique<RangeFile>(u->second, uint32_t(n), fetch_);
    return f.get();
}

}  // namespace hal
```

- [ ] **Step 5: Run the test to see it pass, then the whole suite**

Run: `cd firmware && pio test -e test -f test_range_storage && pio test -e test 2>&1 | tail -3`
Expected: 6 tests pass. Then the full suite reports every test suite passing.

- [ ] **Step 6: Commit**

```bash
git add firmware/lib/hal_wasm/RangeStorage.h firmware/lib/hal_wasm/RangeStorage.cpp firmware/test/test_range_storage
git commit -m "6b: RangeStorage, the browser build's SD card over Range requests

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: The WASM build, its glue, and the pixel parity test

**Files:**
- Create: `firmware/lib/hal_wasm/WasmGlue.h`, `firmware/lib/hal_wasm/WasmGlue.cpp`, `firmware/lib/hal_wasm/localtime.js`, `firmware/src/main_wasm.cpp`, `tools/build_wasm.sh`, `tools/sim_node_test.mjs`
- Modify: `.gitignore` (add `site/sim/`)

**Prerequisite:** `em++ --version` prints 7.0.0. On macOS: `brew install emscripten`; it is already installed on this Mac.

- [ ] **Step 1: Write `firmware/lib/hal_wasm/WasmGlue.h`**

```cpp
#pragma once
// The worker's side of the browser build's HAL (site/sim-worker.js), emscripten only: synchronous XHR for the
// site's files (allowed in a Web Worker), and the frame, the LED and log lines handed to the worker's callbacks
// (Module.onFrame, onLed, onLog).
#include <cstdint>
#include <string>

#include "Replay.h"

namespace hal::wasm {

Fetched fetchFile(const std::string& path);
int64_t fileSize(const std::string& url);
bool fetchRange(const std::string& url, uint32_t offset, uint32_t n, void* out);
void postFrame(const uint16_t* px);  // 240 x 320 RGB565, rows packed
void postLed(uint8_t r, uint8_t g, uint8_t b);
void postLog(const char* line);
uint32_t nowMs();
uint32_t nowUs();

}  // namespace hal::wasm
```

- [ ] **Step 2: Write `firmware/lib/hal_wasm/WasmGlue.cpp`**

```cpp
#include "WasmGlue.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>

#include <cstdlib>

// A GET of path: the status (negative when it could not be sent) and the body, malloc'd, in *body and *len.
EM_JS(int, js_fetch, (const char* path, uint8_t** body, int* len), {
    const xhr = new XMLHttpRequest();
    xhr.open("GET", UTF8ToString(path), false);
    xhr.responseType = "arraybuffer";
    try {
        xhr.send();
    } catch (e) {
        return -1;
    }
    const bytes = new Uint8Array(xhr.response || new ArrayBuffer(0));
    const p = _malloc(bytes.length + 1);
    HEAPU8.set(bytes, p);
    HEAPU32[body >> 2] = p;
    HEAP32[len >> 2] = bytes.length;
    return xhr.status;
});

// The file's size from a HEAD request, or -1.
EM_JS(double, js_size, (const char* url), {
    const xhr = new XMLHttpRequest();
    xhr.open("HEAD", UTF8ToString(url), false);
    try {
        xhr.send();
    } catch (e) {
        return -1;
    }
    const n = Number(xhr.getResponseHeader("Content-Length"));
    return xhr.status === 200 && Number.isFinite(n) ? n : -1;
});

// n bytes at offset into out by a Range request. A server that ignores Range (python -m http.server, when
// testing) answers 200 with the whole file: that is kept, once per URL.
EM_JS(int, js_range, (const char* urlPtr, double offset, int n, uint8_t* out), {
    const url = UTF8ToString(urlPtr);
    const whole = (globalThis.contrailWhole ??= {});
    let bytes = whole[url];
    if (!bytes) {
        const xhr = new XMLHttpRequest();
        xhr.open("GET", url, false);
        xhr.responseType = "arraybuffer";
        xhr.setRequestHeader("Range", "bytes=" + offset + "-" + (offset + n - 1));
        try {
            xhr.send();
        } catch (e) {
            return 0;
        }
        if (xhr.status === 206) {
            const b = new Uint8Array(xhr.response);
            if (b.length !== n) return 0;
            HEAPU8.set(b, out);
            return 1;
        }
        if (xhr.status !== 200) return 0;
        bytes = whole[url] = new Uint8Array(xhr.response);
    }
    if (bytes.length < offset + n) return 0;
    HEAPU8.set(bytes.subarray(offset, offset + n), out);
    return 1;
});

EM_JS(void, js_frame, (const uint16_t* px), { Module["onFrame"]?.(HEAPU16.subarray(px >> 1, (px >> 1) + 240 * 320)); });
EM_JS(void, js_led, (int r, int g, int b), { Module["onLed"]?.(r, g, b); });
EM_JS(void, js_log, (const char* line), { Module["onLog"]?.(UTF8ToString(line)); });

namespace hal::wasm {

Fetched fetchFile(const std::string& path) {
    uint8_t* body = nullptr;
    int len = 0;
    Fetched f;
    f.status = js_fetch(path.c_str(), &body, &len);
    if (body) f.body.assign(reinterpret_cast<char*>(body), size_t(len)), std::free(body);
    return f;
}

int64_t fileSize(const std::string& url) { return int64_t(js_size(url.c_str())); }

bool fetchRange(const std::string& url, uint32_t offset, uint32_t n, void* out) {
    return js_range(url.c_str(), double(offset), int(n), static_cast<uint8_t*>(out)) != 0;
}

void postFrame(const uint16_t* px) { js_frame(px); }
void postLed(uint8_t r, uint8_t g, uint8_t b) { js_led(r, g, b); }
void postLog(const char* line) { js_log(line); }
uint32_t nowMs() { return uint32_t(emscripten_get_now()); }
uint32_t nowUs() { return uint32_t(emscripten_get_now() * 1000.0); }

}  // namespace hal::wasm
#endif
```

If the link step (Step 6) reports `_malloc` as undefined inside `js_fetch`, add `EM_JS_DEPS(contrail_glue, "$UTF8ToString,malloc");` after the include, and `_malloc,_free` to `EXPORTED_FUNCTIONS` (already in the script).

- [ ] **Step 3: Write `firmware/lib/hal_wasm/localtime.js`**

```js
// localtime_r for the browser build. Emscripten's takes the zone from JS Date (the visitor's) and ignores TZ; the
// simulator shows a place's own time, so this one uses the IANA zone the worker sets (Module.timeZone,
// "America/New_York"), "UTC" until then (the parity test's demo frames, whose clocks are UTC). tm_isdst stays 0:
// the firmware reads only the fields and the offset.
addToLibrary({
  _localtime_js__i53abi: true,
  _localtime_js: (time, tmPtr) => {
    const ms = time * 1000;
    if (!Number.isFinite(ms)) return 1;
    const zone = Module["timeZone"] || "UTC";
    const cache = (globalThis.contrailZones ??= {});
    const f = (cache[zone] ??= new Intl.DateTimeFormat("en-US", {
      timeZone: zone, hourCycle: "h23", year: "numeric", month: "numeric", day: "numeric",
      hour: "numeric", minute: "numeric", second: "numeric",
    }));
    const p = {};
    for (const x of f.formatToParts(new Date(ms))) p[x.type] = x.value;
    const y = +p.year, mo = +p.month - 1, d = +p.day, h = +p.hour, mi = +p.minute, s = +p.second;
    const local = Date.UTC(y, mo, d, h, mi, s);
    const off = Math.round((local - Math.floor(time) * 1000) / 1000);
    {{{ makeSetValue('tmPtr', C_STRUCTS.tm.tm_sec, 's', 'i32') }}};
    {{{ makeSetValue('tmPtr', C_STRUCTS.tm.tm_min, 'mi', 'i32') }}};
    {{{ makeSetValue('tmPtr', C_STRUCTS.tm.tm_hour, 'h', 'i32') }}};
    {{{ makeSetValue('tmPtr', C_STRUCTS.tm.tm_mday, 'd', 'i32') }}};
    {{{ makeSetValue('tmPtr', C_STRUCTS.tm.tm_mon, 'mo', 'i32') }}};
    {{{ makeSetValue('tmPtr', C_STRUCTS.tm.tm_year, 'y - 1900', 'i32') }}};
    {{{ makeSetValue('tmPtr', C_STRUCTS.tm.tm_wday, 'new Date(local).getUTCDay()', 'i32') }}};
    {{{ makeSetValue('tmPtr', C_STRUCTS.tm.tm_yday, '((local - Date.UTC(y, 0, 1)) / 86400000) | 0', 'i32') }}};
    {{{ makeSetValue('tmPtr', C_STRUCTS.tm.tm_gmtoff, 'off', LONG_TYPE) }}};
    {{{ makeSetValue('tmPtr', C_STRUCTS.tm.tm_isdst, '0', 'i32') }}};
    return 0;
  },
});
```

- [ ] **Step 4: Write `firmware/src/main_wasm.cpp`**

Read `main_sim.cpp`'s interactive loop (the `else` branch near its end) first: `Sim::step` below is the same sequence, minus SDL.

```cpp
// Browser simulator (6b): the CYD's core and screens in a Web Worker (site/sim-worker.js), on a place's recorded
// flights. The worker calls sim_start for a place, then sim_touch and sim_step about 30 times a second; sim_show,
// sim_theme and sim_alert come from the page's controls. sim_render draws a demo frame as the desktop's --render
// does, for the parity test (tools/sim_node_test.mjs). Built by tools/build_wasm.sh.
#include <emscripten/emscripten.h>

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
                                   const char* theme, const char* mapUrl, const char* logosUrl) {
    svc::logSink() = [](const char* line) { hal::wasm::postLog(line); };
    Place p;
    p.dir = dir, p.mapUrl = mapUrl, p.logosUrl = logosUrl, p.theme = theme;
    p.lat = lat, p.lon = lon, p.start = int64_t(start), p.replies = replies;
    sim.reset();  // before the next is made: one place's stores at a time
    sim = std::make_unique<Sim>(p, nullptr);
    return 0;
}

EMSCRIPTEN_KEEPALIVE void sim_step(double dt) {
    if (!sim) return;
    if (sim->http.ended()) {  // the recording's end: the place again from its start, on the same screen, same settings
        const Place p = sim->place;
        const std::map<std::string, std::string> keep = sim->kv.all();
        const scr::ScreenId at = sim->screens->host().target();
        sim.reset();
        sim = std::make_unique<Sim>(p, &keep);
        sim->screens->host().showNow(at);
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
```

Check these names against the headers before building, and fix any that differ:
- **Settings:** `svc::Settings::getEnum` and `getInt` (`services/Settings.h`), and that `Settings::onChange` takes a `std::function<void(const char*)>`, as `main_sim.cpp` uses it.
- **Map pack:** `basemap::MapPack::begin(IStorage*)`.
- **Renderer:** `gfx::StripRenderer::setMicrosSource(uint32_t (*)())`; it is a function pointer, and `hal::wasm::nowUs` is a plain function.
- **Map clock:** `scr::ScreenApp::setMapClock(uint32_t (*)())`.
- **Alerts:** `alerts::configFromSettings`.

- [ ] **Step 5: Write `tools/build_wasm.sh`**

```bash
#!/usr/bin/env bash
# The browser simulator's WebAssembly build (6b): firmware/lib/core, lib/hal_wasm and src/main_wasm.cpp with
# emscripten (em++ on PATH, 7.0.0), into <out-dir>/contrail.js and contrail.wasm:
#   tools/build_wasm.sh <out-dir>
# ArduinoJson is PlatformIO's copy for the sim env (installed with pio when missing). Objects go to
# firmware/.pio/build/wasm.
set -euo pipefail
if [ $# -ne 1 ]; then
    echo "usage: $0 <out-dir>" >&2
    exit 2
fi
here="$(cd "$(dirname "$0")" && pwd)"
fw="$here/../firmware"
out="$1"
mkdir -p "$out"
json="$fw/.pio/libdeps/sim/ArduinoJson/src"
[ -d "$json" ] || (cd "$fw" && pio pkg install -e sim >/dev/null)

# The version as firmware/scripts/version.py makes it: the release tag, else `git describe` with a dot, else dev.
version=dev
if [ "${GITHUB_REF_TYPE:-}" = tag ] && [[ "${GITHUB_REF_NAME:-}" == v* ]]; then
    version="${GITHUB_REF_NAME#v}"
elif tag="$(git -C "$fw" describe --tags --dirty --always 2>/dev/null)" && [[ "$tag" == *.* ]]; then
    version="${tag#v}"
fi

# -I lib/core only, as PlatformIO does: a -I per folder would let core's headers shadow system ones on a
# case-insensitive file system. -ffp-contract=off as the sim and test envs, so the pixels match theirs.
flags=(-std=gnu++17 -O2 -ffp-contract=off -Wall -Wno-unused-function -DCONTRAIL_WASM
       "-DCONTRAIL_VERSION=\"$version\"" -I "$fw/lib/hal" -I "$fw/lib/core" -I "$fw/lib/hal_wasm" -I "$json")
obj="$fw/.pio/build/wasm"
rm -rf "$obj"
mkdir -p "$obj"
export obj
sources=$(cd "$fw" && find lib/core lib/hal_wasm -name '*.cpp' | sort; echo src/main_wasm.cpp)
# One em++ per source, in parallel; each object named after its path.
(cd "$fw" && printf '%s\n' $sources | xargs -P "$(getconf _NPROCESSORS_ONLN)" -I{} \
    sh -c 'em++ "$@" -c "{}" -o "$obj/$(echo "{}" | tr / _).o"' sh "${flags[@]}")
em++ "${flags[@]}" "$obj"/*.o -o "$out/contrail.js" \
    --no-entry -sMODULARIZE=1 -sEXPORT_NAME=createContrail -sENVIRONMENT=worker,node \
    -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 \
    -sEXPORTED_FUNCTIONS=_sim_start,_sim_step,_sim_touch,_sim_show,_sim_theme,_sim_alert,_sim_render,_malloc,_free \
    -sEXPORTED_RUNTIME_METHODS=ccall,HEAPU16 \
    --js-library "$fw/lib/hal_wasm/localtime.js"
size=$(wc -c <"$out/contrail.wasm" | tr -d ' ')
echo "contrail.wasm: $size bytes ($version)"
# The spec's budget (§2): 2 MB before compression.
[ "$size" -le 2000000 ] || { echo "contrail.wasm is over 2 MB" >&2; exit 1; }
```

Run: `chmod +x tools/build_wasm.sh`

- [ ] **Step 6: Build**

Run: `tools/build_wasm.sh /tmp/contrail-wasm`
Expected: `contrail.wasm: <about 700000-900000> bytes (dev)`, and `/tmp/contrail-wasm/contrail.js` exists.

If a compile fails, fix that file. If the failure is in `lib/core`, look first at the `-I` note above, then at whether the code needs a `CONTRAIL_ESP32` guard it lacks. Do not change core's behaviour for the native builds.

- [ ] **Step 7: Write `tools/sim_node_test.mjs`**

```js
// The browser simulator's WebAssembly build draws every site screenshot exactly as the desktop simulator does
// (6b spec §8): the same app::renderDemo, compiled twice.
//   node tools/sim_node_test.mjs <dir with contrail.js> <desktop sim binary>
import { execFileSync } from "node:child_process";
import fs from "node:fs";
import { createRequire } from "node:module";
import os from "node:os";
import path from "node:path";

const [dir, sim] = process.argv.slice(2);
if (!dir || !sim) {
  console.error("usage: node tools/sim_node_test.mjs <dir with contrail.js> <desktop sim binary>");
  process.exit(2);
}
const require = createRequire(import.meta.url);
const createContrail = require(path.resolve(dir, "contrail.js"));
const m = await createContrail();
m.timeZone = "UTC";  // the desktop's --render sets TZ=UTC0

// tools/screenshots.sh's screen:scene pairs
const pairs = "closest:busy radar:busy board:busy sky:night map:busy stats:stats alert:busy".split(" ");
const header = Buffer.from("P6\n240 320\n255\n");
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "contrail-sim-"));
const shot = path.join(tmp, "desktop.ppm");
let bad = 0;
for (const theme of ["cinematic", "editorial"]) {
  for (const pair of pairs) {
    const [screen, scene] = pair.split(":");
    const ptr = m.ccall("sim_render", "number", ["string", "string", "string"], [scene, screen, theme]);
    if (!ptr) {
      console.log(`${screen} ${theme}: unknown name`);
      ++bad;
      continue;
    }
    const px = m.HEAPU16.subarray(ptr >> 1, (ptr >> 1) + 240 * 320);
    execFileSync(sim, ["--render", scene, screen, theme, shot]);
    const ppm = fs.readFileSync(shot);
    let diff = 0;
    if (!ppm.subarray(0, header.length).equals(header)) diff = -1;
    else {
      for (let i = 0; i < px.length; ++i) {
        const v = px[i], r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;  // gfx::unpack565
        const o = header.length + 3 * i;
        if (ppm[o] !== ((r << 3) | (r >> 2)) || ppm[o + 1] !== ((g << 2) | (g >> 4)) || ppm[o + 2] !== ((b << 3) | (b >> 2))) ++diff;
      }
    }
    console.log(`${screen} ${theme}: ${diff === 0 ? "same" : diff < 0 ? "bad desktop PPM" : `${diff} pixels differ`}`);
    if (diff) ++bad;
  }
}
fs.rmSync(tmp, { recursive: true, force: true });
console.log(bad ? `${bad} of ${pairs.length * 2} differ` : `all ${pairs.length * 2} the same`);
process.exit(bad ? 1 : 0);
```

- [ ] **Step 8: Run the parity test**

Run: `cd firmware && pio run -e sim && cd .. && node tools/sim_node_test.mjs /tmp/contrail-wasm firmware/.pio/build/sim/program`
Expected: 14 lines ending in `same`, then `all 14 the same`.

Also check that the result doesn't depend on the host's time zone: `TZ=Asia/Kolkata node tools/sim_node_test.mjs /tmp/contrail-wasm firmware/.pio/build/sim/program | tail -1`
Expected: `all 14 the same`. The desktop sim forces UTC, and the WASM build uses `Module.timeZone`.

- [ ] **Step 9: Ignore the build output, and commit**

Add `site/sim/` to `.gitignore`, under the existing `site/mapdata/` line.

```bash
chmod +x tools/build_wasm.sh
git add firmware/lib/hal_wasm/WasmGlue.h firmware/lib/hal_wasm/WasmGlue.cpp firmware/lib/hal_wasm/localtime.js \
  firmware/src/main_wasm.cpp tools/build_wasm.sh tools/sim_node_test.mjs .gitignore
git commit -m "6b: the WebAssembly build, its worker glue, and the pixel parity test

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: The places and the recorder

**Files:**
- Create: `tools/sim_places.json`, `tools/record_sky.py`

**Background:**
- **Feed reply:** adsb.lol's `/v2/point/<lat>/<lon>/<nm>` returns `{"ac": [{"hex", "flight", "lat", "lon", ...}], "now": <ms>, ...}`. `flight` is the callsign, padded with spaces.
- **Route lookups:** the firmware looks routes up for the 8 nearest flights (`CoreApp.h` `kEnrichCount`), by the callsign without its padding. Check how the readsb parser trims it (`lib/core/providers/ReadsbParser.cpp`) and do the same in `callsign()`.
- **Style:** tools in this repo are stdlib-only Python, with `--selftest` (see `tools/build_map.py`, `tools/map_regions.py`).

- [ ] **Step 1: Write `tools/sim_places.json`**

```json
[
  {"id": "kfdk", "name": "Frederick, Maryland", "airport": "KFDK", "lat": 39.4176, "lon": -77.3743, "zone": "America/New_York"},
  {"id": "kjfk", "name": "New York JFK", "airport": "KJFK", "lat": 40.6413, "lon": -73.7781, "zone": "America/New_York"},
  {"id": "egll", "name": "London Heathrow", "airport": "EGLL", "lat": 51.4700, "lon": -0.4543, "zone": "Europe/London"},
  {"id": "eham", "name": "Amsterdam Schiphol", "airport": "EHAM", "lat": 52.3105, "lon": 4.7683, "zone": "Europe/Amsterdam"},
  {"id": "ksfo", "name": "San Francisco SFO", "airport": "KSFO", "lat": 37.6189, "lon": -122.375, "zone": "America/Los_Angeles"}
]
```

- [ ] **Step 2: Write `tools/record_sky.py`, self-test first**

```python
#!/usr/bin/env python3
"""Recorded flights for the browser simulator (6b spec §4).

    tools/record_sky.py <places.json> <out-dir> [--replies N]   record every place, in parallel
    tools/record_sky.py --selftest

For each place, <out-dir>/<id>/ holds 000.json, 001.json ...: adsb.lol's replies, one every 10 s (the board's
default community poll) for its default 25 nm radius, unchanged. routes.json maps each callsign the board would look
up to adsbdb's reply (null when adsbdb doesn't know it), and index.json says what was recorded and when. A place
whose recording fails leaves no folder; the exit status is 1 only when no place was recorded.
"""
import argparse
import json
import math
import os
import shutil
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

REPLIES = 60      # 10 minutes
INTERVAL_S = 10   # CoreSettings.cpp: kPollComm's default
RADIUS_NM = 25    # CoreSettings.cpp: kRadius's default
NEAREST = 12      # the board looks routes up for its 8 nearest (CoreApp.h kEnrichCount); a margin for movement
FEED = "https://api.adsb.lol/v2/point/{lat:.4f}/{lon:.4f}/{radius}"
ROUTE = "https://api.adsbdb.com/v0/callsign/{cs}"
USER_AGENT = "Contrail-recorder (+https://github.com/ChristopherJacob/contrail)"


class Spacer:
    """At least `gap` seconds between requests to one host, across threads."""

    def __init__(self, gap, clock=time.monotonic, sleep=time.sleep):
        self.gap, self.clock, self.sleep = gap, clock, sleep
        self.lock = threading.Lock()
        self.last = -math.inf

    def wait(self):
        with self.lock:
            delay = self.last + self.gap - self.clock()
            if delay > 0:
                self.sleep(delay)
            self.last = self.clock()


def http_get(url, spacer):
    """(status, body); status -1 when the request could not be made."""
    spacer.wait()
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT, "Accept": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            return r.status, r.read()
    except urllib.error.HTTPError as e:
        return e.code, b""
    except (urllib.error.URLError, OSError):
        return -1, b""


def callsign(ac):
    return (ac.get("flight") or "").strip().upper()


def nearest_callsigns(body, lat, lon, n=NEAREST):
    """The callsigns of the n flights nearest home in one feed reply, nearest first."""
    k = math.cos(math.radians(lat))
    seen = []
    for ac in json.loads(body).get("ac", []):
        cs = callsign(ac)
        if not cs or not isinstance(ac.get("lat"), (int, float)) or not isinstance(ac.get("lon"), (int, float)):
            continue
        seen.append((60 * math.hypot(ac["lat"] - lat, (ac["lon"] - lon) * k), cs))
    out = []
    for _, cs in sorted(seen):
        if cs not in out:
            out.append(cs)
    return out[:n]


class RecordError(Exception):
    pass


def record_place(place, out_dir, replies, get, feed, routes_spacer, clock=time.time, sleep=time.sleep):
    """Records one place into out_dir/<id> (built in <id>.tmp, then moved). Raises RecordError."""
    final = os.path.join(out_dir, place["id"])
    tmp = final + ".tmp"
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    try:
        start = int(clock())
        url = FEED.format(lat=place["lat"], lon=place["lon"], radius=RADIUS_NM)
        wanted = []
        for k in range(replies):
            began = clock()
            status, body = get(url, feed)
            if status != 200:
                status, body = get(url, feed)  # one retry
            if status != 200:
                raise RecordError(f"{place['id']}: reply {k}: HTTP {status}")
            try:
                for cs in nearest_callsigns(body, place["lat"], place["lon"]):
                    if cs not in wanted:
                        wanted.append(cs)
            except (ValueError, AttributeError) as e:
                raise RecordError(f"{place['id']}: reply {k}: not a feed reply ({e})")
            with open(os.path.join(tmp, f"{k:03d}.json"), "wb") as f:
                f.write(body)
            if k + 1 < replies:
                sleep(max(0.0, INTERVAL_S - (clock() - began)))
        routes = {}
        for cs in wanted:  # after the replies, so the lookups don't delay them
            status, body = get(ROUTE.format(cs=urllib.parse.quote(cs)), routes_spacer)
            if status == 200:
                try:
                    routes[cs] = json.loads(body)
                except ValueError:
                    pass  # the board would get no route either
            elif status == 404:
                routes[cs] = None
        with open(os.path.join(tmp, "routes.json"), "w") as f:
            json.dump(routes, f, separators=(",", ":"), sort_keys=True)
        index = dict(place, start=start, interval=INTERVAL_S, replies=replies)
        with open(os.path.join(tmp, "index.json"), "w") as f:
            json.dump(index, f, indent=1)
        shutil.rmtree(final, ignore_errors=True)
        os.replace(tmp, final)
        return index
    except BaseException:
        shutil.rmtree(tmp, ignore_errors=True)
        raise


def record_all(places, out_dir, replies, get=http_get, sleep=time.sleep):
    """Every place at once, each in its own thread; returns the ids recorded."""
    os.makedirs(out_dir, exist_ok=True)
    feed, routes = Spacer(1.0, sleep=sleep), Spacer(0.5, sleep=sleep)
    done, threads = [], []

    def run(place):
        try:
            record_place(place, out_dir, replies, get, feed, routes, sleep=sleep)
            done.append(place["id"])
        except RecordError as e:
            print(f"warning: {e}", file=sys.stderr)

    for place in places:
        t = threading.Thread(target=run, args=(place,))
        t.start()
        threads.append(t)
    for t in threads:
        t.join()
    return [p["id"] for p in places if p["id"] in done]


def selftest():
    place = {"id": "kfdk", "name": "Frederick, Maryland", "airport": "KFDK", "lat": 39.4, "lon": -77.4,
             "zone": "America/New_York"}
    reply = json.dumps({"ac": [
        {"hex": "a", "flight": "UAL53   ", "lat": 39.5, "lon": -77.4},   # 6 nm
        {"hex": "b", "flight": "N1 ", "lat": 39.41, "lon": -77.4},       # nearest
        {"hex": "c", "flight": "", "lat": 39.4, "lon": -77.4},           # no callsign
        {"hex": "d", "flight": "DAL1", "lat": 41.0, "lon": -77.4},       # far
        {"hex": "e", "flight": "SWA2"},                                  # no position
    ]}).encode()
    assert nearest_callsigns(reply, 39.4, -77.4) == ["N1", "UAL53", "DAL1"], nearest_callsigns(reply, 39.4, -77.4)
    assert nearest_callsigns(reply, 39.4, -77.4, 1) == ["N1"]

    calls = []
    t = [1000.0]
    clock = lambda: t[0]

    def sleep(s):
        t[0] += s

    def get(url, spacer):
        calls.append(url)
        if "adsb.lol" in url:
            return 200, reply
        if url.endswith("/UAL53"):
            return 200, b'{"response":{"flightroute":{"callsign":"UAL53"}}}'
        if url.endswith("/N1"):
            return 404, b""
        return 500, b""  # DAL1: adsbdb failing leaves the callsign out

    with tempfile.TemporaryDirectory() as d:
        sp = Spacer(0)
        index = record_place(place, d, 3, get, sp, sp, clock=clock, sleep=sleep)
        got = sorted(os.listdir(os.path.join(d, "kfdk")))
        assert got == ["000.json", "001.json", "002.json", "index.json", "routes.json"], got
        assert open(os.path.join(d, "kfdk", "001.json"), "rb").read() == reply
        routes = json.load(open(os.path.join(d, "kfdk", "routes.json")))
        assert routes == {"N1": None, "UAL53": {"response": {"flightroute": {"callsign": "UAL53"}}}}, routes
        assert index["start"] == 1000 and index["replies"] == 3 and index["interval"] == 10 and index["zone"]
        assert calls[0] == "https://api.adsb.lol/v2/point/39.4000/-77.4000/25", calls[0]
        assert sum("adsb.lol" in c for c in calls) == 3  # each callsign looked up once, after the replies
        assert calls[3:] == ["https://api.adsbdb.com/v0/callsign/N1", "https://api.adsbdb.com/v0/callsign/UAL53",
                             "https://api.adsbdb.com/v0/callsign/DAL1"], calls[3:]
        assert t[0] == 1020.0, t[0]  # two 10 s gaps, none after the last reply

        def failing(url, spacer):
            return (503, b"") if url.endswith("/25") and failing.n >= 1 else (200, reply)
        failing.n = 0

        def counting(url, spacer):
            r = failing(url, spacer)
            failing.n += "adsb.lol" in url
            return r
        try:
            record_place(dict(place, id="eham"), d, 3, counting, sp, sp, clock=clock, sleep=sleep)
            raise AssertionError("a failing feed must raise")
        except RecordError:
            pass
        assert sorted(os.listdir(d)) == ["kfdk"], os.listdir(d)  # no eham, no eham.tmp

        assert record_all([place, dict(place, id="egll")], d, 2, get=get, sleep=lambda s: None) == ["kfdk", "egll"]
    print("record_sky selftest: ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("places", nargs="?")
    ap.add_argument("out", nargs="?")
    ap.add_argument("--replies", type=int, default=REPLIES, help="replies per place (10 s apart)")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.places or not a.out:
        ap.error("places and out are required")
    with open(a.places) as f:
        places = json.load(f)
    done = record_all(places, a.out, a.replies)
    print(f"recorded {len(done)} of {len(places)} places: {' '.join(done) or 'none'}")
    sys.exit(0 if done else 1)


if __name__ == "__main__":
    main()
```

- [ ] **Step 3: Run the self-test**

Run: `python3 -I tools/record_sky.py --selftest`
Expected: `record_sky selftest: ok`. A failing assertion means a bug in the code, not in the test: fix the code.

- [ ] **Step 4: Record for real, briefly**

Run: `python3 -I tools/record_sky.py tools/sim_places.json /tmp/contrail-rec --replies 3`
Expected: `recorded 5 of 5 places: kfdk kjfk egll eham ksfo`, after about 20 s plus the route lookups. Then `ls /tmp/contrail-rec/kjfk` lists `000.json 001.json 002.json index.json routes.json`. And `python3 -c "import json; print(len(json.load(open('/tmp/contrail-rec/kjfk/routes.json'))))"` prints a number between 1 and 36.

- [ ] **Step 5: Commit**

```bash
chmod +x tools/record_sky.py
git add tools/sim_places.json tools/record_sky.py
git commit -m "6b: the recorder and its five places

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: The simulator's files on the site

**Files:**
- Create: `tools/sim_site.py`

- [ ] **Step 1: Write `tools/sim_site.py`**

```python
#!/usr/bin/env python3
"""The browser simulator's files on the site (6b spec §5), after tools/sd_site.sh (it reads <site>/mapdata):

    tools/sim_site.py <site> <recordings>

<site>/sim/ gets contrail.js and contrail.wasm (tools/build_wasm.sh), each recorded place's folder, its map
(map-<id>.pak, from the site's map regions) and places.json: the recorded places in tools/sim_places.json's order,
and whether the site has logos.pak (from the latest release). Places without a recording are left out; with none,
the page says no recording is available.
"""
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    site, recordings = sys.argv[1:]
    out = os.path.join(site, "sim")
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    subprocess.run([os.path.join(HERE, "build_wasm.sh"), out], check=True)
    with open(os.path.join(HERE, "sim_places.json")) as f:
        order = [p["id"] for p in json.load(f)]
    places = []
    for pid in order:
        index = os.path.join(recordings, pid, "index.json")
        if not os.path.exists(index):
            print(f"{pid}: no recording", file=sys.stderr)
            continue
        with open(index) as f:
            place = json.load(f)
        shutil.copytree(os.path.join(recordings, pid), os.path.join(out, pid))
        subprocess.run([sys.executable, "-I", os.path.join(HERE, "build_map.py"), "--regions",
                        os.path.join(site, "mapdata"), "--lat", str(place["lat"]), "--lon", str(place["lon"]),
                        "--out", os.path.join(out, f"map-{pid}.pak")], check=True)
        places.append(place)
    logos = os.path.exists(os.path.join(site, "firmware", "logos.pak"))
    with open(os.path.join(out, "places.json"), "w") as f:
        json.dump({"logos": logos, "places": places}, f, indent=1)
    print(f"sim: {len(places)} places, logos {'yes' if logos else 'no'}")


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Run it against the short recording from Task 5**

`site/mapdata` must exist first. If it doesn't, run `tools/sd_site.sh site` (it needs the map-source cache under `tools/.cache/map`; that takes a few minutes the first time).

Run: `python3 -I tools/sim_site.py site /tmp/contrail-rec && ls site/sim && du -sh site/sim`
Expected:
- The output ends `sim: 5 places, logos yes`, or `no` without `site/firmware/logos.pak`.
- `ls` lists `contrail.js contrail.wasm egll eham kfdk kjfk ksfo map-egll.pak map-eham.pak map-kfdk.pak map-kjfk.pak map-ksfo.pak places.json`.
- `du` shows tens of MB at most.

- [ ] **Step 3: Commit**

```bash
chmod +x tools/sim_site.py
git add tools/sim_site.py
git commit -m "6b: the simulator's files on the site

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: The page

**Files:**
- Create: `site/sim.html`, `site/sim.js`, `site/sim-worker.js`
- Modify: `site/style.css` (a "Simulator" section before `/* Footer */`), and the nav in `site/index.html`, `site/install.html` and `site/sd.html`. Also the showcase hero's actions and `README.md`.

- [ ] **Step 1: Write `site/sim-worker.js`**

```js
// The browser simulator's worker (6b spec §2): runs the firmware (sim/contrail.js) on a place's recording and hands
// its frames to sim.js. In: {start: place, logos, theme}, {touch: [down, x, y]}, {show: screen}, {theme: name},
// {alert: true}. Out: {ready: true}, {started: id}, {frame: Uint16Array}, {led: [r, g, b]},
// {error: "load" | "crash", what}.
importScripts("sim/contrail.js");

const FRAME_MS = 1000 / 30;
let m = null, timer = 0, last = 0;
// A tap shorter than a frame still reaches the firmware: the press is held until a step has seen it.
const touch = { down: false, x: 0, y: 0, seen: true, upPending: false };

function fail(error, what) {
  clearTimeout(timer);
  timer = 0;
  postMessage({ error, what });
}

function exists(url) {
  const xhr = new XMLHttpRequest();
  xhr.open("HEAD", url, false);
  try {
    xhr.send();
  } catch {
    return false;
  }
  return xhr.status === 200;
}

function step() {
  const now = performance.now();
  try {
    m._sim_touch(touch.down ? 1 : 0, touch.x, touch.y);
    touch.seen = true;
    if (touch.upPending) touch.down = false, touch.upPending = false;
    m._sim_step(Math.min(0.1, (now - last) / 1000));
  } catch (e) {
    return fail("crash", String(e && e.message || e));
  }
  last = now;
  timer = setTimeout(step, Math.max(0, FRAME_MS - (performance.now() - now)));
}

function start({ start: p, logos, theme }) {
  clearTimeout(timer);
  timer = 0;
  if (!exists(`sim/${p.id}/000.json`)) return fail("load", `the ${p.name} recording`);
  m.timeZone = p.zone;
  m.ccall("sim_start", "number", ["string", "number", "number", "number", "number", "string", "string", "string"],
          [`sim/${p.id}`, p.lat, p.lon, p.start, p.replies, theme, `sim/map-${p.id}.pak`, logos ? "firmware/logos.pak" : ""]);
  postMessage({ started: p.id });
  last = performance.now();
  step();
}

onmessage = ({ data: d }) => {
  if (!m) return;
  if (d.start) start(d);
  else if (d.touch) {
    const [down, x, y] = d.touch;
    touch.x = x, touch.y = y;
    if (down && !touch.down) Object.assign(touch, { down: true, seen: false, upPending: false });
    else if (!down && touch.down) touch.seen ? (touch.down = false) : (touch.upPending = true);
  } else if (d.show !== undefined) m._sim_show(d.show);
  else if (d.theme) m.ccall("sim_theme", null, ["string"], [d.theme]);
  else if (d.alert) m._sim_alert();
};

createContrail({
  locateFile: (f) => `sim/${f}`,
  onFrame: (px) => {
    const f = px.slice();
    postMessage({ frame: f }, [f.buffer]);
  },
  onLed: (r, g, b) => postMessage({ led: [r, g, b] }),
  onLog: (line) => console.debug(line),
}).then((mod) => {
  m = mod;
  postMessage({ ready: true });
}, () => postMessage({ error: "load", what: "the simulator" }));
```

`importScripts` failing (a missing `contrail.js`) makes the worker's `onerror` fire on the page. `sim.js` handles that.

- [ ] **Step 2: Write `site/sim.js`**

```js
// Simulator page (6b spec §6): the screen on a canvas, the place, theme and screen controls, and the worker that runs
// the firmware (sim-worker.js). Every request is for a file of this site.
const $ = (id) => document.getElementById(id);
const canvas = $("screen"), ctx = canvas.getContext("2d"), img = ctx.createImageData(240, 320);
const place = $("place"), status = $("status"), recorded = $("recorded"), failed = $("failed"), led = $("led");
const retry = $("retry");
let worker = null, places = [], logos = false, current = null;

if (!("Worker" in window) || typeof WebAssembly !== "object") {
  $("sim").hidden = true;
  $("unsupported").hidden = false;
}

// gfx::unpack565
function draw(px) {
  const out = img.data;
  for (let i = 0, j = 0; i < px.length; ++i, j += 4) {
    const v = px[i], r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
    out[j] = (r << 3) | (r >> 2);
    out[j + 1] = (g << 2) | (g >> 4);
    out[j + 2] = (b << 3) | (b >> 2);
    out[j + 3] = 255;
  }
  ctx.putImageData(img, 0, 0);
}

function showError(text, canRetry) {
  failed.replaceChildren(text);
  if (!canRetry) {
    failed.append(" ", Object.assign(document.createElement("a"), {
      href: "https://github.com/ChristopherJacob/contrail/issues", textContent: "Report it on GitHub" }), ".");
  }
  failed.hidden = false;
  retry.hidden = false;
  retry.textContent = canRetry ? "Try again" : "Restart";
  status.textContent = "";
}

function recordedLine(p) {
  const at = new Intl.DateTimeFormat(undefined, { timeZone: p.zone, hour: "numeric", minute: "2-digit" })
    .format(new Date(p.start * 1000));
  const mins = Math.max(0, Math.round((Date.now() / 1000 - p.start) / 60));
  const ago = mins < 2 ? "just now" : mins < 120 ? `${mins} minutes ago` : `${Math.round(mins / 60)} hours ago`;
  return `${p.name}: recorded at ${at} local time, ${ago}. The recording loops every ${Math.round(p.replies * p.interval / 60)} minutes; a board shows live flights over your own home.`;
}

function theme() {
  return document.querySelector('input[name="theme"]:checked').value;
}

function startPlace(id) {
  current = places.find((p) => p.id === id) || places[0];
  failed.hidden = retry.hidden = true;
  recorded.textContent = recordedLine(current);
  worker.postMessage({ start: current, logos, theme: theme() });
}

function boot() {
  failed.hidden = retry.hidden = true;
  status.textContent = "Loading the simulator…";
  worker?.terminate();
  worker = new Worker("sim-worker.js");
  worker.onerror = (e) => {
    e.preventDefault();
    showError("Couldn't load the simulator. Check the connection and try again.", true);
  };
  worker.onmessage = ({ data: d }) => {
    if (d.frame) draw(d.frame);
    else if (d.led) led.style.background = d.led.some(Boolean) ? `rgb(${d.led.join(",")})` : "";
    else if (d.started) status.textContent = "";
    else if (d.ready) startPlace(place.value);
    else if (d.error === "load") showError(`Couldn't load ${d.what}. Check the connection and try again.`, true);
    else if (d.error) showError(`The simulator stopped: ${d.what}.`, false);
  };
}

async function init() {
  try {
    const r = await fetch("sim/places.json");
    if (!r.ok) throw new Error(r.status);
    ({ places, logos } = await r.json());
  } catch {
    return showError("Couldn't load the list of places. Check the connection and try again.", true);
  }
  if (!places.length) {
    status.textContent = "";
    failed.textContent = "No recording is available right now; try again in an hour.";
    failed.hidden = false;
    return;
  }
  place.replaceChildren(...places.map((p) => new Option(`${p.name} (${p.airport})`, p.id)));
  boot();
}

place.addEventListener("change", () => worker && startPlace(place.value));
for (const r of document.querySelectorAll('input[name="theme"]'))
  r.addEventListener("change", () => worker?.postMessage({ theme: theme() }));
for (const b of document.querySelectorAll("[data-screen]"))
  b.addEventListener("click", () => worker?.postMessage({ show: Number(b.dataset.screen) }));
$("alert").addEventListener("click", () => worker?.postMessage({ alert: true }));
retry.addEventListener("click", () => (places.length ? boot() : init()));

// The canvas is the touch screen: pointer positions in screen pixels.
function at(e) {
  const r = canvas.getBoundingClientRect();
  return [Math.min(239, Math.max(0, Math.floor((e.clientX - r.left) * 240 / r.width))),
          Math.min(319, Math.max(0, Math.floor((e.clientY - r.top) * 320 / r.height)))];
}
canvas.addEventListener("pointerdown", (e) => {
  canvas.setPointerCapture(e.pointerId);
  worker?.postMessage({ touch: [1, ...at(e)] });
});
canvas.addEventListener("pointermove", (e) => {
  if (e.buttons) worker?.postMessage({ touch: [1, ...at(e)] });
});
for (const type of ["pointerup", "pointercancel"])
  canvas.addEventListener(type, (e) => worker?.postMessage({ touch: [0, ...at(e)] }));

if (!$("sim").hidden) init();
```

- [ ] **Step 3: Write `site/sim.html`**

The header and footer are as in `sd.html`. The nav adds Simulator, marked current here.

```html
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="referrer" content="no-referrer">
<title>Contrail simulator</title>
<meta name="description" content="Try Contrail in your browser: the real firmware, compiled to WebAssembly, on recorded flights over five airports.">
<link rel="icon" href="favicon.svg" type="image/svg+xml">
<link rel="stylesheet" href="style.css">
<script type="module" src="sim.js"></script>
</head>
<body>
<header class="top">
  <div class="wrap">
    <a class="brand" href="./"><img src="favicon.svg" alt="">Contrail</a>
    <nav aria-label="Site">
      <a href="./">Showcase</a>
      <a href="sim.html" aria-current="page">Simulator</a>
      <a href="install.html">Install</a>
      <a href="sd.html">SD card</a>
      <a href="https://github.com/ChristopherJacob/contrail">GitHub</a>
    </nav>
  </div>
</header>

<main>
  <section>
    <div class="wrap">
      <h1>Simulator</h1>
      <p class="lead">The real Contrail firmware, compiled to WebAssembly and running in your browser, on flights
        recorded over five airports in the last hour. Tap and swipe the screen as you would the board.</p>

      <div id="sim" class="sim">
        <div class="sim-device">
          <canvas id="screen" width="240" height="320" aria-label="The simulated screen: tap or swipe it"></canvas>
          <span class="led" id="led" title="The board's LED"></span>
        </div>
        <div class="sim-controls">
          <label class="field" for="place">Place
            <select class="input" id="place"></select></label>
          <fieldset class="seg">
            <legend>Theme</legend>
            <label><input type="radio" name="theme" value="cinematic" checked> Cinematic Sky</label>
            <label><input type="radio" name="theme" value="editorial"> Bold Editorial</label>
          </fieldset>
          <fieldset class="screens">
            <legend>Screen</legend>
            <button class="btn ghost" type="button" data-screen="0">Closest</button>
            <button class="btn ghost" type="button" data-screen="1">Radar</button>
            <button class="btn ghost" type="button" data-screen="2">Board</button>
            <button class="btn ghost" type="button" data-screen="3">Sky</button>
            <button class="btn ghost" type="button" data-screen="4">Map</button>
            <button class="btn ghost" type="button" data-screen="5">Stats</button>
          </fieldset>
          <button class="btn ghost" id="alert" type="button">Show an alert</button>
          <p class="status" id="status" aria-live="polite">Loading the simulator…</p>
          <p class="note warn" id="failed" role="alert" hidden></p>
          <button class="btn" id="retry" type="button" hidden>Try again</button>
          <p class="muted" id="recorded"></p>
          <p class="muted">Swipe sideways to change screens, down for the quick settings, and tap a plane to
            select it. Ready for the real thing? <a href="install.html">Install Contrail</a> on a board.</p>
        </div>
      </div>
      <p class="note warn" id="unsupported" hidden>The simulator needs a browser with Web Workers and WebAssembly,
        such as a current Chrome, Edge, Firefox or Safari.</p>
    </div>
  </section>
</main>

<footer>
  <div class="wrap">
    <p>Contrail is free software under the <a href="https://github.com/ChristopherJacob/contrail/blob/main/LICENSE">GPL-3.0</a>.</p>
    <p>Airline logos are trademarks of their respective owners and are shown only to identify airlines; Contrail
      is not affiliated with any airline.</p>
    <p>Flight data: <a href="https://adsb.lol/">adsb.lol</a> (<a href="https://opendatacommons.org/licenses/odbl/">ODbL</a>);
      routes: <a href="https://www.adsbdb.com/">adsbdb</a>. Map data: Natural Earth, OurAirports, GeoNames.</p>
  </div>
</footer>
</body>
</html>
```

Check the swipe directions in the help text against `lib/core/screens/Gesture.cpp` and `ScreenApp.cpp`: which swipe opens the panel, and what a tap does. Correct the sentence if it differs.

- [ ] **Step 4: Add the styles to `site/style.css`, before `/* Footer */`**

```css
/* Simulator */
.sim { display: grid; grid-template-columns: auto 1fr; gap: 32px; align-items: start; }
.sim-device {
    position: relative; width: 480px; max-width: 100%; aspect-ratio: 3 / 4; border-radius: 18px; overflow: hidden;
    background: #000; box-shadow: 0 0 0 10px #10142b, 0 0 0 11px #2b3670, 0 30px 80px rgba(0, 0, 0, .6);
}
.sim-device canvas { display: block; width: 100%; height: 100%; image-rendering: pixelated; touch-action: none; cursor: pointer; }
.led {
    position: absolute; right: -24px; top: 12px; width: 10px; height: 10px; border-radius: 50%;
    background: #1b2040; box-shadow: 0 0 6px currentColor;
}
.sim-controls { display: grid; gap: 16px; align-content: start; }
.sim-controls fieldset { border: 0; padding: 0; margin: 0; display: flex; flex-wrap: wrap; gap: 8px 16px; }
.sim-controls legend { color: var(--soft); font-size: 15px; margin-bottom: 6px; padding: 0; }
.screens .btn { padding: 8px 14px; font-size: 15px; }
.seg label { color: var(--soft); }
.sim-controls .status { margin: 0; }
```

The LED sits just outside the device frame, and `overflow: hidden` would clip it. So place the `.led` span as a sibling of the device, in a wrapper with `position: relative`, or move `overflow: hidden` to the canvas. Check it in the browser (Task 9) and adjust.

Then in the existing `@media (max-width: 760px)` block, add:

```css
    .sim { grid-template-columns: 1fr; }
    .sim-device { width: 100%; }
```

- [ ] **Step 5: Add the nav link and the showcase button**

- **Nav:** in `site/index.html`, `site/install.html` and `site/sd.html`, add `<a href="sim.html">Simulator</a>` right after the Showcase link.
- **Hero:** in `site/index.html`'s hero `.actions`, add `<a class="btn ghost" href="sim.html">Try it in your browser</a>` between the two existing buttons.
- **README:** add a short "Simulator in the browser" paragraph beside the site's other pages, describing:
  - the link, https://christopherjacob.github.io/contrail/sim.html;
  - recorded flights, refreshed hourly;
  - building it locally: `tools/record_sky.py tools/sim_places.json /tmp/rec --replies 6`, then `tools/sim_site.py site /tmp/rec`, then serve `site/`.

Also add the new tools to README's Layout list, as the 6c tools are listed.

- [ ] **Step 6: Commit**

```bash
git add site/sim.html site/sim.js site/sim-worker.js site/style.css site/index.html site/install.html site/sd.html README.md
git commit -m "6b: the Simulator page

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Workflows

**Files:**
- Modify: `.github/workflows/ci.yml` (a new `simulator` job), `.github/workflows/pages.yml` (schedule, emscripten, recording, `sim_site.py`)

- [ ] **Step 1: Add the `simulator` job to `ci.yml`, after `pack-builder`**

```yaml
  simulator:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          fetch-depth: 0
      - uses: actions/setup-python@v5
        with:
          python-version: "3.12"
      - uses: actions/setup-node@v4
        with:
          node-version: "22"
      - name: Cache PlatformIO
        uses: actions/cache@v4
        with:
          path: |
            ~/.platformio
            ~/.cache/pip
          key: pio-${{ runner.os }}-${{ hashFiles('firmware/platformio.ini') }}
      - name: Install PlatformIO, SDL2 and curl
        run: |
          pip install platformio==6.2.0 -r tools/requirements.txt
          sudo apt-get update && sudo apt-get install -y libsdl2-dev libcurl4-openssl-dev
      - uses: mymindstorm/setup-emsdk@v14
        with:
          version: 7.0.0
          actions-cache-folder: emsdk-cache
      - name: Desktop simulator
        working-directory: firmware
        run: pio run -e sim
      - name: WebAssembly build
        run: tools/build_wasm.sh "$RUNNER_TEMP/sim"
      - name: The browser build draws the same pixels
        run: node tools/sim_node_test.mjs "$RUNNER_TEMP/sim" firmware/.pio/build/sim/program
      - name: Recorder self-test
        run: python -I tools/record_sky.py --selftest
```

Before relying on `mymindstorm/setup-emsdk@v14`, check that it exists and accepts `version` and `actions-cache-folder`, using `gh api repos/mymindstorm/setup-emsdk/releases/latest --jq .tag_name` and its README. If emsdk has no `7.0.0` tag, use the emsdk release that matches `em++ --version` on this Mac, and note the version in the spec's "As built".

- [ ] **Step 2: Change `pages.yml`**

- **Schedule:** under `on:` add

```yaml
  # The simulator's recordings (6b): an hour old at most.
  schedule:
    - cron: "17 * * * *"
```

- **Emscripten:** after the `Install PlatformIO, SDL2 and curl` step add

```yaml
      - uses: mymindstorm/setup-emsdk@v14
        with:
          version: 7.0.0
          actions-cache-folder: emsdk-cache
```

- **Recording and publishing:** after the `Latest release's install images and logos` step, which provides `site/firmware/logos.pak`, add the steps below. A failed recording still deploys the site; a failed build doesn't.

```yaml
      # About 10 minutes: 60 replies 10 s apart for each place, at once, then their routes. A failed place is left
      # out; with none recorded, the page says so and the rest of the site still deploys.
      - name: Record the simulator's flights
        run: python -I tools/record_sky.py tools/sim_places.json "$RUNNER_TEMP/recordings" || echo "no recordings"
      - name: Simulator
        run: python -I tools/sim_site.py site "$RUNNER_TEMP/recordings"
```

- **Timeout:** add `timeout-minutes: 30` to the `build` job.

- **Python version:** `sim_site.py` runs `build_map.py --regions` with `sys.executable`, which is the 3.12 from `setup-python`. Nothing else needed.

- [ ] **Step 3: Check the YAML**

Run: `python3 -c "import yaml,sys; [yaml.safe_load(open(f)) for f in sys.argv[1:]]; print('ok')" .github/workflows/ci.yml .github/workflows/pages.yml`
Expected: `ok`. Without PyYAML, run `pip install pyyaml` first.

- [ ] **Step 4: Commit**

```bash
git add .github/workflows/ci.yml .github/workflows/pages.yml
git commit -m "6b: CI builds and checks the browser build; Pages records hourly and publishes the simulator

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: Check the page in the browser, and the as-built notes

**Files:**
- Modify: `docs/superpowers/specs/2026-11-07-contrail-browser-simulator-design.md` (status, and a §9 "As built"), `docs/HARDWARE_TESTING.md` (nothing to do on the board for 6b; leave it unless something comes up)

- [ ] **Step 1: Serve the site locally, with Range support**

Run in the background: `npx --yes http-server@14.1.1 site -p 8417 -c-1`. It supports Range requests, as GitHub Pages does. Then open http://localhost:8417/sim.html in the browser pane.

- [ ] **Step 2: Check, and fix anything that fails**

- **Loading:** the first frame appears within a few seconds, and nothing is red in the console.
- **Places:** each place loads, and planes move. Its line says when it was recorded, in that place's time.
- **Themes:** Cinematic Sky and Bold Editorial switch the screen.
- **Screen buttons:** all six show their screen.
- **Touch:**
  - a tap selects a plane;
  - a sideways swipe changes screens;
  - the quick-settings panel opens and closes, and a change in it (units, say) takes effect.
- **Alert:** "Show an alert" brings up the emergency takeover, and the LED dot pulses red.
- **Requests:** the network panel shows only `localhost:8417` requests. `logos.pak` and `map-*.pak` are fetched as several `206` responses, not whole files.
- **Loop seam:** with a short recording (`--replies 3`, about 30 s), the place starts again on the same screen and with the same theme.
- **Phone width (375 px):** the canvas fits, and the controls stack below it.

Use `read_console_messages` and `read_network_requests` from the browser tools; take screenshots of the Map and the Closest screen for the summary.

- [ ] **Step 3: Record what was built**

Set the spec's Status to "Built on `simulator`; checked in the browser". Add a "## 9. As built" section noting:
- **Build:** `tools/build_wasm.sh`, not a PlatformIO env, and why: PlatformIO's native platform uses the host compiler.
- **Site script:** `tools/sim_site.py`, not `.sh`.
- **Time zones:** the IANA zones and the `_localtime_js` override, in place of `TZ`.
- **Loop seam:** the place is loaded again at the loop, so trails and stats start afresh rather than jumping.
- **Routes:** looked up for the 12 nearest flights per reply, after the replies.
- **Numbers:** the WASM size, the first-frame time, and the bytes downloaded per place.
- **Emscripten:** the version pinned in CI.

- [ ] **Step 4: Run every check once more**

```bash
cd firmware && pio test -e test 2>&1 | tail -2 && pio run -e sim -e cyd2usb 2>&1 | grep -E "SUCCESS|FAILED" && cd ..
tools/build_wasm.sh /tmp/contrail-wasm && node tools/sim_node_test.mjs /tmp/contrail-wasm firmware/.pio/build/sim/program | tail -1
python3 -I tools/record_sky.py --selftest
```
Expected: the tests pass, both envs say `SUCCESS`, and the last two lines are `all 14 the same` and `record_sky selftest: ok`.

- [ ] **Step 5: Commit**

```bash
git add docs/superpowers/specs/2026-11-07-contrail-browser-simulator-design.md
git commit -m "6b: as built

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
