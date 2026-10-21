#include <unity.h>
#include "speed_policy.h"

void test_disarmed_target_emits_nothing(void);
void test_arming_alone_does_not_start_the_belt(void);
void test_armed_target_on_stopped_belt_runs_start_sequence_then_notifies(void);
void test_armed_target_on_running_belt_is_a_plain_set_speed(void);
void test_phone_link_loss_stops_the_belt_but_stays_armed(void);
void test_reconnect_does_not_restart_the_belt_by_itself(void);
void test_ui_stop_works_while_disarmed(void);
void test_belt_link_loss_stops_notifications(void);

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

void test_stop_works_after_remote_start_following_a_stop(void) {
    policy::SpeedPolicy p;
    p.onTarget(400, 0);
    p.onTarget(100, 2000);                       // app stop
    for (int i = 0; i < 10; i++) p.onBeltSpeed(20);  // remote starts belt at 2.0
    auto d = p.onTarget(100, 30000);
    TEST_ASSERT_EQUAL(policy::Action::Stop, d.action);
}

void test_remote_start_after_stop_adopts_reference(void) {
    policy::SpeedPolicy p;
    p.onTarget(100, 0);
    for (int i = 0; i < 3; i++) p.onBeltSpeed(20);
    auto d = p.onTarget(200, 10000);             // belt already at 2.0
    TEST_ASSERT_EQUAL(policy::Action::None, d.action);
}

void test_resync_threshold_matches_send_threshold(void) {
    policy::SpeedPolicy p;
    p.onTarget(400, 0);
    for (int i = 0; i < 3; i++) p.onBeltSpeed(43);   // exactly deadband away
    auto d = p.onTarget(430, 10000);
    TEST_ASSERT_EQUAL(policy::Action::None, d.action);   // no beep to 4.3
}

void test_difference_of_exactly_deadband_is_sent(void) {
    policy::SpeedPolicy p;
    p.onTarget(400, 0);
    auto d = p.onTarget(430, 2000);
    TEST_ASSERT_EQUAL(policy::Action::SetSpeed, d.action);
    TEST_ASSERT_EQUAL_UINT8(43, d.tenthKmh);
}

void test_exactly_min_interval_is_allowed(void) {
    policy::SpeedPolicy p;
    p.onTarget(400, 0);
    TEST_ASSERT_EQUAL(policy::Action::None, p.onTarget(500, policy::kMinCommandIntervalMs - 1).action);
    TEST_ASSERT_EQUAL(policy::Action::SetSpeed, p.onTarget(500, policy::kMinCommandIntervalMs).action);
}

void test_agreeing_sample_resets_divergent_run(void) {
    policy::SpeedPolicy p;
    p.onTarget(400, 0);
    p.onBeltSpeed(20);
    p.onBeltSpeed(20);
    p.onBeltSpeed(40);                           // agrees: run resets
    p.onBeltSpeed(20);
    auto d = p.onTarget(400, 10000);
    TEST_ASSERT_EQUAL(policy::Action::None, d.action);
}

void test_clamp_happens_before_deadband(void) {
    policy::SpeedPolicy p;
    auto a = p.onTarget(900, 0);
    TEST_ASSERT_EQUAL(policy::Action::SetSpeed, a.action);
    TEST_ASSERT_EQUAL_UINT8(60, a.tenthKmh);
    TEST_ASSERT_EQUAL(policy::Action::None, p.onTarget(700, 2000).action);
}

void test_reset_restores_initial_state(void) {
    policy::SpeedPolicy p;
    p.onTarget(100, 0);
    p.reset();
    TEST_ASSERT_EQUAL(policy::Action::Stop, p.onTarget(100, 0).action);
    p.reset();
    TEST_ASSERT_EQUAL(policy::Action::SetSpeed, p.onTarget(400, 0).action);
    p.reset();
    TEST_ASSERT_EQUAL(policy::Action::SetSpeed, p.onTarget(400, 0).action);
}

void test_reset_clears_reference_and_divergent_run(void) {
    policy::SpeedPolicy p;
    p.onTarget(400, 0);                          // reference set
    p.onBeltSpeed(20);
    p.onBeltSpeed(20);                           // run in progress
    p.reset();
    TEST_ASSERT_EQUAL(policy::Action::SetSpeed, p.onTarget(400, 0).action);  // no reference
    p.reset();
    p.onTarget(400, 0);
    p.onBeltSpeed(20);                           // only one divergent sample since reset
    TEST_ASSERT_EQUAL(policy::Action::None, p.onTarget(400, 10000).action);
}

void test_deceleration_ramp_after_stop_is_not_adopted(void) {
    policy::SpeedPolicy p;
    p.onTarget(400, 0);
    p.onTarget(100, 2000);                       // app stop
    const uint8_t ramp[] = {5, 4, 3, 2, 1, 0};
    for (uint8_t v : ramp) p.onBeltSpeed(v);
    TEST_ASSERT_EQUAL(policy::Action::None, p.onTarget(100, 30000).action);  // no second Stop
    // the trailing 0 cleared the previous sample: a fresh rising run adopts
    p.onBeltSpeed(10);
    p.onBeltSpeed(20);
    p.onBeltSpeed(30);
    TEST_ASSERT_EQUAL(policy::Action::Stop, p.onTarget(100, 40000).action);
}

void test_target_below_minimum_is_a_stop(void) {
    policy::SpeedPolicy p;
    TEST_ASSERT_EQUAL(policy::Action::Stop, p.onTarget(0, 0).action);
    policy::SpeedPolicy q;
    TEST_ASSERT_EQUAL(policy::Action::Stop, q.onTarget(49, 0).action);
    TEST_ASSERT_EQUAL(policy::Action::None, q.onTarget(100, 5000).action);  // no second Stop
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
    RUN_TEST(test_stop_works_after_remote_start_following_a_stop);
    RUN_TEST(test_remote_start_after_stop_adopts_reference);
    RUN_TEST(test_resync_threshold_matches_send_threshold);
    RUN_TEST(test_difference_of_exactly_deadband_is_sent);
    RUN_TEST(test_exactly_min_interval_is_allowed);
    RUN_TEST(test_agreeing_sample_resets_divergent_run);
    RUN_TEST(test_clamp_happens_before_deadband);
    RUN_TEST(test_reset_restores_initial_state);
    RUN_TEST(test_reset_clears_reference_and_divergent_run);
    RUN_TEST(test_deceleration_ramp_after_stop_is_not_adopted);
    RUN_TEST(test_target_below_minimum_is_a_stop);
    RUN_TEST(test_disarmed_target_emits_nothing);
    RUN_TEST(test_arming_alone_does_not_start_the_belt);
    RUN_TEST(test_armed_target_on_stopped_belt_runs_start_sequence_then_notifies);
    RUN_TEST(test_armed_target_on_running_belt_is_a_plain_set_speed);
    RUN_TEST(test_phone_link_loss_stops_the_belt_but_stays_armed);
    RUN_TEST(test_reconnect_does_not_restart_the_belt_by_itself);
    RUN_TEST(test_ui_stop_works_while_disarmed);
    RUN_TEST(test_belt_link_loss_stops_notifications);
    return UNITY_END();
}
