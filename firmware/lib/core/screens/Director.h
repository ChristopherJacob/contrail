#pragma once
// Chooses the screen when nobody is touching the device. Rules (spec §4), in order: no location or
// clock → Radar; nothing in range (for 15 s) → Sky View and Stats, 45 s each; a close, low plane →
// Closest Flight; a type seen for the first time → Stats for 20 s (at most once every 5 minutes); a
// plane high overhead when the sky is interesting → Sky View; otherwise Radar, Map and Board, 45 s
// each. A screen stays at least 20 s, thresholds release 20 % past where they trigger, and a touch
// hands control to the user for a minute (explore mode).
//
// Rotations resume rather than restart: each keeps its place while a rule holds the screen, and
// moves on to its next screen if the interrupted one had already been up for the 20 s dwell.
//
// First sightings (Scene::stats.firstSeq growing) in each mode:
// - Auto: Stats for 20 s from when it actually comes up. One that cannot come up within 60 s
//   (a close plane holding the screen, say) is dropped; a takeover blocks the next for 5 minutes.
//   While exploring, sightings are still noted: if the user is already on Stats it counts as shown;
//   otherwise Stats comes up when explore mode ends, if that is within the 60 s.
//   With no planes in range, the Sky ↔ Stats rotation's Stats counts as the takeover.
// - Carousel and Stay: ignored (the user chose a fixed sequence or screen).
//
// An alert (Scene::alert.active) outranks everything, in every mode and while exploring. When it
// ends, the screen it interrupted comes back; explore mode, the carousel and the rotations carry on
// as if the alert's time had not passed.
#include "Scene.h"
#include "Screen.h"

namespace scr {

enum class DirectorMode : uint8_t { Auto, Carousel, Stay };
// The screen_mode setting ("auto", "carousel", "stay"); anything else is Auto.
DirectorMode parseDirectorMode(const char* setting);

class Director {
   public:
    static constexpr uint32_t kDwellMs = 20000, kExploreMs = 60000, kAlternateMs = 45000;
    static constexpr uint32_t kSightingMs = 20000, kSightingGapMs = 300000, kSightingWaitMs = 60000;
    static constexpr uint32_t kEmptyMs = 15000;  // planes gone this long before the idle rule applies
    static constexpr float kCloseNm = 3, kCloseFt = 15000, kHighDeg = 45, kRelease = 1.2f;

    void configure(DirectorMode mode, uint32_t carouselMs) { mode_ = mode, carouselMs_ = carouselMs; }
    void noteTouch(uint32_t now) { lastTouch_ = now, touched_ = true; }
    // An alert stops explore mode's clock: while one is up this holds what it was when it came up.
    bool exploring(uint32_t now) const {
        return touched_ && (alerting_ ? alertFrom_ : now) - lastTouch_ < kExploreMs;
    }
    // The screen that should be showing now. `current` is where the host is heading (its
    // target, not the screen it may still be fading out of); the director adopts it while
    // the user is in charge.
    ScreenId decide(const Scene& s, ScreenId current, uint32_t now);

   private:
    // A list of screens taking turns, 45 s of on-screen time each.
    struct Rotation {
        const ScreenId* list;
        uint8_t n, idx;
        bool shown;      // its current screen is up and its clock is running
        uint32_t since;  // when the current screen came up (while shown)
        uint32_t had;    // on-screen time banked before interruptions
        ScreenId screen() const { return list[idx]; }
        uint32_t onFor(uint32_t now) const { return had + (shown ? now - since : 0); }
        void advance() { idx = uint8_t((idx + 1) % n), had = 0, shown = false; }
    };
    static constexpr ScreenId kBusy[3] = {ScreenId::Radar, ScreenId::Map, ScreenId::Board};
    static constexpr ScreenId kIdle[2] = {ScreenId::Sky, ScreenId::Stats};

    ScreenId rule(const Scene& s, uint32_t now);
    ScreenId rotate(Rotation& r, uint32_t now);
    void trackSightings(const Scene& s, uint32_t now);
    void noteShown(uint32_t now);  // start and stop the clocks for what was just asked for
    void pauseRotations(uint32_t now);  // an alert came up: stop the rotations' clocks

    DirectorMode mode_ = DirectorMode::Auto;
    uint32_t carouselMs_ = 30000;
    bool started_ = false, ruled_ = false;
    // What we last asked for. Holding it (not the host's current screen) keeps a switch
    // going while the host is still fading out of the old screen.
    ScreenId asked_ = ScreenId::Count;
    uint32_t shownSince_ = 0;  // when we last asked for a different screen
    bool closeLatched_ = false, skyLatched_ = false;
    // No planes: the idle rule applies once they have been gone kEmptyMs (at boot, at once).
    bool quiet_ = false, emptySeen_ = false;
    uint32_t emptyAt_ = 0;
    Rotation busy_{kBusy, 3, 0, false, 0, 0}, idle_{kIdle, 2, 0, false, 0, 0};
    Rotation* rot_ = nullptr;  // the rotation running; null while a rule holds the screen
    // First sightings, from Scene::stats.firstSeq.
    bool seqInit_ = false, sighting_ = false, sightingShown_ = false, anySighting_ = false;
    uint32_t seq_ = 0, sightingAt_ = 0, sightingFrom_ = 0, lastSighting_ = 0;
    uint32_t lastTouch_ = 0;
    bool touched_ = false;
    bool alerting_ = false;
    uint32_t alertFrom_ = 0;
};

}  // namespace scr
