#pragma once
// The finished detailed chart, saved in map.pak's reserved area (5b spec §4) as 16 x 16-pixel tiles of RGB565, one
// 512-byte sector each, row by row inside the tile: 15 across, 17 down (the last tile row's bottom two pixel rows are
// padding), band by band from the area's start; then 32 B of key, then room for the label marks. Normal frames read
// the chart back from here instead of repainting it, and a damaged box costs a sector per tile it touches, whatever
// its height (rows of 480 B cost a sector per row). The renderer's strips are 16-row bands on a fixed grid, so a band
// is 15 consecutive sectors: one multi-block write, and one read for a full-width redraw. The key is cleared when a
// rebuild begins and written when it finishes, so a rebuild cut short never reads as valid.
#include <cstdint>

#include "hal.h"

namespace basemap {

// Bump whenever the chart's drawing or the layout of the saved marks changes, so an update can never show a
// chart saved by an older build (MapBase mixes in sizeof(DetailMark) too).
constexpr uint16_t kChartFormat = 2;  // 2: tiles of 16 x 16 pixels

struct ChartKey {
    int32_t latE5 = 0, lonE5 = 0;
    uint16_t radiusNm10 = 0;  // tenths of a nautical mile
    uint16_t theme = 0;       // a hash of the theme's map colours
    uint8_t level = 0;
    uint32_t packId = 0;
    uint16_t format = kChartFormat;
};

class ChartCache {
   public:
    static constexpr int kW = 240, kH = 270, kTile = 16, kTilesX = kW / kTile, kTilesY = (kH + kTile - 1) / kTile;
    static constexpr uint32_t kTileBytes = 512, kTilesBytes = kTileBytes * kTilesX * kTilesY;  // 255 tiles: 130,560
    static constexpr uint32_t kKeyBytes = 32, kExtraBytes = 1440;
    static constexpr uint32_t kKeyAt = kTilesBytes, kExtraAt = kKeyAt + kKeyBytes;  // from the area's start
    static constexpr uint32_t kBytes = kExtraAt + kExtraBytes;                     // 132,032
    static constexpr uint32_t kAreaBytes = 33 * 4096;  // 135,168: what the builder reserves (the pack's minimum)
    ChartCache(hal::IFile& f, uint32_t offset, uint32_t size) : f_(f), off_(offset), size_(size) {}
    bool matches(const ChartKey& k);
    bool begin(const ChartKey& k);   // clears the key
    bool finish(const ChartKey& k);  // writes the key
    // Band `by` (pixel rows 16 by .. 16 by + 15, the padding rows included): `px` holds its 16 rows of 240 pixels,
    // contiguous. Rearranged into tile order in place for the one write, and back again before returning.
    bool writeBand(int by, uint16_t* px);
    // Pixels [x, x + w) x [y, y + h) into `out`, rows `stride` pixels apart; padding rows may be read. When the
    // rectangle is whole tiles of one band and `out` holds exactly them (stride == w), one read, rearranged in place;
    // otherwise a tile at a time through a 512-byte buffer on the stack.
    bool readRect(int x, int y, int w, int h, uint16_t* out, int stride);
    // The caller's bytes kept beside the chart (the label marks): offset within kExtraBytes.
    bool writeExtra(uint32_t at, const void* p, uint32_t n);
    bool readExtra(uint32_t at, void* p, uint32_t n);
    // `p` holds rows x cols chunks of 16 pixels in row order; afterwards in column order (each column's chunks
    // together). rows x cols is at most 16 x 15. Rows to tiles is (16, n); tiles to rows (n, 16).
    static void transpose(uint16_t* p, int rows, int cols);

   private:
    static void encode(const ChartKey& k, uint8_t out[32]);
    uint32_t tileOffset(int tx, int ty) const { return off_ + uint32_t(ty * kTilesX + tx) * kTileBytes; }
    uint32_t keyOffset() const { return off_ + kKeyAt; }
    uint32_t extraOffset() const { return off_ + kExtraAt; }
    hal::IFile& f_;
    uint32_t off_, size_;
};

}  // namespace basemap
