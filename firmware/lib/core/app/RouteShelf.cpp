#include "RouteShelf.h"

#include <cstring>
#include "util/Str.h"

namespace app {
using util::copyStr;
RouteInfo toRouteInfo(const prov::Route& r) {
    RouteInfo o;
    copyStr(o.callsign, sizeof o.callsign, r.callsign);
    copyStr(o.orig, sizeof o.orig, r.origIata);
    copyStr(o.dest, sizeof o.dest, r.destIata);
    copyStr(o.origCity, sizeof o.origCity, r.origCity);
    copyStr(o.destCity, sizeof o.destCity, r.destCity);
    copyStr(o.airline, sizeof o.airline, r.airline);
    o.origLat = r.origLat, o.origLon = r.origLon, o.destLat = r.destLat, o.destLon = r.destLon;
    return o;
}

void RouteShelf::put(const RouteInfo& r) {
    svc::LockGuard g(mu_);
    for (auto& e : e_)
        if (!std::strcmp(e.callsign, r.callsign)) return void(e = r);
    e_[next_] = r;
    next_ = (next_ + 1) % kMax;
}

bool RouteShelf::get(const char* cs, RouteInfo& out) const {
    svc::LockGuard g(mu_);
    for (auto& e : e_)
        if (e.callsign[0] && !std::strcmp(e.callsign, cs)) return out = e, true;
    return false;
}

}  // namespace app
