// The browser simulator's card (hal_wasm/RangeStorage): files read from the site in cached blocks by Range requests.
// The env ignores hal_wasm, so this suite compiles the source itself.
#include <unity.h>

#include <map>
#include <string>

#include "Fixture.h"
#include "assets/Pack.h"
#include "hal.h"  // names the hal lib for the dependency finder, which does not follow the include below
#include "map/MapPack.h"
#include "../../lib/hal_wasm/RangeStorage.cpp"

void setUp() {}
void tearDown() {}

static std::string data;  // the site's sim/map-kfdk.pak
static int requests = 0;
static bool failNext = false;

static int64_t fakeSize(const std::string& url) { return url == "sim/map-kfdk.pak" ? int64_t(data.size()) : -1; }
static bool fakeRange(const std::string& url, uint32_t offset, uint32_t n, void* out) {
    ++requests;
    if (failNext || url != "sim/map-kfdk.pak" || offset + n > data.size()) return failNext = false, false;
    std::memcpy(out, data.data() + offset, n);
    return true;
}

static hal::RangeStorage make() {
    data.resize(40000);
    for (size_t i = 0; i < data.size(); ++i) data[i] = char(i * 7 + i / 251);
    requests = 0;
    return hal::RangeStorage({{"/contrail/map.pak", "sim/map-kfdk.pak"}, {"/contrail/logos.pak", ""}}, fakeSize,
                             fakeRange);
}

static void test_reads_across_blocks() {
    hal::RangeStorage sd = make();
    hal::IFile* f = sd.open("/contrail/map.pak");
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_EQUAL_UINT32(40000, f->size());
    char buf[100];
    TEST_ASSERT_TRUE(f->readAt(16384 - 50, buf, sizeof buf));  // the end of block 0 and the start of block 1
    TEST_ASSERT_EQUAL_MEMORY(data.data() + 16384 - 50, buf, sizeof buf);
    TEST_ASSERT_EQUAL(2, requests);
}

static void test_blocks_are_cached() {
    hal::RangeStorage sd = make();
    hal::IFile* f = sd.open("/contrail/map.pak");
    char buf[10];
    TEST_ASSERT_TRUE(f->readAt(10, buf, sizeof buf));
    TEST_ASSERT_TRUE(f->readAt(1000, buf, sizeof buf));
    TEST_ASSERT_EQUAL(1, requests);
}

static void test_the_last_block_is_short() {
    hal::RangeStorage sd = make();
    hal::IFile* f = sd.open("/contrail/map.pak");
    char buf[16];
    TEST_ASSERT_TRUE(f->readAt(40000 - 16, buf, sizeof buf));
    TEST_ASSERT_EQUAL_MEMORY(data.data() + 40000 - 16, buf, sizeof buf);
    TEST_ASSERT_FALSE(f->readAt(40000 - 8, buf, sizeof buf));  // past the end
}

static void test_a_failed_request_fails_the_read_and_is_not_cached() {
    hal::RangeStorage sd = make();
    hal::IFile* f = sd.open("/contrail/map.pak");
    char buf[10];
    failNext = true;
    TEST_ASSERT_FALSE(f->readAt(10, buf, sizeof buf));
    TEST_ASSERT_TRUE(f->readAt(10, buf, sizeof buf));
    TEST_ASSERT_EQUAL_MEMORY(data.data() + 10, buf, sizeof buf);
}

static void test_missing_files() {
    hal::RangeStorage sd = make();
    TEST_ASSERT_TRUE(sd.mounted());
    TEST_ASSERT_TRUE(sd.writable());  // writes go to memory; MapPack refuses a card that is not writable
    TEST_ASSERT_NULL(sd.open("/contrail/logos.pak"));  // no URL: no release's logos
    TEST_ASSERT_NULL(sd.open("/contrail/other.pak"));
    TEST_ASSERT_EQUAL_PTR(sd.open("/contrail/map.pak"), sd.open("/contrail/map.pak"));
}

static void test_the_least_recently_used_block_is_the_one_dropped() {
    hal::RangeStorage sd = make();
    data.resize(hal::RangeFile::kBlock * (hal::RangeFile::kBlocks + 1));
    hal::IFile* f = sd.open("/contrail/map.pak");
    char c;
    for (int b = 0; b < hal::RangeFile::kBlocks; ++b) TEST_ASSERT_TRUE(f->readAt(b * hal::RangeFile::kBlock, &c, 1));
    TEST_ASSERT_TRUE(f->readAt(0, &c, 1));  // block 0 is now the most recent, block 1 the oldest
    TEST_ASSERT_TRUE(f->readAt(hal::RangeFile::kBlocks * hal::RangeFile::kBlock, &c, 1));  // evicts block 1
    requests = 0;
    TEST_ASSERT_TRUE(f->readAt(0, &c, 1));
    TEST_ASSERT_EQUAL(0, requests);
    TEST_ASSERT_TRUE(f->readAt(hal::RangeFile::kBlock, &c, 1));
    TEST_ASSERT_EQUAL(1, requests);
}

static void test_writes_are_read_back() {
    hal::RangeStorage sd = make();
    hal::IFile* f = sd.open("/contrail/map.pak");
    // Into a block not read yet: the rest of the block keeps the site's bytes.
    const char w[] = "hello";
    TEST_ASSERT_TRUE(f->writeAt(20000, w, 5));
    char buf[10];
    TEST_ASSERT_TRUE(f->readAt(19998, buf, 10));
    TEST_ASSERT_EQUAL_MEMORY(data.data() + 19998, buf, 2);
    TEST_ASSERT_EQUAL_MEMORY(w, buf + 2, 5);
    TEST_ASSERT_EQUAL_MEMORY(data.data() + 20005, buf + 7, 3);
    // Across a block boundary.
    std::string x(100, 'x');
    TEST_ASSERT_TRUE(f->writeAt(16384 - 40, x.data(), 100));
    char big[120];
    TEST_ASSERT_TRUE(f->readAt(16384 - 50, big, sizeof big));
    TEST_ASSERT_EQUAL_MEMORY(data.data() + 16384 - 50, big, 10);
    TEST_ASSERT_EQUAL_MEMORY(x.data(), big + 10, 100);
    TEST_ASSERT_EQUAL_MEMORY(data.data() + 16384 + 60, big + 110, 10);
    // Written blocks are never evicted: still there after the cache turns over.
    char c;
    for (int b = 0; b < 2; ++b) TEST_ASSERT_TRUE(f->readAt(b * hal::RangeFile::kBlock, &c, 1));
    TEST_ASSERT_TRUE(f->readAt(20000, buf, 5));
    TEST_ASSERT_EQUAL_MEMORY(w, buf, 5);
    // The last block is short; writing at its end works, past it does not.
    TEST_ASSERT_TRUE(f->writeAt(40000 - 4, w, 4));
    TEST_ASSERT_FALSE(f->writeAt(40000 - 4, w, 5));
    TEST_ASSERT_FALSE(f->writeAt(40001, w, 0));
    TEST_ASSERT_EQUAL(40000, f->size());  // files do not grow
}

static void test_a_write_needs_the_block_to_be_fetched() {
    hal::RangeStorage sd = make();
    hal::IFile* f = sd.open("/contrail/map.pak");
    failNext = true;
    TEST_ASSERT_FALSE(f->writeAt(10, "ab", 2));
    char buf[2];
    TEST_ASSERT_TRUE(f->readAt(10, buf, 2));
    TEST_ASSERT_EQUAL_MEMORY(data.data() + 10, buf, 2);  // the failed write left nothing behind
}

static void test_edge_reads() {
    hal::RangeStorage sd = make();
    hal::IFile* f = sd.open("/contrail/map.pak");
    char c;
    TEST_ASSERT_TRUE(f->readAt(0, &c, 0));
    TEST_ASSERT_TRUE(f->readAt(40000, &c, 0));  // at the end: nothing to read
    TEST_ASSERT_FALSE(f->readAt(40001, &c, 0));
    TEST_ASSERT_FALSE(f->readAt(40000, &c, 1));
    TEST_ASSERT_EQUAL(0, requests);
}

// The real consumer: MapPack opens the fixture through the storage and finds the pack valid and writable.
static void test_map_pack_opens_over_range_storage() {
    static std::string pak = loadFixture("map_small.pak");
    hal::RangeStorage sd({{"/contrail/map.pak", "sim/map-small.pak"}},
                         [](const std::string&) { return int64_t(pak.size()); },
                         [](const std::string&, uint32_t o, uint32_t n, void* out) {
                             if (uint64_t(o) + n > pak.size()) return false;
                             std::memcpy(out, pak.data() + o, n);
                             return true;
                         });
    basemap::MapPack p;
    TEST_ASSERT_TRUE(p.begin(&sd));
    TEST_ASSERT_EQUAL_STRING("", p.error());
    TEST_ASSERT_EQUAL(3, p.levelCount());
    TEST_ASSERT_EQUAL(0, p.damagedTiles());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_reads_across_blocks);
    RUN_TEST(test_blocks_are_cached);
    RUN_TEST(test_the_last_block_is_short);
    RUN_TEST(test_a_failed_request_fails_the_read_and_is_not_cached);
    RUN_TEST(test_missing_files);
    RUN_TEST(test_the_least_recently_used_block_is_the_one_dropped);
    RUN_TEST(test_writes_are_read_back);
    RUN_TEST(test_a_write_needs_the_block_to_be_fetched);
    RUN_TEST(test_edge_reads);
    RUN_TEST(test_map_pack_opens_over_range_storage);
    return UNITY_END();
}
