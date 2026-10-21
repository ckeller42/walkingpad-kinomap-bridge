# Working on this repo

A BLE bridge on an **M5Stack CoreS3 (ESP32-S3)** between a **KingSmith
WalkingPad R1 Pro** treadmill and **Kinomap**. Most of what follows was learned
the expensive way; the reasons are given so you can tell when a rule stops
applying.

## Global constraints

These are not style preferences. Breaking them damages hardware, breaks a
vendor relationship, or silently corrupts the evidence base.

- **`0xE8` is the WiLink OTA command group and can brick the belt.** The bridge
  only ever emits group `0xA2`, enforced at the single write choke point in
  `BeltLink::send()`. Do not add a second write path that bypasses it.
- **Never touch the Telink OTA service** `00010203-0405-0607-0809-0a0b0c0d1912`
  — no connect, no write, no subscribe. `belt_link.cpp` deliberately names no
  UUID other than `FE00`/`FE01`/`FE02`.
- **Never advertise `decathlon` as the manufacturer string.** Kinomap compares
  it case-insensitively and selects different pause/resume behaviour.
- **No vendor-derived material in this repo** — no APK, DEX, `.so`, decompiled
  source or `sources/` tree. It lives in separate private repos. CI enforces
  this (`no-vendor-material`), and it checks what is *tracked*, not what is on
  disk.
- **Nothing moves the belt unless the bridge is armed**, and STOP works whether
  armed or not.

## The one gotcha that shapes everything

**Opening the serial port reboots the ESP32-S3.** That drops the belt link and
the phone's GATT link, which puts Kinomap into a "Your equipment is
connecting…" retry loop that a stale session never recovers from.

Consequences you will hit:

- You cannot run `tools/console.py` during a live session to see what is
  happening. That is what `logsink` exists for: SD and UDP sinks that need no
  serial port (`tools/logcat.py`).
- To set up a session, open the port **first**, with a long leading `wait`, and
  do the phone-side work inside that wait.
- Device state does not survive between `console.py` invocations, so chain a
  whole scenario into one call.

## Layout

| Path | Rule |
|---|---|
| `lib/` | **Arduino-free and host-tested.** Protocol, policy and screen logic. No `Arduino.h`, no BLE, no display. |
| `src/` | Adapters that need hardware: BLE, display, Wi-Fi, SD. |
| `protocol/*.yaml` | **Single source of truth** for both wire protocols. |
| `docs/` | Also the Sphinx source directory. |
| `tools/` | Host-side test and analysis scripts. |

The `lib`/`src` split is what makes 145 tests runnable with no hardware. When
adding behaviour, put the decision in `lib/` returning a value, and the effect
in `src/`. `BridgeState` is the model: every state-changing entry point returns
a `[[nodiscard]] Outcome` that exactly one `applyOutcome()` drains.

## Commands

```sh
pio test -e native            # 145 host tests - NOT "pio run -e native"
pio run -e cores3             # real belt (default)
pio run -e cores3_sim         # simulated belt
pio run -e cores3 -t upload --upload-port /dev/cu.usbmodem101
doxygen Doxyfile && python3 tools/doxygen_ratchet.py build/doxygen/warnings.txt .doxygen-baseline
sphinx-build -b html -W docs build/sphinx
```

Python tooling lives in `~/tools/venv-ble/bin/python`, not the system Python.

## Protocol documentation is generated

Edit `protocol/wilink.yaml` or `protocol/ftms.yaml`, then regenerate:

```sh
python3 tools/gen_protocol_reference.py
python3 tools/gen_frame_layouts.py
python3 tools/check_protocol_views.py   # CI runs this; it fails on stale docs
```

Never hand-edit `docs/protocol/*.md`.

**Record provenance on every fact.** `provenance: measured` means someone saw
it on this hardware and `evidence:` says where. `provenance: community` means
it came from the decompile or the internet and has *not* been confirmed here.
That distinction has already paid for itself: `start_belt` sat at `community`
for weeks and turned out to be correct, while several "facts" around it did not.

## Evidence discipline

This project's findings are its most valuable output, so do not let an
inference sit unmarked next to a measurement.

- "Counted" means a counter or a captured frame. "Observed" means a screenshot.
  "Inferred" means neither — say so.
- The belt's console **shows the setpoint, not motion**. A photo of `2.0` on
  the display is not evidence the belt is moving; only the state is.
- Log **verbatim hex**, not pre-decoded values. A fragmented status frame
  (issue #3) was only ever visible because the raw bytes were written down.
- `tools/logparse.py` decodes a captured log into a timeline and prints what it
  could not parse, rather than hiding it.

## Testing against hardware

- **Nobody on the belt?** It auto-stops about 15 s after starting. Many
  "the bridge cannot start the belt" results were really this.
- **Pairing needs the belt moving**, not merely on: Kinomap's confirmation step
  reads live values, so ARM and START first.
- **The belt accepts one BLE connection.** KS Fit or Kinomap on another phone
  blocks the bridge in a way indistinguishable from the belt being off.
- Cross-check three sources when it matters: the belt's own console (camera),
  the bridge's decoded status, and the phone's display.

## What Kinomap will and will not do

Settled by counted evidence, the decompile and Kinomap's own support article:
it **reads** from a generic FTMS treadmill and **does not command speed**. Every
`0x02` path is gated behind the account flag `controlSpeedTreadmill` plus, on
one path, being a Domyos device. Do not treat a missing `0x02` as a bridge bug.
See issue #2.
