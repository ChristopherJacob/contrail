#include <unity.h>

#include "FakeHttp.h"
#include "providers/ProviderChain.h"
#include "services/EventBus.h"
#include "services/Scheduler.h"

using namespace prov;

void setUp() {}
void tearDown() {}

class FakeProvider : public IProvider {
   public:
    FakeProvider(const char* n, bool configured = true) : name_(n), configured_(configured) {}
    const char* name() const override { return name_; }
    bool configured() const override { return configured_; }
    uint32_t pollIntervalMs() const override { return 1000; }
    bool poll(hal::IHttp&, model::AircraftStore&, const PollContext&) override {
        ++calls;
        return ok;
    }
    bool ok = true;
    int calls = 0;

   private:
    const char* name_;
    bool configured_;
};

static FakeHttp http;
static model::AircraftStore store;
static PollContext at(uint32_t ms) { return {ms, 0, 0, 0, 25}; }

static void test_primary_ok_stays() {
    FakeProvider a("a"), b("b");
    IProvider* ps[] = {&a, &b};
    ProviderChain c;
    c.setProviders(ps, 2);
    for (int i = 0; i < 5; ++i) TEST_ASSERT_TRUE(c.poll(http, store, at(i * 1000)));
    TEST_ASSERT_EQUAL_STRING("a", c.activeName());
    TEST_ASSERT_EQUAL(0, b.calls);
}

static void test_switches_after_three_failures_and_returns() {
    FakeProvider a("a"), b("b");
    a.ok = false;
    IProvider* ps[] = {&a, &b};
    ProviderChain c;
    c.setProviders(ps, 2);
    c.poll(http, store, at(1000));
    c.poll(http, store, at(2000));
    TEST_ASSERT_EQUAL_STRING("a", c.activeName());
    c.poll(http, store, at(3000));
    TEST_ASSERT_EQUAL_STRING("b", c.activeName());
    TEST_ASSERT_TRUE(c.onFallback());
    TEST_ASSERT_TRUE(c.poll(http, store, at(4000)));
    TEST_ASSERT_EQUAL(3, a.calls);
    // Before 60 s: no primary probe.
    c.poll(http, store, at(62000));
    TEST_ASSERT_EQUAL(3, a.calls);
    // After 60 s the primary is probed; it has recovered, so we switch back.
    a.ok = true;
    TEST_ASSERT_TRUE(c.poll(http, store, at(63001)));
    TEST_ASSERT_EQUAL_STRING("a", c.activeName());
    TEST_ASSERT_FALSE(c.onFallback());
}

static void test_unconfigured_skipped() {
    FakeProvider local("local", false), comm("community");
    IProvider* ps[] = {&local, &comm};
    ProviderChain c;
    c.setProviders(ps, 2);
    TEST_ASSERT_EQUAL_STRING("community", c.activeName());
    TEST_ASSERT_TRUE(c.poll(http, store, at(0)));
    TEST_ASSERT_EQUAL(0, local.calls);
}

static void test_no_fallback_when_disabled() {
    FakeProvider a("a"), b("b");
    a.ok = false;
    IProvider* ps[] = {&a, &b};
    ProviderChain c;
    c.setProviders(ps, 2);
    c.setFallbackEnabled(false);
    for (int i = 0; i < 6; ++i) c.poll(http, store, at(i * 1000));
    TEST_ASSERT_EQUAL_STRING("a", c.activeName());
    TEST_ASSERT_EQUAL(0, b.calls);
}

static void test_scheduler_intervals() {
    svc::Scheduler s;
    int fast = 0, slow = 0;
    int id = s.every(100, [&](uint32_t) { ++fast; });
    s.every(1000, [&](uint32_t) { ++slow; });
    for (uint32_t t = 0; t <= 1000; t += 50) s.tick(t);
    TEST_ASSERT_EQUAL(11, fast);  // t = 0, 100, ..., 1000
    TEST_ASSERT_EQUAL(2, slow);   // t = 0 and 1000
    s.setInterval(id, 500);
    fast = 0;
    for (uint32_t t = 1050; t <= 2000; t += 50) s.tick(t);
    TEST_ASSERT_EQUAL(2, fast);  // 1500, 2000
    s.runSoon(id);
    s.tick(2001);
    TEST_ASSERT_EQUAL(3, fast);
}

static void test_event_bus() {
    svc::EventBus bus;
    int got = -1;
    svc::Ev last = svc::Ev::NetworkChanged;
    bus.subscribe([&](svc::Ev e, int a) { last = e, got = a; });
    bus.publish(svc::Ev::AircraftUpdated, 7);
    TEST_ASSERT_EQUAL(7, got);
    TEST_ASSERT_TRUE(last == svc::Ev::AircraftUpdated);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_primary_ok_stays);
    RUN_TEST(test_switches_after_three_failures_and_returns);
    RUN_TEST(test_unconfigured_skipped);
    RUN_TEST(test_no_fallback_when_disabled);
    RUN_TEST(test_scheduler_intervals);
    RUN_TEST(test_event_bus);
    return UNITY_END();
}
