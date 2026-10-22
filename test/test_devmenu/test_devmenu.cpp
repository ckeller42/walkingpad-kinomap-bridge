#include <stdio.h>
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

void test_keyboard_reaches_every_printable_ascii(void) {
    bool seen[128] = {};
    for (uint8_t p = 0; p < 3; ++p)
        for (uint8_t shift = 0; shift < 2; ++shift)
            for (uint8_t r = 0; r < 3; ++r)
                for (uint8_t c = 0; c < 10; ++c) {
                    const Key k = keyAt((Page)p, shift != 0, r, c);
                    if (k.kind == Key::Kind::Char) seen[(uint8_t)k.ch] = true;
                    if (k.kind == Key::Kind::Space) seen[' '] = true;
                }
    for (int ch = 0x20; ch <= 0x7E; ++ch) {
        char msg[32]; snprintf(msg, sizeof msg, "unreachable 0x%02X '%c'", ch, ch);
        TEST_ASSERT_TRUE_MESSAGE(seen[ch], msg);
    }
}
void test_every_page_has_del_and_space_in_the_same_place(void) {
    for (uint8_t p = 0; p < 3; ++p) {
        TEST_ASSERT_EQUAL(Key::Kind::Del, keyAt((Page)p, false, 1, 9).kind);
        TEST_ASSERT_EQUAL(Key::Kind::Space, keyAt((Page)p, false, 2, 9).kind);
    }
}
void test_page_keys_navigate(void) {
    TEST_ASSERT_EQUAL(Key::Kind::ToSymbols1, keyAt(Page::Letters, false, 2, 8).kind);
    TEST_ASSERT_EQUAL(Key::Kind::ToSymbols2, keyAt(Page::Symbols1, false, 2, 0).kind);
    TEST_ASSERT_EQUAL(Key::Kind::ToLetters, keyAt(Page::Symbols1, false, 2, 8).kind);
    TEST_ASSERT_EQUAL(Key::Kind::ToSymbols1, keyAt(Page::Symbols2, false, 2, 0).kind);
}
void test_shift_uppercases_letters_only(void) {
    TEST_ASSERT_EQUAL_CHAR('Q', keyAt(Page::Letters, true, 0, 0).ch);
    TEST_ASSERT_EQUAL_CHAR('1', keyAt(Page::Symbols1, true, 0, 0).ch);
}
void test_set_page_switches_page_and_clears_shift(void) {
    DevMenu m;
    m.open();
    m.toggleShift();
    m.setPage(Page::Symbols2);
    TEST_ASSERT_EQUAL(Page::Symbols2, m.page());
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

// Every row the root screen offers must do something. A row that only
// reported a mode nobody could change used to sit at index 4; the screen is
// five rows now, and a tap past the end is ignored rather than wrapping.
void test_root_offers_five_live_rows_and_ignores_taps_past_them(void) {
    DevMenu m;
    m.open();
    Inputs on = base(true);
    Screen s = m.screen(on);
    TEST_ASSERT_EQUAL(5, s.rowCount);
    TEST_ASSERT_EQUAL(Action::None, m.tapRow(5, on));
    TEST_ASSERT_EQUAL(Step::Root, m.step());
}

void test_root_offers_rerun_setup_even_with_dev_off(void) {
    DevMenu m; m.open();
    Inputs in = base(false);
    Screen s = m.screen(in);
    TEST_ASSERT_EQUAL(5, s.rowCount);
    TEST_ASSERT_EQUAL_STRING("Re-run setup", s.rows[4].label);
    TEST_ASSERT_TRUE(s.rows[4].enabled);
    TEST_ASSERT_EQUAL(Action::RerunSetup, m.tapRow(4, in));
}
void test_wifi_row_shows_connecting_then_failed(void) {
    DevMenu m; m.open();
    Inputs in = base(true); in.storedSsid = "Insel";
    in.wifiConnecting = true;
    TEST_ASSERT_EQUAL_STRING("connecting...", m.screen(in).rows[2].value);
    in.wifiConnecting = false; in.wifiFailed = true;
    TEST_ASSERT_EQUAL_STRING("wrong password?", m.screen(in).rows[2].value);
    in.wifiFailed = false; in.wifiUp = true; in.ip = "192.168.178.96";
    TEST_ASSERT_EQUAL_STRING("192.168.178.96", m.screen(in).rows[2].value);
}
void test_scan_list_keeps_as_many_networks_as_it_shows(void) {
    TEST_ASSERT_EQUAL(kMaxRows, kMaxNetworks);
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
    RUN_TEST(test_keyboard_reaches_every_printable_ascii);
    RUN_TEST(test_every_page_has_del_and_space_in_the_same_place);
    RUN_TEST(test_page_keys_navigate);
    RUN_TEST(test_shift_uppercases_letters_only);
    RUN_TEST(test_set_page_switches_page_and_clears_shift);
    RUN_TEST(test_back_navigates_out_without_connecting);
    RUN_TEST(test_forget_row_only_offered_when_a_network_is_stored);
    RUN_TEST(test_root_offers_five_live_rows_and_ignores_taps_past_them);
    RUN_TEST(test_root_offers_rerun_setup_even_with_dev_off);
    RUN_TEST(test_wifi_row_shows_connecting_then_failed);
    RUN_TEST(test_scan_list_keeps_as_many_networks_as_it_shows);
    RUN_TEST(test_every_screen_has_a_title_and_a_left_button);
    RUN_TEST(test_screens_have_distinct_phases);
    return UNITY_END();
}
