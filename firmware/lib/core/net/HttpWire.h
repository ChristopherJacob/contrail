#pragma once
// HTTP/1.1 wire format: URL parsing, request formatting, response head parsing and a body
// decoder (Content-Length, chunked or read-to-close). Transport-agnostic so the ESP32
// client can keep connections alive and the logic stays unit-testable.
#include <cstddef>

#include "hal.h"

namespace net {

struct Url {
    bool tls = false;
    char host[64] = "";
    int port = 0;
    char path[256] = "";
};

// Accepts http:// and https:// URLs; fails on anything else or on overlong parts.
bool parseUrl(const char* url, Url& out);

// Writes the request line and headers (including the blank line) into out. A body of
// bodyLen bytes, if any, is sent separately. Returns the length, or -1 if out is too small.
int formatRequest(char* out, size_t cap, const char* method, const Url& url, const char* userAgent,
                  const hal::Header* headers, int nHeaders, const char* contentType, size_t bodyLen);

struct ResponseHead {
    int status = 0;
    long contentLength = -1;  // -1 when absent
    bool chunked = false;
    bool close = false;  // server will close the connection after this response
};

// Reads the status line and headers, stopping right after the blank line.
bool readResponseHead(hal::IByteSource& raw, ResponseHead& out);

// The response body as a plain byte stream. read() returns -1 at the end of the body
// (or early if the connection drops; complete() tells the two apart).
class BodySource : public hal::IByteSource {
   public:
    BodySource(hal::IByteSource& raw, const ResponseHead& head);
    int read() override;
    size_t readBytes(char* buf, size_t n) override;
    bool complete() const { return done_; }
    // Reads and discards the rest of the body so the connection can be reused.
    bool drain();

   private:
    enum class Mode { Length, Chunked, UntilClose };
    bool nextChunk();
    hal::IByteSource& raw_;
    Mode mode_;
    long left_ = 0;  // bytes left in the body (Length) or current chunk (Chunked)
    bool firstChunk_ = true;
    bool done_ = false;
};

}  // namespace net
