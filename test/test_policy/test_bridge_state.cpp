#include <unity.h>
#include "bridge_state.h"

void test_disarmed_target_emits_nothing(void) {
    policy::BridgeState s;
    s.onBeltLink(true); s.onPhoneLink(true);
    auto o = s.apply({policy::Action::SetSpeed, 40});
    TEST_ASSERT_EQUAL_size_t(0, o.count);
    TEST_ASSERT_FALSE(s.beltRunning());
}

void test_arming_alone_does_not_start_the_belt(void) {
    policy::BridgeState s;
    s.onBeltLink(true); s.onPhoneLink(true);
    s.setArmed(true);
    TEST_ASSERT_FALSE(s.beltRunning());
}

void test_armed_target_on_stopped_belt_runs_start_sequence_then_notifies(void) {
    policy::BridgeState s;
    s.onBeltLink(true); s.onPhoneLink(true); s.setArmed(true);
    auto o = s.apply({policy::Action::SetSpeed, 40});
    TEST_ASSERT_EQUAL_size_t(2, o.count);
    TEST_ASSERT_EQUAL(policy::Emit::StartSequence, o.emits[0]);
    TEST_ASSERT_EQUAL(policy::Emit::NotifyStarted, o.emits[1]);
    TEST_ASSERT_EQUAL_UINT8(40, o.tenthKmh);
    TEST_ASSERT_TRUE(s.beltRunning());
}

void test_armed_target_on_running_belt_is_a_plain_set_speed(void) {
    policy::BridgeState s;
    s.onBeltLink(true); s.onPhoneLink(true); s.setArmed(true);
    s.apply({policy::Action::SetSpeed, 40});
    auto o = s.apply({policy::Action::SetSpeed, 50});
    TEST_ASSERT_EQUAL_size_t(1, o.count);
    TEST_ASSERT_EQUAL(policy::Emit::SetSpeed, o.emits[0]);
}

void test_phone_link_loss_stops_the_belt_but_stays_armed(void) {
    policy::BridgeState s;
    s.onBeltLink(true); s.onPhoneLink(true); s.setArmed(true);
    s.apply({policy::Action::SetSpeed, 40});
    s.onPhoneLink(false);
    TEST_ASSERT_FALSE(s.beltRunning());
    TEST_ASSERT_TRUE(s.armed());
}

void test_reconnect_does_not_restart_the_belt_by_itself(void) {
    policy::BridgeState s;
    s.onBeltLink(true); s.onPhoneLink(true); s.setArmed(true);
    s.apply({policy::Action::SetSpeed, 40});
    s.onPhoneLink(false);
    s.onPhoneLink(true);
    TEST_ASSERT_FALSE(s.beltRunning());
}

void test_ui_stop_works_while_disarmed(void) {
    policy::BridgeState s;
    s.onBeltLink(true); s.onPhoneLink(true);
    auto o = s.stopFromUi();
    TEST_ASSERT_EQUAL(policy::Emit::Stop, o.emits[0]);
}

void test_belt_link_loss_stops_notifications(void) {
    policy::BridgeState s;
    s.onBeltLink(true); s.onPhoneLink(true);
    TEST_ASSERT_TRUE(s.notifying());
    s.onBeltLink(false);
    TEST_ASSERT_FALSE(s.notifying());
}
