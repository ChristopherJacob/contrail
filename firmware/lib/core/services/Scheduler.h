#pragma once
#include <cstdint>
#include <functional>

namespace svc {

// Cooperative interval timers driven by tick(nowMs). Jobs run on the ticking thread.
class Scheduler {
   public:
    static constexpr int kMax = 12;
    using Job = std::function<void(uint32_t nowMs)>;

    // Returns a job id, or -1 when full. The first run happens on the first tick.
    int every(uint32_t intervalMs, Job job) {
        if (n_ == kMax) return -1;
        jobs_[n_] = {intervalMs, 0, true, std::move(job)};
        return n_++;
    }
    void setInterval(int id, uint32_t ms) {
        if (id >= 0 && id < n_) jobs_[id].interval = ms;
    }
    void runSoon(int id) {
        if (id >= 0 && id < n_) jobs_[id].due = true;
    }
    void tick(uint32_t nowMs) {
        for (int i = 0; i < n_; ++i) {
            Entry& e = jobs_[i];
            if (e.due || nowMs - e.last >= e.interval) {
                e.due = false;
                e.last = nowMs;
                e.job(nowMs);
            }
        }
    }

   private:
    struct Entry {
        uint32_t interval, last;
        bool due;
        Job job;
    };
    Entry jobs_[kMax];
    int n_ = 0;
};

}  // namespace svc
