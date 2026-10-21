#include <unity.h>
#include "wilink.h"

void setUp(void) {}
void tearDown(void) {}

void test_crc_matches_documented_start_frame(void) {
    // F7 A2 04 01 A7 FD  ->  A2+04+01 = 0xA7
    const uint8_t body[] = {0xA2, 0x04, 0x01};
    TEST_ASSERT_EQUAL_HEX8(0xA7, wilink::crc(body, 3));
}

void test_crc_matches_documented_status_request(void) {
    // F7 A2 00 00 A2 FD  ->  A2+00+00 = 0xA2
    const uint8_t body[] = {0xA2, 0x00, 0x00};
    TEST_ASSERT_EQUAL_HEX8(0xA2, wilink::crc(body, 3));
}

void test_crc_truncates_to_low_byte(void) {
    const uint8_t body[] = {0xFF, 0xFF, 0x03};
    TEST_ASSERT_EQUAL_HEX8(0x01, wilink::crc(body, 3));
}

void test_request_status_matches_documented_frame(void) {
    auto f = wilink::requestStatus();
    const uint8_t want[] = {0xF7, 0xA2, 0x00, 0x00, 0xA2, 0xFD};
    TEST_ASSERT_EQUAL_size_t(6, f.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, f.bytes, 6);
}

void test_start_belt_matches_documented_frame(void) {
    auto f = wilink::startBelt();
    const uint8_t want[] = {0xF7, 0xA2, 0x04, 0x01, 0xA7, 0xFD};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, f.bytes, 6);
}

void test_set_speed_4_2_kmh(void) {
    auto f = wilink::setSpeed(42);           // 4.2 km/h in 0.1 units
    const uint8_t want[] = {0xF7, 0xA2, 0x01, 0x2A, 0xCD, 0xFD};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, f.bytes, 6);
}

void test_set_mode_manual(void) {
    auto f = wilink::setMode(wilink::Mode::Manual);
    const uint8_t want[] = {0xF7, 0xA2, 0x02, 0x01, 0xA5, 0xFD};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, f.bytes, 6);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_crc_matches_documented_start_frame);
    RUN_TEST(test_crc_matches_documented_status_request);
    RUN_TEST(test_crc_truncates_to_low_byte);
    RUN_TEST(test_request_status_matches_documented_frame);
    RUN_TEST(test_start_belt_matches_documented_frame);
    RUN_TEST(test_set_speed_4_2_kmh);
    RUN_TEST(test_set_mode_manual);
    return UNITY_END();
}
