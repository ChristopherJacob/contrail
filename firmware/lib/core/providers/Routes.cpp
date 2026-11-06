#include "Routes.h"

#include <cstdio>
#include <cstring>

#include "JsonStream.h"
#include "model/Geo.h"
#include "util/Str.h"

namespace prov {
using util::copyStr;

// ------------------------------------------------------------ RouteCache

const Route* RouteCache::find(const char* cs, uint32_t nowMs) {
    for (int i = 0; i < n_; ++i) {
        if (std::strcmp(items_[i].callsign, cs) != 0) continue;
        uint32_t ttl = items_[i].known ? kKnownTtlMs : kUnknownTtlMs;
        if (nowMs - items_[i].fetchedMs > ttl) return nullptr;
        used_[i] = ++tick_;
        return &items_[i];
    }
    return nullptr;
}

void RouteCache::put(const Route& r) {
    int slot = -1;
    for (int i = 0; i < n_; ++i)
        if (!std::strcmp(items_[i].callsign, r.callsign)) slot = i;
    if (slot < 0 && n_ < kCapacity) slot = n_++;
    if (slot < 0) {
        slot = 0;
        for (int i = 1; i < n_; ++i)
            if (used_[i] < used_[slot]) slot = i;
    }
    items_[slot] = r;
    used_[slot] = ++tick_;
}

// ------------------------------------------------------------ parsers

bool parseAdsbdb(hal::IByteSource& src, Route& out) {
    JsonDocument doc;
    SourceReader reader{src};
    if (deserializeJson(doc, reader)) return false;
    JsonVariantConst fr = doc["response"]["flightroute"];
    out.known = !fr.isNull();
    if (!out.known) return true;
    copyStr(out.origIata, sizeof out.origIata, fr["origin"]["iata_code"]);
    copyStr(out.origIcao, sizeof out.origIcao, fr["origin"]["icao_code"]);
    copyStr(out.origCity, sizeof out.origCity, fr["origin"]["municipality"]);
    copyStr(out.destIata, sizeof out.destIata, fr["destination"]["iata_code"]);
    copyStr(out.destIcao, sizeof out.destIcao, fr["destination"]["icao_code"]);
    copyStr(out.destCity, sizeof out.destCity, fr["destination"]["municipality"]);
    copyStr(out.airline, sizeof out.airline, fr["airline"]["name"]);
    copyStr(out.airlineIcao, sizeof out.airlineIcao, fr["airline"]["icao"]);
    if (!fr["origin"]["latitude"].isNull()) {
        out.origLat = fr["origin"]["latitude"], out.origLon = fr["origin"]["longitude"];
        out.destLat = fr["destination"]["latitude"], out.destLon = fr["destination"]["longitude"];
    }
    return true;
}

bool routePlausible(const Route& r, double lat, double lon, double tol) {
    if (std::isnan(r.origLat) || std::isnan(r.destLat)) return true;
    return geo::segmentDistanceNm(r.origLat, r.origLon, r.destLat, r.destLon, lat, lon) <= tol;
}

bool parseAeroApiFlights(hal::IByteSource& src, Route& out) {
    JsonDocument filter;
    JsonObject f = filter["flights"].add<JsonObject>();
    for (const char* k : {"actual_off", "actual_on", "progress_percent", "operator"}) f[k] = true;
    for (const char* side : {"origin", "destination"})
        for (const char* k : {"code_iata", "code_icao", "city"}) f[side][k] = true;
    JsonDocument doc;
    SourceReader reader{src};
    if (deserializeJson(doc, reader, DeserializationOption::Filter(filter))) return false;
    out.known = false;
    for (JsonVariantConst fl : doc["flights"].as<JsonArrayConst>()) {
        if (fl["actual_off"].isNull() || !fl["actual_on"].isNull()) continue;
        out.known = true;
        copyStr(out.origIata, sizeof out.origIata, fl["origin"]["code_iata"]);
        copyStr(out.origIcao, sizeof out.origIcao, fl["origin"]["code_icao"]);
        copyStr(out.origCity, sizeof out.origCity, fl["origin"]["city"]);
        copyStr(out.destIata, sizeof out.destIata, fl["destination"]["code_iata"]);
        copyStr(out.destIcao, sizeof out.destIcao, fl["destination"]["code_icao"]);
        copyStr(out.destCity, sizeof out.destCity, fl["destination"]["city"]);
        copyStr(out.airlineIcao, sizeof out.airlineIcao, fl["operator"]);
        out.progressPct = int8_t(fl["progress_percent"] | -1);
        break;
    }
    return true;
}

// ------------------------------------------------------------ clients

bool AdsbdbClient::lookup(hal::IHttp& http, const char* cs, uint32_t nowMs, Route& out) {
    char url[96];
    std::snprintf(url, sizeof url, "https://%s/v0/callsign/%s", kHost, cs);
    out = Route{};
    copyStr(out.callsign, sizeof out.callsign, cs);
    out.fetchedMs = nowMs;
    bool parsed = false;
    hal::HttpStatus st = http.get(url, nullptr, 0, [&](hal::IByteSource& src) { parsed = parseAdsbdb(src, out); });
    if (st.code == 404) return out.known = false, true;  // adsbdb answers unknown callsigns with 404
    return st.ok() && parsed;
}

void AeroApiClient::setKey(const char* key) { copyStr(key_, sizeof key_, key); }

bool AeroApiClient::budgetAvailable(int64_t epochS) {
    int64_t day = epochS / 86400;
    if (day != day_) day_ = day, used_ = 0;
    return used_ < cap_;
}

bool AeroApiClient::lookup(hal::IHttp& http, const char* cs, uint32_t nowMs, int64_t epochS, Route& out) {
    if (!configured() || !budgetAvailable(epochS)) return false;
    ++used_;
    char url[128];
    std::snprintf(url, sizeof url, "https://%s/aeroapi/flights/%s?max_pages=1", kHost, cs);
    hal::Header h[] = {{"x-apikey", key_}};
    out = Route{};
    copyStr(out.callsign, sizeof out.callsign, cs);
    out.fetchedMs = nowMs;
    bool parsed = false;
    hal::HttpStatus st = http.get(url, h, 1, [&](hal::IByteSource& src) { parsed = parseAeroApiFlights(src, out); });
    return st.ok() && parsed;
}

}  // namespace prov
