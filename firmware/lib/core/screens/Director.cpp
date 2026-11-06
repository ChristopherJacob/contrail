#include "Director.h"

#include <cstring>
#include <initializer_list>

namespace scr {

namespace {
// Carousel order: the swipe order.
const ScreenId kCycle[] = {ScreenId::Closest, ScreenId::Radar, ScreenId::Map, ScreenId::Board, ScreenId::Sky,
                           ScreenId::Stats};
constexpr int kCycleN = sizeof(kCycle) / sizeof(kCycle[0]);
}  // namespace

DirectorMode parseDirectorMode(const char* setting) {
    if (!setting) return DirectorMode::Auto;
    if (!std::strcmp(setting, "carousel")) return DirectorMode::Carousel;
    if (!std::strcmp(setting, "stay")) return DirectorMode::Stay;
    return DirectorMode::Auto;
}

ScreenId Director::rotate(Rotation& r, uint32_t now) {
    if (rot_ != &r) {
        // Back after a rule held the screen: resume, moving on if the interrupted screen had its dwell.
        rot_ = &r;
        if (!r.shown && r.had >= kDwellMs) r.advance();
    } else if (r.shown && r.onFor(now) >= kAlternateMs) {
        r.advance();
    }
    return r.screen();
}

void Director::trackSightings(const Scene& s, uint32_t now) {
    const uint32_t seq = s.stats.firstSeq;
    if (!seqInit_) {  // the count at boot is not news
        seqInit_ = true, seq_ = seq;
    } else if (seq != seq_) {
        seq_ = seq;
        // A new type: Stats for a while, unless one took over in the last five minutes (a new
        // logbook sees a new type with almost every plane).
        if (!sighting_ && (!anySighting_ || now - lastSighting_ >= kSightingGapMs))
            sighting_ = true, sightingShown_ = false, sightingAt_ = now;
    }
    if (!sighting_) return;
    if (sightingShown_) {
        if (now - sightingFrom_ >= kSightingMs) sighting_ = false, anySighting_ = true, lastSighting_ = now;
    } else if (now - sightingAt_ >= kSightingWaitMs) {
        sighting_ = false;  // stale: a close plane held the screen too long
    }
}

ScreenId Director::rule(const Scene& s, uint32_t now) {
    // A plane flickering at the edge of range should not flip Sky View and Radar: the idle rule
    // waits until planes have been gone a while (at boot it applies at once).
    if (s.count > 0) {
        quiet_ = emptySeen_ = false;
    } else {
        if (!emptySeen_) emptySeen_ = true, emptyAt_ = now;
        if (!ruled_ || now - emptyAt_ >= kEmptyMs) quiet_ = true;
    }
    ruled_ = true;
    if (!s.locationSet || !s.timeValid) return rot_ = nullptr, ScreenId::Radar;
    if (quiet_) return closeLatched_ = skyLatched_ = false, rotate(idle_, now);
    // Once a rule holds, it lets go only 20 % past where it took hold, so a plane hovering
    // at the threshold does not flip the screen back and forth.
    float closeNm = closeLatched_ ? kCloseNm * kRelease : kCloseNm;
    float closeFt = closeLatched_ ? kCloseFt * kRelease : kCloseFt;
    float highDeg = skyLatched_ ? kHighDeg / kRelease : kHighDeg;
    bool close = false, high = false;
    for (int i = 0; i < s.count; ++i) {
        const SceneFlight& f = s.flights[i];
        if (f.distNm <= closeNm && f.altFt <= closeFt) close = true;
        if (f.elevDeg >= highDeg) high = true;
    }
    // Dusk, night or the moon up: the sky itself is worth looking at.
    bool interesting = s.sunElev < 6 || s.moonElev > 0;
    closeLatched_ = close, skyLatched_ = high && interesting;
    if (close) return rot_ = nullptr, ScreenId::Closest;
    if (sighting_) return rot_ = nullptr, ScreenId::Stats;
    if (skyLatched_) return rot_ = nullptr, ScreenId::Sky;
    return rotate(busy_, now);
}

void Director::noteShown(uint32_t now) {
    // A first sighting's 20 s count from when Stats actually came up (the dwell may delay it,
    // or the user may already be there).
    if (sighting_ && !sightingShown_ && asked_ == ScreenId::Stats) sightingShown_ = true, sightingFrom_ = now;
    // A rotation's clock starts when its screen is actually up (not when the dwell still holds
    // the previous one) and stops, banking the time, when something else is.
    for (Rotation* r : {&busy_, &idle_}) {
        const bool up = asked_ == r->screen() && (r->shown || rot_ == r);
        if (r->shown && !up) r->had += now - r->since, r->shown = false;
        else if (!r->shown && up) r->shown = true, r->since = now;
    }
}

void Director::pauseRotations(uint32_t now) {
    // Bank their time without leaving them: the interrupted screen resumes with what it had left.
    for (Rotation* r : {&busy_, &idle_})
        if (r->shown) r->had += now - r->since, r->shown = false;
}

ScreenId Director::decide(const Scene& s, ScreenId current, uint32_t now) {
    if (s.alert.active) {
        if (!alerting_) alerting_ = true, alertFrom_ = now, pauseRotations(now);
        return ScreenId::Alert;  // asked_ keeps the screen to come back to
    }
    if (alerting_) {
        alerting_ = false;
        const uint32_t held = now - alertFrom_;
        if (touched_ && alertFrom_ - lastTouch_ < kExploreMs) lastTouch_ += held;  // explore mode resumes
        shownSince_ += held;  // nor does the alert count against the carousel or the dwell
        // The host is still on the Alert screen: go back to what we last asked for (an alert before
        // the first decision has nothing to go back to).
        if (asked_ != ScreenId::Count) current = asked_;
        else if (current == ScreenId::Alert) current = ScreenId::Radar;
    }
    if (mode_ == DirectorMode::Stay) return asked_ = current;
    const bool autoMode = mode_ == DirectorMode::Auto;
    if (autoMode) trackSightings(s, now);
    if (exploring(now)) {  // the user is in charge: follow whatever they chose
        asked_ = current;
    } else if (!started_) {  // the first decision after boot applies at once
        started_ = true, shownSince_ = now;
        asked_ = autoMode ? rule(s, now) : current;
    } else if (!autoMode) {  // carousel
        if (now - shownSince_ >= carouselMs_) {
            for (int i = 0; i < kCycleN; ++i)
                if (kCycle[i] == asked_) {
                    asked_ = kCycle[(i + 1) % kCycleN];
                    break;
                }
            shownSince_ = now;
        }
    } else {
        ScreenId want = rule(s, now);
        if (want != asked_ && now - shownSince_ >= kDwellMs) asked_ = want, shownSince_ = now;
    }
    if (autoMode) noteShown(now);
    return asked_;
}

}  // namespace scr
