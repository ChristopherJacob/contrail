#include "Esp32Net.h"

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "ca_bundle.h"
#include "net/HttpWire.h"

namespace hal {
namespace {

constexpr uint32_t kReadTimeoutMs = 8000;

char gActivity[80] = "";  // what the network thread is doing; only it writes
portMUX_TYPE gLowMux = portMUX_INITIALIZER_UNLOCKED;
HeapLow gLow;

// Marks what the network thread is doing for the scope's lifetime.
struct Activity {
    Activity(const char* what, const char* detail) {
        std::snprintf(gActivity, sizeof gActivity, "%s %s", what, detail);
    }
    ~Activity() { gActivity[0] = 0; }
};

// Buffered bytes from a socket. Waits for data by sleeping: Stream::readBytes spins
// without yielding, which starves core 0's idle task and trips the watchdog.
class ClientSource : public IByteSource {
   public:
    explicit ClientSource(WiFiClient& c) : c_(c) {}
    int read() override {
        char ch;
        return readBytes(&ch, 1) == 1 ? (unsigned char)ch : -1;
    }
    size_t readBytes(char* out, size_t n) override {
        size_t k = 0;
        while (k < n) {
            if (pos_ == len_ && !fill()) break;
            size_t take = std::min(n - k, len_ - pos_);
            std::memcpy(out + k, buf_ + pos_, take);
            pos_ += take, k += take;
        }
        return k;
    }

   private:
    bool fill() {
        pos_ = len_ = 0;
        if (++fills_ % 32 == 0) vTaskDelay(1);  // let IDLE0 run during long parses
        uint32_t start = millis();
        int avail;
        while ((avail = c_.available()) <= 0) {
            if (!c_.connected() || millis() - start > kReadTimeoutMs) return false;
            vTaskDelay(1);
        }
        sampleHeap(true);  // received data sits in lwIP buffers until read
        int got = c_.read(reinterpret_cast<uint8_t*>(buf_), std::min<size_t>(avail, sizeof buf_));
        if (got <= 0) return false;
        len_ = size_t(got);
        return true;
    }
    WiFiClient& c_;
    uint32_t fills_ = 0;
    char buf_[512];
    size_t pos_ = 0, len_ = 0;
};

bool writeAll(WiFiClient& c, const char* data, size_t n) { return n == 0 || c.write(reinterpret_cast<const uint8_t*>(data), n) == n; }

}  // namespace

void sampleHeap(bool byNet) {
    uint32_t f = ESP.getFreeHeap();
    if (f >= gLow.free) return;  // the common case, without the lock (a stale read only costs a retry)
    uint32_t big = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    portENTER_CRITICAL(&gLowMux);
    if (f < gLow.free) {
        gLow.free = f, gLow.largest = big, gLow.byNet = byNet;
        std::memcpy(gLow.during, gActivity, sizeof gLow.during);  // may tear mid-update: a log line only
        gLow.during[sizeof gLow.during - 1] = 0;
    }
    portEXIT_CRITICAL(&gLowMux);
}

HeapLow takeHeapLow() {
    portENTER_CRITICAL(&gLowMux);
    HeapLow out = gLow;
    gLow = HeapLow();
    portEXIT_CRITICAL(&gLowMux);
    return out;
}

HttpStatus Esp32Http::get(const char* url, const Header* h, int n, const BodyHandler& body) {
    return request("GET", url, h, n, nullptr, nullptr, body);
}

HttpStatus Esp32Http::post(const char* url, const Header* h, int n, const char* ct, const char* payload,
                           const BodyHandler& body) {
    return request("POST", url, h, n, ct, payload ? payload : "", body);
}

void Esp32Http::drop(Conn* c) {
    if (!c || !c->tls) return;
    c->tls->stop();
    delete c->tls;
    c->tls = nullptr;
    c->host[0] = 0;
}

void Esp32Http::release(const char* host) {
    for (Conn& c : pool_)
        if (c.tls && std::strcmp(c.host, host) == 0) drop(&c);
}

Esp32Http::Conn* Esp32Http::acquire(const char* host, int port, bool& reused) {
    uint32_t now = millis();
    Conn* lru = &pool_[0];
    for (Conn& c : pool_) {
        if (c.tls && c.port == port && std::strcmp(c.host, host) == 0) {
            if (now - c.lastUse < kIdleMs && c.tls->connected()) {
                reused = true;
                return &c;
            }
            drop(&c);
            lru = &c;
            break;
        }
        if (!c.tls) lru = &c;
        else if (lru->tls && c.lastUse < lru->lastUse) lru = &c;
    }
    drop(lru);  // free its TLS buffers before the new handshake allocates
    reused = false;
    // A handshake allocates ~45 KB in total, the largest piece a ~17 KB record buffer.
    if (ESP.getFreeHeap() < 50000 || heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < 18000) {
        Serial.printf("[http] not enough memory for a new TLS connection to %s\n", host);
        return nullptr;
    }
    // A handshake: the big allocations.
    std::strncat(gActivity, " +tls", sizeof gActivity - std::strlen(gActivity) - 1);
    auto* tls = new WiFiClientSecure();
    tls->setCACertBundle(kCaBundle);
    tls->setHandshakeTimeout(20);
    bool connected = tls->connect(host, port);
    sampleHeap(true);
    if (!connected) {
        delete tls;
        return nullptr;
    }
    lru->tls = tls;
    std::strncpy(lru->host, host, sizeof lru->host - 1);
    lru->port = port;
    return lru;
}

HttpStatus Esp32Http::request(const char* method, const char* url, const Header* headers, int nHeaders,
                              const char* contentType, const char* payload, const BodyHandler& body) {
    net::Url u;
    if (!net::parseUrl(url, u)) return {-1};
    size_t bodyLen = payload ? std::strlen(payload) : 0;
    char head[1024];
    int headLen = net::formatRequest(head, sizeof head, method, u, ua_, headers, nHeaders, contentType, bodyLen);
    if (headLen < 0) return {-1};
    Activity act(method, u.host);

    for (int attempt = 0; attempt < 2; ++attempt) {
        uint32_t t0 = millis();
        bool reused = false;
        Conn* slot = nullptr;
        WiFiClient plain;
        WiFiClient* c = &plain;
        if (u.tls) {
            slot = acquire(u.host, u.port, reused);
            if (!slot) return {-1};
            c = slot->tls;
        } else if (!plain.connect(u.host, u.port, 6000)) {
            return {-1};
        }
        uint32_t tConn = millis() - t0;

        ClientSource src(*c);
        net::ResponseHead rh;
        bool ok = writeAll(*c, head, size_t(headLen)) && writeAll(*c, payload, bodyLen) && net::readResponseHead(src, rh);
        if (!ok) {
            // A kept-alive connection the server already closed: reconnect once.
            drop(slot);
            if (reused && attempt == 0) continue;
            Serial.printf("[http] %s %s failed\n", method, u.host);
            return {-1};
        }
        HttpStatus st{rh.status};
        ++replies_;
        net::BodySource bodySrc(src, rh);
        if (st.ok() && body) body(bodySrc);
        bool clean = bodySrc.drain();
        if (slot) {
            if (clean && !rh.close) slot->lastUse = millis();
            else drop(slot);
        } else {
            plain.stop();
        }
        Serial.printf("[http] %s %s %d, connect %lums%s, total %lums\n", method, u.host, rh.status, tConn,
                      reused ? " (reused)" : "", millis() - t0);
        return st;
    }
    return {-1};
}

bool NvsStore::getString(const char* key, char* out, size_t outLen) {
    if (!prefs_.isKey(key)) return false;
    size_t n = prefs_.getString(key, out, outLen);
    return n > 0 || outLen == 0 || (out[0] = 0, true);
}

bool NvsStore::putString(const char* key, const char* value) {
    Activity act("nvs", key);
    bool ok = prefs_.putString(key, value) > 0 || !*value;
    sampleHeap(true);
    return ok;
}

uint32_t Esp32Clock::millis() const { return ::millis(); }

int64_t Esp32Clock::epochSeconds() const { return int64_t(time(nullptr)); }

bool Esp32Clock::timeValid() const { return time(nullptr) > 1700000000; }

}  // namespace hal
