#include "ReadsbParser.h"

#include <cstdlib>
#include <cstring>

#include "JsonStream.h"

namespace prov {
using model::Aircraft;

uint32_t parseHex(const char* s) {
    if (!s) return 0;
    uint32_t nonIcao = 0;
    if (*s == '~') nonIcao = 1u << 24, ++s;
    return uint32_t(std::strtoul(s, nullptr, 16) & 0xFFFFFF) | nonIcao;
}

void copyTrimmed(char* dst, size_t cap, const char* src) {
    if (!src) src = "";
    while (*src == ' ') ++src;
    size_t n = std::strlen(src);
    while (n && src[n - 1] == ' ') --n;
    if (n >= cap) n = cap - 1;
    std::memcpy(dst, src, n);
    dst[n] = 0;
}

bool isEmergencySquawk(const char* sq) {
    return sq && (!std::strcmp(sq, "7500") || !std::strcmp(sq, "7600") || !std::strcmp(sq, "7700"));
}

uint8_t parseCategory(const char* s) {
    if (!s || std::strlen(s) != 2) return 0;
    return uint8_t(std::strtoul(s, nullptr, 16));
}

namespace {

const JsonDocument& filter() {
    static JsonDocument f = [] {
        JsonDocument d;
        for (const char* k : {"hex", "flight", "r", "t", "lat", "lon", "alt_baro", "gs", "track", "true_heading",
                              "baro_rate", "geom_rate", "squawk", "emergency", "dbFlags", "category", "seen", "seen_pos"})
            d[k] = true;
        d["lastPosition"]["lat"] = true;
        d["lastPosition"]["lon"] = true;
        return d;
    }();
    return f;
}

void toAircraft(JsonVariantConst e, Aircraft& a, uint32_t nowMs, model::Source source) {
    a = Aircraft{};
    a.hex = parseHex(e["hex"]);
    copyTrimmed(a.callsign, sizeof a.callsign, e["flight"]);
    copyTrimmed(a.reg, sizeof a.reg, e["r"]);
    copyTrimmed(a.type, sizeof a.type, e["t"]);
    copyTrimmed(a.squawk, sizeof a.squawk, e["squawk"]);
    a.source = source;

    JsonVariantConst lat = e["lat"], lon = e["lon"];
    if (lat.isNull()) lat = e["lastPosition"]["lat"], lon = e["lastPosition"]["lon"];
    if (!lat.isNull() && !lon.isNull()) {
        a.lat = lat.as<float>(), a.lon = lon.as<float>();
        a.flags |= model::AC_HAS_POS;
    }

    JsonVariantConst alt = e["alt_baro"];
    if (alt.is<const char*>()) {
        if (!std::strcmp(alt.as<const char*>(), "ground")) a.flags |= model::AC_GROUND;
    } else {
        a.altFt = alt.as<int32_t>();
    }
    a.gsKt = e["gs"] | 0.f;
    a.trackDeg = e["track"].isNull() ? (e["true_heading"] | 0.f) : e["track"].as<float>();
    a.vrateFpm = int16_t(e["baro_rate"].isNull() ? (e["geom_rate"] | 0) : e["baro_rate"].as<int>());

    const char* em = e["emergency"];
    if (em && std::strcmp(em, "none") != 0) a.flags |= model::AC_EMERG_STATUS;
    if (a.has(model::AC_EMERG_STATUS) || isEmergencySquawk(a.squawk)) a.flags |= model::AC_EMERGENCY;
    if ((e["dbFlags"] | 0) & 1) a.flags |= model::AC_MILITARY;
    a.category = parseCategory(e["category"]);

    float seen = e["seen_pos"].isNull() ? (e["seen"] | 0.f) : e["seen_pos"].as<float>();
    uint32_t ageMs = uint32_t(seen * 1000);
    a.lastSeenMs = ageMs < nowMs ? nowMs - ageMs : 0;
}

}  // namespace

ParseResult parseReadsb(hal::IByteSource& src, model::AircraftStore& store, uint32_t nowMs, model::Source source) {
    ParseResult r;
    static const char* const kKeys[] = {"ac", "aircraft"};
    if (!seekArray(src, kKeys, 2)) return r;
    JsonDocument doc;
    Aircraft a;
    store.beginUpdate(nowMs);
    r.ok = forEachElement(src, doc, &filter(), [&](JsonVariantConst e) {
        ++r.seen;
        toAircraft(e, a, nowMs, source);
        if (a.hex && store.upsert(a)) ++r.accepted;
    });
    store.endUpdate();
    return r;
}

}  // namespace prov
