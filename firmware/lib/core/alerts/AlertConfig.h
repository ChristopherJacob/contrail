#pragma once
// What the alert engine checks for (alerts spec §5), built from the settings.
#include <cstdint>

namespace svc {
class Settings;
}

namespace alerts {

struct AlertConfig {
    static constexpr int kMaxEntries = 24, kEntryLen = 10;  // longer than any tail, callsign or hex
    bool emergency = false, watchlist = false, military = false, lowPass = false;
    int32_t lowFt = 3000;
    float lowNm = 2;
    int quietFrom = 22 * 60, quietTo = 7 * 60;  // minutes past local midnight; equal: no quiet hours
    char entries[kMaxEntries][kEntryLen + 1] = {};  // upper case; a trailing '*' matches a prefix
    int nEntries = 0;
    bool any() const { return emergency || watchlist || military || lowPass; }
};

// Comma-separated entries: trimmed and upper-cased; empty ones, a lone "*" and ones over kEntryLen
// characters are skipped; at most kMaxEntries kept.
void parseWatchlist(const char* text, AlertConfig& c);
// "HH:MM" (hours 0..23, one or two digits) as minutes past midnight; anything else gives `fallback`.
int parseClockMinutes(const char* hhmm, int fallback);
AlertConfig configFromSettings(const svc::Settings& s);

}  // namespace alerts
