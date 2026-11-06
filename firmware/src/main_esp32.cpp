// Contrail firmware entry point for the CYD.
//
// Core 1 (Arduino loop): rendering at up to ~30 fps.
// Core 0 (network task): Wi-Fi status, geolocation, provider polling, route enrichment.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <esp_system.h>
#include <nvs.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <string>

#include "Esp32Hal.h"
#include "Esp32Net.h"
#include "Esp32Storage.h"
#include "alerts/AlertConfig.h"
#include "alerts/AlertRules.h"
#include "app/CoreApp.h"
#include "app/MessageLayer.h"
#include "app/PanelBridge.h"
#include "assets/LogoStore.h"
#include "map/MapPack.h"
#include "screens/ScreenApp.h"
#include "providers/JsonStream.h"
#include "gfx/StripRenderer.h"
#include "services/Log.h"
#include "util/TimeZone.h"
#include "web/WebUi.h"

#ifndef CONTRAIL_VERSION
#define CONTRAIL_VERSION "dev"
#endif

namespace {

constexpr const char* kVersion = CONTRAIL_VERSION;
constexpr const char* kApName = "Contrail-setup";
constexpr const char* kHostname = "contrail";
constexpr uint32_t kCleanUptimeMs = 60000;
constexpr int kCrashesForSafeMode = 3;
// Rows per render strip. Two strip buffers live in internal RAM for the whole run, and with
// both TLS connections open (~45 KB each) 8-bit RAM is the scarce resource: 32-row strips
// (30 KB) left the largest free block at ~6 KB, below the ~18 KB a new TLS handshake
// needs. 16-row strips free 15 KB for it; the radar draws no slower (twice the strips,
// but its damage is tracked per 16-row band, so it redraws fewer pixels).
constexpr int kStripH = 16;

RTC_NOINIT_ATTR uint32_t gCrashCount;

hal::NvsStore kv;
hal::Esp32Clock clockHal;
hal::Esp32Http http("Contrail/" CONTRAIL_VERSION " (+https://github.com/ChristopherJacob/contrail)");
hal::Esp32Backlight backlight;
hal::Esp32Led led;
hal::Esp32Light light;
hal::Esp32Display* display = nullptr;
hal::Esp32Storage sd;
assets::LogoStore logos;
basemap::MapPack mapPack;

app::CoreApp core(http, clockHal, kv);
// Static rather than allocated at boot, so its ~11 KB never takes a piece of the heap.
scr::ScreenApp screensStorage;
scr::ScreenApp* const screens = &screensStorage;
app::MessageScreen message;
gfx::StripRenderer* renderer = nullptr;
uint16_t* bufA = nullptr;
uint16_t* bufB = nullptr;
bool safeMode = false;
// The backlight level from the brightness setting or the light sensor; alerts pulse around it.
uint8_t gBaseBright = 200;
// Set by any settings change (network thread or portal); the render loop rebuilds the alert config.
std::atomic<bool> gAlertsDirty{true};
// The time zone comes from the tz setting when the user set one. Otherwise from the UTC offset
// in force at home's lat/lon (Open-Meteo), looked up at boot and every few hours so a
// daylight-saving change lands within hours; the IP lookup's offset stands in until then. The
// network thread fills gOffsetS; the render loop applies it.
std::atomic<bool> gTzSet{false};  // the user's tz setting is in use
std::atomic<bool> gOffsetReady{false};
std::atomic<int> gOffsetS{0};
bool gZoneFound = false;  // Open-Meteo answered: the IP offset no longer applies (network thread only)
// The web task and the touch panel write settings; these carry the changes to the threads that act
// on them. Display: theme, screen mode and the panel's values (render loop). Tz: the tz setting
// itself. ZoneNow: home or tz changed, so look up the automatic zone again soon (network thread).
std::atomic<bool> gDisplayDirty{true};
std::atomic<bool> gTzDirty{false};
std::atomic<bool> gZoneNow{false};
std::atomic<bool> gHaveOffset{false};  // an automatic offset has been found (IP or Open-Meteo)
// A firmware upload's progress (-1: none) and when it last moved; polls pause while one runs.
std::atomic<int> gUpdatePct{-1};
std::atomic<uint32_t> gUpdateSeenMs{0};
std::atomic<uint32_t> gRestartAtMs{0};  // 0: no restart pending
app::EditQueue gEdits;                  // touch panel → web task, which saves them
std::string gThemeName;                 // the theme drawn now

void serialSink(const char* line) { Serial.println(line); }

bool heapGuard() {
    // Leave headroom for parsing; the HTTP pool checks for a large block itself before a
    // new TLS connection.
    return ESP.getFreeHeap() > 30000;
}

hal::DisplayOptions displayOptionsFromSettings(svc::Settings& s) {
    hal::DisplayOptions o;
    std::string drv = s.getEnum(svc::key::kDispDriver);
    o.panel = drv == "st7789"    ? board::Panel::ST7789
              : drv == "ili9341" ? board::Panel::ILI9341
                                 : board::kDefaultPanel;
    std::string inv = s.getEnum(svc::key::kDispInvert);
    o.invert = inv == "auto" ? board::kDefaultInvert : inv == "on";
    o.bgr = s.getBool(svc::key::kDispBgr);
    o.rotation = s.getEnum(svc::key::kDispRotate) == "180" ? 2 : 0;
    return o;
}

// Render the message screen once (used while blocking calls like the Wi-Fi portal run).
void showMessage(const char* title, const char* l1, const char* l2 = "", const char* l3 = "") {
    message.msg.set(title, l1, l2, l3);
    renderer->setLayers(message.layers, 2);
    renderer->frame(0.016f);
}

void updateCrashCounter() {
    esp_reset_reason_t r = esp_reset_reason();
    bool crash = r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT ||
                 r == ESP_RST_BROWNOUT;
    if (r == ESP_RST_POWERON || gCrashCount > 100) gCrashCount = 0;
    gCrashCount = crash ? gCrashCount + 1 : 0;
    safeMode = gCrashCount >= kCrashesForSafeMode;
}

void connectWifi(bool forcePortal) {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(kHostname);
    WiFiManager wm;
    char lat[16], lon[16];
    std::snprintf(lat, sizeof lat, "%.4f", core.settings().getFloat(svc::key::kLat));
    std::snprintf(lon, sizeof lon, "%.4f", core.settings().getFloat(svc::key::kLon));
    WiFiManagerParameter pLat("lat", "Latitude (optional, else located by IP)", lat, 15);
    WiFiManagerParameter pLon("lon", "Longitude", lon, 15);
    wm.addParameter(&pLat);
    wm.addParameter(&pLon);
    // Everything else is on the web page at http://contrail.local (config spec §3).
    wm.setTitle("Contrail");
    wm.setAPCallback([](WiFiManager*) {
        showMessage("Wi-Fi setup", "Join the Wi-Fi network", kApName, "then follow the page that opens");
    });
    wm.setSaveParamsCallback([&]() {
        double la = std::atof(pLat.getValue()), lo = std::atof(pLon.getValue());
        if (la != 0 || lo != 0) {
            core.settings().set(svc::key::kLat, pLat.getValue());
            core.settings().set(svc::key::kLon, pLon.getValue());
            core.settings().set(svc::key::kLocSet, "true");
        }
    });
    wm.setBreakAfterConfig(true);
    if (forcePortal) wm.startConfigPortal(kApName);
    else if (!wm.autoConnect(kApName)) ESP.restart();
    WiFi.setAutoReconnect(true);
    // No modem sleep. Asleep, the station wakes for each DTIM beacon and the access point
    // flushes the broadcast and multicast it held back as one burst; the driver mallocs a
    // receive buffer (up to 2.3 KB) per frame, and those bursts landing on top of a poll's
    // own TCP receive window were half the heap dip during a poll and split the largest block
    // down to 1.8 KB. Measured over 10 minutes: worst per-window low 40.4 -> 46.3 KB, worst
    // largest block 5.6 -> 12.3 KB. Costs radio power, which a mains-powered display can spare.
    WiFi.setSleep(false);
    MDNS.begin(kHostname);
}

// From the public IP: home location when the user did not enter one, and the UTC offset when
// no time zone is set.
bool geolocate() {
    bool ok = false;
    http.get("http://ip-api.com/json/?fields=status,lat,lon,timezone,offset", nullptr, 0, [&](hal::IByteSource& src) {
        prov::SourceReader r{src};
        JsonDocument doc;
        if (deserializeJson(doc, r) || doc["status"] != "success") return;
        if (!core.settings().getBool(svc::key::kLocSet)) {
            char buf[16];
            std::snprintf(buf, sizeof buf, "%.4f", doc["lat"].as<double>());
            core.settings().set(svc::key::kLat, buf);
            std::snprintf(buf, sizeof buf, "%.4f", doc["lon"].as<double>());
            core.settings().set(svc::key::kLon, buf);
            core.settings().set(svc::key::kLocSet, "true");
            svc::logf("[geo] located %s by IP", buf);
        }
        if (!gTzSet && !gZoneFound && doc["offset"].is<int>()) {
            gOffsetS = doc["offset"].as<int>(), gOffsetReady = true, gHaveOffset = true;
            svc::logf("[geo] IP zone %s, UTC offset %d s", doc["timezone"].as<const char*>(), gOffsetS.load());
        }
        ok = true;
    });
    return ok;
}

// The UTC offset in force at home's lat/lon, from Open-Meteo (plain HTTP, ~200 bytes, no key).
bool lookupZone() {
    char url[160];
    std::snprintf(url, sizeof url,
                  "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&timezone=auto&forecast_days=1",
                  core.settings().getFloat(svc::key::kLat), core.settings().getFloat(svc::key::kLon));
    bool ok = false;
    http.get(url, nullptr, 0, [&](hal::IByteSource& src) {
        prov::SourceReader r{src};
        JsonDocument doc;
        if (deserializeJson(doc, r) || !doc["utc_offset_seconds"].is<int>()) return;
        const int off = doc["utc_offset_seconds"].as<int>();
        gOffsetS = off, gOffsetReady = true, gZoneFound = true, gHaveOffset = true;  // the render loop skips an unchanged zone
        svc::logf("[tz] %s at home, UTC offset %d s", doc["timezone"].as<const char*>(), off);
        ok = true;
    });
    return ok;
}

// Connectivity watchdog. Heap starvation (seen under a stream of web requests) can kill the link
// while WiFi.status() still says WL_CONNECTED: no ping, DNS fails, every poll fails, until a reset.
// Polls run every few seconds once online and located, so 3 minutes without any HTTP reply is
// abnormal: reconnect then, and restart if 3 more minutes pass without one. A reply of any status
// counts, so a provider's 5xx keeps it quiet. If every host is unreachable on the internet side
// while the LAN is fine, this reconnects (harmless) and then restarts about every 6 minutes until
// the internet returns; tolerable for a display that has nothing to show meanwhile. It only
// watches while Wi-Fi says connected and home is set (polls run), and never during an upload.
void watchLink(bool up, bool locSet) {
    constexpr uint32_t kDeadMs = 3 * 60 * 1000u;
    static uint32_t lastReplies = 0, quietSince = 0;
    static bool watching = false, reconnected = false;
    const uint32_t now = millis(), replies = http.replies();
    if (replies != lastReplies) lastReplies = replies, quietSince = now, reconnected = false;
    if (!up || !locSet || gUpdatePct >= 0) {
        watching = false;  // the clock restarts when polling resumes
        return;
    }
    if (!watching) watching = true, quietSince = now;
    if (now - quietSince < kDeadMs) return;
    if (!reconnected) {
        svc::logf("[wifi] link dead, reconnecting");
        // In arduino-esp32 2.0.17 reconnect() is esp_wifi_disconnect() then esp_wifi_connect().
        WiFi.reconnect();
        reconnected = true, quietSince = now;  // kept across the drop the reconnect causes
        return;
    }
    svc::logf("[wifi] link still dead after reconnect, restarting");
    delay(200);  // let the log line out
    ESP.restart();
}

void networkTask(void*) {
    constexpr uint32_t kZoneEveryMs = 3 * 3600 * 1000u, kZoneRetryMs = 60 * 1000u;
    uint32_t lastGeoTry = 0, lastZoneTry = 0, zoneWait = 0;
    bool ipTried = false;
    for (;;) {
        if (gUpdatePct >= 0) {  // an upload is writing flash: no polls, and their TLS heap is free
            watchLink(false, false);
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        if (gZoneNow.exchange(false)) zoneWait = 0, lastZoneTry = millis();
        bool up = WiFi.status() == WL_CONNECTED;
        if (up != core.online()) {
            core.setOnline(up);
            core.events().publish(svc::Ev::NetworkChanged, up);
            svc::logf("[wifi] %s", up ? "connected" : "disconnected");
        }
        const bool locSet = core.settings().getBool(svc::key::kLocSet);
        // The IP lookup finds home when the user did not enter it, and gives a stand-in offset.
        if (up && (!locSet || (!gTzSet && !gZoneFound && !ipTried)) && millis() - lastGeoTry > 30000) {
            lastGeoTry = millis();
            ipTried = geolocate();
        }
        if (up && locSet && !gTzSet && millis() - lastZoneTry >= zoneWait) {
            lastZoneTry = millis();
            zoneWait = lookupZone() ? kZoneEveryMs : kZoneRetryMs;
        }
        core.networkTick();
        watchLink(up, locSet);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void updateBrightness() {
    static float smoothed = -1;
    static uint32_t last = 0;
    if (const int p = screens->previewBrightness(); p >= 0) {  // the panel's slider is held
        gBaseBright = uint8_t(p);
        return;
    }
    if (millis() - last < 250) return;
    last = millis();
    if (core.settings().getEnum(svc::key::kBrightMode) == "manual") {
        gBaseBright = uint8_t(core.settings().getInt(svc::key::kBrightness));
        return;
    }
    float target = 30 + light.read() * (225.f / 1023.f);
    smoothed = smoothed < 0 ? target : smoothed * 0.9f + target * 0.1f;
    gBaseBright = uint8_t(smoothed);
}

// The alert's LED colour and backlight pulse (alerts spec §6); written only when they change.
void updateLights() {
    static int lastBl = -1;
    static gfx::Rgb lastLed{0, 0, 0};
    const alerts::LightOut lo = screens->alertLights(millis());
    const int bl = int(gBaseBright * lo.backlight + 0.5f);
    if (bl != lastBl) backlight.set(uint8_t(bl)), lastBl = bl;
    if (lo.led.r != lastLed.r || lo.led.g != lastLed.g || lo.led.b != lastLed.b)
        led.set(lo.led.r, lo.led.g, lo.led.b), lastLed = lo.led;
}

// "alert emerg|watch|mil|low" on the serial port shows a made-up alert (alerts spec §8).
void pollSerial() {
    static char line[24];
    static size_t n = 0;
    while (Serial.available()) {
        const char ch = char(Serial.read());
        if (ch != '\n' && ch != '\r') {
            if (n < sizeof line - 1) line[n++] = ch;
            continue;
        }
        line[n] = 0, n = 0;
        scr::AlertKind k;
        if (alerts::parseTestCommand(line, k)) {
            svc::logf("[alert] test: %s", line);
            screens->injectAlert(k, millis());
        }
    }
}

// Frame statistics between health logs. Only frames that pushed strips count: a loop pass
// with nothing damaged costs nothing and would report ~900 "fps" on an idle screen.
uint32_t gFrames = 0, gMaxRenderUs = 0, gSlowFrames = 0, gSlowSwitch = 0, gMaxSteadyUs = 0;
int gMaxSteadyScreen = -1;
uint64_t gRenderUsTotal = 0;

// NVS entries in use: the logbook adds about 2 KB of strings to the 20 KB partition.
void logNvs() {
    nvs_stats_t st;
    if (nvs_get_stats(nullptr, &st) == ESP_OK)
        svc::logf("[nvs] used=%u free=%u of %u entries", unsigned(st.used_entries), unsigned(st.free_entries),
                  unsigned(st.total_entries));
}

// [health] fields: minheap = the lowest free heap since boot (ESP-IDF's own count, which sees
// dips too short for the [heap] line's sampling); drawn = frames that drew
// something per second over the last 30 s; busy = share of that time spent rendering them
// (CPU + display waits); render = the last drawn frame; stack = the loop task's stack
// high-water mark (bytes never used; ESP-IDF counts stack in bytes); webstack = the same for
// the web task, to size its 8 KB stack.
void logHealth() {
    static uint32_t last = 0;
    uint32_t now = millis();
    if (now - last < 30000) return;
    float spanS = (now - last) / 1000.f;
    last = now;
    svc::logf("[health] up=%lus heap=%u minheap=%u largest=%u stack=%u webstack=%u drawn=%.1f/s busy=%.1f%% render=%uus "
              "provider=%s aircraft=%d ldr=%d",
              now / 1000, ESP.getFreeHeap(), ESP.getMinFreeHeap(),
              heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
              unsigned(uxTaskGetStackHighWaterMark(nullptr)), web::stackFree(), gFrames / spanS,
              gRenderUsTotal / (spanS * 1e4f), renderer->lastRenderUs(), core.chain().activeName(),
              core.store().size(), light.raw());
    svc::logf("[render] strips=%d draw=%uus wait=%uus; last 30 s: %u frames, max %uus, %u over 33 ms "
              "(%u in screen switches); steady max %uus on screen %d",
              renderer->lastStrips(), renderer->layerUs(0), renderer->waitUs(), gFrames, gMaxRenderUs, gSlowFrames,
              gSlowSwitch, gMaxSteadyUs, gMaxSteadyScreen);
    // The window's lowest free heap, when under 56 KB: what was running then pins the dip.
    hal::HeapLow low = hal::takeHeapLow();
    if (low.free < 56 * 1024)
        svc::logf("[heap] low=%u largest=%u during %s (seen by %s thread), screen=%d", low.free, low.largest,
                  low.during[0] ? low.during : "no request or save", low.byNet ? "network" : "render",
                  int(screens->host().current()));
    static int healthLines = 0;
    if (++healthLines % 20 == 0) logNvs();  // every 10 minutes, after the logbook's saves
    gFrames = gMaxRenderUs = gSlowFrames = gSlowSwitch = gMaxSteadyUs = 0;
    gMaxSteadyScreen = -1;
    gRenderUsTotal = 0;
}

uint32_t microsSource() { return micros(); }

std::string gTz = "UTC0";  // one source for the zone, used at boot and when NTP is configured

// The tz setting changed (web page): a rule applies at once; blank goes back to the automatic
// zone — the last offset found, and a fresh lookup soon.
void applyTzSetting() {
    const std::string tz = core.settings().getString(svc::key::kTz);
    gTzSet = !tz.empty();
    if (gTzSet) {
        gTz = tz;
        setenv("TZ", gTz.c_str(), 1);
        tzset();
        svc::logf("[tz] set to %s", gTz.c_str());
    } else {
        if (gHaveOffset) gOffsetReady = true;
        gZoneNow = true;
    }
}

// Theme, screen mode and the touch panel's values, after any settings change.
void applyDisplaySettings() {
    svc::Settings& s = core.settings();
#ifndef CONTRAIL_FORCE_THEME
    const std::string theme = s.getEnum(svc::key::kTheme);
    if (theme != gThemeName) {
        gThemeName = theme;
        screens->useTheme(scr::themeByName(theme.c_str()));
        svc::logf("[display] theme %s", theme.c_str());
    }
#endif
#ifndef CONTRAIL_SOAK_CAROUSEL
    screens->configure(scr::parseDirectorMode(s.getEnum(svc::key::kScreenMode).c_str()),
                       uint32_t(s.getInt(svc::key::kCarouselS)) * 1000u);
#endif
    screens->setPanelValues(app::panelValues(s));
}

}  // namespace

void setup() {
    Serial.begin(115200);
    svc::logSink() = serialSink;
    updateCrashCounter();
    svc::logf("[boot] Contrail %s, reset reason %d, crash count %u", kVersion, int(esp_reset_reason()), gCrashCount);

    kv.begin();
    core.begin();
    core.settings().onChange([](const char* k) {  // any thread: flags only
        gAlertsDirty = true;
        gDisplayDirty = true;
        if (!std::strcmp(k, svc::key::kTz)) gTzDirty = true;
        if (!std::strcmp(k, svc::key::kLat) || !std::strcmp(k, svc::key::kLon)) gZoneNow = true;
    });
    gTz = core.settings().getString(svc::key::kTz);
    gTzSet = !gTz.empty();
    if (gTz.empty()) gTz = "UTC0";
    setenv("TZ", gTz.c_str(), 1);
    tzset();

    bufA = static_cast<uint16_t*>(heap_caps_malloc(240 * kStripH * 2, MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
    bufB = static_cast<uint16_t*>(heap_caps_malloc(240 * kStripH * 2, MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
    display = new hal::Esp32Display(displayOptionsFromSettings(core.settings()));
    display->begin();
    backlight.begin();
    led.begin();
    light.begin();
    led.set(0, 0, 0);
    renderer = new gfx::StripRenderer(*display, bufA, bufB, kStripH);
    renderer->setMicrosSource(microsSource);

    if (safeMode) {
        showMessage("Safe mode", "Contrail restarted 3 times.", "Data sources are paused.", "Press reset to try again");
        return;
    }

    // Holding the screen during boot reopens the Wi-Fi setup portal.
    int tx, ty;
    bool forcePortal = display->read(tx, ty);
    showMessage("Contrail", forcePortal ? "Opening Wi-Fi setup" : "Connecting to Wi-Fi…");
    // The SD card: logos (5a) and the map pack (5b). No card is not an error; the screens draw tail fins instead. Here, after safe
    // mode (a card that crashes the mount cannot stop safe mode from showing) and well before web::start (the
    // web task reads the status, which begin() writes once).
    const uint32_t sdStart = millis();
    if (sd.begin()) {
        char what[32];
        sd.describe(what, sizeof what);
        svc::logf("[sd] mounted in %u ms (%s)", unsigned(millis() - sdStart), what);
    } else {
        svc::logf("[sd] not mounted (%u ms): %s", unsigned(millis() - sdStart), sd.error());
    }
    logos.begin(&sd);  // logs the pack's logo count or why it was refused
    mapPack.begin(&sd);
    if (mapPack.isOpen())
        svc::logf("[sd] map.pak: %u nm around %.4f, %.4f", unsigned(mapPack.coverNm()), mapPack.homeLatE5() * 1e-5,
                  mapPack.homeLonE5() * 1e-5);
    else if (mapPack.error()[0])
        svc::logf("[sd] map.pak not used: %s", mapPack.error());
    connectWifi(forcePortal);
    // configTzTime keeps the zone; plain configTime would reset TZ to UTC.
    configTzTime(gTz.c_str(), "pool.ntp.org", "time.google.com");
    core.setFetchGuard(heapGuard);
    core.setOnline(WiFi.status() == WL_CONNECTED);
    if (!core.settings().getBool(svc::key::kLocSet)) showMessage("Contrail", "Finding your location…");

    // Idle priority: TLS handshakes are seconds of CPU work, and sharing time slices with
    // IDLE0 keeps the task watchdog fed while they run.
    xTaskCreatePinnedToCore(networkTask, "net", 12288, nullptr, tskIDLE_PRIORITY, nullptr, 0);
    // Which screen shows when nobody touches the device (screen_mode setting).
    screens->configure(scr::parseDirectorMode(core.settings().getEnum(svc::key::kScreenMode).c_str()),
                       uint32_t(core.settings().getInt(svc::key::kCarouselS)) * 1000u);
#ifdef CONTRAIL_SOAK_CAROUSEL
    screens->configure(scr::DirectorMode::Carousel, CONTRAIL_SOAK_CAROUSEL * 1000u);
#endif
#ifdef CONTRAIL_FORCE_THEME
    screens->useTheme(scr::themeByName(CONTRAIL_FORCE_THEME));  // cyd2usb_editorial
#else
    gThemeName = core.settings().getEnum(svc::key::kTheme);
    screens->useTheme(scr::themeByName(gThemeName.c_str()));
#endif
    screens->setLogos(&logos);
    screens->setMap(&mapPack);
    // The chart job's time budget and its [map] log line, with the card's own times.
    screens->setMapClock([] { return uint32_t(micros()); });
    assets::FatVolume::setClock([] { return uint32_t(micros()); });
    // Every showMessage() call happens above, during boot (the Wi-Fi portal callback only
    // fires inside connectWifi); from here on the screens own the renderer.
    renderer->setLayers(screens->layers(), screens->layerCount());
    web::Hooks hooks;
    hooks.info = [](JsonDocument& d) {
        d["version"] = kVersion;
        d["ip"] = WiFi.localIP().toString();
        d["uptimeS"] = millis() / 1000;
        d["heap"] = ESP.getFreeHeap();
        if (gHaveOffset && !gTzSet) d["autoOffsetS"] = gOffsetS.load();
        JsonObject s = d["sd"].to<JsonObject>();
        s["mounted"] = logos.mounted();
        if (logos.logoCount() >= 0) s["logos"] = logos.logoCount();
        else s["logos"] = nullptr;
        // A card that answered but did not mount (exFAT, no FAT partition, …) says why; no card at all is null.
        const char* err = logos.error();
        if (!logos.mounted() && std::strcmp(sd.error(), "no card") != 0) err = sd.error();
        if (err[0]) s["error"] = err;
        else s["error"] = nullptr;
        // The pack's header fields were set before this task started; isOpen() and the status are atomics. Whether
        // home and the radius lie inside it comes from the settings, so the line is right before the Map is shown.
        const bool open = mapPack.isOpen();
        svc::Settings& st = core.settings();
        const bool located = st.getBool(svc::key::kLocSet);
        const bool outside = open && located &&
                             !mapPack.covers(st.getFloat(svc::key::kLat), st.getFloat(svc::key::kLon),
                                             float(st.getInt(svc::key::kRadius)));
        JsonObject m = s["map"].to<JsonObject>();
        m["state"] = open && located && !outside ? "detailed" : "builtin";
        const char* why = open && !located ? "no location set"
                          : outside        ? "home is outside the map pack; rebuild it"
                                           : mapPack.error();
        m["why"] = why[0] ? why : nullptr;
        m["damaged"] = mapPack.damagedTiles();
        if (open) {
            m["cover"] = mapPack.coverNm();
            m["lat"] = mapPack.homeLatE5() * 1e-5, m["lon"] = mapPack.homeLonE5() * 1e-5;
        }
    };
    // The time first: the render loop's stall check reads it after seeing the percentage.
    hooks.updateProgress = [](int p) { gUpdateSeenMs = millis(), gUpdatePct = p; };
    hooks.restart = [] { gRestartAtMs = (millis() + 1000) | 1; };  // | 1: never 0
    hooks.idle = [] {
        scr::SettingEdit e;
        while (gEdits.pop(e))
            if (!app::applyEdit(core.settings(), e)) svc::logf("[panel] refused %s=%s", e.key, e.value);
    };
    web::start(core.settings(), hooks);
    logNvs();
    svc::logf("[boot] ready, heap=%u", ESP.getFreeHeap());
}

void loop() {
    static uint32_t last = micros();
    if (safeMode) {
        delay(1000);
        return;
    }
    uint32_t now = micros();
    float dt = (now - last) / 1e6f;
    last = now;

    if (core.renderTick(screens->scene())) screens->sceneUpdated();
    if (gTzDirty.exchange(false)) applyTzSetting();
    if (gOffsetReady.exchange(false) && !gTzSet) {
        char tz[16];
        util::posixFromOffset(gOffsetS, tz, sizeof tz);
        if (gTz != tz) {
            gTz = tz;
            setenv("TZ", tz, 1);
            tzset();
            svc::logf("[tz] now %s", tz);
        }
    }
    if (gAlertsDirty.exchange(false)) screens->configureAlerts(alerts::configFromSettings(core.settings()));
    if (gDisplayDirty.exchange(false)) applyDisplaySettings();
    // An upload that stops moving for 30 s has died with its connection: back to the screens. The
    // time is read before millis() and compared signed, as the web task may move it in between.
    const uint32_t upSeen = gUpdateSeenMs;
    if (gUpdatePct >= 0 && int32_t(millis() - upSeen) > 30000) {
        // WebServer's upload read has no timeout, so a silent client would hold the web task forever.
        // Update.end() never ran, so the old firmware still boots.
        gUpdatePct = -1, gRestartAtMs = (millis() + 1000) | 1;
        svc::logf("[ota] upload stalled, restarting");
    }
    screens->setUpdateProgress(gUpdatePct);
    pollSerial();
    int tx = 0, ty = 0;
    bool touched = display->read(tx, ty);
    screens->touch(touched, tx, ty, millis());
    screens->tick(millis());
    for (scr::SettingEdit e; screens->takeSettingEdit(e);)
        if (!gEdits.push(e)) svc::logf("[panel] edit queue full, dropped %s", e.key);
#if defined(CONTRAIL_SOAK_CAROUSEL) && defined(CONTRAIL_SOAK_THEMES)
    // Soak: both themes, switching after each full carousel cycle (six screens x 20 s; not Alert).
    static uint32_t themeFlip = millis();
    static bool editorialOn = false;
    if (millis() - themeFlip >= CONTRAIL_SOAK_CAROUSEL * 1000u * 6u) {
        themeFlip = millis(), editorialOn = !editorialOn;
        screens->useTheme(editorialOn ? scr::editorial() : scr::cinematicSky());
        svc::logf("[soak] theme %s", editorialOn ? "editorial" : "cinematic");
    }
#endif
    // A screen switch redraws the whole screen on every frame of the veil dip: count its slow
    // frames apart from the steady ones, and note the steady worst's screen.
    const bool switching = screens->host().transitioning();
    if (renderer->frame(dt)) {
        uint32_t us = renderer->lastRenderUs();
        ++gFrames, gSlowFrames += us > 33333, gMaxRenderUs = std::max(gMaxRenderUs, us);
        gRenderUsTotal += us;
        if (switching) gSlowSwitch += us > 33333;
        else if (us > gMaxSteadyUs) gMaxSteadyUs = us, gMaxSteadyScreen = int(screens->host().current());
    }
    updateBrightness();
    updateLights();
    hal::sampleHeap(false);
    logHealth();
    if (millis() > kCleanUptimeMs && gCrashCount) gCrashCount = 0;
    if (gRestartAtMs && int32_t(millis() - gRestartAtMs) >= 0) ESP.restart();
    // Yield a little so the idle task can feed the watchdog on this core.
    delay(1);
}
