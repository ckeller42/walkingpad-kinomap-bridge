#include <unity.h>
#include "bridge_state.h"
#include "cases.h"

void test_disarmed_target_emits_nothing(void) {
    policy::BridgeState s;
    (void)s.onBeltLink(true); (void)s.onPhoneLink(true);
    auto o = s.apply({policy::Action::SetSpeed, 40});
    TEST_ASSERT_EQUAL_size_t(0, o.count);
    TEST_ASSERT_FALSE(s.beltRunning());
}

void test_arming_alone_does_not_start_the_belt(void) {
    policy::BridgeState s;
    (void)s.onBeltLink(true); (void)s.onPhoneLink(true);
    (void)s.setArmed(true);
    TEST_ASSERT_FALSE(s.beltRunning());
}

void test_armed_target_on_stopped_belt_runs_start_sequence_then_notifies(void) {
    policy::BridgeState s;
    (void)s.onBeltLink(true); (void)s.onPhoneLink(true); (void)s.setArmed(true);
    auto o = s.apply({policy::Action::SetSpeed, 40});
    TEST_ASSERT_EQUAL_size_t(2, o.count);
    TEST_ASSERT_EQUAL(policy::Emit::StartSequence, o.emits[0]);
    TEST_ASSERT_EQUAL(policy::Emit::NotifyStarted, o.emits[1]);
    TEST_ASSERT_EQUAL_UINT8(40, o.tenthKmh);
    TEST_ASSERT_TRUE(s.beltRunning());
}

void test_armed_target_on_running_belt_is_a_plain_set_speed(void) {
    policy::BridgeState s;
    (void)s.onBeltLink(true); (void)s.onPhoneLink(true); (void)s.setArmed(true);
    (void)s.apply({policy::Action::SetSpeed, 40});
    auto o = s.apply({policy::Action::SetSpeed, 50});
    TEST_ASSERT_EQUAL_size_t(1, o.count);
    TEST_ASSERT_EQUAL(policy::Emit::SetSpeed, o.emits[0]);
}

void test_phone_link_loss_stops_the_belt_but_stays_armed(void) {
    policy::BridgeState s;
    (void)s.onBeltLink(true); (void)s.onPhoneLink(true); (void)s.setArmed(true);
    (void)s.apply({policy::Action::SetSpeed, 40});
    (void)s.onPhoneLink(false);
    TEST_ASSERT_FALSE(s.beltRunning());
    TEST_ASSERT_TRUE(s.armed());
}

void test_reconnect_does_not_restart_the_belt_by_itself(void) {
    policy::BridgeState s;
    (void)s.onBeltLink(true); (void)s.onPhoneLink(true); (void)s.setArmed(true);
    (void)s.apply({policy::Action::SetSpeed, 40});
    (void)s.onPhoneLink(false);
    (void)s.onPhoneLink(true);
    TEST_ASSERT_FALSE(s.beltRunning());
}

void test_ui_stop_works_while_disarmed(void) {
    policy::BridgeState s;
    (void)s.onBeltLink(true); (void)s.onPhoneLink(true);
    auto o = s.stopFromUi();
    TEST_ASSERT_EQUAL_size_t(2, o.count);
    TEST_ASSERT_EQUAL(policy::Emit::Stop, o.emits[0]);
    TEST_ASSERT_EQUAL(policy::Emit::ResetPolicy, o.emits[1]);
}

void test_belt_link_loss_stops_notifications(void) {
    policy::BridgeState s;
    (void)s.onBeltLink(true); (void)s.onPhoneLink(true);
    TEST_ASSERT_TRUE(s.notifying());
    (void)s.onBeltLink(false);
    TEST_ASSERT_FALSE(s.notifying());
}

static policy::BridgeState runningBelt() {
    policy::BridgeState s;
    (void)s.onBeltLink(true); (void)s.onPhoneLink(true); (void)s.setArmed(true);
    (void)s.apply({policy::Action::SetSpeed, 40});
    return s;
}

void test_phone_link_loss_while_running_emits_stop(void) {
    auto s = runningBelt();
    auto o = s.onPhoneLink(false);
    TEST_ASSERT_EQUAL_size_t(2, o.count);
    TEST_ASSERT_EQUAL(policy::Emit::Stop, o.emits[0]);
    TEST_ASSERT_EQUAL(policy::Emit::ResetPolicy, o.emits[1]);
    TEST_ASSERT_FALSE(s.beltRunning());
}

void test_phone_link_loss_while_stopped_emits_nothing(void) {
    policy::BridgeState s;
    (void)s.onBeltLink(true); (void)s.onPhoneLink(true);
    TEST_ASSERT_EQUAL_size_t(0, s.onPhoneLink(false).count);
}

void test_repeated_phone_link_loss_is_idempotent(void) {
    auto s = runningBelt();
    (void)s.onPhoneLink(false);
    TEST_ASSERT_EQUAL_size_t(0, s.onPhoneLink(false).count);
    TEST_ASSERT_TRUE(s.armed());
}

void test_disarmed_stop_decision_still_emits_stop(void) {
    policy::BridgeState s;
    (void)s.onBeltLink(true); (void)s.onPhoneLink(true);
    auto o = s.apply({policy::Action::Stop, 0});
    TEST_ASSERT_EQUAL_size_t(1, o.count);
    TEST_ASSERT_EQUAL(policy::Emit::Stop, o.emits[0]);
    TEST_ASSERT_FALSE(s.beltRunning());
}

void test_armed_setspeed_with_belt_link_down_emits_nothing(void) {
    policy::BridgeState s;
    (void)s.onPhoneLink(true); (void)s.setArmed(true);
    TEST_ASSERT_EQUAL_size_t(0, s.apply({policy::Action::SetSpeed, 40}).count);
    TEST_ASSERT_FALSE(s.beltRunning());
}

void test_armed_setspeed_with_phone_link_down_emits_nothing(void) {
    policy::BridgeState s;
    (void)s.onBeltLink(true); (void)s.setArmed(true);
    TEST_ASSERT_EQUAL_size_t(0, s.apply({policy::Action::SetSpeed, 40}).count);
    TEST_ASSERT_FALSE(s.beltRunning());
}

void test_notifying_false_when_phone_link_down(void) {
    policy::BridgeState s;
    (void)s.onBeltLink(true); (void)s.onPhoneLink(true);
    (void)s.onPhoneLink(false);
    TEST_ASSERT_FALSE(s.notifying());
}

void test_ui_stop_clears_running_and_reports_one_stop(void) {
    auto s = runningBelt();
    auto o = s.stopFromUi();
    TEST_ASSERT_FALSE(s.beltRunning());
    TEST_ASSERT_EQUAL_size_t(2, o.count);
    TEST_ASSERT_EQUAL(policy::Emit::Stop, o.emits[0]);
    TEST_ASSERT_EQUAL(policy::Emit::ResetPolicy, o.emits[1]);
    // already stopped: same shape
    o = s.stopFromUi();
    TEST_ASSERT_EQUAL_size_t(2, o.count);
    TEST_ASSERT_EQUAL(policy::Emit::Stop, o.emits[0]);
    TEST_ASSERT_EQUAL(policy::Emit::ResetPolicy, o.emits[1]);
}

void test_ui_stop_disarms_so_next_target_emits_nothing(void) {
    auto s = runningBelt();
    (void)s.stopFromUi();
    TEST_ASSERT_FALSE(s.armed());
    TEST_ASSERT_EQUAL_size_t(0, s.apply({policy::Action::SetSpeed, 50}).count);
    TEST_ASSERT_FALSE(s.beltRunning());
}

void test_arming_emits_reset_policy_once(void) {
    policy::BridgeState s;
    auto o = s.setArmed(true);
    TEST_ASSERT_EQUAL_size_t(1, o.count);
    TEST_ASSERT_EQUAL(policy::Emit::ResetPolicy, o.emits[0]);
    TEST_ASSERT_EQUAL_size_t(0, s.setArmed(true).count);
    TEST_ASSERT_EQUAL_size_t(0, s.setArmed(false).count);
}

void test_belt_link_loss_while_running_emits_reset_policy(void) {
    auto s = runningBelt();
    auto o = s.onBeltLink(false);
    TEST_ASSERT_EQUAL_size_t(1, o.count);
    TEST_ASSERT_EQUAL(policy::Emit::ResetPolicy, o.emits[0]);
    TEST_ASSERT_FALSE(s.beltRunning());
}

void test_none_decision_emits_nothing(void) {
    auto s = runningBelt();
    TEST_ASSERT_EQUAL_size_t(0, s.apply({policy::Action::None, 0}).count);
    TEST_ASSERT_TRUE(s.beltRunning());
}

void test_link_up_emits_reset_policy_only_on_transition(void) {
    policy::BridgeState s;
    auto o = s.onBeltLink(true);
    TEST_ASSERT_EQUAL_size_t(1, o.count);
    TEST_ASSERT_EQUAL(policy::Emit::ResetPolicy, o.emits[0]);
    TEST_ASSERT_EQUAL_size_t(0, s.onBeltLink(true).count);
    o = s.onPhoneLink(true);
    TEST_ASSERT_EQUAL_size_t(1, o.count);
    TEST_ASSERT_EQUAL(policy::Emit::ResetPolicy, o.emits[0]);
    TEST_ASSERT_EQUAL_size_t(0, s.onPhoneLink(true).count);
}

void test_target_dropped_while_belt_down_starts_after_reconnect(void) {
    policy::SpeedPolicy p;
    policy::BridgeState s;
    (void)s.onBeltLink(true); (void)s.onPhoneLink(true); (void)s.setArmed(true);
    (void)s.onBeltLink(false);
    auto o = s.apply(p.onTarget(400, 0));           // dropped: belt link down
    TEST_ASSERT_EQUAL_size_t(0, o.count);
    o = s.onBeltLink(true);
    TEST_ASSERT_EQUAL_size_t(1, o.count);
    TEST_ASSERT_EQUAL(policy::Emit::ResetPolicy, o.emits[0]);
    p.reset();                                       // caller honours ResetPolicy
    o = s.apply(p.onTarget(400, 5000));
    TEST_ASSERT_EQUAL_size_t(2, o.count);
    TEST_ASSERT_EQUAL(policy::Emit::StartSequence, o.emits[0]);
    TEST_ASSERT_EQUAL(policy::Emit::NotifyStarted, o.emits[1]);
}

void test_observed_stopped_clears_running_and_resets_policy(void) {
    policy::BridgeState s;
    (void)s.onBeltLink(true); (void)s.onPhoneLink(true); (void)s.setArmed(true);
    (void)s.apply({policy::Action::SetSpeed, 40});
    TEST_ASSERT_TRUE(s.beltRunning());
    auto o = s.onBeltObservedStopped();
    TEST_ASSERT_EQUAL_size_t(1, o.count);
    TEST_ASSERT_EQUAL(policy::Emit::ResetPolicy, o.emits[0]);
    TEST_ASSERT_FALSE(s.beltRunning());
}

void test_observed_stopped_when_not_running_emits_nothing(void) {
    policy::BridgeState s;
    (void)s.onBeltLink(true); (void)s.onPhoneLink(true); (void)s.setArmed(true);
    auto o = s.onBeltObservedStopped();
    TEST_ASSERT_EQUAL_size_t(0, o.count);
}

void test_target_after_observed_stop_starts_the_belt_again(void) {
    policy::BridgeState s;
    (void)s.onBeltLink(true); (void)s.onPhoneLink(true); (void)s.setArmed(true);
    (void)s.apply({policy::Action::SetSpeed, 40});
    (void)s.onBeltObservedStopped();
    auto o = s.apply({policy::Action::SetSpeed, 40});
    TEST_ASSERT_EQUAL(policy::Emit::StartSequence, o.emits[0]);
}

void test_ui_start_while_disarmed_emits_nothing(void) {
    policy::BridgeState s;
    (void)s.onBeltLink(true); (void)s.onPhoneLink(true);
    auto o = s.startFromUi(20);
    TEST_ASSERT_EQUAL_size_t(0, o.count);
    TEST_ASSERT_FALSE(s.beltRunning());
}

void test_ui_start_armed_runs_start_sequence_and_notifies(void) {
    policy::BridgeState s;
    (void)s.onBeltLink(true); (void)s.onPhoneLink(true); (void)s.setArmed(true);
    auto o = s.startFromUi(20);
    TEST_ASSERT_EQUAL_size_t(3, o.count);
    TEST_ASSERT_EQUAL(policy::Emit::StartSequence, o.emits[0]);
    TEST_ASSERT_EQUAL(policy::Emit::NotifyStarted, o.emits[1]);
    TEST_ASSERT_EQUAL(policy::Emit::ResetPolicy, o.emits[2]);
    TEST_ASSERT_EQUAL_UINT8(20, o.tenthKmh);
    TEST_ASSERT_TRUE(s.beltRunning());
}

void test_ui_start_with_belt_down_or_already_running_emits_nothing(void) {
    policy::BridgeState s;
    (void)s.onPhoneLink(true); (void)s.setArmed(true);
    TEST_ASSERT_EQUAL_size_t(0, s.startFromUi(20).count);   // belt link down
    (void)s.onBeltLink(true);
    (void)s.startFromUi(20);
    TEST_ASSERT_EQUAL_size_t(0, s.startFromUi(20).count);   // already running
}
