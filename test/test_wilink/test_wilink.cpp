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


// ---------------------------------------------------------------------------
// StatusReassembler. The belt can split a status frame across two FE01
// notifications; parsing each alone drops both halves silently.
// ---------------------------------------------------------------------------

// The exact split captured on hardware: 3 bytes, then a 17-byte remainder.
static const uint8_t kHeadFragment[] = {0xf8, 0xa2, 0x00};
static const uint8_t kTailFragment[] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
                                        0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
                                        0x00, 0xa4, 0xfd};

void test_reassembler_passes_a_whole_frame_straight_through(void) {
    wilink::StatusReassembler r;
    wilink::Status st{};
    uint8_t whole[20] = {0xf8, 0xa2, 0x01, 0x19, 0x01};
    TEST_ASSERT_EQUAL(wilink::StatusReassembler::Result::Parsed,
                      r.feed(whole, sizeof whole, st));
    TEST_ASSERT_EQUAL(1, st.beltState);
    TEST_ASSERT_EQUAL(25, st.tenthKmh);
    // A frame that never needed joining is not counted as a reassembly.
    TEST_ASSERT_EQUAL(0, r.reassembled());
}

void test_reassembler_joins_the_split_seen_on_hardware(void) {
    wilink::StatusReassembler r;
    wilink::Status st{};
    TEST_ASSERT_EQUAL(wilink::StatusReassembler::Result::Buffered,
                      r.feed(kHeadFragment, sizeof kHeadFragment, st));
    TEST_ASSERT_EQUAL(wilink::StatusReassembler::Result::Parsed,
                      r.feed(kTailFragment, sizeof kTailFragment, st));
    TEST_ASSERT_EQUAL(0, st.beltState);
    TEST_ASSERT_EQUAL(0, st.tenthKmh);
    TEST_ASSERT_EQUAL(1, st.mode);
    TEST_ASSERT_EQUAL(1, r.reassembled());
    TEST_ASSERT_EQUAL(0, r.discarded());
}

// A fragment whose tail never arrives must not wedge the decoder shut: the
// next whole frame has to come through, and the loss be counted.
void test_reassembler_recovers_when_a_tail_never_arrives(void) {
    wilink::StatusReassembler r;
    wilink::Status st{};
    r.feed(kHeadFragment, sizeof kHeadFragment, st);
    uint8_t whole[20] = {0xf8, 0xa2, 0x04, 0x00, 0x02};
    TEST_ASSERT_EQUAL(wilink::StatusReassembler::Result::Parsed,
                      r.feed(whole, sizeof whole, st));
    TEST_ASSERT_EQUAL(4, st.beltState);
    TEST_ASSERT_EQUAL(1, r.discarded());
}

// A continuation arriving with nothing pending is stray data, not the tail of
// a frame, and must not be joined to the next header.
void test_reassembler_ignores_a_stray_continuation(void) {
    wilink::StatusReassembler r;
    wilink::Status st{};
    TEST_ASSERT_EQUAL(wilink::StatusReassembler::Result::Ignored,
                      r.feed(kTailFragment, sizeof kTailFragment, st));
    TEST_ASSERT_EQUAL(0, r.reassembled());
}

void test_reassembler_handles_three_way_splits(void) {
    wilink::StatusReassembler r;
    wilink::Status st{};
    const uint8_t a[] = {0xf8, 0xa2};
    const uint8_t b[] = {0x01, 0x14, 0x01, 0x00};
    const uint8_t c[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL(wilink::StatusReassembler::Result::Buffered, r.feed(a, sizeof a, st));
    TEST_ASSERT_EQUAL(wilink::StatusReassembler::Result::Buffered, r.feed(b, sizeof b, st));
    TEST_ASSERT_EQUAL(wilink::StatusReassembler::Result::Parsed, r.feed(c, sizeof c, st));
    TEST_ASSERT_EQUAL(1, st.beltState);
    TEST_ASSERT_EQUAL(20, st.tenthKmh);
}

// Runaway continuations must be abandoned rather than overflowing the buffer.
void test_reassembler_abandons_an_oversized_run(void) {
    wilink::StatusReassembler r;
    wilink::Status st{};
    r.feed(kHeadFragment, sizeof kHeadFragment, st);
    uint8_t junk[40] = {0x11};
    TEST_ASSERT_EQUAL(wilink::StatusReassembler::Result::Ignored,
                      r.feed(junk, sizeof junk, st));
    TEST_ASSERT_EQUAL(1, r.discarded());
    // and it is usable again afterwards
    uint8_t whole[20] = {0xf8, 0xa2, 0x07, 0x00, 0x01};
    TEST_ASSERT_EQUAL(wilink::StatusReassembler::Result::Parsed,
                      r.feed(whole, sizeof whole, st));
    TEST_ASSERT_EQUAL(7, st.beltState);
}

void test_reassembler_tolerates_empty_and_null(void) {
    wilink::StatusReassembler r;
    wilink::Status st{};
    TEST_ASSERT_EQUAL(wilink::StatusReassembler::Result::Ignored, r.feed(nullptr, 0, st));
    const uint8_t none[1] = {0};
    TEST_ASSERT_EQUAL(wilink::StatusReassembler::Result::Ignored, r.feed(none, 0, st));
}

void test_reassembler_reset_forgets_a_pending_fragment(void) {
    wilink::StatusReassembler r;
    wilink::Status st{};
    r.feed(kHeadFragment, sizeof kHeadFragment, st);
    r.reset();
    TEST_ASSERT_EQUAL(wilink::StatusReassembler::Result::Ignored,
                      r.feed(kTailFragment, sizeof kTailFragment, st));
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
    RUN_TEST(test_reassembler_passes_a_whole_frame_straight_through);
    RUN_TEST(test_reassembler_joins_the_split_seen_on_hardware);
    RUN_TEST(test_reassembler_recovers_when_a_tail_never_arrives);
    RUN_TEST(test_reassembler_ignores_a_stray_continuation);
    RUN_TEST(test_reassembler_handles_three_way_splits);
    RUN_TEST(test_reassembler_abandons_an_oversized_run);
    RUN_TEST(test_reassembler_tolerates_empty_and_null);
    RUN_TEST(test_reassembler_reset_forgets_a_pending_fragment);
    return UNITY_END();
}
