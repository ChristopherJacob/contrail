#pragma once
// Tables written by tools/gen_map.py into MapData.cpp. Read them through Tiles.h and Airports.h.
#include <cstdint>

#include "Tiles.h"

namespace basemap {
namespace gen {

constexpr int kBitBytes = (kRows * kCols + 7) / 8;  // one bit per tile, row-major, least significant bit first
extern const uint8_t kFullLand[kBitBytes];
extern const uint8_t kHasRings[kBitBytes];
extern const uint16_t kRowRank[kRows + 1];  // ring tiles before each row; [kRows] = all of them
extern const uint32_t kTileOffset[];        // per ring tile, into kRingData, plus the end
extern const uint8_t kRingData[];
extern const uint32_t kRingDataSize;

// OurAirports large and medium airports, sorted by latitude, as packed 10-byte records (about 51 KB
// for ~5,100 airports instead of 81 KB as plain structs). Little-endian bits:
//   0-23   latitude, (lat + 90) / 180 of 2^24      (~1.1 m)
//   24-47  longitude, (lon + 180) / 360 of 2^24    (~2.4 m at the equator)
//   48-68  ICAO code, base 37 from the first character: 0 pads a 3-character code, 1-10 are '0'-'9',
//          11-36 are 'A'-'Z'
//   69-70  size class: 1 large, 0 medium
//   71-75  runway count (large airports only, at most 31)
struct Airport {
    uint8_t b[10];

    int32_t latE5() const;  // degrees × 1e5
    int32_t lonE5() const;
    void ident(char out[5]) const;  // NUL-terminated
    bool large() const { return (b[8] >> 5 & 3) == 1; }
    uint8_t runways() const { return uint8_t((b[8] >> 7 | b[9] << 1) & 31); }
    uint16_t firstRunway() const;  // into kRunways
};
struct Runway {
    int16_t dLat1, dLon1, dLat2, dLon2;  // the two ends, 1e-5 degrees from the airport's packed position
};
extern const Airport kAirports[];
extern const int kAirportCount;
constexpr int kRunwayBlock = 32;
extern const uint16_t kRunwayStart[];  // runways before airport kRunwayBlock × i
extern const Runway kRunways[];
extern const int kRunwayCount;

inline int32_t Airport::latE5() const {
    const uint32_t v = uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16;
    return int32_t((int64_t(v) * 18000000 + (1 << 23)) >> 24) - 9000000;
}

inline int32_t Airport::lonE5() const {
    const uint32_t v = uint32_t(b[3]) | uint32_t(b[4]) << 8 | uint32_t(b[5]) << 16;
    return int32_t((int64_t(v) * 36000000 + (1 << 23)) >> 24) - 18000000;
}

inline void Airport::ident(char out[5]) const {
    static constexpr char kChars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    uint32_t v = (uint32_t(b[6]) | uint32_t(b[7]) << 8 | uint32_t(b[8]) << 16) & 0x1FFFFF;
    int n = 0;
    for (; v && n < 4; v /= 37)
        if (v % 37) out[n++] = kChars[v % 37 - 1];
    out[n] = 0;
}

inline uint16_t Airport::firstRunway() const {
    const int i = int(this - kAirports), block = i / kRunwayBlock;
    int n = kRunwayStart[block];
    for (int j = block * kRunwayBlock; j < i; ++j) n += kAirports[j].runways();
    return uint16_t(n);
}

}  // namespace gen
}  // namespace basemap
