#pragma once
// Route details handed from the network thread (which looks routes up) to the render
// thread (which builds the scene). Small and lock-protected; oldest entries are reused.
#include <cmath>

#include "providers/Routes.h"
#include "services/Lock.h"

namespace app {

struct RouteInfo {
    char callsign[9] = "";
    char orig[4] = "", dest[4] = "";
    char origCity[24] = "", destCity[24] = "", airline[24] = "";
    float origLat = NAN, origLon = NAN, destLat = NAN, destLon = NAN;
};

RouteInfo toRouteInfo(const prov::Route& r);

class RouteShelf {
   public:
    static constexpr int kMax = 16;
    void put(const RouteInfo& r);
    bool get(const char* callsign, RouteInfo& out) const;

   private:
    mutable svc::Mutex mu_;
    RouteInfo e_[kMax];
    int next_ = 0;
};

}  // namespace app
