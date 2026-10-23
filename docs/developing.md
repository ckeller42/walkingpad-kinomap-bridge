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
tools/ftms_client.py 2.5         # act as an FTMS client; exercise paths Kinomap will not
tools/blescan.py scan 12         # who is on the air
tools/beltcam.py shot name       # photograph the belt's own console
```

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
