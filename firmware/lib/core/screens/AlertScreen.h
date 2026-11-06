#pragma once
// The alert takeover (alerts spec §6, mockup 8A). Cinematic Sky: "Emergency beacon", the plane at
// its track inside rings that pulse outward, the callsign as hero, the reason in a pill. Bold
// Editorial: "Colour field", the top half in the alert's colour with the squawk or kind huge. Both:
// a border pulsing in the alert's colour and a countdown bar.
#include "Screen.h"
#include "ScreenArena.h"
#include "Theme.h"
#include "assets/LogoStore.h"
#include "gfx/Badge.h"
#include "ui/Identity.h"

namespace scr {

// The alerting flight's logo, in the screen arena while the Alert is up (5a spec §3). The Alert takes over
// from any screen; the host enters the screen underneath again when it ends, and that screen claims the arena
// back and reloads what it keeps there.
struct AlertLogo {
    char code[4];  // the airline the badge was loaded for; "" none
    gfx::Badge40 badge;
};
using AlertLogoRef = ArenaArray<AlertLogo, 1>;

class AlertView : public gfx::Layer {
   public:
    explicit AlertView(const Theme& t) : t_(t) {}
    void setScene(const Scene* s) { scene_ = s; }
    // The logo's scratch, the arena it lives in and its owner (AlertScreen), and where logos come from.
    void watch(AlertLogoRef* scratch, const ScreenArena* arena, const void* owner) {
        scratch_ = scratch, arena_ = arena, owner_ = owner;
    }
    void setLogos(assets::LogoStore* l) { logos_ = l; }
    void reset() { full_ = true, built_ = false; }
    void update(float dt, gfx::DamageList& damage) override;
    void draw(gfx::Canvas& c) override;

   private:
    // Everything draw() reads from the alert, rebuilt every update. A change to the kind, title,
    // big text or live redraws the screen; any other field redraws its own rows.
    struct Text {
        char title[20], hero[12], sub[32], reason[40], big[12], stats[40];
        char stat[3][16];
        int heading;
        uint8_t shape;  // data::Family
        AlertKind kind;
        bool live;
        // The badge beside the hero. The family only for a registration (an airline's badge does not show it,
        // so a new shape must not redraw the hero).
        const gfx::Badge* idLogo;
        char idCode[4];  // the airline idLogo was loaded for: a new logo redraws even if the hero did not change
        ui::IdentityKey id;
    };
    // What draw() would otherwise work out again for every block it is called for (a pulse redraws
    // the border's ~40 blocks): colours (readableOn and textOn take dozens of powf calls), faces and
    // the lines' left ends. Rebuilt with text_.
    struct Look {
        gfx::Rgb title, ink;  // the title on the sky, text on the alert's colour
        const gfx::FontData *pill, *big, *hero;  // hero: the display face, or the title face when too wide
        int pillW, titleX, heroX, heroY, badgeX, subX, reasonX, statsX;
        int statW[3];
    };
    void loadLogo();
    const AlertLogo* loaded() const;  // the scratch, when it holds a loaded badge; else nullptr
    void build(Text& t) const;
    void layout();
    void damageChanged(const Text& t, gfx::DamageList& damage) const;  // what differs from text_
    void drawBorder(gfx::Canvas& c, gfx::Rgb col) const;
    void drawBadge(gfx::Canvas& c, int x, int y) const;  // the logo, fin or shape tile
    void drawCinematic(gfx::Canvas& c, gfx::Rgb col) const;
    void drawEditorial(gfx::Canvas& c, gfx::Rgb col) const;
    int borderWidth() const { return t_.flat ? 6 : 8; }
    gfx::Rect bar() const;

    const Theme& t_;
    const Scene* scene_ = nullptr;
    AlertLogoRef* scratch_ = nullptr;
    const ScreenArena* arena_ = nullptr;
    const void* owner_ = nullptr;
    assets::LogoStore* logos_ = nullptr;
    bool full_ = true;
    uint32_t startMs_ = 0;  // which alert the clock is for
    float t_s_ = 0;         // seconds into it
    uint8_t border_ = 0, ring_ = 0;  // the pulse levels drawn (border 0..15, ring step 0..29)
    int bar_ = -1;                   // the countdown's drawn width
    Text text_{};
    Look look_{};
    // The alert text_ and look_ were built from, byte for byte: building takes ~0.5 ms of snprintf on
    // the device and update runs every loop, so they are rebuilt only when it changes (or on enter,
    // which a theme change goes through).
    SceneAlert seen_{};
    bool built_ = false;
};

class AlertScreen : public Screen {
   public:
    AlertScreen(const Theme& t, ScreenArena& a) : arena_(a), view_(t) { view_.watch(&logo_, &arena_, this); }
    // Where logos come from (nullptr: fins only). The caller re-enters the screen so it loads them.
    void setLogos(assets::LogoStore* l) { view_.setLogos(l); }
    ScreenId id() const override { return ScreenId::Alert; }
    void setScene(const Scene* s) override { view_.setScene(s); }
    void enter() override;
    int layerCount() const override { return 1; }
    gfx::Layer* const* layers() override { return layers_; }

   private:
    ScreenArena& arena_;
    AlertLogoRef logo_;  // attached in enter()
    AlertView view_;
    gfx::Layer* layers_[1] = {&view_};
};

}  // namespace scr
