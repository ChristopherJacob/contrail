#pragma once
// Radar (mockup 2A): a phosphor scope with a sweep that turns every four seconds. Blips
// light as the sweep passes and fade over one turn; the nearest five carry labels. A strip
// along the bottom shows the selected (or closest) flight.
#include "Screen.h"
#include "ScreenArena.h"
#include "Theme.h"

namespace scr {

class RadarScope : public gfx::Layer {
   public:
    static constexpr float kCx = 120, kCy = 148, kR = 104;
    static constexpr float kTurnS = 4, kTrailDeg = 60;
    static constexpr int kLabels = 5, kBottom = 262;  // the strip owns everything below

    explicit RadarScope(const Theme& t) : t_(t) {}
    void setScene(const Scene* s) { scene_ = s, sceneDirty_ = true; }
    void setSelection(uint32_t hex) { selected_ = hex, sceneDirty_ = true; }
    void reset() { full_ = sceneDirty_ = true; }
    // The blips and what was last drawn live in the screen arena while the radar is up.
    void attach(ScreenArena& a);
    // Flight whose blip is nearest (x, y) within maxPx, or 0.
    uint32_t hitTest(int x, int y, int maxPx) const;

    void update(float dt, gfx::DamageList& damage) override;
    void draw(gfx::Canvas& c) override;

   private:
    struct Blip {
        uint32_t hex = 0;
        float x = 0, y = 0, angle = 0;
        uint8_t level = 0;
        bool label = false, selected = false, alignRight = false;
        int16_t heading = 0;     // the icon's nose, in 3° steps; 0 for a dot
        uint8_t shape = 0;       // data::Family; 0 draws a dot
        int16_t lx = 0, ly = 0;  // label anchor: line 1 baseline; line 2 sits 10 px below
        uint8_t spot = 255;      // label position chosen last time; 255 = none yet
        char line1[10] = "", line2[8] = "";
        gfx::Rect box;
    };
    // What the last scene sync drew for a blip: enough to keep it where it was, match it
    // to its new self and tell whether it changed. A full Blip copy would cost ~1.9 KB.
    struct Drawn {
        uint32_t hex = 0;  // 0 once matched
        float x = 0, y = 0, angle = 0;
        uint32_t text = 0;  // hash of line1 and line2
        int16_t lx = 0, ly = 0;
        int16_t bx = 0, by = 0, bw = 0, bh = 0;  // box
        uint8_t level = 0, spot = 255;
        bool label = false, selected = false, alignRight = false;
        uint8_t shape = 0;    // before heading: it fills the padding, so Drawn stays 40 B
        int16_t heading = 0;
        gfx::Rect box() const { return {bx, by, bw, bh}; }
    };
    struct Scratch {
        Blip blips[Scene::kMaxFlights];
        Drawn prev[Scene::kMaxFlights];
    };
    static Drawn drawnOf(const Blip& b);
    static bool sameDrawn(const Drawn& o, const Blip& b);
    void syncScene(gfx::DamageList& damage);
    void addSweepDamage(float a0, float a1, gfx::DamageList& damage);
    // Places b's label clear of `taken` where it can (the first nSoft entries, blip dots,
    // cost less to cover); returns the label's box (empty if none).
    gfx::Rect layout(Blip& b, const gfx::Rect* taken, int nTaken, int nSoft) const;
    uint8_t levelFor(const Blip& b) const;

    const Theme& t_;
    const Scene* scene_ = nullptr;
    ArenaArray<Blip, Scene::kMaxFlights> blips_;
    ArenaArray<Drawn, Scene::kMaxFlights> prev_;
    int n_ = 0;
    float sweep_ = 0, range_ = 25;
    uint32_t selected_ = 0, headerSig_ = 0;
    bool full_ = true, sceneDirty_ = true;
    char header_[24] = "", count_[16] = "", ringLabel_[3][12] = {};
};

class RadarStrip : public gfx::Layer {
   public:
    static constexpr gfx::Rect kBox{10, 264, 220, 46};
    explicit RadarStrip(const Theme& t) : t_(t) {}
    void setScene(const Scene* s) { scene_ = s; }
    void setSelection(uint32_t hex) { selected_ = hex; }
    void reset() { sig_ = 0; }
    uint32_t shownHex() const { return shown_; }
    void update(float dt, gfx::DamageList& damage) override;
    void draw(gfx::Canvas& c) override;

   private:
    // The strings the strip draws; hashed for damage so drifting raw values don't redraw it.
    struct Text {
        char callsign[9], sub[32], alt[24], dist[24];
    };
    const SceneFlight* pick() const;
    static void compose(const SceneFlight& f, Text& out);
    const Theme& t_;
    const Scene* scene_ = nullptr;
    uint32_t selected_ = 0, shown_ = 0, sig_ = 0;
};

class RadarScreen : public Screen {
   public:
    RadarScreen(const Theme& t, ScreenArena& a) : scope_(t), strip_(t), arena_(a) {}
    ScreenId id() const override { return ScreenId::Radar; }
    void setScene(const Scene* s) override { scope_.setScene(s), strip_.setScene(s); }
    void setSelection(uint32_t hex) override;
    void enter() override { scope_.attach(arena_), scope_.reset(), strip_.reset(); }
    NavRequest onTap(int x, int y) override;
    int layerCount() const override { return 2; }
    gfx::Layer* const* layers() override { return layers_; }

   private:
    RadarScope scope_;
    RadarStrip strip_;
    ScreenArena& arena_;
    gfx::Layer* layers_[2] = {&scope_, &strip_};
};

}  // namespace scr
