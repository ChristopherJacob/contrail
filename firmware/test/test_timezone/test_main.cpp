#include <unity.h>

#include <cstdlib>
#include <ctime>

#include "util/TimeZone.h"

void setUp() {}
void tearDown() {}

static void check(int offsetS, const char* want) {
    char tz[16];
    util::posixFromOffset(offsetS, tz, sizeof tz);
    TEST_ASSERT_EQUAL_STRING(want, tz);
}

static void test_whole_hours() {
    check(-5 * 3600, "<-05>5");  // POSIX counts west of UTC as positive
    check(9 * 3600, "<+09>-9");
    check(0, "<+00>0");
}

static void test_part_hours() {
    check(5 * 3600 + 1800, "<+0530>-5:30");
    check(-(3 * 3600 + 1800), "<-0330>3:30");
    check(5 * 3600 + 45 * 60, "<+0545>-5:45");
}

static void test_localtime_follows_it() {
    char tz[16];
    util::posixFromOffset(-5 * 3600, tz, sizeof tz);
    setenv("TZ", tz, 1);
    tzset();
    const time_t t = 1793750400 + 23 * 3600 + 53 * 60;  // 2026-11-03 23:53 UTC
    struct tm tm;
    localtime_r(&t, &tm);
    TEST_ASSERT_EQUAL(18, tm.tm_hour);
    TEST_ASSERT_EQUAL(53, tm.tm_min);
}

static void test_out_of_range_is_utc() {
    check(15 * 3600, "UTC0");
    check(-13 * 3600, "UTC0");
    check(5 * 3600 + 7, "UTC0");  // not whole minutes
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_whole_hours);
    RUN_TEST(test_part_hours);
    RUN_TEST(test_localtime_follows_it);
    RUN_TEST(test_out_of_range_is_utc);
    return UNITY_END();
}
