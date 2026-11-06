#include <unity.h>

#include <cstring>
#include <string>

#include "net/HttpWire.h"

using namespace net;

void setUp() {}
void tearDown() {}

static std::string readAll(hal::IByteSource& s) {
    std::string out;
    char buf[7];  // odd size to cross chunk boundaries
    size_t n;
    while ((n = s.readBytes(buf, sizeof buf)) > 0) out.append(buf, n);
    return out;
}

static void test_parse_url() {
    Url u;
    TEST_ASSERT_TRUE(parseUrl("https://api.adsb.lol/v2/point/1/2/25", u));
    TEST_ASSERT_TRUE(u.tls);
    TEST_ASSERT_EQUAL_STRING("api.adsb.lol", u.host);
    TEST_ASSERT_EQUAL(443, u.port);
    TEST_ASSERT_EQUAL_STRING("/v2/point/1/2/25", u.path);

    TEST_ASSERT_TRUE(parseUrl("http://192.168.1.5:8080/data/aircraft.json", u));
    TEST_ASSERT_FALSE(u.tls);
    TEST_ASSERT_EQUAL_STRING("192.168.1.5", u.host);
    TEST_ASSERT_EQUAL(8080, u.port);
    TEST_ASSERT_EQUAL_STRING("/data/aircraft.json", u.path);

    TEST_ASSERT_TRUE(parseUrl("http://ip-api.com", u));
    TEST_ASSERT_EQUAL_STRING("/", u.path);
    TEST_ASSERT_EQUAL(80, u.port);

    TEST_ASSERT_FALSE(parseUrl("ftp://x/y", u));
}

static void test_format_request() {
    Url u;
    parseUrl("https://auth.example.com/token", u);
    hal::Header h[] = {{"x-apikey", "abc"}};
    char buf[512];
    int n = formatRequest(buf, sizeof buf, "POST", u, "Contrail/0.1", h, 1, "application/x-www-form-urlencoded", 9);
    TEST_ASSERT_GREATER_THAN(0, n);
    std::string s(buf, n);
    TEST_ASSERT_EQUAL(0, s.find("POST /token HTTP/1.1\r\nHost: auth.example.com\r\n"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, s.find("x-apikey: abc\r\n"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, s.find("Content-Length: 9\r\n"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, s.find("Accept-Encoding: identity\r\n"));
    TEST_ASSERT_EQUAL(s.size() - 4, s.rfind("\r\n\r\n"));
    // Too small a buffer fails instead of truncating.
    TEST_ASSERT_EQUAL(-1, formatRequest(buf, 40, "POST", u, "Contrail/0.1", h, 1, nullptr, 0));
}

static void test_head_content_length() {
    const char* raw =
        "HTTP/1.1 200 OK\r\ncontent-length: 5\r\nContent-Type: application/json\r\n\r\nhelloEXTRA";
    hal::StringSource src(raw, std::strlen(raw));
    ResponseHead h;
    TEST_ASSERT_TRUE(readResponseHead(src, h));
    TEST_ASSERT_EQUAL(200, h.status);
    TEST_ASSERT_EQUAL(5, h.contentLength);
    TEST_ASSERT_FALSE(h.chunked);
    TEST_ASSERT_FALSE(h.close);
    BodySource body(src, h);
    TEST_ASSERT_EQUAL_STRING("hello", readAll(body).c_str());
    TEST_ASSERT_TRUE(body.complete());
}

static void test_head_chunked() {
    const char* raw =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: keep-alive\r\n\r\n"
        "4\r\n{\"a\"\r\nA;ext=1\r\n:[1,2,3,4]\r\n1\r\n}\r\n0\r\n\r\nNEXT";
    hal::StringSource src(raw, std::strlen(raw));
    ResponseHead h;
    TEST_ASSERT_TRUE(readResponseHead(src, h));
    TEST_ASSERT_TRUE(h.chunked);
    BodySource body(src, h);
    TEST_ASSERT_EQUAL_STRING("{\"a\":[1,2,3,4]}", readAll(body).c_str());
    TEST_ASSERT_TRUE(body.complete());
    // The trailer is consumed, so the next response starts cleanly.
    char next[5] = {};
    TEST_ASSERT_EQUAL(4, src.readBytes(next, 4));
    TEST_ASSERT_EQUAL_STRING("NEXT", next);
}

static void test_drain_after_partial_read() {
    const char* raw = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n3\r\ndef\r\n0\r\n\r\nX";
    hal::StringSource src(raw, std::strlen(raw));
    ResponseHead h;
    readResponseHead(src, h);
    BodySource body(src, h);
    TEST_ASSERT_EQUAL('a', body.read());
    TEST_ASSERT_TRUE(body.drain());
    TEST_ASSERT_EQUAL('X', src.read());
}

static void test_close_and_eof_body() {
    const char* raw = "HTTP/1.0 404 Not Found\r\nConnection: close\r\n\r\nnope";
    hal::StringSource src(raw, std::strlen(raw));
    ResponseHead h;
    TEST_ASSERT_TRUE(readResponseHead(src, h));
    TEST_ASSERT_EQUAL(404, h.status);
    TEST_ASSERT_TRUE(h.close);
    TEST_ASSERT_EQUAL(-1, h.contentLength);
    BodySource body(src, h);
    TEST_ASSERT_EQUAL_STRING("nope", readAll(body).c_str());
    TEST_ASSERT_TRUE(body.complete());
}

static void test_truncated_body_is_incomplete() {
    const char* raw = "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabc";
    hal::StringSource src(raw, std::strlen(raw));
    ResponseHead h;
    readResponseHead(src, h);
    BodySource body(src, h);
    TEST_ASSERT_EQUAL_STRING("abc", readAll(body).c_str());
    TEST_ASSERT_FALSE(body.complete());
    TEST_ASSERT_FALSE(body.drain());
}

static void test_bad_head() {
    const char* raw = "garbage\r\n\r\n";
    hal::StringSource src(raw, std::strlen(raw));
    ResponseHead h;
    TEST_ASSERT_FALSE(readResponseHead(src, h));
    hal::StringSource empty("", 0);
    TEST_ASSERT_FALSE(readResponseHead(empty, h));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_parse_url);
    RUN_TEST(test_format_request);
    RUN_TEST(test_head_content_length);
    RUN_TEST(test_head_chunked);
    RUN_TEST(test_drain_after_partial_read);
    RUN_TEST(test_close_and_eof_body);
    RUN_TEST(test_truncated_body_is_incomplete);
    RUN_TEST(test_bad_head);
    return UNITY_END();
}
