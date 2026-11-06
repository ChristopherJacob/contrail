#include <unity.h>

#include <cstring>
#include <map>
#include <string>

#include "alerts/AlertConfig.h"
#include "alerts/AlertRules.h"
#include "services/CoreSettings.h"

using alerts::AlertConfig;
using alerts::kindBit;
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
    TEST_ASSERT_EQUAL(22 * 60, alerts::parseClockMinutes(" 22:00 ", -1));
    TEST_ASSERT_EQUAL(7 * 60 + 5, alerts::parseClockMinutes("\t07:05\n", -1));
    TEST_ASSERT_EQUAL(-1, alerts::parseClockMinutes("7:5", -1));
    TEST_ASSERT_EQUAL(-1, alerts::parseClockMinutes("07:005", -1));
    TEST_ASSERT_EQUAL(-1, alerts::parseClockMinutes("007:05", -1));
    TEST_ASSERT_EQUAL(-1, alerts::parseClockMinutes(":05", -1));
    TEST_ASSERT_EQUAL(-1, alerts::parseClockMinutes("7 :05", -1));
    TEST_ASSERT_EQUAL(-1, alerts::parseClockMinutes("-1:05", -1));
    TEST_ASSERT_EQUAL(-1, alerts::parseClockMinutes("   ", -1));
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
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(kindBit(AlertKind::Watchlist), m.kinds, l);
        TEST_ASSERT_EQUAL_STRING(c.entries[0], m.matched);
    }
    alerts::parseWatchlist("DAL*, A321", c);
    TEST_ASSERT_EQUAL_HEX8(0, alerts::matchFlight(cruiser(), c, false).kinds);
}

static void test_emergency() {
    AlertConfig c = allOn();
    SceneFlight f = cruiser();
    f.emergencyStatus = true;
    TEST_ASSERT_EQUAL_HEX8(kindBit(AlertKind::Emergency), alerts::matchFlight(f, c, false).kinds);
    f.emergencyStatus = false;
    std::strcpy(f.squawk, "7700");
    TEST_ASSERT_EQUAL_HEX8(0, alerts::matchFlight(f, c, false).kinds);  // not yet seen on two polls
    TEST_ASSERT_EQUAL_HEX8(kindBit(AlertKind::Emergency), alerts::matchFlight(f, c, true).kinds);
    std::strcpy(f.squawk, "1200");
    TEST_ASSERT_EQUAL_HEX8(0, alerts::matchFlight(f, c, true).kinds);
}

static void test_military_and_low_pass() {
    AlertConfig c = allOn();
    SceneFlight f = cruiser();
    f.military = true;
    TEST_ASSERT_EQUAL_HEX8(kindBit(AlertKind::Military), alerts::matchFlight(f, c, false).kinds);
    f.military = false;
    f.altFt = 2999, f.distNm = 1.9f;
    TEST_ASSERT_EQUAL_HEX8(kindBit(AlertKind::LowPass), alerts::matchFlight(f, c, false).kinds);
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
    TEST_ASSERT_EQUAL(int(AlertKind::Military), int(alerts::highest(kindBit(AlertKind::Military) | kindBit(AlertKind::LowPass))));
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

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_watchlist_parsing);
    RUN_TEST(test_watchlist_keeps_at_most_24_entries);
    RUN_TEST(test_clock_parsing);
    RUN_TEST(test_config_from_default_settings);
    RUN_TEST(test_config_from_changed_settings);
    RUN_TEST(test_entry_matching);
    RUN_TEST(test_watchlist_matches_each_field);
    RUN_TEST(test_emergency);
    RUN_TEST(test_military_and_low_pass);
    RUN_TEST(test_kinds_switched_off_never_match);
    RUN_TEST(test_highest_kind);
    RUN_TEST(test_reason_lines);
    RUN_TEST(test_serial_command);
    return UNITY_END();
}
