#include "MapTrails.h"

#include <cmath>

namespace scr {

void MapTrails::Track::at(int i, float& e, float& n) const {
    int k = (next - count + i + 2 * kPoints) % kPoints;
    e = east[k] / 16.f, n = north[k] / 16.f;
}

void MapTrails::clear() {
    for (auto& t : tracks_) t = Track{};
    last_ = INT64_MIN;
}

const MapTrails::Track* MapTrails::find(uint32_t hex) const {
    if (!hex) return nullptr;
    for (const auto& t : tracks_)
        if (t.hex == hex) return &t;
    return nullptr;
}

void MapTrails::record(const Scene& s) {
    if (!s.timeValid) return;
    const bool moved = s.homeLat != homeLat_ || s.homeLon != homeLon_;
    if (last_ != INT64_MIN && (s.epoch < last_ || moved)) clear();
    homeLat_ = s.homeLat, homeLon_ = s.homeLon;
    if (s.epoch == last_) return;  // rebuilds within a second: nothing falls due
    const int64_t was = last_;
    last_ = s.epoch;
    // Whether the plane's phase came round since the last call (everyone's, on the first).
    auto due = [&](uint32_t hex) {
        if (was == INT64_MIN) return true;
        const int64_t ph = phaseOf(hex);
        auto period = [&](int64_t e) { return e - ph >= 0 ? (e - ph) / kEveryS : -((ph - e + kEveryS - 1) / kEveryS); };
        return period(s.epoch) != period(was);
    };
    constexpr int kN = Scene::kMaxFlights;
    for (Track& t : tracks_) {
        if (!t.hex || !due(t.hex)) continue;
        bool there = false;
        for (int i = 0; i < s.count && !there; ++i) there = s.flights[i].hex == t.hex;
        if (!there) t = Track{};
    }
    for (int i = 0; i < s.count; ++i) {
        const SceneFlight& f = s.flights[i];
        if (!f.hex || !due(f.hex)) continue;
        int k = 0;
        while (k < kN && tracks_[k].hex != f.hex) ++k;
        if (k == kN)
            for (k = 0; k < kN && tracks_[k].hex; ++k) {
            }
        // None free: a plane that left keeps its slot until its own phase comes round; an arrival due now takes it.
        for (int j = 0; k == kN && j < kN; ++j) {
            bool there = false;
            for (int m = 0; m < s.count && !there; ++m) there = s.flights[m].hex == tracks_[j].hex;
            if (!there) tracks_[j] = Track{}, k = j;
        }
        if (k >= kN) continue;
        Track& t = tracks_[k];
        t.hex = f.hex;
        float a = f.bearingDeg * 3.14159265f / 180.f;
        t.east[t.next] = int16_t(std::lround(f.distNm * std::sin(a) * 16));
        t.north[t.next] = int16_t(std::lround(f.distNm * std::cos(a) * 16));
        t.next = uint8_t((t.next + 1) % kPoints);
        if (t.count < kPoints) ++t.count;
    }
}

}  // namespace scr
