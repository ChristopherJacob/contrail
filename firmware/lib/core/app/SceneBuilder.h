#pragma once
#include <cstdint>

#include "RouteShelf.h"
#include "model/Aircraft.h"
#include "screens/Scene.h"

namespace app {

// Fraction of the route flown, from distances to both airports (robust when the plane
// is off the great circle). < 0 when the airports' coordinates are unknown.
float routeProgress(const RouteInfo& r, double lat, double lon);

// Arrival estimate (epoch seconds) from the remaining distance at the current ground
// speed. 0 when it would mislead: no route, below 1,000 ft, or under 60 kt.
int64_t estimateArrival(const RouteInfo& r, double lat, double lon, float gsKt, int32_t altFt, int64_t now);

void fillFlight(scr::SceneFlight& f, const model::Aircraft& a, const RouteInfo* route, int64_t now);

}  // namespace app
