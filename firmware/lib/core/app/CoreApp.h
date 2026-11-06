#pragma once
// Wires settings, providers, the aircraft store and enrichment together. The platform
// entry point calls networkTick() from its network thread and renderTick() from its
// render thread.
#include <atomic>

#include "RouteShelf.h"
#include "hal.h"
#include "model/AircraftStore.h"
#include "providers/ProviderChain.h"
#include "providers/Providers.h"
#include "providers/Routes.h"
#include "services/CoreSettings.h"
#include "services/EventBus.h"
#include "screens/Scene.h"
#include "services/Scheduler.h"
#include "stats/StatsBook.h"

namespace app {

class CoreApp {
   public:
    CoreApp(hal::IHttp& http, hal::IClock& clock, hal::IKeyValueStore& kv);

    void begin();
    // Network thread: poll providers, enrich routes. Call every ~100 ms.
    void networkTick();
    // Render thread: rebuild the scene at 5 Hz. Returns true when it changed.
    bool renderTick(scr::Scene& scene);

    void setOnline(bool online) { online_ = online; }
    bool online() const { return online_; }
    // Some platforms gate fetches (e.g. low heap); return false to skip this tick.
    void setFetchGuard(bool (*guard)()) { guard_ = guard; }

    svc::Settings& settings() { return settings_; }
    model::AircraftStore& store() { return store_; }
    stats::StatsBook& stats() { return stats_; }
    svc::EventBus& events() { return events_; }
    const prov::ProviderChain& chain() const { return chain_; }

   private:
    void applySettings();
    void pollJob(uint32_t nowMs);
    void enrichJob(uint32_t nowMs);
    prov::PollContext context(uint32_t nowMs) const;

    hal::IHttp& http_;
    hal::IClock& clock_;
    svc::Settings settings_;
    stats::StatsBook stats_;
    svc::EventBus events_;
    svc::Scheduler sched_;
    model::AircraftStore store_;
    prov::CommunityProvider community_;
    prov::LocalReceiverProvider local_;
    prov::OpenSkyProvider opensky_;
    prov::ProviderChain chain_;
    prov::RouteCache routes_;
    prov::AdsbdbClient adsbdb_;
    prov::AeroApiClient aero_;
    static constexpr int kEnrichCount = 8;  // routes are looked up for the nearest few
    RouteShelf shelf_;
    model::Aircraft snap_[scr::Scene::kMaxFlights];  // member: too big for the loop task's stack
    std::atomic<bool> locSet_{false};
    int pollJobId_ = -1;
    std::atomic<bool> settingsDirty_{true};
    std::atomic<bool> online_{true};
    bool (*guard_)() = nullptr;
    uint32_t lastSunMs_ = 0;
    static constexpr uint32_t kModelIntervalMs = 200;
    uint32_t lastModelMs_ = 0;
    bool modelInit_ = false;
    bool sunInit_ = false;
    uint32_t enrichPauseUntil_ = 0;
    // Enrichment's TLS connections are closed once it has had nothing to look up for a while:
    // kept alive they hold ~44 KB each, which the heap needs while a poll's body arrives.
    static constexpr uint32_t kEnrichIdleMs = 60000;
    uint32_t lastLookupMs_ = 0;
    bool enrichOpen_ = false;  // a lookup ran since the connections were last released
    void releaseEnrichIfIdle(uint32_t nowMs);
};

}  // namespace app
