#include "ChartCache.h"

#include <algorithm>
#include <cstring>

#include "assets/Pack.h"  // crc16

namespace basemap {
namespace {
constexpr uint8_t kMagic[4] = {'C', 'H', 'R', '1'};
}

void ChartCache::encode(const ChartKey& k, uint8_t out[32]) {
    std::memset(out, 0, 32);
    std::memcpy(out, kMagic, 4);
    auto put32 = [&](int at, uint32_t v) {
        for (int i = 0; i < 4; ++i) out[at + i] = uint8_t(v >> 8 * i);
    };
    put32(4, uint32_t(k.latE5)), put32(8, uint32_t(k.lonE5));
    out[12] = uint8_t(k.radiusNm10), out[13] = uint8_t(k.radiusNm10 >> 8);
    out[14] = uint8_t(k.theme), out[15] = uint8_t(k.theme >> 8);
    out[16] = k.level;
    out[18] = uint8_t(k.format), out[19] = uint8_t(k.format >> 8);
    put32(20, k.packId);
    const uint16_t crc = assets::crc16(out, 30);
    out[30] = uint8_t(crc), out[31] = uint8_t(crc >> 8);
}

bool ChartCache::matches(const ChartKey& k) {
    if (size_ < kBytes) return false;
    uint8_t want[32], got[32];
    encode(k, want);
    return f_.readAt(keyOffset(), got, sizeof got) && std::memcmp(want, got, sizeof got) == 0;
}

bool ChartCache::begin(const ChartKey&) {
    const uint8_t zero[32] = {};
    return f_.writeAt(keyOffset(), zero, sizeof zero);
}

bool ChartCache::finish(const ChartKey& k) {
    uint8_t b[32];
    encode(k, b);
    return f_.writeAt(keyOffset(), b, sizeof b);
}

void ChartCache::transpose(uint16_t* p, int rows, int cols) {
    const int n = rows * cols;
    uint8_t done[(16 * 15 + 7) / 8] = {};
    if (n > 16 * 15) return;
    for (int s = 0; s < n; ++s) {
        if (done[s >> 3] >> (s & 7) & 1) continue;
        // Follow the cycle through s: chunk d of the result is chunk (d % rows) * cols + d / rows of the input.
        uint16_t first[kTile];
        std::memcpy(first, p + s * kTile, sizeof first);
        for (int d = s;;) {
            const int from = (d % rows) * cols + d / rows;
            done[d >> 3] |= uint8_t(1 << (d & 7));
            if (from == s) {
                std::memcpy(p + d * kTile, first, sizeof first);
                break;
            }
            std::memcpy(p + d * kTile, p + from * kTile, kTile * 2);
            d = from;
        }
    }
}

bool ChartCache::writeBand(int by, uint16_t* px) {
    if (by < 0 || by >= kTilesY) return false;
    transpose(px, kTile, kTilesX);
    const bool ok = f_.writeAt(tileOffset(0, by), px, kTilesX * kTileBytes);
    transpose(px, kTilesX, kTile);
    return ok;
}

bool ChartCache::readRect(int x, int y, int w, int h, uint16_t* out, int stride) {
    if (x < 0 || y < 0 || w < 1 || h < 1 || x + w > kW || y + h > kTilesY * kTile || stride < w) return false;
    if (x % kTile == 0 && w % kTile == 0 && y % kTile == 0 && h == kTile && stride == w) {
        const int n = w / kTile;
        if (!f_.readAt(tileOffset(x / kTile, y / kTile), out, uint32_t(n) * kTileBytes)) return false;
        transpose(out, n, kTile);
        return true;
    }
    uint16_t t[kTile * kTile];
    for (int ty = y / kTile; ty <= (y + h - 1) / kTile; ++ty) {
        const int r0 = std::max(y, ty * kTile), r1 = std::min(y + h, ty * kTile + kTile);
        for (int tx = x / kTile; tx <= (x + w - 1) / kTile; ++tx) {
            if (!f_.readAt(tileOffset(tx, ty), t, kTileBytes)) return false;
            const int c0 = std::max(x, tx * kTile), c1 = std::min(x + w, tx * kTile + kTile);
            for (int r = r0; r < r1; ++r)
                std::memcpy(out + (r - y) * stride + (c0 - x), t + (r % kTile) * kTile + c0 % kTile, size_t(c1 - c0) * 2);
        }
    }
    return true;
}

bool ChartCache::writeExtra(uint32_t at, const void* p, uint32_t n) {
    return at <= kExtraBytes && n <= kExtraBytes - at && f_.writeAt(extraOffset() + at, p, n);
}

bool ChartCache::readExtra(uint32_t at, void* p, uint32_t n) {
    return at <= kExtraBytes && n <= kExtraBytes - at && f_.readAt(extraOffset() + at, p, n);
}

}  // namespace basemap
