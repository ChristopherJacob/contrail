#pragma once
// A small FAT16/FAT32 reader over 512-byte blocks: just enough to open /contrail/logos.pak and map.pak on the
// SD card and read them at random offsets. It is read-only except for overwriting bytes inside an open file
// (map.pak's saved chart): never the FAT, a directory entry, or a file's size. It replaces the Arduino SD/FATFS stack, which cost ~36 KB of flash
// (35.7 KB measured) and ~17 KB of heap with a card mounted. No heap: a two-way LRU cache of 512-byte sectors,
// shared (static) by directory, FAT and file data, so a map tile's index entry, feature table and data do not keep
// evicting one another, and two file slots (the firmware only ever opens logos.pak and map.pak).
//
// A file whose clusters are consecutive (any file a PC copies onto a card with room to spare) is found so
// when open() walks its chain, and from then on a read maps an offset to its cluster with no FAT reads, so the
// cache serves the data alone. A fragmented file still works: each read walks the FAT from the nearest
// checkpoint or recent position, through the same cache.
//
// Paths match 8.3 short names, ignoring case ("/contrail/logos.pak" finds CONTRAIL\LOGOS.PAK); long names are
// not read, so a component that does not fit 8.3 is not found.
#include <cstdint>

#include "hal.h"

namespace assets {

// 512-byte blocks by block number (LBA); false when the read failed.
struct IBlockDevice {
    virtual ~IBlockDevice() = default;
    virtual bool readBlock(uint32_t lba, uint8_t* buf512) = 0;
    // One block written; false when the write failed (or the device is read-only).
    virtual bool writeBlock(uint32_t lba, const uint8_t* buf512) { return false; }
    // n consecutive blocks; a device that can (the SD card's CMD18 and CMD25) does it in one command.
    virtual bool readBlocks(uint32_t lba, uint8_t* buf, uint32_t n) {
        for (uint32_t i = 0; i < n; ++i)
            if (!readBlock(lba + i, buf + 512 * i)) return false;
        return true;
    }
    virtual bool writeBlocks(uint32_t lba, const uint8_t* buf, uint32_t n) {
        for (uint32_t i = 0; i < n; ++i)
            if (!writeBlock(lba + i, buf + 512 * i)) return false;
        return true;
    }
};

class FatVolume;

class FatFile : public hal::IFile {
   public:
    uint32_t size() const override { return size_; }
    bool readAt(uint32_t offset, void* buf, uint32_t n) override;
    // Through the file's own cluster chain, a partial sector read, patched and written back. Never past size().
    bool writeAt(uint32_t offset, const void* buf, uint32_t n) override;
    hal::IoStats ioStats() const override;

    // Only a fragmented file uses them: with 8, a seek on the 10,400-cluster pack walks at most 1,299 entries.
    static constexpr int kCheckpoints = 8;

   private:
    friend class FatVolume;
    // The cluster holding cluster index `idx` of the file (0 when the chain is broken). idx must be inside the
    // file (readAt checks the offset).
    uint32_t clusterAt(uint32_t idx);
    // How many of the next n whole sectors (from `inClus`, sector aligned, into the cluster) lie at consecutive
    // LBAs: to the end of the cluster, or all n when the file is contiguous.
    uint32_t run(uint32_t inClus, uint32_t n) const;

    FatVolume* vol_ = nullptr;  // null: the slot is free
    uint32_t dirLba_ = 0;       // where the directory entry sits: identifies the file
    uint16_t dirIdx_ = 0;
    uint32_t size_ = 0;
    uint32_t first_ = 0;    // first cluster (0 for an empty file)
    bool contiguous_ = false;  // cluster index i is first_ + i: no FAT walks
    // The last few positions found (cluster index → cluster number), most recent at recent_[0]. More than one
    // because a binary search over the pack's index steps back to just after an earlier probe.
    static constexpr int kRecent = 4;
    struct Pos {
        uint32_t idx = 0, clus = 0;  // clus 0: unused
    };
    Pos recent_[kRecent];
    uint32_t spacing_ = 1;  // clusters between checkpoints
    uint32_t marks_[kCheckpoints] = {};  // cluster number at index k * spacing_, 0 until walked
};

class FatVolume : public hal::IStorage {
   public:
    // Reads the MBR or boot sector and the BPB; false with error() saying why.
    bool mount(IBlockDevice* dev);
    bool mounted() const override { return dev_ != nullptr; }
    // The file at an absolute path, or nullptr (missing, a directory, a read failure, or no slot free;
    // error() says which). Opening a path that is already open returns the same file.
    hal::IFile* open(const char* path) override;

    // Why the last mount or open failed ("" after a successful mount).
    const char* error() const { return error_; }
    int fatType() const { return fatType_; }  // 16 or 32 when mounted
    uint32_t clusterBytes() const { return uint32_t(secPerClus_) * 512u; }

    // Times the card's reads and writes (FatFile::ioStats) from now on: a microsecond clock, or nullptr.
    static void setClock(uint32_t (*us)()) { clock_ = us; }

    static constexpr int kMaxFiles = 2;  // logos.pak and map.pak; each slot is ~100 B of static RAM

   private:
    friend class FatFile;
    bool fail(const char* why) {
        error_ = why;
        dev_ = nullptr;
        return false;
    }
    bool mountBpb(IBlockDevice* dev, const uint8_t* bs, uint32_t lba);
    // Block `lba` through the sector cache; nullptr when the read fails. The pointer is good until the next
    // sector() or next() call (treat it so: which way holds what is the cache's business).
    const uint8_t* sector(uint32_t lba);
    // The FAT entry for `clus`: the next cluster, or 0 at the end of the chain or for a bad entry.
    uint32_t next(uint32_t clus);
    bool validCluster(uint32_t c) const { return c >= 2 && c < clusterCount_ + 2; }
    uint32_t clusterLba(uint32_t c) const { return dataLba_ + (c - 2) * secPerClus_; }

    IBlockDevice* dev_ = nullptr;
    const char* error_ = "not mounted";
    int fatType_ = 0;
    uint8_t secPerClus_ = 0;
    uint32_t fatLba_ = 0;       // the first FAT
    uint32_t rootLba_ = 0;      // FAT16: the fixed root directory
    uint32_t rootSectors_ = 0;  // FAT16
    uint32_t rootClus_ = 0;     // FAT32
    uint32_t dataLba_ = 0;      // cluster 2
    uint32_t clusterCount_ = 0;
    FatFile files_[kMaxFiles];

    // Two ways, least recently used evicted. Shared by every volume and file (one card, one thread), and by
    // directory, FAT and data sectors. Tagged by device so tests can mount several.
    struct Cache {
        IBlockDevice* dev = nullptr;
        uint32_t lba = 0;
        uint8_t buf[512];
    };
    static Cache cache_[2];
    static uint8_t oldest_;  // the way to evict next
    static hal::IoStats io_;
    static uint32_t (*clock_)();  // microseconds, for io_'s times
    static uint32_t now() { return clock_ ? clock_() : 0; }
    // The device calls, counted and timed.
    static bool read(IBlockDevice* dev, uint32_t lba, uint8_t* buf, uint32_t n);
    static bool write(IBlockDevice* dev, uint32_t lba, const uint8_t* buf, uint32_t n);
    static const uint8_t* cached(IBlockDevice* dev, uint32_t lba);
    // Drops the block from whichever way holds it: before it is written, so a failed write cannot leave old bytes.
    static void forget(IBlockDevice* dev, uint32_t lba);
};

}  // namespace assets
