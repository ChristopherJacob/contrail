#pragma once
// Desktop implementations of the non-display HAL interfaces.
#include <map>
#include <string>

#include "hal.h"

namespace hal {

class NativeClock : public IClock {
   public:
    NativeClock();
    uint32_t millis() const override;
    int64_t epochSeconds() const override;
    bool timeValid() const override { return true; }

   private:
    int64_t startNs_;
};

// Key/value pairs persisted as a flat JSON object on disk.
class FileStore : public IKeyValueStore {
   public:
    explicit FileStore(std::string path);
    bool getString(const char* key, char* out, size_t outLen) override;
    bool putString(const char* key, const char* value) override;
    bool erase(const char* key) override;

   private:
    void save();
    std::string path_;
    std::map<std::string, std::string> kv_;
};

class CurlHttp : public IHttp {
   public:
    explicit CurlHttp(const char* userAgent);
    HttpStatus get(const char* url, const Header* headers, int nHeaders, const BodyHandler& body) override;
    HttpStatus post(const char* url, const Header* headers, int nHeaders, const char* contentType,
                    const char* payload, const BodyHandler& body) override;

   private:
    HttpStatus request(const char* url, const Header* headers, int nHeaders, const char* contentType,
                       const char* payload, const BodyHandler& body);
    std::string ua_;
};

class NullLed : public ILed {
   public:
    void set(uint8_t, uint8_t, uint8_t) override {}
};

}  // namespace hal
