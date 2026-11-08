#include "RangeStorage.h"

#include <algorithm>
#include <cstring>

namespace hal {

RangeFile::RangeFile(std::string url, uint32_t size, FetchRange fetch)
    : url_(std::move(url)), size_(size), fetch_(std::move(fetch)) {}

bool RangeFile::readAt(uint32_t offset, void* buf, uint32_t n) {
    if (uint64_t(offset) + n > size_) return false;
    uint8_t* out = static_cast<uint8_t*>(buf);
    while (n) {
        const uint8_t* b = block(offset / kBlock);
        if (!b) return false;
        uint32_t at = offset % kBlock, k = std::min(n, kBlock - at);
        std::memcpy(out, b + at, k);
        out += k, offset += k, n -= k;
    }
    return true;
}

bool RangeFile::writeAt(uint32_t offset, const void* buf, uint32_t n) {
    if (uint64_t(offset) + n > size_) return false;
    const uint8_t* in = static_cast<const uint8_t*>(buf);
    while (n) {
        uint32_t index = offset / kBlock, at = offset % kBlock, k = std::min(n, kBlock - at);
        auto d = dirty_.find(index);
        if (d == dirty_.end()) {
            const uint8_t* b = block(index);  // the site's bytes first: the rest of the block must stay readable
            if (!b) return false;
            uint32_t len = std::min(kBlock, size_ - index * kBlock);
            d = dirty_.emplace(index, std::vector<uint8_t>(b, b + len)).first;
        }
        std::memcpy(d->second.data() + at, in, k);
        in += k, offset += k, n -= k;
    }
    return true;
}

const uint8_t* RangeFile::block(uint32_t index) {
    auto d = dirty_.find(index);
    if (d != dirty_.end()) return d->second.data();
    Slot* victim = &slots_[0];
    for (Slot& s : slots_) {
        if (s.index == index) return s.used = ++tick_, s.data.data();
        if (s.used < victim->used) victim = &s;
    }
    uint32_t start = index * kBlock, n = std::min(kBlock, size_ - start);
    victim->data.resize(n);
    victim->index = -1;
    if (!fetch_(url_, start, n, victim->data.data())) return nullptr;
    victim->index = index, victim->used = ++tick_;
    return victim->data.data();
}

RangeStorage::RangeStorage(std::map<std::string, std::string> urls, FetchSize size, FetchRange fetch)
    : urls_(std::move(urls)), size_(std::move(size)), fetch_(std::move(fetch)) {}

IFile* RangeStorage::open(const char* path) {
    auto o = open_.find(path);
    if (o != open_.end()) return o->second.get();
    auto u = urls_.find(path);
    if (u == urls_.end() || u->second.empty()) return nullptr;
    int64_t n = size_(u->second);
    if (n < 0 || n > int64_t(UINT32_MAX)) return nullptr;
    auto& f = open_[path];
    f = std::make_unique<RangeFile>(u->second, uint32_t(n), fetch_);
    return f.get();
}

}  // namespace hal
