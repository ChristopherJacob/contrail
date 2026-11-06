#include "Gesture.h"

#include <cstdlib>

namespace scr {

GestureEvent GestureDetector::feed(bool pressed, int x, int y, uint32_t now) {
    if (pressed) {
        if (!down_) down_ = true, held_ = false, x0_ = x_ = x, y0_ = y_ = y, maxMove_ = 0, samples_ = 0, t0_ = now;
        x_ = x, y_ = y, ++samples_;
        int m = std::abs(x - x0_) + std::abs(y - y0_);
        if (m > maxMove_) maxMove_ = m;
        if (hold_ && !held_ && maxMove_ <= kHoldSlop && now - t0_ >= kHoldMs) {
            held_ = true;
            GestureEvent ev;
            ev.gesture = Gesture::Hold, ev.x = x0_, ev.y = y0_;
            return ev;
        }
        return {};
    }
    if (!down_) return {};
    down_ = false;
    uint32_t dur = now - t0_;
    if (held_ || (hold_ && maxMove_ <= kHoldSlop && dur >= kHoldShowMs)) return {};  // the ring showed
    int dx = x_ - x0_, dy = y_ - y0_;
    GestureEvent ev;
    ev.x = x0_, ev.y = y0_;
    if (std::abs(dx) >= kSwipeMin && std::abs(dx) > 2 * std::abs(dy) && dur <= kSwipeMaxMs)
        ev.gesture = dx < 0 ? Gesture::SwipeLeft : Gesture::SwipeRight;
    else if (std::abs(dy) >= kSwipeMin && std::abs(dy) > 2 * std::abs(dx) && dur <= kSwipeMaxMs)
        ev.gesture = dy < 0 ? Gesture::SwipeUp : Gesture::SwipeDown;
    else if (maxMove_ <= kTapSlop && dur <= kTapMaxMs && samples_ >= kTapMinSamples)
        ev.gesture = Gesture::Tap;
    return ev;
}

float GestureDetector::holdProgress(uint32_t now) const {
    if (!hold_ || !down_ || held_ || maxMove_ > kHoldSlop) return 0;
    const uint32_t t = now - t0_;
    if (t < kHoldShowMs) return 0;
    return t >= kHoldMs ? 1.f : float(t) / float(kHoldMs);
}

}  // namespace scr
