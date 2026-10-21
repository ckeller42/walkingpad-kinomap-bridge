---
name: bench-test
description: Use when running any test against the real WalkingPad belt, the ESP bridge or a paired phone - ordering a session around the serial-port reset, keeping the belt safe, and cross-checking results so a setpoint is not mistaken for motion.
---

# Testing against the hardware

Hardware tests in this project fail in ways that look like bugs but are not.
This is the order that avoids each of them.

## Before anything moves

- **Is anyone on the belt?** If not, it **auto-stops ~15 s after starting**.
  Many "the bridge cannot start the belt" results were only ever this. Plan
  the measurement to land inside that window, or get someone aboard.
- **Say out loud that the belt will move**, and at what speed, before starting
  it. Use a slow walk (2.0–2.5 km/h) unless the test needs otherwise.
- **STOP works armed or not.** The verified stop is set mode standby
  (`F7 A2 02 02 A6 FD`), never set-speed-0, which does not stop the belt.
- **Never emit command group `0xE8`** and never touch the Telink OTA service.

## The serial-port reset dictates the order

Opening the port reboots the board, dropping the belt link and the phone's
GATT link. A stale Kinomap session never recovers from that; it sits in
"Your equipment is connecting…" until the workout is restarted.

So:

1. **Console work first**, in **one** invocation — device state does not
   survive between calls. Chain the whole scenario:
   `"wait 12; arm; start; wait 8; status; stop"`.
2. **Then** phone work, inside a long leading `wait` if both are needed:
   `"wait 75; status; arm; start; ..."` gives 75 s to pair while the port
   stays open.
3. **Never open the console mid-session.** Use the UDP log instead
   (`tools/logcat.py`) — see `docs/logging.md`.

`tools/console.py` stops at the first `err`, so put commands in an order that
cannot fail early: `dev on` before `log udp on`, `arm` before `start`.

## Preconditions that look like faults

| Symptom | Actual cause |
|---|---|
| Phone will not pair | belt off, **or** belt not moving — pairing reads live values, so ARM and START first |
| Phone will not pair | something else holds the belt's single BLE connection: KS Fit, or Kinomap on the other phone |
| "Cannot start training" | belt auto-stopped; ARM and START again |
| `err belt-down` | the board rebooted and the belt link has not come up yet — wait ~10 s |
| No `0x02` from Kinomap | expected. Kinomap does not command speed on a generic FTMS treadmill |

## Cross-check three sources, not one

When a speed result matters, agree all three:

1. **The belt's own console** (camera, `tools/beltcam.py`) — physical truth.
2. **The bridge's decoded status** (`status`, or `rx` lines in the log).
3. **The phone's display** (screenshot over ADB).

The belt's console **shows the setpoint, not motion**. It will happily read
`2.0` while `state=0 speed=0`. A photo alone is not evidence the belt is
moving; pair it with the state.

## Recording the result

- Write what was **counted** (a counter, a captured frame) separately from
  what was **observed** (a screenshot) and what was **inferred**. Do not let an
  inference sit unmarked beside a measurement in `docs/test-log.md`.
- A fact that reaches `protocol/*.yaml` needs `provenance:` and `evidence:`.
  `measured` means it was seen on this hardware; `community` means it came
  from the decompile or the internet and has not been confirmed here.
- Afterwards, decode the capture with `tools/logparse.py` — it prints what it
  could not parse, which is where new findings have come from twice.

## Leaving the bench

Stop the belt and disarm. Check `status` shows `belt_state=4` (standby) and
`armed=0`.
