#include <unity.h>

#include <cstring>
#include <map>
#include <string>

#include "services/CoreSettings.h"

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

static void test_defaults() {
    MemStore m;
    Settings s(m, kCoreSettings, kCoreSettingsCount);
    s.load();
    TEST_ASSERT_EQUAL(25, s.getInt(key::kRadius));
    TEST_ASSERT_EQUAL_STRING("community", s.getEnum(key::kProvider).c_str());
    TEST_ASSERT_TRUE(s.getBool(key::kFallback));
    TEST_ASSERT_EQUAL_STRING("", s.getString(key::kLocalUrl).c_str());
}

static void test_set_persists_and_reloads() {
    MemStore m;
    {
        Settings s(m, kCoreSettings, kCoreSettingsCount);
        s.load();
        TEST_ASSERT_TRUE(s.set(key::kLat, "37.6189"));
        TEST_ASSERT_TRUE(s.set(key::kProvider, "opensky"));
        TEST_ASSERT_TRUE(s.set(key::kLocalUrl, "http://pi.local/data/aircraft.json"));
    }
    Settings s2(m, kCoreSettings, kCoreSettingsCount);
    s2.load();
    TEST_ASSERT_FLOAT_WITHIN(1e-4, 37.6189, s2.getFloat(key::kLat));
    TEST_ASSERT_EQUAL_STRING("opensky", s2.getEnum(key::kProvider).c_str());
    TEST_ASSERT_EQUAL_STRING("http://pi.local/data/aircraft.json", s2.getString(key::kLocalUrl).c_str());
}

static void test_out_of_range_clamped_on_load() {
    MemStore m;
    m.kv[key::kRadius] = "500";
    m.kv[key::kBrightness] = "-3";
    Settings s(m, kCoreSettings, kCoreSettingsCount);
    s.load();
    TEST_ASSERT_EQUAL(100, s.getInt(key::kRadius));
    TEST_ASSERT_EQUAL(5, s.getInt(key::kBrightness));
}

static void test_invalid_values_rejected() {
    MemStore m;
    Settings s(m, kCoreSettings, kCoreSettingsCount);
    s.load();
    TEST_ASSERT_FALSE(s.set(key::kProvider, "flightradar"));
    TEST_ASSERT_FALSE(s.set(key::kRadius, "abc"));
    TEST_ASSERT_FALSE(s.set("no_such_key", "1"));
    TEST_ASSERT_EQUAL_STRING("community", s.getEnum(key::kProvider).c_str());
}

static void test_change_listener_fires() {
    MemStore m;
    Settings s(m, kCoreSettings, kCoreSettingsCount);
    s.load();
    std::string changed;
    s.onChange([&](const char* k) { changed = k; });
    s.set(key::kUnits, "metric");
    TEST_ASSERT_EQUAL_STRING("units", changed.c_str());
}

static void test_schema_and_secrets() {
    MemStore m;
    Settings s(m, kCoreSettings, kCoreSettingsCount);
    s.load();
    s.set(key::kAeroKey, "supersecretkey");
    JsonDocument schema;
    s.toJsonSchema(schema);
    TEST_ASSERT_EQUAL(kCoreSettingsCount, int(schema["settings"].size()));
    for (JsonVariant o : schema["settings"].as<JsonArray>())
        if (o["key"] == "aero_key") TEST_ASSERT_TRUE(o["default"].isNull());
    JsonDocument vals;
    s.toJsonValues(vals, false);
    TEST_ASSERT_EQUAL_STRING("••••", vals["aero_key"]);
    JsonDocument all;
    s.toJsonValues(all, true);
    TEST_ASSERT_EQUAL_STRING("supersecretkey", all["aero_key"]);
}

static void test_keys_fit_nvs() {
    for (int i = 0; i < kCoreSettingsCount; ++i) TEST_ASSERT_TRUE(std::strlen(kCoreSettings[i].key) <= 15);
}

static void test_screen_settings() {
    MemStore m;
    Settings s(m, kCoreSettings, kCoreSettingsCount);
    s.load();
    TEST_ASSERT_EQUAL_STRING("director", s.getEnum(key::kScreenMode).c_str());
    TEST_ASSERT_EQUAL(30, s.getInt(key::kCarouselS));
    TEST_ASSERT_TRUE(s.set(key::kScreenMode, "carousel"));
    TEST_ASSERT_EQUAL_STRING("carousel", s.getEnum(key::kScreenMode).c_str());
}

static void test_theme_setting() {
    MemStore m;
    Settings s(m, kCoreSettings, kCoreSettingsCount);
    s.load();
    TEST_ASSERT_EQUAL_STRING("cinematic", s.getEnum(key::kTheme).c_str());
    TEST_ASSERT_TRUE(s.set(key::kTheme, "editorial"));
    TEST_ASSERT_EQUAL_STRING("editorial", s.getEnum(key::kTheme).c_str());
    TEST_ASSERT_FALSE(s.set(key::kTheme, "neon"));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_defaults);
    RUN_TEST(test_set_persists_and_reloads);
    RUN_TEST(test_out_of_range_clamped_on_load);
    RUN_TEST(test_invalid_values_rejected);
    RUN_TEST(test_change_listener_fires);
    RUN_TEST(test_schema_and_secrets);
    RUN_TEST(test_keys_fit_nvs);
    RUN_TEST(test_screen_settings);
    RUN_TEST(test_theme_setting);
    return UNITY_END();
}
