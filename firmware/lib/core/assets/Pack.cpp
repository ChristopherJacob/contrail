#include "Pack.h"

#include <cstring>

namespace assets {

namespace {
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
constexpr uint32_t kHeader = 32, kEntry = 12;
}  // namespace

uint16_t crc16(const uint8_t* p, size_t n, uint16_t crc) {
    for (size_t i = 0; i < n; ++i) {
        crc ^= uint16_t(p[i]) << 8;
        for (int b = 0; b < 8; ++b) crc = crc & 0x8000 ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
    }
    return crc;
}

bool Pack::open(hal::IFile* f, uint16_t kind) {
    f_ = nullptr, count_ = 0, version_ = 0, readFailed_ = false;
    if (!f) return error_ = PackError::NotOpen, false;
    uint8_t h[kHeader];
    if (f->size() < kHeader || !f->readAt(0, h, kHeader)) return error_ = PackError::Truncated, false;
    if (std::memcmp(h, "CPAK", 4)) return error_ = PackError::BadMagic, false;
    version_ = le16(h + 4);
    if (version_ != kVersion) return error_ = PackError::BadVersion, false;
    if (le16(h + 6) != kind) return error_ = PackError::BadKind, false;
    const uint32_t count = le32(h + 8), index = le32(h + 12);
    if (index < kHeader || index > f->size() || (f->size() - index) / kEntry < count)
        return error_ = PackError::Truncated, false;
    f_ = f, count_ = count, indexOffset_ = index, error_ = PackError::None;
    return true;
}

bool Pack::find(const char* key, uint32_t& offset, uint32_t& size) {
    if (!f_ || !key || !key[0]) return false;
    char want[4] = {};
    for (int i = 0; i < 4 && key[i]; ++i) want[i] = key[i];
    if (std::strlen(key) > 4) return false;
    int lo = 0, hi = int(count_) - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        uint8_t e[kEntry];
        if (!f_->readAt(indexOffset_ + uint32_t(mid) * kEntry, e, kEntry)) return readFailed_ = true, false;
        const int cmp = std::memcmp(e, want, 4);
        if (cmp == 0) {
            offset = le32(e + 4), size = le32(e + 8);
            return offset <= f_->size() && f_->size() - offset >= size;
        }
        if (cmp < 0) lo = mid + 1;
        else hi = mid - 1;
    }
    return false;
}

bool Pack::read(uint32_t offset, void* buf, uint32_t n) {
    if (!f_) return false;
    if (f_->readAt(offset, buf, n)) return true;
    readFailed_ = true;
    return false;
}

}  // namespace assets
