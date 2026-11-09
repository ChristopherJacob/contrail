# Contrail 6d — Animated Previews: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The showcase page's hero plays a tour of the screens and its grid cards loop their screens, every frame drawn by the firmware's renderer.

**Architecture:** A new `app/DemoClip` draws a screen's loop or the tour and hands frames to a callback; `main_sim` streams them to stdout; `tools/previews.sh` pipes them into ffmpeg; `site/previews.js` lays the videos over the existing lazy stills.

**Tech Stack:** C++17 (PlatformIO native, Unity), bash, ffmpeg/libx264, plain JS and CSS, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-11-09-contrail-animated-previews-design.md`.

## Ground rules

- Work on the worktree branch `claude/elastic-napier-7775d5`. Never commit on main, merge or push without the developer's say-so.
- Commands from `firmware/`: `pio test -e test`, `pio run -e sim`. One pio command at a time.
- Commits end with a blank line, then `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## File map

| File | Task | What |
|---|---|---|
| `firmware/lib/core/app/DemoRender.{h,cpp}` | 1 | expose `addDemoEmergency`, `backInTime` |
| `firmware/lib/core/app/DemoClip.{h,cpp}`, `firmware/test/test_demo_clip/test_main.cpp` | 1, 2 | clips and tour, tests |
| `firmware/src/main_sim.cpp` | 3 | `--clip`, `--tour` |
| `tools/previews.sh` | 4 | encode, size limits |
| `site/index.html`, `site/previews.js`, `site/style.css` | 5 | the page |
| `.github/workflows/pages.yml`, `README.md`, the spec | 6 | workflow, docs, as-built |

---

### Task 1: Screen clips

**Files:** Modify `firmware/lib/core/app/DemoRender.h`, `firmware/lib/core/app/DemoRender.cpp`; create `firmware/lib/core/app/DemoClip.h`, `firmware/lib/core/app/DemoClip.cpp`, `firmware/test/test_demo_clip/test_main.cpp`.

- [ ] **Step 1: Write the failing tests** in `firmware/test/test_demo_clip/test_main.cpp`

```cpp
#include <unity.h>

#include <algorithm>
#include <vector>

#include "app/DemoClip.h"

void setUp() {}
void tearDown() {}

using Frame = std::vector<uint16_t>;
using Frames = std::vector<Frame>;
static uint16_t bufA[240 * 16], bufB[240 * 16];

static Frames clip(const char* screen, const char* theme = "cinematic") {
    Frames out;
    int n = app::renderClip(screen, theme, [&](const uint16_t* px) { out.emplace_back(px, px + 240 * 320); }, bufA, bufB, 16);
    TEST_ASSERT_EQUAL(int(out.size()), n);
    return out;
}

static int changed(const Frame& a, const Frame& b) {
    int n = 0;
    for (size_t i = 0; i < a.size(); ++i) n += a[i] != b[i];
    return n;
}

// The step from the last frame back to the first looks like any other step of the clip.
static void assertLoopCloses(const Frames& f) {
    int biggest = 0;
    for (size_t i = 0; i + 1 < f.size(); ++i) biggest = std::max(biggest, changed(f[i], f[i + 1]));
    TEST_ASSERT_GREATER_THAN(0, biggest);
    TEST_ASSERT_LESS_OR_EQUAL(2 * biggest, changed(f.back(), f.front()));
}

void test_radar_is_one_turn_and_closes() {
    Frames f = clip("radar");
    TEST_ASSERT_EQUAL(120, int(f.size()));
    assertLoopCloses(f);
}

void test_alert_closes_through_the_crossfade() {
    Frames f = clip("alert");
    TEST_ASSERT_EQUAL(90, int(f.size()));
    assertLoopCloses(f);
}

void test_map_closes_through_the_crossfade() {
    Frames f = clip("map");
    TEST_ASSERT_EQUAL(240, int(f.size()));
    assertLoopCloses(f);
}

// A callsign's pixels on the top row: the rows re-sort as the nearest flight flies away.
void test_board_rows_re_sort() {
    Frames f = clip("board");
    TEST_ASSERT_EQUAL(240, int(f.size()));
    assertLoopCloses(f);
    int diff = 0;
    for (int y = 80; y < 97; ++y)
        for (int x = 45; x < 100; ++x) diff += f[0][y * 240 + x] != f[200][y * 240 + x];
    TEST_ASSERT_GREATER_THAN(50, diff);
}

void test_two_runs_are_identical() {
    Frames a = clip("radar"), b = clip("radar");
    for (size_t i = 0; i < a.size(); ++i) TEST_ASSERT_EQUAL(0, changed(a[i], b[i]));
}

void test_stats_and_unknown_names_are_refused() {
    auto none = [](const uint16_t*) {};
    TEST_ASSERT_EQUAL(-1, app::renderClip("stats", "cinematic", none, bufA, bufB, 16));
    TEST_ASSERT_EQUAL(-1, app::renderClip("nope", "cinematic", none, bufA, bufB, 16));
    TEST_ASSERT_EQUAL(-1, app::renderClip("radar", "nope", none, bufA, bufB, 16));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_radar_is_one_turn_and_closes);
    RUN_TEST(test_alert_closes_through_the_crossfade);
    RUN_TEST(test_map_closes_through_the_crossfade);
    RUN_TEST(test_board_rows_re_sort);
    RUN_TEST(test_two_runs_are_identical);
    RUN_TEST(test_stats_and_unknown_names_are_refused);
    return UNITY_END();
}
```

- [ ] **Step 2: Run it to see it fail**

Run: `pio test -e test -f test_demo_clip`
Expected: FAIL to compile: `app/DemoClip.h` not found.

- [ ] **Step 3: Expose the helpers.** In `DemoRender.h`, add after the `namespace basemap` block a `namespace scr { struct Scene; }`, and inside `namespace app`, before `renderDemo`:

```cpp
// The emergency takeover on the scene's first flight; every flight stepped `seconds` back along its track (negative:
// forward). For the demo frames here and the site's clips (DemoClip.h).
void addDemoEmergency(scr::Scene& s);
void backInTime(scr::Scene& s, float seconds);
```

In `DemoRender.cpp`, split `sceneBusyEmergency` and move `backInTime` out of the anonymous namespace, without changing what they compute:

```cpp
namespace {
// ... kScenes, kScreens as they are ...

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
```

`sceneBusyEmergency` needs a forward declaration of `addDemoEmergency`, which `DemoRender.h` provides.

- [ ] **Step 4: Write `DemoClip.h`**

```cpp
#pragma once
// The site's animated previews (6d): a screen's loop, or a tour of the screens, frame by frame at 30 fps. Drawn on the
// demo scenes without a clock, alert engine, director or network, so a clip is the same on every run; flights can run
// at 10x (the Map, the Board, the tour). localtime must give UTC, as for renderDemo. Desktop only in practice:
// a crossfading clip holds 2.3 MB.
#include <cstdint>
#include <functional>

namespace app {

// Called with each finished 240 x 320 RGB565 frame, in order.
using ClipSink = std::function<void(const uint16_t* px)>;

// The frame count, or -1 (no frames) for an unknown name or Stats, which has no motion. Screens: closest radar sky alert
// map board. bufA and bufB hold 240 x stripH pixels each.
int renderClip(const char* screen, const char* theme, const ClipSink& sink, uint16_t* bufA, uint16_t* bufB, int stripH);

}  // namespace app
```

- [ ] **Step 5: Write `DemoClip.cpp`**

```cpp
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

}  // namespace app
```

- [ ] **Step 6: Run the tests**

Run: `pio test -e test -f test_demo_clip`
Expected: 6 tests PASS. Then `pio test -e test -f test_screens_golden` still PASS.

- [ ] **Step 7: Commit**

```bash
git add firmware/lib/core/app firmware/test/test_demo_clip
git commit -m "6d: a screen's frames as a clip"
```

### Task 2: The tour

**Files:** Modify `firmware/lib/core/app/DemoClip.h`, `firmware/lib/core/app/DemoClip.cpp`, `firmware/test/test_demo_clip/test_main.cpp`.

- [ ] **Step 1: Add the failing test** (before `main`, and `RUN_TEST(test_tour);` in it)

```cpp
void test_tour() {
    Frames f;
    int n = app::renderTour("cinematic", [&](const uint16_t* px) { f.emplace_back(px, px + 240 * 320); }, bufA, bufB, 16);
    TEST_ASSERT_EQUAL(820, n);
    TEST_ASSERT_EQUAL(820, int(f.size()));
    const Frame black(240 * 320, 0);
    TEST_ASSERT_EQUAL(0, changed(f.front(), black));
    TEST_ASSERT_EQUAL(0, changed(f.back(), black));
    for (int i = 0; i < 6; ++i)  // the middle of each dwell differs from the next screen's
        TEST_ASSERT_GREATER_THAN(10000, changed(f[58 + 117 * i], f[58 + 117 * (i + 1)]));
    TEST_ASSERT_EQUAL(-1, app::renderTour("nope", [](const uint16_t*) {}, bufA, bufB, 16));
}
```

- [ ] **Step 2: Run it to see it fail**

Run: `pio test -e test -f test_demo_clip`
Expected: FAIL to compile: `renderTour` is not a member of `app`.

- [ ] **Step 3: Declare it** in `DemoClip.h`, after `renderClip`:

```cpp
// Every screen in turn with the firmware's own fades, flights at 10x. Starts and ends at full black, so it loops
// inside a fade. The frame count, or -1 for an unknown theme.
int renderTour(const char* theme, const ClipSink& sink, uint16_t* bufA, uint16_t* bufB, int stripH);
```

- [ ] **Step 4: Implement it** in `DemoClip.cpp`. In the anonymous namespace:

```cpp
void sceneStatsAlert(scr::Scene& s) {
    scr::sceneStats(s);
    addDemoEmergency(s);
}
```

Before the closing `}  // namespace app`:

```cpp
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
    // The frame of the switch was drawn at full black: that frame opens the file. draw(false) above discarded it, so
    // it is emitted here from the display, as nothing has been drawn since.
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
```

Frame arithmetic: the opening black frame is the first; each screen adds `kFadeFrames + kDwell` frames, then `kFadeFrames` more for its fade-out, the last of which is the black switch frame: 1 + 7 × 117 = 820.

- [ ] **Step 5: Run the tests**

Run: `pio test -e test -f test_demo_clip`
Expected: 7 tests PASS. If the first `Frame` is not all zero the veil is not at 255 on the switch frame: check `ScreenHost::update` order before changing the test.

- [ ] **Step 6: Commit**

```bash
git add firmware/lib/core/app/DemoClip.h firmware/lib/core/app/DemoClip.cpp firmware/test/test_demo_clip
git commit -m "6d: the tour of the screens"
```

### Task 3: Simulator modes

**Files:** Modify `firmware/src/main_sim.cpp`.

- [ ] **Step 1: Add the modes.** Add `#include "app/DemoClip.h"` by the other `app/` includes. Update the header comment with `program --clip <screen> <theme>   headless: a preview loop as raw 240x320 RGB24 frames on stdout (the site's clips)` and `program --tour   the same for the tour of the screens`. After `renderShot`:

```cpp
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
```

At the top of `main`, after the `--render` lines:

```cpp
    if (argc >= 2 && std::strcmp(argv[1], "--clip") == 0)
        return argc == 4 ? renderFrames(argv[2], argv[3]) : renderFrames("", "");
    if (argc == 2 && std::strcmp(argv[1], "--tour") == 0) return renderFrames(nullptr, "cinematic");
```

- [ ] **Step 2: Build and check the byte count**

Run: `pio run -e sim && .pio/build/sim/program --clip radar cinematic | wc -c`
Expected: `27648000` (120 frames × 230400).

Run: `.pio/build/sim/program --clip stats cinematic | wc -c`
Expected: the usage line on stderr, `0`.

- [ ] **Step 3: Commit**

```bash
git add firmware/src/main_sim.cpp
git commit -m "6d: --clip and --tour in the simulator"
```

### Task 4: Encoding

**Files:** Create `tools/previews.sh` (executable).

- [ ] **Step 1: Write it**

```bash
#!/usr/bin/env bash
# The showcase page's animated previews (6d), from the simulator's --clip and --tour:
#   tools/previews.sh <sim-binary> <out-dir>
# Files: <screen>-<theme>.mp4 and tour.mp4, 480x640 (2x). Pixels are doubled first, so that 4:2:0 chroma lands on whole
# firmware pixels and small coloured text does not smear. Fails if a file is over its size limit (spec 6d).
set -euo pipefail
if [ $# -ne 2 ]; then
    echo "usage: $0 <sim-binary> <out-dir>" >&2
    exit 2
fi
sim="$1"
out="$2"
mkdir -p "$out"

max_clip=$((600 * 1024))
max_tour=$((2048 * 1024))
max_all=$((5120 * 1024))

encode() {  # <output>; frames on stdin
    ffmpeg -nostdin -v error -y -f rawvideo -pix_fmt rgb24 -s 240x320 -r 30 -i - \
        -vf scale=480:640:flags=neighbor,format=yuv420p -c:v libx264 -preset veryslow -crf 23 \
        -movflags +faststart -an "$1"
}

total=0
check() {  # <file> <limit>
    local size
    size=$(wc -c <"$1" | tr -d ' ')
    echo "$((size / 1024)) KB  $(basename "$1")"
    total=$((total + size))
    if [ "$size" -gt "$2" ]; then
        echo "$1 is over its limit of $(($2 / 1024)) KB" >&2
        exit 1
    fi
}

for theme in cinematic editorial; do
    for screen in closest radar sky alert map board; do
        "$sim" --clip "$screen" "$theme" | encode "$out/$screen-$theme.mp4"
        check "$out/$screen-$theme.mp4" "$max_clip"
    done
done
"$sim" --tour | encode "$out/tour.mp4"
check "$out/tour.mp4" "$max_tour"

echo "$((total / 1024)) KB in all"
if [ "$total" -gt "$max_all" ]; then
    echo "the previews are over their limit of $((max_all / 1024)) KB" >&2
    exit 1
fi
```

- [ ] **Step 2: Run it**

Run from the repo root: `chmod +x tools/previews.sh && tools/previews.sh firmware/.pio/build/sim/program site/shots`
Expected: 13 size lines, a total at most 5120 KB, exit 0. `git status` shows only `tools/previews.sh` (`site/shots/` is ignored).

- [ ] **Step 3: Watch the loop points.** Extract each clip's last and first frames and the Map's middle, and look at them:

Run: `ffmpeg -v error -y -sseof -0.05 -i site/shots/map-cinematic.mp4 -frames:v 1 /tmp/map-last.png` and read the image; compare with the page in Task 5.
Expected: nothing about the map looks torn.

- [ ] **Step 4: Commit**

```bash
git add tools/previews.sh
git commit -m "6d: encode the previews"
```

### Task 5: The page

**Files:** Create `site/previews.js`; modify `site/index.html`, `site/style.css`.

- [ ] **Step 1: Wrap the stills.** From the repo root:

```bash
python3 -I - <<'EOF'
import re
p = 'site/index.html'
t = open(p).read()
def card(m):
    return f'<span class="clip"><img data-clip="shots/{m[2]}-{m[3]}.mp4" {m[1]}></span>'
t, n = re.subn(r'<img (src="shots/(closest|radar|map|board|sky|alert)-(cinematic|editorial)\.png"[^>]*loading="lazy"[^>]*)>', lambda m: f'<span class="clip"><img data-clip="shots/{m[2]}-{m[3]}.mp4" {m[1]}></span>', t)
assert n == 12, n
old = '''<img src="shots/closest-cinematic.png" width="360" height="480"
             alt="Closest Flight screen: United 53 from Zurich to Washington Dulles, 34,000 ft, 2.4 nm away">'''
new = '''<span class="clip"><img data-clip="shots/tour.mp4" src="shots/closest-cinematic.png" width="360" height="480"
             alt="Closest Flight screen: United 53 from Zurich to Washington Dulles, 34,000 ft, 2.4 nm away"></span>'''
assert old in t
t = t.replace(old, new)
old = '''<a class="btn ghost" href="https://github.com/ChristopherJacob/contrail">Source on GitHub</a>
        </div>'''
new = old + '''
        <button class="motion" type="button" data-motion hidden>Pause animation</button>'''
assert old in t
t = t.replace(old, new)
old = '''moves between them.</p>'''
new = '''moves between them. The Live Map and Board clips run ten times faster than life.</p>
      <p><button class="motion" type="button" data-motion hidden>Pause animation</button></p>'''
assert old in t
t = t.replace(old, new)
old = '</body>'
new = '<script src="previews.js" defer></script>\n</body>'
assert old in t
t = t.replace(old, new)
open(p, 'w').write(t)
EOF
```

- [ ] **Step 2: Write `site/previews.js`**

```js
// The showcase's animated previews (6d): each still with a clip gets a muted looping video over it as it nears the
// viewport. The still stays underneath, so with reduced motion, Save-Data, no autoplay or a missing file the page is
// the stills.
(() => {
  const spots = document.querySelectorAll(".clip > img[data-clip]");
  if (!spots.length || !("IntersectionObserver" in window)) return;
  if (matchMedia("(prefers-reduced-motion: reduce)").matches || navigator.connection?.saveData) return;

  let paused = false;
  const inView = new Set();
  const buttons = document.querySelectorAll("[data-motion]");
  const play = (v) => v.play().catch(() => {});

  function attach(img) {
    const v = document.createElement("video");
    v.muted = true;
    v.loop = true;
    v.playsInline = true;
    v.preload = "metadata";
    v.setAttribute("aria-hidden", "true");
    v.addEventListener("playing", () => v.classList.add("on"), { once: true });
    v.src = img.dataset.clip;
    img.after(v);
    return v;
  }

  const watcher = new IntersectionObserver((entries) => {
    for (const e of entries) {
      let v = e.target.nextElementSibling;
      if (e.isIntersecting) {
        v = v || attach(e.target);
        inView.add(v);
        if (!paused) play(v);
      } else if (v) {
        inView.delete(v);
        v.pause();
      }
    }
  }, { rootMargin: "200px" });
  spots.forEach((img) => watcher.observe(img));

  buttons.forEach((b) => {
    b.hidden = false;
    b.addEventListener("click", () => {
      paused = !paused;
      buttons.forEach((x) => { x.textContent = paused ? "Play animation" : "Pause animation"; });
      inView.forEach((v) => (paused ? v.pause() : play(v)));
    });
  });
})();
```

- [ ] **Step 3: Style it.** Append to the Showcase rules, directly after `.shot .theme { ... }` in `site/style.css`:

```css
.clip { position: relative; display: block; }
.clip video { position: absolute; inset: 0; display: block; width: 100%; height: 100%; opacity: 0; }
.clip video.on { opacity: 1; }
.shot .clip video { border-radius: 6px; }
.motion { margin-top: 12px; padding: 6px 14px; border: 1px solid var(--edge); border-radius: 999px; background: transparent; color: var(--soft); font: inherit; font-size: 14px; cursor: pointer; }
.motion:hover { color: var(--text); }
```

- [ ] **Step 4: Check it in the browser pane.** Copy nothing: `site/shots` holds the PNGs after `tools/screenshots.sh firmware/.pio/build/sim/program site/shots` and the clips from Task 4. Serve `site/` with a `.claude/launch.json` entry (`python3 -m http.server 8123 --directory site`), then with the Browser tools:
  - `navigate` to `http://localhost:8123/`; the hero shows a `video.on`, and `document.querySelectorAll('video').length` grows on scrolling, staying well under 13 at the top of the page.
  - the "Pause animation" button: every in-view `video.paused` is true, both labels read "Play animation".
  - scroll a card out of view: its `video.paused` is true.
  - `resize_window` preset `mobile`: no horizontal scroll (`document.documentElement.scrollWidth <= innerWidth`).
  - a reload with `matchMedia` reduced motion is exercised by reading the script's guard: run in the console with the Emulation-free check, stubbing `window.matchMedia` before a re-injection of the script, and confirm no videos are added.

- [ ] **Step 5: Commit**

```bash
git add site/index.html site/previews.js site/style.css
git commit -m "6d: the showcase plays the previews"
```

### Task 6: Workflow, docs, verification

**Files:** Modify `.github/workflows/pages.yml`, `README.md`, `docs/superpowers/specs/2026-11-09-contrail-animated-previews-design.md`.

- [ ] **Step 1: Pages workflow.** In the `Install PlatformIO, SDL2 and curl` step's `apt-get install` add `ffmpeg`, and rename the step `Install PlatformIO, SDL2, curl and ffmpeg`. After the `Screenshots` step add:

```yaml
      - name: Animated previews
        run: tools/previews.sh firmware/.pio/build/sim/program site/shots
```

- [ ] **Step 2: README.** After the line ending `screenshots).` under the simulator commands add: `` `tools/previews.sh <sim-binary> <out-dir>` encodes the showcase page's animated clips (needs ffmpeg). `` and in the tree, after the `tools/screenshots.sh` line: `tools/previews.sh        animated previews of the screens for the showcase page`.

- [ ] **Step 3: Full verification**

Run from `firmware/`: `pio test -e test`, then `pio run -e sim`.
Expected: all tests PASS (including the goldens) and `sim` builds.

Run from the repo root, needing emsdk if installed: `tools/build_wasm.sh` only if `emcc` is on the PATH; otherwise note it was not run (CI builds it; `DemoClip.cpp` is compiled there by the `find lib/core` in that script).

- [ ] **Step 4: Spec status.** Set the spec's `Status:` line to `Built on claude/elastic-napier-7775d5; checked in the browser pane.`, and add a `## 9. As built` section recording any difference from the design and the real sizes `tools/previews.sh` printed.

- [ ] **Step 5: Commit**

```bash
git add .github/workflows/pages.yml README.md docs/superpowers/specs/2026-11-09-contrail-animated-previews-design.md
git commit -m "6d: Pages builds the previews; README and spec"
```
