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
