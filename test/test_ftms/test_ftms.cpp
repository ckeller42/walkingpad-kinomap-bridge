#include <unity.h>
#include "ftms.h"

void setUp(void) {}
void tearDown(void) {}

void test_feature_is_8_bytes_with_speed_target_bit(void) {
    auto b = ftms::featureValue();
    TEST_ASSERT_EQUAL_size_t(8, b.len);
    uint32_t targets = (uint32_t)b.bytes[4] | ((uint32_t)b.bytes[5] << 8)
                     | ((uint32_t)b.bytes[6] << 16) | ((uint32_t)b.bytes[7] << 24);
    TEST_ASSERT_TRUE(targets & 0x1);          // bit 0: Speed Target Setting Supported
    TEST_ASSERT_FALSE(targets & 0x2);         // bit 1: Inclination Target Setting NOT supported
    TEST_ASSERT_FALSE(targets & 0x4);         // bit 2: Resistance Target Setting NOT supported
}

void test_supported_speed_range_is_half_to_six_step_tenth(void) {
    auto b = ftms::supportedSpeedRange();
    const uint8_t want[] = {0x32, 0x00, 0x58, 0x02, 0x0A, 0x00};  // 0.50 / 6.00 / 0.10
    TEST_ASSERT_EQUAL_size_t(6, b.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, b.bytes, 6);
}

void test_treadmill_data_is_flags_zero_plus_speed(void) {
    auto b = ftms::treadmillData(420);        // 4.20 km/h
    const uint8_t want[] = {0x00, 0x00, 0xA4, 0x01};
    TEST_ASSERT_EQUAL_size_t(4, b.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, b.bytes, 4);
}

void test_status_started_is_opcode_four(void) {
    auto b = ftms::statusStarted();
    TEST_ASSERT_EQUAL_size_t(1, b.len);
    TEST_ASSERT_EQUAL_HEX8(0x04, b.bytes[0]);
}

void test_parse_request_control(void) {
    const uint8_t buf[] = {0x00};
    auto r = ftms::parseControlPoint(buf, 1);
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_EQUAL(ftms::CpOp::RequestControl, r.op);
}

void test_parse_set_target_speed_little_endian(void) {
    const uint8_t buf[] = {0x02, 0xA4, 0x01};   // 420 = 4.20 km/h
    auto r = ftms::parseControlPoint(buf, 3);
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_EQUAL(ftms::CpOp::SetTargetSpeed, r.op);
    TEST_ASSERT_EQUAL_UINT16(420, r.centiKmh);
}

void test_parse_truncated_set_target_speed_is_invalid(void) {
    const uint8_t buf[] = {0x02};               // opcode with no payload
    auto r = ftms::parseControlPoint(buf, 1);
    TEST_ASSERT_FALSE(r.valid);
}

void test_parse_empty_buffer_is_invalid(void) {
    auto r = ftms::parseControlPoint(nullptr, 0);
    TEST_ASSERT_FALSE(r.valid);
}

void test_success_response_shape(void) {
    auto b = ftms::controlPointSuccess(ftms::CpOp::SetTargetSpeed);
    const uint8_t want[] = {0x80, 0x02, 0x01};
    TEST_ASSERT_EQUAL_size_t(3, b.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, b.bytes, 3);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_feature_is_8_bytes_with_speed_target_bit);
    RUN_TEST(test_supported_speed_range_is_half_to_six_step_tenth);
    RUN_TEST(test_treadmill_data_is_flags_zero_plus_speed);
    RUN_TEST(test_status_started_is_opcode_four);
    RUN_TEST(test_parse_request_control);
    RUN_TEST(test_parse_set_target_speed_little_endian);
    RUN_TEST(test_parse_truncated_set_target_speed_is_invalid);
    RUN_TEST(test_parse_empty_buffer_is_invalid);
    RUN_TEST(test_success_response_shape);
    return UNITY_END();
}
