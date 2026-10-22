#include <unity.h>

#include "battery.h"

void setUp(void) {}
void tearDown(void) {}

// --- unknown wins over everything ---------------------------------------
void test_negative_percent_is_unknown(void) {
    TEST_ASSERT_EQUAL(battery::State::Unknown, battery::classify(-1, false));
}
void test_negative_percent_is_unknown_even_if_charging(void) {
    // On USB with no cell, isCharging() may read true while level is -1.
    TEST_ASSERT_EQUAL(battery::State::Unknown, battery::classify(-1, true));
}

// --- charging dominates the level states --------------------------------
void test_charging_at_low_level_is_charging(void) {
    TEST_ASSERT_EQUAL(battery::State::Charging, battery::classify(5, true));
}
void test_charging_when_full_is_charging(void) {
    TEST_ASSERT_EQUAL(battery::State::Charging, battery::classify(100, true));
}

// --- discharging bands, boundary-exact ----------------------------------
void test_zero_is_critical(void) {
    TEST_ASSERT_EQUAL(battery::State::Critical, battery::classify(0, false));
}
void test_ten_is_critical(void) {
    TEST_ASSERT_EQUAL(battery::State::Critical, battery::classify(battery::kCriticalPct, false));
}
void test_eleven_is_low(void) {
    TEST_ASSERT_EQUAL(battery::State::Low, battery::classify(battery::kCriticalPct + 1, false));
}
void test_twenty_is_low(void) {
    TEST_ASSERT_EQUAL(battery::State::Low, battery::classify(battery::kLowPct, false));
}
void test_twentyone_is_ok(void) {
    TEST_ASSERT_EQUAL(battery::State::Ok, battery::classify(battery::kLowPct + 1, false));
}
void test_full_is_ok(void) {
    TEST_ASSERT_EQUAL(battery::State::Ok, battery::classify(100, false));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_negative_percent_is_unknown);
    RUN_TEST(test_negative_percent_is_unknown_even_if_charging);
    RUN_TEST(test_charging_at_low_level_is_charging);
    RUN_TEST(test_charging_when_full_is_charging);
    RUN_TEST(test_zero_is_critical);
    RUN_TEST(test_ten_is_critical);
    RUN_TEST(test_eleven_is_low);
    RUN_TEST(test_twenty_is_low);
    RUN_TEST(test_twentyone_is_ok);
    RUN_TEST(test_full_is_ok);
    return UNITY_END();
}
