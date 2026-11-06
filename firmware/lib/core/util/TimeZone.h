#pragma once
#include <cstddef>
#include <cstdio>

namespace util {

// A fixed POSIX TZ string for a UTC offset in seconds (east positive, as ip-api.com gives it):
// -18000 -> "<-05>5". POSIX counts west as positive, hence the flipped sign after the name.
// No daylight-saving rule: the offset is the one in force when it was looked up. Offsets outside
// UTC-12..+14 or not in whole minutes give "UTC0".
inline void posixFromOffset(int offsetS, char* out, size_t n) {
    if (offsetS < -12 * 3600 || offsetS > 14 * 3600 || offsetS % 60) {
        std::snprintf(out, n, "UTC0");
        return;
    }
    const char sign = offsetS < 0 ? '-' : '+';
    const int a = offsetS < 0 ? -offsetS : offsetS, h = a / 3600, m = a / 60 % 60;
    const char* west = offsetS > 0 ? "-" : "";
    if (m) std::snprintf(out, n, "<%c%02d%02d>%s%d:%02d", sign, h, m, west, h, m);
    else std::snprintf(out, n, "<%c%02d>%s%d", sign, h, west, h);
}

}  // namespace util
