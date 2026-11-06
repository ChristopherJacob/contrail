#include "ScreenHost.h"

namespace scr {

ScreenHost::ScreenHost(Screen* const* screens, int n, const Theme& theme)
    : screens_(screens), n_(n), theme_(theme), cur_(screens[0]) {}

Screen* ScreenHost::screen(ScreenId id) const {
    for (int i = 0; i < n_; ++i)
        if (screens_[i]->id() == id) return screens_[i];
    return nullptr;
}

uint8_t ScreenHost::veilAlpha() const {
    float f = t_ / kFadeS;
    if (f > 1) f = 1;
    if (phase_ == Phase::Out) return uint8_t(255 * f);
    if (phase_ == Phase::In) return uint8_t(255 * (1 - f));
    return 0;
}

void ScreenHost::activate(Screen* s) {
    cur_ = s;
    cur_->setScene(scene_);
    cur_->setSelection(selected_);
    cur_->enter();
    full_ = true;
}

void ScreenHost::showNow(ScreenId id) {
    Screen* s = screen(id);
    if (!s) return;
    next_ = nullptr, phase_ = Phase::Idle, t_ = 0;
    activate(s);
}

void ScreenHost::show(ScreenId id) {
    Screen* s = screen(id);
    if (!s || s == next_ || (s == cur_ && phase_ != Phase::Out)) return;
    if (s == cur_) {  // heading back to where we came from: fade back in from the current veil level
        t_ = kFadeS * (1 - t_ / kFadeS), next_ = nullptr, phase_ = Phase::In;
        return;
    }
    if (phase_ == Phase::In) t_ = kFadeS * (1 - t_ / kFadeS);  // reverse from the current veil level
    else if (phase_ == Phase::Idle) t_ = 0;
    next_ = s, phase_ = Phase::Out;
}

void ScreenHost::setScene(const Scene* scene) {
    scene_ = scene;
    cur_->setScene(scene);
}

void ScreenHost::sceneChanged() { cur_->setScene(scene_); }

void ScreenHost::setSelection(uint32_t hex) {
    selected_ = hex;
    cur_->setSelection(hex);
}

NavRequest ScreenHost::tap(int x, int y) {
    if (phase_ != Phase::Idle) return {};
    return cur_->onTap(x, y);
}

void ScreenHost::verticalSwipe(int dir) {
    if (phase_ == Phase::Idle) cur_->onVerticalSwipe(dir);
}

void ScreenHost::addOverlay(gfx::Layer* layer) {
    if (nOverlays_ < kMaxOverlays) overlays_[nOverlays_++] = layer;
}

void ScreenHost::update(float dt, gfx::DamageList& damage) {
    if (phase_ != Phase::Idle) {
        t_ += dt;
        if (phase_ == Phase::Out && t_ >= kFadeS) {
            activate(next_);
            next_ = nullptr, phase_ = Phase::In, t_ = 0;
        } else if (phase_ == Phase::In && t_ >= kFadeS) {
            phase_ = Phase::Idle, t_ = 0;
            full_ = true;  // one clean frame without the veil
        }
    }
    if (full_ || phase_ != Phase::Idle) damage.addAll();
    full_ = false;
    gfx::Layer* const* ls = cur_->layers();
    for (int i = 0; i < cur_->layerCount(); ++i) ls[i]->update(dt, damage);
    for (int i = 0; i < nOverlays_; ++i) overlays_[i]->update(dt, damage);
}

void ScreenHost::draw(gfx::Canvas& c) {
    gfx::Layer* const* ls = cur_->layers();
    for (int i = 0; i < cur_->layerCount(); ++i) ls[i]->draw(c);
    for (int i = 0; i < nOverlays_; ++i) overlays_[i]->draw(c);
    if (uint8_t a = veilAlpha()) c.fillBlend(c.clip(), theme_.veil, a);
}

}  // namespace scr
