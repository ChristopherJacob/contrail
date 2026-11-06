# Contrail Configuration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Change every Contrail setting from a web page at `http://contrail.local`, change the common
ones from a touch panel opened by a 1 s hold, and update firmware by browser upload.

**Architecture:** A pure `svc::SettingsApi` turns the existing settings registry into JSON and back
(all-or-nothing saves, secrets never sent). An ESP32-only `web::WebUi` serves one gzipped page and
that API from its own FreeRTOS task, and streams uploads into the spare OTA slot. On the device, the
gesture detector gains a hold; `ScreenApp` draws a ring while it fills, then shows two new
full-screen screens outside the swipe order — `SettingsScreen` (the panel) and `UpdateScreen`. Panel
edits go through a locked queue to the web task, which saves them, so NVS writes never land on the
render thread.

**Tech Stack:** C++17, PlatformIO (`espressif32@6.9.0`, Arduino 2.0.x), ArduinoJson 7, Arduino
`WebServer` + `Update`, Unity tests in the `test` env, golden PPMs, one HTML file with inline JS,
Leaflet 1.9.4 + OpenStreetMap tiles loaded by the browser.

**Spec:** `docs/superpowers/specs/2026-11-04-contrail-config-design.md` (read it first).
**Mockups:** private canvas "Contrail Settings Mockups" — the controller can describe them; the
layouts below already follow them.

---

## Ground rules for every task

- Work on the branch the controller gives you (not `main`). Commit after each task.
- Commands run from `firmware/`. Run one `pio` command at a time.
- All tests: `pio test -e test` (about 380 tests today, all passing). One suite:
  `pio test -e test -f test_settings_api`.
- Goldens: a test calling `assertGolden("name", ...)` fails with "missing golden" until you run it
  once with `UPDATE_GOLDEN=1 pio test -e test -f <suite>`, which writes `test/golden/name.ppm`.
  **Look at every new golden** before committing it:
  `sips -s format png test/golden/name.ppm --out /tmp/name.png`, then open the PNG (Read tool).
  Fix layout problems you see (clipped text, overlaps) and regenerate.
- Firmware build: `pio run -e cyd2usb`. The last lines print `Flash: [...] (used N bytes from
  1966080 bytes)`. Before this plan: 1,802,753 bytes.
- Code style: match the surrounding code — 4-space indent, 120 columns, brief "why" comments,
  `namespace scr` / `svc` / `app` as the folder implies.
- Commit messages end with:
  ```
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  ```

## File map

| File | Task | Responsibility |
|---|---|---|
| `lib/core/services/Settings.h` | 1 | add `find(key)` |
| `lib/core/services/SettingsApi.h/.cpp` | 1 | schema/values JSON, all-or-nothing apply |
| `test/test_settings_api/test_main.cpp` | 1, 6 | its tests, plus PanelBridge's |
| `lib/core/screens/Gesture.h/.cpp` | 2 | `Gesture::Hold`, `holdProgress()` |
| `test/test_gesture/test_main.cpp` | 2 | hold tests |
| `lib/core/screens/Screen.h` | 3, 4 | `ScreenId::Settings`, `ScreenId::Update` |
| `lib/core/screens/SettingsScreen.h/.cpp` | 3 | the touch panel |
| `lib/core/screens/UpdateScreen.h/.cpp` | 4 | upload progress screen |
| `lib/core/screens/Overlays.h/.cpp` | 5 | `HoldRing` |
| `lib/core/screens/ScreenHost.h` | 5 | three overlays |
| `lib/core/screens/ScreenApp.h/.cpp` | 3, 4, 5 | register screens, hold → panel, update mode |
| `test/test_config_screens/test_main.cpp` | 3, 4, 5 | goldens and panel behaviour |
| `lib/core/app/PanelBridge.h/.cpp` | 6 | settings → panel values, edit queue |
| `web/index.html` | 7 | the page |
| `scripts/web_embed.py` | 7 | gzip page → `src/web/WebPage.h` (generated, ignored) |
| `platformio.ini`, `../.gitignore` | 7 | pre-build script, `src/web/` in the build |
| `src/web/WebUi.h/.cpp` | 8 | HTTP routes, OTA, web task |
| `src/main_esp32.cpp` | 9 | wiring, live theme/tz, smaller portal |
| `src/main_sim.cpp` | 10 | panel edits in the simulator |
| `../docs/HARDWARE_TESTING.md`, spec | 11 | results |

---

### Task 1: SettingsApi

**Files:**
- Modify: `lib/core/services/Settings.h`
- Create: `lib/core/services/SettingsApi.h`, `lib/core/services/SettingsApi.cpp`
- Test: `test/test_settings_api/test_main.cpp`

`Settings::set` clamps out-of-range numbers; the web save must reject them instead, so the API
validates every value itself before calling `set`.

- [ ] **Step 1: Add `find` to `Settings`**

In `lib/core/services/Settings.h`, under `int count() const { return n_; }`, add:

```cpp
    // The definition for `key`, or nullptr. Definitions never change, so no lock is needed.
    const SettingDef* find(const char* key) const {
        int i = indexOf(key);
        return i < 0 ? nullptr : &defs_[i];
    }
```

- [ ] **Step 2: Write the failing tests**

Create `test/test_settings_api/test_main.cpp`:

```cpp
#include <unity.h>

#include <cstring>
#include <map>
#include <string>

#include "services/CoreSettings.h"
#include "services/SettingsApi.h"

using namespace svc;

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

struct Fixture {
    MemStore m;
    Settings s{m, kCoreSettings, kCoreSettingsCount};
    Fixture() { s.load(); }
    ApplyResult apply(const char* json, JsonDocument& errors) {
        JsonDocument in;
        TEST_ASSERT_FALSE(deserializeJson(in, json));
        return applyJson(s, in.as<JsonVariantConst>(), errors);
    }
};

static void test_schema_lists_every_setting() {
    Fixture f;
    JsonDocument d;
    schemaJson(f.s, d);
    TEST_ASSERT_EQUAL(kCoreSettingsCount, int(d["settings"].size()));
    TEST_ASSERT_EQUAL_STRING("lat", d["settings"][0]["key"]);
}

static void test_values_never_send_secrets() {
    Fixture f;
    f.s.set(key::kOskySecret, "hunter2");
    JsonDocument d;
    valuesJson(f.s, d);
    TEST_ASSERT_TRUE(d[key::kOskySecret]["set"].as<bool>());
    TEST_ASSERT_FALSE(d[key::kAeroKey]["set"].as<bool>());
    std::string text;
    serializeJson(d, text);
    TEST_ASSERT_NULL(std::strstr(text.c_str(), "hunter2"));
    TEST_ASSERT_EQUAL(25, d[key::kRadius].as<int>());
}

static void test_good_save_stores_every_value() {
    Fixture f;
    JsonDocument errors;
    ApplyResult r = f.apply(R"({"radius_nm":40,"theme":"editorial","alert_mil":false,"lat":"39.05"})", errors);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_FALSE(r.restart);
    TEST_ASSERT_EQUAL(40, f.s.getInt(key::kRadius));
    TEST_ASSERT_EQUAL_STRING("editorial", f.s.getEnum(key::kTheme).c_str());
    TEST_ASSERT_FALSE(f.s.getBool(key::kAlertMil));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 39.05f, f.s.getFloat(key::kLat));
}

static void test_one_bad_value_stores_nothing() {
    Fixture f;
    JsonDocument errors;
    ApplyResult r = f.apply(R"({"radius_nm":40,"theme":"neon"})", errors);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_EQUAL_STRING("not an option", errors["theme"]);
    TEST_ASSERT_TRUE(errors["radius_nm"].isNull());
    TEST_ASSERT_EQUAL(25, f.s.getInt(key::kRadius));  // untouched
}

static void test_out_of_range_is_rejected_not_clamped() {
    Fixture f;
    JsonDocument errors;
    TEST_ASSERT_FALSE(f.apply(R"({"radius_nm":500})", errors).ok);
    TEST_ASSERT_EQUAL_STRING("out of range 5–100", errors["radius_nm"]);
    JsonDocument e2;
    TEST_ASSERT_FALSE(f.apply(R"({"radius_nm":12.5})", e2).ok);
    TEST_ASSERT_EQUAL_STRING("not a whole number", e2["radius_nm"]);
    JsonDocument e3;
    TEST_ASSERT_FALSE(f.apply(R"({"alert_low":"yes"})", e3).ok);
    TEST_ASSERT_EQUAL_STRING("not true or false", e3["alert_low"]);
}

static void test_empty_secret_is_unchanged_and_null_clears_it() {
    Fixture f;
    f.s.set(key::kAeroKey, "abc123");
    JsonDocument e1;
    TEST_ASSERT_TRUE(f.apply(R"({"aero_key":""})", e1).ok);
    TEST_ASSERT_EQUAL_STRING("abc123", f.s.getString(key::kAeroKey).c_str());
    JsonDocument e2;
    TEST_ASSERT_TRUE(f.apply(R"({"aero_key":null})", e2).ok);
    TEST_ASSERT_EQUAL_STRING("", f.s.getString(key::kAeroKey).c_str());
}

static void test_hardware_change_asks_for_restart() {
    Fixture f;
    JsonDocument errors;
    ApplyResult r = f.apply(R"({"disp_bgr":true})", errors);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_TRUE(r.restart);
}

static void test_unknown_key_and_non_object_are_rejected() {
    Fixture f;
    JsonDocument e1;
    TEST_ASSERT_FALSE(f.apply(R"({"colour":"red"})", e1).ok);
    TEST_ASSERT_EQUAL_STRING("unknown setting", e1["colour"]);
    JsonDocument e2;
    TEST_ASSERT_FALSE(f.apply("[1,2]", e2).ok);
    TEST_ASSERT_EQUAL_STRING("expected an object", e2["_"]);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_schema_lists_every_setting);
    RUN_TEST(test_values_never_send_secrets);
    RUN_TEST(test_good_save_stores_every_value);
    RUN_TEST(test_one_bad_value_stores_nothing);
    RUN_TEST(test_out_of_range_is_rejected_not_clamped);
    RUN_TEST(test_empty_secret_is_unchanged_and_null_clears_it);
    RUN_TEST(test_hardware_change_asks_for_restart);
    RUN_TEST(test_unknown_key_and_non_object_are_rejected);
    return UNITY_END();
}
```

- [ ] **Step 3: Run them to see them fail**

Run: `pio test -e test -f test_settings_api`
Expected: compile error, `services/SettingsApi.h` not found.

- [ ] **Step 4: Write `SettingsApi.h`**

```cpp
#pragma once
// The web page's view of the settings registry (config spec §2–3): JSON out, with secrets never
// sent back, and JSON in, stored all or nothing.
#include <ArduinoJson.h>

#include "Settings.h"

namespace svc {

struct ApplyResult {
    bool ok = false;
    bool restart = false;  // a hardware setting changed: the board restarts after replying
};

// {"settings": [...]}, as Settings::toJsonSchema.
void schemaJson(const Settings& s, JsonDocument& out);
// Every value by key; a secret is {"set": true|false} instead of its text.
void valuesJson(const Settings& s, JsonDocument& out);
// `in` is {"key": value, ...}; values may be strings, numbers or booleans. For a secret, "" leaves
// it as it is and null clears it. Every value is checked first: on any error nothing is stored and
// `errors` gets {"key": "reason"} ("_" when `in` is not an object).
ApplyResult applyJson(Settings& s, JsonVariantConst in, JsonDocument& errors);

}  // namespace svc
```

- [ ] **Step 5: Write `SettingsApi.cpp`**

```cpp
#include "SettingsApi.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace svc {

namespace {

constexpr size_t kMaxText = 200;  // Settings::load reads at most 255 bytes back

// The text form Settings::set takes, or false with a reason. Unlike Settings::set, numbers out of
// range are refused, not clamped: the page shows the reason next to the field.
bool toText(const SettingDef& d, JsonVariantConst v, std::string& text, std::string& why) {
    if (v.isNull()) {
        if (d.secret) return text.clear(), true;
        return why = "missing value", false;
    }
    switch (d.type) {
        case SType::Bool:
            if (!v.is<bool>()) return why = "not true or false", false;
            text = v.as<bool>() ? "true" : "false";
            return true;
        case SType::Int:
        case SType::Float: {
            double x = 0;
            if (v.is<const char*>()) {
                const char* s = v.as<const char*>();
                char* end = nullptr;
                x = std::strtod(s, &end);
                if (end == s || *end) return why = "not a number", false;
            } else if (v.is<double>()) {
                x = v.as<double>();
            } else {
                return why = "not a number", false;
            }
            if (std::isnan(x)) return why = "not a number", false;
            if (d.type == SType::Int && x != std::floor(x)) return why = "not a whole number", false;
            if (x < d.min || x > d.max) {
                char b[48];
                std::snprintf(b, sizeof b, "out of range %g–%g", d.min, d.max);
                return why = b, false;
            }
            char b[32];
            std::snprintf(b, sizeof b, d.type == SType::Int ? "%.0f" : "%.6g", x);
            text = b;
            return true;
        }
        case SType::Enum: {
            if (!v.is<const char*>()) return why = "not an option", false;
            const char* s = v.as<const char*>();
            for (int k = 0; k < d.optionCount; ++k)
                if (!std::strcmp(s, d.options[k])) return text = s, true;
            return why = "not an option", false;
        }
        case SType::String:
            if (!v.is<const char*>()) return why = "not text", false;
            text = v.as<const char*>();
            if (text.size() > kMaxText) return why = "too long", false;
            return true;
    }
    return why = "unsupported", false;
}

}  // namespace

void schemaJson(const Settings& s, JsonDocument& out) { s.toJsonSchema(out); }

void valuesJson(const Settings& s, JsonDocument& out) {
    s.toJsonValues(out, false);
    for (int i = 0; i < s.count(); ++i) {
        const SettingDef& d = s.defs()[i];
        if (!d.secret) continue;
        JsonObject o = out[d.key].to<JsonObject>();
        o["set"] = !s.getString(d.key).empty();
    }
}

ApplyResult applyJson(Settings& s, JsonVariantConst in, JsonDocument& errors) {
    ApplyResult r;
    JsonObjectConst obj = in.as<JsonObjectConst>();
    if (obj.isNull()) {
        errors["_"] = "expected an object";
        return r;
    }
    struct Pending {
        const char* key;
        std::string text;
    };
    std::vector<Pending> todo;
    bool restart = false;
    for (JsonPairConst kv : obj) {
        const char* key = kv.key().c_str();
        const SettingDef* d = s.find(key);
        if (!d) {
            errors[key] = "unknown setting";
            continue;
        }
        std::string text, why;
        if (!toText(*d, kv.value(), text, why)) {
            errors[key] = why;
            continue;
        }
        if (d->secret && kv.value().is<const char*>() && text.empty()) continue;  // "" = keep it
        if (!std::strcmp(d->group, "hardware")) restart = true;
        todo.push_back({d->key, std::move(text)});
    }
    if (errors.size() > 0) return r;
    for (const Pending& p : todo) s.set(p.key, p.text.c_str());
    r.ok = true, r.restart = restart;
    return r;
}

}  // namespace svc
```

- [ ] **Step 6: Run the tests**

Run: `pio test -e test -f test_settings_api`
Expected: 8 tests pass.

- [ ] **Step 7: Run everything and commit**

Run: `pio test -e test` — all pass.

```bash
git add lib/core/services/Settings.h lib/core/services/SettingsApi.h lib/core/services/SettingsApi.cpp test/test_settings_api
git commit -m "feat(config): settings JSON API with all-or-nothing saves"
```

---

### Task 2: Hold gesture

**Files:**
- Modify: `lib/core/screens/Gesture.h`, `lib/core/screens/Gesture.cpp`
- Test: `test/test_gesture/test_main.cpp`

A press that stays within 12 px for 1 s fires `Gesture::Hold` while the finger is still down. From
0.3 s the ring shows, and from then on a release delivers nothing (no tap). Taps are shorter than
0.3 s in practice; swipes move more than 12 px, so neither changes.

- [ ] **Step 1: Write the failing tests**

In `test/test_gesture/test_main.cpp`, the existing `test_vertical_swipes_and_long_press` holds still
for 1.5 s inside `stroke()`, which asserts every pressed sample returns `None` — that now fires a
hold. Replace that test with:

```cpp
static void test_vertical_swipes() {
    GestureDetector g;
    TEST_ASSERT_EQUAL(int(Gesture::SwipeDown), int(stroke(g, 120, 40, 125, 260, 300).gesture));
    TEST_ASSERT_EQUAL(int(Gesture::SwipeUp), int(stroke(g, 120, 260, 118, 120, 250, 3000).gesture));
    TEST_ASSERT_EQUAL(int(Gesture::None), int(stroke(g, 100, 100, 160, 150, 300, 8000).gesture));  // diagonal
}

// Held still: Hold fires once, at 1 s, while the finger is down; the release then gives nothing.
static void test_hold_fires_at_one_second() {
    GestureDetector g;
    int holds = 0;
    uint32_t t = 1000;
    for (; t <= 2200; t += 20) {
        scr::GestureEvent ev = g.feed(true, 100 + (t / 20) % 2, 200, t);  // a pixel of jitter
        if (ev.gesture == Gesture::Hold) {
            ++holds;
            TEST_ASSERT_UINT32_WITHIN(20, 2000, t);
            TEST_ASSERT_EQUAL(100, ev.x);
            TEST_ASSERT_EQUAL(200, ev.y);
        } else {
            TEST_ASSERT_EQUAL(int(Gesture::None), int(ev.gesture));
        }
    }
    TEST_ASSERT_EQUAL(1, holds);
    TEST_ASSERT_EQUAL(int(Gesture::None), int(g.feed(false, 0, 0, t).gesture));
}

static void test_hold_progress_shows_from_0_3_s() {
    GestureDetector g;
    g.feed(true, 50, 50, 1000);
    g.feed(true, 50, 50, 1200);
    TEST_ASSERT_EQUAL_FLOAT(0, g.holdProgress(1200));
    g.feed(true, 51, 50, 1300);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.3f, g.holdProgress(1300));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.6f, g.holdProgress(1600));
    TEST_ASSERT_EQUAL(50, g.pressX());
    TEST_ASSERT_EQUAL(50, g.pressY());
    g.feed(false, 0, 0, 1620);
    TEST_ASSERT_EQUAL_FLOAT(0, g.holdProgress(1640));
}

static void test_moving_cancels_a_hold() {
    GestureDetector g;
    for (uint32_t t = 0; t <= 400; t += 20) g.feed(true, 100 + int(t / 10), 200, 1000 + t);  // drifts 40 px
    TEST_ASSERT_EQUAL_FLOAT(0, g.holdProgress(1400));
    for (uint32_t t = 420; t <= 1600; t += 20)
        TEST_ASSERT_EQUAL(int(Gesture::None), int(g.feed(true, 140, 200, 1000 + t).gesture));
}

static void test_release_once_the_ring_shows_is_not_a_tap() {
    GestureDetector g;
    for (uint32_t t = 0; t <= 400; t += 20) g.feed(true, 100, 200, 1000 + t);
    TEST_ASSERT_EQUAL(int(Gesture::None), int(g.feed(false, 0, 0, 1420).gesture));
}
```

And make `main()`:

```cpp
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_tap);
    RUN_TEST(test_single_noise_sample_is_not_a_tap);
    RUN_TEST(test_swipes);
    RUN_TEST(test_vertical_swipes);
    RUN_TEST(test_hold_fires_at_one_second);
    RUN_TEST(test_hold_progress_shows_from_0_3_s);
    RUN_TEST(test_moving_cancels_a_hold);
    RUN_TEST(test_release_once_the_ring_shows_is_not_a_tap);
    return UNITY_END();
}
```

- [ ] **Step 2: Run them to see them fail**

Run: `pio test -e test -f test_gesture`
Expected: compile errors (`Gesture::Hold`, `holdProgress`, `pressX` unknown).

- [ ] **Step 3: Implement**

`lib/core/screens/Gesture.h` — replace the file with:

```cpp
#pragma once
// Turns raw touch samples into taps and swipes (horizontal: screens; vertical: within a screen),
// and a hold (config spec §5) that opens the touch settings. Resistive panels jitter a few pixels,
// so a tap tolerates small movement; taps and swipes fire on release, a hold while still pressed.
#include <cstdint>

namespace scr {

enum class Gesture : uint8_t { None, Tap, SwipeLeft, SwipeRight, SwipeUp, SwipeDown, Hold };

struct GestureEvent {
    Gesture gesture = Gesture::None;
    int x = 0, y = 0;  // where the press started
};

class GestureDetector {
   public:
    static constexpr int kTapSlop = 14, kSwipeMin = 50, kHoldSlop = 12;
    static constexpr int kTapMinSamples = 2;  // a lone sample is usually panel noise
    static constexpr uint32_t kTapMaxMs = 600, kSwipeMaxMs = 900;
    // The ring shows from kHoldShowMs and the hold fires at kHoldMs; once the ring shows, a
    // release is nothing, not a tap.
    static constexpr uint32_t kHoldShowMs = 300, kHoldMs = 1000;
    GestureEvent feed(bool pressed, int x, int y, uint32_t nowMs);
    // 0 when no hold is filling (not pressed, moved, already fired, or under kHoldShowMs), else the
    // fraction of kHoldMs held.
    float holdProgress(uint32_t nowMs) const;
    int pressX() const { return x0_; }
    int pressY() const { return y0_; }

   private:
    bool down_ = false, held_ = false;
    int x0_ = 0, y0_ = 0, x_ = 0, y_ = 0, maxMove_ = 0, samples_ = 0;
    uint32_t t0_ = 0;
};

}  // namespace scr
```

`lib/core/screens/Gesture.cpp` — replace the file with:

```cpp
#include "Gesture.h"

#include <cstdlib>

namespace scr {

GestureEvent GestureDetector::feed(bool pressed, int x, int y, uint32_t now) {
    if (pressed) {
        if (!down_) down_ = true, held_ = false, x0_ = x_ = x, y0_ = y_ = y, maxMove_ = 0, samples_ = 0, t0_ = now;
        x_ = x, y_ = y, ++samples_;
        int m = std::abs(x - x0_) + std::abs(y - y0_);
        if (m > maxMove_) maxMove_ = m;
        if (!held_ && maxMove_ <= kHoldSlop && now - t0_ >= kHoldMs) {
            held_ = true;
            GestureEvent ev;
            ev.gesture = Gesture::Hold, ev.x = x0_, ev.y = y0_;
            return ev;
        }
        return {};
    }
    if (!down_) return {};
    down_ = false;
    uint32_t dur = now - t0_;
    if (held_ || (maxMove_ <= kHoldSlop && dur >= kHoldShowMs)) return {};  // the ring showed
    int dx = x_ - x0_, dy = y_ - y0_;
    GestureEvent ev;
    ev.x = x0_, ev.y = y0_;
    if (std::abs(dx) >= kSwipeMin && std::abs(dx) > 2 * std::abs(dy) && dur <= kSwipeMaxMs)
        ev.gesture = dx < 0 ? Gesture::SwipeLeft : Gesture::SwipeRight;
    else if (std::abs(dy) >= kSwipeMin && std::abs(dy) > 2 * std::abs(dx) && dur <= kSwipeMaxMs)
        ev.gesture = dy < 0 ? Gesture::SwipeUp : Gesture::SwipeDown;
    else if (maxMove_ <= kTapSlop && dur <= kTapMaxMs && samples_ >= kTapMinSamples)
        ev.gesture = Gesture::Tap;
    return ev;
}

float GestureDetector::holdProgress(uint32_t now) const {
    if (!down_ || held_ || maxMove_ > kHoldSlop) return 0;
    const uint32_t t = now - t0_;
    if (t < kHoldShowMs) return 0;
    return t >= kHoldMs ? 1.f : float(t) / float(kHoldMs);
}

}  // namespace scr
```

`ScreenApp::touch` passes every non-`None` gesture on; until Task 5 a `Hold` must be ignored there.
In `lib/core/screens/ScreenApp.cpp`, change the first line after `feed` from
`if (ev.gesture == Gesture::None) return;` to:

```cpp
    if (ev.gesture == Gesture::None || ev.gesture == Gesture::Hold) return;  // Hold: Task 5 opens the panel
```

- [ ] **Step 4: Run the tests**

Run: `pio test -e test -f test_gesture` — 8 pass. Then `pio test -e test` — all pass. If another
suite fails because it feeds a still press of 0.3 s or longer and expects a tap, shorten that press
to under 0.3 s (that is the spec change, not a regression) and say so in your report.

- [ ] **Step 5: Commit**

```bash
git add lib/core/screens/Gesture.h lib/core/screens/Gesture.cpp lib/core/screens/ScreenApp.cpp test/test_gesture
git commit -m "feat(config): hold gesture with progress for the ring"
```

---

### Task 3: SettingsScreen (the touch panel)

**Files:**
- Modify: `lib/core/screens/Screen.h`, `lib/core/screens/ScreenApp.h`, `lib/core/screens/ScreenApp.cpp`
- Create: `lib/core/screens/SettingsScreen.h`, `lib/core/screens/SettingsScreen.cpp`
- Test: `test/test_config_screens/test_main.cpp`

Layout (240×320, from the mockup): header with "Settings" and a Done button; four cards —
brightness slider, theme (two options), radius (− value +, 5 nm steps), screens (three options);
footer "More at contrail.local". Cinematic: rounded cards, accent outline on the chosen option.
Editorial (`theme.flat`): square fills, the chosen option white with dark text, capitals.

- [ ] **Step 1: Add the screen ids**

In `lib/core/screens/Screen.h`:

```cpp
enum class ScreenId : uint8_t { Closest, Radar, Board, Sky, Map, Stats, Alert, Settings, Update, Count };
```

(`Director` uses `ScreenId::Count` as "none"; it stays last.)

- [ ] **Step 2: Write `SettingsScreen.h`**

```cpp
#pragma once
// The touch settings panel (config spec §5, mockup "Touch settings"): brightness, theme, radius and
// screen mode. Opened by holding the screen for 1 s (ScreenApp); outside the swipe order. A tap
// applies at once; the slider follows the finger and saves on release. Edits queue here for the
// platform, which saves them off the render thread.
#include "Director.h"
#include "Screen.h"
#include "Theme.h"

namespace scr {

// What the panel shows; the platform passes it from the settings (app::panelValues).
struct PanelValues {
    int brightness = 200;  // the manual level, 5..255
    bool editorial = false;
    int radiusNm = 25;
    DirectorMode mode = DirectorMode::Auto;
};

// One setting to save: a registry key (svc::key) and the text form Settings::set takes.
struct SettingEdit {
    const char* key = nullptr;
    char value[12] = {};
};

class SettingsView : public gfx::Layer {
   public:
    static constexpr gfx::Rect kDone{170, 10, 58, 30};
    static constexpr gfx::Rect kBrightCard{12, 50, 216, 58};
    static constexpr gfx::Rect kThemeCard{12, 114, 216, 58};
    static constexpr gfx::Rect kRadiusCard{12, 178, 216, 52};
    static constexpr gfx::Rect kModeCard{12, 236, 216, 58};
    static constexpr int kSliderX0 = 26, kSliderX1 = 214, kSliderY = 92;
    static constexpr gfx::Rect kSliderHit{12, 76, 216, 32};  // where a press grabs the slider
    static constexpr gfx::Rect kTheme[2] = {{22, 140, 96, 26}, {122, 140, 96, 26}};
    static constexpr gfx::Rect kMinus{88, 187, 34, 34}, kPlus{184, 187, 34, 34};
    static constexpr gfx::Rect kMode[3] = {{22, 262, 62, 26}, {89, 262, 62, 26}, {156, 262, 62, 26}};

    explicit SettingsView(const Theme& t) : t_(t) {}
    void reset() { full_ = true; }
    void setValues(const PanelValues& v);  // redraws the cards that changed
    const PanelValues& values() const { return v_; }
    static int brightnessAt(int x);  // the level for a slider position

    void update(float dt, gfx::DamageList& damage) override;
    void draw(gfx::Canvas& c) override;

   private:
    void dirty(const gfx::Rect& r);
    const char* cap(const char* mixed, const char* upper) const { return t_.upperHeads ? upper : mixed; }
    const gfx::FontData& labelFont() const;
    void drawHeader(gfx::Canvas& c) const;
    void cardBg(gfx::Canvas& c, const gfx::Rect& r) const;
    void rowLabel(gfx::Canvas& c, const gfx::Rect& card, int baseline, const char* mixed, const char* upper) const;
    void option(gfx::Canvas& c, const gfx::Rect& r, const char* label, bool on) const;

    const Theme& t_;
    PanelValues v_;
    bool full_ = true;
    gfx::Rect dirty_[4];
    int nDirty_ = 0;
};

class SettingsScreen : public Screen {
   public:
    static constexpr uint32_t kIdleCloseMs = 30000;
    static constexpr int kRadiusMin = 5, kRadiusMax = 100, kRadiusStep = 5;
    explicit SettingsScreen(const Theme& t) : view_(t) {}
    ScreenId id() const override { return ScreenId::Settings; }
    void setScene(const Scene*) override {}
    void enter() override { view_.reset(); }
    int layerCount() const override { return 1; }
    gfx::Layer* const* layers() override { return layers_; }

    // Opened at nowMs by a hold whose finger is still down: touches count from its release.
    void open(uint32_t nowMs);
    // The settings changed elsewhere (or a save landed). Ignored while the slider is held.
    void setValues(const PanelValues& v);
    const PanelValues& values() const { return view_.values(); }
    // Every raw touch sample while the panel shows: the slider follows the finger.
    void raw(bool pressed, int x, int y, uint32_t nowMs);
    NavRequest onTap(int x, int y) override;
    bool takeEdit(SettingEdit& out);  // oldest first
    bool wantsClose(uint32_t nowMs) const { return close_ || nowMs - lastTouch_ >= kIdleCloseMs; }
    // The level to drive the backlight with while the slider is held, else -1.
    int previewBrightness() const { return dragging_ ? view_.values().brightness : -1; }

   private:
    static constexpr int kMaxEdits = 6;
    void push(const char* key, const char* value);
    void pushInt(const char* key, int v);

    SettingsView view_;
    gfx::Layer* layers_[1] = {&view_};
    SettingEdit edits_[kMaxEdits];
    int nEdits_ = 0;
    bool armed_ = true, down_ = false, dragging_ = false, close_ = false;
    uint32_t lastTouch_ = 0;
};

}  // namespace scr
```

- [ ] **Step 3: Write `SettingsScreen.cpp`**

```cpp
#include "SettingsScreen.h"

#include <cstdio>
#include <cstring>

#include "gfx/Font.h"
#include "gfx/Raster.h"
#include "gfx/fonts/Fonts.h"
#include "services/CoreSettings.h"
#include "ui/Widgets.h"

namespace scr {

namespace {

constexpr int kBrightMin = 5, kBrightMax = 255;
const DirectorMode kModes[] = {DirectorMode::Auto, DirectorMode::Carousel, DirectorMode::Stay};
const char* const kModeKeys[] = {"director", "carousel", "stay"};  // the screen_mode options
const char* const kModeLabels[] = {"Director", "Carousel", "Stay"};
const char* const kModeCaps[] = {"DIRECTOR", "CAROUSEL", "STAY"};

// Hit test with a little slack for the resistive panel's jitter (gaps between options are ≥ 4 px).
bool hit(const gfx::Rect& r, int x, int y) {
    constexpr int s = 2;
    return x >= r.x - s && x < r.right() + s && y >= r.y - s && y < r.bottom() + s;
}
bool inside(const gfx::Rect& r, int x, int y) { return x >= r.x && x < r.right() && y >= r.y && y < r.bottom(); }
// The baseline that centres one line of `f` in `r`.
int midline(const gfx::FontData& f, const gfx::Rect& r) { return r.y + (r.h + f.ascent - f.descent) / 2; }

void outline(gfx::Canvas& c, const gfx::Rect& r, gfx::Rgb col) {
    gfx::fillRoundRect(c, {r.x, r.y, r.w, 1}, 0, col);
    gfx::fillRoundRect(c, {r.x, r.bottom() - 1, r.w, 1}, 0, col);
    gfx::fillRoundRect(c, {r.x, r.y, 1, r.h}, 0, col);
    gfx::fillRoundRect(c, {r.right() - 1, r.y, 1, r.h}, 0, col);
}

}  // namespace

// ------------------------------------------------------------ SettingsView

int SettingsView::brightnessAt(int x) {
    float f = float(x - kSliderX0) / float(kSliderX1 - kSliderX0);
    f = f < 0 ? 0 : f > 1 ? 1 : f;
    return kBrightMin + int(f * (kBrightMax - kBrightMin) + 0.5f);
}

void SettingsView::setValues(const PanelValues& v) {
    if (v.brightness != v_.brightness) dirty(kBrightCard);
    if (v.editorial != v_.editorial) dirty(kThemeCard);
    if (v.radiusNm != v_.radiusNm) dirty(kRadiusCard);
    if (v.mode != v_.mode) dirty(kModeCard);
    v_ = v;
}

void SettingsView::dirty(const gfx::Rect& r) {
    for (int i = 0; i < nDirty_; ++i)
        if (dirty_[i] == r) return;
    if (nDirty_ < 4) dirty_[nDirty_++] = r;
}

void SettingsView::update(float, gfx::DamageList& damage) {
    if (full_) damage.addAll();
    else
        for (int i = 0; i < nDirty_; ++i) damage.add(dirty_[i]);
    full_ = false, nDirty_ = 0;
}

const gfx::FontData& SettingsView::labelFont() const { return t_.upperHeads ? *t_.fontLabel : gfx::kFontSmall; }

void SettingsView::drawHeader(gfx::Canvas& c) const {
    gfx::drawText(c, *t_.fontTitle, 12, 33, cap("Settings", "SETTINGS"), t_.text);
    gfx::fillRoundRect(c, kDone, t_.flat ? 0 : 15, t_.accent);
    const gfx::FontData& f = t_.upperHeads ? *t_.fontLabel : gfx::kFontBold;
    gfx::drawText(c, f, kDone.x + kDone.w / 2, midline(f, kDone), cap("Done", "DONE"), t_.boardBgTop,
                  gfx::Align::Center);
    if (t_.flat) gfx::fillRoundRect(c, {12, 44, 216, 2}, 0, t_.accent);
}

void SettingsView::cardBg(gfx::Canvas& c, const gfx::Rect& r) const {
    if (t_.flat) gfx::fillRoundRect(c, r, 0, t_.rowFill);
    else ui::card(c, r, 10, t_.rowFill, t_.cardEdge);
}

void SettingsView::rowLabel(gfx::Canvas& c, const gfx::Rect& card, int baseline, const char* mixed,
                            const char* upper) const {
    gfx::drawText(c, labelFont(), card.x + 10, baseline, cap(mixed, upper), t_.upperHeads ? t_.textDim : t_.textSoft);
}

void SettingsView::option(gfx::Canvas& c, const gfx::Rect& r, const char* label, bool on) const {
    if (t_.flat) {
        if (on) gfx::fillRoundRect(c, r, 0, t_.text);
        else outline(c, r, t_.cardEdge);
    } else {
        gfx::fillRoundRect(c, r, 8, on ? t_.accent : t_.cardEdge);
        const gfx::Rect in{r.x + 1, r.y + 1, r.w - 2, r.h - 2};
        gfx::fillRoundRect(c, in, 7, t_.rowFill);
        if (on) gfx::fillRoundRect(c, in, 7, t_.accent, 36);
    }
    const gfx::FontData& f = t_.upperHeads ? *t_.fontLabel : on ? gfx::kFontBold : gfx::kFontSmall;
    const gfx::Rgb ink = on ? (t_.flat ? t_.boardBgTop : t_.text) : t_.textSoft;
    gfx::drawText(c, f, r.x + r.w / 2, midline(f, r), label, ink, gfx::Align::Center);
}

void SettingsView::draw(gfx::Canvas& c) {
    gfx::fillRoundRect(c, c.clip(), 0, t_.boardBgTop);
    char buf[16];
    if (c.clip().intersects({0, 0, 240, 47})) drawHeader(c);
    if (c.clip().intersects(kBrightCard)) {
        cardBg(c, kBrightCard);
        rowLabel(c, kBrightCard, kBrightCard.y + 18, "Brightness", "BRIGHTNESS");
        std::snprintf(buf, sizeof buf, "%d%%", (v_.brightness * 100 + 127) / 255);
        gfx::drawText(c, *t_.fontLabel, kBrightCard.right() - 10, kBrightCard.y + 18, buf, t_.text, gfx::Align::Right);
        const float frac = float(v_.brightness - kBrightMin) / float(kBrightMax - kBrightMin);
        ui::progressBar(c, kSliderX0, kSliderY, kSliderX1 - kSliderX0, frac, t_.cardEdge, t_.accent, t_.flat);
        const float kx = kSliderX0 + frac * (kSliderX1 - kSliderX0);
        if (t_.flat) gfx::fillRoundRect(c, {int(kx) - 4, kSliderY - 10, 8, 20}, 0, t_.text);
        else gfx::aaCircle(c, kx, float(kSliderY), 9, t_.text);
    }
    if (c.clip().intersects(kThemeCard)) {
        cardBg(c, kThemeCard);
        rowLabel(c, kThemeCard, kThemeCard.y + 18, "Theme", "THEME");
        option(c, kTheme[0], cap("Cinematic", "CINEMATIC"), !v_.editorial);
        option(c, kTheme[1], cap("Editorial", "EDITORIAL"), v_.editorial);
    }
    if (c.clip().intersects(kRadiusCard)) {
        cardBg(c, kRadiusCard);
        rowLabel(c, kRadiusCard, midline(labelFont(), kRadiusCard), "Radius", "RADIUS");
        option(c, kMinus, "-", false);
        option(c, kPlus, "+", false);
        std::snprintf(buf, sizeof buf, t_.upperHeads ? "%d NM" : "%d nm", v_.radiusNm);
        gfx::drawText(c, *t_.fontLabel, (kMinus.right() + kPlus.x) / 2, midline(*t_.fontLabel, kRadiusCard), buf,
                      t_.text, gfx::Align::Center);
    }
    if (c.clip().intersects(kModeCard)) {
        cardBg(c, kModeCard);
        rowLabel(c, kModeCard, kModeCard.y + 18, "Screens", "SCREENS");
        for (int i = 0; i < 3; ++i)
            option(c, kMode[i], t_.upperHeads ? kModeCaps[i] : kModeLabels[i], v_.mode == kModes[i]);
    }
    gfx::drawText(c, labelFont(), 120, 312, cap("More at contrail.local", "MORE AT CONTRAIL.LOCAL"), t_.textDim,
                  gfx::Align::Center);
}

// ------------------------------------------------------------ SettingsScreen

void SettingsScreen::open(uint32_t now) {
    armed_ = false, down_ = true, dragging_ = false, close_ = false, lastTouch_ = now;
}

void SettingsScreen::setValues(const PanelValues& v) {
    if (!dragging_) view_.setValues(v);
}

void SettingsScreen::raw(bool pressed, int x, int y, uint32_t now) {
    const bool start = pressed && !down_;
    down_ = pressed;
    if (!pressed) {
        if (dragging_) {
            dragging_ = false;
            push(svc::key::kBrightMode, "manual");
            pushInt(svc::key::kBrightness, view_.values().brightness);
        }
        armed_ = true;
        return;
    }
    lastTouch_ = now;
    if (!armed_) return;
    if (start) dragging_ = inside(SettingsView::kSliderHit, x, y);
    if (dragging_) {
        PanelValues v = view_.values();
        v.brightness = SettingsView::brightnessAt(x);
        view_.setValues(v);
    }
}

NavRequest SettingsScreen::onTap(int x, int y) {
    if (!armed_) return {};
    PanelValues v = view_.values();
    if (hit(SettingsView::kDone, x, y)) close_ = true;
    for (int i = 0; i < 2; ++i)
        if (hit(SettingsView::kTheme[i], x, y)) {
            v.editorial = i == 1;
            push(svc::key::kTheme, i ? "editorial" : "cinematic");
        }
    const bool up = hit(SettingsView::kPlus, x, y);
    if (up || hit(SettingsView::kMinus, x, y)) {
        int r = up ? (v.radiusNm + kRadiusStep) / kRadiusStep * kRadiusStep
                   : (v.radiusNm - 1) / kRadiusStep * kRadiusStep;  // to the next multiple of 5
        v.radiusNm = r < kRadiusMin ? kRadiusMin : r > kRadiusMax ? kRadiusMax : r;
        pushInt(svc::key::kRadius, v.radiusNm);
    }
    for (int i = 0; i < 3; ++i)
        if (hit(SettingsView::kMode[i], x, y)) {
            v.mode = kModes[i];
            push(svc::key::kScreenMode, kModeKeys[i]);
        }
    view_.setValues(v);
    return {};
}

void SettingsScreen::push(const char* key, const char* value) {
    int i = 0;
    while (i < nEdits_ && std::strcmp(edits_[i].key, key)) ++i;  // a newer value for a queued key replaces it
    if (i == nEdits_) {
        if (nEdits_ == kMaxEdits) return;
        ++nEdits_;
    }
    edits_[i].key = key;
    std::snprintf(edits_[i].value, sizeof edits_[i].value, "%s", value);
}

void SettingsScreen::pushInt(const char* key, int v) {
    char b[12];
    std::snprintf(b, sizeof b, "%d", v);
    push(key, b);
}

bool SettingsScreen::takeEdit(SettingEdit& out) {
    if (!nEdits_) return false;
    out = edits_[0];
    for (int i = 1; i < nEdits_; ++i) edits_[i - 1] = edits_[i];
    --nEdits_;
    return true;
}

}  // namespace scr
```

- [ ] **Step 4: Register it in `ScreenApp`**

`lib/core/screens/ScreenApp.h`: add `#include "SettingsScreen.h"` with the other screen includes;
after `AlertScreen alert_;` add `SettingsScreen settings_;`; change `kScreens = 7` to `8`; add to the
public section:

```cpp
    // The touch panel's values (config spec §5); the platform passes them from the settings.
    void setPanelValues(const PanelValues& v) { settings_.setValues(v); }
```

`lib/core/screens/ScreenApp.cpp` constructor: add `settings_(theme_)` after `alert_(theme_)`, and
`&settings_` at the end of `screens_{...}`.

- [ ] **Step 5: Write the golden tests**

Create `test/test_config_screens/test_main.cpp`:

```cpp
#include <unity.h>

#include <cstdlib>
#include <ctime>

#include "GoldenRender.h"
#include "screens/DemoScenes.h"

using scr::ScreenId;

void setUp() {
    setenv("TZ", "UTC0", 1);
    tzset();
}
void tearDown() {}

static void panelGolden(const scr::Theme& t, bool editorial, const char* name) {
    golden::Rig g(t);
    scr::sceneQuiet(g.app->scene());
    g.app->sceneUpdated();
    scr::PanelValues v;
    v.editorial = editorial;
    g.app->setPanelValues(v);
    g.show(ScreenId::Settings);
    g.frames(10);
    assertGolden(name, g.d.px, 240, 320);
    g.assertNoStale();
}

static void test_panel_goldens() {
    panelGolden(scr::cinematicSky(), false, "settings_panel");
    panelGolden(scr::editorial(), true, "ed_settings_panel");
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_panel_goldens);
    return UNITY_END();
}
```

- [ ] **Step 6: Generate and inspect the goldens**

Run: `UPDATE_GOLDEN=1 pio test -e test -f test_config_screens`, then convert both PPMs to PNG with
`sips` and look at them. Check: nothing clipped or overlapping; "Settings"/"SETTINGS" and Done on
one line; the chosen theme and "Director" highlighted; "78%" and "25 nm" shown; footer readable.
Adjust the layout constants in `SettingsView` if needed and regenerate. Then run
`pio test -e test -f test_config_screens` without the variable — passes.

- [ ] **Step 7: Run everything and commit**

Run: `pio test -e test` — all pass.

```bash
git add lib/core/screens test/test_config_screens test/golden/settings_panel.ppm test/golden/ed_settings_panel.ppm
git commit -m "feat(config): touch settings panel screen"
```

---

### Task 4: UpdateScreen

**Files:**
- Create: `lib/core/screens/UpdateScreen.h`, `lib/core/screens/UpdateScreen.cpp`
- Modify: `lib/core/screens/ScreenApp.h`, `lib/core/screens/ScreenApp.cpp`
- Test: `test/test_config_screens/test_main.cpp`

- [ ] **Step 1: Write `UpdateScreen.h`**

```cpp
#pragma once
// Full-screen progress while a firmware upload runs (config spec §6, mockup "Firmware update on
// the device"). Only the percentage and bar redraw as it climbs.
#include "Screen.h"
#include "Theme.h"

namespace scr {

class UpdateView : public gfx::Layer {
   public:
    static constexpr gfx::Rect kLive{0, 158, 240, 76};  // the percentage and the bar
    explicit UpdateView(const Theme& t) : t_(t) {}
    void reset() { full_ = true; }
    void setPercent(int p) {
        if (p != pct_) pct_ = p, dirty_ = true;
    }
    int percent() const { return pct_; }
    void update(float dt, gfx::DamageList& damage) override;
    void draw(gfx::Canvas& c) override;

   private:
    const Theme& t_;
    int pct_ = 0;
    bool full_ = true, dirty_ = false;
};

class UpdateScreen : public Screen {
   public:
    explicit UpdateScreen(const Theme& t) : view_(t) {}
    ScreenId id() const override { return ScreenId::Update; }
    void setScene(const Scene*) override {}
    void enter() override { view_.reset(); }
    void setPercent(int p) { view_.setPercent(p); }
    int layerCount() const override { return 1; }
    gfx::Layer* const* layers() override { return layers_; }

   private:
    UpdateView view_;
    gfx::Layer* layers_[1] = {&view_};
};

}  // namespace scr
```

- [ ] **Step 2: Write `UpdateScreen.cpp`**

```cpp
#include "UpdateScreen.h"

#include <cstdio>

#include "gfx/Font.h"
#include "gfx/Raster.h"
#include "gfx/fonts/Fonts.h"
#include "ui/Widgets.h"

namespace scr {

void UpdateView::update(float, gfx::DamageList& damage) {
    if (full_) damage.addAll();
    else if (dirty_) damage.add(kLive);
    full_ = dirty_ = false;
}

void UpdateView::draw(gfx::Canvas& c) {
    gfx::fillRoundRect(c, c.clip(), 0, t_.boardBgTop);
    const bool caps = t_.upperHeads;
    gfx::drawText(c, gfx::kFontSmall, 120, 118, "CONTRAIL", t_.textDim, gfx::Align::Center);
    gfx::drawText(c, *t_.fontTitle, 120, 150, caps ? "UPDATING" : "Updating", t_.text, gfx::Align::Center);
    char buf[8];
    std::snprintf(buf, sizeof buf, "%d%%", pct_);
    gfx::drawText(c, *t_.fontDisplay, 120, 205, buf, t_.accent, gfx::Align::Center);
    ui::progressBar(c, 30, 226, 180, pct_ / 100.f, t_.cardEdge, t_.accent, t_.flat);
    gfx::drawText(c, gfx::kFontSmall, 120, 258,
                  caps ? "DON'T UNPLUG. RESTARTS WHEN DONE." : "Don't unplug. Restarts when done.", t_.textSoft,
                  gfx::Align::Center);
}

}  // namespace scr
```

If the golden in Step 5 shows the `%` missing from the display font, use `*t_.fontTitle` for the
percentage instead.

- [ ] **Step 3: Add update mode to `ScreenApp`**

`ScreenApp.h`: `#include "UpdateScreen.h"`; member `UpdateScreen update_;` after `settings_`;
`kScreens = 9`; private members `bool updating_ = false; ScreenId beforeUpdate_ = ScreenId::Radar;`;
public:

```cpp
    // A firmware upload is running at `percent` (0..100), or none (-1): the Update screen covers
    // everything and touch is ignored. Called every frame by the platform; cheap when unchanged.
    void setUpdateProgress(int percent);
    bool updating() const { return updating_; }
```

`ScreenApp.cpp`: constructor `update_(theme_)` after `settings_(theme_)`, `&update_` at the end of
`screens_{...}`. Add:

```cpp
void ScreenApp::setUpdateProgress(int pct) {
    if (pct >= 0) {
        if (!updating_) updating_ = true, beforeUpdate_ = host_.target();
        update_.setPercent(pct);
        host_.show(ScreenId::Update);
    } else if (updating_) {
        updating_ = false;
        host_.show(beforeUpdate_);
    }
}
```

At the top of `tick()`, after the alert-engine line, add:

```cpp
    if (updating_) {  // the upload's screen owns the display; the director waits
        dots_.set(false, 0, kOrderN);
        badge_.set(false);
        return;
    }
```

At the top of `touch()`: `if (updating_) return;`

- [ ] **Step 4: Add the golden tests**

In `test/test_config_screens/test_main.cpp` add:

```cpp
static void updateGolden(const scr::Theme& t, const char* name) {
    golden::Rig g(t);
    scr::sceneQuiet(g.app->scene());
    g.app->sceneUpdated();
    g.app->setUpdateProgress(62);
    g.app->tick(1000);
    g.frames(30);  // through the fade
    TEST_ASSERT_EQUAL(int(ScreenId::Update), int(g.app->host().current()));
    assertGolden(name, g.d.px, 240, 320);
    g.assertNoStale();
}

static void test_update_goldens() {
    updateGolden(scr::cinematicSky(), "update_screen");
    updateGolden(scr::editorial(), "ed_update_screen");
}

// Only the percentage and bar redraw as the upload climbs; ending it returns to the screen before.
static void test_update_progress_redraws_little_and_returns() {
    golden::Rig g;
    scr::sceneQuiet(g.app->scene());
    g.app->sceneUpdated();
    g.show(ScreenId::Board);
    g.app->configure(scr::DirectorMode::Stay, 30000);
    g.app->tick(1000);
    g.app->setUpdateProgress(10);
    g.app->tick(1033);
    g.frames(30);
    g.app->setUpdateProgress(11);
    TEST_ASSERT_LESS_OR_EQUAL(240 * 80, g.pushed());
    g.app->touch(true, 120, 160, 2000);  // ignored
    g.app->setUpdateProgress(-1);
    g.app->tick(2100);
    g.frames(30);
    TEST_ASSERT_EQUAL(int(ScreenId::Board), int(g.app->host().current()));
}
```

and `RUN_TEST(test_update_goldens); RUN_TEST(test_update_progress_redraws_little_and_returns);`.

- [ ] **Step 5: Generate, inspect, run, commit**

`UPDATE_GOLDEN=1 pio test -e test -f test_config_screens`; inspect `update_screen` and
`ed_update_screen` PNGs (centred text, "62%", bar at 62%). Then `pio test -e test` — all pass.

```bash
git add lib/core/screens test/test_config_screens test/golden/update_screen.ppm test/golden/ed_update_screen.ppm
git commit -m "feat(config): firmware update progress screen"
```

---

### Task 5: Hold ring and the panel in ScreenApp

**Files:**
- Modify: `lib/core/screens/Overlays.h`, `lib/core/screens/Overlays.cpp`,
  `lib/core/screens/ScreenHost.h`, `lib/core/screens/ScreenApp.h`, `lib/core/screens/ScreenApp.cpp`
- Test: `test/test_config_screens/test_main.cpp`

Behaviour (spec §5): hold → ring over the current screen → at 1 s the Settings screen. While it is
open: raw samples go to the panel (slider), taps go to it, nothing else reacts, the director does
not run. It closes on Done or 30 s idle and returns to the screen the hold started on. An emergency
alert closes it at once; a non-emergency alert shows after it closes. An upload closes it too.

- [ ] **Step 1: Add `HoldRing` to `Overlays.h`**

Append inside `namespace scr`:

```cpp
// The ring that fills while a finger holds still (config spec §5), centred on the press but kept
// clear of the edges, with a hint under it (above it near the bottom).
class HoldRing : public gfx::Layer {
   public:
    static constexpr int kR = 36, kHalf = 46;  // ring radius; half the box around it
    explicit HoldRing(const Theme& t) : t_(t) {}
    void set(bool visible, int x, int y, float progress);
    bool visible() const { return visible_; }
    void update(float, gfx::DamageList& d) override;
    void draw(gfx::Canvas& c) override;

   private:
    int hintBaseline() const;
    gfx::Rect box() const;
    const Theme& t_;
    bool visible_ = false, dirty_ = false;
    int cx_ = 0, cy_ = 0;
    int step_ = 0;       // progress drawn, in 64ths of a turn
    gfx::Rect drawn_{};  // what the last drawn ring covered
};
```

- [ ] **Step 2: Implement it in `Overlays.cpp`**

Append inside `namespace scr` (add `#include "gfx/Canvas.h"` only if `Rect::unite` is not already
visible through the existing includes):

```cpp
void HoldRing::set(bool visible, int x, int y, float progress) {
    const int cx = x < kHalf ? kHalf : x > 240 - kHalf ? 240 - kHalf : x;
    const int cy = y < kHalf ? kHalf : y > 320 - kHalf ? 320 - kHalf : y;
    const int step = visible ? int(progress * 64) : 0;
    if (visible != visible_ || (visible && (step != step_ || cx != cx_ || cy != cy_))) dirty_ = true;
    visible_ = visible, cx_ = cx, cy_ = cy, step_ = step;
}

int HoldRing::hintBaseline() const { return cy_ + kHalf + 14 <= 314 ? cy_ + kHalf + 14 : cy_ - kHalf - 6; }

gfx::Rect HoldRing::box() const {
    const gfx::Rect ring{cx_ - kHalf, cy_ - kHalf, 2 * kHalf, 2 * kHalf};
    const gfx::Rect hint{0, hintBaseline() - 15, 240, 20};
    return ring.unite(hint);
}

void HoldRing::update(float, gfx::DamageList& d) {
    if (!dirty_) return;
    dirty_ = false;
    if (!drawn_.empty()) d.add(drawn_);
    drawn_ = visible_ ? box() : gfx::Rect{};
    if (!drawn_.empty()) d.add(drawn_);
}

void HoldRing::draw(gfx::Canvas& c) {
    if (!visible_ || !c.clip().intersects(drawn_)) return;
    const float x = float(cx_), y = float(cy_);
    gfx::aaCircle(c, x, y, kHalf - 2, t_.shadow, 170);
    gfx::aaRing(c, x, y, kR, 6, t_.text, 50);
    gfx::aaArc(c, x, y, kR, 6, 0, 360.f * step_ / 64.f, t_.accent);
    gfx::aaCircle(c, x, y, 12, t_.text, 90);
    const gfx::FontData& f = t_.upperHeads ? *t_.fontLabel : gfx::kFontBold;
    ui::shadowText(c, f, 120, hintBaseline(), t_.upperHeads ? "KEEP HOLDING FOR SETTINGS" : "Keep holding for settings",
                   t_.text, gfx::Align::Center, t_);
}
```

(One full-width row is a lot for a hint, but it stays 20 px tall and only for the second the hold
lasts. Overlays.cpp already includes `gfx/Raster.h`, `gfx/Font.h`, `gfx/fonts/Fonts.h` and
`ui/Widgets.h`.)

- [ ] **Step 3: Allow three overlays**

`lib/core/screens/ScreenHost.h`: `static constexpr int kMaxOverlays = 3;`

- [ ] **Step 4: Wire the panel into `ScreenApp`**

`ScreenApp.h`: member `HoldRing ring_;` after `badge_`; private
`bool panel_ = false; ScreenId beforePanel_ = ScreenId::Radar;` and
`void openPanel(uint32_t now); void closePanel(uint32_t now);`; public:

```cpp
    bool panelOpen() const { return panel_; }
    // Panel edits waiting to be saved, oldest first (the platform saves them off the render thread).
    bool takeSettingEdit(SettingEdit& e) { return settings_.takeEdit(e); }
    // The backlight level while the panel's slider is held, else -1.
    int previewBrightness() const { return panel_ ? settings_.previewBrightness() : -1; }
    const HoldRing& holdRing() const { return ring_; }
```

Replace `lib/core/screens/ScreenApp.cpp` with:

```cpp
#include "ScreenApp.h"

namespace scr {

namespace {
// Swipe order, left to right.
const ScreenId kOrder[] = {ScreenId::Closest, ScreenId::Radar, ScreenId::Map, ScreenId::Board, ScreenId::Sky,
                          ScreenId::Stats};
constexpr int kOrderN = sizeof(kOrder) / sizeof(kOrder[0]);
}  // namespace

ScreenApp::ScreenApp(const Theme& theme)
    : theme_(theme), closest_(theme_), radar_(theme_, arena_), map_(theme_, arena_, trails_), board_(theme_, arena_),
      sky_(theme_, arena_), stats_(theme_, arena_), alert_(theme_), settings_(theme_), update_(theme_),
      screens_{&closest_, &radar_, &map_, &board_, &sky_, &stats_, &alert_, &settings_, &update_},
      host_(screens_, kScreens, theme_), dots_(theme_), badge_(theme_), ring_(theme_), layers_{&host_} {
    host_.setScene(&scene_);
    host_.addOverlay(&dots_);
    host_.addOverlay(&badge_);
    host_.addOverlay(&ring_);
}

void ScreenApp::useTheme(const Theme& t) {
    theme_ = t;
    host_.showNow(host_.target());
}

void ScreenApp::setUpdateProgress(int pct) {
    if (pct >= 0) {
        if (!updating_) {
            updating_ = true;
            beforeUpdate_ = panel_ ? beforePanel_ : host_.target();
            panel_ = false;  // the upload's screen replaces the panel
        }
        update_.setPercent(pct);
        host_.show(ScreenId::Update);
    } else if (updating_) {
        updating_ = false;
        host_.show(beforeUpdate_);
    }
}

void ScreenApp::openPanel(uint32_t now) {
    panel_ = true;
    beforePanel_ = host_.target();
    settings_.open(now);
    host_.show(ScreenId::Settings);
}

void ScreenApp::closePanel(uint32_t now) {
    panel_ = false;
    director_.noteTouch(now);  // the user was just here: stay in explore mode for a while
    host_.show(beforePanel_);
}

void ScreenApp::tick(uint32_t now) {
    // The alert engine first, so the director sees this rebuild's alert.
    if (scene_.version != alertVersion_) alertVersion_ = scene_.version, alerts_.update(scene_, now);
    if (updating_) {  // the upload's screen owns the display; the director waits
        dots_.set(false, 0, kOrderN);
        badge_.set(false);
        ring_.set(false, 0, 0, 0);
        return;
    }
    if (panel_) {
        // An emergency takes over at once; any other alert waits for the panel to close.
        const bool emergency = scene_.alert.active && scene_.alert.kind == AlertKind::Emergency;
        if (emergency || settings_.wantsClose(now)) {
            closePanel(now);
        } else {
            dots_.set(false, 0, kOrderN);
            badge_.set(false);
            ring_.set(false, 0, 0, 0);
            return;
        }
    }
    // The host's target, not its current screen: mid-fade the current one is the screen being
    // left, and handing that to the director (which adopts it while the user explores) would
    // turn the user's swipe back.
    ScreenId want = director_.decide(scene_, host_.target(), now);
    if (!started_) host_.showNow(want), started_ = true;
    else host_.show(want);
    int active = 0;
    for (int i = 0; i < kOrderN; ++i)
        if (kOrder[i] == host_.target()) active = i;
    // A selection lasts as long as explore mode (spec §3.1): when the director takes over
    // again, every screen falls back to its own default flight.
    bool exploring = director_.exploring(now);
    if (exploring_ && !exploring) host_.setSelection(0);
    exploring_ = exploring;
    const bool alert = host_.target() == ScreenId::Alert;  // the takeover covers the overlays
    dots_.set(exploring && !alert, active, kOrderN);
    badge_.set(!alert && (!scene_.online || scene_.dataAgeS > 60));
    const float hold = gestures_.holdProgress(now);
    ring_.set(hold > 0 && !alert && !host_.transitioning(), gestures_.pressX(), gestures_.pressY(), hold);
}

void ScreenApp::touch(bool pressed, int x, int y, uint32_t now) {
    if (updating_) return;
    GestureEvent ev = gestures_.feed(pressed, x, y, now);
    if (panel_) {  // the panel takes every sample (its slider) and every tap; nothing else reacts
        settings_.raw(pressed, x, y, now);
        if (ev.gesture == Gesture::Tap) host_.tap(ev.x, ev.y);
        return;
    }
    if (ev.gesture == Gesture::None) return;
    // An alert takes taps only, and a tap ends it; it is not the user exploring (alerts spec §7).
    // While the Alert screen fades out the gestures are still its own.
    if (scene_.alert.active || host_.current() == ScreenId::Alert) {
        if (ev.gesture == Gesture::Tap && scene_.alert.active) alerts_.dismiss(scene_, now);
        return;
    }
    if (ev.gesture == Gesture::Hold) {
        if (!host_.transitioning()) openPanel(now);
        return;
    }
    director_.noteTouch(now);
    if (ev.gesture == Gesture::Tap) handle(host_.tap(ev.x, ev.y));
    else if (ev.gesture == Gesture::SwipeUp || ev.gesture == Gesture::SwipeDown)
        host_.verticalSwipe(ev.gesture == Gesture::SwipeUp ? -1 : +1);
    else step(ev.gesture == Gesture::SwipeLeft ? +1 : -1);
}

void ScreenApp::handle(const NavRequest& r) {
    if (r.selectHex) host_.setSelection(r.selectHex);
    if (r.open) host_.show(r.to);
}

void ScreenApp::step(int dir) {
    int at = 0;
    for (int i = 0; i < kOrderN; ++i)
        if (kOrder[i] == host_.target()) at = i;
    // Next screen in the swipe direction that exists, wrapping around.
    for (int k = 1; k < kOrderN; ++k) {
        ScreenId id = kOrder[(at + dir * k + kOrderN * k) % kOrderN];
        if (host_.screen(id)) return host_.show(id);
    }
}

}  // namespace scr
```

(Task 4's `setUpdateProgress` is replaced by this version, which also closes the panel.)

- [ ] **Step 5: Write the behaviour tests and the ring golden**

Add to `test/test_config_screens/test_main.cpp`:

```cpp
#include <cstring>

// Drives the app the way main_esp32's loop does, 33 ms per frame.
struct Drive {
    golden::Rig g;
    uint32_t t = 1000;
    explicit Drive(const scr::Theme& theme = scr::cinematicSky()) : g(theme) {
        scr::sceneQuiet(g.app->scene());
        g.app->sceneUpdated();
        g.app->configure(scr::DirectorMode::Stay, 30000);
        g.show(ScreenId::Board);
        frame(false);
    }
    void frame(bool pressed, int x = 0, int y = 0) {
        t += 33;
        g.app->touch(pressed, x, y, t);
        g.app->tick(t);
        g.r.frame(0.033f);
    }
    void idle(uint32_t ms) {
        for (uint32_t e = 0; e < ms; e += 33) frame(false);
    }
    void hold(int x, int y, uint32_t ms) {
        for (uint32_t e = 0; e < ms; e += 33) frame(true, x, y);
    }
    void tap(int x, int y) {
        hold(x, y, 66);
        frame(false);
    }
    void openPanel() {
        hold(120, 200, 1100);
        frame(false);
        idle(500);  // the fade
    }
    ScreenId current() const { return g.app->host().current(); }
};

static int centreX(const gfx::Rect& r) { return r.x + r.w / 2; }
static int centreY(const gfx::Rect& r) { return r.y + r.h / 2; }

static void test_hold_ring_golden() {
    Drive d;
    d.hold(120, 160, 600);
    TEST_ASSERT_TRUE(d.g.app->holdRing().visible());
    assertGolden("hold_ring", d.g.d.px, 240, 320);
    d.g.assertNoStale();
}

static void test_hold_opens_the_panel_and_done_returns() {
    Drive d;
    d.openPanel();
    TEST_ASSERT_TRUE(d.g.app->panelOpen());
    TEST_ASSERT_EQUAL(int(ScreenId::Settings), int(d.current()));
    TEST_ASSERT_FALSE(d.g.app->holdRing().visible());
    d.tap(centreX(scr::SettingsView::kDone), centreY(scr::SettingsView::kDone));
    d.idle(500);
    TEST_ASSERT_FALSE(d.g.app->panelOpen());
    TEST_ASSERT_EQUAL(int(ScreenId::Board), int(d.current()));
}

static void test_taps_queue_edits() {
    Drive d;
    d.openPanel();
    d.tap(centreX(scr::SettingsView::kTheme[1]), centreY(scr::SettingsView::kTheme[1]));
    d.tap(centreX(scr::SettingsView::kPlus), centreY(scr::SettingsView::kPlus));
    d.tap(centreX(scr::SettingsView::kMode[2]), centreY(scr::SettingsView::kMode[2]));
    scr::SettingEdit e;
    TEST_ASSERT_TRUE(d.g.app->takeSettingEdit(e));
    TEST_ASSERT_EQUAL_STRING("theme", e.key);
    TEST_ASSERT_EQUAL_STRING("editorial", e.value);
    TEST_ASSERT_TRUE(d.g.app->takeSettingEdit(e));
    TEST_ASSERT_EQUAL_STRING("radius_nm", e.key);
    TEST_ASSERT_EQUAL_STRING("30", e.value);
    TEST_ASSERT_TRUE(d.g.app->takeSettingEdit(e));
    TEST_ASSERT_EQUAL_STRING("screen_mode", e.key);
    TEST_ASSERT_EQUAL_STRING("stay", e.value);
    TEST_ASSERT_FALSE(d.g.app->takeSettingEdit(e));
    d.g.assertNoStale();
}

static void test_radius_steps_and_clamps() {
    Drive d;
    scr::PanelValues v;
    v.radiusNm = 7;
    d.g.app->setPanelValues(v);
    d.openPanel();
    const gfx::Rect& m = scr::SettingsView::kMinus;
    d.tap(centreX(m), centreY(m));  // 7 -> 5
    d.tap(centreX(m), centreY(m));  // stays 5
    scr::SettingEdit e;
    TEST_ASSERT_TRUE(d.g.app->takeSettingEdit(e));
    TEST_ASSERT_EQUAL_STRING("5", e.value);  // one queued entry per key, the newest value
    TEST_ASSERT_FALSE(d.g.app->takeSettingEdit(e));
}

static void test_slider_previews_then_saves_on_release() {
    Drive d;
    d.openPanel();
    const int y = scr::SettingsView::kSliderY;
    d.hold(60, y, 99);
    for (int x = 60; x <= 200; x += 20) d.frame(true, x, y);
    TEST_ASSERT_EQUAL(scr::SettingsView::brightnessAt(200), d.g.app->previewBrightness());
    scr::SettingEdit e;
    TEST_ASSERT_FALSE(d.g.app->takeSettingEdit(e));  // nothing saved while dragging
    d.frame(false);
    TEST_ASSERT_EQUAL(-1, d.g.app->previewBrightness());
    TEST_ASSERT_TRUE(d.g.app->takeSettingEdit(e));
    TEST_ASSERT_EQUAL_STRING("bright_mode", e.key);
    TEST_ASSERT_EQUAL_STRING("manual", e.value);
    TEST_ASSERT_TRUE(d.g.app->takeSettingEdit(e));
    TEST_ASSERT_EQUAL_STRING("brightness", e.key);
    char want[12];
    std::snprintf(want, sizeof want, "%d", scr::SettingsView::brightnessAt(200));
    TEST_ASSERT_EQUAL_STRING(want, e.value);
    d.g.assertNoStale();
}

static void test_the_opening_hold_does_not_grab_the_slider() {
    Drive d;
    d.hold(120, scr::SettingsView::kSliderY, 1100);  // opens over where the slider will be
    d.idle(500);
    TEST_ASSERT_TRUE(d.g.app->panelOpen());
    scr::SettingEdit e;
    TEST_ASSERT_FALSE(d.g.app->takeSettingEdit(e));
}

static void test_panel_closes_after_30_s_idle() {
    Drive d;
    d.openPanel();
    d.idle(29000);
    TEST_ASSERT_TRUE(d.g.app->panelOpen());
    d.idle(1500);
    TEST_ASSERT_FALSE(d.g.app->panelOpen());
}

static void test_emergency_closes_the_panel() {
    Drive d;
    d.openPanel();
    scr::SceneAlert& a = d.g.app->scene().alert;
    a.active = true, a.kind = scr::AlertKind::Emergency, a.live = true;
    a.flight = d.g.app->scene().flights[0];
    a.startMs = d.t, a.endsMs = d.t + 30000;
    d.idle(500);
    TEST_ASSERT_FALSE(d.g.app->panelOpen());
    TEST_ASSERT_EQUAL(int(ScreenId::Alert), int(d.current()));
}

static void test_other_alerts_wait_for_the_panel() {
    Drive d;
    d.openPanel();
    scr::SceneAlert& a = d.g.app->scene().alert;
    a.active = true, a.kind = scr::AlertKind::Military, a.live = true;
    a.flight = d.g.app->scene().flights[0];
    a.startMs = d.t, a.endsMs = d.t + 30000;
    d.idle(500);
    TEST_ASSERT_TRUE(d.g.app->panelOpen());
    d.tap(centreX(scr::SettingsView::kDone), centreY(scr::SettingsView::kDone));
    d.idle(500);
    TEST_ASSERT_EQUAL(int(ScreenId::Alert), int(d.current()));
}

static void test_upload_replaces_the_panel() {
    Drive d;
    d.openPanel();
    d.g.app->setUpdateProgress(5);
    d.idle(500);
    TEST_ASSERT_FALSE(d.g.app->panelOpen());
    TEST_ASSERT_EQUAL(int(ScreenId::Update), int(d.current()));
    d.g.app->setUpdateProgress(-1);
    d.idle(500);
    TEST_ASSERT_EQUAL(int(ScreenId::Board), int(d.current()));
}
```

Register them all in `main()` after the existing `RUN_TEST`s.

- [ ] **Step 6: Run, generate the ring golden, inspect**

Run: `pio test -e test -f test_config_screens` — everything but `test_hold_ring_golden` should pass
(that one reports the missing golden). If a behaviour test fails, fix the code, not the test,
unless the test contradicts the spec. Then `UPDATE_GOLDEN=1 pio test -e test -f test_config_screens`
and look at `hold_ring.png`: the Board screen with a dark disc, a ring about 60% filled clockwise
from the top in the accent colour, and the hint under it.

- [ ] **Step 7: Run everything and commit**

`pio test -e test` — all pass (the existing screen goldens must be unchanged).

```bash
git add lib/core/screens test/test_config_screens test/golden/hold_ring.ppm
git commit -m "feat(config): hold ring opens the touch panel"
```

---

### Task 6: PanelBridge

**Files:**
- Create: `lib/core/app/PanelBridge.h`, `lib/core/app/PanelBridge.cpp`
- Test: `test/test_settings_api/test_main.cpp` (append)

- [ ] **Step 1: Write the failing tests**

Append to `test/test_settings_api/test_main.cpp` (before `main`), and add
`#include "app/PanelBridge.h"` at the top:

```cpp
static void test_panel_values_follow_the_settings() {
    Fixture f;
    f.s.set(key::kBrightness, "120");
    f.s.set(key::kTheme, "editorial");
    f.s.set(key::kRadius, "40");
    f.s.set(key::kScreenMode, "carousel");
    scr::PanelValues v = app::panelValues(f.s);
    TEST_ASSERT_EQUAL(120, v.brightness);
    TEST_ASSERT_TRUE(v.editorial);
    TEST_ASSERT_EQUAL(40, v.radiusNm);
    TEST_ASSERT_EQUAL(int(scr::DirectorMode::Carousel), int(v.mode));
}

static void test_edits_apply_and_queue_in_order() {
    Fixture f;
    app::EditQueue q;
    scr::SettingEdit a, b, out;
    a.key = key::kRadius, std::strcpy(a.value, "45");
    b.key = key::kTheme, std::strcpy(b.value, "editorial");
    TEST_ASSERT_TRUE(q.push(a));
    TEST_ASSERT_TRUE(q.push(b));
    TEST_ASSERT_TRUE(q.pop(out));
    TEST_ASSERT_TRUE(app::applyEdit(f.s, out));
    TEST_ASSERT_TRUE(q.pop(out));
    TEST_ASSERT_TRUE(app::applyEdit(f.s, out));
    TEST_ASSERT_FALSE(q.pop(out));
    TEST_ASSERT_EQUAL(45, f.s.getInt(key::kRadius));
    TEST_ASSERT_EQUAL_STRING("editorial", f.s.getEnum(key::kTheme).c_str());
    scr::SettingEdit bad;
    bad.key = key::kTheme, std::strcpy(bad.value, "neon");
    TEST_ASSERT_FALSE(app::applyEdit(f.s, bad));
    for (int i = 0; i < app::EditQueue::kMax; ++i) TEST_ASSERT_TRUE(q.push(a));
    TEST_ASSERT_FALSE(q.push(a));  // full
}
```

and the two `RUN_TEST`s.

- [ ] **Step 2: Run to see the compile failure**

`pio test -e test -f test_settings_api` — `app/PanelBridge.h` not found.

- [ ] **Step 3: Implement**

`lib/core/app/PanelBridge.h`:

```cpp
#pragma once
// The touch panel's link to the settings registry (config spec §5): what the panel shows, and a
// queue that carries its edits from the render thread to the web task, which saves them (an NVS
// write can take tens of milliseconds, too long for a frame).
#include "screens/SettingsScreen.h"
#include "services/Lock.h"
#include "services/Settings.h"

namespace app {

scr::PanelValues panelValues(const svc::Settings& s);
// Saves one edit; false if the registry refused it.
bool applyEdit(svc::Settings& s, const scr::SettingEdit& e);

class EditQueue {
   public:
    static constexpr int kMax = 8;
    bool push(const scr::SettingEdit& e);  // false when full
    bool pop(scr::SettingEdit& out);       // oldest first

   private:
    svc::Mutex mu_;
    scr::SettingEdit q_[kMax];
    int n_ = 0;
};

}  // namespace app
```

`lib/core/app/PanelBridge.cpp`:

```cpp
#include "PanelBridge.h"

#include "services/CoreSettings.h"

namespace app {

scr::PanelValues panelValues(const svc::Settings& s) {
    scr::PanelValues v;
    v.brightness = s.getInt(svc::key::kBrightness);
    v.editorial = s.getEnum(svc::key::kTheme) == "editorial";
    v.radiusNm = s.getInt(svc::key::kRadius);
    v.mode = scr::parseDirectorMode(s.getEnum(svc::key::kScreenMode).c_str());
    return v;
}

bool applyEdit(svc::Settings& s, const scr::SettingEdit& e) { return e.key && s.set(e.key, e.value); }

bool EditQueue::push(const scr::SettingEdit& e) {
    svc::LockGuard g(mu_);
    if (n_ == kMax) return false;
    q_[n_++] = e;
    return true;
}

bool EditQueue::pop(scr::SettingEdit& out) {
    svc::LockGuard g(mu_);
    if (!n_) return false;
    out = q_[0];
    for (int i = 1; i < n_; ++i) q_[i - 1] = q_[i];
    --n_;
    return true;
}

}  // namespace app
```

- [ ] **Step 4: Run, then commit**

`pio test -e test -f test_settings_api` — 10 pass; `pio test -e test` — all pass.

```bash
git add lib/core/app/PanelBridge.h lib/core/app/PanelBridge.cpp test/test_settings_api
git commit -m "feat(config): panel values and edit queue"
```

---

### Task 7: The page and its embedding

**Files:**
- Create: `web/index.html`, `scripts/web_embed.py`
- Modify: `platformio.ini`, `../.gitignore`

- [ ] **Step 1: Write `scripts/web_embed.py`**

```python
# Gzips web/index.html into src/web/WebPage.h (config spec §4). PlatformIO runs it before each
# build of the ESP32 envs; `python3 scripts/web_embed.py` runs it by hand. The header is generated
# and not committed.
import gzip
import os


def embed(project_dir):
    src = os.path.join(project_dir, "web", "index.html")
    out = os.path.join(project_dir, "src", "web", "WebPage.h")
    if os.path.exists(out) and os.path.getmtime(out) >= os.path.getmtime(src):
        return
    with open(src, "rb") as f:
        raw = f.read()
    gz = gzip.compress(raw, compresslevel=9, mtime=0)
    rows = [", ".join("0x%02x" % b for b in gz[i:i + 16]) for i in range(0, len(gz), 16)]
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        f.write("// Generated by scripts/web_embed.py from web/index.html. Do not edit.\n")
        f.write("#pragma once\n#include <cstddef>\n#include <cstdint>\n\n")
        f.write("constexpr size_t kWebPageLen = %d;\n" % len(gz))
        f.write("alignas(4) const uint8_t kWebPage[] = {\n    " + ",\n    ".join(rows) + "\n};\n")
    print("web_embed: %d bytes -> %d gzipped" % (len(raw), len(gz)))


try:
    Import("env")  # noqa: F821 (PlatformIO / SCons)
    embed(env["PROJECT_DIR"])  # noqa: F821
except NameError:
    import sys
    embed(sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
```

- [ ] **Step 2: Build config**

`platformio.ini`, in `[esp32_base]`: change `build_src_filter = +<main_esp32.cpp>` to
`build_src_filter = +<main_esp32.cpp> +<web/>` and add `extra_scripts = pre:scripts/web_embed.py`.
(The probe envs set their own `build_src_filter`, so they skip `src/web/`.)

`../.gitignore` (repo root): add the line `firmware/src/web/WebPage.h`.

- [ ] **Step 3: Write `web/index.html`**

One file, no build step, no framework. Colours follow the Cinematic theme. Everything below is the
complete file:

```html
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Contrail</title>
<style>
:root{--bg:#0B1029;--card:#141C42;--edge:#2B3670;--text:#fff;--soft:#DFE5FF;--dim:#A9B3D6;--accent:#FFD29A;--bad:#FF8A8A}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:15px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif}
a{color:var(--accent)}
header{position:sticky;top:0;z-index:5;background:var(--bg);border-bottom:1px solid var(--edge)}
.bar{max-width:1040px;margin:0 auto;padding:12px 16px;display:flex;flex-wrap:wrap;gap:10px 18px;align-items:center}
.brand{flex:1 1 auto;display:flex;flex-wrap:wrap;gap:4px 12px;align-items:baseline}
.brand b{font-size:22px}
.muted{color:var(--dim);font-size:13px}
.actions{display:flex;gap:8px;align-items:center}
#count{color:var(--accent);font-size:14px}
button{font:inherit;height:40px;padding:0 16px;border-radius:8px;border:1px solid var(--edge);background:transparent;color:var(--soft);cursor:pointer}
button.primary{background:var(--accent);border-color:var(--accent);color:var(--bg);font-weight:600}
button:disabled{opacity:.45;cursor:default}
.wrap{max-width:1040px;margin:0 auto;padding:20px 16px;display:flex;flex-wrap:wrap;gap:20px;align-items:flex-start}
nav{flex:1 1 160px;max-width:200px;display:flex;flex-direction:column;gap:2px}
nav a{padding:8px 12px;border-radius:6px;color:var(--soft);text-decoration:none}
nav a:hover{background:var(--card)}
main{flex:999 1 560px;min-width:0;display:flex;flex-direction:column;gap:18px}
section{background:var(--card);border:1px solid var(--edge);border-radius:12px;padding:18px;display:flex;flex-direction:column;gap:14px}
h2{margin:0;font-size:18px}
.note{color:var(--dim);font-size:13px;font-weight:400;margin-left:8px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(190px,1fr));gap:14px}
label.f{display:flex;flex-direction:column;gap:6px;font-size:13px;color:var(--soft)}
label.c{display:flex;align-items:center;gap:10px;min-height:40px}
input,select{font:inherit;height:40px;padding:0 12px;border-radius:8px;border:1px solid var(--edge);background:var(--bg);color:var(--text);width:100%}
input[type=checkbox]{width:20px;height:20px;accent-color:var(--accent)}
input[type=range]{padding:0;accent-color:var(--accent)}
.seg{display:flex;flex-wrap:wrap;gap:8px}
.seg button.on{border-color:var(--accent);background:rgba(255,210,154,.12);color:var(--text)}
.row{display:flex;justify-content:space-between}
.err{color:var(--bad);font-size:12px;min-height:0}
.changed>input,.changed>select,.changed .seg{outline:1px dashed var(--accent);outline-offset:2px}
#map{height:300px;border-radius:8px;border:1px solid var(--edge);background:#081026;display:flex;align-items:center;justify-content:center;color:var(--dim)}
.drop{display:flex;flex-direction:column;align-items:center;justify-content:center;gap:6px;min-height:110px;border:2px dashed var(--edge);border-radius:10px;cursor:pointer;text-align:center;padding:16px}
.drop.over{border-color:var(--accent)}
.prog{height:8px;border-radius:4px;background:var(--bg);overflow:hidden}
.prog div{height:8px;width:0;background:var(--accent)}
#toast{position:fixed;left:50%;bottom:20px;transform:translateX(-50%);background:var(--card);border:1px solid var(--edge);padding:10px 16px;border-radius:8px;display:none}
</style>
</head>
<body>
<header><div class="bar">
  <div class="brand"><b>Contrail</b><span class="muted" id="info">Connecting…</span></div>
  <div class="actions"><span id="count"></span><button id="discard" disabled>Discard</button><button id="save" class="primary" disabled>Save</button></div>
</div></header>
<div class="wrap">
  <nav aria-label="Sections" id="nav"></nav>
  <main id="main"></main>
</div>
<div id="toast" role="status"></div>
<script>
"use strict";
const GROUPS = [["location", "Location"], ["display", "Display"], ["alerts", "Alerts"], ["data", "Flight data"], ["hardware", "Hardware"]];
// Time zones: label and POSIX rule (with its daylight-saving dates). "" = automatic.
const ZONES = [
  ["UTC", "UTC0"],
  ["Hawaii", "HST10"],
  ["Alaska", "AKST9AKDT,M3.2.0,M11.1.0"],
  ["Pacific — Los Angeles, Vancouver", "PST8PDT,M3.2.0,M11.1.0"],
  ["Mountain — Denver, Calgary", "MST7MDT,M3.2.0,M11.1.0"],
  ["Arizona", "MST7"],
  ["Central — Chicago, Winnipeg", "CST6CDT,M3.2.0,M11.1.0"],
  ["Mexico City", "CST6"],
  ["Eastern — New York, Toronto", "EST5EDT,M3.2.0,M11.1.0"],
  ["Atlantic — Halifax", "AST4ADT,M3.2.0,M11.1.0"],
  ["Puerto Rico", "AST4"],
  ["Newfoundland", "NST3:30NDT,M3.2.0,M11.1.0"],
  ["São Paulo, Buenos Aires", "<-03>3"],
  ["UK, Ireland, Portugal", "GMT0BST,M3.5.0/1,M10.5.0"],
  ["Central Europe — Paris, Berlin, Madrid", "CET-1CEST,M3.5.0,M10.5.0/3"],
  ["West Africa — Lagos", "WAT-1"],
  ["Eastern Europe — Athens, Helsinki, Kyiv", "EET-2EEST,M3.5.0/3,M10.5.0/4"],
  ["South Africa", "SAST-2"],
  ["Israel", "IST-2IDT,M3.4.4/26,M10.5.0"],
  ["Moscow", "MSK-3"],
  ["Istanbul", "<+03>-3"],
  ["East Africa — Nairobi", "EAT-3"],
  ["Gulf — Dubai", "<+04>-4"],
  ["India", "IST-5:30"],
  ["Bangladesh", "<+06>-6"],
  ["Bangkok, Jakarta", "<+07>-7"],
  ["China, Singapore, Perth", "<+08>-8"],
  ["Japan, Korea", "JST-9"],
  ["Darwin", "ACST-9:30"],
  ["Adelaide", "ACST-9:30ACDT,M10.1.0,M4.1.0/3"],
  ["Brisbane", "AEST-10"],
  ["Sydney, Melbourne", "AEST-10AEDT,M10.1.0,M4.1.0/3"],
  ["New Zealand", "NZST-12NZDT,M9.5.0,M4.1.0/3"],
];
const HIDDEN = new Set(["loc_set", "lat", "lon"]);  // loc_set is implied; lat/lon live in the map card
const POST = {"Content-Type": "application/json", "X-Contrail": "1"};
let info = {}, schema = [], saved = {}, edits = {}, map = null, pin = null, ring = null;
const $ = s => document.querySelector(s);
const el = (tag, attrs = {}, ...kids) => {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) k === "class" ? e.className = v : k.startsWith("on") ? e.addEventListener(k.slice(2), v) : e.setAttribute(k, v);
  for (const k of kids) e.append(k);
  return e;
};
const def = k => schema.find(d => d.key === k);
const current = k => k in edits ? edits[k] : saved[k];
const nice = s => s.charAt(0).toUpperCase() + s.slice(1);

function toast(msg, ms = 4000) {
  const t = $("#toast");
  t.textContent = msg, t.style.display = "block";
  clearTimeout(toast.h);
  if (ms) toast.h = setTimeout(() => t.style.display = "none", ms);
}

async function api(path, opts) {
  const r = await fetch(path, opts);
  const j = await r.json().catch(() => ({}));
  if (!r.ok) throw Object.assign(new Error(j.error || ("HTTP " + r.status)), {body: j});
  return j;
}

function edit(key, value) {
  const was = saved[key];
  const same = typeof was === "object" ? value === "" : String(was) === String(value);
  if (same) delete edits[key]; else edits[key] = value;
  if (key === "lat" || key === "lon") edits.loc_set = true;
  if (key === "radius_nm" && ring) ring.setRadius(Number(value) * 1852);
  const n = Object.keys(edits).filter(k => k !== "loc_set").length;
  $("#count").textContent = n ? n + (n === 1 ? " unsaved change" : " unsaved changes") : "";
  $("#save").disabled = $("#discard").disabled = !n;
  const f = document.querySelector(`[data-key="${key}"]`);
  if (f) f.classList.toggle("changed", key in edits), f.querySelector(".err").textContent = "";
}

function field(d, control, cls = "f") {
  const lab = el("label", {class: cls, "data-key": d.key});
  if (cls === "c") lab.append(control, d.label);
  else lab.append(d.label, control);
  lab.append(el("span", {class: "err"}));
  return lab;
}

function control(d) {
  const v = current(d.key);
  if (d.key === "tz") return field(d, tzSelect(v));
  if (d.key === "quiet_from" || d.key === "quiet_to") {
    const i = el("input", {type: "time", value: v});
    i.oninput = () => edit(d.key, i.value);
    return field(d, i);
  }
  if (d.secret) {
    const i = el("input", {type: "password", autocomplete: "off", placeholder: v && v.set ? "Saved — type to replace" : "Not set"});
    i.oninput = () => edit(d.key, i.value);
    return field(d, i);
  }
  if (d.type === "bool") {
    const i = el("input", {type: "checkbox"});
    i.checked = !!v;
    i.onchange = () => edit(d.key, i.checked);
    return field(d, i, "c");
  }
  if (d.type === "enum" && d.options.length <= 3) {
    const seg = el("div", {class: "seg", role: "radiogroup"});
    for (const o of d.options) {
      const b = el("button", {type: "button", class: o === v ? "on" : ""}, nice(o));
      b.onclick = e => {
        e.preventDefault();
        seg.querySelectorAll("button").forEach(x => x.classList.toggle("on", x === b));
        edit(d.key, o);
      };
      seg.append(b);
    }
    const wrap = el("div", {class: "f", "data-key": d.key}, d.label, seg, el("span", {class: "err"}));
    return wrap;
  }
  if (d.type === "enum") {
    const s = el("select");
    for (const o of d.options) s.append(el("option", {value: o}, nice(o)));
    s.value = v;
    s.onchange = () => edit(d.key, s.value);
    return field(d, s);
  }
  if ((d.type === "int" || d.type === "float") && (d.key === "radius_nm" || d.key === "brightness")) {
    const out = el("span", {}, "");
    const show = x => out.textContent = d.key === "brightness" ? Math.round(x * 100 / 255) + "%" : x + " nm";
    const i = el("input", {type: "range", min: d.min, max: d.max, step: 1, value: v});
    i.oninput = () => { show(i.value); edit(d.key, Number(i.value)); };
    show(v);
    const lab = el("label", {class: "f", "data-key": d.key}, el("span", {class: "row"}, el("span", {}, d.label), out), i, el("span", {class: "err"}));
    return lab;
  }
  if (d.type === "int" || d.type === "float") {
    const i = el("input", {type: "number", min: d.min, max: d.max, step: d.type === "int" ? 1 : "any", value: v});
    i.oninput = () => edit(d.key, i.value === "" ? "" : Number(i.value));
    return field(d, i);
  }
  const i = el("input", {type: "text", value: v || ""});
  i.oninput = () => edit(d.key, i.value);
  return field(d, i);
}

function tzSelect(v) {
  const s = el("select");
  const off = info.autoOffsetS;
  const auto = off == null ? "Automatic" : `Automatic — now UTC${off < 0 ? "−" : "+"}${String(Math.floor(Math.abs(off) / 3600)).padStart(2, "0")}:${String(Math.abs(off) / 60 % 60).padStart(2, "0")} from your location`;
  s.append(el("option", {value: ""}, auto));
  for (const [label, rule] of ZONES) s.append(el("option", {value: rule}, label));
  if (v && !ZONES.some(z => z[1] === v)) s.append(el("option", {value: v}, "Custom: " + v));
  s.value = v || "";
  s.onchange = () => edit("tz", s.value);
  return s;
}

function locationCard() {
  const lat = el("input", {type: "number", step: "any", min: -90, max: 90, value: saved.lat});
  const lon = el("input", {type: "number", step: "any", min: -180, max: 180, value: saved.lon});
  lat.oninput = () => { edit("lat", Number(lat.value)); movePin(); };
  lon.oninput = () => { edit("lon", Number(lon.value)); movePin(); };
  const movePin = () => {
    if (!map) return;
    const ll = [Number(current("lat")), Number(current("lon"))];
    pin.setLatLng(ll), ring.setLatLng(ll);
  };
  const box = el("div", {id: "map"}, "Loading map…");
  loadLeaflet().then(() => {
    box.textContent = "";
    const ll = [Number(saved.lat), Number(saved.lon)];
    map = L.map(box).setView(ll, saved.loc_set ? 9 : 3);
    L.tileLayer("https://tile.openstreetmap.org/{z}/{x}/{y}.png", {maxZoom: 18, attribution: "© OpenStreetMap contributors"}).addTo(map);
    ring = L.circle(ll, {radius: Number(current("radius_nm")) * 1852, color: "#FFD29A", weight: 2, dashArray: "6 6", fillOpacity: 0.08}).addTo(map);
    pin = L.circleMarker(ll, {radius: 8, color: "#0B1029", weight: 3, fillColor: "#FFD29A", fillOpacity: 1}).addTo(map);
    map.on("click", e => {
      lat.value = e.latlng.lat.toFixed(4), lon.value = e.latlng.lng.toFixed(4);
      edit("lat", Number(lat.value)), edit("lon", Number(lon.value));
      movePin();
    });
  }).catch(() => box.textContent = "Map unavailable — enter coordinates below");
  const sec = el("section", {id: "location"}, el("h2", {}, "Location"),
    el("div", {class: "muted"}, "Click the map to move home. The ring is the radius."), box,
    el("div", {class: "grid"}, field(def("lat"), lat), field(def("lon"), lon)));
  for (const d of schema) if (d.group === "location" && !HIDDEN.has(d.key)) sec.append(control(d));
  return sec;
}

function loadLeaflet() {
  return new Promise((ok, fail) => {
    const css = el("link", {rel: "stylesheet", href: "https://unpkg.com/leaflet@1.9.4/dist/leaflet.css"});
    const js = el("script", {src: "https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"});
    js.onload = ok, js.onerror = fail;
    setTimeout(() => window.L ? ok() : fail(), 10000);
    document.head.append(css, js);
  });
}

function firmwareCard() {
  const file = el("input", {type: "file", accept: ".bin", hidden: ""});
  const status = el("div", {class: "muted"}, "");
  const bar = el("div");
  const prog = el("div", {class: "prog", hidden: ""}, bar);
  const drop = el("label", {class: "drop"}, el("b", {}, "Drop firmware.bin here"), el("span", {class: "muted"}, "or click to choose a file"), file);
  drop.ondragover = e => { e.preventDefault(); drop.classList.add("over"); };
  drop.ondragleave = () => drop.classList.remove("over");
  drop.ondrop = e => { e.preventDefault(); drop.classList.remove("over"); if (e.dataTransfer.files[0]) upload(e.dataTransfer.files[0]); };
  file.onchange = () => file.files[0] && upload(file.files[0]);
  function upload(f) {
    if (!/\.bin$/i.test(f.name)) return status.textContent = "Choose a .bin file.";
    const x = new XMLHttpRequest();
    x.open("POST", "/api/update");
    x.setRequestHeader("X-Contrail", "1");
    x.setRequestHeader("X-Size", String(f.size));
    x.upload.onprogress = e => { if (e.lengthComputable) { const p = Math.round(e.loaded * 100 / e.total); bar.style.width = p + "%"; status.textContent = `Uploading ${f.name} — ${p}%`; } };
    x.onload = () => {
      let j = {};
      try { j = JSON.parse(x.responseText); } catch (e) {}
      if (x.status === 200 && j.ok) { status.textContent = "Installed. Contrail is restarting…"; waitForRestart(); }
      else { status.textContent = "Update failed: " + (j.error || x.status) + ". The old firmware is still running."; prog.hidden = true; }
    };
    x.onerror = () => { status.textContent = "Upload interrupted. The old firmware is still running."; prog.hidden = true; };
    const form = new FormData();
    form.append("firmware", f, f.name);
    prog.hidden = false, bar.style.width = "0";
    status.textContent = "Keep this page open. The device restarts when the upload finishes.";
    x.send(form);
  }
  return el("section", {id: "firmware"}, el("h2", {}, "Firmware"),
    el("div", {}, `Running ${info.version}, built ${info.built}. Upload a firmware.bin to update. Settings are kept.`),
    drop, prog, status);
}

function render() {
  const main = $("#main"), nav = $("#nav");
  main.textContent = "", nav.textContent = "";
  main.append(locationCard());
  for (const [g, title] of GROUPS) {
    nav.append(el("a", {href: "#" + g}, title));
    if (g === "location") continue;
    const h = el("h2", {}, title);
    if (g === "hardware") h.append(el("span", {class: "note"}, "Saving these restarts the device"));
    const grid = el("div", {class: "grid"});
    for (const d of schema) if (d.group === g && !HIDDEN.has(d.key)) grid.append(control(d));
    main.append(el("section", {id: g}, h, grid));
  }
  nav.append(el("a", {href: "#firmware"}, "Firmware"));
  main.append(firmwareCard());
}

async function load() {
  try {
    [info, {settings: schema}, saved] = await Promise.all([api("/api/info"), api("/api/schema"), api("/api/settings")]);
  } catch (e) {
    $("#info").textContent = "Couldn't reach Contrail — reload to try again";
    return;
  }
  $("#info").textContent = `contrail.local · ${info.ip} · firmware ${info.version}`;
  edits = {};
  edit("_", undefined);
  render();
}

async function save() {
  $("#save").disabled = true;
  try {
    const r = await api("/api/settings", {method: "POST", headers: POST, body: JSON.stringify(edits)});
    saved = await api("/api/settings");
    edits = {};
    edit("_", undefined);
    document.querySelectorAll(".changed").forEach(e => e.classList.remove("changed"));
    if (r.restart) { toast("Saved. Contrail is restarting…", 0); waitForRestart(); }
    else toast("Saved");
  } catch (e) {
    const errs = (e.body && e.body.errors) || {};
    for (const [k, why] of Object.entries(errs)) {
      const f = document.querySelector(`[data-key="${k}"] .err`);
      if (f) f.textContent = why;
    }
    toast(Object.keys(errs).length ? "Nothing saved: fix the marked fields" : "Couldn't reach Contrail — try again");
    $("#save").disabled = false;
  }
}

async function waitForRestart() {
  await new Promise(r => setTimeout(r, 4000));
  for (let i = 0; i < 60; ++i) {
    try { await api("/api/info"); location.reload(); return; } catch (e) {}
    await new Promise(r => setTimeout(r, 2000));
  }
  toast("Contrail hasn't come back yet — reload the page later", 0);
}

$("#save").onclick = save;
$("#discard").onclick = () => load();
addEventListener("beforeunload", e => { if (Object.keys(edits).length) e.preventDefault(); });
load();
</script>
</body>
</html>
```

Note `edit("_", undefined)` is only used to refresh the counter and buttons: `"_"` is never a key,
and `saved["_"]` and `undefined` compare equal as strings, so nothing is added to `edits`.

- [ ] **Step 4: Check the embed script and size**

Run: `python3 scripts/web_embed.py` — prints `web_embed: N bytes -> M gzipped`; M must be
≤ 25,600 (spec target). Then `ls src/web/WebPage.h` exists and `git status` does not list it.

- [ ] **Step 5: Check the page in a desktop browser against a fake API**

Write a throwaway mock server in the scratchpad (not committed) that serves `web/index.html` at `/`
and answers `/api/info`, `/api/schema`, `/api/settings` with JSON built from
`lib/core/services/CoreSettings.cpp` (copy a handful of entries per group), and logs POST bodies.
Open it, check: every group renders, the map loads, the time-zone list shows, changing values shows
"N unsaved changes", Save posts only the changed keys with `X-Contrail: 1`, a 400 with
`{"ok":false,"errors":{"radius_nm":"out of range 5–100"}}` marks that field, phone width (375 px)
stacks the menu above the cards. Fix anything broken in `index.html`.

- [ ] **Step 6: Commit**

```bash
git add web/index.html scripts/web_embed.py platformio.ini ../.gitignore
git commit -m "feat(config): settings web page and its gzip embedding"
```

---

### Task 8: WebUi (ESP32)

**Files:**
- Create: `src/web/WebUi.h`, `src/web/WebUi.cpp`

No native tests (it is Arduino-only); it is checked on the board in Task 11.

- [ ] **Step 1: Write `src/web/WebUi.h`**

```cpp
#pragma once
// The settings page and its JSON API at http://contrail.local (config spec §2), served from its own
// task on core 0, and firmware upload into the spare OTA slot (§6). No password (the home network
// is trusted); both POSTs need an X-Contrail header so another website can't send them.
#include <ArduinoJson.h>

#include <functional>

#include "services/Settings.h"

namespace web {

struct Hooks {
    std::function<void(JsonDocument&)> info;          // fills /api/info
    std::function<void(int percent)> updateProgress;  // 0..100 while an upload runs, -1 when it ends or fails
    std::function<void()> restart;                    // after a reply that needs a restart
    std::function<void()> idle;                       // every pass of the task (saves panel edits)
};

void start(svc::Settings& settings, const Hooks& hooks);

}  // namespace web
```

- [ ] **Step 2: Write `src/web/WebUi.cpp`**

```cpp
#include "WebUi.h"

#include <Arduino.h>
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

bool guarded() { return server->header("X-Contrail") == "1"; }

void sendJson(int code, const JsonDocument& doc) {
    String s;
    serializeJson(doc, s);
    server->send(code, "application/json", s);
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
            if (upPct < 0) return;  // already failed: drain the rest
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
    server->on("/api/update", HTTP_POST, handleUploadDone, handleUploadChunk);
    server->onNotFound([] { server->send(404, "text/plain", "not found"); });
    server->begin();
    MDNS.addService("http", "tcp", 80);
    // Above the network task's idle priority, so a slow TLS poll never makes the page wait; the
    // 5 ms delay each pass leaves it the CPU.
    xTaskCreatePinnedToCore(task, "web", 8192, nullptr, 1, nullptr, 0);
    svc::logf("[web] serving http://contrail.local");
}

}  // namespace web
```

- [ ] **Step 3: Compile**

Nothing calls `web::start` yet, but the file is in the build: `pio run -e cyd2usb` must compile.
Fix any API mismatch against the installed Arduino core
(`~/.platformio/packages/framework-arduinoespressif32/libraries/WebServer/src/WebServer.h`,
`.../Update/src/Update.h`): `send_P(int, PGM_P, PGM_P, size_t)`, `collectHeaders(const char**,
size_t)`, `Update.begin(size_t, int)`, `Update.abort()`, `Update.errorString()`.

- [ ] **Step 4: Commit**

```bash
git add src/web/WebUi.h src/web/WebUi.cpp
git commit -m "feat(config): web server, settings API routes and OTA upload"
```

---

### Task 9: Wiring on the ESP32

**Files:**
- Modify: `src/main_esp32.cpp`

- [ ] **Step 1: Includes and globals**

Add `#include "app/PanelBridge.h"` and `#include "web/WebUi.h"` with the other project includes.
After `bool gZoneFound = false;` add:

```cpp
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
```

In `geolocate()`, change `gOffsetS = doc["offset"].as<int>(), gOffsetReady = true;` to
`gOffsetS = doc["offset"].as<int>(), gOffsetReady = true, gHaveOffset = true;`. In `lookupZone()`,
change `gOffsetS = off, gOffsetReady = true, gZoneFound = true;` to
`gOffsetS = off, gOffsetReady = true, gZoneFound = true, gHaveOffset = true;`.

- [ ] **Step 2: Network task pauses during an upload and re-looks-up the zone on demand**

In `networkTask`, at the top of the `for (;;)` body:

```cpp
        if (gUpdatePct >= 0) {  // an upload is writing flash: no polls, and their TLS heap is free
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        if (gZoneNow.exchange(false)) zoneWait = 0, lastZoneTry = millis();
```

- [ ] **Step 3: Smaller portal**

In `connectWifi`, delete everything from the `// Alerts (spec §5)` comment through
`for (WiFiManagerParameter* p : alertParams) wm.addParameter(p);`, and inside
`setSaveParamsCallback` delete everything after the lat/lon `if` block (the `ticked` lambda and
the `st.set(...)` lines). The callback keeps only the lat/lon block. Add above `wm.setTitle`:

```cpp
    // Everything else is on the web page at http://contrail.local (config spec §3).
```

- [ ] **Step 4: Apply tz and display changes on the render thread**

Directly after `std::string gTz = "UTC0";`, add:

```cpp
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
```

- [ ] **Step 5: `setup()`**

Replace `core.settings().onChange([](const char*) { gAlertsDirty = true; });` with:

```cpp
    core.settings().onChange([](const char* k) {  // any thread: flags only
        gAlertsDirty = true;
        gDisplayDirty = true;
        if (!std::strcmp(k, svc::key::kTz)) gTzDirty = true;
        if (!std::strcmp(k, svc::key::kLat) || !std::strcmp(k, svc::key::kLon)) gZoneNow = true;
    });
```

Replace the theme block:

```cpp
#ifdef CONTRAIL_FORCE_THEME
    screens->useTheme(scr::themeByName(CONTRAIL_FORCE_THEME));  // cyd2usb_editorial
#else
    gThemeName = core.settings().getEnum(svc::key::kTheme);
    screens->useTheme(scr::themeByName(gThemeName.c_str()));
#endif
```

(Its comment "read once at boot (spec §1: live switching comes with sub-project 4)" goes; live
switching is `applyDisplaySettings`.)

After `renderer->setLayers(screens->layers(), screens->layerCount());` add:

```cpp
    web::Hooks hooks;
    hooks.info = [](JsonDocument& d) {
        d["version"] = kVersion;
        d["ip"] = WiFi.localIP().toString();
        d["uptimeS"] = millis() / 1000;
        d["heap"] = ESP.getFreeHeap();
        if (gHaveOffset && !gTzSet) d["autoOffsetS"] = gOffsetS.load();
    };
    hooks.updateProgress = [](int p) { gUpdatePct = p, gUpdateSeenMs = millis(); };
    hooks.restart = [] { gRestartAtMs = millis() + 1000 | 1; };  // | 1: never 0
    hooks.idle = [] {
        scr::SettingEdit e;
        while (gEdits.pop(e))
            if (!app::applyEdit(core.settings(), e)) svc::logf("[panel] refused %s=%s", e.key, e.value);
    };
    web::start(core.settings(), hooks);
```

- [ ] **Step 6: `loop()`**

Replace the `if (gOffsetReady.exchange(false)) {` line with
`if (gOffsetReady.exchange(false) && !gTzSet) {` and put `if (gTzDirty.exchange(false)) applyTzSetting();`
just above it. After the `gAlertsDirty` line add:

```cpp
    if (gDisplayDirty.exchange(false)) applyDisplaySettings();
    // An upload that stops moving for 30 s has died with its connection: back to the screens.
    if (gUpdatePct >= 0 && millis() - gUpdateSeenMs > 30000) gUpdatePct = -1, svc::logf("[ota] upload stalled");
    screens->setUpdateProgress(gUpdatePct);
```

After `screens->tick(millis());` add:

```cpp
    for (scr::SettingEdit e; screens->takeSettingEdit(e);)
        if (!gEdits.push(e)) svc::logf("[panel] edit queue full, dropped %s", e.key);
```

Before the final `delay(1);` add:

```cpp
    if (gRestartAtMs && int32_t(millis() - gRestartAtMs) >= 0) ESP.restart();
```

- [ ] **Step 7: Brightness preview and the soak count**

At the top of `updateBrightness()`:

```cpp
    if (const int p = screens->previewBrightness(); p >= 0) {  // the panel's slider is held
        gBaseBright = uint8_t(p);
        return;
    }
```

In the soak block, `(uint32_t(scr::ScreenId::Count) - 1)` is no longer six screens; replace it with
`6u` and keep the comment "six screens x 20 s; not Alert".

- [ ] **Step 8: Build and check the size**

Run: `pio run -e cyd2usb` — compiles. Note the flash use; it must be ≤ 1,802,753 + 40,960 =
1,843,713 bytes. Also `pio run -e cyd2usb_editorial` and `pio run -e cyd2usb_soak` compile.

- [ ] **Step 9: Commit**

```bash
git add src/main_esp32.cpp
git commit -m "feat(config): wire web UI, touch panel, live theme and tz on the CYD"
```

---

### Task 10: Simulator wiring

**Files:**
- Modify: `src/main_sim.cpp`

The simulator has no web server, but the touch panel should work there (mouse hold) so it can be
tried on the desktop. Saves run on the main thread: the sim has no frame budget.

- [ ] **Step 1: Wire it**

Add `#include "app/PanelBridge.h"`. After the `screens->configureAlerts(...)` line add:

```cpp
    // Panel edits save at once here; any settings change re-applies theme, mode and panel values.
    std::atomic<bool> displayDirty{true};
    core.settings().onChange([&](const char*) { displayDirty = true; });
    std::string themeName = core.settings().getEnum(svc::key::kTheme);
    auto applyDisplay = [&] {
        svc::Settings& s = core.settings();
        const std::string theme = s.getEnum(svc::key::kTheme);
        if (!(themeEnv && *themeEnv) && theme != themeName)
            themeName = theme, screens->useTheme(scr::themeByName(theme.c_str()));
        screens->configure(scr::parseDirectorMode(s.getEnum(svc::key::kScreenMode).c_str()),
                           uint32_t(s.getInt(svc::key::kCarouselS)) * 1000u);
        screens->setPanelValues(app::panelValues(s));
    };
```

In the interactive loop, after `screens->touch(touched, tx, ty, clock.millis());` add:

```cpp
            for (scr::SettingEdit e; screens->takeSettingEdit(e);) app::applyEdit(core.settings(), e);
            if (displayDirty.exchange(false)) applyDisplay();
```

- [ ] **Step 2: Build and try it**

Run: `pio run -e sim`, then `.pio/build/sim/program`. Hold the mouse on the screen: the ring fills,
the panel opens; switch theme (the panel redraws in Editorial), change radius, press Done. Quit and
start again: the settings persisted (`~/.contrail/settings.json`).

- [ ] **Step 3: Commit**

```bash
git add src/main_sim.cpp
git commit -m "feat(config): touch panel in the simulator"
```

---

### Task 11: On the board, and the record

**Files:**
- Modify: `../docs/HARDWARE_TESTING.md`, `../docs/superpowers/specs/2026-11-04-contrail-config-design.md`

The controller runs this task (it has the board and the serial tools). Board: `/dev/cu.usbserial-114440`.

- [ ] **Step 1: Flash and boot**

`pio run -e cyd2usb -t upload --upload-port /dev/cu.usbserial-114440`, then watch serial for 90 s:
`[web] serving http://contrail.local` appears; `[health]` shows heap and minheap.

- [ ] **Step 2: Web checks (laptop browser, then phone)**

- `http://contrail.local` and `http://<ip>` load; every group shows; the map loads.
- Change radius and theme, Save: serial shows `[web] saved 2 settings`, `[display] theme editorial`;
  the screen switches theme live; the radar ring changes.
- Out of range: `curl -s -X POST -H 'Content-Type: application/json' -H 'X-Contrail: 1'
  -d '{"radius_nm":500}' http://contrail.local/api/settings` → 400 with the reason; nothing saved.
- Guard: the same without `-H 'X-Contrail: 1'` → 403.
- Secrets: `curl -s http://contrail.local/api/settings | grep -c '"set"'` → 3 (the three secrets), and the
  saved OpenSky secret's text never appears.
- Time zone: pick "Central — Chicago", Save; serial `[tz] set to CST6CDT,...`; the Board clock moves
  back an hour. Pick Automatic, Save; the clock returns and `[tz] now <-05>5` appears.
- Hardware: tick Swap red/blue, Save; the page says restarting, the board restarts, colours swap;
  untick and save again.
- With the page open (it polls nothing, but load it a few times), `[health]` minheap stays
  ≥ 30 KB and `[render]` steady max is no worse than before (≈ 36 ms).

- [ ] **Step 3: Touch panel**

Hold 1 s on several screens: ring, then panel. Brightness slider dims live, saves on release
(serial shows the setting saved, no frame stall). Theme, radius, screen mode work; Done returns to
the screen the hold began on; 30 s idle closes it. `alert emerg` over serial while the panel is
open: the alert takes over.

- [ ] **Step 4: Firmware upload**

Upload `.pio/build/cyd2usb/firmware.bin` from the page: the device shows Updating with a rising
percentage, restarts, boots, settings kept. Then upload a truncated copy
(`head -c 500000 .pio/build/cyd2usb/firmware.bin > /tmp/bad.bin`): the page reports the failure,
the device returns to its screens, polls resume, still running the old build.

- [ ] **Step 5: Record**

Add a "Configuration (sub-project 4)" section to `docs/HARDWARE_TESTING.md` with the date, flash
used before/after, heap and minheap with the page open, steady max frame time, and the result of
each check above (pass/fail with any notes). Set the spec's `Status:` line to
`Built (plan docs/superpowers/plans/2026-11-04-contrail-config.md)`.

```bash
git add ../docs/HARDWARE_TESTING.md ../docs/superpowers/specs/2026-11-04-contrail-config-design.md
git commit -m "docs: configuration hardware results"
```
