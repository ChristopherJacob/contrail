#include "PanelBridge.h"

#include "services/CoreSettings.h"

namespace app {

scr::PanelValues panelValues(const svc::Settings& s) {
    scr::PanelValues v;
    v.brightness = s.getInt(svc::key::kBrightness);
    v.editorial = s.getEnum(svc::key::kTheme) == "editorial";
    v.radiusNm = s.getInt(svc::key::kRadius);
    v.mode = scr::parseDirectorMode(s.getEnum(svc::key::kScreenMode).c_str());
    return v;
}

bool applyEdit(svc::Settings& s, const scr::SettingEdit& e) { return e.key && s.set(e.key, e.value); }

bool EditQueue::push(const scr::SettingEdit& e) {
    svc::LockGuard g(mu_);
    if (n_ == kMax) return false;
    q_[n_++] = e;
    return true;
}

bool EditQueue::pop(scr::SettingEdit& out) {
    svc::LockGuard g(mu_);
    if (!n_) return false;
    out = q_[0];
    for (int i = 1; i < n_; ++i) q_[i - 1] = q_[i];
    --n_;
    return true;
}

}  // namespace app
