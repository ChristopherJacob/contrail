#include <unity.h>

#include <initializer_list>

#include "alerts/AlertLights.h"

using scr::AlertKind;

void setUp() {}
void tearDown() {}

static scr::SceneAlert on(AlertKind k, bool soft = false) {
    scr::SceneAlert a;
    a.active = true, a.kind = k, a.soft = soft;
    return a;
}

static void test_pulse_shape() {
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0, alerts::pulse(0));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.5f, alerts::pulse(250));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1, alerts::pulse(500));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0, alerts::pulse(1000));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1, alerts::pulse(29500));
}

static void test_no_alert_leaves_the_lights_alone() {
    scr::SceneAlert a;
    alerts::LightOut o = alerts::lightsFor(a, 500);
    TEST_ASSERT_FALSE(o.on);
    TEST_ASSERT_EQUAL_FLOAT(1, o.backlight);
    TEST_ASSERT_EQUAL_FLOAT(0, o.border);
    TEST_ASSERT_EQUAL_UINT8(0, o.led.r + o.led.g + o.led.b);
}

static void test_pulsing() {
    alerts::LightOut o = alerts::lightsFor(on(AlertKind::Military), 0);
    TEST_ASSERT_TRUE(o.on);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.25f, o.border);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.6f, o.backlight);
    TEST_ASSERT_EQUAL_UINT8(64, o.led.r);  // amber (255, 120, 0) at 25 %
    TEST_ASSERT_EQUAL_UINT8(30, o.led.g);
    TEST_ASSERT_EQUAL_UINT8(0, o.led.b);
    o = alerts::lightsFor(on(AlertKind::Military), 250);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.625f, o.border);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.8f, o.backlight);
    o = alerts::lightsFor(on(AlertKind::Military), 500);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1, o.border);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1, o.backlight);
    TEST_ASSERT_EQUAL_UINT8(255, o.led.r);
    TEST_ASSERT_EQUAL_UINT8(120, o.led.g);
}

static void test_soft_is_steady_and_dim() {
    for (uint32_t ms : {0u, 250u, 500u}) {
        alerts::LightOut o = alerts::lightsFor(on(AlertKind::Watchlist, true), ms);
        TEST_ASSERT_EQUAL_FLOAT(1, o.border);
        TEST_ASSERT_EQUAL_FLOAT(1, o.backlight);
        TEST_ASSERT_EQUAL_UINT8(0, o.led.r);
        TEST_ASSERT_EQUAL_UINT8(0, o.led.g);
        TEST_ASSERT_EQUAL_UINT8(51, o.led.b);  // blue at 20 %
    }
}

static void test_colours() {
    gfx::Rgb red = alerts::ledColour(AlertKind::Emergency);
    TEST_ASSERT_EQUAL_UINT8(255, red.r);
    TEST_ASSERT_EQUAL_UINT8(0, red.g);
    gfx::Rgb green = alerts::kindColour(AlertKind::LowPass);
    TEST_ASSERT_EQUAL_UINT8(0x3D, green.r);
    TEST_ASSERT_EQUAL_UINT8(0xDC, green.g);
    TEST_ASSERT_EQUAL_UINT8(0x84, green.b);
}

static void test_quiet_hours() {
    const int from = 22 * 60, to = 7 * 60;
    TEST_ASSERT_TRUE(alerts::inQuietHours(22 * 60, from, to));  // the start is inside
    TEST_ASSERT_TRUE(alerts::inQuietHours(23 * 60, from, to));
    TEST_ASSERT_TRUE(alerts::inQuietHours(6 * 60 + 59, from, to));
    TEST_ASSERT_FALSE(alerts::inQuietHours(7 * 60, from, to));  // the end is outside
    TEST_ASSERT_FALSE(alerts::inQuietHours(12 * 60, from, to));
    TEST_ASSERT_TRUE(alerts::inQuietHours(10 * 60, 9 * 60, 17 * 60));  // a daytime window
    TEST_ASSERT_FALSE(alerts::inQuietHours(17 * 60, 9 * 60, 17 * 60));
    TEST_ASSERT_FALSE(alerts::inQuietHours(23 * 60, from, from));  // equal times: off
    TEST_ASSERT_FALSE(alerts::inQuietHours(-1, from, to));         // clock not set
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_pulse_shape);
    RUN_TEST(test_no_alert_leaves_the_lights_alone);
    RUN_TEST(test_pulsing);
    RUN_TEST(test_soft_is_steady_and_dim);
    RUN_TEST(test_colours);
    RUN_TEST(test_quiet_hours);
    return UNITY_END();
}
