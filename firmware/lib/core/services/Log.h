#pragma once
// printf-style logging routed to a platform sink (Serial on ESP32, stderr on desktop).
#include <cstdarg>
#include <cstdio>

namespace svc {

using LogSink = void (*)(const char* line);

inline LogSink& logSink() {
    static LogSink sink = nullptr;
    return sink;
}

inline void logf(const char* fmt, ...) {
    if (!logSink()) return;
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    logSink()(buf);
}

}  // namespace svc
