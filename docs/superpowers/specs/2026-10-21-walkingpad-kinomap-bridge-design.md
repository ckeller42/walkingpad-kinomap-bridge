# WalkingPad R1 Pro → Kinomap bridge — design

**Date:** 2026-10-21
**Status:** design, not yet approved for implementation

---

## Intent, and why

The owner has a KingSmith **WalkingPad R1 Pro** treadmill and wants to use it with
**Kinomap**, which does not support it. Kinomap supports treadmills that speak the
Bluetooth **Fitness Machine Service (FTMS)**; the R1 Pro speaks KingSmith's own
legacy "WiLink" protocol instead. Nothing bridges the two.

The goal is **parity with a natively supported treadmill**: Kinomap sets the speed
to follow the video, and reads back the machine's live data, exactly as it would
with a machine off its own brand list.

An **M5Stack CoreS3** (ESP32-S3, already owned, already on this Mac's USB) sits in
the middle, holding two BLE connections at once:

```
WalkingPad R1 Pro  ◄── BLE, WiLink FE00 ──►  CoreS3  ◄── BLE, FTMS 1826 ──►  Kinomap (Android/iOS)
                          bridge = central                bridge = peripheral
```

### Why a bridge and not something else

- Kinomap is closed and cloud-backed; it cannot be extended with a plugin.
- The R1 Pro's firmware cannot be replaced.
- A phone-side app would have to hold both BLE links *and* be the thing Kinomap
  talks to — not possible on iOS or Android without replacing Kinomap itself.
- A dedicated piece of hardware is therefore the only seam, and it has a bonus:
  it is also where the safety controls can live, physically reachable.

---

## Goals

1. Kinomap discovers the bridge, connects, and treats it as a treadmill.
2. Kinomap sets the target speed; the belt follows.
3. Kinomap receives live speed and derives distance and time as it normally would.
4. Kinomap can start the belt — subject to the arming rule below.
5. Stopping always works, from Kinomap or from the bridge.
6. The bridge is usable while walking: readable at a glance, one reachable STOP.

## Non-goals

- Supporting other treadmills, or other apps than Kinomap.
- Incline. The R1 Pro has no incline hardware (see *Established facts*).
- Replacing the KS Fit app, or its workout history and cloud sync.
- Controlling the belt from the bridge beyond arming and stopping. The handheld
  remote remains the primary manual control.
- Firmware updates for the belt, heart-rate relaying, multi-user profiles.

## Anti-goals — things that would make this a failure even if it worked

- **A beeping treadmill.** The R1 Pro beeps on every accepted command and the beep
  cannot be disabled. A bridge that forwards Kinomap's continuous speed targets
  one-for-one would beep incessantly and be unusable. Suppressing unnecessary
  commands is a primary requirement, not an optimisation.
- **A belt that starts when nobody expects it.** Kinomap may start the belt, but
  only when the owner has armed the bridge.
- **A belt that keeps running when the link drops.** Losing the phone mid-workout
  must stop the belt, not leave it running.
- **Impersonating another vendor.** Not needed (see *Established facts*), and not
  acceptable as a shortcut.

---

## Established facts

Everything in this section was verified during design. Sources are cited; no
vendor source is reproduced here. The vendor-app analysis lives in the separate
private repos `~/src/ksfit-decompile` and `~/src/kinomap-decompile`.

### The treadmill side

The R1 Pro uses KingSmith's legacy **WiLink** protocol. Confirmed independently by
community reverse-engineering (ph4r05/ph4-walkingpad, DorianRudolph/QWalkingPad,
anativ/R1-Walkingpad-Controller) **and** by the presence of `0000fe00`, `0000fe01`,
`0000fe02` in KS Fit 6.6.0's own Dart AOT snapshot.

| | |
|---|---|
| Service | `0000fe00` |
| Notify (status) | `0000fe01` |
| Write (commands) | `0000fe02` |

Frames are `F7 <cmd> <payload…> <crc> FD`, where `crc` is the low byte of the sum
of everything between the header and the CRC.

| Function | Frame |
|---|---|
| Request status | `F7 A2 00 00 A2 FD` |
| Set speed (0.1 km/h units) | `F7 A2 01 <speed> <crc> FD` |
| Set mode (0 auto, 1 manual, 2 standby) | `F7 A2 02 <mode> <crc> FD` |
| Start belt | `F7 A2 04 01 A7 FD` |

Status frames begin `F8 A2`: byte 2 belt state, byte 3 speed×10, byte 4 mode, then
elapsed time (big-endian 24-bit seconds), distance (10 m units), steps.

**Three behavioural constraints shape the whole design:**

1. Every accepted command **beeps**, and cannot be muted in software. Status polling is silent.
2. Commands sent less than **~0.7 s** apart are **ignored**.
3. Speed changes apply only in **manual mode on a running belt**. From a stop the
   vendor app sends mode → start → speed in sequence.

**There is no incline.** KingSmith's FAQ says WalkingPads cannot combine folding
with incline. In KS Fit, `supportIncline` is a per-model capability flag and
`slopeMax` is persisted as a column in the local workout table beside heart rate
and power — it is *logged*, never commanded. The owner confirmed by observation:
changing the incline value moves nothing.

### The Kinomap side

Kinomap 4.7.1 (versionCode 654) is a **native Android app**. Observed and read
directly.

**Pairing is not brand-gated.** The onboarding offers a brand list, but also
"I can't find my brand" → *"We will search for your machine using the most common
protocols."* The resulting scan does **not** filter by service UUID — the observed
device list included a power bank and an environment sensor. Discovery is therefore not a
risk; **acceptance after connection is**. A distinctive advertised name matters,
because the owner picks the bridge by name out of a list of neighbours' gadgets.

**What Kinomap sends to the control point** (`2AD9`), from `ag8.java` (its FTMS
treadmill handler, log tag `FTMSTREAD`) and its parent `uf8.java`:

| Opcode | Meaning | Encoding |
|---|---|---|
| `00` | Request Control | sent immediately on discovering `2AD9` |
| `02 lo hi` | Set Target Speed | uint16 LE, **km/h × 100** |
| `07` | Start / Resume | |
| `08 01` | Stop | |
| `08 02` | Pause | |

**What Kinomap requires back.** Two of these are silent-failure traps — the bridge
appears to work and simply never moves:

1. **`2ACC` Fitness Machine Feature must be at least 8 bytes, and bit 0 of the
   Target Setting Features field (bytes 4–7, uint32 LE) must be set.**
   `uf8.java:154` reads that field and stores bit 0; `ag8.java` only sends Set
   Target Speed when that flag is true. Without it Kinomap connects, shows data,
   and never commands a speed.
2. **`2AD4` Supported Speed Range must be present and 6 bytes.** `ag8.java`
   clamps every target speed to the min/max it read from `2AD4`. Those fields
   default to zero, so a missing `2AD4` clamps **every** target to 0 km/h.
3. **`2AD9` must indicate responses.** Kinomap expects `80 <request-opcode> 01`
   for success. On a failed Request Control it retries; on a failed Start it sends
   Stop and then Start again. Silence breaks the handshake.

**Kinomap computes distance itself.** Its 1 Hz tick integrates instantaneous speed
(`uf8.java:t0()` — metres += km/h ÷ 3.6 each second) and reports that as distance.
Total Distance in the data notification is therefore optional.

**Kinomap's flags parser is fragile.** `ag8.java:z()` reads the `2ACD` flags field
**big-endian** (the spec says little-endian) and with a `×255` multiplier rather
than `×256`. Any optional field we add risks a mis-parse that silently corrupts
every subsequent offset. This is the reason for the minimal-payload decision below.

**Device Information (`180A`) is read** — manufacturer (`2A29`) and model (`2A24`)
— and the manufacturer string gates vendor-specific quirk paths. The string
`decathlon` in particular triggers alternate pause/resume behaviour, so the bridge
must not use it.

---

## Decisions the owner made

These are theirs, in their words where possible.

1. **Full parity** with a natively supported treadmill — Kinomap controls the
   belt and reads its values.
2. **"Only change speed if the difference is big enough."** A deadband, to keep
   the beeping tolerable.
3. **Ignore the climb, keep it flat.** Route gradient is discarded; no attempt to
   simulate hills with speed.
4. **Kinomap shall be able to start the belt**, with controls added to the ESP32
   if that is what safety requires.
5. **Armed stays armed until tapped off** — no auto-disarm on idle or at the end
   of a workout.
6. **Screen layout A, "metric-first":** speed dominant, distance and time small
   beneath it, link indicators for belt and phone, and ARM and STOP sharing a
   bottom bar. Chosen over a state-banner layout and a three-control split.

## Decisions made for them, and flagged as such

These are the designer's calls, not the owner's. Each is a sensible default, and
each is cheap to change.

- **Deadband 0.3 km/h**, minimum write spacing **1 s** (the belt ignores anything
  under ~0.7 s; 1 s gives margin). Both configurable rather than compiled in.
- **Framework: Arduino + M5Unified, built with PlatformIO.** The owner delegated
  this choice. The deciding factor is the CoreS3's hardware: its display, touch
  controller, AXP2101 power management and I²C expander are all handled by
  M5Unified/M5GFX out of the box, where ESP-IDF would mean bringing up that stack
  by hand or depending on M5's less-mature IDF component. The bridge's own logic
  is small; almost all of the incidental work here is board bring-up and GUI, and
  that is exactly what M5Unified removes.
  *Cost, stated plainly:* this diverges from the owner's existing ESP-IDF CoreS3
  project on `bar`, so the two will not share build tooling. Judged worth it —
  the flashing workflow is the only thing they would have shared, and `esptool`
  is common to both anyway.
- **NimBLE-Arduino** rather than Bluedroid — dual-role in roughly 40 KB RAM, and
  the simultaneous central + peripheral case is well-trodden there.
- The bridge presents itself with a **distinctive advertised name**, since the
  owner picks it from a list by name.
- **Minimal `2ACD` payload**: flags `0x0000`, instantaneous speed only. Justified
  by Kinomap's fragile flags parser and the fact that it derives distance itself.

---

## How it behaves

### Translating downward (Kinomap → belt)

Kinomap writes Set Target Speed (km/h × 100). The bridge:

1. Converts to the belt's 0.1 km/h units.
2. Applies the **deadband** — a new target within 0.3 km/h of the last *sent*
   speed is dropped, not queued.
3. Applies **rate limiting** — at most one command per second, with the most
   recent target winning if several arrive.
4. If the belt is stopped and the bridge is armed, runs the **start sequence**:
   set manual mode, start, then speed.

Start / Resume and Stop / Pause are handled per the arming rules below. Every
accepted control-point write is answered with the FTMS success indication; every
rejected one with the appropriate error code, so Kinomap's retry logic behaves.

### Translating upward (belt → Kinomap)

The bridge polls the belt's status (silent, no beep) and republishes instantaneous
speed as a `2ACD` notification with flags `0x0000`. Elapsed time and distance from
the belt are shown on the CoreS3 screen but are not put in the notification.

### Arming, starting, stopping

- The bridge is **disarmed** at power-on.
- Arming is a deliberate action on the CoreS3 screen, and **persists until tapped
  off** — not across reboots.
- **While armed**, Kinomap's Start/Resume starts the belt.
- **While disarmed**, Start/Resume is refused at the control point with an FTMS
  error, so Kinomap's own retry/fallback path runs rather than it hanging.
- **Stop always works**, armed or not, from Kinomap or from the screen.
- **Link loss stops the belt.** If the phone's connection drops while the belt is
  running, the bridge stops it. Losing the *belt* link is reported on screen.

### The screen (layout A)

Top: two link indicators, belt and phone. Centre: instantaneous speed, very large.
Below: distance and elapsed time, small. Bottom bar: ARM (showing the current
state) and STOP, side by side, each half the width.

---

## Constraints

- **The belt beeps on every command.** Fewer commands is better, always.
- **~0.7 s minimum between commands**, or they are silently dropped.
- **Speed only applies in manual mode on a moving belt.**
- **One BLE connection per device.** The belt cannot be on the bridge and on the
  iPhone's KS Fit at the same time.
- **The CoreS3 has no ESP toolchain on this Mac yet** — PlatformIO is installed in
  `~/tools/venv-ble`, but no project exists. `esptool` needs `--before usb-reset`
  on this board; the default reset mode fails.
- Kinomap requires a login, which only the owner performs.

---

## What is left to the builder

- Firmware structure, task layout, and how the two BLE roles are scheduled.
- How the screen is drawn and how ARM/STOP are hit-tested, within M5GFX.
- Exact `2ACC` feature bits beyond the required speed-target bit.
- How configuration (deadband, spacing, advertised name) is surfaced and stored.
- Reconnection and retry strategy for both links.
- Test strategy. Two cheap ones exist and should be used early:
  - `tools/blescan.py` on this Mac proves the bridge's GATT table before Kinomap
    is involved.
  - The `android-ble-app-lab` skill (on `wuerfel`) can run Kinomap against a
    Python fake peripheral, so FTMS acceptance can be exercised without the belt.

## Still open, and known to be open

- **The live GATT dump from the owner's belt has not been taken.** The WiLink
  details above are corroborated but not confirmed against this specific unit's
  firmware. Do this before trusting byte offsets for distance and steps.
- Whether Kinomap's brand path and generic path converge on the same handler.
- Kinomap end-to-end testing needs the owner signed in.
