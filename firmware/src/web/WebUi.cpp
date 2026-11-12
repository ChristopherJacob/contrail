#include "WebUi.h"

#include <Arduino.h>
#include <algorithm>
#include <ESPmDNS.h>
#include <Update.h>
#include <WebServer.h>

#include "WebPage.h"
#include "services/Log.h"
#include "services/SettingsApi.h"

namespace web {

namespace {

WebServer* server = nullptr;
svc::Settings* settings = nullptr;
Hooks hooks;
const char* const kBuild = __DATE__ " " __TIME__;

// The upload in progress (one at a time; WebServer handles one request at a time).
size_t upSize = 0, upDone = 0;
int upPct = -1;
String upError;

// A DNS-rebinding page reaches us under its own hostname; only our names and a bare IP are served.
bool hostOk(String h) {
    h.toLowerCase();
    const int colon = h.indexOf(':');
    if (colon >= 0) {
        if (colon == int(h.length()) - 1) return false;
        for (unsigned i = colon + 1; i < h.length(); ++i)
            if (!isdigit(h[i])) return false;
        h.remove(colon);
    }
    if (h == "contrail.local" || h == "contrail") return true;
    int dots = 0;  // IPv4 literal: four dot-separated groups of 1-3 digits
    size_t run = 0;
    for (unsigned i = 0; i < h.length(); ++i) {
        if (h[i] == '.') {
            if (!run) return false;
            ++dots, run = 0;
        } else if (!isdigit(h[i]) || ++run > 3) {
            return false;
        }
    }
    return dots == 3 && run;
}

bool guarded() { return server->header("X-Contrail") == "1" && hostOk(server->hostHeader()); }

// Collects writes into small packets for the socket.
class Buffered : public Print {
   public:
    explicit Buffered(WiFiClient& c) : c_(c) {}
    ~Buffered() { flush(); }
    size_t write(uint8_t b) override {
        buf_[n_++] = b;
        if (n_ == sizeof buf_) flush();
        return 1;
    }
    void flush() override {
        if (n_) c_.write(buf_, n_), n_ = 0;
    }

   private:
    WiFiClient& c_;
    uint8_t buf_[512];
    size_t n_ = 0;
};

// Streamed rather than built as one String: the schema is ~5 KB, and with the TLS connections open
// the largest free block is often smaller than that, so a String reply fails to allocate.
void sendJson(int code, const JsonDocument& doc) {
    if (doc.overflowed()) {  // the heap ran out building it (spec §7): say so rather than send half
        server->send(503, "application/json", "{\"ok\":false,\"error\":\"out of memory\"}");
        svc::logf("[web] reply dropped: out of memory");
        return;
    }
    server->setContentLength(measureJson(doc));
    server->send(code, "application/json", "");
    WiFiClient client = server->client();
    Buffered out(client);
    serializeJson(doc, out);
}

void sendError(int code, const char* why) {
    JsonDocument d;
    d["ok"] = false;
    d["error"] = why;
    sendJson(code, d);
}

void handlePage() {
    const String etag = String("\"") + kBuild + "\"";
    if (server->header("If-None-Match") == etag) return server->send(304);
    server->sendHeader("ETag", etag);
    server->sendHeader("Cache-Control", "no-cache");
    server->sendHeader("Content-Encoding", "gzip");
    server->send_P(200, "text/html; charset=utf-8", reinterpret_cast<const char*>(kWebPage), kWebPageLen);
}

void handleInfo() {
    JsonDocument d;
    if (hooks.info) hooks.info(d);
    d["built"] = kBuild;
    sendJson(200, d);
}

void handleSchema() {
    JsonDocument d;
    svc::schemaJson(*settings, d);
    sendJson(200, d);
}

void handleGetSettings() {
    JsonDocument d;
    svc::valuesJson(*settings, d);
    sendJson(200, d);
}

void handlePostSettings() {
    if (!guarded() || !server->header("Content-Type").startsWith("application/json"))
        return sendError(403, "missing X-Contrail header or JSON content type");
    JsonDocument in;
    if (deserializeJson(in, server->arg("plain"))) return sendError(400, "bad JSON");
    JsonDocument errors;
    const svc::ApplyResult r = svc::applyJson(*settings, in.as<JsonVariantConst>(), errors);
    JsonDocument out;
    out["ok"] = r.ok;
    if (!r.ok) {
        out["errors"] = errors;
        return sendJson(400, out);
    }
    out["restart"] = r.restart;
    sendJson(200, out);
    svc::logf("[web] saved %u settings%s", unsigned(in.as<JsonObjectConst>().size()), r.restart ? ", restarting" : "");
    if (r.restart && hooks.restart) hooks.restart();
}

void endUpload(const char* why) {
    if (why) upError = why;
    Update.abort();
    upPct = -1;
    if (hooks.updateProgress) hooks.updateProgress(-1);
}

// The alert chime at the volume the page sends ({"volume": 0..100}), so it can be tried before saving.
void handleChime() {
    if (!guarded() || !server->header("Content-Type").startsWith("application/json"))
        return sendError(403, "missing X-Contrail header or JSON content type");
    JsonDocument in;
    if (deserializeJson(in, server->arg("plain")) || !in["volume"].is<int>()) return sendError(400, "bad JSON");
    if (hooks.chime) hooks.chime(std::max(0, std::min(100, in["volume"].as<int>())));
    JsonDocument out;
    out["ok"] = true;
    sendJson(200, out);
}

void handleUploadChunk() {
    HTTPUpload& up = server->upload();
    switch (up.status) {
        case UPLOAD_FILE_START:
            upError = "", upDone = 0, upPct = 0;
            if (!guarded()) return endUpload("missing X-Contrail header");
            upSize = size_t(server->header("X-Size").toInt());
            if (hooks.updateProgress) hooks.updateProgress(0);
            svc::logf("[ota] upload %s, %u bytes", up.filename.c_str(), unsigned(upSize));
            if (!Update.begin(upSize ? upSize : UPDATE_SIZE_UNKNOWN, U_FLASH)) endUpload(Update.errorString());
            break;
        case UPLOAD_FILE_WRITE: {
            // WebServer reads the whole upload inside one handleClient(): without a pause per chunk
            // this task (above idle priority) starves IDLE0 and the task watchdog resets the board.
            vTaskDelay(1);
            if (upPct < 0) return;  // already failed: drain the rest
            // An app image starts with 0xE9 and names its chip at byte 12 (esp_image_header_t.chip_id): refuse one
            // built for the other board family (the CYD's ESP32 or the Waveshare's ESP32-S3) before writing it.
            if (upDone == 0 && up.currentSize >= 16 &&
                (up.buf[0] != 0xE9 || (up.buf[12] | (up.buf[13] << 8)) != CONFIG_IDF_FIRMWARE_CHIP_ID))
                return endUpload(up.buf[0] != 0xE9 ? "not a firmware image" : "this firmware is for a different board");
            if (Update.write(up.buf, up.currentSize) != up.currentSize) return endUpload(Update.errorString());
            upDone += up.currentSize;
            int p = upSize ? int(uint64_t(upDone) * 100 / upSize) : 0;
            if (p > 99) p = 99;
            if (p != upPct && hooks.updateProgress) hooks.updateProgress(p);
            upPct = p;
            break;
        }
        case UPLOAD_FILE_END:
            if (upPct < 0) return;
            if (upSize && upDone != upSize) return endUpload("file incomplete");
            if (!Update.end(true)) return endUpload(Update.errorString());
            upPct = 100;
            break;
        case UPLOAD_FILE_ABORTED:
            if (upPct == 100) {  // the client dropped after END: the new image is already set to boot
                svc::logf("[ota] abort after end, treating as success");
                if (hooks.updateProgress) hooks.updateProgress(100);
                if (hooks.restart) hooks.restart();
                break;
            }
            endUpload("upload interrupted");
            break;
    }
}

void handleUploadDone() {
    if (upPct != 100) {
        svc::logf("[ota] failed: %s", upError.c_str());
        return sendError(400, upError.isEmpty() ? "no firmware file" : upError.c_str());
    }
    if (hooks.updateProgress) hooks.updateProgress(100);
    JsonDocument d;
    d["ok"] = true;
    sendJson(200, d);
    svc::logf("[ota] installed %u bytes, restarting", unsigned(upDone));
    if (hooks.restart) hooks.restart();
}

TaskHandle_t taskHandle = nullptr;  // for the [health] line's stack high-water mark

void task(void*) {
    for (;;) {
        server->handleClient();
        if (hooks.idle) hooks.idle();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

}  // namespace

void start(svc::Settings& s, const Hooks& h) {
    settings = &s, hooks = h;
    server = new WebServer(80);
    const char* headers[] = {"X-Contrail", "X-Size", "Content-Type", "If-None-Match"};
    server->collectHeaders(headers, 4);
    server->on("/", HTTP_GET, handlePage);
    server->on("/api/info", HTTP_GET, handleInfo);
    server->on("/api/schema", HTTP_GET, handleSchema);
    server->on("/api/settings", HTTP_GET, handleGetSettings);
    server->on("/api/settings", HTTP_POST, handlePostSettings);
    server->on("/api/chime", HTTP_POST, handleChime);
    server->on("/api/update", HTTP_POST, handleUploadDone, handleUploadChunk);
    server->onNotFound([] { server->send(404, "text/plain", "not found"); });
    server->begin();
    MDNS.addService("http", "tcp", 80);
    // Above the network task's idle priority, so a slow TLS poll never makes the page wait; the
    // 5 ms delay each pass leaves it the CPU.
    // 5 KB: measured high-water use ~2.5 KB serving 900 back-to-back requests and an upload; every
    // byte here is heap the TLS handshakes need.
    xTaskCreatePinnedToCore(task, "web", 5120, nullptr, 1, &taskHandle, 0);
    svc::logf("[web] serving http://contrail.local");
}

unsigned stackFree() { return taskHandle ? unsigned(uxTaskGetStackHighWaterMark(taskHandle)) : 0; }

}  // namespace web
