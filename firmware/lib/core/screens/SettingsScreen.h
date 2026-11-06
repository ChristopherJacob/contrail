#pragma once
// The touch settings panel (config spec §5, mockup "Touch settings"): brightness, theme, radius and
// screen mode. Opened by holding the screen for 1 s (ScreenApp); outside the swipe order. A tap
// applies at once; the slider follows the finger and saves on release. Edits queue here for the
// platform, which saves them off the render thread.
#include "Director.h"
#include "Screen.h"
#include "Theme.h"

namespace scr {

// What the panel shows; the platform passes it from the settings (app::panelValues).
struct PanelValues {
    int brightness = 200;  // the manual level, 5..255
    bool editorial = false;
    int radiusNm = 25;
    DirectorMode mode = DirectorMode::Auto;
};

// One setting to save: a registry key (svc::key) and the text form Settings::set takes.
struct SettingEdit {
    const char* key = nullptr;
    char value[12] = {};
};

class SettingsView : public gfx::Layer {
   public:
    static constexpr gfx::Rect kDone{170, 10, 58, 30};
    static constexpr gfx::Rect kBrightCard{12, 50, 216, 58};
    static constexpr gfx::Rect kThemeCard{12, 114, 216, 58};
    static constexpr gfx::Rect kRadiusCard{12, 178, 216, 52};
    static constexpr gfx::Rect kModeCard{12, 236, 216, 58};
    static constexpr int kSliderX0 = 26, kSliderX1 = 214, kSliderY = 92;
    static constexpr gfx::Rect kSliderHit{12, 76, 216, 32};  // where a press grabs the slider
    static constexpr gfx::Rect kTheme[2] = {{22, 140, 96, 26}, {122, 140, 96, 26}};
    static constexpr gfx::Rect kMinus{88, 187, 34, 34}, kPlus{184, 187, 34, 34};
    static constexpr gfx::Rect kMode[3] = {{22, 262, 62, 26}, {89, 262, 62, 26}, {156, 262, 62, 26}};

    explicit SettingsView(const Theme& t) : t_(t) {}
    void reset() { full_ = true; }
    void setValues(const PanelValues& v);  // redraws the cards that changed
    const PanelValues& values() const { return v_; }
    static int brightnessAt(int x);  // the level for a slider position

    void update(float dt, gfx::DamageList& damage) override;
    void draw(gfx::Canvas& c) override;

   private:
    void dirty(const gfx::Rect& r);
    const char* cap(const char* mixed, const char* upper) const { return t_.upperHeads ? upper : mixed; }
    const gfx::FontData& labelFont() const;
    void drawHeader(gfx::Canvas& c) const;
    void cardBg(gfx::Canvas& c, const gfx::Rect& r) const;
    void rowLabel(gfx::Canvas& c, const gfx::Rect& card, int baseline, const char* mixed, const char* upper) const;
    void option(gfx::Canvas& c, const gfx::Rect& r, const char* label, bool on,
                const gfx::FontData* font = nullptr) const;

    const Theme& t_;
    PanelValues v_;
    bool full_ = true;
    gfx::Rect dirty_[4];
    int nDirty_ = 0;
};

class SettingsScreen : public Screen {
   public:
    static constexpr uint32_t kIdleCloseMs = 30000;
    static constexpr int kRadiusMin = 5, kRadiusMax = 100, kRadiusStep = 5;
    explicit SettingsScreen(const Theme& t) : view_(t) {}
    ScreenId id() const override { return ScreenId::Settings; }
    void setScene(const Scene*) override {}
    void enter() override { view_.reset(); }
    int layerCount() const override { return 1; }
    gfx::Layer* const* layers() override { return layers_; }

    // Opened at nowMs by a hold whose finger is still down: touches count from its release.
    void open(uint32_t nowMs);
    // The settings changed elsewhere (or a save landed). Ignored while the slider is held.
    void setValues(const PanelValues& v);
    const PanelValues& values() const { return view_.values(); }
    // The panel closed under the finger: end any drag without saving it, re-arm, and show the
    // values last passed to setValues (the dragged level was never saved).
    void cancel();
    // Every raw touch sample while the panel shows: the slider follows the finger.
    void raw(bool pressed, int x, int y, uint32_t nowMs);
    NavRequest onTap(int x, int y) override;
    bool takeEdit(SettingEdit& out);  // oldest first
    bool wantsClose(uint32_t nowMs) const { return close_ || nowMs - lastTouch_ >= kIdleCloseMs; }
    // The level to drive the backlight with while the slider is held, else -1.
    int previewBrightness() const { return dragging_ ? view_.values().brightness : -1; }

   private:
    static constexpr int kMaxEdits = 6;
    void push(const char* key, const char* value);
    void pushInt(const char* key, int v);

    SettingsView view_;
    PanelValues latest_;  // what setValues last got, kept for cancel()
    gfx::Layer* layers_[1] = {&view_};
    SettingEdit edits_[kMaxEdits];
    int nEdits_ = 0;
    bool armed_ = true, down_ = false, dragging_ = false, close_ = false;
    uint32_t lastTouch_ = 0;
};

}  // namespace scr
