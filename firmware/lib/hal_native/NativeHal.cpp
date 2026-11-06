#include "NativeHal.h"

#include <ArduinoJson.h>
#include <curl/curl.h>

#include <chrono>
#include <fstream>
#include <sstream>

namespace hal {
namespace {

int64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

size_t appendBody(char* ptr, size_t size, size_t n, void* user) {
    static_cast<std::string*>(user)->append(ptr, size * n);
    return size * n;
}

}  // namespace

NativeClock::NativeClock() : startNs_(nowNs()) {}
uint32_t NativeClock::millis() const { return uint32_t((nowNs() - startNs_) / 1000000); }
int64_t NativeClock::epochSeconds() const {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

FileStore::FileStore(std::string path) : path_(std::move(path)) {
    std::ifstream in(path_);
    if (!in) return;
    JsonDocument doc;
    if (deserializeJson(doc, in)) return;
    for (JsonPair p : doc.as<JsonObject>()) kv_[p.key().c_str()] = p.value().as<std::string>();
}

bool FileStore::getString(const char* key, char* out, size_t outLen) {
    auto it = kv_.find(key);
    if (it == kv_.end() || outLen == 0) return false;
    size_t n = std::min(outLen - 1, it->second.size());
    it->second.copy(out, n);
    out[n] = 0;
    return true;
}

bool FileStore::putString(const char* key, const char* value) {
    kv_[key] = value;
    save();
    return true;
}

bool FileStore::erase(const char* key) {
    bool had = kv_.erase(key) > 0;
    if (had) save();
    return had;
}

void FileStore::save() {
    JsonDocument doc;
    for (auto& [k, v] : kv_) doc[k] = v;
    std::ofstream out(path_);
    serializeJsonPretty(doc, out);
}

CurlHttp::CurlHttp(const char* userAgent) : ua_(userAgent) { curl_global_init(CURL_GLOBAL_DEFAULT); }

HttpStatus CurlHttp::get(const char* url, const Header* h, int n, const BodyHandler& body) {
    return request(url, h, n, nullptr, nullptr, body);
}

HttpStatus CurlHttp::post(const char* url, const Header* h, int n, const char* ct, const char* payload,
                          const BodyHandler& body) {
    return request(url, h, n, ct, payload ? payload : "", body);
}

HttpStatus CurlHttp::request(const char* url, const Header* headers, int nHeaders, const char* contentType,
                             const char* payload, const BodyHandler& body) {
    CURL* c = curl_easy_init();
    if (!c) return {-1};
    std::string buf;
    curl_slist* hl = nullptr;
    for (int i = 0; i < nHeaders; ++i) hl = curl_slist_append(hl, (std::string(headers[i].name) + ": " + headers[i].value).c_str());
    if (contentType) hl = curl_slist_append(hl, (std::string("Content-Type: ") + contentType).c_str());
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_USERAGENT, ua_.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hl);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, appendBody);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &buf);
    if (payload) curl_easy_setopt(c, CURLOPT_POSTFIELDS, payload);
    CURLcode rc = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(hl);
    curl_easy_cleanup(c);
    HttpStatus st{rc == CURLE_OK ? int(code) : -int(rc)};
    if (st.ok() && body) {
        StringSource src(buf.data(), buf.size());
        body(src);
    }
    return st;
}

}  // namespace hal
