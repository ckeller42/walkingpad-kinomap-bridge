# UI / UX Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the three safety findings and the quick wins from the 2026-10-22 UI/UX review: no accidental disarm during a start, STOP the easiest target while moving, no screen without a safe path, visible reasons for refused taps, a usable password keyboard, and a Wi-Fi menu that shows what it is doing.

**Architecture:** Decisions stay in `lib/` (a `startAllowed()` predicate on `BridgeState`, a keyboard layout table in `lib/devmenu` with a reachability test, new dev-menu rows/inputs/actions) and effects stay in `src/` (`ui.*` draws, `main.cpp` wires). One `render()` signature change carries a reason line and a "starting" flag; the bottom-bar split becomes a member both `render()` and `poll()` read.

**Tech Stack:** C++17 / PlatformIO, Unity host tests (`~/tools/venv-ble/bin/pio test -e native`, 197 today), M5Unified/LovyanGFX, Arduino-ESP32 WiFi.

**Spec:** `docs/superpowers/reviews/2026-10-22-ui-ux-review.md` (findings #1-#3, #4-#12, #14 and the quick wins; #13 and RSSI bars deferred). Design decisions not in the review are recorded in this plan's Global Constraints and the tasks; rulings made against them are provisional.

## Global Constraints

- `lib/` is Arduino-free and host-tested; `src/` holds adapters. Every new decision is a `lib/` function with a test; `src/ui.*` holds no policy.
- **STOP always works**, armed or not. **Disarm never stops the belt** (documented invariant; unchanged). Nothing moves the belt unless armed; the Auto-mode rules from `docs/superpowers/specs/2026-10-22-belt-auto-mode-design.md` stay as they are.
- `BeltLink::send()` stays the only write path (group `0xA2` only). No task adds a belt write.
- Fonts: `FreeSansBold12pt7b` for primary labels, `FreeSans9pt7b` for secondary; existing colours: `ARM_GO` (bright green, call to action), `ARM_ON` (dark green, armed), `ARM_OFF`/`DIM` (inert), `TFT_RED` STOP, `TFT_ORANGE` amber = warning/SIM, `TFT_CYAN` Auto.
- A simulated belt never looks like a real one: the amber `SIM` label stays on every screen.
- Touch zones and drawing must read the same constants/members; never two formulas for one zone.
- Doxygen for every new public symbol (warning ratchet: `doxygen Doxyfile; tools/doxygen_ratchet.py build/doxygen/warnings.txt .doxygen-baseline`).
- Board builds `FW_VERSION=dev ~/tools/venv-ble/bin/pio run -e cores3` and `-e cores3_shot` must end `[SUCCESS]`; run shell steps as plain separate commands (the sandbox refuses compound shell with runtime values).
- Commit footer (two lines at the end of every commit body):
  `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`
  `Claude-Session: https://claude.ai/code/session_01BmiMYgcv4b45r7o9z27GZc`

## Review Focus

1. **START then an impatient second tap within the 3 s countdown**: the second tap must *not* disarm; the walker sees "Starting…" and the belt still starts. Pinned by Task 1 (`startFromUi` makes `startPending`; main refuses the DISARM face while `startPending`) and the host test `test_start_allowed_false_while_running_or_auto`.
2. **Disarm by another path while a start is pending** (console `arm off`, FTMS): `driveStart()` must drop the pending speed. Pinned by Task 1's `driveStart` guard (src, build-verified) and the reason log.
3. **Bottom-bar split while moving**: STOP takes two thirds; a tap at x = 120 while running must be STOP, not DISARM, in both `poll()` and `render()`. Pinned by Task 2's single `barSplit_` member.
4. **Every printable ASCII character can be typed**, and a tap in the 183..190 px dead band under the keys does nothing. Pinned by Task 3's `test_keyboard_reaches_every_printable_ascii` and the `poll()` dead band.
5. **Gear/menu entry while Auto is observed or the belt is not parked** must be refused with a reason, and opening the menu disarms. Pinned by Task 4 (src, build-verified) and the reason log.

---

### Task 1: `startAllowed()` and the start lockout

**Files:**
- Modify: `lib/policy/bridge_state.h`, `lib/policy/bridge_state.cpp`
- Modify: `src/main.cpp` (ARM-button handler ~line 824, `driveStart()` ~line 176, the `render()` call and the two boot calls)
- Modify: `src/ui.h`, `src/ui.cpp` (`render()` gains `bool starting, const char* reason`; this task adds the parameters and the "STARTING" face only — Task 2 draws the reason line)
- Test: `test/test_policy/test_bridge_state.cpp` (+ registration in `test/test_policy/cases.h` and `test_speed_policy.cpp`'s `main()`)

**Interfaces:**
- Produces: `bool BridgeState::startAllowed(bool beltFresh) const;` — true iff `armed_ && beltUp_ && beltFresh && !running_ && !autoActive() && !autoPending()`. This is the one definition of "START is offered".
- Produces: `render(..., ModeBadge badge, bool autoMode, bool starting, const char* reason)` — `starting` = a start sequence is in flight (`startPending`); `reason` = a short string to show for ~1 s, or `nullptr`.
- Produces in `main.cpp`: `static const char* reasonText; static uint32_t reasonUntilMs; static void showReason(const char* s)` (sets both, 1200 ms), passed to `render()` while unexpired.

- [ ] **Step 1: Write the failing tests**

```cpp
void test_start_allowed_requires_armed_link_fresh_and_stopped(void) {
    policy::BridgeState s;
    TEST_ASSERT_FALSE(s.startAllowed(true));            // disarmed
    (void)s.setArmed(true);
    TEST_ASSERT_FALSE(s.startAllowed(true));            // belt link down
    (void)s.onBeltLink(true);
    TEST_ASSERT_TRUE(s.startAllowed(true));
    TEST_ASSERT_FALSE(s.startAllowed(false));           // stale status
}
void test_start_allowed_false_while_running_or_auto(void) {
    policy::BridgeState s;
    (void)s.onBeltLink(true); (void)s.setArmed(true);
    (void)s.startFromUi(20);                            // optimistic running
    TEST_ASSERT_FALSE(s.startAllowed(true));
    (void)s.onBeltObservedStopped();
    TEST_ASSERT_TRUE(s.startAllowed(true));
    (void)s.observeMode(policy::BeltMode::Auto);        // Auto: stepping on starts it
    TEST_ASSERT_FALSE(s.startAllowed(true));
}
```

- [ ] **Step 2: Run to verify they fail**

Run: `~/tools/venv-ble/bin/pio test -e native -f test_policy`
Expected: compile error, `startAllowed` undeclared.

- [ ] **Step 3: Implement `startAllowed()`** in `bridge_state.{h,cpp}` with doxygen ("the one definition of whether START is offered; `main.cpp` and the screen both derive the button face from it").

- [ ] **Step 4: Wire `main.cpp`**

- ARM-button handler: replace the local `canStart` expression with `state.startAllowed(beltFresh())`. New branch order: `if (!state.armed()) setArmed(true); else if (startPending) showReason("Starting - wait"); else if (canStart) startFromUi(...); else if (!state.beltUp()/* use theBelt.linkUp() && beltFresh() */) { showReason("Belt offline"); applyOutcome(state.setArmed(false)); } else { applyOutcome(state.setArmed(false)); }` — i.e. the DISARM face is refused only while a start is in flight; every other case keeps today's behaviour, with a reason where the face could confuse.
- `driveStart()`: after the Auto guard add `if (!state.armed()) { startPending = false; logsink::line("start dropped: disarmed"); return; }`.
- `showReason()` + the two statics; pass `startPending` and the live reason to the loop's `render()`; boot calls pass `false, nullptr`.

- [ ] **Step 5: `render()` signature + the STARTING face** (`src/ui.h` doc + `src/ui.cpp`): while `starting`, the left button reads `STARTING` in `ARM_ON` (inert look); `canStart` in `render()` becomes `armed && !running && !starting && beltUp && !autoMode`. Store `reason` for Task 2 (draw nothing yet, or draw it — Task 2 owns placement).

- [ ] **Step 6: Verify**

Run: `~/tools/venv-ble/bin/pio test -e native` → all pass (197 + 2). Then `FW_VERSION=dev ~/tools/venv-ble/bin/pio run -e cores3 2>&1 | tail -3` → `[SUCCESS]`.

- [ ] **Step 7: Commit**

`git commit -m "fix(ui): startAllowed() in policy; refuse disarm while a start is in flight"`

---

### Task 2: Main-screen layout — STOP wide while moving, reason line, armed stripe, battery move, gear zone

**Files:**
- Modify: `src/ui.h` (layout table), `src/ui.cpp` (constants ~line 10-25, `poll()` bottom bar ~line 210, `render()` top bar / readout / bottom bar, `finishFrame` bits)
- Modify: `src/main.cpp` (reason strings for refused badge / `+` / `-` / gear taps)

**Interfaces:**
- Consumes: Task 1's `render(..., bool starting, const char* reason)` and `showReason()`.
- Produces: `int Ui::barSplit_` (private; set in `render()`, read in `poll()`): `W / 3` while `running || starting`, else `HALF`. `GEAR_W` becomes `44`. The BELT strip no longer latches `setupTapped_` (keep `tookSetupTap()` returning false until Task 4 removes its caller; then delete it).

- [ ] **Step 1: Bottom bar** — `barButton()` takes an `x0, width` instead of `x, HALF`; `render()` draws the left button over `[0, barSplit_)` and STOP over `[barSplit_, W)`; `barDivider()` draws at `barSplit_`. `poll()`: `const bool leftBar = t.x < barSplit_;` for the `t.y >= HIT_Y` branch on the main screen (wizard/menu keep `left`).
- [ ] **Step 2: Reason line** — when `reason` is non-null, draw it `bottom_left` at `(SPD_BTN_W + 6, 150)` in `TFT_ORANGE`, `FreeSansBold12pt7b`, instead of the "-> commanded" text. `main.cpp`: `showReason("ARM first")` on a `+`/`-` tap while disarmed; `showReason("AUTO: belt sets speed")` while `autoMode`; badge refusal maps `armed=0 → "ARM first"`, not stationary → `"STOP first"`, start in flight → `"Starting - wait"`; gear tap refused (Task 4 adds the gate) → the reason it returns.
- [ ] **Step 3: Armed stripe + tag** — a 3 px full-width stripe at `y = 25` in `ARM_GO` when `armed` (cyan `TFT_CYAN` when `autoMode`); the `ARMED`/`AUTO` tag in `FreeSansBold12pt7b`, `top_right` at `(W - 4, 30)`.
- [ ] **Step 4: Battery gauge** moves to `(4, 31)` (clear of the BELT dot at x 31-49 / y 5-23 and of the `-` button at y 44+).
- [ ] **Step 5: Gear zone** — `GEAR_W = 44`; the top-strip branch becomes gear → badge → (nothing). Update the `ui.h` layout table and doc comments ("tap BELT to re-run setup" is gone; setup lives in the gear menu after Task 4).
- [ ] **Step 6: Phase bits** — add `+ (starting ? 64 : 0)` to `finishFrame`'s code.
- [ ] **Step 7: Verify** — `cores3` and `cores3_shot` build `[SUCCESS]`; native suite unchanged.
- [ ] **Step 8: Commit** — `git commit -m "feat(ui): wide STOP while moving, reason line, armed stripe, battery and gear zones"`

---

### Task 3: Keyboard — layout table in lib, second symbols page, every printable ASCII reachable, dead band

**Files:**
- Create: `lib/devmenu/keyboard_layout.h`, `lib/devmenu/keyboard_layout.cpp`
- Modify: `lib/devmenu/devmenu.h`, `lib/devmenu/devmenu.cpp` (page model replaces the `numeric_` bool)
- Modify: `src/ui.cpp` (tables removed; `pollKeyboard()` and the key drawing read the lib layout; dead band)
- Test: `test/test_devmenu/test_devmenu.cpp` (+ a new `test/test_devmenu/test_keyboard_layout.cpp` is NOT needed — add to the same file)

**Interfaces:**
- Produces (`namespace devmenu`):
  - `enum class Page : uint8_t { Letters, Symbols1, Symbols2 };`
  - `struct Key { char ch; const char* label; enum class Kind : uint8_t { Char, Blank, Del, Shift, Space, ToLetters, ToSymbols1, ToSymbols2 } kind; };`
  - `Key keyAt(Page page, bool shift, uint8_t row, uint8_t col);` — the single source for both drawing and hit-testing. Rows: 3, cols: 10. `label` is what to draw (`"DEL"`, `"SP"`, `"shf"`/`"SHF"`, `"123"`, `"#+="`, `"ABC"`; for `Char` the one-character string).
  - Layout: **Letters** row0 `qwertyuiop`, row1 `asdfghjkl` + DEL (col 9), row2 col0 shift, cols 1-7 `zxcvbnm`, col 8 `123` (ToSymbols1), col 9 SP. **Symbols1** row0 `1234567890`, row1 `!@#$%^&*(` + DEL, row2 col0 `#+=` (ToSymbols2), cols 1-7 `-_=+:/?`, col 8 `ABC`, col 9 SP. **Symbols2** row0 `` ~`|\<>[]{} ``, row1 `.,;'")` then three Blank then DEL, row2 col0 `123` (ToSymbols1), cols 1-7 Blank, col 8 `ABC`, col 9 SP.
  - `DevMenu`: `Page page() const;` replaces `numeric()`; `void setPage(Page)`; `toggleNumeric()` is removed (host calls `setPage` with the page a `ToX` key names). `Screen::numeric` becomes `Page page`.
- Consumes: nothing from other tasks.

- [ ] **Step 1: Write the failing tests**

```cpp
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
```
Update the existing devmenu tests that use `numeric()`/`toggleNumeric()` to `page()`/`setPage()`; `test_shift_applies_to_one_character` keeps its meaning.

- [ ] **Step 2: Run to verify they fail** — `~/tools/venv-ble/bin/pio test -e native -f test_devmenu` → compile error on `keyAt`/`Page`.
- [ ] **Step 3: Implement** `keyboard_layout.{h,cpp}` and the `DevMenu` page model (doxygen). `append()` unchanged; `setPage()` clears `shift_`.
- [ ] **Step 4: `src/ui.cpp`** — delete `KB_LOWER/KB_UPPER/KB_NUM`; `pollKeyboard()` and the keyboard drawing both call `devmenu::keyAt(s.page, s.shift, row, col)`: `Char` → `keyChar_ = ch` / draw the label; `Blank` → ignore / draw an empty key; `Del`, `Shift`, `Space`, `ToX` → the existing flags, where `ToX` sets a new latched `keyPage_` (`int tookKeyPage()` returns the page+1 or 0) and `main.cpp`'s `devMenuTick()` calls `devMenu.setPage(...)`. Dead band: in menu mode the bottom bar accepts taps only at `t.y >= 190` while `menuKeyboard_` (keys end at 183); `HIT_Y` stays 180 for the rows screens.
- [ ] **Step 5: Verify** — native suite passes; `cores3` and `cores3_shot` build.
- [ ] **Step 6: Commit** — `git commit -m "feat(keyboard): layout table in lib with a second symbols page; every printable ASCII reachable"`

---

### Task 4: Developer menu — re-run setup row, live Wi-Fi state, 1 Hz redraw, entry gate, scan list, password clip, docs

**Files:**
- Modify: `lib/devmenu/devmenu.h`, `lib/devmenu/devmenu.cpp`; Test: `test/test_devmenu/test_devmenu.cpp`
- Modify: `src/logsink.h`, `src/logsink.cpp` (`wifiConnecting()`, `wifiFailed()`)
- Modify: `src/main.cpp` (`devInputs()`, `devMenuAct()`, `devMenuTick()`, gear-tap gate, remove the BELT setup tap), `src/ui.cpp` (keyboard header: clip the typed text from the left), `src/ui.h` (delete `tookSetupTap()` / `setupTapped_`)
- Modify docs: `lib/devmenu/devmenu.h` header comment (×2 "tap the PHONE indicator" → "the gear in the top-right corner"), `docs/developer-menu.md:7`, `docs/setup-wizard.md:76` (describe the faces ARM / START / STARTING / DISARM and that re-run setup lives in the gear menu)

**Interfaces:**
- Produces (`namespace devmenu`): `Action::RerunSetup`; Root row 4 `"Re-run setup"` (always enabled; `rowCount = 5`); `Inputs::wifiConnecting`, `Inputs::wifiFailed`; `kMaxNetworks = kMaxRows` (5). Root Wi-Fi row value precedence: `wifiUp → ip`, `wifiConnecting → "connecting..."`, `wifiFailed → "wrong password?"`, stored → `"offline"`, else `"not set"`.
- Produces (`logsink`): `bool wifiConnecting()` — a `setWifi()` was issued less than 15 s ago and `WiFi.status() != WL_CONNECTED`; `bool wifiFailed()` — `WiFi.status() == WL_CONNECT_FAILED`, or 15 s passed since `setWifi()` without `WL_CONNECTED` (cleared by the next `setWifi()`).
- Consumes: Task 1's `showReason()`, Task 2's gear zone.

- [ ] **Step 1: Write the failing tests**

```cpp
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
```
Rename/adjust `test_root_offers_four_live_rows_and_ignores_taps_past_them` to five rows.

- [ ] **Step 2: Run to verify they fail** — `-f test_devmenu` → compile errors (`RerunSetup`, `wifiConnecting`).
- [ ] **Step 3: Implement the lib** (devmenu.h/.cpp with doxygen) and **logsink** (`wifiConnecting()/wifiFailed()` using a `static uint32_t wifiBeginMs` set in `setWifi()`).
- [ ] **Step 4: `main.cpp`** — `devInputs()` fills the two flags; `devMenuAct(RerunSetup)` → `leaveDevMenu(); enterWizard(now)`; `devMenuTick()` sets `devDirty = true` at least once per second while the menu is open (static `lastDevRedrawMs`); gear tap: `if (ui.tookDevTap()) { if (!wilink::parked(lastStatus) || !beltFresh()) showReason("STOP first"); else if (state.autoActive() || state.autoPending()) showReason("Leave AUTO first"); else { applyOutcome(state.setArmed(false)); devMenu.open(); ... } }`; delete the `tookSetupTap()` call.
- [ ] **Step 5: `ui.*`** — remove `setupTapped_`/`tookSetupTap()`; keyboard header draws the typed password right-aligned but clipped from the left so it never crosses `x = 160` (drop leading characters until `textWidth <= W/2 - 8`).
- [ ] **Step 6: Docs** — the four text fixes listed under Files.
- [ ] **Step 7: Verify** — native suite passes; `cores3` + `cores3_shot` build; `doxygen` ratchet clean.
- [ ] **Step 8: Commit** — `git commit -m "feat(devmenu): re-run setup row, live Wi-Fi state, entry gate, docs"`

---

### Task 5: Wizard copy, counts, final verification

**Files:**
- Modify: `lib/wizard/wizard.cpp` (last step, ~line 165-171), `test/test_wizard/test_wizard.cpp` (`test_last_step_states_kinomap_waits_for_movement`)
- Modify: `README.md` (three host-test counts), `AGENTS.md` (two counts)

- [ ] **Step 1: Update the test** to assert the new copy: `detail[0]` contains `"ARM and START"`, `detail[1]` contains `"moving"`, `detail[2]` contains `"15 s"`, `detail[3]` contains `"gear"`; drop the `"never starts"` assertion.
- [ ] **Step 2: Run to verify it fails** — `-f test_wizard`.
- [ ] **Step 3: Implement the copy**: `detail[0] "ARM and START first: pairing"`, `detail[1] "needs it moving. Stand on it:"`, `detail[2] "empty, it stops after 15 s."`, `detail[3] "Re-run setup: gear menu."`.
- [ ] **Step 4: Counts** — run the full native suite, write its total into the three README lines and the two AGENTS.md lines.
- [ ] **Step 5: Full verification** — native suite; `cores3` + `cores3_shot` builds; doxygen ratchet; `python3 tools/check_protocol_views.py` (unchanged but must still pass).
- [ ] **Step 6: Commit** — `git commit -m "docs: wizard last step mentions the 15 s auto-stop; test counts"`
