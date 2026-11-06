#include "SkyProjection.h"

#include <cmath>

#include "gfx/Math.h"

namespace scr {

using gfx::kDeg;

gfx::Vec2 domeProjectRaw(float az, float elev, const DomeGeom& g) {
    float r = (90.f - (elev > 90 ? 90 : elev)) / 90.f;
    float a = az * kDeg;
    return {g.cx - r * g.rx * std::sin(a), g.cy - r * g.ry * std::cos(a)};
}

gfx::Vec2 domeProject(float az, float elev, const DomeGeom& g) { return domeProjectRaw(az, elev < 0 ? 0 : elev, g); }

gfx::Vec2 domeDirectionToward(float az, float elev, float toAz, float toElev, const DomeGeom& g) {
    // Unit vectors (east, north, up); step 4 degrees from the first toward the second along
    // the great circle through both, and project that step.
    auto vec = [](float a, float e, float* v) {
        v[0] = std::cos(e * kDeg) * std::sin(a * kDeg), v[1] = std::cos(e * kDeg) * std::cos(a * kDeg);
        v[2] = std::sin(e * kDeg);
    };
    float m[3], s[3];
    vec(az, elev, m), vec(toAz, toElev, s);
    float dot = m[0] * s[0] + m[1] * s[1] + m[2] * s[2];
    float t[3] = {s[0] - dot * m[0], s[1] - dot * m[1], s[2] - dot * m[2]};
    float tl = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
    if (tl < 1e-5f) return {0, -1};
    const float c = std::cos(4 * kDeg), sn = std::sin(4 * kDeg);
    float p[3];
    for (int i = 0; i < 3; ++i) p[i] = m[i] * c + t[i] / tl * sn;
    float pe = std::asin(std::fmax(-1.f, std::fmin(1.f, p[2]))) / kDeg, pa = std::atan2(p[0], p[1]) / kDeg;
    gfx::Vec2 a = domeProjectRaw(az, elev, g), b = domeProjectRaw(pa, pe, g);
    float dx = b.x - a.x, dy = b.y - a.y, l = std::sqrt(dx * dx + dy * dy);
    if (l < 1e-5f) return {0, -1};
    return {dx / l, dy / l};
}

}  // namespace scr
