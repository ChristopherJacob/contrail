#include "ScreenApp.h"

namespace scr {

namespace {
// Swipe order, left to right.
const ScreenId kOrder[] = {ScreenId::Closest, ScreenId::Radar, ScreenId::Map, ScreenId::Board, ScreenId::Sky,
                          ScreenId::Stats};
constexpr int kOrderN = sizeof(kOrder) / sizeof(kOrder[0]);
}  // namespace

ScreenApp::ScreenApp(const Theme& theme)
    : theme_(theme), closest_(theme_, arena_), radar_(theme_, arena_), map_(theme_, arena_, trails_),
      board_(theme_, arena_), sky_(theme_, arena_), stats_(theme_, arena_), alert_(theme_, arena_), settings_(theme_),
      update_(theme_),
      screens_{&closest_, &radar_, &map_, &board_, &sky_, &stats_, &alert_, &settings_, &update_},
      host_(screens_, kScreens, theme_), dots_(theme_), badge_(theme_), ring_(theme_), layers_{&host_} {
    host_.setScene(&scene_);
    host_.addOverlay(&dots_);
    host_.addOverlay(&badge_);
    host_.addOverlay(&ring_);
}

void ScreenApp::useTheme(const Theme& t) {
    theme_ = t;
    host_.showNow(host_.target());
}

void ScreenApp::setLogos(assets::LogoStore* l) {
    closest_.setLogos(l);
    board_.setLogos(l);
    alert_.setLogos(l);
    host_.showNow(host_.target());  // re-enter, so the screen up loads its badges
}

void ScreenApp::setMap(basemap::MapPack* p) {
    map_.setPack(p);
    host_.showNow(host_.target());
}

void ScreenApp::setUpdateProgress(int pct) {
    if (pct >= 0) {
        if (!updating_) {
            updating_ = true;
            beforeUpdate_ = panel_ ? beforePanel_ : host_.target();
            if (panel_) settings_.cancel();
            panel_ = false;  // the upload's screen replaces the panel
        }
        update_.setPercent(pct);
        host_.show(ScreenId::Update);
    } else if (updating_) {
        updating_ = false;
        host_.show(beforeUpdate_);
    }
}

void ScreenApp::openPanel(uint32_t now) {
    panel_ = true;
    beforePanel_ = host_.target();
    settings_.open(now);
    host_.show(ScreenId::Settings);
}

void ScreenApp::closePanel(uint32_t now) {
    panel_ = false;
    settings_.cancel();  // a drag cut off by an emergency saves nothing and must not strand the panel
    director_.noteTouch(now);  // the user was just here: stay in explore mode for a while
    host_.show(beforePanel_);
}

void ScreenApp::tick(uint32_t now) {
    // The alert engine first, so the director sees this rebuild's alert.
    if (scene_.version != alertVersion_) alertVersion_ = scene_.version, alerts_.update(scene_, now);
    if (updating_) {  // the upload's screen owns the display; the director waits
        dots_.set(false, 0, kOrderN);
        badge_.set(false);
        ring_.set(false, 0, 0, 0);
        return;
    }
    if (panel_) {
        // An emergency takes over at once; any other alert waits for the panel to close.
        const bool emergency = scene_.alert.active && scene_.alert.kind == AlertKind::Emergency;
        if (emergency || settings_.wantsClose(now)) {
            closePanel(now);
        } else {
            dots_.set(false, 0, kOrderN);
            badge_.set(false);
            ring_.set(false, 0, 0, 0);
            return;
        }
    }
    // The host's target, not its current screen: mid-fade the current one is the screen being
    // left, and handing that to the director (which adopts it while the user explores) would
    // turn the user's swipe back.
    ScreenId want = director_.decide(scene_, host_.target(), now);
    const float hold = gestures_.holdProgress(now);
    // A finger holding for settings freezes the screen: a switch now would make the panel's Done
    // return somewhere the user never was.
    if (!started_) host_.showNow(want), started_ = true;
    else if (hold <= 0) host_.show(want);
    int active = 0;
    for (int i = 0; i < kOrderN; ++i)
        if (kOrder[i] == host_.target()) active = i;
    // A selection lasts as long as explore mode (spec §3.1): when the director takes over
    // again, every screen falls back to its own default flight.
    bool exploring = director_.exploring(now);
    if (exploring_ && !exploring) host_.setSelection(0);
    exploring_ = exploring;
    const bool alert = host_.target() == ScreenId::Alert;  // the takeover covers the overlays
    dots_.set(exploring && !alert, active, kOrderN);
    badge_.set(!alert && (!scene_.online || scene_.dataAgeS > 60));
    ring_.set(hold > 0 && !alert && !host_.transitioning(), gestures_.pressX(), gestures_.pressY(), hold);
}

void ScreenApp::touch(bool pressed, int x, int y, uint32_t now) {
    if (updating_) return;
    // The ring shows only on a plain screen; elsewhere a slow press is an ordinary tap.
    gestures_.enableHold(!panel_ && !scene_.alert.active && host_.current() != ScreenId::Alert &&
                         !host_.transitioning());
    GestureEvent ev = gestures_.feed(pressed, x, y, now);
    if (panel_) {  // the panel takes every sample (its slider) and every tap; nothing else reacts
        settings_.raw(pressed, x, y, now);
        if (ev.gesture == Gesture::Tap) host_.tap(ev.x, ev.y);
        return;
    }
    if (ev.gesture == Gesture::None) return;
    // An alert takes taps only, and a tap ends it; it is not the user exploring (alerts spec §7).
    // While the Alert screen fades out the gestures are still its own.
    if (scene_.alert.active || host_.current() == ScreenId::Alert) {
        if (ev.gesture == Gesture::Tap && scene_.alert.active) alerts_.dismiss(scene_, now);
        return;
    }
    if (ev.gesture == Gesture::Hold) {
        if (!host_.transitioning()) openPanel(now);
        return;
    }
    director_.noteTouch(now);
    if (ev.gesture == Gesture::Tap) handle(host_.tap(ev.x, ev.y));
    else if (ev.gesture == Gesture::SwipeUp || ev.gesture == Gesture::SwipeDown)
        host_.verticalSwipe(ev.gesture == Gesture::SwipeUp ? -1 : +1);
    else step(ev.gesture == Gesture::SwipeLeft ? +1 : -1);
}

void ScreenApp::handle(const NavRequest& r) {
    if (r.selectHex) host_.setSelection(r.selectHex);
    if (r.open) host_.show(r.to);
}

void ScreenApp::step(int dir) {
    int at = 0;
    for (int i = 0; i < kOrderN; ++i)
        if (kOrder[i] == host_.target()) at = i;
    // Next screen in the swipe direction that exists, wrapping around.
    for (int k = 1; k < kOrderN; ++k) {
        ScreenId id = kOrder[(at + dir * k + kOrderN * k) % kOrderN];
        if (host_.screen(id)) return host_.show(id);
    }
}

}  // namespace scr
