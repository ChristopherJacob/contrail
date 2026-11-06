#include "DemoScenes.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <utility>

#include "data/Airlines.h"
#include "data/Shapes.h"
#include "model/Geo.h"

namespace scr {
namespace {

void base(Scene& s, float sunElev, float sunAz) {
    s = Scene{};
    s.epoch = kDemoEpoch, s.timeValid = true, s.locationSet = true;
    s.homeLat = 37.6189, s.homeLon = -122.375, s.radiusNm = 25;
    s.sunElev = sunElev, s.sunAz = sunAz;
    s.moonElev = -20, s.moonAz = 90, s.moonIllum = 0.64f, s.moonWaxing = true;
    s.online = true, s.dataAgeS = 2;
}

SceneFlight& add(Scene& s, uint32_t hex, const char* cs, const char* reg, const char* type, int32_t alt, int16_t vrate,
                 float gs, float track, float dist, float brg) {
    SceneFlight& f = s.flights[s.count++];
    f = SceneFlight{};
    f.hex = hex;
    std::snprintf(f.callsign, sizeof f.callsign, "%s", cs);
    std::snprintf(f.reg, sizeof f.reg, "%s", reg);
    std::snprintf(f.type, sizeof f.type, "%s", type);
    f.shape = uint8_t(data::familyForType(type));
    if (data::airlineForCallsign(cs)) std::snprintf(f.airline, sizeof f.airline, "%.3s", cs);
    f.altFt = alt, f.vrateFpm = vrate, f.gsKt = gs, f.trackDeg = track, f.distNm = dist, f.bearingDeg = brg;
    f.elevDeg = float(geo::elevationDeg(dist, alt));
    return f;
}

void route(Scene& s, SceneFlight& f, const char* orig, const char* dest, const char* origCity, const char* destCity,
           const char* airline, float progress, int64_t eta) {
    f.hasRoute = true;
    std::snprintf(f.orig, sizeof f.orig, "%s", orig);
    std::snprintf(f.dest, sizeof f.dest, "%s", dest);
    f.progress = progress, f.etaEpoch = eta;
    SceneRoute& r = s.routes[s.routeCount++];
    std::snprintf(r.callsign, sizeof r.callsign, "%s", f.callsign);
    std::snprintf(r.origCity, sizeof r.origCity, "%s", origCity);
    std::snprintf(r.destCity, sizeof r.destCity, "%s", destCity);
    std::snprintf(r.airline, sizeof r.airline, "%s", airline);
}

void named(Scene& s) {
    route(s, add(s, 0xA1B2C3, "UAL53", "N24974", "B789", 34000, 1180, 487, 52, 2.4f, 40), "ZRH", "IAD", "Zurich",
          "Washington Dulles", "United Airlines", 0.62f, kDemoEpoch + 8820);
    route(s, add(s, 0xA2C3D4, "DAL22", "N301DN", "A321", 9000, -1400, 290, 140, 4.1f, 300), "BOS", "ATL", "Boston",
          "Atlanta", "Delta Air Lines", 0.81f, kDemoEpoch + 2100);
    route(s, add(s, 0xA3D4E5, "SWA88", "N8710M", "B38M", 18000, 2100, 380, 75, 5.6f, 160), "LGA", "MDW", "New York",
          "Chicago Midway", "Southwest Airlines", 0.12f, kDemoEpoch + 6600);
    add(s, 0xA4E5F6, "N512KA", "N512KA", "C172", 3500, 0, 105, 210, 6.0f, 250);
    route(s, add(s, 0xA5F607, "AAL101", "N717AN", "B77W", 37000, 0, 520, 60, 8.2f, 20), "JFK", "LHR", "New York",
          "London Heathrow", "American Airlines", 0.05f, kDemoEpoch + 23400);
    route(s, add(s, 0xA60718, "JBU44", "N968JT", "BCS3", 24000, -900, 410, 330, 9.7f, 110), "MCO", "BOS", "Orlando",
          "Boston", "JetBlue", 0.78f, kDemoEpoch + 2900);
}

}  // namespace

void sceneBusy(Scene& s) {
    base(s, 3, 250);
    named(s);
    // Deterministic filler traffic out to the edge of the radius, nearest first.
    static const char* const kPrefixes[] = {"UAL", "DAL", "SWA", "AAL", "ASA", "SKW", "FDX", "UPS", "JBU", "NKS"};
    static const char* const kTypes[] = {"A320", "B738", "E75L", "CRJ9", "B739", "A21N", "B763", "A20N"};
    uint32_t seed = 12345;
    auto rnd = [&seed]() { return (seed = seed * 1664525u + 1013904223u) >> 8; };
    int first = s.count;
    while (s.count < Scene::kMaxFlights) {
        char cs[9];
        // One draw per statement: argument evaluation order differs between compilers.
        const char* prefix = kPrefixes[rnd() % 10];
        unsigned number = 100 + rnd() % 2800;
        std::snprintf(cs, sizeof cs, "%s%u", prefix, number);
        float dist = 10.f + (rnd() % 1500) / 100.f;
        int32_t alt = 4000 + int32_t(rnd() % 36000);
        int16_t vr = int16_t(int(rnd() % 4000) - 2000);
        const char* type = kTypes[rnd() % 8];
        float gs = float(250 + rnd() % 250), track = float(rnd() % 360), brg = float(rnd() % 360);
        add(s, 0xB00000 + s.count, cs, "", type, alt, vr, gs, track, dist, brg);
    }
    std::stable_sort(s.flights + first, s.flights + s.count,
              [](const SceneFlight& a, const SceneFlight& b) { return a.distNm < b.distNm; });
}

void sceneQuiet(Scene& s) {
    base(s, 35, 200);
    route(s, add(s, 0xA1B2C3, "UAL53", "N24974", "B789", 34000, 1180, 487, 52, 2.4f, 40), "ZRH", "IAD", "Zurich",
          "Washington Dulles", "United Airlines", 0.62f, kDemoEpoch + 8820);
}

void sceneEmpty(Scene& s) {
    base(s, -25, 330);
    s.moonElev = 40, s.moonAz = 120;
}

void sceneNight(Scene& s) {
    base(s, -25, 330);
    s.moonElev = 40, s.moonAz = 120;
    // DAL22 high overhead (steep elevation), the others lower around the sky.
    route(s, add(s, 0xA2C3D4, "DAL22", "N301DN", "A321", 9000, -1400, 290, 140, 1.2f, 300), "BOS", "ATL", "Boston",
          "Atlanta", "Delta Air Lines", 0.81f, kDemoEpoch + 2100);
    route(s, add(s, 0xA1B2C3, "UAL53", "N24974", "B789", 34000, 1180, 487, 52, 6.4f, 40), "ZRH", "IAD", "Zurich",
          "Washington Dulles", "United Airlines", 0.62f, kDemoEpoch + 8820);
    add(s, 0xA4E5F6, "N512KA", "N512KA", "C172", 3500, 0, 105, 210, 7.0f, 250);
}

void sceneNoRoute(Scene& s) {
    base(s, 20, 180);
    add(s, 0xA4E5F6, "N512KA", "N512KA", "C172", 3500, 0, 105, 210, 1.6f, 250);
    add(s, 0xACAFE1, "RCH471", "", "C17", 22000, 0, 410, 300, 9.0f, 120).military = true;
}

void sceneOcean(Scene& s) {
    sceneQuiet(s);
    s.homeLat = 30.0, s.homeLon = -140.0;
}

void sceneNoLocation(Scene& s) {
    sceneEmpty(s);
    s.locationSet = false;
}

void fillStats(StatsSummary& t) {
    t = StatsSummary{};
    t.valid = true, t.hour = 18, t.day = 2496, t.ymd = 20261031, t.startYmd = 20260912;
    static const uint16_t kHours[24] = {2, 1, 0, 0, 1, 4, 11, 19, 24, 22, 18, 17, 21, 23, 20, 19, 26, 31, 14, 0, 0, 0, 0, 0};
    std::memcpy(t.hourly, kHours, sizeof kHours);
    for (uint16_t h : kHours) t.flightsToday += h;  // 273
    t.flightsAll = 18420;
    auto set = [](StatsEntry* e, int& n, std::initializer_list<std::pair<const char*, uint32_t>> v) {
        for (const auto& p : v) std::snprintf(e[n].code, sizeof e[n].code, "%s", p.first), e[n++].count = p.second;
    };
    set(t.airlinesToday, t.nAirlinesToday, {{"UAL", 61}, {"SWA", 44}, {"DAL", 31}, {"ASA", 27}, {"SKW", 22}});
    set(t.airlinesAll, t.nAirlinesAll, {{"UAL", 3912}, {"SWA", 2877}, {"ASA", 2140}, {"DAL", 1985}, {"AAL", 1406}});
    set(t.typesAll, t.nTypesAll, {{"B738", 3120}, {"A320", 2650}, {"E75L", 1890}, {"B739", 1544}, {"A321", 1210}});
    t.typesEver = 47, t.typesNewToday = 3;
    std::snprintf(t.newestType, sizeof t.newestType, "BCS3");
    std::snprintf(t.newestCallsign, sizeof t.newestCallsign, "JBU44");
    t.newestEpoch = kDemoEpoch - 1800;
    t.firstSeq = 47;
}

void sceneStats(Scene& s) {
    sceneBusy(s);
    fillStats(s.stats);
}

void sceneStatsEmpty(Scene& s) {
    sceneEmpty(s);
    s.stats = StatsSummary{};
    s.stats.valid = true, s.stats.hour = 18, s.stats.day = 2496, s.stats.ymd = s.stats.startYmd = 20261031;
}

void sceneStatsBig(Scene& s) {
    sceneStats(s);
    StatsSummary& t = s.stats;
    t.flightsAll = 1000000;
    const uint32_t air[] = {1204311, 987654, 412000, 99999, 8765}, typ[] = {2304118, 1876002, 940311, 120450, 9876};
    for (int i = 0; i < 5; ++i) t.airlinesAll[i].count = air[i], t.typesAll[i].count = typ[i];
    t.typesEver = 123, t.typesNewToday = 0;
    std::snprintf(t.newestCallsign, sizeof t.newestCallsign, "SWA12345");
}

void sceneStatsNoClock(Scene& s) {
    sceneStats(s);
    s.timeValid = false;
    s.stats.valid = false;  // the saved day's counts are loaded, but nobody knows whether that day is today
}

}  // namespace scr
