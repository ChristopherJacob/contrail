#pragma once
#include "Scene.h"

namespace scr {

constexpr int64_t kDemoEpoch = 1793471700;  // 2026-10-31 18:35 UTC; tests run with TZ=UTC0

void sceneBusy(Scene& s);     // 32 flights, dusk (sun at 3°)
void sceneQuiet(Scene& s);    // one flight, day
void sceneEmpty(Scene& s);    // nothing in range, night, 64% moon
void sceneNight(Scene& s);    // a few flights, night with the moon up, one high overhead
void sceneNoRoute(Scene& s);  // the nearest flight has no known route
void sceneOcean(Scene& s);       // sceneQuiet with home in the North Pacific: no land tiles anywhere near
void sceneNoLocation(Scene& s);  // sceneEmpty without a home location
void fillStats(StatsSummary& t);     // a well-filled logbook at 18:35 (goldens, bench)
void sceneStats(Scene& s);           // sceneBusy with fillStats
void sceneStatsEmpty(Scene& s);      // sceneEmpty, the clock set, nothing counted yet
void sceneStatsNoClock(Scene& s);
void sceneStatsBig(Scene& s);        // years of logbook: 1,000,000 flights, 7-digit totals, 123 types, 8-char callsign    // sceneStats before the clock is set: last saved counts, no live day

}  // namespace scr
