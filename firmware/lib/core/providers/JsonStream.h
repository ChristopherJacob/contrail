#pragma once
// Stream large JSON arrays element by element so memory stays proportional to one
// element, not the whole response (the CYD has ~100 KB of usable heap with TLS up).
#include <ArduinoJson.h>

#include <cstring>

#include "hal.h"

namespace prov {

// ArduinoJson reader adapter over a polymorphic byte source.
struct SourceReader {
    hal::IByteSource& src;
    int read() { return src.read(); }
    size_t readBytes(char* buf, size_t n) { return src.readBytes(buf, n); }
};

// Advance past `"<key>" : [` for the first of `keys` found. Returns false at end of input
// or if the key's value is not an array (e.g. `"states": null`).
inline bool seekArray(hal::IByteSource& src, const char* const* keys, int nKeys) {
    // Track a window of the last bytes read to recognise `"key"`.
    char win[24] = {0};
    int c;
    while ((c = src.read()) >= 0) {
        std::memmove(win, win + 1, sizeof(win) - 2);
        win[sizeof(win) - 2] = char(c);
        if (c != '"') continue;
        for (int k = 0; k < nKeys; ++k) {
            size_t len = std::strlen(keys[k]);
            const char* tail = win + sizeof(win) - 2 - len - 1;  // position of the opening quote
            if (tail < win || tail[0] != '"' || std::strncmp(tail + 1, keys[k], len) != 0) continue;
            // Found the key; expect ':' then '['.
            int d;
            while ((d = src.read()) == ' ' || d == '\n' || d == '\r' || d == '\t') {}
            if (d != ':') break;
            while ((d = src.read()) == ' ' || d == '\n' || d == '\r' || d == '\t') {}
            return d == '[';
        }
    }
    return false;
}

// Calls fn(JsonVariantConst) for each element after seekArray(). Returns false if the
// stream ends early or an element fails to parse.
template <typename Fn>
bool forEachElement(hal::IByteSource& src, JsonDocument& doc, const JsonDocument* filter, Fn fn) {
    for (;;) {
        // Peek for an empty array or the end of the array.
        int c;
        while ((c = src.read()) == ' ' || c == '\n' || c == '\r' || c == '\t' || c == ',') {}
        if (c == ']') return true;
        if (c < 0) return false;
        // Re-feed the consumed first character through a tiny prefix source.
        struct Prefixed : hal::IByteSource {
            hal::IByteSource& inner;
            int first;
            Prefixed(hal::IByteSource& i, int f) : inner(i), first(f) {}
            int read() override {
                if (first >= 0) {
                    int f = first;
                    first = -1;
                    return f;
                }
                return inner.read();
            }
            size_t readBytes(char* buf, size_t n) override {
                size_t k = 0;
                if (n && first >= 0) buf[k++] = char(first), first = -1;
                return k + inner.readBytes(buf + k, n - k);
            }
        } pre(src, c);
        SourceReader r2{pre};
        DeserializationError err = filter ? deserializeJson(doc, r2, DeserializationOption::Filter(*filter))
                                          : deserializeJson(doc, r2);
        if (err) return false;
        fn(doc.as<JsonVariantConst>());
    }
}

}  // namespace prov
