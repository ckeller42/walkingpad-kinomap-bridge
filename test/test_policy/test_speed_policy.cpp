#include <unity.h>
#include "speed_policy.h"

void setUp(void) {}
void tearDown(void) {}

void test_sentinel_is_a_stop(void) {
    policy::SpeedPolicy p;
    auto d = p.onTarget(100, 0);                 // exactly 1.00 km/h
    TEST_ASSERT_EQUAL(policy::Action::Stop, d.action);
}

void test_sentinel_while_already_stopped_emits_nothing(void) {
    policy::SpeedPolicy p;
    p.onTarget(100, 0);
    auto d = p.onTarget(100, 5000);
    TEST_ASSERT_EQUAL(policy::Action::None, d.action);   // no second beep
}

void test_target_above_max_is_clamped_to_six(void) {
    policy::SpeedPolicy p;
    auto d = p.onTarget(900, 0);                 // 9.00 km/h, above advertised max
    TEST_ASSERT_EQUAL(policy::Action::SetSpeed, d.action);
    TEST_ASSERT_EQUAL_UINT8(60, d.tenthKmh);     // 6.0 km/h
}

void test_small_change_inside_deadband_is_dropped(void) {
    policy::SpeedPolicy p;
    p.onTarget(400, 0);                          // 4.00 -> sent
    auto d = p.onTarget(420, 2000);              // +0.2 km/h, inside 0.3 deadband
    TEST_ASSERT_EQUAL(policy::Action::None, d.action);
}

void test_change_outside_deadband_is_sent(void) {
    policy::SpeedPolicy p;
    p.onTarget(400, 0);
    auto d = p.onTarget(440, 2000);              // +0.4 km/h
    TEST_ASSERT_EQUAL(policy::Action::SetSpeed, d.action);
    TEST_ASSERT_EQUAL_UINT8(44, d.tenthKmh);
}

void test_rate_limit_suppresses_second_command_within_one_second(void) {
    policy::SpeedPolicy p;
    p.onTarget(400, 0);
    auto d = p.onTarget(500, 900);               // big change, but only 900 ms later
    TEST_ASSERT_EQUAL(policy::Action::None, d.action);
}

void test_manual_override_resyncs_reference_after_three_polls(void) {
    policy::SpeedPolicy p;
    p.onTarget(400, 0);                          // reference = 4.0
    for (int i = 0; i < 3; i++) p.onBeltSpeed(20);   // remote dropped it to 2.0
    auto d = p.onTarget(400, 10000);             // same target as before
    TEST_ASSERT_EQUAL(policy::Action::SetSpeed, d.action);  // no longer suppressed
    TEST_ASSERT_EQUAL_UINT8(40, d.tenthKmh);
}

void test_two_divergent_polls_do_not_resync(void) {
    policy::SpeedPolicy p;
    p.onTarget(400, 0);
    p.onBeltSpeed(20);
    p.onBeltSpeed(20);
    auto d = p.onTarget(400, 10000);
    TEST_ASSERT_EQUAL(policy::Action::None, d.action);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_sentinel_is_a_stop);
    RUN_TEST(test_sentinel_while_already_stopped_emits_nothing);
    RUN_TEST(test_target_above_max_is_clamped_to_six);
    RUN_TEST(test_small_change_inside_deadband_is_dropped);
    RUN_TEST(test_change_outside_deadband_is_sent);
    RUN_TEST(test_rate_limit_suppresses_second_command_within_one_second);
    RUN_TEST(test_manual_override_resyncs_reference_after_three_polls);
    RUN_TEST(test_two_divergent_polls_do_not_resync);
    return UNITY_END();
}
