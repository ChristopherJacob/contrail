#include "Providers.h"

#include <cstring>

#include "JsonStream.h"
#include "model/Geo.h"
#include "util/Str.h"

namespace prov {
using util::copyStr;
using model::Aircraft;

namespace {
constexpr float kFtPerM = 3.28084f, kKtPerMps = 1.94384f, kFpmPerMps = 196.85f;

}  // namespace

// ------------------------------------------------------------ Community

const CommunityProvider::Host CommunityProvider::hosts_[] = {
    {"adsb.lol", "https://api.adsb.lol/v2/point/%.4f/%.4f/%d"},
    {"adsb.fi", "https://opendata.adsb.fi/api/v3/lat/%.4f/lon/%.4f/dist/%d"},
    {"airplanes.live", "https://api.airplanes.live/v2/point/%.4f/%.4f/%d"},
};

CommunityProvider::CommunityProvider() = default;

bool CommunityProvider::poll(hal::IHttp& http, model::AircraftStore& store, const PollContext& ctx) {
    constexpr int kHosts = sizeof(hosts_) / sizeof(hosts_[0]);
    char url[160];
    std::snprintf(url, sizeof url, hosts_[current_].urlFormat, ctx.lat, ctx.lon, int(ctx.radiusNm + 0.999f));
    ParseResult r;
    hal::HttpStatus st = http.get(url, nullptr, 0, [&](hal::IByteSource& src) {
        r = parseReadsb(src, store, ctx.nowMs, model::Source::Community);
    });
    if (st.ok() && r.ok) return true;
    current_ = (current_ + 1) % kHosts;
    return false;
}

// ------------------------------------------------------------ Local receiver

void LocalReceiverProvider::setUrl(const char* url) { copyStr(url_, sizeof url_, url); }

bool LocalReceiverProvider::poll(hal::IHttp& http, model::AircraftStore& store, const PollContext& ctx) {
    ParseResult r;
    hal::HttpStatus st = http.get(url_, nullptr, 0, [&](hal::IByteSource& src) {
        r = parseReadsb(src, store, ctx.nowMs, model::Source::Local);
    });
    return st.ok() && r.ok;
}

// ------------------------------------------------------------ OpenSky

bool OpenSkyAuth::parseTokenResponse(hal::IByteSource& src, uint32_t nowMs) {
    JsonDocument filter;
    filter["access_token"] = true;
    filter["expires_in"] = true;
    JsonDocument doc;
    SourceReader reader{src};
    if (deserializeJson(doc, reader, DeserializationOption::Filter(filter))) return false;
    const char* tok = doc["access_token"];
    if (!tok || std::strlen(tok) >= sizeof token_) return false;
    std::strcpy(token_, tok);
    expiresAtMs_ = nowMs + uint32_t(doc["expires_in"] | 1800) * 1000;
    return true;
}

ParseResult parseOpenSkyStates(hal::IByteSource& src, model::AircraftStore& store, uint32_t nowMs, int64_t epochS) {
    ParseResult r;
    static const char* const kKeys[] = {"states"};
    if (!seekArray(src, kKeys, 1)) {
        r.ok = true;  // "states": null means no aircraft in the box
        return r;
    }
    JsonDocument doc;
    Aircraft a;
    store.beginUpdate(nowMs);
    r.ok = forEachElement(src, doc, nullptr, [&](JsonVariantConst s) {
        ++r.seen;
        a = Aircraft{};
        a.hex = parseHex(s[0]);
        copyTrimmed(a.callsign, sizeof a.callsign, s[1]);
        if (!s[5].isNull() && !s[6].isNull()) {
            a.lon = s[5].as<float>(), a.lat = s[6].as<float>();
            a.flags |= model::AC_HAS_POS;
        }
        a.altFt = int32_t((s[7] | 0.f) * kFtPerM);
        if (s[8] | false) a.flags |= model::AC_GROUND;
        a.gsKt = (s[9] | 0.f) * kKtPerMps;
        a.trackDeg = s[10] | 0.f;
        a.vrateFpm = int16_t((s[11] | 0.f) * kFpmPerMps);
        copyTrimmed(a.squawk, sizeof a.squawk, s[14]);
        if (isEmergencySquawk(a.squawk)) a.flags |= model::AC_EMERGENCY;
        a.source = model::Source::OpenSky;
        int64_t tpos = s[3].isNull() ? (s[4] | int64_t(0)) : s[3].as<int64_t>();
        int64_t ageS = (epochS > 0 && tpos > 0) ? epochS - tpos : 0;
        if (ageS < 0) ageS = 0;
        uint32_t ageMs = uint32_t(ageS * 1000);
        a.lastSeenMs = ageMs < nowMs ? nowMs - ageMs : 0;
        if (a.hex && store.upsert(a)) ++r.accepted;
    });
    store.endUpdate();
    return r;
}

void OpenSkyProvider::setCredentials(const char* clientId, const char* clientSecret) {
    copyStr(id_, sizeof id_, clientId);
    copyStr(secret_, sizeof secret_, clientSecret);
    auth_.clear();
}

bool OpenSkyProvider::refreshToken(hal::IHttp& http, uint32_t nowMs) {
    char body[200];
    std::snprintf(body, sizeof body, "grant_type=client_credentials&client_id=%s&client_secret=%s", id_, secret_);
    bool ok = false;
    hal::HttpStatus st = http.post(
        "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token", nullptr, 0,
        "application/x-www-form-urlencoded", body,
        [&](hal::IByteSource& src) { ok = auth_.parseTokenResponse(src, nowMs); });
    return st.ok() && ok;
}

bool OpenSkyProvider::poll(hal::IHttp& http, model::AircraftStore& store, const PollContext& ctx) {
    bool authed = id_[0] && secret_[0];
    if (authed && !auth_.valid(ctx.nowMs) && !refreshToken(http, ctx.nowMs)) return false;
    double lamin, lomin, lamax, lomax;
    geo::boundingBox(ctx.lat, ctx.lon, ctx.radiusNm, lamin, lomin, lamax, lomax);
    char url[200];
    std::snprintf(url, sizeof url,
                  "https://opensky-network.org/api/states/all?lamin=%.4f&lomin=%.4f&lamax=%.4f&lomax=%.4f", lamin,
                  lomin, lamax, lomax);
    static char bearer[1610];
    hal::Header h[1];
    int nh = 0;
    if (authed) {
        std::snprintf(bearer, sizeof bearer, "Bearer %s", auth_.token());
        h[nh++] = {"Authorization", bearer};
    }
    ParseResult r;
    hal::HttpStatus st = http.get(url, h, nh, [&](hal::IByteSource& src) {
        r = parseOpenSkyStates(src, store, ctx.nowMs, ctx.epochS);
    });
    if (st.code == 401) auth_.clear();
    return st.ok() && r.ok;
}

}  // namespace prov
