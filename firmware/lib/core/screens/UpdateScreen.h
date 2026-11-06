#pragma once
// Full-screen progress while a firmware upload runs (config spec §6, mockup "Firmware update on
// the device"). Only the percentage and bar redraw as it climbs.
#include "Screen.h"
#include "Theme.h"

namespace scr {

class UpdateView : public gfx::Layer {
   public:
    static constexpr gfx::Rect kLive{0, 158, 240, 76};  // the percentage and the bar
    explicit UpdateView(const Theme& t) : t_(t) {}
    void reset() { full_ = true; }
    void setPercent(int p) {
        if (p != pct_) pct_ = p, dirty_ = true;
    }
    int percent() const { return pct_; }
    void update(float dt, gfx::DamageList& damage) override;
    void draw(gfx::Canvas& c) override;

   private:
    const Theme& t_;
    int pct_ = 0;
    bool full_ = true, dirty_ = false;
};

class UpdateScreen : public Screen {
   public:
    explicit UpdateScreen(const Theme& t) : view_(t) {}
    ScreenId id() const override { return ScreenId::Update; }
    void setScene(const Scene*) override {}
    void enter() override { view_.reset(); }
    void setPercent(int p) { view_.setPercent(p); }
    int layerCount() const override { return 1; }
    gfx::Layer* const* layers() override { return layers_; }

   private:
    UpdateView view_;
    gfx::Layer* layers_[1] = {&view_};
};

}  // namespace scr
