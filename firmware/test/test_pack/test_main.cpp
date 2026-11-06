#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include "TestStorage.h"
#include "assets/Pack.h"

void setUp() {}
void tearDown() {}

static void put16(std::string& s, uint16_t v) { s += char(v & 0xFF), s += char(v >> 8); }
static void put32(std::string& s, uint32_t v) {
    for (int i = 0; i < 4; ++i) s += char((v >> (8 * i)) & 0xFF);
}

// A pack with items keyed "AAL", "BAW", ... each holding its key repeated.
static std::string makePack(const std::vector<std::string>& keys, uint16_t version = 1, uint16_t kind = 1) {
    std::string head = "CPAK", index, data;
    put16(head, version), put16(head, kind), put32(head, uint32_t(keys.size())), put32(head, 32);
    const uint32_t dataOff = 32 + 12 * uint32_t(keys.size());
    put32(head, dataOff), put32(head, 0), head += std::string(8, '\0');
    uint32_t off = dataOff;
    for (const auto& k : keys) {
        std::string item = k + k;
        index += k + std::string(4 - k.size(), '\0');
        put32(index, off), put32(index, uint32_t(item.size()));
        data += item, off += uint32_t(item.size());
    }
    return head + index + data;
}

static void test_crc16_check_value() {
    const char* s = "123456789";
    TEST_ASSERT_EQUAL_HEX16(0x29B1, assets::crc16(reinterpret_cast<const uint8_t*>(s), 9));
}

static void test_open_and_find_first_last_missing() {
    MemStorage st;
    MemFile& f = st.add("/p", makePack({"AAL", "BAW", "DAL", "UAL", "ZZZZ"}));
    assets::Pack p;
    TEST_ASSERT_TRUE(p.open(&f, 1));
    TEST_ASSERT_EQUAL(5u, p.count());
    uint32_t off, size;
    char buf[8] = {};
    TEST_ASSERT_TRUE(p.find("AAL", off, size));
    TEST_ASSERT_EQUAL(6u, size);
    TEST_ASSERT_TRUE(p.read(off, buf, size));
    TEST_ASSERT_EQUAL_STRING("AALAAL", buf);
    TEST_ASSERT_TRUE(p.find("ZZZZ", off, size));
    TEST_ASSERT_TRUE(p.find("DAL", off, size));
    TEST_ASSERT_FALSE(p.find("DLH", off, size));
    TEST_ASSERT_FALSE(p.readFailed());  // absent, not unreadable
    TEST_ASSERT_FALSE(p.find("A", off, size));
    TEST_ASSERT_FALSE(p.find("", off, size));
    TEST_ASSERT_FALSE(p.find("UALX1", off, size));  // longer than a key: never matches "UAL"
}

static void test_huge_count_and_low_index_refused() {
    MemStorage st;
    assets::Pack p;
    std::string s = makePack({"AAL", "BAW"});
    std::string huge = s;
    for (int i = 0; i < 4; ++i) huge[8 + i] = char(0xFF);  // count 0xFFFFFFFF, indexOffset still 32
    TEST_ASSERT_FALSE(p.open(&st.add("/h", huge), 1));
    TEST_ASSERT_EQUAL(int(assets::PackError::Truncated), int(p.error()));
    std::string low = s;
    low[12] = 16;  // indexOffset 16: inside the header
    TEST_ASSERT_FALSE(p.open(&st.add("/l", low), 1));
    TEST_ASSERT_EQUAL(int(assets::PackError::Truncated), int(p.error()));
}

static void test_failed_read_during_find() {
    MemStorage st;
    MemFile& f = st.add("/p", makePack({"AAL", "BAW", "DAL"}));
    assets::Pack p;
    TEST_ASSERT_TRUE(p.open(&f, 1));
    f.fail = true;
    uint32_t off = 0, size = 0;
    TEST_ASSERT_FALSE(p.find("BAW", off, size));
    TEST_ASSERT_TRUE(p.readFailed());
}

static void test_refusals() {
    MemStorage st;
    assets::Pack p;
    TEST_ASSERT_FALSE(p.open(nullptr, 1));
    TEST_ASSERT_EQUAL(int(assets::PackError::NotOpen), int(p.error()));
    std::string bad = makePack({"AAL"});
    bad[0] = 'X';
    TEST_ASSERT_FALSE(p.open(&st.add("/a", bad), 1));
    TEST_ASSERT_EQUAL(int(assets::PackError::BadMagic), int(p.error()));
    TEST_ASSERT_FALSE(p.open(&st.add("/b", makePack({"AAL"}, 2)), 1));
    TEST_ASSERT_EQUAL(int(assets::PackError::BadVersion), int(p.error()));
    TEST_ASSERT_EQUAL(2, p.version());
    TEST_ASSERT_FALSE(p.open(&st.add("/c", makePack({"AAL"}, 1, 2)), 1));
    TEST_ASSERT_EQUAL(int(assets::PackError::BadKind), int(p.error()));
    std::string cut = makePack({"AAL", "BAW", "DAL"});
    cut.resize(40);  // the index runs past the end
    TEST_ASSERT_FALSE(p.open(&st.add("/d", cut), 1));
    TEST_ASSERT_EQUAL(int(assets::PackError::Truncated), int(p.error()));
    TEST_ASSERT_FALSE(p.open(&st.add("/e", "CPA"), 1));
    TEST_ASSERT_EQUAL(int(assets::PackError::Truncated), int(p.error()));
    TEST_ASSERT_FALSE(p.isOpen());
}

static void test_item_out_of_bounds_reads_as_absent() {
    MemStorage st;
    std::string s = makePack({"AAL", "BAW"});
    s.resize(s.size() - 3);  // BAW's item now runs past the end
    assets::Pack p;
    TEST_ASSERT_TRUE(p.open(&st.add("/p", s), 1));
    uint32_t off, size;
    TEST_ASSERT_TRUE(p.find("AAL", off, size));
    TEST_ASSERT_FALSE(p.find("BAW", off, size));
    TEST_ASSERT_FALSE(p.readFailed());  // a bad item is absent; the card still reads
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_crc16_check_value);
    RUN_TEST(test_open_and_find_first_last_missing);
    RUN_TEST(test_refusals);
    RUN_TEST(test_huge_count_and_low_index_refused);
    RUN_TEST(test_failed_read_during_find);
    RUN_TEST(test_item_out_of_bounds_reads_as_absent);
    return UNITY_END();
}
