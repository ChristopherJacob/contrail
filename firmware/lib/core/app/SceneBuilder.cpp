#include "SceneBuilder.h"

#include <cctype>
#include <cmath>
#include <cstring>

#include "data/Airlines.h"
#include "data/Shapes.h"
#include "model/Geo.h"
#include "util/Str.h"

namespace app {
using util::copyStr;
namespace {
bool hasCoords(const RouteInfo& r) {
    return !std::isnan(r.origLat) && !std::isnan(r.origLon) && !std::isnan(r.destLat) && !std::isnan(r.destLon);
}
}  // namespace

float routeProgress(const RouteInfo& r, double lat, double lon) {
    if (!hasCoords(r) || !std::isfinite(lat) || !std::isfinite(lon)) return -1;
    double flown = geo::distanceNm(r.origLat, r.origLon, lat, lon);
    double left = geo::distanceNm(lat, lon, r.destLat, r.destLon);
    if (flown + left < 1) return -1;
    return float(flown / (flown + left));
}

int64_t estimateArrival(const RouteInfo& r, double lat, double lon, float gsKt, int32_t altFt, int64_t now) {
    if (!hasCoords(r) || !std::isfinite(lat) || !std::isfinite(lon)) return 0;
    if (!(gsKt >= 60) || altFt < 1000 || now <= 0) return 0;  // !(>=) also rejects a NaN speed
    double left = geo::distanceNm(lat, lon, r.destLat, r.destLon);
    return now + int64_t(left / gsKt * 3600.0);
}

void fillFlight(scr::SceneFlight& f, const model::Aircraft& a, const RouteInfo* route, int64_t now) {
    f = scr::SceneFlight{};
    f.hex = a.hex;
    copyStr(f.callsign, sizeof f.callsign, a.callsign[0] ? a.callsign : a.reg);
    // Callsigns in capitals, once, here: every screen and theme shows the same thing, and the condensed
    // hero face (capitals only) never meets a lower-case letter.
    for (char* p = f.callsign; *p; ++p) *p = char(std::toupper((unsigned char)*p));
    copyStr(f.reg, sizeof f.reg, a.reg);
    copyStr(f.type, sizeof f.type, a.type);
    f.shape = uint8_t(data::familyFor(a.type, a.category));
    copyStr(f.squawk, sizeof f.squawk, a.squawk);
    if (a.callsign[0] && data::airlineForCallsign(f.callsign)) copyStr(f.airline, 4, f.callsign);
    f.altFt = a.altFt, f.vrateFpm = a.vrateFpm, f.gsKt = a.gsKt, f.trackDeg = a.trackDeg;
    f.distNm = a.distNm, f.bearingDeg = a.bearingDeg, f.elevDeg = a.elevDeg;
    f.military = a.has(model::AC_MILITARY), f.emergency = a.has(model::AC_EMERGENCY);
    f.emergencyStatus = a.has(model::AC_EMERG_STATUS);
    if (!route) return;
    f.hasRoute = true;
    copyStr(f.orig, sizeof f.orig, route->orig);
    copyStr(f.dest, sizeof f.dest, route->dest);
    f.progress = routeProgress(*route, a.lat, a.lon);
    f.etaEpoch = estimateArrival(*route, a.lat, a.lon, a.gsKt, a.altFt, now);
}

}  // namespace app
