#pragma once
#include <functional>

namespace svc {

enum class Ev { AircraftUpdated, ProviderStatusChanged, SettingsChanged, NetworkChanged };

// Synchronous publish/subscribe. Handlers run on the publisher's thread, so keep them
// short and thread-aware.
class EventBus {
   public:
    using Handler = std::function<void(Ev, int)>;
    static constexpr int kMax = 16;

    bool subscribe(Handler h) {
        if (n_ == kMax) return false;
        handlers_[n_++] = std::move(h);
        return true;
    }
    void publish(Ev e, int arg = 0) const {
        for (int i = 0; i < n_; ++i) handlers_[i](e, arg);
    }

   private:
    Handler handlers_[kMax];
    int n_ = 0;
};

}  // namespace svc
