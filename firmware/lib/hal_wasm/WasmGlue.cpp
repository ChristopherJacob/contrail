#include "WasmGlue.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>

#include <cstdlib>

// A GET of path: the status (negative when it could not be sent) and the body, malloc'd, in *body and *len.
EM_JS(int, js_fetch, (const char* path, uint8_t** body, int* len), {
    const xhr = new XMLHttpRequest();
    xhr.open("GET", UTF8ToString(path), false);
    xhr.responseType = "arraybuffer";
    try {
        xhr.send();
    } catch (e) {
        return -1;
    }
    const bytes = new Uint8Array(xhr.response || new ArrayBuffer(0));
    const p = _malloc(bytes.length + 1);
    HEAPU8.set(bytes, p);
    HEAPU32[body >> 2] = p;
    HEAP32[len >> 2] = bytes.length;
    return xhr.status;
});

// The file's size from a HEAD request, or -1.
EM_JS(double, js_size, (const char* url), {
    const xhr = new XMLHttpRequest();
    xhr.open("HEAD", UTF8ToString(url), false);
    try {
        xhr.send();
    } catch (e) {
        return -1;
    }
    const n = Number(xhr.getResponseHeader("Content-Length"));
    return xhr.status === 200 && Number.isFinite(n) ? n : -1;
});

// n bytes at offset into out by a Range request. A server that ignores Range (python -m http.server, when
// testing) answers 200 with the whole file: that is kept, once per URL.
EM_JS(int, js_range, (const char* urlPtr, double offset, int n, uint8_t* out), {
    const url = UTF8ToString(urlPtr);
    const whole = globalThis.contrailWhole || (globalThis.contrailWhole = {});  // not ??=: a trigraph to C++
    let bytes = whole[url];
    if (!bytes) {
        const xhr = new XMLHttpRequest();
        xhr.open("GET", url, false);
        xhr.responseType = "arraybuffer";
        xhr.setRequestHeader("Range", "bytes=" + offset + "-" + (offset + n - 1));
        try {
            xhr.send();
        } catch (e) {
            return 0;
        }
        if (xhr.status === 206) {
            const b = new Uint8Array(xhr.response);
            if (b.length !== n) return 0;
            HEAPU8.set(b, out);
            return 1;
        }
        if (xhr.status !== 200) return 0;
        bytes = whole[url] = new Uint8Array(xhr.response);
    }
    if (bytes.length < offset + n) return 0;
    HEAPU8.set(bytes.subarray(offset, offset + n), out);
    return 1;
});

EM_JS(void, js_frame, (const uint16_t* px), { Module["onFrame"]?.(HEAPU16.subarray(px >> 1, (px >> 1) + 240 * 320)); });
EM_JS(void, js_led, (int r, int g, int b), { Module["onLed"]?.(r, g, b); });
EM_JS(void, js_log, (const char* line), { Module["onLog"]?.(UTF8ToString(line)); });

namespace hal::wasm {

Fetched fetchFile(const std::string& path) {
    uint8_t* body = nullptr;
    int len = 0;
    Fetched f;
    f.status = js_fetch(path.c_str(), &body, &len);
    if (body) f.body.assign(reinterpret_cast<char*>(body), size_t(len)), std::free(body);
    return f;
}

int64_t fileSize(const std::string& url) { return int64_t(js_size(url.c_str())); }

bool fetchRange(const std::string& url, uint32_t offset, uint32_t n, void* out) {
    return js_range(url.c_str(), double(offset), int(n), static_cast<uint8_t*>(out)) != 0;
}

void postFrame(const uint16_t* px) { js_frame(px); }
void postLed(uint8_t r, uint8_t g, uint8_t b) { js_led(r, g, b); }
void postLog(const char* line) { js_log(line); }
uint32_t nowMs() { return uint32_t(emscripten_get_now()); }
uint32_t nowUs() { return uint32_t(emscripten_get_now() * 1000.0); }

}  // namespace hal::wasm
#endif
