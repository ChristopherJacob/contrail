#pragma once
// Small layers the host draws above whichever screen is active (they fade with it). Both
// sit where none of the screens put content: see the placement notes below.
#include "Theme.h"
#include "gfx/Layer.h"

namespace scr {

// Dots along the bottom edge while the user is exploring, one per screen in swipe order.
// The bottom 6 px are empty on every screen: Closest's footer and Sky's caption have their
// baseline at y = 312 and the Board's last card ends at y = 314; Map and Stats keep the bottom 6 px
// free too.
class PageDots : public gfx::Layer {
   public:
    static constexpr int kY = 317;
    static constexpr gfx::Rect kBox{88, 312, 64, 8};  // room for six dots
    explicit PageDots(const Theme& t) : t_(t) {}
    void set(bool visible, int active, int count);
    bool visible() const { return visible_; }
    void update(float, gfx::DamageList& d) override;
    void draw(gfx::Canvas& c) override;

   private:
    const Theme& t_;
    bool visible_ = false, dirty_ = false;
    int active_ = 0, count_ = 4;
};

// "offline" pill when there has been no good poll for a minute (or no network). Top row,
// right of centre: the Sky View's "N" sits at the top centre (x ≈ 116..124), the Radar's
// aircraft count starts at x ≈ 177 ("32 aircraft", the most it shows), the Board's clock
// and the Closest compass start at x ≈ 193, and the left headers end by x ≈ 97.
class OfflineBadge : public gfx::Layer {
   public:
    static constexpr gfx::Rect kBox{128, 6, 44, 15};
    explicit OfflineBadge(const Theme& t) : t_(t) {}
    void set(bool on);
    bool on() const { return on_; }
    void update(float, gfx::DamageList& d) override;
    void draw(gfx::Canvas& c) override;

   private:
    const Theme& t_;
    bool on_ = false, dirty_ = false;
};

// The ring that fills while a finger holds still (config spec §5), centred on the press but kept
// clear of the edges, with a hint under it (above it near the bottom).
class HoldRing : public gfx::Layer {
   public:
    static constexpr int kR = 36, kHalf = 46;  // ring radius; half the box around it
    explicit HoldRing(const Theme& t) : t_(t) {}
    void set(bool visible, int x, int y, float progress);
    bool visible() const { return visible_; }
    void update(float, gfx::DamageList& d) override;
    void draw(gfx::Canvas& c) override;

   private:
    int hintBaseline() const;
    gfx::Rect box() const;
    const Theme& t_;
    bool visible_ = false, dirty_ = false;
    int cx_ = 0, cy_ = 0;
    int step_ = 0;       // progress drawn, in 64ths of a turn
    gfx::Rect drawn_{};  // what the last drawn ring covered
};

}  // namespace scr
