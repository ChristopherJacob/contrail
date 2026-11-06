#include "Airports.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "MapData.h"
#include "ui/Widgets.h"

namespace basemap {

int airportsInView(const MapView& v, AirportMark* out, int max) {
    const double k = 60.0 * v.pxPerNm, kx = k * v.cosLat;
    const int32_t latLo = int32_t(std::floor((v.lat - (v.h - v.cy) / k) * 1e5));
    const int32_t latHi = int32_t(std::ceil((v.lat + v.cy / k) * 1e5));
    // Longitudes as an integer window around home, so airports in the latitude band but far east or
    // west are skipped without projecting them (the ESP32 does doubles in software).
    const int32_t lonHome = int32_t(std::lround(v.lon * 1e5));
    const double halfW = (v.cx > v.w - v.cx ? v.cx : v.w - v.cx) / kx + 1e-3;
    const int32_t lonHalf = halfW >= 180 ? 18000000 : int32_t(std::ceil(halfW * 1e5));
    const gen::Airport* const all = gen::kAirports;
    const gen::Airport* const end = all + gen::kAirportCount;
    const gen::Airport* a =
        std::lower_bound(all, end, latLo, [](const gen::Airport& x, int32_t lat) { return x.latE5() < lat; });
    // Large first, then nearest the centre.
    auto rank = [&](const AirportMark& m) {
        float dx = m.x - v.cx, dy = m.y - v.cy;
        return (m.large ? 0.f : 1e7f) + dx * dx + dy * dy;
    };
    int n = 0;
    for (; a < end && a->latE5() <= latHi; ++a) {
        int32_t dlon = a->lonE5() - lonHome;
        if (dlon > 18000000) dlon -= 36000000;
        else if (dlon < -18000000) dlon += 36000000;
        if (dlon > lonHalf || dlon < -lonHalf) continue;
        gfx::Vec2 p = v.project(a->latE5() * 1e-5, a->lonE5() * 1e-5);
        if (p.x < 0 || p.x >= v.w || p.y < 0 || p.y >= v.h) continue;
        AirportMark m;
        m.x = int16_t(std::lround(p.x)), m.y = int16_t(std::lround(p.y));
        a->ident(m.ident);
        m.large = a->large();
        m.index = uint16_t(a - all);
        if (n < max) {
            out[n++] = m;
            continue;
        }
        int worst = 0;
        for (int i = 1; i < n; ++i)
            if (rank(out[i]) > rank(out[worst])) worst = i;
        if (n > 0 && rank(m) < rank(out[worst])) out[worst] = m;
    }
    // Ties broken by table index, so the order never depends on the sort's implementation.
    std::sort(out, out + n, [&](const AirportMark& x, const AirportMark& y) {
        const float rx = rank(x), ry = rank(y);
        return rx < ry || (rx == ry && x.index < y.index);
    });
    return n;
}

void placeLabels(AirportMark* m, int n, const gfx::FontData& f, int w, const gfx::Rect* keep, int nKeep) {
    for (int i = 0; i < n; ++i) {
        AirportMark& a = m[i];
        a.alignRight = a.x > w - 36;  // near the right edge the label goes on the left of the dot
        const gfx::Align al = a.alignRight ? gfx::Align::Right : gfx::Align::Left;
        int lx = a.x + (a.alignRight ? -5 : 5);
        const int ly = a.y + 4;
        gfx::Rect r = ui::textBox(f, lx, ly, a.ident, al);
        // Step out along the label's side past anything it must keep clear of. The text itself (the
        // box less its one-pixel margin) must not touch the keep rects, whose margins keep a gap.
        auto ink = [&] { return gfx::Rect{r.x + 1, r.y + 1, r.w - 3, r.h - 3}; };
        for (int k = 0; k < nKeep; ++k) {
            if (!ink().intersects(keep[k])) continue;
            const int dx = a.alignRight ? keep[k].x - ink().right() : keep[k].right() - ink().x;
            lx += dx, r.x += dx;
        }
        a.labelled = std::abs(lx - a.x) <= 5 + kKeepShift;
        for (int k = 0; k < nKeep && a.labelled; ++k)
            if (ink().intersects(keep[k])) a.labelled = false;
        for (int j = 0; j < i && a.labelled; ++j)
            if (m[j].labelled && r.intersects(m[j].box())) a.labelled = false;
        if (!a.labelled) r = {};
        a.lx = int16_t(lx), a.ly = int16_t(ly);
        a.bx = int16_t(r.x), a.by = int16_t(r.y), a.bw = int16_t(r.w), a.bh = int16_t(r.h);
    }
}

}  // namespace basemap
