#include "CoreApp.h"

#include <cstdio>
#include <cstring>

#include "SceneBuilder.h"
#include "services/Astro.h"
#include "services/Log.h"
#include "services/Sun.h"
#include "util/Str.h"

namespace app {
using util::copyStr;
using namespace svc;

// ------------------------------------------------------------ CoreApp

CoreApp::CoreApp(hal::IHttp& http, hal::IClock& clock, hal::IKeyValueStore& kv)
    : http_(http),
      clock_(clock),
      settings_(kv, kCoreSettings, kCoreSettingsCount),
      stats_(kv, stats::deviceSeenWords()) {}

void CoreApp::begin() {
    settings_.load();
    stats_.load();
    settings_.onChange([this](const char*) {
        settingsDirty_ = true;
        events_.publish(Ev::SettingsChanged);
    });
    prov::IProvider* ps[] = {&community_, &local_, &opensky_};  // order matches the "provider" enum
    chain_.setProviders(ps, 3);
    applySettings();
    pollJobId_ = sched_.every(5000, [this](uint32_t now) { pollJob(now); });
    sched_.every(1500, [this](uint32_t now) { enrichJob(now); });
}

void CoreApp::applySettings() {
    settingsDirty_ = false;
    store_.configure(settings_.getFloat(key::kLat), settings_.getFloat(key::kLon), float(settings_.getInt(key::kRadius)));
    community_.setIntervalMs(settings_.getInt(key::kPollComm) * 1000u);
    local_.setIntervalMs(settings_.getInt(key::kPollLocal) * 1000u);
    local_.setUrl(settings_.getString(key::kLocalUrl).c_str());
    opensky_.setIntervalMs(settings_.getInt(key::kPollOsky) * 1000u);
    opensky_.setCredentials(settings_.getString(key::kOskyId).c_str(), settings_.getString(key::kOskySecret).c_str());
    aero_.setKey(settings_.getString(key::kAeroKey).c_str());
    aero_.setDailyCap(settings_.getInt(key::kAeroCap));
    chain_.setFallbackEnabled(settings_.getBool(key::kFallback));
    chain_.setPrimary(settings_.getInt(key::kProvider));
    locSet_ = settings_.getBool(key::kLocSet);
    if (pollJobId_ >= 0) sched_.runSoon(pollJobId_);
}

prov::PollContext CoreApp::context(uint32_t nowMs) const {
    return {nowMs, clock_.timeValid() ? clock_.epochSeconds() : 0, store_.homeLat(), store_.homeLon(),
            store_.radiusNm()};
}

void CoreApp::networkTick() {
    if (settingsDirty_) applySettings();
    sched_.tick(clock_.millis());
    // Saving goes through NVS, which can take tens of milliseconds: on this thread, never a frame's.
    stats_.saveIfDue(clock_.timeValid() ? clock_.epochSeconds() : 0, clock_.timeValid());
}

void CoreApp::pollJob(uint32_t nowMs) {
    if (!online_ || !settings_.getBool(key::kLocSet)) return;
    if (guard_ && !guard_()) return;
    const char* before = chain_.activeName();
    bool ok = chain_.poll(http_, store_, context(nowMs));
    logf("[poll] %s %s, %d aircraft", before, ok ? "ok" : "failed", store_.size());
    sched_.setInterval(pollJobId_, chain_.pollIntervalMs());
    if (ok) events_.publish(Ev::AircraftUpdated, store_.size());
    if (std::strcmp(before, chain_.activeName()) != 0) events_.publish(Ev::ProviderStatusChanged);
}

void CoreApp::enrichJob(uint32_t nowMs) {
    if (!online_ || (guard_ && !guard_())) return releaseEnrichIfIdle(nowMs);
    if (int32_t(nowMs - enrichPauseUntil_) < 0) return releaseEnrichIfIdle(nowMs);
    model::Aircraft near[kEnrichCount];
    int n = store_.snapshot(near, kEnrichCount, nowMs);
    // One lookup per tick, nearest first, to keep the network quiet.
    for (int i = 0; i < n; ++i) {
        const char* cs = near[i].callsign;
        if (!cs[0] || near[i].has(model::AC_GROUND)) continue;
        const prov::Route* cached = routes_.find(cs, nowMs);
        if (cached) {
            if (cached->known && cached->origIata[0] && prov::routePlausible(*cached, near[i].lat, near[i].lon)) {
                shelf_.put(toRouteInfo(*cached));
            }
            continue;
        }
        prov::Route r;
        int64_t epoch = clock_.timeValid() ? clock_.epochSeconds() : 0;
        bool ok = (i == 0 && aero_.configured() && aero_.lookup(http_, cs, nowMs, epoch, r) && r.known) ||
                  adsbdb_.lookup(http_, cs, nowMs, r);
        lastLookupMs_ = nowMs, enrichOpen_ = true;
        if (ok) routes_.put(r);
        else enrichPauseUntil_ = nowMs + 30000;  // host unreachable: don't retry every tick
        return;
    }
    releaseEnrichIfIdle(nowMs);
}

void CoreApp::releaseEnrichIfIdle(uint32_t nowMs) {
    if (!enrichOpen_ || nowMs - lastLookupMs_ < kEnrichIdleMs) return;
    http_.release(prov::AdsbdbClient::kHost);
    http_.release(prov::AeroApiClient::kHost);
    enrichOpen_ = false;
}

bool CoreApp::renderTick(scr::Scene& s) {
    uint32_t now = clock_.millis();
    if (modelInit_ && now - lastModelMs_ < kModelIntervalMs) return false;
    lastModelMs_ = now, modelInit_ = true;

    s.timeValid = clock_.timeValid();
    s.epoch = s.timeValid ? clock_.epochSeconds() : 0;
    s.locationSet = locSet_;
    s.homeLat = store_.homeLat(), s.homeLon = store_.homeLon(), s.radiusNm = store_.radiusNm();
    if (s.timeValid && (!sunInit_ || now - lastSunMs_ > 30000)) {
        SunPos sun = sunPosition(s.epoch, s.homeLat, s.homeLon);
        MoonState moon = moonState(s.epoch, s.homeLat, s.homeLon);
        s.sunElev = float(sun.elevationDeg), s.sunAz = float(sun.azimuthDeg);
        s.moonElev = float(moon.elevationDeg), s.moonAz = float(moon.azimuthDeg);
        s.moonIllum = float(moon.illumination), s.moonWaxing = moon.waxing;
        lastSunMs_ = now, sunInit_ = true;
    }

    // The store's generation is read with the data, so the alert engine's two-poll squawk rule
    // never sees a new poll's planes under the previous poll's number.
    int n = store_.snapshot(snap_, scr::Scene::kMaxFlights, now, &s.pollSeq);
    s.count = 0, s.routeCount = 0;
    for (int i = 0; i < n; ++i) {
        const model::Aircraft& a = snap_[i];
        if (a.has(model::AC_GROUND)) continue;  // airport ground traffic would crowd out the sky
        RouteInfo r;
        bool known = a.callsign[0] && shelf_.get(a.callsign, r);
        fillFlight(s.flights[s.count++], a, known ? &r : nullptr, s.epoch);
        if (known && s.routeCount < scr::Scene::kMaxRoutes) {
            scr::SceneRoute& sr = s.routes[s.routeCount++];
            copyStr(sr.callsign, sizeof sr.callsign, a.callsign);
            copyStr(sr.origCity, sizeof sr.origCity, r.origCity);
            copyStr(sr.destCity, sizeof sr.destCity, r.destCity);
            copyStr(sr.airline, sizeof sr.airline, r.airline);
        }
    }
    s.online = online_;
    s.dataAgeS = chain_.everOk() ? int((now - chain_.lastOkMs()) / 1000) : -1;
    stats_.observe(s);
    stats_.summary(s.stats);
    ++s.version;
    return true;
}

}  // namespace app
