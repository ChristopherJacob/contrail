#pragma once
// Small float helpers shared by the drawing code.

namespace gfx {

constexpr float kPi = 3.14159265f;
constexpr float kDeg = kPi / 180.f;  // degrees to radians

// Wraps degrees into [0, 360). fmodf is a slow software routine on the ESP32, and some
// callers run per pixel; angles here are never more than a turn or two out.
inline float wrap360(float d) {
    while (d >= 360.f) d -= 360.f;
    while (d < 0.f) d += 360.f;
    return d;
}

}  // namespace gfx
