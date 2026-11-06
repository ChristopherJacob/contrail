#pragma once
#include <string>
#include <vector>

#include "hal.h"

// Scripted HTTP: each request pops the next response (or repeats the last one).
class FakeHttp : public hal::IHttp {
   public:
    struct Response {
        int code;
        std::string body;
    };
    struct Request {
        std::string method, url, payload;
        std::vector<std::pair<std::string, std::string>> headers;
    };
    std::vector<Response> responses;
    std::vector<Request> requests;
    std::vector<std::string> released;  // hosts passed to release(), in order

    hal::HttpStatus get(const char* url, const hal::Header* h, int n, const hal::BodyHandler& body) override {
        return handle("GET", url, h, n, "", body);
    }
    hal::HttpStatus post(const char* url, const hal::Header* h, int n, const char*, const char* payload,
                         const hal::BodyHandler& body) override {
        return handle("POST", url, h, n, payload ? payload : "", body);
    }

    void release(const char* host) override { released.emplace_back(host); }

   private:
    size_t next_ = 0;
    hal::HttpStatus handle(const char* method, const char* url, const hal::Header* h, int n, const char* payload,
                           const hal::BodyHandler& body) {
        Request r{method, url, payload, {}};
        for (int i = 0; i < n; ++i) r.headers.emplace_back(h[i].name, h[i].value);
        requests.push_back(r);
        if (responses.empty()) return {-1};
        const Response& resp = responses[next_ < responses.size() ? next_ : responses.size() - 1];
        ++next_;
        hal::HttpStatus st{resp.code};
        if (st.ok() && body) {
            hal::StringSource src(resp.body.data(), resp.body.size());
            body(src);
        }
        return st;
    }
};
