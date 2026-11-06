#pragma once
// ESP32 networking, storage and clock HAL.
#include <Preferences.h>

#include <atomic>

class WiFiClientSecure;

#include "hal.h"

namespace hal {

// The lowest free heap seen since the last take, and what the network thread was doing then
// ("GET api.adsb.lol", "+tls" for a new handshake, "nvs st_air"; "" when idle). Both threads
// sample: the render loop every pass, the network thread on every socket read and NVS write, so
// a dip inside a request is caught on the thread that causes it.
struct HeapLow {
    uint32_t free = UINT32_MAX, largest = 0;
    bool byNet = false;  // seen by the network thread
    char during[80] = "";
};
void sampleHeap(bool byNet);
HeapLow takeHeapLow();  // and start a new window

// HTTP/1.1 client. HTTPS verifies against the bundled CAs (ca_bundle.h) and keeps up to
// kPoolSize connections alive: a TLS handshake costs several seconds of CPU on the ESP32,
// so polls reuse the open connection instead of handshaking every time.
class Esp32Http : public IHttp {
   public:
    explicit Esp32Http(const char* userAgent) : ua_(userAgent) {}
    HttpStatus get(const char* url, const Header* headers, int nHeaders, const BodyHandler& body) override;
    HttpStatus post(const char* url, const Header* headers, int nHeaders, const char* contentType,
                    const char* payload, const BodyHandler& body) override;
    void release(const char* host) override;
    // Counts responses of any status: a reply, even a 503, proves the link carries data
    // (main's connectivity watchdog). Atomic for readers on other threads.
    uint32_t replies() const { return replies_; }

   private:
    struct Conn {
        WiFiClientSecure* tls = nullptr;
        char host[64] = "";
        int port = 0;
        uint32_t lastUse = 0;
    };
    static constexpr int kPoolSize = 2;
    static constexpr uint32_t kIdleMs = 300000;  // reconnect rather than reuse after this (a stale socket is retried anyway)

    HttpStatus request(const char* method, const char* url, const Header* headers, int nHeaders,
                       const char* contentType, const char* payload, const BodyHandler& body);
    Conn* acquire(const char* host, int port, bool& reused);
    static void drop(Conn* c);
    const char* ua_;
    Conn pool_[kPoolSize];
    std::atomic<uint32_t> replies_{0};
};

class NvsStore : public IKeyValueStore {
   public:
    void begin() { prefs_.begin("contrail", false); }
    bool getString(const char* key, char* out, size_t outLen) override;
    bool putString(const char* key, const char* value) override;
    bool erase(const char* key) override { return prefs_.remove(key); }
    void clearAll() { prefs_.clear(); }

   private:
    Preferences prefs_;
};

class Esp32Clock : public IClock {
   public:
    uint32_t millis() const override;
    int64_t epochSeconds() const override;
    bool timeValid() const override;
};

}  // namespace hal
