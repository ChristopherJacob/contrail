#include "AlertRules.h"

#include <cctype>
#include <cstdio>
#include <cstring>

#include "providers/ReadsbParser.h"
#include "util/Str.h"

namespace alerts {

AlertKind highest(uint8_t kinds) {
    for (int k = 0; k < 4; ++k)
        if (kinds & (1u << k)) return AlertKind(k);
    return AlertKind::LowPass;
}

bool entryMatches(const char* entry, const char* value) {
    if (!value[0]) return false;
    size_t n = std::strlen(entry);
    const bool prefix = n && entry[n - 1] == '*';
    if (prefix) --n;
    size_t i = 0;
    for (; i < n; ++i)
        if (!value[i] || std::toupper((unsigned char)value[i]) != entry[i]) return false;
    return prefix || !value[i];
}

Match matchFlight(const scr::SceneFlight& f, const AlertConfig& c, bool squawkConfirmed) {
    Match m;
    if (c.emergency && (f.emergencyStatus || (squawkConfirmed && prov::isEmergencySquawk(f.squawk))))
        m.kinds |= kindBit(AlertKind::Emergency);
    if (c.watchlist) {
        char hex[7];
        std::snprintf(hex, sizeof hex, "%06X", unsigned(f.hex & 0xFFFFFFu));
        const char* const fields[] = {f.reg, f.callsign, hex, f.type};
        for (int i = 0; i < c.nEntries && !(m.kinds & kindBit(AlertKind::Watchlist)); ++i)
            for (const char* v : fields)
                if (entryMatches(c.entries[i], v)) {
                    m.kinds |= kindBit(AlertKind::Watchlist);
                    util::copyStr(m.matched, sizeof m.matched, c.entries[i]);
                    break;
                }
    }
    if (c.military && f.military) m.kinds |= kindBit(AlertKind::Military);
    if (c.lowPass && f.altFt > 0 && f.altFt < c.lowFt && f.distNm < c.lowNm) m.kinds |= kindBit(AlertKind::LowPass);
    return m;
}

const char* kindName(AlertKind k) {
    switch (k) {
        case AlertKind::Emergency: return "EMERGENCY";
        case AlertKind::Watchlist: return "WATCHLIST";
        case AlertKind::Military: return "MILITARY";
        case AlertKind::LowPass: return "LOW PASS";
    }
    return "";
}

const char* squawkMeaning(const char* sq) {
    if (!std::strcmp(sq, "7500")) return "HIJACK";
    if (!std::strcmp(sq, "7600")) return "RADIO FAILURE";
    if (!std::strcmp(sq, "7700")) return "GENERAL EMERGENCY";
    return nullptr;
}

void reasonLine(const scr::SceneAlert& a, bool withSquawk, char* out, size_t n) {
    switch (a.kind) {
        case AlertKind::Emergency: {
            const char* m = squawkMeaning(a.flight.squawk);
            if (!m) util::copyStr(out, n, "DECLARED EMERGENCY");
            else if (withSquawk) std::snprintf(out, n, "%s · %s", a.flight.squawk, m);
            else util::copyStr(out, n, m);
            return;
        }
        case AlertKind::Watchlist: std::snprintf(out, n, "MATCHED %s", a.matched); return;
        case AlertKind::Military: util::copyStr(out, n, "MILITARY AIRCRAFT"); return;
        case AlertKind::LowPass: util::copyStr(out, n, "LOW AND CLOSE"); return;
    }
}

bool parseTestCommand(const char* line, AlertKind& out) {
    static const struct {
        const char* word;
        AlertKind kind;
    } kWords[] = {{"emerg", AlertKind::Emergency}, {"watch", AlertKind::Watchlist},
                  {"mil", AlertKind::Military}, {"low", AlertKind::LowPass}};
    if (std::strncmp(line, "alert ", 6) != 0) return false;
    for (const auto& w : kWords)
        if (!std::strcmp(line + 6, w.word)) return out = w.kind, true;
    return false;
}

}  // namespace alerts
