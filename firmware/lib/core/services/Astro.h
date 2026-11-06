#pragma once
#include <cstdint>

namespace svc {

struct HorizPos {
    double elevationDeg;  // above the horizon
    double azimuthDeg;    // clockwise from true north
};

// Where a star (J2000 right ascension and declination, degrees) is in the local sky.
// Precession since J2000 (~0.4°) is ignored: fine for drawing.
HorizPos equatorialToHorizontal(double raDeg, double decDeg, int64_t epochUtc, double lat, double lon);

// Local sidereal time in degrees (0..360) at a longitude: the right ascension on the meridian.
double localSiderealDeg(int64_t epochUtc, double lon);

struct MoonState {
    double elevationDeg, azimuthDeg;  // topocentric
    double illumination;              // lit fraction of the disc, 0..1
    bool waxing;
};

// Low-precision lunar theory (Schlyter): position to well under 1°, phase to ~1%.
MoonState moonState(int64_t epochUtc, double lat, double lon);

}  // namespace svc
