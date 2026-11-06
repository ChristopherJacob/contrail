#include "FatReader.h"

#include <cstring>

namespace assets {

FatVolume::Cache FatVolume::cache_[2];
uint8_t FatVolume::oldest_ = 0;
hal::IoStats FatVolume::io_;
uint32_t (*FatVolume::clock_)() = nullptr;

bool FatVolume::read(IBlockDevice* dev, uint32_t lba, uint8_t* buf, uint32_t n) {
    const uint32_t t = now();
    const bool ok = n == 1 ? dev->readBlock(lba, buf) : dev->readBlocks(lba, buf, n);
    io_.blocksRead += n, io_.readUs += now() - t;
    return ok;
}

bool FatVolume::write(IBlockDevice* dev, uint32_t lba, const uint8_t* buf, uint32_t n) {
    const uint32_t t = now();
    const bool ok = n == 1 ? dev->writeBlock(lba, buf) : dev->writeBlocks(lba, buf, n);
    io_.blocksWritten += n, io_.writeUs += now() - t;
    return ok;
}

namespace {
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
bool pow2(uint32_t v) { return v && !(v & (v - 1)); }
char upper(char c) { return c >= 'a' && c <= 'z' ? char(c - 'a' + 'A') : c; }

// A boot sector with a plausible BPB (the check FAT drivers use to tell one from an MBR).
bool looksLikeBpb(const uint8_t* s) {
    if (!(s[0] == 0xEB && s[2] == 0x90) && s[0] != 0xE9) return false;
    uint16_t bps = le16(s + 11);
    return bps >= 512 && bps <= 4096 && pow2(bps) && pow2(s[13]) && le16(s + 14) != 0 && (s[16] == 1 || s[16] == 2);
}
bool isExfat(const uint8_t* s) { return std::memcmp(s + 3, "EXFAT   ", 8) == 0; }
bool signed55aa(const uint8_t* s) { return s[510] == 0x55 && s[511] == 0xAA; }

// One path component as a padded, upper-case 8.3 name; false when it cannot be one.
bool to83(const char* s, size_t len, char out[11]) {
    std::memset(out, ' ', 11);
    size_t dot = len;
    for (size_t i = 0; i < len; ++i)
        if (s[i] == '.') {
            if (dot != len) return false;  // two dots
            dot = i;
        }
    const size_t ext = dot == len ? 0 : len - dot - 1;
    if (dot == 0 || dot > 8 || ext > 3) return false;
    for (size_t i = 0; i < dot; ++i) out[i] = upper(s[i]);
    for (size_t i = 0; i < ext; ++i) out[8 + i] = upper(s[dot + 1 + i]);
    return true;
}

bool nameMatches(const uint8_t* d, const char name[11]) {
    for (int i = 0; i < 11; ++i) {
        char c = char(i == 0 && d[0] == 0x05 ? 0xE5 : d[i]);
        if (upper(c) != name[i]) return false;
    }
    return true;
}

constexpr uint8_t kAttrLfn = 0x0F, kAttrVolume = 0x08, kAttrDir = 0x10;

// The FAT spec's limit on a directory: 65536 entries, 4096 sectors.
constexpr uint32_t kMaxDirSectors = 65536 / 16;
// The most clusters a file may have: open() walks (and loop-checks) every one of them, so no reader ever
// walks a chain that was not checked. 16 MB at 512-byte clusters, 128 MB at 4 KB; logos.pak is 10400 clusters
// on the 512-byte-cluster card.
constexpr uint32_t kMaxEagerWalk = 32768;

// Brent's cycle detection over a cluster chain: step() is false once a cluster repeats. A loop of length L
// entered after M clusters is caught within about M + 2L steps.
struct LoopCheck {
    explicit LoopCheck(uint32_t start) : tortoise(start) {}
    bool step(uint32_t c) {
        if (c == tortoise) return false;
        if (++lam == power) tortoise = c, power <<= 1, lam = 0;
        return true;
    }
    uint32_t tortoise, power = 1, lam = 0;
};
}  // namespace

const uint8_t* FatVolume::cached(IBlockDevice* dev, uint32_t lba) {
    for (uint8_t i = 0; i < 2; ++i)
        if (cache_[i].dev == dev && cache_[i].lba == lba) {
            oldest_ = uint8_t(1 - i);
            return cache_[i].buf;
        }
    const uint8_t way = !cache_[0].dev ? 0 : !cache_[1].dev ? 1 : oldest_;  // an empty way first
    Cache& c = cache_[way];
    c.dev = nullptr;  // a failed read leaves nothing behind
    if (!read(dev, lba, c.buf, 1)) return nullptr;
    c.dev = dev, c.lba = lba;
    oldest_ = uint8_t(1 - way);
    return c.buf;
}

void FatVolume::forget(IBlockDevice* dev, uint32_t lba) {
    for (Cache& c : cache_)
        if (c.dev == dev && c.lba == lba) c.dev = nullptr;
}

const uint8_t* FatVolume::sector(uint32_t lba) { return cached(dev_, lba); }

uint32_t FatVolume::next(uint32_t clus) {
    const uint32_t off = fatType_ == 32 ? clus * 4 : clus * 2;
    const uint8_t* s = cached(dev_, fatLba_ + off / 512);
    if (!s) return 0;
    const uint32_t v = fatType_ == 32 ? le32(s + off % 512) & 0x0FFFFFFF : le16(s + off % 512);
    return validCluster(v) ? v : 0;  // end of chain, bad cluster or free: all end the walk
}

bool FatVolume::mount(IBlockDevice* dev) {
    dev_ = nullptr, fatType_ = 0;
    for (auto& f : files_) f.vol_ = nullptr;
    cache_[0].dev = cache_[1].dev = nullptr;  // perhaps another card
    if (!dev) return fail("no card");
    const uint8_t* s = cached(dev, 0);
    if (!s) return fail("card read failed");
    if (!signed55aa(s)) return fail("not FAT (no boot signature)");
    if (isExfat(s)) return fail("exFAT (format the card FAT32)");
    if (looksLikeBpb(s)) return mountBpb(dev, s, 0);

    // An MBR: the first FAT partition.
    uint32_t part = 0, other = 0;
    bool gpt = false;
    for (int i = 0; i < 4 && !part; ++i) {
        const uint8_t* e = s + 446 + 16 * i;
        const uint8_t type = e[4];
        const uint32_t lba = le32(e + 8);
        if (!lba) continue;
        switch (type) {
            case 0x01: case 0x04: case 0x06: case 0x0B: case 0x0C: case 0x0E: part = lba; break;
            case 0x07: if (!other) other = lba; break;  // exFAT or NTFS
            case 0xEE: gpt = true; break;
            default: break;
        }
    }
    if (!part && other) {
        s = cached(dev, other);
        if (!s) return fail("card read failed");
        return fail(isExfat(s) ? "exFAT (format the card FAT32)" : "NTFS (format the card FAT32)");
    }
    if (!part) return fail(gpt ? "GPT partition table (needs MBR)" : "no FAT partition");
    s = cached(dev, part);
    if (!s) return fail("card read failed");
    if (isExfat(s)) return fail("exFAT (format the card FAT32)");
    if (!signed55aa(s) || !looksLikeBpb(s)) return fail("not FAT (bad boot sector)");
    return mountBpb(dev, s, part);
}

bool FatVolume::mountBpb(IBlockDevice* dev, const uint8_t* bs, uint32_t lba) {
    if (le16(bs + 11) != 512) return fail("sector size not 512");
    const uint32_t spc = bs[13], rsvd = le16(bs + 14), nfats = bs[16], rootEnt = le16(bs + 17);
    const uint32_t fatSz = le16(bs + 22) ? le16(bs + 22) : le32(bs + 36);
    const uint32_t total = le16(bs + 19) ? le16(bs + 19) : le32(bs + 32);
    const uint32_t rootSecs = (rootEnt * 32 + 511) / 512;
    const uint64_t meta = uint64_t(rsvd) + uint64_t(nfats) * fatSz + rootSecs;  // 64-bit: no wrap
    if (!fatSz || meta >= total || spc > 128 || uint64_t(lba) + total > (uint64_t(1) << 32))
        return fail("not FAT (bad BPB)");
    const uint32_t clusters = uint32_t((total - meta) / spc);
    // The FAT type is the cluster count alone (Microsoft's FAT spec, "FAT type determination").
    if (clusters < 4085) return fail("FAT12 (format the card FAT32)");
    const int type = clusters < 65525 ? 16 : 32;
    // Cluster numbers run to clusters + 1, which must stay below FAT32's reserved values (0x0FFFFFF7 up).
    if (clusters > 0x0FFFFFF5) return fail("not FAT (too many clusters)");
    if (uint64_t(fatSz) * 512 / (type / 8) < uint64_t(clusters) + 2) return fail("not FAT (FAT too small)");
    // FAT32 BPB_ExtFlags: with bit 7 set, mirroring is off and bits 0-3 name the one active FAT.
    uint32_t active = 0;
    if (type == 32 && (le16(bs + 40) & 0x80)) {
        active = le16(bs + 40) & 0x0F;
        if (active >= nfats) return fail("not FAT (bad active FAT)");
    }
    secPerClus_ = uint8_t(spc);
    clusterCount_ = clusters;
    fatType_ = type;
    fatLba_ = lba + rsvd + active * fatSz;
    rootLba_ = lba + rsvd + nfats * fatSz;
    rootSectors_ = rootSecs;
    dataLba_ = rootLba_ + rootSecs;
    rootClus_ = type == 32 ? le32(bs + 44) : 0;
    if (type == 32 && !validCluster(rootClus_)) return fail("not FAT (bad root cluster)");
    dev_ = dev;
    error_ = "";
    return true;
}

hal::IFile* FatVolume::open(const char* path) {
    if (!dev_) return nullptr;
    if (!path || path[0] != '/') return error_ = "bad path", nullptr;
    uint32_t dir = rootClus_;  // FAT16: 0, the fixed root directory
    const char* p = path;
    for (;;) {
        while (*p == '/') ++p;
        if (!*p) return error_ = "is a directory", nullptr;
        const char* end = p;
        while (*end && *end != '/') ++end;
        char name[11];
        if (!to83(p, size_t(end - p), name)) return error_ = "not found", nullptr;
        const char* rest = end;
        while (*rest == '/') ++rest;
        const bool last = !*rest;
        const bool slash = *end == '/';  // "dir/" names a directory; "file/" names nothing

        // Search `dir`: the FAT16 root's fixed sectors, or a cluster chain.
        bool found = false, ended = false;
        uint8_t entry[32];
        uint32_t entLba = 0;
        uint16_t entIdx = 0;
        // At most kMaxDirSectors (4096) directory sectors, plus one FAT sector per cluster: worst case about
        // 8192 block reads (~4 s at 0.5 ms each) for a corrupt directory with no end marker, per component.
        uint32_t clus = dir, sec = 0, seen = 0;
        LoopCheck loop(dir);
        while (!found && !ended) {
            if (seen++ == kMaxDirSectors) break;
            uint32_t lba;
            if (clus == 0) {  // FAT16 root
                if (sec >= rootSectors_) break;
                lba = rootLba_ + sec++;
            } else {
                if (sec == secPerClus_) {
                    clus = next(clus), sec = 0;
                    if (!clus) break;
                    if (!loop.step(clus)) return error_ = "broken cluster chain", nullptr;
                }
                lba = clusterLba(clus) + sec++;
            }
            const uint8_t* s = sector(lba);
            if (!s) return error_ = "card read failed", nullptr;
            for (int i = 0; i < 16; ++i) {
                const uint8_t* d = s + 32 * i;
                if (d[0] == 0x00) {
                    ended = true;
                    break;
                }
                if (d[0] == 0xE5 || d[11] == kAttrLfn || (d[11] & kAttrVolume)) continue;
                if (!nameMatches(d, name)) continue;
                std::memcpy(entry, d, 32);
                entLba = lba, entIdx = uint16_t(i);
                found = true;
                break;
            }
        }
        if (!found) return error_ = "not found", nullptr;
        uint32_t first = le16(entry + 26);
        if (fatType_ == 32) first = (first | uint32_t(le16(entry + 20)) << 16) & 0x0FFFFFFF;
        const bool isDir = entry[11] & kAttrDir;
        if (!last || (slash && !isDir)) {
            if (!isDir) return error_ = "not found", nullptr;
            if (first && !validCluster(first)) return error_ = "bad directory entry", nullptr;
            dir = first ? first : rootClus_;  // ".." to the root is cluster 0
            p = rest;
            continue;
        }
        if (isDir) return error_ = "is a directory", nullptr;

        for (auto& f : files_)
            if (f.vol_ && f.dirLba_ == entLba && f.dirIdx_ == entIdx) return &f;
        const uint32_t size = le32(entry + 28);
        if (size && !validCluster(first)) return error_ = "bad directory entry", nullptr;
        const uint32_t clusters = uint32_t((uint64_t(size) + clusterBytes() - 1) / clusterBytes());
        if (clusters > clusterCount_) return error_ = "file larger than the card", nullptr;
        if (clusters > kMaxEagerWalk) return error_ = "file too large", nullptr;
        for (auto& f : files_) {
            if (f.vol_) continue;
            f = FatFile{};
            f.vol_ = this;
            f.dirLba_ = entLba, f.dirIdx_ = entIdx;
            f.size_ = size;
            f.first_ = size ? first : 0;
            f.spacing_ = clusters > FatFile::kCheckpoints
                             ? (clusters + FatFile::kCheckpoints - 1) / FatFile::kCheckpoints
                             : 1;
            f.marks_[0] = f.first_;
            f.contiguous_ = true;  // so far: a one-cluster file is
            // Walk the whole chain once now: it fills the checkpoints, so the first read near the end is not a
            // walk of the FAT on the render thread, and a chain that is short, loops or runs on past the file's
            // end is refused here. A chain running past the end is a FAT inconsistency rather than unreadable,
            // but the packs are copied from a PC, whose FAT drivers never leave one, so refusing it is safe and
            // turns a loop that starts after the file's last cluster into a refusal too.
            // It also finds whether the clusters are consecutive, so reads need no FAT at all.
            // Cost: one FAT entry per cluster plus the end check, at most kMaxEagerWalk entries. A real file is
            // contiguous, 128 entries to a FAT sector: logos.pak is ~82 reads (~40 ms). A pathological chain
            // that changes FAT sector at every step costs one read per entry: 32769 block reads (~16 s at
            // 0.5 ms), once, at boot.
            if (clusters > 1) {
                const uint32_t steps = clusters - 1;
                LoopCheck loop(f.first_);
                uint32_t c = f.first_;
                bool ok = true;
                for (uint32_t i = 1; i <= steps && ok; ++i) {
                    const uint32_t prev = c;
                    c = next(c);
                    ok = c && loop.step(c);
                    if (c != prev + 1) f.contiguous_ = false;
                    if (ok && i % f.spacing_ == 0 && i / f.spacing_ < FatFile::kCheckpoints)
                        f.marks_[i / f.spacing_] = c;
                }
                if (ok && next(c)) ok = false;  // the chain goes on past the end
                if (!ok) {
                    f.vol_ = nullptr;
                    return error_ = "broken cluster chain", nullptr;
                }
                f.recent_[0] = FatFile::Pos{steps, c};
            }
            return &f;
        }
        return error_ = "too many open files", nullptr;
    }
}

// Walks at most idx - (nearest known position) entries, so never past the file's last cluster, and only over a
// chain open() has walked in full and loop-checked.
uint32_t FatFile::clusterAt(uint32_t idx) {
    if (contiguous_) return first_ + idx;  // open() checked every one of them
    // Start from the nearest point at or before idx: the last position found, or a checkpoint.
    uint32_t k = idx / spacing_;
    if (k >= kCheckpoints) k = kCheckpoints - 1;
    while (k > 0 && !marks_[k]) --k;
    uint32_t at = k * spacing_, clus = marks_[k];
    for (const Pos& p : recent_)
        if (p.clus && p.idx <= idx && p.idx >= at) at = p.idx, clus = p.clus;
    while (at < idx) {
        clus = vol_->next(clus);
        if (!clus) return 0;
        ++at;
        if (at % spacing_ == 0 && at / spacing_ < kCheckpoints) marks_[at / spacing_] = clus;
    }
    int slot = kRecent - 1;  // replace the oldest, or this position if it is already there
    for (int i = 0; i < kRecent; ++i)
        if (recent_[i].clus == clus) {
            slot = i;
            break;
        }
    for (int i = slot; i > 0; --i) recent_[i] = recent_[i - 1];
    recent_[0] = Pos{at, clus};
    return clus;
}

hal::IoStats FatFile::ioStats() const { return FatVolume::io_; }

uint32_t FatFile::run(uint32_t inClus, uint32_t n) const {
    // A contiguous file's clusters are consecutive LBAs (readAt and writeAt have checked n against the size).
    const uint32_t k = contiguous_ ? n : (vol_->clusterBytes() - inClus) / 512;
    return k < n ? k : n;
}

bool FatFile::readAt(uint32_t offset, void* buf, uint32_t n) {
    if (!vol_ || !vol_->dev_ || offset > size_ || size_ - offset < n) return false;
    const uint32_t cb = vol_->clusterBytes();
    uint8_t* out = static_cast<uint8_t*>(buf);
    while (n) {
        const uint32_t clus = clusterAt(offset / cb);
        if (!clus) return false;
        const uint32_t inClus = offset % cb, inSec = inClus % 512;
        const uint32_t lba = vol_->clusterLba(clus) + inClus / 512;
        uint32_t take = n < 512 - inSec ? n : 512 - inSec;
        if (take == 512) {  // whole sectors: straight into the caller's buffer, as many as lie in a row
            const uint32_t k = run(inClus, n / 512);
            if (!FatVolume::read(vol_->dev_, lba, out, k)) return false;
            take = k * 512;
        } else {
            const uint8_t* s = vol_->sector(lba);
            if (!s) return false;
            std::memcpy(out, s + inSec, take);
        }
        out += take, offset += take, n -= take;
    }
    return true;
}

// Overwrites bytes inside the file through its own cluster chain: never the FAT, a directory entry or the
// file's size. A partial sector is read, patched and written back; whole sectors in a row go in one write.
bool FatFile::writeAt(uint32_t offset, const void* buf, uint32_t n) {
    if (!vol_ || !vol_->dev_ || offset > size_ || size_ - offset < n) return false;
    const uint32_t cb = vol_->clusterBytes();
    const uint8_t* in = static_cast<const uint8_t*>(buf);
    uint8_t patched[512];
    while (n) {
        const uint32_t clus = clusterAt(offset / cb);
        if (!clus) return false;
        const uint32_t inClus = offset % cb, inSec = inClus % 512;
        const uint32_t lba = vol_->clusterLba(clus) + inClus / 512;
        uint32_t take = n < 512 - inSec ? n : 512 - inSec;
        if (take == 512) {
            const uint32_t k = run(inClus, n / 512);
            for (uint32_t i = 0; i < k; ++i) FatVolume::forget(vol_->dev_, lba + i);  // a failed write: unknown
            if (!FatVolume::write(vol_->dev_, lba, in, k)) return false;
            take = k * 512;
        } else {
            const uint8_t* s = vol_->sector(lba);
            if (!s) return false;
            std::memcpy(patched, s, 512);
            std::memcpy(patched + inSec, in, take);
            FatVolume::forget(vol_->dev_, lba);  // before the write: a failed write leaves the block unknown
            if (!FatVolume::write(vol_->dev_, lba, patched, 1)) return false;
        }
        in += take, offset += take, n -= take;
    }
    return true;
}

}  // namespace assets
