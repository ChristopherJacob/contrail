#pragma once
// The browser simulator's recorded data (6b spec §3-4), free of emscripten so the native tests build it:
// ReplayHttp answers the community feeds with a place's recorded adsb.lol replies and adsbdb with its recorded
// routes, ReplayClock runs from the recording's start, MemStore keeps the settings in memory.
#include <cstdint>
#include <functional>
#include <map>
#include <string>

#include "hal.h"

namespace hal {

struct Fetched {
    int status = 0;  // HTTP status, or negative when the request failed
    std::string body;
};
// A GET of a file of the site, relative to the worker ("sim/kfdk/000.json").
using Fetch = std::function<Fetched(const std::string& path)>;

class ReplayHttp : public IHttp {
   public:
    // dir: the place's folder ("sim/kfdk"); replies: how many NNN.json it holds, recorded intervalS seconds apart;
    // ms: a millisecond counter (the one the ReplayClock reads), the playback starting when this is built.
    ReplayHttp(Fetch fetch, std::string dir, int replies, int intervalS, std::function<uint32_t()> ms);
    // A community feed URL (any of the three hosts, Providers.cpp) gets the reply of the elapsed time: reply
    // elapsed / interval. A poll the firmware makes early (a settings change polls at once) repeats the current
    // reply, so the recording keeps its pace. At the first poll past the last reply the poll fails and ended()
    // turns true: the caller loads the place again. An adsbdb callsign URL
    // (Routes.cpp) gets the recorded route, or 404 as adsbdb answers an unknown callsign. Anything else fails
    // without a request.
    HttpStatus get(const char* url, const Header* headers, int nHeaders, const BodyHandler& body) override;
    HttpStatus post(const char*, const Header*, int, const char*, const char*, const BodyHandler&) override {
        return {-1};
    }
    bool ended() const { return ended_; }
    int served() const { return served_; }  // distinct replies handed out

   private:
    void loadRoutes();
    Fetch fetch_;
    std::string dir_;
    int replies_;
    int intervalMs_;
    std::function<uint32_t()> ms_;
    uint32_t base_;
    int current_ = -1;  // the reply index cached in cur_
    Fetched cur_;
    int served_ = 0;
    bool ended_ = false;
    bool routesLoaded_ = false;
    std::map<std::string, std::string> routes_;  // callsign → adsbdb's reply body; absent or null: 404
};

class ReplayClock : public IClock {
   public:
    // ms: a millisecond counter (performance.now() in the worker); the clock starts at 0 and at startEpoch.
    ReplayClock(std::function<uint32_t()> ms, int64_t startEpoch);
    uint32_t millis() const override { return ms_() - base_; }
    int64_t epochSeconds() const override { return start_ + millis() / 1000; }
    bool timeValid() const override { return true; }

   private:
    std::function<uint32_t()> ms_;
    uint32_t base_;
    int64_t start_;
};

class MemStore : public IKeyValueStore {
   public:
    bool getString(const char* key, char* out, size_t outLen) override;
    bool putString(const char* key, const char* value) override;
    bool erase(const char* key) override;
    // Kept across a reload of the place, so panel edits survive the recording's loop.
    const std::map<std::string, std::string>& all() const { return kv_; }
    void load(const std::map<std::string, std::string>& kv) { kv_ = kv; }

   private:
    std::map<std::string, std::string> kv_;
};

}  // namespace hal
