// Renderer micro-benchmark: times each drawing primitive on one 240x32 strip, the radar
// screen's real per-frame cost with a per-primitive breakdown, then every screen with the
// demo scenes: full redraw, steady state with the scene nudged at 5 Hz the way live data
// moves it, the Board's reorder glide and the Sky View's once-a-minute recompute. Frames
// render to a display that discards pixels, so times are CPU (update + draw) only; the
// px/frame column gives the SPI transfer (~2.5 Mpx/s at 40 MHz) that overlaps with it.
// Prints to serial at 115200.
#include <Arduino.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <new>

#include "gfx/Canvas.h"
#include "gfx/Font.h"
#include "gfx/Raster.h"
#include "gfx/StripRenderer.h"
#include "gfx/fonts/Fonts.h"
#include "map/Airports.h"
#include "map/LandMask.h"
#include "screens/DemoScenes.h"
#include "screens/ScreenApp.h"

using namespace gfx;

namespace {

uint16_t buf[240 * 32], bufB[240 * 32];

template <typename F>
void bench(const char* name, int bandY, F&& fn, int bandX = 0, int width = 240) {
    constexpr int kRuns = 20;
    uint32_t best = UINT32_MAX;
    for (int i = 0; i < kRuns; ++i) {
        Canvas c(buf, bandX, width, bandY, 32);
        uint32_t t = micros();
        fn(c);
        best = std::min(best, uint32_t(micros() - t));
    }
    Serial.printf("%-28s %6u us\n", name, best);
}

// Discards pixels but remembers the blocks the renderer pushed, so primitives can be
// replayed over exactly the area a real radar frame redraws.
class NullDisplay : public hal::IDisplay {
   public:
    static constexpr int kMaxRects = 16 * 64;
    int width() const override { return 240; }
    int height() const override { return 320; }
    void pushRect(int x, int y, int w, int h, const uint16_t*) override {
        if (record && n < kMaxRects) rects[n++] = {x, y, w, h};
    }
    Rect rects[kMaxRects];
    int n = 0;
    bool record = false;
};

uint32_t microsSource() { return micros(); }

NullDisplay nullDisplay;

// Time fn over every block in the recorded frames (same canvases the renderer used).
template <typename F>
uint32_t replay(F&& fn) {
    uint32_t total = 0;
    for (int i = 0; i < nullDisplay.n; ++i) {
        const Rect& r = nullDisplay.rects[i];
        Canvas c(buf, r.x, r.w, r.y, r.h);
        uint32_t t = micros();
        fn(c);
        total += micros() - t;
    }
    return total;
}

// ScreenApp is large: keep it off the stack, and rebuild it for each run so every run
// starts from the same state (the radar's sweep from north, the board settled).
alignas(scr::ScreenApp) uint8_t appMem[sizeof(scr::ScreenApp)];
scr::ScreenApp* appLive = nullptr;
scr::ScreenApp& freshApp(const scr::Theme& theme = scr::cinematicSky()) {
    if (appLive) appLive->~ScreenApp();
    appLive = new (appMem) scr::ScreenApp(theme);
    appLive->tick(0);  // the first tick opens the radar; the benches pick their own screen after
    return *appLive;
}

void radarBench(int stripH) {
    using scr::RadarScope;
    scr::ScreenApp& app = freshApp();
    scr::sceneBusy(app.scene());
    app.sceneUpdated();
    app.host().showNow(scr::ScreenId::Radar);
    StripRenderer r(nullDisplay, buf, bufB, stripH);
    r.setMicrosSource(microsSource);
    r.setLayers(app.layers(), app.layerCount());

    r.frame(1 / 30.f);  // full first frame
    uint32_t fullUs = r.lastRenderUs();
    r.invalidateAll();
    r.frame(0);
    fullUs = std::min(fullUs, r.lastRenderUs());
    for (int i = 0; i < 30; ++i) r.frame(1 / 30.f);  // settle

    constexpr int kFrames = 120;  // one sweep turn at 30 fps
    uint32_t sum = 0, worst = 0, strips = 0, px = 0;
    nullDisplay.n = 0;
    nullDisplay.record = true;
    static int frameStart[kFrames + 1];
    for (int i = 0; i < kFrames; ++i) {
        int before = frameStart[i] = nullDisplay.n;
        r.frame(1 / 30.f);
        sum += r.lastRenderUs(), worst = std::max(worst, r.lastRenderUs()), strips += r.lastStrips();
        for (int k = before; k < nullDisplay.n; ++k) px += nullDisplay.rects[k].w * nullDisplay.rects[k].h;
    }
    nullDisplay.record = false;
    frameStart[kFrames] = nullDisplay.n;
    Serial.printf("[bench] radar busy, %d-row strips: full redraw %u us; incremental avg %u us, worst %u us, %.1f strips, %u px/frame\n",
                  stripH, fullUs, sum / kFrames, worst, float(strips) / kFrames, px / kFrames);

    // Per-primitive cost over the same blocks, averaged per frame. First the whole layer
    // (at the final sweep angle, so only an estimate of the real frames' draw time).
    Serial.printf("  %-22s %6u us/frame\n", "layer draw (replay)", replay([&](Canvas& c) { app.layers()[0]->draw(c); }) / kFrames);

    const scr::Theme& t = scr::cinematicSky();
    constexpr float kCx = RadarScope::kCx, kCy = RadarScope::kCy, kR = RadarScope::kR;
    Rgb stops[] = {t.radarBg[0], t.radarBg[1], t.radarBg[2]};
    float pos[] = {0.f, 0.62f, 1.f};
    float sweep;
    const scr::Scene& sc = app.scene();
    auto per = [](uint32_t us) { return us / kFrames; };
    Serial.printf("  %-22s %6u us/frame\n", "radialGradient",
                  per(replay([&](Canvas& c) { c.radialGradient({0, 0, 240, 320}, kCx, kCy, 150, 190, stops, pos, 3); })));
    Serial.printf("  %-22s %6u us/frame\n", "3 rings", per(replay([&](Canvas& c) {
                      aaRing(c, kCx, kCy, kR, 1.2f, t.radarLine, 140);
                      aaRing(c, kCx, kCy, kR * 2 / 3, 1, t.radarLine, 75);
                      aaRing(c, kCx, kCy, kR / 3, 1, t.radarLine, 75);
                  })));
    Serial.printf("  %-22s %6u us/frame\n", "2 crosshair lines", per(replay([&](Canvas& c) {
                      aaLine(c, kCx - kR, kCy, kCx + kR, kCy, 1, t.radarLine, 45);
                      aaLine(c, kCx, kCy - kR, kCx, kCy + kR, 1, t.radarLine, 45);
                  })));
    Serial.printf("  %-22s %6u us/frame\n", "ring labels", per(replay([&](Canvas& c) {
                      drawText(c, kFontSmall, int(kCx) + 3, int(kCy - kR / 3) + 10, "8", t.radarLine, Align::Left, 150);
                      drawText(c, kFontSmall, int(kCx) + 3, int(kCy - kR * 2 / 3) + 10, "17", t.radarLine, Align::Left, 150);
                      drawText(c, kFontSmall, int(kCx) + 3, int(kCy - kR) + 10, "25 nm", t.radarLine, Align::Left, 150);
                      drawText(c, kFontSmall, int(kCx), int(kCy - kR) - 3, "N", t.radarLine, Align::Center, 210);
                  })));
    // The wedge's angle follows the sweep in the recorded frames: rerun the sweep turn.
    // Sweep advances 3 degrees per 1/30 s frame: 32 frames ran before the recording.
    uint32_t wedgeUs = 0, tipUs = 0;
    for (int f = 0; f < kFrames; ++f) {
        sweep = 3.f * (f + 32);
        while (sweep >= 360.f) sweep -= 360.f;
        for (int k = frameStart[f]; k < frameStart[f + 1]; ++k) {
            const Rect& rr = nullDisplay.rects[k];
            Canvas c(buf, rr.x, rr.w, rr.y, rr.h);
            uint32_t t0 = micros();
            aaWedge(c, kCx, kCy, kR, sweep - 60, sweep, t.radarLine, 0, 110);
            uint32_t t1 = micros();
            float tx = kCx + std::sin(sweep * 0.0174533f) * kR, ty = kCy - std::cos(sweep * 0.0174533f) * kR;
            aaLine(c, kCx, kCy, tx, ty, 1.4f, t.radarText, 230);
            tipUs += micros() - t1, wedgeUs += t1 - t0;
        }
    }
    Serial.printf("  %-22s %6u us/frame\n", "wedge", per(wedgeUs));
    Serial.printf("  %-22s %6u us/frame\n", "tip line", per(tipUs));
    Serial.printf("  %-22s %6u us/frame\n", "blips glow+dot", per(replay([&](Canvas& c) {
                      for (int i = 0; i < sc.count; ++i) {
                          const scr::SceneFlight& fl = sc.flights[i];
                          if (fl.distNm > sc.radiusNm) continue;
                          float d = fl.distNm / sc.radiusNm * kR;
                          float x = kCx + std::sin(fl.bearingDeg * 0.0174533f) * d;
                          float y = kCy - std::cos(fl.bearingDeg * 0.0174533f) * d;
                          if (!c.clip().intersects({int(x) - 8, int(y) - 8, 17, 17})) continue;
                          c.radialGlow(x, y, 7, t.radarBlip, 120);
                          aaCircle(c, x, y, 1.6f, {255, 255, 255}, 160);
                      }
                  })));
    Serial.printf("  %-22s %6u us/frame\n", "blip labels (5)", per(replay([&](Canvas& c) {
                      for (int i = 0; i < 5 && i < sc.count; ++i) {
                          const scr::SceneFlight& fl = sc.flights[i];
                          float d = fl.distNm / sc.radiusNm * kR;
                          int x = int(kCx + std::sin(fl.bearingDeg * 0.0174533f) * d) + 8;
                          int y = int(kCy - std::cos(fl.bearingDeg * 0.0174533f) * d) + 1;
                          drawText(c, kFontSmall, x, y, fl.callsign, t.text, Align::Left, 225);
                          drawText(c, kFontSmall, x, y + 10, "120↑", t.radarText, Align::Left, 200);
                      }
                  })));
}

struct Stats {
    uint32_t sum = 0, worst = 0, over = 0, strips = 0, px = 0, worstPx = 0, worstStrips = 0;
    int frames = 0;
    uint32_t layerUs[8] = {};  // replayed draw time per screen layer
    uint32_t otherUs = 0, worstOther = 0;  // frame time outside draw(): layer updates and the renderer
    int layers = 0;
    void add(uint32_t us, int pushedStrips, uint32_t pushedPx) {
        if (us > worst) worst = us, worstPx = pushedPx, worstStrips = pushedStrips;
        sum += us, over += us > 33333, strips += pushedStrips, px += pushedPx;
        ++frames;
    }
    void print(const char* what) const {
        int n = frames ? frames : 1;
        Serial.printf("    %-10s avg %6u us  worst %6u us (%5u px, %2u blocks)  >33ms %3u/%-3d  %5.1f blocks  %6u px/frame",
                      what, sum / n, worst, worstPx, worstStrips, over, frames, float(strips) / n, px / n);
        Serial.printf("  update %u/%u", otherUs / n, worstOther);
        if (layers) {
            Serial.print("  layers");
            for (int i = 0; i < layers; ++i) Serial.printf(" %u", layerUs[i] / n);
        }
        Serial.println();
    }
};

// Nudge the scene the way a 5 Hz rebuild from live data does: every flight a little further
// round (0.2 deg) and a touch closer, the clock 0.2 s on.
void nudge(scr::Scene& s, int k) {
    for (int i = 0; i < s.count; ++i) {
        scr::SceneFlight& f = s.flights[i];
        f.bearingDeg = std::fmod(f.bearingDeg + 0.2f, 360.f);
        f.distNm = std::max(0.3f, f.distNm - 0.004f);
        f.trackDeg = std::fmod(f.trackDeg + 0.05f, 360.f);
    }
    if (k % 5 == 4) ++s.epoch;
    ++s.version;
}

uint32_t pushedPx(int from) {
    uint32_t px = 0;
    for (int k = from; k < nullDisplay.n; ++k) px += nullDisplay.rects[k].w * nullDisplay.rects[k].h;
    return px;
}

// One frame at 30 fps (dt fixed, so runs are repeatable): update + draw time, pushed pixels.
scr::Screen* profiled = nullptr;  // when set, step() also replays each of its layers

void step(StripRenderer& r, Stats* st) {
    nullDisplay.n = 0;
    nullDisplay.record = true;
    // Timed here rather than with lastRenderUs(), which a frame with nothing to draw leaves alone.
    uint32_t t = micros();
    int strips = r.frame(1 / 30.f);
    uint32_t us = micros() - t;
    nullDisplay.record = false;
    if (st) {
        st->add(us, strips, pushedPx(0));
        uint32_t other = us - (strips ? r.layerUs(0) : 0);
        st->otherUs += other, st->worstOther = std::max(st->worstOther, other);
    }
    if (st && profiled) {
        st->layers = std::min(profiled->layerCount(), 8);
        for (int i = 0; i < st->layers; ++i)
            st->layerUs[i] += replay([&](Canvas& c) { profiled->layers()[i]->draw(c); });
    }
}

enum Extra { kNone, kGlide, kRecompute, kTrails };

void screenBench(const char* name, scr::ScreenId id, void (*scene)(scr::Scene&), Extra extra,
                 const scr::Theme& theme) {
    scr::ScreenApp& app = freshApp(theme);
    scene(app.scene());
    app.scene().epoch += 5;  // off the minute, so the steady run sees no minute tick
    app.sceneUpdated();
    uint32_t enterUs = micros();
    app.host().showNow(id);  // enter(): the Map rebuilds its land mask and airports here
    enterUs = micros() - enterUs;
    profiled = app.host().screen(id);
    StripRenderer r(nullDisplay, buf, bufB, 16);
    r.setMicrosSource(microsSource);
    r.setLayers(app.layers(), app.layerCount());

    uint32_t full = UINT32_MAX;
    for (int i = 0; i < 3; ++i) {
        r.invalidateAll();
        r.frame(i == 0 ? 1 / 30.f : 0.f);
        full = std::min(full, r.lastRenderUs());
    }
    for (int i = 0; i < 30; ++i) step(r, nullptr);  // settle (fades, first sync)

    Stats idle, steady;
    for (int i = 0; i < 30; ++i) step(r, &idle);  // 1 s with no new data
    for (int i = 0; i < 120; ++i) {                     // 4 s, scene rebuilt every 6th frame
        if (i % 6 == 0) nudge(app.scene(), i / 6), app.sceneUpdated();
        step(r, &steady);
    }
    Serial.printf("[screen] %-12s full %6u us (min of 3), enter %6u us\n", name, full, enterUs);
    {  // one more full redraw, for its per-layer split
        Stats f;
        r.invalidateAll();
        step(r, &f);
        f.print("full");
    }
    idle.print("idle");
    steady.print("steady");

    if (extra == kGlide) {
        // Two rows trade places: the nearest drops to fourth.
        scr::Scene& s = app.scene();
        std::swap(s.flights[0].distNm, s.flights[3].distNm);
        std::rotate(s.flights, s.flights + 1, s.flights + 4);
        ++s.version;
        app.sceneUpdated();
        Stats glide;
        for (int i = 0; i < 18; ++i) step(r, &glide);  // 0.6 s: the 0.3 s glide and fades
        glide.print("glide");
    } else if (extra == kTrails) {
        // The Live Map as it runs: every plane with a full eight-point trail (one point per 10 s of
        // scene time), then 4 s of 5 Hz rebuilds that cross a trail sample.
        scr::Scene& s = app.scene();
        for (int k = 0; k < 8 * 50; ++k) {  // 80 s of rebuilds at 5 Hz
            nudge(s, k);
            if (k % 50 == 49) app.sceneUpdated(), step(r, nullptr);
        }
        s.epoch = (s.epoch / 10 + 1) * 10 - 2;  // a trail sample falls 2 s into the run
        app.sceneUpdated();
        for (int i = 0; i < 30; ++i) step(r, nullptr);
        Stats tr;
        for (int i = 0; i < 120; ++i) {
            if (i % 6 == 0) nudge(app.scene(), i / 6), app.sceneUpdated();
            step(r, &tr);
        }
        tr.print("trailed");
    } else if (extra == kRecompute) {
        scr::Scene& s = app.scene();
        s.epoch = (s.epoch / 60 + 1) * 60;  // the next minute: sky colours and stars are recomputed
        ++s.version;
        app.sceneUpdated();
        Stats rc;
        step(r, &rc);
        rc.print("recompute");
        Stats after;
        for (int i = 0; i < 6; ++i) step(r, &after);
        after.print("after");
    }

    // A screen switch: the host dips through the veil (0.2 s out, 0.2 s in) and redraws the whole
    // screen on every frame of it. "leave" fades this screen out and another in; "arrive" fades back
    // to this one, whose enter() runs at the swap (the Map rebuilds its mask and airports there).
    const scr::ScreenId other = id == scr::ScreenId::Closest ? scr::ScreenId::Radar : scr::ScreenId::Closest;
    profiled = nullptr;  // the other screen owns the arena while it is up: no layer replays
    for (int pass = 0; pass < 2; ++pass) {
        app.host().show(pass == 0 ? other : id);
        Stats sw;
        for (int i = 0; i < 60 && (i == 0 || app.host().transitioning()); ++i) step(r, &sw);
        step(r, &sw);  // the clean frame after the veil
        sw.print(pass == 0 ? "leave" : "arrive");
    }
}

// The Live Map's mask rebuild and airport query (on entering the map, or a new home or radius): at
// San Francisco and around New York, where the coastline is busiest. Target <= 60 ms.
void mapMaskBench() {
    static uint8_t bits[basemap::LandMask::kBytes];
    static basemap::AirportMark marks[scr::MapBase::kMaxMarks];
    basemap::LandMask m(bits);
    struct Place {
        const char* name;
        double lat, lon;
    };
    for (const Place& p : {Place{"SFO", 37.6189, -122.375}, Place{"JFK", 40.6413, -73.7781}})
        for (float r : {5.f, 25.f, 50.f, 100.f}) {
            basemap::MapView v = basemap::viewFor(p.lat, p.lon, r);
            uint32_t t = micros();
            int tiles = m.build(v);
            uint32_t maskUs = micros() - t;
            t = micros();
            int n = basemap::airportsInView(v, marks, scr::MapBase::kMaxMarks);
            basemap::placeLabels(marks, n, kFontSmall, 240);
            uint32_t apUs = micros() - t;
            Serial.printf("[map] %s %3.0f nm: mask %d tiles %6u us, %2d airports %5u us\n", p.name, r, tiles, maskUs, n,
                          apUs);
        }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(500);
    Rgb stops[] = {{10, 18, 54}, {44, 60, 128}, {226, 120, 84}};
    float pos[] = {0, 0.58f, 1};
    Serial.println("\n[bench] one 240x32 strip, best of 20");
    bench("fill", 0, [](Canvas& c) { c.fill({0, 0, 240, 32}, 0x1234); });
    bench("vGradient", 0, [&](Canvas& c) { c.vGradient({0, 0, 240, 320}, stops, pos, 3); });
    bench("fillBlend a150", 0, [](Canvas& c) { c.fillBlend({0, 0, 240, 32}, {4, 8, 24}, 150); });
    bench("radialGradient", 128, [&](Canvas& c) { c.radialGradient({0, 0, 240, 320}, 120, 148, 150, 190, stops, pos, 3); });
    bench("radialGlow r120 (centre)", 160, [](Canvas& c) { c.radialGlow(120, 176, 120, {255, 170, 90}, 85); });
    bench("radialGlow r46", 240, [](Canvas& c) { c.radialGlow(186, 258, 46, {10, 60, 40}, 120); });
    bench("aaRing r46", 240, [](Canvas& c) { aaRing(c, 186, 258, 46, 1.2f, {90, 255, 170}, 150); });
    bench("aaRing r104 (radar)", 128, [](Canvas& c) { aaRing(c, 120, 148, 104, 1.2f, {90, 255, 170}, 150); });
    bench("aaWedge r46 60deg", 240, [](Canvas& c) { aaWedge(c, 186, 258, 46, 30, 90, {90, 255, 170}, 0, 110); });
    bench("aaWedge r104 60deg", 64, [](Canvas& c) { aaWedge(c, 120, 148, 104, -30, 30, {90, 255, 170}, 0, 110); });
    bench("aaLine 92px", 240, [](Canvas& c) { aaLine(c, 140, 258, 232, 258, 1, {90, 255, 170}, 40); });
    bench("aaLine diag 46px", 240, [](Canvas& c) { aaLine(c, 186, 258, 218, 230, 1.4f, {90, 255, 170}, 220); });
    bench("aaCircle r9", 160, [](Canvas& c) { aaCircle(c, 120, 176, 9, {255, 244, 214}); });
    bench("planeGlyph 30", 160, [](Canvas& c) { planeGlyph(c, 120, 176, 30, 80, {255, 255, 255}); });
    static const Vec2 ridge[] = {{0, 320},   {0, 296},   {28, 288},  {52, 293},  {80, 281}, {104, 287},
                                 {131, 279}, {158, 289}, {186, 284}, {214, 292}, {240, 286}, {240, 320}};
    bench("aaPolygon ridge", 288, [](Canvas& c) { aaPolygon(c, ridge, 12, {6, 8, 20}); });
    bench("text body 'UAL1423 ...'", 240, [](Canvas& c) { drawText(c, kFontBody, 10, 258, "UAL1423  SWA2210", {255, 255, 255}); });
    bench("text hero", 32, [](Canvas& c) { drawText(c, kFontHero, 16, 46, "UAL1423", {255, 255, 255}); });

    {
        // Float op costs, 1000 dependent-free ops each, for choosing per-pixel maths.
        volatile float sink = 0;
        float v[16];
        for (int i = 0; i < 16; ++i) v[i] = 1.5f + i;
        auto op = [&](const char* name, auto f) {
            uint32_t t = micros();
            float acc = 0;
            for (int i = 0; i < 1000; ++i) acc += f(v[i & 15]);
            uint32_t us = micros() - t;
            sink = acc;
            Serial.printf("%-28s %6u ns/op\n", name, us);
        };
        op("add (loop baseline)", [](float x) { return x; });
        op("mul", [](float x) { return x * 1.37f; });
        op("sqrt", [](float x) { return std::sqrt(x); });
        op("div", [](float x) { return 1.f / x; });
        op("float->int", [](float x) { return float(int(x * 7.3f)); });
        (void)sink;
    }
    radarBench(16);

    mapMaskBench();
    for (const scr::Theme* theme : {&scr::cinematicSky(), &scr::editorial()}) {
        Serial.printf("\n[bench] screens (%s), 16-row strips, demo scenes, 30 fps frames\n",
                      theme->flat ? "editorial" : "cinematic");
        screenBench("closest", scr::ScreenId::Closest, scr::sceneBusy, kNone, *theme);
        screenBench("closest-nt", scr::ScreenId::Closest, scr::sceneNight, kNone, *theme);
        screenBench("radar", scr::ScreenId::Radar, scr::sceneBusy, kNone, *theme);
        screenBench("map", scr::ScreenId::Map, scr::sceneBusy, kTrails, *theme);
        screenBench("board", scr::ScreenId::Board, scr::sceneBusy, kGlide, *theme);
        screenBench("board-night", scr::ScreenId::Board, scr::sceneNight, kGlide, *theme);
        screenBench("sky-dusk", scr::ScreenId::Sky, scr::sceneBusy, kRecompute, *theme);
        screenBench("sky-night", scr::ScreenId::Sky, scr::sceneNight, kRecompute, *theme);
        screenBench("stats", scr::ScreenId::Stats, scr::sceneStats, kNone, *theme);
    }
    Serial.printf("[bench] heap %u, largest block %u\n", ESP.getFreeHeap(),
                  heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    Serial.println("[bench] done");
}

void loop() { delay(1000); }
