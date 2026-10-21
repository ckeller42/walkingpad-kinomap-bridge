> **GENERATED** from `protocol/wilink.yaml` and `protocol/ftms.yaml` by `tools/gen_frame_layouts.py` — do not hand-edit; see [README.md](README.md) for how to regenerate.

# Frame layouts

Byte-offset layouts. All multi-byte FTMS fields are little-endian; WiLink 24-bit counters are big-endian. No bit grid is used because neither protocol is bit-packed.

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
