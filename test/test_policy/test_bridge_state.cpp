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

// ---- manual speed (UI +/- and console `speed`) ----
static policy::BridgeState armedLinked() {
    policy::BridgeState s;
    (void)s.onBeltLink(true); (void)s.onPhoneLink(true); (void)s.setArmed(true);
    return s;
}

void test_step_tenths_moves_in_half_kmh_steps_and_clamps(void) {
    TEST_ASSERT_EQUAL_UINT8(25, policy::stepTenths(20, +1));
    TEST_ASSERT_EQUAL_UINT8(15, policy::stepTenths(20, -1));
    TEST_ASSERT_EQUAL_UINT8(25, policy::stepTenths(23, +1));   // off-grid snaps to the grid
    TEST_ASSERT_EQUAL_UINT8(20, policy::stepTenths(23, -1));
    TEST_ASSERT_EQUAL_UINT8(5, policy::stepTenths(5, -1));     // clamp at 0.50
    TEST_ASSERT_EQUAL_UINT8(5, policy::stepTenths(0, +1));
    TEST_ASSERT_EQUAL_UINT8(60, policy::stepTenths(60, +1));   // clamp at 6.00
    TEST_ASSERT_EQUAL_UINT8(60, policy::stepTenths(58, +1));
    TEST_ASSERT_EQUAL_UINT8(60, policy::stepTenths(200, -1));  // belt reading above range
}

void test_manual_speed_while_disarmed_emits_nothing(void) {
    policy::BridgeState s;
    (void)s.onBeltLink(true); (void)s.onPhoneLink(true);
    TEST_ASSERT_EQUAL_size_t(0, s.setSpeedFromUi(30).count);
    TEST_ASSERT_EQUAL_size_t(0, s.stepSpeedFromUi(+1, 20).count);
    TEST_ASSERT_FALSE(s.beltRunning());
}

void test_manual_speed_while_running_sets_speed_and_syncs_policy(void) {
    auto s = armedLinked();
    (void)s.startFromUi(20);
    auto o = s.stepSpeedFromUi(+1, 20);
    TEST_ASSERT_EQUAL_size_t(2, o.count);
    TEST_ASSERT_EQUAL(policy::Emit::SetSpeed, o.emits[0]);
    TEST_ASSERT_EQUAL(policy::Emit::SyncPolicy, o.emits[1]);
    TEST_ASSERT_EQUAL_UINT8(25, o.tenthKmh);
    auto d = s.stepSpeedFromUi(-1, 20);
    TEST_ASSERT_EQUAL_UINT8(15, d.tenthKmh);
}

void test_manual_plus_on_stopped_belt_starts_at_minimum(void) {
    auto s = armedLinked();
    auto o = s.stepSpeedFromUi(+1, 0);
    TEST_ASSERT_EQUAL_size_t(3, o.count);
    TEST_ASSERT_EQUAL(policy::Emit::StartSequence, o.emits[0]);
    TEST_ASSERT_EQUAL(policy::Emit::NotifyStarted, o.emits[1]);
    TEST_ASSERT_EQUAL(policy::Emit::SyncPolicy, o.emits[2]);
    TEST_ASSERT_EQUAL_UINT8(5, o.tenthKmh);
    TEST_ASSERT_TRUE(s.beltRunning());
}

void test_manual_minus_on_stopped_belt_does_nothing(void) {
    auto s = armedLinked();
    TEST_ASSERT_EQUAL_size_t(0, s.stepSpeedFromUi(-1, 0).count);
    TEST_ASSERT_FALSE(s.beltRunning());
}

void test_manual_absolute_speed_is_clamped_and_needs_belt_link(void) {
    policy::BridgeState s;
    (void)s.onPhoneLink(true); (void)s.setArmed(true);
    TEST_ASSERT_EQUAL_size_t(0, s.setSpeedFromUi(30).count);   // belt link down
    (void)s.onBeltLink(true);
    TEST_ASSERT_EQUAL_UINT8(60, s.setSpeedFromUi(99).tenthKmh);
    TEST_ASSERT_EQUAL_UINT8(5, s.setSpeedFromUi(1).tenthKmh);
}

void test_manual_step_at_limits_never_stops_the_belt(void) {
    auto s = armedLinked();
    (void)s.startFromUi(5);
    auto o = s.stepSpeedFromUi(-1, 5);
    TEST_ASSERT_EQUAL(policy::Emit::SetSpeed, o.emits[0]);
    TEST_ASSERT_EQUAL_UINT8(5, o.tenthKmh);
    TEST_ASSERT_TRUE(s.beltRunning());
}

// The Outcome carries a fixed-size emit array and one path already fills it
// completely (StartSequence, NotifyStarted, SyncPolicy). Nothing in the type
// stops a fourth being appended, so this pins the bound: if any entry point
// ever produces more, this fails instead of overrunning silently.
void test_no_outcome_exceeds_the_emit_array(void) {
    policy::BridgeState s;
    auto check = [](const policy::Outcome& o) {
        TEST_ASSERT_LESS_OR_EQUAL(policy::Outcome::kMaxEmits, o.count);
    };

    check(s.onBeltLink(true));
    check(s.onPhoneLink(true));
    check(s.setArmed(true));
    // A UI speed on a stopped belt is the longest sequence produced anywhere.
    check(s.setSpeedFromUi(25));
    check(s.onBeltObservedStopped());
    check(s.startFromUi(20));
    check(s.stepSpeedFromUi(1, 20));
    check(s.apply({policy::Action::SetSpeed, 30}));
    check(s.stopFromUi());
    check(s.apply({policy::Action::Stop, 0}));
    check(s.apply({policy::Action::None, 0}));
    check(s.setArmed(false));
    check(s.onPhoneLink(false));
    check(s.onBeltLink(false));
    check(s.requestMode(policy::BeltMode::Auto, false));
    check(s.observeMode(policy::BeltMode::Auto));
    check(s.observeMode(policy::BeltMode::Manual));
}

void test_request_mode_refused_when_disarmed(void) {
    policy::BridgeState s;
    (void)s.onBeltLink(true);
    auto o = s.requestMode(policy::BeltMode::Auto, false);
    TEST_ASSERT_EQUAL_size_t(0, o.count);
    TEST_ASSERT_FALSE(s.autoPending());
}
void test_request_mode_refused_when_belt_down(void) {
    policy::BridgeState s;
    (void)s.setArmed(true);
    auto o = s.requestMode(policy::BeltMode::Auto, false);
    TEST_ASSERT_EQUAL_size_t(0, o.count);
}
void test_request_mode_refused_while_observed_running(void) {
    policy::BridgeState s;
    (void)s.onBeltLink(true); (void)s.setArmed(true);
    auto o = s.requestMode(policy::BeltMode::Auto, true);
    TEST_ASSERT_EQUAL_size_t(0, o.count);
    TEST_ASSERT_FALSE(s.autoPending());
}
void test_request_mode_emits_set_mode_and_is_pending_not_active(void) {
    policy::BridgeState s;
    (void)s.onBeltLink(true); (void)s.setArmed(true);
    auto o = s.requestMode(policy::BeltMode::Auto, false);
    TEST_ASSERT_EQUAL_size_t(1, o.count);
    TEST_ASSERT_EQUAL(policy::Emit::SetMode, o.emits[0]);
    TEST_ASSERT_EQUAL(policy::BeltMode::Auto, o.mode);
    TEST_ASSERT_TRUE(s.autoPending());
    TEST_ASSERT_FALSE(s.autoActive());
}
void test_observe_mode_confirms_pending_request(void) {
    policy::BridgeState s;
    (void)s.onBeltLink(true); (void)s.setArmed(true);
    (void)s.requestMode(policy::BeltMode::Auto, false);
    auto o = s.observeMode(policy::BeltMode::Auto);
    TEST_ASSERT_EQUAL_size_t(0, o.count);
    TEST_ASSERT_TRUE(s.autoActive());
    TEST_ASSERT_FALSE(s.autoPending());
}
void test_observe_mode_third_mode_while_pending_is_a_rejection(void) {
    policy::BridgeState s;
    (void)s.onBeltLink(true); (void)s.setArmed(true);
    (void)s.observeMode(policy::BeltMode::Manual);          // belt is Manual
    (void)s.requestMode(policy::BeltMode::Auto, false);     // ask for Auto
    (void)s.observeMode(policy::BeltMode::Standby);         // belt went elsewhere
    TEST_ASSERT_FALSE(s.autoPending());
    TEST_ASSERT_FALSE(s.autoActive());
    TEST_ASSERT_EQUAL(policy::BeltMode::Standby, s.observedMode());
}
void test_observe_mode_unchanged_frames_time_out_after_confirm_frames(void) {
    policy::BridgeState s;
    (void)s.onBeltLink(true); (void)s.setArmed(true);
    (void)s.observeMode(policy::BeltMode::Manual);          // belt is Manual
    (void)s.requestMode(policy::BeltMode::Auto, false);
    for (uint8_t i = 0; i + 1 < policy::kModeConfirmFrames; ++i) {
        (void)s.observeMode(policy::BeltMode::Manual);      // not applied yet
        TEST_ASSERT_TRUE(s.autoPending());
    }
    (void)s.observeMode(policy::BeltMode::Manual);          // the kModeConfirmFrames-th frame
    TEST_ASSERT_FALSE(s.autoPending());
    TEST_ASSERT_FALSE(s.autoActive());
}
void test_external_auto_while_disarmed_is_observed_hands_off(void) {
    policy::BridgeState s;
    (void)s.onBeltLink(true);  // disarmed
    auto o = s.observeMode(policy::BeltMode::Auto);
    TEST_ASSERT_EQUAL_size_t(0, o.count);
    TEST_ASSERT_TRUE(s.autoActive());
}
void test_leaving_auto_emits_reset_policy(void) {
    policy::BridgeState s;
    (void)s.onBeltLink(true);
    (void)s.observeMode(policy::BeltMode::Auto);
    auto o = s.observeMode(policy::BeltMode::Manual);
    TEST_ASSERT_EQUAL_size_t(1, o.count);
    TEST_ASSERT_EQUAL(policy::Emit::ResetPolicy, o.emits[0]);
    TEST_ASSERT_FALSE(s.autoActive());
}
void test_request_manual_from_auto_at_rest_emits_set_mode_manual(void) {
    policy::BridgeState s;
    (void)s.onBeltLink(true); (void)s.setArmed(true);
    (void)s.observeMode(policy::BeltMode::Auto);
    auto o = s.requestMode(policy::BeltMode::Manual, false);
    TEST_ASSERT_EQUAL_size_t(1, o.count);
    TEST_ASSERT_EQUAL(policy::Emit::SetMode, o.emits[0]);
    TEST_ASSERT_EQUAL(policy::BeltMode::Manual, o.mode);
}
void test_belt_link_loss_clears_pending_and_reconnect_resends_nothing(void) {
    policy::BridgeState s;
    (void)s.onBeltLink(true); (void)s.setArmed(true);
    (void)s.requestMode(policy::BeltMode::Auto, false);
    (void)s.onBeltLink(false);
    TEST_ASSERT_FALSE(s.autoPending());
    TEST_ASSERT_EQUAL(policy::BeltMode::Unknown, s.observedMode());
    auto o = s.onBeltLink(true);
    for (size_t i = 0; i < o.count; ++i) TEST_ASSERT_NOT_EQUAL(policy::Emit::SetMode, o.emits[i]);
}
void test_initial_mode_is_unknown_and_not_auto(void) {
    policy::BridgeState s;
    TEST_ASSERT_EQUAL(policy::BeltMode::Unknown, s.observedMode());
    TEST_ASSERT_FALSE(s.autoActive());
    TEST_ASSERT_FALSE(s.autoPending());
}
