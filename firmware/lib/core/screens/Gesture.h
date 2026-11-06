#pragma once
// Turns raw touch samples into taps and swipes (horizontal: screens; vertical: within a screen),
// and a hold (config spec §5) that opens the touch settings. Resistive panels jitter a few pixels,
// so a tap tolerates small movement; taps and swipes fire on release, a hold while still pressed.
#include <cstdint>

namespace scr {

enum class Gesture : uint8_t { None, Tap, SwipeLeft, SwipeRight, SwipeUp, SwipeDown, Hold };

struct GestureEvent {
    Gesture gesture = Gesture::None;
    int x = 0, y = 0;  // where the press started
};

class GestureDetector {
   public:
    static constexpr int kTapSlop = 14, kSwipeMin = 50, kHoldSlop = 12;
    static constexpr int kTapMinSamples = 2;  // a lone sample is usually panel noise
    static constexpr uint32_t kTapMaxMs = 600, kSwipeMaxMs = 900;
    // The ring shows from kHoldShowMs and the hold fires at kHoldMs; once the ring shows, a
    // release is nothing, not a tap.
    static constexpr uint32_t kHoldShowMs = 300, kHoldMs = 1000;
    // Off where the ring cannot show (the panel, an alert, a transition): there is no hold, and a
    // slow still press stays a tap (up to kTapMaxMs) instead of vanishing.
    void enableHold(bool on) { hold_ = on; }
    GestureEvent feed(bool pressed, int x, int y, uint32_t nowMs);
    // 0 when no hold is filling (not pressed, moved, already fired, or under kHoldShowMs), else the
    // fraction of kHoldMs held.
    float holdProgress(uint32_t nowMs) const;
    int pressX() const { return x0_; }
    int pressY() const { return y0_; }

   private:
    bool down_ = false, held_ = false, hold_ = true;
    int x0_ = 0, y0_ = 0, x_ = 0, y_ = 0, maxMove_ = 0, samples_ = 0;
    uint32_t t0_ = 0;
};

}  // namespace scr
