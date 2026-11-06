#pragma once
// Runs the active screen as a single composite layer for the strip renderer, and
// switches screens by dipping through the theme's veil colour: fade out, swap, fade in.
// Only the active screen is updated and drawn, so a transition never renders twice.
#include "Screen.h"
#include "Theme.h"

namespace scr {

class ScreenHost : public gfx::Layer {
   public:
    static constexpr float kFadeS = 0.2f;  // each half of a transition
    ScreenHost(Screen* const* screens, int n, const Theme& theme);

    Screen* screen(ScreenId id) const;
    ScreenId current() const { return cur_->id(); }
    // Where the host is heading (the current screen when idle).
    ScreenId target() const { return next_ ? next_->id() : cur_->id(); }
    bool transitioning() const { return phase_ != Phase::Idle; }
    uint8_t veilAlpha() const;

    void showNow(ScreenId id);  // cut without a fade (boot)
    void show(ScreenId id);     // fade; ignored if already there or heading there
    void setScene(const Scene* scene);
    void sceneChanged();        // the scene was rebuilt in place
    void setSelection(uint32_t hex);
    uint32_t selection() const { return selected_; }
    NavRequest tap(int x, int y);  // ignored mid-transition
    void verticalSwipe(int dir);   // ignored mid-transition
    void addOverlay(gfx::Layer* layer);

    void update(float dt, gfx::DamageList& damage) override;
    void draw(gfx::Canvas& c) override;

   private:
    enum class Phase { Idle, Out, In };
    void activate(Screen* s);

    Screen* const* screens_;
    int n_;
    const Theme& theme_;
    Screen* cur_;
    Screen* next_ = nullptr;
    const Scene* scene_ = nullptr;
    uint32_t selected_ = 0;
    Phase phase_ = Phase::Idle;
    float t_ = 0;
    bool full_ = true;
    static constexpr int kMaxOverlays = 3;
    gfx::Layer* overlays_[kMaxOverlays] = {};
    int nOverlays_ = 0;
};

}  // namespace scr
