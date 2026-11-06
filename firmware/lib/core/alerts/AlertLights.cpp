#include "AlertLights.h"

#include <cmath>

namespace alerts {
using scr::AlertKind;

namespace {
gfx::Rgb scaled(gfx::Rgb c, float f) {
    return {uint8_t(c.r * f + 0.5f), uint8_t(c.g * f + 0.5f), uint8_t(c.b * f + 0.5f)};
}
}  // namespace

float pulse(uint32_t ms) {
    const float phase = float(ms % kPulseMs) / float(kPulseMs);
    return 0.5f - 0.5f * std::cos(2 * float(M_PI) * phase);
}

gfx::Rgb kindColour(AlertKind k) {
    switch (k) {
        case AlertKind::Emergency: return {0xFF, 0x3B, 0x3B};
        case AlertKind::Watchlist: return {0x4A, 0x8B, 0xFF};
        case AlertKind::Military: return {0xFF, 0xB0, 0x20};
        case AlertKind::LowPass: return {0x3D, 0xDC, 0x84};
    }
    return {255, 255, 255};
}

gfx::Rgb ledColour(AlertKind k) {
    switch (k) {
        case AlertKind::Emergency: return {255, 0, 0};
        case AlertKind::Watchlist: return {0, 0, 255};
        case AlertKind::Military: return {255, 120, 0};  // an LED's green is strong: amber needs little
        case AlertKind::LowPass: return {0, 255, 0};
    }
    return {0, 0, 0};
}

LightOut lightsFor(const scr::SceneAlert& a, uint32_t ms) {
    LightOut o;
    if (!a.active) return o;
    o.on = true;
    float led;
    if (a.soft) {
        led = 0.2f, o.border = 1;
    } else {
        const float p = pulse(ms);
        led = o.border = 0.25f + 0.75f * p;
        o.backlight = 0.6f + 0.4f * p;
    }
    o.led = scaled(ledColour(a.kind), led);
    return o;
}

bool inQuietHours(int m, int from, int to) {
    if (m < 0 || from == to) return false;
    return from < to ? m >= from && m < to : m >= from || m < to;
}

}  // namespace alerts
