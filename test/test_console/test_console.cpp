#include <string.h>
#include <unity.h>

#include "console_cmd.h"

using namespace console;

void setUp(void) {}
void tearDown(void) {}

void test_every_documented_command_parses(void) {
    TEST_ASSERT_EQUAL(Cmd::Status, parse("status"));
    TEST_ASSERT_EQUAL(Cmd::Arm, parse("arm"));
    TEST_ASSERT_EQUAL(Cmd::Disarm, parse("disarm"));
    TEST_ASSERT_EQUAL(Cmd::Start, parse("start"));
    TEST_ASSERT_EQUAL(Cmd::Stop, parse("stop"));
    TEST_ASSERT_EQUAL(Cmd::SimOn, parse("sim on"));
    TEST_ASSERT_EQUAL(Cmd::SimOff, parse("sim off"));
    TEST_ASSERT_EQUAL(Cmd::Wizard, parse("wizard"));
    TEST_ASSERT_EQUAL(Cmd::WizardNext, parse("wizard next"));
    TEST_ASSERT_EQUAL(Cmd::WizardBack, parse("wizard back"));
    TEST_ASSERT_EQUAL(Cmd::Shot, parse("shot"));
    TEST_ASSERT_EQUAL(Cmd::Help, parse("help"));
}

void test_case_and_whitespace_are_forgiven(void) {
    TEST_ASSERT_EQUAL(Cmd::SimOn, parse("  SIM \t On  "));
    TEST_ASSERT_EQUAL(Cmd::Arm, parse("ARM"));
}

void test_unknown_and_prefix_commands_are_unknown_not_silent(void) {
    TEST_ASSERT_EQUAL(Cmd::Unknown, parse("fly"));
    TEST_ASSERT_EQUAL(Cmd::Unknown, parse("arm now"));
    TEST_ASSERT_EQUAL(Cmd::Unknown, parse("sim"));
    TEST_ASSERT_EQUAL(Cmd::Unknown, parse("sim maybe"));
}

void test_blank_line_is_empty(void) {
    TEST_ASSERT_EQUAL(Cmd::Empty, parse(""));
    TEST_ASSERT_EQUAL(Cmd::Empty, parse("   "));
}

void test_line_buffer_assembles_lines_and_handles_crlf(void) {
    LineBuffer b;
    const char* in = "arm\r\nstop\n";
    int lines = 0;
    for (const char* p = in; *p; ++p) {
        if (b.feed(*p)) {
            ++lines;
            TEST_ASSERT_EQUAL_STRING(lines == 1 ? "arm" : "stop", b.line());
            TEST_ASSERT_FALSE(b.overflowed());
        }
    }
    TEST_ASSERT_EQUAL_INT(2, lines);
}

void test_overlong_line_is_flagged_and_next_line_is_clean(void) {
    LineBuffer b;
    bool done = false;
    for (int i = 0; i < 200; ++i) done = b.feed('a') || done;
    TEST_ASSERT_FALSE(done);
    TEST_ASSERT_TRUE(b.feed('\n'));
    TEST_ASSERT_TRUE(b.overflowed());
    for (const char* p = "stop"; *p; ++p) b.feed(*p);
    TEST_ASSERT_TRUE(b.feed('\n'));
    TEST_ASSERT_FALSE(b.overflowed());
    TEST_ASSERT_EQUAL_STRING("stop", b.line());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_every_documented_command_parses);
    RUN_TEST(test_case_and_whitespace_are_forgiven);
    RUN_TEST(test_unknown_and_prefix_commands_are_unknown_not_silent);
    RUN_TEST(test_blank_line_is_empty);
    RUN_TEST(test_line_buffer_assembles_lines_and_handles_crlf);
    RUN_TEST(test_overlong_line_is_flagged_and_next_line_is_clean);
    return UNITY_END();
}
