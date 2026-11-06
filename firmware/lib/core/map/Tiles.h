#pragma once
// The Live Map's built-in base map (spec §2): Natural Earth 1:50m land and lakes cut into 2°×2°
// tiles, generated into flash by tools/gen_map.py (MapData.cpp). This is the format and its reader;
// LandMask rasterises it.
//
// Tile (row, col) covers latitudes [-90 + 2 row, -88 + 2 row) and longitudes [-180 + 2 col,
// -178 + 2 col). Points are tile-local, 0..kUnits on each axis (~108 m of latitude a unit), x east
// and y north. A tile is water (no data), all land (its bit in the full-land table) or a list of
// closed rings filled even-odd: land and lake rings alike, so lakes cut holes and islands in lakes
// fill again. Tiles entirely south of 60° S (Antarctica, rows below kFirstDataRow) are left out of
// the tables and so read as water.
//
// Tile data: uint16 ring count, then the rings. A ring, little-endian:
//   uint16 count | closingCut << 15   points; bit 15: the edge from the last point back to the first
//                                       is a tile cut
//   uint16 x0, uint16 y0              the first point
//   count - 1 steps, each one of
//     int8 dx, int8 dy                dx in -125..127 (0x80..0x82 never start a short step)
//     0x82, 3 bytes                   a medium step: dx and dy as int12, packed into a little-endian
//                                       24-bit value, dx in the low 12 bits
//     0x80, int16 dx, int16 dy        a long step
//     0x81, then one of the above     the edge into this point is a tile cut
// A tile cut is an edge the clipper made along the tile border: filled, never stroked. The mask
// never shows them (land lies on both sides); they are kept for sub-project 5's vector strokes.
#include <cstdint>

namespace basemap {

constexpr int kTileDeg = 2, kRows = 90, kCols = 180, kUnits = 2048;
constexpr int kFirstDataRow = 15;  // rows 0..14 (south of 60° S) hold no data

struct TileRef {
    enum Kind : uint8_t { Water, Land, Rings };
    Kind kind = Water;
    const uint8_t* data = nullptr;  // Rings: uint16 ring count, then the rings
    uint32_t size = 0;
};

struct RingPoint {
    int32_t x, y;
    bool cut;  // the edge from the previous point to this one is a tile cut
};

// Walks one ring. After the last point it reports the first again (the closing edge, with its cut
// flag), then returns false; end() is then where the next ring starts.
class RingReader {
   public:
    RingReader(const uint8_t* p, const uint8_t* end);
    bool next(RingPoint& out);
    const uint8_t* end() const { return p_; }
    int count() const { return count_; }
    bool ok() const { return ok_; }  // false if the data ran out mid-ring

   private:
    const uint8_t* p_;
    const uint8_t* end_;
    int count_ = 0, left_ = 0;
    int32_t x0_ = 0, y0_ = 0, x_ = 0, y_ = 0;
    bool closingCut_ = false, started_ = false, closed_ = false, ok_ = true;
};

// Where tiles come from: the flash tables, or a test's own.
class TileSource {
   public:
    virtual ~TileSource() = default;
    virtual TileRef tile(int row, int col) const = 0;  // Water for rows or columns out of range
};
const TileSource& builtinTiles();

int tileRow(double lat);  // clamped to 0..kRows-1
int tileCol(double lon);  // wrapped into 0..kCols-1

}  // namespace basemap
