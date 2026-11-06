#include <unity.h>

#include <cstring>
#include <memory>
#include <string>

#include "Fixture.h"
#include "TestStorage.h"
#include "assets/LogoStore.h"
#include "services/Log.h"

void setUp() {}
void tearDown() {}

static std::unique_ptr<MemStorage> withFixture() {
    auto st = std::make_unique<MemStorage>();
    st->add(assets::LogoStore::kPath, loadFixture("logos_tiny.pak"));
    return st;
}

static uint32_t rd32(const std::string& s, size_t at) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= uint32_t(uint8_t(s[at + i])) << (8 * i);
    return v;
}

// Where `key`'s `side` px size descriptor and block sit in the pack, found through its index (so the tests
// do not depend on the fixture's layout).
struct SizeAt {
    size_t descriptor = 0;  // file offset of the 8-byte size descriptor
    size_t block = 0;       // file offset of its block (palette, pixels, CRC)
    int colours = 0;
    uint32_t itemSize = 0;  // the item's size, from the index
};
static SizeAt sizeAt(const std::string& pack, const char* key, int side) {
    const uint32_t count = rd32(pack, 8), index = rd32(pack, 12);
    for (uint32_t i = 0; i < count; ++i) {
        const size_t e = index + 12 * i;
        if (std::strncmp(pack.data() + e, key, 4) != 0) continue;
        const uint32_t item = rd32(pack, e + 4);
        const int n = uint8_t(pack[item]);
        for (int k = 0; k < n; ++k) {
            const size_t d = item + 1 + 8 * k;
            if (uint8_t(pack[d]) == side)
                return SizeAt{d, item + rd32(pack, d + 4), uint8_t(pack[d + 3]), rd32(pack, e + 8)};
        }
    }
    TEST_FAIL_MESSAGE("size not in the fixture");
    return {};
}

static void test_fixture_from_build_pack_opens() {
    auto st = withFixture();
    assets::LogoStore logos;
    logos.begin(st.get());
    TEST_ASSERT_TRUE(logos.mounted());
    TEST_ASSERT_EQUAL(3, logos.logoCount());
    TEST_ASSERT_EQUAL_STRING("", logos.error());
}

static void test_all_three_sizes_decode() {
    auto st = withFixture();
    assets::LogoStore logos;
    logos.begin(st.get());
    static gfx::Badge40 b40;
    static gfx::Badge30 b30;
    static gfx::Badge25 b25;
    TEST_ASSERT_TRUE(logos.load("UAL", b40));
    TEST_ASSERT_EQUAL(40, b40.badge.w);
    TEST_ASSERT_EQUAL(8, b40.badge.bpp);
    TEST_ASSERT_TRUE(b40.badge.colours >= 2 && b40.badge.colours <= 128);
    TEST_ASSERT_TRUE(logos.load("DAL", b30));
    TEST_ASSERT_EQUAL(30, b30.badge.w);
    TEST_ASSERT_TRUE(logos.load("UAL", b25));
    TEST_ASSERT_EQUAL(25, b25.badge.w);
    TEST_ASSERT_EQUAL(4, b25.badge.bpp);
    TEST_ASSERT_TRUE(b25.badge.colours <= 16);
    // Rounded corners: the corner pixel's palette entry is transparent; the centre's opaque.
    auto alphaAt = [](const gfx::Badge& b, int x, int y) {
        int rowBytes = (b.w * b.bpp + 7) / 8;
        uint8_t v = b.px[y * rowBytes + (b.bpp == 8 ? x : x / 2)];
        uint8_t idx = b.bpp == 8 ? v : (x & 1 ? v & 0x0F : v >> 4);
        return b.alpha[idx];
    };
    TEST_ASSERT_EQUAL(0, alphaAt(b40.badge, 0, 0));
    TEST_ASSERT_EQUAL(255, alphaAt(b40.badge, 20, 20));
    TEST_ASSERT_EQUAL(0, alphaAt(b25.badge, 24, 24));
}

static void test_missing_airline_and_no_pack() {
    auto st = withFixture();
    assets::LogoStore logos;
    logos.begin(st.get());
    static gfx::Badge40 b;
    TEST_ASSERT_FALSE(logos.load("DLH", b));
    TEST_ASSERT_TRUE(b.badge.empty());
    TEST_ASSERT_EQUAL_STRING("", logos.error());  // an unknown airline is not a read failure
    TEST_ASSERT_TRUE(logos.load("UAL", b));
    MemStorage empty;
    assets::LogoStore none;
    none.begin(&empty);
    TEST_ASSERT_TRUE(none.mounted());
    TEST_ASSERT_EQUAL(-1, none.logoCount());
    TEST_ASSERT_EQUAL_STRING("no logos.pak", none.error());
    TEST_ASSERT_FALSE(none.load("UAL", b));
    assets::LogoStore noCard;
    noCard.begin(nullptr);
    TEST_ASSERT_FALSE(noCard.mounted());
    TEST_ASSERT_EQUAL_STRING("", noCard.error());
}

// begin() again forgets the earlier pack: a card without logos.pak reads as having none.
static void test_begin_resets_the_pack() {
    auto st = withFixture();
    assets::LogoStore logos;
    logos.begin(st.get());
    TEST_ASSERT_EQUAL(3, logos.logoCount());
    MemStorage empty;
    logos.begin(&empty);
    TEST_ASSERT_EQUAL(-1, logos.logoCount());
    TEST_ASSERT_EQUAL_STRING("no logos.pak", logos.error());
    static gfx::Badge40 b;
    TEST_ASSERT_FALSE(logos.load("UAL", b));
    TEST_ASSERT_TRUE(b.badge.empty());
}

static void test_refused_pack_names_the_reason() {
    MemStorage st;
    std::string s = loadFixture("logos_tiny.pak");
    s[4] = 2;  // version 2
    st.add(assets::LogoStore::kPath, s);
    assets::LogoStore logos;
    logos.begin(&st);
    TEST_ASSERT_EQUAL_STRING("logos.pak is a newer version; update the firmware", logos.error());
    s[4] = 0;  // version 0
    st.add(assets::LogoStore::kPath, s);
    logos.begin(&st);
    TEST_ASSERT_EQUAL_STRING("logos.pak is an older version; rebuild it", logos.error());
}

static void test_crc_failure_reads_as_absent() {
    MemStorage st;
    std::string s = loadFixture("logos_tiny.pak");
    const SizeAt at = sizeAt(s, "UAL", 25);
    s[at.block + 3 * at.colours + 10] ^= 0x55;  // a pixel byte of UAL's 25 px block
    st.add(assets::LogoStore::kPath, s);
    assets::LogoStore logos;
    logos.begin(&st);
    static gfx::Badge25 b25;
    static gfx::Badge40 b40;
    TEST_ASSERT_FALSE(logos.load("UAL", b25));
    TEST_ASSERT_TRUE(b25.badge.empty());
    TEST_ASSERT_TRUE(logos.load("UAL", b40));  // its other sizes are intact
    TEST_ASSERT_TRUE(logos.load("DAL", b25));
}

static int gCheckLogs = 0;
static void countCheckLogs(const char* line) {
    if (std::strstr(line, "failed their checks")) ++gCheckLogs;
}

static void test_check_failure_logged_once_per_begin() {
    MemStorage st;
    std::string s = loadFixture("logos_tiny.pak");
    const SizeAt at = sizeAt(s, "UAL", 25);
    s[at.block + 3 * at.colours + 10] ^= 0x55;
    st.add(assets::LogoStore::kPath, s);
    assets::LogoStore logos;
    svc::logSink() = countCheckLogs;
    gCheckLogs = 0;
    static gfx::Badge25 b25;
    logos.begin(&st);
    TEST_ASSERT_FALSE(logos.load("UAL", b25));
    TEST_ASSERT_FALSE(logos.load("UAL", b25));
    TEST_ASSERT_EQUAL(1, gCheckLogs);  // once, however many loads fail
    logos.begin(&st);  // a fresh start logs again
    TEST_ASSERT_FALSE(logos.load("UAL", b25));
    svc::logSink() = nullptr;
    TEST_ASSERT_EQUAL(2, gCheckLogs);
}

static void test_header_with_too_many_colours_is_refused() {
    // A descriptor claiming more colours than the store holds must be refused before the palette is read,
    // or the palette loop would write past pal[] / alpha[].
    for (int side : {25, 40}) {
        MemStorage st;
        std::string s = loadFixture("logos_tiny.pak");
        const SizeAt at = sizeAt(s, "UAL", side);
        s[at.descriptor + 3] = char(side == 25 ? 17 : 129);
        MemFile& f = st.add(assets::LogoStore::kPath, s);
        assets::LogoStore logos;
        logos.begin(&st);
        static gfx::Badge25 b25;
        static gfx::Badge40 b40;
        const int before = f.reads;
        TEST_ASSERT_FALSE(side == 25 ? logos.load("UAL", b25) : logos.load("UAL", b40));
        // find (at most 2 index reads for a 2-entry pack) and the two header reads; nothing from the block.
        TEST_ASSERT_LESS_OR_EQUAL(4, f.reads - before);
        TEST_ASSERT_EQUAL_STRING("", logos.error());
    }
}

static void test_wrong_side_or_bpp_is_not_used() {
    MemStorage st;
    std::string s = loadFixture("logos_tiny.pak");
    const SizeAt at = sizeAt(s, "UAL", 25);
    s[at.descriptor + 2] = 8;  // the 25 px block now claims 8 bpp: no 25 px x 4 bpp size remains
    st.add(assets::LogoStore::kPath, s);
    assets::LogoStore logos;
    logos.begin(&st);
    static gfx::Badge25 b25;
    TEST_ASSERT_FALSE(logos.load("UAL", b25));
}

static void test_block_past_the_item_is_refused() {
    MemStorage st;
    std::string s = loadFixture("logos_tiny.pak");
    const SizeAt at = sizeAt(s, "UAL", 40);
    s[at.descriptor + 4] = char(0xF0), s[at.descriptor + 5] = char(0xFF);  // the block offset past the item
    st.add(assets::LogoStore::kPath, s);
    assets::LogoStore logos;
    logos.begin(&st);
    static gfx::Badge40 b40;
    TEST_ASSERT_FALSE(logos.load("UAL", b40));
    TEST_ASSERT_EQUAL_STRING("", logos.error());
}

static void test_block_running_past_the_item_end_is_refused() {
    // The offset is inside the item, but palette + pixels + CRC from there would run past the item's end.
    MemStorage st;
    std::string s = loadFixture("logos_tiny.pak");
    const SizeAt at = sizeAt(s, "UAL", 40);
    const uint32_t inside = at.itemSize - 4;
    for (int i = 0; i < 4; ++i) s[at.descriptor + 4 + i] = char(inside >> (8 * i));
    MemFile& f = st.add(assets::LogoStore::kPath, s);
    assets::LogoStore logos;
    logos.begin(&st);
    static gfx::Badge40 b40;
    const int before = f.reads;
    TEST_ASSERT_FALSE(logos.load("UAL", b40));
    TEST_ASSERT_TRUE(b40.badge.empty());
    TEST_ASSERT_LESS_OR_EQUAL(4, f.reads - before);  // refused from the header, before any block read
    TEST_ASSERT_EQUAL_STRING("", logos.error());
    static gfx::Badge30 b30;
    TEST_ASSERT_TRUE(logos.load("UAL", b30));  // its other sizes are intact
}

static void test_card_pulled_stops_reading() {
    auto st = withFixture();
    assets::LogoStore logos;
    logos.begin(st.get());
    MemFile* f = st->files[assets::LogoStore::kPath].get();
    f->fail = true;
    static gfx::Badge40 b;
    TEST_ASSERT_FALSE(logos.load("UAL", b));
    int reads = f->reads;
    TEST_ASSERT_FALSE(logos.load("DAL", b));
    TEST_ASSERT_EQUAL(reads, f->reads);  // no further reads
    TEST_ASSERT_EQUAL(-1, logos.logoCount());
    TEST_ASSERT_EQUAL_STRING("SD card read failed", logos.error());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_fixture_from_build_pack_opens);
    RUN_TEST(test_all_three_sizes_decode);
    RUN_TEST(test_check_failure_logged_once_per_begin);
    RUN_TEST(test_missing_airline_and_no_pack);
    RUN_TEST(test_begin_resets_the_pack);
    RUN_TEST(test_refused_pack_names_the_reason);
    RUN_TEST(test_crc_failure_reads_as_absent);
    RUN_TEST(test_header_with_too_many_colours_is_refused);
    RUN_TEST(test_wrong_side_or_bpp_is_not_used);
    RUN_TEST(test_block_past_the_item_is_refused);
    RUN_TEST(test_block_running_past_the_item_end_is_refused);
    RUN_TEST(test_card_pulled_stops_reading);
    return UNITY_END();
}
