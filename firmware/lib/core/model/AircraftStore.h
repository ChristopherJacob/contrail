#pragma once
#include "Aircraft.h"
#include "Geo.h"
#include "services/Lock.h"

namespace model {

// Fixed-capacity set of the nearest aircraft around home, shared between the network
// task (writer) and the render task (reader). All public methods are thread-safe.
class AircraftStore {
   public:
    static constexpr int kCapacity = 64;
    static constexpr uint32_t kStaleMs = 60000;
    static constexpr float kMaxReckonS = 30;

    void configure(double homeLat, double homeLon, float radiusNm);
    // Ground traffic is dropped by default: near an airport it would fill every slot.
    void setKeepGround(bool keep) { keepGround_ = keep; }
    double homeLat() const { return lat_; }
    double homeLon() const { return lon_; }
    float radiusNm() const { return radius_; }

    // Batch update: beginUpdate(), upsert()*, endUpdate() (evicts stale records).
    void beginUpdate(uint32_t nowMs);
    // Returns false when dropped (no position, beyond radius, or no room for something farther).
    bool upsert(const Aircraft& a);
    void endUpdate();

    // Copies up to `max` aircraft sorted nearest first, with positions dead-reckoned to nowMs.
    // `generation`, when given, gets generation() read under the same lock as the copy.
    int snapshot(Aircraft* out, int max, uint32_t nowMs, uint32_t* generation = nullptr) const;
    // Bumped by each endUpdate(): which batch of data a snapshot holds (Scene::pollSeq). Never reset,
    // so a value is never reused for different data.
    uint32_t generation() const;
    int size() const;
    uint32_t lastUpdateMs() const;
    void clear();

   private:
    void derive(Aircraft& a) const;
    mutable svc::Mutex mu_;
    Aircraft items_[kCapacity];
    int n_ = 0;
    double lat_ = 0, lon_ = 0;
    geo::LocalFrame frame_;
    float radius_ = 25;
    bool keepGround_ = false;
    uint32_t updateMs_ = 0, lastUpdateMs_ = 0;
    uint32_t generation_ = 0;
};

// Extrapolate position along track at ground speed for `seconds`.
void deadReckon(Aircraft& a, float seconds);

}  // namespace model
