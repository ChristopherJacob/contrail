#pragma once
// Everything the screens draw from, as plain values. CoreApp rebuilds it at 5 Hz on the
// render thread; screens keep a pointer to it and read it in update()/draw().
#include <cstdint>

namespace scr {

// Alert kinds, highest priority first (alerts spec §2).
enum class AlertKind : uint8_t { Emergency, Watchlist, Military, LowPass };

struct SceneFlight {
    uint32_t hex = 0;
    char callsign[9] = "", reg[9] = "", type[5] = "";
    uint8_t shape = 0;           // data::Family: the type's shape family, else the emitter category's; 0 unknown
    char airline[4] = "";        // ICAO airline prefix when the callsign has one
    char squawk[5] = "";         // transponder code, "" unknown
    char orig[4] = "", dest[4] = "";  // IATA codes when the route is known
    int32_t altFt = 0;
    int16_t vrateFpm = 0;
    float gsKt = 0, trackDeg = 0, distNm = 0, bearingDeg = 0, elevDeg = 0;
    float progress = -1;  // 0..1 along the route; < 0 unknown
    int64_t etaEpoch = 0;  // estimated arrival; 0 unknown
    bool hasRoute = false, military = false, emergency = false;
    bool emergencyStatus = false;  // a declared emergency (emergency is that or an emergency squawk)
};

// The alert on screen (alerts::AlertEngine is its only writer). While inactive the last alert's
// fields stay, so the Alert screen can fade out on them.
struct SceneAlert {
    bool active = false;
    AlertKind kind = AlertKind::LowPass;
    bool soft = false;  // quiet hours (never for an emergency): steady border, dim LED, no backlight pulse
    bool live = false;  // the plane is in this scene, so its numbers are current
    char matched[11] = "";  // the watchlist entry that matched
    SceneFlight flight;     // a copy: the alert outlasts the plane leaving the feed
    uint32_t startMs = 0, endsMs = 0;
};

// Longer route text, kept only for flights that have a route.
struct SceneRoute {
    char callsign[9] = "";
    char origCity[24] = "", destCity[24] = "", airline[24] = "";
};

struct StatsEntry {
    char code[5] = "";  // ICAO airline or type code
    uint32_t count = 0;
};

// The logbook as the Stats screen and the director read it (stats::StatsBook::summary fills it).
struct StatsSummary {
    static constexpr int kTop = 5;
    bool valid = false;                  // the clock is set: today's counts and the hour are live
    int hour = 0;                        // the local hour, 0..23
    int day = 0, ymd = 0, startYmd = 0;  // today (day number, yyyymmdd); the first day counted
    uint32_t flightsToday = 0, flightsAll = 0;
    uint16_t hourly[24] = {};
    StatsEntry airlinesToday[kTop], airlinesAll[kTop], typesAll[kTop];  // largest first
    int nAirlinesToday = 0, nAirlinesAll = 0, nTypesAll = 0;
    int typesEver = 0, typesNewToday = 0;
    char newestType[5] = "", newestCallsign[9] = "";  // the latest first sighting of a type
    int64_t newestEpoch = 0;
    uint32_t firstSeq = 0;  // grows with every first sighting: the director's cue (Task 23)
};

struct Scene {
    static constexpr int kMaxFlights = 32, kMaxRoutes = 12;
    SceneFlight flights[kMaxFlights];  // nearest first, airborne only
    int count = 0;
    SceneRoute routes[kMaxRoutes];
    int routeCount = 0;

    int64_t epoch = 0;
    bool timeValid = false, locationSet = false;
    double homeLat = 0, homeLon = 0;
    float radiusNm = 25;
    float sunElev = 5, sunAz = 250;
    float moonElev = -90, moonAz = 0, moonIllum = 0;
    bool moonWaxing = true;
    bool online = true;
    int dataAgeS = -1;     // seconds since the last good poll; -1 = never
    uint32_t pollSeq = 0;  // the store generation the flights came from: the alert engine's two-poll squawk rule
    SceneAlert alert;
    uint32_t version = 0;  // bumped on every rebuild
    StatsSummary stats;    // a value copy, refreshed with every rebuild

    const SceneFlight* find(uint32_t hex) const;
    const SceneRoute* route(const char* callsign) const;
};

}  // namespace scr
