#include "Geo.h"

#include <cmath>

namespace geo {
namespace {
constexpr double kPi = 3.14159265358979323846;
inline double rad(double d) { return d * kPi / 180.0; }
inline double deg(double r) { return r * 180.0 / kPi; }
}  // namespace

double distanceNm(double lat1, double lon1, double lat2, double lon2) {
    double p1 = rad(lat1), p2 = rad(lat2), dp = rad(lat2 - lat1), dl = rad(lon2 - lon1);
    double a = std::sin(dp / 2) * std::sin(dp / 2) + std::cos(p1) * std::cos(p2) * std::sin(dl / 2) * std::sin(dl / 2);
    return 2 * kEarthRadiusNm * std::asin(std::sqrt(a < 1 ? a : 1));
}

double bearingDeg(double lat1, double lon1, double lat2, double lon2) {
    double p1 = rad(lat1), p2 = rad(lat2), dl = rad(lon2 - lon1);
    double y = std::sin(dl) * std::cos(p2);
    double x = std::cos(p1) * std::sin(p2) - std::sin(p1) * std::cos(p2) * std::cos(dl);
    double b = deg(std::atan2(y, x));
    return b < 0 ? b + 360 : b;
}

double elevationDeg(double distNm, double altFt) {
    double h = altFt / kFtPerNm;  // height in nm
    if (distNm < 1e-6) return 90;
    double theta = distNm / kEarthRadiusNm;  // central angle
    // Aircraft position relative to observer in the observer's local vertical plane.
    double r = kEarthRadiusNm + h;
    double up = r * std::cos(theta) - kEarthRadiusNm;
    double across = r * std::sin(theta);
    return deg(std::atan2(up, across));
}

void project(double lat, double lon, double distNm, double brgDeg, double& outLat, double& outLon) {
    double d = distNm / kEarthRadiusNm, b = rad(brgDeg), p1 = rad(lat), l1 = rad(lon);
    double p2 = std::asin(std::sin(p1) * std::cos(d) + std::cos(p1) * std::sin(d) * std::cos(b));
    double l2 = l1 + std::atan2(std::sin(b) * std::sin(d) * std::cos(p1), std::cos(d) - std::sin(p1) * std::sin(p2));
    outLat = deg(p2);
    outLon = std::fmod(deg(l2) + 540, 360) - 180;
}

double segmentDistanceNm(double latA, double lonA, double latB, double lonB, double latP, double lonP) {
    double dAP = distanceNm(latA, lonA, latP, lonP) / kEarthRadiusNm;  // angular
    double dAB = distanceNm(latA, lonA, latB, lonB) / kEarthRadiusNm;
    double bAP = rad(bearingDeg(latA, lonA, latP, lonP)), bAB = rad(bearingDeg(latA, lonA, latB, lonB));
    double xt = std::asin(std::sin(dAP) * std::sin(bAP - bAB));     // cross-track
    double at = std::acos(std::cos(dAP) / std::cos(xt));           // along-track
    if (std::cos(bAP - bAB) < 0) return dAP * kEarthRadiusNm;      // behind A
    if (at > dAB) return distanceNm(latB, lonB, latP, lonP);        // beyond B
    return std::fabs(xt) * kEarthRadiusNm;
}

void boundingBox(double lat, double lon, double radiusNm, double& lamin, double& lomin, double& lamax,
                 double& lomax) {
    double dLat = radiusNm / 60.0;
    double c = std::cos(rad(lat));
    double dLon = radiusNm / (60.0 * (c > 0.01 ? c : 0.01));
    lamin = lat - dLat, lamax = lat + dLat, lomin = lon - dLon, lomax = lon + dLon;
}

namespace {
constexpr float kDegF = 3.14159265f / 180.f;
constexpr float kNmPerDeg = float(kEarthRadiusNm) * kDegF;  // ~60.04, matching the exact maths
inline float wrapLon(float d) { return d > 180.f ? d - 360.f : d < -180.f ? d + 360.f : d; }
}  // namespace

void LocalFrame::set(double lat, double lon) {
    lat0 = float(lat), lon0 = float(lon), cosLat0 = std::cos(lat0 * kDegF);
}

void LocalFrame::distBearing(float lat, float lon, float& distNm, float& bearingDeg) const {
    float north = (lat - lat0) * kNmPerDeg, east = wrapLon(lon - lon0) * kNmPerDeg * cosLat0;
    distNm = std::sqrt(north * north + east * east);
    float b = std::atan2(east, north) / kDegF;
    bearingDeg = b < 0 ? b + 360.f : b;
}

void projectFast(float lat, float lon, float distNm, float brgDeg, float& outLat, float& outLon) {
    float r = brgDeg * kDegF;
    outLat = lat + distNm * std::cos(r) / kNmPerDeg;
    float c = std::cos(lat * kDegF);
    outLon = wrapLon(lon + (c > 1e-4f ? distNm * std::sin(r) / (kNmPerDeg * c) : 0.f));
}

float elevationDegFast(float distNm, float altFt) {
    if (distNm < 1e-4f) return 90.f;
    // Height above the observer's horizon plane, less the Earth's curvature drop.
    float h = altFt / float(kFtPerNm) - distNm * distNm / (2.f * float(kEarthRadiusNm));
    return std::atan2(h, distNm) / kDegF;
}

}  // namespace geo
