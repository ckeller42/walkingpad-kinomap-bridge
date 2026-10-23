# Sim-belt End-to-End Harness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** One command runs the real firmware on the CoreS3 against its simulated belt through serial, BLE and UDP, asserts six scenarios with recorded evidence, and replays a committed recording in CI.

**Architecture:** `tools/e2e_sim.py` holds a scenario runner whose steps are Python objects evaluated against a `Session`. A `LiveSession` drives the serial console, a bleak FTMS client and a UDP listener and records everything into one host-timestamped `timeline.jsonl`; a `ReplaySession` answers the same scenario code from a recording, so the checker is tested on the host and in CI with no hardware. One firmware addition (`mode auto|manual` on the console, `speed` refused in Auto) goes through the host-tested parser and `BridgeState`.

**Tech Stack:** Python 3.9+ (`~/tools/venv-ble`: pyserial, bleak 3.0.2; both imported lazily), `unittest`; C++17 firmware with Unity host tests (`pio test -e native`).

**Spec:** `docs/superpowers/specs/2026-10-23-sim-belt-e2e-harness-design.md`

## Global Constraints

- The harness refuses every scenario unless the first `status` reply says `src=sim`; there is no override flag.
- The serial port is opened once per run, first, with the `console.py` recipe (DTR and RTS low before and after `open()`, 3 s settle, drain); all BLE work happens afterwards inside that session.
- A check whose stream produced nothing in its window is `FAIL  stream silent`, never a skip. Unparsed lines are counted and printed.
- Zero new dependencies. `e2e_sim.py` imports `serial` and `bleak` only inside the live-session code paths; `--replay` and the unit tests import neither.
- `lib/` stays Arduino-free; the console addition is parsed in `lib/console` (host-tested) and decided by `BridgeState`; `src/main.cpp` only wires it. Every console path keeps going through `BridgeState`/`applyOutcome`/`BeltGate`.
- Recordings under `docs/captures/` are verbatim evidence; synthetic failing fixtures live under `tools/fixtures/e2e/` with a `README` naming what was doctored.
- Commit footer on every commit: `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>` and `Claude-Session: https://claude.ai/code/session_01BmiMYgcv4b45r7o9z27GZc`.
- Log line formats are fixed by `src/main.cpp`: `emit StartSequence tenths=20`, `emit SetSpeed tenths=25`, `emit NotifyStarted tenths=…`, `emit SetMode mode=1`, `emit ResetPolicy tenths=0`, `belt mode <a> -> <b> raw=<m> pending=<p> active=<x>` where `a`/`b` are `policy::BeltMode` numbers (Unknown 0, Auto 1, Manual 2, Standby 3), `udp log ON`.
- Sim timings (`lib/belt/sim_belt.h`): countdown 3000 ms, ramp up 0.7 km/h/s, ramp down 0.8 km/h/s; START commands 2.0 km/h (`kUiStartTenths = 20`).

## Review Focus

1. A port that resets but stays silent (the known USB-Serial-JTAG trap): the runner must exit 2 with "no reply from console" rather than hang. Test in Task 3 (timeout path) and Task 2 (`ReplaySession` with an empty recording).
2. A Kinomap phone holding the GATT link: bleak finds no `WalkingPad Bridge`; exit 2 with the "is a phone holding the link?" message, and the recording so far is still written. Task 3.
3. A device that boots into the wizard (fresh NVS): the runner clicks through at most six times, records it in the report, and the `wizard` scenario still passes. Task 3 (precondition) and Task 4.
4. A `status` reply with a key the checker does not expect, or a missing key: `parse_status` keeps unknown keys and a `Status` check on a missing key fails with "key absent", not a `KeyError`. Task 2.
5. Replaying a recording whose console commands differ from the scenario (someone edited a scenario after recording): `ReplaySession` fails that step with "recording sent `<x>`, scenario expects `<y>`" instead of silently answering. Task 2.

---

### Task 1: Console `mode auto|manual`, and `speed` refused in Auto

**Files:**
- Modify: `lib/console/console_cmd.h` (enum `Cmd`), `lib/console/console_cmd.cpp` (command table)
- Modify: `src/main.cpp` (`consoleRun`, `manualSpeed`)
- Modify: `docs/serial-console.md` (command table)
- Test: `test/test_console/test_console.cpp`

**Interfaces:**
- Produces: `Cmd::ModeAuto`, `Cmd::ModeManual` from `console::parse("mode auto")` / `parse("mode manual")`. Console replies: `ok`, `err wizard-active`, `err not-armed`, `err belt-down`, `err belt-moving`, `err refused`; `speed …` while Auto is observed or pending answers `err auto-mode`.

- [ ] **Step 1: Write the failing tests** in `test/test_console/test_console.cpp`: add to `test_every_documented_command_parses`
  `TEST_ASSERT_EQUAL(Cmd::ModeAuto, parse("mode auto"));` and `TEST_ASSERT_EQUAL(Cmd::ModeManual, parse("Mode  Manual"));`; add to `test_unknown_and_prefix_commands_are_unknown_not_silent` `TEST_ASSERT_EQUAL(Cmd::Unknown, parse("mode"));` and `TEST_ASSERT_EQUAL(Cmd::Unknown, parse("mode fast"));`.
- [ ] **Step 2: Run** `~/tools/venv-ble/bin/pio test -e native -f test_console`. Expected: compile error (no `Cmd::ModeAuto`).
- [ ] **Step 3: Implement** `Cmd::ModeAuto`, `Cmd::ModeManual` in the enum (doc comments like their neighbours) and `{"mode auto", Cmd::ModeAuto}, {"mode manual", Cmd::ModeManual}` in the exact-match table in `console_cmd.cpp`.
- [ ] **Step 4: Run** the same command. Expected: PASS.
- [ ] **Step 5: Wire `consoleRun`** in `src/main.cpp`, one case for both commands, mirroring the badge-tap handler (`ui.tookModeTap()` block) without its reason line:
  `wizard-active` if `inWizard`; `not-armed` if `!state.armed()`; `belt-down` if `!(theBelt.linkUp() && beltFresh())`; `belt-moving` if `!wilink::parked(lastStatus) || startPending || state.beltRunning()`; then `const auto o = state.requestMode(want, false);` and `err refused` if `o.count == 0`, else `applyOutcome(o)` (which prints `ok` like every other case). `want` is `policy::BeltMode::Auto` or `Manual` from the command.
- [ ] **Step 6: Refuse `speed` in Auto**: in `manualSpeed()` after the `not-armed` check add `if (state.autoActive() || state.autoPending()) return "auto-mode";`.
- [ ] **Step 7: Build** `~/tools/venv-ble/bin/pio run -e cores3_sim`. Expected: SUCCESS. Run `~/tools/venv-ble/bin/pio test -e native`. Expected: 214 tests pass (two assertions were added to existing tests).
- [ ] **Step 8: Document** in `docs/serial-console.md`: a `mode auto` / `mode manual` row ("asks the belt to switch mode, like tapping the badge; `err not-armed`, `err belt-down`, `err belt-moving` (belt not parked or a start in flight), `err refused`") and extend the `speed` row with `err auto-mode` (the belt sets its own speed in Auto). Run `npx --yes markdownlint-cli@0.42.0 --config .markdownlint.json docs/serial-console.md`. Expected: no output.
- [ ] **Step 9: Commit** `feat(console): mode auto|manual; speed refused in Auto`.

### Task 2: Harness core — recording model, checks, replay, unit tests (no hardware)

**Files:**
- Create: `tools/e2e_sim.py`
- Create: `tools/test_e2e_sim.py` (unittest)

**Interfaces:**
- Produces (module `e2e_sim`, importable without serial/bleak):
  - `parse_status(line: str) -> dict[str, str]` — `"status armed=1 speed=20 …"` to `{"armed": "1", "speed": "20", …}`; raises `ValueError` if the line does not start with the word `status` followed by a space.
  - `ftms_speed_kmh(hex: str) -> float | None` — `2ACD` payload (`flags u16, speed u16 LE in 0.01 km/h`) to km/h; `None` if shorter than 4 bytes.
  - Steps (dataclasses): `Con(cmd: str, expect: str = "ok")` (`expect` is the exact terminating line, or `"*"` to accept `ok` and any `err …`), `Wait(seconds: float)`, `FtmsConnect()`, `FtmsDisconnect()`, `FtmsCp(hex: str)`, `Status(**kv: str)`, `Udp(regex: str, within: float)`, `UdpAbsent(regex: str)`, `SpeedAtLeast(kmh: float, within: float)`, `SpeedZero(within: float)`, `Indication(hex: str, within: float = 5.0)`.
  - `Scenario(name: str, steps: list)`.
  - `class Session` (abstract): `console(cmd) -> (t: float, reply: list[str])`, `ftms_connect()`, `ftms_disconnect()`, `ftms_cp(hex) -> t`, `wait(seconds)`, `now() -> float`, `timeline() -> list[dict]`.
  - Timeline records (one JSON object per line in `timeline.jsonl`): `{"t": 12.345, "stream": "console", "dir": ">", "line": "arm"}` / `{"t", "stream": "console", "dir": "<", "line": "ok"}`; `{"t", "stream": "udp", "ip": "…", "millis": 65828, "line": "tx f7 a2 …"}`; `{"t", "stream": "ftms", "kind": "write"|"indication"|"notify"|"connect"|"disconnect", "uuid": "2ad9", "hex": "80 07 01"}`.
  - `run_scenario(sc: Scenario, s: Session) -> list[CheckResult]` where `CheckResult(step: str, ok: bool, why: str)`; control steps (`Con`, `FtmsCp`, …) are also results (a `Con` with the wrong reply is a FAIL); the anchor for `within` windows is the `t` of the most recent control step.
  - `class ReplaySession(Session)` built from a `timeline.jsonl` path: `console(cmd)` pops the next recorded `>` line, fails the step with `recording sent '<x>', scenario expects '<y>'` on mismatch, and returns the recorded reply lines (everything until the next `>`); `ftms_cp(hex)` likewise pops the next `write` record; `wait()` advances a cursor only; udp/ftms checks read the recorded records by `t`.
  - `write_report(results_by_scenario, path) -> bool` (Markdown table; returns all-ok) and `exit_code(all_ok, precondition_failed) -> int` (0/1/2).
- Consumes: nothing from Task 1 (the scenario list comes in Task 4).

- [ ] **Step 1: Write failing tests** in `tools/test_e2e_sim.py` (`python3 -m unittest tools.test_e2e_sim` style, `sys.path` to `tools/`):
  - `test_parse_status_keeps_unknown_keys`: `parse_status("status armed=1 speed=20 zz=7")["zz"] == "7"`; `parse_status("ok")` raises `ValueError`.
  - `test_ftms_speed`: `ftms_speed_kmh("00 00 fa 00") == 2.5`; `ftms_speed_kmh("00") is None`.
  - `test_udp_window_includes_only_window`: timeline with udp `emit StartSequence tenths=20` at `t=5.0`; after a control step at `t=4.0`, `Udp("emit StartSequence", within=2)` passes and `Udp("emit StartSequence", within=0.5)` fails with `no match in 0.5 s`.
  - `test_silent_stream_is_fail`: timeline with no udp records → `Udp(".", within=1)` fails with why containing `stream silent`.
  - `test_speed_at_least_reads_2acd`: notify `2acd` hex `00 00 c8 00` (2.0) at `t=6` → `SpeedAtLeast(1.9, within=12)` passes; `SpeedAtLeast(2.1, 12)` fails naming the max seen (`max 2.00`).
  - `test_status_missing_key`: reply `status armed=1` with `Status(running="0")` fails with `key absent: running`.
  - `test_replay_mismatch`: recording sent `arm`, scenario `Con("status")` → FAIL containing `recording sent 'arm'`.
  - `test_replay_empty_recording`: an empty timeline, `Con("status")` → FAIL containing `recording ended` (the replay analogue of a silent port).
  - `test_replay_round_trip`: a hand-built timeline (`> arm`, `< ok`, udp emit line) replays `Scenario("x", [Con("arm"), Udp("emit SyncPolicy|emit", 2)])` green.
  - `test_exit_codes`: `exit_code(True, False) == 0`, `(False, False) == 1`, `(_, True) == 2`.
- [ ] **Step 2: Run** `~/tools/venv-ble/bin/python -m unittest tools/test_e2e_sim.py`. Expected: ImportError / failures.
- [ ] **Step 3: Implement** the interfaces above in `tools/e2e_sim.py`. Module docstring in the style of `assert_capture.py` (what it proves, the `src=sim` gate, the three streams, usage lines: run, `--scenario`, `--replay DIR`, `--record-to DIR`, `--flash`). `main()` may be a stub that only handles `--replay` for now; `LiveSession` arrives in Task 3. No top-level `import serial` / `import bleak`.
- [ ] **Step 4: Run** the tests. Expected: all pass. Also `python3 -I -c "import sys; sys.path.insert(0,'tools'); import e2e_sim"` with the system Python (no bleak) succeeds.
- [ ] **Step 5: Commit** `feat(tools): e2e_sim core: scenario steps, checks, replay, unit tests`.

### Task 3: Live session — serial, UDP, bleak, preconditions, recording

**Files:**
- Modify: `tools/e2e_sim.py` (`LiveSession`, preconditions, recorder, `main()`)
- Modify: `tools/console.py` (move `import serial` into the functions that need it so `default_port()` is importable without pyserial)

**Interfaces:**
- Consumes: Task 2's `Session`, timeline record shapes, `run_scenario`, `write_report`.
- Produces: `class LiveSession(Session)` with `__init__(port: str, out_dir: Path)`; `preconditions(scan_seconds=15) -> str | None` (returns the failure text, or `None`); `close()` writes `console.txt` (`%.3f > cmd` / `%.3f < line`), `udp.log` (verbatim `logcat.py` lines: `<ip> <millis> <rest>`), `ftms.jsonl`, `timeline.jsonl`. `main()` flags: `--port`, `--scenario a,b`, `--out DIR` (default `build/e2e/<UTC %Y%m%dT%H%M%SZ>`), `--record-to DIR` (copy the run there), `--flash` (runs `pio run -e cores3_sim -t upload --upload-port <port>` first), `--replay DIR`, `--list`.

- [ ] **Step 1: Serial** — open with `console.py`'s recipe (import `default_port` from it; DTR/RTS low before and after open, 3 s settle, drain, send `\n`), a reader thread that splits lines, filters `[SHOT`/`S:` frames as `console.py` does, and appends `console` records; `console(cmd)` writes the line and waits up to 5 s for `ok` or `err …`; a timeout returns reply `["err timeout"]` so the step fails and the run can decide.
- [ ] **Step 2: UDP** — a daemon thread bound to `0.0.0.0:9999` with `SO_REUSEADDR` (and `SO_REUSEPORT` where available, as `logcat.py`), parsing `<ip> <millis> <rest>` into udp records; unparsable lines are kept with `"line"` only and counted.
- [ ] **Step 3: FTMS** — a daemon thread running an asyncio loop; `ftms_connect()` scans ≤ 15 s for a device whose local name contains `WalkingPad Bridge` (as `ftms_client.py`), connects, subscribes `2ad9` (indications), `2acd` and `2ada` (notifications); every callback appends an `ftms` record with the characteristic's 16-bit UUID and `hex` with spaces; `ftms_cp(hex)` does `write_gatt_char(CP, bytes, response=True)` and appends a `write` record; `ftms_disconnect()` disconnects. Calls from the scenario thread use `asyncio.run_coroutine_threadsafe(...).result(timeout=20)`.
- [ ] **Step 4: Preconditions**, in this order, each returning its failure text: (1) port exists; (2) `status` answers (else `no reply from console (port reset but silent?)`) and `src=sim` (else `refusing: src=<x> is not the simulated belt`); (3) if `wizard` ≠ `off`, send `wizard next` up to 6 times until `off`, noting `clicked through wizard (<n> presses)` for the report; (4) send `log udp on` and require a udp record matching `udp log ON` within 5 s (else the `log` reply is included in the failure text); (5) `ftms_connect()` succeeds (else `bridge not advertising; is a phone holding the link?`) and `ftms_disconnect()` again so scenarios start unconnected.
- [ ] **Step 5: `main()`** — live run: preconditions → each selected scenario (each begins with `Con("stop", expect="*")` sent by the runner, where `"*"` accepts `ok` or any `err`) → `close()` → `write_report` → optional `--record-to` copy → exit code. `--replay DIR`: `ReplaySession(DIR/"timeline.jsonl")`, same scenarios, report to stdout, exit 0/1. Print one line per check as it is judged: `PASS  <scenario>  <step>  <why>`.
- [ ] **Step 6: Bench smoke** (hardware on `/dev/cu.usbmodem101`): `~/tools/venv-ble/bin/python tools/e2e_sim.py --flash --scenario boot` with a provisional `boot` scenario `[Status(src="sim", belt_link="1", phone_link="0", armed="0", running="0")]`. Expected: preconditions pass (print each), `PASS boot`, exit 0, and `build/e2e/<stamp>/` holds the four files plus `report.md`. Then run with the real-belt build flashed (`pio run -e cores3 -t upload`) and confirm exit 2 with `refusing: src=real …` and that no command after `status` appears in `console.txt`. Flash `cores3_sim` back.
- [ ] **Step 7: Unit tests still green**: `~/tools/venv-ble/bin/python -m unittest tools/test_e2e_sim.py`; `python3 -I -c "…import e2e_sim"` with the system Python still succeeds (lazy imports).
- [ ] **Step 8: Commit** `feat(tools): e2e_sim live session: serial, UDP, bleak, src=sim gate, recording`.

### Task 4: The six scenarios, bench run, committed recording

**Files:**
- Modify: `tools/e2e_sim.py` (`SCENARIOS`)
- Create: `docs/captures/e2e-sim-2026-10-23/` (`console.txt`, `udp.log`, `ftms.jsonl`, `timeline.jsonl`, `report.md`)

**Interfaces:**
- Consumes: Task 1's console replies; Task 2 steps; Task 3 `LiveSession`.
- Produces: `SCENARIOS: dict[str, Scenario]` in this order: `boot`, `refusals`, `ui_start_stop`, `phone_start`, `auto_mode`, `wizard`.

- [ ] **Step 1: Write the scenarios** exactly:
  - `boot`: `Status(src="sim", belt_link="1", phone_link="0", armed="0", running="0")`.
  - `refusals`: `Con("start", expect="err not-armed")`, `UdpAbsent("emit StartSequence")`, `Con("speed 3", expect="err not-armed")`.
  - `ui_start_stop`: `FtmsConnect()`, `Con("arm")`, `Con("start")`, `Udp(r"emit StartSequence tenths=20", 3)`, `SpeedAtLeast(1.9, 12)`, `Udp(r"emit NotifyStarted", 12)`, `Status(running="1", belt_state="1", speed="20")`, `Con("stop")`, `SpeedZero(8)`, `Status(armed="0", running="0", belt_state="4")`, `FtmsDisconnect()`.
  - `phone_start`: `FtmsConnect()`, `Con("arm")`, `FtmsCp("00")`, `Indication("80 00 01")`, `FtmsCp("07")`, `Indication("80 07 01")`, `Udp(r"emit StartSequence", 3)`, `SpeedAtLeast(1.9, 12)`, `FtmsCp("02 fa 00")`, `Indication("80 02 01")`, `Udp(r"emit SetSpeed tenths=25", 3)`, `SpeedAtLeast(2.4, 8)`, `FtmsCp("08 01")`, `Indication("80 08 01")`, `SpeedZero(8)`, `Status(running="0")`, `FtmsDisconnect()`.
  - `auto_mode`: `Con("arm")`, `Con("mode auto")`, `Udp(r"emit SetMode mode=1", 3)`, `Udp(r"belt mode 3 -> 1 raw=0", 5)`, `Con("speed 3", expect="err auto-mode")`, `UdpAbsent(r"emit SetSpeed")`, `Con("mode manual")`, `Udp(r"belt mode 1 -> 2 raw=1", 5)`, `Udp(r"emit ResetPolicy", 5)`, `Con("stop", expect="*")`.
  - `wizard`: `Con("wizard")`, `Status(wizard="siminfo")`, `Con("wizard next")`, `Con("wizard next")`, `Con("wizard next")`, `Status(wizard="off")`.
- [ ] **Step 2: Bench run** `~/tools/venv-ble/bin/python tools/e2e_sim.py` (sim build flashed). Expected: six PASS scenarios, exit 0, under three minutes. If a check fails, decide from the evidence whether the expectation or the firmware is wrong; a wrong expectation is fixed here, a firmware defect is reported in the task report (not patched silently).
- [ ] **Step 3: Record** `… tools/e2e_sim.py --record-to docs/captures/e2e-sim-2026-10-23` (a fresh full run). Then `… tools/e2e_sim.py --replay docs/captures/e2e-sim-2026-10-23`. Expected: identical PASS table, exit 0.
- [ ] **Step 4: Check the recording for secrets**: `grep -i -E 'wifi|pass|psk' docs/captures/e2e-sim-2026-10-23/*` prints nothing except the `udp log ON` / `log` lines (the `log` reply names the SSID, which is already in the committed screenshots; no password ever reaches the log).
- [ ] **Step 5: Commit** `test(e2e): six sim-belt scenarios and the 2026-10-23 bench recording`.

### Task 5: Failing fixtures, CI, docs

**Files:**
- Create: `tools/fixtures/e2e/missing-start-ack/` (copy of the recording with the `80 07 01` indication record deleted from `timeline.jsonl`), `tools/fixtures/e2e/udp-cut-before-start/` (udp records at and after the first `emit StartSequence` deleted), `tools/fixtures/e2e/empty-ftms/` (all `ftms` records except `connect`/`disconnect`/`write` deleted), each with `README.md` (one paragraph: synthetic, derived from which recording, what was removed, which check must fail)
- Modify: `.github/workflows/ci.yml` (`test` job), `docs/conf.py` (`exclude_patterns`), `docs/developing.md`, `AGENTS.md`
- Modify: `docs/serial-console.md` (one sentence pointing at the harness)

- [ ] **Step 1: Fixtures** — produce them with a small one-off filter (not committed) from `docs/captures/e2e-sim-2026-10-23/timeline.jsonl`; only `timeline.jsonl` and `README.md` per fixture. Run `tools/e2e_sim.py --replay tools/fixtures/e2e/<each>`. Expected: exit 1 with exactly the named check red (`Indication 80 07 01`; `Udp emit StartSequence`; `SpeedAtLeast … stream silent`).
- [ ] **Step 2: CI** in `.github/workflows/ci.yml` `test` job, before `pip install platformio`:
  ```yaml
      - run: python3 -m unittest tools/test_e2e_sim.py
      - run: python3 tools/e2e_sim.py --replay docs/captures/e2e-sim-2026-10-23
      - run: |
          for f in tools/fixtures/e2e/*/; do
            if python3 tools/e2e_sim.py --replay "$f" >/dev/null; then echo "fixture $f did not fail"; exit 1; fi
          done
  ```
- [ ] **Step 3: Sphinx** — add `"captures/**"` to `exclude_patterns` in `docs/conf.py`; run `~/tools/venv-ble/bin/sphinx-build -q -b html -W docs build/sphinx`. Expected: exit 0.
- [ ] **Step 4: Docs** — `docs/developing.md`: new section `## End to end on the bench` after "Useful harnesses" (what it proves; three streams; the `src=sim` gate; flash `cores3_sim`, run, replay, `--record-to`, when to commit a new recording; fixtures are synthetic and live under `tools/`), and the harness line in the "Useful harnesses" block; `AGENTS.md` Commands block gains `~/tools/venv-ble/bin/python tools/e2e_sim.py          # bench E2E on the sim belt (needs the CoreS3)` and `python3 tools/e2e_sim.py --replay docs/captures/e2e-sim-2026-10-23`; `docs/serial-console.md` one sentence under "Console": the console is what `tools/e2e_sim.py` drives. Run markdownlint on the three files. Expected: clean.
- [ ] **Step 5: Verify everything** — `pio test -e native` (214), the three CI commands from Step 2 locally with `python3` (system), `check_protocol_views.py`, doxygen ratchet. Expected: all green.
- [ ] **Step 6: Commit** `ci: replay the sim-belt E2E recording and its failing fixtures; docs`.

### Task 6: Log sink reliability for the bench (added by controller ruling after Task 4)

**Files:**
- Modify: `src/logsink.cpp`, `src/logsink.h`, `docs/logging.md`
- Modify: `tools/e2e_sim.py`, `tools/test_e2e_sim.py`, `tools/logcat.py` (comment only), `tools/logparse.py` (treat `udp-seq` as an "other" row)

**Interfaces:**
- Produces (firmware): every UDP datagram the sink sends starts with one header line `<millis> udp-seq <n>` where `n` is a `uint32_t` counter starting at 1 per boot, incremented per datagram (chunks of one flush count separately). The sink's socket binds to `LOG_UDP_PORT` (9999) instead of an ephemeral port and, each `tick()`, reads any pending datagram: a datagram whose payload begins with `hello` makes its source address the unicast target for the next 120 s; while a target is live every datagram is sent twice, broadcast as before and unicast to the target. `logsink::describe()` adds `unicast=<ip or ->`.
- Produces (harness): `LiveSession` sends `hello` to `<bridge ip>:9999` once the first UDP record arrives (the record carries the bridge's IP) and again every 60 s; `udp-seq` records are kept in the timeline like any udp line. `run_scenario` gives every FAILed `Udp`/`UdpAbsent`/`SpeedAtLeast`… check whose window overlaps a sequence gap the suffix `; udp lost (seq a->b)`, and `UdpAbsent` inside a gap reports `inconclusive: udp lost` as a FAIL. A recording without `udp-seq` lines behaves exactly as before (test pins it).

- [ ] **Step 1: Failing harness tests**: `test_seq_gap_annotates_failed_udp_check` (records seq 3 then 5, a `Udp` that misses → why ends with `udp lost (seq 3->5)`), `test_absent_inside_gap_is_inconclusive`, `test_recording_without_seq_is_unchanged` (the committed `docs/captures/e2e-sim-2026-10-23` replays identically).
- [ ] **Step 2: Implement** the harness side; run `~/tools/venv-ble/bin/python -m unittest tools/test_e2e_sim.py` (green) and the replay of the committed recording (unchanged table).
- [ ] **Step 3: Firmware**: the header line and the hello/unicast target in `logsink.cpp` per Produces; `docs/logging.md` gets a short "Sequence numbers and unicast" paragraph (why: a broadcast frame is unacknowledged, so one lost frame loses a second of evidence; with a sequence number a gap is visible, with unicast it is rare). `pio run -e cores3` and `-e cores3_sim` build.
- [ ] **Step 4: Bench**: flash `cores3_sim`; run the full suite three times; expected: three exit-0 runs, `udp-seq` lines in `udp.log`, no gaps (or gaps annotated, never a bare check failure). Keep `cores3_sim` on the board.
- [ ] **Step 5: Commit** `feat(logsink): sequence-numbered datagrams and unicast to a hello sender; harness reports udp loss`.
