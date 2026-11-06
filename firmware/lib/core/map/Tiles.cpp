#include "Tiles.h"

#include <cmath>

#include "MapData.h"

namespace basemap {

namespace {

uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
bool bit(const uint8_t* bits, int i) { return bits[i >> 3] >> (i & 7) & 1; }

class FlashTiles : public TileSource {
   public:
    TileRef tile(int row, int col) const override {
        TileRef t;
        if (row < 0 || row >= kRows || col < 0 || col >= kCols) return t;
        const int i = row * kCols + col;
        if (bit(gen::kFullLand, i)) return t.kind = TileRef::Land, t;
        if (!bit(gen::kHasRings, i)) return t;
        // The tile's rank among ring tiles: the row's start plus the ring tiles before it in the row.
        int k = gen::kRowRank[row];
        for (int j = row * kCols; j < i; ++j) k += bit(gen::kHasRings, j);
        t.kind = TileRef::Rings;
        t.data = gen::kRingData + gen::kTileOffset[k];
        t.size = gen::kTileOffset[k + 1] - gen::kTileOffset[k];
        return t;
    }
};

const FlashTiles kFlashTiles;  // namespace scope: no function-local static guard on every call

}  // namespace

RingReader::RingReader(const uint8_t* p, const uint8_t* end) : p_(p), end_(end) {
    if (end_ - p_ < 6) {
        ok_ = false;
        return;
    }
    uint16_t head = rd16(p_);
    count_ = left_ = head & 0x7FFF, closingCut_ = head >> 15;
    x_ = x0_ = rd16(p_ + 2), y_ = y0_ = rd16(p_ + 4);
    p_ += 6;
}

bool RingReader::next(RingPoint& o) {
    if (!ok_ || count_ == 0) return false;
    if (!started_) {
        started_ = true, --left_;
        o = {x0_, y0_, false};
        return true;
    }
    if (left_ > 0) {
        bool cut = p_ < end_ && *p_ == 0x81;
        if (cut) ++p_;
        if (p_ < end_ && *p_ == 0x80) {
            if (end_ - p_ < 5) return ok_ = false;
            x_ += int16_t(rd16(p_ + 1)), y_ += int16_t(rd16(p_ + 3)), p_ += 5;
        } else if (p_ < end_ && *p_ == 0x82) {
            if (end_ - p_ < 4) return ok_ = false;
            const uint32_t v = uint32_t(p_[1]) | uint32_t(p_[2]) << 8 | uint32_t(p_[3]) << 16;
            x_ += int32_t((v & 0xFFF) ^ 0x800) - 0x800, y_ += int32_t((v >> 12) ^ 0x800) - 0x800, p_ += 4;
        } else {
            if (end_ - p_ < 2) return ok_ = false;
            x_ += int8_t(p_[0]), y_ += int8_t(p_[1]), p_ += 2;
        }
        --left_;
        o = {x_, y_, cut};
        return true;
    }
    if (closed_) return false;
    closed_ = true;
    o = {x0_, y0_, closingCut_};
    return true;
}

const TileSource& builtinTiles() { return kFlashTiles; }

// NaN (a missing position) maps to tile 0 on either axis: converting it to int is undefined.
int tileRow(double lat) {
    if (std::isnan(lat)) return 0;
    int r = int(std::floor((lat + 90) / kTileDeg));
    return r < 0 ? 0 : r >= kRows ? kRows - 1 : r;
}

int tileCol(double lon) {
    if (std::isnan(lon)) return 0;
    int c = int(std::floor((lon + 180) / kTileDeg)) % kCols;
    return c < 0 ? c + kCols : c;
}

}  // namespace basemap
