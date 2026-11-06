#pragma once
// Which alert kinds one plane matches (alerts spec §2), and the words the Alert screen uses for them.
#include <cstddef>
#include <cstdint>

#include "AlertConfig.h"
#include "screens/Scene.h"

namespace alerts {

using scr::AlertKind;

constexpr uint8_t kindBit(AlertKind k) { return uint8_t(1u << int(k)); }
// The highest-priority kind in a non-empty set.
AlertKind highest(uint8_t kinds);

struct Match {
    uint8_t kinds = 0;                              // kindBit() per kind matched
    char matched[AlertConfig::kEntryLen + 1] = "";  // the watchlist entry, when Watchlist matched
};

// `squawkConfirmed`: an emergency squawk has been seen on two polls in a row (the engine tracks it).
// Low pass needs a known altitude (> 0); the scene holds airborne planes only.
Match matchFlight(const scr::SceneFlight& f, const AlertConfig& c, bool squawkConfirmed);
// One watchlist entry (upper case, a trailing '*' for a prefix) against a field, ignoring case.
bool entryMatches(const char* entry, const char* value);

// "EMERGENCY", "WATCHLIST", "MILITARY", "LOW PASS".
const char* kindName(AlertKind k);
// "HIJACK", "RADIO FAILURE", "GENERAL EMERGENCY" for 7500 / 7600 / 7700, else null.
const char* squawkMeaning(const char* squawk);
// The alert's reason: "7700 · GENERAL EMERGENCY" (`withSquawk`) or "GENERAL EMERGENCY",
// "DECLARED EMERGENCY", "MATCHED N512*", "MILITARY AIRCRAFT", "LOW AND CLOSE".
void reasonLine(const scr::SceneAlert& a, bool withSquawk, char* out, size_t n);
// The serial test command: "alert emerg|watch|mil|low".
bool parseTestCommand(const char* line, AlertKind& out);

}  // namespace alerts
