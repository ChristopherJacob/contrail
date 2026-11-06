#include "HttpWire.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace net {
namespace {

bool startsWithNoCase(const char* s, const char* prefix) {
    for (; *prefix; ++s, ++prefix)
        if (std::tolower((unsigned char)*s) != std::tolower((unsigned char)*prefix)) return false;
    return true;
}

bool containsNoCase(const char* s, const char* needle) {
    for (; *s; ++s)
        if (startsWithNoCase(s, needle)) return true;
    return false;
}

// Reads one CRLF- (or LF-) terminated line without the terminator. Overlong lines are
// truncated. Returns false at end of stream before any terminator.
bool readLine(hal::IByteSource& src, char* out, size_t cap) {
    size_t n = 0;
    for (;;) {
        int c = src.read();
        if (c < 0) return false;
        if (c == '\n') break;
        if (c != '\r' && n + 1 < cap) out[n++] = char(c);
    }
    out[n] = 0;
    return true;
}

}  // namespace

bool parseUrl(const char* url, Url& out) {
    const char* p;
    if (std::strncmp(url, "https://", 8) == 0) out.tls = true, p = url + 8;
    else if (std::strncmp(url, "http://", 7) == 0) out.tls = false, p = url + 7;
    else return false;
    const char* hostEnd = p;
    while (*hostEnd && *hostEnd != '/' && *hostEnd != ':') ++hostEnd;
    size_t hostLen = size_t(hostEnd - p);
    if (hostLen == 0 || hostLen >= sizeof out.host) return false;
    std::memcpy(out.host, p, hostLen);
    out.host[hostLen] = 0;
    out.port = out.tls ? 443 : 80;
    p = hostEnd;
    if (*p == ':') {
        out.port = std::atoi(p + 1);
        while (*p && *p != '/') ++p;
        if (out.port <= 0 || out.port > 65535) return false;
    }
    const char* path = *p ? p : "/";
    if (std::strlen(path) >= sizeof out.path) return false;
    std::strcpy(out.path, path);
    return true;
}

int formatRequest(char* out, size_t cap, const char* method, const Url& url, const char* userAgent,
                  const hal::Header* headers, int nHeaders, const char* contentType, size_t bodyLen) {
    size_t n = 0;
    auto put = [&](const char* fmt, auto... args) {
        if (n >= cap) return;
        int k = std::snprintf(out + n, cap - n, fmt, args...);
        n = k < 0 ? cap : n + size_t(k);
    };
    bool defaultPort = url.port == (url.tls ? 443 : 80);
    put("%s %s HTTP/1.1\r\n", method, url.path);
    if (defaultPort) put("Host: %s\r\n", url.host);
    else put("Host: %s:%d\r\n", url.host, url.port);
    put("User-Agent: %s\r\nAccept: application/json\r\nAccept-Encoding: identity\r\n", userAgent);
    put("%s", "Connection: keep-alive\r\n");
    for (int i = 0; i < nHeaders; ++i) put("%s: %s\r\n", headers[i].name, headers[i].value);
    if (contentType) put("Content-Type: %s\r\n", contentType);
    if (contentType || bodyLen) put("Content-Length: %u\r\n", unsigned(bodyLen));
    put("%s", "\r\n");
    return n < cap ? int(n) : -1;
}

bool readResponseHead(hal::IByteSource& raw, ResponseHead& out) {
    out = ResponseHead{};
    char line[256];
    if (!readLine(raw, line, sizeof line)) return false;
    if (std::strncmp(line, "HTTP/1.", 7) != 0 || std::strlen(line) < 12) return false;
    bool http10 = line[7] == '0';
    out.status = std::atoi(line + 9);
    if (out.status < 100) return false;
    bool keepAlive = false;
    for (;;) {
        if (!readLine(raw, line, sizeof line)) return false;
        if (!line[0]) break;
        const char* colon = std::strchr(line, ':');
        if (!colon) continue;
        const char* v = colon + 1;
        while (*v == ' ' || *v == '\t') ++v;
        if (startsWithNoCase(line, "content-length:")) out.contentLength = std::atol(v);
        else if (startsWithNoCase(line, "transfer-encoding:")) out.chunked = containsNoCase(v, "chunked");
        else if (startsWithNoCase(line, "connection:")) {
            out.close = containsNoCase(v, "close");
            keepAlive = containsNoCase(v, "keep-alive");
        }
    }
    if (http10 && !keepAlive) out.close = true;
    return true;
}

BodySource::BodySource(hal::IByteSource& raw, const ResponseHead& head) : raw_(raw) {
    if (head.chunked) mode_ = Mode::Chunked;
    else if (head.contentLength >= 0) mode_ = Mode::Length, left_ = head.contentLength, done_ = left_ == 0;
    else mode_ = Mode::UntilClose;
    // Responses that never carry a body.
    if (head.status == 204 || head.status == 304 || head.status < 200) done_ = true;
}

bool BodySource::nextChunk() {
    char line[64];
    if (!firstChunk_ && !readLine(raw_, line, sizeof line)) return false;  // CRLF after data
    firstChunk_ = false;
    if (!readLine(raw_, line, sizeof line)) return false;
    left_ = std::strtol(line, nullptr, 16);
    if (left_ > 0) return true;
    // Last chunk: skip any trailers up to the blank line.
    do {
        if (!readLine(raw_, line, sizeof line)) return false;
    } while (line[0]);
    done_ = true;
    return false;
}

int BodySource::read() {
    char c;
    return readBytes(&c, 1) == 1 ? (unsigned char)c : -1;
}

size_t BodySource::readBytes(char* buf, size_t n) {
    if (done_ || n == 0) return 0;
    if (mode_ == Mode::UntilClose) {
        size_t k = raw_.readBytes(buf, n);
        if (k == 0) done_ = true;
        return k;
    }
    if (mode_ == Mode::Chunked && left_ == 0 && !nextChunk()) return 0;
    size_t want = size_t(left_) < n ? size_t(left_) : n;
    size_t k = raw_.readBytes(buf, want);
    left_ -= long(k);
    if (mode_ == Mode::Length && left_ == 0) done_ = true;
    return k;
}

bool BodySource::drain() {
    char buf[64];
    while (readBytes(buf, sizeof buf) > 0) {
    }
    return done_;
}

}  // namespace net
