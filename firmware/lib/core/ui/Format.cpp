#include "Format.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace ui {

uint32_t hashBytes(const void* p, size_t n, uint32_t h) {
    auto b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 16777619u;
    return h;
}

uint32_t hashStr(const char* s, uint32_t h) {
    for (; *s; ++s) h = (h ^ uint8_t(*s)) * 16777619u;
    return (h ^ 0xFF) * 16777619u;  // terminator, so "ab"+"c" differs from "a"+"bc"
}

void formatThousands(char* out, size_t n, int32_t v) {
    const int64_t a = v < 0 ? -int64_t(v) : int64_t(v);
    const char* sign = v < 0 ? "-" : "";
    if (a >= 1000000000)
        std::snprintf(out, n, "%s%d,%03d,%03d,%03d", sign, int(a / 1000000000), int(a / 1000000 % 1000),
                      int(a / 1000 % 1000), int(a % 1000));
    else if (a >= 1000000)
        std::snprintf(out, n, "%s%d,%03d,%03d", sign, int(a / 1000000), int(a / 1000 % 1000), int(a % 1000));
    else if (a >= 1000) std::snprintf(out, n, "%s%d,%03d", sign, int(a / 1000), int(a % 1000));
    else std::snprintf(out, n, "%d", int(v));
}

void formatAltitude(char* out, size_t n, int32_t ft) {
    char num[16];
    formatThousands(num, sizeof num, ft);
    std::snprintf(out, n, "%s ft", num);
}

void formatHundreds(char* out, size_t n, int32_t ft) {
    std::snprintf(out, n, "%03d", int(ft < 0 ? 0 : ft / 100));
}

void formatDistance(char* out, size_t n, float nm) {
    if (nm < 9.95f) std::snprintf(out, n, "%.1f nm", nm);
    else std::snprintf(out, n, "%d nm", int(nm + 0.5f));
}

void formatClock(char* out, size_t n, int64_t epoch) {
    std::time_t t = std::time_t(epoch);
    std::tm tm{};
    localtime_r(&t, &tm);
    std::snprintf(out, n, "%02d:%02d", tm.tm_hour, tm.tm_min);
}

void formatVrate(char* out, size_t n, int16_t fpm) {
    if (std::abs(fpm) < 300) {
        std::snprintf(out, n, "level");
        return;
    }
    char num[16];
    int rounded = (std::abs(fpm) + 50) / 100 * 100;
    formatThousands(num, sizeof num, rounded);
    std::snprintf(out, n, "%s %s fpm", fpm > 0 ? "↑" : "↓", num);
}

const char* compassPoint(float b) {
    static const char* const kPts[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    int i = int(std::floor((b + 22.5f) / 45.f)) % 8;
    return kPts[i < 0 ? i + 8 : i];
}

const char* trendArrow(int16_t fpm) { return fpm >= 300 ? "↑" : fpm <= -300 ? "↓" : ""; }

}  // namespace ui
