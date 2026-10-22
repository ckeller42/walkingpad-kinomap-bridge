# walkingpad-kinomap-bridge

[![CI](https://github.com/ckeller42/walkingpad-kinomap-bridge/actions/workflows/ci.yml/badge.svg)](https://github.com/ckeller42/walkingpad-kinomap-bridge/actions/workflows/ci.yml)
[![firmware](https://github.com/ckeller42/walkingpad-kinomap-bridge/actions/workflows/firmware.yml/badge.svg)](https://github.com/ckeller42/walkingpad-kinomap-bridge/actions/workflows/firmware.yml)
[![docs](https://img.shields.io/badge/docs-sphinx-blue)](docs/index.md)
[![tests](https://img.shields.io/badge/tests-197%20host-brightgreen)](test)
[![protocol](https://img.shields.io/badge/protocol-WiLink%20%2B%20FTMS-orange)](docs/protocol/reference.md)

**Kinomap does not support the KingSmith WalkingPad R1 Pro. This makes it work
anyway.**

The belt speaks KingSmith's proprietary *WiLink* protocol; Kinomap speaks the
Bluetooth *Fitness Machine Service*. An M5Stack CoreS3 sits between them as a
BLE **dual-role** device — central to the belt, peripheral to the phone —
translating in both directions and applying its own safety policy in between.

![The bridge running: speed, distance, elapsed time, and the arming controls](docs/screens/main-walking.png)

Verified over an eight-minute walk: speed control from the bridge's own
buttons, with Kinomap tracking live speed, distance and power, and integrating
distance to within 1% of the measured truth.

---

## How it fits together

```text
   KingSmith WalkingPad R1 Pro          M5Stack CoreS3               Kinomap
   ───────────────────────────          ──────────────              ─────────
   Telink BLE module (WLT8266M)         ESP32-S3, NimBLE            Fairphone
   firmware M26_V187.1.2                dual-role                   or iPhone

        FE00 service                 ┌─ BLE central ─┐
        FE01 notify ─── status ──────▶  BeltLink      │
        FE02 write  ◀── commands ────  BeltGate      │   SpeedPolicy
                                        (paces ≥1 s)  ├─▶ BridgeState
                                                      │   (arming gate)
                                       ┌─ BLE periph ─┘
                                       │  FtmsServer
        ◀── 0.5 … 6.0 km/h ───────────┤    2ACD ──── speed ─────────▶ displays
            no incline                 │    2AD9 ◀── control point ── commands
                                       └─ SD + UDP logging
```

**Why a gate in the middle.** The belt ignores commands sent closer than ~0.7 s
apart, silently. The phone can ask for anything at any rate. `BeltGate` paces
and prioritises writes; `SpeedPolicy` applies a deadband so small target
changes do not thrash the motor; `BridgeState` refuses to move the belt at all
unless the user has armed the bridge on the device.

## Hardware

| | |
|---|---|
| **Bridge** | [M5Stack CoreS3](https://docs.m5stack.com/en/core/CoreS3) — ESP32-S3, 240 MHz, 16 MB flash, 320 KB RAM, 320×240 touch LCD, microSD, Wi-Fi + BLE 5 |
| **Treadmill** | KingSmith WalkingPad **R1 Pro** — Telink BLE module `WLT8266M`, firmware `M26_V187.1.2`, advertises as `RE`, 0.5–6.0 km/h, **no incline** |
| **App** | [Kinomap](https://www.kinomap.com/) 4.7.1, Android and iOS |

## What works, and what does not

**Works, measured on hardware:**

- The bridge drives the belt — start, stop and speed, from the touchscreen or
  the USB console.
- Kinomap reads live speed, distance and power, and starts its session when
  the belt moves.
- Any FTMS client that *does* command speed is honoured: writing `0x02` starts
  the belt and sets the exact speed asked for.

**Does not, and cannot:**

- **Kinomap will not command the belt's speed.** Not a bug here — it is a
  product decision on their side, for safety and user choice, and it applies to
  every treadmill. A proprietary Domyos mode was investigated as a way around
  it and rejected: Kinomap's handler for those machines is a *bike*, so the
  speed would have arrived with the walk recorded as a ride. See
  [the test log](docs/test-log.md) for the counted evidence and
  [the design record](docs/superpowers/specs/2026-10-21-domyos-speed-mode-design.md)
  for why that door is closed
  ([#2](https://github.com/ckeller42/walkingpad-kinomap-bridge/issues/2),
  `wontfix`).
- **No incline.** The R1 Pro has no incline mechanism, so `2AD5` is
  deliberately not exposed and Kinomap's inclination targets clamp to zero.

## Getting started

```sh
pio test -e native      # 197 host tests, no hardware needed
pio run -e cores3       # real belt (default)
pio run -e cores3_sim   # simulated belt, for development without the treadmill
pio run -e cores3 -t upload --upload-port /dev/cu.usbmodem101
```

On first boot the screen runs a [setup wizard](docs/setup-wizard.md) with real
screenshots. The bridge can also be driven over USB for testing — see the
[serial console](docs/serial-console.md).

## Documentation

The docs are a Sphinx site: the Markdown below is its source, and the C++ API
is generated from the headers by Doxygen and pulled in with Breathe, so the
reference cannot drift from the code.

```sh
doxygen Doxyfile && sphinx-build -b html docs build/sphinx
```

| | |
|---|---|
| [Documentation home](docs/index.md) | architecture, and everything below |
| [Setup wizard](docs/setup-wizard.md) | first-run pairing, with real screenshots |
| [Serial console](docs/serial-console.md) | the USB command surface |
| [Developer menu](docs/developer-menu.md) | on-device Wi-Fi pairing and the radio switches |
| [Session logging](docs/logging.md) | SD and UDP sinks, and why they exist |
| [Protocol reference](docs/protocol/reference.md) | generated from `protocol/*.yaml` |
| [Test log](docs/test-log.md) | what was measured, and how |
| [UX review](docs/ux-review.md) | the on-device screens, assessed |
| [API reference](docs/api.rst) | generated from the headers |
| [AGENTS.md](AGENTS.md) | constraints and conventions, with their reasons |

## Design

Protocol and policy live in Arduino-free `lib/` modules with host tests; BLE,
display and Wi-Fi adapters live in `src/`. That split is what makes 197 tests
runnable with no hardware attached, and it is worth preserving: put the
decision in `lib/` returning a value, and the effect in `src/`.

`protocol/wilink.yaml` and `protocol/ftms.yaml` are the single source of truth
for both wire formats, and every fact in them records whether it was
**measured** on this hardware or inherited from the community and not yet
confirmed.

## Safety

- `0xE8` is the WiLink OTA command group and **can brick the belt**. Only
  group `0xA2` is ever emitted, enforced at a single write choke point.
- The Telink OTA service is never connected to, written to or subscribed on.
- Nothing moves the belt unless the bridge is **armed**, and STOP works whether
  armed or not.
