#include <unity.h>
#include "sim_belt.h"

void setUp(void) {}
void tearDown(void) {}

void test_sim_belt_reports_commanded_speed_after_ramp(void) {
    belt::SimBelt b;
    b.setMode(wilink::Mode::Manual); b.start(); b.setSpeed(40);
    for (uint32_t t = 0; t <= 10000; t += 1000) b.tick(t);
    wilink::Status s{};
    TEST_ASSERT_TRUE(b.poll(s));
    TEST_ASSERT_EQUAL_UINT8(40, s.tenthKmh);
}

void test_sim_belt_accumulates_distance_while_running(void) {
    belt::SimBelt b;
    b.setMode(wilink::Mode::Manual); b.start(); b.setSpeed(36);   // 3.6 km/h = 1 m/s
    for (uint32_t t = 0; t <= 60000; t += 1000) b.tick(t);
    wilink::Status s{};
    b.poll(s);
    TEST_ASSERT_GREATER_THAN_UINT32(0, s.distance10m);
}

void test_sim_belt_stop_zeroes_speed(void) {
    belt::SimBelt b;
    b.setMode(wilink::Mode::Manual); b.start(); b.setSpeed(40);
    for (uint32_t t = 0; t <= 10000; t += 1000) b.tick(t);
    b.stop();
    for (uint32_t t = 10000; t <= 20000; t += 1000) b.tick(t);
    wilink::Status s{};
    b.poll(s);
    TEST_ASSERT_EQUAL_UINT8(0, s.tenthKmh);
}

void test_sim_belt_counts_commands(void) {
    belt::SimBelt b;
    b.setSpeed(40);
    b.setSpeed(45);
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
    b.setSpeed(40);
    b.tick(2000);
    wilink::Status s{};
    b.poll(s);
    TEST_ASSERT_EQUAL_UINT8(7, s.beltState);
    TEST_ASSERT_EQUAL_UINT8(0, s.tenthKmh);
}

void test_sim_belt_stop_ends_in_standby_and_stays(void) {
    belt::SimBelt b;
    b.start(); b.setSpeed(28);
    for (uint32_t t = 0; t <= 10000; t += 500) b.tick(t);
    b.stop();
    for (uint32_t t = 10000; t <= 30000; t += 500) b.tick(t);
    wilink::Status s{};
    b.poll(s);
    TEST_ASSERT_EQUAL_UINT8(4, s.beltState);
    TEST_ASSERT_EQUAL_UINT8(2, s.mode);
    TEST_ASSERT_EQUAL_UINT8(0, s.tenthKmh);
}

void test_sim_belt_start_refused_from_standby_until_manual(void) {
    belt::SimBelt b;
    b.start(); b.setSpeed(28);
    for (uint32_t t = 0; t <= 10000; t += 500) b.tick(t);
    b.stop();
    for (uint32_t t = 10000; t <= 20000; t += 500) b.tick(t);
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
    b.start(); b.setSpeed(28);
    for (uint32_t t = 0; t <= 10000; t += 100) b.tick(t);
    wilink::Status s{};
    b.poll(s);
    TEST_ASSERT_EQUAL_UINT8(28, s.tenthKmh);
    b.stop();
    uint8_t prev = s.tenthKmh;
    for (uint32_t t = 10500; t <= 14000; t += 500) {
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
    RUN_TEST(test_sim_belt_start_enters_countdown);
    RUN_TEST(test_sim_belt_speed_during_countdown_not_applied_until_running);
    RUN_TEST(test_sim_belt_stop_ends_in_standby_and_stays);
    RUN_TEST(test_sim_belt_start_refused_from_standby_until_manual);
    RUN_TEST(test_sim_belt_deceleration_strictly_decreasing);
    return UNITY_END();
}
