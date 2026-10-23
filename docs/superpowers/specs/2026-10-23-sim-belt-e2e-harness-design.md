# Sim-belt end-to-end harness — design (2026-10-23)

## Intent

The bridge has 214 host tests for its decisions and no automated test of the
whole device: BLE in, firmware, BLE out, screen. Both real bugs found on
2026-10-22 (the belt parking in state 3; text overrunning its box) were
invisible to every test and found by eye. The owner chose, from the E2E
proposal, the cheapest piece that closes that gap on the bench: a harness that
runs the real firmware on the real CoreS3 against the firmware's own
**simulated belt**, drives it the way a walker and Kinomap would, and asserts
the result instead of someone watching the screen.

The harness proves the bridge's half of every path. It never involves the real
belt: the simulated belt exists precisely so that nothing can move while a
script runs, and a simulated belt "never looks like a healthy real one"
(amber SIM tag, dedicated wizard screen), so nothing it produces can be
mistaken for evidence about the belt.

## Goals

- One command runs a scripted session end to end on the bench and prints
  PASS/FAIL per check, exit code 0/1, with every claim backed by a recorded
  line (console reply, FTMS notification or UDP log line), never by eye.
- Covers the paths that matter to a walker and to Kinomap: boot and link,
  refusals while disarmed, START/STOP from the bridge, Kinomap's Request
  Control / Start / Set Target Speed / Stop over FTMS, Auto mode in and out,
  and the wizard on a fresh device.
- Every run leaves a recording that can be re-judged offline (`--replay`), and
  one such recording is committed and replayed in CI, so the checker itself is
  tested on every push even though CI has no hardware (the same pattern as
  `tools/assert_capture.py` and `docs/captures/`).
- Zero new dependencies: pyserial and bleak from `~/tools/venv-ble`, which the
  existing `console.py` and `ftms_client.py` already use.

## Non-goals

- Automating the real belt. Someone has to stand on it and it auto-stops; the
  verdict on a real-belt capture is `assert_capture.py`'s job.
- Screenshot or pixel assertions (the headless LovyanGFX item, separate).
- Running on GitHub-hosted CI with hardware. CI replays a recording only.
- Testing Kinomap itself. The Mac's bleak client stands in for it and sends
  exactly the writes Kinomap sends (and `0x02`, which Kinomap will not).

## Anti-goals

- **A scripted command reaching a real belt.** The harness refuses to run any
  scenario unless the device's first `status` reply says `src=sim`. A board
  that was flashed with the real-belt build by mistake gets no `arm`, no
  `start`, nothing. There is no flag to override this.
- A run that passes because a stream was silent. Every check names the
  stream it needs; a stream that produced nothing in its window is a FAIL
  with that reason, never a skip.
- Hiding what could not be parsed. Unparsed console or log lines are counted
  and printed, as `logparse.py` does.

## Constraints (from the project)

- Opening the serial port reboots the ESP32-S3 and drops both BLE links. The
  harness opens the port **once**, first, settles, and does all BLE work from
  the Mac afterwards inside that one session.
- The UDP log (`logcat.py`, port 9999) is the only live evidence channel that
  needs no serial port; it requires developer mode, the UDP sink and stored
  Wi-Fi, all of which persist in NVS. The harness checks the stream is alive
  before any check depends on it.
- The bridge accepts one phone. If Kinomap holds the GATT link the Mac cannot
  connect; that is a precondition failure (exit 2), not a test failure.
- `lib/` stays Arduino-free and host-tested; the one firmware addition goes
  through `console::parse()` (host-tested) and `BridgeState` (host-tested),
  with `main.cpp` only wiring it.
- Nothing moves the belt unless armed; STOP always works; the console must
  keep going through `BridgeState`/`applyOutcome`/`BeltGate` like the touch UI.

## What gets built

### 1. `tools/e2e_sim.py`

A scenario runner with three streams and one report.

**Streams**

| stream | transport | role | evidence it yields |
|---|---|---|---|
| console | serial, one open per run (`console.py` port recipe) | control: commands, host waits | every reply line; `status key=value` parsed into a dict |
| ftms | bleak on the Mac, connects to `WalkingPad Bridge` | stands in for Kinomap | `2ACD` speeds (0.01 km/h), `2AD9` indications, `2ADA` status, each timestamped |
| udp | UDP 9999 listener thread | evidence | every log line with host time and device millis |

**Scenario language** (Python data, in the tool; no DSL file):

- `con("arm")` — send a console command, expect `ok` (or `con("start", err="not-armed")` to expect that error).
- `wait(s)` — host-side pause.
- `ftms.connect()`, `ftms.cp(bytes)`, `ftms.disconnect()`.
- Checks: `status(key=value, ...)` on the next `status` reply; `udp(regex, within=s)` — a log line matching within the window after the previous step; `udp_absent(regex)` — no match since the previous step; `speed_at_least(kmh, within=s)` / `speed_zero(within=s)` on `2ACD`; `indication(hex)` on `2AD9`.

A check that cannot be evaluated because its stream is dead reports FAIL with
`stream silent`.

**Preconditions, checked in this order (exit 2 on failure):**

1. Serial port exists (`/dev/cu.usbmodem*`, or `--port`).
2. After opening and settling, `status` answers and says `src=sim`. Any other
   answer ends the run before any other command is sent.
3. If `wizard` is not `off` (fresh device), the runner presses `wizard next`
   until it is, at most 6 times (the sim wizard is 3 steps), and says so in
   the report. This is setup, not a check.
4. A UDP line arrives within 20 s (the boot broadcasts). Otherwise the run
   stops with the `log` console reply printed, which says why (dev mode, UDP
   sink, Wi-Fi).
5. The bridge advertises `WalkingPad Bridge` (bleak scan ≤ 15 s). Otherwise
   stop with "is a phone holding the link?".

**Built-in scenarios** (`--scenario a,b` selects; default all, in this order,
each starting from a known state by sending `stop` first):

| name | steps | checks (sim timings: countdown 3 s, ramp 0.7 km/h/s up, 0.8 down, START = 2.0 km/h) |
|---|---|---|
| `boot` | `status` | `src=sim belt_link=1 phone_link=0 armed=0 running=0`; udp `udp/9999` seen |
| `refusals` | `start` | `err not-armed`; udp_absent `emit StartSequence`; `speed 3` → `err not-armed` |
| `ui_start_stop` | `arm`, `start`, wait, `status`, `stop`, wait, `status` | `ok`; udp `emit StartSequence tenths=20`; ftms connected beforehand: `speed_at_least(1.9, within=12)`; udp `emit NotifyStarted`; status `running=1 belt_state=1 speed=20`; after stop `speed_zero(within=8)`, status `armed=0 running=0 belt_state=4` |
| `phone_start` | `arm`; ftms `cp 00`, `cp 07`, wait, `cp 02 fa 00` (2.5 km/h), wait, `cp 08 01` | indication `80 00 01`; indication `80 07 01`; udp `emit StartSequence`; `speed_at_least(1.9, 12)`; udp `emit SetSpeed tenths=25`; `speed_at_least(2.4, 8)`; after `cp 08 01` (Stop, which `main.cpp` honours via `stopRequested`): indication `80 08 01`, `speed_zero(8)`, status `running=0` |
| `auto_mode` | `arm`, `mode auto`, `status`, `speed 3`, `mode manual`, `status` | `ok`; udp `emit SetMode mode=1`; udp `belt mode 2 -> 1 raw=0` (sim confirms from standby); `speed 3` → `err auto-mode`; udp_absent `emit SetSpeed`; `mode manual` → udp `belt mode 1 -> 2 raw=1` |
| `wizard` | `wizard`, `status`, `wizard next` ×3, `status` | re-entry only: `wizard=siminfo` after `wizard`; `wizard=off` after three presses. The true first-boot path (`setup_done=0`) is not exercised; clearing the flag would need a second console command and is out of scope. |

**Recording and replay.** Each run writes `build/e2e/<UTC-stamp>/` with
`console.txt` (every line sent and received, host-timestamped), `udp.log`
(verbatim, `logcat.py` format), `ftms.jsonl` (one JSON object per
notification/indication/write with host time), `report.md` (the PASS/FAIL
table with the evidence line for each check). `--replay DIR` re-evaluates the
same scenarios from those files with no hardware; checks with `within=`
windows use the recorded host times. `--record-to docs/captures/e2e-sim-<date>`
copies a run there for committing.

**CI.** The `test` job runs `tools/e2e_sim.py --replay docs/captures/e2e-sim-2026-10-23`
(the first good run, committed with this work) before `pio test`, expecting
exit 0, and then replays each directory under `tools/fixtures/e2e/`, expecting
exit 1. Those fixtures are **synthetic**: copies of the real recording with
one line removed or one stream truncated (a missing `80 07 01` indication; a
`udp.log` cut before `emit StartSequence`; an empty `ftms.jsonl`), each with a
`README` naming what was doctored. They live under `tools/`, never under
`docs/captures/`, which holds only verbatim evidence. The replay path imports
neither pyserial nor bleak (lazy imports in `console.py`, `ftms_client.py`
and the tool), so CI needs no BLE packages.

The recording is made by the author on the bench before the PR opens; a PR
without a green recording does not merge. `docs/conf.py` excludes
`captures/**` so a `report.md` in a recording is not a Sphinx page.

### 2. Firmware: `mode auto|manual` on the console

Needed because Auto is otherwise reachable only by touch. In
`lib/console/console_cmd`: `Cmd::ModeAuto`, `Cmd::ModeManual` (`"mode auto"`,
`"mode manual"`), host-tested like the other commands. In `main.cpp`:
refuse with `err wizard-active`, `err not-armed`, `err belt-down`,
`err belt-moving` (observed running, or the policy says running) before
calling `state.requestMode(BeltMode, observedRunning)` exactly as the badge
tap does; an `Outcome` with no emits (policy refused) answers
`err refused`. `speed …` while Auto is observed or pending answers
`err auto-mode` instead of silently doing nothing (today `manualSpeed()`
applies an Outcome that is empty). Documented in `docs/serial-console.md`.

### 3. Documentation

- `docs/developing.md`: an "End to end on the bench" section — what the
  harness proves, the three-stream idea, the `src=sim` gate, how to flash
  `cores3_sim`, run, replay and commit a recording; "Useful harnesses" gains
  the command.
- `docs/serial-console.md`: the `mode` row and the new `speed` error.
- `AGENTS.md` commands block: the one-line run and replay commands.

## Calls I made (the owner was not asked)

- The "sim belt" is the firmware's `SimBelt` on the real CoreS3, not a fake
  BLE belt on the Mac. It is what the E2E proposal the owner picked described,
  it needs no BLE peripheral on macOS, and it exercises the real BLE server,
  the real policy and the real screen.
- Python, in `~/tools/venv-ble`, reusing `console.py`'s port recipe and
  `ftms_client.py`'s UUIDs by import, not by copy.
- Scenarios are code in the tool, not a YAML DSL: six scenarios do not justify
  a language, and Python keeps the checks readable and greppable.
- The `src=sim` gate is absolute (no override flag). Anyone who wants to
  script a real belt has `console.py` and knows what they are doing.
- The replayed recording is committed under `docs/captures/` beside the
  real-belt capture, because both are evidence and the directory already has
  the CI hook.
- `speed` in Auto becomes an `err`, a behaviour change to the console only;
  the touch UI keeps its reason line. A silent `ok` for a dropped command is
  the kind of thing this harness exists to catch.
- The bench is mine (the CoreS3 on this Mac, developer mode, UDP sink and
  Wi-Fi already stored), so firmware, tool, recording and CI wiring ship in
  one PR.
- The wizard scenario tests re-entry, not first boot: a `setup reset`
  command would be a second firmware change for one check.
- Failing fixtures are allowed, synthetic, labelled and kept out of
  `docs/captures/`.

## Left to the builder

- Exact regexes for the UDP checks (the formats are in `main.cpp`'s
  `logsink::line` calls and are stable). Note the `belt mode a -> b` line
  prints `policy::BeltMode` numbers (Unknown 0, Auto 1, Manual 2, Standby 3)
  with `raw=` the wire mode; after a `stop` the sim is in Standby, so the
  Auto confirmation reads `belt mode 3 -> 1 raw=0`.
- Whether `status` is polled at 1 Hz or on demand inside `speed_at_least`
  (on demand is enough: the FTMS stream carries the speed).
- The report's exact layout.

## Success

On the bench: `~/tools/venv-ble/bin/python tools/e2e_sim.py` against a CoreS3
flashed with `cores3_sim` prints six PASS scenarios with their evidence and
exits 0, in under three minutes. In CI: the committed recording replays
green. A regression in the start sequence, the FTMS acks, the Auto gating or
the wizard flow turns one of those lines red with the stream line that
proves it.
