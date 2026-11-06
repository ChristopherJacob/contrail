#include "ProviderChain.h"

namespace prov {

void ProviderChain::setProviders(IProvider* const* p, int n) {
    n_ = n > kMax ? kMax : n;
    for (int i = 0; i < n_; ++i) providers_[i] = p[i];
    setPrimary(primary_ < n_ ? primary_ : 0);
}

void ProviderChain::setPrimary(int index) {
    if (index < 0 || index >= n_) return;
    primary_ = active_ = index;
    failures_ = 0;
    if (!providers_[active_]->configured()) active_ = nextConfigured(active_);
}

int ProviderChain::nextConfigured(int from) const {
    for (int k = 1; k <= n_; ++k) {
        int i = (from + k) % n_;
        if (providers_[i]->configured()) return i;
    }
    return from;
}

bool ProviderChain::poll(hal::IHttp& http, model::AircraftStore& store, const PollContext& ctx) {
    if (!n_) return false;
    // While on a fallback, periodically give the primary another chance.
    if (active_ != primary_ && providers_[primary_]->configured() && ctx.nowMs - lastPrimaryTryMs_ >= kPrimaryRetryMs) {
        lastPrimaryTryMs_ = ctx.nowMs;
        if (providers_[primary_]->poll(http, store, ctx)) {
            active_ = primary_;
            failures_ = 0;
            lastOkMs_ = ctx.nowMs, everOk_ = true;
            return true;
        }
    }
    if (providers_[active_]->poll(http, store, ctx)) {
        failures_ = 0;
        lastOkMs_ = ctx.nowMs, everOk_ = true;
        return true;
    }
    if (++failures_ >= kFailuresBeforeSwitch && fallback_) {
        int next = nextConfigured(active_);
        if (next != active_) {
            if (active_ == primary_) lastPrimaryTryMs_ = ctx.nowMs;
            active_ = next;
            failures_ = 0;
        }
    }
    return false;
}

}  // namespace prov
