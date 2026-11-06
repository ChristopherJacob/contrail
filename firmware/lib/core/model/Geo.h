#pragma once

namespace geo {

constexpr double kEarthRadiusNm = 3440.065;
constexpr double kFtPerNm = 6076.12;

double distanceNm(double lat1, double lon1, double lat2, double lon2);
// Initial great-circle bearing from point 1 to point 2, degrees clockwise from true north.
double bearingDeg(double lat1, double lon1, double lat2, double lon2);
// Angle above the observer's horizon of an aircraft at altFt, distNm away (earth curvature included).
double elevationDeg(double distNm, double altFt);
// Destination point after travelling distNm on bearing brgDeg.
void project(double lat, double lon, double distNm, double brgDeg, double& outLat, double& outLon);
// Distance from point P to the great-circle segment A→B (nm), clamped to the endpoints.
double segmentDistanceNm(double latA, double lonA, double latB, double lonB, double latP, double lonP);
// Fast float maths for points near a reference (equirectangular approximation): within
// ~100 nm and away from the poles the error is far below what the display can show, and
// it avoids double-precision trig, which the ESP32 runs in software.
struct LocalFrame {
    float lat0 = 0, lon0 = 0, cosLat0 = 1;
    void set(double lat, double lon);
    void distBearing(float lat, float lon, float& distNm, float& bearingDeg) const;
};
void projectFast(float lat, float lon, float distNm, float brgDeg, float& outLat, float& outLon);
float elevationDegFast(float distNm, float altFt);

void boundingBox(double lat, double lon, double radiusNm, double& lamin, double& lomin, double& lamax,
                 double& lomax);

}  // namespace geo
