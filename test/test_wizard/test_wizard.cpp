#include <initializer_list>
#include <string.h>
#include <unity.h>

#include "wizard.h"

using namespace wizard;

void setUp(void) {}
void tearDown(void) {}

static Inputs real(bool up = false, ScanPhase sp = ScanPhase::Scanning, uint32_t sec = 0) {
    Inputs i;
    i.beltUp = up;
    i.scan = sp;
    i.scanSec = sec;
    i.foundAddr = "aa:bb:cc:dd:ee:ff";
    return i;
}

void test_real_path_walks_welcome_scan_linked_arm_done_finish(void) {
    Wizard w;
    w.start(false);
    TEST_ASSERT_EQUAL(Step::Welcome, w.step());
    TEST_ASSERT_EQUAL(Result::Continue, w.press(Btn::Right, real()));
    TEST_ASSERT_EQUAL(Step::Scan, w.step());
    TEST_ASSERT_TRUE(w.update(real(true)));
    TEST_ASSERT_EQUAL(Step::Linked, w.step());
    w.press(Btn::Right, real(true));
    TEST_ASSERT_EQUAL(Step::Arm, w.step());
    w.press(Btn::Right, real(true));
    TEST_ASSERT_EQUAL(Step::Done, w.step());
    TEST_ASSERT_EQUAL(Result::Finished, w.press(Btn::Right, real(true)));
}

void test_sim_path_skips_pairing_and_says_so(void) {
    Wizard w;
    w.start(true);
    TEST_ASSERT_EQUAL(Step::SimInfo, w.step());
    Inputs in;
    in.sim = true;
    Screen s = w.screen(in);
    TEST_ASSERT_NOT_NULL(strstr(s.title, "Simulated"));
    TEST_ASSERT_EQUAL_UINT8(3, s.stepTotal);
    w.press(Btn::Right, in);
    TEST_ASSERT_EQUAL(Step::Arm, w.step());
    w.update(in);  // a sim wizard never pretends the belt paired
    TEST_ASSERT_EQUAL(Step::Arm, w.step());
}

void test_lost_link_returns_to_scan(void) {
    Wizard w;
    w.start(false);
    w.press(Btn::Right, real());
    w.update(real(true));
    TEST_ASSERT_TRUE(w.update(real(false)));
    TEST_ASSERT_EQUAL(Step::Scan, w.step());
}

void test_later_leaves_without_finishing(void) {
    Wizard w;
    w.start(false);
    TEST_ASSERT_EQUAL(Result::Later, w.press(Btn::Left, real()));
}

void test_scan_can_always_be_skipped_or_backed_out(void) {
    Wizard w;
    w.start(false);
    w.press(Btn::Right, real());
    w.press(Btn::Right, real());  // SKIP with no belt
    TEST_ASSERT_EQUAL(Step::Arm, w.step());
    w.press(Btn::Left, real());   // back: belt still not there -> scan
    TEST_ASSERT_EQUAL(Step::Scan, w.step());
    w.press(Btn::Left, real());
    TEST_ASSERT_EQUAL(Step::Welcome, w.step());
}

void test_scan_hint_after_timeout_tells_user_what_to_do(void) {
    Wizard w;
    w.start(false);
    w.press(Btn::Right, real());
    Screen early = w.screen(real(false, ScanPhase::Scanning, 5));
    Screen late = w.screen(real(false, ScanPhase::Scanning, kScanHintSec));
    TEST_ASSERT_NOT_EQUAL(early.phase, late.phase);
    TEST_ASSERT_NOT_NULL(strstr(late.action[0], "OFF and ON"));
}

void test_found_screen_shows_address(void) {
    Wizard w;
    w.start(false);
    w.press(Btn::Right, real());
    Screen s = w.screen(real(false, ScanPhase::Found));
    TEST_ASSERT_NOT_NULL(strstr(s.detail[0], "aa:bb:cc:dd:ee:ff"));
}

void test_every_screen_has_an_instruction_and_both_buttons(void) {
    for (int sim = 0; sim < 2; ++sim) {
        for (int st = 0; st < 6; ++st) {
            for (int sp = 0; sp < 4; ++sp) {
                for (uint32_t sec : {0u, 100u}) {
                    Wizard w;
                    w.start(sim);
                    // walk to the wanted step with real presses
                    Inputs in = real(false, (ScanPhase)sp, sec);
                    in.sim = sim;
                    Step want = (Step)st;
                    for (int k = 0; k < 8 && w.step() != want; ++k) w.press(Btn::Right, in);
                    if (w.step() != want) continue;
                    Screen s = w.screen(in);
                    TEST_ASSERT_TRUE(s.title[0]);
                    TEST_ASSERT_TRUE(s.action[0][0]);
                    TEST_ASSERT_TRUE(s.action[1][0]);
                    TEST_ASSERT_TRUE(s.left[0]);
                    TEST_ASSERT_TRUE(s.right[0]);
                }
            }
        }
    }
}

void test_last_step_states_kinomap_waits_for_movement(void) {
    Wizard w;
    w.start(false);
    Inputs in = real(true);
    w.press(Btn::Right, in);
    w.update(in);
    w.press(Btn::Right, in);
    w.press(Btn::Right, in);
    TEST_ASSERT_EQUAL(Step::Done, w.step());
    Screen s = w.screen(in);
    TEST_ASSERT_NOT_NULL(strstr(s.action[1], "START"));
    // Measured: Kinomap never initiates FTMS StartResume, so the last screen
    // must say the user starts the belt, not the app.
    TEST_ASSERT_NOT_NULL(strstr(s.detail[2], "never starts"));
    // Kinomap's pairing check reads live values, so a stopped belt reads the
    // same as no belt. The screen has to say to start it, not just switch it on.
    TEST_ASSERT_NOT_NULL(strstr(s.detail[0], "ARM and START"));
    TEST_ASSERT_NOT_NULL(strstr(s.detail[1], "moving"));
}

// A belt left off makes Kinomap fail to pair with an error that blames the
// bridge, so the last screen has to catch that case before the user goes
// looking at the wrong thing.
void test_last_step_warns_when_the_belt_is_off(void) {
    Wizard w;
    w.start(false);
    Inputs on = real(true);
    w.press(Btn::Right, on);
    w.update(on);
    w.press(Btn::Right, on);
    w.press(Btn::Right, on);
    TEST_ASSERT_EQUAL(Step::Done, w.step());

    Inputs off = real(false);
    Screen s = w.screen(off);
    TEST_ASSERT_NOT_NULL(strstr(s.title, "OFF"));
    TEST_ASSERT_NOT_NULL(strstr(s.action[0], "Switch the belt ON"));
    // Distinct phase, so the screenshot tooling captures it as its own screen.
    TEST_ASSERT_NOT_EQUAL(w.screen(on).phase, s.phase);
    // Still finishable: the warning informs, it does not trap the user.
    TEST_ASSERT_EQUAL_STRING("FINISH", s.right);
}

void test_arm_screen_explains_arm_and_stop(void) {
    Wizard w;
    w.start(true);
    Inputs in;
    in.sim = true;
    w.press(Btn::Right, in);
    Screen s = w.screen(in);
    TEST_ASSERT_NOT_NULL(strstr(s.detail[1], "ARMED"));
    TEST_ASSERT_NOT_NULL(strstr(s.detail[2], "STOP"));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_real_path_walks_welcome_scan_linked_arm_done_finish);
    RUN_TEST(test_sim_path_skips_pairing_and_says_so);
    RUN_TEST(test_lost_link_returns_to_scan);
    RUN_TEST(test_later_leaves_without_finishing);
    RUN_TEST(test_scan_can_always_be_skipped_or_backed_out);
    RUN_TEST(test_scan_hint_after_timeout_tells_user_what_to_do);
    RUN_TEST(test_found_screen_shows_address);
    RUN_TEST(test_every_screen_has_an_instruction_and_both_buttons);
    RUN_TEST(test_last_step_states_kinomap_waits_for_movement);
    RUN_TEST(test_last_step_warns_when_the_belt_is_off);
    RUN_TEST(test_arm_screen_explains_arm_and_stop);
    return UNITY_END();
}
