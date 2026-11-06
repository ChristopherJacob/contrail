#include "SkyViewScreen.h"

#ifdef CONTRAIL_ESP32
// The treeline and moon are per-pixel loops: -Os roughly halves their speed.
#pragma GCC optimize("O2")
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "data/Stars.h"
#include "gfx/Font.h"
#include "gfx/Hot.h"
#include "gfx/Raster.h"
#include "gfx/fonts/Fonts.h"
#include "services/Astro.h"
#include "TreeProfile.h"
#include "ui/Format.h"
#include "ui/Widgets.h"
#include "gfx/Math.h"

namespace scr {
using namespace gfx;

namespace {
constexpr float k2Pi = 2 * kPi;
constexpr float kMinStarElev = 2.5f;  // lower stars are behind the trees anyway
// Planes are drawn over the trees, so they show right down to the rim; below a degree they are
// usually ground clutter or about to drop out of range.
constexpr float kMinPlaneElev = 1.f;
constexpr DomeGeom kDome{};
constexpr float kBeaconPeriodS = 1.2f, kBeaconOnS = 0.12f, kBeaconR = 9;
constexpr float kDuskR = 64;  // Bold Editorial's flat sunset glow (the inner disc is half this)
constexpr int kPlaneRects = 8;  // damage budget for the planes; the caption takes one more

float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
float smooth01(float v) {
    v = clampf(v, 0, 1);
    return v * v * (3 - 2 * v);
}
}  // namespace

const SceneFlight* highestFlight(const Scene* s) {
    const SceneFlight* top = nullptr;
    for (int i = 0; s && i < s->count; ++i) {
        const SceneFlight& f = s->flights[i];
        if (f.hex && f.elevDeg >= kMinPlaneElev && (!top || f.elevDeg > top->elevDeg)) top = &f;
    }
    return top;
}

// ------------------------------------------------------------------ SkyDome

void SkyDome::update(float, DamageList& damage) {
    stars_.check();
    if (dirty_ && scene_) {
        dirty_ = false;
        const Scene& s = *scene_;
        int64_t minute = s.timeValid ? s.epoch / 60 : INT64_MIN;
        bool newMinute = minute != minute_;
        minute_ = minute;
        // The sky drifts slowly: small changes wait for the next minute, so the whole dome is
        // redrawn at most once a minute. Jumps (a new scene, the time or location arriving) go at once.
        // Half a degree is under a pixel of motion and a barely visible change of colour, so by day
        // the dome is redrawn every few minutes; at night the stars need it every minute.
        // Positions are compared on screen: near the zenith the azimuth swings fast while the
        // point hardly moves. The sun's azimuth stops mattering below -14 degrees (no glow, no
        // lean) and the moon's position while it stays below the horizon.
        auto px = [this](float az0, float el0, float az1, float el1) {
            Vec2 a = domeProject(az0, el0, g_), b = domeProject(az1, el1, g_);
            return std::fabs(a.x - b.x) + std::fabs(a.y - b.y);
        };
        bool sunSeen = s.sunElev > -14 || sunElev_ > -14, moonSeen = s.moonElev > -1 || moonElev_ > -1;
        float sunPx = sunSeen ? px(s.sunAz, s.sunElev, sunAz_, sunElev_) : 0.f;
        float moonPx = moonSeen ? px(s.moonAz, s.moonElev, moonAz_, moonElev_) : 0.f;
        float illum = moonSeen ? std::fabs(s.moonIllum - moonIllum_) : 0.f;
        bool moonFlip = (s.moonElev > -1) != (moonElev_ > -1);
        bool drift = std::fabs(s.sunElev - sunElev_) > 0.5f || sunPx > 0.8f || moonPx > 0.8f || illum > 0.005f;
        bool jump = std::fabs(s.sunElev - sunElev_) > 2 || sunPx > 4 || moonPx > 4 || illum > 0.03f || moonFlip;
        bool starsVisible = starF_ > 0 && s.timeValid && s.locationSet;
        bool starsStale = starsVisible && (starEpoch_ == INT64_MIN || s.epoch / 60 != starEpoch_ / 60 ||
                                           s.epoch < starEpoch_);
        bool starsMissing = s.sunElev < -6 && s.timeValid && s.locationSet && starEpoch_ == INT64_MIN;
        if (full_ || jump || starsMissing || (newMinute && (drift || starsStale))) {
            sunElev_ = s.sunElev, sunAz_ = s.sunAz, moonElev_ = s.moonElev, moonAz_ = s.moonAz;
            moonIllum_ = s.moonIllum;
            recompute();
            full_ = true;
        }
    }
    if (full_) damage.addAll();
    full_ = false;
}

void SkyDome::recompute() {
    stops_ = app::skyForSun(sunElev_);
    tree_ = lerp(t_.tree, stops_.top, 0.15f);  // backlit treetops pick up a little of the sky
    starF_ = sunElev_ < -14 ? 1.f : sunElev_ < -6 ? (-6 - sunElev_) / 8.f : 0.f;
    if (starF_ > 0 && stars_ && scene_->timeValid && scene_->locationSet) computeStars();
    else nStars_ = 0, starEpoch_ = INT64_MIN;

    sunPt_ = domeProject(sunAz_, sunElev_ > 0 ? sunElev_ : 0, g_);
    // A low sun warms its side of the horizon and leaves the far side in blue twilight: shift the
    // gradient's centre away from it, which costs nothing per pixel.
    float lean = sunElev_ > -14 ? smooth01(1 - std::fabs(sunElev_ - 1) / 16) : 0.f;
    float dx = sunPt_.x - g_.cx, dy = sunPt_.y - g_.cy, d = std::sqrt(dx * dx + dy * dy);
    gradCx_ = g_.cx, gradCy_ = g_.cy;
    if (d > 1) gradCx_ -= dx / d * 46 * lean * (d / g_.ry), gradCy_ -= dy / d * 46 * lean * (d / g_.ry);

    moonPt_ = domeProject(moonAz_, moonElev_, g_);
    limb_ = domeDirectionToward(moonAz_, moonElev_, sunAz_, sunElev_, g_);
}

void SkyDome::computeStars() {
    const Scene& s = *scene_;
    starEpoch_ = s.epoch;
    nStars_ = 0;
    // svc::equatorialToHorizontal per star took ~30 ms (a dozen double-precision trig calls
    // each): the sidereal time and the latitude's sine and cosine are the same for every star,
    // and float is plenty for placing a dot.
    const float lst = float(svc::localSiderealDeg(s.epoch, s.homeLon));
    const float sinLat = std::sin(float(s.homeLat) * kDeg), cosLat = std::cos(float(s.homeLat) * kDeg);
    const float sinMin = std::sin(kMinStarElev * kDeg);
    for (int i = 0; i < data::kStarCount && nStars_ < kMaxStars; ++i) {
        const data::Star& st = data::kStars[i];
        float ha = (lst - st.raDeg) * kDeg, dec = st.decDeg * kDeg;
        float sinDec = std::sin(dec), cosDec = std::cos(dec), cosHa = std::cos(ha);
        float sinAlt = sinLat * sinDec + cosLat * cosDec * cosHa;
        if (sinAlt < sinMin) continue;
        float elev = std::asin(sinAlt) / kDeg;
        float az = std::atan2(-std::sin(ha) * cosDec, cosLat * sinDec - sinLat * cosDec * cosHa) / kDeg;
        if (az < 0) az += 360;
        Vec2 p = domeProject(az, elev, g_);
        // Brightness by magnitude, dimmed near the horizon where the air is thickest.
        float a = 255.f - (st.mag + 1.5f) * 36.f;
        a *= 0.45f + 0.55f * smooth01((elev - kMinStarElev) / 22.f);
        StarPt& o = stars_[nStars_++];
        // Stars above the trees are well inside the screen, so the quarter pixels fit the fields.
        o.x4 = uint32_t(clampf(std::round(p.x * 4), 0, 1023)), o.y4 = uint32_t(clampf(std::round(p.y * 4), 0, 2047));
        o.a = uint32_t(clampf(a, 40, 255));
        o.kind = st.mag < 0.6f ? 2 : st.mag < 1.8f ? 1 : 0;
    }
    std::sort(stars_.data(), stars_.data() + nStars_, [](const StarPt& a, const StarPt& b) { return a.y4 < b.y4; });
}

void SkyDome::drawStars(Canvas& c) const {
    const Rect& cl = c.clip();
    const int y4lo = (cl.y - 5) * 4, y4hi = (cl.bottom() + 5) * 4;
    const StarPt* it = std::lower_bound(stars_.data(), stars_.data() + nStars_, y4lo,
                                        [](const StarPt& a, int y4) { return int(a.y4) < y4; });
    for (; it != stars_.data() + nStars_ && int(it->y4) < y4hi; ++it) {
        uint8_t a = uint8_t(it->a * starF_);
        float x = it->x4 / 4.f, y = it->y4 / 4.f;
        if (it->kind == 0) {
            // Faint: a 2x2 bilinear splat, so the star sits between pixels instead of on the grid.
            int fx = int(it->x4) - 2, fy = int(it->y4) - 2;
            int ix = fx >> 2, iy = fy >> 2, wx = fx & 3, wy = fy & 3;
            const int w[4] = {(4 - wx) * (4 - wy), wx * (4 - wy), (4 - wx) * wy, wx * wy};
            for (int k = 0; k < 4; ++k)
                if (w[k]) c.addPixel(ix + (k & 1), iy + (k >> 1), t_.star, uint8_t(a * w[k] / 16));
        } else if (it->kind == 1) {
            aaCircle(c, x, y, 0.75f, t_.star, a);
        } else {
            ui::glow(c, x, y, 5, t_.star, uint8_t(a / 4), t_);
            aaCircle(c, x, y, 1.05f, t_.star, a);
        }
    }
}

void SkyDome::drawMoon(Canvas& c) const {
    const float r = 8.f, mx = moonPt_.x, my = moonPt_.y;
    Rect b = Rect{int(std::floor(mx - r - 1)), int(std::floor(my - r - 1)), int(2 * r) + 3, int(2 * r) + 3}.intersect(
        c.clip());
    if (b.empty()) return;
    // The lit part: everything sunward of the terminator, an ellipse whose half-width is
    // w * (1 - 2k) along the sun direction (k = illuminated fraction).
    const float sx = limb_.x, sy = limb_.y, k = clampf(moonIllum_, 0, 1), term = 1 - 2 * k;
    const Rgb lit = t_.moon;
    const float litA = 255 - 120 * smooth01((sunElev_ + 2) / 10);  // pale by day
    // The unlit part (earthshine) only shows against a dark sky: by day it would be a navy blot on
    // the blue, so it fades out through twilight, gone once the sun is up to -2 degrees.
    const float darkA = 225 * smooth01((-2 - sunElev_) / 8);
    // A few darker maria, fixed on the disc, so it reads as the moon and not a lamp.
    struct Mare {
        float x, y, r2, depth;
    };
    static constexpr Mare kMaria[] = {{-0.30f, -0.28f, 0.09f, 0.09f}, {0.12f, -0.30f, 0.05f, 0.07f},
                                      {0.08f, 0.10f, 0.06f, 0.07f},   {-0.38f, 0.20f, 0.04f, 0.06f}};
    const bool flat = t_.flat;  // Bold Editorial: a plain disc, no limb darkening or maria
    for (int y = b.y; y < b.bottom(); ++y) {
        for (int x = b.x; x < b.right(); ++x) {
            float dx = x + 0.5f - mx, dy = y + 0.5f - my, d2 = dx * dx + dy * dy;
            if (d2 >= (r + 0.5f) * (r + 0.5f)) continue;
            float d = std::sqrt(d2), edge = clampf(r + 0.5f - d, 0, 1);
            float along = dx * sx + dy * sy, across = dy * sx - dx * sy;
            float w = std::sqrt(std::fmax(0.f, r * r - across * across));
            float l = clampf((along - w * term) / 1.4f + 0.5f, 0, 1);  // a soft terminator
            uint16_t& p = c.at(x, y);
            if (darkA > 0) p = blend565(p, t_.moonDark, uint8_t(edge * darkA));  // hides the stars behind it
            if (l <= 0) continue;
            float shade = 1;
            if (!flat) {
                float nx = dx / r, ny = dy / r;
                shade = 0.86f + 0.14f * std::sqrt(std::fmax(0.f, 1 - d2 / (r * r)));
                for (const Mare& m : kMaria) {
                    float ex = nx - m.x, ey = ny - m.y, q = (ex * ex + ey * ey) / m.r2;
                    if (q < 1) shade -= m.depth * (1 - q);
                }
            }
            Rgb col{uint8_t(lit.r * shade), uint8_t(lit.g * shade), uint8_t(lit.b * shade)};
            p = blend565(p, col, uint8_t(edge * l * litA));
        }
    }
}

void SkyDome::draw(Canvas& c) {
    stars_.check();
    const Rect cl = c.clip();
    // At night a little airglow low down, so the treetops stand out against the sky.
    float night = smooth01((-sunElev_ - 6) / 10);
    Rgb stops[] = {stops_.top, stops_.mid, lerp(stops_.horizon, t_.airglow, 0.6f * night)};
    float pos[] = {0.f, 0.58f - 0.03f * night, 1.f - 0.08f * night};
    // Glow parameters, latched once for all chunks.
    const bool sun = sunElev_ > -12;
    float sunA = 0, day = 0;
    Rgb wide{}, core{};
    if (sun) {
        // Warm at sunrise and sunset, white-gold by day.
        sunA = sunElev_ > 0 ? 1.f : (sunElev_ + 12) / 12;
        day = smooth01((sunElev_ - 4) / 20);
        wide = lerp(t_.sunGlow, t_.sunCore, day), core = lerp(t_.sunCore, t_.sunDisc, day);
    }
    // Opposite a low sun, the pink Belt of Venus over the blue of the earth's shadow.
    const float belt = sunElev_ > -8 && sunElev_ < 8 ? smooth01(1 - std::fabs(sunElev_ + 1) / 7) : 0.f;
    const Vec2 anti = belt > 0 ? domeProject(sunAz_ + 180, 0, g_) : Vec2{};
    const bool moon = moonElev_ > -1;
    const float halo = 18 + 40 * moonIllum_ * (1 - 0.6f * smooth01((sunElev_ + 4) / 10));  // fainter in daylight
    // Bold Editorial: four flat bands by elevation (90-67.5°, 67.5-45°, 45-22.5°, 22.5-0°), centred on
    // the zenith (the projection is linear in elevation), all in the sky's cool colours. A warm
    // horizon stop (dusk) would make the outer band an orange ring all round the treeline, so the
    // outer band is the mid colour a step lighter instead, and the sunset shows only toward the sun:
    // a flat disc of the dusk colour on the sun's point (on the rim when it is down) and a lighter
    // one inside it, both fading into the band as the horizon cools.
    Rgb bands[4];
    float warmth = 0;
    Rgb duskOuter{}, duskInner{};
    if (t_.flat) {
        const Rgb h = stops_.horizon;
        warmth = clampf((float(h.r) - float(h.b)) / 120.f, 0, 1);
        const Rgb cool = lerp(stops_.mid, h, 0.5f), lifted = lerp(stops_.mid, Rgb{200, 215, 240}, 0.2f);
        bands[0] = stops_.top, bands[1] = lerp(stops_.top, stops_.mid, 0.5f), bands[2] = stops_.mid;
        bands[3] = lerp(cool, lifted, warmth);
        duskOuter = lerp(bands[3], h, warmth);
        duskInner = lerp(bands[3], lerp(h, t_.sunDisc, 0.45f), warmth);
    }
    const bool dusk = t_.flat && warmth > 0.05f;

    // Sky and glows only where the trees don't cover them: in 8-row chunks, each as wide as the
    // outer tree ellipse at its widest row (the trees fill everything outside it solid). The big
    // glows are clipped too, so their ~1/3 of the area outside the dome costs nothing.
    for (int y0 = cl.y; y0 < cl.bottom(); y0 = (y0 & ~7) + 8) {
        int y1 = std::min(cl.bottom(), (y0 & ~7) + 8);
        float yn = std::fmin(std::fmax(g_.cy, float(y0)), float(y1));  // the chunk's row nearest the centre
        float v = (yn - g_.cy) / SkyTrees::kRy;
        if (v * v >= 1) continue;
        float half = SkyTrees::kRx * std::sqrt(1 - v * v) + 2;
        Rect chunk = Rect{int(std::floor(g_.cx - half)), y0, int(2 * half) + 2, y1 - y0}.intersect(cl);
        if (chunk.empty()) continue;
        if (t_.flat) {
            const float bandPos[] = {0.f, 0.25f, 0.5f, 0.75f};
            ui::radialFill(c, chunk, g_.cx, g_.cy, g_.rx, g_.ry, bands, bandPos, 4, t_);
        } else {
            c.radialGradient(chunk, gradCx_, gradCy_, SkyTrees::kRx, SkyTrees::kRy, stops, pos, 3);
        }
        c.setClip(chunk);
        if (dusk) {
            aaCircle(c, sunPt_.x, sunPt_.y, kDuskR, duskOuter);
            aaCircle(c, sunPt_.x, sunPt_.y, kDuskR * 0.5f, duskInner);
        }
        if (sun) {
            ui::glow(c, sunPt_.x, sunPt_.y, 105, wide, uint8_t(sunA * (70 - 25 * day)), t_);
            ui::glow(c, sunPt_.x, sunPt_.y, 34, core, uint8_t(sunA * 170), t_);
            if (sunElev_ > 0) {
                ui::glow(c, sunPt_.x, sunPt_.y, 14, t_.sunDisc, 200, t_);
                aaCircle(c, sunPt_.x, sunPt_.y, 6.5f, t_.sunDisc);
            }
        }
        if (belt > 0) ui::glow(c, anti.x, anti.y, 100, t_.beltOfVenus, uint8_t(55 * belt), t_);
        if (moon) ui::glow(c, moonPt_.x, moonPt_.y, 38, t_.moonGlow, uint8_t(halo), t_);
        c.setClip(cl);
    }
    if (starF_ > 0) drawStars(c);
    if (moon) drawMoon(c);
}

// ------------------------------------------------------------------ SkyPlanes

bool SkyPlanes::Drawn::same(const Drawn& o) const {
    return x == o.x && y == o.y && heading == o.heading && track == o.track && size == o.size && trail == o.trail &&
           tag == o.tag && !std::strcmp(callsign, o.callsign);
}

void SkyPlanes::reset() {
    for (auto& k : tracks_) k = Track{};
    dirty_ = full_ = true;
}

namespace {
// Where the label of the tagged plane goes: beside it, on whichever side has room.
void tagAnchor(float x, float y, float size, const char* s, int& tx, int& ty, Align& al) {
    int w = textWidth(kFontSmall, s);
    float off = size * 0.55f + 4;
    al = x + off + w > 232 ? Align::Right : Align::Left;
    // Kept on the dome's rows: a plane low in the north or south must not push its label off
    // the top of the screen or into the caption.
    tx = int(al == Align::Left ? x + off : x - off), ty = int(clampf(y + 4, 14, SkyInfo::kBox.y - 4));
}
}  // namespace

Rect SkyPlanes::boxFor(const Drawn& d) const {
    float r = std::fmax(std::fmax(d.size * 0.55f + 1, d.size * 0.85f), kBeaconR) + 2;
    Rect b{int(std::floor(d.x - r)), int(std::floor(d.y - r)), int(2 * r) + 2, int(2 * r) + 2};
    if (d.trail) {
        float a = d.heading * kDeg, bx = -std::sin(a), by = std::cos(a), sc = d.size / 34.f;
        float tx = d.x + bx * 15 * sc, ty = d.y + by * 15 * sc, ex = tx + bx * d.trail, ey = ty + by * d.trail;
        float pad = 5.4f * sc + 2;
        Rect t{int(std::floor(std::fmin(tx, ex) - pad)), int(std::floor(std::fmin(ty, ey) - pad)), 0, 0};
        t.w = int(std::ceil(std::fmax(tx, ex) + pad)) - t.x, t.h = int(std::ceil(std::fmax(ty, ey) + pad)) - t.y;
        b = b.unite(t);
    }
    if (d.tag) {
        int tx, ty;
        Align al;
        tagAnchor(d.x, d.y, d.size, d.callsign, tx, ty, al);
        b = b.unite(ui::textBox(kFontSmall, tx, ty, d.callsign, al));
    }
    return b.intersect({0, 0, 240, 320});
}

Rect SkyPlanes::beaconBox(const Drawn& d) {
    constexpr int r = int(kBeaconR) + 2;
    return {int(std::floor(d.x)) - r, int(std::floor(d.y)) - r, 2 * r + 2, 2 * r + 2};
}

bool SkyPlanes::beaconAt(const Track& k) const {
    float t = t_s_ + k.phase;
    if (t >= kBeaconPeriodS) t -= kBeaconPeriodS;
    return t < kBeaconOnS;
}

void SkyPlanes::sync(Rect* pending, int& n) {
    dirty_ = false;
    // The highest planes, up to kMaxTracks, highest first.
    int idx[kMaxTracks], m = 0;
    for (int i = 0; scene_ && i < scene_->count; ++i) {
        const SceneFlight& f = scene_->flights[i];
        if (f.elevDeg < kMinPlaneElev || !f.hex) continue;
        int at = m < kMaxTracks ? m++ : kMaxTracks;
        while (at > 0 && scene_->flights[idx[at - 1]].elevDeg < f.elevDeg) {
            if (at < kMaxTracks) idx[at] = idx[at - 1];
            --at;
        }
        if (at < kMaxTracks) idx[at] = i;
    }
    // Keep the tracks still in the list, free the rest, then give new planes the free slots.
    Track* slot[kMaxTracks] = {};
    for (auto& k : tracks_) k.live = false;
    for (int j = 0; j < m; ++j)
        for (auto& k : tracks_)
            if (k.hex && k.hex == scene_->flights[idx[j]].hex) slot[j] = &k, k.live = true;
    for (auto& k : tracks_)
        if (k.hex && !k.live) pending[n++] = k.box, k = Track{};
    for (int j = 0; j < m; ++j) {
        if (slot[j]) continue;
        for (auto& k : tracks_)
            if (!k.hex) {
                k = Track{};
                k.hex = scene_->flights[idx[j]].hex, k.live = true;
                k.phase = float((k.hex * 2654435761u) >> 16) / 65536.f * kBeaconPeriodS;
                k.beacon = beaconAt(k);
                slot[j] = &k;
                break;
            }
    }
    for (int j = 0; j < m; ++j) {
        Track& k = *slot[j];
        const SceneFlight& f = scene_->flights[idx[j]];
        Drawn d;
        Vec2 p = domeProject(f.bearingDeg, f.elevDeg);
        d.x = std::round(p.x * 2) / 2, d.y = std::round(p.y * 2) / 2;
        d.track = int16_t(int(std::lround(f.trackDeg)) % 360);
        d.heading = int16_t((360 - d.track) % 360);  // mirrored: we are looking up
        d.size = uint8_t(std::lround(clampf(24.f - f.distNm * 1.2f, 9, 22)));
        // Contrails form only in the cold air up high: none below about FL220, full by FL300.
        float high = clampf((f.altFt - 22000) / 8000.f, 0, 1);
        d.trail = uint8_t(std::lround(d.size * 2.4f * high));
        d.tag = j == 0;
        std::snprintf(d.callsign, sizeof d.callsign, "%s", d.tag ? f.callsign : "");
        if (!k.box.empty() && d.same(k.d)) continue;
        Rect b = boxFor(d);
        pending[n++] = k.box, pending[n++] = b;
        k.d = d, k.box = b;
    }
}

void SkyPlanes::update(float dt, DamageList& damage) {
    tracks_.check();
    t_s_ += dt;
    while (t_s_ >= kBeaconPeriodS) t_s_ -= kBeaconPeriodS;
    Rect pending[3 * kMaxTracks + 2];
    int n = 0;
    if (dirty_) sync(pending, n);
    for (auto& k : tracks_) {
        if (!k.hex) continue;
        bool on = beaconAt(k);
        if (on != k.beacon && !full_) pending[n++] = beaconBox(k.d);
        k.beacon = on;
    }
    if (!full_) damage.addMerged(pending, n, kPlaneRects);  // after a reset the dome redraws everything
    full_ = false;
}

uint32_t SkyPlanes::hitTest(int x, int y, int maxPx) const {
    uint32_t best = 0;
    float bestD = float(maxPx * maxPx);
    for (const auto& k : tracks_) {
        if (!k.hex) continue;
        float dx = k.d.x - x, dy = k.d.y - y, d = dx * dx + dy * dy;
        if (d <= bestD) bestD = d, best = k.hex;
    }
    return best;
}

void SkyPlanes::draw(Canvas& c) {
    tracks_.check();
    // Lit by the sun by day, by the sunset from below the horizon at dusk (high contrails glow
    // gold and pink while the ground is dark), and faintly by the moon at night.
    float e = dome_.sunElev();
    float dusk = smooth01(1 - std::fabs(e - 1) / 9), night = smooth01((-e - 4) / 6);
    Rgb trailCol = lerp(lerp(t_.contrailDay, t_.contrailNight, night), t_.contrailDusk, dusk);
    float trailPeak = 230 - 95 * night;
    uint8_t glowA = uint8_t(14 + 34 * night), navA = uint8_t(120 + 135 * night);
    for (const auto& k : tracks_) {
        const Drawn& d = k.d;
        if (!k.hex || !c.clip().intersects(k.box)) continue;
        float a = d.heading * kDeg, bx = -std::sin(a), by = std::cos(a), sc = d.size / 34.f;
        if (d.trail) ui::contrail(c, d.x + bx * 15 * sc, d.y + by * 15 * sc, bx, by, d.trail, trailCol, sc, trailPeak);
        ui::glow(c, d.x, d.y, d.size * 0.85f, t_.skyPlaneGlow, glowA, t_);
        planeGlyph(c, d.x, d.y, float(d.size), float(d.heading), t_.skyPlane);
        // Steady wingtip lights: red on the left wing, green on the right (mirrored, from below).
        float ta = d.track * kDeg, px = std::cos(ta) * d.size * 0.48f, py = -std::sin(ta) * d.size * 0.48f;
        aaCircle(c, d.x + px, d.y + py, 0.9f, t_.navPort, navA);
        aaCircle(c, d.x - px, d.y - py, 0.9f, t_.navStarboard, navA);
        if (k.beacon) {
            // The belly beacon: a red flash that lights the air round it, hot at the centre.
            ui::glow(c, d.x, d.y, kBeaconR, t_.beacon, 190, t_);
            aaCircle(c, d.x, d.y, 1.8f, t_.beacon);
            aaCircle(c, d.x, d.y, 0.7f, lerp(t_.beacon, t_.text, 0.6f));
        }
        if (d.tag) {
            int tx, ty;
            Align al;
            tagAnchor(d.x, d.y, d.size, d.callsign, tx, ty, al);
            ui::shadowText(c, kFontSmall, tx, ty, d.callsign, t_.textSoft, al, t_);
        }
    }
}

// ------------------------------------------------------------------ SkyTrees

namespace {
using trees::kRhoLo;
using trees::kRhoStep;
using trees::kMinScale;
// The tree ellipse shares the dome's centre (its radii are a little larger: see SkyTrees::kRx).
constexpr float kTreeCx = kDome.cx, kTreeCy = kDome.cy;
constexpr float kInvRx = 1.f / SkyTrees::kRx, kInvRy = 1.f / SkyTrees::kRy;

// kRx / |x + 0.5 - kTreeCx| for each screen column: 1 / |u|, so the band needs no division.
struct InvColumns {
    float v[240];
    constexpr InvColumns() : v() {
        for (int x = 0; x < 240; ++x) {
            float d = x + 0.5f - kTreeCx;
            v[x] = SkyTrees::kRx / (d < 0 ? -d : d);
        }
    }
};
constexpr InvColumns kInvU{};

// Tree coverage of a point at normalised (u, v) from the tree ellipse's centre, given 1/|u|
// and 1/|v|. The angle is atan2 to ~0.0015 rad (a short polynomial in the octant's tangent,
// whose division those reciprocals replace), and the distance from the edge is taken in rho^2: rho - edge = (rho^2 - edge^2) / (rho + edge), which
// next to the edge (the only place coverage is fractional) is (rho^2 - edge^2) / (2 edge). That
// needs neither a square root nor a division (1 / (2 edge) is a quadratic fit over the edge's
// 0.76..0.97 range, good to 0.1%).
inline float treeCover(const uint8_t* rho, const uint8_t* scale, float u, float v, float invU, float invV) {
    float ax = std::fabs(u), ay = std::fabs(v);
    float t = ax > ay ? ay * invU : ax * invV;
    float a = t * (0.7853982f + (1 - t) * (0.2447f + 0.0663f * t));
    if (ay > ax) a = kPi / 2 - a;
    if (u < 0) a = kPi - a;
    if (v < 0) a = k2Pi - a;
    float fb = a * (SkyTrees::kBins / k2Pi);
    int k = int(fb);
    float fr = fb - k;
    k &= SkyTrees::kBins - 1;
    int k1 = (k + 1) & (SkyTrees::kBins - 1);
    float edge = kRhoLo + (rho[k] + (rho[k1] - rho[k]) * fr) * kRhoStep;
    float e = edge - 0.865f, inv2e = 0.578035f + e * (-0.668210f + e * 0.772500f);
    float s = scale[k] + (scale[k1] - scale[k]) * fr;
    return 0.5f + (u * u + v * v - edge * edge) * inv2e * s;
}
}  // namespace

SkyTrees::SkyTrees(const SkyDome& dome) : dome_(dome) {}

uint8_t SkyTrees::coverage(float px, float py) const {
    float u = (px - kTreeCx) * kInvRx, v = (py - kTreeCy) * kInvRy;
    if (u * u + v * v >= 1) return 255;
    float cov = treeCover(kTreeProfile.rho, kTreeProfile.scale, u, v, u != 0 ? 1 / std::fabs(u) : 0.f, v != 0 ? 1 / std::fabs(v) : 0.f);
    return uint8_t(clampf(cov, 0, 1) * 255 + 0.5f);
}

void SkyTrees::update(float, DamageList& damage) {
    if (full_) damage.addAll();
    full_ = false;
}

GFX_HOT void SkyTrees::draw(Canvas& c) {
    const Rect& cl = c.clip();
    const Rgb col = dome_.treeColour();
    const uint16_t solid = rgb565(col);
    // Inside rhoIn nothing is covered: the lowest tip less the widest anti-aliasing reach.
    const float rhoIn = kTreeProfile.rhoMin - 0.5f / kMinScale - 0.002f, rhoIn2 = rhoIn * rhoIn;
    for (int y = cl.y; y < cl.bottom(); ++y) {
        float v = (y + 0.5f - kTreeCy) * kInvRy, v2 = v * v;
        // row[i] is pixel (cl.x + i, y).
        const int rx = cl.x;
        uint16_t* row = &c.at(rx, y);
        if (v2 >= 1) {
            for (int x = cl.x; x < cl.right(); ++x) row[x - rx] = solid;
            continue;
        }
        float hs = kRx * std::sqrt(1 - v2);  // solid outside the ellipse
        int l0 = std::max(cl.x, std::min(cl.right(), int(std::floor(kTreeCx - hs - 0.5f)) + 1));
        int r0 = std::min(cl.right(), std::max(cl.x, int(std::ceil(kTreeCx + hs - 0.5f))));
        for (int x = cl.x; x < l0; ++x) row[x - rx] = solid;
        for (int x = r0; x < cl.right(); ++x) row[x - rx] = solid;
        // The band between the solid rim and the clear sky, which may be the whole row near the top
        // and bottom.
        int i0 = r0, i1 = r0;
        if (v2 < rhoIn2) {
            float hi = kRx * std::sqrt(rhoIn2 - v2);
            i0 = std::max(l0, int(std::floor(kTreeCx - hi - 0.5f)) + 1);
            i1 = std::min(r0, std::max(i0, int(std::ceil(kTreeCx + hi - 0.5f))));
        }
        const float invV = v != 0 ? 1 / std::fabs(v) : 0.f;  // one division a row
        auto band = [&](int x0, int x1) {
            for (int x = x0; x < x1; ++x) {
                float u = (x + 0.5f - kTreeCx) * kInvRx;
                float cov = treeCover(kTreeProfile.rho, kTreeProfile.scale, u, v, kInvU.v[x], invV);
                if (cov <= 0) continue;
                uint16_t& p = row[x - rx];
                p = cov >= 1 ? solid : blend565(p, solid, uint8_t(cov * 255));
            }
        };
        band(l0, std::min(i0, r0));
        band(std::max(i1, l0), r0);
    }
}

// ------------------------------------------------------------------ SkyInfo

void SkyInfo::update(float, DamageList& damage) {
    if (dirty_ && scene_) {
        dirty_ = false;
        const SceneFlight* top = highestFlight(scene_);
        hex_ = top ? top->hex : 0;
        if (!scene_->locationSet) {
            std::snprintf(title_, sizeof title_, "%s", t_.upperHeads ? "SET LOCATION" : "Set location");
            // The Wi-Fi portal (opened by holding the screen at boot) is the only way in for now.
            std::snprintf(sub_, sizeof sub_, "Hold screen at power-on");
        } else if (top) {
            char alt[16];
            ui::formatAltitude(alt, sizeof alt, top->altFt);
            const char* arrow = ui::trendArrow(top->vrateFpm);
            const char* sp = arrow[0] ? " " : "";
            std::snprintf(title_, sizeof title_, "%s", top->callsign);
            if (top->hasRoute)
                std::snprintf(sub_, sizeof sub_, "%s → %s · %s%s%s", top->orig, top->dest, alt, sp, arrow);
            else std::snprintf(sub_, sizeof sub_, "%s · %s%s%s", top->type, alt, sp, arrow);
        } else {
            std::snprintf(title_, sizeof title_, "%s", t_.upperHeads ? "QUIET SKIES" : "Quiet skies");
            std::snprintf(sub_, sizeof sub_, "%s", scene_->online ? "Nothing overhead right now" : "Offline");
        }
        if (scene_->timeValid) clock_.set(scene_->epoch);
        else clock_.clear();
        std::snprintf(moon_, sizeof moon_, "Moon %d%%", int(scene_->moonIllum * 100 + 0.5f));
        uint32_t sig = ui::hashStr(title_, ui::hashStr(sub_, ui::hashStr(clock_.text(), ui::hashStr(moon_))));
        if (sig != sig_) damage.add(kBox);
        sig_ = sig;
    }
    if (full_) damage.add(kBox);
    full_ = false;
}

void SkyInfo::draw(Canvas& c) {
    // Compass letters on the treetops; east and west are swapped because we are looking up.
    drawText(c, kFontSmall, 120, 15, "N", t_.textSoft, Align::Center, 190);
    drawText(c, kFontSmall, 9, 145, "E", t_.textDim, Align::Center, 160);
    drawText(c, kFontSmall, 231, 145, "W", t_.textDim, Align::Center, 160);
    if (!c.clip().intersects(kBox)) return;
    ui::shadowText(c, *t_.fontTitle, 14, 297, title_, t_.text, Align::Left, t_);
    drawText(c, kFontSmall, 14, 312, sub_, t_.textSoft);
    drawText(c, kFontBody, 226, 297, clock_.text(), t_.textSoft, Align::Right);
    drawText(c, kFontSmall, 226, 312, moon_, t_.textDim, Align::Right);
}

// ------------------------------------------------------------------ SkyViewScreen

void SkyViewScreen::enter() {
    Scratch* s = arena_.claim<Scratch>(this);
    dome_.attach(s->stars, arena_, this), planes_.attach(s->tracks, arena_, this);
    dome_.reset(), planes_.reset(), trees_.reset(), info_.reset();
}

NavRequest SkyViewScreen::onTap(int x, int y) {
    NavRequest r;
    uint32_t hex = y < SkyInfo::kBox.y ? planes_.hitTest(x, y, 22) : 0;
    if (!hex) hex = info_.highestHex();
    if (hex) r.open = true, r.to = ScreenId::Closest, r.selectHex = hex;
    return r;
}

}  // namespace scr
