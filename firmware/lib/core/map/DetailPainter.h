#pragma once
// Paints the detailed chart from a map pack (5b spec §4), 8 rows at a time, into the strip buffer: water,
// land (an even-odd XOR fill of the rings at 2 x 2 sub-pixels, so lakes cut holes and islands fill again),
// coastline (ring edges that are not tile cuts), rivers, roads and runways. Names are placed once per rebuild
// by layout() into DetailMarks, which the map screen draws and plane labels keep clear of.
//
// Memory: kScratchBytes of caller scratch (the screen arena) for the sub-pixel rows and layout()'s rank mask, and the
// marks. Tiles are read through ByteStream; nothing is allocated.
#include <cstdint>

#include "Features.h"
#include "LandMask.h"  // MapView
#include "MapPack.h"
#include "gfx/Canvas.h"
#include "screens/Theme.h"

namespace basemap {

struct DetailMark {
    int16_t x = 0, y = 0;                    // the dot
    int16_t lx = 0, ly = 0;                  // label anchor (left of the baseline)
    int16_t bx = 0, by = 0, bw = 0, bh = 0;  // label box
    uint8_t kind = 0, rank = 0;              // kTown or kAirport; the feature's rank
    bool labelled = false;
    char name[21] = "";
    gfx::Rect box() const { return {bx, by, bw, bh}; }
};

class DetailPainter {
   public:
    static constexpr int kRows = 8;                             // a half-strip
    static constexpr int kSubW = 480, kSubRows = 2 * kRows;     // 2 x 2 sub-pixels
    static constexpr int kMaxMarks = 32;
    static constexpr int kMaxTiles = 64;  // tiles of a view whose damage is remembered; beyond that, painted unchecked
    static constexpr int kSubBytes = kSubW / 8 * kSubRows;           // 960
    static constexpr int kScratchBytes = kSubBytes + 2 * kMaxTiles;  // 1,088: and a 16-bit rank mask per tile

    explicit DetailPainter(const scr::Theme& t) : t_(t) {}
    void setPack(MapPack* p) { pack_ = p; }
    // A new chart for `v` at `radiusNm`; scratch is kScratchBytes, marks kMaxMarks.
    void begin(const MapView& v, float radiusNm, uint8_t* scratch, DetailMark* marks);
    // Checks the view's tiles and reads their towns and airports, placing the names; returns the marks filled, in
    // the order placed: airports by rank, then towns by rank. A point whose name fits nowhere gets no mark, as it
    // gets no dot. Call it before paint(). `w` is the chart width; names keep clear of the nKeep rects in keep.
    int layout(int w, const gfx::Rect* keep, int nKeep);
    // Paints rows [y0, y0 + rows) of the chart into c, which covers at least those rows. At most kRows: more are
    // not painted.
    void paint(gfx::Canvas& c, int y0, int rows);
    // The first part of paint(): water and land, without the lines.
    void paintFill(gfx::Canvas& c, int y0, int rows);
    // A read failed while painting or laying out: the caller drops to the built-in map.
    bool failed() const { return failed_; }
    int level() const { return level_; }

   private:
    // Tile units to screen pixels in 16.16 fixed point. There is one affine per chart, anchored at home's tile; a
    // tile offsets it by a whole number of tile units. So x(2048) of one tile and x(0) of the next are the same
    // integer, and the border edges both toggle cancel exactly, row for row (as LandMask does it). Units are
    // clamped to a tile's width either side of the tile: real data stays inside the tile, and a ring remains closed
    // when clamped, so card data out of range can neither overflow anything nor cost more than a nearby ring.
    struct Tf {
        int64_t sx, ox, sy, oy;
        static int32_t unit(int32_t u) { return u < -kUnits ? -kUnits : u > 2 * kUnits ? 2 * kUnits : u; }
        int64_t x(int32_t u) const { return ox + sx * unit(u); }
        int64_t y(int32_t u) const { return oy + sy * unit(u); }
    };
    Tf tf(int row, int col) const;
    // The level's tiles over screen rows [yTop, yBottom], and a little past the chart's sides.
    void tileRange(int& r0, int& r1, int& c0, int& c1, float yTop, float yBottom) const;
    // The tile's header without its CRC; false when empty, unreadable or found bad by layout().
    bool tileFor(int row, int col, TileInfo& t);
    void verifyView();
    // Which airport and town ranks a tile of the view holds. Bytes in the scratch (it need not be aligned).
    static uint16_t rankBit(uint8_t kind, uint8_t rank);
    uint16_t tileRanks(int row, int col) const;
    void setTileRanks(int i, uint16_t bits);
    bool isBad(int row, int col) const;
    void fillLand(gfx::Canvas& c, int y0, int rows);
    void edge(int64_t x0, int64_t y0, int64_t x1, int64_t y1, int sy0);
    void strokeLines(gfx::Canvas& c, int y0, int rows);
    void stroke(gfx::Canvas& c, int64_t x0, int64_t y0, int64_t x1, int64_t y1, float w, gfx::Rgb col, uint8_t a);
    void lineFeature(gfx::Canvas& c, const FeatureEntry& e, const Tf& m, gfx::Rgb col, float w);
    void coast(gfx::Canvas& c, const FeatureEntry& e, const Tf& m);
    void placePoints(uint8_t kind, uint8_t rank, int w, const gfx::Rect* keep, int nKeep, int cap);
    bool place(DetailMark& k, int w, const gfx::Rect* keep, int nKeep);

    MapPack* pack_ = nullptr;
    const scr::Theme& t_;
    uint16_t viewRanks_ = 0;  // every rank any tile of the view holds: passes for the rest are skipped whole
    uint64_t bad_ = 0;  // tiles layout() found damaged, one bit per tile of the view (at most kMaxTiles)
    int vr0_ = 0, vc0_ = 0, vcols_ = 1, vrows_ = 0;
    int anchorRow_ = 0, anchorCol_ = 0;
    Tf anchor_{};
    MapView v_;
    int level_ = 0;
    uint8_t* sub_ = nullptr;
    DetailMark* marks_ = nullptr;
    int nMarks_ = 0;
    bool failed_ = false;
};

}  // namespace basemap
