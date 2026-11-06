#include "AlertScreen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "alerts/AlertEngine.h"
#include "alerts/AlertLights.h"
#include "alerts/AlertRules.h"
#include "data/Airlines.h"
#include "data/Shapes.h"
#include "gfx/Raster.h"
#include "gfx/StripRenderer.h"
#include "gfx/fonts/Fonts.h"
#include "ui/Format.h"
#include "ui/Identity.h"
#include "ui/Widgets.h"
#include "util/Str.h"

namespace scr {
using namespace gfx;
using util::copyStr;

namespace {
// Cinematic Sky: three rings travel outward from the plane, one every 0.5 s.
constexpr float kCx = 120, kCy = 100;
constexpr float kRingS = 1.5f, kRingR0 = 20, kRingTravel = 56;
constexpr int kRingSteps = 30;              // ring positions per trip (20 a second)
constexpr Rect kRingBox{40, 20, 160, 160};  // centred on the plane; holds the largest ring (76 px + AA)
constexpr float kTargetPx = 40;  // the target's shape box
constexpr int kField = 150;  // Bold Editorial: the colour field's height
constexpr int kBorderSteps = 15;
// Baselines. Cinematic Sky: hero, sub, the pill's top, stats. Bold Editorial: reason, hero, sub,
// the stat values and their labels.
constexpr int kHeroY = 218, kSubY = 238, kPillY = 248, kPillH = 22, kStatsY = 293;
constexpr int kEdReasonY = 132, kEdHeroY = 206, kEdSubY = 228, kEdStatY = 264, kEdLabelY = 280;
// The airline's badge left of the hero, its middle on the hero's cap height (Inter ExtraBold's caps are 25 px,
// the condensed face's 28): Cinematic one centred group [badge][gap][hero] (the mockup's 9 px gap), Bold
// Editorial the badge at the margin and the hero after it. A hero too wide beside it takes the title face,
// its baseline kSmallHeroUp higher so its caps stay level with the badge.
constexpr int kBadge = 40, kBadgeGap = 9, kHeroAfterBadge = kBadge + kBadgeGap;
// Cinematic: 2 px above centring on the caps, for 4 px of clear space above the sub line. Its top, y 184, is the
// top of the hero's damage rows (rows(): kHeroY - Inter ExtraBold's 33 px ascent - 1).
constexpr int kBadgeY = kHeroY - 34, kEdBadgeX = 14, kEdBadgeY = kEdHeroY - 34, kEdHeroX = kEdBadgeX + 48;
constexpr int kHeroRoom = 216, kEdHeroRoom = 240 - 14 - kEdHeroX;  // the group's / the hero's widest
constexpr int kSmallHeroUp = 4;
static_assert(sizeof(AlertLogo) <= ScreenArena::kBytes, "the Alert's logo must fit the arena");

// The full-width rows from `top`'s line at baseline y0 to `bottom`'s at y1 (the shadow included).
Rect rows(const FontData& top, int y0, const FontData& bottom, int y1) {
    const int a = y0 - top.ascent - 1;
    return {0, a, 240, y1 + bottom.descent + 3 - a};
}

// Ring i's radius at a step (drawCinematic draws exactly this).
float ringRadius(int step, int i) {
    return kRingR0 + kRingTravel * std::fmod(step / float(kRingSteps) + i / 3.f, 1.f);
}

// A ring step changes only the annulus its rings, old and new, cover (aaRing reaches 1.5 px either
// side of the radius): per 16-row band (the device's strips), its columns, split round the hollow
// middle when that is wide enough to leave out. The corners of the ring box and the middle, the
// plane included, are left alone.
void addRings(DamageList& d, int from, int to) {
    float lo = 1e9f, hi = 0;
    for (int step : {from, to})
        for (int i = 0; i < 3; ++i) {
            const float r = ringRadius(step, i);
            lo = std::fmin(lo, r), hi = std::fmax(hi, r);
        }
    const float in = lo - 2.5f, out = hi + 2.5f;  // the 1.5 px reach and a pixel to spare
    constexpr int kBand = 16;
    const int top = int(std::floor(kCy - out)), bottom = int(std::ceil(kCy + out)) + 1;
    for (int y = top - ((top % kBand) + kBand) % kBand; y < bottom; y += kBand) {
        const int y0 = std::max(y, top), y1 = std::min(y + kBand, bottom);
        // Pixel centres' rows: nearest to and farthest from the centre.
        const float a = y0 + 0.5f - kCy, b = y1 - 0.5f - kCy;
        const float near = a > 0 ? a : b < 0 ? -b : 0, far = std::fmax(std::fabs(a), std::fabs(b));
        if (near >= out) continue;
        const float h = std::sqrt(out * out - near * near);
        const int x0 = int(std::floor(kCx - h - 0.5f)) - 1, x1 = int(std::ceil(kCx + h - 0.5f)) + 1;
        int h0 = x1, h1 = x1;  // the hollow's columns: every pixel there lies within `in`
        if (in > 0 && far < in) {
            const float hh = std::sqrt(in * in - far * far);
            h0 = int(std::ceil(kCx - hh - 0.5f)) + 1, h1 = int(std::floor(kCx + hh - 0.5f));
        }
        if (h1 - h0 >= StripRenderer::kMinSpanGap) {
            d.add({x0, y0, h0 - x0, y1 - y0});
            d.add({h1, y0, x1 - h1, y1 - y0});
        } else {
            d.add({x0, y0, x1 - x0, y1 - y0});
        }
    }
}

void addBorder(DamageList& d, int w) {
    d.add({0, 0, 240, w});
    d.add({0, 320 - w, 240, w});
    d.add({0, w, w, 320 - 2 * w});
    d.add({240 - w, w, w, 320 - 2 * w});
}
}  // namespace

Rect AlertView::bar() const { return t_.flat ? Rect{14, 298, 212, 4} : Rect{24, 304, 192, 3}; }

void AlertView::loadLogo() {
    AlertLogo* lg = scratch_ ? scratch_->data() : nullptr;
    if (!lg || !arena_ || !arena_->owns(owner_) || !scene_) return;
    ui::reloadLogo(logos_, scene_->alert.flight.callsign, lg->code, lg->badge);
}

const AlertLogo* AlertView::loaded() const {
    const AlertLogo* lg = scratch_ ? scratch_->data() : nullptr;
    if (!lg || !arena_ || !arena_->owns(owner_) || lg->badge.badge.empty()) return nullptr;
    return lg;
}

void AlertView::build(Text& t) const {
    const SceneAlert& a = scene_->alert;
    const SceneFlight& f = a.flight;
    const bool squawk = a.kind == AlertKind::Emergency && alerts::squawkMeaning(f.squawk);
    // Editorial puts the kind (or squawk) huge in the field, so its small title only says what this is.
    copyStr(t.title, sizeof t.title, t_.flat && !squawk ? "ALERT" : alerts::kindName(a.kind));
    copyStr(t.hero, sizeof t.hero, f.callsign[0] ? f.callsign : "UNKNOWN");
    const bool reg = f.reg[0] && std::strcmp(f.reg, f.callsign) != 0;
    std::snprintf(t.sub, sizeof t.sub, "%s%s%s", f.type, f.type[0] && reg ? " · " : "", reg ? f.reg : "");
    alerts::reasonLine(a, !t_.flat, t.reason, sizeof t.reason);
    copyStr(t.big, sizeof t.big, squawk ? f.squawk : alerts::kindName(a.kind));
    ui::formatDistance(t.stat[0], sizeof t.stat[0], f.distNm);
    ui::formatAltitude(t.stat[1], sizeof t.stat[1], f.altFt);
    std::snprintf(t.stat[2], sizeof t.stat[2], "%d kt", int(f.gsKt + 0.5f));
    std::snprintf(t.stats, sizeof t.stats, "%s · %s · %s", t.stat[0], t.stat[1], t.stat[2]);
    t.heading = int(f.trackDeg + 0.5f) % 360;
    t.shape = f.shape;
    t.kind = a.kind;
    t.live = a.live;
    t.id = ui::IdentityKey::of(f.callsign, f.shape);
    if (t.id.airline) t.id.family = 0;
    const AlertLogo* lg = t.id.airline ? loaded() : nullptr;
    t.idLogo = lg ? &lg->badge.badge : nullptr;
    if (lg) std::memcpy(t.idCode, lg->code, sizeof t.idCode);
}

void AlertView::damageChanged(const Text& t, DamageList& damage) const {
    const Text& o = text_;
    // The badge sits on the hero's rows (and in Cinematic moves with its width).
    const bool badge = t.idLogo != o.idLogo || std::strcmp(t.idCode, o.idCode) ||
                       std::memcmp(&t.id, &o.id, sizeof t.id) != 0;
    const bool name = badge || std::strcmp(t.hero, o.hero) || std::strcmp(t.sub, o.sub);
    const bool reason = std::strcmp(t.reason, o.reason) != 0;
    const bool stats = std::strcmp(t.stats, o.stats) != 0;  // holds every stat[]
    if (t_.flat) {
        if (reason) damage.add(rows(kFontBold, kEdReasonY, kFontBold, kEdReasonY));
        if (name) damage.add(rows(*t_.fontDisplay, kEdHeroY, kFontBody, kEdSubY));
        if (stats) damage.add(rows(kFontBold, kEdStatY, kFontSmall, kEdLabelY));  // the labels move too
    } else {
        if (t.heading != o.heading || t.shape != o.shape) damage.add(kRingBox);
        if (name) damage.add(rows(*t_.fontDisplay, kHeroY, kFontBody, kSubY));
        if (reason) damage.add({0, kPillY, 240, kPillH});
        if (stats) damage.add(rows(kFontBold, kStatsY, kFontBold, kStatsY));
    }
}

void AlertView::update(float dt, DamageList& damage) {
    if (!scene_) return;
    const SceneAlert& a = scene_->alert;
    if (a.startMs != startMs_) startMs_ = a.startMs, t_s_ = 0, full_ = true;
    else t_s_ += dt;
    if (!built_ || std::memcmp(&a, &seen_, sizeof a)) {
        loadLogo();  // when the airline changes (and after enter(), into the fresh scratch)
        Text t{};  // zeroed, so the comparison sees no stale bytes
        build(t);
        if (std::memcmp(&t, &text_, sizeof t)) {
            if (t.kind != text_.kind || t.live != text_.live || std::strcmp(t.title, text_.title) ||
                std::strcmp(t.big, text_.big))
                full_ = true;
            else if (!full_)
                damageChanged(t, damage);
            text_ = t;
        }
        layout();
        std::memcpy(&seen_, &a, sizeof a);  // padding too, for the memcmp
        built_ = true;
    }

    const float level = alerts::lightsFor(a, uint32_t(t_s_ * 1000)).border;
    const uint8_t border = uint8_t(level * kBorderSteps + 0.5f);
    const uint8_t ring = a.soft ? 0 : uint8_t(int(std::fmod(t_s_, kRingS) / kRingS * kRingSteps) % kRingSteps);
    const float left = 1 - std::fmin(t_s_ / (alerts::AlertEngine::kShowMs / 1000.f), 1.f);
    const Rect b = bar();
    const int barW = int(left * b.w + 0.5f);
    if (full_) {
        damage.addAll();
        full_ = false;
    } else {
        if (border != border_) addBorder(damage, borderWidth());
        if (!t_.flat && ring != ring_) addRings(damage, ring_, ring);
        if (barW != bar_) damage.add(b);
    }
    border_ = border, ring_ = ring, bar_ = barW;
}

void AlertView::drawBorder(Canvas& c, Rgb col) const {
    if (!border_) return;
    const float level = border_ / float(kBorderSteps);
    const int w = borderWidth();
    // Flat: a solid frame. Cinematic: 3 px solid, fading out over the rest.
    for (int i = 0; i < w; ++i) {
        const float fade = t_.flat || i < 3 ? 1.f : 1.f - float(i - 2) / float(w - 2);
        const uint8_t al = uint8_t(255 * level * fade + 0.5f);
        if (!al) continue;
        c.fillBlend({i, i, 240 - 2 * i, 1}, col, al);
        c.fillBlend({i, 319 - i, 240 - 2 * i, 1}, col, al);
        c.fillBlend({i, i + 1, 1, 318 - 2 * i}, col, al);
        c.fillBlend({239 - i, i + 1, 1, 318 - 2 * i}, col, al);
    }
}

void AlertView::layout() {
    Look& l = look_;
    const Rgb col = alerts::kindColour(text_.kind);
    l.title = ui::readableOn(col, t_.boardBgTop, 4.5f);
    l.ink = ui::textOn(col);
    // Centred lines: their left ends, as drawText's centring would place them.
    auto left = [](const FontData& f, const char* s) { return 120 - textWidth(f, s) / 2; };
    const int room = t_.flat ? kEdHeroRoom : kHeroRoom - kHeroAfterBadge;
    const bool big = textWidth(*t_.fontDisplay, text_.hero) <= room;
    l.hero = big ? t_.fontDisplay : t_.fontTitle;
    l.heroY = (t_.flat ? kEdHeroY : kHeroY) - (big ? 0 : kSmallHeroUp);
    if (t_.flat) {
        l.badgeX = kEdBadgeX, l.heroX = kEdHeroX;
        l.big = textWidth(*t_.fontDisplay, text_.big) <= 212 ? t_.fontDisplay : t_.fontTitle;
        for (int i = 0; i < 3; ++i) l.statW[i] = textWidth(kFontBold, text_.stat[i]);
        return;
    }
    l.titleX = left(kFontBold, text_.title);
    l.badgeX = 120 - (kHeroAfterBadge + textWidth(*l.hero, text_.hero)) / 2;
    l.heroX = l.badgeX + kHeroAfterBadge;
    l.subX = left(kFontBody, text_.sub);
    // "7700 · GENERAL EMERGENCY" is too wide for Inter Bold inside the border: it takes the body face.
    l.pill = textWidth(kFontBold, text_.reason) <= 190 ? &kFontBold : &kFontBody;
    l.pillW = textWidth(*l.pill, text_.reason) + 20;
    l.reasonX = left(*l.pill, text_.reason);
    l.statsX = left(kFontBold, text_.stats);
}

namespace {
// Whether a line of text at baseline y can reach the canvas's rows (drawText's own test).
bool rowsIn(const Canvas& c, const FontData& f, int y) {
    return y + f.descent > c.clip().y && y - f.ascent < c.clip().bottom();
}
}  // namespace

void AlertView::drawBadge(Canvas& c, int x, int y) const {
    if (!c.clip().intersects({x, y, kBadge, kBadge})) return;
    if (text_.idLogo && scratch_) scratch_->check();
    ui::drawIdentity(c, x, y, kBadge, text_.id.get(text_.idLogo), t_);
}

void AlertView::drawCinematic(Canvas& c, Rgb col) const {
    const Look& l = look_;
    const Rect& clip = c.clip();
    const Rgb stops[] = {t_.boardBgTop, t_.boardBgBottom};
    const float pos[] = {0, 1};
    ui::vFill(c, {0, 0, 240, 320}, stops, pos, 2, t_);
    ui::glow(c, kCx, kCy, 96, col, 70, t_);
    if (clip.intersects(kRingBox)) {
        const float phase = ring_ / float(kRingSteps);
        for (int i = 0; i < 3; ++i) {
            const float f = std::fmod(phase + i / 3.f, 1.f);
            aaRing(c, kCx, kCy, kRingR0 + kRingTravel * f, 2, col, uint8_t(190 * (1 - f)));  // as ringRadius()
        }
    }
    // The shape reaches 0.71 of its box from the centre (the rotor and the box diagonals), 29 px, and its
    // shadow 2 px more.
    if (clip.intersects({int(kCx) - 31, int(kCy) - 31, 64, 64})) {
        const data::Family fam = data::Family(text_.shape);
        data::drawTop(c, fam, kCx + 1.5f, kCy + 2, kTargetPx, float(text_.heading), t_.shadow, 60);
        data::drawTop(c, fam, kCx, kCy, kTargetPx, float(text_.heading), t_.text);
    }
    const Rgb val = text_.live ? t_.text : t_.textDim;
    const FontData& hero = *l.hero;
    if (rowsIn(c, kFontBold, 30)) drawText(c, kFontBold, l.titleX, 30, text_.title, l.title);
    drawBadge(c, l.badgeX, kBadgeY);
    if (rowsIn(c, hero, l.heroY) || rowsIn(c, hero, l.heroY + 1)) {
        drawText(c, hero, l.heroX + 1, l.heroY + 1, text_.hero, t_.shadow, 110);  // ui::shadowText's shadow
        drawText(c, hero, l.heroX, l.heroY, text_.hero, val);
    }
    if (rowsIn(c, kFontBody, kSubY)) drawText(c, kFontBody, l.subX, kSubY, text_.sub, t_.textSoft);
    if (clip.intersects({0, kPillY, 240, kPillH}))
        fillRoundRect(c, {120 - l.pillW / 2, kPillY, l.pillW, kPillH}, kPillH / 2, col);
    if (rowsIn(c, *l.pill, kPillY + 16)) drawText(c, *l.pill, l.reasonX, kPillY + 16, text_.reason, l.ink);
    if (rowsIn(c, kFontBold, kStatsY)) drawText(c, kFontBold, l.statsX, kStatsY, text_.stats, val);
}

void AlertView::drawEditorial(Canvas& c, Rgb col) const {
    const Look& l = look_;
    c.fill({0, 0, 240, kField}, rgb565(col));
    c.fill({0, kField, 240, 320 - kField}, rgb565(t_.boardBgBottom));
    const Rgb ink = l.ink;
    if (rowsIn(c, *t_.fontLabel, 30)) drawText(c, *t_.fontLabel, 14, 30, text_.title, ink);
    if (rowsIn(c, *l.big, 100)) drawText(c, *l.big, 14, 100, text_.big, ink);
    if (rowsIn(c, kFontBold, kEdReasonY)) drawText(c, kFontBold, 14, kEdReasonY, text_.reason, ink);
    const Rgb val = text_.live ? t_.text : t_.textDim;
    drawBadge(c, l.badgeX, kEdBadgeY);
    if (rowsIn(c, *l.hero, l.heroY)) drawText(c, *l.hero, l.heroX, l.heroY, text_.hero, val);
    if (rowsIn(c, kFontBody, kEdSubY)) drawText(c, kFontBody, 14, kEdSubY, text_.sub, t_.textSoft);
    if (!rowsIn(c, kFontBold, kEdStatY) && !rowsIn(c, kFontSmall, kEdLabelY)) return;
    // Three columns spread across the 212 px between the margins, the last flush right.
    static const char* const kLabels[] = {"DIST", "ALT", "SPEED"};
    const int gap = (212 - l.statW[0] - l.statW[1] - l.statW[2]) / 2;
    for (int i = 0, x = 14; i < 3; x += l.statW[i] + gap, ++i) {
        drawText(c, kFontBold, x, kEdStatY, text_.stat[i], val);
        drawText(c, kFontSmall, x, kEdLabelY, kLabels[i], t_.textDim);
    }
}

void AlertView::draw(Canvas& c) {
    if (!scene_) return;
    const Rgb col = alerts::kindColour(scene_->alert.kind);
    if (t_.flat) drawEditorial(c, col);
    else drawCinematic(c, col);
    const Rect b = bar();
    c.fill(b, rgb565(t_.rowFill));
    if (bar_ > 0) c.fill({b.x, b.y, bar_, b.h}, rgb565(col));
    drawBorder(c, col);
}

void AlertScreen::enter() {
    logo_.attach(arena_.claim<AlertLogo>(this), arena_, this);
    view_.reset();  // rebuilds, so the alert's logo loads into the fresh scratch
}

}  // namespace scr
