#include <unity.h>

#include <cstring>
#include <map>
#include <string>

#include "app/PanelBridge.h"
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
    RUN_TEST(test_panel_values_follow_the_settings);
    RUN_TEST(test_edits_apply_and_queue_in_order);
    return UNITY_END();
}
