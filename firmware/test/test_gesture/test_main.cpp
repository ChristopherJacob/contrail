#include <unity.h>

#include "screens/Gesture.h"

using scr::Gesture;
using scr::GestureDetector;

void setUp() {}
void tearDown() {}

// Feeds a straight press from (x0,y0) to (x1,y1) over `ms`, sampled every 20 ms.
static scr::GestureEvent stroke(GestureDetector& g, int x0, int y0, int x1, int y1, uint32_t ms, uint32_t t0 = 1000) {
    scr::GestureEvent ev{};
    for (uint32_t t = 0; t <= ms; t += 20) {
        int x = x0 + int((x1 - x0) * float(t) / ms), y = y0 + int((y1 - y0) * float(t) / ms);
        ev = g.feed(true, x, y, t0 + t);
        TEST_ASSERT_EQUAL(int(Gesture::None), int(ev.gesture));
    }
    return g.feed(false, 0, 0, t0 + ms + 20);
}

static void test_tap() {
    GestureDetector g;
    scr::GestureEvent ev = stroke(g, 100, 200, 103, 202, 120);
    TEST_ASSERT_EQUAL(int(Gesture::Tap), int(ev.gesture));
    TEST_ASSERT_EQUAL(100, ev.x);
    TEST_ASSERT_EQUAL(200, ev.y);
}

static void test_single_noise_sample_is_not_a_tap() {
    GestureDetector g;
    g.feed(true, 50, 50, 1000);
    TEST_ASSERT_EQUAL(int(Gesture::None), int(g.feed(false, 0, 0, 1033).gesture));
}

static void test_swipes() {
    GestureDetector g;
    TEST_ASSERT_EQUAL(int(Gesture::SwipeLeft), int(stroke(g, 200, 160, 60, 170, 250).gesture));
    TEST_ASSERT_EQUAL(int(Gesture::SwipeRight), int(stroke(g, 40, 160, 190, 150, 250, 5000).gesture));
}

static void test_vertical_swipes() {
    GestureDetector g;
    TEST_ASSERT_EQUAL(int(Gesture::SwipeDown), int(stroke(g, 120, 40, 125, 260, 300).gesture));
    TEST_ASSERT_EQUAL(int(Gesture::SwipeUp), int(stroke(g, 120, 260, 118, 120, 250, 3000).gesture));
    TEST_ASSERT_EQUAL(int(Gesture::None), int(stroke(g, 100, 100, 160, 150, 300, 8000).gesture));  // diagonal
}

// Held still: Hold fires once, at 1 s, while the finger is down; the release then gives nothing.
static void test_hold_fires_at_one_second() {
    GestureDetector g;
    int holds = 0;
    uint32_t t = 1000;
    for (; t <= 2200; t += 20) {
        scr::GestureEvent ev = g.feed(true, 100 + (t / 20) % 2, 200, t);  // a pixel of jitter
        if (ev.gesture == Gesture::Hold) {
            ++holds;
            TEST_ASSERT_UINT32_WITHIN(20, 2000, t);
            TEST_ASSERT_EQUAL(100, ev.x);
            TEST_ASSERT_EQUAL(200, ev.y);
        } else {
            TEST_ASSERT_EQUAL(int(Gesture::None), int(ev.gesture));
        }
    }
    TEST_ASSERT_EQUAL(1, holds);
    TEST_ASSERT_EQUAL(int(Gesture::None), int(g.feed(false, 0, 0, t).gesture));
}

static void test_hold_progress_shows_from_0_3_s() {
    GestureDetector g;
    g.feed(true, 50, 50, 1000);
    g.feed(true, 50, 50, 1200);
    TEST_ASSERT_EQUAL_FLOAT(0, g.holdProgress(1200));
    g.feed(true, 51, 50, 1300);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.3f, g.holdProgress(1300));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.6f, g.holdProgress(1600));
    TEST_ASSERT_EQUAL(50, g.pressX());
    TEST_ASSERT_EQUAL(50, g.pressY());
    g.feed(false, 0, 0, 1620);
    TEST_ASSERT_EQUAL_FLOAT(0, g.holdProgress(1640));
}

static void test_moving_cancels_a_hold() {
    GestureDetector g;
    for (uint32_t t = 0; t <= 400; t += 20) g.feed(true, 100 + int(t / 10), 200, 1000 + t);  // drifts 40 px
    TEST_ASSERT_EQUAL_FLOAT(0, g.holdProgress(1400));
    for (uint32_t t = 420; t <= 1600; t += 20)
        TEST_ASSERT_EQUAL(int(Gesture::None), int(g.feed(true, 140, 200, 1000 + t).gesture));
}

static void test_release_once_the_ring_shows_is_not_a_tap() {
    GestureDetector g;
    for (uint32_t t = 0; t <= 400; t += 20) g.feed(true, 100, 200, 1000 + t);
    TEST_ASSERT_EQUAL(int(Gesture::None), int(g.feed(false, 0, 0, 1420).gesture));
}

static void test_slow_press_is_a_tap_when_hold_is_off() {
    GestureDetector g;
    g.enableHold(false);
    bool fired = false;
    for (uint32_t t = 0; t <= 450; t += 20) fired |= g.feed(true, 100, 200, 1000 + t).gesture != Gesture::None;
    TEST_ASSERT_FALSE(fired);
    TEST_ASSERT_EQUAL_FLOAT(0, g.holdProgress(1450));
    TEST_ASSERT_EQUAL(int(Gesture::Tap), int(g.feed(false, 0, 0, 1470).gesture));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_tap);
    RUN_TEST(test_single_noise_sample_is_not_a_tap);
    RUN_TEST(test_swipes);
    RUN_TEST(test_vertical_swipes);
    RUN_TEST(test_hold_fires_at_one_second);
    RUN_TEST(test_hold_progress_shows_from_0_3_s);
    RUN_TEST(test_moving_cancels_a_hold);
    RUN_TEST(test_release_once_the_ring_shows_is_not_a_tap);
    RUN_TEST(test_slow_press_is_a_tap_when_hold_is_off);
    return UNITY_END();
}
