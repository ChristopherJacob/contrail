#include "ClosestScreen.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "data/AircraftTypes.h"
#include "data/Airlines.h"
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
constexpr float kPlaneSize = 34;              // wingspan, px
constexpr float kTailGap = 15;                // contrail starts this far behind the plane's centre
constexpr float kTrailMax = 84, kTrailGrowS = 2.5f;
constexpr float kGlowR = 24, kBobPx = 1.5f;
constexpr float kBobRate = 1.3f, kBobPeriod = 2 * 3.14159265f / kBobRate;  // rad/s, and seconds per cycle
constexpr int kMargin = 14;                   // card text inset
const Rect kCompassBox{190, 8, 42, 42};
constexpr float kCompassX = 211, kCompassY = 29, kCompassR = 18;
constexpr int kNameX = 14, kNameY = 26;  // the airline's name in the band (Bold Editorial), left and baseline
// Bold Editorial's band badge (mockup "Closest, Editorial"): 30 px at the band's top-left, the name after it.
constexpr int kBandIdX = 10, kBandIdY = 8, kBandIdSide = 30, kNameAfterIdX = kBandIdX + kBandIdSide + 7;
// Cinematic's card badge (mockup "Closest, Cinematic"): 40 px left of the callsign, which moves right.
constexpr int kCardIdX = 12, kCardIdY = 145, kCardIdSide = 40, kCallsignAfterIdX = kCardIdX + kCardIdSide + 7;
const Rect kHeaderBox{0, 140, 240, 46}, kRouteBox{0, 186, 240, 50}, kFooterBox{0, 296, 240, 24};
constexpr int kShadowH = 8;  // soft shadow the card casts on the sky above it
const Rect kCardArea{0, InfoCard::kTop - kShadowH, 240, 320 - InfoCard::kTop + kShadowH};
// The outer tiles carry the longest sub-lines ("↓ 1,400 fpm", "NW · 51° up"); speed is short.
constexpr int kTileW[3] = {70, 62, 70}, kTileGap = 6, kTileX0 = 13, kTileY = 240, kTileH = 52;

}  // namespace

// ------------------------------------------------------------------ PlaneBand

void PlaneBand::setFlight(const SceneFlight* f) {
    if (!f) {
        if (has_) full_ = true;
        has_ = false, hex_ = 0;
        return;
    }
    if (!has_ || f->hex != hex_) trail_ = 0, full_ = true;  // a new flight: start a fresh contrail
    has_ = true, hex_ = f->hex, shape_ = f->shape, heading_ = f->trackDeg, bearing_ = f->bearingDeg;
}

void PlaneBand::setName(const char* name) {
    char up[sizeof name_];
    std::snprintf(up, sizeof up, "%s", name ? name : "");
    for (char* p = up; *p; ++p) *p = char(std::toupper((unsigned char)*p));
    if (std::strcmp(up, name_)) std::memcpy(name_, up, sizeof name_), full_ = true;
}

void PlaneBand::setIdentity(bool show, const ui::Identity& id, const char* code) {
    if (!show) code = "";
    const bool same = show == showId_ && (!show || (id.logo == id_.logo && id.airline == id_.airline &&
                                                    id.colours == id_.colours && id.family == id_.family &&
                                                    !std::strcmp(code, idCode_)));
    if (same) return;
    showId_ = show, id_ = show ? id : ui::Identity{};
    std::snprintf(idCode_, sizeof idCode_, "%s", code);
    full_ = true;  // the badge and the name after it
}

Rect PlaneBand::box(const Drawn& d) {
    if (!d.has) return {};
    float a = d.heading * kDeg, bx = -std::sin(a), by = std::cos(a);
    float r = std::fmax(kPlaneSize / 2, kGlowR) + 2;
    Rect plane{int(d.x - r), int(d.y - r), int(2 * r) + 2, int(2 * r) + 2};
    float tx = d.x + bx * kTailGap, ty = d.y + by * kTailGap;
    float ex = tx + bx * d.trail, ey = ty + by * d.trail;
    const float pad = 9;
    Rect trail{int(std::fmin(tx, ex) - pad), int(std::fmin(ty, ey) - pad), int(std::fabs(ex - tx) + 2 * pad) + 1,
               int(std::fabs(ey - ty) + 2 * pad) + 1};
    return plane.unite(trail).intersect(kBand);
}

void PlaneBand::update(float dt, DamageList& damage) {
    t_s_ += dt;
    if (t_s_ >= kBobPeriod) t_s_ -= kBobPeriod;  // stays small, so float precision never degrades the bob
    Drawn d;
    if (has_) {
        trail_ = std::fmin(kTrailMax, trail_ + kTrailMax / kTrailGrowS * dt);
        d.has = true;
        d.shape = shape_;
        d.heading = int(std::lround(heading_)) % 360;
        d.bearing = int(std::lround(bearing_)) % 360;
        // Sit the plane off-centre, ahead along its heading, so the contrail has the band to grow into.
        float a = d.heading * kDeg;
        float x = 120 + std::sin(a) * 40, y = 68 - std::cos(a) * 22;
        // Clear of the sun disc and the compass, which would otherwise fight it for attention.
        auto push = [&x, &y](float ax, float ay, float minD) {
            float dx = x - ax, dy = y - ay, dd = std::sqrt(dx * dx + dy * dy);
            if (dd >= minD) return;
            if (dd < 1) dx = ax < 120 ? 1.f : -1.f, dy = 0, dd = 1;
            x = ax + dx / dd * minD, y = ay + dy / dd * minD;
        };
        if (avoid_) push(avoidX_, avoidY_, 50);
        push(kCompassX, kCompassY, 46);
        d.x = std::round(std::fmin(std::fmax(x, 28.f), 212.f));
        d.y = std::round(std::fmin(std::fmax(y, 26.f), 110.f));
        // A gentle bob keeps it alive; in half-pixel steps, so it redraws a few times a second, not every frame.
        d.y += std::round(std::sin(t_s_ * kBobRate) * kBobPx * 2) / 2;
        d.trail = std::floor(trail_);
    }
    if (full_) damage.add(kBand);
    else {
        // The compass alone for a bearing change; the plane's box (sky, contrail, glow) only when it moved.
        if (d.bearing != drawn_.bearing) damage.add(kCompassBox);
        if (!d.samePlane(drawn_)) damage.add(box(d).unite(box(drawn_)));
    }
    full_ = false;
    drawn_ = d;
}

void PlaneBand::draw(Canvas& c) {
    const Drawn& d = drawn_;
    if (!d.has || !c.clip().intersects(kBand)) return;
    Rect saved = c.clip();
    c.setClip(saved.intersect(kBand));  // the card covers the rest; keep the plume off it
    if (c.clip().intersects(box(d))) {
        float a = d.heading * kDeg, bx = -std::sin(a), by = std::cos(a);
        if (t_.flat) {
            // A pale band for the contrail and the plane in white (black on a light airline colour):
            // no plume, glow or shadow.
            const Rgb ink = ui::textOn(field_);
            float x0 = d.x + bx * kTailGap, y0 = d.y + by * kTailGap;
            aaLine(c, x0, y0, x0 + bx * d.trail, y0 + by * d.trail, 7, ink, 70);
            data::drawTop(c, data::Family(d.shape), d.x, d.y, kPlaneSize, float(d.heading), ink);
        } else {
            ui::contrail(c, d.x + bx * kTailGap, d.y + by * kTailGap, bx, by, d.trail, t_.text);
            ui::glow(c, d.x, d.y, kGlowR, t_.sunCore, 70, t_);  // sunlit fuselage
            data::drawTop(c, data::Family(d.shape), d.x + 1.5f, d.y + 2, kPlaneSize, float(d.heading), t_.shadow, 60);
            data::drawTop(c, data::Family(d.shape), d.x, d.y, kPlaneSize, float(d.heading), t_.text);
        }
    }
    if (c.clip().intersects(kCompassBox)) ui::compass(c, kCompassX, kCompassY, kCompassR, float(d.bearing), t_, field_);
    // The badge and name last, over the contrail when it passes under them.
    if (showId_) {
        if (id_.logo && scratch_) scratch_->check();
        ui::drawIdentity(c, kBandIdX, kBandIdY, kBandIdSide, id_, t_);
    }
    if (name_[0]) drawText(c, *t_.fontLabel, showId_ ? kNameAfterIdX : kNameX, kNameY, name_, ui::textOn(field_));
    c.setClip(saved);
}

// ------------------------------------------------------------------ InfoCard

void InfoCard::set(const Scene* s, const SceneFlight* f, int index) {
    scene_ = s, f_ = f, index_ = index;
    dirty_ = true;  // the scene is rebuilt in place, so the flight's values may have changed too
}

void InfoCard::build(Text& t) {
    std::memset(&t, 0, sizeof t);
    t.progressFrac = -1;
    if (!f_) {
        std::snprintf(t.footer, sizeof t.footer, "%s", scene_ && !scene_->online ? "Offline" : "Scanning the sky…");
        return;
    }
    const SceneFlight& f = *f_;
    t.has = true;
    const SceneRoute* r = scene_ ? scene_->route(f.callsign) : nullptr;
    std::snprintf(t.callsign, sizeof t.callsign, "%s", f.callsign);  // in capitals (app::fillFlight)
    // Cinematic: the logo, fin or shape left of the callsign (Bold Editorial puts it in the band).
    if (!t_.flat) {
        t.idLogo = logo_, t.id = ui::IdentityKey::of(f.callsign, f.shape);
    }
    const int csX = t_.flat ? kMargin : kCallsignAfterIdX;
    // Hero-size callsign unless it would crowd out the airline and type beside it (without a badge, only
    // seven- and eight-character callsigns; with one, it leaves the same room beside the callsign).
    t.bigCallsign = textWidth(*t_.fontDisplay, t.callsign) <= 150 - (csX - kMargin);
    int csW = textWidth(t.bigCallsign ? *t_.fontDisplay : *t_.fontTitle, t.callsign);
    int sideW = 240 - kMargin - (csX + csW + 10);

    // Airline: the route's full name if it fits, else the short table name.
    const data::Airline* al = data::airlineForCallsign(f.callsign);
    t.brand = data::airlineFill(al, t_.brandNeutral);
    const char* name = r && r->airline[0] && textWidth(kFontSmall, r->airline) <= sideW ? r->airline
                       : al                                                       ? al->name
                       : r && r->airline[0]                                       ? r->airline
                       : f.military                                               ? "Military"
                                                                                  : "";
    ui::ellipsize(t.airline, sizeof t.airline, kFontSmall, name, sideW);

    // Type and registration: full type name, then the bare code, then trimmed.
    const char* tn = data::aircraftTypeName(f.type);
    const char* reg = std::strcmp(f.reg, f.callsign) ? f.reg : "";  // GA callsigns are the registration
    const char* sep = reg[0] && (tn || f.type[0]) ? " · " : "";
    // "Boeing 787-9 · N24974" on one line when it fits beside the callsign, else on two.
    char line[48];
    std::snprintf(line, sizeof line, "%s%s%s", tn ? tn : f.type, sep, reg);
    if (textWidth(kFontSmall, line) > sideW) {
        std::snprintf(line, sizeof line, "%s", tn && textWidth(kFontSmall, tn) <= sideW ? tn : f.type);
        ui::ellipsize(t.regLine, sizeof t.regLine, kFontSmall, reg, sideW);
    }
    ui::ellipsize(t.typeLine, sizeof t.typeLine, kFontSmall, line, sideW);

    t.shape = f.shape;
    t.hasRoute = f.hasRoute;
    if (f.hasRoute) {
        std::snprintf(t.orig, sizeof t.orig, "%s", f.orig);
        std::snprintf(t.dest, sizeof t.dest, "%s", f.dest);
        if (f.progress >= 0) {
            int pct = int(std::fmin(1.f, f.progress) * 100 + 0.5f);
            t.progressFrac = pct / 100.f;  // the bar moves with the percentage, so one hash covers both
            if (f.etaEpoch) {
                std::snprintf(t.progress, sizeof t.progress, "%d%% · lands ~%s", pct, eta_.set(f.etaEpoch));
            } else {
                std::snprintf(t.progress, sizeof t.progress, "%d%%", pct);
            }
        }
        if (r) {
            ui::ellipsize(t.origCity, sizeof t.origCity, kFontSmall, r->origCity, 102);
            ui::ellipsize(t.destCity, sizeof t.destCity, kFontSmall, r->destCity, 102);
        }
    }
    std::snprintf(t.tiles[0][0], 24, "ALT FT");
    ui::formatThousands(t.tiles[0][1], 24, f.altFt);
    ui::formatVrate(t.tiles[0][2], 24, f.vrateFpm);
    t.trend = f.vrateFpm >= 300 ? 1 : f.vrateFpm <= -300 ? 2 : 0;
    std::snprintf(t.tiles[1][0], 24, "SPEED");
    std::snprintf(t.tiles[1][1], 24, "%d kt", int(f.gsKt + 0.5f));
    std::snprintf(t.tiles[1][2], 24, "hdg %03d°", int(f.trackDeg + 0.5f) % 360);
    std::snprintf(t.tiles[2][0], 24, "DIST");
    ui::formatDistance(t.tiles[2][1], 24, f.distNm);
    std::snprintf(t.tiles[2][2], 24, "%s · %d° up", ui::compassPoint(f.bearingDeg), int(f.elevDeg + 0.5f));
    int n = scene_ ? scene_->count : 1;
    std::snprintf(t.footer, sizeof t.footer, n > 1 ? "%d of %d · tap for next" : "%d of %d", index_ + 1, n);
}

void InfoCard::update(float, DamageList& damage) {
    if (dirty_) {
        build(text_);
        dirty_ = false;
        const Text& t = text_;
        if (t.idLogo && scratch_) scratch_->check();
        // The badge: the callsign names the airline (so which logo), plus whether one is loaded, the fin's
        // colours and the shape a registration shows.
        const uint32_t idBits = uint32_t(t.idLogo ? t.idLogo->w : 0) << 16 | uint32_t(t.id.airline) << 8 | t.id.family;
        const uint32_t idSig = ui::hashBytes(&t.id.colours, sizeof t.id.colours, idBits);
        uint32_t sig[kRegions] = {
            ui::hashStr(t.callsign, ui::hashStr(t.airline, ui::hashStr(t.typeLine, ui::hashStr(t.regLine,
                        ui::hashBytes(&idSig, sizeof idSig, t.has ? 1u : 2u))))),
            ui::hashStr(t.orig, ui::hashStr(t.dest, ui::hashStr(t.progress, ui::hashStr(t.origCity,
                        ui::hashStr(t.destCity, ui::hashBytes(&t.brand, sizeof t.brand,
                        ui::hashBytes(&t.shape, 1, (t.hasRoute ? 1u : 2u) + (t.progressFrac >= 0 ? 4u : 0u)))))))),
            ui::hashBytes(t.tiles[0], sizeof t.tiles[0], t.trend),  // the trend colours the altitude sub-line
            ui::hashBytes(t.tiles[1], sizeof t.tiles[1]),
            ui::hashBytes(t.tiles[2], sizeof t.tiles[2]),
            ui::hashStr(t.footer),
        };
        Rect boxes[kRegions] = {kHeaderBox, kRouteBox, {}, {}, {}, kFooterBox};
        for (int i = 0, x = kTileX0; i < 3; x += kTileW[i++] + kTileGap) boxes[2 + i] = {x, kTileY, kTileW[i], kTileH};
        if (t.has != shownHas_) full_ = true;  // "Quiet skies" and a flight lay the card out differently
        shownHas_ = t.has;
        for (int i = 0; i < kRegions; ++i) {
            if (!full_ && sig[i] != sig_[i]) damage.add(boxes[i]);
            sig_[i] = sig[i];
        }
    }
    if (full_) damage.add(kCardArea);
    full_ = false;
}

void InfoCard::draw(Canvas& c) {
    if (!c.clip().intersects(kCardArea)) return;
    // A soft shadow lifts the card off the sky.
    if (!t_.flat)
        for (int i = 0; i < kShadowH; ++i) {
            float f = float(i + 1) / kShadowH;
            c.fillBlend({0, kTop - kShadowH + i, 240, 1}, t_.shadow, uint8_t(70 * f * f));
        }
    ui::panel(c, {0, kTop, 240, 320 - kTop + 16}, 14, t_.card, t_.cardEdge, t_);  // bottom corners off-screen
    const Text& t = text_;
    if (!t.has) {
        drawText(c, *t_.fontTitle, 120, 222, t_.upperHeads ? "QUIET SKIES" : "Quiet skies", t_.text, Align::Center);
        drawText(c, kFontBody, 120, 246, t.footer, t_.textDim, Align::Center);
        return;
    }
    if (!t_.flat) {
        if (t.idLogo && scratch_) scratch_->check();
        ui::drawIdentity(c, kCardIdX, kCardIdY, kCardIdSide, t.id.get(t.idLogo), t_);
    }
    drawText(c, t.bigCallsign ? *t_.fontDisplay : *t_.fontTitle, t_.flat ? kMargin : kCallsignAfterIdX,
             t.bigCallsign ? 176 : 172, t.callsign, t_.text);
    const int right = 240 - kMargin;
    // The airline, type and registration lines, centred on the callsign's height.
    const char* lines[3];
    int n = 0;
    for (const char* l : {t.airline, t.typeLine, t.regLine})
        if (l[0]) lines[n++] = l;
    const int lineH = 13, firstBase = 167 - (n - 1) * lineH / 2;
    for (int i = 0; i < n; ++i)
        drawText(c, kFontSmall, right, firstBase + i * lineH, lines[i], i == 0 ? t_.textSoft : t_.textDim,
                 Align::Right);
    if (t.hasRoute) {
        drawText(c, *t_.fontLabel, kMargin, 204, t.orig, t_.text);
        drawText(c, *t_.fontLabel, right, 204, t.dest, t_.text, Align::Right);
        const char* pctEnd = t_.brandFill ? std::strchr(t.progress, '%') : nullptr;
        if (pctEnd) {
            // Bold Editorial (mockup 7A): the percentage in the display face, like the codes beside it, in
            // a tint of the airline's colour that reads on the card; the arrival estimate after it.
            char pct[sizeof t.progress];
            std::snprintf(pct, sizeof pct, "%.*s", int(pctEnd + 1 - t.progress), t.progress);
            const char* rest = pctEnd + 1;
            int x = 120 - (textWidth(*t_.fontLabel, pct) + textWidth(kFontSmall, rest)) / 2;
            x = drawText(c, *t_.fontLabel, x, 204, pct, ui::readableOn(t.brand, t_.card, 4.5f));
            drawText(c, kFontSmall, x, 203, rest, t_.textDim);
        } else {
            drawText(c, kFontSmall, 120, 202, t.progress, t_.textDim, Align::Center);
        }
        if (t.progressFrac >= 0) {
            const bool knob = !t.shape;
            if (t_.flat)  // a 6 px bar in the airline's colour (lifted to 3:1 on the card) on a dark track
                ui::progressBar(c, 18, 214, 204, t.progressFrac, t_.tile,
                                t_.brandFill ? ui::readableOn(t.brand, t_.card, 3.f) : t_.accent, true, knob);
            else
                ui::progressBar(c, 18, 214, 204, t.progressFrac, t_.cardEdge, t_.brandFill ? t.brand : t_.accent,
                                false, knob);
            if (t.shape) {
                // The flight's side view rides the bar at its progress (mockups 1B and 7A), 34 x ~11 px, nose right.
                constexpr float kMarkerW = 34;
                const float mx = 18 + 204 * t.progressFrac;
                data::drawSide(c, data::Family(t.shape), mx - kMarkerW / 2, 214 - kMarkerW / 6 + 0.5f, kMarkerW,
                               t_.text, t_.flat ? t_.textDim : t_.textSoft);
            }
        }
        drawText(c, kFontSmall, kMargin, 231, t.origCity, t_.textDim);
        drawText(c, kFontSmall, right, 231, t.destCity, t_.textDim, Align::Right);
    } else {
        drawText(c, kFontBody, 120, 212, "Route unknown", t_.textDim, Align::Center);
    }
    Rgb trend[3] = {t_.level, t_.climb, t_.descend};
    Rgb subs[3] = {trend[t.trend], t_.textDim, t_.textDim};
    for (int i = 0, x = kTileX0; i < 3; x += kTileW[i++] + kTileGap)
        ui::statTile(c, {x, kTileY, kTileW[i], kTileH}, t_, t.tiles[i][0], t.tiles[i][1], t.tiles[i][2], subs[i]);
    if (t_.flat) {  // Bold Editorial: a white rule over the tiles and thin rules between them, instead of boxes
        c.fill({kTileX0, kTileY - 3, kTileW[0] + kTileW[1] + kTileW[2] + 2 * kTileGap, 2}, rgb565(t_.text));
        for (int i = 1, x = kTileX0 + kTileW[0]; i < 3; x += kTileW[i++] + kTileGap)
            c.fill({x + kTileGap / 2, kTileY + 6, 1, kTileH - 12}, rgb565(t_.cardEdge));
    }
    drawText(c, kFontSmall, 120, 309, t.footer, t_.textDim, Align::Center);
}

// ------------------------------------------------------------------ ClosestScreen

ClosestScreen::ClosestScreen(const Theme& t, ScreenArena& a) : t_(t), arena_(a), plane_(t), card_(t) {
    sky_.setArea(PlaneBand::kBand);
    sky_.setRidge(false);
    plane_.watch(&logo_), card_.watch(&logo_);
}

// The featured flight's logo into the arena when its airline changes (the theme picks the size); kept while the
// airline stays, and kept absent when the pack has none, so a missing logo is not looked up every rebuild.
void ClosestScreen::loadLogo(const SceneFlight* f) {
    ClosestLogo* lg = logo_.data();
    // enter() made the theme's store the live member; checking flat means a stale theme never picks the other.
    if (!lg || !arena_.owns(this) || lg->flat != t_.flat) return;
    const char* cs = f ? f->callsign : nullptr;
    if (t_.flat) ui::reloadLogo(logos_, cs, lg->code, lg->slot.band);
    else ui::reloadLogo(logos_, cs, lg->code, lg->slot.card);
}

void ClosestScreen::refresh() {
    const SceneFlight* f = nullptr;
    int index = 0;
    if (scene_ && scene_->count) {
        f = selected_ ? scene_->find(selected_) : nullptr;
        if (!f) f = &scene_->flights[0];
        index = int(f - scene_->flights);
    }
    // Bold Editorial: the band is a flat field in the plane's airline colour (neutral when unknown).
    const data::Airline* al = f ? data::airlineForCallsign(f->callsign) : nullptr;
    const Rgb field = data::airlineFill(al, t_.brandNeutral);
    sky_.setFlat(t_.brandFill, field);
    plane_.setField(field);
    plane_.setName(t_.brandFill && al ? al->name : "");
    // The logo, else the fin or shape: Cinematic in the card, Bold Editorial in the band. The arena is read
    // only while this screen owns it (the host passes the scene before enter() claims it).
    loadLogo(f);
    const ClosestLogo* lg = arena_.owns(this) ? logo_.data() : nullptr;
    if (lg && lg->flat != t_.flat) lg = nullptr;  // never read the member the theme did not set up
    const gfx::Badge* logo = !lg ? nullptr
                             : t_.flat ? (lg->slot.band.badge.empty() ? nullptr : &lg->slot.band.badge)
                                       : (lg->slot.card.badge.empty() ? nullptr : &lg->slot.card.badge);
    card_.setLogo(t_.flat ? nullptr : logo);
    ui::Identity id = f ? ui::identityOf(f->callsign, f->shape) : ui::Identity{};
    id.logo = t_.flat ? logo : nullptr;
    plane_.setIdentity(t_.flat && f, id, lg ? lg->code : "");
    if (scene_) {
        sky_.setSun(scene_->sunElev, scene_->sunAz);
        float sx, sy;
        bool sun = sky_.sunPoint(sx, sy) && sky_.sunDiscVisible() && sy < InfoCard::kTop;  // a visible disc
        plane_.setAvoid(sun, sx, sy);
    }
    plane_.setFlight(f);
    card_.set(scene_, f, index);
}

void ClosestScreen::setScene(const Scene* s) {
    scene_ = s;
    refresh();
}

void ClosestScreen::setSelection(uint32_t hex) {
    Screen::setSelection(hex);
    refresh();
}

void ClosestScreen::enter() {
    ClosestLogo* lg = arena_.claim<ClosestLogo>(this);
    // The union starts with the card store live; Bold Editorial switches it to the band store. A theme change
    // re-enters, so the live member always matches t_.
    if (t_.flat) new (&lg->slot.band) gfx::Badge30(), lg->flat = true;
    logo_.attach(lg, arena_, this);
    refresh();  // loads the featured flight's logo into the fresh scratch
    sky_.invalidate();
    plane_.reset();
    card_.reset();
}

NavRequest ClosestScreen::onTap(int, int y) {
    NavRequest r;
    if (y < InfoCard::kTop || !scene_ || scene_->count < 2) return r;
    const SceneFlight* f = selected_ ? scene_->find(selected_) : nullptr;
    int at = f ? int(f - scene_->flights) : 0;
    r.selectHex = scene_->flights[(at + 1) % scene_->count].hex;
    return r;
}

}  // namespace scr
