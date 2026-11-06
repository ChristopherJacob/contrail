#include "Sun.h"

#include <cmath>

namespace svc {
namespace {
constexpr double kPi = 3.14159265358979323846;
inline double rad(double d) { return d * kPi / 180.0; }
inline double deg(double r) { return r * 180.0 / kPi; }
}  // namespace

SunPos sunPosition(int64_t epochUtc, double lat, double lon) {
    double jd = epochUtc / 86400.0 + 2440587.5;
    double t = (jd - 2451545.0) / 36525.0;  // Julian centuries since J2000

    double l0 = std::fmod(280.46646 + t * (36000.76983 + t * 0.0003032), 360.0);
    double m = 357.52911 + t * (35999.05029 - 0.0001537 * t);
    double e = 0.016708634 - t * (0.000042037 + 0.0000001267 * t);
    double c = std::sin(rad(m)) * (1.914602 - t * (0.004817 + 0.000014 * t)) +
               std::sin(rad(2 * m)) * (0.019993 - 0.000101 * t) + std::sin(rad(3 * m)) * 0.000289;
    double trueLong = l0 + c;
    double omega = 125.04 - 1934.136 * t;
    double appLong = trueLong - 0.00569 - 0.00478 * std::sin(rad(omega));
    double eps0 = 23 + (26 + (21.448 - t * (46.815 + t * (0.00059 - t * 0.001813))) / 60) / 60;
    double eps = eps0 + 0.00256 * std::cos(rad(omega));
    double decl = deg(std::asin(std::sin(rad(eps)) * std::sin(rad(appLong))));

    double y = std::tan(rad(eps / 2)) * std::tan(rad(eps / 2));
    double eqTime = 4 * deg(y * std::sin(2 * rad(l0)) - 2 * e * std::sin(rad(m)) +
                            4 * e * y * std::sin(rad(m)) * std::cos(2 * rad(l0)) -
                            0.5 * y * y * std::sin(4 * rad(l0)) - 1.25 * e * e * std::sin(2 * rad(m)));

    double minutesUtc = std::fmod(double(epochUtc), 86400.0) / 60.0;
    if (minutesUtc < 0) minutesUtc += 1440;
    double trueSolarTime = std::fmod(minutesUtc + eqTime + 4 * lon, 1440.0);
    if (trueSolarTime < 0) trueSolarTime += 1440;
    double hourAngle = trueSolarTime / 4 - 180;

    double zenith = deg(std::acos(std::sin(rad(lat)) * std::sin(rad(decl)) +
                                  std::cos(rad(lat)) * std::cos(rad(decl)) * std::cos(rad(hourAngle))));
    double az = deg(std::atan2(std::sin(rad(hourAngle)),
                               std::cos(rad(hourAngle)) * std::sin(rad(lat)) - std::tan(rad(decl)) * std::cos(rad(lat))));
    az = std::fmod(az + 180.0, 360.0);
    return {90 - zenith, az};
}

}  // namespace svc
