> **GENERATED** from `protocol/wilink.yaml`, `protocol/ftms.yaml` and `protocol/domyos.yaml` by `tools/gen_frame_layouts.py` — do not hand-edit; see [README.md](README.md) for how to regenerate.

# Frame layouts

Byte-offset layouts. All multi-byte FTMS and Domyos-mode fields are little-endian; WiLink 24-bit counters are big-endian. No bit grid is used because none of these protocols is bit-packed.

## WiLink

### Command frame

6 bytes, written to `FE02`. Provenance: community (design doc: Established facts; every command in the test log conforms).

| Offset | Field | Value | Note |
|---|---|---|---|
| 0 | header | `0xF7` |  |
| 1 | group | `0xA2` | 0xA2 for every known command |
| 2 | function | command selector |  |
| 3 | argument | variable | one byte; meaning depends on function |
| 4 | crc | computed |  |
| 5 | trailer | `0xFD` |  |

CRC: Low byte of the sum of every byte between the header byte and the CRC byte: group, function and argument for a command; bytes 1 to 17 for a status frame.

### Worked command frames

CRC computed by the generator; `Argument` is the raw byte.

| Command | Example | Argument | Frame | CRC |
|---|---|---|---|---|
| Request status | request status | 0x00 | `F7 A2 00 00 A2 FD` | 0xA2 |
| Set speed | 4.0 km/h | 0x28 | `F7 A2 01 28 CB FD` | 0xCB |
| Set speed | 2.8 km/h | 0x1C | `F7 A2 01 1C BF FD` | 0xBF |
| Set speed | 0 (NOT a stop) | 0x00 | `F7 A2 01 00 A3 FD` | 0xA3 |
| Set mode | manual | 0x01 | `F7 A2 02 01 A5 FD` | 0xA5 |
| Set mode | auto | 0x00 | `F7 A2 02 00 A4 FD` | 0xA4 |
| Start belt | start | 0x01 | `F7 A2 04 01 A7 FD` | 0xA7 |
| Stop belt (set mode standby) | stop | 0x02 | `F7 A2 02 02 A6 FD` | 0xA6 |

### Status frame

20 bytes, read from `FE01`. Provenance: measured (test-log: Stage 2, status frame offsets validated against real data).

| Offset | Size | Field | Type | Unit | Meaning | Provenance |
|---|---|---|---|---|---|---|
| 0 | 1 | header | const |  | constant `0xF8` | measured |
| 1 | 1 | group | const |  | constant `0xA2` | measured |
| 2 | 1 | state | u8 |  |  | measured |
| 3 | 1 | speed | u8 | × 0.1 km/h |  | measured |
| 4 | 1 | mode | u8 |  |  | measured |
| 5–7 | 3 | elapsed | u24be | s |  | measured |
| 8–10 | 3 | distance | u24be | × 10 m | Unit (10 m per count) is an assumption until a real walk is captured. | community |
| 11–13 | 3 | steps | u24be | steps | Validated only at near-zero values. | measured |
| 14 | 1 | preset_speed | u8 | × 0.1 km/h | The belt's own preset/target speed. The bridge does not use it. | measured |
| 15–17 | 3 | unknown | raw |  | Zero in the captured frame; meaning not established. | measured |
| 18 | 1 | crc | u8 |  |  | measured |
| 19 | 1 | trailer | const |  | constant `0xFD` | measured |

#### Decoded example: idle belt, captured 2026-10-21

`F8 A2 00 00 01 00 00 0B 00 00 00 00 00 02 0A 00 00 00 BA FD` — measured (test-log: Stage 2, status frame offsets). Mode 1, elapsed 11 s, distance 0, steps 2, preset 0x0a (1.0 km/h).

| Offset | Field | Bytes | Decoded |
|---|---|---|---|
| 0 | header | `F8` | F8 |
| 1 | group | `A2` | A2 |
| 2 | state | `00` | 0 (idle) |
| 3 | speed | `00` | 0.0 km/h |
| 4 | mode | `01` | 1 (manual) |
| 5–7 | elapsed | `00 00 0B` | 11 s |
| 8–10 | distance | `00 00 00` | 0 m |
| 11–13 | steps | `00 00 02` | 2 steps |
| 14 | preset_speed | `0A` | 1.0 km/h |
| 15–17 | unknown | `00 00 00` | 00 00 00 |
| 18 | crc | `BA` | 0xBA |
| 19 | trailer | `FD` | FD |

## FTMS

### Fitness Machine Feature (2ACC)

Direction: read. Length: 8 bytes. Provenance: measured (test-log: Stage 0, characteristic values).

| Offset | Size | Field | Type | Unit | Meaning | Provenance |
|---|---|---|---|---|---|---|
| 0–3 | 4 | fitness_machine_features | u32le |  | value `0x00000000` | measured |
| 4–7 | 4 | target_setting_features | u32le |  | value `0x00000001` | measured |

Bits of `target_setting_features`:

| Bit | Name | State | Note |
|---|---|---|---|
| 0 | speed_target_setting | set | Required; Kinomap only sends Set Target Speed if this is set |
| 1 | inclination_target_setting | clear |  |
| 2 | resistance_target_setting | clear |  |

Example: `00 00 00 00 01 00 00 00`

| Offset | Field | Bytes | Decoded |
|---|---|---|---|
| 0–3 | fitness_machine_features | `00 00 00 00` | 0 |
| 4–7 | target_setting_features | `01 00 00 00` | 1 |

### Supported Speed Range (2AD4)

Direction: read. Length: 6 bytes. Provenance: measured (test-log: Stage 0, characteristic values).

| Offset | Size | Field | Type | Unit | Meaning | Provenance |
|---|---|---|---|---|---|---|
| 0–1 | 2 | minimum_speed | u16le | × 0.01 km/h | value `0x0032` | measured |
| 2–3 | 2 | maximum_speed | u16le | × 0.01 km/h | value `0x0258`; 6.00 km/h: the belt's walking-mode limit with the handrail folded | measured |
| 4–5 | 2 | minimum_increment | u16le | × 0.01 km/h | value `0x000A` | measured |

Example: `32 00 58 02 0A 00`

| Offset | Field | Bytes | Decoded |
|---|---|---|---|
| 0–1 | minimum_speed | `32 00` | 0.50 km/h |
| 2–3 | maximum_speed | `58 02` | 6.00 km/h |
| 4–5 | minimum_increment | `0A 00` | 0.10 km/h |

### Treadmill Data (2ACD)

Direction: notify. Length: 4 bytes. Provenance: vendor-app (design doc: minimal payload, chosen around the ag8.java z() flags parser).

| Offset | Size | Field | Type | Unit | Meaning | Provenance |
|---|---|---|---|---|---|---|
| 0–1 | 2 | flags | u16le |  | value `0x0000`; No optional fields present. Kinomap reads this field big-endian; with 0x0000 both readings agree. | vendor-app |
| 2–3 | 2 | instantaneous_speed | u16le | × 0.01 km/h |  | vendor-app |

Example (5.00 km/h): `00 00 F4 01`

| Offset | Field | Bytes | Decoded |
|---|---|---|---|
| 0–1 | flags | `00 00` | 0 |
| 2–3 | instantaneous_speed | `F4 01` | 5.00 km/h |

### Control Point request (2AD9, write)

Direction: write. Length: variable. Provenance: vendor-app (design doc: Kinomap side, from ag8.java and uf8.java).

| Offset | Size | Field | Type | Unit | Meaning | Provenance |
|---|---|---|---|---|---|---|
| 0 | 1 | opcode | u8 |  |  | vendor-app |
| 1–2 | 2 | parameter | u16le | × 0.01 km/h | Only for opcode 0x02 (Set Target Speed); other opcodes carry no parameter. | vendor-app |

Example (Set Target Speed 5.00 km/h): `02 F4 01`

| Offset | Field | Bytes | Decoded |
|---|---|---|---|
| 0 | opcode | `02` | 2 (set_target_speed) |
| 1–2 | parameter | `F4 01` | 5.00 km/h |

### Control Point response (2AD9, indicate)

Direction: indicate. Length: 3 bytes. Provenance: vendor-app (design doc: Kinomap expects 80 <request-opcode> 01).

| Offset | Size | Field | Type | Unit | Meaning | Provenance |
|---|---|---|---|---|---|---|
| 0 | 1 | response_code | u8 |  | value `0x80` | vendor-app |
| 1 | 1 | request_opcode | u8 |  | Echo of the opcode being answered. | vendor-app |
| 2 | 1 | result | u8 |  | value `0x01`; Success. The bridge never returns an error; see constraints. | vendor-app |

Example (Success for Set Target Speed): `80 02 01`

| Offset | Field | Bytes | Decoded |
|---|---|---|---|
| 0 | response_code | `80` | 128 |
| 1 | request_opcode | `02` | 2 |
| 2 | result | `01` | 1 |

### Fitness Machine Status (2ADA)

Direction: notify. Length: 1 byte. Provenance: vendor-app (design doc: ag8.java line 398).

| Offset | Size | Field | Type | Unit | Meaning | Provenance |
|---|---|---|---|---|---|---|
| 0 | 1 | op_code | u8 |  |  | vendor-app |

Example (Started or resumed by the user): `04`

| Offset | Field | Bytes | Decoded |
|---|---|---|---|
| 0 | op_code | `04` | 4 (started_or_resumed_by_user) |

## Domyos mode

Nothing below has been measured, and the examples are illustrative values in the declared layout rather than captures. A payload marked `disputed` has two incompatible readings; see [reference.md](reference.md) for both.

### Handshake challenge (638A1104, read)

Direction: read. Length: 2 bytes. Provenance: community (kinomap-decompile: ih8 length-gates this read at exactly 2 bytes).

| Offset | Size | Field | Type | Unit | Meaning | Provenance |
|---|---|---|---|---|---|---|
| 0–1 | 2 | challenge | raw |  | Two arbitrary bytes chosen by the peripheral. Kinomap feeds them into the response block in swapped order, byte 1 before byte 0. | community |

Example (an illustrative challenge; any two bytes are legal): `12 34`

| Offset | Field | Bytes | Decoded |
|---|---|---|---|
| 0–1 | challenge | `12 34` | 12 34 |

### Handshake response (638A1105, write)

Direction: write. Length: 16 bytes. Provenance: community (kinomap-decompile: ih8 builds a 16-byte block and writes the AES-128-ECB ciphertext of it).

| Offset | Size | Field | Type | Unit | Meaning | Provenance |
|---|---|---|---|---|---|---|
| 0–15 | 16 | aes_block | raw |  | AES-128-ECB ciphertext of a 16-byte plaintext derived from the challenge, under a fixed key shipped in the app. The bridge as peripheral neither computes nor verifies this; it only has to accept the write and report success. | community |

Example (placeholder, not a real ciphertext - the key is not recorded here and no example write has been captured): `00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00`

| Offset | Field | Bytes | Decoded |
|---|---|---|---|
| 0–15 | aes_block | `00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00` | 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 |

### Level (638A1001, notify and write)

Direction: notify and write. Length: 1 byte. Provenance: disputed (kinomap-decompile: exactly 1 byte on notify, 1 byte on write; qdomyos-zwift: read as uint16 little-endian in a branch requiring 2 bytes).

| Offset | Size | Field | Type | Unit | Meaning | Provenance |
|---|---|---|---|---|---|---|
| 0 | 1 | level | u8 |  | Plain level, no scale, bounded by the 0x2906 descriptor. The width is the disputed part: Kinomap insists on 1 byte, qdomyos-zwift reads 2 little-endian. | disputed |

Example (level 5): `05`

| Offset | Field | Bytes | Decoded |
|---|---|---|---|
| 0 | level | `05` | 5 |

### Level limits (638A1001, descriptor 0x2906)

Direction: descriptor read. Length: 2 bytes. Provenance: community (kinomap-decompile: ih8's descriptor-read callback takes byte 0 as the minimum level and byte 1 as the maximum; qdomyos-zwift: reads no descriptor, so it neither confirms nor contradicts this).

| Offset | Size | Field | Type | Unit | Meaning | Provenance |
|---|---|---|---|---|---|---|
| 0 | 1 | minimum_level | u8 |  |  | community |
| 1 | 1 | maximum_level | u8 |  |  | community |

Example (levels 1 to 16, illustrative): `01 10`

| Offset | Field | Bytes | Decoded |
|---|---|---|---|
| 0 | minimum_level | `01` | 1 |
| 1 | maximum_level | `10` | 16 |

### Cadence (638A1002, notify)

Direction: notify. Length: 2 bytes. Provenance: community (kinomap-decompile: uint16 little-endian, integer-divided by 10; qdomyos-zwift: subscribes but decodes cadence elsewhere).

| Offset | Size | Field | Type | Unit | Meaning | Provenance |
|---|---|---|---|---|---|---|
| 0–1 | 2 | cadence | u16le | × 0.1 rpm |  | community |

Example (400 raw = 40.0 rpm): `90 01`

| Offset | Field | Bytes | Decoded |
|---|---|---|---|
| 0–1 | cadence | `90 01` | 40.0 rpm |

### Power (638A1003, notify)

Direction: notify. Length: 2 bytes. Provenance: community (kinomap-decompile: uint16 little-endian, unscaled, passed through as watts; qdomyos-zwift: uint16 little-endian at offset 0 assigned straight to watts).

| Offset | Size | Field | Type | Unit | Meaning | Provenance |
|---|---|---|---|---|---|---|
| 0–1 | 2 | power | u16le | W |  | community |

Example (125 W): `7D 00`

| Offset | Field | Bytes | Decoded |
|---|---|---|---|
| 0–1 | power | `7D 00` | 125 W |

### Speed (638A1004, notify and write)

Direction: notify and write. Length: 2 bytes. Provenance: community (kinomap-decompile: uint16 little-endian divided by 10 on notify, and the same encoding written back on a speed target; qdomyos-zwift: uint16 little-endian at offset 0 divided by 10.0).

| Offset | Size | Field | Type | Unit | Meaning | Provenance |
|---|---|---|---|---|---|---|
| 0–1 | 2 | speed | u16le | × 0.1 km/h | Little-endian, low byte first. Kinomap applies no clamp of its own on the write, so the peripheral must clamp; a write of 00 00 is the stop. | community |

Example (25 raw = 2.5 km/h): `19 00`

| Offset | Field | Bytes | Decoded |
|---|---|---|---|
| 0–1 | speed | `19 00` | 2.5 km/h |

### Second speed value (638A1005, notify) - disputed meaning

Direction: notify. Length: 2 bytes. Provenance: disputed (kinomap-decompile: handled identically to 638A1004, uint16 little-endian divided by 10, stored as speed; qdomyos-zwift: handled as resistance, uint16 little-endian unscaled and rejected above its maximum resistance).

| Offset | Size | Field | Type | Unit | Meaning | Provenance |
|---|---|---|---|---|---|---|
| 0–1 | 2 | value | u16le |  | Speed in 0.1 km/h under Kinomap's reading, an unscaled resistance level under qdomyos-zwift's. No unit is asserted here because the two readings are incompatible. | disputed |

Example (25 raw = 2.5 km/h under Kinomap's reading, level 25 under qdomyos-zwift's): `19 00`

| Offset | Field | Bytes | Decoded |
|---|---|---|---|
| 0–1 | value | `19 00` | 25 |

### Four-byte counter (638A1008, notify) - disputed meaning

Direction: notify. Length: 4 bytes. Provenance: disputed (kinomap-decompile: exactly 4 bytes, parsed and then discarded; qdomyos-zwift: uint32 little-endian divided by 1000, used as distance in kilometres).

| Offset | Size | Field | Type | Unit | Meaning | Provenance |
|---|---|---|---|---|---|---|
| 0–3 | 4 | counter | u32le |  | Distance in metres under qdomyos-zwift's reading. Kinomap reads the four bytes and uses none of them, so no unit is asserted. | disputed |

Example (1000 raw = 1.000 km under qdomyos-zwift's reading): `E8 03 00 00`

| Offset | Field | Bytes | Decoded |
|---|---|---|---|
| 0–3 | counter | `E8 03 00 00` | 1000 |

### Console button events (638A1010, notify)

Direction: notify. Length: 6 bytes. Provenance: community (kinomap-decompile: exactly 6 bytes; bytes 0, 4 and 5 are read as signed deltas (values above 128 are negative) that move the user's incline offset; qdomyos-zwift: silent).

| Offset | Size | Field | Type | Unit | Meaning | Provenance |
|---|---|---|---|---|---|---|
| 0 | 1 | level_delta | u8 |  | Signed; above 128 means negative. Applied directly to the incline offset. | community |
| 1–3 | 3 | unused | raw |  | Not read by Kinomap. | community |
| 4 | 1 | coarse_delta | u8 |  | Non-zero moves the offset by three levels, signed as above. Only read when byte 0 is 0. | community |
| 5 | 1 | fine_delta | u8 |  | Non-zero moves the offset by one level, signed as above. Only read when byte 0 is 0. | community |

Example (no button pressed - the only value the bridge should ever notify): `00 00 00 00 00 00`

| Offset | Field | Bytes | Decoded |
|---|---|---|---|
| 0 | level_delta | `00` | 0 |
| 1–3 | unused | `00 00 00` | 00 00 00 |
| 4 | coarse_delta | `00` | 0 |
| 5 | fine_delta | `00` | 0 |

### In power range (638A1103, notify)

Direction: notify. Length: 1 byte. Provenance: community (kinomap-decompile: byte 0 of 0 means in range, anything else out of range; qdomyos-zwift: silent).

| Offset | Size | Field | Type | Unit | Meaning | Provenance |
|---|---|---|---|---|---|---|
| 0 | 1 | in_range | u8 |  |  | community |

Example (in range): `00`

| Offset | Field | Bytes | Decoded |
|---|---|---|---|
| 0 | in_range | `00` | 0 (in_range) |

### Target power (638A100E, write) - disputed payload

Direction: write. Length: 2 bytes. Provenance: disputed (kinomap-decompile: a bare uint16 little-endian watt value, two bytes, no framing; qdomyos-zwift: a ten-byte framed command with a rolling sequence byte carrying the same watt value little-endian in its last two bytes).

| Offset | Size | Field | Type | Unit | Meaning | Provenance |
|---|---|---|---|---|---|---|
| 0–1 | 2 | target_power | u16le | W | Kinomap's reading. qdomyos-zwift's ten-byte framing is recorded in the constraints rather than as a second layout, because its leading bytes were taken from a packet capture and their meaning is not established in either source. | disputed |

Example (125 W, under Kinomap's reading): `7D 00`

| Offset | Field | Bytes | Decoded |
|---|---|---|---|
| 0–1 | target_power | `7D 00` | 125 W |

### Power limits (638A100E, descriptor 0x2906)

Direction: descriptor read. Length: 4 bytes. Provenance: community (kinomap-decompile: bytes 0-1 are the minimum and bytes 2-3 the maximum, both uint16 little-endian; qdomyos-zwift: reads no descriptor).

| Offset | Size | Field | Type | Unit | Meaning | Provenance |
|---|---|---|---|---|---|---|
| 0–1 | 2 | minimum_power | u16le | W |  | community |
| 2–3 | 2 | maximum_power | u16le | W |  | community |

Example (25 W to 250 W, illustrative): `19 00 FA 00`

| Offset | Field | Bytes | Decoded |
|---|---|---|---|
| 0–1 | minimum_power | `19 00` | 25 W |
| 2–3 | maximum_power | `FA 00` | 250 W |

### Heart Rate Measurement (2A37, notify)

Direction: notify. Length: 2 bytes. Provenance: community (kinomap-decompile: bit 0 of byte 0 clear means an 8-bit rate in byte 1, set means a 16-bit little-endian rate in bytes 1-2; this is the standard Bluetooth layout).

| Offset | Size | Field | Type | Unit | Meaning | Provenance |
|---|---|---|---|---|---|---|
| 0 | 1 | flags | u8 |  | Bit 0 clear: 8-bit rate follows. Bit 0 set: 16-bit little-endian rate follows. | community |
| 1 | 1 | heart_rate | u8 | bpm |  | community |

Example (72 bpm, 8-bit form): `00 48`

| Offset | Field | Bytes | Decoded |
|---|---|---|---|
| 0 | flags | `00` | 0 |
| 1 | heart_rate | `48` | 72 bpm |
