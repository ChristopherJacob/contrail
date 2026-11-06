#pragma once
// Helpers for the per-pixel drawing code.
//
// GFX_HOT puts a function in IRAM on the ESP32. Code otherwise runs from flash through a
// small cache, and the drawing loops measured up to 3x slower depending on how the rest of
// the firmware happened to be laid out around them.
//
// floorf/ceilf are library calls on the ESP32 (no inline instruction), so the per-row span
// maths uses these instead; they match std::floor/std::ceil for |x| < 2^31.
#ifdef CONTRAIL_ESP32
#include <esp_attr.h>
#define GFX_HOT IRAM_ATTR
#else
#define GFX_HOT
#endif

namespace gfx {

inline float fastFloor(float x) {
    float t = float(int(x));
    return t > x ? t - 1.f : t;
}
inline float fastCeil(float x) {
    float t = float(int(x));
    return t < x ? t + 1.f : t;
}

}  // namespace gfx
