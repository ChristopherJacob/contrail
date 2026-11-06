#pragma once
// Hardware abstraction interfaces. lib/core depends only on these; each target
// (hal_esp32, hal_native) implements them.
#include <cstddef>
#include <cstdint>
#include <functional>

namespace hal {

class IDisplay {
   public:
    virtual ~IDisplay() = default;
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual void beginFrame() {}
    // Push a w x h block of RGB565 pixels (rows packed, w wide) to (x, y). May return
    // before the transfer completes (DMA); the caller must not touch `px` until waitDma().
    virtual void pushRect(int x, int y, int w, int h, const uint16_t* px) = 0;
    virtual void waitDma() {}
    virtual void endFrame() {}
};

class ITouch {
   public:
    virtual ~ITouch() = default;
    // True while pressed; coordinates in screen pixels.
    virtual bool read(int& x, int& y) = 0;
};

class ILed {
   public:
    virtual ~ILed() = default;
    virtual void set(uint8_t r, uint8_t g, uint8_t b) = 0;
};

class IBacklight {
   public:
    virtual ~IBacklight() = default;
    virtual void set(uint8_t level) = 0;  // 0..255
};

class ILightSensor {
   public:
    virtual ~ILightSensor() = default;
    virtual int read() = 0;  // 0 (dark) .. 1023 (bright)
};

class IClock {
   public:
    virtual ~IClock() = default;
    virtual uint32_t millis() const = 0;
    virtual int64_t epochSeconds() const = 0;  // UTC
    virtual bool timeValid() const = 0;
};

class IKeyValueStore {
   public:
    virtual ~IKeyValueStore() = default;
    // Returns false when the key is absent.
    virtual bool getString(const char* key, char* out, size_t outLen) = 0;
    virtual bool putString(const char* key, const char* value) = 0;
    virtual bool erase(const char* key) = 0;
};

// A file opened for reading at any offset, and overwriting inside it. Both happen on the render thread (5a
// spec §1).
// Blocks moved to and from a card, and the microseconds spent waiting for them (0 without a clock).
struct IoStats {
    uint32_t blocksRead = 0, readUs = 0, blocksWritten = 0, writeUs = 0;
};

class IFile {
   public:
    virtual ~IFile() = default;
    virtual uint32_t size() const = 0;
    // False when fewer than n bytes could be read (past the end, or the card is gone).
    virtual bool readAt(uint32_t offset, void* buf, uint32_t n) = 0;
    // Overwrites n bytes inside the file (never past size(): files do not grow). False where writing is not
    // supported or failed; after a failure the bytes there are unknown.
    virtual bool writeAt(uint32_t offset, const void* buf, uint32_t n) { return false; }
    // The card's work so far, for every file on it: zeros where nobody counts (timing logs only).
    virtual IoStats ioStats() const { return {}; }
};

// Removable storage: the SD card on the device, a folder on the desktop. Mounted once at boot.
class IStorage {
   public:
    virtual ~IStorage() = default;
    virtual bool mounted() const = 0;
    // The file at `path` ("/contrail/logos.pak"), or nullptr when it is missing or nothing is mounted. The
    // storage owns the file and keeps it open until restart; open each path once.
    virtual IFile* open(const char* path) = 0;
    // Whether files may be written safely (the SD card checks command CRCs); reads work either way.
    virtual bool writable() const { return true; }
};

// Byte source compatible with ArduinoJson's custom-reader contract.
class IByteSource {
   public:
    virtual ~IByteSource() = default;
    virtual int read() = 0;  // -1 at end
    virtual size_t readBytes(char* buf, size_t n) = 0;
};

struct Header {
    const char* name;
    const char* value;
};

struct HttpStatus {
    int code = 0;  // HTTP status, or negative for transport errors
    bool ok() const { return code >= 200 && code < 300; }
};

using BodyHandler = std::function<void(IByteSource&)>;

class IHttp {
   public:
    virtual ~IHttp() = default;
    // The body handler runs only for 2xx responses.
    virtual HttpStatus get(const char* url, const Header* headers, int nHeaders, const BodyHandler& body) = 0;
    virtual HttpStatus post(const char* url, const Header* headers, int nHeaders, const char* contentType,
                            const char* payload, const BodyHandler& body) = 0;
    // Closes any kept-alive connection to host, freeing its buffers (a TLS one holds ~44 KB).
    virtual void release(const char* host) { (void)host; }
};

// Memory-backed source, used by native HTTP and tests.
class StringSource : public IByteSource {
   public:
    StringSource(const char* data, size_t len) : p_(data), end_(data + len) {}
    int read() override { return p_ < end_ ? (unsigned char)*p_++ : -1; }
    size_t readBytes(char* buf, size_t n) override {
        size_t k = size_t(end_ - p_) < n ? size_t(end_ - p_) : n;
        for (size_t i = 0; i < k; ++i) buf[i] = p_[i];
        p_ += k;
        return k;
    }

   private:
    const char* p_;
    const char* end_;
};

}  // namespace hal
