#include "StatsBook.h"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <initializer_list>

#include "data/AircraftTypes.h"
#include "data/Airlines.h"
#include "services/Log.h"

#if defined(CONTRAIL_ESP32)
#include <esp_attr.h>
#endif

namespace stats {

namespace {

#if defined(CONTRAIL_ESP32)
// RTC slow memory: no heap, and kept through a crash reset (SeenFilter::begin checks the stamp).
RTC_NOINIT_ATTR uint32_t gSeenWords[SeenFilter::kWords];
#else
uint32_t gSeenWords[SeenFilter::kWords];
#endif

constexpr uint32_t kMagic = 0x5EE17DA7u;

uint32_t mix(uint32_t x) {
    x ^= x >> 16, x *= 0x7FEB352Du, x ^= x >> 15, x *= 0x846CA68Bu, x ^= x >> 16;
    return x;
}

// Days since 1970-01-01 (H. Hinnant's days_from_civil).
int daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = unsigned(y - era * 400);
    const unsigned doy = unsigned((153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + int(doe) - 719468;
}

void copyAlnum(char* out, size_t n, const char* s) {
    size_t k = 0;
    for (; s && *s && k + 1 < n; ++s)
        if (std::isalnum((unsigned char)*s)) out[k++] = *s;
    out[k] = 0;
}

}  // namespace

int dayNumber(int y, int m, int d) { return daysFromCivil(y, m, d) - daysFromCivil(2020, 1, 1) + 1; }

LocalTime localTime(int64_t epoch) {
    time_t t = time_t(epoch);
    struct tm tm;
    localtime_r(&t, &tm);
    LocalTime lt;
    lt.day = dayNumber(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    lt.ymd = (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
    lt.hour = tm.tm_hour;
    return lt;
}

uint32_t* deviceSeenWords() { return gSeenWords; }

uint32_t packCode(const char* s) {
    uint32_t v = 0;
    bool end = false;
    for (int i = 0; i < 4; ++i) {
        const uint8_t c = end ? 0 : uint8_t(s[i]);
        end = end || !c;
        v = v << 8 | c;
    }
    return v;
}

void unpackCode(uint32_t v, char out[5]) {
    for (int i = 0; i < 4; ++i) out[i] = char(v >> (24 - 8 * i));
    out[4] = 0;
}

void SeenFilter::begin(int day) {
    if (w_[0] != kMagic || w_[1] != uint32_t(day)) reset(day);
}

void SeenFilter::reset(int day) {
    std::memset(w_ + 2, 0, (kWords - 2) * sizeof(uint32_t));
    w_[0] = kMagic, w_[1] = uint32_t(day);
}

namespace {
void probesFor(uint32_t hex, uint32_t out[3]) {
    out[0] = mix(hex), out[1] = mix(hex ^ 0x5BD1E995u), out[2] = mix(hex + 0x27D4EB2Fu);
}
}  // namespace

bool SeenFilter::contains(uint32_t hex) const {
    uint32_t probes[3];
    probesFor(hex, probes);
    for (uint32_t v : probes) {
        const uint32_t b = v & (kBits - 1);
        if (!(w_[2 + (b >> 5)] & (1u << (b & 31)))) return false;
    }
    return true;
}

bool SeenFilter::testAndSet(uint32_t hex) {
    uint32_t probes[3];
    probesFor(hex, probes);
    bool seen = true;
    for (uint32_t v : probes) {
        const uint32_t b = v & (kBits - 1);
        uint32_t& word = w_[2 + (b >> 5)];
        const uint32_t m = 1u << (b & 31);
        if (!(word & m)) seen = false, word |= m;
    }
    return seen;
}

StatsBook::StatsBook(hal::IKeyValueStore& kv, uint32_t* seenWords) : kv_(kv), seen_(seenWords) {}

void StatsBook::clear() {
    now_ = LocalTime{}, minute_ = INT64_MIN;
    day_ = ymd_ = startYmd_ = 0;
    valid_ = dayEnded_ = dirty_ = false;
    std::memset(hourly_, 0, sizeof hourly_);
    flightsToday_ = flightsAll_ = firstSeq_ = 0;
    for (Count* t : {airlinesToday_, airlinesAll_, typesToday_, typesAll_})
        for (int i = 0; i < kTop; ++i) t[i] = Count{};
    std::memset(typeCodes_, 0, sizeof typeCodes_);
    std::memset(typeDays_, 0, sizeof typeDays_);
    typesEver_ = 0;
    for (auto& s : newest_) s = Sighting{};
}

void StatsBook::rollTo(const LocalTime& lt) {
    if (day_) dayEnded_ = true;  // a day just ended: save it at once (spec: at local midnight)
    if (!startYmd_) startYmd_ = lt.ymd;
    day_ = lt.day, ymd_ = lt.ymd;
    std::memset(hourly_, 0, sizeof hourly_);
    flightsToday_ = 0;
    for (Count* t : {airlinesToday_, typesToday_})
        for (int i = 0; i < kTop; ++i) t[i] = Count{};
    seen_.reset(day_);
    dirty_ = true;
}

void StatsBook::bump(Count* t, const char* code) {
    int slot = -1;
    for (int i = 0; i < kTop; ++i) {
        if (t[i].code[0] && !std::strncmp(t[i].code, code, 4)) {
            ++t[i].n;
            return;
        }
        // Not this one: remember the first empty slot, else the smallest entry.
        if (slot < 0 || (t[slot].code[0] && (!t[i].code[0] || t[i].n < t[slot].n))) slot = i;
    }
    // A full table evicts its smallest entry and the newcomer starts at 1, so the last slot
    // churns among rare codes. Counts stay exact for what is shown (the top 5 of 16); handing
    // the evicted count on (space-saving) would let a late riser climb, but shows inflated counts.
    std::snprintf(t[slot].code, sizeof t[slot].code, "%.4s", code);
    t[slot].n = 1;
}

void StatsBook::observe(const scr::Scene& s) {
    svc::LockGuard g(mu_);
    if (!s.timeValid) {
        valid_ = false;
        return;
    }
    if (s.epoch / 60 != minute_) minute_ = s.epoch / 60, now_ = localTime(s.epoch);  // localtime once a minute
    // Only forward: a clock stepped back a little across midnight (NTP correcting a fast clock) must not
    // reset today twice. A jump of more than a day back is a new day all the same.
    if (now_.day > day_ || now_.day < day_ - 1) rollTo(now_);
    valid_ = true;
    for (int i = 0; i < s.count; ++i) {
        const scr::SceneFlight& f = s.flights[i];
        if (!f.hex || f.distNm > s.radiusNm || seen_.testAndSet(f.hex)) continue;
        ++flightsToday_, ++flightsAll_;
        if (hourly_[now_.hour] < 65535) ++hourly_[now_.hour];
        char al[4];
        if (data::callsignAirline(f.callsign, al)) bump(airlinesToday_, al), bump(airlinesAll_, al);
        if (data::validTypeCode(f.type)) {  // any ICAO designator, named in the table or not
            bump(typesToday_, f.type), bump(typesAll_, f.type);
            if (addType(f.type, day_)) {
                ++firstSeq_;
                for (int k = kNewest - 1; k > 0; --k) newest_[k] = newest_[k - 1];
                std::snprintf(newest_[0].type, sizeof newest_[0].type, "%s", f.type);
                newest_[0].epoch = s.epoch;
                copyAlnum(newest_[0].callsign, sizeof newest_[0].callsign, f.callsign);
            }
        }
        dirty_ = true;
    }
}

bool StatsBook::addType(const char* code, int day) {
    const uint32_t c = packCode(code);
    uint32_t* const end = typeCodes_ + typesEver_;
    uint32_t* at = std::lower_bound(typeCodes_, end, c);
    if (at != end && *at == c) return false;
    if (typesEver_ == kMaxTypes) return false;  // the cap: later new types count in the tables only
    const int i = int(at - typeCodes_);
    std::memmove(at + 1, at, size_t(typesEver_ - i) * sizeof *at);
    std::memmove(typeDays_ + i + 1, typeDays_ + i, size_t(typesEver_ - i) * sizeof *typeDays_);
    *at = c, typeDays_[i] = uint16_t(day), ++typesEver_;
    return true;
}

int StatsBook::top(const Count* t, scr::StatsEntry* out) {
    bool used[kTop] = {};
    int n = 0;
    for (; n < scr::StatsSummary::kTop; ++n) {
        int best = -1;
        for (int i = 0; i < kTop; ++i) {
            if (used[i] || !t[i].code[0]) continue;
            if (best < 0 || t[i].n > t[best].n || (t[i].n == t[best].n && std::strcmp(t[i].code, t[best].code) < 0))
                best = i;
        }
        if (best < 0) break;
        used[best] = true;
        std::snprintf(out[n].code, sizeof out[n].code, "%s", t[best].code);
        out[n].count = t[best].n;
    }
    return n;
}

void StatsBook::summary(scr::StatsSummary& o) const {
    svc::LockGuard g(mu_);
    o = scr::StatsSummary{};
    o.valid = valid_, o.hour = now_.hour, o.day = day_, o.ymd = ymd_, o.startYmd = startYmd_;
    o.flightsToday = flightsToday_, o.flightsAll = flightsAll_, o.firstSeq = firstSeq_;
    std::memcpy(o.hourly, hourly_, sizeof hourly_);
    o.nAirlinesToday = top(airlinesToday_, o.airlinesToday);
    o.nAirlinesAll = top(airlinesAll_, o.airlinesAll);
    o.nTypesAll = top(typesAll_, o.typesAll);
    o.typesEver = typesEver_;
    for (int i = 0; i < typesEver_; ++i) o.typesNewToday += typeDays_[i] == day_;
    if (newest_[0].type[0]) {
        std::snprintf(o.newestType, sizeof o.newestType, "%s", newest_[0].type);
        std::snprintf(o.newestCallsign, sizeof o.newestCallsign, "%s", newest_[0].callsign);
        o.newestEpoch = newest_[0].epoch;
    }
}

// ------------------------------------------------------------------ persistence
//
// Eight string keys, each "payload#<fnv-1a of payload, 8 hex digits>":
//   st_day     1,<day>,<ymd>,<startYmd>,<flightsToday>,<flightsAll>,<24 hourly counts>
//   st_air     airlines today then all-time: "UAL:12;DAL:9;/UAL:1203;…"
//   st_typ     types, the same way
//   st_new     the newest first sightings: "BCS3:JBU44:1793469900;…"
//   st_first0…3  every type ever seen and its first day, 100 codes per key in sorted order:
//              "A320:2401;B738:2496;C30J:2510;…" (≤ 10 characters a code, so ≤ ~1 KB a key)

namespace {

const char* const kKeys[] = {"st_day",    "st_air",    "st_typ",    "st_new",
                             "st_first0", "st_first1", "st_first2", "st_first3"};
constexpr int kKeyCount = 8, kFirstPerKey = 100;
static_assert(kFirstPerKey * (kKeyCount - 4) >= StatsBook::kMaxTypes, "first sightings need more keys");
constexpr size_t kBuf = 1200;  // on the caller's stack: the network task at save, setup() at load

uint32_t fnv(const char* s) {
    uint32_t h = 2166136261u;
    while (*s) h = (h ^ uint8_t(*s++)) * 16777619u;
    return h;
}

struct Out {
    char* b;
    size_t cap, n = 0;
    bool ok = true;
    Out(char* buf, size_t c) : b(buf), cap(c) { b[0] = 0; }
    __attribute__((format(printf, 2, 3))) void add(const char* fmt, ...) {
        if (!ok) return;
        va_list ap;
        va_start(ap, fmt);
        int w = std::vsnprintf(b + n, cap - n, fmt, ap);
        va_end(ap);
        if (w < 0 || size_t(w) >= cap - n) {
            ok = false, b[n] = 0;
            return;
        }
        n += size_t(w);
    }
};

// A cursor over saved text: any mismatch clears ok.
struct In {
    const char* p;
    bool ok = true;
    void expect(char c) {
        if (*p == c) ++p;
        else ok = false;
    }
    long long num() {
        char* e;
        long long v = std::strtoll(p, &e, 10);
        if (e == p) ok = false;
        p = e;
        return v;
    }
    void word(char* out, size_t n) {
        size_t k = 0;
        while (std::isalnum((unsigned char)*p) && k + 1 < n) out[k++] = *p++;
        out[k] = 0;
        if (std::isalnum((unsigned char)*p)) ok = false;  // longer than the field
    }
};

// "payload#hash" → payload, in place; false if the hash does not match.
bool unwrap(char* s) {
    char* h = std::strrchr(s, '#');
    if (!h || std::strlen(h + 1) != 8) return false;
    for (const char* q = h + 1; *q; ++q)
        if (!std::isxdigit((unsigned char)*q)) return false;
    *h = 0;
    return uint32_t(std::strtoul(h + 1, nullptr, 16)) == fnv(s);
}

void writeCounts(Out& o, const StatsBook::Count* t) {
    for (int i = 0; i < StatsBook::kTop; ++i)
        if (t[i].code[0]) o.add("%s:%lu;", t[i].code, (unsigned long)t[i].n);
}

bool readCounts(In& in, StatsBook::Count* t) {
    for (int i = 0; in.ok && *in.p && *in.p != '/'; ++i) {
        if (i >= StatsBook::kTop) return false;
        in.word(t[i].code, sizeof t[i].code);
        in.expect(':');
        long long n = in.num();
        in.expect(';');
        if (!t[i].code[0] || n < 0 || n > 0xFFFFFFFFLL) return false;
        t[i].n = uint32_t(n);
    }
    return in.ok;
}

}  // namespace

bool StatsBook::serialize(int k, char* buf, size_t cap) const {
    Out o(buf, cap);
    switch (k) {
        case 0:
            o.add("1,%d,%d,%d,%lu,%lu", day_, ymd_, startYmd_, (unsigned long)flightsToday_,
                  (unsigned long)flightsAll_);
            for (uint16_t h : hourly_) o.add(",%u", unsigned(h));
            break;
        case 1:
            writeCounts(o, airlinesToday_), o.add("/"), writeCounts(o, airlinesAll_);
            break;
        case 2:
            writeCounts(o, typesToday_), o.add("/"), writeCounts(o, typesAll_);
            break;
        case 3:
            for (const Sighting& s : newest_)
                if (s.type[0]) o.add("%s:%s:%lld;", s.type, s.callsign, (long long)s.epoch);
            break;
        default: {
            const int from = (k - 4) * kFirstPerKey;
            for (int i = from; i < from + kFirstPerKey && i < typesEver_; ++i) {
                char code[5];
                unpackCode(typeCodes_[i], code);
                o.add("%s:%u;", code, unsigned(typeDays_[i]));
            }
            if (o.ok && !o.n) return true;  // nothing in this chunk: an empty value erases the key
        }
    }
    if (!o.ok) return false;
    const uint32_t h = fnv(buf);
    o.add("#%08lx", (unsigned long)h);
    return o.ok;
}

bool StatsBook::parse(int k, const char* text) {
    In in{text};
    switch (k) {
        case 0: {
            if (in.num() != 1) return false;  // format version
            long long v[5];
            for (auto& x : v) in.expect(','), x = in.num();
            if (!in.ok || v[0] < 1 || v[0] > 65535 || v[3] < 0 || v[4] < v[3] || v[4] > 0xFFFFFFFFLL) return false;
            day_ = int(v[0]), ymd_ = int(v[1]), startYmd_ = int(v[2]);
            flightsToday_ = uint32_t(v[3]), flightsAll_ = uint32_t(v[4]);
            for (uint16_t& h : hourly_) {
                in.expect(',');
                long long n = in.num();
                if (n < 0 || n > 65535) return false;
                h = uint16_t(n);
            }
            break;
        }
        case 1:
        case 2: {
            Count* today = k == 1 ? airlinesToday_ : typesToday_;
            Count* all = k == 1 ? airlinesAll_ : typesAll_;
            if (!readCounts(in, today)) return false;
            in.expect('/');
            if (!readCounts(in, all)) return false;
            break;
        }
        case 3:
            for (int i = 0; in.ok && *in.p; ++i) {
                char code[5], cs[9];
                in.word(code, sizeof code), in.expect(':'), in.word(cs, sizeof cs), in.expect(':');
                long long e = in.num();
                in.expect(';');
                if (!in.ok) return false;
                if (!data::validTypeCode(code)) return false;
                if (i < kNewest) {
                    std::snprintf(newest_[i].type, sizeof newest_[i].type, "%s", code);
                    std::snprintf(newest_[i].callsign, sizeof newest_[i].callsign, "%s", cs);
                    newest_[i].epoch = e;
                }
            }
            break;
        default:
            while (in.ok && *in.p) {
                char code[5];
                in.word(code, sizeof code), in.expect(':');
                long long d = in.num();
                in.expect(';');
                if (!in.ok || d <= 0 || d > 65535 || !data::validTypeCode(code)) return false;
                addType(code, int(d));  // saved in order, so each lands at the end
            }
    }
    return in.ok && !*in.p;
}

void StatsBook::load() {
    svc::LockGuard g(mu_);
    clear();
    char buf[kBuf];
    bool any = false, bad = false, haveDay = false;
    // What is stored now, as save() would hash it: the first save after boot skips unchanged keys.
    for (int k = 0; k < kKeyCount; ++k) savedHash_[k] = fnv("");  // absent reads as empty
    for (int k = 0; k < kKeyCount && !bad; ++k) {
        if (!kv_.getString(kKeys[k], buf, sizeof buf)) continue;
        any = true, haveDay |= k == 0;
        savedHash_[k] = fnv(buf);
        // An over-long value reads as "" on the ESP32 (or cut short here): no checksum, so unreadable.
        if (!unwrap(buf) || !parse(k, buf)) bad = true;
    }
    if (any && !haveDay) bad = true;  // the rest means nothing without the day it belongs to
    if (bad) {
        clear();
        std::memset(savedHash_, 0, sizeof savedHash_);  // rewrite every key at the first save
        svc::logf("[stats] saved stats unreadable, starting empty");
        return;
    }
    if (!any) {
        svc::logf("[stats] no saved stats, starting empty");
        return;
    }
    // A crash reset on the same day keeps today's planes marked as seen, so none is counted
    // twice. The cost: planes counted since the last save (up to kSaveEveryS) are lost, as the
    // filter still holds them and they are not counted again today.
    seen_.begin(day_);
    svc::logf("[stats] loaded: %lu flights today, %lu in all, %d types", (unsigned long)flightsToday_,
              (unsigned long)flightsAll_, typesEver_);
}

bool StatsBook::saveIfDue(int64_t epoch, bool timeValid) {
    {
        svc::LockGuard g(mu_);
        if (!timeValid || !day_) return false;  // spec §6: no saving without a valid clock
        if (!lastSave_ || epoch < lastSave_) lastSave_ = epoch;  // a clock stepped back restarts the wait
        const bool due = dayEnded_ || (dirty_ && epoch - lastSave_ >= kSaveEveryS);
        if (!due) return false;
        lastSave_ = epoch, dirty_ = dayEnded_ = false;  // cleared first: a count during the save sets them again
    }
    return save();
}

bool StatsBook::save() {
    int wrote = 0, day = 0;
    for (int k = 0; k < kKeyCount; ++k) {
        char buf[kBuf];
        bool ok;
        {
            svc::LockGuard g(mu_);
            if (!k) day = day_;
            if (day_ != day) {  // midnight passed mid-save: the keys would mix two days; the roll saves again
                dirty_ = true;
                break;
            }
            ok = serialize(k, buf, sizeof buf);
        }
        if (!ok) {
            svc::logf("[stats] %s does not fit, not saved", kKeys[k]);
            continue;
        }
        const uint32_t h = fnv(buf);
        if (h == savedHash_[k]) continue;
        const bool done = buf[0] ? kv_.putString(kKeys[k], buf) : (kv_.erase(kKeys[k]), true);
        if (done) {
            savedHash_[k] = h, ++wrote;
        } else {
            svc::logf("[stats] %s not written, will retry", kKeys[k]);
            svc::LockGuard g(mu_);
            dirty_ = true;
        }
    }
    if (wrote) svc::logf("[stats] saved %d keys", wrote);
    return wrote > 0;
}

}  // namespace stats
