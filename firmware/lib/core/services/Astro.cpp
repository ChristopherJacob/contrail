#include "Astro.h"

#include <cmath>

namespace svc {
namespace {

constexpr double kDeg = M_PI / 180.0;

double rev(double x) { return x - 360.0 * std::floor(x / 360.0); }
double sind(double x) { return std::sin(x * kDeg); }
double cosd(double x) { return std::cos(x * kDeg); }

double julianDay(int64_t epoch) { return epoch / 86400.0 + 2440587.5; }

// Local sidereal time in degrees.
double localSidereal(int64_t epoch, double lon) {
    double jd = julianDay(epoch);
    return rev(280.46061837 + 360.98564736629 * (jd - 2451545.0) + lon);
}

HorizPos fromHourAngle(double haDeg, double decDeg, double lat) {
    double sinAlt = sind(lat) * sind(decDeg) + cosd(lat) * cosd(decDeg) * cosd(haDeg);
    double alt = std::asin(sinAlt) / kDeg;
    double az = std::atan2(-sind(haDeg) * cosd(decDeg), cosd(lat) * sind(decDeg) - sind(lat) * cosd(decDeg) * cosd(haDeg));
    return {alt, rev(az / kDeg)};
}

}  // namespace

double localSiderealDeg(int64_t epoch, double lon) { return localSidereal(epoch, lon); }

HorizPos equatorialToHorizontal(double ra, double dec, int64_t epoch, double lat, double lon) {
    return fromHourAngle(localSidereal(epoch, lon) - ra, dec, lat);
}

MoonState moonState(int64_t epoch, double lat, double lon) {
    // Days since 2000 Jan 0.0 UT, the epoch of Schlyter's elements.
    double d = julianDay(epoch) - 2451543.5;
    double N = rev(125.1228 - 0.0529538083 * d), i = 5.1454, w = rev(318.0634 + 0.1643573223 * d);
    double a = 60.2666, e = 0.054900, M = rev(115.3654 + 13.0649929509 * d);
    double ws = rev(282.9404 + 4.70935e-5 * d), Ms = rev(356.0470 + 0.9856002585 * d);

    double E = M + (180 / M_PI) * e * sind(M) * (1 + e * cosd(M));
    for (int k = 0; k < 5; ++k) E = E - (E - (180 / M_PI) * e * sind(E) - M) / (1 - e * cosd(E));
    double xv = a * (cosd(E) - e), yv = a * std::sqrt(1 - e * e) * sind(E);
    double v = std::atan2(yv, xv) / kDeg, r = std::sqrt(xv * xv + yv * yv);
    double xh = r * (cosd(N) * cosd(v + w) - sind(N) * sind(v + w) * cosd(i));
    double yh = r * (sind(N) * cosd(v + w) + cosd(N) * sind(v + w) * cosd(i));
    double zh = r * sind(v + w) * sind(i);
    double lonEcl = rev(std::atan2(yh, xh) / kDeg);
    double latEcl = std::atan2(zh, std::sqrt(xh * xh + yh * yh)) / kDeg;

    // The largest perturbations (evection, variation, yearly equation, …).
    double Ls = rev(Ms + ws), Lm = rev(M + w + N), D = rev(Lm - Ls), F = rev(Lm - N);
    lonEcl += -1.274 * sind(M - 2 * D) + 0.658 * sind(2 * D) - 0.186 * sind(Ms) - 0.059 * sind(2 * M - 2 * D) -
              0.057 * sind(M - 2 * D + Ms) + 0.053 * sind(M + 2 * D) + 0.046 * sind(2 * D - Ms) +
              0.041 * sind(M - Ms) - 0.035 * sind(D) - 0.031 * sind(M + Ms) - 0.015 * sind(2 * F - 2 * D) +
              0.011 * sind(M - 4 * D);
    latEcl += -0.173 * sind(F - 2 * D) - 0.055 * sind(M - F - 2 * D) - 0.046 * sind(M + F - 2 * D) +
              0.033 * sind(F + 2 * D) + 0.017 * sind(2 * M + F);
    r += -0.58 * cosd(M - 2 * D) - 0.46 * cosd(2 * D);

    double ecl = 23.4393 - 3.563e-7 * d;
    double xe = cosd(lonEcl) * cosd(latEcl);
    double ye = sind(lonEcl) * cosd(latEcl) * cosd(ecl) - sind(latEcl) * sind(ecl);
    double ze = sind(lonEcl) * cosd(latEcl) * sind(ecl) + sind(latEcl) * cosd(ecl);
    double ra = rev(std::atan2(ye, xe) / kDeg), dec = std::atan2(ze, std::sqrt(xe * xe + ye * ye)) / kDeg;

    HorizPos h = equatorialToHorizontal(ra, dec, epoch, lat, lon);
    // Parallax: the moon is close enough that it sits ~1° lower than seen from Earth's centre.
    h.elevationDeg -= std::asin(1.0 / r) / kDeg * cosd(h.elevationDeg);

    // Phase from the moon's elongation from the sun.
    double sunLon = rev(Ms + ws + 1.915 * sind(Ms) + 0.020 * sind(2 * Ms));
    double cosElong = cosd(latEcl) * cosd(lonEcl - sunLon);
    MoonState m;
    m.elevationDeg = h.elevationDeg, m.azimuthDeg = h.azimuthDeg;
    m.illumination = (1 - cosElong) / 2;
    m.waxing = sind(lonEcl - sunLon) > 0;
    return m;
}

}  // namespace svc
