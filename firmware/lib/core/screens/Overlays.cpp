#include "Overlays.h"

#include "gfx/Font.h"
#include "gfx/Raster.h"
#include "gfx/fonts/Fonts.h"
#include "ui/Widgets.h"

namespace scr {

void PageDots::set(bool visible, int active, int count) {
    if (visible != visible_ || (visible && (active != active_ || count != count_))) dirty_ = true;
    visible_ = visible, active_ = active, count_ = count;
}

void PageDots::update(float, gfx::DamageList& d) {
    if (dirty_) d.add(kBox);
    dirty_ = false;
}

void PageDots::draw(gfx::Canvas& c) {
    if (visible_ && c.clip().intersects(kBox)) ui::pageDots(c, count_, active_, kY, t_);
}

void OfflineBadge::set(bool on) {
    if (on != on_) dirty_ = true;
    on_ = on;
}

void OfflineBadge::update(float, gfx::DamageList& d) {
    if (dirty_) d.add(kBox);
    dirty_ = false;
}

void OfflineBadge::draw(gfx::Canvas& c) {
    if (!on_ || !c.clip().intersects(kBox)) return;
    gfx::fillRoundRect(c, kBox, 7.5f, t_.veil, 210);
    gfx::drawText(c, gfx::kFontSmall, kBox.x + kBox.w / 2, kBox.y + 11, "offline", t_.descend, gfx::Align::Center);
}

void HoldRing::set(bool visible, int x, int y, float progress) {
    const int cx = x < kHalf ? kHalf : x > 240 - kHalf ? 240 - kHalf : x;
    const int cy = y < kHalf ? kHalf : y > 320 - kHalf ? 320 - kHalf : y;
    const int step = visible ? int(progress * 64) : 0;
    if (visible != visible_ || (visible && (step != step_ || cx != cx_ || cy != cy_))) dirty_ = true;
    visible_ = visible, cx_ = cx, cy_ = cy, step_ = step;
}

int HoldRing::hintBaseline() const { return cy_ + kHalf + 14 <= 314 ? cy_ + kHalf + 14 : cy_ - kHalf - 6; }

gfx::Rect HoldRing::box() const {
    const gfx::Rect ring{cx_ - kHalf, cy_ - kHalf, 2 * kHalf, 2 * kHalf};
    const gfx::Rect hint{0, hintBaseline() - 15, 240, 20};
    return ring.unite(hint);
}

void HoldRing::update(float, gfx::DamageList& d) {
    if (!dirty_) return;
    dirty_ = false;
    if (!drawn_.empty()) d.add(drawn_);
    drawn_ = visible_ ? box() : gfx::Rect{};
    if (!drawn_.empty()) d.add(drawn_);
}

void HoldRing::draw(gfx::Canvas& c) {
    if (!visible_ || !c.clip().intersects(drawn_)) return;
    const float x = float(cx_), y = float(cy_);
    gfx::aaCircle(c, x, y, kHalf - 2, t_.shadow, 170);
    gfx::aaRing(c, x, y, kR, 6, t_.text, 50);
    gfx::aaArc(c, x, y, kR, 6, 0, 360.f * step_ / 64.f, t_.accent);
    gfx::aaCircle(c, x, y, 12, t_.text, 90);
    const gfx::FontData& f = t_.upperHeads ? *t_.fontLabel : gfx::kFontBold;
    ui::shadowText(c, f, 120, hintBaseline(), t_.upperHeads ? "KEEP HOLDING FOR SETTINGS" : "Keep holding for settings",
                   t_.text, gfx::Align::Center, t_);
}

}  // namespace scr
