// NativeStorage's files: reads, and writeAt inside the file (the sim's saved chart). The env ignores
// hal_native (it needs SDL only for the display), so this suite compiles the storage source itself.
#include <unity.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "hal.h"
#include "../../lib/hal_native/NativeStorage.cpp"

void setUp() {}
void tearDown() {}

static std::string makeDir() {
    char tmpl[] = "/tmp/contrail_native_storage_XXXXXX";
    TEST_ASSERT_NOT_NULL(mkdtemp(tmpl));
    return tmpl;
}

static void put(const std::string& path, const std::string& data) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    TEST_ASSERT_NOT_NULL(f);
    std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
}

static void test_write_inside_the_file_only() {
    const std::string dir = makeDir();
    put(dir + "/a.bin", std::string(100, 'x'));
    {
        hal::NativeStorage st(dir);
        hal::IFile* f = st.open("/a.bin");
        TEST_ASSERT_NOT_NULL(f);
        TEST_ASSERT_EQUAL(100, f->size());
        TEST_ASSERT_TRUE(f->writeAt(10, "hello", 5));
        char back[5];
        TEST_ASSERT_TRUE(f->readAt(10, back, 5));
        TEST_ASSERT_EQUAL_MEMORY("hello", back, 5);
        TEST_ASSERT_TRUE(f->writeAt(95, "world", 5));  // up to the last byte
        TEST_ASSERT_FALSE(f->writeAt(96, "world", 5));  // files do not grow
        TEST_ASSERT_FALSE(f->writeAt(101, "x", 1));
        TEST_ASSERT_EQUAL(100, f->size());
    }
    // It reached the file (flushed), and nothing else changed.
    std::FILE* in = std::fopen((dir + "/a.bin").c_str(), "rb");
    char all[101] = {};
    TEST_ASSERT_EQUAL(100, std::fread(all, 1, 101, in));
    std::fclose(in);
    std::string want(100, 'x');
    want.replace(10, 5, "hello"), want.replace(95, 5, "world");
    TEST_ASSERT_EQUAL_STRING(want.c_str(), all);
    std::remove((dir + "/a.bin").c_str());
    rmdir(dir.c_str());
}

static void test_a_read_only_file_opens_but_will_not_write() {
    const std::string dir = makeDir();
    put(dir + "/r.bin", "abcdef");
    chmod((dir + "/r.bin").c_str(), 0444);
    {
        hal::NativeStorage st(dir);
        hal::IFile* f = st.open("/r.bin");
        if (f) {  // as root the file still opens read-write: nothing to check then
            char b[6];
            TEST_ASSERT_TRUE(f->readAt(0, b, 6));
            if (geteuid() != 0) TEST_ASSERT_FALSE(f->writeAt(0, "x", 1));
        }
    }
    chmod((dir + "/r.bin").c_str(), 0644);
    std::remove((dir + "/r.bin").c_str());
    rmdir(dir.c_str());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_write_inside_the_file_only);
    RUN_TEST(test_a_read_only_file_opens_but_will_not_write);
    return UNITY_END();
}
