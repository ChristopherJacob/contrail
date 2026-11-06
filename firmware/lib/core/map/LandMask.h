#pragma once
// Land and water around home as a 1-bit mask at half the screen's resolution (spec §2), rebuilt when
// the map opens or home or the radius changes. Every tile the view covers is rasterised with an XOR
// scanline fill: each ring edge toggles one bit per mask row it crosses, then a running parity
// along each row turns the toggles into even-odd fill. No edge list is kept, so the mask itself is
// the only memory it needs.
#include <cstdint>

#include "Tiles.h"
#include "gfx/Canvas.h"

namespace basemap {

// The map's projection: home at (cx, cy), equirectangular with longitudes scaled by the cosine of
// home's latitude, pxPerNm screen pixels per nautical mile.
struct MapView {
    double lat = 0, lon = 0, cosLat = 1;
    float pxPerNm = 4.8f;
    float cx = 120, cy = 135;
    int w = 240, h = 270;
    gfx::Vec2 project(double plat, double plon) const;
};
// The radius spans half the map's width (decision 7).
MapView viewFor(double lat, double lon, float radiusNm);

class LandMask {
   public:
    static constexpr int kW = 120, kH = 135, kStride = kW / 8, kBytes = kStride * kH;
    LandMask() = default;
    explicit LandMask(uint8_t* bits) : bits_(bits) {}  // kBytes of storage (the screen arena)
    uint8_t* bits() const { return bits_; }
    // Rasterises the tiles the view covers; returns how many held land.
    int build(const MapView& v, const TileSource& src = builtinTiles());
    bool land(int mx, int my) const { return bits_[my * kStride + (mx >> 3)] >> (7 - (mx & 7)) & 1; }
    bool landAtScreen(int x, int y) const;
    // Land coverage at screen pixel (x, y), 0 (water) .. 16 (land): the four nearest mask pixels,
    // weighted bilinearly. A screen pixel centre sits a quarter of the way between two mask pixel
    // centres, so the weights are always 3:1 or 1:3 on each axis.
    int coverage16(int x, int y) const;

   private:
    // Tile units to mask pixels in 16.16 fixed point, set once per tile: the ESP32 has no double
    // FPU, so nothing per vertex or per edge is floating point.
    struct Affine {
        int64_t sx, ox, sy, oy;
        int64_t x(int32_t tx) const { return ox + sx * tx; }
        int64_t y(int32_t ty) const { return oy + sy * ty; }
    };
    void edge(int64_t x0, int64_t y0, int64_t x1, int64_t y1);  // mask pixels, 16.16
    void fillParity();
    uint8_t* bits_ = nullptr;
};

}  // namespace basemap
