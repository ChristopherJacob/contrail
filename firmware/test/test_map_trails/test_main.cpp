#include <unity.h>

#include <algorithm>

#include "screens/MapTrails.h"

using scr::MapTrails;

void setUp() {}
void tearDown() {}

// n flights (hex 1..n) due east of home at `dist` nm.
static scr::Scene at(int64_t epoch, float dist, int n = 1) {
    scr::Scene s;
    s.timeValid = s.locationSet = true;
    s.epoch = epoch;
    s.count = n;
    for (int i = 0; i < n; ++i) s.flights[i].hex = uint32_t(i + 1), s.flights[i].distNm = dist, s.flights[i].bearingDeg = 90;
    return s;
}

// The first record samples every plane; then each takes a point every 10 s, in the second its phase says.
static int64_t nextDue(int64_t after, uint32_t hex) {
    int64_t e = after + 1;
    while ((e - MapTrails::phaseOf(hex)) % MapTrails::kEveryS) ++e;
    return e;
}

static void test_samples_every_ten_seconds() {
    MapTrails t;
    t.record(at(1000, 5));
    const int64_t due = nextDue(1000, 1);
    t.record(at(due - 1, 6));  // too soon
    TEST_ASSERT_EQUAL(1, t.find(1)->count);
    t.record(at(due, 7));
    TEST_ASSERT_EQUAL(2, t.find(1)->count);
    t.record(at(due + 9, 8));  // too soon again
    TEST_ASSERT_EQUAL(2, t.find(1)->count);
    float e, n;
    t.find(1)->at(1, e, n);
    TEST_ASSERT_FLOAT_WITHIN(0.07f, 7.f, e);
    TEST_ASSERT_FLOAT_WITHIN(0.07f, 0.f, n);
}

static void test_keeps_the_last_eight() {
    MapTrails t;
    for (int k = 0; k < 12; ++k) t.record(at(1000 + 10 * k, float(1 + k)));
    const MapTrails::Track* tr = t.find(1);
    TEST_ASSERT_EQUAL(MapTrails::kPoints, tr->count);
    float e, n;
    tr->at(0, e, n);
    TEST_ASSERT_FLOAT_WITHIN(0.07f, 5.f, e);  // samples 0..3 have rolled off
    tr->at(7, e, n);
    TEST_ASSERT_FLOAT_WITHIN(0.07f, 12.f, e);
}

static void test_planes_that_leave_lose_their_trail() {
    MapTrails t;
    t.record(at(1000, 5, 2));
    t.record(at(1010, 5, 1));
    TEST_ASSERT_NULL(t.find(2));
    t.record(at(1020, 5, 2));
    TEST_ASSERT_EQUAL(1, t.find(2)->count);
    TEST_ASSERT_EQUAL(3, t.find(1)->count);
}

static void test_clock_going_back_or_missing() {
    MapTrails t;
    t.record(at(1000, 5));
    t.record(at(900, 5));  // the clock jumped back: start again
    TEST_ASSERT_EQUAL(1, t.find(1)->count);
    scr::Scene s = at(2000, 5);
    s.timeValid = false;
    MapTrails u;
    u.record(s);
    TEST_ASSERT_NULL(u.find(1));
}

static void test_home_moving_clears_trails() {
    MapTrails t;
    t.record(at(1000, 5));
    t.record(at(1010, 6));
    TEST_ASSERT_EQUAL(2, t.find(1)->count);
    scr::Scene s = at(1020, 7);
    s.homeLat += 0.5;
    t.record(s);
    TEST_ASSERT_EQUAL(1, t.find(1)->count);  // started again from the new home
}

// Each plane steps once every 10 s at a phase from its code, so a second's frames redraw about a tenth of the trails
// rather than all of them at once. Points stay 10 s apart.
static void test_planes_step_at_their_own_phase() {
    MapTrails t;
    constexpr int kN = 30;
    scr::Scene s = at(1000, 5, kN);
    for (int i = 0; i < kN; ++i) s.flights[i].hex = 0xA00000u + 0x2F1Bu * uint32_t(i);  // ICAO-like codes
    t.record(s);
    int phases[MapTrails::kEveryS] = {};
    for (int i = 0; i < kN; ++i) ++phases[MapTrails::phaseOf(s.flights[i].hex)];
    const int most = *std::max_element(phases, phases + MapTrails::kEveryS);
    TEST_ASSERT_TRUE_MESSAGE(most <= kN / 10 + 3, "phases bunch up");
    uint8_t counts[kN];
    for (int i = 0; i < kN; ++i) counts[i] = t.find(s.flights[i].hex)->count;
    int maxAtOnce = 0;
    for (int64_t e = 1001; e <= 1060; ++e) {
        for (int i = 0; i < kN; ++i) s.flights[i].distNm = 5 + float(e - 1000) / 10;  // 1 nm every 10 s
        s.epoch = e;
        t.record(s);
        t.record(s);  // another rebuild in the same second: nothing more
        int now = 0;
        for (int i = 0; i < kN; ++i) {
            const MapTrails::Track* tr = t.find(s.flights[i].hex);
            const bool due = (e - MapTrails::phaseOf(s.flights[i].hex)) % MapTrails::kEveryS == 0;
            TEST_ASSERT_EQUAL(counts[i] + (due ? 1 : 0), tr->count);
            now += due, counts[i] = tr->count;
        }
        maxAtOnce = std::max(maxAtOnce, now);
    }
    TEST_ASSERT_EQUAL(most, maxAtOnce);
    for (int i = 0; i < kN; ++i) TEST_ASSERT_EQUAL(7, counts[i]);  // the first point and six more
    float e0, n0, e1, n1;
    const MapTrails::Track* tr = t.find(s.flights[3].hex);
    tr->at(1, e0, n0), tr->at(2, e1, n1);
    TEST_ASSERT_FLOAT_WITHIN(0.07f, 1.f, e1 - e0);  // 10 s apart
}

// A code whose phase is `ph`, from `from` up.
static uint32_t withPhase(int ph, uint32_t from) {
    while (MapTrails::phaseOf(from) != ph) ++from;
    return from;
}

// With all 32 slots taken, a plane that left keeps its slot only until a due arrival needs it: A (phase 1) leaves at
// second 1002, B (phase 9) arrives at 1009, before A's phase comes round again, and still gets its point.
static void test_an_arrival_takes_a_departed_planes_slot() {
    MapTrails t;
    constexpr int kN = scr::Scene::kMaxFlights;
    scr::Scene s = at(1000, 5, kN);
    const uint32_t a = withPhase(1, 0x100000), b = withPhase(9, 0x200000);
    for (int i = 0; i < kN; ++i) s.flights[i].hex = i == 0 ? a : 0x300000u + 977u * uint32_t(i);
    t.record(s);
    s.epoch = 1001;  // A's second: it takes its point
    t.record(s);
    TEST_ASSERT_EQUAL(2, t.find(a)->count);
    s.flights[0] = s.flights[kN - 1], --s.count;  // A leaves
    s.epoch = 1002;
    t.record(s);
    TEST_ASSERT_NOT_NULL(t.find(a));  // not due yet: its slot is still taken
    s.flights[s.count] = s.flights[0], s.flights[s.count].hex = b, ++s.count;  // B arrives
    s.epoch = 1009;
    t.record(s);
    TEST_ASSERT_NOT_NULL_MESSAGE(t.find(b), "the arrival lost its first point");
    TEST_ASSERT_EQUAL(1, t.find(b)->count);
    TEST_ASSERT_NULL(t.find(a));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_samples_every_ten_seconds);
    RUN_TEST(test_keeps_the_last_eight);
    RUN_TEST(test_planes_that_leave_lose_their_trail);
    RUN_TEST(test_clock_going_back_or_missing);
    RUN_TEST(test_home_moving_clears_trails);
    RUN_TEST(test_planes_step_at_their_own_phase);
    RUN_TEST(test_an_arrival_takes_a_departed_planes_slot);
    return UNITY_END();
}
