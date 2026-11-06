#pragma once
#include "IProvider.h"

namespace prov {

// Polls the primary provider and fails over to the next configured one after repeated
// failures, probing the primary again every minute.
class ProviderChain {
   public:
    static constexpr int kMax = 4;
    static constexpr int kFailuresBeforeSwitch = 3;
    static constexpr uint32_t kPrimaryRetryMs = 60000;

    void setProviders(IProvider* const* p, int n);
    void setPrimary(int index);
    void setFallbackEnabled(bool on) { fallback_ = on; }

    bool poll(hal::IHttp& http, model::AircraftStore& store, const PollContext& ctx);

    IProvider* active() const { return n_ ? providers_[active_] : nullptr; }
    const char* activeName() const { return n_ ? providers_[active_]->name() : "none"; }
    uint32_t pollIntervalMs() const { return n_ ? providers_[active_]->pollIntervalMs() : 5000; }
    uint32_t lastOkMs() const { return lastOkMs_; }
    bool everOk() const { return everOk_; }
    int consecutiveFailures() const { return failures_; }
    bool onFallback() const { return active_ != primary_; }

   private:
    int nextConfigured(int from) const;
    IProvider* providers_[kMax] = {};
    int n_ = 0, primary_ = 0, active_ = 0, failures_ = 0;
    bool fallback_ = true, everOk_ = false;
    uint32_t lastOkMs_ = 0, lastPrimaryTryMs_ = 0;
};

}  // namespace prov
