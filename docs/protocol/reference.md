> **GENERATED** from `protocol/wilink.yaml`, `protocol/ftms.yaml` and `protocol/domyos.yaml` by `tools/gen_protocol_reference.py` — do not hand-edit; see [README.md](README.md) for how to regenerate.

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
| disputed | two sources disagree; both readings are recorded and neither has been measured |

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
- **Set mode**: Select the belt mode. Mode 0 (auto) is produced by the bridge only on an explicit user request, and the bridge treats Auto as active only once a status frame reports mode 0.
- **Start belt**: Start the belt (countdown, then run at the belt's preset speed). Sent after mode manual. The variant with argument 0 (F7 A2 04 00 A6 FD) is not a stop; see the constraints. Confirmed on an R1 Pro from standby and from idle, at mode-to-start gaps of 1.0, 1.5, 2.0 and 3.0 s - every gap started the belt, so the pacing interval is not load-bearing here. The belt ramps to its own preset speed (~1.1 km/h) unless a speed is set.
- **Stop belt (set mode standby)**: The only stop. Sets mode standby; the belt decelerates to 0 and parks in state 4. Same wire format as Set mode with mode 2. Leaves the belt in standby, so it will not start again until mode is set back to manual.

### Enumerations

`state` — status byte 2.

| Value | Name | Meaning | Provenance | Evidence |
|---|---|---|---|---|
| 0x00 | idle |  | measured | test-log: Stage 2, state machine |
| 0x03 | waking | Seen briefly on the way out of standby; also the parked state after a STOP, held at speed 0 for 7+ s | measured | test-log: Stage 2, start probe; UDP capture 2026-10-22: 'f8 a2 03 00 02 ...' repeated from 4.5 s after the stop until the next start |
| 0x08 | spin_up | Seen between pre-countdown and starting | measured | test-log: Stage 3, decoded from a captured UDP log |
| 0x09 | pre_countdown | Seen between start and countdown | measured | test-log: Stage 2, start probe |
| 0x07 | countdown | Countdown immediately after start | measured | test-log: Stage 2, state machine |
| 0x06 | starting | Starting / transitioning | measured | test-log: Stage 2, state machine |
| 0x01 | running |  | measured | test-log: Stage 2, state machine |
| 0x04 | standby | Stopped by the standby command | measured | test-log: Stage 2, state machine |

`mode` — status byte 4; Set mode argument.

| Value | Name | Meaning | Provenance | Evidence |
|---|---|---|---|---|
| 0x00 | auto | The belt sets its own speed from the walker's position and starts on step-on | measured | UDP capture 2026-10-22: status byte 4 = 00 after the belt's own A key ('f8 a2 00 00 00 ...'); bridge logged 'belt mode 3 -> 1 raw=0' |
| 0x01 | manual |  | measured | test-log: Stage 2, state machine |
| 0x02 | standby |  | measured | test-log: Stage 2, state machine |

### Status frame

20 bytes, received on `FE01` in reply to Request status. Provenance: measured (test-log: Stage 2, status frame offsets validated against real data). Field offsets are in [frame-layouts.md](frame-layouts.md).

### Behavioural constraints

| Key | Value | Statement | Provenance | Evidence |
|---|---|---|---|---|
| `status_may_fragment` | yes | A status frame can arrive split across two FE01 notifications, so a handler that parses each notification alone silently drops that reading. | measured | test-log: Stage 3, captured as 'f8 a2 00' then a 17-byte remainder ending fd |
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
| `auto_mode_reported_in_status` | yes | Switched to Auto by its own A key, the belt reports mode 0 in status byte 4 (from standby, state 4 mode 2, to idle, state 0 mode 0); pressing M reports mode 1 again while the belt keeps running. | measured | UDP capture 2026-10-22: 'f8 a2 04 00 02 ...' then 'f8 a2 00 00 00 ...'; later 'f8 a2 01 1d 01 ...' (running 2.9 km/h, mode 1); bridge logged 'belt mode 3 -> 1 raw=0' and 'belt mode 1 -> 2 raw=1' |
| `parked_state_after_stop` | 3 | After a STOP (mode 2) from 4.0 km/h the belt decelerated in state 1, then reported state 3 at speed 0 with mode 2 and held it for more than 7 s; a stationary check must accept state 3, not only 0 and 4. | measured | UDP capture 2026-10-22: 'f8 a2 01 28 02 ...' 0.5 s after the stop, then 'f8 a2 03 00 02 ...' at 4.5, 6.6, 8.6 and 10.6 s; four badge taps in that window were refused by the bridge |
| `auto_starts_on_step_on` | yes | In Auto the belt starts with no start command - status went state 6 then state 1 at 0.7 km/h while the bridge sent only status requests - and then followed the walker up to 2.9 km/h; UI speed taps during Auto were muted by the bridge and never reached the wire. | measured | UDP capture 2026-10-22: 'f8 a2 06 00 00 ...' then 'f8 a2 01 07 00 ...' with tx limited to 'f7 a2 00 00 a2 fd'; bridge emitted NotifyStarted; two + taps logged with zero SetSpeed emits |

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

## Domyos mode — the second Kinomap side

A proprietary one-value-per-characteristic service. Every live metric is its own 1-2 byte notify characteristic rather than a packed frame, limits come from 0x2906 Valid Range descriptors rather than from readable characteristics, and the only speed command is a write back to the same characteristic speed is reported on. Kinomap reaches this handler by equipment type 42, not by any manufacturer string. The bridge is the BLE peripheral; Kinomap is the central.

> **NOT MEASURED.** Every fact in this section comes from reading two sources and none from this hardware. `disputed` means the two disagree; both readings are kept and neither is implemented without a measurement. See [`protocol/domyos.yaml`](../../protocol/domyos.yaml) for the sources and the naming warnings.

Service `638AF000-7BDE-3E25-FFC5-9DE9B2A0197A` (Proprietary fitness service), community: kinomap-decompile: ih8 resolves this UUID as the service and every other 638A* UUID as a characteristic inside it; qdomyos-zwift: the same UUID is KETTLER_ALT_SERVICE_UUID, the alternate service its characteristic lookups run against.

The project brief and the design doc both call 638A1001 the service UUID. Both sources contradict that: 638A1001 is the level characteristic inside 638AF000. qdomyos-zwift also knows a primary service 8ce5cc01-0a4d-11e9-ab14-d663bd873d93 carrying the same characteristics on other firmware; Kinomap never looks for it, so the bridge must advertise 638AF000.

Advertised service `638AF000-7BDE-3E25-FFC5-9DE9B2A0197A`, community. Not determined: the advertised local name. A `modelFilter` string exists on Kinomap's paired-equipment record and was not traced to a scan filter, so no name is recorded here rather than a plausible-looking guess.

### Characteristics

| UUID | Name | Properties | 0x2906 descriptor | Provenance | Evidence |
|---|---|---|---|---|---|
| 638A1104 | Handshake challenge | read |  | community | kinomap-decompile: ih8 reads this characteristic immediately after service discovery and ignores any value whose length is not exactly 2; qdomyos-zwift: silent, it performs no handshake |
| 638A1105 | Handshake response | write |  | community | kinomap-decompile: ih8 writes the 16-byte response here and treats the write-complete callback for this characteristic as "equipment connected"; qdomyos-zwift: names the same UUID KETTLER_HANDSHAKE_CHAR_UUID but never reads or writes it |
| 638A1001 | Level (resistance) | notify, write | `level_valid_range` | disputed | kinomap-decompile: ih8 discards any notification whose length is not exactly 1, and writes a single byte; qdomyos-zwift: parses this characteristic only in a branch guarded by length >= 2 and reads it as uint16 little-endian |
| 638A1002 | Cadence | notify |  | community | kinomap-decompile: ih8 requires exactly 2 bytes, reads uint16 little-endian and divides by 10 before passing the value as the cadence argument of onEquipmentUpdate; qdomyos-zwift: names the same UUID KETTLER_CADENCE_CHAR_UUID and subscribes to it, but decodes cadence from standard CSC/Cycling Power instead and so documents no encoding |
| 638A1003 | Power | notify |  | community | kinomap-decompile: ih8 requires exactly 2 bytes, reads uint16 little-endian unscaled and passes it as the watt argument of onEquipmentUpdate; qdomyos-zwift: KETTLER_POWER_CHAR_UUID, uint16 little-endian at offset 0, assigned straight to watts |
| 638A1004 | Speed | notify, write |  | community | kinomap-decompile: ih8 requires exactly 2 bytes, reads uint16 little-endian and divides by 10 for the speed argument of onEquipmentUpdate, and writes the same encoding back on a speed target; qdomyos-zwift: KETTLER_SPEED_CHAR_UUID, uint16 little-endian at offset 0 divided by 10.0 |
| 638A1005 | Second speed value (disputed) | notify |  | disputed | kinomap-decompile: ih8 handles 638A1005 in the same branch as 638A1004 and stores it as speed, uint16 little-endian divided by 10; qdomyos-zwift: handles 638A1005 in its resistance branch, uint16 little-endian unscaled and rejected unless <= max_resistance |
| 638A1008 | Four-byte counter | notify |  | disputed | kinomap-decompile: ih8 requires exactly 4 bytes, splits them into four locals and then uses none of them; qdomyos-zwift: KETTLER_DISTANCE_CHAR_UUID, uint32 little-endian at offset 0 divided by 1000 to give kilometres |
| 638A1010 | Console button events | notify |  | community | kinomap-decompile: ih8 requires exactly 6 bytes and reads bytes 0, 4 and 5 as signed level deltas that move the user's incline offset; qdomyos-zwift: silent, it does not reference this UUID |
| 638A1103 | In power range | notify |  | community | kinomap-decompile: ih8 sets its in-power-range flag true when byte 0 is 0 and false otherwise; qdomyos-zwift: silent, it does not reference this UUID |
| 638A100E | Target power | notify, write | `power_valid_range` | disputed | kinomap-decompile: ih8 writes a bare uint16 little-endian watt value (two bytes, no framing) and reads the characteristic's 0x2906 descriptor for the power limits; qdomyos-zwift: KETTLER_CONTROL_CHAR_UUID, writes a ten-byte framed command with a rolling sequence byte and reads no descriptor |
| 2A37 | Heart Rate Measurement (Heart Rate 180D) | notify |  | community | kinomap-decompile: ih8 subscribes to every notify characteristic of service 180D before touching 638AF000, and decodes 2A37 with the standard 8/16-bit flag in bit 0 of byte 0; qdomyos-zwift: silent for this device, it takes heart rate from its own sources |

- `638A1104` (Handshake challenge): Read once, never subscribed. Exactly two bytes or Kinomap drops the value and the connection never progresses.
- `638A1105` (Handshake response): The gate on everything. Kinomap calls onEquipmentConnected() only from the successful-write callback for this characteristic. A peripheral that rejects the write is never connected as far as the app is concerned. qdomyos-zwift naming it the handshake characteristic corroborates the role, not the mechanism.
- `638A1001` (Level (resistance)): Disputed width: 1 byte (Kinomap) against 2 bytes little-endian (qdomyos-zwift). Both read it as a plain level with no scale. Since Kinomap is the consumer here, 1 byte is what the bridge would have to notify - but that is a reading of one source, not a measurement, and the 0x2906 descriptor on this characteristic is two bytes wide, which is consistent with either.
- `638A1002` (Cadence): Only Kinomap speaks to the scale, so the 0.1 rpm unit rests on one source. qdomyos-zwift corroborates the role and nothing else.
- `638A1003` (Power): Agreed by both sources, encoding included. Also the training trigger - see the `training_starts_on_power` constraint.
- `638A1004` (Speed): The one characteristic this whole mode exists for. Read and written in the same encoding, on the same UUID. Both sources agree exactly on the read side; only Kinomap documents the write.
- `638A1005` (Second speed value (disputed)): A flat contradiction - speed against resistance, scaled against unscaled. Kinomap's reading is what matters for a peripheral, but a bridge is safest not notifying this characteristic at all until a measurement settles it, because under Kinomap's reading a resistance value here would overwrite the reported speed.
- `638A1008` (Four-byte counter): The length (4) is the only thing both sources agree on. qdomyos-zwift calls it distance in metres; Kinomap parses and discards it, so the decompile cannot confirm or deny the meaning. Kinomap derives distance by integrating speed instead, so the bridge does not need this characteristic - see `distance_derived_by_kinomap`.
- `638A1010` (Console button events): One source only. Documented because serving it wrongly is actively harmful: a non-zero byte here moves Kinomap's incline offset behind the user's back. See the `console_buttons_must_be_zero` constraint.
- `638A1103` (In power range): One source only. Display-only in Kinomap; no behaviour hangs off it.
- `638A100E` (Target power): Disputed write payload: two raw bytes against a ten-byte framed command. Not reconcilable by reading, and not needed by this bridge either way - the belt has no resistance and Kinomap only writes here when it is commanding power, which it is not in a speed workout. Recorded because the 0x2906 descriptor on this characteristic IS needed; see `data_pump_needs_both_valid_ranges`.
- `2A37` (Heart Rate Measurement (Heart Rate 180D)): Standard Bluetooth Heart Rate Measurement, in service 180D and not in 638AF000. The bridge has no heart-rate source, so this is documented as part of what Kinomap looks for rather than as something to serve.

### Connection chain

Ordered, and every step gates the next. A peripheral that skips one shows no data at all.

| Step | Action | Detail | Provenance | Evidence |
|---|---|---|---|---|
| 1 | Subscribe to Heart Rate | Every notify or indicate characteristic of service 180D has its Client Characteristic Configuration descriptor written, before 638AF000 is looked at. | community | kinomap-decompile: ih8's service-discovery callback, first two calls |
| 2 | Subscribe to the proprietary service | Service 638AF000 is resolved and every notify or indicate characteristic in it has its CCCD written, one at a time, each waiting for the previous write to complete. | community | kinomap-decompile: ih8's service-discovery callback, third call |
| 3 | Read the challenge | Characteristic 638A1104 is read. Exactly two bytes, or the chain stops. | community | kinomap-decompile: ih8 queues this read in the same callback and length-gates the result at 2 |
| 4 | Write the response | A 16-byte block is built from the challenge, encrypted with AES-128-ECB under a fixed key compiled into the app, and written to 638A1105. The key is deliberately not recorded in this repository, and the bridge does not need it: as the peripheral it only has to accept the write. | community | kinomap-decompile: ih8's characteristic-read callback for 638A1104 |
| 5 | Equipment reported connected | Kinomap's onEquipmentConnected() fires from the write-complete callback for 638A1105 and from nowhere else. Until the write succeeds the app has no connected equipment. | community | kinomap-decompile: ih8's characteristic-write callback, guarded on this UUID and on success |
| 6 | Read the level limits | The 0x2906 Valid Range descriptor of 638A1001 is read; byte 0 becomes the minimum level and byte 1 the maximum. | community | kinomap-decompile: ih8's characteristic-write callback, immediately after onEquipmentConnected |
| 7 | Read the power limits | The 0x2906 Valid Range descriptor of 638A100E is read; bytes 0-1 become the minimum power and bytes 2-3 the maximum, both uint16 little-endian. | community | kinomap-decompile: ih8's descriptor-read callback for 638A1001 |
| 8 | The 1 Hz data pump starts | Only on completing step 7 does Kinomap start the once-per-second task that publishes the latest notified values as an equipment update. Both descriptor reads must succeed or no data is ever shown. | community | kinomap-decompile: ih8's descriptor-read callback for 638A100E schedules the task at a 1 s fixed rate |

### Enumerations

`power_range` — 638A1103 byte 0.

| Value | Name | Meaning | Provenance | Evidence |
|---|---|---|---|---|
| 0x00 | in_range | Kinomap shows the user as inside the target power range | community | kinomap-decompile: ih8 sets the flag true only on 0 |
| 0x01 | out_of_range | Any non-zero value; Kinomap does not distinguish between them | community | kinomap-decompile: ih8's else branch, reached for every non-zero byte |

### Behavioural constraints

| Key | Value | Statement | Provenance | Evidence |
|---|---|---|---|---|
| `equipment_type_42_is_a_bike` | yes | Equipment type 42 selects a handler whose machine type is BIKE, and the speed write exists only on that branch. Presenting the bridge here means presenting the treadmill to Kinomap as a bike, with the session, units and summary that follow from that. | community | kinomap-decompile: jh8 sets TYPE_BIKE in its constructor, and ih8's speed-target path is guarded on the machine type being BIKE; the type-42 selection is in bqj's equipment-type switch |
| `speed_write_requires_manufacturer_id` | 2 | A speed command reaches the equipment only when the account preference controlSpeedTreadmill is on (it defaults off), the handler declares it can receive the speed feature, and the paired profile's manufacturer id is 2. All three, every time. | community | kinomap-decompile: bqj's gate reads the controlSpeedTreadmill preference with a false default and ANDs it with a helper requiring both the can-receive-speed declaration and a profile manufacturer id of 2 |
| `speed_unit_kmh` | 0.1 km/h | Speed on 638A1004 is uint16 little-endian in units of 0.1 km/h, in both directions. | community | kinomap-decompile: divides by 10 on read and multiplies by 10 on write, low byte first; qdomyos-zwift: divides its little-endian read by 10.0 |
| `stop_is_speed_zero` | yes | There is no start, stop, pause or resume command in this protocol - Kinomap's implementations of all four are empty. Stopping a session writes a speed of 0 to 638A1004. | community | kinomap-decompile: ih8 overrides start, stop and resume with empty bodies, and its training-stopped handler sends a speed target of 0 |
| `speed_write_is_clamped_by_kinomap` | no | Kinomap clamps level and power writes to the limits it read from the 0x2906 descriptors, but clamps speed writes to nothing. A speed above the belt's range will arrive unaltered and must be clamped by the bridge. | community | kinomap-decompile: the level and power target setters clamp against the stored minimum and maximum; the speed path multiplies by 10 and writes |
| `requires_valid_range_descriptors` | yes | Kinomap publishes no equipment data until it has successfully read the 0x2906 Valid Range descriptor of 638A1001 and then of 638A100E; the second read is what starts the 1 Hz update task. A peripheral without both descriptors connects and shows nothing. | community | kinomap-decompile: ih8's descriptor-read callback chains 638A1001 to 638A100E and schedules the 1 s task only on the second |
| `requires_handshake_write` | yes | Kinomap reports the equipment connected only from the successful-write callback for 638A1105. Nothing else sets that state, so the challenge read and response write are not optional. | community | kinomap-decompile: ih8's characteristic-write callback is the only caller of onEquipmentConnected and is guarded on this UUID and on success |
| `handshake_key_recorded_here` | no | The handshake is AES-128-ECB under a fixed 16-byte key compiled into the app. The key is deliberately not recorded in this repository, and is not needed: a peripheral only has to serve a two-byte challenge and accept a 16-byte write, since Kinomap never checks the ciphertext against anything. | community | AGENTS.md no-vendor-material rule; kinomap-decompile: the cipher is initialised for encryption with a fixed key constant and the result is written without any verification step |
| `notification_lengths_are_exact` | yes | Every notification is length-gated to an exact value and silently dropped otherwise - 1 byte for 638A1001 and 638A1103, 2 for 638A1002, 638A1003, 638A1004 and 638A1005, 4 for 638A1008, 6 for 638A1010, 2 for the 638A1104 read. A notification one byte long or short is invisible, not an error. | community | kinomap-decompile: every branch of ih8's notification handler returns early unless the length matches exactly |
| `training_starts_above_power` | 5 W | Kinomap starts the training session when the value on 638A1003 exceeds 5, and treats 5 or below as 0. A bridge that never reports power above 5 never starts a session, whatever speed it reports. | community | kinomap-decompile: ih8's 1 Hz task floors the power value at 5, and calls onStartTraining the first time the floored value is positive |
| `kinomap_derives_distance` | yes | Kinomap integrates the notified speed itself, once per second, to get distance. The 638A1008 counter is parsed and discarded. | community | kinomap-decompile: the 1 Hz task accumulates speed divided by 3.6 into its own metre counter and passes that as the distance argument |
| `console_button_bytes_must_be_zero` | yes | A non-zero byte 0, 4 or 5 on 638A1010 moves the user's incline offset in Kinomap with no visible cause. The bridge must either not expose the characteristic or only ever notify six zero bytes. | community | kinomap-decompile: ih8's 6-byte branch applies those bytes to the incline offset and raises a feature-changed event |
| `update_interval_s` | 1 s | Kinomap samples whatever the last notified value was once per second; it does not poll and does not read any of the metric characteristics. The notification rate only has to beat 1 Hz. | community | kinomap-decompile: the task is scheduled at a fixed rate of 1 s with no initial delay |
| `target_power_payload_settled` | no | The 638A100E write payload is not settled. Kinomap writes two raw little-endian watt bytes; qdomyos-zwift writes a ten-byte command whose first eight bytes include a rolling sequence number taken from a packet capture, with the watt value little-endian in the last two. Neither source explains the other. | disputed | kinomap-decompile: a two-byte little-endian write in the power-target setter; qdomyos-zwift: a ten-byte array in setTargetPower, and a separate fixed startup sequence of 12- and 13-byte writes to the same characteristic that has no counterpart in the decompile |
| `sources_fully_independent` | no | qdomyos-zwift's startup writes and target-power framing for this service are annotated in its own source as having come from a Kinomap packet capture. For those two facts the second source is downstream of the first, so agreement between them would not be independent corroboration. | community | qdomyos-zwift: the comment above its startup sequence states it was observed in a Kinomap bug-report btsnoop; its metric parsing carries no such note |

### Open questions

Neither source answers these. A later task must not read the silence as a settled value.

| Id | Question | Status |
|---|---|---|
| advertised_name | What local name must the peripheral advertise for Kinomap's pairing flow to offer it as equipment type 42? | Not determined. Kinomap's scan callback accepts any device with a non-empty name and applies no service-UUID filter; a modelFilter string exists on the paired-equipment record but was not traced to the scan or pairing UI. |
| equipment_type_origin | Where does equipment type 42 come from, and can it be chosen by the user? | Not determined. The type is a field on a server-supplied equipment catalogue record, not something the peripheral advertises, so whether the bridge can be paired as type 42 at all is a question for the pairing test in the plan's Task 4, not for this document. |
| notify_on_1105 | Does 638A1105 also need notify, or only write? | Not determined. Kinomap only writes it, and would subscribe to it if it were notifiable because it subscribes to the whole service; nothing in either source says whether the real machine notifies on it. |
| level_width | Is 638A1001 one byte or two? | Disputed, not determined. Kinomap requires exactly one; qdomyos-zwift reads two little-endian. Settle by measurement, not by argument. |
| characteristic_1005_meaning | Is 638A1005 a second speed value or a resistance value? | Disputed, not determined. Under Kinomap's reading a resistance value notified here would silently overwrite the reported speed, so the safe course until measured is not to notify it. |
| unused_characteristics | What are 638A1006, 638A1007 and 638A1009 through 638A100D? | Not determined, and out of scope. Kinomap never touches them. qdomyos-zwift treats 638A100A as another resistance characteristic. No entry is recorded for any of them because this file covers only what Kinomap uses. |
