#include "Replay.h"

#include <ArduinoJson.h>

#include "providers/Routes.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace hal {
namespace {

// The community provider's hosts (Providers.cpp): on a failed poll it tries the next, so all of them replay.
const char* const kFeeds[] = {"https://api.adsb.lol/", "https://opendata.adsb.fi/", "https://api.airplanes.live/"};
// Built from AdsbdbClient's host so a change there is followed here.
const std::string kRoutes = std::string("https://") + prov::AdsbdbClient::kHost + "/v0/callsign/";

bool startsWith(const char* s, const char* prefix) { return std::strncmp(s, prefix, std::strlen(prefix)) == 0; }

// The reply as old as it really is when served: a reply repeated after an early poll is ageSec older than it was
// recorded, and the parser dates each plane from seen_pos (else seen), so without this the planes would jump back by
// the time elapsed. Only the fields that are there; the rest keeps its value (the body is re-serialized).
std::string aged(const std::string& raw, float ageSec) {
    if (ageSec <= 0) return raw;
    JsonDocument doc;
    if (deserializeJson(doc, raw)) return raw;
    for (const char* key : {"ac", "aircraft"}) {
        for (JsonObject e : doc[key].as<JsonArray>()) {
            for (const char* f : {"seen_pos", "seen"})
                if (e[f].is<float>()) e[f] = e[f].as<float>() + ageSec;
        }
    }
    std::string out;
    serializeJson(doc, out);
    return out;
}

HttpStatus answer(int status, const std::string& text, const BodyHandler& body) {
    HttpStatus st{status};
    if (st.ok()) {
        StringSource src(text.data(), text.size());
        body(src);
    }
    return st;
}

}  // namespace

ReplayHttp::ReplayHttp(Fetch fetch, std::string dir, int replies, int intervalS, std::function<uint32_t()> ms)
    : fetch_(std::move(fetch)),
      dir_(std::move(dir)),
      replies_(replies),
      intervalMs_(std::max(1, intervalS) * 1000),
      ms_(std::move(ms)),
      base_(ms_()) {}

HttpStatus ReplayHttp::get(const char* url, const Header*, int, const BodyHandler& body) {
    for (const char* feed : kFeeds) {
        if (!startsWith(url, feed)) continue;
        // By elapsed time, not by poll count: the firmware also polls early (applySettings), which must not skip ahead.
        const uint32_t elapsed = ms_() - base_;
        const int k = int(elapsed / uint32_t(intervalMs_));
        if (ended_ || k >= replies_) return ended_ = true, HttpStatus{-1};
        if (k != current_) {
            char path[32];
            std::snprintf(path, sizeof path, "/%03d.json", k);
            cur_ = fetch_(dir_ + path);
            current_ = k;
            ++served_;
        }
        return answer(cur_.status, aged(cur_.body, float(elapsed - uint32_t(k) * uint32_t(intervalMs_)) / 1000.f), body);
    }
    if (startsWith(url, kRoutes.c_str())) {
        loadRoutes();
        auto it = routes_.find(url + kRoutes.size());
        return it == routes_.end() ? HttpStatus{404} : answer(200, it->second, body);
    }
    return {-1};
}

void ReplayHttp::loadRoutes() {
    if (routesLoaded_) return;
    routesLoaded_ = true;
    Fetched f = fetch_(dir_ + "/routes.json");
    JsonDocument doc;
    if (f.status != 200 || deserializeJson(doc, f.body)) return;  // no routes: every lookup is unknown
    for (JsonPair p : doc.as<JsonObject>()) {
        if (p.value().isNull()) continue;
        std::string text;  // re-serialized: equivalent values, not identical bytes, which is deliberate
        serializeJson(p.value(), text);
        routes_[p.key().c_str()] = std::move(text);
    }
}

ReplayClock::ReplayClock(std::function<uint32_t()> ms, int64_t startEpoch)
    : ms_(std::move(ms)), base_(ms_()), start_(startEpoch) {}

bool MemStore::getString(const char* key, char* out, size_t outLen) {
    // Truncates like hal::FileStore, so the simulator's settings read as the device's do.
    auto it = kv_.find(key);
    if (it == kv_.end() || outLen == 0) return false;
    size_t n = std::min(outLen - 1, it->second.size());
    it->second.copy(out, n);
    out[n] = 0;
    return true;
}

bool MemStore::putString(const char* key, const char* value) { return kv_[key] = value, true; }

bool MemStore::erase(const char* key) { return kv_.erase(key) > 0; }

}  // namespace hal
