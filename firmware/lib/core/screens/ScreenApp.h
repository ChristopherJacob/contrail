#pragma once
// Everything on screen: the scene, the screens, the host that runs them, the gesture
// detector, the director, the alert engine and the overlays. The platform rebuilds scene() through
// CoreApp::renderTick, then calls sceneUpdated(), feeds touch() and tick() every frame,
// and hands layers() to its StripRenderer.
#include "AlertScreen.h"
#include "BoardScreen.h"
#include "ClosestScreen.h"
#include "Director.h"
#include "Gesture.h"
#include "MapScreen.h"
#include "MapTrails.h"
#include "Overlays.h"
#include "RadarScreen.h"
#include "Scene.h"
#include "ScreenArena.h"
#include "ScreenHost.h"
#include "SettingsScreen.h"
#include "SkyViewScreen.h"
#include "StatsScreen.h"
#include "Theme.h"
#include "UpdateScreen.h"
#include "alerts/AlertEngine.h"
#include "alerts/AlertLights.h"
#include "assets/LogoStore.h"

namespace scr {

class ScreenApp {
   public:
    explicit ScreenApp(const Theme& theme = cinematicSky());
    Scene& scene() { return scene_; }
    // Draw from now on with a copy of `t`: the current screen is entered again (it latches
    // colours when it enters) and everything redraws. Boot, bench and soak use it; it is not
    // the user-facing live theme switch (sub-project 4).
    void useTheme(const Theme& t);
    const Theme& theme() const { return theme_; }
    // Where airline logos come from (nullptr: fins only); the current screen is entered again to load them.
    // The store must outlive the app.
    void setLogos(assets::LogoStore* l);
    // Where the Live Map's detailed chart comes from (nullptr, or a pack that did not open: the built-in map); the
    // current screen is entered again. The pack must outlive the app.
    void setMap(basemap::MapPack* p);
    // A microsecond clock for the map's chart job (its time budget a frame, and its log line).
    void setMapClock(uint32_t (*us)()) { map_.setClock(us); }
    void sceneUpdated() { trails_.record(scene_), host_.sceneChanged(); }
    void touch(bool pressed, int x, int y, uint32_t nowMs);
    void tick(uint32_t nowMs);
    ScreenHost& host() { return host_; }
    void configure(DirectorMode mode, uint32_t carouselMs) { director_.configure(mode, carouselMs); }
    // Alerts start switched off (every kind false) until the platform passes the settings.
    void configureAlerts(const alerts::AlertConfig& c) { alerts_.configure(c); }
    void injectAlert(AlertKind k, uint32_t nowMs) { alerts_.inject(k, nowMs); }
    // The LED and backlight for the alert showing (or none) at nowMs.
    // Off while the panel is open: a non-emergency alert waits for it to close.
    alerts::LightOut alertLights(uint32_t nowMs) const {
        if (panel_) return {};
        return alerts::lightsFor(scene_.alert, nowMs - scene_.alert.startMs);
    }
    // The touch panel's values (config spec §5); the platform passes them from the settings.
    void setPanelValues(const PanelValues& v) { settings_.setValues(v); }
    // A firmware upload is running at `percent` (0..100), or none (-1): the Update screen covers
    // everything and touch is ignored. Called every frame by the platform; cheap when unchanged.
    void setUpdateProgress(int percent);
    bool updating() const { return updating_; }
    const PanelValues& panelValues() const { return settings_.values(); }
    bool panelOpen() const { return panel_; }
    // Panel edits waiting to be saved, oldest first (the platform saves them off the render thread).
    bool takeSettingEdit(SettingEdit& e) { return settings_.takeEdit(e); }
    // The backlight level while the panel's slider is held, else -1.
    int previewBrightness() const { return panel_ ? settings_.previewBrightness() : -1; }
    const HoldRing& holdRing() const { return ring_; }
    Director& director() { return director_; }
    const PageDots& pageDots() const { return dots_; }
    const OfflineBadge& offlineBadge() const { return badge_; }
    gfx::Layer* const* layers() { return layers_; }
    int layerCount() const { return 1; }

   private:
    void handle(const NavRequest& r);
    void step(int dir);
    void openPanel(uint32_t now);
    void closePanel(uint32_t now);

    Theme theme_;  // first: every screen holds a reference to it
    Scene scene_;
    ScreenArena arena_;  // before the screens, which hold references to it
    MapTrails trails_;   // fed on every rebuild, whatever screen is up (the Map reads it)
    ClosestScreen closest_;
    RadarScreen radar_;
    MapScreen map_;
    BoardScreen board_;
    SkyViewScreen sky_;
    StatsScreen stats_;
    AlertScreen alert_;
    SettingsScreen settings_;
    UpdateScreen update_;
    static constexpr int kScreens = 9;
    Screen* screens_[kScreens];
    ScreenHost host_;
    GestureDetector gestures_;
    Director director_;
    alerts::AlertEngine alerts_;
    uint32_t alertVersion_ = 0;  // the scene version the engine last ran on
    PageDots dots_;
    OfflineBadge badge_;
    HoldRing ring_;
    gfx::Layer* layers_[1];
    bool started_ = false;
    bool updating_ = false;
    ScreenId beforeUpdate_ = ScreenId::Radar;
    bool panel_ = false;
    ScreenId beforePanel_ = ScreenId::Radar;
    bool exploring_ = false;  // last tick's explore state; leaving it drops the selection
};

}  // namespace scr
