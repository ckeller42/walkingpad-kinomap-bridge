# Developing

## Getting a build

```sh
pio test -e native            # 153 host tests, no hardware
pio run -e cores3             # real belt (default)
pio run -e cores3_sim         # simulated belt
pio run -e cores3_shot        # simulated belt + a scripted tour over serial
pio run -e cores3 -t upload --upload-port /dev/cu.usbmodem101
```

`pio run -e native` is **not** how the tests run — that environment exists for
`pio test`, and asking it to build will fail on `Arduino.h`.

The toolchain is the pioarduino fork of the Espressif platform, because the
official PlatformIO package lags Arduino-ESP32 3.x.

## Where code goes

| | |
|---|---|
| `lib/` | Arduino-free, host-tested: protocol, pacing, policy, state, screen flows |
| `src/` | adapters that need hardware: BLE, display, Wi-Fi, SD |
| `protocol/` | the YAML single source of truth for both wire formats |
| `tools/` | host-side test and analysis scripts |

**Put the decision in `lib/` returning a value, and the effect in `src/`.**
`BridgeState` is the pattern: every state-changing entry point returns a
`[[nodiscard]] Outcome`, and exactly one `applyOutcome()` drains it. That is
what lets the awkward cases be tested on a laptop — a status frame split three
ways, a start whose tail never arrives, a password too long to store.

## Testing against hardware

The project ships a `bench-test` skill with the full checklist. The parts that
bite hardest:

- **Opening the serial port reboots the board.** Chain a whole scenario into
  one `tools/console.py` call; device state does not survive between
  invocations. To watch a live session, use the [UDP log](logging.md) instead.
- **With nobody on the belt it auto-stops after ~15 s.** Plan a measurement to
  land inside that window, or get someone aboard.
- **The belt's console shows the setpoint, not motion.** A photo reading `2.0`
  is not evidence the belt is moving; only the state is. Cross-check the
  camera, the decoded status and the phone.

Useful harnesses:

```sh
tools/console.py "wait 12; arm; start; wait 8; status; stop"
tools/logcat.py run.log          # live UDP log, no serial port
tools/logparse.py run.log        # decode a capture into a timeline
tools/assert_capture.py run.log  # re-derive the measured facts from a capture's bytes
tools/e2e_sim.py                 # bench E2E on the sim belt; --replay DIR needs no hardware
tools/ftms_client.py 2.5         # act as an FTMS client; exercise paths Kinomap will not
tools/blescan.py scan 12         # who is on the air
tools/beltcam.py shot name       # photograph the belt's own console
```

## End to end on the bench

`tools/e2e_sim.py` runs the whole bridge (console, policy, belt link, FTMS
server, log sink) through scripted scenarios on the real ESP32-S3 and over
real BLE, with the simulated belt standing in for the treadmill. It proves the
bridge's own behaviour end to end. It proves nothing about the belt: the
simulated belt is the bridge's model of it, so a belt fact still needs a
capture from the real one.

It watches three streams and writes them into one host-timestamped timeline:

- **console** — the serial port, opened **once** per run (that reboots the
  board, so the run starts from a known state). Control: commands and replies.
- **ftms** — the Mac connects as an FTMS client in place of Kinomap: `2ACD`
  speeds, `2AD9` writes and indications.
- **udp** — the `logcat` broadcast on port 9999: the bridge's own decisions,
  verbatim. Before any scenario the runner sends `log udp on` and expects to
  hear its echo, which proves the stream alive rather than assuming it.

**The `src=sim` gate.** Before anything else the runner asks `status` and stops
(exit 2) unless the reply says `src=sim`. There is no override: a harness that
sends `arm` and `start` must never reach firmware wired to a real belt. The
gate is asked again before every scenario (the `gate src=sim` row), and a boot
banner or `belt source` line on the console after the preconditions marks the
bench broken (exit 2, nothing further sent): a board that rebooted mid-run
may have come up on the real belt, since holding the screen at boot flips the
source.

```sh
pio run -e cores3_sim -t upload --upload-port /dev/cu.usbmodem101
~/tools/venv-ble/bin/python tools/e2e_sim.py                  # every scenario
~/tools/venv-ble/bin/python tools/e2e_sim.py --scenario phone_start
python3 tools/e2e_sim.py --replay docs/captures/e2e-sim-2026-10-23-seq
~/tools/venv-ble/bin/python tools/e2e_sim.py --record-to docs/captures/e2e-sim-<date>
```

The live run needs the CoreS3 on USB, developer mode with the UDP sink on and
stored Wi-Fi credentials ([logging](logging.md)). It records into
`build/e2e/<stamp>/`: `timeline.jsonl` (the replay source), `console.txt`,
`udp.log` (verbatim `logcat.py` format), `ftms.jsonl` and `report.md`. Exit
status is 0 all green, 1 a check failed, 2 a precondition failed.

`--replay` re-evaluates the same scenarios from `timeline.jsonl` with no
hardware and no BLE or serial packages; CI replays both committed recordings on
every push, so a change to a check is tested against real bytes.
`docs/captures/e2e-sim-2026-10-23` predates sequence numbers and the
per-scenario gate, and stays as the evidence that such a recording still
replays (the failing fixtures are derived from it);
`docs/captures/e2e-sim-2026-10-23-seq` is the same suite on the firmware with
`udp-seq` headers, unicast and heartbeats. Commit a new
recording (`--record-to docs/captures/e2e-sim-<date>`) when a scenario or a
check changes, or when bridge behaviour that a scenario covers changes; never
edit one by hand.

**Known limitation: the UDP log can still lose a datagram.** The bridge
batches about one second of log lines into one datagram. Broadcast frames are
not acknowledged, and on 2026-10-23, before the measures below, 2 of 5 live
runs failed a `udp` check because a datagram was lost while the bridge itself
was fine (the console and FTMS streams agreed). Now:

- Every datagram opens with a `<millis> udp-seq <n>` header, so a lost one is
  a gap in `n`. The runner says `hello` to the bridge, which then also
  unicasts every datagram to it (acknowledged and retried by the Wi-Fi MAC),
  and, when it has nothing to send, a header-only heartbeat
  `<millis> udp-seq <n> idle`: datagrams up to `n` were sent, nothing since.
  The harness counts only data-bearing headers as received, so a datagram
  `n` lost on both copies is a gap once heartbeat `n` arrives, rather than
  being hidden by it.
- A failed check whose window overlaps a gap still **fails**, and its evidence
  ends in `; udp lost (seq a->b)`, which says why: treat it as missing
  evidence and re-run before suspecting the bridge.
- An absence check (`udp absent ...`) holds its window open until a header
  logged at least one flush period (1 s of bridge millis) after the command has
  arrived, then judges every udp line in that window, so a line the command
  caused cannot slip past in the second before its flush. A gap inside the
  window makes it FAIL as `inconclusive: udp lost ...`, and no covering header
  within 5 s makes it FAIL as `inconclusive: no datagram covered the window`.
  A heartbeat `n` closes the window only if datagram `n` itself was received;
  otherwise the check FAILs as `inconclusive: datagram <n> lost`.
  A recording without `udp-seq` lines holds 2.5 s of host time instead.
- `report.md` counts the duplicate datagrams (the broadcast and the unicast
  copy, recorded once) and lists every gap.

What `seq_gaps` cannot see, because a gap needs a number on either side: a
lost datagram 1; lost trailing datagrams after the run's last heartbeat, or
while no hello sender is live (heartbeats go to it alone); and a reboot less
than 3 s after the last header whose own datagram 1 is also lost (the new
boot's numbers then read as late datagrams of the old boot). Trailing loss
during a run is no longer a blind spot: the next heartbeat names the number
the bridge got to, and everything above the last received one is a gap.

The failing fixtures under `tools/fixtures/e2e/` are **synthetic**: each is the
committed recording with exactly one thing removed, and its `README.md` says
what and which check must go red. CI requires every one to fail, which keeps
the checks from passing on missing evidence. They are not recordings and do
not belong in `docs/captures/`.

## Changing a protocol fact

A fact marked `measured` names a capture as its evidence. The captures that
back facts live in `docs/captures/` (verbatim `logcat.py` output), and
`tools/assert_capture.py` turns each such fact into a predicate over the raw
frames: CI runs it, so the fact is re-derived from the bytes on every push
instead of being trusted as prose. Promoting a fact to `measured` means adding
the capture and, where the claim can be stated over frames, a claim there.

`docs/protocol/*.md` is **generated**. Edit the YAML, then:

```sh
python3 tools/gen_protocol_reference.py
python3 tools/gen_frame_layouts.py
python3 tools/check_protocol_views.py    # CI runs this; it fails on stale docs
```

Every fact records `provenance:` — `measured` (seen on this hardware, with
`evidence:` saying where) or `community` (from the decompile or the internet,
**not** confirmed here). Never promote one to the other without a capture.

A `PostToolUse` hook catches stale generated docs at the edit rather than in
CI, and the project ships a `protocol-fact` skill with the rest.

## Changing the GUI

Screenshots in `docs/screens` are **real captures**, which is what makes them
worth having and what makes them rot invisibly. A `Stop` hook refuses to finish
a turn that leaves the GUI newer than its PNGs.

```sh
pio run -e cores3_shot -t upload --upload-port /dev/cu.usbmodem101
tools/capture_shots.py --port /dev/cu.usbmodem101 --seconds 100 --outdir docs/screens
pio run -e cores3 -t upload --upload-port /dev/cu.usbmodem101   # back to normal
```

Give every visually distinct screen its own `phase`, or the tooling captures
only the first of them. Three layout bugs have been found this way that
compiled perfectly and were invisible in code review.

## Documentation

The site is Sphinx with Breathe: the Markdown in `docs/` is the source, and the
C++ API is generated from the headers so the reference cannot drift.

```sh
doxygen Doxyfile && sphinx-build -b html -W docs build/sphinx
```

Doxygen coverage is a **ratchet**, not a gate: `tools/doxygen_ratchet.py` fails
only when the undocumented count goes up, so new code documents itself while
the backlog is paid down. A gate that failed until every module was converted
would simply be switched off.

Reaching zero for a module needs more than a `@brief` — `WARN_NO_PARAMDOC` also
wants an explicit `@param` per argument and a `@return`.

## Releasing

Tag, and CI builds the firmware, publishes it with a manifest, and verifies the
manifest describes the binary beside it:

```sh
git tag v0.2.0 && git push --tags
```

Devices update by pointing at the manifest — see [OTA](ota.md).

## Conventions worth knowing

- Never emit command group `0xE8`, and never name the Telink OTA service.
- Never advertise `decathlon` as the manufacturer.
- No vendor-derived material in this repository; CI enforces it on what is
  *tracked*.
- Distinguish **counted** (a counter or captured frame) from **observed** (a
  screenshot) from **inferred**. Do not let an inference sit unmarked beside a
  measurement.

`AGENTS.md` has the full list with the reasoning behind each.
