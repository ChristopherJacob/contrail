#pragma once
// Short display strings shared by the screens, and a string hash used to decide what
// to redraw (hash what is drawn, not the raw values, which drift by unseen amounts).
#include <cstddef>
#include <cstdint>

namespace ui {

constexpr uint32_t kHashSeed = 2166136261u;
uint32_t hashBytes(const void* p, size_t n, uint32_t h = kHashSeed);
uint32_t hashStr(const char* s, uint32_t h = kHashSeed);

void formatThousands(char* out, size_t n, int32_t v);  // 34000 → "34,000"
void formatAltitude(char* out, size_t n, int32_t ft);  // "34,000 ft"
void formatHundreds(char* out, size_t n, int32_t ft);  // 34000 → "340" (radar/board tags)
void formatDistance(char* out, size_t n, float nm);    // "2.4 nm" under 10, else "15 nm"
void formatClock(char* out, size_t n, int64_t epoch);  // local "HH:MM"
void formatVrate(char* out, size_t n, int16_t fpm);    // "↑ 1,200 fpm", "↓ 600 fpm", "level"
const char* compassPoint(float bearingDeg);             // 8-point: "N", "NE", …
// "↑" climbing, "↓" descending, "" level (±300 fpm).
const char* trendArrow(int16_t fpm);

// A local "HH:MM" that is formatted again only when the minute changes: localtime is not
// free, and the screens rebuild their text at 5 Hz.
class MinuteClock {
   public:
    const char* set(int64_t epoch) {
        if (epoch / 60 != minute_) minute_ = epoch / 60, formatClock(text_, sizeof text_, epoch);
        return text_;
    }
    void clear() { text_[0] = 0, minute_ = INT64_MIN; }
    const char* text() const { return text_; }

   private:
    char text_[8] = "";
    int64_t minute_ = INT64_MIN;
};

}  // namespace ui
