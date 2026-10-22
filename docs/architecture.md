# How it works

The belt and the app cannot talk to each other. This chapter explains what sits
between them, and why it is shaped the way it is.

## The problem

| | Belt | App |
|---|---|---|
| Speaks | KingSmith **WiLink**, proprietary | Bluetooth **FTMS**, a standard |
| Role | BLE peripheral | BLE central |
| Expects | one connected client, commanded at ≤ ~1.4 Hz | to poll and command freely |

Both want to be the one connected *to* something. Neither understands the
other's wire format. So the bridge has to be a **dual-role** BLE device: a
central to the belt and a peripheral to the phone, at the same time, on one
radio.

```text
   WalkingPad R1 Pro                 M5Stack CoreS3                  Kinomap
   ─────────────────                 ──────────────                 ─────────
   BLE peripheral                  central  +  peripheral          BLE central

   FE01 notify ──── F8 A2 status ────▶ BeltLink ─┐
   FE02 write  ◀─── F7 A2 command ──── BeltGate  │
                                                  ▼
                                            SpeedPolicy
                                            BridgeState
                                                  │
                                                  ▼
                            2ACD notify ──── FtmsServer ────▶ speed, distance
                            2AD9 write  ◀─────────────────── control point
```

## The layers, and what each is for

### BeltLink — the wire

Finds the belt (service `FE00`, or the name `RE`), connects, subscribes to
`FE01`, and writes commands to `FE02`. It is the **single choke point for every
write**, and it refuses anything that is not command group `0xA2`. That is what
makes "the OTA group can brick the belt" a structural guarantee rather than a
convention.

It also rejoins status frames the belt splits across two notifications — see
[the test log](test-log.md).

### BeltGate — pacing

The belt **silently ignores** commands sent closer together than about 0.7 s.
Not an error, not a NAK: the command simply does not happen. The phone, mean-
while, can ask for anything at any rate.

`BeltGate` queues writes and releases them no faster than the belt accepts,
with a priority order — stop first, then a status request if one is overdue,
then mode, start, speed. It also retries a stop until the belt confirms it,
because a dropped stop is the one failure that matters.

### SpeedPolicy — deciding whether to act

A deadband, so a target that differs by less than 0.3 km/h is ignored rather
than thrashing the motor, plus the start sequence and a resync after the belt
has been moved by something else.

### BridgeState — permission

Nothing moves the belt unless the user has **armed** the bridge on the device.
Every state-changing entry point returns a `[[nodiscard]] Outcome` listing what
should happen, and exactly one `applyOutcome()` carries it out. Decisions are
therefore testable with no belt attached, and no code path can quietly move the
belt without going through the gate.

### FtmsServer — what the phone sees

Serves the characteristics Kinomap reads and accepts its control-point writes.
Two of those values are load-bearing in ways that are invisible at runtime,
because Kinomap fails *silently* when they are wrong:

- `2ACC` must be **at least 8 bytes with the speed-target bit set**, or the app
  connects, displays data, and never commands a speed.
- `2AD4` must be present, or every target clamps to 0 km/h.

`2AD5` (inclination range) is deliberately **absent**, because the R1 Pro has no
incline. Kinomap still sends inclination targets, and with no range to clamp
against they arrive as 0.0% and are acknowledged and ignored.

## Why the split between `lib/` and `src/`

Everything above the wire — protocol encoding, pacing, policy, state, and the
screen flows — lives in `lib/` and is **Arduino-free**. Everything that needs
hardware — BLE, display, Wi-Fi, SD — lives in `src/`.

That is why 153 tests run on a laptop with no treadmill, no phone and no ESP32
attached, including awkward cases that are hard to produce on a bench: a status
frame split three ways, a start whose tail never arrives, a password too long
to store.

The rule for new work: **put the decision in `lib/` returning a value, and the
effect in `src/`.**

## What the bridge does not do

Kinomap does not command treadmill speed for a generic FTMS machine. That is a
product decision on their side — stated in their own support article, visible
in their code, and confirmed by counting control-point writes on the wire. The
bridge honours `0x02` correctly if one ever arrives; it simply never does.

So the division of labour in practice is: **you and the bridge drive the belt,
and Kinomap reads and records it.** See the [test log](test-log.md) for the
evidence.

The one remaining escape route — presenting the proprietary Domyos profile,
which Kinomap *does* command — was investigated and closed: that handler is a
bike (`TYPE_BIKE`), so it would have bought app-driven speed at the price of
recording a walk as a ride, in cadence and power the belt does not measure.
The [design record](https://github.com/ckeller42/walkingpad-kinomap-bridge/blob/main/docs/superpowers/specs/2026-10-21-domyos-speed-mode-design.md)
keeps what was learned; the firmware was never written, and there is no
protocol setting on the device. The bridge is a treadmill, and only that.
