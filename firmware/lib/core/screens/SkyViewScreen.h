#pragma once
// Look-Up Sky View (mockup 4B): the whole screen is the sky as seen lying on your back,
// zenith in the centre, north at the top and east on the LEFT, ringed by treetops. The sky
// colours follow the real sun; at night the real bright stars and the moon (lit limb toward
// the sun) come out; planes sit at their real azimuth and elevation.
//
// Layers, back to front: the dome (sky, sun, stars, moon), the treeline, the planes (drawn over
// the trees, so even low planes at the rim stay visible) and the caption.
#include "Screen.h"
#include "ScreenArena.h"
#include "SkyProjection.h"
#include "Theme.h"
#include "app/SkyLayer.h"
#include "ui/Format.h"

namespace scr {

// Sky gradient, sun glow, stars and moon: everything that changes at most about once a
// minute. Stars are kept sorted by row so a strip only walks the stars it contains.
class SkyDome : public gfx::Layer {
   public:
    explicit SkyDome(const Theme& t) : t_(t) {}
    void setScene(const Scene* s) { scene_ = s, dirty_ = true; }
    void reset() { full_ = dirty_ = true; }
    void update(float dt, gfx::DamageList& damage) override;
    void draw(gfx::Canvas& c) override;

    // Latched in update(); changing any of them redraws the whole screen.
    float sunElev() const { return sunElev_; }
    gfx::Rgb treeColour() const { return tree_; }

    // Packed into 4 bytes: stars above the trees lie within the screen, so quarter pixels fit
    // in 10 bits across and 11 down.
    struct StarPt {
        uint32_t y4 : 11, x4 : 10;  // quarter pixels
        uint32_t kind : 2;           // 0 faint (2x2 splat), 1 bright, 2 brilliant (with a halo)
        uint32_t a : 8;              // peak alpha
    };
    static_assert(sizeof(StarPt) == 4, "StarPt should pack into one word");
    // Half the sky is up at any moment: at most 166 of the catalogue's 287 stars anywhere on
    // Earth (test_sky checks), so this never runs out.
    static constexpr int kMaxStars = 180;
    // The stars live in the screen arena while the view is up; attaching forgets any placed.
    void attach(StarPt* stars, const ScreenArena& a, const void* owner) {
        stars_.attach(stars, a, owner), nStars_ = 0, starEpoch_ = INT64_MIN;
    }

   private:
    void recompute();
    void computeStars();
    void drawStars(gfx::Canvas& c) const;
    void drawMoon(gfx::Canvas& c) const;

    const Theme& t_;
    const Scene* scene_ = nullptr;
    DomeGeom g_;
    ArenaArray<StarPt, kMaxStars> stars_;
    int nStars_ = 0;
    int64_t starEpoch_ = INT64_MIN;  // when the stars were placed; INT64_MIN: not placed
    // Drawn state.
    float sunElev_ = 99, sunAz_ = 0, moonElev_ = -90, moonAz_ = 0, moonIllum_ = 0;
    app::SkyStops stops_{};
    gfx::Rgb tree_{};
    float gradCx_ = 120, gradCy_ = 140;  // the gradient's centre leans away from a low sun
    float starF_ = 0;                      // star visibility 0..1
    gfx::Vec2 sunPt_{}, moonPt_{}, limb_{0, -1};
    int64_t minute_ = INT64_MIN;
    bool full_ = true, dirty_ = true;
};

// Planes at their real direction and elevation, with contrails and blinking beacons. Each
// plane's drawn state is quantised so sub-pixel drift does not redraw it.
class SkyPlanes : public gfx::Layer {
   public:
    static constexpr int kMaxTracks = 12;
    SkyPlanes(const Theme& t, const SkyDome& dome) : t_(t), dome_(dome) {}
    void setScene(const Scene* s) { scene_ = s, dirty_ = true; }
    void reset();
    // The plane nearest (x, y) within maxPx, or 0.
    uint32_t hitTest(int x, int y, int maxPx) const;
    void update(float dt, gfx::DamageList& damage) override;
    void draw(gfx::Canvas& c) override;

    struct Drawn {
        float x = 0, y = 0;  // half-pixel steps
        int16_t heading = 0;  // glyph heading, whole degrees (mirrored track)
        int16_t track = 0;    // whole degrees, for the wingtip lights
        uint8_t size = 0, trail = 0;
        bool tag = false;  // the highest plane carries its callsign
        char callsign[9] = "";
        bool same(const Drawn& o) const;
    };
    struct Track {
        uint32_t hex = 0;
        bool live = false, beacon = false;
        float phase = 0;  // beacon offset, so beacons don't flash in step
        Drawn d;
        gfx::Rect box;
    };
    // The tracks live in the screen arena while the view is up.
    void attach(Track* tracks, const ScreenArena& a, const void* owner) { tracks_.attach(tracks, a, owner); }

   private:
    gfx::Rect boxFor(const Drawn& d) const;
    static gfx::Rect beaconBox(const Drawn& d);
    void sync(gfx::Rect* pending, int& n);
    bool beaconAt(const Track& k) const;

    const Theme& t_;
    const SkyDome& dome_;
    const Scene* scene_ = nullptr;
    ArenaArray<Track, kMaxTracks> tracks_;
    float t_s_ = 0;
    bool dirty_ = true, full_ = true;
};

// The treetops ringing the dome, drawn under the planes. The silhouette is a radius profile by
// angle round the ellipse, so it is evaluated only in the thin band where the tips are; outside
// it is solid, inside it is untouched.
class SkyTrees : public gfx::Layer {
   public:
    // Deliberately 4 px outside the dome's horizon, so the brush line sits on it.
    static constexpr float kRx = DomeGeom{}.rx + 4, kRy = DomeGeom{}.ry + 4;
    static constexpr int kBins = 1024;               // silhouette samples round the rim
    SkyTrees(const SkyDome& dome);
    void reset() { full_ = true; }
    void update(float dt, gfx::DamageList& damage) override;
    void draw(gfx::Canvas& c) override;
    // Tree coverage (0..255) at a pixel centre; for tests and hit-testing.
    uint8_t coverage(float x, float y) const;

   private:
    // The silhouette itself is a const table in flash (TreeProfile.h).
    const SkyDome& dome_;
    bool full_ = true;
};

// Bottom caption: the highest plane, or "Quiet skies"; the time and the moon's phase.
class SkyInfo : public gfx::Layer {
   public:
    static constexpr gfx::Rect kBox{0, 274, 240, 46};
    explicit SkyInfo(const Theme& t) : t_(t) {}
    void setScene(const Scene* s) { scene_ = s, dirty_ = true; }
    void reset() { full_ = dirty_ = true; }
    uint32_t highestHex() const { return hex_; }
    void update(float dt, gfx::DamageList& damage) override;
    void draw(gfx::Canvas& c) override;

   private:
    const Theme& t_;
    const Scene* scene_ = nullptr;
    char title_[24] = "", sub_[48] = "", moon_[16] = "";
    ui::MinuteClock clock_;
    uint32_t sig_ = 0, hex_ = 0;
    bool full_ = true, dirty_ = true;
};

// The highest plane shown (a real hex, at least a degree up), or null.
const SceneFlight* highestFlight(const Scene* s);

class SkyViewScreen : public Screen {
   public:
    SkyViewScreen(const Theme& t, ScreenArena& a) : dome_(t), planes_(t, dome_), trees_(dome_), info_(t), arena_(a) {}
    ScreenId id() const override { return ScreenId::Sky; }
    void setScene(const Scene* s) override { dome_.setScene(s), planes_.setScene(s), info_.setScene(s); }
    void enter() override;
    NavRequest onTap(int x, int y) override;
    int layerCount() const override { return 4; }
    gfx::Layer* const* layers() override { return layers_; }

   private:
    // The dome's star list and the planes' tracks live in the screen arena while the view is up.
    struct Scratch {
        SkyDome::StarPt stars[SkyDome::kMaxStars];
        SkyPlanes::Track tracks[SkyPlanes::kMaxTracks];
    };
    SkyDome dome_;
    SkyPlanes planes_;
    SkyTrees trees_;
    SkyInfo info_;
    ScreenArena& arena_;
    gfx::Layer* layers_[4] = {&dome_, &trees_, &planes_, &info_};
};

}  // namespace scr
