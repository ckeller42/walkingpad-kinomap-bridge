# WalkingPad → Kinomap Bridge Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** An M5Stack CoreS3 that presents a WalkingPad R1 Pro to Kinomap as a standard FTMS treadmill, translating Kinomap's speed targets into KingSmith WiLink commands and the belt's status frames back into FTMS notifications.

**Architecture:** All protocol and policy logic lives in Arduino-free libraries under `lib/`, unit-tested natively on the host with Unity. Hardware adapters (NimBLE central to the belt, NimBLE peripheral to the phone, M5GFX screen) are thin shells over those libraries, verified on-device against the spec's test ladder. A `SimBelt` implementing the same interface as the real belt link is what makes the spec's stage-1 test — real Kinomap, real CoreS3, nothing that can move — possible.

**Tech Stack:** PlatformIO (pioarduino platform fork), Arduino-ESP32 3.x, NimBLE-Arduino, M5Unified/M5GFX, Unity for native tests.

**Spec:** `docs/superpowers/specs/2026-10-21-walkingpad-kinomap-bridge-design.md`

## Global Constraints

- Advertised FTMS manufacturer string (`2A29`) must **never** be `decathlon` — it selects a different code path in Kinomap.
- `2ACC` Fitness Machine Feature is **exactly 8 bytes**. Bytes 0–3 (Fitness Machine Features, uint32 LE) = `0x00000000` — no optional measurement features, consistent with a `2ACD` payload carrying speed only. Bytes 4–7 (Target Setting Features, uint32 LE) = `0x00000001` — **bit 0 set** (Speed Target Setting), **bit 1 clear** (Inclination Target Setting), **bit 2 clear** (Resistance Target Setting). Note the bit numbering: inclination is bit 1 and resistance is bit 2, which is what Kinomap's own reader assumes (`uf8.java:155-157`).
- `2AD5` Supported Inclination Range is **not exposed**.
- `2AD4` Supported Speed Range is **6 bytes**: min `0.50`, max `6.00`, increment `0.10`, each uint16 LE in 0.01 km/h units → `32 00 58 02 0A 00`.
- `2ACD` Treadmill Data payload is **flags `0x0000` + instantaneous speed uint16 LE in 0.01 km/h units**, 4 bytes total. No optional fields.
- `2AD9` responses are always `80 <request-opcode> 01` for success. The bridge never returns an FTMS error code.
- A Set Target Speed of **exactly 1.00 km/h (`0x0064`)** is a stop request, not a speed.
- Deadband **0.3 km/h**; minimum **1000 ms** between belt commands.
- WiLink frames: `F7 <cmd> <payload…> <crc> FD`, `crc` = low byte of the sum of all bytes between the header and the CRC.
- Belt speed unit on the wire is **0.1 km/h**.
- **NEVER emit a frame whose command byte is `0xE8`.** The `z1-walkingpad` project
  documents `0xE8` as the OTA command group and warns it can brick the pad. The
  bridge only ever sends `0xA2` (belt control); no code path may construct any
  other command group. The belt's OTA service is a separate GATT service
  (`00010203-0405-0607-0809-0a0b0c0d1912`, Telink OAD) which the bridge never
  connects to — but `0xE8` would reach the same functionality through `FE02`.
- All code under `lib/` must compile without Arduino — it is built by the `native` test environment.

## Review Focus

Five conditions the spec implies but which no task's happy path exercises. Each has a test, added to the task that owns the code.

1. **A truncated control-point write** (e.g. a bare `02` with no payload). Must be ignored without reading past the buffer — Task 5.
2. **A malformed or truncated status frame from the belt.** Must be discarded, not decoded into a bogus speed that then drives the belt — Task 3. (CRC validation of *status* frames is deferred to Task 13, because the spec does not document where the CRC sits in an `F8 A2` frame; the live capture settles it.)
3. **A Set Target Speed above the advertised 6.0 km/h maximum.** Kinomap clamps app-side, but nothing guarantees a client does. Must be clamped bridge-side — Task 6.
4. **A stop sentinel arriving while the belt is already stopped.** Must be idempotent and must not emit a belt command (which would beep) — Task 6.
5. **The belt link dropping while the phone link is up.** `2ACD` notifications must cease rather than repeating the last known speed forever — Task 7.

---

### Task 1: Project skeleton and native test harness

**Files:**
- Create: `platformio.ini`
- Create: `lib/wilink/wilink.h`, `lib/wilink/wilink.cpp`
- Test: `test/test_wilink/test_wilink.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: a working `pio test -e native` cycle; `uint8_t wilink::crc(const uint8_t* body, size_t len)`.

- [ ] **Step 1: Write the failing test**

```cpp
void test_crc_matches_documented_start_frame(void) {
    // F7 A2 04 01 A7 FD  ->  A2+04+01 = 0xA7
    const uint8_t body[] = {0xA2, 0x04, 0x01};
    TEST_ASSERT_EQUAL_HEX8(0xA7, wilink::crc(body, 3));
}

void test_crc_matches_documented_status_request(void) {
    // F7 A2 00 00 A2 FD  ->  A2+00+00 = 0xA2
    const uint8_t body[] = {0xA2, 0x00, 0x00};
    TEST_ASSERT_EQUAL_HEX8(0xA2, wilink::crc(body, 3));
}

void test_crc_truncates_to_low_byte(void) {
    const uint8_t body[] = {0xFF, 0xFF, 0x03};
    TEST_ASSERT_EQUAL_HEX8(0x01, wilink::crc(body, 3));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `~/tools/venv-ble/bin/pio test -e native`
Expected: FAIL — compilation error, `wilink::crc` not declared.

- [ ] **Step 3: Write `platformio.ini`**

Two environments. `native` uses `platform = native`, `test_framework = unity`, `build_flags = -std=gnu++17`. `cores3` uses `board = m5stack-cores3`, `framework = arduino`, and `platform` pointing at the current stable **pioarduino** `platform-espressif32` release zip (official PlatformIO does not support Arduino-ESP32 3.x; take the URL from the pioarduino repo README). Add `lib_deps = m5stack/M5Unified`, `h2zero/NimBLE-Arduino`. Set `test_ignore` on `cores3` so native tests do not try to build for the board.

- [ ] **Step 4: Implement `uint8_t crc(const uint8_t* body, size_t len)` in `lib/wilink/wilink.cpp`**

Namespace `wilink`. Header must not include Arduino.

- [ ] **Step 5: Run test to verify it passes**

Run: `~/tools/venv-ble/bin/pio test -e native`
Expected: PASS, 3 tests.

- [ ] **Step 6: Commit**

```bash
git add platformio.ini lib/wilink test/test_wilink
git commit -m "feat: project skeleton with native test harness and WiLink CRC"
```

---

### Task 2: WiLink command frame builders

**Files:**
- Modify: `lib/wilink/wilink.h`, `lib/wilink/wilink.cpp`
- Test: `test/test_wilink/test_wilink.cpp`

**Interfaces:**
- Consumes: `wilink::crc`.
- Produces: `struct Frame { uint8_t bytes[8]; size_t len; };` and
  `Frame requestStatus()`, `Frame setSpeed(uint8_t tenthKmh)`, `Frame setMode(Mode m)`, `Frame startBelt()`;
  `enum class Mode : uint8_t { Auto = 0, Manual = 1, Standby = 2 };`

- [ ] **Step 1: Write the failing test**

```cpp
void test_request_status_matches_documented_frame(void) {
    auto f = wilink::requestStatus();
    const uint8_t want[] = {0xF7, 0xA2, 0x00, 0x00, 0xA2, 0xFD};
    TEST_ASSERT_EQUAL_size_t(6, f.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, f.bytes, 6);
}

void test_start_belt_matches_documented_frame(void) {
    auto f = wilink::startBelt();
    const uint8_t want[] = {0xF7, 0xA2, 0x04, 0x01, 0xA7, 0xFD};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, f.bytes, 6);
}

void test_set_speed_4_2_kmh(void) {
    auto f = wilink::setSpeed(42);           // 4.2 km/h in 0.1 units
    const uint8_t want[] = {0xF7, 0xA2, 0x01, 0x2A, 0xCD, 0xFD};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, f.bytes, 6);
}

void test_set_mode_manual(void) {
    auto f = wilink::setMode(wilink::Mode::Manual);
    const uint8_t want[] = {0xF7, 0xA2, 0x02, 0x01, 0xA5, 0xFD};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, f.bytes, 6);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `~/tools/venv-ble/bin/pio test -e native -f test_wilink`
Expected: FAIL — `requestStatus` not declared.

- [ ] **Step 3: Implement the four builders in `lib/wilink/wilink.cpp`**

Each returns a `Frame` by value. Signatures as in the Interfaces block.

- [ ] **Step 4: Run test to verify it passes**

Run: `~/tools/venv-ble/bin/pio test -e native -f test_wilink`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add lib/wilink test/test_wilink
git commit -m "feat: WiLink command frame builders"
```

---

### Task 3: WiLink status frame parser

**Files:**
- Modify: `lib/wilink/wilink.h`, `lib/wilink/wilink.cpp`
- Test: `test/test_wilink/test_wilink.cpp`

**Interfaces:**
- Produces: `struct Status { uint8_t beltState; uint8_t tenthKmh; uint8_t mode; uint32_t elapsedSec; uint32_t distance10m; uint32_t steps; };`
  and `bool parseStatus(const uint8_t* buf, size_t len, Status& out);`

Returns `false` and leaves `out` untouched for anything that is not a well-formed `F8 A2` frame.

- [ ] **Step 1: Write the failing test**

Covers the happy path plus **Review Focus item 2**.

```cpp
void test_parse_status_extracts_speed_and_mode(void) {
    // F8 A2 <state> <speed*10> <mode> <time BE24> <dist BE24> <steps BE24>
    const uint8_t buf[] = {0xF8, 0xA2, 0x01, 0x2A, 0x01,
                           0x00, 0x00, 0x3C,      // 60 s
                           0x00, 0x00, 0x0A,      // 10 * 10 m = 100 m
                           0x00, 0x00, 0x64};     // 100 steps
    wilink::Status s{};
    TEST_ASSERT_TRUE(wilink::parseStatus(buf, sizeof(buf), s));
    TEST_ASSERT_EQUAL_UINT8(42, s.tenthKmh);
    TEST_ASSERT_EQUAL_UINT8(1, s.mode);
    TEST_ASSERT_EQUAL_UINT32(60, s.elapsedSec);
    TEST_ASSERT_EQUAL_UINT32(10, s.distance10m);
    TEST_ASSERT_EQUAL_UINT32(100, s.steps);
}

void test_parse_status_rejects_wrong_header(void) {
    const uint8_t buf[14] = {0xF7, 0xA2};
    wilink::Status s{};
    TEST_ASSERT_FALSE(wilink::parseStatus(buf, sizeof(buf), s));
}

void test_parse_status_rejects_short_buffer(void) {
    const uint8_t buf[] = {0xF8, 0xA2, 0x01, 0x2A};
    wilink::Status s{};
    TEST_ASSERT_FALSE(wilink::parseStatus(buf, sizeof(buf), s));
}

void test_parse_status_leaves_output_untouched_on_reject(void) {
    const uint8_t buf[] = {0xF8, 0xA2};
    wilink::Status s{};
    s.tenthKmh = 99;
    TEST_ASSERT_FALSE(wilink::parseStatus(buf, sizeof(buf), s));
    TEST_ASSERT_EQUAL_UINT8(99, s.tenthKmh);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `~/tools/venv-ble/bin/pio test -e native -f test_wilink`
Expected: FAIL — `parseStatus` not declared.

- [ ] **Step 3: Implement `parseStatus` in `lib/wilink/wilink.cpp`**

Byte offsets exactly as documented in the spec's WiLink section. The three 24-bit fields are big-endian.

> The spec records these offsets as **unverified against this unit's firmware**. Task 12 confirms them against a live capture; if they differ, this test's vector is the thing to update.

- [ ] **Step 4: Run test to verify it passes**

Run: `~/tools/venv-ble/bin/pio test -e native -f test_wilink`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add lib/wilink test/test_wilink
git commit -m "feat: WiLink status frame parser with malformed-frame rejection"
```

---

### Task 4: FTMS characteristic payload encoders

**Files:**
- Create: `lib/ftms/ftms.h`, `lib/ftms/ftms.cpp`
- Test: `test/test_ftms/test_ftms.cpp`

**Interfaces:**
- Produces: `namespace ftms` with
  `struct Buf { uint8_t bytes[8]; size_t len; };`,
  `Buf featureValue()`, `Buf supportedSpeedRange()`, `Buf treadmillData(uint16_t centiKmh)`, `Buf statusStarted()`.

- [ ] **Step 1: Write the failing test**

```cpp
void test_feature_is_8_bytes_with_speed_target_bit(void) {
    auto b = ftms::featureValue();
    TEST_ASSERT_EQUAL_size_t(8, b.len);
    uint32_t targets = (uint32_t)b.bytes[4] | ((uint32_t)b.bytes[5] << 8)
                     | ((uint32_t)b.bytes[6] << 16) | ((uint32_t)b.bytes[7] << 24);
    TEST_ASSERT_TRUE(targets & 0x1);          // Speed Target Setting Supported
    TEST_ASSERT_FALSE(targets & 0x4);         // Inclination Target Setting NOT supported
}

void test_supported_speed_range_is_half_to_six_step_tenth(void) {
    auto b = ftms::supportedSpeedRange();
    const uint8_t want[] = {0x32, 0x00, 0x58, 0x02, 0x0A, 0x00};  // 0.50 / 6.00 / 0.10
    TEST_ASSERT_EQUAL_size_t(6, b.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, b.bytes, 6);
}

void test_treadmill_data_is_flags_zero_plus_speed(void) {
    auto b = ftms::treadmillData(420);        // 4.20 km/h
    const uint8_t want[] = {0x00, 0x00, 0xA4, 0x01};
    TEST_ASSERT_EQUAL_size_t(4, b.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, b.bytes, 4);
}

void test_status_started_is_opcode_four(void) {
    auto b = ftms::statusStarted();
    TEST_ASSERT_EQUAL_size_t(1, b.len);
    TEST_ASSERT_EQUAL_HEX8(0x04, b.bytes[0]);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `~/tools/venv-ble/bin/pio test -e native -f test_ftms`
Expected: FAIL — namespace `ftms` not declared.

- [ ] **Step 3: Implement the four encoders in `lib/ftms/ftms.cpp`**

`featureValue()` bytes 0–3 are the Fitness Machine Features bitfield: set the bits the bridge genuinely has (average speed and total distance are **not** reported — see the `2ACD` constraint) and leave every inclination bit clear.

- [ ] **Step 4: Run test to verify it passes**

Run: `~/tools/venv-ble/bin/pio test -e native -f test_ftms`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add lib/ftms test/test_ftms
git commit -m "feat: FTMS characteristic payload encoders"
```

---

### Task 5: FTMS control point parser and response builder

**Files:**
- Modify: `lib/ftms/ftms.h`, `lib/ftms/ftms.cpp`
- Test: `test/test_ftms/test_ftms.cpp`

**Interfaces:**
- Produces: `enum class CpOp : uint8_t { RequestControl = 0x00, SetTargetSpeed = 0x02, StartResume = 0x07, StopPause = 0x08, Unknown = 0xFF };`,
  `struct CpRequest { CpOp op; uint16_t centiKmh; uint8_t param; bool valid; };`,
  `CpRequest parseControlPoint(const uint8_t* buf, size_t len);`,
  `Buf controlPointSuccess(CpOp op);`

- [ ] **Step 1: Write the failing test**

Covers the happy paths plus **Review Focus item 1**.

```cpp
void test_parse_request_control(void) {
    const uint8_t buf[] = {0x00};
    auto r = ftms::parseControlPoint(buf, 1);
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_EQUAL(ftms::CpOp::RequestControl, r.op);
}

void test_parse_set_target_speed_little_endian(void) {
    const uint8_t buf[] = {0x02, 0xA4, 0x01};   // 420 = 4.20 km/h
    auto r = ftms::parseControlPoint(buf, 3);
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_EQUAL(ftms::CpOp::SetTargetSpeed, r.op);
    TEST_ASSERT_EQUAL_UINT16(420, r.centiKmh);
}

void test_parse_truncated_set_target_speed_is_invalid(void) {
    const uint8_t buf[] = {0x02};               // opcode with no payload
    auto r = ftms::parseControlPoint(buf, 1);
    TEST_ASSERT_FALSE(r.valid);
}

void test_parse_empty_buffer_is_invalid(void) {
    auto r = ftms::parseControlPoint(nullptr, 0);
    TEST_ASSERT_FALSE(r.valid);
}

void test_success_response_shape(void) {
    auto b = ftms::controlPointSuccess(ftms::CpOp::SetTargetSpeed);
    const uint8_t want[] = {0x80, 0x02, 0x01};
    TEST_ASSERT_EQUAL_size_t(3, b.len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, b.bytes, 3);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `~/tools/venv-ble/bin/pio test -e native -f test_ftms`
Expected: FAIL — `parseControlPoint` not declared.

- [ ] **Step 3: Implement `parseControlPoint` and `controlPointSuccess` in `lib/ftms/ftms.cpp`**

`parseControlPoint` must check `len` before indexing any payload byte. Unrecognised opcodes return `op = Unknown, valid = true` so the caller can still acknowledge them.

- [ ] **Step 4: Run test to verify it passes**

Run: `~/tools/venv-ble/bin/pio test -e native -f test_ftms`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add lib/ftms test/test_ftms
git commit -m "feat: FTMS control point parsing and success responses"
```

---

### Task 6: Speed policy — stop sentinel, deadband, rate limit, resync

**Files:**
- Create: `lib/policy/speed_policy.h`, `lib/policy/speed_policy.cpp`
- Test: `test/test_policy/test_speed_policy.cpp`

**Interfaces:**
- Produces: everything below in namespace `policy` —
  `enum class Action { None, SetSpeed, Stop };`
  `struct Decision { Action action; uint8_t tenthKmh; };`
  `class SpeedPolicy { public: Decision onTarget(uint16_t centiKmh, uint32_t nowMs); void onBeltSpeed(uint8_t tenthKmh); void reset(); };`
  plus the named constants `kStopSentinelCentiKmh = 100`, `kMaxCentiKmh = 600`,
  `kDeadbandTenths = 3`, `kMinCommandIntervalMs = 1000`, `kResyncPolls = 3`.

`onTarget` is the whole pipeline from the spec: sentinel → convert → clamp → deadband → rate limit. `onBeltSpeed` is fed one sample per status poll and implements the manual-override resync.

- [ ] **Step 1: Write the failing test**

Covers the pipeline plus **Review Focus items 3 and 4**.

```cpp
void test_sentinel_is_a_stop(void) {
    policy::SpeedPolicy p;
    auto d = p.onTarget(100, 0);                 // exactly 1.00 km/h
    TEST_ASSERT_EQUAL(policy::Action::Stop, d.action);
}

void test_sentinel_while_already_stopped_emits_nothing(void) {
    policy::SpeedPolicy p;
    p.onTarget(100, 0);
    auto d = p.onTarget(100, 5000);
    TEST_ASSERT_EQUAL(policy::Action::None, d.action);   // no second beep
}

void test_target_above_max_is_clamped_to_six(void) {
    policy::SpeedPolicy p;
    auto d = p.onTarget(900, 0);                 // 9.00 km/h, above advertised max
    TEST_ASSERT_EQUAL(policy::Action::SetSpeed, d.action);
    TEST_ASSERT_EQUAL_UINT8(60, d.tenthKmh);     // 6.0 km/h
}

void test_small_change_inside_deadband_is_dropped(void) {
    policy::SpeedPolicy p;
    p.onTarget(400, 0);                          // 4.00 -> sent
    auto d = p.onTarget(420, 2000);              // +0.2 km/h, inside 0.3 deadband
    TEST_ASSERT_EQUAL(policy::Action::None, d.action);
}

void test_change_outside_deadband_is_sent(void) {
    policy::SpeedPolicy p;
    p.onTarget(400, 0);
    auto d = p.onTarget(440, 2000);              // +0.4 km/h
    TEST_ASSERT_EQUAL(policy::Action::SetSpeed, d.action);
    TEST_ASSERT_EQUAL_UINT8(44, d.tenthKmh);
}

void test_rate_limit_suppresses_second_command_within_one_second(void) {
    policy::SpeedPolicy p;
    p.onTarget(400, 0);
    auto d = p.onTarget(500, 900);               // big change, but only 900 ms later
    TEST_ASSERT_EQUAL(policy::Action::None, d.action);
}

void test_manual_override_resyncs_reference_after_three_polls(void) {
    policy::SpeedPolicy p;
    p.onTarget(400, 0);                          // reference = 4.0
    for (int i = 0; i < 3; i++) p.onBeltSpeed(20);   // remote dropped it to 2.0
    auto d = p.onTarget(400, 10000);             // same target as before
    TEST_ASSERT_EQUAL(policy::Action::SetSpeed, d.action);  // no longer suppressed
    TEST_ASSERT_EQUAL_UINT8(40, d.tenthKmh);
}

void test_two_divergent_polls_do_not_resync(void) {
    policy::SpeedPolicy p;
    p.onTarget(400, 0);
    p.onBeltSpeed(20);
    p.onBeltSpeed(20);
    auto d = p.onTarget(400, 10000);
    TEST_ASSERT_EQUAL(policy::Action::None, d.action);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `~/tools/venv-ble/bin/pio test -e native -f test_policy`
Expected: FAIL — `policy::SpeedPolicy` not declared.

- [ ] **Step 3: Implement `SpeedPolicy` in `lib/policy/speed_policy.cpp`**

Constants come from the Global Constraints: sentinel `100`, max `600` centi-km/h, deadband `3` tenths, minimum interval `1000` ms, resync threshold 3 consecutive divergent polls. Expose them as named constants in the header so Task 11 can display them and a future config surface can override them.

The sentinel bypasses deadband and rate limit; a stop when already stopped returns `None`.

- [ ] **Step 4: Run test to verify it passes**

Run: `~/tools/venv-ble/bin/pio test -e native -f test_policy`
Expected: PASS, 8 tests.

- [ ] **Step 5: Commit**

```bash
git add lib/policy test/test_policy
git commit -m "feat: speed policy with stop sentinel, deadband, rate limit and resync"
```

---

### Task 7: Bridge state machine — arming, start sequence, link loss

**Files:**
- Create: `lib/policy/bridge_state.h`, `lib/policy/bridge_state.cpp`
- Test: `test/test_policy/test_bridge_state.cpp`

**Interfaces:**
- Consumes: `policy::Decision`, `policy::Action`.
- Produces:
  `enum class Emit { None, SetSpeed, Stop, StartSequence, NotifyStarted, ResetPolicy };`
  `struct Outcome { Emit emits[3]; size_t count; uint8_t tenthKmh; };`
  `class BridgeState { public: Outcome setArmed(bool); bool armed() const; Outcome onBeltLink(bool up); Outcome onPhoneLink(bool up); bool beltRunning() const; bool notifying() const; Outcome apply(const Decision& d); Outcome stopFromUi(); };`

  **Every state-changing entry point returns an `Outcome`.** This is deliberate:
  the link-loss Stop and the `SpeedPolicy` reset are obligations that must travel
  in the return value, not in a comment the caller may not read. `Emit::ResetPolicy`
  tells the composition layer to call `SpeedPolicy::reset()` — without it, a
  decision that `BridgeState` drops still leaves `SpeedPolicy` believing it was
  sent, and the next identical target is deadbanded away.

`apply` turns a `SpeedPolicy` decision into what the adapters must actually emit, given arm state and whether the belt is running. `notifying()` tells the FTMS server whether to send `2ACD` at all.

- [ ] **Step 1: Write the failing test**

Covers arming, the start sequence, and **Review Focus item 5**.

```cpp
void test_disarmed_target_emits_nothing(void) {
    policy::BridgeState s;
    s.onBeltLink(true); s.onPhoneLink(true);
    auto o = s.apply({policy::Action::SetSpeed, 40});
    TEST_ASSERT_EQUAL_size_t(0, o.count);
    TEST_ASSERT_FALSE(s.beltRunning());
}

void test_arming_alone_does_not_start_the_belt(void) {
    policy::BridgeState s;
    s.onBeltLink(true); s.onPhoneLink(true);
    s.setArmed(true);
    TEST_ASSERT_FALSE(s.beltRunning());
}

void test_armed_target_on_stopped_belt_runs_start_sequence_then_notifies(void) {
    policy::BridgeState s;
    s.onBeltLink(true); s.onPhoneLink(true); s.setArmed(true);
    auto o = s.apply({policy::Action::SetSpeed, 40});
    TEST_ASSERT_EQUAL_size_t(2, o.count);
    TEST_ASSERT_EQUAL(policy::Emit::StartSequence, o.emits[0]);
    TEST_ASSERT_EQUAL(policy::Emit::NotifyStarted, o.emits[1]);
    TEST_ASSERT_EQUAL_UINT8(40, o.tenthKmh);
    TEST_ASSERT_TRUE(s.beltRunning());
}

void test_armed_target_on_running_belt_is_a_plain_set_speed(void) {
    policy::BridgeState s;
    s.onBeltLink(true); s.onPhoneLink(true); s.setArmed(true);
    s.apply({policy::Action::SetSpeed, 40});
    auto o = s.apply({policy::Action::SetSpeed, 50});
    TEST_ASSERT_EQUAL_size_t(1, o.count);
    TEST_ASSERT_EQUAL(policy::Emit::SetSpeed, o.emits[0]);
}

void test_phone_link_loss_stops_the_belt_but_stays_armed(void) {
    policy::BridgeState s;
    s.onBeltLink(true); s.onPhoneLink(true); s.setArmed(true);
    s.apply({policy::Action::SetSpeed, 40});
    s.onPhoneLink(false);
    TEST_ASSERT_FALSE(s.beltRunning());
    TEST_ASSERT_TRUE(s.armed());
}

void test_reconnect_does_not_restart_the_belt_by_itself(void) {
    policy::BridgeState s;
    s.onBeltLink(true); s.onPhoneLink(true); s.setArmed(true);
    s.apply({policy::Action::SetSpeed, 40});
    s.onPhoneLink(false);
    s.onPhoneLink(true);
    TEST_ASSERT_FALSE(s.beltRunning());
}

void test_ui_stop_works_while_disarmed(void) {
    policy::BridgeState s;
    s.onBeltLink(true); s.onPhoneLink(true);
    auto o = s.stopFromUi();
    TEST_ASSERT_EQUAL(policy::Emit::Stop, o.emits[0]);
}

void test_belt_link_loss_stops_notifications(void) {
    policy::BridgeState s;
    s.onBeltLink(true); s.onPhoneLink(true);
    TEST_ASSERT_TRUE(s.notifying());
    s.onBeltLink(false);
    TEST_ASSERT_FALSE(s.notifying());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `~/tools/venv-ble/bin/pio test -e native -f test_policy`
Expected: FAIL — `policy::BridgeState` not declared.

- [ ] **Step 3: Implement `BridgeState` in `lib/policy/bridge_state.cpp`**

`StartSequence` is a single emit standing for mode(manual) → start → speed; Task 9 expands it into three WiLink writes honouring the inter-command spacing.

- [ ] **Step 4: Run test to verify it passes**

Run: `~/tools/venv-ble/bin/pio test -e native -f test_policy`
Expected: PASS, 8 tests.

- [ ] **Step 5: Commit**

```bash
git add lib/policy test/test_policy
git commit -m "feat: bridge state machine for arming, start sequence and link loss"
```

---

### Task 8: Belt interface and simulated belt

**Files:**
- Create: `lib/belt/belt.h` (abstract interface), `lib/belt/sim_belt.h`, `lib/belt/sim_belt.cpp`
- Test: `test/test_belt/test_sim_belt.cpp`

**Interfaces:**
- Produces:
  `class Belt { public: virtual ~Belt() = default; virtual void setSpeed(uint8_t tenthKmh) = 0; virtual void setMode(wilink::Mode) = 0; virtual void start() = 0; virtual void stop() = 0; virtual bool poll(wilink::Status& out) = 0; virtual bool linkUp() const = 0; };`
  `class SimBelt : public Belt { public: void tick(uint32_t nowMs); uint32_t commandCount() const; };`

`SimBelt` ramps toward the commanded speed and accumulates elapsed time, distance and steps, so stage 1 produces plausible data. `commandCount()` is what proves the deadband is working.

- [ ] **Step 1: Write the failing test**

```cpp
void test_sim_belt_reports_commanded_speed_after_ramp(void) {
    belt::SimBelt b;
    b.setMode(wilink::Mode::Manual); b.start(); b.setSpeed(40);
    for (uint32_t t = 0; t <= 10000; t += 1000) b.tick(t);
    wilink::Status s{};
    TEST_ASSERT_TRUE(b.poll(s));
    TEST_ASSERT_EQUAL_UINT8(40, s.tenthKmh);
}

void test_sim_belt_accumulates_distance_while_running(void) {
    belt::SimBelt b;
    b.setMode(wilink::Mode::Manual); b.start(); b.setSpeed(36);   // 3.6 km/h = 1 m/s
    for (uint32_t t = 0; t <= 60000; t += 1000) b.tick(t);
    wilink::Status s{};
    b.poll(s);
    TEST_ASSERT_GREATER_THAN_UINT32(0, s.distance10m);
}

void test_sim_belt_stop_zeroes_speed(void) {
    belt::SimBelt b;
    b.setMode(wilink::Mode::Manual); b.start(); b.setSpeed(40);
    for (uint32_t t = 0; t <= 10000; t += 1000) b.tick(t);
    b.stop();
    for (uint32_t t = 10000; t <= 20000; t += 1000) b.tick(t);
    wilink::Status s{};
    b.poll(s);
    TEST_ASSERT_EQUAL_UINT8(0, s.tenthKmh);
}

void test_sim_belt_counts_commands(void) {
    belt::SimBelt b;
    b.setSpeed(40);
    b.setSpeed(45);
    TEST_ASSERT_EQUAL_UINT32(2, b.commandCount());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `~/tools/venv-ble/bin/pio test -e native -f test_belt`
Expected: FAIL — `belt::SimBelt` not declared.

- [ ] **Step 3: Implement `Belt` and `SimBelt`**

Ramp rate: 0.5 km/h per second, chosen to look like a real belt; nothing depends on the exact value.

- [ ] **Step 4: Run test to verify it passes**

Run: `~/tools/venv-ble/bin/pio test -e native -f test_belt`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add lib/belt test/test_belt
git commit -m "feat: belt interface and simulated belt for hardware-free testing"
```

---

### Task 9: NimBLE FTMS peripheral adapter

**Files:**
- Create: `src/ftms_server.h`, `src/ftms_server.cpp`

**Interfaces:**
- Consumes: `ftms::*`, `policy::BridgeState`.
- Produces: `class FtmsServer { public: void begin(const char* advName); void notifySpeed(uint16_t centiKmh); void notifyStarted(); bool phoneConnected() const; void setOnTarget(std::function<void(uint16_t)>); void setOnControlOp(std::function<void(ftms::CpOp)>); };`

No native test — this is the hardware shell. Its correctness is established by the Task 10 on-device check.

- [ ] **Step 1: Implement `FtmsServer` in `src/ftms_server.cpp`**

Service `0x1826` with `2ACC` (read), `2AD4` (read), `2ACD` (notify), `2AD9` (write + indicate), `2ADA` (notify). Service `0x180A` with `2A29` manufacturer and `2A24` model — manufacturer must not be `decathlon`. Advertise `0x1826` and `advName`.

On a `2AD9` write: parse with `ftms::parseControlPoint`, always indicate `ftms::controlPointSuccess(req.rawOp)` for a valid request — the **raw** opcode byte, so the acknowledgement names what the client actually wrote — and dispatch Set Target Speed to the `onTarget` callback.

- [ ] **Step 2: Build for the board**

Run: `~/tools/venv-ble/bin/pio run -e cores3`
Expected: SUCCESS.

- [ ] **Step 3: Commit**

```bash
git add src/ftms_server.h src/ftms_server.cpp
git commit -m "feat: NimBLE FTMS peripheral adapter"
```

---

### Task 10: Wire the bridge, run stage 0 (GATT self-check)

**Files:**
- Create: `src/main.cpp`
- Create: `src/config.h`

**Interfaces:**
- Consumes: everything from Tasks 4–9.
- Produces: a flashable firmware that advertises FTMS and runs `SimBelt`. `config.h` is the single place the spec's tunables live — advertised name, manufacturer and model strings, a `USE_SIM_BELT` default, and overrides for the `policy` constants (deadband, command spacing) so they are configurable rather than compiled into the policy itself.

- [ ] **Step 1: Implement `src/main.cpp`**

Wire `FtmsServer` → `SpeedPolicy` → `BridgeState` → `SimBelt`. The `onTarget` callback runs the policy and applies the outcome to the belt; the `onControlOp` callback handles the `{07}` Kinomap sends in reply to our `2ADA` `0x04`. A 1 Hz tick polls the belt, feeds `onBeltSpeed`, and notifies `2ACD` when `BridgeState::notifying()`. Arming starts `true` for this task only, so stage 0 needs no touch input; Task 11 moves it behind the UI.

**Every `Outcome` must be drained, including the ones returned by `setArmed`,
`onBeltLink` and `onPhoneLink`** — those carry the link-loss `Stop` and the
`ResetPolicy` instruction. Write one `applyOutcome(const Outcome&)` helper that
walks `emits[0..count)` and route every call through it; `Emit::ResetPolicy`
calls `SpeedPolicy::reset()`. Dropping one of these outcomes is how the belt
keeps running after the phone disappears.

- [ ] **Step 2: Flash the CoreS3**

Run: `~/tools/venv-ble/bin/pio run -e cores3 -t upload --upload-port /dev/cu.usbmodem101`
Expected: SUCCESS. (If `esptool` is invoked directly instead, it needs `--before usb-reset` — the S3 has no USB-UART bridge to toggle.)

- [ ] **Step 3: Run the stage 0 self-check from the Mac**

Run:
```bash
~/tools/venv-ble/bin/python tools/blescan.py scan 10
~/tools/venv-ble/bin/python tools/blescan.py gatt "<advertised name>"
```
Expected, per the spec's stage 0 pass criteria: advertises `1826` under the name; `2ACC` present; `2AD4` present; `2AD5` **absent**; `2AD9` has write and indicate; `2ACD` has notify; `180A` present.

- [ ] **Step 4: Record the result**

Append the `gatt` output to `docs/test-log.md` under a "Stage 0" heading, with the firmware commit hash.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp src/config.h docs/test-log.md
git commit -m "feat: wire the bridge end to end and pass the stage 0 GATT self-check"
```

---

### Task 11: CoreS3 screen and touch controls (layout A)

**Files:**
- Create: `src/ui.h`, `src/ui.cpp`
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `policy::BridgeState`.
- Produces: `class Ui { public: void begin(); void render(uint16_t centiKmh, uint32_t distance10m, uint32_t elapsedSec, bool beltUp, bool phoneUp, bool armed); bool tookArmToggle(); bool tookStop(); };`

Layout A from the spec: link dots for belt and phone across the top, instantaneous speed very large in the centre, distance and elapsed time small beneath, and ARM and STOP sharing a bottom bar at half width each.

- [ ] **Step 1: Implement `Ui` in `src/ui.cpp` using M5Unified/M5GFX**

Hit-test the bottom bar in `M5.update()`; `tookArmToggle()` and `tookStop()` are edge-triggered and clear on read.

- [ ] **Step 2: Modify `src/main.cpp` to drive arming from the UI**

Remove the Task 10 `armed = true` default; the bridge now boots **disarmed**. `tookStop()` calls `BridgeState::stopFromUi()`.

- [ ] **Step 3: Flash and check by eye**

Run: `~/tools/venv-ble/bin/pio run -e cores3 -t upload --upload-port /dev/cu.usbmodem101`
Expected: screen shows layout A; tapping ARM toggles its state; tapping STOP while the simulated belt runs stops it.

- [ ] **Step 4: Commit**

```bash
git add src/ui.h src/ui.cpp src/main.cpp
git commit -m "feat: CoreS3 metric-first screen with ARM and STOP"
```

---

### Task 12: Stage 1 — real Kinomap against real CoreS3, simulated belt

**Files:**
- Modify: `docs/test-log.md`

**Interfaces:**
- Consumes: the firmware from Task 11.
- Produces: a recorded pass or fail for each of the spec's eleven stage-1 criteria.

This task writes no production code. It is the gate that retires most of the project's risk, and a failure here sends work back to the task that owns the defect.

- [ ] **Step 1: Pair Kinomap with the bridge**

On the Fairphone (`adb -s 100.126.233.97:46363`), in Kinomap: machine type TREADMILL → **I can't find my brand** → LAUNCH SEARCH → select the bridge by name.

- [ ] **Step 2: Walk the eleven stage-1 criteria from the spec**

Verify each in order. Criteria 2, 3 and 8 are the load-bearing ones — Request Control completing without retries, Kinomap actually sending `02 lo hi`, and Kinomap answering our `2ADA` `0x04` with `{07}`.

- [ ] **Step 3: Confirm the deadband with numbers, not impressions**

Compare `SimBelt::commandCount()` against the number of `2ACD` notifications sent over the same interval. Expected: far fewer commands than notifications.

- [ ] **Step 4: Record the results**

One row per criterion in `docs/test-log.md` under "Stage 1": criterion, PASS/FAIL, and the observation. Record the firmware commit hash and the Kinomap version.

- [ ] **Step 5: Commit**

```bash
git add docs/test-log.md
git commit -m "test: stage 1 results, real Kinomap against the bridge with a simulated belt"
```

---

### Task 13: Capture the belt's stop frame and live GATT table

**Files:**
- Modify: `docs/test-log.md`
- Modify: `lib/wilink/wilink.h`, `lib/wilink/wilink.cpp`
- Test: `test/test_wilink/test_wilink.cpp`

**Interfaces:**
- Produces: `wilink::Frame stopBelt();`

**This task resolves the spec's blocking unknown.** The belt ignores unrecognised commands silently, so the stop frame must be observed, never guessed.

- [ ] **Step 1: Capture the vendor app stopping the belt**

Enable **Developer options → Enable Bluetooth HCI snoop log** on the Fairphone by hand (`adb shell setprop persist.bluetooth.btsnooplogmode full` is refused by SELinux). Pair KS Fit with the belt, start it, stop it, then `adb bugreport` and open `btsnoop_hci.log` in Wireshark. Filter on writes to `FE02`.

- [ ] **Step 2: Dump the belt's live GATT table**

Run: `~/tools/venv-ble/bin/python tools/blescan.py gatt "<belt name>"` with the belt powered on and not connected to the iPhone.
Expected: `FE00` with `FE01` notify and `FE02` write, confirming the spec's assumption against this unit's firmware.

- [ ] **Step 3: Confirm the status frame offsets**

Run: `~/tools/venv-ble/bin/python tools/blescan.py watch "<belt name>"`, walk the belt at a known speed, and check that byte 3 of the `F8 A2` frames is speed×10. If the distance or steps offsets differ from the spec, update the Task 3 test vector and parser.

- [ ] **Step 4: Write the failing test for the captured stop frame**

```cpp
void test_stop_belt_matches_captured_frame(void) {
    auto f = wilink::stopBelt();
    const uint8_t want[] = { /* bytes observed in step 1 */ };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, f.bytes, sizeof(want));
}
```

- [ ] **Step 5: Run test to verify it fails, implement `stopBelt()`, run again**

Run: `~/tools/venv-ble/bin/pio test -e native -f test_wilink`
Expected: FAIL, then PASS.

- [ ] **Step 6: Record the capture and commit**

Append the captured frame and the GATT dump to `docs/test-log.md` under "Stage 2 — belt capture". Update the spec's *Still open* section to close the blocking item.

```bash
git add lib/wilink test/test_wilink docs/test-log.md docs/superpowers/specs
git commit -m "feat: captured WiLink stop frame, closing the blocking unknown"
```

---

### Task 14: NimBLE belt central adapter

**Files:**
- Create: `src/belt_link.h`, `src/belt_link.cpp`
- Modify: `src/main.cpp`, `src/config.h`

**Interfaces:**
- Consumes: `belt::Belt`, `wilink::*`, including `stopBelt()` from Task 13.
- Produces: `class BeltLink : public belt::Belt` — a NimBLE central that connects to `FE00`, writes to `FE02`, subscribes to `FE01`, and satisfies the same interface `SimBelt` does.

- [ ] **Step 1: Implement `BeltLink` in `src/belt_link.cpp`**

Scan for `FE00`, connect, subscribe to `FE01`, decode notifications with `wilink::parseStatus`. Writes are queued with a minimum **1000 ms** spacing. Poll with `requestStatus()` on the 1 Hz tick — polling is silent and does not beep.

**`FE02` is write-without-response** (confirmed on the real unit), so there is no
acknowledgement and no flow control. The belt also sends **no unsolicited status**:
every reading comes from a poll. The rate limit is the only backpressure there is.

**The start sequence is not three back-to-back writes.** Measured on the real
belt: a `setSpeed` issued during the countdown (`beltState 7`) is **discarded**,
and the belt ramps to its own preset — 4.0 km/h — regardless. So `StartSequence`
expands to: set mode manual → start → **wait for the belt to report `beltState`
1 or 6** → then send the speed. Between the start and that re-send, the belt will
transiently run at its preset, so the policy will observe a speed the bridge never
commanded; `SpeedPolicy`'s resync is what absorbs that.

`SimBelt` does **not** reproduce the preset ramp — it sits at 0 when no speed was
accepted. This behaviour therefore cannot be validated in stage 1 and must be
implemented from this paragraph, then confirmed in stage 2 against the real belt.

- [ ] **Step 2: Make the belt source selectable**

`config.h` gains a runtime switch between `SimBelt` and `BeltLink`, defaulting to `BeltLink`. The same binary must still run stage 1.

- [ ] **Step 3: Run stage 2 — belt only, no Kinomap**

With the owner standing clear: drive the belt from the CoreS3 screen. Verify the start sequence works from a stopped belt, that commands closer than ~0.7 s are dropped by the belt, that status frames decode to plausible values, and that the observed beep rate matches the configured deadband and spacing.

- [ ] **Step 4: Record the results in `docs/test-log.md` under "Stage 2"**

- [ ] **Step 5: Commit**

```bash
git add src/belt_link.h src/belt_link.cpp src/main.cpp src/config.h docs/test-log.md
git commit -m "feat: NimBLE belt central adapter, stage 2 passing"
```

---

### Task 15: Stage 3 — full loop

**Files:**
- Modify: `docs/test-log.md`, `README.md`

**Interfaces:**
- Consumes: the firmware from Task 14.

- [ ] **Step 1: Run the full loop**

Both links live, simulated belt off. Start **disarmed**, and exercise a deliberate STOP before anything else. Then a short Kinomap route end to end.

- [ ] **Step 2: Re-verify the two safety behaviours on real hardware**

Link loss stops the belt and leaves the bridge armed; the 1.00 km/h sentinel at the end of a Kinomap workout stops the belt rather than leaving it crawling.

- [ ] **Step 3: Write `README.md`**

What the bridge is, how to flash it, how to pair it with Kinomap, what the two screen controls do, and the residual risks from the spec — in particular that the handrail position is invisible to the bridge and the ceiling is fixed at 6.0 km/h per connection.

- [ ] **Step 4: Record the results and commit**

```bash
git add docs/test-log.md README.md
git commit -m "test: stage 3 full-loop results and user documentation"
```
