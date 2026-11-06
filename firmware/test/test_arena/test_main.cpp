#include <unity.h>

#include <cstdint>

#include "screens/ScreenArena.h"

void setUp() {}
void tearDown() {}

struct Small {
    int a = 7;
    uint8_t b[16] = {};
};
struct Other {
    uint32_t x = 0xFEED;
};

static scr::ScreenArena arena;

static void test_claim_builds_fresh_state_and_moves_ownership() {
    int ownerA = 0, ownerB = 0;
    Small* s = arena.claim<Small>(&ownerA);
    TEST_ASSERT_EQUAL(7, s->a);
    s->a = 99;
    TEST_ASSERT_TRUE(arena.owns(&ownerA));
    Other* o = arena.claim<Other>(&ownerB);
    TEST_ASSERT_FALSE(arena.owns(&ownerA));
    TEST_ASSERT_TRUE(arena.owns(&ownerB));
    TEST_ASSERT_EQUAL_HEX32(0xFEED, o->x);
    s = arena.claim<Small>(&ownerA);
    TEST_ASSERT_EQUAL(7, s->a);  // a new object, not what the last owner left there
}

static void test_arena_array_is_empty_until_attached() {
    scr::ArenaArray<Small, 4> a;
    int n = 0;
    for (auto& s : a) n += s.a;
    TEST_ASSERT_EQUAL(0, n);
    Small store[4];
    a.attach(store);
    for (auto& s : a) n += s.a;
    TEST_ASSERT_EQUAL(28, n);
    a[2].a = 1;
    TEST_ASSERT_EQUAL(1, store[2].a);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_claim_builds_fresh_state_and_moves_ownership);
    RUN_TEST(test_arena_array_is_empty_until_attached);
    return UNITY_END();
}
