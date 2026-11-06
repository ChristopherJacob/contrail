#pragma once
// Where each plane in the scene has been: one position every 10 s, the last 8 (spec §3.1). Each plane
// takes its point in its own second of the 10 (a phase from its code), so the map redraws about a
// tenth of the trails each second, not all of them at once. Fed from
// every scene rebuild whatever screen is up (not from the arena, decision 3), so the Live Map shows
// full trails the moment it opens. Positions are kept relative to home in 1/16 nm, so they survive
// a change of map scale.
#include <cstdint>

#include "Scene.h"

namespace scr {

class MapTrails {
   public:
    static constexpr int kPoints = 8;
    static constexpr int64_t kEveryS = 10;
    struct Track {
        uint32_t hex = 0;
        int16_t east[kPoints] = {}, north[kPoints] = {};  // 1/16 nm from home
        uint8_t count = 0, next = 0;                         // points kept; the slot the next one goes in
        // Point i, oldest first (0 <= i < count), in nm east and north of home.
        void at(int i, float& eastNm, float& northNm) const;
    };
    // Call on every rebuild: samples each flight once per 10 s of scene time, when its phase comes
    // round (every flight on the first call). A plane no longer in the scene loses its trail when
    // its phase comes round, or sooner when an arrival needs its slot; the clock going back or home
    // moving clears everything (positions are relative to home).
    void record(const Scene& s);
    // The second within each 10 in which the plane with this code takes its point.
    static int phaseOf(uint32_t hex) { return int((hex * 2654435761u) >> 16) % int(kEveryS); }
    const Track* find(uint32_t hex) const;
    void clear();

   private:
    Track tracks_[Scene::kMaxFlights];
    int64_t last_ = INT64_MIN;  // the epoch of the last record()
    double homeLat_ = 0, homeLon_ = 0;
};

}  // namespace scr
