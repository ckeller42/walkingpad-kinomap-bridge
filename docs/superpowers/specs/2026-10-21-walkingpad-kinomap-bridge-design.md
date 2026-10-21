# WalkingPad R1 Pro → Kinomap bridge — design

**Date:** 2026-10-21
**Status:** approved 2026-10-21

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
   **Amended 2026-10-21 after stage 1.** This holds only once a Kinomap session
   is already running. Kinomap shows *"Begin running to start the training…"*
   and sends **no** speed target until the treadmill reports movement — its
   session is started **by the belt**, not by the app. So the **first** start is
   always the user's, from the bridge's own START control or the belt's remote.
   The bridge must therefore be able to start a session by itself; it cannot
   depend on the app, and in simulated-belt mode there is no remote at all.
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

| Opcode | Meaning | Encoding | When |
|---|---|---|---|
| `00` | Request Control | — | immediately on discovering `2AD9` |
| `02 lo hi` | Set Target Speed | uint16 LE, **km/h × 100** | whenever the target changes |
| `07` | Start / Resume | — | **only** after we notify `2ADA` byte 0 = `0x04` |
| `08 01` / `08 02` | Stop / Pause | — | **never**, on this machine |

**Start and Stop are brand-gated, and this governs the whole design.**
`ag8.java` wraps the bodies of `onEquipmentStopped` (line 166),
`onEquipmentPaused` (line 138) and `onEquipmentResumed` (line 152) in a test for
the manufacturer string being `decathlon`. We are not Decathlon and will not
pretend to be (anti-goal). Therefore:

- **Kinomap never sends `08`.** Its generic treadmill stop is `stop()` at
  `ag8.java:183`, which calls `y0(1.0f)` — it **sets the target speed to
  1.00 km/h** and writes no stop opcode.
- **Kinomap never spontaneously sends `07`.** Line 398 sends `{07}` when it
  receives a `2ADA` Fitness Machine Status notification with byte 0 = `0x04`
  ("started or resumed by the user"). That is a handle we can pull deliberately.

**The only control channel is Set Target Speed.** Everything else the bridge needs
it must infer from speed targets, or provoke via `2ADA`.

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
7. **A target of exactly 1.00 km/h is a stop**, not a speed. Accepted cost: under
   Kinomap's control the belt cannot be walked at exactly 1.0 km/h; the slowest
   usable commanded speed is the next step up. (Chosen over forwarding it
   literally, and over a grace-window variant that only treats it as a stop after
   the speed has been higher.)
8. **`2AD4` advertises a maximum of 6.0 km/h** — the belt's walking-mode limit
   with the handrail folded. Chosen over 10.0 km/h (running mode, handrail
   raised), because with the handrail folded the belt silently caps itself at 6
   while Kinomap would believe it was at 10.
9. **Losing the phone link stops the belt but does not disarm the bridge.**
   Consistent with decision 5. The residual risk is recorded under *Known
   residual risks*.

## Decisions made for them, and flagged as such

These are the designer's calls, not the owner's. Each is a sensible default, and
each is cheap to change.

- **Deadband 0.3 km/h**, minimum write spacing **1 s** (the belt ignores anything
  under ~0.7 s; 1 s gives margin). Both configurable rather than compiled in.
- **Framework: Arduino + M5Unified + NimBLE-Arduino, built with PlatformIO.** The
  owner delegated this choice.

  *Board bring-up is **not** the reason.* M5GFX ships as an ESP-IDF component and
  lists CoreS3 among its supported devices, and M5Stack publishes a CoreS3 ESP-IDF
  BSP. Display, touch and PMIC are handled either way; this factor is a wash.

  *The actual reason:* the dual-role BLE code is the substance of this project,
  and the contract it must satisfy is exacting and fails silently (see the `2ACC`
  and `2AD4` traps above). NimBLE-Arduino's C++ API expresses a simultaneous GATT
  server + client with far less boilerplate than the raw NimBLE host API, and the
  body of open-source ESP32 FTMS bridges — nearly all Arduino/NimBLE-Arduino —
  gives working implementations to diff against. That attacks the project's
  largest risk directly.

  *Costs, stated plainly:*
  - Official PlatformIO lags Arduino-ESP32 3.x, so this means pointing `platform`
    at the **pioarduino** community fork rather than stock `espressif32`. A real
    third-party dependency.
  - It diverges from the owner's existing ESP-IDF CoreS3 project on `bar`,
    forfeiting the `flashing-cores3-on-bar` tooling — including its on-device
    screenshot capture, which would have been genuinely useful for a GUI project.

  *Reversibility:* Arduino-ESP32 3.x sits on ESP-IDF 5.x and IDF APIs stay
  callable. Switching later means rewriting the BLE and GUI glue, not the
  protocol logic.

  **If the owner weights toolchain consistency and the screenshot pipeline more
  heavily, ESP-IDF is a defensible choice and this decision should flip.**
- **NimBLE-Arduino** rather than Bluedroid — dual-role in roughly 40 KB RAM, and
  the simultaneous central + peripheral case is well-trodden there.
- The bridge presents itself with a **distinctive advertised name**, since the
  owner picks it from a list by name.
- **Minimal `2ACD` payload**: flags `0x0000`, instantaneous speed only. Justified
  by Kinomap's fragile flags parser and the fact that it derives distance itself.

---

## How it behaves

### Translating downward (Kinomap → belt)

Kinomap writes Set Target Speed (km/h × 100). Every target passes through this
pipeline, in order:

1. **Stop sentinel.** A target of exactly **1.00 km/h** is a stop request. Stop
   the belt and emit nothing further; do not treat it as a speed.
2. **Convert** to the belt's 0.1 km/h units.
3. **Deadband.** A target within **0.3 km/h** of the bridge's current reference
   speed is dropped — not queued. The stop sentinel bypasses this.
4. **Rate limit.** At most one belt command per second; if several targets arrive
   inside a window, the most recent wins and the rest are discarded.
5. **Start sequence.** If the belt is stopped, the bridge is **armed**, and the
   target is above the stop sentinel, send mode(manual) → start → speed. Then
   notify `2ADA` byte 0 = `0x04` so Kinomap starts its own training session.

**The deadband reference, and the handheld remote.** The reference is the last
speed the bridge *sent*, not the belt's reported speed — comparing against the
reported value would re-fire commands while the belt ramps. But the remote
remains the primary manual control, so a manual nudge makes that reference stale
and would wrongly suppress a genuinely different Kinomap target. Rule: if the
belt's **reported** speed differs from the last sent speed by more than the
deadband for **3 consecutive status polls**, treat it as a manual override and
resync the reference to the reported speed. *(Designer's call, not the owner's.)*

Every control-point write is answered with the FTMS success indication
`80 <opcode> 01`. See the arming rules for why the bridge does not return errors.

### Translating upward (belt → Kinomap)

The bridge polls the belt's status (silent, no beep) and republishes instantaneous
speed as a `2ACD` notification with flags `0x0000`. Elapsed time and distance from
the belt are shown on the CoreS3 screen but are not put in the notification.

### Arming, starting, stopping

Because Kinomap never sends Start or Stop opcodes, **arming gates speed-driven
starts**, not an opcode.

- The bridge is **disarmed** at power-on.
- Arming is a deliberate action on the CoreS3 screen, and **persists until tapped
  off** — not across reboots, and not on link loss.
- **While armed**, a Kinomap target above the stop sentinel will start a stopped
  belt via the start sequence.
- **But Kinomap cannot begin a session by itself.** Amended after stage 1: it
  waits for the treadmill to report movement before sending any target, so the
  bridge needs a **START** control of its own — available only while armed — that
  runs the start sequence. Without it the bridge and the app wait on each other
  forever, and in simulated-belt mode there is no remote to break the tie. START
  is how a session begins; Kinomap's targets take over once it is running.
- **While disarmed**, speed targets are **accepted and acknowledged with success,
  then ignored.** The belt does not start. Arming by itself does not start the
  belt either — the *next* target after arming does.
- **Stop always works**, armed or not: the 1.00 km/h sentinel from Kinomap, the
  on-screen STOP, or the belt's own remote.
- **Link loss stops the belt**, and the bridge stays armed. On reconnect the belt
  remains stopped until a fresh target above the sentinel arrives.
- Losing the **belt** link is shown on screen and reported to Kinomap by ceasing
  `2ACD` notifications.

**Why acknowledge-and-ignore rather than return an FTMS error.** An earlier draft
refused writes while disarmed, on the reasoning that Kinomap's fallback path
would handle it. The decompile says otherwise: a failed Request Control is
re-sent unconditionally (`ag8.java:311`), and a failed `07` sets a retry flag,
sends `08 01`, then re-sends `07` on the stop's success (`ag8.java:323, 341–344`)
— an unbounded loop. Since every belt command that *is* accepted beeps, returning
errors buys a beep storm and a stuck handshake. *(Designer's call, corrected
after review.)*

### The screen (layout A)

Top: two link indicators, belt and phone. Centre: instantaneous speed, very large.
Below: distance and elapsed time, small. Bottom bar: ARM (showing the current
state) and STOP, side by side, each half the width.

---

## Bring-up and test ladder

The two protocols are independent, and the dangerous one is the belt. So they are
brought up **separately**, in this order, and only combined at the end. Each stage
has to pass before the next begins; a failure is then attributable to one protocol
rather than to the pair.

A **simulated belt** is a runtime mode of the real firmware, not a separate build —
the same binary runs stages 1 and 3, so stage 1 is testing the shipping code.

### Stage 0 — GATT self-check, Mac only, no phone and no belt

Flash the FTMS peripheral with the simulated belt behind it. From this Mac:

    ~/tools/venv-ble/bin/python tools/blescan.py scan 10
    ~/tools/venv-ble/bin/python tools/blescan.py gatt "<bridge name>"

Pass criteria, all checkable without Kinomap:

- advertises service `1826` under the distinctive name
- `2ACC` is **≥ 8 bytes** and bit 0 of bytes 4–7 (uint32 LE) is **set**
- `2AD4` present and **6 bytes**: min `0.50`, max **`6.00`**, increment `0.10`,
  each uint16 LE in 0.01 km/h units (Kinomap reads only min and max; the
  increment field merely has to be there to make six bytes)
- `2AD5` **absent**, and no inclination bit in `2ACC`
- `2AD9` has **write and indicate**; `2ACD` has **notify**
- `180A` present, manufacturer **not** the string `decathlon`

### Stage 1 — real Kinomap against real CoreS3, dummy belt ★

**The key test, and the one that retires most of the project's risk.** Real app,
real hardware, real radio — with nothing that can move. Failures here cost nothing.

Setup: CoreS3 flashed and running the simulated belt. No R1 Pro involved and none
required. Kinomap on the Fairphone, driven over ADB.

Pass criteria:

1. The bridge appears in **FOUND EQUIPMENT** by name via "I can't find my brand".
2. On connect, Kinomap writes `00` and the **Request Control handshake completes**
   — our `80 00 01` indication is accepted and it does not retry.
3. Kinomap reads `2ACC` and `2AD4`, and subsequently **does send `02 lo hi`**.
   (If it never does, trap 1 or trap 2 is in play.)
4. Target speeds arrive as **km/h × 100** and the simulated belt follows them.
5. Kinomap's on-screen speed tracks our `2ACD` notifications, and its **distance
   advances** — confirming its 1 Hz integration is fed correctly.
6. **Deadband and rate limiting** visibly suppress commands: a slowly-ramping
   Kinomap target produces far fewer belt commands than notifications.
7. **Arming:** disarmed, speed targets are acknowledged with success and the
   simulated belt stays stopped, with no retry loop from Kinomap. Armed, the
   next target above the sentinel starts it.
8. **`2ADA` handshake:** on the simulated start, the bridge notifies `2ADA`
   byte 0 = `0x04` and Kinomap responds by writing `{07}` — confirming its
   training session started from the belt rather than the other way round.
9. **Stop sentinel:** ending the workout in Kinomap produces a target of exactly
   `1.00` km/h and the simulated belt stops. Confirm no `08` ever arrives.
10. **Speed clamp:** a Kinomap target above 6.0 km/h never reaches the bridge —
    it is clamped app-side by our advertised `2AD4` max.
11. **Link loss** — kill Bluetooth on the phone mid-run; the simulated belt stops
    and the bridge stays armed. On reconnect it does not restart by itself.

### Stage 2 — real belt, no Kinomap

CoreS3 as central only, against the R1 Pro, driven from the CoreS3 screen. Owner
standing clear of the belt.

Pass criteria: WiLink framing and CRC accepted; the start sequence (mode → start →
speed) works from a stopped belt; commands closer than ~0.7 s are confirmed to be
dropped; status frames decode to plausible speed, distance and time; the observed
beep rate matches the deadband and spacing settings.

This stage is also where the **live GATT dump** of the belt gets taken, closing the
open item below.

### Stage 3 — full loop, owner on the belt

Both links live, simulated belt off. Only after stages 1 and 2 pass. First run
starts disarmed, with a deliberate STOP exercised before anything else.

### Optional — Kinomap against a software fake, no hardware at all

The `android-ble-app-lab` skill (on `wuerfel`) bridges an emulator's Bluetooth to
a Python (Bumble) peripheral. Useful for walking Kinomap through error paths that
are awkward to produce on real hardware — refused control, malformed `2ACC`,
out-of-range targets. Not a substitute for stage 1, because it does not exercise
the real radio or the real firmware.

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

- **BLOCKING — the WiLink frame that stops the belt is not known.** The documented
  command set covers request-status, set speed, set mode, start, request session
  and set preference, but **no stop**. Every safety guarantee in this document
  rests on it: the on-screen STOP, the link-loss stop, and the 1.00 km/h
  sentinel. The plausible candidates behave differently — `F7 A2 04 00 …` by
  analogy with start, a speed of 0, or mode 2 (standby, which may drop the BLE
  link or need a wake sequence before the next start). **The belt ignores
  unrecognised commands silently**, so a wrong guess means the primary safety
  feature does nothing and nothing reports it. Capture this in stage 2 before any
  firmware depends on it.
- **The live GATT dump from the owner's belt has not been taken.** The WiLink
  details above are corroborated but not confirmed against this specific unit's
  firmware. Do this before trusting byte offsets for distance and steps.
- Whether Kinomap's brand path and generic path converge on the same handler.
- Kinomap end-to-end testing needs the owner signed in.

## Known residual risks

Accepted deliberately, recorded so nobody rediscovers them as surprises.

- **Armed through a dropped link.** The owner chose armed-until-tapped-off. If
  Kinomap reconnects by itself and sends a speed target, the belt starts while
  the owner may not be expecting it. Mitigated only by the belt staying stopped
  until a *fresh* target arrives after reconnect.
- **1.0 km/h is unreachable** under Kinomap's control, by construction.
- **The handrail position is invisible to the bridge.** The R1 Pro is a 2-in-1:
  handrail folded it is a walking pad limited to 6.0 km/h, handrail raised it is
  a treadmill good for 10.0 km/h. The bridge has no way to know which it is —
  nothing in the documented WiLink status frame reports handrail state, and
  `2AD4` is read **once** by Kinomap at connect, so the ceiling cannot be changed
  mid-session even if we could detect it.

  Consequences, both live:
  - With the handrail **raised**, the 6.0 km/h ceiling under-drives the belt.
    Running-mode routes are paced slower than the video intends and there is no
    in-session way to lift the cap — it needs a bridge setting and a reconnect.
  - If the ceiling were ever raised to 10.0 for running, then with the handrail
    **folded** the belt would silently cap itself at 6 while Kinomap believed it
    was at 10. Its pace, distance and training metrics would all drift from
    reality with no error shown. This is the reason 6.0 was chosen.

  **Worth investigating during stage 2:** whether the belt state or mode byte in
  the `F8 A2` status frame encodes handrail position. If it does, the bridge
  could at least *warn* on screen when the advertised ceiling and the physical
  configuration disagree — it still could not change `2AD4` mid-session, but a
  mismatch would stop being silent.
- **Kinomap's `2ACD` flags parser is buggy** (`×255`, big-endian). The minimal
  payload sidesteps it, but any future addition of an optional field must be
  re-verified against `ag8.java:z()` rather than against the FTMS spec.
