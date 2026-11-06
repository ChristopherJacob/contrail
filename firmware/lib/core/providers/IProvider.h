#pragma once
#include <cstdint>

#include "hal.h"
#include "model/AircraftStore.h"

namespace prov {

struct PollContext {
    uint32_t nowMs;
    int64_t epochS;  // UTC seconds, 0 if not yet known
    double lat, lon;
    float radiusNm;
};

class IProvider {
   public:
    virtual ~IProvider() = default;
    virtual const char* name() const = 0;
    virtual bool configured() const = 0;
    virtual uint32_t pollIntervalMs() const = 0;
    // Fetch and store one update. Returns false on any transport or parse failure.
    virtual bool poll(hal::IHttp& http, model::AircraftStore& store, const PollContext& ctx) = 0;
};

}  // namespace prov
