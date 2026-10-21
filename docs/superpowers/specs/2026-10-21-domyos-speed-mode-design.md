# Domyos protocol mode: letting Kinomap drive the belt's speed

**Status:** design, not yet planned or built. Tracks
[issue #2](https://github.com/ckeller42/walkingpad-kinomap-bridge/issues/2).

**Author's note:** the project owner opted out of the brainstorming questions
and asked for the design directly. Everything below marked *(my call)* is a
decision I made on their behalf and should be checked.

## Why

The bridge works, and one half of the original goal is unmet: Kinomap reads the
belt but never commands it. That is not a defect here. Three independent lines
of evidence agree:

- **Counted on the wire.** A full session with a walker aboard and three speed
  changes produced `cp 00=1 03=2 07=1` and no `0x02`.
- **In Kinomap's code.** Every `0x02` path is gated behind `bqj.J()`, which
  reads the account preference `controlSpeedTreadmill` (default false), and
  `kti.java:869` additionally requires `aVar3 instanceof jh8`.
- **In Kinomap's own words.** Their support article states the app does not set
  treadmill speed, for safety and user choice, and that **only Domyos
  treadmills** do, and **only in SPD and INC+SPD structured modes**.

`jh8 extends ih8`, which speaks a proprietary non-FTMS protocol on service
`638A1001-7BDE-3E25-FFC5-9DE9B2A0197A`. `bqj.java:743` selects it by
**equipment type 42**, a property of the paired equipment record — not by the
`2A29` manufacturer string.

So the only route to app-driven speed is to present the bridge as a Domyos
treadmill: a second BLE profile, spoken alongside or instead of FTMS.

## The decision this document does not make

Kinomap's refusal to drive treadmill speed is **stated by the vendor as a
safety measure**. Implementing this mode deliberately defeats that gate for a
machine they have not qualified.

Arguments that it is reasonable here: it is the owner's own treadmill, own
phone and own account; the bridge keeps its own arming gate and a STOP that
works regardless; and the belt itself auto-stops when nobody is walking.

Arguments against: it presents the device as another vendor's product to that
vendor's partner app, and a speed commanded by a route profile is a speed
nobody chose while standing on a moving belt.

**This design assumes the owner has decided to proceed.** It should not be
built on the strength of this document alone. If the answer is no, the honest
alternative is the status quo: the bridge drives, Kinomap records.

## Goals

1. Kinomap commands the belt's speed during an SPD or INC+SPD structured
   workout, on real hardware, with a walker on the belt.
2. The existing FTMS mode remains the **default** and is untouched by this
   work. A user who never enables Domyos mode sees no change at all.
3. The Domyos protocol is documented in `protocol/domyos.yaml` to the same
   standard as the other two, with `provenance:` and `evidence:` on every fact.
4. The protocol logic is testable on a laptop, with no phone, no belt and no
   ESP32 — as `lib/wilink`, `lib/ftms` and `lib/policy` already are.
5. A host-side simulator stands in for Kinomap, so the profile can be developed
   and regression-tested without the phone.

## Non-goals

- **Supporting Domyos hardware.** We are not building a bridge *for* Domyos
  treadmills; we are speaking their protocol to one app.
- **Full protocol coverage.** Only what Kinomap's `ih8`/`jh8` handler actually
  uses. Anything else is noise.
- **Simultaneous FTMS and Domyos advertising.** One profile at a time, selected
  by the existing `PROTOCOL_MODE` setting *(my call — two profiles on one radio
  doubles the GATT surface and the failure modes for no benefit; the mode
  switch already exists and costs one reboot)*.
- **Incline.** The R1 Pro has none. Whatever the Domyos protocol says about
  gradient is accepted and ignored, exactly as `2AD5` is today.

## Anti-goals

Things that would make this a failure even if Kinomap commanded speed:

- **A speed arriving without the user having armed the bridge.** The arming
  gate is not negotiable, whatever profile is active.
- **Losing the ability to stop.** STOP must work in Domyos mode, armed or not,
  by the same verified standby frame.
- **Regressing FTMS mode.** If enabling this work makes the default path worse,
  it has failed.
- **Vendor-derived material entering this repository.** `ih8.java` and
  `qdomyos-zwift` are *sources to read*, not code to copy. CI enforces this on
  what is tracked.
- **A protocol document that cannot be checked.** A `domyos.yaml` full of
  `provenance: community` with no measurements is a liability, not an asset.

## Constraints

Inherited, and not up for renegotiation:

- Command group `0xE8` is never emitted; the belt-side write choke point in
  `BeltLink::send()` stays exactly as it is. **Domyos mode changes only the
  phone-facing profile — the belt side is untouched.**
- The Telink OTA service is never named, connected to, written to or
  subscribed on.
- `lib/` stays Arduino-free and host-tested; `src/` holds the adapters.
- Protocol facts carry provenance; generated docs regenerate from the YAML.
- Nothing moves the belt unless the bridge is armed.

One inherited constraint needs an explicit amendment:

> **"The manufacturer string must never be `decathlon`."**

That rule exists because, in the **FTMS** handler `ag8`, a `decathlon`
manufacturer selects alternate pause/resume behaviour we did not want. It is a
rule about FTMS mode. In Domyos mode the device is identified by its service
UUID and the paired equipment type, not by `2A29`. The constraint therefore
stands **unchanged for FTMS mode** and does not apply to the Domyos profile
*(my call)*. Whatever identification the Domyos profile requires is a protocol
fact to be measured, not a style choice.

## Shape of the work

### 1. Document the protocol first

Before any firmware, produce `protocol/domyos.yaml` covering only what Kinomap
uses: the service and its characteristics (`…1002` through `…100E`), framing,
the status payload the app reads, and the command payload that carries speed.

Two independent sources, which is the point:

- `ih8.java` / `jh8.java` in the Kinomap decompile — the authoritative
  description of *what Kinomap expects*, which is all that matters here.
- the open-source `qdomyos-zwift` project, which documents the same protocol
  from the other side.

Where they agree, mark `provenance: community` with both cited. Where they
disagree, say so and leave it open until measured. Nothing is `measured` until
it has been seen on the wire.

### 2. `lib/domyos/` — encoding and decoding

Pure framing: build the status notification Kinomap reads, parse the command
writes it sends, with no I/O. Host-tested like `lib/wilink`. This is where the
awkward cases get exercised: short writes, unknown opcodes, a speed outside the
belt's 0.5–6.0 km/h range, values at the encoding's boundaries.

**Reuse, do not duplicate.** A speed arriving over Domyos goes through the same
`SpeedPolicy` and `BridgeState` as one arriving over FTMS: the same deadband,
the same pacing, the same arming gate, the same `Outcome` drained by the same
`applyOutcome()`. The new profile is a second *front door*, not a second
system. If it needs its own policy, something has been modelled wrong.

### 3. `tools/domyos_client.py` — the simulator

A host-side Domyos **central** that stands in for Kinomap: connects to the
bridge, subscribes, decodes status, and writes speed commands.

This is the piece that makes the work tractable, and it earns its place for the
same reason `tools/ftms_client.py` did — that one proved the bridge honours
`0x02` correctly *months before* any app would send one. The simulator lets us:

- develop the profile with no phone in the loop;
- exercise paths Kinomap will not (malformed frames, out-of-range speeds,
  rapid-fire commands that the pacing gate must absorb);
- regression-test the profile from CI-adjacent scripts on a bench.

It also inverts: a `--serve` mode that **pretends to be a Domyos treadmill**,
so the phone can be pointed at a laptop first. That isolates "Kinomap accepts
our protocol" from "our ESP32 implementation is correct", which are two
different failures that otherwise look identical *(my call)*.

### 4. `src/domyos_server.*` — the BLE peripheral

The adapter: advertises the Domyos service, serves the characteristics, hands
writes to `lib/domyos` and then to the shared policy. Mirrors `ftms_server` in
structure so the two read the same way.

Selected at boot by `PROTOCOL_MODE`, which already exists in `devmenu` and
currently refuses to switch. Switching modes requires a reboot and re-pairing,
and the UI should say so rather than appearing to fail.

### 5. The belt side does not change

Worth stating plainly because it bounds the blast radius: `BeltLink`,
`BeltGate`, the WiLink encoding and the `0xA2`-only choke point are untouched.
This work adds a phone-facing profile and nothing else.

## How it gets verified

A ladder, cheapest first. Each rung must pass before the next is attempted.

| Rung | What it proves | Needs |
|---|---|---|
| 1. Host unit tests | framing and decoding are right | laptop |
| 2. `domyos_client.py --serve` + phone | Kinomap accepts the protocol and pairs as type 42 | phone |
| 3. `domyos_client.py` against the bridge | our ESP32 profile is correct | bridge |
| 4. Bridge + phone + **sim belt** | the whole chain, with nothing that can move | bridge, phone |
| 5. Bridge + phone + real belt, **walker aboard** | the goal | everything |

Rung 2 is the one that can fail for reasons entirely outside our control — if
Kinomap will not pair with a simulated Domyos treadmill at all, the rest is
wasted effort. **Do rung 2 before writing any firmware.** *(my call, and the
single most important sequencing decision in this document.)*

Rung 5 inherits the hardware lessons already recorded: the belt auto-stops when
nobody is on it, pairing needs the belt *moving*, the belt takes one BLE
connection, and the console's port-open reboots the bridge. The `bench-test`
skill has the full checklist.

**Counted evidence, not impressions.** Success at rung 5 means a control-point
counter showing Kinomap's speed writes and a log showing the matching
`F7 A2 01 <speed>` reaching the belt — the same standard the FTMS work was held
to. A video of a belt changing speed is not evidence of what commanded it.

## What is left to the builder

- The exact framing, characteristic roles and payload layouts. These are to be
  read from the sources, written into `protocol/domyos.yaml`, and measured.
- Whether Kinomap requires any handshake or identification beyond the service
  UUID before it treats the device as equipment type 42.
- How the mode switch presents a required reboot in the developer menu.
- Whether the simulator's `--serve` mode needs a real BLE peripheral on the
  laptop or can reuse an existing tool.

## Execution

The owner asked for **subagent-driven development**. This design should go to
`superpowers:writing-plans` next, and the resulting plan executed with
`superpowers:subagent-driven-development`, as the original bridge was.

The work decomposes well for that: the protocol document, the `lib/` encoding
with its tests, the simulator, and the BLE adapter are four largely independent
tasks with clear interfaces. Rung 2 of the verification ladder is a gate the
plan should respect, not a task to be parallelised past.
