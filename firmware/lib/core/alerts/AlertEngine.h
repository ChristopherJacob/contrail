#pragma once
// Decides what the Alert screen shows (alerts spec §3-4). ScreenApp runs it on the render thread
// after each scene rebuild; it is the only writer of Scene::alert.
//
// Each plane alerts once per kind, and again only for a kind above everything it has alerted
// for. New matches queue highest kind first; an emergency replaces any other alert at once (the
// replaced one is not shown again). An emergency squawk counts once a later poll still carries it.
//
// The queue holds planes by hex; an alert takes the plane's numbers from the scene when it starts
// (all but the squawk and emergency flags it alerted with, which it keeps throughout),
// and one whose plane has been out of the feed over kStaleMs by then is dropped. Emergencies are never skipped: the engine keeps
// a copy of the first queued emergency's plane (with three at once, a later one whose plane has
// left is shown by its hex alone).
#include "AlertConfig.h"
#include "AlertRules.h"

namespace alerts {

class AlertEngine {
   public:
    static constexpr int kRemember = 16, kQueue = 4;
    static constexpr uint32_t kShowMs = 30000;  // a takeover's length
    static constexpr uint32_t kStaleMs = 30000;  // a queued plane out of the feed longer is dropped

    // A kind switched off loses its queued alerts, and the one showing if it is of that kind
    // (on the next update).
    void configure(const AlertConfig& c);
    const AlertConfig& config() const { return cfg_; }
    void update(scr::Scene& s, uint32_t nowMs);
    // Ends the alert showing and starts the next queued one at once, so the screen never fades
    // out between them.
    void dismiss(scr::Scene& s, uint32_t nowMs);
    // A made-up alert of kind `k`, queued as any other (the serial "alert <kind>" command).
    void inject(AlertKind k, uint32_t nowMs);
    int queued() const { return nQueue_; }
    uint32_t queuedHex(int i) const { return queue_[i].hex; }  // 0 <= i < queued()

   private:
    struct Seen {  // 12 bytes
        uint32_t lastSeenMs = 0;  // last update it was in the scene: the least recent is forgotten first
        uint32_t hex : 25;        // the ICAO address, with prov::parseHex's non-ICAO bit
        uint32_t alerted : 4;     // kindBit() per kind already alerted
        uint32_t used : 1;
        uint32_t squawkPending : 1;  // an emergency squawk seen, waiting for a later poll
        uint16_t squawkSeq = 0;      // the poll it was first seen on (low bits: only compared for equality)
        Seen() : hex(0), alerted(0), used(0), squawkPending(0) {}
    };
    struct Queued {
        uint32_t hex;
        AlertKind kind;
        char matched[AlertConfig::kEntryLen + 1];
        // What it alerted for: the alert keeps these while its other numbers follow the plane.
        char squawk[sizeof scr::SceneFlight::squawk];
        bool emergency, emergencyStatus;
    };
    Seen* find(uint32_t hex);
    // A slot for a new plane, or nullptr when every slot's plane is in this update's scene.
    Seen* take(uint32_t hex, uint32_t nowMs);
    void enqueue(AlertKind k, const char* matched, const scr::SceneFlight& f);
    void start(scr::Scene& s, const Queued& q, uint32_t nowMs);  // leaves s.alert inactive when skipped
    void startNext(scr::Scene& s, uint32_t nowMs);  // when none is showing
    // A queued non-emergency whose plane is out of this scene but seen within kStaleMs.
    bool waiting(const scr::Scene& s, const Queued& q, uint32_t nowMs);
    bool quietNow(const scr::Scene& s);

    AlertConfig cfg_;
    bool reconfigured_ = false;  // update() ends the alert showing if its kind is off
    Seen seen_[kRemember];
    Queued queue_[kQueue];
    int nQueue_ = 0;
    scr::SceneFlight held_;  // the first queued emergency's plane, as last seen
    int64_t minute_ = -1;  // the epoch minute minuteOfDay_ is for
    int minuteOfDay_ = -1;
};

}  // namespace alerts
