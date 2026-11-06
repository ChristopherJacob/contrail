#include <unity.h>

#include <cmath>
#include <cstring>
#include <initializer_list>

#include "data/AircraftTypes.h"
#include "data/Airlines.h"
#include "data/Stars.h"

void setUp() {}
void tearDown() {}

static void test_type_names() {
    TEST_ASSERT_EQUAL_STRING("Boeing 787-9", data::aircraftTypeName("B789"));
    TEST_ASSERT_EQUAL_STRING("Airbus A320neo", data::aircraftTypeName("A20N"));
    TEST_ASSERT_EQUAL_STRING("Cessna 172", data::aircraftTypeName("C172"));
    TEST_ASSERT_EQUAL_STRING("Boeing C-17", data::aircraftTypeName("C17"));
    TEST_ASSERT_EQUAL_STRING("Embraer E175", data::aircraftTypeName("E75L"));  // ICAO code ADS-B reports
    TEST_ASSERT_NULL(data::aircraftTypeName("ZZZZ"));
    TEST_ASSERT_NULL(data::aircraftTypeName(""));
}

static void test_type_codes_unique() {
    for (int i = 0; i < data::kAircraftTypeCount; ++i)
        for (int j = i + 1; j < data::kAircraftTypeCount; ++j)
            TEST_ASSERT_NOT_EQUAL(0, std::strcmp(data::kAircraftTypes[i].code, data::kAircraftTypes[j].code));
}

static void test_airline_from_callsign() {
    const data::Airline* a = data::airlineForCallsign("UAL53");
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_EQUAL_STRING("United", a->name);
    TEST_ASSERT_NOT_NULL(data::airlineForCallsign("DAL2210"));
    TEST_ASSERT_NULL(data::airlineForCallsign("N512KA"));  // a registration, not an airline
    TEST_ASSERT_NULL(data::airlineForCallsign("UA"));
    TEST_ASSERT_NULL(data::airlineForCallsign("XYZ123"));
    TEST_ASSERT_EQUAL_PTR(a, data::airlineForCallsign("ual53"));  // case does not matter
    char code[4];
    TEST_ASSERT_TRUE(data::callsignAirline("dal22", code));
    TEST_ASSERT_EQUAL_STRING("DAL", code);
}

static void test_callsign_airline_and_type_codes() {
    char a[4];
    TEST_ASSERT_TRUE(data::callsignAirline("UAL53", a));
    TEST_ASSERT_EQUAL_STRING("UAL", a);
    TEST_ASSERT_TRUE(data::callsignAirline("XAX123", a));  // not in the table, still an airline prefix
    TEST_ASSERT_EQUAL_STRING("XAX", a);
    TEST_ASSERT_FALSE(data::callsignAirline("N512KA", a));
    TEST_ASSERT_FALSE(data::callsignAirline("", a));
    TEST_ASSERT_FALSE(data::callsignAirline(nullptr, a));
    TEST_ASSERT_NOT_NULL(data::airlineByCode("DAL"));
    TEST_ASSERT_NULL(data::airlineByCode("XAX"));
    // Any well-formed ICAO type designator counts, in the name table or not.
    for (const char* ok : {"B738", "C30J", "C17", "H2", "A3ST"}) TEST_ASSERT_TRUE_MESSAGE(data::validTypeCode(ok), ok);
    for (const char* bad : {"", "ZZZZ", "GRND", "SERV", "b738", "7B38", "B7 8", "B7378", "B", "B-38"})
        TEST_ASSERT_FALSE_MESSAGE(data::validTypeCode(bad), bad);
    TEST_ASSERT_FALSE(data::validTypeCode(nullptr));
    TEST_ASSERT_NULL(data::aircraftTypeName("C30J"));  // rare: valid, but shown as the raw code
}

static void test_star_table() {
    TEST_ASSERT_TRUE(data::kStarCount > 200 && data::kStarCount < 320);
    bool sirius = false, polaris = false;
    for (int i = 0; i < data::kStarCount; ++i) {
        const data::Star& s = data::kStars[i];
        TEST_ASSERT_TRUE(s.raDeg >= 0 && s.raDeg < 360 && s.decDeg >= -90 && s.decDeg <= 90);
        if (s.mag < -1.3f && std::fabs(s.raDeg - 101.29f) < 0.5f) sirius = true;
        if (s.decDeg > 89 && s.mag < 2.1f) polaris = true;
    }
    TEST_ASSERT_TRUE(sirius);
    TEST_ASSERT_TRUE(polaris);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_type_names);
    RUN_TEST(test_type_codes_unique);
    RUN_TEST(test_airline_from_callsign);
    RUN_TEST(test_callsign_airline_and_type_codes);
    RUN_TEST(test_star_table);
    return UNITY_END();
}
