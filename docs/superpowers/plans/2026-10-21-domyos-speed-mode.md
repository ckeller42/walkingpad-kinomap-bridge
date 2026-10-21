# Domyos Protocol Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let Kinomap command the belt's speed by presenting the bridge as a Domyos **bike**, without changing the default FTMS behaviour. (Task 1 established that Kinomap's type-42 handler is a bike; there is no Domyos treadmill. The session would be recorded as a ride, not a run — see the spec's "Correction" section.)

**Architecture:** A second phone-facing BLE profile. Encoding lives in Arduino-free `lib/domyos/`; the GATT adapter in `src/domyos_server.*` mirrors `FtmsServer`. A Domyos speed is handed to the **same** `SpeedPolicy`/`BridgeState` as an FTMS one — a second front door, not a second system. The belt side (`BeltLink`, `BeltGate`, WiLink) is untouched.

**Tech Stack:** C++17, NimBLE-Arduino, PlatformIO/pioarduino, Unity host tests. Host tooling in Python with `bleak` (central) and `bless` (peripheral).

**Spec:** `docs/superpowers/specs/2026-10-21-domyos-speed-mode-design.md`

## Global Constraints

- Command group `0xE8` is never emitted; `BeltLink::send()` and the WiLink encoding are **not modified by this plan**.
- The Telink OTA service `00010203-0405-0607-0809-0a0b0c0d1912` is never named, connected to, written to or subscribed on.
- `lib/` stays Arduino-free and host-testable; `src/` holds hardware adapters.
- No vendor-derived material is committed: `ih8.java` and `qdomyos-zwift` are sources to **read**, never to copy. CI job `no-vendor-material` enforces this on tracked files.
- Every fact in `protocol/domyos.yaml` carries `provenance:` (`measured` or `community`) and `evidence:`. Nothing is `measured` until seen on the wire.
- Nothing moves the belt unless `BridgeState` is armed. STOP works armed or not, in either protocol mode.
- FTMS remains the default mode. A user who never enables Domyos mode sees no behavioural change.
- The "never advertise `decathlon`" rule stands unchanged **for FTMS mode** and does not apply to the Domyos profile (see spec).
- Builds must be warning-clean under `-Wall -Wextra`. Do not filter build output when checking.
- Domyos service UUID: **`638AF000-7BDE-3E25-FFC5-9DE9B2A0197A`**. `638A1001` is the level characteristic, not the service. See `protocol/domyos.yaml`.
- The device presents as a **bike** (`jh8` sets `TYPE_BIKE`, and `ih8.java:256` gates the speed write on it). There is no Domyos treadmill handler.
- Three connection gates, each of which silently yields a dead session if missed: a write to `638A1105` is what fires `onEquipmentConnected`; the 1 Hz pump needs the `0x2906` Valid Range descriptors of `638A1001` and `638A100E`; `onStartTraining` needs `638A1003` (power) above 5.

## Review Focus

Five failure modes the spec implies but no task's happy path exercises. Each has a test pinned to the task that owns the code.

1. **A speed outside the belt's range.** Kinomap may command 8 km/h on a 0.5–6.0 belt. Expected: clamp to the belt's maximum, never reject silently and never pass through. → Task 3.
2. **A truncated or zero-length write.** BLE writes can arrive short. Expected: decoded as invalid, acknowledged, no speed change. → Task 2.
3. **An unknown opcode.** Expected: treated as unknown and acknowledged, never acted on, by analogy with FTMS where an error makes Kinomap retry in a loop. → Task 2.
4. **A speed arriving while disarmed.** Expected: dropped by `BridgeState`, exactly as an FTMS target is. → Task 5.
5. **Mode switched while the belt is moving.** Expected: refused, like the wizard and developer menu already are. → Task 6.

---

## Task 1: Document the Domyos protocol

Nothing is built until the protocol is written down, because every later task's values come from here.

**Files:**
- Create: `protocol/domyos.yaml`
- Modify: `tools/gen_protocol_reference.py`, `tools/gen_frame_layouts.py`, `tools/check_protocol_views.py` (add `domyos.yaml` to the inputs they already process for `wilink.yaml`/`ftms.yaml`)
- Generated: `docs/protocol/reference.md`, `docs/protocol/frame-layouts.md`

**Interfaces:**
- Produces: the field layouts, opcode values and encoding scale that Tasks 2 and 3 encode against. Later tasks cite `protocol/domyos.yaml` rather than restating values.

- [ ] **Step 1: Read both sources completely**

Read `ih8.java` and `jh8.java` in `/Users/ckeller/src/kinomap-decompile/sources/sources/defpackage/`, and the Domyos protocol description in the open-source `qdomyos-zwift` project. Read them fully before writing — partial understanding of a wire format guarantees bugs.

- [ ] **Step 2: Write `protocol/domyos.yaml`**

Same structure as `protocol/wilink.yaml`: `service`, `characteristics`, `frame`, `commands`, `enums`, `constraints`. Cover only what Kinomap uses: the service, the characteristics it subscribes to and writes, the status payload it reads, and the command that carries speed.

Every entry gets `provenance: community` with **both** sources cited in `evidence:`, since nothing has been measured yet. Where the two sources disagree, record both readings and mark the entry `provenance: disputed` — do not pick one.

- [ ] **Step 3: Regenerate and verify the docs**

Run:
```sh
python3 tools/gen_protocol_reference.py
python3 tools/gen_frame_layouts.py
python3 tools/check_protocol_views.py
```
Expected: `protocol views up to date`.

- [ ] **Step 4: Commit**

```bash
git add protocol/domyos.yaml tools/ docs/protocol/
git commit -m "docs: document the Domyos protocol from two independent sources"
```

---

## Task 2: `lib/domyos` — decode what Kinomap writes

**Files:**
- Create: `lib/domyos/domyos.h`, `lib/domyos/domyos.cpp`
- Test: `test/test_domyos/test_domyos.cpp`

**Interfaces:**
- Consumes: values from `protocol/domyos.yaml` (Task 1).
- Produces:
  ```cpp
  namespace domyos {
  struct Buf { uint8_t bytes[24]; size_t len; };
  enum class Op : uint8_t { /* per protocol/domyos.yaml */ Unknown = 0xFF };
  struct Request { Op op; uint16_t centiKmh; bool valid; uint8_t rawOp; };
  Request parseCommand(const uint8_t* buf, size_t len);
  }
  ```
  `centiKmh` is in 0.01 km/h units, matching `ftms::CpRequest` so both front doors speak one unit to the policy layer.

- [ ] **Step 1: Write the failing tests**

```cpp
void test_parses_a_speed_command(void);        // a known-good frame -> Op::SetSpeed, exact centiKmh
void test_short_write_is_invalid(void);        // len 0 and len 1 -> valid == false, no crash
void test_null_buffer_is_invalid(void);        // nullptr -> valid == false
void test_unknown_opcode_is_reported_not_guessed(void);  // Op::Unknown, valid == true, rawOp preserved
void test_speed_encoding_boundaries(void);     // min and max of the encoding decode exactly
```
Assertions use the exact values recorded in `protocol/domyos.yaml`. `rawOp` preserves the first byte as written, so a response can echo an opcode the decoder did not recognise (same reason as `ftms::CpRequest::rawOp`).

- [ ] **Step 2: Run them and watch them fail**

Run: `pio test -e native -f test_domyos`
Expected: FAIL, `domyos.h` not found.

- [ ] **Step 3: Implement `parseCommand`**

Never read past `len`; treat `nullptr` as empty. Model on `ftms::parseControlPoint` in `lib/ftms/ftms.cpp`.

- [ ] **Step 4: Run tests**

Run: `pio test -e native -f test_domyos`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add lib/domyos test/test_domyos
git commit -m "feat: decode Domyos command writes"
```

---

## Task 3: `lib/domyos` — encode what Kinomap reads

**Files:**
- Modify: `lib/domyos/domyos.h`, `lib/domyos/domyos.cpp`
- Test: `test/test_domyos/test_domyos.cpp`

**Interfaces:**
- Produces:
  ```cpp
  namespace domyos {
  Buf statusFrame(uint16_t centiKmh, uint32_t distance10m, uint32_t elapsedSec);
  uint16_t clampToBelt(uint16_t centiKmh);  // to [kMinCentiKmh, kMaxCentiKmh]
  constexpr uint16_t kMinCentiKmh = 50;     // 0.50 km/h
  constexpr uint16_t kMaxCentiKmh = 600;    // 6.00 km/h
  }
  ```
  The bounds are the belt's measured `2AD4` range, already recorded in `protocol/wilink.yaml`.

- [ ] **Step 1: Write the failing tests**

```cpp
void test_status_frame_carries_speed_distance_time(void);
void test_status_frame_length_matches_the_protocol(void);
void test_speed_above_the_belt_maximum_is_clamped(void);   // 800 -> 600  (Review Focus 1)
void test_speed_below_the_belt_minimum_is_clamped(void);   // 10  -> 50
void test_in_range_speed_is_unchanged(void);               // 250 -> 250
```

- [ ] **Step 2: Run them and watch them fail**

Run: `pio test -e native -f test_domyos`
Expected: FAIL.

- [ ] **Step 3: Implement `statusFrame` and `clampToBelt`**

Clamp rather than reject: a command the belt cannot honour must still produce motion at the nearest speed it can, and never pass through unchanged.

- [ ] **Step 4: Run tests**

Run: `pio test -e native -f test_domyos`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add lib/domyos test/test_domyos
git commit -m "feat: encode Domyos status frames, clamped to the belt's range"
```

---

## Task 4: The simulator

This is the gate. Rung 2 of the spec's verification ladder — *does Kinomap accept a simulated Domyos treadmill at all* — must pass before any firmware is written, because it can fail for reasons wholly outside our control.

**Files:**
- Create: `tools/domyos_client.py`
- Modify: `ENVIRONMENT.md` (record the `bless` dependency alongside `bleak`)

**Interfaces:**
- Produces two modes:
  - `tools/domyos_client.py serve` — a BLE **peripheral** advertising the Domyos service, for the phone to pair with. Uses `bless`.
  - `tools/domyos_client.py drive <km/h>` — a BLE **central** that connects to the bridge and writes a speed command, standing in for Kinomap. Uses `bleak`, mirroring `tools/ftms_client.py`.

- [ ] **Step 1: Install the dependency**

Run: `~/tools/venv-ble/bin/pip install bless` and record the pinned version in `ENVIRONMENT.md`.

- [ ] **Step 2: Implement `serve`**

Advertise the Domyos service and characteristics from `protocol/domyos.yaml`, notify a plausible status frame at the protocol's rate, and print every write received as verbatim hex. Verbatim, not decoded: a decoder in the logging path discards exactly what is worth having (see `docs/logging.md`).

- [ ] **Step 3: THE GATE — pair the phone with the simulator**

On the phone: add equipment and pair with the simulated treadmill. Start an **SPD** or **INC+SPD** structured workout.

Expected: Kinomap pairs, and writes a speed command to `638A1004` that `serve` prints.

This rung now answers two questions, not one: whether it *can* work, and — since the device appears as a bike — whether what it produces is worth having. Capture a screenshot of the Kinomap session so the second question can be judged on evidence rather than description.

**If Kinomap will not pair, or pairs but never writes a speed: STOP.** Report what was observed and do not start Task 5. The remaining tasks are wasted effort if this rung fails. Record the outcome in `docs/test-log.md` either way — a negative result here is as valuable as a positive one, and was the single most expensive thing to learn late in the FTMS work.

- [ ] **Step 4: Promote what was measured**

Every field confirmed by a real Kinomap write moves from `provenance: community` to `provenance: measured` in `protocol/domyos.yaml`, with `evidence:` naming this test. Resolve any `disputed` entries. Regenerate the docs and run `tools/check_protocol_views.py`.

- [ ] **Step 5: Implement `drive`**

Mirror `tools/ftms_client.py`: connect to the bridge, subscribe, write a speed command, print indications and status. Used from Task 5 onward to exercise the firmware without the phone.

- [ ] **Step 6: Commit**

```bash
git add tools/domyos_client.py protocol/domyos.yaml docs/ ENVIRONMENT.md
git commit -m "feat: Domyos simulator; confirm Kinomap drives a simulated treadmill"
```

---

## Task 5: `src/domyos_server` — the BLE adapter

**Files:**
- Create: `src/domyos_server.h`, `src/domyos_server.cpp`
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `domyos::parseCommand`, `domyos::statusFrame`, `domyos::clampToBelt` (Tasks 2–3).
- Produces, mirroring `FtmsServer` so the two read alike:
  ```cpp
  class DomyosServer {
  public:
      void begin(const char* advName);
      void notifyStatus(uint16_t centiKmh, uint32_t distance10m, uint32_t elapsedSec);
      bool phoneConnected() const;
      uint32_t cmdCount(uint8_t rawOp) const;
      void setOnTarget(std::function<void(uint16_t)> cb);
      void handleCommandWrite(const uint8_t* data, size_t len);
  };
  ```

- [ ] **Step 1: Implement `DomyosServer`**

Model on `src/ftms_server.cpp`. Log every write as verbatim hex via `logsink::hex("dcmd", ...)` and count it by raw first byte, so `cp`-style counting works for this profile too.

- [ ] **Step 2: Wire it into `main.cpp`**

`setOnTarget` sets `hasTarget`/`targetCenti` — **the same path FTMS uses** (`src/main.cpp`, the existing `bleServer.setOnTarget` lambda). Do not add a second policy path. This is what makes Review Focus 4 hold without new code: a speed arriving while disarmed is dropped by `BridgeState` exactly as an FTMS target is.

- [ ] **Step 3: Verify the disarmed case on hardware**

With the bridge **disarmed**, run `tools/domyos_client.py drive 2.5`.
Expected: the write is logged and counted, and the belt does not move.

- [ ] **Step 4: Verify the armed case on hardware**

Arm the bridge, belt stopped, nobody on it. Run `tools/domyos_client.py drive 2.5`.
Expected: the log shows the command, then `emit StartSequence`, then `tx f7 a2 01 19` — the same chain `tools/ftms_client.py` produced for FTMS, recorded in `docs/test-log.md`.

- [ ] **Step 5: Commit**

```bash
git add src/domyos_server.h src/domyos_server.cpp src/main.cpp
git commit -m "feat: serve the Domyos profile, sharing the FTMS policy path"
```

---

## Task 6: Mode switching

**Files:**
- Modify: `lib/devmenu/devmenu.cpp`, `src/main.cpp`, `src/logsink.h`, `src/logsink.cpp`
- Test: `test/test_devmenu/test_devmenu.cpp`

**Interfaces:**
- Consumes: `devmenu::Protocol` (already defined in `lib/devmenu/devmenu.h`).
- Produces: `logsink::protocolMode()` / `logsink::setProtocolMode(devmenu::Protocol)`, persisted in NVS under key `proto`, defaulting to `Protocol::Ftms`.

- [ ] **Step 1: Write the failing test**

```cpp
void test_protocol_row_cycles_once_domyos_is_available(void);
```
Tapping the Protocol row returns `Action::CycleProtocol`, and the row's value reflects the mode passed in `Inputs`.

- [ ] **Step 2: Run it and watch it fail**

Run: `pio test -e native -f test_devmenu`
Expected: FAIL.

- [ ] **Step 3: Persist the mode and honour it at boot**

`main.cpp` reads the mode in `setup()` and starts **either** `FtmsServer` **or** `DomyosServer`, never both. Replace the current `CycleProtocol` handler, which prints "only FTMS is implemented".

- [ ] **Step 4: Refuse a switch under a moving belt** *(Review Focus 5)*

Guard the `CycleProtocol` action with `state.beltRunning()`, matching the existing guards on the wizard and the developer menu. Switching requires a reboot and re-pairing; say so on screen rather than appearing to fail.

- [ ] **Step 5: Run tests and build**

Run: `pio test -e native` and `pio run -e cores3`
Expected: all tests pass; build is warning-clean.

- [ ] **Step 6: Commit**

```bash
git add lib/devmenu src/main.cpp src/logsink.h src/logsink.cpp test/test_devmenu
git commit -m "feat: persist and honour the protocol mode"
```

---

## Task 7: End-to-end verification

**Files:**
- Modify: `docs/test-log.md`, `docs/developer-menu.md`, `README.md`

**Interfaces:**
- Consumes: everything above.

- [ ] **Step 1: Rung 4 — phone, bridge, simulated belt**

Boot with `cores3_sim` (the simulated belt cannot move), Domyos mode on. Pair the phone, start an SPD workout.
Expected: counters show Kinomap's speed writes; `status` shows the commanded speed; nothing can physically move.

- [ ] **Step 2: Rung 5 — the real belt, with a walker**

Follow the `bench-test` skill. The belt auto-stops when nobody walks on it, pairing needs the belt **moving**, and the belt takes one BLE connection.

Expected, as **counted** evidence and not impressions: a command counter showing Kinomap's speed writes, and a log showing the matching `tx f7 a2 01 <speed>` reaching the belt.

- [ ] **Step 3: Record the result**

Write it up in `docs/test-log.md` separating what was **counted** from what was **observed** from what was **inferred**. If Kinomap still does not command speed, say so plainly and state what the evidence rules out.

- [ ] **Step 4: Update the user-facing docs**

`docs/developer-menu.md`: the Protocol row now switches modes, needs a reboot and re-pairing. `README.md`: amend "what works and what does not" to match what was actually measured.

- [ ] **Step 5: Commit**

```bash
git add docs/ README.md
git commit -m "test: Domyos mode end to end, with counted evidence"
```

---

## Self-review notes

- **Spec coverage:** protocol document → T1; `lib/domyos` → T2–T3; simulator → T4; BLE adapter → T5; mode switch → T6; verification ladder → T4 step 3 (rung 2), T5 steps 3–4 (rung 3), T7 (rungs 4–5). The spec's non-goal "no simultaneous advertising" is enforced in T6 step 3.
- **Review Focus coverage:** 1 → T3; 2 and 3 → T2; 4 → T5 step 3; 5 → T6 step 4.
- **Unit consistency:** `centiKmh` (0.01 km/h) throughout `lib/domyos` and both servers, matching `ftms::CpRequest::centiKmh`. `lib/wilink` uses 0.1 km/h; conversion stays where it already is, in `applyOutcome`.
- **Deliberately left open:** the exact framing and characteristic roles, which Task 1 establishes from the sources and Task 4 step 4 promotes to `measured`. Writing guessed values into this plan would make them look decided.
