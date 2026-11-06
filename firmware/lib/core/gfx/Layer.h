#pragma once
#include "Canvas.h"

namespace gfx {

// Rects that changed since the last frame, in global coordinates.
class DamageList {
   public:
    static constexpr int kMax = 24;  // the radar sweep alone takes up to 9 (16-row bands)
    // Overlapping rects are united only when that wastes no area; otherwise they are kept
    // apart (the renderer draws each strip's damaged column ranges separately). When the list
    // is full, a rect joins the entry it wastes least area with.
    void add(const Rect& r);
    // Adds n rects (reordering r), first merging the pairs that waste the least area until at
    // most `budget` remain, so a busy layer leaves room in the list for the others.
    void addMerged(Rect* r, int n, int budget);
    void addAll() { all_ = true; }
    bool any() const { return all_ || n_ > 0; }
    bool all() const { return all_; }
    bool touchesBand(int y, int h) const;
    // The columns of the damage that touches rows [y, y + h), clamped to [0, width), as up to
    // maxSpans disjoint ranges [x0, x1), sorted: ranges closer than minGap are joined (a
    // separate push has its own overhead), then the closest pairs until maxSpans remain.
    // When y0/y1 are given, each range's rows go there too: [y0, y1), the band's rows that
    // the damage in it covers. Returns the count.
    int spansInBand(int y, int h, int width, int minGap, int maxSpans, int* x0, int* x1, int* y0 = nullptr,
                    int* y1 = nullptr) const;
    void clear() { n_ = 0, all_ = false; }

   private:
    Rect rects_[kMax];
    int n_ = 0;
    bool all_ = false;
};

class Layer {
   public:
    virtual ~Layer() = default;
    // Advance animation by dt seconds and report any area whose pixels will change.
    virtual void update(float dt, DamageList& damage) = 0;
    // Draw everything that intersects the canvas band. Called once per damaged strip.
    virtual void draw(Canvas& c) = 0;
};

}  // namespace gfx
