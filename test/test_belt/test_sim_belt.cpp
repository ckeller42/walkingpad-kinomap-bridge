#include <unity.h>
#include "sim_belt.h"
#include "test_belt_gate.h"

void setUp(void) {}
void tearDown(void) {}

// Ticks 1 s apart from t=0 until the belt leaves the countdown; returns the
// time reached. Speed commands are only accepted from here on.
static uint32_t runPastCountdown(belt::SimBelt& b) {
    uint32_t t = 0;
    b.tick(t);
    wilink::Status s{};
    do { t += 1000; b.tick(t); b.poll(s); } while (s.beltState == 7);
    return t;
}

void test_sim_belt_reports_commanded_speed_after_ramp(void) {
    belt::SimBelt b;
    // Deviation from the brief: setSpeed is sent after the countdown, because
    // the real belt discards a speed command sent during state 7.
    b.setMode(wilink::Mode::Manual); b.start();
    uint32_t t = runPastCountdown(b);
    b.setSpeed(40);
    for (; t <= 10000; t += 1000) b.tick(t);
    wilink::Status s{};
    TEST_ASSERT_TRUE(b.poll(s));
    TEST_ASSERT_EQUAL_UINT8(40, s.tenthKmh);
}

void test_sim_belt_accumulates_distance_while_running(void) {
    belt::SimBelt b;
    b.setMode(wilink::Mode::Manual); b.start();
    uint32_t t = runPastCountdown(b);
    b.setSpeed(36);   // 3.6 km/h = 1 m/s
    for (; t <= 60000; t += 1000) b.tick(t);
    wilink::Status s{};
    b.poll(s);
    TEST_ASSERT_GREATER_THAN_UINT32(0, s.distance10m);
}

void test_sim_belt_stop_zeroes_speed(void) {
    belt::SimBelt b;
    b.setMode(wilink::Mode::Manual); b.start();
    uint32_t t = runPastCountdown(b);
    b.setSpeed(40);
    for (; t <= 10000; t += 1000) b.tick(t);
    b.stop();
    for (t = 10000; t <= 20000; t += 1000) b.tick(t);
    wilink::Status s{};
    b.poll(s);
    TEST_ASSERT_EQUAL_UINT8(0, s.tenthKmh);
    TEST_ASSERT_EQUAL_UINT8(4, s.beltState);
    TEST_ASSERT_EQUAL_UINT8(2, s.mode);
}

void test_sim_belt_counts_commands(void) {
    belt::SimBelt b;
    b.start();
    runPastCountdown(b);
    b.setSpeed(40);
    b.setSpeed(45);
    // start + 2 speed commands (a speed command on an idle belt would be dropped)
    TEST_ASSERT_EQUAL_UINT32(3, b.commandCount());
    TEST_ASSERT_EQUAL_UINT32(2, b.setSpeedCount());
}

void test_sim_belt_counts_all_accepted_command_types(void) {
    belt::SimBelt b;
    b.setMode(wilink::Mode::Manual);   // idle: accepted
    b.start();                         // accepted
    runPastCountdown(b);
    b.setSpeed(28);                    // accepted
    b.stop();                          // accepted
    TEST_ASSERT_EQUAL_UINT32(1, b.modeCount());
    TEST_ASSERT_EQUAL_UINT32(1, b.startCount());
    TEST_ASSERT_EQUAL_UINT32(1, b.setSpeedCount());
    TEST_ASSERT_EQUAL_UINT32(1, b.stopCount());
    TEST_ASSERT_EQUAL_UINT32(4, b.commandCount());
}

void test_sim_belt_rejected_commands_are_not_counted(void) {
    belt::SimBelt b;
    b.setSpeed(40);                    // idle: dropped
    b.start();
    b.tick(0);
    b.setSpeed(40);                    // countdown: dropped
    b.start();                         // not idle: rejected
    b.setMode(wilink::Mode::Auto);     // not idle: rejected
    TEST_ASSERT_EQUAL_UINT32(1, b.commandCount());   // only the first start
    b.stop();                          // accepted (aborts countdown)
    b.stop();                          // already stopped: rejected
    b.start();                         // standby: rejected
    TEST_ASSERT_EQUAL_UINT32(2, b.commandCount());
}

// --- real-belt behaviour ---

void test_sim_belt_start_enters_countdown(void) {
    belt::SimBelt b;
    b.start(); b.tick(0); b.tick(1000);
    wilink::Status s{};
    b.poll(s);
    TEST_ASSERT_EQUAL_UINT8(7, s.beltState);
    TEST_ASSERT_EQUAL_UINT8(0, s.tenthKmh);
}

void test_sim_belt_speed_during_countdown_not_applied_until_running(void) {
    belt::SimBelt b;
    b.start(); b.tick(0);
    b.setSpeed(40);   // sent during countdown: must be dropped
    wilink::Status s{};
    for (uint32_t t = 1000; t <= 15000; t += 1000) {
        b.tick(t);
        b.poll(s);
        TEST_ASSERT_LESS_THAN_UINT8(40, s.tenthKmh);
    }
    TEST_ASSERT_EQUAL_UINT8(0, s.tenthKmh);
    TEST_ASSERT_EQUAL_UINT32(0, b.setSpeedCount());
}

void test_sim_belt_restart_does_not_resurrect_previous_speed(void) {
    belt::SimBelt b;
    b.start();
    uint32_t t = runPastCountdown(b);
    b.setSpeed(28);
    for (; t <= 10000; t += 500) b.tick(t);
    b.stop();
    for (; t <= 20000; t += 500) b.tick(t);
    b.setMode(wilink::Mode::Manual);
    b.start();
    wilink::Status s{};
    for (; t <= 40000; t += 500) {
        b.tick(t);
        b.poll(s);
        TEST_ASSERT_EQUAL_UINT8(0, s.tenthKmh);
    }
}

void test_sim_belt_upramp_sampled_at_1hz_is_non_decreasing(void) {
    belt::SimBelt b;
    b.start();
    uint32_t t = runPastCountdown(b);
    b.setSpeed(40);
    wilink::Status s{};
    b.poll(s);
    uint8_t prev = s.tenthKmh;
    for (int i = 0; i < 12; ++i) {
        t += 1000;
        b.tick(t);
        b.poll(s);
        TEST_ASSERT_GREATER_OR_EQUAL_UINT8(prev, s.tenthKmh);
        prev = s.tenthKmh;
    }
    TEST_ASSERT_EQUAL_UINT8(40, s.tenthKmh);
}

void test_sim_belt_stop_ends_in_standby_and_stays(void) {
    belt::SimBelt b;
    b.start();
    uint32_t t = runPastCountdown(b);
    b.setSpeed(28);
    for (; t <= 10000; t += 500) b.tick(t);
    b.stop();
    for (t = 10000; t <= 30000; t += 500) b.tick(t);
    wilink::Status s{};
    b.poll(s);
    TEST_ASSERT_EQUAL_UINT8(4, s.beltState);
    TEST_ASSERT_EQUAL_UINT8(2, s.mode);
    TEST_ASSERT_EQUAL_UINT8(0, s.tenthKmh);
}

void test_sim_belt_start_refused_from_standby_until_manual(void) {
    belt::SimBelt b;
    b.start();
    uint32_t t = runPastCountdown(b);
    b.setSpeed(28);
    for (; t <= 10000; t += 500) b.tick(t);
    b.stop();
    for (t = 10000; t <= 20000; t += 500) b.tick(t);
    b.start();
    b.tick(20500);
    wilink::Status s{};
    b.poll(s);
    TEST_ASSERT_EQUAL_UINT8(4, s.beltState);

    b.setMode(wilink::Mode::Manual);
    b.poll(s);
    TEST_ASSERT_EQUAL_UINT8(0, s.beltState);
    TEST_ASSERT_EQUAL_UINT8(1, s.mode);

    b.start();
    b.poll(s);
    TEST_ASSERT_EQUAL_UINT8(7, s.beltState);
}

void test_sim_belt_deceleration_strictly_decreasing(void) {
    belt::SimBelt b;
    b.start();
    uint32_t t = runPastCountdown(b);
    b.setSpeed(28);
    for (; t <= 10000; t += 100) b.tick(t);
    wilink::Status s{};
    b.poll(s);
    TEST_ASSERT_EQUAL_UINT8(28, s.tenthKmh);
    b.stop();
    uint8_t prev = s.tenthKmh;
    for (t = 10500; t <= 14000; t += 500) {
        b.tick(t);
        b.poll(s);
        if (prev > 0) TEST_ASSERT_LESS_THAN_UINT8(prev, s.tenthKmh);
        prev = s.tenthKmh;
    }
    TEST_ASSERT_EQUAL_UINT8(0, s.tenthKmh);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_sim_belt_reports_commanded_speed_after_ramp);
    RUN_TEST(test_sim_belt_accumulates_distance_while_running);
    RUN_TEST(test_sim_belt_stop_zeroes_speed);
    RUN_TEST(test_sim_belt_counts_commands);
    RUN_TEST(test_sim_belt_counts_all_accepted_command_types);
    RUN_TEST(test_sim_belt_rejected_commands_are_not_counted);
    RUN_TEST(test_sim_belt_restart_does_not_resurrect_previous_speed);
    RUN_TEST(test_sim_belt_upramp_sampled_at_1hz_is_non_decreasing);
    RUN_TEST(test_sim_belt_start_enters_countdown);
    RUN_TEST(test_sim_belt_speed_during_countdown_not_applied_until_running);
    RUN_TEST(test_sim_belt_stop_ends_in_standby_and_stays);
    RUN_TEST(test_sim_belt_start_refused_from_standby_until_manual);
    RUN_TEST(test_sim_belt_deceleration_strictly_decreasing);
    RUN_TEST(test_gate_mode_then_start_are_at_least_a_gap_apart);
    RUN_TEST(test_gate_stop_after_start_drops_queued_start_and_goes_on_next_clear_tick);
    RUN_TEST(test_gate_ignored_stop_is_resent_until_poll_shows_stopped);
    RUN_TEST(test_gate_state_zero_also_confirms_stop);
    RUN_TEST(test_gate_two_speeds_in_one_gap_only_newest_written);
    RUN_TEST(test_gate_stop_while_start_pending_clears_the_start);
    RUN_TEST(test_gate_spacing_survives_millis_wraparound);
    RUN_TEST(test_gate_stop_always_writes_even_when_last_poll_showed_idle);
    RUN_TEST(test_gate_forwards_tick_even_inside_the_gap);
    RUN_TEST(test_gate_status_request_shares_the_gap_with_commands);
    RUN_TEST(test_gate_queued_command_outranks_status_request);
    RUN_TEST(test_gate_steady_state_polling_does_not_starve_commands);
    RUN_TEST(test_gate_constant_commands_cannot_starve_status_forever);
    RUN_TEST(test_gate_stop_outranks_status_request);
    return UNITY_END();
}
