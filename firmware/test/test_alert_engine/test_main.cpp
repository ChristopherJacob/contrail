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
    e.dismiss(s, 50000);
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

// The squawk the alert was raised for stays, though the pilot changes code; the numbers still follow.
static void test_the_alerting_squawk_stays() {
    Scene s;
    AlertEngine e = engine();
    plane(s, 1).emergencyStatus = true;  // showing while the second one queues
    SceneFlight& f = plane(s, 2, "DAL22");
    std::strcpy(f.squawk, "7600"), s.pollSeq = 1;
    e.update(s, 1000);
    s.pollSeq = 2;
    e.update(s, 6000);  // confirmed, queued
    TEST_ASSERT_EQUAL(1, e.queued());
    std::strcpy(f.squawk, "1200"), s.pollSeq = 3;
    e.update(s, 11000);
    e.update(s, 31000);  // the first ends
    TEST_ASSERT_EQUAL_HEX32(2, s.alert.flight.hex);
    TEST_ASSERT_EQUAL_STRING("7600", s.alert.flight.squawk);
    std::strcpy(f.squawk, "7700"), f.altFt = 12000, f.emergencyStatus = true, s.pollSeq = 4;
    e.update(s, 36000);
    TEST_ASSERT_EQUAL_STRING("7600", s.alert.flight.squawk);
    TEST_ASSERT_FALSE(s.alert.flight.emergencyStatus);
    TEST_ASSERT_EQUAL(12000, s.alert.flight.altFt);
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
    e.dismiss(s, 2000);  // the next starts at once, not on the next update
    TEST_ASSERT_TRUE(s.alert.active);
    TEST_ASSERT_EQUAL(int(AlertKind::Military), int(s.alert.kind));
    TEST_ASSERT_EQUAL_UINT32(2000, s.alert.startMs);
    TEST_ASSERT_EQUAL(1, e.queued());
    e.dismiss(s, 3000);
    TEST_ASSERT_EQUAL(int(AlertKind::LowPass), int(s.alert.kind));
    e.dismiss(s, 4000);
    TEST_ASSERT_FALSE(s.alert.active);
    e.update(s, 5000);
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
    e.dismiss(s, 4000);
    e.update(s, 4000);
    TEST_ASSERT_EQUAL(int(AlertKind::Military), int(s.alert.kind));
}

static void test_stale_queued_alerts_are_skipped_but_emergencies_kept() {
    Scene s;
    AlertEngine e = engine();
    plane(s, 100).emergencyStatus = true;
    lowPlane(s, 1);
    e.update(s, 1000);
    lowPlane(s, 1);  // still in view until a moment before its turn
    e.update(s, 30000);
    s.count = 1;  // the low plane leaves
    e.update(s, 31000);  // the emergency ends: the low pass waits for its plane to come back
    TEST_ASSERT_FALSE(s.alert.active);
    TEST_ASSERT_EQUAL(1, e.queued());
    e.update(s, 60000);
    TEST_ASSERT_EQUAL(1, e.queued());
    e.update(s, 60001);  // gone over 30 s: dropped
    TEST_ASSERT_FALSE(s.alert.active);
    TEST_ASSERT_EQUAL(0, e.queued());

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

static void test_a_queued_plane_missing_a_poll_still_shows() {
    Scene s;
    AlertEngine e = engine();
    plane(s, 100).emergencyStatus = true;
    plane(s, 2).military = true;
    lowPlane(s, 1);
    e.update(s, 1000);  // the emergency shows; military and the low pass wait
    s.count = 1;
    lowPlane(s, 1);  // the military plane drops out of a poll just before its turn
    e.update(s, 31000);
    TEST_ASSERT_TRUE(s.alert.active);  // the low pass goes ahead of it
    TEST_ASSERT_EQUAL(int(AlertKind::LowPass), int(s.alert.kind));
    TEST_ASSERT_EQUAL(1, e.queued());
    plane(s, 2).military = true;  // back
    e.update(s, 61000);
    TEST_ASSERT_TRUE(s.alert.active);
    TEST_ASSERT_EQUAL(int(AlertKind::Military), int(s.alert.kind));
    TEST_ASSERT_TRUE(s.alert.live);
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

static void test_switching_one_kind_off_drops_its_alerts() {
    Scene s;
    AlertEngine e = engine();
    plane(s, 1).military = true;
    lowPlane(s, 2);
    plane(s, 3).military = true;
    e.update(s, 1000);
    TEST_ASSERT_EQUAL_HEX32(1, s.alert.flight.hex);
    TEST_ASSERT_EQUAL(2, e.queued());
    AlertConfig c = allOn();
    c.military = false;
    e.configure(c);
    TEST_ASSERT_EQUAL(1, e.queued());  // plane 3's military alert went
    TEST_ASSERT_EQUAL_HEX32(2, e.queuedHex(0));
    e.update(s, 2000);  // the military alert showing ends; the low pass is next
    TEST_ASSERT_TRUE(s.alert.active);
    TEST_ASSERT_EQUAL(int(AlertKind::LowPass), int(s.alert.kind));
    TEST_ASSERT_EQUAL_UINT32(2000, s.alert.startMs);
    c.emergency = false;  // not the kind showing: it carries on
    e.configure(c);
    e.update(s, 3000);
    TEST_ASSERT_TRUE(s.alert.active);
    TEST_ASSERT_EQUAL_UINT32(2000, s.alert.startMs);
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

static void test_a_plane_in_view_stays_remembered() {
    Scene s;
    AlertEngine e = engine();
    plane(s, 0xAE0001, "RCH471").military = true;  // stays in view throughout
    e.update(s, 1000);
    TEST_ASSERT_EQUAL_HEX32(0xAE0001, s.alert.flight.hex);
    e.dismiss(s, 1000);
    // 20 others pass through one at a time, each alerting and taking a slot.
    for (uint32_t h = 1; h <= 20; ++h) {
        s.count = 1;
        plane(s, h).military = true;
        e.update(s, h * 40000);
        TEST_ASSERT_TRUE(s.alert.active);
        TEST_ASSERT_EQUAL_HEX32(h, s.alert.flight.hex);
        e.dismiss(s, h * 40000);
    }
    s.count = 1;
    e.update(s, 21 * 40000);
    TEST_ASSERT_FALSE(s.alert.active);  // never forgotten, so never alerted again
    TEST_ASSERT_EQUAL(0, e.queued());
}

static void test_twenty_matching_at_once_never_alert_twice() {
    Scene s;
    AlertEngine e = engine();
    for (uint32_t h = 1; h <= 20; ++h) plane(s, h).military = true;
    bool shown[21] = {};
    for (int step = 0; step < 60; ++step) {
        e.update(s, 1000 + uint32_t(step) * 1000);
        if (s.alert.active) {
            const uint32_t h = s.alert.flight.hex;
            TEST_ASSERT_TRUE(h >= 1 && h <= 20);
            TEST_ASSERT_FALSE_MESSAGE(shown[h], "a plane alerted twice");
            shown[h] = true;
            e.dismiss(s, 1000 + uint32_t(step) * 1000);
        }
    }
    int n = 0;
    for (bool b : shown) n += b;
    TEST_ASSERT_TRUE(n >= 4);  // those that overflowed the queue are dropped, not re-alerted
}

static void test_the_queue_never_holds_a_plane_twice() {
    Scene s;
    AlertEngine e = engine();
    plane(s, 100).emergencyStatus = true;  // B: showing throughout the queue's build-up
    for (uint32_t h = 1; h <= 20; ++h) plane(s, h).military = true;
    for (int step = 0; step < 10; ++step) {
        e.update(s, 1000 + uint32_t(step) * 1000);
        TEST_ASSERT_EQUAL_HEX32(100, s.alert.flight.hex);
        uint32_t seen[AlertEngine::kQueue];
        for (int i = 0; i < e.queued(); ++i) {
            seen[i] = e.queuedHex(i);
            for (int j = 0; j < i; ++j) TEST_ASSERT_NOT_EQUAL(seen[j], seen[i]);
        }
    }
}

static void test_a_queued_plane_that_turns_emergency_is_queued_once() {
    Scene s;
    AlertEngine e = engine();
    plane(s, 0xB, "DAL22").emergencyStatus = true;  // B
    SceneFlight& a = plane(s, 0xA, "RCH471");      // A
    a.military = true;
    std::strcpy(a.squawk, "7700");
    s.pollSeq = 1;
    e.update(s, 1000);
    TEST_ASSERT_EQUAL_HEX32(0xB, s.alert.flight.hex);
    TEST_ASSERT_EQUAL(1, e.queued());  // A, military
    s.pollSeq = 2;
    e.update(s, 6000);  // A's squawk confirmed
    TEST_ASSERT_EQUAL_HEX32(0xB, s.alert.flight.hex);  // an emergency is not replaced
    TEST_ASSERT_EQUAL(1, e.queued());
    e.update(s, 31000);  // B ends
    TEST_ASSERT_TRUE(s.alert.active);
    TEST_ASSERT_EQUAL_HEX32(0xA, s.alert.flight.hex);
    TEST_ASSERT_EQUAL(int(AlertKind::Emergency), int(s.alert.kind));
    e.update(s, 61000);  // A ends: no military alert for it follows
    TEST_ASSERT_FALSE(s.alert.active);
    TEST_ASSERT_EQUAL(0, e.queued());
}

static void test_a_cleared_squawk_frees_its_slot() {
    Scene s;
    AlertEngine e = engine();
    plane(s, 0xAE0001, "RCH471").military = true;  // P
    e.update(s, 1000);
    e.dismiss(s, 1000);
    // 15 planes dial through 7700 and back: none alerted, so none should keep a slot.
    s.count = 0;
    for (uint32_t h = 1; h <= 15; ++h) std::strcpy(plane(s, h).squawk, "7700");
    s.pollSeq = 1;
    e.update(s, 2000);
    for (int i = 0; i < s.count; ++i) std::strcpy(s.flights[i].squawk, "1200");
    s.pollSeq = 2;
    e.update(s, 3000);
    TEST_ASSERT_FALSE(s.alert.active);
    s.count = 0;
    plane(s, 0x200).military = true;  // Q needs a slot: one must be free without forgetting P
    e.update(s, 4000);
    TEST_ASSERT_EQUAL_HEX32(0x200, s.alert.flight.hex);
    e.dismiss(s, 4000);
    s.count = 0;
    plane(s, 0xAE0001, "RCH471").military = true;
    e.update(s, 5000);
    TEST_ASSERT_FALSE(s.alert.active);  // P still remembered
}

static void test_an_emergency_waits_for_the_emergency_showing() {
    Scene s;
    AlertEngine e = engine();
    plane(s, 1).emergencyStatus = true;
    e.update(s, 1000);
    plane(s, 2, "DAL22").emergencyStatus = true;
    e.update(s, 5000);
    TEST_ASSERT_EQUAL_HEX32(1, s.alert.flight.hex);
    TEST_ASSERT_EQUAL_UINT32(1000, s.alert.startMs);
    TEST_ASSERT_EQUAL(1, e.queued());
    e.update(s, 31000);
    TEST_ASSERT_EQUAL_HEX32(2, s.alert.flight.hex);
    TEST_ASSERT_EQUAL(int(AlertKind::Emergency), int(s.alert.kind));
    TEST_ASSERT_EQUAL_UINT32(31000, s.alert.startMs);
}

// Two queued emergencies whose planes both leave: both are shown. The engine keeps the first one's
// numbers; the second (three emergencies at once) is known by its hex alone.
static void test_queued_emergencies_outlast_their_planes() {
    Scene s;
    AlertEngine e = engine();
    plane(s, 100).emergencyStatus = true;
    e.update(s, 1000);
    plane(s, 101, "DAL22").emergencyStatus = true;
    plane(s, 102, "SWA9").emergencyStatus = true;
    e.update(s, 2000);
    TEST_ASSERT_EQUAL(2, e.queued());
    s.count = 1;  // both leave
    e.update(s, 31000);
    TEST_ASSERT_EQUAL_HEX32(101, s.alert.flight.hex);
    TEST_ASSERT_EQUAL_STRING("DAL22", s.alert.flight.callsign);
    TEST_ASSERT_FALSE(s.alert.live);
    e.update(s, 61000);
    TEST_ASSERT_TRUE(s.alert.active);
    TEST_ASSERT_EQUAL_HEX32(102, s.alert.flight.hex);
    TEST_ASSERT_EQUAL(int(AlertKind::Emergency), int(s.alert.kind));
    TEST_ASSERT_FALSE(s.alert.live);
}

// Alerts spec §1: about 600 B of static RAM, and the settings copy (288 B).
static void test_static_ram() { TEST_ASSERT_LESS_OR_EQUAL(700, int(sizeof(AlertEngine))); }

static void test_millis_wraparound() {
    const uint32_t t0 = 0xFFFFF000u;
    Scene s;
    AlertEngine e = engine();
    plane(s, 100).emergencyStatus = true;
    lowPlane(s, 1);
    e.update(s, t0);
    TEST_ASSERT_EQUAL_UINT32(t0 + 30000u, s.alert.endsMs);  // wrapped past 0
    s.count = 1;  // the low plane leaves
    e.update(s, t0 + 29999u);
    TEST_ASSERT_TRUE(s.alert.active);
    TEST_ASSERT_EQUAL_HEX32(100, s.alert.flight.hex);
    e.update(s, t0 + 31000u);  // the emergency ended; the low pass has left
    TEST_ASSERT_FALSE(s.alert.active);
    TEST_ASSERT_EQUAL(0, e.queued());

    Scene t;
    AlertEngine e2 = engine();
    plane(t, 100).emergencyStatus = true;
    lowPlane(t, 1);
    e2.update(t, t0);
    e2.update(t, t0 + 30000u);  // ends exactly now, past 0: the low pass is next
    TEST_ASSERT_TRUE(t.alert.active);
    TEST_ASSERT_EQUAL(int(AlertKind::LowPass), int(t.alert.kind));
    TEST_ASSERT_EQUAL_UINT32(t0 + 60000u, t.alert.endsMs);

    // Memory ages correctly across the wrap: the plane seen least recently is the one forgotten.
    Scene m;
    AlertEngine e3 = engine();
    for (uint32_t h = 1; h <= 17; ++h) {
        m.count = 0;
        plane(m, h).military = true;
        e3.update(m, t0 + (h - 1) * 1000u);  // crosses 0 after the fourth
        e3.dismiss(m, t0 + (h - 1) * 1000u);
    }
    m.count = 0;
    plane(m, 17).military = true;
    e3.update(m, t0 + 20000u);
    TEST_ASSERT_FALSE(m.alert.active);  // the newest is remembered
    m.count = 0;
    plane(m, 1).military = true;
    e3.update(m, t0 + 21000u);
    TEST_ASSERT_TRUE(m.alert.active);  // the oldest was forgotten
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_a_plane_alerts_once);
    RUN_TEST(test_a_higher_kind_alerts_again_a_lower_one_does_not);
    RUN_TEST(test_an_emergency_squawk_needs_two_polls);
    RUN_TEST(test_a_squawk_that_goes_back_starts_over);
    RUN_TEST(test_the_alerting_squawk_stays);
    RUN_TEST(test_a_declared_emergency_alerts_at_once);
    RUN_TEST(test_alerts_queue_in_priority_order);
    RUN_TEST(test_an_emergency_replaces_the_alert_showing);
    RUN_TEST(test_the_queue_holds_four_and_drops_the_lowest);
    RUN_TEST(test_stale_queued_alerts_are_skipped_but_emergencies_kept);
    RUN_TEST(test_a_queued_plane_missing_a_poll_still_shows);
    RUN_TEST(test_memory_forgets_the_oldest_plane);
    RUN_TEST(test_the_alert_follows_the_plane);
    RUN_TEST(test_switching_every_kind_off_clears_alerts);
    RUN_TEST(test_switching_one_kind_off_drops_its_alerts);
    RUN_TEST(test_quiet_hours_soften_all_but_emergencies);
    RUN_TEST(test_inject_shows_a_made_up_alert);
    RUN_TEST(test_a_plane_in_view_stays_remembered);
    RUN_TEST(test_twenty_matching_at_once_never_alert_twice);
    RUN_TEST(test_the_queue_never_holds_a_plane_twice);
    RUN_TEST(test_a_queued_plane_that_turns_emergency_is_queued_once);
    RUN_TEST(test_a_cleared_squawk_frees_its_slot);
    RUN_TEST(test_an_emergency_waits_for_the_emergency_showing);
    RUN_TEST(test_queued_emergencies_outlast_their_planes);
    RUN_TEST(test_static_ram);
    RUN_TEST(test_millis_wraparound);
    return UNITY_END();
}
