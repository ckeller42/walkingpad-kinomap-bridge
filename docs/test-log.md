# Test log

## Stage 2 (partial) — live belt GATT capture, 2026-10-21

Taken from this Mac with `tools/blescan.py` while the belt was in pairing mode.
No firmware involved; no commands were written to the belt.

### Advertisement

| | |
|---|---|
| Advertised name | **`RE`** |
| CoreBluetooth handle (this Mac) | `202FB137-0854-BDD4-C086-789C12A093CA` |
| RSSI at capture | −53 dBm |
| Advertised service | `0000fe00-0000-1000-8000-00805f9b34fb` |

### GATT table

```
service 0000180a   Device Information
   char 00002a25   read                      Serial Number String
   char 00002a26   read                      Firmware Revision String
   char 00002a28   read                      Software Revision String
   char 00002a24   read                      Model Number String
   char 00002a29   read                      Manufacturer Name String
service 0000fe00   Vendor specific
   char 0000fe01   notify, read              status
       desc 00002902 (CCCD)
   char 0000fe02   write-without-response    commands
service 00010203-0405-0607-0809-0a0b0c0d1912   (OTA)
   char 00010203-0405-0607-0809-0a0b0c0d2b12   read, write-without-response
       desc 00002901
```

### Device Information strings as read

| Characteristic | Value |
|---|---|
| Firmware Revision (`2A26`) | `M26_V187.1.2` |
| Model Number (`2A24`) | `WLT8266M` |
| Serial Number (`2A25`) | `"Serial Number"` |
| Software Revision (`2A28`) | `"Hardware Revision"` |
| Manufacturer Name (`2A29`) | `"Software Revision"` |

### What this confirms, and what it changes

**Confirms** — `FE00` / `FE01` / `FE02` exist on this unit with the roles the
design assumed, so the WiLink protocol reference is correct about the transport.
There is **no FTMS (`1826`) service**: the belt is WiLink-only, as expected.

**Changes the plan — `FE02` is `write-without-response`.** Task 14's `BeltLink`
must write without response and will get **no acknowledgement and no flow
control** from the belt. Combined with the belt silently dropping commands sent
less than ~0.7 s apart, this means the bridge cannot know a command landed except
by polling `FE01` and watching the speed change. The existing rate limit is the
only backpressure there is.

**Changes the plan — the belt does not push status.** Subscribing to `FE01` and
waiting 10 s yielded **zero** unsolicited notifications. Status must be
*requested* (`F7 A2 00 00 A2 FD`) and the reply read from `FE01`. The 1 Hz poll
in the design is therefore required, not merely a convenience.

**Worth knowing** — three of the five Device Information strings are mis-wired in
the belt's own firmware: Serial Number returns the literal text "Serial Number",
Software Revision returns "Hardware Revision", and Manufacturer Name returns
"Software Revision". Only Firmware Revision and Model Number carry real values,
and `WLT8266M` identifies the Bluetooth module rather than the treadmill. Do not
rely on any of these to identify the machine.

## Stage 2 (partial) — active FE02 probe for the stop frame, 2026-10-21

Owner authorised an active probe with nobody on the belt. Three candidates were
written to `FE02`, each from its own start, with status polled throughout.

### Result — the stop frame is `F7 A2 02 02 A6 FD` (set mode = standby)

Confirmed against a **moving** belt, not just during the countdown:

```
belt running at 2.8 km/h
  send F7 A2 02 02 A6 FD
  t+1.2s   state=1 speed=2.3 mode=2
  t+2.4s   state=4 speed=0.4 mode=2
  t+3.6s   state=4 speed=0.0 mode=2
```

Negative results, both useful:

| Candidate | Frame | Effect |
|---|---|---|
| start-with-0 | `F7 A2 04 00 A6 FD` | **No stop.** Belt carried on ramping to 4.0 km/h. |
| set-speed-0 | `F7 A2 01 00 A3 FD` | **No stop.** Belt carried on ramping to 4.0 km/h. |
| mode-standby | `F7 A2 02 02 A6 FD` | **Stops.** Decelerates to 0 and parks in standby. |

### The belt's state machine, as observed

| `state` | Meaning |
|---|---|
| 0 | idle |
| 7 | countdown, immediately after start |
| 6 | starting / transitioning |
| 1 | running |
| 4 | standby (stopped by the standby command) |

`mode`: 1 = manual, 2 = standby.

### Three consequences for the bridge

1. **A stop leaves the belt in `mode 2` (standby), not idle.** It will not start
   again until mode is set back to manual. The design's start sequence
   (mode manual → start → speed) already does this first, so it is correct — but
   now it is known to be *necessary*, not merely tidy.
2. **The deceleration ramp is strictly decreasing**: 2.8 → 2.3 → 0.4 → 0.0 over
   about 3.6 s. This directly validates Task 6's ramp-down discriminator, which
   rejects a run containing any decrease. A real post-stop ramp will never be
   misread as a remote start.
3. **Setting speed to 0 is not a stop.** Anything that wants the belt stopped
   must send the standby frame.

### Status frame offsets — validated against real data

```
f8 a2 00 00 01 00 00 0b 00 00 00 00 00 02 0a 00 00 00 ba fd
      |  |  |  \_____/  \_____/  \_____/  |
      |  |  |   time=11  dist=0   steps=2  preset speed = 0x0a (1.0 km/h)
      |  |  mode
      |  speed
      state
```

Byte 2 state, byte 3 speed×10, byte 4 mode, bytes 5–7 elapsed seconds (BE24),
bytes 8–10 distance, bytes 11–13 steps — **exactly the layout the plan assumed.**
Frames are 20 bytes with a trailing CRC and `FD`, so Task 3's `len >= 14` check
is right and an `== 14` check would have rejected every real frame.

Byte 14 carries the preset/target speed, which the plan did not know about.

### Honest note on how the probe went

The probe was written to hold the belt at 1.0 km/h. It did not: the speed command
sent during the countdown was ignored, and the belt used its own preset, reaching
**4.0 km/h** unattended in the first two cycles. Nobody was on it and it was
stopped each time, but the limit intended was not the limit achieved. A future
probe should set the speed *after* the belt reaches `state 1`, not during the
countdown, and verify it took before letting the belt run.

### Still outstanding from Stage 2

- Distance and steps are validated only at near-zero values; a real walk is
  needed to confirm their units (10 m for distance is still an assumption).

## Stage 0 — GATT self-check, 2026-10-21

Firmware commit `df1a01b`, flashed to the CoreS3 (819,120 bytes, hash verified).
Read over the air from this Mac with `tools/blescan.py` plus a one-off value read.
Belt source: **simulated**. No treadmill involved.

### Advertisement

Advertises as **`WalkingPad Bridge`** with service `00001826` (FTMS), −43 dBm.

### GATT table

```
service 00001826   Fitness Machine
   char 00002acc   read              Fitness Machine Feature
   char 00002ad4   read              Supported Speed Range
   char 00002acd   notify            Treadmill Data
   char 00002ad9   indicate, write   Fitness Machine Control Point
   char 00002ada   notify            Fitness Machine Status
service 0000180a   Device Information
   char 00002a29   read              Manufacturer Name String
   char 00002a24   read              Model Number String
```

### Pass criteria

| Criterion | Result |
|---|---|
| Advertises `1826` under the advertised name | PASS |
| `2ACC` present | PASS |
| `2AD4` present | PASS |
| `2ACD` has notify | PASS |
| `2AD9` has write **and** indicate | PASS |
| `2ADA` present | PASS |
| `180A` present | PASS |
| **`2AD5` ABSENT** | PASS |

### Characteristic values — the two silent-failure traps

```
2ACC : 00 00 00 00 01 00 00 00   (8 bytes)
       target features = 0x00000001
       bit0 Speed Target Setting   SET     PASS
       bit1 Inclination            CLEAR   PASS
       bit2 Resistance             CLEAR   PASS

2AD4 : 32 00 58 02 0a 00          (6 bytes)
       min 0.50 / max 6.00 / step 0.10 km/h   PASS

2A29 : 'WalkingPad Bridge'  (not 'decathlon')  PASS
2A24 : 'R1 Pro FTMS Bridge'
```

Both traps identified in Kinomap's decompile are closed on real hardware: a
`2ACC` shorter than 8 bytes or without bit 0 would have meant Kinomap connects,
shows data, and never commands a speed; a missing `2AD4` would have clamped
every target to 0 km/h. Neither fails visibly — which is why they are checked
here rather than discovered during a workout.

**Stage 0: PASS.**

## Stage 1 — real Kinomap, real CoreS3, simulated belt, 2026-10-21

Firmware `12f4d2d` (`cores3_sim`). Kinomap 4.7.1 on the Fairphone. Driven via the
bridge's serial console (`tools/console.py`) and ADB. No treadmill involved.

### Results

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 1 | Appears in FOUND EQUIPMENT by name | **PASS** | listed as `(FTMS) WALKINGPAD BRIDGE`, MAC `44:1B:F6:E1:F5:89` — Kinomap recognised the Fitness Machine Service from the advertisement and tagged it |
| 2 | Request Control handshake completes, no retries | **PASS** | `control op 0x00` logged once per connection; no repeats, so our `80 00 01` indication was accepted |
| 3 | Kinomap sends `02 lo hi` Set Target Speed | **NOT OBSERVED** | `target=0` throughout a 60 s session in ORIGINAL SPEED mode — see below |
| 4 | Targets arrive as km/h × 100 | **untested** | blocked on 3 |
| 5 | Kinomap's speed tracks our `2ACD`; distance advances | **PASS** | belt at 2.0 km/h → Kinomap displayed **`29:59 /km`** (= exactly 2.0 km/h) and distance climbed to 139 m |
| 6 | Deadband and rate limiting suppress commands | **untested** | blocked on 3 |
| 7 | Arming gate | **PASS** | `arm` then `start` ran the start sequence; a `start` while disarmed is refused |
| 8 | `2ADA 0x04` → Kinomap answers `{07}` | **PASS** | `notify started (2ADA 04), phone=1` immediately followed by `control op 0x07` |
| 9 | Stop sentinel (1.00 km/h) stops the belt | **untested** | blocked on 3 |
| 10 | Targets above 6.0 km/h clamped app-side | **untested** | blocked on 3 |
| 11 | Link loss stops the belt, bridge stays armed | **untested** | |

### The session genuinely ran

Video playing with no pause overlay, `0:15.25` elapsed, 19% of the route, 139 m
covered, 40 watts, and Kinomap's own pacing indicator (`→ 4s`) live. The whole
start handshake works end to end:

```
Kinomap → 0x00 Request Control
bridge  → arm, start, belt reaches 2.0 km/h
bridge  → 2ADA 0x04  "started"
Kinomap → 0x07 Start/Resume, session begins
bridge  → 2ACD speed notifications, Kinomap displays 29:59 /km
```

### Open question: Kinomap sent no speed target

In **ORIGINAL SPEED** mode ("depending on the pace of the contributor") Kinomap
played the video at the contributor's pace and showed how far ahead or behind the
user was (`→ 4s`), but **never wrote `02`**. It did write one unrecognised opcode
— logged as `control op 0xff`, our mapping for Unknown — which on a route with a
**−5.4 % gradient** is almost certainly `0x03` Set Target Inclination. Kinomap's
`uf8.receive` sends that on gradient change **without gating on a feature bit**,
exactly as the decompile predicted, and the bridge correctly acknowledges and
ignores it.

Two candidate explanations, to be distinguished next:

1. ORIGINAL SPEED does not drive the machine at all — the user is meant to match
   the video, and the pacing arrow is the whole feedback mechanism. Speed control
   would then live in **Coaching** or **Structured workout** sessions instead.
2. Kinomap only commands speed once some other condition is met.

**Diagnostic gap found:** the firmware logs the *mapped* opcode (`0xff` for
Unknown) rather than `req.rawOp`, so the actual byte Kinomap wrote is lost. That
should be logged — it is the difference between knowing and inferring.

## Stage 1 (continued) — belt control proven; Kinomap speed command disproven

Firmware `df64496` (`cores3_sim`), which logs raw control-point opcodes and counts
them per opcode (`cp` console command).

### Belt control works, end to end

Commanded from the bridge console with an active Kinomap workout running:

| Commanded | Bridge `status` | Kinomap displayed |
|---|---|---|
| `speed 4.5` | `speed=45` | — |
| `speed 5.0` | `speed=50` | **`11:59 /km`** = 5.0 km/h, 101 watts, 47 m |
| (earlier) 2.0 | `speed=20` | **`29:59 /km`** = 2.0 km/h, 139 m |

So the whole data path is proven on hardware: bridge commands → belt runs →
`F8 A2` status → `2ACD` notification → Kinomap decodes and displays the correct
speed, and integrates it into distance.

### Kinomap does NOT send Set Target Speed in video ORIGINAL SPEED mode

After a full session with the belt running and its speed changed twice, the
per-opcode counters read:

```
cp 00=1  03=2  07=1  empty=0
```

- `00` Request Control — once, at connect
- `07` Start/Resume — once, in reply to our `2ADA 0x04`
- `03` Set Target Inclination — twice
- **`02` Set Target Speed — never**

This is counted evidence, not inference. The earlier `control op 0xff` is now
confirmed to have been `0x03`.

The inclination payload is itself informative: `cp op=0x03 len=3 payload=00 00`
— a target of **0.0 %** on a route displaying **−5.4 %**. That is Kinomap's own
clamp behaving exactly as the decompile predicted: `ag8.w0()` clamps the target
against the maximum read from `2AD5`, and the bridge deliberately does not expose
`2AD5`, so that maximum is zero. The bridge acknowledges and ignores it.

### What this means

In **ORIGINAL SPEED** video mode Kinomap plays the route at the contributor's pace
and shows the user how far ahead or behind they are (`→ 238s`). It does not drive
the treadmill's speed. The bridge is doing everything correctly; the app is simply
not commanding.

Kinomap's **Structured workout** tab exists and its featured workout is
**"Mode: INC — Incline-based training"**, with the mode a fixed property of each
workout rather than a user choice. A speed-mode workout, or a Coaching session,
remains the place where `02` would be expected — still to be tested.

**Consequence for the project goal.** "Kinomap controls the belt" may hold only in
specific workout types. The bridge's own speed controls (`+`/`−` on the screen,
`speed` on the console) make it usable regardless, which is why they were added.

## Stage 2/3 — the real belt, and the loop closed

`USE_SIM_BELT=0` (`cores3`), the real WalkingPad R1 Pro, Kinomap 4.7.1 on the
Fairphone. Nobody on the belt for any of it.

### The loop runs end to end

One session, no hands on anything:

```
> status   cp op=0x00            <- Kinomap reconnected by itself
  armed=0 running=0 belt_link=1 phone_link=1 belt_state=0 speed=0  src=real
> arm
> start    notify started (2ADA 04), phone=1
> status   cp op=0x07            <- Kinomap accepted the start
  armed=1 running=1 belt_link=1 phone_link=1 belt_state=1 speed=4
> status   cp op=0x03 x2
  armed=1 running=1 belt_link=1 phone_link=1 belt_state=1 speed=20
```

and Kinomap, at that moment, displayed **`29:59 /km` = exactly 2.0 km/h**, with
the timer running, 40 watts, distance accumulating and the video playing. The
`cp op=0x03` writes only happen once its session is live, so they are themselves
evidence that it started.

Bridge -> real belt -> `F8 A2` status -> `2ACD` -> Kinomap. Proven on hardware.

### What actually controls what

**Kinomap is passive.** It waits for the machine to report motion before it
starts its session ("Begin running to start the training…"), and it only ever
writes `0x07` in *reply* to our `2ADA 0x04`. It never initiates, and it never
writes `0x02` Set Target Speed in any video mode. The in-workout settings panel
is seven video/display toggles with no speed control on it.

So the division of labour is: **the bridge drives the belt** (its own `+`/`-`
buttons, STOP, and the console) and **Kinomap reads it**. That is the whole
system, and it works. `PHONE_START_CENTI_KMH` exists for FTMS apps that do drive
the machine, and is 0 (off) by default - see `src/config.h` for why.

### Start belt is now measured, not inherited

`F7 A2 04 01 A7 FD` had been carried since the design doc with provenance
`community`: the A1/C1 command set, never checked on an R1 Pro. A direct FE02
probe confirms it, from standby and from idle, at mode-to-start gaps of 1.0,
1.5, 2.0 and 3.0 s - **every** gap started the belt, so the pacing interval is
not load-bearing. Two undocumented states turned up on the way: **9** between
start and countdown, and **3** leaving standby. Both are now in `wilink.yaml`.

### Two traps worth keeping

1. **The belt auto-stops when nobody walks on it**, about 15 s after starting,
   returning to state 0 / speed 0. The window varied and got shorter on repeated
   starts. Every "the bridge failed to start the belt" result earlier in this
   session was this: the belt had started, run, and stopped again before a
   status read 35 s later caught it.

2. **The console displays the setpoint, not motion.** The camera read a clear
   `2.0` and later `3.5` while the status stream said `state=0 speed=0`. A photo
   of the display is *not* proof the belt is moving; only the state is.

Trap 1 is why the camera cross-check needs to fire inside a ~15 s window, and
trap 2 is why the cross-check needs the status stream next to the photo rather
than the photo alone.

## Stage 3 — a real walk, with a walker on the belt

`PHONE_START_CENTI_KMH=200`, bridge armed, real belt, Kinomap 4.7.1, the project
owner walking. Sampled by screenshotting the phone over ADB only: the serial
console was not opened once, because opening it reboots the bridge and would
have dropped both the arming and Kinomap's link.

| Elapsed | Kinomap pace | km/h | Watts | Distance | Note |
|---|---|---|---|---|---|
| 0:00:20 | 29:59 /km | 2.0 | 40 | 10 m | started from the ESP |
| 0:02:47 | 29:59 /km | 2.0 | 40 | 91 m | |
| 0:05:07 | 29:59 /km | 2.0 | 40 | 169 m | |
| 0:06:38 | 20:00 /km | 3.0 | 60 | 227 m | **`+` on the ESP** |
| 0:07:28 | 40:00 /km | 1.5 | 30 | 254 m | **`-` on the ESP** |
| 0:08:17 | — | 0 | 0 | 271 m | stopped; 33% of route, 18 kcal |

### What this settles

1. **Speed control works under load.** 2.0 -> 3.0 -> 1.5 km/h from the ESP's
   `+`/`-` buttons, each tracked live by Kinomap, with watts scaling sensibly
   (40 -> 60 -> 30). This is the first test of the speed path with a person on
   the belt.

2. **The auto-stop is the unattended interlock, nothing else.** Eight minutes of
   continuous running with weight on the belt. Every truncated run earlier was
   this interlock, not a bridge fault.

3. **The distance integration is real.** 169 m at 0:05:07 against 170.6 m
   predicted for 307 s at 2.0 km/h. Kinomap is integrating the bridge's measured
   speed, not estimating from the route.

4. **Kinomap never wrote `0x02` Set Target Speed**, across a full eight-minute
   session with a walker aboard and three speed changes. Taken with the counted
   `00=1 03=2 07=1` from the earlier session, treat this as settled for video
   modes in 4.7.1. `PHONE_START_CENTI_KMH` never fired either, consistent with
   Kinomap never initiating `0x07`.

### The shape of the finished system

The user and the bridge drive the belt; Kinomap reads and records it. Kinomap
supplies the route, the video, the gradient display and the session record, and
takes its speed, distance and power from the bridge. It does not drive the
machine, and no setting in it makes it do so.
