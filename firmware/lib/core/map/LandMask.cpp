#include "LandMask.h"

#ifdef CONTRAIL_ESP32
#pragma GCC optimize("O2")  // the toggles and the parity pass are tight loops
#endif

#include <cmath>
#include <cstring>
#include <utility>

namespace basemap {

namespace {
constexpr double kPiD = 3.14159265358979323846;
constexpr int kFrac = 16;
constexpr int64_t kOne = int64_t(1) << kFrac, kHalf = kOne / 2;

int64_t fixed(double v) { return int64_t(std::llround(v * double(kOne))); }

// Every point of the ring decodes and the data holds the whole ring: only then may its edges
// touch the mask, or an open polygon would flip the parity of everything right of it.
bool ringIntact(const uint8_t* p, const uint8_t* end) {
    RingReader rr(p, end);
    RingPoint pt;
    while (rr.next(pt)) {
    }
    return rr.ok();
}
}  // namespace

MapView viewFor(double lat, double lon, float radiusNm) {
    MapView v;
    v.lat = lat, v.lon = lon, v.cosLat = std::cos(lat * kPiD / 180);
    v.pxPerNm = 120.f / (radiusNm > 1 ? radiusNm : 1);
    return v;
}

gfx::Vec2 MapView::project(double plat, double plon) const {
    double dlon = plon - lon;
    if (dlon > 180) dlon -= 360;
    else if (dlon < -180) dlon += 360;
    const double k = 60.0 * pxPerNm;  // a degree of latitude is 60 nm
    return {float(cx + dlon * k * cosLat), float(cy - (plat - lat) * k)};
}

int LandMask::build(const MapView& v, const TileSource& src) {
    std::memset(bits_, 0, kBytes);
    const double k = 60.0 * v.pxPerNm, kx = k * v.cosLat;  // screen pixels per degree
    // The view's extent, a mask pixel wider all round.
    const double latTop = v.lat + (v.cy + 2) / k, latBot = v.lat - (v.h - v.cy + 2) / k;
    const double lonW = v.lon - (v.cx + 2) / kx, lonE = v.lon + (v.w - v.cx + 2) / kx;
    const int r0 = tileRow(latBot), r1 = tileRow(latTop);
    const int c0 = int(std::floor((lonW + 180) / kTileDeg)), c1 = int(std::floor((lonE + 180) / kTileDeg));
    // One affine for the whole view, anchored at tile (r0, c0); each tile offsets it by a whole number
    // of tile units. Two tiles that share a border then compute it from the same integer, bit for
    // bit: rounding a separate affine per tile could leave a gap between them that a pixel or row
    // centre falls into, a one-pixel water hairline along the seam.
    Affine g;
    {
        const double lat0 = -90.0 + r0 * kTileDeg, lon0 = -180.0 + c0 * kTileDeg;
        g.sx = fixed(kx * kTileDeg / kUnits / 2), g.ox = fixed((v.cx + (lon0 - v.lon) * kx) / 2);
        g.sy = fixed(-k * kTileDeg / kUnits / 2), g.oy = fixed((v.cy - (lat0 - v.lat) * k) / 2);
    }
    int landTiles = 0;
    for (int row = r0; row <= r1; ++row)
        for (int cu = c0; cu <= c1; ++cu) {  // unwrapped column: the projection stays continuous across 180°
            TileRef t = src.tile(row, ((cu % kCols) + kCols) % kCols);
            if (t.kind == TileRef::Water) continue;
            ++landTiles;
            Affine a = g;
            a.ox = g.ox + g.sx * (int64_t(cu - c0) * kUnits), a.oy = g.oy + g.sy * (int64_t(row - r0) * kUnits);
            if (t.kind == TileRef::Land) {
                const int64_t xl = a.x(0), xr = a.x(kUnits), yb = a.y(0), yt = a.y(kUnits);
                edge(xr, yb, xr, yt), edge(xl, yt, xl, yb);  // the horizontal sides toggle nothing
                continue;
            }
            const int n = t.data[0] | t.data[1] << 8;
            const uint8_t* p = t.data + 2;
            const uint8_t* end = t.data + t.size;
            for (int i = 0; i < n && p < end; ++i) {
                if (!ringIntact(p, end)) break;  // truncated: nothing after it can be found either
                RingReader rr(p, end);
                RingPoint pt;
                if (rr.next(pt)) {  // a ring with no points has no edges, but the rings after it still count
                    int64_t px = a.x(pt.x), py = a.y(pt.y);
                    while (rr.next(pt)) {
                        const int64_t qx = a.x(pt.x), qy = a.y(pt.y);
                        edge(px, py, qx, qy);
                        px = qx, py = qy;
                    }
                }
                p = rr.end();
            }
        }
    fillParity();
    return landTiles;
}

void LandMask::edge(int64_t x0, int64_t y0, int64_t x1, int64_t y1) {
    if (y0 == y1) return;
    // Wholly right of the mask (every crossing at or past the last pixel centre): toggles nothing.
    constexpr int64_t kRight = int64_t(kW) * kOne - kHalf;
    if (x0 >= kRight && x1 >= kRight) return;
    if (y0 > y1) std::swap(x0, x1), std::swap(y0, y1);
    // Rows whose centre (r + 0.5) lies in [y0, y1): half-open, so a vertex shared by two edges
    // toggles once. ceil(y - 0.5) in fixed point; >> on a negative value floors.
    int64_t r0 = (y0 - kHalf + kOne - 1) >> kFrac, r1 = ((y1 - kHalf + kOne - 1) >> kFrac) - 1;
    if (r0 < 0) r0 = 0;
    if (r1 > kH - 1) r1 = kH - 1;
    if (r0 > r1) return;
    // x at each row centre, stepped by the edge's slope (16.16 pixels per row).
    const int64_t slope = ((x1 - x0) << kFrac) / (y1 - y0);
    int64_t xc = x0 + ((((r0 << kFrac) + kHalf - y0) * slope) >> kFrac);
    for (int r = int(r0); r <= int(r1); ++r, xc += slope) {
        // The first pixel whose centre is right of the crossing: floor(xc - 0.5) + 1.
        int64_t c = ((xc - kHalf) >> kFrac) + 1;
        if (c >= kW) continue;
        if (c < 0) c = 0;
        bits_[r * kStride + (int(c) >> 3)] ^= uint8_t(0x80 >> (c & 7));
    }
}

void LandMask::fillParity() {
    for (int r = 0; r < kH; ++r) {
        uint8_t parity = 0;
        for (int i = 0; i < kStride; ++i) {
            // Prefix XOR within the byte from its first pixel (the most significant bit) onward.
            uint8_t v = bits_[r * kStride + i];
            v ^= v >> 1, v ^= v >> 2, v ^= v >> 4;
            if (parity) v = uint8_t(~v);
            parity = v & 1;
            bits_[r * kStride + i] = v;
        }
    }
}

bool LandMask::landAtScreen(int x, int y) const {
    int mx = x >> 1, my = y >> 1;
    return mx >= 0 && mx < kW && my >= 0 && my < kH && land(mx, my);
}

int LandMask::coverage16(int x, int y) const {
    const int i0 = (x - 1) >> 1, j0 = (y - 1) >> 1;  // arithmetic shift: x = 0 gives -1, clamped below
    const int wx = (x & 1) ? 3 : 1, wy = (y & 1) ? 3 : 1;
    auto at = [&](int i, int j) {
        i = i < 0 ? 0 : i >= kW ? kW - 1 : i;
        j = j < 0 ? 0 : j >= kH ? kH - 1 : j;
        return int(land(i, j));
    };
    return wy * (wx * at(i0, j0) + (4 - wx) * at(i0 + 1, j0)) +
           (4 - wy) * (wx * at(i0, j0 + 1) + (4 - wx) * at(i0 + 1, j0 + 1));
}

}  // namespace basemap
