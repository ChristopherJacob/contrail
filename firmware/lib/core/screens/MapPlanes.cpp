#include "MapPlanes.h"

#include <cmath>
#include <cstdio>

#include "MapScreen.h"
#include "data/Airlines.h"
#include "data/Shapes.h"
#include "gfx/Font.h"
#include "gfx/Math.h"
#include "gfx/Raster.h"
#include "gfx/fonts/Fonts.h"
#include "ui/Format.h"
#include "ui/Widgets.h"

namespace scr {
using namespace gfx;

namespace {
// Small text with a one-pixel halo in the water colour, so it reads over trails, coast and other
// planes. The halo stays inside ui::textBox's one-pixel margin. `w` is the text's width.
void haloText(Canvas& c, int x, int y, const char* s, int w, Rgb col, Align al, Rgb halo) {
    static constexpr int8_t kD[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    if (al == Align::Right) x -= w;
    for (const auto& d : kD) drawText(c, kFontSmall, x + d[0], y + d[1], s, halo, 150);
    drawText(c, kFontSmall, x, y, s, col);
}
}  // namespace

// Label spots around a plane, as on the Radar: right, left, then above and below on each side.
const MapPlanes::Spot MapPlanes::kSpots[6] = {{9, 1, false}, {9, 1, true}, {7, -10, false},
                                              {7, 13, false}, {7, -10, true}, {7, 13, true}};

uint8_t MapPlanes::colourIndex(const SceneFlight& f) const {
    if (t_.brandFill) {
        const data::Airline* al = data::airlineForCallsign(f.callsign);
        return al ? uint8_t(4 + (al - data::kAirlines)) : 3;
    }
    return f.altFt < 10000 ? 0 : f.altFt <= 25000 ? 1 : 2;
}

Rgb MapPlanes::colourOf(uint8_t i) const {
    switch (i) {
        case 0: return t_.mapLow;
        case 1: return t_.mapMid;
        case 2: return t_.mapHigh;
        case 3: return t_.brandNeutral;
        default: return data::kAirlines[i - 4].color;
    }
}

// Trail point i on screen, in whole pixels; the same arithmetic for the box and for drawing.
bool MapPlanes::trailPoint(const MapTrails::Track& t, int i, int& x, int& y) const {
    if (i >= t.count) return false;
    float e, n;
    t.at(i, e, n);
    const basemap::MapView& v = base_.view();
    x = int(std::floor(v.cx + e * v.pxPerNm)), y = int(std::floor(v.cy - n * v.pxPerNm));
    return true;
}

// In half pixels; whole ones over the detailed chart, where a plane that moves redraws from the card (a half-pixel
// step every frame or two would read its tiles that often).
bool MapPlanes::place(const SceneFlight& f, int16_t& x2, int16_t& y2) const {
    const basemap::MapView& v = base_.view();
    const float a = f.bearingDeg * kDeg;
    const float fx = v.cx + std::sin(a) * f.distNm * v.pxPerNm, fy = v.cy - std::cos(a) * f.distNm * v.pxPerNm;
    const long x = base_.detailed() ? 2 * std::lround(fx) : std::lround(fx * 2);
    const long y = base_.detailed() ? 2 * std::lround(fy) : std::lround(fy * 2);
    if (x < -16 || x > 496 || y < -16 || y > 2 * MapBase::kMapH) return false;
    x2 = int16_t(x), y2 = int16_t(y);
    return true;
}

// Colour, text and trail at (x2, y2); the label spot and the box come later (layout, sync).
MapPlanes::Plane MapPlanes::make(const SceneFlight& f, int16_t x2, int16_t y2, bool label) const {
    Plane p;
    p.hex = f.hex;
    p.x2 = x2, p.y2 = y2;
    p.heading = int16_t(int(std::lround(f.trackDeg)) % 360);
    p.colour = colourIndex(f);
    p.shape = f.shape;
    p.selected = f.hex == selected_;
    p.label = label || p.selected;
    if (p.label) {
        std::snprintf(p.line1, sizeof p.line1, "%s", f.callsign);
        char h[6];
        ui::formatHundreds(h, sizeof h, f.altFt);
        std::snprintf(p.line2, sizeof p.line2, "%s%s", h, ui::trendArrow(f.vrateFpm));
        p.w1 = uint8_t(textWidth(kFontSmall, p.line1)), p.w2 = uint8_t(textWidth(kFontSmall, p.line2));
    }
    if (const MapTrails::Track* tr = trails_.find(f.hex)) {
        p.trail = tr->count;
        uint32_t sig = tr->count + 256u * p.colour;  // a band change recolours the whole trail
        int tx, ty;
        for (int i = 0; trailPoint(*tr, i, tx, ty); ++i) sig = sig * 31 + uint32_t(tx) * 7919u + uint32_t(ty);
        p.trailSig = sig;
    }
    return p;
}

Rect MapPlanes::labelBox(const Plane& p, int i, int& lx, int& ly) const {
    const Spot& s = kSpots[i];
    const Align al = s.left ? Align::Right : Align::Left;
    // Clear of the selection ring, and of an icon bigger than the 12 px glyph the spots were set for.
    const int x = p.x2 >> 1, y = p.y2 >> 1, out = data::labelPushOut(data::Family(p.shape), p.selected);
    data::labelAnchor(x, y, s.dx, s.dy, s.left, out, lx, ly);
    return ui::textBox(kFontSmall, lx, ly, p.w1, al).unite(ui::textBox(kFontSmall, lx, ly + 10, p.w2, al));
}

uint32_t MapPlanes::sigOf(const Plane& p) {
    const int16_t pos[3] = {p.x2, p.y2, p.heading};
    const uint8_t flags[6] = {p.trail, uint8_t(p.label), uint8_t(p.selected), p.spot, p.colour, p.shape};
    uint32_t h = ui::hashBytes(&p.hex, sizeof p.hex);
    h = ui::hashBytes(pos, sizeof pos, h);
    h = ui::hashBytes(flags, sizeof flags, h);
    h = ui::hashBytes(&p.trailSig, sizeof p.trailSig, h);
    return ui::hashStr(p.line2, ui::hashStr(p.line1, h));
}

int MapPlanes::coverCost(const Rect& r, int self, uint32_t placed, bool& textHit) const {
    const Rect clear{r.x - 4, r.y - 1, r.w + 8, r.h + 2};  // labels need a gap to read apart
    int cost = 0;
    textHit = false;
    auto soft = [&](const Rect& o) {  // over a dot or a plane
        Rect i = clear.intersect(o);
        if (!i.empty()) cost += i.w * i.h;
    };
    auto hard = [&](const Rect& o) {  // text over text is worst; text that only crowds less so
        Rect i = clear.intersect(o);
        if (i.empty()) return;
        Rect hit = r.intersect(o);
        cost += i.w * i.h * 4 + (hit.empty() ? 0 : hit.w * hit.h * 12);
        textHit |= !hit.empty();
    };
    // The chart: airport (and town) dots and labels, home, the header, north arrow and scale bar.
    for (int i = 0; i < base_.chartMarkCount(); ++i) {
        const MapBase::ChartMark m = base_.chartMark(i);
        soft({m.x - 3, m.y - 3, 7, 7});
        if (m.labelled) hard(m.box);
    }
    soft(base_.homeBox());
    for (int i = 0; i < MapBase::kFixedText; ++i) hard(base_.fixedText()[i]);
    // The other planes and the labels placed so far.
    int lx, ly;
    for (int k = 0; k < Scene::kMaxFlights; ++k) {
        const Plane& o = planes_[k];
        if (!o.hex || k == self) continue;
        const int h = o.shape ? data::iconReach(data::Family(o.shape), false) : 4;  // the icon, else the glyph
        soft({(o.x2 >> 1) - h, (o.y2 >> 1) - h, 2 * h + 1, 2 * h + 1});
        if (placed >> k & 1) hard(labelBox(o, o.spot, lx, ly));
    }
    return cost;
}

Rect MapPlanes::movingBox(const Plane& p) const {
    const int x = p.x2 >> 1, y = p.y2 >> 1;
    const int r = data::iconReach(data::Family(p.shape), p.selected);  // the selection ring included
    Rect box{x - r, y - r, 2 * r + 1, 2 * r + 1};
    int lx, ly;
    if (p.label && p.spot < 6) box = box.unite(labelBox(p, p.spot, lx, ly));
    if (const MapTrails::Track* tr = p.trail ? trails_.find(p.hex) : nullptr) {
        int tx, ty;
        if (trailPoint(*tr, tr->count - 1, tx, ty)) box = box.unite({tx - 2, ty - 2, 5, 5});
    }
    return box;
}

bool MapPlanes::layout(int self, uint32_t placed) {
    // The first spot that covers nothing, else the one that covers least; the spot used last time
    // gets a head start so labels don't hop as traffic moves.
    Plane& p = planes_[self];
    static constexpr uint8_t kRightFirst[] = {0, 1, 2, 3, 4, 5}, kLeftFirst[] = {1, 0, 4, 5, 2, 3};
    const uint8_t* order = (p.x2 >> 1) >= 170 ? kLeftFirst : kRightFirst;
    constexpr int kSticky = 12;
    const Rect area{2, 2, 236, MapBase::kMapH - 4};
    int best = order[0], bestCost = 1 << 30, lx, ly;
    bool bestHit = false;
    for (int n = 0; n < 6 && bestCost; ++n) {
        const int i = order[n];
        Rect r = labelBox(p, i, lx, ly);
        Rect in = r.intersect(area);
        int cost = (r.w * r.h - (in.empty() ? 0 : in.w * in.h)) * 8;  // off the chart
        if (p.spot < 6 && i != p.spot) cost += kSticky;
        bool hit;
        cost += coverCost(r, self, placed, hit);
        if (cost < bestCost) bestCost = cost, best = i, bestHit = hit;
    }
    p.spot = uint8_t(best);
    return !bestHit;
}

// Planes keep their slot while they stay on the chart, so the sync works in place: of the last one
// it keeps only each slot's plane, signature, trail signature and boxes (~1.4 KB of stack in all).
void MapPlanes::sync(DamageList& damage) {
    dirty_ = false;
    version_ = base_.version();
    constexpr int kN = Scene::kMaxFlights;
    struct Was {
        uint32_t hex, sig, trailSig;
        Rect box, move;
    } was[kN];
    for (int k = 0; k < kN; ++k) {
        const Plane& p = planes_[k];
        // The trails have taken their new points already; `move` only counts when the trail is the same.
        was[k] = {p.hex, p.hex ? sigOf(p) : 0, p.trailSig, p.box(), p.hex ? movingBox(p) : Rect{}};
    }
    auto slotOf = [&](uint32_t hex) {
        for (int k = 0; k < kN; ++k)
            if (planes_[k].hex == hex) return k;
        return -1;
    };

    // Which flights are on the chart, and which slots keep their plane; the other slots empty. None while the
    // detailed chart is being drawn: the job's rows are saved as they are drawn, and the planes come back with the
    // version bump when it finishes.
    const int count = scene_ && base_.located() && base_.ready() ? scene_->count : 0;
    int8_t slot[kN];  // per flight: its slot, -2 new, -1 not on the chart
    uint32_t kept = 0;
    int16_t x2, y2;
    for (int i = 0; i < count; ++i) {
        const SceneFlight& f = scene_->flights[i];
        slot[i] = -1;
        if (!f.hex || !place(f, x2, y2)) continue;
        const int k = slotOf(f.hex);
        slot[i] = int8_t(k < 0 ? -2 : k);
        if (k >= 0) kept |= 1u << k;
    }
    for (int k = 0; k < kN; ++k)
        if (!(kept >> k & 1)) planes_[k] = Plane{};

    // The new state of every plane on the chart, nearest first (the nearest kLabels get labels);
    // new planes take empty slots.
    uint8_t order[kN];
    int n = 0, labels = 0;
    for (int i = 0; i < count; ++i) {
        if (slot[i] == -1) continue;
        const SceneFlight& f = scene_->flights[i];
        const int k = slot[i] >= 0 ? slot[i] : slotOf(0);
        if (k < 0) continue;
        place(f, x2, y2);
        Plane p = make(f, x2, y2, labels < kLabels);
        if (labels < kLabels) ++labels;
        p.spot = slot[i] >= 0 ? planes_[k].spot : 255;
        planes_[k] = p, order[n++] = uint8_t(k);
    }

    // Labels go on nearest first, each clear of the nearer ones; one that can only go over other
    // text is left off (the selected plane's always shows). A second pass lets an early label move
    // where it now covers less, as long as it still covers no text.
    uint32_t placed = 0;
    for (int j = 0; j < n; ++j) {
        Plane& p = planes_[order[j]];
        if (!p.label) continue;
        if (layout(order[j], placed) || p.selected) placed |= 1u << order[j];
        else p.label = false;
    }
    for (int j = 0; j < n; ++j) {
        const int k = order[j];
        if (!(placed >> k & 1)) continue;
        const uint8_t spot = planes_[k].spot;
        if (!layout(k, placed & ~(1u << k)) && !planes_[k].selected) planes_[k].spot = spot;
    }

    // Boxes (the glyph, the label and the trail), then damage what moved, changed, left or arrived,
    // merged down to a budget. A plane whose trail is the same redraws only what moves with it.
    constexpr int kPending = 24;
    Rect pending[kPending];
    int np = 0;
    auto add = [&](const Rect& r) {
        if (np < kPending) return void(pending[np++] = r);
        int best = 0;  // full: join the entry that grows least
        long bestGrow = -1;
        for (int j = 0; j < kPending; ++j) {
            const Rect u = pending[j].unite(r);
            const long grow = long(u.w) * u.h - long(pending[j].w) * pending[j].h;
            if (bestGrow < 0 || grow < bestGrow) bestGrow = grow, best = j;
        }
        pending[best] = pending[best].unite(r);
    };
    for (int k = 0; k < kN; ++k) {
        Plane& p = planes_[k];
        const Rect& old = was[k].box;
        if (!p.hex) {
            if (was[k].hex) add(old);  // gone
            continue;
        }
        const Rect move = movingBox(p);
        Rect box = move;
        if (const MapTrails::Track* tr = p.trail ? trails_.find(p.hex) : nullptr) {
            int tx, ty;
            for (int i = 0; trailPoint(*tr, i, tx, ty); ++i) box = box.unite({tx - 2, ty - 2, 5, 5});
        }
        p.bx = int16_t(box.x), p.by = int16_t(box.y), p.bw = int16_t(box.w), p.bh = int16_t(box.h);
        if (was[k].hex == p.hex && was[k].sig == sigOf(p) && old == box) continue;  // unchanged
        if (was[k].hex == p.hex && was[k].trailSig == p.trailSig) {
            add(was[k].move), add(move);
            continue;
        }
        if (was[k].hex) add(old);
        add(box);
    }
    damage.addMerged(pending, np, kMaxRects);
}

uint32_t MapPlanes::hitTest(int x, int y, int maxPx) const {
    planes_.check();
    uint32_t best = 0;
    float bestD = float(maxPx * maxPx);
    for (const Plane& p : planes_) {
        if (!p.hex) continue;
        float dx = p.x2 / 2.f - x, dy = p.y2 / 2.f - y, d = dx * dx + dy * dy;
        if (d <= bestD) bestD = d, best = p.hex;
    }
    return best;
}

void MapPlanes::update(float, DamageList& damage) {
    if (!planes_) return;
    planes_.check();
    if (dirty_ || version_ != base_.version()) sync(damage);
}

void MapPlanes::drawTrail(Canvas& c, const Plane& p, float x, float y, Rgb col) const {
    const MapTrails::Track* tr = trails_.find(p.hex);
    if (!tr || !p.trail) return;
    // One line from the oldest point to the plane, fading out towards the oldest (mockup 5A).
    int tx, ty;
    float px = 0, py = 0;
    for (int i = 0; i < p.trail && trailPoint(*tr, i, tx, ty); ++i) {
        const float fx = tx + 0.5f, fy = ty + 0.5f;
        if (i) aaLine(c, px, py, fx, fy, 1.5f, col, uint8_t(20 + 150 * i / p.trail));
        px = fx, py = fy;
    }
    aaLine(c, px, py, x, y, 1.5f, col, 170);
}

void MapPlanes::draw(Canvas& c) {
    if (!planes_ || !base_.located()) return;
    planes_.check();
    const Rect saved = c.clip();
    c.setClip(saved.intersect({0, 0, 240, MapBase::kMapH}));
    // Trails, then planes, then labels, so nothing crosses a plane or a label.
    for (const Plane& p : planes_)
        if (p.hex && c.clip().intersects(p.box())) drawTrail(c, p, p.x2 / 2.f, p.y2 / 2.f, colourOf(p.colour));
    for (const Plane& p : planes_) {
        if (!p.hex || !c.clip().intersects(p.box())) continue;
        const float x = p.x2 / 2.f, y = p.y2 / 2.f;
        const data::Family fam = data::Family(p.shape);
        if (p.selected) aaRing(c, x, y, data::selectRingR(fam), 1.2f, t_.accent);
        data::drawTop(c, fam, x, y, float(data::iconPx(fam)), p.heading, colourOf(p.colour));
    }
    for (const Plane& p : planes_) {
        if (!p.hex || !p.label || p.spot >= 6 || !c.clip().intersects(p.box())) continue;
        int lx, ly;
        labelBox(p, p.spot, lx, ly);
        const Align al = kSpots[p.spot].left ? Align::Right : Align::Left;
        haloText(c, lx, ly, p.line1, p.w1, t_.text, al, t_.mapWater);
        haloText(c, lx, ly + 10, p.line2, p.w2, colourOf(p.colour), al, t_.mapWater);
    }
    c.setClip(saved);
}

}  // namespace scr
