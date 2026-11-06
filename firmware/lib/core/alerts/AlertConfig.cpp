#include "AlertConfig.h"

#include <cctype>
#include <cstring>

#include "services/CoreSettings.h"

namespace alerts {

void parseWatchlist(const char* text, AlertConfig& c) {
    c.nEntries = 0;
    const char* p = text ? text : "";
    while (*p && c.nEntries < AlertConfig::kMaxEntries) {
        const char* end = std::strchr(p, ',');
        if (!end) end = p + std::strlen(p);
        const char *a = p, *b = end;
        while (a < b && std::isspace((unsigned char)*a)) ++a;
        while (b > a && std::isspace((unsigned char)b[-1])) --b;
        const size_t n = size_t(b - a);
        if (n > 0 && n <= size_t(AlertConfig::kEntryLen) && !(n == 1 && *a == '*')) {
            char* out = c.entries[c.nEntries++];
            for (size_t i = 0; i < n; ++i) out[i] = char(std::toupper((unsigned char)a[i]));
            out[n] = 0;
        }
        p = *end ? end + 1 : end;
    }
}

int parseClockMinutes(const char* s, int fallback) {
    if (!s) return fallback;
    const char *a = s, *b = s + std::strlen(s);
    while (a < b && std::isspace((unsigned char)*a)) ++a;
    while (b > a && std::isspace((unsigned char)b[-1])) --b;
    auto digit = [](char c) { return c >= '0' && c <= '9'; };
    // "H:MM" or "HH:MM": one or two hour digits, exactly two minute digits.
    const char* colon = a;
    while (colon < b && digit(*colon)) ++colon;
    const long hDigits = colon - a;
    if (hDigits < 1 || hDigits > 2 || b - colon != 3 || *colon != ':' || !digit(colon[1]) || !digit(colon[2]))
        return fallback;
    const int h = hDigits == 2 ? (a[0] - '0') * 10 + (a[1] - '0') : a[0] - '0';
    const int m = (colon[1] - '0') * 10 + (colon[2] - '0');
    if (h > 23 || m > 59) return fallback;
    return h * 60 + m;
}

AlertConfig configFromSettings(const svc::Settings& s) {
    using namespace svc;
    AlertConfig c;
    c.emergency = s.getBool(key::kAlertEmerg);
    c.watchlist = s.getBool(key::kAlertWatch);
    c.military = s.getBool(key::kAlertMil);
    c.lowPass = s.getBool(key::kAlertLow);
    c.lowFt = s.getInt(key::kAlertLowFt);
    c.lowNm = s.getFloat(key::kAlertLowNm);
    c.quietFrom = parseClockMinutes(s.getString(key::kQuietFrom).c_str(), 22 * 60);
    c.quietTo = parseClockMinutes(s.getString(key::kQuietTo).c_str(), 7 * 60);
    parseWatchlist(s.getString(key::kWatchlist).c_str(), c);
    return c;
}

}  // namespace alerts
