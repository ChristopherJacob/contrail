#include <unity.h>

#include <cstdlib>
#include <ctime>

#include "ui/Format.h"

void setUp() {}
void tearDown() {}

static void test_altitude() {
    char b[24];
    ui::formatAltitude(b, sizeof b, 34000);
    TEST_ASSERT_EQUAL_STRING("34,000 ft", b);
    ui::formatAltitude(b, sizeof b, 800);
    TEST_ASSERT_EQUAL_STRING("800 ft", b);
    ui::formatThousands(b, sizeof b, 1200);
    TEST_ASSERT_EQUAL_STRING("1,200", b);
    ui::formatThousands(b, sizeof b, 1000000);  // the Stats screen's all-time totals get this big
    TEST_ASSERT_EQUAL_STRING("1,000,000", b);
    ui::formatThousands(b, sizeof b, -2304118);
    TEST_ASSERT_EQUAL_STRING("-2,304,118", b);
    ui::formatHundreds(b, sizeof b, 34049);
    TEST_ASSERT_EQUAL_STRING("340", b);
    ui::formatHundreds(b, sizeof b, 3500);
    TEST_ASSERT_EQUAL_STRING("035", b);
}

static void test_distance() {
    char b[16];
    ui::formatDistance(b, sizeof b, 2.43f);
    TEST_ASSERT_EQUAL_STRING("2.4 nm", b);
    ui::formatDistance(b, sizeof b, 14.6f);
    TEST_ASSERT_EQUAL_STRING("15 nm", b);
}

static void test_compass_point() {
    TEST_ASSERT_EQUAL_STRING("N", ui::compassPoint(359));
    TEST_ASSERT_EQUAL_STRING("NE", ui::compassPoint(40));
    TEST_ASSERT_EQUAL_STRING("S", ui::compassPoint(180));
    TEST_ASSERT_EQUAL_STRING("NW", ui::compassPoint(310));
}

static void test_vrate() {
    char b[24];
    ui::formatVrate(b, sizeof b, 1180);
    TEST_ASSERT_EQUAL_STRING("↑ 1,200 fpm", b);
    ui::formatVrate(b, sizeof b, -640);
    TEST_ASSERT_EQUAL_STRING("↓ 600 fpm", b);
    ui::formatVrate(b, sizeof b, 40);
    TEST_ASSERT_EQUAL_STRING("level", b);
    TEST_ASSERT_EQUAL_STRING("↑", ui::trendArrow(900));
    TEST_ASSERT_EQUAL_STRING("↓", ui::trendArrow(-900));
    TEST_ASSERT_EQUAL_STRING("", ui::trendArrow(100));
}

static void test_clock() {
    setenv("TZ", "UTC0", 1);
    tzset();
    char b[8];
    ui::formatClock(b, sizeof b, 1793471700);  // 2026-10-31 18:35:00 UTC
    TEST_ASSERT_EQUAL_STRING("18:35", b);
}

static void test_hash_changes_with_text() {
    TEST_ASSERT_NOT_EQUAL(ui::hashStr("UAL53"), ui::hashStr("UAL54"));
    // Chained hashes keep string boundaries: "ab" then "c" differs from "a" then "bc".
    TEST_ASSERT_NOT_EQUAL(ui::hashStr("c", ui::hashStr("ab")), ui::hashStr("bc", ui::hashStr("a")));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_altitude);
    RUN_TEST(test_distance);
    RUN_TEST(test_compass_point);
    RUN_TEST(test_vrate);
    RUN_TEST(test_clock);
    RUN_TEST(test_hash_changes_with_text);
    return UNITY_END();
}
