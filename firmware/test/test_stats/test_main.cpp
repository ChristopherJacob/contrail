#include <unity.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>

#include "FakeHttp.h"
#include "app/CoreApp.h"
#include "services/Log.h"
#include "stats/StatsBook.h"

void setUp() {
    setenv("TZ", "UTC0", 1);
    tzset();
}
void tearDown() {}

class MemStore : public hal::IKeyValueStore {
   public:
    std::map<std::string, std::string> kv;
    bool getString(const char* k, char* out, size_t n) override {
        auto it = kv.find(k);
        if (it == kv.end()) return false;
        std::strncpy(out, it->second.c_str(), n - 1);
        out[n - 1] = 0;
        return true;
    }
    bool putString(const char* k, const char* v) override { return kv[k] = v, true; }
    bool erase(const char* k) override { return kv.erase(k) > 0; }
};

constexpr int64_t kNoon = 1793448000;  // 2026-10-31 12:00 UTC

static scr::SceneFlight fl(uint32_t hex, const char* cs, const char* type, float dist = 5) {
    scr::SceneFlight f;
    f.hex = hex, f.distNm = dist;
    std::snprintf(f.callsign, sizeof f.callsign, "%s", cs);
    std::snprintf(f.type, sizeof f.type, "%s", type);
    return f;
}

static scr::Scene sky(int64_t epoch, std::initializer_list<scr::SceneFlight> list) {
    scr::Scene s;
    s.timeValid = s.locationSet = true;
    s.epoch = epoch, s.radiusNm = 25;
    for (const auto& f : list) s.flights[s.count++] = f;
    return s;
}

static scr::StatsSummary sum(const stats::StatsBook& b) {
    scr::StatsSummary s;
    b.summary(s);
    return s;
}

static void test_dates() {
    TEST_ASSERT_EQUAL(1, stats::dayNumber(2020, 1, 1));
    TEST_ASSERT_EQUAL(2496, stats::dayNumber(2026, 10, 31));
    TEST_ASSERT_EQUAL(2, stats::dayNumber(2024, 3, 1) - stats::dayNumber(2024, 2, 28));  // a leap year
    stats::LocalTime t = stats::localTime(kNoon);
    TEST_ASSERT_EQUAL(20261031, t.ymd);
    TEST_ASSERT_EQUAL(12, t.hour);
    setenv("TZ", "PST8PDT,M3.2.0,M11.1.0", 1);
    tzset();
    t = stats::localTime(kNoon - 7 * 3600);  // 05:00 UTC is 22:00 the evening before in California
    TEST_ASSERT_EQUAL(20261030, t.ymd);
    TEST_ASSERT_EQUAL(22, t.hour);
}

static void test_counts_each_plane_once_a_day() {
    static uint32_t w[stats::SeenFilter::kWords];
    MemStore kv;
    stats::StatsBook b(kv, w);
    scr::Scene s = sky(kNoon, {fl(1, "UAL53", "B738"), fl(2, "DAL22", "A321"), fl(3, "SWA88", "B38M", 40)});
    for (int k = 0; k < 3; ++k) s.epoch = kNoon + k, b.observe(s);  // the third plane is beyond the radius
    scr::StatsSummary t = sum(b);
    TEST_ASSERT_TRUE(t.valid);
    TEST_ASSERT_EQUAL_UINT32(2, t.flightsToday);
    TEST_ASSERT_EQUAL_UINT32(2, t.flightsAll);
    TEST_ASSERT_EQUAL(12, t.hour);
    TEST_ASSERT_EQUAL_UINT16(2, t.hourly[12]);
    TEST_ASSERT_EQUAL(2, t.nAirlinesToday);
    TEST_ASSERT_EQUAL(20261031, t.ymd);
    TEST_ASSERT_EQUAL(20261031, t.startYmd);
}

static void test_registrations_and_unknown_types() {
    static uint32_t w[stats::SeenFilter::kWords];
    MemStore kv;
    stats::StatsBook b(kv, w);
    b.observe(sky(kNoon, {fl(1, "N512KA", "C172"), fl(2, "UAL1", "ZZZZ"), fl(3, "RCH471", "C30J"), fl(4, "N1", ""),
                          fl(5, "DAL1", "GRND")}));
    scr::StatsSummary t = sum(b);
    TEST_ASSERT_EQUAL_UINT32(5, t.flightsToday);
    TEST_ASSERT_EQUAL(3, t.nAirlinesToday);  // UAL, RCH and DAL; registrations are no airline
    TEST_ASSERT_EQUAL(2, t.typesEver);       // C172 and the rare C30J; not ZZZZ, GRND or a blank
    TEST_ASSERT_EQUAL(2, t.nTypesAll);
    TEST_ASSERT_EQUAL_STRING("C30J", t.newestType);  // not in the name table: the screen shows the raw code
}

// The set of types ever seen holds 400 codes; newer types still count in the tables.
static void test_type_set_is_capped() {
    static uint32_t w[stats::SeenFilter::kWords];
    MemStore kv;
    stats::StatsBook b(kv, w);
    for (int k = 0; k <= stats::StatsBook::kMaxTypes; ++k) {  // 401 distinct codes, A000 … A400, last first
        char type[5];
        std::snprintf(type, sizeof type, "A%03d", stats::StatsBook::kMaxTypes - k);
        b.observe(sky(kNoon, {fl(uint32_t(k + 1), "UAL1", type)}));
    }
    scr::StatsSummary t = sum(b);
    TEST_ASSERT_EQUAL(stats::StatsBook::kMaxTypes, t.typesEver);
    TEST_ASSERT_EQUAL_UINT32(stats::StatsBook::kMaxTypes, t.firstSeq);
    TEST_ASSERT_EQUAL_STRING("A001", t.newestType);  // A000, the 401st, did not fit
    TEST_ASSERT_EQUAL_UINT32(401, t.flightsToday);
}

static void test_first_sightings() {
    static uint32_t w[stats::SeenFilter::kWords];
    MemStore kv;
    stats::StatsBook b(kv, w);
    b.observe(sky(kNoon, {fl(1, "UAL53", "B738")}));
    scr::StatsSummary t = sum(b);
    TEST_ASSERT_EQUAL(1, t.typesEver);
    TEST_ASSERT_EQUAL(1, t.typesNewToday);
    TEST_ASSERT_EQUAL_UINT32(1, t.firstSeq);
    TEST_ASSERT_EQUAL_STRING("B738", t.newestType);
    TEST_ASSERT_EQUAL_STRING("UAL53", t.newestCallsign);
    TEST_ASSERT_EQUAL_INT64(kNoon, t.newestEpoch);
    b.observe(sky(kNoon + 60, {fl(2, "SWA1", "B738")}));  // the same type again: not a first sighting
    TEST_ASSERT_EQUAL_UINT32(1, sum(b).firstSeq);
    b.observe(sky(kNoon + 86400, {fl(3, "DAL5", "B738"), fl(4, "ASA9", "A320")}));  // the next day
    t = sum(b);
    TEST_ASSERT_EQUAL(2, t.typesEver);
    TEST_ASSERT_EQUAL(1, t.typesNewToday);
    TEST_ASSERT_EQUAL_UINT32(2, t.firstSeq);
    TEST_ASSERT_EQUAL_STRING("A320", t.newestType);
    TEST_ASSERT_EQUAL_STRING("ASA9", t.newestCallsign);
}

static void test_midnight_resets_today_and_keeps_all_time() {
    static uint32_t w[stats::SeenFilter::kWords];
    MemStore kv;
    stats::StatsBook b(kv, w);
    b.observe(sky(kNoon + 11 * 3600 + 59 * 60, {fl(1, "UAL53", "B738"), fl(2, "UAL7", "A320")}));  // 23:59
    TEST_ASSERT_EQUAL_UINT16(2, sum(b).hourly[23]);
    b.observe(sky(kNoon + 12 * 3600 + 5, {fl(1, "UAL53", "B738")}));  // 00:00:05: the same plane counts again
    scr::StatsSummary t = sum(b);
    TEST_ASSERT_EQUAL_UINT32(1, t.flightsToday);
    TEST_ASSERT_EQUAL_UINT32(3, t.flightsAll);
    TEST_ASSERT_EQUAL_UINT16(0, t.hourly[23]);
    TEST_ASSERT_EQUAL_UINT16(1, t.hourly[0]);
    TEST_ASSERT_EQUAL_STRING("UAL", t.airlinesToday[0].code);
    TEST_ASSERT_EQUAL_UINT32(1, t.airlinesToday[0].count);
    TEST_ASSERT_EQUAL_UINT32(3, t.airlinesAll[0].count);
    TEST_ASSERT_EQUAL(20261101, t.ymd);
    TEST_ASSERT_EQUAL(20261031, t.startYmd);
}

static void test_no_clock_counts_nothing() {
    static uint32_t w[stats::SeenFilter::kWords];
    MemStore kv;
    stats::StatsBook b(kv, w);
    scr::Scene s = sky(kNoon, {fl(1, "UAL53", "B738")});
    s.timeValid = false;
    b.observe(s);
    scr::StatsSummary t = sum(b);
    TEST_ASSERT_FALSE(t.valid);
    TEST_ASSERT_EQUAL_UINT32(0, t.flightsToday);
    TEST_ASSERT_EQUAL(0, t.startYmd);
}

static void test_top_tables_keep_the_leaders() {
    static uint32_t w[stats::SeenFilter::kWords];
    MemStore kv;
    stats::StatsBook b(kv, w);
    uint32_t hex = 1;
    for (int k = 0; k < 20; ++k) {  // 20 airlines into 16 slots; the first is seen six times
        char cs[5] = {char('A' + k), 'B', 'C', '1', 0};
        for (int j = 0; j < (k == 0 ? 6 : 1); ++j) b.observe(sky(kNoon, {fl(hex++, cs, "")}));
    }
    scr::StatsSummary t = sum(b);
    TEST_ASSERT_EQUAL(scr::StatsSummary::kTop, t.nAirlinesToday);
    TEST_ASSERT_EQUAL_STRING("ABC", t.airlinesToday[0].code);
    TEST_ASSERT_EQUAL_UINT32(6, t.airlinesToday[0].count);
    TEST_ASSERT_EQUAL_UINT32(1, t.airlinesToday[4].count);
}

static void test_seen_filter_rarely_mistakes_a_new_plane() {
    static uint32_t w[stats::SeenFilter::kWords];
    stats::SeenFilter f(w);
    f.reset(1);
    int fresh = 0;
    for (uint32_t i = 0; i < 2000; ++i) fresh += !f.testAndSet(0x100000 + i * 7919);
    TEST_ASSERT_GREATER_OR_EQUAL(1990, fresh);  // a few may collide while the filter fills
    int wrong = 0;
    for (uint32_t i = 0; i < 20000; ++i) wrong += f.contains(0xA00000 + i);  // never inserted
    TEST_ASSERT_LESS_THAN(200, wrong);  // under 1 % (about 0.5 % expected)
    f.begin(1);
    TEST_ASSERT_TRUE(f.testAndSet(0x100000));  // same day: kept (it was inserted above)
    f.begin(2);
    TEST_ASSERT_FALSE(f.testAndSet(0x100000));  // a new day: cleared
}

class CountingStore : public MemStore {
   public:
    std::map<std::string, int> puts;
    bool putString(const char* k, const char* v) override { return ++puts[k], MemStore::putString(k, v); }
};

static int gLogs = 0;
static char gLog[192];
static std::string gAllLogs;
static void sink(const char* line) {
    ++gLogs, std::snprintf(gLog, sizeof gLog, "%s", line);
    gAllLogs += line, gAllLogs += '\n';
}

static uint32_t fnv(const char* s) {
    uint32_t h = 2166136261u;
    while (*s) h = (h ^ uint8_t(*s++)) * 16777619u;
    return h;
}
static std::string wrap(const std::string& payload) {
    char h[12];
    std::snprintf(h, sizeof h, "#%08lx", (unsigned long)fnv(payload.c_str()));
    return payload + h;
}

static void test_save_and_reload_round_trip() {
    static uint32_t w1[stats::SeenFilter::kWords], w2[stats::SeenFilter::kWords];
    MemStore kv;
    stats::StatsBook a(kv, w1);
    a.observe(sky(kNoon, {fl(1, "UAL53", "B738"), fl(2, "DAL22", "A321"), fl(3, "N512KA", "C172")}));
    a.observe(sky(kNoon + 3600, {fl(4, "UAL7", "A320"), fl(6, "RCH9", "C30J")}));
    TEST_ASSERT_TRUE(a.save());
    stats::StatsBook b(kv, w2);
    b.load();
    scr::StatsSummary x = sum(a), y = sum(b);
    TEST_ASSERT_FALSE(y.valid);  // until a scene with a valid clock arrives
    TEST_ASSERT_EQUAL_UINT32(5, y.flightsToday);
    TEST_ASSERT_EQUAL_UINT32(x.flightsAll, y.flightsAll);
    TEST_ASSERT_EQUAL(x.ymd, y.ymd);
    TEST_ASSERT_EQUAL(x.startYmd, y.startYmd);
    TEST_ASSERT_EQUAL_MEMORY(x.hourly, y.hourly, sizeof x.hourly);
    TEST_ASSERT_EQUAL(x.nAirlinesToday, y.nAirlinesToday);
    TEST_ASSERT_EQUAL_STRING(x.airlinesToday[0].code, y.airlinesToday[0].code);
    TEST_ASSERT_EQUAL_UINT32(x.airlinesToday[0].count, y.airlinesToday[0].count);
    TEST_ASSERT_EQUAL(x.nTypesAll, y.nTypesAll);
    TEST_ASSERT_EQUAL(5, y.typesEver);  // the rare C30J survives the round trip too
    TEST_ASSERT_EQUAL(x.typesNewToday, y.typesNewToday);
    TEST_ASSERT_EQUAL_STRING("C30J", y.newestType);
    TEST_ASSERT_EQUAL_STRING("RCH9", y.newestCallsign);
    TEST_ASSERT_EQUAL_INT64(kNoon + 3600, y.newestEpoch);
    // A crash reset the same day keeps the device's filter (w1, as RTC memory would): no recount.
    stats::StatsBook c(kv, w1);
    c.load();
    c.observe(sky(kNoon + 3700, {fl(1, "UAL53", "B738")}));
    TEST_ASSERT_EQUAL_UINT32(5, sum(c).flightsToday);
    // After a power cut the filter is gone (w2 was never stamped): the plane counts again.
    b.observe(sky(kNoon + 3700, {fl(1, "UAL53", "B738")}));
    TEST_ASSERT_EQUAL_UINT32(6, sum(b).flightsToday);
}

static void test_corrupt_data_starts_empty_and_logs_once() {
    static uint32_t w[stats::SeenFilter::kWords];
    const std::string goodDay = wrap("1,2496,20261031,20261031,3,3,0,0,0,0,0,0,0,0,0,0,0,0,3,0,0,0,0,0,0,0,0,0,0,0");
    const std::pair<std::string, std::string> cases[][2] = {
        {{"st_day", "garbage"}, {"", ""}},
        {{"st_day", "1,2496,20261031,20261031,3,3#00000000"}, {"", ""}},  // wrong checksum
        {{"st_day", wrap("1,2496,20261031")}, {"", ""}},                  // cut short
        {{"st_air", wrap("UAL:1;/UAL:1;")}, {"", ""}},                    // no day to belong to
        {{"st_day", goodDay}, {"st_typ", wrap("B738:x;/")}},              // one bad key spoils the rest
        {{"st_day", "1,2496,20261031,20261031,3,3#1234567"}, {"", ""}},   // the checksum is 8 hex digits
        {{"st_day", wrap("2,2496,20261031,20261031,3,3,0,0,0,0,0,0,0,0,0,0,0,0,3,0,0,0,0,0,0,0,0,0,0,0")},
         {"", ""}},  // a future format version
        {{"st_day", wrap("1,0,20261031,20261031,3,3,0,0,0,0,0,0,0,0,0,0,0,0,3,0,0,0,0,0,0,0,0,0,0,0")},
         {"", ""}},  // no day
    };
    svc::logSink() = sink;
    for (const auto& c : cases) {
        MemStore kv;
        for (const auto& kvp : c)
            if (!kvp.first.empty()) kv.kv[kvp.first] = kvp.second;
        gLogs = 0;
        stats::StatsBook b(kv, w);
        b.load();
        scr::StatsSummary t = sum(b);
        TEST_ASSERT_EQUAL_UINT32(0, t.flightsAll);
        TEST_ASSERT_EQUAL_UINT32(0, t.flightsToday);
        TEST_ASSERT_EQUAL(0, t.typesEver);
        TEST_ASSERT_EQUAL(1, gLogs);
        TEST_ASSERT_NOT_NULL(std::strstr(gLog, "unreadable"));
    }
    MemStore none;
    gLogs = 0;
    stats::StatsBook fresh(none, w);
    fresh.load();
    TEST_ASSERT_EQUAL(1, gLogs);
    TEST_ASSERT_NOT_NULL(std::strstr(gLog, "no saved stats"));
    MemStore ok;
    ok.kv["st_day"] = goodDay;
    stats::StatsBook loaded(ok, w);
    loaded.load();
    TEST_ASSERT_EQUAL_UINT32(3, sum(loaded).flightsAll);
    TEST_ASSERT_EQUAL_UINT16(3, sum(loaded).hourly[12]);
    svc::logSink() = nullptr;
}

static void test_saves_every_ten_minutes_and_at_midnight() {
    static uint32_t w[stats::SeenFilter::kWords];
    CountingStore kv;
    stats::StatsBook b(kv, w);
    TEST_ASSERT_FALSE(b.saveIfDue(kNoon, true));  // nothing counted yet
    b.observe(sky(kNoon, {fl(1, "UAL53", "B738")}));
    TEST_ASSERT_FALSE(b.saveIfDue(kNoon, true));  // the first call starts the 10-minute clock
    TEST_ASSERT_FALSE(b.saveIfDue(kNoon + 599, true));
    TEST_ASSERT_TRUE(b.saveIfDue(kNoon + 600, true));
    TEST_ASSERT_EQUAL(1, kv.puts["st_day"]);
    TEST_ASSERT_EQUAL(1, kv.puts["st_first0"]);
    TEST_ASSERT_FALSE(b.saveIfDue(kNoon + 1300, true));  // nothing new
    b.observe(sky(kNoon + 1300, {fl(2, "UAL7", "B738")}));  // same airline and type
    TEST_ASSERT_TRUE(b.saveIfDue(kNoon + 1300, true));
    TEST_ASSERT_EQUAL(2, kv.puts["st_day"]);
    TEST_ASSERT_EQUAL(1, kv.puts["st_first0"]);  // the first sightings did not change: not rewritten
    b.observe(sky(kNoon + 11 * 3600 + 55 * 60, {fl(3, "DAL1", "A321")}));  // 23:55
    TEST_ASSERT_TRUE(b.saveIfDue(kNoon + 11 * 3600 + 55 * 60, true));
    TEST_ASSERT_EQUAL(3, kv.puts["st_day"]);
    b.observe(sky(kNoon + 12 * 3600 + 1, {fl(5, "DAL2", "A321")}));  // 00:00:01, six minutes later
    TEST_ASSERT_TRUE(b.saveIfDue(kNoon + 12 * 3600 + 1, true));      // the day that ended is saved at once
    TEST_ASSERT_EQUAL(4, kv.puts["st_day"]);
    b.observe(sky(kNoon + 13 * 3600, {fl(4, "SWA1", "B38M")}));
    TEST_ASSERT_FALSE(b.saveIfDue(kNoon + 14 * 3600, false));  // no valid clock: no saving
}

class FakeClock : public hal::IClock {
   public:
    uint32_t ms = 1000;
    int64_t epoch = kNoon;
    uint32_t millis() const override { return ms; }
    int64_t epochSeconds() const override { return epoch; }
    bool timeValid() const override { return true; }
};

static void test_core_app_counts_into_the_scene_and_saves() {
    FakeHttp http;
    FakeClock clock;
    MemStore kv;
    auto core = std::make_unique<app::CoreApp>(http, clock, kv);
    core->begin();
    core->setOnline(false);  // no polling or route lookups in this test
    model::Aircraft a;
    a.hex = 0xABC123, a.lat = 0.05f, a.lon = 0, a.altFt = 12000, a.flags = model::AC_HAS_POS, a.lastSeenMs = clock.ms;
    std::snprintf(a.callsign, sizeof a.callsign, "UAL53");
    std::snprintf(a.type, sizeof a.type, "B738");
    core->store().beginUpdate(clock.ms);
    core->store().upsert(a);
    core->store().endUpdate();
    auto scene = std::make_unique<scr::Scene>();
    TEST_ASSERT_TRUE(core->renderTick(*scene));
    TEST_ASSERT_EQUAL_UINT32(1, scene->stats.flightsToday);
    TEST_ASSERT_EQUAL_STRING("UAL", scene->stats.airlinesToday[0].code);
    core->networkTick();  // starts the save clock
    clock.epoch += 601, clock.ms += 601000;
    core->networkTick();
    char b[256];
    TEST_ASSERT_TRUE(kv.getString("st_day", b, sizeof b));
}

// Not stats, but this suite has the CoreApp harness: enrichment's kept-alive connections are
// released once it has had nothing to look up for a minute, and not while it is still busy.
static void test_core_app_releases_idle_enrichment_connections() {
    FakeHttp http;
    http.responses = {{404, ""}};  // adsbdb: no route known, so the answer is cached
    FakeClock clock;
    MemStore kv;
    auto core = std::make_unique<app::CoreApp>(http, clock, kv);
    core->begin();
    model::Aircraft a;
    a.hex = 0xABC123, a.lat = 0.05f, a.lon = 0, a.altFt = 12000, a.flags = model::AC_HAS_POS, a.lastSeenMs = clock.ms;
    std::snprintf(a.callsign, sizeof a.callsign, "UAL53");
    auto tick = [&](uint32_t ms) {
        clock.ms += ms;
        a.lastSeenMs = clock.ms;
        core->store().beginUpdate(clock.ms);
        core->store().upsert(a);
        core->store().endUpdate();
        core->networkTick();
    };
    tick(0);
    for (int i = 0; i < 2 && http.requests.empty(); ++i) tick(1600);
    TEST_ASSERT_EQUAL(1, int(http.requests.size()));  // the lookup
    TEST_ASSERT_TRUE(http.released.empty());
    for (int i = 0; i < 36; ++i) tick(1600);  // 57.6 s with nothing new: still open
    TEST_ASSERT_TRUE(http.released.empty());
    for (int i = 0; i < 3; ++i) tick(1600);
    TEST_ASSERT_EQUAL(2, int(http.released.size()));
    TEST_ASSERT_EQUAL_STRING("api.adsbdb.com", http.released[0].c_str());
    for (int i = 0; i < 5; ++i) tick(1600);  // released once, not every idle tick
    TEST_ASSERT_EQUAL(2, int(http.released.size()));
    TEST_ASSERT_EQUAL(1, int(http.requests.size()));
}

// NVS on the ESP32: a value longer than the buffer reads as "" (NvsStore::getString).
class NvsLikeStore : public MemStore {
   public:
    bool getString(const char* k, char* out, size_t n) override {
        auto it = kv.find(k);
        if (it == kv.end()) return false;
        if (it->second.size() >= n) return out[0] = 0, true;
        return MemStore::getString(k, out, n);
    }
};

static void test_over_long_value_is_unreadable() {
    static uint32_t w[stats::SeenFilter::kWords];
    std::string many;  // 130 well-formed first sightings: more than one key holds (1,200 bytes)
    for (int i = 0; i < 130; ++i) {
        char e[16];
        std::snprintf(e, sizeof e, "A%03d:2496;", i);
        many += e;
    }
    const std::string goodDay = wrap("1,2496,20261031,20261031,3,3,0,0,0,0,0,0,0,0,0,0,0,0,3,0,0,0,0,0,0,0,0,0,0,0");
    svc::logSink() = sink;
    NvsLikeStore nvs;  // the "" path
    MemStore mem;      // cut short, as the native store does
    for (MemStore* kv : {static_cast<MemStore*>(&nvs), &mem}) {
        kv->kv["st_day"] = goodDay;
        kv->kv["st_first0"] = wrap(many);
        gLogs = 0;
        stats::StatsBook b(*kv, w);
        b.load();
        TEST_ASSERT_EQUAL_UINT32(0, sum(b).flightsAll);
        TEST_ASSERT_EQUAL(0, sum(b).typesEver);
        TEST_ASSERT_EQUAL(1, gLogs);
        TEST_ASSERT_NOT_NULL(std::strstr(gLog, "unreadable"));
    }
    svc::logSink() = nullptr;
}

// NTP stepping a fast clock back across midnight must not reset today twice.
static void test_clock_step_back_across_midnight() {
    static uint32_t w[stats::SeenFilter::kWords];
    MemStore kv;
    stats::StatsBook b(kv, w);
    const int64_t mid = kNoon + 12 * 3600;  // 2026-11-01 00:00 UTC
    b.observe(sky(mid + 30, {fl(1, "UAL1", "B738"), fl(2, "DAL2", "A321")}));
    TEST_ASSERT_EQUAL_UINT32(2, sum(b).flightsToday);
    b.observe(sky(mid - 10, {fl(1, "UAL1", "B738"), fl(2, "DAL2", "A321")}));  // 23:59:50 the day before
    scr::StatsSummary t = sum(b);
    TEST_ASSERT_EQUAL_UINT32(2, t.flightsToday);
    TEST_ASSERT_EQUAL(20261101, t.ymd);
    b.observe(sky(mid + 60, {fl(1, "UAL1", "B738"), fl(2, "DAL2", "A321"), fl(3, "SWA3", "B38M")}));
    t = sum(b);
    TEST_ASSERT_EQUAL_UINT32(3, t.flightsToday);  // only the new plane
    TEST_ASSERT_EQUAL_UINT32(3, t.flightsAll);
    TEST_ASSERT_EQUAL(20261101, t.ymd);
    b.observe(sky(mid - 3 * 86400, {fl(1, "UAL1", "B738")}));  // days back: a new day all the same
    t = sum(b);
    TEST_ASSERT_EQUAL(20261029, t.ymd);
    TEST_ASSERT_EQUAL_UINT32(1, t.flightsToday);
}

static void test_clock_step_back_does_not_delay_saving() {
    static uint32_t w[stats::SeenFilter::kWords];
    CountingStore kv;
    stats::StatsBook b(kv, w);
    b.observe(sky(kNoon, {fl(1, "UAL53", "B738")}));
    TEST_ASSERT_FALSE(b.saveIfDue(kNoon, true));
    TEST_ASSERT_TRUE(b.saveIfDue(kNoon + 600, true));
    b.observe(sky(kNoon - 3600, {fl(2, "DAL1", "A321")}));  // the clock stepped back an hour
    TEST_ASSERT_FALSE(b.saveIfDue(kNoon - 3600, true));     // the wait restarts from here
    TEST_ASSERT_FALSE(b.saveIfDue(kNoon - 3001, true));
    TEST_ASSERT_TRUE(b.saveIfDue(kNoon - 3000, true));  // not at kNoon + 1200
}

class FlakyStore : public CountingStore {
   public:
    int failDay = 1;  // st_day writes that fail
    bool putString(const char* k, const char* v) override {
        if (!std::strcmp(k, "st_day") && failDay > 0) return --failDay, false;
        return CountingStore::putString(k, v);
    }
};

static void test_failed_write_is_retried() {
    static uint32_t w[stats::SeenFilter::kWords];
    FlakyStore kv;
    stats::StatsBook b(kv, w);
    svc::logSink() = sink;
    gAllLogs.clear();
    b.observe(sky(kNoon, {fl(1, "UAL53", "B738")}));
    TEST_ASSERT_FALSE(b.saveIfDue(kNoon, true));
    TEST_ASSERT_TRUE(b.saveIfDue(kNoon + 600, true));  // the other keys went in
    TEST_ASSERT_EQUAL(0, (int)kv.kv.count("st_day"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, gAllLogs.find("st_day not written"));
    TEST_ASSERT_FALSE(b.saveIfDue(kNoon + 1199, true));
    TEST_ASSERT_TRUE(b.saveIfDue(kNoon + 1200, true));  // nothing new was counted, but st_day is retried
    TEST_ASSERT_EQUAL(1, (int)kv.kv.count("st_day"));
    TEST_ASSERT_EQUAL(1, kv.puts["st_air"]);  // unchanged keys are not rewritten
    svc::logSink() = nullptr;
}

static void test_load_then_save_skips_unchanged_keys() {
    static uint32_t w1[stats::SeenFilter::kWords], w2[stats::SeenFilter::kWords];
    MemStore kv;
    stats::StatsBook a(kv, w1);
    a.observe(sky(kNoon, {fl(1, "UAL53", "B738"), fl(2, "DAL22", "A321")}));
    TEST_ASSERT_TRUE(a.save());
    CountingStore copy;
    copy.kv = kv.kv;
    stats::StatsBook b(copy, w2);
    b.load();
    TEST_ASSERT_FALSE(b.save());  // the first save after boot finds nothing changed
    TEST_ASSERT_TRUE(copy.puts.empty());
    b.observe(sky(kNoon + 60, {fl(3, "SWA1", "B738")}));
    TEST_ASSERT_TRUE(b.save());
    TEST_ASSERT_EQUAL(1, copy.puts["st_day"]);
    TEST_ASSERT_EQUAL(0, copy.puts["st_first0"]);  // B738 was already there
}

// A store whose st_air write lets the render thread run past midnight.
class MidnightStore : public CountingStore {
   public:
    stats::StatsBook* book = nullptr;
    int64_t at = 0;
    bool putString(const char* k, const char* v) override {
        if (book && !std::strcmp(k, "st_air")) {
            stats::StatsBook* b = book;
            book = nullptr;
            b->observe(sky(at, {fl(9, "ASA9", "A320")}));
        }
        return CountingStore::putString(k, v);
    }
};

static void test_day_change_mid_save_stops_and_saves_again() {
    static uint32_t w[stats::SeenFilter::kWords];
    MidnightStore kv;
    stats::StatsBook b(kv, w);
    b.observe(sky(kNoon, {fl(1, "UAL53", "B738")}));
    kv.book = &b, kv.at = kNoon + 12 * 3600 + 1;  // 00:00:01
    b.save();
    TEST_ASSERT_EQUAL(1, kv.puts["st_day"]);
    TEST_ASSERT_EQUAL(0, kv.puts["st_typ"]);  // stopped: the rest would belong to the new day
    TEST_ASSERT_TRUE(b.saveIfDue(kNoon + 12 * 3600 + 2, true));  // the roll saves at once
    TEST_ASSERT_EQUAL(2, kv.puts["st_day"]);
    TEST_ASSERT_EQUAL(1, kv.puts["st_typ"]);
    MemStore check;
    check.kv = kv.kv;
    static uint32_t w2[stats::SeenFilter::kWords];
    stats::StatsBook c(check, w2);
    c.load();
    TEST_ASSERT_EQUAL(20261101, sum(c).ymd);
    TEST_ASSERT_EQUAL_UINT32(1, sum(c).flightsToday);
    TEST_ASSERT_EQUAL_UINT32(2, sum(c).flightsAll);
}

static int64_t utcMidnight(int y, int m, int d) { return int64_t(18262 + stats::dayNumber(y, m, d) - 1) * 86400; }

// POSIX rules, so the test needs no zoneinfo files: US Eastern, as America/New_York.
static void useNewYork() {
    setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);
    tzset();
}

static void test_dst_end_day_has_25_hours() {
    useNewYork();
    static uint32_t w[stats::SeenFilter::kWords];
    MemStore kv;
    stats::StatsBook b(kv, w);
    const int64_t d = utcMidnight(2026, 11, 1);  // clocks go back at 02:00 EDT
    b.observe(sky(d + 4 * 3600 + 1800, {fl(1, "UAL1", "B738")}));      // 00:30 EDT
    b.observe(sky(d + 5 * 3600 + 1800, {fl(2, "UAL2", "B738")}));      // 01:30 EDT
    b.observe(sky(d + 6 * 3600 + 1800, {fl(3, "UAL3", "B738")}));      // 01:30 EST, the hour again
    b.observe(sky(d + 28 * 3600 + 1800, {fl(4, "UAL4", "B738")}));     // 23:30 EST, 25 hours in
    scr::StatsSummary t = sum(b);
    TEST_ASSERT_EQUAL(20261101, t.ymd);
    TEST_ASSERT_EQUAL_UINT32(4, t.flightsToday);
    TEST_ASSERT_EQUAL_UINT16(1, t.hourly[0]);
    TEST_ASSERT_EQUAL_UINT16(2, t.hourly[1]);
    TEST_ASSERT_EQUAL_UINT16(1, t.hourly[23]);
    b.observe(sky(d + 29 * 3600 + 300, {fl(1, "UAL1", "B738")}));  // 00:05 EST the next day
    t = sum(b);
    TEST_ASSERT_EQUAL(20261102, t.ymd);
    TEST_ASSERT_EQUAL_UINT32(1, t.flightsToday);
    TEST_ASSERT_EQUAL_UINT16(1, t.hourly[0]);
}

static void test_dst_start_day_has_23_hours() {
    useNewYork();
    static uint32_t w[stats::SeenFilter::kWords];
    MemStore kv;
    stats::StatsBook b(kv, w);
    const int64_t d = utcMidnight(2026, 3, 8);  // clocks go forward at 02:00 EST
    b.observe(sky(d + 5 * 3600 + 1800, {fl(1, "UAL1", "B738")}));   // 00:30 EST
    b.observe(sky(d + 7 * 3600 + 1800, {fl(2, "UAL2", "B738")}));   // 03:30 EDT (02:xx never happens)
    b.observe(sky(d + 27 * 3600 + 1800, {fl(3, "UAL3", "B738")}));  // 23:30 EDT, 23 hours in
    scr::StatsSummary t = sum(b);
    TEST_ASSERT_EQUAL(20260308, t.ymd);
    TEST_ASSERT_EQUAL_UINT32(3, t.flightsToday);
    TEST_ASSERT_EQUAL_UINT16(0, t.hourly[2]);
    TEST_ASSERT_EQUAL_UINT16(1, t.hourly[3]);
    TEST_ASSERT_EQUAL_UINT16(1, t.hourly[23]);
    b.observe(sky(d + 28 * 3600 + 600, {fl(1, "UAL1", "B738")}));  // 00:10 EDT the next day
    t = sum(b);
    TEST_ASSERT_EQUAL(20260309, t.ymd);
    TEST_ASSERT_EQUAL_UINT32(1, t.flightsToday);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_dates);
    RUN_TEST(test_counts_each_plane_once_a_day);
    RUN_TEST(test_registrations_and_unknown_types);
    RUN_TEST(test_first_sightings);
    RUN_TEST(test_midnight_resets_today_and_keeps_all_time);
    RUN_TEST(test_no_clock_counts_nothing);
    RUN_TEST(test_top_tables_keep_the_leaders);
    RUN_TEST(test_type_set_is_capped);
    RUN_TEST(test_seen_filter_rarely_mistakes_a_new_plane);
    RUN_TEST(test_save_and_reload_round_trip);
    RUN_TEST(test_corrupt_data_starts_empty_and_logs_once);
    RUN_TEST(test_saves_every_ten_minutes_and_at_midnight);
    RUN_TEST(test_core_app_counts_into_the_scene_and_saves);
    RUN_TEST(test_core_app_releases_idle_enrichment_connections);
    RUN_TEST(test_over_long_value_is_unreadable);
    RUN_TEST(test_clock_step_back_across_midnight);
    RUN_TEST(test_clock_step_back_does_not_delay_saving);
    RUN_TEST(test_failed_write_is_retried);
    RUN_TEST(test_load_then_save_skips_unchanged_keys);
    RUN_TEST(test_day_change_mid_save_stops_and_saves_again);
    RUN_TEST(test_dst_end_day_has_25_hours);
    RUN_TEST(test_dst_start_day_has_23_hours);
    return UNITY_END();
}
