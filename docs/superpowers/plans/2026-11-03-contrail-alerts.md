# Contrail Alerts Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Raise a full-screen alert with a pulsing border, the RGB LED and a backlight pulse when an emergency, watchlisted, military or low-passing plane is in range, on every screen and in every director mode.

**Architecture:** Pure pieces in `lib/core/alerts/` (config parsing, rules, lights) feed an `AlertEngine` that `ScreenApp` owns and runs in `tick()` after each scene rebuild; it is the only writer of `Scene::alert`. A seventh screen, `ScreenId::Alert`, draws `Scene::alert` in each theme's mockup-8A design, and the `Director` puts it up ahead of everything while `alert.active`. The platform turns `ScreenApp::alertLights()` into LED and backlight writes each frame.

**Tech Stack:** C++17, PlatformIO (`cyd2usb`, `sim`, `test` envs), Unity tests with golden PPM images, WiFiManager custom parameters.

**Spec:** `docs/superpowers/specs/2026-11-03-contrail-alerts-design.md`.

---

## Decisions made while planning (choices the spec leaves open, and deviations)

1. **The alert settings are rows appended to `kCoreSettings`**, not a separate `kAlertSettings` table (spec §5): `svc::Settings` takes one table, and the core table's comment already expects later sub-projects to add to it. Group `"alerts"`.
2. **`ScreenApp` owns the engine** (spec §1 leaves it open). The engine runs in `ScreenApp::tick()` whenever `scene.version` changed, before the director decides; a tap calls `dismiss()` directly. Everything stays on the render thread. `ScreenApp`'s engine starts with every kind **off** until `configureAlerts()` is called, so existing tests that tick a `ScreenApp` over demo scenes (which contain a military plane) see no alerts.
3. **`AlertKind` and `SceneAlert` live in `screens/Scene.h`** (namespace `scr`), so the screens read alerts without depending on the engine.
4. **A lower kind never re-alerts** a plane that already alerted for a higher one (spec: "can alert again for a higher kind"; a watchlisted plane that later flies low stays quiet).
5. **Low pass needs `altFt > 0`**: the parser leaves an unknown altitude at 0, and the scene has no altitude-known flag.
6. **The watchlist keeps at most 24 entries of ≤ 10 characters** (264 B in `AlertConfig`). A lone `*` is skipped (it would match everything). Static RAM total is ≈ 900 B, not the spec's ≈ 600 B (§1).
7. **The Alert screen keeps its own clock** (seconds since `alert.startMs` changed, from frame `dt`); the LED and backlight use `millis() − startMs`. They can drift apart by the 0.2 s fade, which is invisible in a 1 s pulse.
8. **The serial `alert <kind>` command is always compiled in** (spec §8 says debug builds; there is no debug build). It only reads the USB serial port. The sim takes `CONTRAIL_ALERT=<kind>` and injects that alert 3 s after start.
9. **Swipe order, page dots and the carousel stay at six screens.** Alert is not in `kOrder`/`kCycle`; the dots and the offline badge hide while it is up.
10. **The pill and reason lines use Inter (`kFontBold`)** in both themes: the condensed faces may lack `*` and `·`.
11. **Explore mode, the carousel interval and the director's dwell all pause during an alert**: their clocks shift forward by the alert's length when it ends.
12. **Alerts are logged** (`[alert] <kind> <callsign>`) so the hardware check can see them.

## Conventions used by every task

- All paths are relative to `firmware/` unless they start with `docs/` or `tools/`. Run firmware commands from `firmware/`.
- One suite: `pio test -e test -f <suite>`. Everything: `pio test -e test`.
- **One pio env per invocation** (parallel envs race in SCons).
- Goldens: `UPDATE_GOLDEN=1 pio test -e test -f <suite>` writes `test/golden/<name>.ppm`. Convert for review with `../tools/.venv/bin/python ../tools/ppm2png.py test/golden/<name>.ppm "$SCRATCH/<name>.png"` and **show the PNGs to the user before committing new goldens**. A run without `UPDATE_GOLDEN` must then pass.
- `SCRATCH` = your session scratchpad directory.
- Hardware: the CYD2USB is on `/dev/cu.usbserial-114440`. Flash: `pio run -e cyd2usb -t upload --upload-port /dev/cu.usbserial-114440`. If the board is not attached, report the step as not done.
- Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Commit only the files the task lists.
- Code style: match the surrounding files (4-space indent, `// ` comments that say why, 120-column lines).

## Memory plan

| Item | Where | Bytes |
|---|---|---|
| `SceneFlight::squawk` + `emergencyStatus` × 32 flights | `Scene` (in `ScreenApp`, static) | ~192 |
| `Scene::alert` (`SceneAlert`, one `SceneFlight` copy) + `pollSeq` | `Scene` | ~110 |
| `AlertEngine`: 16 remembered planes (16 B each) + queue of 4 (`SceneFlight` + kind + entry, ~105 B each) | `ScreenApp` | ~680 |
| `AlertConfig` (24 × 11 B watchlist + numbers) | in the engine | ~290 |
| `AlertScreen` (text cache, clocks) | `ScreenApp` | ~250 |

About 1.5 KB of static RAM. Task 9 measures it on hardware (spec Done-when 6 allows 1 KB of change in free heap; if `heap=` drops by more than 1.5 KB, report the number).

## File map

| File | Status | Responsibility |
|---|---|---|
| `lib/core/model/Aircraft.h` | modify | `AC_EMERG_STATUS` flag |
| `lib/core/providers/ReadsbParser.cpp` | modify | set `AC_EMERG_STATUS` from the `emergency` field |
| `lib/core/screens/Scene.h` | modify | `AlertKind`, `SceneAlert`, `SceneFlight::squawk/emergencyStatus`, `Scene::pollSeq/alert` |
| `lib/core/app/SceneBuilder.cpp` | modify | copy squawk and status into the scene |
| `lib/core/app/CoreApp.{h,cpp}` | modify | count good polls into `Scene::pollSeq` |
| `lib/core/services/CoreSettings.{h,cpp}` | modify | nine alert settings |
| `lib/core/alerts/AlertConfig.{h,cpp}` | create | `AlertConfig`, watchlist and clock parsing, `configFromSettings` |
| `lib/core/alerts/AlertRules.{h,cpp}` | create | per-plane matching, kind names, reason lines, serial command parsing |
| `lib/core/alerts/AlertLights.{h,cpp}` | create | pulse, colours, LED/backlight/border levels, quiet hours |
| `lib/core/alerts/AlertEngine.{h,cpp}` | create | memory, debounce, queue, timer, dismiss, inject |
| `lib/core/screens/Screen.h` | modify | `ScreenId::Alert` |
| `lib/core/screens/Director.{h,cpp}` | modify | the alert interrupt |
| `lib/core/screens/AlertScreen.{h,cpp}` | create | the takeover in both themes |
| `lib/core/screens/ScreenApp.{h,cpp}` | modify | owns the screen and engine; taps dismiss; lights |
| `src/main_esp32.cpp` | modify | portal fields, settings → engine, LED/backlight, serial command, soak count |
| `src/main_sim.cpp` | modify | settings → engine, `CONTRAIL_ALERT` |
| `test/test_readsb`, `test/test_scene` | modify | new flag and fields |
| `test/test_alert_rules`, `test/test_alert_lights`, `test/test_alert_engine`, `test/test_alert_screen` | create | unit tests and goldens |
| `test/test_director` | modify | interrupt and resume |
| `docs/HARDWARE_TESTING.md`, the spec | modify | how to check alerts on the board; status |

---

### Task 1: Scene data for alerts

**Files:**
- Modify: `lib/core/model/Aircraft.h`, `lib/core/providers/ReadsbParser.cpp:78-79`, `lib/core/screens/Scene.h`, `lib/core/app/SceneBuilder.cpp:46`, `lib/core/app/CoreApp.h`, `lib/core/app/CoreApp.cpp` (`pollJob`, `renderTick`)
- Test: `test/test_readsb/test_main.cpp`, `test/test_scene/test_main.cpp`

- [ ] **Step 1: Write the failing tests**

In `test/test_readsb/test_main.cpp`, in `test_local_receiver_format`, after `TEST_ASSERT_TRUE(e.has(AC_EMERGENCY));` add:

```cpp
    // A squawk alone is not a declared emergency: the alert engine debounces squawks (alerts spec §4).
    TEST_ASSERT_FALSE(e.has(AC_EMERG_STATUS));
    TEST_ASSERT_EQUAL_STRING("7700", e.squawk);
```

And add a new test before `test_garbage_leaves_store_unchanged`:

```cpp
static void test_emergency_status_sets_its_own_flag() {
    parse(R"({"ac":[{"hex":"a1b2c3","lat":37.62,"lon":-122.37,"alt_baro":9000,"squawk":"2000","emergency":"general"}]})");
    Aircraft out[1];
    TEST_ASSERT_EQUAL(1, store.snapshot(out, 1, 100000));
    TEST_ASSERT_TRUE(out[0].has(AC_EMERG_STATUS));
    TEST_ASSERT_TRUE(out[0].has(AC_EMERGENCY));
}
```

Register it in `main()`: `RUN_TEST(test_emergency_status_sets_its_own_flag);`

In `test/test_scene/test_main.cpp` add after `test_fill_flight_upper_cases_the_callsign`:

```cpp
static void test_fill_flight_copies_squawk_and_emergency_status() {
    model::Aircraft a;
    a.hex = 0xA1B2C3;
    std::strcpy(a.squawk, "7700");
    a.flags = model::AC_EMERGENCY | model::AC_EMERG_STATUS;
    scr::SceneFlight f;
    app::fillFlight(f, a, nullptr, 0);
    TEST_ASSERT_EQUAL_STRING("7700", f.squawk);
    TEST_ASSERT_TRUE(f.emergency);
    TEST_ASSERT_TRUE(f.emergencyStatus);
    a.flags = model::AC_EMERGENCY;  // the squawk alone
    app::fillFlight(f, a, nullptr, 0);
    TEST_ASSERT_FALSE(f.emergencyStatus);
}
```

Register it: `RUN_TEST(test_fill_flight_copies_squawk_and_emergency_status);`

- [ ] **Step 2: Run them to see them fail**

Run: `pio test -e test -f test_readsb` and `pio test -e test -f test_scene`
Expected: compile errors (`AC_EMERG_STATUS`, `squawk`, `emergencyStatus` undeclared).

- [ ] **Step 3: Implement**

`lib/core/model/Aircraft.h`, the enum:

```cpp
enum AcFlags : uint8_t {
    AC_MILITARY = 1,
    AC_EMERGENCY = 2,     // a declared emergency or an emergency squawk
    AC_GROUND = 4,
    AC_HAS_POS = 8,
    AC_EMERG_STATUS = 16,  // the declared emergency alone (readsb "emergency" field)
};
```

`lib/core/providers/ReadsbParser.cpp`, replace the two emergency lines:

```cpp
    const char* em = e["emergency"];
    if (em && std::strcmp(em, "none") != 0) a.flags |= model::AC_EMERG_STATUS;
    if (a.has(model::AC_EMERG_STATUS) || isEmergencySquawk(a.squawk)) a.flags |= model::AC_EMERGENCY;
```

`lib/core/screens/Scene.h`: add before `struct SceneFlight`:

```cpp
// Alert kinds, highest priority first (alerts spec §2).
enum class AlertKind : uint8_t { Emergency, Watchlist, Military, LowPass };
```

In `SceneFlight`, after `char type[5] = "";`:

```cpp
    char squawk[5] = "";          // transponder code, "" unknown
```

and replace the flags line:

```cpp
    bool hasRoute = false, military = false, emergency = false;
    bool emergencyStatus = false;  // a declared emergency (emergency is that or an emergency squawk)
```

After `struct SceneFlight { ... };` add:

```cpp
// The alert on screen (alerts::AlertEngine is its only writer). While inactive the last alert's
// fields stay, so the Alert screen can fade out on them.
struct SceneAlert {
    bool active = false;
    AlertKind kind = AlertKind::LowPass;
    bool soft = false;  // quiet hours (never for an emergency): steady border, dim LED, no backlight pulse
    bool live = false;  // the plane is in this scene, so its numbers are current
    char matched[11] = "";  // the watchlist entry that matched
    SceneFlight flight;     // a copy: the alert outlasts the plane leaving the feed
    uint32_t startMs = 0, endsMs = 0;
};
```

In `struct Scene`, after `int dataAgeS = -1; ...` add:

```cpp
    uint32_t pollSeq = 0;  // good polls so far: the alert engine's two-poll squawk rule
    SceneAlert alert;
```

`lib/core/app/SceneBuilder.cpp`, in `fillFlight`, after `copyStr(f.type, sizeof f.type, a.type);`:

```cpp
    copyStr(f.squawk, sizeof f.squawk, a.squawk);
```

and replace the military/emergency line:

```cpp
    f.military = a.has(model::AC_MILITARY), f.emergency = a.has(model::AC_EMERGENCY);
    f.emergencyStatus = a.has(model::AC_EMERG_STATUS);
```

`lib/core/app/CoreApp.h`, after `std::atomic<bool> online_{true};`:

```cpp
    std::atomic<uint32_t> pollSeq_{0};  // good polls, for Scene::pollSeq
```

`lib/core/app/CoreApp.cpp`, in `pollJob` replace `if (ok) events_.publish(Ev::AircraftUpdated, store_.size());` with:

```cpp
    if (ok) ++pollSeq_, events_.publish(Ev::AircraftUpdated, store_.size());
```

and in `renderTick`, after `s.dataAgeS = ...;`:

```cpp
    s.pollSeq = pollSeq_;
```

- [ ] **Step 4: Run the tests**

Run: `pio test -e test -f test_readsb`, `pio test -e test -f test_scene`, then `pio test -e test`
Expected: all PASS (no golden changes: nothing draws the new fields yet).

- [ ] **Step 5: Commit**

```bash
git add lib/core/model/Aircraft.h lib/core/providers/ReadsbParser.cpp lib/core/screens/Scene.h lib/core/app/SceneBuilder.cpp lib/core/app/CoreApp.h lib/core/app/CoreApp.cpp test/test_readsb/test_main.cpp test/test_scene/test_main.cpp
git commit -m "alerts: squawk, declared emergency and poll count in the scene

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Alert settings and `AlertConfig`

**Files:**
- Modify: `lib/core/services/CoreSettings.h`, `lib/core/services/CoreSettings.cpp`
- Create: `lib/core/alerts/AlertConfig.h`, `lib/core/alerts/AlertConfig.cpp`
- Test: `test/test_alert_rules/test_main.cpp` (created here; Task 3 adds to it)

- [ ] **Step 1: Write the failing tests**

Create `test/test_alert_rules/test_main.cpp`:

```cpp
#include <unity.h>

#include <cstring>
#include <map>
#include <string>

#include "alerts/AlertConfig.h"
#include "services/CoreSettings.h"

using alerts::AlertConfig;

void setUp() {}
void tearDown() {}

class MemStore : public hal::IKeyValueStore {
   public:
    std::map<std::string, std::string> kv;
    bool getString(const char* k, char* out, size_t n) override {
        auto it = kv.find(k);
        if (it == kv.end()) return false;
        std::strncpy(out, it->second.c_str(), n - 1);
        out[n - 1] = 0;
        return true;
    }
    bool putString(const char* k, const char* v) override { return kv[k] = v, true; }
    bool erase(const char* k) override { return kv.erase(k) > 0; }
};

static void test_watchlist_parsing() {
    AlertConfig c;
    alerts::parseWatchlist(" n512* , ,RCH471,TOOLONGENTRY1, *,c17", c);
    TEST_ASSERT_EQUAL(3, c.nEntries);
    TEST_ASSERT_EQUAL_STRING("N512*", c.entries[0]);
    TEST_ASSERT_EQUAL_STRING("RCH471", c.entries[1]);
    TEST_ASSERT_EQUAL_STRING("C17", c.entries[2]);
    alerts::parseWatchlist("", c);
    TEST_ASSERT_EQUAL(0, c.nEntries);
    alerts::parseWatchlist(nullptr, c);
    TEST_ASSERT_EQUAL(0, c.nEntries);
}

static void test_watchlist_keeps_at_most_24_entries() {
    std::string many;
    for (int i = 0; i < 30; ++i) many += "A" + std::to_string(i) + ",";
    AlertConfig c;
    alerts::parseWatchlist(many.c_str(), c);
    TEST_ASSERT_EQUAL(AlertConfig::kMaxEntries, c.nEntries);
    TEST_ASSERT_EQUAL_STRING("A23", c.entries[23]);
}

static void test_clock_parsing() {
    TEST_ASSERT_EQUAL(22 * 60, alerts::parseClockMinutes("22:00", -1));
    TEST_ASSERT_EQUAL(7 * 60 + 5, alerts::parseClockMinutes("7:05", -1));
    TEST_ASSERT_EQUAL(-1, alerts::parseClockMinutes("24:00", -1));
    TEST_ASSERT_EQUAL(-1, alerts::parseClockMinutes("12:60", -1));
    TEST_ASSERT_EQUAL(-1, alerts::parseClockMinutes("ab", -1));
    TEST_ASSERT_EQUAL(-1, alerts::parseClockMinutes("12:00x", -1));
    TEST_ASSERT_EQUAL(-1, alerts::parseClockMinutes("", -1));
    TEST_ASSERT_EQUAL(-1, alerts::parseClockMinutes(nullptr, -1));
}

static void test_config_from_default_settings() {
    MemStore m;
    svc::Settings s(m, svc::kCoreSettings, svc::kCoreSettingsCount);
    s.load();
    AlertConfig c = alerts::configFromSettings(s);
    TEST_ASSERT_TRUE(c.emergency && c.watchlist && c.military && c.lowPass);
    TEST_ASSERT_EQUAL(3000, c.lowFt);
    TEST_ASSERT_EQUAL_FLOAT(2, c.lowNm);
    TEST_ASSERT_EQUAL(22 * 60, c.quietFrom);
    TEST_ASSERT_EQUAL(7 * 60, c.quietTo);
    TEST_ASSERT_EQUAL(0, c.nEntries);
}

static void test_config_from_changed_settings() {
    MemStore m;
    svc::Settings s(m, svc::kCoreSettings, svc::kCoreSettingsCount);
    s.load();
    s.set(svc::key::kAlertMil, "false");
    s.set(svc::key::kWatchlist, "N512*, C17");
    s.set(svc::key::kAlertLowFt, "2000");
    s.set(svc::key::kQuietFrom, "nonsense");  // a bad time falls back to the default
    s.set(svc::key::kQuietTo, "06:30");
    AlertConfig c = alerts::configFromSettings(s);
    TEST_ASSERT_FALSE(c.military);
    TEST_ASSERT_TRUE(c.emergency);
    TEST_ASSERT_EQUAL(2, c.nEntries);
    TEST_ASSERT_EQUAL(2000, c.lowFt);
    TEST_ASSERT_EQUAL(22 * 60, c.quietFrom);
    TEST_ASSERT_EQUAL(6 * 60 + 30, c.quietTo);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_watchlist_parsing);
    RUN_TEST(test_watchlist_keeps_at_most_24_entries);
    RUN_TEST(test_clock_parsing);
    RUN_TEST(test_config_from_default_settings);
    RUN_TEST(test_config_from_changed_settings);
    return UNITY_END();
}
```

- [ ] **Step 2: Run to see it fail**

Run: `pio test -e test -f test_alert_rules`
Expected: compile error (`alerts/AlertConfig.h` not found).

- [ ] **Step 3: Implement**

`lib/core/services/CoreSettings.h`, inside `namespace key` after `kTheme`:

```cpp
constexpr const char* kAlertEmerg = "alert_emerg";
constexpr const char* kAlertWatch = "alert_watch";
constexpr const char* kAlertMil = "alert_mil";
constexpr const char* kAlertLow = "alert_low";
constexpr const char* kWatchlist = "watchlist";
constexpr const char* kAlertLowFt = "alert_low_ft";
constexpr const char* kAlertLowNm = "alert_low_nm";
constexpr const char* kQuietFrom = "quiet_from";
constexpr const char* kQuietTo = "quiet_to";
```

`lib/core/services/CoreSettings.cpp`, append to `kCoreSettings` after the `kDispRotate` row (keep the column alignment):

```cpp
    {key::kAlertEmerg,  SType::Bool,   "alerts",   "Emergency alerts",                 0,    1,      1,   nullptr, nullptr,      0, false},
    {key::kAlertWatch,  SType::Bool,   "alerts",   "Watchlist alerts",                 0,    1,      1,   nullptr, nullptr,      0, false},
    {key::kAlertMil,    SType::Bool,   "alerts",   "Military alerts",                  0,    1,      1,   nullptr, nullptr,      0, false},
    {key::kAlertLow,    SType::Bool,   "alerts",   "Low pass alerts",                  0,    1,      1,   nullptr, nullptr,      0, false},
    {key::kWatchlist,   SType::String, "alerts",   "Watchlist",                        0,    0,      0,   "",      nullptr,      0, false},
    {key::kAlertLowFt,  SType::Int,    "alerts",   "Low pass below (ft)",              500,  10000,  3000, nullptr, nullptr,     0, false},
    {key::kAlertLowNm,  SType::Float,  "alerts",   "Low pass within (nm)",             0.5,  10,     2,   nullptr, nullptr,      0, false},
    {key::kQuietFrom,   SType::String, "alerts",   "Quiet hours from (HH:MM)",         0,    0,      0,   "22:00", nullptr,      0, false},
    {key::kQuietTo,     SType::String, "alerts",   "Quiet hours to (HH:MM)",           0,    0,      0,   "07:00", nullptr,      0, false},
```

Create `lib/core/alerts/AlertConfig.h`:

```cpp
#pragma once
// What the alert engine checks for (alerts spec §5), built from the settings.
#include <cstdint>

namespace svc {
class Settings;
}

namespace alerts {

struct AlertConfig {
    static constexpr int kMaxEntries = 24, kEntryLen = 10;  // longer than any tail, callsign or hex
    bool emergency = false, watchlist = false, military = false, lowPass = false;
    int32_t lowFt = 3000;
    float lowNm = 2;
    int quietFrom = 22 * 60, quietTo = 7 * 60;  // minutes past local midnight; equal: no quiet hours
    char entries[kMaxEntries][kEntryLen + 1] = {};  // upper case; a trailing '*' matches a prefix
    int nEntries = 0;
    bool any() const { return emergency || watchlist || military || lowPass; }
};

// Comma-separated entries: trimmed and upper-cased; empty ones, a lone "*" and ones over kEntryLen
// characters are skipped; at most kMaxEntries kept.
void parseWatchlist(const char* text, AlertConfig& c);
// "HH:MM" (hours 0..23, one or two digits) as minutes past midnight; anything else gives `fallback`.
int parseClockMinutes(const char* hhmm, int fallback);
AlertConfig configFromSettings(const svc::Settings& s);

}  // namespace alerts
```

Create `lib/core/alerts/AlertConfig.cpp`:

```cpp
#include "AlertConfig.h"

#include <cctype>
#include <cstdio>
#include <cstring>

#include "services/CoreSettings.h"

namespace alerts {

void parseWatchlist(const char* text, AlertConfig& c) {
    c.nEntries = 0;
    const char* p = text ? text : "";
    while (*p && c.nEntries < AlertConfig::kMaxEntries) {
        const char* end = std::strchr(p, ',');
        if (!end) end = p + std::strlen(p);
        const char *a = p, *b = end;
        while (a < b && std::isspace((unsigned char)*a)) ++a;
        while (b > a && std::isspace((unsigned char)b[-1])) --b;
        const size_t n = size_t(b - a);
        if (n > 0 && n <= size_t(AlertConfig::kEntryLen) && !(n == 1 && *a == '*')) {
            char* out = c.entries[c.nEntries++];
            for (size_t i = 0; i < n; ++i) out[i] = char(std::toupper((unsigned char)a[i]));
            out[n] = 0;
        }
        p = *end ? end + 1 : end;
    }
}

int parseClockMinutes(const char* s, int fallback) {
    int h = 0, m = 0;
    char extra = 0;
    if (!s || std::sscanf(s, "%d:%d%c", &h, &m, &extra) != 2) return fallback;
    if (h < 0 || h > 23 || m < 0 || m > 59) return fallback;
    return h * 60 + m;
}

AlertConfig configFromSettings(const svc::Settings& s) {
    using namespace svc;
    AlertConfig c;
    c.emergency = s.getBool(key::kAlertEmerg);
    c.watchlist = s.getBool(key::kAlertWatch);
    c.military = s.getBool(key::kAlertMil);
    c.lowPass = s.getBool(key::kAlertLow);
    c.lowFt = s.getInt(key::kAlertLowFt);
    c.lowNm = s.getFloat(key::kAlertLowNm);
    c.quietFrom = parseClockMinutes(s.getString(key::kQuietFrom).c_str(), 22 * 60);
    c.quietTo = parseClockMinutes(s.getString(key::kQuietTo).c_str(), 7 * 60);
    parseWatchlist(s.getString(key::kWatchlist).c_str(), c);
    return c;
}

}  // namespace alerts
```

- [ ] **Step 4: Run the tests**

Run: `pio test -e test -f test_alert_rules`, then `pio test -e test -f test_settings`
Expected: PASS. If `test_settings` checks the schema size or the group list, update its expectation to include the nine rows.

- [ ] **Step 5: Commit**

```bash
git add lib/core/services/CoreSettings.h lib/core/services/CoreSettings.cpp lib/core/alerts/AlertConfig.h lib/core/alerts/AlertConfig.cpp test/test_alert_rules/test_main.cpp
git commit -m "alerts: settings and AlertConfig (watchlist, low pass, quiet hours)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Alert rules

**Files:**
- Create: `lib/core/alerts/AlertRules.h`, `lib/core/alerts/AlertRules.cpp`
- Test: `test/test_alert_rules/test_main.cpp`

- [ ] **Step 1: Write the failing tests**

Add to `test/test_alert_rules/test_main.cpp`, after the existing includes:

```cpp
#include "alerts/AlertRules.h"

using alerts::bit;
using scr::AlertKind;
using scr::SceneFlight;

static AlertConfig allOn() {
    AlertConfig c;
    c.emergency = c.watchlist = c.military = c.lowPass = true;
    return c;
}

static SceneFlight cruiser() {
    SceneFlight f;
    f.hex = 0xAE1234;
    std::strcpy(f.callsign, "UAL53"), std::strcpy(f.reg, "N24974"), std::strcpy(f.type, "B789");
    f.altFt = 34000, f.distNm = 8, f.gsKt = 480;
    return f;
}
```

and these tests before `main`:

```cpp
static void test_entry_matching() {
    TEST_ASSERT_TRUE(alerts::entryMatches("N512*", "n512ka"));
    TEST_ASSERT_TRUE(alerts::entryMatches("N512*", "N512"));
    TEST_ASSERT_FALSE(alerts::entryMatches("N512*", "N51"));
    TEST_ASSERT_TRUE(alerts::entryMatches("C17", "c17"));
    TEST_ASSERT_FALSE(alerts::entryMatches("C17", "C17A"));
    TEST_ASSERT_FALSE(alerts::entryMatches("C17", ""));
    TEST_ASSERT_FALSE(alerts::entryMatches("N512*", ""));
}

static void test_watchlist_matches_each_field() {
    AlertConfig c = allOn();
    const char* lists[] = {"N24974", "ual*", "ae1234", "B789"};
    for (const char* l : lists) {
        alerts::parseWatchlist(l, c);
        alerts::Match m = alerts::matchFlight(cruiser(), c, false);
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(bit(AlertKind::Watchlist), m.kinds, l);
        TEST_ASSERT_EQUAL_STRING(c.entries[0], m.matched);
    }
    alerts::parseWatchlist("DAL*, A321", c);
    TEST_ASSERT_EQUAL_HEX8(0, alerts::matchFlight(cruiser(), c, false).kinds);
}

static void test_emergency() {
    AlertConfig c = allOn();
    SceneFlight f = cruiser();
    f.emergencyStatus = true;
    TEST_ASSERT_EQUAL_HEX8(bit(AlertKind::Emergency), alerts::matchFlight(f, c, false).kinds);
    f.emergencyStatus = false;
    std::strcpy(f.squawk, "7700");
    TEST_ASSERT_EQUAL_HEX8(0, alerts::matchFlight(f, c, false).kinds);  // not yet seen on two polls
    TEST_ASSERT_EQUAL_HEX8(bit(AlertKind::Emergency), alerts::matchFlight(f, c, true).kinds);
    std::strcpy(f.squawk, "1200");
    TEST_ASSERT_EQUAL_HEX8(0, alerts::matchFlight(f, c, true).kinds);
}

static void test_military_and_low_pass() {
    AlertConfig c = allOn();
    SceneFlight f = cruiser();
    f.military = true;
    TEST_ASSERT_EQUAL_HEX8(bit(AlertKind::Military), alerts::matchFlight(f, c, false).kinds);
    f.military = false;
    f.altFt = 2999, f.distNm = 1.9f;
    TEST_ASSERT_EQUAL_HEX8(bit(AlertKind::LowPass), alerts::matchFlight(f, c, false).kinds);
    f.altFt = 3000;
    TEST_ASSERT_EQUAL_HEX8(0, alerts::matchFlight(f, c, false).kinds);
    f.altFt = 2000, f.distNm = 2;
    TEST_ASSERT_EQUAL_HEX8(0, alerts::matchFlight(f, c, false).kinds);
    f.altFt = 0, f.distNm = 1;  // altitude unknown
    TEST_ASSERT_EQUAL_HEX8(0, alerts::matchFlight(f, c, false).kinds);
}

static void test_kinds_switched_off_never_match() {
    AlertConfig c;  // everything off
    alerts::parseWatchlist("UAL*", c);
    SceneFlight f = cruiser();
    f.military = f.emergencyStatus = true;
    f.altFt = 1000, f.distNm = 0.5f;
    TEST_ASSERT_EQUAL_HEX8(0, alerts::matchFlight(f, c, true).kinds);
    c = allOn();
    alerts::parseWatchlist("UAL*", c);
    TEST_ASSERT_EQUAL_HEX8(0x0F, alerts::matchFlight(f, c, true).kinds);
}

static void test_highest_kind() {
    TEST_ASSERT_EQUAL(int(AlertKind::Military), int(alerts::highest(bit(AlertKind::Military) | bit(AlertKind::LowPass))));
    TEST_ASSERT_EQUAL(int(AlertKind::Emergency), int(alerts::highest(0x0F)));
}

static void test_reason_lines() {
    scr::SceneAlert a;
    a.kind = AlertKind::Emergency;
    std::strcpy(a.flight.squawk, "7700");
    char out[40];
    alerts::reasonLine(a, true, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("7700 · GENERAL EMERGENCY", out);
    alerts::reasonLine(a, false, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("GENERAL EMERGENCY", out);
    std::strcpy(a.flight.squawk, "2000");  // declared, not squawked
    alerts::reasonLine(a, true, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("DECLARED EMERGENCY", out);
    a.kind = AlertKind::Watchlist;
    std::strcpy(a.matched, "N512*");
    alerts::reasonLine(a, true, out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("MATCHED N512*", out);
    TEST_ASSERT_EQUAL_STRING("HIJACK", alerts::squawkMeaning("7500"));
    TEST_ASSERT_EQUAL_STRING("RADIO FAILURE", alerts::squawkMeaning("7600"));
    TEST_ASSERT_NULL(alerts::squawkMeaning("1200"));
    TEST_ASSERT_EQUAL_STRING("LOW PASS", alerts::kindName(AlertKind::LowPass));
}

static void test_serial_command() {
    AlertKind k = AlertKind::Military;
    TEST_ASSERT_TRUE(alerts::parseTestCommand("alert emerg", k));
    TEST_ASSERT_EQUAL(int(AlertKind::Emergency), int(k));
    TEST_ASSERT_TRUE(alerts::parseTestCommand("alert watch", k));
    TEST_ASSERT_EQUAL(int(AlertKind::Watchlist), int(k));
    TEST_ASSERT_TRUE(alerts::parseTestCommand("alert mil", k));
    TEST_ASSERT_EQUAL(int(AlertKind::Military), int(k));
    TEST_ASSERT_TRUE(alerts::parseTestCommand("alert low", k));
    TEST_ASSERT_EQUAL(int(AlertKind::LowPass), int(k));
    TEST_ASSERT_FALSE(alerts::parseTestCommand("alert", k));
    TEST_ASSERT_FALSE(alerts::parseTestCommand("alert boom", k));
    TEST_ASSERT_FALSE(alerts::parseTestCommand("hello", k));
}
```

Register all seven in `main()` after the Task 2 tests.

- [ ] **Step 2: Run to see it fail**

Run: `pio test -e test -f test_alert_rules`
Expected: compile error (`alerts/AlertRules.h` not found).

- [ ] **Step 3: Implement**

Create `lib/core/alerts/AlertRules.h`:

```cpp
#pragma once
// Which alert kinds one plane matches (alerts spec §2), and the words the Alert screen uses for them.
#include <cstddef>
#include <cstdint>

#include "AlertConfig.h"
#include "screens/Scene.h"

namespace alerts {

using scr::AlertKind;

constexpr uint8_t bit(AlertKind k) { return uint8_t(1u << int(k)); }
// The highest-priority kind in a non-empty set.
AlertKind highest(uint8_t kinds);

struct Match {
    uint8_t kinds = 0;                              // bit() per kind matched
    char matched[AlertConfig::kEntryLen + 1] = "";  // the watchlist entry, when Watchlist matched
};

// `squawkConfirmed`: an emergency squawk has been seen on two polls in a row (the engine tracks it).
// Low pass needs a known altitude (> 0); the scene holds airborne planes only.
Match matchFlight(const scr::SceneFlight& f, const AlertConfig& c, bool squawkConfirmed);
// One watchlist entry (upper case, a trailing '*' for a prefix) against a field, ignoring case.
bool entryMatches(const char* entry, const char* value);

// "EMERGENCY", "WATCHLIST", "MILITARY", "LOW PASS".
const char* kindName(AlertKind k);
// "HIJACK", "RADIO FAILURE", "GENERAL EMERGENCY" for 7500 / 7600 / 7700, else null.
const char* squawkMeaning(const char* squawk);
// The alert's reason: "7700 · GENERAL EMERGENCY" (`withSquawk`) or "GENERAL EMERGENCY",
// "DECLARED EMERGENCY", "MATCHED N512*", "MILITARY AIRCRAFT", "LOW AND CLOSE".
void reasonLine(const scr::SceneAlert& a, bool withSquawk, char* out, size_t n);
// The serial test command: "alert emerg|watch|mil|low".
bool parseTestCommand(const char* line, AlertKind& out);

}  // namespace alerts
```

Create `lib/core/alerts/AlertRules.cpp`:

```cpp
#include "AlertRules.h"

#include <cctype>
#include <cstdio>
#include <cstring>

#include "providers/ReadsbParser.h"
#include "util/Str.h"

namespace alerts {

AlertKind highest(uint8_t kinds) {
    for (int k = 0; k < 4; ++k)
        if (kinds & (1u << k)) return AlertKind(k);
    return AlertKind::LowPass;
}

bool entryMatches(const char* entry, const char* value) {
    if (!value[0]) return false;
    size_t n = std::strlen(entry);
    const bool prefix = n && entry[n - 1] == '*';
    if (prefix) --n;
    size_t i = 0;
    for (; i < n; ++i)
        if (!value[i] || std::toupper((unsigned char)value[i]) != entry[i]) return false;
    return prefix || !value[i];
}

Match matchFlight(const scr::SceneFlight& f, const AlertConfig& c, bool squawkConfirmed) {
    Match m;
    if (c.emergency && (f.emergencyStatus || (squawkConfirmed && prov::isEmergencySquawk(f.squawk))))
        m.kinds |= bit(AlertKind::Emergency);
    if (c.watchlist) {
        char hex[7];
        std::snprintf(hex, sizeof hex, "%06X", unsigned(f.hex & 0xFFFFFFu));
        const char* const fields[] = {f.reg, f.callsign, hex, f.type};
        for (int i = 0; i < c.nEntries && !(m.kinds & bit(AlertKind::Watchlist)); ++i)
            for (const char* v : fields)
                if (entryMatches(c.entries[i], v)) {
                    m.kinds |= bit(AlertKind::Watchlist);
                    util::copyStr(m.matched, sizeof m.matched, c.entries[i]);
                    break;
                }
    }
    if (c.military && f.military) m.kinds |= bit(AlertKind::Military);
    if (c.lowPass && f.altFt > 0 && f.altFt < c.lowFt && f.distNm < c.lowNm) m.kinds |= bit(AlertKind::LowPass);
    return m;
}

const char* kindName(AlertKind k) {
    switch (k) {
        case AlertKind::Emergency: return "EMERGENCY";
        case AlertKind::Watchlist: return "WATCHLIST";
        case AlertKind::Military: return "MILITARY";
        case AlertKind::LowPass: return "LOW PASS";
    }
    return "";
}

const char* squawkMeaning(const char* sq) {
    if (!std::strcmp(sq, "7500")) return "HIJACK";
    if (!std::strcmp(sq, "7600")) return "RADIO FAILURE";
    if (!std::strcmp(sq, "7700")) return "GENERAL EMERGENCY";
    return nullptr;
}

void reasonLine(const scr::SceneAlert& a, bool withSquawk, char* out, size_t n) {
    switch (a.kind) {
        case AlertKind::Emergency: {
            const char* m = squawkMeaning(a.flight.squawk);
            if (!m) util::copyStr(out, n, "DECLARED EMERGENCY");
            else if (withSquawk) std::snprintf(out, n, "%s · %s", a.flight.squawk, m);
            else util::copyStr(out, n, m);
            return;
        }
        case AlertKind::Watchlist: std::snprintf(out, n, "MATCHED %s", a.matched); return;
        case AlertKind::Military: util::copyStr(out, n, "MILITARY AIRCRAFT"); return;
        case AlertKind::LowPass: util::copyStr(out, n, "LOW AND CLOSE"); return;
    }
}

bool parseTestCommand(const char* line, AlertKind& out) {
    static const struct {
        const char* word;
        AlertKind kind;
    } kWords[] = {{"emerg", AlertKind::Emergency}, {"watch", AlertKind::Watchlist},
                  {"mil", AlertKind::Military}, {"low", AlertKind::LowPass}};
    if (std::strncmp(line, "alert ", 6) != 0) return false;
    for (const auto& w : kWords)
        if (!std::strcmp(line + 6, w.word)) return out = w.kind, true;
    return false;
}

}  // namespace alerts
```

- [ ] **Step 4: Run the tests**

Run: `pio test -e test -f test_alert_rules`
Expected: all 12 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add lib/core/alerts/AlertRules.h lib/core/alerts/AlertRules.cpp test/test_alert_rules/test_main.cpp
git commit -m "alerts: per-plane rules, kind names and reason lines

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Alert lights and quiet hours

**Files:**
- Create: `lib/core/alerts/AlertLights.h`, `lib/core/alerts/AlertLights.cpp`
- Test: `test/test_alert_lights/test_main.cpp`

- [ ] **Step 1: Write the failing tests**

Create `test/test_alert_lights/test_main.cpp`:

```cpp
#include <unity.h>

#include "alerts/AlertLights.h"

using scr::AlertKind;

void setUp() {}
void tearDown() {}

static scr::SceneAlert on(AlertKind k, bool soft = false) {
    scr::SceneAlert a;
    a.active = true, a.kind = k, a.soft = soft;
    return a;
}

static void test_pulse_shape() {
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0, alerts::pulse(0));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.5f, alerts::pulse(250));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1, alerts::pulse(500));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0, alerts::pulse(1000));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1, alerts::pulse(29500));
}

static void test_no_alert_leaves_the_lights_alone() {
    scr::SceneAlert a;
    alerts::LightOut o = alerts::lightsFor(a, 500);
    TEST_ASSERT_FALSE(o.on);
    TEST_ASSERT_EQUAL_FLOAT(1, o.backlight);
    TEST_ASSERT_EQUAL_FLOAT(0, o.border);
    TEST_ASSERT_EQUAL_UINT8(0, o.led.r + o.led.g + o.led.b);
}

static void test_pulsing() {
    alerts::LightOut o = alerts::lightsFor(on(AlertKind::Military), 0);
    TEST_ASSERT_TRUE(o.on);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.25f, o.border);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.6f, o.backlight);
    TEST_ASSERT_EQUAL_UINT8(64, o.led.r);  // amber (255, 120, 0) at 25 %
    TEST_ASSERT_EQUAL_UINT8(30, o.led.g);
    TEST_ASSERT_EQUAL_UINT8(0, o.led.b);
    o = alerts::lightsFor(on(AlertKind::Military), 250);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.625f, o.border);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.8f, o.backlight);
    o = alerts::lightsFor(on(AlertKind::Military), 500);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1, o.border);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1, o.backlight);
    TEST_ASSERT_EQUAL_UINT8(255, o.led.r);
    TEST_ASSERT_EQUAL_UINT8(120, o.led.g);
}

static void test_soft_is_steady_and_dim() {
    for (uint32_t ms : {0u, 250u, 500u}) {
        alerts::LightOut o = alerts::lightsFor(on(AlertKind::Watchlist, true), ms);
        TEST_ASSERT_EQUAL_FLOAT(1, o.border);
        TEST_ASSERT_EQUAL_FLOAT(1, o.backlight);
        TEST_ASSERT_EQUAL_UINT8(0, o.led.r);
        TEST_ASSERT_EQUAL_UINT8(0, o.led.g);
        TEST_ASSERT_EQUAL_UINT8(51, o.led.b);  // blue at 20 %
    }
}

static void test_colours() {
    gfx::Rgb red = alerts::ledColour(AlertKind::Emergency);
    TEST_ASSERT_EQUAL_UINT8(255, red.r);
    TEST_ASSERT_EQUAL_UINT8(0, red.g);
    gfx::Rgb green = alerts::kindColour(AlertKind::LowPass);
    TEST_ASSERT_EQUAL_UINT8(0x3D, green.r);
    TEST_ASSERT_EQUAL_UINT8(0xDC, green.g);
    TEST_ASSERT_EQUAL_UINT8(0x84, green.b);
}

static void test_quiet_hours() {
    const int from = 22 * 60, to = 7 * 60;
    TEST_ASSERT_TRUE(alerts::inQuietHours(22 * 60, from, to));  // the start is inside
    TEST_ASSERT_TRUE(alerts::inQuietHours(23 * 60, from, to));
    TEST_ASSERT_TRUE(alerts::inQuietHours(6 * 60 + 59, from, to));
    TEST_ASSERT_FALSE(alerts::inQuietHours(7 * 60, from, to));  // the end is outside
    TEST_ASSERT_FALSE(alerts::inQuietHours(12 * 60, from, to));
    TEST_ASSERT_TRUE(alerts::inQuietHours(10 * 60, 9 * 60, 17 * 60));  // a daytime window
    TEST_ASSERT_FALSE(alerts::inQuietHours(17 * 60, 9 * 60, 17 * 60));
    TEST_ASSERT_FALSE(alerts::inQuietHours(23 * 60, from, from));  // equal times: off
    TEST_ASSERT_FALSE(alerts::inQuietHours(-1, from, to));         // clock not set
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_pulse_shape);
    RUN_TEST(test_no_alert_leaves_the_lights_alone);
    RUN_TEST(test_pulsing);
    RUN_TEST(test_soft_is_steady_and_dim);
    RUN_TEST(test_colours);
    RUN_TEST(test_quiet_hours);
    return UNITY_END();
}
```

- [ ] **Step 2: Run to see it fail**

Run: `pio test -e test -f test_alert_lights`
Expected: compile error (`alerts/AlertLights.h` not found).

- [ ] **Step 3: Implement**

Create `lib/core/alerts/AlertLights.h`:

```cpp
#pragma once
// How an alert drives the border, the RGB LED and the backlight (alerts spec §6), as plain
// functions of time so the tests need no hardware.
#include <cstdint>

#include "gfx/Color.h"
#include "screens/Scene.h"

namespace alerts {

constexpr uint32_t kPulseMs = 1000;  // one soft cycle a second: far under 3 flashes/s

// 0 at the start of each cycle, 1 halfway through (a raised cosine).
float pulse(uint32_t ms);
gfx::Rgb kindColour(scr::AlertKind k);  // on screen
gfx::Rgb ledColour(scr::AlertKind k);   // the LED, fully saturated

struct LightOut {
    bool on = false;
    gfx::Rgb led{0, 0, 0};
    float backlight = 1;  // a scale for the backlight's normal level
    float border = 0;     // border strength, 0..1
};
// What the lights do `msIntoAlert` after `a` started. Pulsing: border and LED 25-100 %, backlight
// 60-100 %. Soft (quiet hours): border steady, LED at 20 %, no backlight pulse.
LightOut lightsFor(const scr::SceneAlert& a, uint32_t msIntoAlert);

// Minutes past local midnight in [from, to), wrapping past midnight; never when from == to or
// minuteOfDay < 0 (the clock is not set).
bool inQuietHours(int minuteOfDay, int from, int to);

}  // namespace alerts
```

Create `lib/core/alerts/AlertLights.cpp`:

```cpp
#include "AlertLights.h"

#include <cmath>

namespace alerts {
using scr::AlertKind;

namespace {
gfx::Rgb scaled(gfx::Rgb c, float f) {
    return {uint8_t(c.r * f + 0.5f), uint8_t(c.g * f + 0.5f), uint8_t(c.b * f + 0.5f)};
}
}  // namespace

float pulse(uint32_t ms) {
    const float phase = float(ms % kPulseMs) / float(kPulseMs);
    return 0.5f - 0.5f * std::cos(2 * float(M_PI) * phase);
}

gfx::Rgb kindColour(AlertKind k) {
    switch (k) {
        case AlertKind::Emergency: return {0xFF, 0x3B, 0x3B};
        case AlertKind::Watchlist: return {0x4A, 0x8B, 0xFF};
        case AlertKind::Military: return {0xFF, 0xB0, 0x20};
        case AlertKind::LowPass: return {0x3D, 0xDC, 0x84};
    }
    return {255, 255, 255};
}

gfx::Rgb ledColour(AlertKind k) {
    switch (k) {
        case AlertKind::Emergency: return {255, 0, 0};
        case AlertKind::Watchlist: return {0, 0, 255};
        case AlertKind::Military: return {255, 120, 0};  // an LED's green is strong: amber needs little
        case AlertKind::LowPass: return {0, 255, 0};
    }
    return {0, 0, 0};
}

LightOut lightsFor(const scr::SceneAlert& a, uint32_t ms) {
    LightOut o;
    if (!a.active) return o;
    o.on = true;
    float led;
    if (a.soft) {
        led = 0.2f, o.border = 1;
    } else {
        const float p = pulse(ms);
        led = o.border = 0.25f + 0.75f * p;
        o.backlight = 0.6f + 0.4f * p;
    }
    o.led = scaled(ledColour(a.kind), led);
    return o;
}

bool inQuietHours(int m, int from, int to) {
    if (m < 0 || from == to) return false;
    return from < to ? m >= from && m < to : m >= from || m < to;
}

}  // namespace alerts
```

- [ ] **Step 4: Run the tests**

Run: `pio test -e test -f test_alert_lights`
Expected: 6 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add lib/core/alerts/AlertLights.h lib/core/alerts/AlertLights.cpp test/test_alert_lights/test_main.cpp
git commit -m "alerts: pulse, colours, LED and backlight levels, quiet hours

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Alert engine

**Files:**
- Create: `lib/core/alerts/AlertEngine.h`, `lib/core/alerts/AlertEngine.cpp`
- Test: `test/test_alert_engine/test_main.cpp`

- [ ] **Step 1: Write the failing tests**

Create `test/test_alert_engine/test_main.cpp`:

```cpp
#include <unity.h>

#include <cstdlib>
#include <cstring>
#include <ctime>

#include "alerts/AlertEngine.h"

using alerts::AlertConfig;
using alerts::AlertEngine;
using scr::AlertKind;
using scr::Scene;
using scr::SceneFlight;

void setUp() {
    setenv("TZ", "UTC0", 1);
    tzset();
}
void tearDown() {}

constexpr int64_t kMidnight = 1793750400;  // 2026-11-03 00:00 UTC

static AlertConfig allOn() {
    AlertConfig c;
    c.emergency = c.watchlist = c.military = c.lowPass = true;
    c.quietFrom = c.quietTo = 0;  // no quiet hours unless a test sets them
    return c;
}

static AlertEngine engine(const AlertConfig& c = allOn()) {
    AlertEngine e;
    e.configure(c);
    return e;
}

static SceneFlight& plane(Scene& s, uint32_t hex, const char* cs = "UAL53") {
    SceneFlight& f = s.flights[s.count++];
    f = SceneFlight{};
    f.hex = hex;
    std::strcpy(f.callsign, cs);
    f.altFt = 30000, f.distNm = 10, f.gsKt = 450;
    return f;
}

static SceneFlight& lowPlane(Scene& s, uint32_t hex) {
    SceneFlight& f = plane(s, hex);
    f.altFt = 2000, f.distNm = 1;
    return f;
}

static void test_a_plane_alerts_once() {
    Scene s;
    AlertEngine e = engine();
    plane(s, 0xAE0001, "RCH471").military = true;
    e.update(s, 1000);
    TEST_ASSERT_TRUE(s.alert.active);
    TEST_ASSERT_EQUAL(int(AlertKind::Military), int(s.alert.kind));
    TEST_ASSERT_EQUAL_HEX32(0xAE0001, s.alert.flight.hex);
    TEST_ASSERT_TRUE(s.alert.live);
    TEST_ASSERT_EQUAL_UINT32(1000, s.alert.startMs);
    TEST_ASSERT_EQUAL_UINT32(31000, s.alert.endsMs);
    e.update(s, 30999);
    TEST_ASSERT_TRUE(s.alert.active);
    e.update(s, 31000);
    TEST_ASSERT_FALSE(s.alert.active);
    e.update(s, 60000);
    TEST_ASSERT_FALSE(s.alert.active);  // the same plane: not again
}

static void test_a_higher_kind_alerts_again_a_lower_one_does_not() {
    Scene s;
    AlertEngine e = engine();
    SceneFlight& f = plane(s, 1);
    f.military = true;
    e.update(s, 1000);
    e.update(s, 31000);  // the military alert times out
    f.emergencyStatus = true;
    e.update(s, 40000);
    TEST_ASSERT_TRUE(s.alert.active);
    TEST_ASSERT_EQUAL(int(AlertKind::Emergency), int(s.alert.kind));
    e.dismiss(s);
    f.emergencyStatus = false, f.altFt = 2000, f.distNm = 1;  // now low and close: lower than military
    e.update(s, 50000);
    TEST_ASSERT_FALSE(s.alert.active);
}

static void test_an_emergency_squawk_needs_two_polls() {
    Scene s;
    AlertEngine e = engine();
    std::strcpy(plane(s, 1).squawk, "7700");
    s.pollSeq = 5;
    e.update(s, 1000);
    TEST_ASSERT_FALSE(s.alert.active);
    e.update(s, 1200);  // the same poll, rebuilt
    TEST_ASSERT_FALSE(s.alert.active);
    s.pollSeq = 6;
    e.update(s, 6000);
    TEST_ASSERT_TRUE(s.alert.active);
    TEST_ASSERT_EQUAL(int(AlertKind::Emergency), int(s.alert.kind));
}

static void test_a_squawk_that_goes_back_starts_over() {
    Scene s;
    AlertEngine e = engine();
    SceneFlight& f = plane(s, 1);
    std::strcpy(f.squawk, "7700"), s.pollSeq = 5;
    e.update(s, 1000);
    std::strcpy(f.squawk, "1200"), s.pollSeq = 6;  // dialled through 7700 on the way to 1200
    e.update(s, 6000);
    std::strcpy(f.squawk, "7700"), s.pollSeq = 7;
    e.update(s, 11000);
    TEST_ASSERT_FALSE(s.alert.active);
    s.pollSeq = 8;
    e.update(s, 16000);
    TEST_ASSERT_TRUE(s.alert.active);
}

static void test_a_declared_emergency_alerts_at_once() {
    Scene s;
    AlertEngine e = engine();
    plane(s, 1).emergencyStatus = true;
    e.update(s, 1000);
    TEST_ASSERT_TRUE(s.alert.active);
    TEST_ASSERT_EQUAL(int(AlertKind::Emergency), int(s.alert.kind));
}

static void test_alerts_queue_in_priority_order() {
    Scene s;
    AlertConfig c = allOn();
    alerts::parseWatchlist("N512*", c);
    AlertEngine e = engine(c);
    lowPlane(s, 1);
    plane(s, 2).military = true;
    std::strcpy(plane(s, 3, "N512KA").reg, "N512KA");
    e.update(s, 1000);
    TEST_ASSERT_EQUAL(int(AlertKind::Watchlist), int(s.alert.kind));
    TEST_ASSERT_EQUAL_STRING("N512*", s.alert.matched);
    TEST_ASSERT_EQUAL(2, e.queued());
    e.dismiss(s);
    TEST_ASSERT_FALSE(s.alert.active);
    e.update(s, 2000);
    TEST_ASSERT_EQUAL(int(AlertKind::Military), int(s.alert.kind));
    TEST_ASSERT_EQUAL_UINT32(2000, s.alert.startMs);
    e.dismiss(s);
    e.update(s, 3000);
    TEST_ASSERT_EQUAL(int(AlertKind::LowPass), int(s.alert.kind));
    e.dismiss(s);
    e.update(s, 4000);
    TEST_ASSERT_FALSE(s.alert.active);
}

static void test_an_emergency_replaces_the_alert_showing() {
    Scene s;
    AlertEngine e = engine();
    plane(s, 2).military = true;
    e.update(s, 1000);
    TEST_ASSERT_EQUAL(int(AlertKind::Military), int(s.alert.kind));
    plane(s, 1).emergencyStatus = true;
    e.update(s, 5000);
    TEST_ASSERT_EQUAL(int(AlertKind::Emergency), int(s.alert.kind));
    TEST_ASSERT_EQUAL_HEX32(1, s.alert.flight.hex);
    e.update(s, 35000);  // the emergency ends; the military alert is not shown again
    TEST_ASSERT_FALSE(s.alert.active);
    TEST_ASSERT_EQUAL(0, e.queued());
}

static void test_the_queue_holds_four_and_drops_the_lowest() {
    Scene s;
    AlertEngine e = engine();
    plane(s, 100).emergencyStatus = true;
    e.update(s, 1000);
    TEST_ASSERT_EQUAL(int(AlertKind::Emergency), int(s.alert.kind));
    TEST_ASSERT_EQUAL(0, e.queued());
    for (uint32_t h = 1; h <= 5; ++h) lowPlane(s, h);
    e.update(s, 2000);
    TEST_ASSERT_EQUAL(4, e.queued());  // the fifth low pass did not fit
    plane(s, 200).military = true;
    e.update(s, 3000);
    TEST_ASSERT_EQUAL(4, e.queued());  // in ahead of the low passes, the last of which went
    e.dismiss(s);
    e.update(s, 4000);
    TEST_ASSERT_EQUAL(int(AlertKind::Military), int(s.alert.kind));
}

static void test_stale_queued_alerts_are_skipped_but_emergencies_kept() {
    Scene s;
    AlertEngine e = engine();
    plane(s, 100).emergencyStatus = true;
    lowPlane(s, 1);
    e.update(s, 1000);
    s.count = 1;  // the low plane leaves
    e.update(s, 32000);  // the emergency ended at 31000; the low pass was last seen 31 s ago
    TEST_ASSERT_FALSE(s.alert.active);

    Scene t;
    AlertEngine e2 = engine();
    plane(t, 100).emergencyStatus = true;
    plane(t, 101, "DAL22").emergencyStatus = true;
    e2.update(t, 1000);
    t.count = 1;  // the second emergency leaves
    e2.update(t, 32000);
    TEST_ASSERT_TRUE(t.alert.active);
    TEST_ASSERT_EQUAL_HEX32(101, t.alert.flight.hex);
    TEST_ASSERT_FALSE(t.alert.live);
    TEST_ASSERT_EQUAL_STRING("DAL22", t.alert.flight.callsign);
}

static void test_memory_forgets_the_oldest_plane() {
    Scene s;
    AlertEngine e = engine();
    // 17 planes alert one after another: the 17th takes the first one's place.
    for (uint32_t h = 1; h <= 17; ++h) {
        s.count = 0;
        plane(s, h).military = true;
        e.update(s, h * 40000);
        TEST_ASSERT_TRUE(s.alert.active);
        TEST_ASSERT_EQUAL_HEX32(h, s.alert.flight.hex);
    }
    s.count = 0;
    plane(s, 1).military = true;
    e.update(s, 18 * 40000);
    TEST_ASSERT_TRUE(s.alert.active);  // forgotten, so it alerts again
    TEST_ASSERT_EQUAL_HEX32(1, s.alert.flight.hex);
    s.count = 0;
    plane(s, 17).military = true;
    e.update(s, 19 * 40000);
    TEST_ASSERT_FALSE(s.alert.active);  // still remembered
}

static void test_the_alert_follows_the_plane() {
    Scene s;
    AlertEngine e = engine();
    SceneFlight& f = plane(s, 1, "RCH471");
    f.military = true;
    e.update(s, 1000);
    f.altFt = 25000;
    e.update(s, 1200);
    TEST_ASSERT_EQUAL(25000, s.alert.flight.altFt);
    TEST_ASSERT_TRUE(s.alert.live);
    s.count = 0;
    e.update(s, 1400);
    TEST_ASSERT_TRUE(s.alert.active);
    TEST_ASSERT_FALSE(s.alert.live);
    TEST_ASSERT_EQUAL(25000, s.alert.flight.altFt);  // the last numbers stay
}

static void test_switching_every_kind_off_clears_alerts() {
    Scene s;
    AlertEngine e = engine();
    plane(s, 1).military = true;
    plane(s, 2).emergencyStatus = true;
    e.update(s, 1000);
    TEST_ASSERT_TRUE(s.alert.active);
    e.configure(AlertConfig{});
    e.update(s, 2000);
    TEST_ASSERT_FALSE(s.alert.active);
    TEST_ASSERT_EQUAL(0, e.queued());
}

static void test_quiet_hours_soften_all_but_emergencies() {
    Scene s;
    AlertConfig c = allOn();
    c.quietFrom = 22 * 60, c.quietTo = 7 * 60;
    AlertEngine e = engine(c);
    s.timeValid = true, s.epoch = kMidnight + 23 * 3600;
    plane(s, 1).military = true;
    e.update(s, 1000);
    TEST_ASSERT_TRUE(s.alert.soft);
    plane(s, 2).emergencyStatus = true;
    e.update(s, 2000);
    TEST_ASSERT_EQUAL(int(AlertKind::Emergency), int(s.alert.kind));
    TEST_ASSERT_FALSE(s.alert.soft);

    Scene day;
    AlertEngine e2 = engine(c);
    day.timeValid = true, day.epoch = kMidnight + 12 * 3600;
    plane(day, 1).military = true;
    e2.update(day, 1000);
    TEST_ASSERT_FALSE(day.alert.soft);

    Scene noClock;
    AlertEngine e3 = engine(c);
    noClock.timeValid = false, noClock.epoch = kMidnight + 23 * 3600;
    plane(noClock, 1).military = true;
    e3.update(noClock, 1000);
    TEST_ASSERT_TRUE(noClock.alert.active);
    TEST_ASSERT_FALSE(noClock.alert.soft);  // better loud than missed
}

static void test_inject_shows_a_made_up_alert() {
    Scene s;
    AlertEngine e;  // every kind off: injecting still works
    e.inject(AlertKind::Watchlist, 1000);
    e.update(s, 1000);
    TEST_ASSERT_TRUE(s.alert.active);
    TEST_ASSERT_EQUAL(int(AlertKind::Watchlist), int(s.alert.kind));
    TEST_ASSERT_EQUAL_STRING("N512*", s.alert.matched);
    TEST_ASSERT_EQUAL_STRING("N512TS", s.alert.flight.callsign);
    e.inject(AlertKind::Emergency, 2000);
    e.update(s, 2000);
    TEST_ASSERT_EQUAL(int(AlertKind::Emergency), int(s.alert.kind));
    TEST_ASSERT_EQUAL_STRING("7700", s.alert.flight.squawk);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_a_plane_alerts_once);
    RUN_TEST(test_a_higher_kind_alerts_again_a_lower_one_does_not);
    RUN_TEST(test_an_emergency_squawk_needs_two_polls);
    RUN_TEST(test_a_squawk_that_goes_back_starts_over);
    RUN_TEST(test_a_declared_emergency_alerts_at_once);
    RUN_TEST(test_alerts_queue_in_priority_order);
    RUN_TEST(test_an_emergency_replaces_the_alert_showing);
    RUN_TEST(test_the_queue_holds_four_and_drops_the_lowest);
    RUN_TEST(test_stale_queued_alerts_are_skipped_but_emergencies_kept);
    RUN_TEST(test_memory_forgets_the_oldest_plane);
    RUN_TEST(test_the_alert_follows_the_plane);
    RUN_TEST(test_switching_every_kind_off_clears_alerts);
    RUN_TEST(test_quiet_hours_soften_all_but_emergencies);
    RUN_TEST(test_inject_shows_a_made_up_alert);
    return UNITY_END();
}
```

- [ ] **Step 2: Run to see it fail**

Run: `pio test -e test -f test_alert_engine`
Expected: compile error (`alerts/AlertEngine.h` not found).

- [ ] **Step 3: Implement**

Create `lib/core/alerts/AlertEngine.h`:

```cpp
#pragma once
// Decides what the Alert screen shows (alerts spec §3-4). ScreenApp runs it on the render thread
// after each scene rebuild; it is the only writer of Scene::alert.
//
// Each plane alerts once per kind, and again only for a kind above everything it has alerted
// for. New matches queue highest kind first; an emergency replaces any other alert at once (the
// replaced one is not shown again). An emergency squawk counts once a later poll still carries it.
#include "AlertConfig.h"
#include "AlertRules.h"

namespace alerts {

class AlertEngine {
   public:
    static constexpr int kRemember = 16, kQueue = 4;
    static constexpr uint32_t kShowMs = 30000;   // a takeover's length
    static constexpr uint32_t kStaleMs = 30000;  // a queued plane gone this long is skipped (not emergencies)

    // Switching every kind off clears the queue, and the alert showing on the next update.
    void configure(const AlertConfig& c);
    const AlertConfig& config() const { return cfg_; }
    void update(scr::Scene& s, uint32_t nowMs);
    // Ends the alert showing; the next queued one starts on the following update.
    void dismiss(scr::Scene& s) { s.alert.active = false; }
    // A made-up alert of kind `k`, queued as any other (the serial "alert <kind>" command).
    void inject(AlertKind k, uint32_t nowMs);
    int queued() const { return nQueue_; }

   private:
    struct Seen {
        bool used = false;
        uint32_t hex = 0;
        uint8_t alerted = 0;         // bit() per kind already alerted
        bool squawkPending = false;  // an emergency squawk seen, waiting for a later poll
        uint32_t squawkSeq = 0;      // the poll it was first seen on
        uint32_t usedMs = 0;         // for forgetting the oldest
    };
    struct Queued {
        AlertKind kind;
        char matched[AlertConfig::kEntryLen + 1];
        scr::SceneFlight flight;
        uint32_t seenMs;  // last in the scene
    };
    Seen* find(uint32_t hex);
    Seen* take(uint32_t hex, uint32_t nowMs);
    void enqueue(AlertKind k, const char* matched, const scr::SceneFlight& f, uint32_t nowMs);
    void start(scr::Scene& s, const Queued& q, uint32_t nowMs);
    bool quietNow(const scr::Scene& s);

    AlertConfig cfg_;
    bool clear_ = false;
    Seen seen_[kRemember];
    Queued queue_[kQueue];
    int nQueue_ = 0;
    int64_t minute_ = -1;  // the epoch minute minuteOfDay_ is for
    int minuteOfDay_ = -1;
};

}  // namespace alerts
```

Create `lib/core/alerts/AlertEngine.cpp`:

```cpp
#include "AlertEngine.h"

#include <cstring>
#include <ctime>

#include "AlertLights.h"
#include "providers/ReadsbParser.h"
#include "services/Log.h"
#include "util/Str.h"

namespace alerts {
using scr::Scene;
using scr::SceneFlight;
using util::copyStr;

void AlertEngine::configure(const AlertConfig& c) {
    cfg_ = c;
    if (!cfg_.any()) nQueue_ = 0, clear_ = true;
}

AlertEngine::Seen* AlertEngine::find(uint32_t hex) {
    for (Seen& e : seen_)
        if (e.used && e.hex == hex) return &e;
    return nullptr;
}

AlertEngine::Seen* AlertEngine::take(uint32_t hex, uint32_t now) {
    Seen* pick = &seen_[0];
    for (Seen& e : seen_) {
        if (!e.used) {
            pick = &e;
            break;
        }
        if (int32_t(e.usedMs - pick->usedMs) < 0) pick = &e;
    }
    *pick = Seen{};
    pick->used = true, pick->hex = hex, pick->usedMs = now;
    return pick;
}

void AlertEngine::enqueue(AlertKind k, const char* matched, const SceneFlight& f, uint32_t now) {
    int at = nQueue_;
    while (at > 0 && int(queue_[at - 1].kind) > int(k)) --at;  // after its own kind, before lower ones
    if (at == kQueue) return;                                    // full of alerts at least as important
    if (nQueue_ == kQueue) --nQueue_;                            // drop the least important
    for (int i = nQueue_; i > at; --i) queue_[i] = queue_[i - 1];
    Queued& q = queue_[at];
    q.kind = k;
    copyStr(q.matched, sizeof q.matched, matched);
    q.flight = f, q.seenMs = now;
    ++nQueue_;
}

void AlertEngine::start(Scene& s, const Queued& q, uint32_t now) {
    scr::SceneAlert& a = s.alert;
    a.active = true, a.kind = q.kind;
    copyStr(a.matched, sizeof a.matched, q.matched);
    a.flight = q.flight;
    a.live = s.find(q.flight.hex) != nullptr;
    a.startMs = now, a.endsMs = now + kShowMs;
    svc::logf("[alert] %s %s", kindName(q.kind), q.flight.callsign);
}

bool AlertEngine::quietNow(const Scene& s) {
    if (!s.timeValid) return false;  // better loud than missed
    if (s.epoch / 60 != minute_) {   // localtime once a minute
        minute_ = s.epoch / 60;
        time_t t = time_t(s.epoch);
        struct tm tm;
        localtime_r(&t, &tm);
        minuteOfDay_ = tm.tm_hour * 60 + tm.tm_min;
    }
    return inQuietHours(minuteOfDay_, cfg_.quietFrom, cfg_.quietTo);
}

void AlertEngine::update(Scene& s, uint32_t now) {
    scr::SceneAlert& a = s.alert;
    if (clear_) a.active = false, clear_ = false;
    if (!cfg_.any() && !nQueue_ && !a.active) return;

    for (int i = 0; i < s.count; ++i) {
        const SceneFlight& f = s.flights[i];
        Seen* e = find(f.hex);
        bool confirmed = false;
        if (cfg_.emergency && prov::isEmergencySquawk(f.squawk)) {
            if (!e) e = take(f.hex, now);
            if (!e->squawkPending) e->squawkPending = true, e->squawkSeq = s.pollSeq;
            else confirmed = s.pollSeq != e->squawkSeq;
        } else if (e) {
            e->squawkPending = false;
        }
        const Match m = matchFlight(f, cfg_, confirmed);
        const uint8_t had = e ? e->alerted : 0;
        const uint8_t fresh = m.kinds & ~had;
        if (!fresh) continue;
        if (!e) e = take(f.hex, now);
        e->alerted |= m.kinds, e->usedMs = now;
        const AlertKind k = highest(fresh);
        if (had && int(k) >= int(highest(had))) continue;  // nothing above what it already alerted for
        enqueue(k, k == AlertKind::Watchlist ? m.matched : "", f, now);
    }

    for (int q = 0; q < nQueue_; ++q)
        if (const SceneFlight* f = s.find(queue_[q].flight.hex)) queue_[q].flight = *f, queue_[q].seenMs = now;
    if (a.active) {
        const SceneFlight* f = s.find(a.flight.hex);
        if (f) a.flight = *f;
        a.live = f != nullptr;
        if (int32_t(now - a.endsMs) >= 0) a.active = false;
    }
    if (a.active && a.kind != AlertKind::Emergency && nQueue_ && queue_[0].kind == AlertKind::Emergency)
        a.active = false;  // replaced, and not shown again
    while (!a.active && nQueue_) {
        const Queued q = queue_[0];
        for (int i = 1; i < nQueue_; ++i) queue_[i - 1] = queue_[i];
        --nQueue_;
        if (q.kind != AlertKind::Emergency && now - q.seenMs > kStaleMs) continue;
        start(s, q, now);
    }
    a.soft = a.active && a.kind != AlertKind::Emergency && quietNow(s);
}

void AlertEngine::inject(AlertKind k, uint32_t now) {
    SceneFlight f;
    f.hex = 0xFFFFFE;  // no real plane: the alert shows as not live
    copyStr(f.callsign, sizeof f.callsign, "N512TS");
    copyStr(f.reg, sizeof f.reg, "N512TS");
    copyStr(f.type, sizeof f.type, "C172");
    f.altFt = 2500, f.distNm = 1.2f, f.gsKt = 110, f.trackDeg = 300, f.bearingDeg = 45;
    if (k == AlertKind::Emergency) copyStr(f.squawk, sizeof f.squawk, "7700"), f.emergency = f.emergencyStatus = true;
    if (k == AlertKind::Military) f.military = true;
    enqueue(k, k == AlertKind::Watchlist ? "N512*" : "", f, now);
}

}  // namespace alerts
```

- [ ] **Step 4: Run the tests**

Run: `pio test -e test -f test_alert_engine`
Expected: 14 tests PASS. If `test_memory_forgets_the_oldest_plane` fails, check `take()` picks the smallest `usedMs` (oldest), not the first used slot.

- [ ] **Step 5: Commit**

```bash
git add lib/core/alerts/AlertEngine.h lib/core/alerts/AlertEngine.cpp test/test_alert_engine/test_main.cpp
git commit -m "alerts: engine (once per plane, two-poll squawks, priority queue, timer, quiet hours)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: `ScreenId::Alert` and the director interrupt

**Files:**
- Modify: `lib/core/screens/Screen.h:9`, `lib/core/screens/Director.h`, `lib/core/screens/Director.cpp` (`decide`, new `pauseRotations`), `src/main_esp32.cpp:321` (soak cycle length)
- Test: `test/test_director/test_main.cpp`

- [ ] **Step 1: Write the failing tests**

Add to `test/test_director/test_main.cpp` before `main`:

```cpp
// An alert outranks every rule in every mode, and the screen it interrupted comes back (alerts spec §7).
static void test_alert_interrupts_every_mode_and_the_screen_comes_back() {
    const scr::DirectorMode modes[] = {scr::DirectorMode::Auto, scr::DirectorMode::Carousel,
                                       scr::DirectorMode::Stay};
    for (scr::DirectorMode mode : modes) {
        Director d;
        d.configure(mode, 30000);
        ScreenId cur = ScreenId::Board;
        scr::Scene s = scene(3);
        run(d, s, cur, 0);  // Auto: Radar (the busy rotation); Carousel and Stay: Board
        const ScreenId before = cur;
        s.alert.active = true;
        TEST_ASSERT_EQUAL(int(ScreenId::Alert), int(run(d, s, cur, 1000)));
        TEST_ASSERT_EQUAL(int(ScreenId::Alert), int(run(d, s, cur, 20000)));
        s.alert.active = false;
        TEST_ASSERT_EQUAL(int(before), int(run(d, s, cur, 31000)));
        // The 30 s of alert did not count against the carousel's interval or a rotation's 45 s.
        TEST_ASSERT_EQUAL(int(before), int(run(d, s, cur, 40000)));
    }
}

static void test_alert_outranks_a_close_plane() {
    Director d;
    ScreenId cur = ScreenId::Radar;
    scr::Scene s = scene(1, 2, 8000);
    TEST_ASSERT_EQUAL(int(ScreenId::Closest), int(run(d, s, cur, 0)));
    s.alert.active = true;
    TEST_ASSERT_EQUAL(int(ScreenId::Alert), int(run(d, s, cur, 1000)));  // no dwell for an alert
    s.alert.active = false;
    TEST_ASSERT_EQUAL(int(ScreenId::Closest), int(run(d, s, cur, 31000)));
}

static void test_alert_pauses_explore_mode() {
    Director d;
    ScreenId cur = ScreenId::Radar;
    scr::Scene s = scene(3);
    run(d, s, cur, 0);
    d.noteTouch(1000);
    cur = ScreenId::Stats;  // the user swiped to Stats
    run(d, s, cur, 1100);
    s.alert.active = true;
    TEST_ASSERT_EQUAL(int(ScreenId::Alert), int(run(d, s, cur, 5000)));
    s.alert.active = false;
    TEST_ASSERT_EQUAL(int(ScreenId::Stats), int(run(d, s, cur, 50000)));
    // 4 s of exploring before the alert, 56 s after it: until 106 s.
    TEST_ASSERT_TRUE(d.exploring(100000));
    TEST_ASSERT_FALSE(d.exploring(107000));
}
```

Register the three in `main()`.

- [ ] **Step 2: Run to see it fail**

Run: `pio test -e test -f test_director`
Expected: compile error (`ScreenId::Alert` not a member).

- [ ] **Step 3: Implement**

`lib/core/screens/Screen.h`:

```cpp
enum class ScreenId : uint8_t { Closest, Radar, Board, Sky, Map, Stats, Alert, Count };
```

`lib/core/screens/Director.h`: add to the header comment, after the first-sightings block:

```cpp
//
// An alert (Scene::alert.active) outranks everything, in every mode and while exploring. When it
// ends, the screen it interrupted comes back; explore mode, the carousel and the rotations carry on
// as if the alert's time had not passed.
```

In the private section add:

```cpp
    void pauseRotations(uint32_t now);  // an alert came up: stop the rotations' clocks
    bool alerting_ = false;
    uint32_t alertFrom_ = 0;
```

`lib/core/screens/Director.cpp`: add before `decide`:

```cpp
void Director::pauseRotations(uint32_t now) {
    // Bank their time without leaving them: the interrupted screen resumes with what it had left.
    for (Rotation* r : {&busy_, &idle_})
        if (r->shown) r->had += now - r->since, r->shown = false;
}
```

and at the very top of `decide`:

```cpp
    if (s.alert.active) {
        if (!alerting_) alerting_ = true, alertFrom_ = now, pauseRotations(now);
        return ScreenId::Alert;  // asked_ keeps the screen to come back to
    }
    if (alerting_) {
        alerting_ = false;
        const uint32_t held = now - alertFrom_;
        if (touched_ && alertFrom_ - lastTouch_ < kExploreMs) lastTouch_ += held;  // explore mode resumes
        shownSince_ += held;  // nor does the alert count against the carousel or the dwell
        // The host is still on the Alert screen: go back to what we last asked for (an alert before
        // the first decision has nothing to go back to).
        if (asked_ != ScreenId::Count) current = asked_;
        else if (current == ScreenId::Alert) current = ScreenId::Radar;
    }
```

`src/main_esp32.cpp`, the soak's theme flip (Alert is not in the carousel):

```cpp
    if (millis() - themeFlip >= CONTRAIL_SOAK_CAROUSEL * 1000u * (uint32_t(scr::ScreenId::Count) - 1)) {
```

and change its comment to `// Soak: both themes, switching after each full carousel cycle (six screens x 20 s; not Alert).`

- [ ] **Step 4: Run the tests**

Run: `pio test -e test -f test_director`, then `pio test -e test`
Expected: PASS. (No Alert screen exists yet; `ScreenHost::show(Alert)` ignores an id it has no screen for.)

- [ ] **Step 5: Commit**

```bash
git add lib/core/screens/Screen.h lib/core/screens/Director.h lib/core/screens/Director.cpp src/main_esp32.cpp test/test_director/test_main.cpp
git commit -m "alerts: the director puts an alert up ahead of everything, then resumes

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: The Alert screen and `ScreenApp` wiring

**Files:**
- Create: `lib/core/screens/AlertScreen.h`, `lib/core/screens/AlertScreen.cpp`
- Modify: `lib/core/screens/ScreenApp.h`, `lib/core/screens/ScreenApp.cpp`
- Test: `test/test_alert_screen/test_main.cpp`, goldens `test/golden/alert_{emergency,watchlist,military,lowpass}.ppm`, `test/golden/ed_alert_{emergency,watchlist,military,lowpass}.ppm`

- [ ] **Step 1: Write the failing tests**

Create `test/test_alert_screen/test_main.cpp`:

```cpp
#include <unity.h>

#include <cstdlib>
#include <cstring>
#include <ctime>

#include "GoldenRender.h"
#include "screens/DemoScenes.h"

using scr::AlertKind;
using scr::ScreenId;

void setUp() {
    setenv("TZ", "UTC0", 1);
    tzset();
}
void tearDown() {}

// sceneQuiet's one flight as the alert's plane.
static void alertOn(scr::Scene& s, AlertKind k) {
    scr::sceneQuiet(s);
    scr::SceneAlert& a = s.alert;
    a = scr::SceneAlert{};
    a.active = true, a.kind = k, a.live = true;
    a.flight = s.flights[0];
    a.startMs = 1000, a.endsMs = 31000;
    if (k == AlertKind::Emergency) std::strcpy(a.flight.squawk, "7700"), a.flight.emergency = true;
    if (k == AlertKind::Watchlist) std::strcpy(a.matched, "N512*");
    if (k == AlertKind::Military) a.flight.military = true;
    if (k == AlertKind::LowPass) a.flight.altFt = 2500, a.flight.distNm = 1.2f;
}
static void emergency(scr::Scene& s) { alertOn(s, AlertKind::Emergency); }
static void watchlist(scr::Scene& s) { alertOn(s, AlertKind::Watchlist); }
static void military(scr::Scene& s) { alertOn(s, AlertKind::Military); }
static void lowPass(scr::Scene& s) { alertOn(s, AlertKind::LowPass); }

static void test_cinematic_goldens() {
    golden::render(ScreenId::Alert, emergency, "alert_emergency");
    golden::render(ScreenId::Alert, watchlist, "alert_watchlist");
    golden::render(ScreenId::Alert, military, "alert_military");
    golden::render(ScreenId::Alert, lowPass, "alert_lowpass");
}

static void test_editorial_goldens() {
    golden::render(ScreenId::Alert, emergency, "ed_alert_emergency", scr::editorial());
    golden::render(ScreenId::Alert, watchlist, "ed_alert_watchlist", scr::editorial());
    golden::render(ScreenId::Alert, military, "ed_alert_military", scr::editorial());
    golden::render(ScreenId::Alert, lowPass, "ed_alert_lowpass", scr::editorial());
}

// The border, rings and countdown move every frame: what is redrawn must match a full redraw.
static void test_pulse_leaves_no_stale_pixels() {
    for (const scr::Theme* t : {&scr::cinematicSky(), &scr::editorial()}) {
        golden::Rig g(*t);
        emergency(g.app->scene());
        g.app->sceneUpdated();
        g.show(ScreenId::Alert);
        g.frames(1);
        for (int i = 0; i < 6; ++i) {
            g.frames(7);
            g.assertNoStale();
        }
    }
}

// Quiet hours: the border and rings hold still, so frames only redraw the countdown.
static void test_soft_alert_redraws_only_the_countdown() {
    golden::Rig g;
    military(g.app->scene());
    g.app->scene().alert.soft = true;
    g.app->sceneUpdated();
    g.show(ScreenId::Alert);
    g.frames(3);
    for (int i = 0; i < 10; ++i) TEST_ASSERT_LESS_THAN(240 * 16, g.pushed());
}

// A new number from the feed redraws the screen; a plane that has left shows dimmed.
static void test_flight_changes_redraw() {
    golden::Rig g;
    military(g.app->scene());
    g.app->sceneUpdated();
    g.show(ScreenId::Alert);
    g.frames(3);
    g.app->scene().alert.flight.altFt += 1000;
    g.app->sceneUpdated();
    TEST_ASSERT_GREATER_THAN(240 * 200, g.pushed());
    g.assertNoStale();
    g.app->scene().alert.live = false;
    g.app->sceneUpdated();
    g.frames(1);
    g.assertNoStale();
}

// While an alert is up a tap ends it and does not reach the screen or start explore mode.
static void test_tap_dismisses() {
    golden::Rig g;
    scr::ScreenApp& app = *g.app;
    alerts::AlertConfig c;
    c.military = true;
    app.configureAlerts(c);
    scr::sceneQuiet(app.scene());
    app.scene().flights[0].military = true;
    ++app.scene().version;
    app.sceneUpdated();
    app.tick(1000);
    TEST_ASSERT_TRUE(app.scene().alert.active);
    TEST_ASSERT_EQUAL(int(ScreenId::Alert), int(app.host().target()));
    app.touch(true, 120, 160, 2000);  // two samples: a lone one is taken for panel noise
    app.touch(true, 120, 160, 2040);
    app.touch(false, 120, 160, 2080);
    TEST_ASSERT_FALSE(app.scene().alert.active);
    TEST_ASSERT_FALSE(app.director().exploring(2100));
    app.tick(2100);
    TEST_ASSERT_TRUE(app.host().target() != ScreenId::Alert);
}

static void test_lights_follow_the_alert() {
    scr::ScreenApp app;
    TEST_ASSERT_FALSE(app.alertLights(0).on);
    emergency(app.scene());
    alerts::LightOut o = app.alertLights(1500);  // 500 ms in: the top of the pulse
    TEST_ASSERT_TRUE(o.on);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1, o.backlight);
    TEST_ASSERT_EQUAL_UINT8(255, o.led.r);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_cinematic_goldens);
    RUN_TEST(test_editorial_goldens);
    RUN_TEST(test_pulse_leaves_no_stale_pixels);
    RUN_TEST(test_soft_alert_redraws_only_the_countdown);
    RUN_TEST(test_flight_changes_redraw);
    RUN_TEST(test_tap_dismisses);
    RUN_TEST(test_lights_follow_the_alert);
    return UNITY_END();
}
```

`test_tap_dismisses` relies on `GestureDetector` reading two pressed samples and a release within 600 ms and 14 px as a tap (`Gesture.h`: `kTapMinSamples`, `kTapMaxMs`, `kTapSlop`).

- [ ] **Step 2: Run to see it fail**

Run: `pio test -e test -f test_alert_screen`
Expected: compile errors (`configureAlerts`, `alertLights` not members).

- [ ] **Step 3: Implement the screen**

Create `lib/core/screens/AlertScreen.h`:

```cpp
#pragma once
// The alert takeover (alerts spec §6, mockup 8A). Cinematic Sky: "Emergency beacon", the plane at
// its track inside rings that pulse outward, the callsign as hero, the reason in a pill. Bold
// Editorial: "Colour field", the top half in the alert's colour with the squawk or kind huge. Both:
// a border pulsing in the alert's colour and a countdown bar.
#include "Screen.h"
#include "Theme.h"

namespace scr {

class AlertView : public gfx::Layer {
   public:
    explicit AlertView(const Theme& t) : t_(t) {}
    void setScene(const Scene* s) { scene_ = s; }
    void reset() { full_ = true; }
    void update(float dt, gfx::DamageList& damage) override;
    void draw(gfx::Canvas& c) override;

   private:
    // Everything draw() reads from the alert, rebuilt every update; a change redraws the screen.
    struct Text {
        char title[20], hero[12], sub[32], reason[40], big[12], stats[40];
        char stat[3][16];
        int heading;
        bool live;
    };
    void build(Text& t) const;
    void drawBorder(gfx::Canvas& c, gfx::Rgb col) const;
    void drawCinematic(gfx::Canvas& c, gfx::Rgb col) const;
    void drawEditorial(gfx::Canvas& c, gfx::Rgb col) const;
    int borderWidth() const { return t_.flat ? 6 : 8; }
    gfx::Rect bar() const;

    const Theme& t_;
    const Scene* scene_ = nullptr;
    bool full_ = true;
    uint32_t startMs_ = 0;  // which alert the clock is for
    float t_s_ = 0;         // seconds into it
    uint8_t border_ = 0, ring_ = 0;  // the pulse levels drawn (border 0..15, ring step 0..29)
    int bar_ = -1;                   // the countdown's drawn width
    uint32_t sig_ = 0;
    Text text_{};
};

class AlertScreen : public Screen {
   public:
    explicit AlertScreen(const Theme& t) : view_(t) {}
    ScreenId id() const override { return ScreenId::Alert; }
    void setScene(const Scene* s) override { view_.setScene(s); }
    void enter() override { view_.reset(); }
    int layerCount() const override { return 1; }
    gfx::Layer* const* layers() override { return layers_; }

   private:
    AlertView view_;
    gfx::Layer* layers_[1] = {&view_};
};

}  // namespace scr
```

Create `lib/core/screens/AlertScreen.cpp`:

```cpp
#include "AlertScreen.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "alerts/AlertEngine.h"
#include "alerts/AlertLights.h"
#include "alerts/AlertRules.h"
#include "gfx/Raster.h"
#include "gfx/fonts/Fonts.h"
#include "ui/Format.h"
#include "ui/Widgets.h"
#include "util/Str.h"

namespace scr {
using namespace gfx;
using util::copyStr;

namespace {
// Cinematic Sky: three rings travel outward from the plane, one every 0.5 s.
constexpr float kCx = 120, kCy = 114;
constexpr float kRingS = 1.5f, kRingR0 = 22, kRingTravel = 62;
constexpr int kRingSteps = 30;              // ring positions per trip (20 a second)
constexpr Rect kRingBox{32, 26, 176, 176};  // centred on the plane; holds the largest ring (84 px + AA)
constexpr float kPlaneSize = 40;
constexpr int kField = 150;  // Bold Editorial: the colour field's height
constexpr int kBorderSteps = 15;

void addBorder(DamageList& d, int w) {
    d.add({0, 0, 240, w});
    d.add({0, 320 - w, 240, w});
    d.add({0, w, w, 320 - 2 * w});
    d.add({240 - w, w, w, 320 - 2 * w});
}
}  // namespace

Rect AlertView::bar() const { return t_.flat ? Rect{14, 298, 212, 4} : Rect{24, 303, 192, 3}; }

void AlertView::build(Text& t) const {
    const SceneAlert& a = scene_->alert;
    const SceneFlight& f = a.flight;
    const bool squawk = a.kind == AlertKind::Emergency && alerts::squawkMeaning(f.squawk);
    // Editorial puts the kind (or squawk) huge in the field, so its small title only says what this is.
    copyStr(t.title, sizeof t.title, t_.flat && !squawk ? "ALERT" : alerts::kindName(a.kind));
    copyStr(t.hero, sizeof t.hero, f.callsign[0] ? f.callsign : "UNKNOWN");
    const bool reg = f.reg[0] && std::strcmp(f.reg, f.callsign) != 0;
    std::snprintf(t.sub, sizeof t.sub, "%s%s%s", f.type, f.type[0] && reg ? " · " : "", reg ? f.reg : "");
    alerts::reasonLine(a, !t_.flat, t.reason, sizeof t.reason);
    copyStr(t.big, sizeof t.big, squawk ? f.squawk : alerts::kindName(a.kind));
    ui::formatDistance(t.stat[0], sizeof t.stat[0], f.distNm);
    ui::formatAltitude(t.stat[1], sizeof t.stat[1], f.altFt);
    std::snprintf(t.stat[2], sizeof t.stat[2], "%d kt", int(f.gsKt + 0.5f));
    std::snprintf(t.stats, sizeof t.stats, "%s · %s · %s", t.stat[0], t.stat[1], t.stat[2]);
    t.heading = int(f.trackDeg + 0.5f) % 360;
    t.live = a.live;
}

void AlertView::update(float dt, DamageList& damage) {
    if (!scene_) return;
    const SceneAlert& a = scene_->alert;
    if (a.startMs != startMs_) startMs_ = a.startMs, t_s_ = 0, full_ = true;
    else t_s_ += dt;
    Text t{};  // zeroed, so the hash sees no stale bytes
    build(t);
    const uint32_t sig = ui::hashBytes(&t, sizeof t);
    if (sig != sig_) sig_ = sig, text_ = t, full_ = true;

    const float level = alerts::lightsFor(a, uint32_t(t_s_ * 1000)).border;
    const uint8_t border = uint8_t(level * kBorderSteps + 0.5f);
    const uint8_t ring = a.soft ? 0 : uint8_t(int(std::fmod(t_s_, kRingS) / kRingS * kRingSteps) % kRingSteps);
    const float left = 1 - std::fmin(t_s_ / (alerts::AlertEngine::kShowMs / 1000.f), 1.f);
    const Rect b = bar();
    const int barW = int(left * b.w + 0.5f);
    if (full_) {
        damage.addAll();
        full_ = false;
    } else {
        if (border != border_) addBorder(damage, borderWidth());
        if (!t_.flat && ring != ring_) damage.add(kRingBox);
        if (barW != bar_) damage.add(b);
    }
    border_ = border, ring_ = ring, bar_ = barW;
}

void AlertView::drawBorder(Canvas& c, Rgb col) const {
    if (!border_) return;
    const float level = border_ / float(kBorderSteps);
    const int w = borderWidth();
    // Flat: a solid frame. Cinematic: 3 px solid, fading out over the rest.
    for (int i = 0; i < w; ++i) {
        const float fade = t_.flat || i < 3 ? 1.f : 1.f - float(i - 2) / float(w - 2);
        const uint8_t al = uint8_t(255 * level * fade + 0.5f);
        if (!al) continue;
        c.fillBlend({i, i, 240 - 2 * i, 1}, col, al);
        c.fillBlend({i, 319 - i, 240 - 2 * i, 1}, col, al);
        c.fillBlend({i, i + 1, 1, 318 - 2 * i}, col, al);
        c.fillBlend({239 - i, i + 1, 1, 318 - 2 * i}, col, al);
    }
}

void AlertView::drawCinematic(Canvas& c, Rgb col) const {
    const Rgb stops[] = {t_.boardBgTop, t_.boardBgBottom};
    const float pos[] = {0, 1};
    ui::vFill(c, {0, 0, 240, 320}, stops, pos, 2, t_);
    ui::glow(c, kCx, kCy, 96, col, 70, t_);
    const float phase = ring_ / float(kRingSteps);
    for (int i = 0; i < 3; ++i) {
        const float f = std::fmod(phase + i / 3.f, 1.f);
        aaRing(c, kCx, kCy, kRingR0 + kRingTravel * f, 2, col, uint8_t(190 * (1 - f)));
    }
    planeGlyph(c, kCx + 1.5f, kCy + 2, kPlaneSize, float(text_.heading), t_.shadow, 60);
    planeGlyph(c, kCx, kCy, kPlaneSize, float(text_.heading), t_.text);
    drawText(c, kFontBold, 120, 24, text_.title, ui::readableOn(col, t_.boardBgTop, 4.5f), Align::Center);
    const Rgb val = text_.live ? t_.text : t_.textDim;
    ui::shadowText(c, *t_.fontDisplay, 120, 232, text_.hero, val, Align::Center, t_);
    drawText(c, kFontBody, 120, 250, text_.sub, t_.textSoft, Align::Center);
    const int pw = textWidth(kFontBold, text_.reason) + 20;
    fillRoundRect(c, {120 - pw / 2, 258, pw, 20}, 10, col);
    drawText(c, kFontBold, 120, 273, text_.reason, ui::textOn(col), Align::Center);
    drawText(c, kFontBold, 120, 296, text_.stats, val, Align::Center);
}

void AlertView::drawEditorial(Canvas& c, Rgb col) const {
    c.fill({0, 0, 240, kField}, rgb565(col));
    c.fill({0, kField, 240, 320 - kField}, rgb565(t_.boardBgBottom));
    const Rgb ink = ui::textOn(col);
    drawText(c, *t_.fontLabel, 14, 30, text_.title, ink);
    const FontData& big = textWidth(*t_.fontDisplay, text_.big) <= 212 ? *t_.fontDisplay : *t_.fontTitle;
    drawText(c, big, 14, 100, text_.big, ink);
    drawText(c, kFontBold, 14, 132, text_.reason, ink);
    const Rgb val = text_.live ? t_.text : t_.textDim;
    drawText(c, *t_.fontDisplay, 14, 206, text_.hero, val);
    drawText(c, kFontBody, 14, 228, text_.sub, t_.textSoft);
    static const char* const kLabels[] = {"DIST", "ALT", "SPEED"};
    for (int i = 0; i < 3; ++i) {
        drawText(c, kFontBold, 14 + 76 * i, 264, text_.stat[i], val);
        drawText(c, kFontSmall, 14 + 76 * i, 280, kLabels[i], t_.textDim);
    }
}

void AlertView::draw(Canvas& c) {
    if (!scene_) return;
    const Rgb col = alerts::kindColour(scene_->alert.kind);
    if (t_.flat) drawEditorial(c, col);
    else drawCinematic(c, col);
    const Rect b = bar();
    c.fill(b, rgb565(t_.rowFill));
    if (bar_ > 0) c.fill({b.x, b.y, bar_, b.h}, rgb565(col));
    drawBorder(c, col);
}

}  // namespace scr
```

- [ ] **Step 4: Wire it into `ScreenApp`**

`lib/core/screens/ScreenApp.h`: add includes

```cpp
#include "AlertScreen.h"
#include "alerts/AlertEngine.h"
#include "alerts/AlertLights.h"
```

update the header comment's first sentence to mention alerts:

```cpp
// Everything on screen: the scene, the screens, the host that runs them, the gesture
// detector, the director, the alert engine and the overlays. ...
```

public members, after `configure(...)`:

```cpp
    // Alerts start switched off (every kind false) until the platform passes the settings.
    void configureAlerts(const alerts::AlertConfig& c) { alerts_.configure(c); }
    void injectAlert(AlertKind k, uint32_t nowMs) { alerts_.inject(k, nowMs); }
    // The LED and backlight for the alert showing (or none) at nowMs.
    alerts::LightOut alertLights(uint32_t nowMs) const {
        return alerts::lightsFor(scene_.alert, nowMs - scene_.alert.startMs);
    }
```

members: after `StatsScreen stats_;` add `AlertScreen alert_;`, change `kScreens = 6` to `kScreens = 7`, and after `Director director_;` add:

```cpp
    alerts::AlertEngine alerts_;
    uint32_t alertVersion_ = 0;  // the scene version the engine last ran on
```

`lib/core/screens/ScreenApp.cpp`: in the constructor initialiser list, after `stats_(theme_, arena_),` add `alert_(theme_),` and make the array `screens_{&closest_, &radar_, &map_, &board_, &sky_, &stats_, &alert_},`.

In `tick`, before the `director_.decide` line:

```cpp
    // The alert engine first, so the director sees this rebuild's alert.
    if (scene_.version != alertVersion_) alertVersion_ = scene_.version, alerts_.update(scene_, now);
```

and replace the dots/badge lines at the end of `tick`:

```cpp
    const bool alert = host_.target() == ScreenId::Alert;  // the takeover covers the overlays
    dots_.set(exploring && !alert, active, kOrderN);
    badge_.set(!alert && (!scene_.online || scene_.dataAgeS > 60));
```

In `touch`, after `if (ev.gesture == Gesture::None) return;`:

```cpp
    // An alert takes taps only, and a tap ends it; it is not the user exploring (alerts spec §7).
    if (scene_.alert.active) {
        if (ev.gesture == Gesture::Tap) alerts_.dismiss(scene_);
        return;
    }
```

- [ ] **Step 5: Generate the goldens and look at them**

Run: `UPDATE_GOLDEN=1 pio test -e test -f test_alert_screen`
Then convert all eight (`alert_*.ppm`, `ed_alert_*.ppm`) to PNG with `../tools/.venv/bin/python ../tools/ppm2png.py test/golden/<name>.ppm "$SCRATCH/<name>.png"` and compare them with mockups 8A on the canvas. Check: the border is visible; the rings sit around the plane and nothing is clipped by `kRingBox`; the callsign, sub-line, pill and stats line do not overlap or run off the screen; no glyph shows as a box or is missing (`·`, `*`, digits in the condensed faces); Editorial's big word fits on one line. Fix layout numbers if needed and regenerate. **Show the eight PNGs to the user and wait for their go-ahead before committing.**

- [ ] **Step 6: Run the suites**

Run: `pio test -e test -f test_alert_screen` (without `UPDATE_GOLDEN`), then `pio test -e test`
Expected: all PASS, and no existing golden changes (the overlays only hide on the Alert screen).

- [ ] **Step 7: Commit**

```bash
git add lib/core/screens/AlertScreen.h lib/core/screens/AlertScreen.cpp lib/core/screens/ScreenApp.h lib/core/screens/ScreenApp.cpp test/test_alert_screen/test_main.cpp test/golden/alert_emergency.ppm test/golden/alert_watchlist.ppm test/golden/alert_military.ppm test/golden/alert_lowpass.ppm test/golden/ed_alert_emergency.ppm test/golden/ed_alert_watchlist.ppm test/golden/ed_alert_military.ppm test/golden/ed_alert_lowpass.ppm
git commit -m "alerts: the Alert screen in both themes, run by ScreenApp

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Platforms (setup portal, lights, serial command, sim)

**Files:**
- Modify: `src/main_esp32.cpp`, `src/main_sim.cpp`

No unit tests (platform glue); both envs must build, and Task 9 checks the board.

- [ ] **Step 1: Settings into the engine, and the lights (ESP32)**

`src/main_esp32.cpp`: add includes `#include <atomic>` and `#include "alerts/AlertConfig.h"`, `#include "alerts/AlertRules.h"`.

In the anonymous namespace, after `bool safeMode = false;`:

```cpp
// The backlight level from the brightness setting or the light sensor; alerts pulse around it.
uint8_t gBaseBright = 200;
// Set by any settings change (network thread or portal); the render loop rebuilds the alert config.
std::atomic<bool> gAlertsDirty{true};
```

Change `updateBrightness` to store the level instead of writing it:

```cpp
void updateBrightness() {
    static float smoothed = -1;
    static uint32_t last = 0;
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
```

In `setup()`, after `core.begin();`:

```cpp
    core.settings().onChange([](const char*) { gAlertsDirty = true; });
```

In `loop()`, after `if (core.renderTick(screens->scene())) screens->sceneUpdated();`:

```cpp
    if (gAlertsDirty.exchange(false)) screens->configureAlerts(alerts::configFromSettings(core.settings()));
    pollSerial();
```

and replace `updateBrightness();` with:

```cpp
    updateBrightness();
    updateLights();
```

- [ ] **Step 2: Portal fields (ESP32)**

In `connectWifi`, after `wm.addParameter(&pLon);`:

```cpp
    // Alerts (spec §5). A checkbox posts its value ("T") only when ticked.
    svc::Settings& st = core.settings();
    auto box = [&](const char* k) { return st.getBool(k) ? "type=\"checkbox\" checked" : "type=\"checkbox\""; };
    WiFiManagerParameter pEmerg(svc::key::kAlertEmerg, "Emergency alerts", "T", 2, box(svc::key::kAlertEmerg),
                                WFM_LABEL_AFTER);
    WiFiManagerParameter pWatchOn(svc::key::kAlertWatch, "Watchlist alerts", "T", 2, box(svc::key::kAlertWatch),
                                  WFM_LABEL_AFTER);
    WiFiManagerParameter pMil(svc::key::kAlertMil, "Military alerts", "T", 2, box(svc::key::kAlertMil),
                              WFM_LABEL_AFTER);
    WiFiManagerParameter pLow(svc::key::kAlertLow, "Low pass alerts", "T", 2, box(svc::key::kAlertLow),
                              WFM_LABEL_AFTER);
    const std::string watch = st.getString(svc::key::kWatchlist);
    WiFiManagerParameter pWatch(svc::key::kWatchlist,
                                "Watchlist (tails, callsigns, hex or types, comma-separated; * at the end matches a prefix)",
                                watch.c_str(), 160);
    char lowFt[8], lowNm[8];
    std::snprintf(lowFt, sizeof lowFt, "%d", st.getInt(svc::key::kAlertLowFt));
    std::snprintf(lowNm, sizeof lowNm, "%.1f", st.getFloat(svc::key::kAlertLowNm));
    WiFiManagerParameter pLowFt(svc::key::kAlertLowFt, "Low pass below (ft)", lowFt, 6, "type=\"number\"");
    WiFiManagerParameter pLowNm(svc::key::kAlertLowNm, "Low pass within (nm)", lowNm, 5);
    const std::string qFrom = st.getString(svc::key::kQuietFrom), qTo = st.getString(svc::key::kQuietTo);
    WiFiManagerParameter pFrom(svc::key::kQuietFrom, "Quiet hours from (HH:MM; same as to: off)", qFrom.c_str(), 5);
    WiFiManagerParameter pTo(svc::key::kQuietTo, "Quiet hours to (HH:MM)", qTo.c_str(), 5);
    WiFiManagerParameter* alertParams[] = {&pEmerg, &pWatchOn, &pMil, &pLow, &pWatch, &pLowFt, &pLowNm, &pFrom, &pTo};
    for (WiFiManagerParameter* p : alertParams) wm.addParameter(p);
```

and inside the `setSaveParamsCallback` lambda, after the lat/lon block:

```cpp
        auto ticked = [](WiFiManagerParameter& p) { return std::strcmp(p.getValue(), "T") == 0 ? "true" : "false"; };
        st.set(svc::key::kAlertEmerg, ticked(pEmerg));
        st.set(svc::key::kAlertWatch, ticked(pWatchOn));
        st.set(svc::key::kAlertMil, ticked(pMil));
        st.set(svc::key::kAlertLow, ticked(pLow));
        st.set(svc::key::kWatchlist, pWatch.getValue());
        st.set(svc::key::kAlertLowFt, pLowFt.getValue());  // the registry rejects or clamps bad numbers
        st.set(svc::key::kAlertLowNm, pLowNm.getValue());
        if (alerts::parseClockMinutes(pFrom.getValue(), -1) >= 0) st.set(svc::key::kQuietFrom, pFrom.getValue());
        if (alerts::parseClockMinutes(pTo.getValue(), -1) >= 0) st.set(svc::key::kQuietTo, pTo.getValue());
```

(`WFM_LABEL_AFTER` is WiFiManager 2.0's label placement constant; if the build says it is undeclared, drop that argument and the label shows before the box.)

- [ ] **Step 3: Build the board firmware**

Run: `pio run -e cyd2usb`
Expected: SUCCESS. Note the RAM and flash lines for Task 9.

- [ ] **Step 4: The sim**

`src/main_sim.cpp`: add `#include "alerts/AlertConfig.h"` and `#include "alerts/AlertRules.h"`. After the `useTheme` lines:

```cpp
    screens->configureAlerts(alerts::configFromSettings(core.settings()));
    // CONTRAIL_ALERT=emerg|watch|mil|low shows that alert 3 s in, for trying the takeover from the shell.
    scr::AlertKind simAlert = scr::AlertKind::Emergency;
    const char* alertEnv = std::getenv("CONTRAIL_ALERT");
    bool alertPending = alertEnv && alerts::parseTestCommand((std::string("alert ") + alertEnv).c_str(), simAlert);
    const uint32_t simStart = clock.millis();
    auto maybeInject = [&] {
        if (alertPending && clock.millis() - simStart >= 3000)
            screens->injectAlert(simAlert, clock.millis()), alertPending = false;
    };
```

and call `maybeInject();` at the start of both loop bodies (the `--shot` loop and the SDL loop).

- [ ] **Step 5: Build the sim and take a shot**

Run: `pio run -e sim`, then
`CONTRAIL_ALERT=emerg .pio/build/sim/program --shot "$SCRATCH/sim_alert.ppm" 6` (PlatformIO's native binary is `program`; use the path the build prints if it differs)
Expected: builds; the shot shows the emergency takeover (convert with `ppm2png.py` and look). If the sim has no network, the takeover still shows (the injected plane is not live, so its numbers are dimmed).

- [ ] **Step 6: Commit**

```bash
git add src/main_esp32.cpp src/main_sim.cpp
git commit -m "alerts: setup portal fields, LED and backlight, serial test command, sim

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: Check on the board, and docs

**Files:**
- Modify: `docs/HARDWARE_TESTING.md`, `docs/superpowers/specs/2026-11-03-contrail-alerts-design.md`

- [ ] **Step 1: Flash and watch the log**

Run: `pio run -e cyd2usb -t upload --upload-port /dev/cu.usbserial-114440`, then start
`/opt/homebrew/Cellar/platformio/6.2.0_1/libexec/bin/python "$SCRATCH/mon.py" /dev/cu.usbserial-114440 240 reset > "$SCRATCH/alerts.log" 2>&1` in the background (the monitor script from pass 2; if it is gone, use `pio device monitor -p /dev/cu.usbserial-114440 -b 115200` in the background and stop it after 4 minutes).

- [ ] **Step 2: Fire each kind**

After the `[boot] ready` line, send each command about 40 s apart from a second shell, without resetting the board:

```bash
/opt/homebrew/Cellar/platformio/6.2.0_1/libexec/bin/python -c "import serial,sys,time; s=serial.Serial(); s.port='/dev/cu.usbserial-114440'; s.baudrate=115200; s.dtr=False; s.rts=False; s.open(); s.write(('alert '+sys.argv[1]+'\n').encode()); time.sleep(0.2)" emerg
```

then `watch`, `mil`, `low`. (If the monitor holds the port exclusively, stop it first, send, and restart it without `reset`.)

Expected for each: `[alert] test: alert <kind>` and `[alert] <KIND> N512TS` in the log; the takeover appears in red / blue / amber / green with the border and LED pulsing once a second and the backlight dipping; a tap ends it early and the previous screen returns; without a tap it ends after 30 s. Ask the user to watch one and confirm the LED colours (the code cannot see the LED).

- [ ] **Step 3: Check frame time and heap**

From the `[health]` and `[render]` lines while an alert is up: `drawn=` ≥ 25/s on the Alert screen, and `heap=` within 1.5 KB of the last pass-2 figure (record both numbers). Report anything outside.

- [ ] **Step 4: Document it**

Append to `docs/HARDWARE_TESTING.md` a section:

```markdown
## Alerts

Send `alert emerg`, `alert watch`, `alert mil` or `alert low` (with a newline) over the USB serial port at
115200 baud to show a made-up alert of that kind. Each should show the takeover in its colour (red, blue,
amber, green), pulse the border, LED and backlight once a second, end after 30 s or on a tap, and log
`[alert] <KIND> N512TS`. During quiet hours (default 22:00-07:00) only an emergency pulses.
```

In the spec, set `Status: Built (plan docs/superpowers/plans/2026-11-03-contrail-alerts.md)` and change "in debug builds a serial command" in §8 to "a serial command (always built in; plan decision 8)".

- [ ] **Step 5: Run everything and commit**

Run: `pio test -e test`
Expected: all PASS.

```bash
git add docs/HARDWARE_TESTING.md docs/superpowers/specs/2026-11-03-contrail-alerts-design.md
git commit -m "alerts: hardware check notes; spec marked built

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
