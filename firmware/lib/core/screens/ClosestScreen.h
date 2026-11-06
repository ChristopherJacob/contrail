#pragma once
// Closest Flight (mockup 1B). The featured flight is the selected one when it is still in
// the scene, otherwise the nearest. Tapping the card steps to the next-nearest.
#include "Screen.h"
#include "ScreenArena.h"
#include "Theme.h"
#include "app/SkyLayer.h"
#include "assets/LogoStore.h"
#include "gfx/Badge.h"
#include "ui/Format.h"
#include "ui/Identity.h"

namespace scr {

// The featured flight's logo, in the screen arena while Closest Flight is up (5a spec §3). A theme uses one
// size, so the two stores share memory: `flat` says which is live (ClosestScreen::enter sets it up).
struct ClosestLogo {
    char code[4];       // the airline the badge was loaded for; "" none
    bool flat = false;  // band (Bold Editorial) is the live member, else card (Cinematic)
    union Slot {
        Slot() : card() {}
        gfx::Badge40 card;  // Cinematic: beside the callsign
        gfx::Badge30 band;  // Bold Editorial: the band's top-left
    } slot;
};
using ClosestLogoRef = ArenaArray<ClosestLogo, 1>;

// The plane at its real heading in the sky band, with a contrail that grows behind it,
// and a compass showing where to look.
class PlaneBand : public gfx::Layer {
   public:
    static constexpr gfx::Rect kBand{0, 0, 240, 150};
    explicit PlaneBand(const Theme& t) : t_(t) {}
    void setFlight(const SceneFlight* f);
    // The flat field behind the plane (Bold Editorial): the plane, contrail and compass take the
    // colour that reads on it (ui::textOn).
    void setField(gfx::Rgb col) { field_ = col; }
    // Bold Editorial: the airline's name in capitals in the band's top-left (mockup 7A); "" for none.
    void setName(const char* name);
    // Bold Editorial: the flight's logo, fin or shape in the band's top-left, before the name; `show` false
    // for none (Cinematic, or no flight). `code` is the airline the logo is for: the logo's store is reused.
    void setIdentity(bool show, const ui::Identity& id, const char* code);
    // The arena scratch a logo lives in, checked while drawing.
    void watch(const ClosestLogoRef* scratch) { scratch_ = scratch; }
    // Keep the plane clear of the sun disc at (x, y); off when `on` is false.
    void setAvoid(bool on, float x, float y) { avoid_ = on, avoidX_ = x, avoidY_ = y; }
    void reset() { full_ = true; }
    void update(float dt, gfx::DamageList& damage) override;
    void draw(gfx::Canvas& c) override;

   private:
    // What draw() uses. update() latches it from the flight, quantised so that drift too
    // small to see does not redraw the band, and damages the old and new boxes when it changes.
    struct Drawn {
        bool has = false;
        int heading = 0, bearing = -1;  // whole degrees
        uint8_t shape = 0;              // data::Family
        float x = 0, y = 0, trail = 0;  // plane centre and contrail length (px)
        // Everything the plane's box depends on; the compass (bearing) is damaged separately.
        bool samePlane(const Drawn& o) const {
            return has == o.has && heading == o.heading && shape == o.shape && x == o.x && y == o.y &&
                   trail == o.trail;
        }
    };
    static gfx::Rect box(const Drawn& d);
    const Theme& t_;
    gfx::Rgb field_{0, 0, 0};
    char name_[24] = "";
    bool showId_ = false;
    ui::Identity id_;
    char idCode_[4] = "";
    const ClosestLogoRef* scratch_ = nullptr;
    uint32_t hex_ = 0;
    bool has_ = false, full_ = true, avoid_ = false;
    uint8_t shape_ = 0;  // data::Family of the flight
    float avoidX_ = 0, avoidY_ = 0;
    float heading_ = 0, bearing_ = 0, t_s_ = 0, trail_ = 0;
    Drawn drawn_;
};

class InfoCard : public gfx::Layer {
   public:
    static constexpr int kTop = 136;
    explicit InfoCard(const Theme& t) : t_(t) {}
    void set(const Scene* s, const SceneFlight* f, int index);
    // Cinematic: the flight's logo (nullptr: its fin or shape) beside the callsign; Bold Editorial's card has
    // none. Read on the next rebuild, which set() asks for.
    void setLogo(const gfx::Badge* logo) { logo_ = logo; }
    void watch(const ClosestLogoRef* scratch) { scratch_ = scratch; }
    void reset() { full_ = dirty_ = true; }
    void update(float dt, gfx::DamageList& damage) override;
    void draw(gfx::Canvas& c) override;

   private:
    // Everything draw() reads, rebuilt only when the scene or the featured flight changes,
    // so what is drawn always matches what was damaged.
    struct Text {
        bool has, hasRoute, bigCallsign;
        char callsign[10], airline[28], typeLine[32], regLine[12];
        char orig[4], dest[4], progress[32], origCity[28], destCity[28];
        float progressFrac;  // < 0: no bar; else the drawn (whole-percent) position
        uint8_t shape;       // data::Family: its side view marks the progress; 0 the bar's dot
        gfx::Rgb brand;      // the airline's colour (or the neutral one)
        char tiles[3][3][24];  // label, value, sub
        uint8_t trend;         // 0 level, 1 climbing, 2 descending: colour of the altitude sub-line
        char footer[32];
        // The identity beside the callsign (Cinematic).
        const gfx::Badge* idLogo;
        ui::IdentityKey id;
    };
    void build(Text& t);
    const Theme& t_;
    const Scene* scene_ = nullptr;
    const SceneFlight* f_ = nullptr;
    const gfx::Badge* logo_ = nullptr;
    const ClosestLogoRef* scratch_ = nullptr;
    int index_ = 0;
    bool full_ = true, dirty_ = true, shownHas_ = false;
    static constexpr int kRegions = 6;  // header, route, three tiles, footer
    uint32_t sig_[kRegions] = {};
    ui::MinuteClock eta_;  // the arrival estimate
    Text text_{};
};

class ClosestScreen : public Screen {
   public:
    ClosestScreen(const Theme& t, ScreenArena& a);
    // Where logos come from (nullptr: fins only). The caller re-enters the screen so it loads them.
    void setLogos(assets::LogoStore* l) { logos_ = l; }
    ScreenId id() const override { return ScreenId::Closest; }
    void setScene(const Scene* s) override;
    void setSelection(uint32_t hex) override;
    void enter() override;
    NavRequest onTap(int x, int y) override;
    int layerCount() const override { return 3; }
    gfx::Layer* const* layers() override { return layers_; }

   private:
    void refresh();
    void loadLogo(const SceneFlight* f);
    const Theme& t_;
    ScreenArena& arena_;
    assets::LogoStore* logos_ = nullptr;
    ClosestLogoRef logo_;  // attached in enter()
    const Scene* scene_ = nullptr;
    app::SkyLayer sky_;
    PlaneBand plane_;
    InfoCard card_;
    gfx::Layer* layers_[3] = {&sky_, &plane_, &card_};
};

}  // namespace scr
