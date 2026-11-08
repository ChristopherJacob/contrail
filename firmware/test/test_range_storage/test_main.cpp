// The browser simulator's card (hal_wasm/RangeStorage): files read from the site in cached blocks by Range requests.
// The env ignores hal_wasm, so this suite compiles the source itself.
#include <unity.h>

#include <map>
#include <string>

#include "hal.h"  // names the hal lib for the dependency finder, which does not follow the include below
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
    TEST_ASSERT_FALSE(sd.writable());
    TEST_ASSERT_NULL(sd.open("/contrail/logos.pak"));  // no URL: no release's logos
    TEST_ASSERT_NULL(sd.open("/contrail/other.pak"));
    TEST_ASSERT_EQUAL_PTR(sd.open("/contrail/map.pak"), sd.open("/contrail/map.pak"));
}

static void test_the_cache_is_bounded() {
    hal::RangeStorage sd = make();
    data.resize(hal::RangeFile::kBlock * (hal::RangeFile::kBlocks + 2));
    hal::IFile* f = sd.open("/contrail/map.pak");
    char c;
    for (int b = 0; b < hal::RangeFile::kBlocks + 2; ++b) TEST_ASSERT_TRUE(f->readAt(b * hal::RangeFile::kBlock, &c, 1));
    requests = 0;
    TEST_ASSERT_TRUE(f->readAt(0, &c, 1));  // block 0 was the least recently used: fetched again
    TEST_ASSERT_EQUAL(1, requests);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_reads_across_blocks);
    RUN_TEST(test_blocks_are_cached);
    RUN_TEST(test_the_last_block_is_short);
    RUN_TEST(test_a_failed_request_fails_the_read_and_is_not_cached);
    RUN_TEST(test_missing_files);
    RUN_TEST(test_the_cache_is_bounded);
    return UNITY_END();
}
