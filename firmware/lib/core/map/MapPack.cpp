#include "MapPack.h"

#include <cmath>
#include <cstring>

#include "ChartCache.h"
#include "assets/Pack.h"  // crc16

namespace basemap {
namespace {
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
constexpr const char* kDamaged = "map.pak is damaged or from another version; rebuild it";
constexpr double kPi = 3.14159265358979323846;
constexpr uint32_t kHeaderBytes = 64, kLevelBytes = 24, kIndexEntry = 8;
}  // namespace

void PackLevel::cell(double lat, double lon, int& row, int& col) const {
    row = int(std::floor((lat - originLatE5 * 1e-5) / tileDeg()));
    col = int(std::floor((lon - originLonE5 * 1e-5) / tileDeg()));
}

bool MapPack::readAt(uint32_t off, void* buf, uint32_t n) {
    if (!file_ || failed_) return false;
    if (file_->readAt(off, buf, n)) return true;
    readFailed();
    return false;
}

bool MapPack::begin(hal::IStorage* storage) {
    open_ = false, failed_ = false, status_ = "", damaged_ = 0, file_ = nullptr, levels_ = 0;
    std::memset(damagedKey_, 0, sizeof damagedKey_);
    if (!storage || !storage->mounted()) return false;
    file_ = storage->open(kPath);
    if (!file_) return fail("no map.pak");
    // The saved chart is written into the pack: without command CRCs a garbled address could write elsewhere.
    if (!storage->writable()) return fail("the card can't be written safely: CRC off");
    uint8_t h[kHeaderBytes];
    if (file_->size() < sizeof h) return fail(kDamaged);
    if (!file_->readAt(0, h, sizeof h)) return fail("card error");
    if (std::memcmp(h, "CMAP", 4) != 0 || le16(h + 4) != 1 || le16(h + 6) != kHeaderBytes ||
        assets::crc16(h, 62) != le16(h + 62))
        return fail(kDamaged);
    homeLatE5_ = int32_t(le32(h + 8)), homeLonE5_ = int32_t(le32(h + 12));
    coverNm_ = le16(h + 16);
    const int levels = h[18];
    packId_ = le32(h + 20), cacheOffset_ = le32(h + 24), cacheSize_ = le32(h + 28);
    if (levels < 1 || levels > kMaxLevels || cacheOffset_ % 4096 || cacheSize_ < ChartCache::kAreaBytes ||
        uint64_t(cacheOffset_) + cacheSize_ > file_->size())
        return fail(kDamaged);
    // The level table has no CRC of its own: the header holds one (a flipped tile size or origin would
    // otherwise move every tile).
    uint8_t table[kMaxLevels * kLevelBytes];
    if (!file_->readAt(kHeaderBytes, table, kLevelBytes * levels)) return fail("card error");
    if (assets::crc16(table, kLevelBytes * levels) != le16(h + 32)) return fail(kDamaged);
    for (int i = 0; i < levels; ++i) {
        const uint8_t* e = table + kLevelBytes * i;
        PackLevel& lv = level_[i];
        lv.maxRadiusNm = le16(e), lv.tileMilliDeg = le16(e + 2);
        lv.originLatE5 = int32_t(le32(e + 4)), lv.originLonE5 = int32_t(le32(e + 8));
        lv.rows = le16(e + 12), lv.cols = le16(e + 14);
        lv.indexOffset = le32(e + 16);
        const uint64_t bytes = uint64_t(lv.rows) * lv.cols * kIndexEntry;
        if (!lv.tileMilliDeg || !lv.rows || !lv.cols || lv.indexOffset < kHeaderBytes + kLevelBytes * levels ||
            uint64_t(lv.indexOffset) + bytes > cacheOffset_)
            return fail(kDamaged);
        // The index CRC, 64 bytes at a time.
        uint16_t crc = 0xFFFF;
        uint8_t buf[64];
        for (uint32_t at = 0; at < bytes; at += sizeof buf) {
            const uint32_t n = bytes - at < sizeof buf ? uint32_t(bytes - at) : uint32_t(sizeof buf);
            if (!file_->readAt(lv.indexOffset + at, buf, n)) return fail("card error");
            crc = assets::crc16(buf, n, crc);
        }
        if (crc != le16(e + 20)) return fail(kDamaged);
    }
    levels_ = levels;
    open_ = true;
    return true;
}

int MapPack::levelFor(float radiusNm) const {
    for (int i = 0; i < levels_; ++i)
        if (radiusNm <= level_[i].maxRadiusNm) return i;
    return levels_ > 0 ? levels_ - 1 : 0;
}

bool MapPack::covers(double lat, double lon, float radiusNm) const {
    if (!isOpen()) return false;
    const double hl = homeLatE5_ * 1e-5, hn = homeLonE5_ * 1e-5;
    const double dLatNm = std::fabs(lat - hl) * 60 + 1.125 * radiusNm;
    // Longitude in nautical miles along home's parallel (the pack is a square in nm there); the view's half
    // width radiusNm is measured along its own parallel, so it is scaled by the cosine of its latitude.
    const double cosHome = std::cos(hl * kPi / 180), cosView = std::cos(lat * kPi / 180);
    const double dLonNm = std::fabs(lon - hn) * 60 * cosHome + radiusNm * cosHome / cosView;
    return dLatNm <= coverNm_ && dLonNm <= coverNm_;
}

bool MapPack::tile(int level, int row, int col, TileInfo& t, bool verify) {
    t = TileInfo{};
    t.level = level, t.row = row, t.col = col;
    if (!isOpen() || level < 0 || level >= levels_) return false;
    const PackLevel& lv = level_[level];
    if (row < 0 || col < 0 || row >= lv.rows || col >= lv.cols) return false;
    uint8_t e[kIndexEntry];
    if (!readAt(lv.indexOffset + (uint32_t(row) * lv.cols + col) * kIndexEntry, e, sizeof e)) return false;
    t.offset = le32(e), t.size = le16(e + 4), t.allLand = e[6] & 1;  // before any return that follows
    if (!t.offset) return false;
    if (t.size < 4 || uint64_t(t.offset) + t.size > cacheOffset_) return false;
    uint8_t buf[64];
    if (!readAt(t.offset, buf, 4)) return false;
    const uint16_t want = le16(buf);
    t.featureCount = le16(buf + 2);
    bool good = 4u + 8u * t.featureCount <= t.size;
    if (good && verify) {
        // The tile's CRC over everything after it, 64 bytes at a time (a tile is at most 64 KB).
        uint16_t crc = 0xFFFF;
        for (uint32_t at = 2; at < t.size; at += sizeof buf) {
            const uint32_t n = t.size - at < sizeof buf ? t.size - at : uint32_t(sizeof buf);
            if (!readAt(t.offset + at, buf, n)) return false;
            crc = assets::crc16(buf, n, crc);
        }
        good = crc == want;
    }
    if (good) return true;
    t.featureCount = 0;
    const uint32_t key = uint32_t(level + 1) << 28 | uint32_t(row) << 14 | uint32_t(col);
    for (uint32_t& k : damagedKey_) {
        if (k == key) return false;  // counted already
        if (!k) {
            k = key;
            ++damaged_;
            return false;
        }
    }
    return false;  // the set is full: the count stops at kDamagedKeys rather than counting tiles twice
}

bool MapPack::feature(const TileInfo& t, int i, FeatureEntry& out) {
    if (i < 0 || i >= t.featureCount) return false;
    uint8_t e[8];
    if (!readAt(t.offset + 4 + 8 * uint32_t(i), e, sizeof e)) return false;
    out.kind = e[0], out.rank = e[1];
    out.begin = t.offset + le16(e + 2);
    out.ymin = int16_t(le16(e + 4)), out.ymax = int16_t(le16(e + 6));
    uint32_t next = t.size;
    if (i + 1 < t.featureCount) {
        uint8_t n[8];
        if (!readAt(t.offset + 4 + 8 * uint32_t(i + 1), n, sizeof n)) return false;
        next = le16(n + 2);
    }
    out.end = t.offset + next;
    return out.begin >= t.offset + 4 + 8u * t.featureCount && out.begin < out.end && out.end <= t.offset + t.size;
}

void MapPack::readFailed() {
    failed_ = true;
    status_ = "card error";
}

}  // namespace basemap
