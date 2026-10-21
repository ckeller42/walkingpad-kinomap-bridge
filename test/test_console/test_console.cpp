#include <string.h>
#include <stdio.h>
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
    TEST_ASSERT_EQUAL(Cmd::SpeedUp, parse("speed up"));
    TEST_ASSERT_EQUAL(Cmd::SpeedDown, parse("SPEED  Down"));
    TEST_ASSERT_EQUAL(Cmd::SpeedSet, parse("speed 3.5"));
    TEST_ASSERT_EQUAL(Cmd::Cp, parse("cp"));
    TEST_ASSERT_EQUAL(Cmd::Shot, parse("shot"));
    TEST_ASSERT_EQUAL(Cmd::Help, parse("help"));
    TEST_ASSERT_EQUAL(Cmd::Log, parse("log"));
    TEST_ASSERT_EQUAL(Cmd::Wifi, parse("wifi"));
    TEST_ASSERT_EQUAL(Cmd::Wifi, parse("wifi MyNet hunter2"));
    TEST_ASSERT_EQUAL(Cmd::DevOn, parse("dev on"));
    TEST_ASSERT_EQUAL(Cmd::DevOff, parse("dev off"));
    TEST_ASSERT_EQUAL(Cmd::UdpOn, parse("log udp on"));
    TEST_ASSERT_EQUAL(Cmd::UdpOff, parse("log udp off"));
    TEST_ASSERT_EQUAL(Cmd::Menu, parse("menu close"));
    TEST_ASSERT_EQUAL(Cmd::OtaCheck, parse("ota check"));
    // "log udp on" must not be swallowed by the bare "log" entry.
    TEST_ASSERT_EQUAL(Cmd::Log, parse("log"));
}

// parseWifiArg deliberately reads the RAW line, because normalise() lower-cases
// and collapses whitespace - either of which silently corrupts a credential.
void test_wifi_arg_preserves_case_and_spaces(void) {
    char ssid[48], pass[64];

    TEST_ASSERT_TRUE(parseWifiArg("wifi MyNet hunter2", ssid, sizeof ssid, pass, sizeof pass));
    TEST_ASSERT_EQUAL_STRING("MyNet", ssid);
    TEST_ASSERT_EQUAL_STRING("hunter2", pass);

    // Case must survive on both fields.
    TEST_ASSERT_TRUE(parseWifiArg("wifi FRITZ!Box-ABC PaSsWoRd", ssid, sizeof ssid, pass, sizeof pass));
    TEST_ASSERT_EQUAL_STRING("FRITZ!Box-ABC", ssid);
    TEST_ASSERT_EQUAL_STRING("PaSsWoRd", pass);

    // Everything after the one separating space is password, runs of spaces
    // included - collapsing these would store a credential that never works.
    TEST_ASSERT_TRUE(parseWifiArg("wifi Net correct  horse battery", ssid, sizeof ssid, pass, sizeof pass));
    TEST_ASSERT_EQUAL_STRING("Net", ssid);
    TEST_ASSERT_EQUAL_STRING("correct  horse battery", pass);

    // Trailing CRLF from the serial line must not become part of the password.
    TEST_ASSERT_TRUE(parseWifiArg("wifi Net secret\r\n", ssid, sizeof ssid, pass, sizeof pass));
    TEST_ASSERT_EQUAL_STRING("secret", pass);

    // An open network: ssid, no password.
    TEST_ASSERT_TRUE(parseWifiArg("wifi OpenNet", ssid, sizeof ssid, pass, sizeof pass));
    TEST_ASSERT_EQUAL_STRING("OpenNet", ssid);
    TEST_ASSERT_EQUAL_STRING("", pass);

    // "wifi" alone forgets both.
    TEST_ASSERT_TRUE(parseWifiArg("wifi", ssid, sizeof ssid, pass, sizeof pass));
    TEST_ASSERT_EQUAL_STRING("", ssid);
    TEST_ASSERT_EQUAL_STRING("", pass);

    // Overlong fields are refused, not truncated into something half-right.
    char longLine[200];
    snprintf(longLine, sizeof longLine, "wifi %.*s pw", 60, "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
    TEST_ASSERT_FALSE(parseWifiArg(longLine, ssid, sizeof ssid, pass, sizeof pass));

    TEST_ASSERT_FALSE(parseWifiArg("speed 3.5", ssid, sizeof ssid, pass, sizeof pass));
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

void test_speed_value_parsing(void) {
    unsigned char t = 0;
    TEST_ASSERT_EQUAL(SpeedArg::Ok, parseSpeedArg("speed 3.5", 50, 600, t));
    TEST_ASSERT_EQUAL_UINT8(35, t);
    TEST_ASSERT_EQUAL(SpeedArg::Ok, parseSpeedArg("SPEED 0.50", 50, 600, t));
    TEST_ASSERT_EQUAL_UINT8(5, t);
    TEST_ASSERT_EQUAL(SpeedArg::Ok, parseSpeedArg("speed 6", 50, 600, t));
    TEST_ASSERT_EQUAL_UINT8(60, t);
    TEST_ASSERT_EQUAL(SpeedArg::OutOfRange, parseSpeedArg("speed 0.4", 50, 600, t));
    TEST_ASSERT_EQUAL(SpeedArg::OutOfRange, parseSpeedArg("speed 6.1", 50, 600, t));
    TEST_ASSERT_EQUAL(SpeedArg::OutOfRange, parseSpeedArg("speed 0", 50, 600, t));
    TEST_ASSERT_EQUAL(SpeedArg::Bad, parseSpeedArg("speed fast", 50, 600, t));
    TEST_ASSERT_EQUAL(SpeedArg::Bad, parseSpeedArg("speed 3.55", 50, 600, t));
    TEST_ASSERT_EQUAL(SpeedArg::Bad, parseSpeedArg("speed 3.", 50, 600, t));
    TEST_ASSERT_EQUAL(SpeedArg::Bad, parseSpeedArg("speed -3", 50, 600, t));
    TEST_ASSERT_EQUAL(SpeedArg::Bad, parseSpeedArg("speed 3.5x", 50, 600, t));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_every_documented_command_parses);
    RUN_TEST(test_wifi_arg_preserves_case_and_spaces);
    RUN_TEST(test_case_and_whitespace_are_forgiven);
    RUN_TEST(test_unknown_and_prefix_commands_are_unknown_not_silent);
    RUN_TEST(test_blank_line_is_empty);
    RUN_TEST(test_speed_value_parsing);
    RUN_TEST(test_line_buffer_assembles_lines_and_handles_crlf);
    RUN_TEST(test_overlong_line_is_flagged_and_next_line_is_clean);
    return UNITY_END();
}
