#pragma once
// Airports for the Live Map (spec §3.1): the ones inside the view, large first then nearest home,
// and their ident labels, placed largest first and skipped where they would overlap one already
// placed.
#include <cstdint>

#include "LandMask.h"
#include "gfx/Font.h"

namespace basemap {

struct AirportMark {
    int16_t x = 0, y = 0;  // screen position
    char ident[5] = "";
    bool large = false, labelled = false, alignRight = false;
    int16_t lx = 0, ly = 0;                  // label anchor (baseline)
    int16_t bx = 0, by = 0, bw = 0, bh = 0;  // label box
    uint16_t index = 0;                      // into gen::kAirports (runways, exact position)
    gfx::Rect box() const { return {bx, by, bw, bh}; }
};

// Fills `out` with up to `max` airports inside the view's map area, large first, then by distance
// from home; returns the count.
int airportsInView(const MapView& v, AirportMark* out, int max);
// Labels in the order given (largest first); `w` is the map width, for the right-edge rule. A label
// that would touch one of the `nKeep` rects in `keep` (home, the chart's own text) moves out along
// its side by up to kKeepShift px to clear it, or is left off.
constexpr int kKeepShift = 12;
void placeLabels(AirportMark* marks, int n, const gfx::FontData& f, int w, const gfx::Rect* keep = nullptr,
                 int nKeep = 0);

}  // namespace basemap
