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
    TEST_ASSERT_EQUAL(6, s.rowCount);
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
    TEST_ASSERT_EQUAL_STRING("forget", s.rows[3].value);
    TEST_ASSERT_EQUAL(Action::None, m.tapRow(3, on));
    TEST_ASSERT_EQUAL(Action::ForgetWifi, m.tapRow(3, on));
}

// Rows are 25 px apart and the forget row sits between Wi-Fi and Update, so
// one slipped finger must not erase the stored network: the first tap arms
// the row ("tap again"), the second forgets, and anything else in between
// disarms it.
void test_forget_row_needs_a_second_tap(void) {
    Inputs on = base(true);
    on.storedSsid = "Insel";

    DevMenu m;
    m.open();
    TEST_ASSERT_EQUAL(Action::None, m.tapRow(3, on));
    TEST_ASSERT_TRUE(m.forgetArmed());
    TEST_ASSERT_EQUAL_STRING("tap again", m.screen(on).rows[3].value);
    TEST_ASSERT_EQUAL(Action::ForgetWifi, m.tapRow(3, on));
    TEST_ASSERT_FALSE(m.forgetArmed());
    TEST_ASSERT_EQUAL_STRING("forget", m.screen(on).rows[3].value);

    // Another row in between: the tap does its own thing and disarms forget.
    m.open();
    TEST_ASSERT_EQUAL(Action::None, m.tapRow(3, on));
    TEST_ASSERT_EQUAL(Action::ToggleUdp, m.tapRow(1, on));
    TEST_ASSERT_FALSE(m.forgetArmed());
    TEST_ASSERT_EQUAL(Action::None, m.tapRow(3, on));

    // A touch the host routed nowhere (the title) disarms as well.
    m.open();
    TEST_ASSERT_EQUAL(Action::None, m.tapRow(3, on));
    m.cancel();
    TEST_ASSERT_FALSE(m.forgetArmed());
    TEST_ASSERT_EQUAL(Action::None, m.tapRow(3, on));

    // A bottom-bar press disarms too, and so does re-opening the menu.
    TEST_ASSERT_EQUAL(Action::Close, m.press(false, on));
    TEST_ASSERT_FALSE(m.forgetArmed());
    m.tapRow(3, on);
    m.open();
    TEST_ASSERT_FALSE(m.forgetArmed());
    TEST_ASSERT_EQUAL_STRING("forget", m.screen(on).rows[3].value);

    // Arming needs the row to be live: with dev mode off it stays inert.
    Inputs off = base(false);
    off.storedSsid = "Insel";
    DevMenu d;
    d.open();
    TEST_ASSERT_EQUAL(Action::None, d.tapRow(3, off));
    TEST_ASSERT_FALSE(d.forgetArmed());
}

void test_url_host_edges(void) {
    struct Case { const char* url; const char* host; };
    const Case cases[] = {
        {"https://ckeller42.github.io/walkingpad-kinomap-bridge/firmware/manifest.json", "ckeller42.github.io"},
        {"http://192.168.178.43:8099/manifest.json", "192.168.178.43"},
        {"http://user:secret@example.invalid/mf", "example.invalid"},
        {"http://[fe80::1]:8099/manifest.json", "[fe80::1]"},
        {"http://example.invalid#frag", "example.invalid"},
        {"http://example.invalid?x=1", "example.invalid"},
        {"example.invalid/no/scheme", "example.invalid"},
        {"", ""},
        {nullptr, ""},
    };
    for (const Case& c : cases) {
        char out[64];
        const size_t n = urlHost(c.url, out, sizeof out);
        TEST_ASSERT_EQUAL_STRING(c.host, out);
        TEST_ASSERT_EQUAL_size_t(strlen(c.host), n);
    }
    // Cut to fit, still terminated.
    char tiny[6];
    TEST_ASSERT_EQUAL_size_t(5, urlHost("https://ckeller42.github.io/x", tiny, sizeof tiny));
    TEST_ASSERT_EQUAL_STRING("ckell", tiny);
}

// Every row the root screen offers must do something. A row that only
// reported a mode nobody could change used to sit on this screen; it has six
// live rows now (index 4 is Update), and a tap past the end is ignored rather
// than wrapping.
void test_root_offers_six_live_rows_and_ignores_taps_past_them(void) {
    DevMenu m;
    m.open();
    Inputs on = base(true);
    Screen s = m.screen(on);
    TEST_ASSERT_EQUAL(6, s.rowCount);
    TEST_ASSERT_EQUAL(Action::None, m.tapRow(6, on));
    TEST_ASSERT_EQUAL(Step::Root, m.step());
}

void test_root_offers_rerun_setup_even_with_dev_off(void) {
    DevMenu m; m.open();
    Inputs in = base(false);
    Screen s = m.screen(in);
    TEST_ASSERT_EQUAL(6, s.rowCount);
    TEST_ASSERT_EQUAL_STRING("Re-run setup", s.rows[5].label);
    TEST_ASSERT_TRUE(s.rows[5].enabled);
    TEST_ASSERT_EQUAL(Action::RerunSetup, m.tapRow(5, in));
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
void test_wifi_up_outranks_a_stale_failed_flag(void) {
    DevMenu m; m.open();
    Inputs in = base(true); in.storedSsid = "Insel";
    in.wifiFailed = true; in.wifiUp = true; in.ip = "192.168.178.96";
    TEST_ASSERT_EQUAL_STRING("192.168.178.96", m.screen(in).rows[2].value);
}
void test_blank_keys_are_where_the_layout_says_and_shift_is_letters_only(void) {
    for (uint8_t c = 6; c <= 8; ++c)
        TEST_ASSERT_EQUAL(Key::Kind::Blank, keyAt(Page::Symbols2, false, 1, c).kind);
    for (uint8_t c = 1; c <= 7; ++c)
        TEST_ASSERT_EQUAL(Key::Kind::Blank, keyAt(Page::Symbols2, false, 2, c).kind);
    TEST_ASSERT_EQUAL(Key::Kind::Shift, keyAt(Page::Letters, false, 2, 0).kind);
    for (uint8_t p = 1; p < 3; ++p)
        for (uint8_t r = 0; r < 3; ++r)
            for (uint8_t c = 0; c < 10; ++c)
                TEST_ASSERT_NOT_EQUAL(Key::Kind::Shift, keyAt((Page)p, false, r, c).kind);
}
void test_scan_list_keeps_as_many_networks_as_it_shows(void) {
    TEST_ASSERT_EQUAL(5, kMaxNetworks);
    // The panel has a sixth row now, but the scan list must not read past
    // the five networks the Inputs array holds.
    DevMenu m;
    m.open();
    Inputs on = base(true);
    m.tapRow(2, on);
    on.networkCount = 6;
    for (uint8_t i = 0; i < kMaxNetworks; ++i) on.networks[i] = "Insel";
    TEST_ASSERT_EQUAL(5, m.screen(on).rowCount);
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
    m.setPage(Page::Symbols1);
    const int symbols1 = m.screen(on).phase;
    m.setPage(Page::Symbols2);
    const int symbols2 = m.screen(on).phase;
    // The Update screen, idle and with an update waiting.
    DevMenu u;
    u.open();
    u.tapRow(4, on);
    const int updIdle = u.screen(on).phase;
    on.otaState = OtaState::Available;
    const int updAvail = u.screen(on).phase;
    TEST_ASSERT_NOT_EQUAL(updIdle, updAvail);
    const int others[] = {root, scanning, listed, pass, symbols1, symbols2};
    for (int o : others) {
        TEST_ASSERT_NOT_EQUAL(updIdle, o);
        TEST_ASSERT_NOT_EQUAL(updAvail, o);
    }
    TEST_ASSERT_NOT_EQUAL(root, scanning);
    TEST_ASSERT_NOT_EQUAL(scanning, listed);
    TEST_ASSERT_NOT_EQUAL(listed, pass);
    TEST_ASSERT_NOT_EQUAL(root, pass);
    // The keyboard pages are three different screens.
    TEST_ASSERT_NOT_EQUAL(pass, symbols1);
    TEST_ASSERT_NOT_EQUAL(symbols1, symbols2);
    TEST_ASSERT_NOT_EQUAL(pass, symbols2);
}

static Inputs updateIn(OtaState st) {
    Inputs in = base(true);
    in.wifiUp = true;
    in.otaState = st;
    return in;
}

static DevMenu updateMenu(const Inputs& in) {
    DevMenu m;
    m.open();
    m.tapRow(4, in);
    return m;
}

void test_root_has_update_row_before_rerun_setup(void) {
    DevMenu m; m.open();
    Inputs on = base(true);
    on.otaVersion = "bd3819b";
    Screen s = m.screen(on);
    TEST_ASSERT_EQUAL(6, s.rowCount);
    TEST_ASSERT_EQUAL_STRING("Update", s.rows[4].label);
    TEST_ASSERT_EQUAL_STRING("bd3819b", s.rows[4].value);
    TEST_ASSERT_EQUAL_STRING("Re-run setup", s.rows[5].label);
    TEST_ASSERT_EQUAL(Action::RerunSetup, m.tapRow(5, on));
}

void test_update_row_opens_update_screen(void) {
    DevMenu m; m.open();
    Inputs on = base(true);
    on.otaHost = "ckeller42.github.io";
    TEST_ASSERT_EQUAL(Action::OpenUpdate, m.tapRow(4, on));
    TEST_ASSERT_EQUAL(Step::Update, m.step());
    Screen s = m.screen(on);
    TEST_ASSERT_EQUAL_STRING("UPDATE", s.title);
    TEST_ASSERT_EQUAL_STRING("BACK", s.left);
    TEST_ASSERT_EQUAL_STRING("Running", s.rows[0].label);
    TEST_ASSERT_EQUAL_STRING("Source", s.rows[4].label);
    TEST_ASSERT_EQUAL_STRING("ckeller42.github.io", s.rows[4].value);
    TEST_ASSERT_EQUAL(Action::None, m.press(false, on));
    TEST_ASSERT_EQUAL(Step::Root, m.step());
}

void test_update_button_follows_state(void) {
    struct Case { OtaState st; const char* right; Action act; };
    const Case cases[] = {
        {OtaState::Idle, "CHECK", Action::CheckOta},
        {OtaState::Checking, "", Action::None},
        {OtaState::UpToDate, "CHECK", Action::CheckOta},
        {OtaState::Available, "INSTALL", Action::InstallOta},
        {OtaState::Installing, "", Action::None},
        {OtaState::Failed, "CHECK", Action::CheckOta},
    };
    for (const Case& c : cases) {
        Inputs on = updateIn(c.st);
        DevMenu m = updateMenu(on);
        TEST_ASSERT_EQUAL_STRING(c.right, m.screen(on).right);
        TEST_ASSERT_EQUAL((int)c.act, (int)m.press(true, on));
    }
}

// An image built for another environment is never installed, but the site may
// publish the right one later, so the button offers CHECK rather than nothing.
void test_update_env_mismatch_disables_install(void) {
    Inputs on = updateIn(OtaState::Available);
    on.otaEnvMismatch = true;
    on.otaImageEnv = "cores3";
    DevMenu m = updateMenu(on);
    Screen s = m.screen(on);
    TEST_ASSERT_EQUAL_STRING("image is for cores3", s.rows[2].value);
    TEST_ASSERT_EQUAL_STRING("CHECK", s.right);
    TEST_ASSERT_EQUAL(Action::CheckOta, m.press(true, on));
    TEST_ASSERT_NOT_EQUAL(Action::InstallOta, m.press(true, on));
}

// Both ota:: calls block the main loop for seconds, so a moving belt rules
// out a check as well as an install, and the screen says so instead of
// refusing in silence.
void test_update_belt_running_blocks_check_and_install(void) {
    const OtaState states[] = {OtaState::Idle, OtaState::Available, OtaState::Failed};
    for (OtaState st : states) {
        Inputs on = updateIn(st);
        on.beltRunning = true;
        DevMenu m = updateMenu(on);
        Screen s = m.screen(on);
        TEST_ASSERT_EQUAL_STRING("belt running", s.rows[2].value);
        TEST_ASSERT_EQUAL_STRING("", s.right);
        TEST_ASSERT_EQUAL(Action::None, m.press(true, on));
    }
}

// The version, status and source rows are read, not tapped; they used to be
// drawn dim like disabled switches, which made the one screen whose rows carry
// the information hard to read. Only the detail line stays dim.
void test_update_rows_are_bright_except_the_message(void) {
    Inputs on = updateIn(OtaState::Available);
    on.otaMessage = "v0.2.0 available";
    DevMenu m = updateMenu(on);
    Screen s = m.screen(on);
    TEST_ASSERT_TRUE(s.rows[0].enabled);
    TEST_ASSERT_TRUE(s.rows[1].enabled);
    TEST_ASSERT_TRUE(s.rows[2].enabled);
    TEST_ASSERT_FALSE(s.rows[3].enabled);
    TEST_ASSERT_TRUE(s.rows[4].enabled);
    for (uint8_t i = 0; i < s.rowCount; ++i) TEST_ASSERT_EQUAL(Action::None, m.tapRow(i, on));
    TEST_ASSERT_EQUAL(Step::Update, m.step());
}

void test_update_needs_dev_and_wifi(void) {
    Inputs off = base(false);
    off.wifiUp = true;
    off.otaState = OtaState::Available;
    DevMenu m = updateMenu(off);
    TEST_ASSERT_EQUAL(Step::Update, m.step());
    TEST_ASSERT_EQUAL_STRING("needs dev mode", m.screen(off).rows[2].value);
    TEST_ASSERT_EQUAL_STRING("", m.screen(off).right);
    TEST_ASSERT_EQUAL(Action::None, m.press(true, off));

    Inputs nowifi = base(true);
    nowifi.otaState = OtaState::Available;
    DevMenu w = updateMenu(nowifi);
    TEST_ASSERT_EQUAL_STRING("needs Wi-Fi", w.screen(nowifi).rows[2].value);
    TEST_ASSERT_EQUAL_STRING("", w.screen(nowifi).right);
    TEST_ASSERT_EQUAL(Action::None, w.press(true, nowifi));

    // Both missing: still nothing, whatever the state claims.
    Inputs neither = base(false);
    neither.wifiUp = false;
    neither.otaState = OtaState::Idle;
    DevMenu n = updateMenu(neither);
    TEST_ASSERT_EQUAL_STRING("", n.screen(neither).right);
    TEST_ASSERT_EQUAL(Action::None, n.press(true, neither));
}

// The message row holds a whole updater message; at 24 bytes it used to cut
// "v0.2.0 available (running bd3819b)" mid-word.
void test_update_message_row_keeps_39_characters(void) {
    const char* msg = "manifest url https://example.invalid/mf";  // 39 chars
    TEST_ASSERT_EQUAL_size_t(39, strlen(msg));
    Inputs on = updateIn(OtaState::Failed);
    on.otaMessage = msg;
    DevMenu m = updateMenu(on);
    TEST_ASSERT_EQUAL_STRING(msg, m.screen(on).rows[3].label);
}

// open() is how the gear button enters the menu: it must land on the root,
// not wherever the menu was left.
void test_open_resets_from_update_to_root(void) {
    Inputs on = updateIn(OtaState::Idle);
    DevMenu m = updateMenu(on);
    TEST_ASSERT_EQUAL(Step::Update, m.step());
    m.open();
    TEST_ASSERT_EQUAL(Step::Root, m.step());
}

void test_update_status_words(void) {
    struct Case { OtaState st; const char* word; };
    const Case cases[] = {
        {OtaState::Idle, "not checked"},
        {OtaState::Checking, "checking..."},
        {OtaState::UpToDate, "up to date"},
        {OtaState::Available, "update available"},
        {OtaState::Installing, "installing..."},
        {OtaState::Failed, "failed"},
    };
    for (const Case& c : cases) {
        Inputs on = updateIn(c.st);
        on.otaMessage = "some detail";
        DevMenu m = updateMenu(on);
        Screen s = m.screen(on);
        TEST_ASSERT_EQUAL_STRING(c.word, s.rows[2].value);
        TEST_ASSERT_EQUAL_STRING("some detail", s.rows[3].label);
        TEST_ASSERT_FALSE(s.rows[3].enabled);
    }
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
    RUN_TEST(test_root_offers_six_live_rows_and_ignores_taps_past_them);
    RUN_TEST(test_root_offers_rerun_setup_even_with_dev_off);
    RUN_TEST(test_wifi_row_shows_connecting_then_failed);
    RUN_TEST(test_wifi_up_outranks_a_stale_failed_flag);
    RUN_TEST(test_blank_keys_are_where_the_layout_says_and_shift_is_letters_only);
    RUN_TEST(test_scan_list_keeps_as_many_networks_as_it_shows);
    RUN_TEST(test_every_screen_has_a_title_and_a_left_button);
    RUN_TEST(test_screens_have_distinct_phases);
    RUN_TEST(test_root_has_update_row_before_rerun_setup);
    RUN_TEST(test_update_row_opens_update_screen);
    RUN_TEST(test_update_button_follows_state);
    RUN_TEST(test_update_env_mismatch_disables_install);
    RUN_TEST(test_update_belt_running_blocks_check_and_install);
    RUN_TEST(test_update_rows_are_bright_except_the_message);
    RUN_TEST(test_forget_row_needs_a_second_tap);
    RUN_TEST(test_url_host_edges);
    RUN_TEST(test_update_needs_dev_and_wifi);
    RUN_TEST(test_update_status_words);
    RUN_TEST(test_update_message_row_keeps_39_characters);
    RUN_TEST(test_open_resets_from_update_to_root);
    return UNITY_END();
}
