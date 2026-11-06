#pragma once
// Route enrichment (origin, destination, airline) for a callsign.
#include <cmath>
#include <cstdint>

#include "hal.h"

namespace prov {

struct Route {
    char callsign[9] = "";
    bool known = false;  // false = looked up, no route exists (negative cache)
    char origIata[4] = "", origIcao[5] = "", origCity[24] = "";
    char destIata[4] = "", destIcao[5] = "", destCity[24] = "";
    char airline[32] = "", airlineIcao[4] = "";
    float origLat = NAN, origLon = NAN, destLat = NAN, destLon = NAN;
    int8_t progressPct = -1;  // AeroAPI only
    uint32_t fetchedMs = 0;
};

// Small LRU of routes keyed by callsign. Not thread-safe: owned by the network task,
// with copies handed to the renderer.
class RouteCache {
   public:
    static constexpr int kCapacity = 48;
    static constexpr uint32_t kKnownTtlMs = 6u * 3600 * 1000;
    static constexpr uint32_t kUnknownTtlMs = 6u * 3600 * 1000;

    // Returns the cached route (fresh only) or nullptr. Marks it most recently used.
    const Route* find(const char* callsign, uint32_t nowMs);
    void put(const Route& r);
    int size() const { return n_; }

   private:
    Route items_[kCapacity];
    uint32_t used_[kCapacity] = {};
    uint32_t tick_ = 0;
    int n_ = 0;
};

// Callsign route databases go stale. A route is plausible only if the aircraft is within
// `toleranceNm` of the great-circle path between its airports (true when coordinates are unknown).
bool routePlausible(const Route& r, double lat, double lon, double toleranceNm = 150);

bool parseAdsbdb(hal::IByteSource& src, Route& out);
// Picks the leg that is airborne now (actual_off set, actual_on null).
bool parseAeroApiFlights(hal::IByteSource& src, Route& out);

class AdsbdbClient {
   public:
    static constexpr const char* kHost = "api.adsbdb.com";
    // Returns false on transport error; on success `out.known` says whether a route exists.
    bool lookup(hal::IHttp& http, const char* callsign, uint32_t nowMs, Route& out);
};

class AeroApiClient {
   public:
    static constexpr const char* kHost = "aeroapi.flightaware.com";
    void setKey(const char* key);
    void setDailyCap(int cap) { cap_ = cap; }
    bool configured() const { return key_[0] != 0; }
    // False when unconfigured, over budget, or on error.
    bool lookup(hal::IHttp& http, const char* callsign, uint32_t nowMs, int64_t epochS, Route& out);
    int usedToday() const { return used_; }

   private:
    bool budgetAvailable(int64_t epochS);
    char key_[64] = "";
    int cap_ = 20, used_ = 0;
    int64_t day_ = -1;
};

}  // namespace prov
