#pragma once
#include <cstddef>
#include <cstring>

namespace util {

// Copies src (null: empty) into dst, truncating to cap - 1 bytes; always terminates.
inline void copyStr(char* dst, size_t cap, const char* src) {
    std::strncpy(dst, src ? src : "", cap - 1);
    dst[cap - 1] = 0;
}

}  // namespace util
