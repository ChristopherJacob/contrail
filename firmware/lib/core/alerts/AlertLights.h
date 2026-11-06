#pragma once
// How an alert drives the border, the RGB LED and the backlight (alerts spec §6), as plain
// functions of time so the tests need no hardware.
#include <cstdint>

#include "gfx/Color.h"
#include "screens/Scene.h"

namespace alerts {

constexpr uint32_t kPulseMs = 1000;  // one soft cycle a second: far under 3 flashes/s

// 0 at the start of each cycle, 1 halfway through (a raised cosine).
float pulse(uint32_t ms);
gfx::Rgb kindColour(scr::AlertKind k);  // on screen
gfx::Rgb ledColour(scr::AlertKind k);   // the LED, fully saturated

struct LightOut {
    bool on = false;
    gfx::Rgb led{0, 0, 0};
    float backlight = 1;  // a scale for the backlight's normal level
    float border = 0;     // border strength, 0..1
};
// What the lights do `msIntoAlert` after `a` started. Pulsing: border and LED 25-100 %, backlight
// 60-100 %. Soft (quiet hours): border steady, LED at 20 %, no backlight pulse.
LightOut lightsFor(const scr::SceneAlert& a, uint32_t msIntoAlert);

// Minutes past local midnight in [from, to), wrapping past midnight; never when from == to or
// minuteOfDay < 0 (the clock is not set).
bool inQuietHours(int minuteOfDay, int from, int to);

}  // namespace alerts
