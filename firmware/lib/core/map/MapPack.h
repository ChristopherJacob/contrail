#pragma once
// /contrail/map.pak, the Live Map's detailed map for one home (5b spec §3): header, three levels of square
// tiles, each tile a CRC'd feature table and its data, and a reserved area at the end for the saved chart.
// Opened once at boot (the header and each level's index are checked then, ~20 KB of reads at most); tiles are
// checked as they are read. Render thread only, except the status getters, which the web task may read.
#include <atomic>
#include <cstdint>

#include "hal.h"

namespace basemap {

enum FeatureKind : uint8_t {
    kRing = 0, kMajorRoad = 1, kRoad = 2, kRiver = 3, kRunway = 4, kTown = 16, kAirport = 17
};

struct PackLevel {
    uint16_t maxRadiusNm = 0, tileMilliDeg = 0;
    int32_t originLatE5 = 0, originLonE5 = 0;
    uint16_t rows = 0, cols = 0;
    uint32_t indexOffset = 0;
    double tileDeg() const { return tileMilliDeg * 1e-3; }
    // The tile holding (lat, lon); may be outside 0..rows-1 / 0..cols-1.
    void cell(double lat, double lon, int& row, int& col) const;
};

struct TileInfo {
    uint32_t offset = 0;  // into the file; 0: the tile has no data
    uint16_t size = 0;
    uint16_t featureCount = 0;
    bool allLand = false;  // from the index: set whether or not the tile also has data
    int level = 0, row = 0, col = 0;
};

struct FeatureEntry {
    uint8_t kind = 0, rank = 0;
    uint32_t begin = 0, end = 0;  // the feature's bytes in the file
    int16_t ymin = 0, ymax = 0;   // tile units
};

class MapPack {
   public:
    static constexpr const char* kPath = "/contrail/map.pak";
    static constexpr int kMaxLevels = 3;

    // Opens and checks the pack. Starts over: a read failure from before is forgotten.
    bool begin(hal::IStorage* storage);
    bool isOpen() const { return open_ && !failed_; }
    // Why there is no detailed map: "" (open, or no card), "no map.pak",
    // "map.pak is damaged or from another version; rebuild it", "the card can't be written safely: CRC off",
    // "card error". A static literal.
    const char* error() const { return status_.load(); }
    int damagedTiles() const { return damaged_.load(); }

    int levelCount() const { return levels_; }
    const PackLevel& level(int i) const { return level_[i]; }
    int levelFor(float radiusNm) const;
    uint16_t coverNm() const { return coverNm_; }
    int32_t homeLatE5() const { return homeLatE5_; }
    int32_t homeLonE5() const { return homeLonE5_; }
    uint32_t packId() const { return packId_; }
    uint32_t cacheOffset() const { return cacheOffset_; }
    uint32_t cacheSize() const { return cacheSize_; }
    // True when the whole view (home ± radius across, ± 1.125 radius down) lies inside the coverage. Reads only
    // what begin() set, so the web task may call it too.
    bool covers(double lat, double lon, float radiusNm) const;

    // A tile's header: false when it is empty, out of range, damaged or unreadable. Whenever the index entry
    // was read, t.allLand and t.offset are set (t.offset != 0: the tile has data), whatever the result; a
    // tile can have data and also be all land. `verify` also checks the tile's CRC, a full read of it: the
    // painter does that once per rebuild. A failed read turns the pack off (readFailed).
    bool tile(int level, int row, int col, TileInfo& t, bool verify = true);
    bool feature(const TileInfo& t, int i, FeatureEntry& out);
    hal::IFile* file() { return file_; }
    // A read or write failed (card pulled): the detailed map is off until restart.
    void readFailed();

   private:
    bool fail(const char* why) { return status_ = why, open_ = false, false; }
    bool readAt(uint32_t off, void* buf, uint32_t n);
    hal::IFile* file_ = nullptr;
    bool open_ = false;
    std::atomic<bool> failed_{false};
    std::atomic<const char*> status_{""};
    std::atomic<int> damaged_{0};
    int levels_ = 0;
    PackLevel level_[kMaxLevels];
    uint16_t coverNm_ = 0;
    int32_t homeLatE5_ = 0, homeLonE5_ = 0;
    uint32_t packId_ = 0, cacheOffset_ = 0, cacheSize_ = 0;
    // Damaged tiles already counted: a small set of (level, row, col) keys, so a tile is counted once. Once it
    // is full the count stays at kDamagedKeys.
    static constexpr int kDamagedKeys = 16;
    uint32_t damagedKey_[kDamagedKeys] = {};
};

}  // namespace basemap
