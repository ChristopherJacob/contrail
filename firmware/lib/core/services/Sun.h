#pragma once
#include <cstdint>

namespace svc {

struct SunPos {
    double elevationDeg;  // above horizon, uncorrected for refraction
    double azimuthDeg;    // clockwise from true north
};

// NOAA solar position algorithm (accurate to ~0.1° for current dates).
SunPos sunPosition(int64_t epochUtc, double lat, double lon);

}  // namespace svc
