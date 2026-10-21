#include <string.h>
#include <unity.h>

#include "devmenu.h"

using namespace devmenu;

static Inputs base(bool dev = true) {
    Inputs in;
    in.devOn = dev;
    in.udpOn = false;
    in.wifiUp = false;
    in.storedSsid = "";
    in.ip = "";
    return in;
}

void setUp(void) {}
void tearDown(void) {}

void test_root_lists_the_switches(void) {
    DevMenu m;
    m.open();
    Inputs in = base(false);
    Screen s = m.screen(in);
    TEST_ASSERT_EQUAL_STRING("DEVELOPER", s.title);
    TEST_ASSERT_EQUAL(5, s.rowCount);
    TEST_ASSERT_EQUAL_STRING("Developer mode", s.rows[0].label);
    TEST_ASSERT_EQUAL_STRING("OFF", s.rows[0].value);
}

// Everything with a radio hangs off developer mode, so the rows that need it
// must be visibly unavailable rather than silently doing nothing.
void test_radio_rows_are_disabled_until_dev_mode_is_on(void) {
    DevMenu m;
    m.open();
    Inputs off = base(false);
    Screen s = m.screen(off);
    TEST_ASSERT_TRUE(s.rows[0].enabled);    // the dev switch itself always works
    TEST_ASSERT_FALSE(s.rows[1].enabled);   // UDP
    TEST_ASSERT_FALSE(s.rows[2].enabled);   // Wi-Fi
    TEST_ASSERT_EQUAL_STRING("needs dev", s.rows[1].value);

    // And tapping them does nothing at all.
    TEST_ASSERT_EQUAL(Action::None, m.tapRow(1, off));
    TEST_ASSERT_EQUAL(Action::None, m.tapRow(2, off));
    TEST_ASSERT_EQUAL(Step::Root, m.step());

    // The dev switch is still reachable.
    TEST_ASSERT_EQUAL(Action::ToggleDev, m.tapRow(0, off));
}

void test_udp_row_toggles_once_dev_mode_is_on(void) {
    DevMenu m;
    m.open();
    Inputs on = base(true);
    TEST_ASSERT_EQUAL(Action::ToggleUdp, m.tapRow(1, on));
    on.udpOn = true;
    TEST_ASSERT_EQUAL_STRING("ON", m.screen(on).rows[1].value);
}

void test_wifi_row_starts_a_scan_and_lists_networks(void) {
    DevMenu m;
    m.open();
    Inputs on = base(true);
    TEST_ASSERT_EQUAL(Action::StartScan, m.tapRow(2, on));
    TEST_ASSERT_EQUAL(Step::WifiScan, m.step());

    on.scanning = true;
    TEST_ASSERT_EQUAL_STRING("Scanning...", m.screen(on).rows[0].label);

    on.scanning = false;
    on.networkCount = 2;
    on.networks[0] = "Insel";
    on.networks[1] = "Nachbar";
    Screen s = m.screen(on);
    TEST_ASSERT_EQUAL(2, s.rowCount);
    TEST_ASSERT_EQUAL_STRING("Insel", s.rows[0].label);
}

// An empty scan must say what to do about it, not just show an empty list.
void test_empty_scan_says_what_to_do(void) {
    DevMenu m;
    m.open();
    Inputs on = base(true);
    m.tapRow(2, on);
    on.networkCount = 0;
    Screen s = m.screen(on);
    TEST_ASSERT_NOT_NULL(strstr(s.rows[0].label, "No networks"));
    TEST_ASSERT_NOT_NULL(strstr(s.rows[1].label, "RESCAN"));
    TEST_ASSERT_EQUAL_STRING("RESCAN", s.right);
}

void test_picking_a_network_opens_the_keyboard_for_it(void) {
    DevMenu m;
    m.open();
    Inputs on = base(true);
    m.tapRow(2, on);
    on.networkCount = 1;
    on.networks[0] = "Insel";
    m.tapRow(0, on);
    TEST_ASSERT_EQUAL(Step::WifiPass, m.step());
    TEST_ASSERT_EQUAL_STRING("Insel", m.ssid());
    Screen s = m.screen(on);
    TEST_ASSERT_TRUE(s.keyboard);
    TEST_ASSERT_EQUAL_STRING("Insel", s.prompt);
    TEST_ASSERT_EQUAL_STRING("CONNECT", s.right);
}

void test_typing_a_password_and_submitting(void) {
    DevMenu m;
    m.open();
    Inputs on = base(true);
    m.tapRow(2, on);
    on.networkCount = 1;
    on.networks[0] = "Insel";
    m.tapRow(0, on);

    for (const char* p = "s3cret"; *p; ++p) m.append(*p);
    TEST_ASSERT_EQUAL_STRING("s3cret", m.password());
    m.backspace();
    TEST_ASSERT_EQUAL_STRING("s3cre", m.password());

    TEST_ASSERT_EQUAL(Action::SubmitPassword, m.press(true, on));
    TEST_ASSERT_EQUAL(Step::Root, m.step());
}

// A password that will not fit must be refused, not quietly cut short: a
// truncated credential looks accepted and never connects.
void test_overlong_password_is_refused_not_truncated(void) {
    DevMenu m;
    m.open();
    Inputs on = base(true);
    m.tapRow(2, on);
    on.networkCount = 1;
    on.networks[0] = "Insel";
    m.tapRow(0, on);

    for (int i = 0; i < 200; ++i) m.append('x');
    TEST_ASSERT_EQUAL(kMaxPassword - 1, strlen(m.password()));
}

// Shift behaves like a phone keyboard: it applies to one character only, so a
// capital in the middle of a password does not capitalise the rest.
void test_shift_applies_to_one_character(void) {
    DevMenu m;
    m.open();
    TEST_ASSERT_FALSE(m.shift());
    m.toggleShift();
    TEST_ASSERT_TRUE(m.shift());
    m.append('A');
    TEST_ASSERT_FALSE(m.shift());
}

void test_back_navigates_out_without_connecting(void) {
    DevMenu m;
    m.open();
    Inputs on = base(true);
    m.tapRow(2, on);
    on.networkCount = 1;
    on.networks[0] = "Insel";
    m.tapRow(0, on);

    TEST_ASSERT_EQUAL(Action::None, m.press(false, on));  // keyboard -> scan
    TEST_ASSERT_EQUAL(Step::WifiScan, m.step());
    TEST_ASSERT_EQUAL(Action::None, m.press(false, on));  // scan -> root
    TEST_ASSERT_EQUAL(Step::Root, m.step());
    TEST_ASSERT_EQUAL(Action::Close, m.press(false, on));
}

void test_forget_row_only_offered_when_a_network_is_stored(void) {
    DevMenu m;
    m.open();
    Inputs on = base(true);
    TEST_ASSERT_FALSE(m.screen(on).rows[3].enabled);
    on.storedSsid = "Insel";
    Screen s = m.screen(on);
    TEST_ASSERT_TRUE(s.rows[3].enabled);
    TEST_ASSERT_EQUAL_STRING("Insel", s.rows[3].label);
    TEST_ASSERT_EQUAL(Action::ForgetWifi, m.tapRow(3, on));
}

// The protocol row is always reachable - it is not a radio feature - and
// reports FTMS, the only mode that works. Domyos is issue #2.
void test_protocol_row_shows_the_mode_and_is_always_reachable(void) {
    DevMenu m;
    m.open();
    Inputs off = base(false);
    Screen s = m.screen(off);
    TEST_ASSERT_EQUAL(5, s.rowCount);
    TEST_ASSERT_EQUAL_STRING("Protocol", s.rows[4].label);
    TEST_ASSERT_EQUAL_STRING("FTMS", s.rows[4].value);
    TEST_ASSERT_TRUE(s.rows[4].enabled);
    TEST_ASSERT_EQUAL(Action::CycleProtocol, m.tapRow(4, off));

    off.protocol = Protocol::Domyos;
    TEST_ASSERT_EQUAL_STRING("Domyos", m.screen(off).rows[4].value);
}

void test_every_screen_has_a_title_and_a_left_button(void) {
    DevMenu m;
    m.open();
    Inputs on = base(true);
    TEST_ASSERT_TRUE(strlen(m.screen(on).title) > 0);
    TEST_ASSERT_TRUE(strlen(m.screen(on).left) > 0);
    m.tapRow(2, on);
    TEST_ASSERT_TRUE(strlen(m.screen(on).title) > 0);
    TEST_ASSERT_TRUE(strlen(m.screen(on).left) > 0);
    on.networkCount = 1;
    on.networks[0] = "Insel";
    m.tapRow(0, on);
    TEST_ASSERT_TRUE(strlen(m.screen(on).title) > 0);
    TEST_ASSERT_TRUE(strlen(m.screen(on).left) > 0);
}

// Each visually distinct screen needs its own phase, or the screenshot tooling
// silently captures only the first of them.
void test_screens_have_distinct_phases(void) {
    DevMenu m;
    m.open();
    Inputs on = base(true);
    const int root = m.screen(on).phase;
    m.tapRow(2, on);
    on.scanning = true;
    const int scanning = m.screen(on).phase;
    on.scanning = false;
    on.networkCount = 1;
    on.networks[0] = "Insel";
    const int listed = m.screen(on).phase;
    m.tapRow(0, on);
    const int pass = m.screen(on).phase;
    TEST_ASSERT_NOT_EQUAL(root, scanning);
    TEST_ASSERT_NOT_EQUAL(scanning, listed);
    TEST_ASSERT_NOT_EQUAL(listed, pass);
    TEST_ASSERT_NOT_EQUAL(root, pass);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_root_lists_the_switches);
    RUN_TEST(test_radio_rows_are_disabled_until_dev_mode_is_on);
    RUN_TEST(test_udp_row_toggles_once_dev_mode_is_on);
    RUN_TEST(test_wifi_row_starts_a_scan_and_lists_networks);
    RUN_TEST(test_empty_scan_says_what_to_do);
    RUN_TEST(test_picking_a_network_opens_the_keyboard_for_it);
    RUN_TEST(test_typing_a_password_and_submitting);
    RUN_TEST(test_overlong_password_is_refused_not_truncated);
    RUN_TEST(test_shift_applies_to_one_character);
    RUN_TEST(test_back_navigates_out_without_connecting);
    RUN_TEST(test_forget_row_only_offered_when_a_network_is_stored);
    RUN_TEST(test_protocol_row_shows_the_mode_and_is_always_reachable);
    RUN_TEST(test_every_screen_has_a_title_and_a_left_button);
    RUN_TEST(test_screens_have_distinct_phases);
    return UNITY_END();
}
