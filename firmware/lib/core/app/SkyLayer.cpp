#include "SkyLayer.h"

#include <algorithm>
#include <cmath>

#include "gfx/Raster.h"

namespace app {
using namespace gfx;

namespace {

struct SkyKey {
    float elev;
    SkyStops s;
};

// Hand-tuned keyframes from deep night to full day.
const SkyKey kKeys[] = {
    {-18, {{2, 4, 14}, {6, 10, 30}, {14, 20, 44}}},
    {-8, {{6, 10, 34}, {22, 30, 76}, {70, 56, 100}}},
    {-2, {{10, 18, 54}, {44, 60, 128}, {226, 120, 84}}},
    {4, {{18, 40, 104}, {72, 110, 186}, {250, 176, 110}}},
    {12, {{24, 72, 168}, {84, 144, 222}, {214, 214, 230}}},
    {30, {{22, 88, 196}, {78, 152, 236}, {170, 212, 248}}},
};
constexpr int kNumKeys = sizeof(kKeys) / sizeof(kKeys[0]);

constexpr int kStars = 70;

uint32_t lcg(uint32_t& s) { return s = s * 1664525u + 1013904223u; }

}  // namespace

SkyStops skyForSun(float e) {
    if (e <= kKeys[0].elev) return kKeys[0].s;
    for (int i = 1; i < kNumKeys; ++i) {
        if (e <= kKeys[i].elev) {
            float t = (e - kKeys[i - 1].elev) / (kKeys[i].elev - kKeys[i - 1].elev);
            const SkyStops &a = kKeys[i - 1].s, &b = kKeys[i].s;
            return {lerp(a.top, b.top, t), lerp(a.mid, b.mid, t), lerp(a.horizon, b.horizon, t)};
        }
    }
    return kKeys[kNumKeys - 1].s;
}

void SkyLayer::setSun(float elevationDeg, float azimuthDeg) {
    if (std::fabs(elevationDeg - elev_) < 0.25f && std::fabs(azimuthDeg - az_) < 1.f) return;
    elev_ = elevationDeg, az_ = azimuthDeg;
    stops_ = skyForSun(elev_);
    dirty_ = true;
}

void SkyLayer::setStarExclusions(const gfx::Rect* rects, int n) {
    n = std::max(0, std::min(n, kMaxExclusions));
    bool same = n == numExcl_;
    for (int i = 0; same && i < n; ++i)
        same = rects[i] == excl_[i];
    if (same) return;
    for (int i = 0; i < n; ++i) excl_[i] = rects[i];
    numExcl_ = n;
    dirty_ = true;
}

void SkyLayer::setFlat(bool on, Rgb col) {
    bool same = on == flat_ && (!on || (col.r == flatCol_.r && col.g == flatCol_.g && col.b == flatCol_.b));
    if (same) return;
    flat_ = on, flatCol_ = col, dirty_ = true;
}

bool SkyLayer::sunPoint(float& x, float& y) const {
    if (flat_ || elev_ <= -8) return false;
    float scale = area_.h / 320.f;
    x = area_.x + area_.w * 0.5f + std::sin((az_ - 180.f) * 3.14159f / 180.f) * area_.w * 0.45f;
    y = area_.bottom() - 40 * scale - elev_ * 6.f * scale;
    return true;
}

void SkyLayer::update(float, DamageList& damage) {
    if (dirty_) damage.add(area_);
    dirty_ = false;
}

void SkyLayer::draw(Canvas& c) {
    if (!c.clip().intersects(area_)) return;
    Rect saved = c.clip();
    c.setClip(saved.intersect(area_));
    if (flat_) {
        c.fill(area_, rgb565(flatCol_));
        c.setClip(saved);
        return;
    }
    const Rect& a = area_;
    Rgb stops[] = {stops_.top, stops_.mid, stops_.horizon};
    float pos[] = {0.f, 0.58f, 1.f};
    c.vGradient(a, stops, pos, 3);

    // Stars fade in through nautical twilight.
    float starAlpha = elev_ < -12 ? 1.f : elev_ < -4 ? (-4 - elev_) / 8.f : 0.f;
    if (starAlpha > 0) {
        uint32_t seed = 0xC0FFEE;
        for (int i = 0; i < kStars; ++i) {
            int x = a.x + int(lcg(seed) % uint32_t(a.w));
            int y = a.y + int(lcg(seed) % uint32_t(std::max(1, a.h * 2 / 3)));
            uint8_t b = uint8_t((120 + lcg(seed) % 135) * starAlpha);
            // The generator still advances for skipped stars, so the rest keep their places.
            bool hidden = false;
            for (int k = 0; k < numExcl_ && !hidden; ++k) {
                const Rect& e = excl_[k];
                hidden = x >= e.x - 1 && x < e.right() + 1 && y >= e.y - 1 && y < e.bottom() + 1;  // 1 px for the big stars
            }
            if (hidden) continue;
            if (i % 9 == 0) aaCircle(c, x + 0.5f, y + 0.5f, 0.9f, {255, 250, 235}, b);
            else c.blendPixel(x, y, {235, 240, 255}, b);
        }
    }

    // Sun (or its afterglow) near the horizon, placed left-right by azimuth.
    float x, y;
    if (sunPoint(x, y)) {
        // Keep the glow readable in a short band, where plain scaling would shrink it to nothing.
        float scale = a.h / 320.f;
        uint8_t al = uint8_t(elev_ > 0 ? 170 : 170 * (elev_ + 8) / 8);
        c.radialGlow(x, y, std::max(120 * scale, 40.f), {255, 170, 90}, al / 2);
        c.radialGlow(x, y, std::max(40 * scale, 12.f), {255, 220, 160}, al);
        if (elev_ > 0) aaCircle(c, x, y, 9, {255, 244, 214});
    }

    if (ridge_) {
        static const Vec2 ridge[] = {{0, 320},   {0, 296},   {28, 288},  {52, 293},  {80, 281}, {104, 287},
                                     {131, 279}, {158, 289}, {186, 284}, {214, 292}, {240, 286}, {240, 320}};
        Rgb ground = lerp(stops_.top, {0, 0, 0}, 0.6f);
        aaPolygon(c, ridge, sizeof(ridge) / sizeof(ridge[0]), ground);
    }
    c.setClip(saved);
}

}  // namespace app
