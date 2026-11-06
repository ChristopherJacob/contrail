#include "AlertEngine.h"

#include <cstring>
#include <ctime>

#include "AlertLights.h"
#include "providers/ReadsbParser.h"
#include "services/Log.h"
#include "util/Str.h"

namespace alerts {
using scr::Scene;
using scr::SceneFlight;
using util::copyStr;

static_assert(sizeof(scr::SceneAlert::matched) == AlertConfig::kEntryLen + 1,
              "SceneAlert::matched must hold any watchlist entry");
static_assert(kindBit(AlertKind::LowPass) < 16, "Seen::alerted holds four kinds");

namespace {
constexpr uint32_t kMadeUpHex = 0xFFFFFE;  // inject()'s plane: in no scene, so its alert shows as not live

bool on(const AlertConfig& c, AlertKind k) {
    switch (k) {
        case AlertKind::Emergency: return c.emergency;
        case AlertKind::Watchlist: return c.watchlist;
        case AlertKind::Military: return c.military;
        case AlertKind::LowPass: return c.lowPass;
    }
    return false;
}

// `from`'s numbers, with the squawk and emergency flags the alert was raised with.
void refresh(SceneFlight& to, const SceneFlight& from, const char* squawk, bool emergency, bool status) {
    to = from;
    copyStr(to.squawk, sizeof to.squawk, squawk);
    to.emergency = emergency, to.emergencyStatus = status;
}

SceneFlight madeUp(AlertKind k) {
    SceneFlight f;
    f.hex = kMadeUpHex;
    copyStr(f.callsign, sizeof f.callsign, "N512TS");
    copyStr(f.reg, sizeof f.reg, "N512TS");
    copyStr(f.type, sizeof f.type, "C172");
    f.altFt = 2500, f.distNm = 1.2f, f.gsKt = 110, f.trackDeg = 300, f.bearingDeg = 45;
    if (k == AlertKind::Emergency) copyStr(f.squawk, sizeof f.squawk, "7700"), f.emergency = f.emergencyStatus = true;
    if (k == AlertKind::Military) f.military = true;
    return f;
}
}  // namespace

void AlertEngine::configure(const AlertConfig& c) {
    cfg_ = c;
    int w = 0;
    for (int i = 0; i < nQueue_; ++i)
        if (on(cfg_, queue_[i].kind)) queue_[w++] = queue_[i];
    nQueue_ = w, reconfigured_ = true;
}

AlertEngine::Seen* AlertEngine::find(uint32_t hex) {
    for (Seen& e : seen_)
        if (e.used && e.hex == hex) return &e;
    return nullptr;
}

AlertEngine::Seen* AlertEngine::take(uint32_t hex, uint32_t now) {
    Seen* pick = nullptr;
    for (Seen& e : seen_) {
        if (!e.used) {
            pick = &e;
            break;
        }
        if (e.lastSeenMs == now) continue;  // in this scene: forgetting it would let it alert again
        if (!pick || int32_t(e.lastSeenMs - pick->lastSeenMs) < 0) pick = &e;
    }
    if (!pick) return nullptr;
    *pick = Seen{};
    pick->used = true, pick->hex = hex, pick->lastSeenMs = now;
    return pick;
}

void AlertEngine::enqueue(AlertKind k, const char* matched, const SceneFlight& f) {
    // One entry per plane: a new match is always above what it queued before, so it replaces it.
    int w = 0;
    for (int i = 0; i < nQueue_; ++i)
        if (queue_[i].hex != f.hex) queue_[w++] = queue_[i];
    nQueue_ = w;
    int at = nQueue_;
    while (at > 0 && int(queue_[at - 1].kind) > int(k)) --at;  // after its own kind, before lower ones
    if (at == kQueue) return;                                    // full of alerts at least as important
    if (nQueue_ == kQueue) --nQueue_;                            // drop the least important
    for (int i = nQueue_; i > at; --i) queue_[i] = queue_[i - 1];
    Queued& q = queue_[at];
    q.hex = f.hex, q.kind = k;
    copyStr(q.matched, sizeof q.matched, matched);
    copyStr(q.squawk, sizeof q.squawk, f.squawk);
    q.emergency = f.emergency, q.emergencyStatus = f.emergencyStatus;
    ++nQueue_;
    if (k == AlertKind::Emergency && at == 0) held_ = f;  // the first queued emergency
}

void AlertEngine::start(Scene& s, const Queued& q, uint32_t now) {
    const SceneFlight* in = s.find(q.hex);
    SceneFlight f;
    if (in) f = *in;
    else if (q.hex == kMadeUpHex) f = madeUp(q.kind);
    else if (q.kind != AlertKind::Emergency) return;  // gone over kStaleMs
    else if (held_.hex == q.hex) f = held_;
    else f.hex = q.hex;
    scr::SceneAlert& a = s.alert;
    a.active = true, a.kind = q.kind;
    copyStr(a.matched, sizeof a.matched, q.matched);
    refresh(a.flight, f, q.squawk, q.emergency, q.emergencyStatus);
    a.live = in != nullptr;
    a.startMs = now, a.endsMs = now + kShowMs;
    svc::logf("[alert] %s %s", kindName(q.kind), f.callsign);
}

bool AlertEngine::quietNow(const Scene& s) {
    if (!s.timeValid) return false;  // better loud than missed
    if (s.epoch / 60 != minute_) {   // localtime once a minute
        minute_ = s.epoch / 60;
        time_t t = time_t(s.epoch);
        struct tm tm;
        localtime_r(&t, &tm);
        minuteOfDay_ = tm.tm_hour * 60 + tm.tm_min;
    }
    return inQuietHours(minuteOfDay_, cfg_.quietFrom, cfg_.quietTo);
}

void AlertEngine::update(Scene& s, uint32_t now) {
    scr::SceneAlert& a = s.alert;
    if (reconfigured_ && !on(cfg_, a.kind)) a.active = false;
    reconfigured_ = false;
    if (!cfg_.any() && !nQueue_ && !a.active) return;

    // Mark every remembered plane in view first, so a newcomer never takes the slot of one
    // later in the list.
    for (int i = 0; i < s.count; ++i)
        if (Seen* e = find(s.flights[i].hex)) e->lastSeenMs = now;

    for (int i = 0; i < s.count; ++i) {
        const SceneFlight& f = s.flights[i];
        Seen* e = find(f.hex);
        bool confirmed = false;
        if (cfg_.emergency && prov::isEmergencySquawk(f.squawk)) {
            if (!e) e = take(f.hex, now);
            if (!e) continue;  // memory full of planes in view: tried again next update
            if (!e->squawkPending) e->squawkPending = true, e->squawkSeq = uint16_t(s.pollSeq);
            else confirmed = uint16_t(s.pollSeq) != e->squawkSeq;
        } else if (e && e->squawkPending) {
            e->squawkPending = false;
            if (!e->alerted) e->used = false, e = nullptr;  // held only for the note
        }
        const Match m = matchFlight(f, cfg_, confirmed);
        const uint8_t had = e ? e->alerted : 0;
        const uint8_t fresh = m.kinds & ~had;
        if (!fresh) continue;
        if (!e) e = take(f.hex, now);
        // Every slot holds a plane in this scene. It waits for one to leave; with more than
        // kRemember planes matching at once some wait a while, which beats alerting twice.
        if (!e) continue;
        e->alerted |= m.kinds;
        const AlertKind k = highest(fresh);
        if (had && int(k) >= int(highest(had))) continue;  // nothing above what it already alerted for
        enqueue(k, k == AlertKind::Watchlist ? m.matched : "", f);
    }

    if (a.active) {
        const SceneFlight* f = s.find(a.flight.hex);
        if (f) {
            const SceneFlight was = a.flight;
            refresh(a.flight, *f, was.squawk, was.emergency, was.emergencyStatus);
        }
        a.live = f != nullptr;
        if (int32_t(now - a.endsMs) >= 0) a.active = false;
    }
    if (a.active && a.kind != AlertKind::Emergency && nQueue_ && queue_[0].kind == AlertKind::Emergency)
        a.active = false;  // replaced, and not shown again
    startNext(s, now);
}

bool AlertEngine::waiting(const Scene& s, const Queued& q, uint32_t now) {
    if (q.kind == AlertKind::Emergency || q.hex == kMadeUpHex || s.find(q.hex)) return false;
    const Seen* e = find(q.hex);
    return e && now - e->lastSeenMs <= kStaleMs;
}

void AlertEngine::startNext(Scene& s, uint32_t now) {
    scr::SceneAlert& a = s.alert;
    // In queue order, the first that can start. One whose plane missed a poll waits up to
    // kStaleMs for it while those behind it go ahead; start() drops it after that.
    int w = 0;
    for (int i = 0; i < nQueue_; ++i) {
        const Queued q = queue_[i];
        if (!a.active && !waiting(s, q, now)) start(s, q, now);
        else queue_[w++] = q;
    }
    nQueue_ = w;
    if (nQueue_ && queue_[0].kind == AlertKind::Emergency)  // after the starts: the head may be new
        if (const SceneFlight* f = s.find(queue_[0].hex)) held_ = *f;
    a.soft = a.active && a.kind != AlertKind::Emergency && quietNow(s);
}

void AlertEngine::dismiss(Scene& s, uint32_t now) {
    s.alert.active = false;
    startNext(s, now);
}

void AlertEngine::inject(AlertKind k, uint32_t) { enqueue(k, k == AlertKind::Watchlist ? "N512*" : "", madeUp(k)); }

}  // namespace alerts
