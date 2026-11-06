#pragma once
#include "gfx/Layer.h"

namespace app {

// Sky colours for a sun elevation (degrees): top, middle and horizon stops.
struct SkyStops {
    gfx::Rgb top, mid, horizon;
};
SkyStops skyForSun(float elevationDeg);

// Full-screen cinematic sky: dithered gradient driven by the real sun elevation, a sun
// glow near the horizon, stars at night and a dark ridge silhouette along the bottom.
class SkyLayer : public gfx::Layer {
   public:
    void setSun(float elevationDeg, float azimuthDeg);
    // Draw only inside `area`, with the gradient spanning it (default: the whole screen).
    void setArea(const gfx::Rect& area) { area_ = area, dirty_ = true; }
    void setRidge(bool on) { ridge_ = on, dirty_ = true; }
    void invalidate() { dirty_ = true; }
    // A flat field of one colour instead of the sky (Bold Editorial's airline-colour band): no
    // gradient, stars, sun or ridge.
    void setFlat(bool on, gfx::Rgb col);
    // Where the sun (or its afterglow) is drawn; false when it is not.
    bool sunPoint(float& x, float& y) const;
    // True while the layer is drawing the sun's disc (its own latched elevation, not the scene's).
    bool sunDiscVisible() const { return !flat_ && elev_ > 0; }
    // Keep the star field out of these rectangles (up to 4), so stars do not sit beside header text.
    void setStarExclusions(const gfx::Rect* rects, int n);
    void update(float dt, gfx::DamageList& damage) override;
    void draw(gfx::Canvas& c) override;

   private:
    float elev_ = 5, az_ = 250;
    gfx::Rect area_{0, 0, 240, 320};
    bool ridge_ = true;
    bool dirty_ = true;
    bool flat_ = false;
    gfx::Rgb flatCol_{};
    static constexpr int kMaxExclusions = 4;
    gfx::Rect excl_[kMaxExclusions];
    int numExcl_ = 0;
    SkyStops stops_ = skyForSun(5);
};

}  // namespace app
