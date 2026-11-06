#pragma once
// The Live Map's planes over the chart (spec §3.1): heading-rotated glyphs coloured by altitude band
// (or airline, in a brand-fill theme), the trail of their last eight positions, and labels for the
// nearest five. Each plane's drawn state is quantised to half pixels; only planes whose state
// changed are redrawn.
#include "MapTrails.h"
#include "Scene.h"
#include "ScreenArena.h"
#include "Theme.h"
#include "gfx/Layer.h"

namespace scr {

class MapBase;

class MapPlanes : public gfx::Layer {
   public:
    static constexpr int kLabels = 5;
    static constexpr int kMaxRects = 12;  // damage budget for a sync, leaving room for the strip
    struct Plane {
        uint32_t hex = 0;
        int16_t x2 = 0, y2 = 0;  // half pixels
        int16_t heading = 0;     // whole degrees
        uint8_t trail = 0;       // trail points drawn
        bool label = false, selected = false;
        uint8_t spot = 255;      // label position (kSpots); 255 = none yet
        uint8_t colour = 0;      // 0..2 altitude bands, 3 unknown airline, 4 + i data::kAirlines[i]
        uint8_t shape = 0;       // data::Family (the Map's arena scratch has ~12 B to spare: grow Plane with care)
        char line1[9] = "", line2[8] = "";
        uint8_t w1 = 0, w2 = 0;  // their widths in kFontSmall: layout boxes them many times a sync
        uint32_t trailSig = 0;
        int16_t bx = 0, by = 0, bw = 0, bh = 0;
        gfx::Rect box() const { return {bx, by, bw, bh}; }
    };
    MapPlanes(const Theme& t, const MapBase& base, const MapTrails& trails) : t_(t), base_(base), trails_(trails) {}
    void attach(Plane* planes, const ScreenArena& a, const void* owner) {
        planes_.attach(planes, a, owner), version_ = 0;
    }
    void setScene(const Scene* s) { scene_ = s, dirty_ = true; }
    void setSelection(uint32_t hex) { selected_ = hex, dirty_ = true; }
    void reset() { dirty_ = true; }
    // The plane nearest (x, y) within maxPx, or 0.
    uint32_t hitTest(int x, int y, int maxPx) const;
    void update(float dt, gfx::DamageList& damage) override;
    void draw(gfx::Canvas& c) override;

   private:
    struct Spot {
        int8_t dx, dy;
        bool left;  // text extends left of the anchor
    };
    static const Spot kSpots[6];
    // Where f sits on the chart, in half pixels; false if it is off the chart.
    bool place(const SceneFlight& f, int16_t& x2, int16_t& y2) const;
    Plane make(const SceneFlight& f, int16_t x2, int16_t y2, bool label) const;
    // The label's box at spot `i` (anchor written to lx, ly).
    gfx::Rect labelBox(const Plane& p, int i, int& lx, int& ly) const;
    // Everything drawn for a plane except its box, hashed: what tells a sync that it changed.
    static uint32_t sigOf(const Plane& p);
    // What a move redraws when the trail stays put: the glyph (and selection ring), the label and
    // the trail's last leg, from its newest point to the plane.
    gfx::Rect movingBox(const Plane& p) const;
    // Picks the label spot of planes_[self] clear of the chart's marks and text, the other planes
    // and the labels of the slots in `placed` where it can; false if the label still covers text.
    bool layout(int self, uint32_t placed);
    // How much a label at r would cover: dots and planes cost little, text a lot (textHit).
    int coverCost(const gfx::Rect& r, int self, uint32_t placed, bool& textHit) const;
    uint8_t colourIndex(const SceneFlight& f) const;
    gfx::Rgb colourOf(uint8_t i) const;
    bool trailPoint(const MapTrails::Track& t, int i, int& x, int& y) const;
    void sync(gfx::DamageList& damage);
    void drawTrail(gfx::Canvas& c, const Plane& p, float x, float y, gfx::Rgb col) const;
    const Theme& t_;
    const MapBase& base_;
    const MapTrails& trails_;
    const Scene* scene_ = nullptr;
    ArenaArray<Plane, Scene::kMaxFlights> planes_;
    uint32_t selected_ = 0, version_ = 0;
    bool dirty_ = true;
};

}  // namespace scr
