#include "RadarScreen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "data/Shapes.h"
#include "gfx/Font.h"
#include "gfx/Raster.h"
#include "gfx/fonts/Fonts.h"
#include "ui/Format.h"
#include "ui/Widgets.h"
#include "gfx/Math.h"

namespace scr {
using namespace gfx;

namespace {

Vec2 polar(float cx, float cy, float r, float deg) {
    return {cx + std::sin(deg * kDeg) * r, cy - std::cos(deg * kDeg) * r};
}

// Bounding box of the pie slice from a0 to a1 (degrees clockwise from up, a0 <= a1).
Rect sectorBox(float cx, float cy, float r, float a0, float a1) {
    float x0 = cx, x1 = cx, y0 = cy, y1 = cy;
    auto add = [&](float a) {
        Vec2 p = polar(cx, cy, r, a);
        x0 = std::fmin(x0, p.x), x1 = std::fmax(x1, p.x), y0 = std::fmin(y0, p.y), y1 = std::fmax(y1, p.y);
    };
    add(a0), add(a1);
    for (float a = std::ceil(a0 / 90.f) * 90.f; a < a1; a += 90.f) add(a);
    return {int(x0) - 2, int(y0) - 2, int(x1 - x0) + 5, int(y1 - y0) + 5};
}

// Columns [x0, x1] the pie slice from a0 to a1 (a0 <= a1, less than a full turn) covers
// within rows [y0, y1]. Returns false if it misses them. The slice's extent in a row band
// comes from its boundary: the apex, the two edges and the arc (where it crosses the band's
// top and bottom rows, and its left and right extremes).
bool sectorSpan(float cx, float cy, float r, float a0, float a1, float y0, float y1, float& x0, float& x1) {
    x0 = 1e9f, x1 = -1e9f;
    auto add = [&](float x) { x0 = std::fmin(x0, x), x1 = std::fmax(x1, x); };
    auto within = [&](float deg) { return wrap360(deg - a0) <= a1 - a0; };
    if (cy >= y0 && cy <= y1) add(cx);
    for (float a : {a0, a1}) {
        Vec2 p = polar(cx, cy, r, a);
        if (p.y >= y0 && p.y <= y1) add(p.x);
        float dy = p.y - cy;
        for (float y : {y0, y1}) {
            float t = dy != 0 ? (y - cy) / dy : -1.f;  // where the edge crosses row y
            if (t >= 0 && t <= 1) add(cx + (p.x - cx) * t);
        }
    }
    for (float y : {y0, y1}) {
        float v = (cy - y) / r;  // cos of the arc angle at row y
        if (v < -1 || v > 1) continue;
        float th = std::acos(v) / kDeg, dx = std::sqrt(1 - v * v) * r;
        if (within(th)) add(cx + dx);
        if (within(360.f - th)) add(cx - dx);
    }
    if (cy >= y0 && cy <= y1) {
        if (within(90.f)) add(cx + r);
        if (within(270.f)) add(cx - r);
    }
    return x0 <= x1;
}

}  // namespace

// ------------------------------------------------------------------ RadarScope

namespace {
uint32_t textHash(const char* line1, const char* line2) { return ui::hashStr(line2, ui::hashStr(line1)); }
}  // namespace

void RadarScope::attach(ScreenArena& a) {
    Scratch* s = a.claim<Scratch>(this);
    blips_.attach(s->blips, a, this), prev_.attach(s->prev, a, this);
    n_ = 0;
}

RadarScope::Drawn RadarScope::drawnOf(const Blip& b) {
    Drawn d;
    d.hex = b.hex, d.x = b.x, d.y = b.y, d.angle = b.angle;
    d.text = textHash(b.line1, b.line2);
    d.lx = b.lx, d.ly = b.ly;
    d.bx = int16_t(b.box.x), d.by = int16_t(b.box.y), d.bw = int16_t(b.box.w), d.bh = int16_t(b.box.h);
    d.level = b.level, d.spot = b.spot;
    d.label = b.label, d.selected = b.selected, d.alignRight = b.alignRight;
    d.heading = b.heading, d.shape = b.shape;
    return d;
}

bool RadarScope::sameDrawn(const Drawn& o, const Blip& b) {
    return o.x == b.x && o.y == b.y && o.label == b.label && o.selected == b.selected && o.lx == b.lx &&
           o.ly == b.ly && o.alignRight == b.alignRight && o.heading == b.heading && o.shape == b.shape &&
           o.box() == b.box && o.text == textHash(b.line1, b.line2);
}

Rect RadarScope::layout(Blip& b, const Rect* taken, int nTaken, int nSoft) const {
    // The icon and its selection ring (data::iconReach); never less than the glow's 7 px, which is all a
    // dot draws.
    const data::Family fam = data::Family(b.shape);
    const int h = std::max(8, data::iconReach(fam, b.selected));
    b.box = {int(b.x) - h, int(b.y) - h, 2 * h + 1, 2 * h + 1};
    if (!b.label) return {};
    const int out = data::labelPushOut(fam, b.selected);
    // Try spots around the blip, preferring the side away from the right edge, and take the
    // first that is clear of other labels, blips, the home marker and the header; failing
    // that, the one with the least overlap. The spot used last time gets a small head start
    // so labels don't hop between spots as traffic moves.
    struct Spot {
        int dx, dy;
        bool left;  // text extends left of the anchor
    };
    static constexpr Spot kSpots[] = {{8, 1, false}, {8, 1, true}, {6, -10, false},
                                      {6, 12, false}, {6, -10, true}, {6, 12, true}};
    static constexpr uint8_t kRightFirst[] = {0, 1, 2, 3, 4, 5}, kLeftFirst[] = {1, 0, 4, 5, 2, 3};
    const uint8_t* order = b.x >= 170 ? kLeftFirst : kRightFirst;
    constexpr int kSticky = 12;
    const Rect area{2, 26, 236, kBottom - 26};
    int best = 0, bestCost = 1 << 30;
    Rect bestLabel;
    for (int n = 0; n < 6 && bestCost; ++n) {
        int i = order[n];
        const Spot& s = kSpots[i];
        Align al = s.left ? Align::Right : Align::Left;
        int lx, ly;
        data::labelAnchor(int(b.x), int(b.y), s.dx, s.dy, s.left, out, lx, ly);
        Rect r = ui::textBox(kFontSmall, lx, ly, b.line1, al).unite(ui::textBox(kFontSmall, lx, ly + 10, b.line2, al));
        Rect in = r.intersect(area);
        int cost = (r.w * r.h - (in.empty() ? 0 : in.w * in.h)) * 4;  // off the scope area
        if (b.spot < 6 && i != b.spot) cost += kSticky;
        Rect clear{r.x - 2, r.y - 1, r.w + 4, r.h + 2};  // labels need a gap to read apart
        for (int k = 0; k < nTaken; ++k) {
            Rect o = clear.intersect(taken[k]);
            if (!o.empty()) cost += o.w * o.h * (k < nSoft ? 1 : 4);  // text over text is worst
        }
        if (cost < bestCost) bestCost = cost, best = i, bestLabel = r, b.lx = int16_t(lx), b.ly = int16_t(ly);
    }
    b.spot = uint8_t(best);
    b.alignRight = kSpots[best].left;
    b.box = b.box.unite(bestLabel);
    return bestLabel;
}

uint8_t RadarScope::levelFor(const Blip& b) const {
    // Brightest just after the sweep passes, fading linearly over one turn to a faint glow.
    float since = wrap360(sweep_ - b.angle) / 360.f;
    float lit = 1.f - since * 0.85f;
    return uint8_t(lit * 15.f + 0.5f);
}

void RadarScope::syncScene(DamageList& damage) {
    sceneDirty_ = false;
    int prevN = n_;
    for (int i = 0; i < prevN; ++i) prev_[i] = drawnOf(blips_[i]);
    n_ = 0;
    float range = scene_ ? scene_->radiusNm : 25;
    if (range != range_) damage.add({0, 0, 240, kBottom});  // ring labels and every blip move
    range_ = range;
    int labels = 0;
    for (int i = 0; scene_ && i < scene_->count; ++i) {
        const SceneFlight& f = scene_->flights[i];
        if (f.distNm > range_ * 1.02f) continue;
        Blip& b = blips_[n_++];
        b = Blip{};
        b.hex = f.hex;
        float d = std::fmin(f.distNm / range_, 1.f) * kR;
        Vec2 p = polar(kCx, kCy, d, f.bearingDeg);
        b.x = p.x, b.y = p.y, b.angle = f.bearingDeg;
        // The icon turns in 3° steps, so small track wobbles don't redraw it; a dot has no heading.
        b.shape = f.shape;
        b.heading = b.shape ? int16_t(std::lround(f.trackDeg / 3) * 3 % 360) : 0;
        // Sub-pixel drift is not redrawn, so keep drawing where the blip was last drawn.
        for (int k = 0; k < prevN; ++k)
            if (prev_[k].hex == b.hex) {
                b.spot = prev_[k].spot;
                if (std::fabs(prev_[k].x - b.x) < 0.3f && std::fabs(prev_[k].y - b.y) < 0.3f)
                    b.x = prev_[k].x, b.y = prev_[k].y, b.angle = prev_[k].angle;
            }
        b.selected = f.hex == selected_;
        b.label = labels < kLabels || b.selected;
        if (labels < kLabels) ++labels;
        std::snprintf(b.line1, sizeof b.line1, "%s", f.callsign);
        char h[6];
        ui::formatHundreds(h, sizeof h, f.altFt);
        std::snprintf(b.line2, sizeof b.line2, "%s%s", h, ui::trendArrow(f.vrateFpm));
    }

    // Labels go on nearest first, each avoiding what is already there; two more passes let
    // an early label move out of the way of ones placed after it.
    Rect taken[Scene::kMaxFlights + 1 + 4 + kLabels + 1];
    Rect labelBox[Scene::kMaxFlights] = {};
    int base = 0;
    for (int i = 0; i < n_; ++i) {
        const Blip& b = blips_[i];
        const int h = b.shape ? data::iconReach(data::Family(b.shape), false) : 3;  // the icon, else the dot
        taken[base++] = {int(b.x) - h, int(b.y) - h, 2 * h + 1, 2 * h + 1};
    }
    const int nSoft = base;  // a label may cover another blip's dot before it covers text
    taken[base++] = {int(kCx) - 5, int(kCy) - 5, 11, 11};  // home
    // The ring labels and the N marker are fixed text; blip labels steer clear of them.
    const float ringR[3] = {kR / 3, kR * 2 / 3, kR};
    const float ringNm[3] = {range_ / 3, range_ * 2 / 3, range_};
    for (int k = 0; k < 3; ++k) {
        // Formatted here, not in draw(), which runs once per strip.
        std::snprintf(ringLabel_[k], sizeof ringLabel_[k], k == 2 ? "%d nm" : "%d", int(ringNm[k] + 0.5f));
        taken[base++] = ui::textBox(kFontSmall, int(kCx) + 3, int(kCy - ringR[k]) + 10, ringLabel_[k], Align::Left);
    }
    taken[base++] = ui::textBox(kFontSmall, int(kCx), int(kCy - kR) - 3, "N", Align::Center);
    for (int pass = 0; pass < 3; ++pass) {
        for (int i = 0; i < n_; ++i) {
            if (!blips_[i].label && pass) continue;
            int nTaken = base;
            for (int j = 0; j < n_; ++j)
                if (j != i && !labelBox[j].empty() && nTaken < int(sizeof taken / sizeof taken[0]))
                    taken[nTaken++] = labelBox[j];
            labelBox[i] = layout(blips_[i], taken, nTaken, nSoft);
        }
    }

    // Damage only what moved or changed, merged down to a budget that leaves room in the
    // damage list for the sweep (live data moves most blips a little on every update).
    constexpr int kMaxBlipRects = 10;
    Rect pending[Scene::kMaxFlights * 3];
    int nPending = 0;
    for (int i = 0; i < n_; ++i) {
        Blip& b = blips_[i];
        b.level = levelFor(b);
        bool same = false;
        for (int k = 0; k < prevN; ++k) {
            const Drawn& o = prev_[k];
            if (o.hex != b.hex) continue;
            same = sameDrawn(o, b);
            // The fade loop in update() damages level changes, so it must see the drawn level.
            if (same) b.level = o.level;
            else pending[nPending++] = o.box();
            prev_[k].hex = 0;  // matched
            break;
        }
        if (!same) pending[nPending++] = b.box;
    }
    for (int k = 0; k < prevN; ++k)
        if (prev_[k].hex) pending[nPending++] = prev_[k].box();  // gone
    damage.addMerged(pending, nPending, kMaxBlipRects);
    std::snprintf(header_, sizeof header_, "RADAR · %d NM", int(range_ + 0.5f));
    std::snprintf(count_, sizeof count_, "%d aircraft", n_);
}

uint32_t RadarScope::hitTest(int x, int y, int maxPx) const {
    uint32_t best = 0;
    float bestD = float(maxPx * maxPx);
    for (int i = 0; i < n_; ++i) {
        float dx = blips_[i].x - x, dy = blips_[i].y - y, d = dx * dx + dy * dy;
        if (d <= bestD) bestD = d, best = blips_[i].hex;
    }
    return best;
}

void RadarScope::addSweepDamage(float a0, float a1, DamageList& damage) {
    // The whole trail changes every frame (its fade moves with the sweep), but only the
    // slice itself: damage it 16 rows at a time (bands aligned to the renderer's 16- or
    // 32-row strips), each as wide as the slice is there, instead of its bounding box.
    // About 30% fewer pixels a frame. test_radar_damage checks it over whole turns.
    constexpr int kBand = 16, kPad = 2;  // pad: edge anti-aliasing and the tip line's width
    const float r = kR + 2;
    Rect box = sectorBox(kCx, kCy, r, a0, a1);
    for (int y = box.y - box.y % kBand; y < box.bottom(); y += kBand) {
        int top = std::max(y, box.y), bottom = std::min(y + kBand, box.bottom());
        float x0, x1;
        if (!sectorSpan(kCx, kCy, r, a0, a1, top - kPad, bottom + kPad, x0, x1)) continue;
        int l = int(std::floor(x0)) - kPad, rt = int(std::ceil(x1)) + kPad + 1;
        damage.add({l, top, rt - l, bottom - top});
    }
}

void RadarScope::update(float dt, DamageList& damage) {
    if (!blips_) return;  // not entered yet: nothing to sync or draw
    blips_.check();
    float prev = sweep_;
    float step = 360.f / kTurnS * dt;
    sweep_ = wrap360(sweep_ + step);
    if (full_) damage.add({0, 0, 240, kBottom});
    else addSweepDamage(prev - kTrailDeg, prev + step, damage);
    if (sceneDirty_) syncScene(damage);
    for (int i = 0; i < n_; ++i) {
        uint8_t l = levelFor(blips_[i]);
        if (l != blips_[i].level) blips_[i].level = l, damage.add(blips_[i].box);
    }
    uint32_t sig = ui::hashStr(header_, ui::hashStr(count_));
    if (sig != headerSig_) damage.add({0, 6, 240, 18});
    headerSig_ = sig;
    full_ = false;
}

void RadarScope::draw(Canvas& c) {
    blips_.check();
    // The background runs under the strip too.
    Rgb stops[] = {t_.radarBg[0], t_.radarBg[1], t_.radarBg[2]};
    float pos[] = {0.f, 0.62f, 1.f};
    ui::radialFill(c, {0, 0, 240, 320}, kCx, kCy, 150, 190, stops, pos, 3, t_);
    if (!c.clip().intersects({0, 0, 240, kBottom})) return;

    aaRing(c, kCx, kCy, kR, 1.2f, t_.radarLine, 140);
    aaRing(c, kCx, kCy, kR * 2 / 3, 1, t_.radarLine, 75);
    aaRing(c, kCx, kCy, kR / 3, 1, t_.radarLine, 75);
    aaLine(c, kCx - kR, kCy, kCx + kR, kCy, 1, t_.radarLine, 45);
    aaLine(c, kCx, kCy - kR, kCx, kCy + kR, 1, t_.radarLine, 45);
    const float ringR[3] = {kR / 3, kR * 2 / 3, kR};
    for (int k = 0; k < 3; ++k)
        drawText(c, kFontSmall, int(kCx) + 3, int(kCy - ringR[k]) + 10, ringLabel_[k], t_.radarLine, Align::Left, 150);
    drawText(c, kFontSmall, int(kCx), int(kCy - kR) - 3, "N", t_.radarLine, Align::Center, 210);

    // Flat: one even tint of the accent (signal yellow), no fading trail. At 64 every dither offset
    // lands on blend level 8, so the wedge is one solid tint (60 would hatch).
    if (t_.flat) aaWedge(c, kCx, kCy, kR, sweep_ - kTrailDeg, sweep_, t_.accent, 64, 64);
    else aaWedge(c, kCx, kCy, kR, sweep_ - kTrailDeg, sweep_, t_.radarLine, 0, 110);
    Vec2 tip = polar(kCx, kCy, kR, sweep_);
    aaLine(c, kCx, kCy, tip.x, tip.y, 1.4f, t_.radarText, 230);

    for (int i = 0; i < n_; ++i) {
        const Blip& b = blips_[i];
        if (!c.clip().intersects(b.box)) continue;
        float l = b.level / 15.f;
        ui::glow(c, b.x, b.y, 7, t_.radarBlip, uint8_t(30 + 170 * l), t_);
        const data::Family fam = data::Family(b.shape);
        if (b.shape) data::drawTop(c, fam, b.x, b.y, float(data::iconPx(fam)), float(b.heading), t_.radarBlipCore,
                                   uint8_t(70 + 185 * l));
        else aaCircle(c, b.x, b.y, t_.flat ? 2.2f : 1.6f, t_.radarBlipCore, uint8_t(70 + 185 * l));
        // Around an icon the ring sits just clear of its box (as on the Map); around a dot, at 6 px.
        if (b.selected) aaRing(c, b.x, b.y, b.shape ? data::selectRingR(fam) : 6, 1.2f, t_.accent);
        if (!b.label) continue;
        Align al = b.alignRight ? Align::Right : Align::Left;
        drawText(c, kFontSmall, b.lx, b.ly, b.line1, t_.text, al, 225);
        drawText(c, kFontSmall, b.lx, b.ly + 10, b.line2, t_.radarText, al, 200);
    }
    aaCircle(c, kCx, kCy, 2.5f, t_.home);
    drawText(c, kFontSmall, 12, 20, header_, t_.radarHeader);
    drawText(c, kFontSmall, 228, 20, count_, t_.radarText, Align::Right);
}

// ------------------------------------------------------------------ RadarStrip

const SceneFlight* RadarStrip::pick() const {
    if (!scene_ || !scene_->count) return nullptr;
    if (const SceneFlight* f = selected_ ? scene_->find(selected_) : nullptr) return f;
    return &scene_->flights[0];
}

void RadarStrip::compose(const SceneFlight& f, Text& t) {
    std::snprintf(t.callsign, sizeof t.callsign, "%s", f.callsign);
    if (f.hasRoute) std::snprintf(t.sub, sizeof t.sub, "%s → %s · %s", f.orig, f.dest, f.type);
    else std::snprintf(t.sub, sizeof t.sub, "%s", f.type);
    char a[16], d[12];
    ui::formatAltitude(a, sizeof a, f.altFt);
    const char* arrow = ui::trendArrow(f.vrateFpm);
    std::snprintf(t.alt, sizeof t.alt, "%s%s%s", a, arrow[0] ? " " : "", arrow);  // no trailing space when level
    ui::formatDistance(d, sizeof d, f.distNm);
    std::snprintf(t.dist, sizeof t.dist, "%s %s", d, ui::compassPoint(f.bearingDeg));
}

void RadarStrip::update(float, DamageList& damage) {
    const SceneFlight* f = pick();
    uint32_t sig = 1;
    if (f) {
        Text t;
        compose(*f, t);
        sig = ui::hashStr(t.dist, ui::hashStr(t.alt, ui::hashStr(t.sub, ui::hashStr(t.callsign))));
    }
    if (sig != sig_) damage.add({kBox.x - 2, kBox.y - 2, kBox.w + 4, kBox.h + 4});
    sig_ = sig;
    shown_ = f ? f->hex : 0;
}

void RadarStrip::draw(Canvas& c) {
    if (!c.clip().intersects(kBox)) return;
    ui::panel(c, kBox, 10, t_.radarCard, t_.radarCardEdge, t_);
    const SceneFlight* f = pick();
    if (!f) {
        drawText(c, kFontBody, 120, kBox.y + 28, "No aircraft in range", t_.radarText, Align::Center);
        return;
    }
    Text t;
    compose(*f, t);
    drawText(c, *t_.fontLabel, kBox.x + 10, kBox.y + 20, t.callsign, t_.text);
    drawText(c, kFontSmall, kBox.x + 10, kBox.y + 36, t.sub, t_.radarBlip);
    drawText(c, kFontSmall, kBox.right() - 10, kBox.y + 20, t.alt, t_.text, Align::Right);
    drawText(c, kFontSmall, kBox.right() - 10, kBox.y + 35, t.dist, t_.radarText, Align::Right);
}

// ------------------------------------------------------------------ RadarScreen

void RadarScreen::setSelection(uint32_t hex) {
    Screen::setSelection(hex);
    scope_.setSelection(hex), strip_.setSelection(hex);
}

NavRequest RadarScreen::onTap(int x, int y) {
    NavRequest r;
    if (y >= RadarScope::kBottom) {
        if (uint32_t hex = strip_.shownHex()) r.open = true, r.to = ScreenId::Closest, r.selectHex = hex;
        return r;
    }
    r.selectHex = scope_.hitTest(x, y, 20);
    return r;
}

}  // namespace scr
