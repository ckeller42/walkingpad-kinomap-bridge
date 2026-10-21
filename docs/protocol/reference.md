> **GENERATED** from `protocol/wilink.yaml` and `protocol/ftms.yaml` by `tools/gen_protocol_reference.py` — do not hand-edit; see [README.md](README.md) for how to regenerate.

# Protocol reference

Every fact below carries a provenance. Where this and `docs/test-log.md` differ, the test log wins.

> **DO NOT SEND group `0xE8` (OTA / firmware update) — can brick the belt.**
> Never send any frame whose group byte is 0xE8, under any circumstance, including probing, fuzzing and "just reading". The bridge firmware must not be able to emit one. Groups other than 0xA2 are unexplored; do not probe them on a belt you want to keep.
> Provenance: community (project owner's instruction; no capture exists and none should be taken).

| Provenance | Meaning |
|---|---|
| measured | observed on real hardware; `docs/test-log.md` is the record |
| community | from the reverse-engineering projects or the design doc, not independently captured here |
| vendor-app | from the vendor-app decompiles (KS Fit, Kinomap) |

## WiLink — the treadmill side

KingSmith's legacy vendor protocol over service FE00. Commands are written to FE02 without response; status is read from FE01. The belt never pushes status, so every reading must be requested. The bridge is the BLE central; the belt is the peripheral.

### Transport

| UUID | Role | Properties | Provenance | Evidence |
|---|---|---|---|---|
| FE00 | service |  | measured | test-log: Stage 2, GATT table |
| FE01 | status | notify, read | measured | test-log: Stage 2, GATT table |
| FE02 | commands | write-without-response | measured | test-log: Stage 2, GATT table |

- `FE01` (status): Carries the 20-byte status frame. Subscribing for 10 s without sending a request yielded zero notifications.
- `FE02` (commands): No acknowledgement and no flow control. The only way to know a command landed is to poll FE01 and watch the state change.
- `180A` (Device Information), measured: Mis-wired in the belt firmware. Serial Number (2A25) returns the text "Serial Number", Software Revision (2A28) returns "Hardware Revision", Manufacturer Name (2A29) returns "Software Revision". Only Firmware Revision (2A26 = M26_V187.1.2) and Model Number (2A24 = WLT8266M, the Bluetooth module) carry real values. Do not use any of them to identify the machine.
- `00010203-0405-0607-0809-0a0b0c0d1912` (OTA), measured: Characteristic 00010203-0405-0607-0809-0a0b0c0d2b12 (read, write-without-response). Not used by the bridge. See the OTA prohibition at the top of the reference.

### Framing

Command frames are six bytes: `F7 <group> <function> <argument> <crc> FD`. Byte-level tables are in [frame-layouts.md](frame-layouts.md).

**CRC** (community): Low byte of the sum of every byte between the header byte and the CRC byte: group, function and argument for a command; bytes 1 to 17 for a status frame.

### Commands

Example frames carry a CRC computed by the generator from the group, function and argument.

| Command | Group | Function | Argument | Example frame | Provenance | Evidence |
|---|---|---|---|---|---|---|
| Request status | 0xA2 | 0x00 | fixed `0x00` | `F7 A2 00 00 A2 FD` request status | measured | test-log: Stage 2, the belt does not push status |
| Set speed | 0xA2 | 0x01 | `speed` (× 0.1 km/h) | `F7 A2 01 28 CB FD` 4.0 km/h<br>`F7 A2 01 1C BF FD` 2.8 km/h<br>`F7 A2 01 00 A3 FD` 0 (NOT a stop) | community | design doc: Established facts |
| Set mode | 0xA2 | 0x02 | `mode`, enum `mode` | `F7 A2 02 01 A5 FD` manual | community | design doc: Established facts; modes 1 and 2 also measured |
| Start belt | 0xA2 | 0x04 | fixed `0x01` | `F7 A2 04 01 A7 FD` start | measured | test-log: Stage 2, direct FE02 probe and via the bridge |
| Stop belt (set mode standby) | 0xA2 | 0x02 | fixed `0x02` | `F7 A2 02 02 A6 FD` stop | measured | test-log: Stage 2, active FE02 probe, confirmed on a moving belt |

- **Request status**: Ask the belt for one status frame; the reply arrives on FE01. Silent. Needed for every reading; the bridge polls at 1 Hz.
- **Set speed**: Set the target belt speed. Discarded during the countdown (state 7); the belt then uses its own preset. A speed of 0 is not a stop.
- **Set mode**: Select the belt mode. Mode 0 (auto) is never produced by the bridge.
- **Start belt**: Start the belt (countdown, then run at the belt's preset speed). Sent after mode manual. The variant with argument 0 (F7 A2 04 00 A6 FD) is not a stop; see the constraints. Confirmed on an R1 Pro from standby and from idle, at mode-to-start gaps of 1.0, 1.5, 2.0 and 3.0 s - every gap started the belt, so the pacing interval is not load-bearing here. The belt ramps to its own preset speed (~1.1 km/h) unless a speed is set.
- **Stop belt (set mode standby)**: The only stop. Sets mode standby; the belt decelerates to 0 and parks in state 4. Same wire format as Set mode with mode 2. Leaves the belt in standby, so it will not start again until mode is set back to manual.

### Enumerations

`state` — status byte 2.

| Value | Name | Meaning | Provenance | Evidence |
|---|---|---|---|---|
| 0x00 | idle |  | measured | test-log: Stage 2, state machine |
| 0x03 | waking | Seen briefly on the way out of standby | measured | test-log: Stage 2, start probe |
| 0x09 | pre_countdown | Seen between start and countdown | measured | test-log: Stage 2, start probe |
| 0x07 | countdown | Countdown immediately after start | measured | test-log: Stage 2, state machine |
| 0x06 | starting | Starting / transitioning | measured | test-log: Stage 2, state machine |
| 0x01 | running |  | measured | test-log: Stage 2, state machine |
| 0x04 | standby | Stopped by the standby command | measured | test-log: Stage 2, state machine |

`mode` — status byte 4; Set mode argument.

| Value | Name | Meaning | Provenance | Evidence |
|---|---|---|---|---|
| 0x00 | auto | Settable; never seen in a status frame | community | design doc: Established facts |
| 0x01 | manual |  | measured | test-log: Stage 2, state machine |
| 0x02 | standby |  | measured | test-log: Stage 2, state machine |

### Status frame

20 bytes, received on `FE01` in reply to Request status. Provenance: measured (test-log: Stage 2, status frame offsets validated against real data). Field offsets are in [frame-layouts.md](frame-layouts.md).

### Behavioural constraints

| Key | Value | Statement | Provenance | Evidence |
|---|---|---|---|---|
| `unattended_auto_stop_s` | 15 s | With nobody walking on it the belt stops by itself a short time after starting, returning to state 0 with speed 0. | measured | test-log: Stage 2, observed on every unattended start; the window varied and shortened on repeated starts |
| `display_shows_setpoint` | yes | The belt's console shows the commanded speed even while the belt is not moving, so the display alone does not prove motion. | measured | test-log: Stage 2, camera read 2.0 and 3.5 while status reported state 0 speed 0 |
| `unsolicited_notifications` | no | The belt sends no unsolicited notifications on FE01; every status reading must be requested. | measured | test-log: Stage 2, a 10 s subscription yielded zero notifications |
| `min_command_interval_ms` | 700 ms | Commands closer together than this are silently ignored. | community | design doc: Established facts; the test log cites it (~0.7 s) but records no measurement of it |
| `set_speed_honoured_in_countdown` | no | A Set speed sent during the countdown (state 7) is discarded; the belt uses its own preset and reached 4.0 km/h. | measured | test-log: Stage 2, honest note |
| `belt_preset_after_start_kmh` | 4.0 km/h | Left alone after a start, the belt ramps to its own preset speed. | measured | test-log: Stage 2, honest note |
| `set_speed_zero_stops_belt` | no | Set speed 0 does not stop the belt; it carried on ramping to 4.0 km/h. | measured | test-log: Stage 2, set-speed-0 candidate |
| `start_argument_zero_stops_belt` | no | Start with argument 0 (F7 A2 04 00 A6 FD) does not stop the belt. | measured | test-log: Stage 2, start-with-0 candidate |
| `restart_requires_mode_manual` | yes | After a stop the belt is in standby (mode 2) and will not start until mode is set back to manual. | measured | test-log: Stage 2, consequence 1 |
| `stop_ramp_strictly_decreasing` | yes | The post-stop deceleration ramp is strictly decreasing (2.8, 2.3, 0.4, 0.0 km/h over about 3.6 s). | measured | test-log: Stage 2, consequence 2 |
| `set_speed_after_state` | 1 | To hold a speed, send Set speed only once the belt reports state 1 (running), then verify it took. | measured | test-log: Stage 2, honest note |

## FTMS — the Kinomap side

Fitness Machine Service 1826, minimal subset. Kinomap's only control channel is Set Target Speed; Start and Stop are brand-gated in the app and are never sent by it. See the constraints. The bridge is the BLE peripheral; Kinomap is the central.

Advertised as `WalkingPad Bridge` with service `1826` (measured: test-log: Stage 0, advertisement).

### Characteristics

| UUID | Name | Properties | Value | Provenance | Evidence |
|---|---|---|---|---|---|
| 2ACC | Fitness Machine Feature | read |  | measured | test-log: Stage 0, characteristic values |
| 2AD4 | Supported Speed Range | read |  | measured | test-log: Stage 0, characteristic values |
| 2ACD | Treadmill Data | notify |  | measured | test-log: Stage 0, GATT table (properties); payload layout is design, not yet read back |
| 2AD9 | Fitness Machine Control Point | write, indicate |  | measured | test-log: Stage 0, GATT table |
| 2ADA | Fitness Machine Status | notify |  | measured | test-log: Stage 0, GATT table |
| 2A29 | Manufacturer Name String (Device Information 180A) | read | `WalkingPad Bridge` | measured | test-log: Stage 0, characteristic values |
| 2A24 | Model Number String (Device Information 180A) | read | `R1 Pro FTMS Bridge` | measured | test-log: Stage 0, characteristic values |

- `2ACC`: Trap: shorter than 8 bytes, or bit 0 of the Target Setting Features clear, and Kinomap connects, shows data and never commands a speed.
- `2AD4`: Trap: if absent, Kinomap clamps every target speed to 0 km/h. Kinomap reads only min and max; the increment merely has to be there.
- `2ACD`: Deliberately minimal. Kinomap's flags parser is fragile, so no optional field is ever added. Kinomap derives distance itself from speed.
- `2AD9`: Every write is answered with an indication, success or not.
- `2A29`: Must not be "decathlon"; Kinomap switches to vendor quirk paths on that string.

### Deliberately absent

| UUID | Name | Reason | Provenance | Evidence |
|---|---|---|---|---|
| 2AD5 | Supported Inclination Range | The R1 Pro has no incline. No inclination bit is set in 2ACC either. | measured | test-log: Stage 0, pass criteria (2AD5 ABSENT) |

### Control point opcodes

`control_opcode` — 2AD9 request byte 0.

| Value | Name | Direction | Meaning | Provenance | Evidence |
|---|---|---|---|---|---|
| 0x00 | request_control | kinomap-sends | Sent on discovering 2AD9 | vendor-app | design doc: ag8.java |
| 0x02 | set_target_speed | kinomap-sends | uint16 LE, km/h x 100; sent whenever the target changes | vendor-app | design doc: ag8.java |
| 0x07 | start_resume | kinomap-sends | Sent only after we notify 2ADA byte 0 = 0x04 | vendor-app | design doc: ag8.java line 398 |
| 0x08 | stop_pause | never-sent | 08 01 stop / 08 02 pause. NEVER sent on this machine (brand-gated) | vendor-app | design doc: ag8.java lines 138-166 |

`status_opcode` — 2ADA byte 0.

| Value | Name | Direction | Meaning | Provenance | Evidence |
|---|---|---|---|---|---|
| 0x04 | started_or_resumed_by_user |  | Provokes Kinomap to write 07 and start its training session | vendor-app | design doc: ag8.java line 398 |

Responses are `80 <request-opcode> 01`. Byte-level tables are in [frame-layouts.md](frame-layouts.md).

### Behavioural constraints

| Key | Value | Statement | Provenance | Evidence |
|---|---|---|---|---|
| `stop_sentinel_kmh` | 1.0 km/h | Kinomap's generic treadmill stop is a Set Target Speed of exactly 1.00 km/h (stop() calls y0(1.0f)); it writes no stop opcode. The bridge treats that value as a stop, not a speed. | vendor-app | design doc: ag8.java:183 |
| `kinomap_sends_stop_opcode` | no | Kinomap never sends opcode 0x08 to this machine; Start/Stop handling is gated on the manufacturer string being "decathlon". | vendor-app | design doc: ag8.java lines 138, 152, 166 |
| `start_requires_status_0x04` | yes | Kinomap sends opcode 0x07 only in response to a 2ADA notification with byte 0 = 0x04. | vendor-app | design doc: ag8.java line 398 |
| `control_point_always_success` | yes | Every control point write is answered with an indication 80 <request-opcode> 01. Silence or an error breaks the handshake (unbounded retry loops). | vendor-app | design doc: ag8.java lines 311, 323, 341-344 |
| `speed_unit_kmh` | 0.01 km/h | Speeds in 2ACD, 2AD4 and Set Target Speed are uint16 little-endian in units of 0.01 km/h. | measured | test-log: Stage 0, 2AD4 read back as 0.50 / 6.00 / 0.10 |
| `advertised_max_speed_kmh` | 6.0 km/h | The advertised maximum is 6.00 km/h, so Kinomap clamps targets app-side and none above it reach the bridge. | measured | test-log: Stage 0, 2AD4 read back |
| `inclination_supported` | no | 2AD5 is absent and 2ACC advertises no inclination or resistance target setting. | measured | test-log: Stage 0, pass criteria |
| `kinomap_derives_distance` | yes | Kinomap integrates instantaneous speed itself once per second; Total Distance in 2ACD is not needed. | vendor-app | design doc: uf8.java t0() |
| `flags_must_be_zero` | yes | Kinomap reads the 2ACD flags field big-endian with a x255 multiplier. Any optional field risks silently corrupting later offsets. | vendor-app | design doc: ag8.java z() |
