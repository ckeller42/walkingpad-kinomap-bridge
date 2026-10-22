# Belt Auto Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the walker switch the belt between Manual and Auto from a top-bar badge, with the bridge's speed-command path muted structurally while Auto is observed and the FTMS read path untouched.

**Architecture:** `BridgeState` (lib/policy) gains an observed belt mode, a pending mode request with a frame-counted confirmation window, and one gate that empties every speed outcome while Auto is observed or pending. `src/main.cpp` feeds it the belt's status byte 4 and drains one new `Emit::SetMode`. `src/ui.*` draws the badge and mutes `+`/`-`.

**Tech Stack:** C++17 (PlatformIO), Unity host tests (`pio test -e native`), M5Unified/LovyanGFX for the screen, NimBLE for FTMS.

**Spec:** `docs/superpowers/specs/2026-10-22-belt-auto-mode-design.md`

## Global Constraints

- `lib/` is Arduino-free and host-tested; `src/` holds adapters. Put the decision in `lib/` returning a value, the effect in `src/`.
- Every state-changing `BridgeState` entry point returns a `[[nodiscard]] Outcome`; exactly one `applyOutcome()` in `src/main.cpp` drains it.
- `BeltLink::send()` is the single write choke point and refuses anything but group `0xA2`. `set_mode` is `A2 02 <mode>`; no new write path.
- **STOP always works**, armed or not. **Disarm is not stop** (documented invariant on `setArmed`): disarming never stops a moving belt, in any mode.
- Auto is active only when a status frame **reports** mode 0; sending the request is never enough. The command path is muted while Auto is observed **or pending**.
- Hands-off on external mode changes (the belt's own M/A keys): observe and display, never fight — even while disarmed.
- No remembered Auto preference. STOP (Standby, the belt's only stop) leaves Auto; Auto returns with one badge tap after re-arming.
- `protocol/wilink.yaml` is the source of truth; `docs/protocol/*.md` are generated — never hand-edit them.
- Doxygen ratchet: every new public symbol is documented.
- The uncommitted battery + steps change already in the worktree (`lib/battery/`, `src/ui.*`, `src/main.cpp`, `test/test_battery/`) is left as-is. Do not revert or commit it.
- Commit messages end with: `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>` and `Claude-Session: https://claude.ai/code/session_01BmiMYgcv4b45r7o9z27GZc`.

## Review Focus

1. **Badge tap during the countdown** (belt state 7/9/6/8, speed 0): not stationary, must be refused — a mode write mid-countdown is exactly the "mid-walk flip". Pinned by Task 1 (`observedRunning=true` refused) and Task 4's mapping (anything but idle/standby at speed 0 counts as running).
2. **Belt's A key pressed while a Manual start is in flight**: the bridge must go hands-off and mute, not fight the belt. Pinned by Task 2 `test_external_auto_while_running_mutes_and_keeps_running`.
3. **Kinomap StartResume while Auto is merely pending**: must emit nothing (no `set_speed` can leak during the gap). Pinned by Task 2 `test_phone_target_while_auto_pending_emits_nothing`.
4. **Repeated Auto status frames must notify Kinomap once**, not per frame. Pinned by Task 2 `test_observed_running_in_auto_notifies_started_once`.
5. **Belt link drops with an Auto request pending**: nothing may be re-sent on reconnect. Pinned by Task 1 `test_belt_link_loss_clears_pending_and_reconnect_resends_nothing`.

---

### Task 1: `BridgeState` learns the belt mode

**Files:**
- Modify: `lib/policy/bridge_state.h` (Emit enum ~line 35, `Outcome` ~line 85, class ~line 110, private fields ~line 282)
- Modify: `lib/policy/bridge_state.cpp`
- Test: `test/test_policy/test_bridge_state.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces (all in `namespace policy`):
  - `enum class BeltMode : uint8_t { Unknown, Auto, Manual, Standby };` — `Unknown` is a sentinel, not a wire value.
  - `Emit::SetMode` — "write `set_mode Outcome::mode`".
  - `Outcome::mode` (`BeltMode`), valid for `Emit::SetMode`. `kMaxEmits` stays `3`.
  - `constexpr uint8_t kModeConfirmFrames = 5;` — status frames an Auto request may wait for confirmation.
  - `Outcome BridgeState::requestMode(BeltMode m, bool observedRunning);`
  - `Outcome BridgeState::observeMode(BeltMode m);` — call once per status frame.
  - `BeltMode BridgeState::observedMode() const;`
  - `bool BridgeState::autoActive() const;` — `observedMode() == BeltMode::Auto`.
  - `bool BridgeState::autoPending() const;` — a `SetMode(Auto)` was emitted and is unconfirmed.

- [ ] **Step 1: Write the failing tests** (append to `test/test_policy/test_bridge_state.cpp`, register each in `main()`)

```cpp
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
void test_observe_mode_other_mode_while_pending_is_a_rejection(void) {
    policy::BridgeState s;
    (void)s.onBeltLink(true); (void)s.setArmed(true);
    (void)s.requestMode(policy::BeltMode::Auto, false);
    (void)s.observeMode(policy::BeltMode::Manual);
    TEST_ASSERT_FALSE(s.autoPending());
    TEST_ASSERT_FALSE(s.autoActive());
    TEST_ASSERT_EQUAL(policy::BeltMode::Manual, s.observedMode());
}
void test_observe_mode_times_out_after_confirm_frames(void) {
    policy::BridgeState s;
    (void)s.onBeltLink(true); (void)s.setArmed(true);
    (void)s.requestMode(policy::BeltMode::Auto, false);
    for (uint8_t i = 0; i < policy::kModeConfirmFrames; ++i)
        (void)s.observeMode(policy::BeltMode::Standby);  // unchanged mode, no confirmation
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
```

Also extend `test_no_outcome_exceeds_the_emit_array` with
`check(s.requestMode(policy::BeltMode::Auto, false));` and
`check(s.observeMode(policy::BeltMode::Auto)); check(s.observeMode(policy::BeltMode::Manual));`.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `~/tools/venv-ble/bin/pio test -e native -f test_policy`
Expected: compile error — `BeltMode`, `requestMode`, `observeMode` not declared.

- [ ] **Step 3: Implement in `lib/policy/bridge_state.h` / `.cpp`**

Add `BeltMode`, `Emit::SetMode`, `Outcome::mode` (set to `BeltMode::Unknown` by `empty()`), `kModeConfirmFrames`, the three accessors, and the two entry points. Private fields: `BeltMode observedMode_ = BeltMode::Unknown; BeltMode pendingMode_ = BeltMode::Unknown; uint8_t pendingFrames_ = 0;`.

`requestMode`: refuse (empty) unless `armed_ && beltUp_ && !observedRunning`; else set `pendingMode_ = m`, `pendingFrames_ = 0`, emit `SetMode` with `o.mode = m`.

`observeMode(m)`: the transition table from the spec — confirm when `m == pendingMode_`; a different mode while pending clears pending (rejection); otherwise an external change just updates. While pending and unconfirmed, `++pendingFrames_` and clear pending once it reaches `kModeConfirmFrames`. Whenever `observedMode_` goes from `Auto` to anything else, emit `ResetPolicy`. Update `observedMode_ = m` last.

`onBeltLink(false)`: additionally `observedMode_ = BeltMode::Unknown; pendingMode_ = BeltMode::Unknown;`.

Doxygen every new symbol in the existing voice (the class note gains one sentence: it now also knows the belt's reported mode).

- [ ] **Step 4: Run the tests to verify they pass**

Run: `~/tools/venv-ble/bin/pio test -e native`
Expected: all pass (168 + 12 new).

- [ ] **Step 5: Commit**

```bash
git add lib/policy/bridge_state.h lib/policy/bridge_state.cpp test/test_policy/test_bridge_state.cpp
git commit -m "feat(policy): observed belt mode with a frame-counted Auto request"
```

---

### Task 2: Mute the command path in Auto; observed running

**Files:**
- Modify: `lib/policy/bridge_state.h`, `lib/policy/bridge_state.cpp`
- Test: `test/test_policy/test_bridge_state.cpp`

**Interfaces:**
- Consumes: Task 1's `BeltMode`, `autoActive()`, `autoPending()`.
- Produces: `Outcome BridgeState::onBeltObservedRunning();` — call when a poll shows the belt moving (state 1 or speed > 0). Emits `NotifyStarted` and sets the running flag **only** while `autoActive()` and not already running; otherwise emits nothing.
- Changes: `apply()` (SetSpeed branch), `setSpeedFromUi()`, `stepSpeedFromUi()`, `startFromUi()` return an empty `Outcome` while `autoActive() || autoPending()`. `apply()`'s Stop branch and `stopFromUi()` are unchanged except both also clear `pendingMode_`.

- [ ] **Step 1: Write the failing tests**

```cpp
static policy::BridgeState autoBelt() {  // armed, both links up, belt confirmed in Auto
    policy::BridgeState s;
    (void)s.onBeltLink(true); (void)s.onPhoneLink(true); (void)s.setArmed(true);
    (void)s.observeMode(policy::BeltMode::Auto);
    return s;
}
void test_apply_set_speed_in_auto_emits_nothing(void) {
    auto s = autoBelt();
    auto o = s.apply({policy::Action::SetSpeed, 40});
    TEST_ASSERT_EQUAL_size_t(0, o.count);
    TEST_ASSERT_FALSE(s.beltRunning());
}
void test_ui_speed_paths_in_auto_emit_nothing(void) {
    auto s = autoBelt();
    TEST_ASSERT_EQUAL_size_t(0, s.setSpeedFromUi(25).count);
    TEST_ASSERT_EQUAL_size_t(0, s.stepSpeedFromUi(1, 20).count);
    TEST_ASSERT_EQUAL_size_t(0, s.startFromUi(20).count);
}
void test_phone_target_while_auto_pending_emits_nothing(void) {
    policy::BridgeState s;
    (void)s.onBeltLink(true); (void)s.onPhoneLink(true); (void)s.setArmed(true);
    (void)s.requestMode(policy::BeltMode::Auto, false);  // pending, not confirmed
    auto o = s.apply({policy::Action::SetSpeed, 40});
    TEST_ASSERT_EQUAL_size_t(0, o.count);
}
void test_stop_in_auto_still_stops_and_clears_pending(void) {
    auto s = autoBelt();
    (void)s.requestMode(policy::BeltMode::Manual, false);
    auto o = s.stopFromUi();
    TEST_ASSERT_EQUAL(policy::Emit::Stop, o.emits[0]);
    TEST_ASSERT_FALSE(s.autoPending());
    TEST_ASSERT_FALSE(s.armed());
}
void test_phone_stop_in_auto_still_stops(void) {
    auto s = autoBelt();
    auto o = s.apply({policy::Action::Stop, 0});
    TEST_ASSERT_EQUAL_size_t(1, o.count);
    TEST_ASSERT_EQUAL(policy::Emit::Stop, o.emits[0]);
}
void test_observed_running_in_auto_notifies_started_once(void) {
    auto s = autoBelt();
    auto o1 = s.onBeltObservedRunning();
    TEST_ASSERT_EQUAL_size_t(1, o1.count);
    TEST_ASSERT_EQUAL(policy::Emit::NotifyStarted, o1.emits[0]);
    TEST_ASSERT_TRUE(s.beltRunning());
    auto o2 = s.onBeltObservedRunning();
    TEST_ASSERT_EQUAL_size_t(0, o2.count);
}
void test_observed_running_outside_auto_emits_nothing(void) {
    policy::BridgeState s;
    (void)s.onBeltLink(true); (void)s.onPhoneLink(true); (void)s.setArmed(true);
    auto o = s.onBeltObservedRunning();
    TEST_ASSERT_EQUAL_size_t(0, o.count);
    TEST_ASSERT_FALSE(s.beltRunning());
}
void test_external_auto_while_running_mutes_and_keeps_running(void) {
    policy::BridgeState s;
    (void)s.onBeltLink(true); (void)s.onPhoneLink(true); (void)s.setArmed(true);
    (void)s.apply({policy::Action::SetSpeed, 40});   // manual start in flight
    auto o = s.observeMode(policy::BeltMode::Auto);   // walker pressed A on the belt
    TEST_ASSERT_EQUAL_size_t(0, o.count);
    TEST_ASSERT_TRUE(s.beltRunning());
    TEST_ASSERT_EQUAL_size_t(0, s.apply({policy::Action::SetSpeed, 50}).count);
}
void test_disarm_in_auto_does_not_stop(void) {
    auto s = autoBelt();
    (void)s.onBeltObservedRunning();
    auto o = s.setArmed(false);
    for (size_t i = 0; i < o.count; ++i) TEST_ASSERT_NOT_EQUAL(policy::Emit::Stop, o.emits[i]);
    TEST_ASSERT_TRUE(s.beltRunning());
}
```

Extend `test_no_outcome_exceeds_the_emit_array` with `check(s.onBeltObservedRunning());`.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `~/tools/venv-ble/bin/pio test -e native -f test_policy`
Expected: compile error on `onBeltObservedRunning`; after a stub, the mute tests fail with `count` 1–3 instead of 0.

- [ ] **Step 3: Implement**

Add a private `bool commandsMuted() const { return autoActive() || autoPending(); }` and return `empty(...)` early from the four speed entry points when it is true (after the existing disarmed/link checks or before — order does not matter, both yield empty). Implement `onBeltObservedRunning()`. Clear `pendingMode_` in `stopFromUi()` and in `apply()`'s Stop branch. Doxygen: `apply`, `setSpeedFromUi`, `stepSpeedFromUi`, `startFromUi` each gain one `@note` that Auto (observed or pending) makes them emit nothing; document `onBeltObservedRunning` fully.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `~/tools/venv-ble/bin/pio test -e native`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add lib/policy/bridge_state.h lib/policy/bridge_state.cpp test/test_policy/test_bridge_state.cpp
git commit -m "feat(policy): mute the speed path in Auto; observed running notifies Kinomap"
```

---

### Task 3: Screen — mode badge, muted `+`/`-`, AUTO tag

**Files:**
- Modify: `src/ui.h` (layout table, `render()` doc + signature, latched-input group)
- Modify: `src/ui.cpp` (constants ~line 10, `poll()` top-strip zone ~line 167, `render()` top bar and `+`/`-` block and bottom bar, `finishFrame` phase code)

**Interfaces:**
- Consumes: nothing from lib (the UI holds no policy).
- Produces:
  - `enum class ModeBadge : uint8_t { None, Manual, Auto, AutoPending };` in `ui.h` (namespace-level, beside the class). `None` draws nothing (mode unknown / no belt).
  - `render()` gains two trailing parameters: `ModeBadge badge, bool autoMode` — `autoMode` is "observed or pending", it mutes `+`/`-`, turns the `ARMED` tag into `AUTO`, and removes the `START` face.
  - `bool Ui::tookModeTap();` — the badge was tapped (once per press).
  - Constants: `BADGE_X0 = 104, BADGE_X1 = 176` (badge hit zone, `y < SETUP_HIT_Y`), badge drawn centred at `x = 140, y = 14`.

- [ ] **Step 1: Declare the interface in `src/ui.h`**

Add `ModeBadge`, the two `render()` parameters with `@param` lines, `tookModeTap()` in the latched-input group with a one-line doc, and update the layout table: top bar now reads "link indicators (BELT left, PHONE right), battery gauge (top-left), mode badge (centre) and the settings gear". Update the `setupTapped_` doc: the badge zone is carved out of the BELT tap strip.

- [ ] **Step 2: Build to verify it fails**

Run: `FW_VERSION=dev ~/tools/venv-ble/bin/pio run -e cores3 2>&1 | tail -5`
Expected: FAIL — `main.cpp` still calls the old `render()` signature.

- [ ] **Step 3: Implement in `src/ui.cpp`**

- `drawModeBadge(lgfx::LovyanGFX& g, int cx, int cy, ModeBadge b, bool armed)`: a rounded rect ~26×20 at the centre with one bold letter — `M` green (`TFT_GREEN`), `A` cyan (`TFT_CYAN`), `A?` amber (`TFT_ORANGE`, use `FreeSans9pt7b` so both glyphs fit); all drawn in `DIM` when `!armed`; nothing for `None`.
- `poll()`: in the `t.y < SETUP_HIT_Y` branch, test `t.x >= BADGE_X0 && t.x < BADGE_X1` **before** the existing `left` test and latch `modeTapped_` instead of `setupTapped_`.
- `render()`: call `drawModeBadge` after `drawGear`; change the `+`/`-` block's `armed ? … : …` to `(armed && !autoMode) ? … : …`; the `ARMED` tag draws `"AUTO"` when `autoMode`; `canStart` becomes `armed && !running && beltUp && !autoMode`; phase code adds `+ (autoMode ? 16 : 0) + (badge == ModeBadge::AutoPending ? 32 : 0)`.
- Update the three existing `render()` call sites in `src/main.cpp` **minimally** to compile: pass `ModeBadge::None, false` (Task 4 wires the real values).

- [ ] **Step 4: Build to verify it passes**

Run: `FW_VERSION=dev ~/tools/venv-ble/bin/pio run -e cores3 2>&1 | tail -5`
Expected: `[SUCCESS]`.

- [ ] **Step 5: Commit**

```bash
git add src/ui.h src/ui.cpp src/main.cpp
git commit -m "feat(ui): belt mode badge, AUTO tag, muted speed buttons"
```

---

### Task 4: Host wiring in `src/main.cpp`

**Files:**
- Modify: `src/main.cpp` — `emitName()`, `applyOutcome()` (~line 60–110), the status handler (~line 769–785), the UI tap block (~line 728–760), the three `render()` calls, logging.

**Interfaces:**
- Consumes: Task 1/2 (`BeltMode`, `Emit::SetMode`, `Outcome::mode`, `requestMode`, `observeMode`, `onBeltObservedRunning`, `autoActive`, `autoPending`, `observedMode`), Task 3 (`ModeBadge`, `render()` params, `tookModeTap()`).
- Produces: two static helpers in `main.cpp`:
  - `static policy::BeltMode toBeltMode(uint8_t wireMode)` — `0→Auto, 1→Manual, 2→Standby, else Unknown`.
  - `static wilink::Mode toWireMode(policy::BeltMode m)` — inverse for `Auto/Manual/Standby` (never called with `Unknown`; `applyOutcome` skips `SetMode` with `Unknown` and logs it).

- [ ] **Step 1: Wire the status frame**

In the `theBelt.poll(st)` block, after `lastStatus = st;`:
```cpp
const auto prevMode = state.observedMode();
applyOutcome(state.observeMode(toBeltMode(st.mode)));
if (state.observedMode() != prevMode)
    logsink::line("belt mode %u -> %u pending=%d active=%d", (unsigned)prevMode,
                  (unsigned)state.observedMode(), (int)state.autoPending(), (int)state.autoActive());
if (state.autoActive() && !state.beltRunning() && (st.beltState == 1 || st.tenthKmh > 0))
    applyOutcome(state.onBeltObservedRunning());
```
The existing `onBeltObservedStopped()` call stays as it is.

- [ ] **Step 2: Drain `Emit::SetMode`**

Add `case policy::Emit::SetMode:` to `applyOutcome()`: if `o.mode == policy::BeltMode::Unknown` log `"emit SetMode skipped: unknown"` and break; else `theBelt.setMode(toWireMode(o.mode));`. Add `"SetMode"` to `emitName()`.

- [ ] **Step 3: Wire the badge tap and the button faces**

In the UI tap block, before the `tookArmButton()` handling:
```cpp
if (ui.tookModeTap()) {
    const bool fresh = beltFresh();
    const bool stationary = fresh && lastStatus.tenthKmh == 0 &&
                            (lastStatus.beltState == 0 || lastStatus.beltState == 4);
    const auto want = (state.autoActive() || state.autoPending())
                          ? policy::BeltMode::Manual : policy::BeltMode::Auto;
    applyOutcome(state.requestMode(want, !stationary));
    redraw = true;
}
```
`canStart` in the ARM-button handler gains `&& !state.autoActive() && !state.autoPending()`.

- [ ] **Step 4: Feed `render()`**

Compute once per frame:
```cpp
const bool autoMode = state.autoActive() || state.autoPending();
const auto badge = state.autoPending() ? ModeBadge::AutoPending
                 : state.autoActive()  ? ModeBadge::Auto
                 : state.observedMode() == policy::BeltMode::Unknown ? ModeBadge::None
                 : ModeBadge::Manual;
```
and pass `badge, autoMode` to the loop's `render()`; the two boot-time calls pass `ModeBadge::None, false`.

- [ ] **Step 5: Build and run the host suite**

Run: `FW_VERSION=dev ~/tools/venv-ble/bin/pio run -e cores3 2>&1 | tail -5 && ~/tools/venv-ble/bin/pio test -e native 2>&1 | tail -4`
Expected: `[SUCCESS]` and all host tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/main.cpp
git commit -m "feat: wire belt mode badge, SetMode emit and observed running into main"
```

---

### Task 5: Screenshot tour, protocol notes, docs refresh

**Files:**
- Modify: `src/main.cpp` (`demoAdvance()` ~line 275), `tools/capture_shots.py` / `tools/decode_shots.py` (only if they enumerate phases — check with `grep -n "18\|phase" tools/capture_shots.py tools/decode_shots.py`)
- Modify: `protocol/wilink.yaml` (`set_mode` entry ~line 130)
- Modify: `AGENTS.md` (test counts), `docs/superpowers/specs/2026-10-22-belt-auto-mode-design.md` (one sentence)
- Generated: `docs/protocol/*.md` via the tools — never by hand.

**Interfaces:**
- Consumes: Task 1/4 entry points.
- Produces: nothing new in code.

- [ ] **Step 1: Append tour phases**

After the last `case` in `demoAdvance()`, append three phases that return to the main screen and show the three badge states:
```cpp
case 19: inDevMenu = false; ui.setMenuMode(false);   // the file's close-menu idiom (see ~line 324)
         applyOutcome(state.setArmed(true));
         applyOutcome(state.requestMode(policy::BeltMode::Auto, false)); break;   // A?
case 20: applyOutcome(state.observeMode(policy::BeltMode::Auto)); break;          // A
case 21: applyOutcome(state.observeMode(policy::BeltMode::Manual)); break;        // M
```
Extend any phase table or count in `tools/capture_shots.py` / `tools/decode_shots.py` to 22 phases with names `auto-pending`, `auto`, `manual`.

- [ ] **Step 2: Build the screenshot env**

Run: `FW_VERSION=dev ~/tools/venv-ble/bin/pio run -e cores3_shot 2>&1 | tail -3`
Expected: `[SUCCESS]`.

- [ ] **Step 3: Protocol notes**

In `protocol/wilink.yaml`, `set_mode.notes` becomes:
`Mode 0 (auto) is produced by the bridge only on an explicit user request, and the bridge treats Auto as active only once a status frame reports mode 0.`
Leave the `mode` enum's provenance for value 0 as `community`.

Run: `python3 tools/gen_protocol_reference.py && python3 tools/gen_frame_layouts.py && python3 tools/check_protocol_views.py`
Expected: regenerated `docs/protocol/*.md`; the checker exits 0.

- [ ] **Step 4: Docs refresh**

- `AGENTS.md`: replace both stale host-test counts (`145`, `153`) with the number `pio test -e native` reports after Task 2, and add one line under *Global constraints*: "Auto mode is active only when a status frame reports mode 0; the speed path is muted while Auto is observed or pending."
- Spec: change "Disarm (`setArmed(false)`) behaves as today — it stops a moving belt — which in Auto parks it in Standby and so leaves Auto." to "Disarm (`setArmed(false)`) behaves as today — it never stops the belt (documented invariant) — so in Auto it only greys the badge; STOP is the stop."

- [ ] **Step 5: Full verification**

Run:
```bash
~/tools/venv-ble/bin/pio test -e native 2>&1 | tail -4
doxygen Doxyfile >/dev/null 2>&1; python3 tools/doxygen_ratchet.py build/doxygen/warnings.txt .doxygen-baseline
python3 tools/check_protocol_views.py
```
Expected: all tests pass; ratchet reports no new warnings; checker exits 0.

- [ ] **Step 6: Commit**

```bash
git add src/main.cpp tools/ protocol/wilink.yaml docs/protocol AGENTS.md docs/superpowers/specs/2026-10-22-belt-auto-mode-design.md
git commit -m "docs: Auto mode tour phases, set_mode notes, test counts"
```
