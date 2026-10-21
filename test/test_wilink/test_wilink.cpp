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

void test_parse_status_extracts_speed_and_mode(void) {
    // F8 A2 <state> <speed*10> <mode> <time BE24> <dist BE24> <steps BE24>
    const uint8_t buf[] = {0xF8, 0xA2, 0x01, 0x2A, 0x01,
                           0x00, 0x00, 0x3C,      // 60 s
                           0x00, 0x00, 0x0A,      // 10 * 10 m = 100 m
                           0x00, 0x00, 0x64};     // 100 steps
    wilink::Status s{};
    TEST_ASSERT_TRUE(wilink::parseStatus(buf, sizeof(buf), s));
    TEST_ASSERT_EQUAL_UINT8(42, s.tenthKmh);
    TEST_ASSERT_EQUAL_UINT8(1, s.mode);
    TEST_ASSERT_EQUAL_UINT32(60, s.elapsedSec);
    TEST_ASSERT_EQUAL_UINT32(10, s.distance10m);
    TEST_ASSERT_EQUAL_UINT32(100, s.steps);
}

void test_parse_status_rejects_wrong_header(void) {
    const uint8_t buf[14] = {0xF7, 0xA2};
    wilink::Status s{};
    TEST_ASSERT_FALSE(wilink::parseStatus(buf, sizeof(buf), s));
}

void test_parse_status_rejects_short_buffer(void) {
    const uint8_t buf[] = {0xF8, 0xA2, 0x01, 0x2A};
    wilink::Status s{};
    TEST_ASSERT_FALSE(wilink::parseStatus(buf, sizeof(buf), s));
}

void test_parse_status_leaves_output_untouched_on_reject(void) {
    const uint8_t buf[] = {0xF8, 0xA2};
    wilink::Status s{};
    s.tenthKmh = 99;
    TEST_ASSERT_FALSE(wilink::parseStatus(buf, sizeof(buf), s));
    TEST_ASSERT_EQUAL_UINT8(99, s.tenthKmh);
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
    RUN_TEST(test_parse_status_extracts_speed_and_mode);
    RUN_TEST(test_parse_status_rejects_wrong_header);
    RUN_TEST(test_parse_status_rejects_short_buffer);
    RUN_TEST(test_parse_status_leaves_output_untouched_on_reject);
    return UNITY_END();
}
