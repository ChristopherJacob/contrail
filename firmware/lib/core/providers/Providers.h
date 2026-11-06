#pragma once
// Concrete flight-position providers.
#include <cstdio>

#include "IProvider.h"
#include "ReadsbParser.h"

namespace prov {

// Free community ADS-B aggregators sharing the readsb JSON schema. Rotates to the next
// host when one fails so a single outage does not trip the provider chain.
class CommunityProvider : public IProvider {
   public:
    struct Host {
        const char* name;
        const char* urlFormat;  // printf with lat, lon, radius (nm)
    };
    CommunityProvider();
    const char* name() const override { return hosts_[current_].name; }
    bool configured() const override { return true; }
    uint32_t pollIntervalMs() const override { return intervalMs_; }
    void setIntervalMs(uint32_t ms) { intervalMs_ = ms; }
    bool poll(hal::IHttp& http, model::AircraftStore& store, const PollContext& ctx) override;

   private:
    static const Host hosts_[];
    int current_ = 0;
    uint32_t intervalMs_ = 5000;
};

// A dump1090 / readsb / tar1090 receiver on the LAN, e.g. http://192.168.1.20/tar1090/data/aircraft.json
class LocalReceiverProvider : public IProvider {
   public:
    void setUrl(const char* url);
    const char* name() const override { return "local"; }
    bool configured() const override { return url_[0] != 0; }
    uint32_t pollIntervalMs() const override { return intervalMs_; }
    void setIntervalMs(uint32_t ms) { intervalMs_ = ms; }
    bool poll(hal::IHttp& http, model::AircraftStore& store, const PollContext& ctx) override;

   private:
    char url_[128] = "";
    uint32_t intervalMs_ = 1000;
};

// OpenSky OAuth2 client-credentials token, refreshed a minute before expiry.
class OpenSkyAuth {
   public:
    bool parseTokenResponse(hal::IByteSource& src, uint32_t nowMs);
    bool valid(uint32_t nowMs) const { return token_[0] && int32_t(expiresAtMs_ - nowMs) > 60000; }
    const char* token() const { return token_; }
    void clear() { token_[0] = 0; }

   private:
    char token_[1600] = "";  // JWTs from OpenSky are ~1.2 KB
    uint32_t expiresAtMs_ = 0;
};

// Parses /api/states/all. Returns the same shape as parseReadsb.
ParseResult parseOpenSkyStates(hal::IByteSource& src, model::AircraftStore& store, uint32_t nowMs, int64_t epochS);

class OpenSkyProvider : public IProvider {
   public:
    void setCredentials(const char* clientId, const char* clientSecret);
    const char* name() const override { return "opensky"; }
    bool configured() const override { return true; }  // anonymous access works, just slower
    uint32_t pollIntervalMs() const override { return intervalMs_; }
    void setIntervalMs(uint32_t ms) { intervalMs_ = ms; }
    bool poll(hal::IHttp& http, model::AircraftStore& store, const PollContext& ctx) override;
    OpenSkyAuth& auth() { return auth_; }

   private:
    bool refreshToken(hal::IHttp& http, uint32_t nowMs);
    char id_[64] = "", secret_[96] = "";
    OpenSkyAuth auth_;
    uint32_t intervalMs_ = 30000;
};

}  // namespace prov
