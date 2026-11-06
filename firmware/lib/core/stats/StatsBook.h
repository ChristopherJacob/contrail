#pragma once
// The spotter's logbook (spec §2, §3.2): counts each plane once per local day, when it is first seen
// within the radius, by hour, airline and type; keeps all-time airline and type totals; remembers
// the day each type was first seen. Thread-safe: the render thread calls observe() and summary(),
// the network thread saves.
#include <cstddef>
#include <cstdint>

#include "hal.h"
#include "screens/Scene.h"
#include "services/Lock.h"

namespace stats {

// Days since 2019-12-31 for a civil date: 2020-01-01 is day 1, and 0 means none.
int dayNumber(int y, int m, int d);

struct LocalTime {
    int day = 0;   // dayNumber of the local date
    int ymd = 0;   // the same date as yyyymmdd
    int hour = 0;  // 0..23
};
// The local date and hour of an epoch (the TZ environment variable applies, as for the clock).
LocalTime localTime(int64_t epoch);

// The planes counted today: a Bloom filter with three probes over the ICAO hex. At 2,000 planes a
// day about 0.5 % of new planes are taken for seen, which only undercounts. The words live where the
// caller puts them: RTC memory on the ESP32, which survives a crash reset on the same day.
class SeenFilter {
   public:
    static constexpr int kBits = 32768, kWords = kBits / 32 + 2;  // + a magic word and the day
    explicit SeenFilter(uint32_t* words) : w_(words) {}
    void begin(int day);  // keep the bits if they were stamped for `day`, else start the day empty
    void reset(int day);
    bool contains(uint32_t hex) const;  // (probably) seen today
    bool testAndSet(uint32_t hex);      // contains(), then marks it seen

   private:
    uint32_t* w_;
};
// The device's filter: RTC_NOINIT memory on the ESP32, a static array elsewhere.
uint32_t* deviceSeenWords();

// A type code in 4 bytes, its characters big-endian and NUL-padded, so numeric order is alphabetical.
uint32_t packCode(const char* code);
void unpackCode(uint32_t packed, char out[5]);

class StatsBook {
   public:
    static constexpr int kTop = 16, kMaxTypes = 400, kNewest = 4;
    static constexpr int64_t kSaveEveryS = 600;
    struct Count {
        char code[5] = "";
        uint32_t n = 0;
    };
    struct Sighting {
        char type[5] = "";  // ICAO type code; "" none
        char callsign[9] = "";
        int64_t epoch = 0;
    };
    StatsBook(hal::IKeyValueStore& kv, uint32_t* seenWords);
    // A scene rebuild. Nothing is counted until the clock is valid; the day rolls over at local midnight.
    void observe(const scr::Scene& s);
    void summary(scr::StatsSummary& out) const;
    // At boot: restores the saved logbook. Missing or unreadable data: start empty, log once.
    void load();
    // From the network thread: saves when there is something new and 10 minutes have passed, or at
    // once when a day has just ended. Never without a valid clock. True if it wrote anything.
    bool saveIfDue(int64_t epoch, bool timeValid);
    // Writes every key whose text changed since the last save.
    bool save();

   private:
    void rollTo(const LocalTime& lt);
    void clear();
    static void bump(Count* table, const char* code);
    static int top(const Count* table, scr::StatsEntry* out);
    bool serialize(int key, char* buf, size_t n) const;  // "payload#fnv"; "" for an empty chunk
    bool parse(int key, const char* payload);
    // Adds a code to the set of types ever seen, first seen on `day`. False if it was there already
    // or the set is full.
    bool addType(const char* code, int day);

    hal::IKeyValueStore& kv_;
    SeenFilter seen_;
    mutable svc::Mutex mu_;
    LocalTime now_;
    int64_t minute_ = INT64_MIN;
    int day_ = 0, ymd_ = 0, startYmd_ = 0;
    bool valid_ = false, dayEnded_ = false, dirty_ = false;
    uint16_t hourly_[24] = {};
    uint32_t flightsToday_ = 0, flightsAll_ = 0, firstSeq_ = 0;
    Count airlinesToday_[kTop], airlinesAll_[kTop], typesToday_[kTop], typesAll_[kTop];
    // Every type code ever seen (packCode, sorted, for binary search) and the day it was first seen:
    // 6 bytes a code, 2.4 KB at the cap. The first typesEver_ entries are in use.
    uint32_t typeCodes_[kMaxTypes] = {};
    uint16_t typeDays_[kMaxTypes] = {};
    int typesEver_ = 0;
    Sighting newest_[kNewest];  // the latest first sightings, newest first
    int64_t lastSave_ = 0;
    uint32_t savedHash_[8] = {};  // per key, of the text last written (network thread only)
};

}  // namespace stats
