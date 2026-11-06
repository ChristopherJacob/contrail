#pragma once
// Minimal mutex usable from both FreeRTOS and desktop builds.
#if defined(CONTRAIL_ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
namespace svc {
class Mutex {
   public:
    Mutex() : h_(xSemaphoreCreateMutex()) {}
    void lock() { xSemaphoreTake(h_, portMAX_DELAY); }
    void unlock() { xSemaphoreGive(h_); }

   private:
    SemaphoreHandle_t h_;
};
}  // namespace svc
#else
#include <mutex>
namespace svc {
class Mutex {
   public:
    void lock() { m_.lock(); }
    void unlock() { m_.unlock(); }

   private:
    std::mutex m_;
};
}  // namespace svc
#endif

namespace svc {
class LockGuard {
   public:
    explicit LockGuard(Mutex& m) : m_(m) { m_.lock(); }
    ~LockGuard() { m_.unlock(); }
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;

   private:
    Mutex& m_;
};
}  // namespace svc
