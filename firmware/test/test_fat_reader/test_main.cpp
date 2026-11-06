// FatReader over real FAT images made by macOS (tools/make_fat_fixtures.sh) and a few hand-made sectors.
//
// Many tests pin layout values of the fixtures as macOS's FAT driver allocated them: LBAs, cluster numbers,
// where files are fragmented, which directory slots it reused. Regenerating the images with another macOS
// may move them; fatls-style inspection of the new image then gives the new values.
#include <unity.h>
#include <zlib.h>

#include <cstring>
#include <array>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "Fixture.h"
#include "TestStorage.h"
#include "assets/FatReader.h"
#include "assets/LogoStore.h"

void setUp() {}
void tearDown() {}

// The fixtures' file contents: the little-endian word at offset 4k is k * 2654435761 + seed.
static uint8_t pat(uint32_t i, uint32_t seed) {
    const uint32_t w = (i / 4) * 2654435761u + seed;
    return uint8_t(w >> (8 * (i % 4)));
}
static bool matches(const uint8_t* buf, uint32_t off, uint32_t n, uint32_t seed) {
    for (uint32_t i = 0; i < n; ++i)
        if (buf[i] != pat(off + i, seed)) return false;
    return true;
}

static std::vector<uint8_t> gunzip(const char* name) {
    std::vector<uint8_t> out;
    gzFile gz = gzopen((std::string(FIXTURE_DIR) + "/" + name).c_str(), "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(gz, name);
    uint8_t buf[65536];
    int n;
    while ((n = gzread(gz, buf, sizeof buf)) > 0) out.insert(out.end(), buf, buf + n);
    gzclose(gz);
    return out;
}

struct MemDisk : assets::IBlockDevice {
    std::vector<uint8_t> bytes;
    int reads = 0;
    bool fail = false;
    uint32_t lo = 0, hi = 0;  // reads in [lo, hi) also counted in rangeReads
    int rangeReads = 0;
    int writes = 0;
    bool writeFail = false;
    bool writeBlock(uint32_t lba, const uint8_t* buf) override {
        ++writes;
        if (writeFail || (uint64_t(lba) + 1) * 512 > bytes.size()) return false;
        std::memcpy(bytes.data() + size_t(lba) * 512, buf, 512);
        return true;
    }
    bool readBlock(uint32_t lba, uint8_t* buf) override {
        ++reads;
        if (lba >= lo && lba < hi) ++rangeReads;
        if (fail || (uint64_t(lba) + 1) * 512 > bytes.size()) return false;
        std::memcpy(buf, bytes.data() + size_t(lba) * 512, 512);
        return true;
    }
};

static MemDisk& fat32() {
    static MemDisk d;
    if (d.bytes.empty()) d.bytes = gunzip("fat32_mbr.img.gz");
    d.fail = false, d.reads = d.rangeReads = 0;
    return d;
}
static MemDisk& fat16() {
    static MemDisk d;
    if (d.bytes.empty()) d.bytes = gunzip("fat16_flat.img.gz");
    d.fail = false, d.reads = d.rangeReads = 0;
    return d;
}

static void test_mounts_fat32_behind_an_mbr() {
    assets::FatVolume v;
    TEST_ASSERT_TRUE_MESSAGE(v.mount(&fat32()), v.error());
    TEST_ASSERT_TRUE(v.mounted());
    TEST_ASSERT_EQUAL(32, v.fatType());
    TEST_ASSERT_EQUAL_STRING("", v.error());
    TEST_ASSERT_EQUAL(512, v.clusterBytes());
}

static void test_mounts_fat16_superfloppy() {
    assets::FatVolume v;
    TEST_ASSERT_TRUE_MESSAGE(v.mount(&fat16()), v.error());
    TEST_ASSERT_EQUAL(16, v.fatType());
    TEST_ASSERT_EQUAL(2048, v.clusterBytes());
}

// LOGOS.PAK on the FAT32 image: 200000 bytes in 391 one-sector clusters, in two runs (55-94, 106-456).
static void test_whole_file_reads_back_the_pattern() {
    assets::FatVolume v;
    v.mount(&fat32());
    hal::IFile* f = v.open("/contrail/logos.pak");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, v.error());
    TEST_ASSERT_EQUAL_UINT32(200000, f->size());
    static uint8_t buf[200000];
    TEST_ASSERT_TRUE(f->readAt(0, buf, sizeof buf));
    TEST_ASSERT_TRUE(matches(buf, 0, sizeof buf, 1));
    // Odd-sized pieces, each straddling sector (= cluster) boundaries.
    for (uint32_t off = 0; off < 200000; off += 1237) {
        uint32_t n = std::min<uint32_t>(1237, 200000 - off);
        std::memset(buf, 0, n);
        TEST_ASSERT_TRUE(f->readAt(off, buf, n));
        TEST_ASSERT_TRUE(matches(buf, off, n, 1));
    }
}

static void test_random_reads_match_the_pattern() {
    for (auto* disk : {&fat32(), &fat16()}) {
        assets::FatVolume v;
        TEST_ASSERT_TRUE(v.mount(disk));
        const bool is32 = v.fatType() == 32;
        hal::IFile* f = v.open(is32 ? "/CONTRAIL/LOGOS.PAK" : "/data/pattern.bin");
        TEST_ASSERT_NOT_NULL(f);
        const uint32_t seed = is32 ? 1 : 6;
        std::mt19937 rng(42);
        static uint8_t buf[2200];
        for (int i = 0; i < 3000; ++i) {
            const uint32_t n = i % 3 == 0 ? 12 : rng() % 2100 + 1;  // index-sized and item-sized reads
            const uint32_t off = rng() % (f->size() - n + 1);
            TEST_ASSERT_TRUE(f->readAt(off, buf, n));
            TEST_ASSERT_TRUE_MESSAGE(matches(buf, off, n, seed), "pattern mismatch");
        }
    }
}

static void test_reads_across_the_fragment_boundary() {
    assets::FatVolume v;
    v.mount(&fat32());
    hal::IFile* f = v.open("/contrail/logos.pak");
    uint8_t buf[64];
    // Cluster index 39 is cluster 94, index 40 is cluster 106.
    TEST_ASSERT_TRUE(f->readAt(40 * 512 - 30, buf, 60));
    TEST_ASSERT_TRUE(matches(buf, 40 * 512 - 30, 60, 1));
    assets::FatVolume v2;  // one file to a volume
    v2.mount(&fat32());
    hal::IFile* c = v2.open("/frag/c.bin");  // 506-521, then 538-580
    TEST_ASSERT_NOT_NULL(c);
    static uint8_t all[30000];
    TEST_ASSERT_TRUE(c->readAt(0, all, 30000));
    TEST_ASSERT_TRUE(matches(all, 0, 30000, 5));
    TEST_ASSERT_TRUE(c->readAt(16 * 512 - 7, buf, 14));
    TEST_ASSERT_TRUE(matches(buf, 16 * 512 - 7, 14, 5));
}

// What the map's timing log reports: every block fetched, through the cache or straight into the caller's buffer.
static void test_io_stats_count_the_card_reads() {
    assets::FatVolume v;
    MemDisk& d = fat32();
    v.mount(&d);
    hal::IFile* f = v.open("/contrail/logos.pak");
    const uint32_t before = f->ioStats().blocksRead;
    const int reads = d.reads;
    uint8_t buf[1400];
    TEST_ASSERT_TRUE(f->readAt(100, buf, sizeof buf));
    TEST_ASSERT_TRUE(d.reads > reads);
    TEST_ASSERT_EQUAL_UINT32(uint32_t(d.reads - reads), f->ioStats().blocksRead - before);
}

static void test_seeks_forward_and_backward() {
    assets::FatVolume v;
    v.mount(&fat32());
    hal::IFile* f = v.open("/contrail/logos.pak");
    uint8_t buf[16];
    const uint32_t offs[] = {199990, 0, 100000, 50, 150000, 149000, 199000, 20000, 20481, 3};
    for (uint32_t off : offs) {
        TEST_ASSERT_TRUE(f->readAt(off, buf, 10));
        TEST_ASSERT_TRUE(matches(buf, off, 10, 1));
    }
}

static void test_reads_past_the_end_fail() {
    assets::FatVolume v;
    v.mount(&fat32());
    hal::IFile* f = v.open("/contrail/logos.pak");
    uint8_t buf[16];
    TEST_ASSERT_TRUE(f->readAt(199990, buf, 10));
    TEST_ASSERT_FALSE(f->readAt(199991, buf, 10));
    TEST_ASSERT_FALSE(f->readAt(300000, buf, 1));
    TEST_ASSERT_TRUE(f->readAt(200000, buf, 0));
    assets::FatVolume v2;  // one file to a volume
    v2.mount(&fat32());
    hal::IFile* e = v2.open("/empty.txt");
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_EQUAL_UINT32(0, e->size());
    TEST_ASSERT_TRUE(e->readAt(0, buf, 0));
    TEST_ASSERT_FALSE(e->readAt(0, buf, 1));
}

static void put16(std::vector<uint8_t>& d, size_t at, uint16_t v) { d[at] = uint8_t(v), d[at + 1] = uint8_t(v >> 8); }
static void put32(std::vector<uint8_t>& d, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) d[at + i] = uint8_t(v >> (8 * i));
}
// The FAT starts at LBA 1 + 32 (partition + reserved sectors); one 512-byte FAT sector holds 128 entries, so
// LOGOS.PAK's chain (clusters 55-456) spans FAT sectors 0-3.
static void test_checkpoints_bound_backward_seeks() {
    MemDisk& d = fat32();
    d.lo = 33, d.hi = 33 + 630;
    assets::FatVolume v;
    v.mount(&d);
    // Opening walks the chain once, through every FAT sector it touches, filling the checkpoints.
    d.reads = d.rangeReads = 0;
    hal::IFile* f = v.open("/contrail/logos.pak");
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_TRUE(d.rangeReads >= 3);  // FAT sector 0 may be cached from the directory walk
    uint8_t buf[12];
    // So the end costs one data sector.
    d.reads = d.rangeReads = 0;
    TEST_ASSERT_TRUE(f->readAt(199990, buf, 10));
    TEST_ASSERT_EQUAL(0, d.rangeReads);
    TEST_ASSERT_EQUAL(1, d.reads);
    // Back 30 clusters: from the checkpoint at or before it, not from the start.
    d.reads = d.rangeReads = 0;
    TEST_ASSERT_TRUE(f->readAt(199990 - 30 * 512, buf, 10));
    TEST_ASSERT_TRUE(matches(buf, 199990 - 30 * 512, 10, 1));
    TEST_ASSERT_TRUE(d.rangeReads <= 1);
    TEST_ASSERT_TRUE(d.reads <= 2);
    // Every checkpoint is now known: any backward seek walks fewer than `spacing` (49) entries, at most two
    // FAT sectors, plus the data sector. FAT and data share the sector cache, so a read that straddles two
    // clusters may read a FAT sector again for the second.
    std::mt19937 rng(7);
    for (int i = 0; i < 200; ++i) {
        const uint32_t off = rng() % (200000 - 12);
        d.reads = 0;
        TEST_ASSERT_TRUE(f->readAt(off, buf, 12));
        TEST_ASSERT_TRUE(matches(buf, off, 12, 1));
        TEST_ASSERT_TRUE(d.reads <= 5);  // ≤ 2 FAT + data, then FAT + data for a read that straddles two sectors
    }
    // The start costs only the data sector: the first cluster is in the directory entry.
    d.reads = 0;
    TEST_ASSERT_TRUE(f->readAt(10, buf, 10));
    TEST_ASSERT_TRUE(d.reads <= 1);
    d.lo = d.hi = 0;
}

// A card-sized file, made up on the fly: 10400 one-sector clusters (logos.pak on the real card), one cluster
// skipped every `gap` (100) so the chain is fragmented throughout; gap 0 lays it out contiguously. FAT32
// superfloppy: reserved 32, one FAT.
struct SynthDisk : assets::IBlockDevice {
    static constexpr uint32_t kRsvd = 32, kFatSz = 1563, kData = kRsvd + kFatSz;
    const uint32_t kFileClusters, kGap, kSpc, kTotal;  // kSpc sectors a cluster: as many clusters whatever it is
    uint32_t clusterOf(uint32_t i) const { return 3 + i + (kGap ? i / kGap : 0); }
    std::vector<uint32_t> fat, index;  // FAT entries; file cluster → its index
    int reads = 0, fatReads = 0;
    bool junkDir = false;  // the root directory is the long chain, full of deleted entries and no end marker
    explicit SynthDisk(uint32_t fileClusters = 10400, uint32_t gap = 100, uint32_t spc = 1)
        : kFileClusters(fileClusters), kGap(gap), kSpc(spc), kTotal(200000 * spc) {
        fat.assign(clusterOf(kFileClusters) + 2, 0);
        index.assign(fat.size(), UINT32_MAX);
        fat[2] = 0x0FFFFFFF;
        for (uint32_t i = 0; i < kFileClusters; ++i) {
            fat[clusterOf(i)] = i + 1 < kFileClusters ? clusterOf(i + 1) : 0x0FFFFFFF;
            index[clusterOf(i)] = i;
        }
    }
    // Multi-block calls (n > 1) and the largest n; written blocks read back over the generated ones.
    int multiReads = 0, multiWrites = 0, singleWrites = 0;
    uint32_t longest = 0;
    std::map<uint32_t, std::array<uint8_t, 512>> written;
    bool readBlocks(uint32_t lba, uint8_t* b, uint32_t n) override {
        multiReads += n > 1, longest = std::max(longest, n);
        return IBlockDevice::readBlocks(lba, b, n);
    }
    bool writeBlocks(uint32_t lba, const uint8_t* b, uint32_t n) override {
        multiWrites += n > 1, longest = std::max(longest, n);
        return IBlockDevice::writeBlocks(lba, b, n);
    }
    bool writeBlock(uint32_t lba, const uint8_t* b) override {
        ++singleWrites;
        std::memcpy(written[lba].data(), b, 512);
        return true;
    }
    bool readBlock(uint32_t lba, uint8_t* b) override {
        ++reads;
        if (written.count(lba)) return std::memcpy(b, written[lba].data(), 512), true;
        std::memset(b, 0, 512);
        auto w16 = [&](int at, uint16_t v) { b[at] = uint8_t(v), b[at + 1] = uint8_t(v >> 8); };
        auto w32 = [&](int at, uint32_t v) { for (int i = 0; i < 4; ++i) b[at + i] = uint8_t(v >> (8 * i)); };
        if (lba == 0) {
            b[0] = 0xEB, b[1] = 0x58, b[2] = 0x90;
            w16(11, 512), b[13] = uint8_t(kSpc), w16(14, kRsvd), b[16] = 1;
            w32(32, kTotal), w32(36, kFatSz), w32(44, junkDir ? 3 : 2);
            b[510] = 0x55, b[511] = 0xAA;
        } else if (lba >= kRsvd && lba < kData) {
            ++fatReads;
            for (uint32_t e = 0; e < 128; ++e) {
                const uint32_t c = (lba - kRsvd) * 128 + e;
                if (c < fat.size()) w32(4 * e, fat[c]);
            }
        } else if (lba == kData) {  // cluster 2, the root directory
            std::memcpy(b, "BIG     BIN", 11);
            b[11] = 0x20;
            w16(26, 3);
            w32(28, kFileClusters * kSpc * 512);
        } else if ((lba - kData) % kSpc) {  // a cluster's later sectors: file data only
            const uint32_t c = (lba - kData) / kSpc + 2, sec = (lba - kData) % kSpc;
            if (!junkDir && c < index.size() && index[c] != UINT32_MAX)
                for (uint32_t j = 0; j < 512; ++j) b[j] = pat((index[c] * kSpc + sec) * 512 + j, 9);
        } else {
            const uint32_t c = (lba - kData) / kSpc + 2;
            if (junkDir) {
                for (int e = 0; e < 16; ++e) b[32 * e] = 0xE5, b[32 * e + 11] = 0x20;
            } else if (c < index.size() && index[c] != UINT32_MAX)
                for (uint32_t j = 0; j < 512; ++j) b[j] = pat(index[c] * kSpc * 512 + j, 9);
        }
        return true;
    }
};

static void test_card_sized_chain_seeks_stay_bounded() {
    SynthDisk d;
    assets::FatVolume v;
    TEST_ASSERT_TRUE_MESSAGE(v.mount(&d), v.error());
    TEST_ASSERT_EQUAL(32, v.fatType());
    d.fatReads = 0;
    hal::IFile* f = v.open("/big.bin");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, v.error());
    TEST_ASSERT_EQUAL(83, d.fatReads);  // the one walk, at open: entries 3-10505, 128 a sector
    // Checkpoints every 325 clusters: any seek walks at most 324 entries, so at most 4 FAT sectors.
    std::mt19937 rng(3);
    static uint8_t buf[2100];
    int worst = 0;
    for (int i = 0; i < 400; ++i) {
        const uint32_t n = i % 2 ? 12 : rng() % 2000 + 1;
        const uint32_t off = rng() % (f->size() - n + 1);
        d.fatReads = 0;
        TEST_ASSERT_TRUE(f->readAt(off, buf, n));
        TEST_ASSERT_TRUE(matches(buf, off, n, 9));
        worst = std::max(worst, d.fatReads);
    }
    // Checkpoints every 1300 clusters: a seek walks at most 1299 entries, at most 12 FAT sectors; each further
    // cluster of a read that spans several (up to 5 here) may read a FAT sector again, as data shares the cache.
    TEST_ASSERT_TRUE_MESSAGE(worst <= 16, std::to_string(worst).c_str());
    // A binary search over a 19.5 KB index, as LogoStore does: each probe walks on from a recent probe below
    // it. A probe's data read can evict a FAT sector from the shared cache: at most about one FAT read a probe
    // (11 probes); a contiguous file needs none (test_contiguous_file_reads_without_the_fat).
    d.reads = d.fatReads = 0;
    const uint32_t base = 5000000;
    uint32_t lo = 0, hi = 1628;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        TEST_ASSERT_TRUE(f->readAt(base + 12 * mid, buf, 12));
        if (mid < 700) lo = mid + 1;
        else hi = mid;
    }
    TEST_ASSERT_TRUE_MESSAGE(d.fatReads <= 11, std::to_string(d.fatReads).c_str());
}

// A contiguous file, as a PC writes logos.pak: open() walks the chain once and finds it consecutive, and from
// then on no read touches the FAT, so every block read is file data.
static void test_contiguous_file_reads_without_the_fat() {
    SynthDisk d(10400, 0);
    assets::FatVolume v;
    TEST_ASSERT_TRUE_MESSAGE(v.mount(&d), v.error());
    d.fatReads = 0;
    hal::IFile* f = v.open("/big.bin");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, v.error());
    TEST_ASSERT_EQUAL(82, d.fatReads);  // the one walk: entries 3-10402, 128 a sector
    d.reads = d.fatReads = 0;
    std::mt19937 rng(5);
    static uint8_t buf[2100];
    for (int i = 0; i < 400; ++i) {
        const uint32_t n = i % 2 ? 12 : rng() % 2000 + 1;
        const uint32_t off = rng() % (f->size() - n + 1);
        const int before = d.reads;
        TEST_ASSERT_TRUE(f->readAt(off, buf, n));
        TEST_ASSERT_TRUE(matches(buf, off, n, 9));
        TEST_ASSERT_TRUE(d.reads - before <= int((off % 512 + n + 511) / 512));  // its data sectors, no more
    }
    // The index binary search too.
    uint32_t lo = 0, hi = 1628;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        TEST_ASSERT_TRUE(f->readAt(5000000 + 12 * mid, buf, 12));
        TEST_ASSERT_TRUE(matches(buf, 5000000 + 12 * mid, 12, 9));
        if (mid < 700) lo = mid + 1;
        else hi = mid;
    }
    TEST_ASSERT_EQUAL(0, d.fatReads);
    // The last byte, straight to its cluster.
    TEST_ASSERT_TRUE(f->readAt(f->size() - 1, buf, 1));
    TEST_ASSERT_TRUE(matches(buf, f->size() - 1, 1, 9));
    TEST_ASSERT_EQUAL(0, d.fatReads);
}

// Whole sectors in a row go to the device as one multi-block call: the whole run in a contiguous file, a cluster at
// a time in a fragmented one. The partial sectors at either end still come through the cache.
static void test_whole_sectors_in_a_row_are_one_call() {
    SynthDisk d(10400, 0);
    assets::FatVolume v;
    TEST_ASSERT_TRUE(v.mount(&d));
    hal::IFile* f = v.open("/big.bin");
    TEST_ASSERT_NOT_NULL(f);
    static uint8_t buf[20480];
    d.multiReads = 0, d.longest = 0;
    const int before = d.reads;
    TEST_ASSERT_TRUE(f->readAt(1000, buf, sizeof buf));
    TEST_ASSERT_TRUE(matches(buf, 1000, sizeof buf, 9));
    TEST_ASSERT_EQUAL(1, d.multiReads);
    TEST_ASSERT_EQUAL_UINT32((20480 - 24 - 488) / 512, d.longest);  // sectors 2..40, whole
    TEST_ASSERT_EQUAL(41, d.reads - before);                        // and the two ends

    SynthDisk frag(10400, 100);  // one-sector clusters, a gap after every 100
    assets::FatVolume w;
    TEST_ASSERT_TRUE(w.mount(&frag));
    f = w.open("/big.bin");
    frag.multiReads = 0, frag.longest = 0;
    TEST_ASSERT_TRUE(f->readAt(512 * 95, buf, 512 * 10));
    TEST_ASSERT_TRUE(matches(buf, 512 * 95, 512 * 10, 9));
    TEST_ASSERT_EQUAL(0, frag.multiReads);
    TEST_ASSERT_EQUAL_UINT32(0, frag.longest);  // a sector at a time, through readBlock
}

static void test_whole_sector_writes_in_a_row_are_one_call() {
    SynthDisk d(10400, 0);
    assets::FatVolume v;
    TEST_ASSERT_TRUE(v.mount(&d));
    hal::IFile* f = v.open("/big.bin");
    std::vector<uint8_t> w(15 * 512 + 100), r(w.size());
    for (size_t i = 0; i < w.size(); ++i) w[i] = uint8_t(i * 31 + 7);
    TEST_ASSERT_TRUE(f->readAt(4096, r.data(), 16));  // two sectors of it in the cache first: the first and the sixth
    TEST_ASSERT_TRUE(f->readAt(4096 + 5 * 512 + 10, r.data(), 16));
    const uint32_t written = f->ioStats().blocksWritten;
    TEST_ASSERT_TRUE(f->writeAt(4096, w.data(), uint32_t(w.size())));
    TEST_ASSERT_EQUAL(1, d.multiWrites);
    TEST_ASSERT_EQUAL_UINT32(15, d.longest);
    TEST_ASSERT_EQUAL(16, d.singleWrites);  // the 15 through the default loop, then the patched last sector
    TEST_ASSERT_TRUE(f->readAt(4096, r.data(), uint32_t(r.size())));
    TEST_ASSERT_TRUE(w == r);
    // Partial reads come through the cache: neither cached sector was left stale.
    for (uint32_t at : {0u, 5u * 512 + 10}) {
        TEST_ASSERT_TRUE(f->readAt(4096 + at, r.data(), 16));
        TEST_ASSERT_EQUAL_MEMORY(&w[at], r.data(), 16);
    }
    TEST_ASSERT_EQUAL_UINT32(16, f->ioStats().blocksWritten - written);
    uint8_t after[4];
    TEST_ASSERT_TRUE(f->readAt(4096 + uint32_t(w.size()), after, 4));
    TEST_ASSERT_TRUE(matches(after, 4096 + uint32_t(w.size()), 4, 9));
}

// A fragmented file with eight-sector clusters: a read of fewer whole sectors than the cluster has left takes just
// those (the run stops at n, not at the cluster's end), and one that crosses a gap is cut there.
static void test_runs_stop_at_the_read_and_at_the_cluster() {
    SynthDisk d(2000, 3, 8);  // a gap after every third cluster
    assets::FatVolume v;
    TEST_ASSERT_TRUE_MESSAGE(v.mount(&d), v.error());
    TEST_ASSERT_EQUAL(32, v.fatType());
    TEST_ASSERT_EQUAL(4096, v.clusterBytes());
    hal::IFile* f = v.open("/big.bin");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, v.error());
    std::vector<uint8_t> buf(8192 + 64, 0xCC);
    d.longest = 0;
    TEST_ASSERT_TRUE(f->readAt(4096 + 1024, buf.data(), 1024));  // two whole sectors of six left in the cluster
    TEST_ASSERT_TRUE(matches(buf.data(), 4096 + 1024, 1024, 9));
    TEST_ASSERT_EQUAL_UINT32(2, d.longest);
    for (size_t i = 1024; i < buf.size(); ++i) TEST_ASSERT_EQUAL_HEX8(0xCC, buf[i]);  // nothing past the read
    d.longest = 0, d.multiReads = 0;
    TEST_ASSERT_TRUE(f->readAt(2 * 4096 + 2048, buf.data(), 8192));  // the third cluster's end, then the gap
    TEST_ASSERT_TRUE(matches(buf.data(), 2 * 4096 + 2048, 8192, 9));
    TEST_ASSERT_EQUAL(3, d.multiReads);  // 4 sectors, then 8 (one cluster), then 4
    TEST_ASSERT_EQUAL_UINT32(8, d.longest);
}

static void test_broken_chain_is_refused_at_open() {
    std::vector<uint8_t> img = fat32().bytes;
    // Cut LOGOS.PAK's chain at cluster 200 (FAT at LBA 33, 4 bytes an entry).
    put32(img, 33 * 512 + 200 * 4, 0x0FFFFFFF);
    MemDisk d;
    d.bytes = std::move(img);
    assets::FatVolume v;
    TEST_ASSERT_TRUE(v.mount(&d));
    TEST_ASSERT_NULL(v.open("/contrail/logos.pak"));
    TEST_ASSERT_EQUAL_STRING("broken cluster chain", v.error());
    TEST_ASSERT_NOT_NULL(v.open("/frag/c.bin"));  // the slot was given back
}

static void test_repeated_reads_hit_the_cache() {
    MemDisk& d = fat32();
    assets::FatVolume v;
    v.mount(&d);
    hal::IFile* f = v.open("/contrail/logos.pak");
    uint8_t buf[12];
    TEST_ASSERT_TRUE(f->readAt(70000, buf, 12));
    d.reads = 0;
    TEST_ASSERT_TRUE(f->readAt(70100, buf, 12));  // same sector
    TEST_ASSERT_EQUAL(0, d.reads);
}

static MemDisk copyOfFat32();

// Two sectors in turn stay cached (index, feature table and data no longer evict one another); a third evicts the one
// used least recently.
static void test_two_sectors_share_the_cache() {
    MemDisk& d = fat32();
    assets::FatVolume v;
    v.mount(&d);
    hal::IFile* f = v.open("/contrail/logos.pak");
    uint8_t buf[12];
    const uint32_t a = 70000, b = 80000, c = 90000;
    // Each once, so their clusters are remembered and no FAT sector is walked or cached from here on; then a and b
    // again, so both ways hold them.
    for (uint32_t off : {c, a, b, a, b}) TEST_ASSERT_TRUE(f->readAt(off, buf, 12));
    d.reads = 0;
    for (int i = 0; i < 4; ++i) {
        TEST_ASSERT_TRUE(f->readAt(a, buf, 12));
        TEST_ASSERT_TRUE(matches(buf, a, 12, 1));
        TEST_ASSERT_TRUE(f->readAt(b, buf, 12));
        TEST_ASSERT_TRUE(matches(buf, b, 12, 1));
    }
    TEST_ASSERT_EQUAL(0, d.reads);
    // b was used last, so c evicts a.
    TEST_ASSERT_TRUE(f->readAt(c, buf, 12));
    TEST_ASSERT_TRUE(matches(buf, c, 12, 1));
    TEST_ASSERT_EQUAL(1, d.reads);
    d.reads = 0;
    TEST_ASSERT_TRUE(f->readAt(b, buf, 12));
    TEST_ASSERT_EQUAL(0, d.reads);
    TEST_ASSERT_TRUE(f->readAt(a, buf, 12));
    TEST_ASSERT_TRUE(d.reads >= 1);
}

// A write drops its sector from whichever way holds it: the read that follows sees the new bytes.
static void test_a_write_is_seen_whichever_way_held_the_sector() {
    MemDisk d = copyOfFat32();
    assets::FatVolume v;
    TEST_ASSERT_TRUE(v.mount(&d));
    hal::IFile* f = v.open("/contrail/logos.pak");
    uint8_t buf[4];
    const uint32_t a = 70000, b = 80000;
    for (int order = 0; order < 2; ++order) {
        // The sector to write read first, then second: in turn it sits in each way.
        for (int round = 0; round < 3; ++round) {
            TEST_ASSERT_TRUE(f->readAt(order ? b : a, buf, 4));
            TEST_ASSERT_TRUE(f->readAt(order ? a : b, buf, 4));
            const uint8_t mark[4] = {uint8_t(order), uint8_t(round), 0x5A, 0xA5};
            TEST_ASSERT_TRUE(f->writeAt(a, mark, 4));
            TEST_ASSERT_TRUE(f->readAt(a, buf, 4));
            TEST_ASSERT_EQUAL_MEMORY(mark, buf, 4);
        }
    }
}

static void test_paths_match_short_names_in_either_case() {
    assets::FatVolume v;
    v.mount(&fat32());
    hal::IFile* a = v.open("/contrail/logos.pak");
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_EQUAL_PTR(a, v.open("/CONTRAIL/LOGOS.PAK"));
    TEST_ASSERT_EQUAL_PTR(a, v.open("/Contrail/Logos.Pak"));
    TEST_ASSERT_EQUAL_PTR(a, v.open("//contrail//logos.pak"));
    assets::FatVolume v2;  // one file to a volume
    v2.mount(&fat32());
    hal::IFile* in = v2.open("/contrail/nested/deep/inner.bin");  // nested directories
    TEST_ASSERT_NOT_NULL_MESSAGE(in, v2.error());
    TEST_ASSERT_EQUAL_UINT32(3000, in->size());
    static uint8_t buf[3000];
    TEST_ASSERT_TRUE(in->readAt(0, buf, 3000));
    TEST_ASSERT_TRUE(matches(buf, 0, 3000, 2));
}

static void test_lower_case_and_long_names() {
    assets::FatVolume v;
    v.mount(&fat32());
    // readme.txt: stored as README.TXT with the lower-case flags.
    hal::IFile* r = v.open("/readme.txt");
    TEST_ASSERT_NOT_NULL(r);
    char buf[16] = {};
    TEST_ASSERT_TRUE(r->readAt(0, buf, 11));
    TEST_ASSERT_EQUAL_STRING("lower case\n", buf);
    // A long name is not read; its short alias is.
    TEST_ASSERT_NULL(v.open("/Long Name File.txt"));
    TEST_ASSERT_EQUAL_STRING("not found", v.error());
    assets::FatVolume v2;  // one file to a volume
    v2.mount(&fat32());
    hal::IFile* l = v2.open("/longn~13.txt");
    TEST_ASSERT_NOT_NULL(l);
    std::memset(buf, 0, sizeof buf);
    TEST_ASSERT_TRUE(l->readAt(0, buf, 10));
    TEST_ASSERT_EQUAL_STRING("long name\n", buf);
}

static void test_missing_deleted_and_directory_paths() {
    assets::FatVolume v;
    v.mount(&fat32());
    TEST_ASSERT_NULL(v.open("/contrail/missing.pak"));
    TEST_ASSERT_EQUAL_STRING("not found", v.error());
    TEST_ASSERT_NULL(v.open("/contrail/old.pak"));  // a deleted entry
    TEST_ASSERT_NULL(v.open("/frag/a.bin"));        // deleted, its slot reused
    TEST_ASSERT_NULL(v.open("/ctest32"));           // the volume label
    TEST_ASSERT_NULL(v.open("/nodir/logos.pak"));
    TEST_ASSERT_NULL(v.open("/contrail/logos.pak/x"));  // a file used as a directory
    TEST_ASSERT_EQUAL_STRING("not found", v.error());
    TEST_ASSERT_NULL(v.open("/contrail/averylongname.pak"));  // cannot be 8.3
    TEST_ASSERT_NULL(v.open("/contrail"));
    TEST_ASSERT_EQUAL_STRING("is a directory", v.error());
    TEST_ASSERT_NULL(v.open("/contrail/nested/"));
    TEST_ASSERT_EQUAL_STRING("is a directory", v.error());
    TEST_ASSERT_NULL(v.open("/"));
    TEST_ASSERT_NULL(v.open("contrail/logos.pak"));
    TEST_ASSERT_EQUAL_STRING("bad path", v.error());
    TEST_ASSERT_NULL(v.open(nullptr));
}

static void test_two_files_at_most() {
    assets::FatVolume v;
    v.mount(&fat32());
    hal::IFile* a = v.open("/contrail/logos.pak");
    TEST_ASSERT_NOT_NULL(a);
    hal::IFile* c = v.open("/frag/c.bin");  // the second slot (map.pak's)
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_TRUE(a != c);
    TEST_ASSERT_NULL(v.open("/contrail/nested/deep/inner.bin"));
    TEST_ASSERT_EQUAL_STRING("too many open files", v.error());
    TEST_ASSERT_EQUAL_PTR(a, v.open("/CONTRAIL/LOGOS.PAK"));  // already open: still fine
    TEST_ASSERT_EQUAL_PTR(c, v.open("/FRAG/C.BIN"));
    // Interleaved reads share the sector cache and still read right.
    uint8_t buf[600];
    for (uint32_t off = 0; off < 29000; off += 977) {
        TEST_ASSERT_TRUE(a->readAt(off * 6, buf, 600));
        TEST_ASSERT_TRUE(matches(buf, off * 6, 600, 1));
        TEST_ASSERT_TRUE(c->readAt(off, buf, 600));
        TEST_ASSERT_TRUE(matches(buf, off, 600, 5));
    }
    // Two volumes on one card: the same.
    assets::FatVolume v2;
    v2.mount(&fat32());
    hal::IFile* b = v2.open("/frag/c.bin");
    TEST_ASSERT_NOT_NULL(b);
    for (uint32_t off = 0; off < 29000; off += 977) {
        TEST_ASSERT_TRUE(a->readAt(off * 6, buf, 600));
        TEST_ASSERT_TRUE(matches(buf, off * 6, 600, 1));
        TEST_ASSERT_TRUE(b->readAt(off, buf, 600));
        TEST_ASSERT_TRUE(matches(buf, off, 600, 5));
    }
}

// A private copy of the FAT32 image, so a test that writes leaves the shared one alone.
static MemDisk copyOfFat32() {
    MemDisk d;
    d.bytes = fat32().bytes;
    return d;
}

static void test_write_inside_a_file_leaves_the_rest_of_the_card_alone() {
    MemDisk d = copyOfFat32();
    const std::vector<uint8_t> before = d.bytes;
    assets::FatVolume v;
    TEST_ASSERT_TRUE(v.mount(&d));
    hal::IFile* f = v.open("/contrail/logos.pak");
    TEST_ASSERT_NOT_NULL(f);
    // A run across the fragment boundary (cluster index 40) that starts and ends inside sectors.
    const uint32_t off = 40 * 512 - 700, n = 1500;
    std::vector<uint8_t> w(n);
    size_t differs = 0;
    for (uint32_t i = 0; i < n; ++i) {
        w[i] = uint8_t(i * 7 + 3);
        differs += w[i] != pat(off + i, 1);
    }
    TEST_ASSERT_TRUE(differs > n / 2);
    TEST_ASSERT_TRUE(f->writeAt(off, w.data(), n));
    std::vector<uint8_t> r(n);
    TEST_ASSERT_TRUE(f->readAt(off, r.data(), n));
    TEST_ASSERT_EQUAL_MEMORY(w.data(), r.data(), n);
    // Nothing else on the card changed: not the FAT, a directory entry, or the file's neighbours.
    size_t changed = 0;
    for (size_t i = 0; i < d.bytes.size(); ++i) changed += d.bytes[i] != before[i];
    TEST_ASSERT_EQUAL(differs, changed);
    // The bytes just outside the run still read as the file's pattern.
    uint8_t edge[2];
    TEST_ASSERT_TRUE(f->readAt(off - 1, edge, 1));
    TEST_ASSERT_TRUE(f->readAt(off + n, edge + 1, 1));
    TEST_ASSERT_TRUE(matches(edge, off - 1, 1, 1) && matches(edge + 1, off + n, 1, 1));
    // A file written through one volume reads back through a fresh mount.
    assets::FatVolume v2;
    TEST_ASSERT_TRUE(v2.mount(&d));
    hal::IFile* g = v2.open("/contrail/logos.pak");
    TEST_ASSERT_TRUE(g->readAt(off, r.data(), n));
    TEST_ASSERT_EQUAL_MEMORY(w.data(), r.data(), n);
}

static void test_write_patches_a_cached_sector_and_a_whole_sector() {
    MemDisk d = copyOfFat32();
    assets::FatVolume v;
    TEST_ASSERT_TRUE(v.mount(&d));
    hal::IFile* f = v.open("/contrail/logos.pak");
    uint8_t b[16];
    TEST_ASSERT_TRUE(f->readAt(1024 + 10, b, 16));  // the sector is now in the shared cache
    const uint8_t w[16] = {9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 1, 2, 3, 4, 5, 6};
    TEST_ASSERT_TRUE(f->writeAt(1024 + 10, w, 16));
    TEST_ASSERT_TRUE(f->readAt(1024 + 10, b, 16));  // not the stale cached bytes
    TEST_ASSERT_EQUAL_MEMORY(w, b, 16);
    uint8_t whole[512];
    for (int i = 0; i < 512; ++i) whole[i] = uint8_t(i ^ 0x5A);
    TEST_ASSERT_TRUE(f->writeAt(2048, whole, 512));  // exactly one sector: no read first
    uint8_t back[512];
    TEST_ASSERT_TRUE(f->readAt(2048, back, 512));
    TEST_ASSERT_EQUAL_MEMORY(whole, back, 512);
    TEST_ASSERT_TRUE(f->writeAt(f->size() - 3, w, 3));  // the last bytes of the file
    TEST_ASSERT_TRUE(f->readAt(f->size() - 3, back, 3));
    TEST_ASSERT_EQUAL_MEMORY(w, back, 3);
}

static void test_write_past_the_end_is_refused() {
    MemDisk d = copyOfFat32();
    assets::FatVolume v;
    TEST_ASSERT_TRUE(v.mount(&d));
    hal::IFile* f = v.open("/contrail/logos.pak");
    uint8_t b[4] = {};
    TEST_ASSERT_FALSE(f->writeAt(f->size() - 2, b, 4));
    TEST_ASSERT_FALSE(f->writeAt(f->size() + 1, b, 1));
    TEST_ASSERT_EQUAL(0, d.writes);
}

static void test_write_failure_is_reported_and_leaves_no_stale_cache() {
    MemDisk d = copyOfFat32();
    assets::FatVolume v;
    TEST_ASSERT_TRUE(v.mount(&d));
    hal::IFile* f = v.open("/contrail/logos.pak");
    uint8_t b[16];
    TEST_ASSERT_TRUE(f->readAt(10, b, 16));  // the sector is now in the cache
    const uint8_t w[16] = {1, 2, 3};
    d.writeFail = true;
    TEST_ASSERT_FALSE(f->writeAt(10, w, 16));
    d.writeFail = false;
    TEST_ASSERT_TRUE(f->readAt(10, b, 16));
    TEST_ASSERT_TRUE(matches(b, 10, 16, 1));  // the card was not changed, and neither is what we read
}

static void test_write_on_a_read_only_device_fails() {
    struct ReadOnly : assets::IBlockDevice {
        MemDisk* d;
        bool readBlock(uint32_t lba, uint8_t* buf) override { return d->readBlock(lba, buf); }
    } ro;
    MemDisk d = copyOfFat32();
    ro.d = &d;
    assets::FatVolume v;
    TEST_ASSERT_TRUE(v.mount(&ro));
    hal::IFile* f = v.open("/contrail/logos.pak");
    const uint8_t w[4] = {1, 2, 3, 4};
    TEST_ASSERT_FALSE(f->writeAt(0, w, 4));
}

static void test_failing_device_fails_cleanly() {
    MemDisk& d = fat32();
    assets::FatVolume v;
    d.fail = true;
    TEST_ASSERT_FALSE(v.mount(&d));
    TEST_ASSERT_FALSE(v.mounted());
    TEST_ASSERT_EQUAL_STRING("card read failed", v.error());
    TEST_ASSERT_NULL(v.open("/contrail/logos.pak"));
    d.fail = false;
    TEST_ASSERT_TRUE(v.mount(&d));
    hal::IFile* f = v.open("/contrail/logos.pak");
    uint8_t buf[700];
    d.fail = true;  // the card is pulled
    TEST_ASSERT_FALSE(f->readAt(100000, buf, 700));
    TEST_ASSERT_FALSE(f->readAt(512, buf, 512));
    TEST_ASSERT_NULL(v.open("/frag/c.bin"));
    TEST_ASSERT_EQUAL_STRING("card read failed", v.error());
    d.fail = false;  // nothing bad was cached
    TEST_ASSERT_TRUE(f->readAt(100000, buf, 700));
    TEST_ASSERT_TRUE(matches(buf, 100000, 700, 1));
}

// --- refusals, from hand-made sectors --------------------------------------------------------------------

static std::vector<uint8_t> blank(size_t sectors = 8) {
    std::vector<uint8_t> d(sectors * 512, 0);
    d[510] = 0x55, d[511] = 0xAA;
    return d;
}
// A boot sector at `lba` with the given geometry.
static void bpb(std::vector<uint8_t>& d, size_t lba, uint16_t bps, uint8_t spc, uint16_t rootEnt, uint32_t total,
                uint16_t fatSz) {
    const size_t b = lba * 512;
    d[b] = 0xEB, d[b + 1] = 0x3C, d[b + 2] = 0x90;
    std::memcpy(&d[b + 3], "MSDOS5.0", 8);
    put16(d, b + 11, bps);
    d[b + 13] = spc;
    put16(d, b + 14, 1);
    d[b + 16] = 2;
    put16(d, b + 17, rootEnt);
    if (total < 65536) put16(d, b + 19, uint16_t(total));
    else put32(d, b + 32, total);
    put16(d, b + 22, fatSz);
    d[b + 510] = 0x55, d[b + 511] = 0xAA;
}
static std::string refusal(std::vector<uint8_t> bytes) {
    MemDisk d;
    d.bytes = std::move(bytes);
    assets::FatVolume v;
    TEST_ASSERT_FALSE(v.mount(&d));
    TEST_ASSERT_FALSE(v.mounted());
    return v.error();
}
static void mbr(std::vector<uint8_t>& d, uint8_t type, uint32_t lba) {
    d[446 + 4] = type;
    put32(d, 446 + 8, lba);
}

static void test_refusals_name_the_reason() {
    std::vector<uint8_t> junk(4096);
    std::mt19937 rng(1);
    for (auto& b : junk) b = uint8_t(rng());
    junk[510] = 0;
    TEST_ASSERT_EQUAL_STRING("not FAT (no boot signature)", refusal(junk).c_str());
    TEST_ASSERT_EQUAL_STRING("no FAT partition", refusal(blank()).c_str());

    auto d = blank();
    mbr(d, 0x83, 1);  // Linux
    TEST_ASSERT_EQUAL_STRING("no FAT partition", refusal(d).c_str());
    d = blank();
    mbr(d, 0xEE, 1);
    TEST_ASSERT_EQUAL_STRING("GPT partition table (needs MBR)", refusal(d).c_str());

    d = blank();  // exFAT, no partition table
    d[0] = 0xEB, d[1] = 0x76, d[2] = 0x90;
    std::memcpy(&d[3], "EXFAT   ", 8);
    TEST_ASSERT_EQUAL_STRING("exFAT (format the card FAT32)", refusal(d).c_str());
    d = blank();  // exFAT in an MBR partition (type 0x07, as SDXC cards ship)
    mbr(d, 0x07, 2);
    std::memcpy(&d[2 * 512 + 3], "EXFAT   ", 8);
    TEST_ASSERT_EQUAL_STRING("exFAT (format the card FAT32)", refusal(d).c_str());
    d = blank();
    mbr(d, 0x07, 2);
    std::memcpy(&d[2 * 512 + 3], "NTFS    ", 8);
    TEST_ASSERT_EQUAL_STRING("NTFS (format the card FAT32)", refusal(d).c_str());

    d = blank();  // a 1.44 MB floppy: 2847 clusters
    bpb(d, 0, 512, 1, 224, 2880, 9);
    TEST_ASSERT_EQUAL_STRING("FAT12 (format the card FAT32)", refusal(d).c_str());
    d = blank();  // the same behind an MBR
    mbr(d, 0x01, 3);
    bpb(d, 3, 512, 1, 224, 2880, 9);
    TEST_ASSERT_EQUAL_STRING("FAT12 (format the card FAT32)", refusal(d).c_str());
    d = blank();
    bpb(d, 0, 4096, 8, 512, 2000000, 64);
    TEST_ASSERT_EQUAL_STRING("sector size not 512", refusal(d).c_str());
    d = blank();  // the FAT cannot hold the clusters
    bpb(d, 0, 512, 4, 512, 400000, 20);
    TEST_ASSERT_EQUAL_STRING("not FAT (FAT too small)", refusal(d).c_str());
    d = blank();  // the partition is not a boot sector
    mbr(d, 0x0C, 2);
    TEST_ASSERT_EQUAL_STRING("not FAT (bad boot sector)", refusal(d).c_str());
    d = blank(2);  // the partition is past the end of the card
    mbr(d, 0x0C, 100);
    TEST_ASSERT_EQUAL_STRING("card read failed", refusal(d).c_str());

    // The real FAT32 image with its partition type changed.
    d = fat32().bytes;
    d[446 + 4] = 0x83;
    TEST_ASSERT_EQUAL_STRING("no FAT partition", refusal(d).c_str());
}

static void test_mount_after_a_refusal_reads_the_new_card() {
    MemDisk bad;
    bad.bytes = blank();
    assets::FatVolume v;
    TEST_ASSERT_FALSE(v.mount(&bad));
    TEST_ASSERT_TRUE(v.mount(&fat16()));
    TEST_ASSERT_NOT_NULL(v.open("/data/pattern.bin"));
}


// --- corrupt cards (hand-made from the FAT32 image) -------------------------------------------------------

// FAT32 fixture layout: partition at LBA 1, 32 reserved sectors, two 630-sector FATs, one-sector clusters.
constexpr size_t kFat0 = 33 * 512, kFat1 = (33 + 630) * 512;
constexpr uint32_t kDataLba = 1 + 32 + 2 * 630;
static size_t clusterAt(uint32_t c) { return size_t(kDataLba + c - 2) * 512; }
static void setFat(std::vector<uint8_t>& img, uint32_t c, uint32_t v) { put32(img, kFat0 + 4 * c, v); }
// The short-name entry `name11` with attribute `attr`; each one the tests use occurs once in the image.
static size_t entryAt(const std::vector<uint8_t>& img, const char* name11, uint8_t attr) {
    for (size_t o = 0; o + 32 <= img.size(); o += 32)
        if (!std::memcmp(&img[o], name11, 11) && img[o + 11] == attr) return o;
    TEST_FAIL_MESSAGE(name11);
    return 0;
}
// Opens `path` on a copy of the image; the error ("" when it opened) and the block reads it took.
static std::string openOn(std::vector<uint8_t> img, const char* path, int* reads = nullptr) {
    static MemDisk d;
    d.bytes = std::move(img);
    d.reads = 0, d.fail = false;
    assets::FatVolume v;
    if (!v.mount(&d)) return std::string("mount: ") + v.error();
    hal::IFile* f = v.open(path);
    if (reads) *reads = d.reads;
    return f ? "" : v.error();
}

static void test_looping_directory_is_refused() {
    std::vector<uint8_t> img = fat32().bytes;
    const size_t frag = entryAt(img, "FRAG       ", 0x10);
    TEST_ASSERT_EQUAL(38, img[frag + 26]);
    // No end marker in FRAG's one cluster: every free slot becomes a deleted entry.
    for (int i = 0; i < 16; ++i) {
        const size_t e = clusterAt(38) + 32 * i;
        if (img[e] == 0) img[e] = 0xE5, img[e + 11] = 0x20;
    }
    int reads = 0;
    setFat(img, 38, 38);  // FRAG's chain points at itself
    TEST_ASSERT_EQUAL_STRING("broken cluster chain", openOn(img, "/frag/zz.bin", &reads).c_str());
    TEST_ASSERT_TRUE(reads < 20);
    // A two-cluster loop, 38 → 39 → 38, with 39 junk too.
    setFat(img, 38, 39), setFat(img, 39, 38);
    std::memset(&img[clusterAt(39)], 0xE5, 512);
    TEST_ASSERT_EQUAL_STRING("broken cluster chain", openOn(img, "/frag/zz.bin", &reads).c_str());
    TEST_ASSERT_TRUE(reads < 20);
}

// A long, loop-free directory chain with no end marker stops at the spec's 65536 entries (4096 sectors).
static void test_directory_scan_is_capped() {
    SynthDisk d;
    d.junkDir = true;
    assets::FatVolume v;
    TEST_ASSERT_TRUE(v.mount(&d));
    d.reads = 0;
    TEST_ASSERT_NULL(v.open("/x.bin"));
    TEST_ASSERT_EQUAL_STRING("not found", v.error());
    TEST_ASSERT_TRUE(d.reads >= 4096);
    // + a FAT sector per cluster: the directory's sectors evict it from the shared cache.
    TEST_ASSERT_TRUE(d.reads <= 2 * 4096 + 2);
}

static void test_huge_or_looping_file_is_refused() {
    const std::vector<uint8_t> base = fat32().bytes;
    const size_t logos = entryAt(base, "LOGOS   PAK", 0x20);
    int reads = 0;
    std::vector<uint8_t> img = base;
    put32(img, logos + 28, 0xFFFFFFFF);  // 8 M clusters on an 80627-cluster volume
    TEST_ASSERT_EQUAL_STRING("file larger than the card", openOn(img, "/contrail/logos.pak", &reads).c_str());
    TEST_ASSERT_TRUE(reads < 20);
    // As large as a file may be (32768 clusters), with the last cluster (456) looping back to the first (55).
    img = base;
    put32(img, logos + 28, 32768 * 512);
    setFat(img, 456, 55);
    TEST_ASSERT_EQUAL_STRING("broken cluster chain", openOn(img, "/contrail/logos.pak", &reads).c_str());
    TEST_ASSERT_TRUE(reads < 40);
    // Larger than that: refused before any walk.
    img = base;
    put32(img, logos + 28, 80000 * 512);
    TEST_ASSERT_EQUAL_STRING("file too large", openOn(img, "/contrail/logos.pak", &reads).c_str());
    // A chain that goes on past the file's end (here: the size cut in half).
    img = base;
    put32(img, logos + 28, 100000);
    TEST_ASSERT_EQUAL_STRING("broken cluster chain", openOn(img, "/contrail/logos.pak").c_str());
}

// Every chain is walked and loop-checked at open, so a file is capped at 32768 clusters.
static void test_file_cluster_cap() {
    {
        SynthDisk d(32768);
        assets::FatVolume v;
        TEST_ASSERT_TRUE(v.mount(&d));
        hal::IFile* f = v.open("/big.bin");
        TEST_ASSERT_NOT_NULL_MESSAGE(f, v.error());
        uint8_t buf[16];
        TEST_ASSERT_TRUE(f->readAt(f->size() - 16, buf, 16));
        TEST_ASSERT_TRUE(matches(buf, f->size() - 16, 16, 9));
    }
    SynthDisk d(32769);
    assets::FatVolume v;
    TEST_ASSERT_TRUE(v.mount(&d));
    d.reads = 0;
    TEST_ASSERT_NULL(v.open("/big.bin"));
    TEST_ASSERT_EQUAL_STRING("file too large", v.error());
    TEST_ASSERT_TRUE(d.reads < 5);  // refused from the directory entry, before any walk
}

static void test_fat32_cluster_count_limit() {
    auto d = blank();
    bpb(d, 0, 512, 1, 0, 0xFFFFFFFF, 0);
    put32(d, 36, 1u << 25);  // a FAT big enough for 2^32 entries: ~4.2 G clusters
    TEST_ASSERT_EQUAL_STRING("not FAT (too many clusters)", refusal(d).c_str());
}

// Blocks by number, zeros elsewhere: for geometry far past any real image.
struct MapDisk : assets::IBlockDevice {
    std::map<uint32_t, std::vector<uint8_t>> blocks;
    bool readBlock(uint32_t lba, uint8_t* b) override {
        auto it = blocks.find(lba);
        if (it == blocks.end()) std::memset(b, 0, 512);
        else std::memcpy(b, it->second.data(), 512);
        return true;
    }
};

static void test_bpb_arithmetic_cannot_wrap() {
    auto d = blank();  // two FATs of 2^31 sectors: 2^32 + 1 sectors of metadata, 0 + 1 in 32 bits
    bpb(d, 0, 512, 8, 0, 0xFFFFFFF0, 0);
    put32(d, 36, 0x80000000);
    TEST_ASSERT_EQUAL_STRING("not FAT (bad BPB)", refusal(d).c_str());
    // A partition at 2^31 whose volume would run past LBA 2^32.
    MapDisk m;
    auto s0 = blank(1), boot = blank(1);
    mbr(s0, 0x0C, 0x80000000);
    bpb(boot, 0, 512, 64, 0, 0x80000001, 0);
    put32(boot, 36, 16384);
    put32(boot, 44, 2);
    m.blocks[0] = s0, m.blocks[0x80000000] = boot;
    assets::FatVolume v;
    TEST_ASSERT_FALSE(v.mount(&m));
    TEST_ASSERT_EQUAL_STRING("not FAT (bad BPB)", v.error());
}

static void test_bad_first_clusters() {
    const std::vector<uint8_t> base = fat32().bytes;
    const size_t dir = entryAt(base, "CONTRAIL   ", 0x10), logos = entryAt(base, "LOGOS   PAK", 0x20);
    std::vector<uint8_t> img = base;
    put16(img, dir + 20, 0x0FFF), put16(img, dir + 26, 0xFFF0);  // a subdirectory at cluster 0x0FFFFFF0
    TEST_ASSERT_EQUAL_STRING("bad directory entry", openOn(img, "/contrail/logos.pak").c_str());
    img = base;
    put16(img, dir + 20, 0xF000);  // the high nibble is reserved on FAT32: 0xF000000B is cluster 11
    TEST_ASSERT_EQUAL_STRING("", openOn(img, "/contrail/logos.pak").c_str());
    img = base;
    put16(img, logos + 26, 1);  // a file at cluster 1 (reserved), size > 0
    TEST_ASSERT_EQUAL_STRING("bad directory entry", openOn(img, "/contrail/logos.pak").c_str());
    img = base;
    put16(img, logos + 26, 0);  // no cluster, size > 0
    TEST_ASSERT_EQUAL_STRING("bad directory entry", openOn(img, "/contrail/logos.pak").c_str());
}

// BPB_ExtFlags (FAT32, offset 40): bit 7 set turns mirroring off and bits 0-3 pick the active FAT.
static void test_active_fat_is_honoured() {
    std::vector<uint8_t> img = fat32().bytes;
    constexpr size_t kExtFlags = 512 + 40;
    setFat(img, 200, 0);  // LOGOS.PAK's chain broken in FAT 0 only
    TEST_ASSERT_EQUAL_UINT32(201, img[kFat1 + 4 * 200]);  // FAT 1 still has it
    TEST_ASSERT_EQUAL_STRING("broken cluster chain", openOn(img, "/contrail/logos.pak").c_str());
    put16(img, kExtFlags, 0x01);  // an active index but mirroring on: FAT 0 is still the one
    TEST_ASSERT_EQUAL_STRING("broken cluster chain", openOn(img, "/contrail/logos.pak").c_str());
    put16(img, kExtFlags, 0x81);  // FAT 1 alone
    TEST_ASSERT_EQUAL_STRING("", openOn(img, "/contrail/logos.pak").c_str());
    static MemDisk d;
    d.bytes = img;
    assets::FatVolume v;
    TEST_ASSERT_TRUE(v.mount(&d));
    static uint8_t buf[200000];
    TEST_ASSERT_TRUE(v.open("/contrail/logos.pak")->readAt(0, buf, sizeof buf));
    TEST_ASSERT_TRUE(matches(buf, 0, sizeof buf, 1));
    put16(img, kExtFlags, 0x82);  // FAT 2 of 2
    TEST_ASSERT_EQUAL_STRING("mount: not FAT (bad active FAT)", openOn(img, "/contrail/logos.pak").c_str());
}

static void test_trailing_slash_after_a_file() {
    assets::FatVolume v;
    v.mount(&fat32());
    TEST_ASSERT_NULL(v.open("/frag/b.bin/"));
    TEST_ASSERT_EQUAL_STRING("not found", v.error());
    TEST_ASSERT_NULL(v.open("/frag/b.bin//"));
    TEST_ASSERT_EQUAL_STRING("not found", v.error());
    TEST_ASSERT_NULL(v.open("/frag/"));
    TEST_ASSERT_EQUAL_STRING("is a directory", v.error());
    TEST_ASSERT_NOT_NULL(v.open("/frag/b.bin"));
}

// --- end to end: LogoStore reading logos_tiny.pak at /contrail/logos.pak on the FAT16 image -------------

static void test_logo_store_reads_through_fat() {
    assets::FatVolume v;
    TEST_ASSERT_TRUE(v.mount(&fat16()));
    assets::LogoStore viaFat;
    viaFat.begin(&v);
    TEST_ASSERT_TRUE(viaFat.mounted());
    TEST_ASSERT_EQUAL(3, viaFat.logoCount());
    TEST_ASSERT_EQUAL_STRING("", viaFat.error());

    MemStorage mem;
    mem.add(assets::LogoStore::kPath, loadFixture("logos_tiny.pak"));
    assets::LogoStore viaMem;
    viaMem.begin(&mem);
    static gfx::Badge40 a, b;
    static gfx::Badge25 c, e;
    for (const char* icao : {"UAL", "DAL"}) {
        TEST_ASSERT_TRUE(viaFat.load(icao, a));
        TEST_ASSERT_TRUE(viaMem.load(icao, b));
        TEST_ASSERT_EQUAL_MEMORY(b.px, a.px, sizeof a.px);
        TEST_ASSERT_EQUAL_MEMORY(b.pal, a.pal, sizeof a.pal);
        TEST_ASSERT_EQUAL(b.badge.colours, a.badge.colours);
        TEST_ASSERT_TRUE(viaFat.load(icao, c));
        TEST_ASSERT_TRUE(viaMem.load(icao, e));
        TEST_ASSERT_EQUAL_MEMORY(e.px, c.px, sizeof c.px);
    }
    TEST_ASSERT_FALSE(viaFat.load("ZZZ", a));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_mounts_fat32_behind_an_mbr);
    RUN_TEST(test_mounts_fat16_superfloppy);
    RUN_TEST(test_whole_file_reads_back_the_pattern);
    RUN_TEST(test_random_reads_match_the_pattern);
    RUN_TEST(test_reads_across_the_fragment_boundary);
    RUN_TEST(test_io_stats_count_the_card_reads);
    RUN_TEST(test_whole_sectors_in_a_row_are_one_call);
    RUN_TEST(test_whole_sector_writes_in_a_row_are_one_call);
    RUN_TEST(test_runs_stop_at_the_read_and_at_the_cluster);
    RUN_TEST(test_seeks_forward_and_backward);
    RUN_TEST(test_reads_past_the_end_fail);
    RUN_TEST(test_checkpoints_bound_backward_seeks);
    RUN_TEST(test_card_sized_chain_seeks_stay_bounded);
    RUN_TEST(test_contiguous_file_reads_without_the_fat);
    RUN_TEST(test_broken_chain_is_refused_at_open);
    RUN_TEST(test_repeated_reads_hit_the_cache);
    RUN_TEST(test_two_sectors_share_the_cache);
    RUN_TEST(test_a_write_is_seen_whichever_way_held_the_sector);
    RUN_TEST(test_paths_match_short_names_in_either_case);
    RUN_TEST(test_lower_case_and_long_names);
    RUN_TEST(test_missing_deleted_and_directory_paths);
    RUN_TEST(test_two_files_at_most);
    RUN_TEST(test_write_inside_a_file_leaves_the_rest_of_the_card_alone);
    RUN_TEST(test_write_patches_a_cached_sector_and_a_whole_sector);
    RUN_TEST(test_write_past_the_end_is_refused);
    RUN_TEST(test_write_failure_is_reported_and_leaves_no_stale_cache);
    RUN_TEST(test_write_on_a_read_only_device_fails);
    RUN_TEST(test_failing_device_fails_cleanly);
    RUN_TEST(test_refusals_name_the_reason);
    RUN_TEST(test_mount_after_a_refusal_reads_the_new_card);
    RUN_TEST(test_logo_store_reads_through_fat);
    RUN_TEST(test_looping_directory_is_refused);
    RUN_TEST(test_directory_scan_is_capped);
    RUN_TEST(test_huge_or_looping_file_is_refused);
    RUN_TEST(test_file_cluster_cap);
    RUN_TEST(test_fat32_cluster_count_limit);
    RUN_TEST(test_bpb_arithmetic_cannot_wrap);
    RUN_TEST(test_bad_first_clusters);
    RUN_TEST(test_active_fat_is_honoured);
    RUN_TEST(test_trailing_slash_after_a_file);
    return UNITY_END();
}
