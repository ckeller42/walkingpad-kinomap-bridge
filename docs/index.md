# WalkingPad → Kinomap bridge

**Kinomap does not support the KingSmith WalkingPad R1 Pro. This makes it work
anyway.**

An [M5Stack CoreS3](https://docs.m5stack.com/en/core/CoreS3) sits between the
treadmill and the phone as a Bluetooth **dual-role** device — central to the
belt, peripheral to the app — translating KingSmith's proprietary *WiLink*
protocol into the standard *Fitness Machine Service* that Kinomap speaks.

![The bridge running](screens/main-walking.png)

Verified over an eight-minute walk: speed control from the bridge's own
buttons, with Kinomap tracking live speed, distance and power, and integrating
distance to within 1% of the measured truth.

**It does** drive the belt — start, stop and speed — from the touchscreen or
the console, and feed Kinomap live speed, distance and power so the app runs
and records a normal session.

**It does not** let Kinomap command the belt's speed. That is a product
decision on Kinomap's side and applies to every generic FTMS treadmill; see
[How it works](architecture.md). You drive the belt, Kinomap reads it.

## For users

Start here to use the bridge.

```{toctree}
:maxdepth: 2
:caption: For users

setup-wizard
serial-console
troubleshooting
```

- [Setup wizard](setup-wizard.md) — first-run pairing, with real screenshots of
  every screen.
- [Serial console](serial-console.md) — driving the bridge over USB.
- [Troubleshooting](troubleshooting.md) — the things that look like faults and
  are not.

## How it works

The concepts, for understanding the system rather than operating it.

```{toctree}
:maxdepth: 2
:caption: How it works

architecture
protocol/README
protocol/reference
protocol/frame-layouts
kinomap-pairing-observed
```

- [How it works](architecture.md) — the dual-role BLE design, and why each
  layer exists.
- [Protocol reference](protocol/reference.md) — both wire formats, generated
  from the YAML source of truth.
- [Frame layouts](protocol/frame-layouts.md) — byte-level diagrams.
- [Kinomap's requirements](kinomap-pairing-observed.md) — what the app demands
  of an FTMS treadmill, and how it fails when they are not met.

## Operating and debugging

```{toctree}
:maxdepth: 2
:caption: Operating

developer-menu
logging
ota
```

- [Developer menu](developer-menu.md) — on-device Wi-Fi pairing and the radio
  switches.
- [Session logging](logging.md) — SD and UDP sinks, and why the console cannot
  be used during a live session.
- [Firmware updates](ota.md) — OTA from the developer menu, its trust model,
  and what CI publishes: every push to `main` puts the firmware and its
  manifest on this site, at
  `https://ckeller42.github.io/walkingpad-kinomap-bridge/firmware/manifest.json`.

## For developers

```{toctree}
:maxdepth: 2
:caption: For developers

developing
api
```

- [Developing](developing.md) — building, testing, conventions, publishing firmware.
- [API reference](api.rst) — generated from the headers by Doxygen and Breathe,
  so it cannot drift from the code.

## Evidence

Findings are this project's most valuable output, so they are kept with their
provenance: what was **counted**, what was **observed**, and what was merely
**inferred**.

```{toctree}
:maxdepth: 2
:caption: Evidence

test-log
ux-review
```

- [Test log](test-log.md) — every measurement, and how it was taken.
- [UX review](ux-review.md) — the on-device screens, assessed and fixed.

## Hardware

| | |
|---|---|
| **Bridge** | M5Stack CoreS3 — ESP32-S3, 240 MHz, 16 MB flash, 320 KB RAM, 320×240 touch LCD, microSD, Wi-Fi + BLE 5 |
| **Treadmill** | KingSmith WalkingPad **R1 Pro** — Telink BLE module `WLT8266M`, firmware `M26_V187.1.2`, advertises as `RE`, 0.5–6.0 km/h, **no incline** |
| **App** | Kinomap 4.7.1, Android and iOS |

## Safety

- `0xE8` is the WiLink OTA command group and **can brick the belt**. Only group
  `0xA2` is ever emitted, enforced at a single write choke point.
- The Telink OTA service is never connected to, written to or subscribed on.
- Nothing moves the belt unless the bridge is **armed**, and STOP works whether
  armed or not.
