#include <unity.h>

void setUp() {}
void tearDown() {}

static void test_toolchain_runs() { TEST_ASSERT_EQUAL(4, 2 + 2); }

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_toolchain_runs);
    return UNITY_END();
}
