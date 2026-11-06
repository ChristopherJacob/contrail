// Feature decoding from a byte stream: the ring and line step encodings, points, truncation.
#include <unity.h>

#include <cstring>
#include <vector>

#include "TestStorage.h"
#include "map/Features.h"

void setUp() {}
void tearDown() {}

using namespace basemap;

static MemFile file(std::vector<uint8_t> b) {
    MemFile f;
    f.bytes = std::move(b);
    return f;
}

static void test_line_with_short_medium_and_long_steps() {
    // count 4, start (100, 200), steps (+5, -3) short, (+300, 0) medium, (+3000, -2500) long.
    std::vector<uint8_t> b = {4, 0, 100, 0, 200, 0, 5, 0xFD, 0x82};
    const uint32_t v = (300 & 0xFFF) | (0 & 0xFFF) << 12;
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
    b.push_back(uint8_t(v >> 16));
    b.push_back(0x80);
    b.push_back(uint8_t(3000 & 0xFF));
    b.push_back(uint8_t(3000 >> 8));
    b.push_back(uint8_t(-2500 & 0xFF));
    b.push_back(uint8_t((-2500 >> 8) & 0xFF));
    MemFile f = file(b);
    ByteStream s(f, 0, uint32_t(b.size()));
    LineReader r(s);
    TEST_ASSERT_EQUAL(4, r.count());
    int32_t x, y;
    TEST_ASSERT_TRUE(r.next(x, y));
    TEST_ASSERT_EQUAL(100, x);
    TEST_ASSERT_EQUAL(200, y);
    TEST_ASSERT_TRUE(r.next(x, y));
    TEST_ASSERT_EQUAL(105, x);
    TEST_ASSERT_EQUAL(197, y);
    TEST_ASSERT_TRUE(r.next(x, y));
    TEST_ASSERT_EQUAL(405, x);
    TEST_ASSERT_EQUAL(197, y);
    TEST_ASSERT_TRUE(r.next(x, y));
    TEST_ASSERT_EQUAL(3405, x);
    TEST_ASSERT_EQUAL(-2303, y);
    TEST_ASSERT_FALSE(r.next(x, y));
    TEST_ASSERT_TRUE(s.ok());
    TEST_ASSERT_EQUAL(b.size(), s.tell());
}

static void test_medium_step_sign_extends() {
    // Start (1000, 1000), step (-300, +2047) as a medium step: the int12 pair's signs survive.
    std::vector<uint8_t> b = {2, 0, 0xE8, 3, 0xE8, 3, 0x82};
    const uint32_t v = (uint32_t(-300) & 0xFFF) | (uint32_t(2047) & 0xFFF) << 12;
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
    b.push_back(uint8_t(v >> 16));
    MemFile f = file(b);
    ByteStream s(f, 0, uint32_t(b.size()));
    LineReader r(s);
    int32_t x, y;
    TEST_ASSERT_TRUE(r.next(x, y));
    TEST_ASSERT_TRUE(r.next(x, y));
    TEST_ASSERT_EQUAL(700, x);
    TEST_ASSERT_EQUAL(3047, y);
}

static void test_ring_reports_cuts_and_closes() {
    // 3 points, closing edge a cut (bit 15), the edge into point 2 a cut (0x81).
    std::vector<uint8_t> b = {3, 0x80, 0, 0, 0, 0, 10, 0, 0x81, 0, 10};
    MemFile f = file(b);
    ByteStream s(f, 0, uint32_t(b.size()));
    StreamRingReader r(s);
    RingPoint p;
    TEST_ASSERT_TRUE(r.next(p));
    TEST_ASSERT_FALSE(p.cut);
    TEST_ASSERT_TRUE(r.next(p));
    TEST_ASSERT_EQUAL(10, p.x);
    TEST_ASSERT_FALSE(p.cut);
    TEST_ASSERT_TRUE(r.next(p));
    TEST_ASSERT_EQUAL(10, p.y);
    TEST_ASSERT_TRUE(p.cut);
    TEST_ASSERT_TRUE(r.next(p));
    TEST_ASSERT_EQUAL(0, p.x);
    TEST_ASSERT_EQUAL(0, p.y);
    TEST_ASSERT_TRUE(p.cut);
    TEST_ASSERT_FALSE(r.next(p));
    TEST_ASSERT_TRUE(s.ok());
}

static void test_ring_matches_the_in_memory_reader() {
    // Every step form and a cut, read by both readers.
    std::vector<uint8_t> b = {5, 0, 10, 0, 20, 0, 3, 4, 0x82, 0x2C, 0x01, 0x00, 0x81, 0x80, 0xB8, 0x0B, 0x38, 0xFF,
                              0xFB, 0x02};
    MemFile f = file(b);
    ByteStream s(f, 0, uint32_t(b.size()));
    StreamRingReader a(s);
    RingReader m(b.data(), b.data() + b.size());
    RingPoint pa, pm;
    int n = 0;
    while (m.next(pm)) {
        TEST_ASSERT_TRUE(a.next(pa));
        TEST_ASSERT_EQUAL(pm.x, pa.x);
        TEST_ASSERT_EQUAL(pm.y, pa.y);
        TEST_ASSERT_EQUAL(pm.cut, pa.cut);
        ++n;
    }
    TEST_ASSERT_EQUAL(6, n);
    TEST_ASSERT_FALSE(a.next(pa));
    TEST_ASSERT_EQUAL(m.end() - b.data(), s.tell());
}

static void test_point_name_is_bounded_and_terminated() {
    std::vector<uint8_t> b = {7, 0, 9, 0, 5, 'K', 'F', 'D', 'K', 'X'};
    MemFile f = file(b);
    ByteStream s(f, 0, uint32_t(b.size()));
    Point p;
    TEST_ASSERT_TRUE(readPoint(s, p));
    TEST_ASSERT_EQUAL(7, p.x);
    TEST_ASSERT_EQUAL(9, p.y);
    TEST_ASSERT_EQUAL_STRING("KFDKX", p.name);
    // A name longer than the buffer is cut, not overrun; the stream still moves past all of it.
    std::vector<uint8_t> c = {1, 0, 2, 0, 30};
    for (int i = 0; i < 30; ++i) c.push_back(uint8_t('a' + i % 26));
    MemFile g = file(c);
    ByteStream t(g, 0, uint32_t(c.size()));
    TEST_ASSERT_TRUE(readPoint(t, p));
    TEST_ASSERT_EQUAL(20, int(std::strlen(p.name)));
    TEST_ASSERT_EQUAL(c.size(), t.tell());
}

static void test_truncated_data_stops_cleanly() {
    std::vector<uint8_t> b = {5, 0, 1, 0, 1, 0, 2, 2};  // promises 5 points, holds 2
    MemFile f = file(b);
    ByteStream s(f, 0, uint32_t(b.size()));
    LineReader r(s);
    int32_t x, y;
    int n = 0;
    while (r.next(x, y)) ++n;
    TEST_ASSERT_EQUAL(2, n);
    TEST_ASSERT_FALSE(s.ok());
}

static void test_failed_reads_stop_cleanly() {
    std::vector<uint8_t> b(100, 0);
    MemFile f = file(b);
    f.fail = true;
    ByteStream s(f, 0, 100);
    TEST_ASSERT_EQUAL(0, s.u8());
    TEST_ASSERT_FALSE(s.ok());
}

static void test_stream_crosses_its_buffer_and_seeks() {
    std::vector<uint8_t> b(100);
    for (int i = 0; i < 100; ++i) b[i] = uint8_t(i);
    MemFile f = file(b);
    ByteStream s(f, 10, 90);
    for (int i = 10; i < 90; ++i) TEST_ASSERT_EQUAL(i, s.u8());
    TEST_ASSERT_TRUE(s.ok());
    TEST_ASSERT_EQUAL(90, s.tell());
    TEST_ASSERT_EQUAL(0, s.u8());  // past the end
    TEST_ASSERT_FALSE(s.ok());
    ByteStream t(f, 0, 100);
    TEST_ASSERT_EQUAL(0, t.u8());
    t.seek(50);
    TEST_ASSERT_EQUAL(50, t.tell());
    TEST_ASSERT_EQUAL(50, t.u8());
    TEST_ASSERT_EQUAL(51, t.tell());
    TEST_ASSERT_EQUAL(0x3433, t.u16());
    // A seek outside [begin, end) fails the stream rather than reading elsewhere.
    ByteStream u(f, 20, 40);
    u.seek(10);
    TEST_ASSERT_FALSE(u.ok());
    TEST_ASSERT_EQUAL(0, u.u8());
    ByteStream v(f, 20, 40);
    v.seek(45);
    TEST_ASSERT_FALSE(v.ok());
    ByteStream w(f, 20, 40);
    w.seek(30);
    TEST_ASSERT_TRUE(w.ok());
    TEST_ASSERT_EQUAL(30, w.u8());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_line_with_short_medium_and_long_steps);
    RUN_TEST(test_medium_step_sign_extends);
    RUN_TEST(test_ring_reports_cuts_and_closes);
    RUN_TEST(test_ring_matches_the_in_memory_reader);
    RUN_TEST(test_point_name_is_bounded_and_terminated);
    RUN_TEST(test_truncated_data_stops_cleanly);
    RUN_TEST(test_failed_reads_stop_cleanly);
    RUN_TEST(test_stream_crosses_its_buffer_and_seeks);
    return UNITY_END();
}
