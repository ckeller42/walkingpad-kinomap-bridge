# WalkingPad → Kinomap bridge

A BLE bridge on an **M5Stack CoreS3 (ESP32-S3)** that talks to a **KingSmith
WalkingPad R1 Pro** treadmill as a BLE central and presents itself to
**Kinomap** as an FTMS treadmill.

```{toctree}
:maxdepth: 2
:caption: Using it

setup-wizard
serial-console
developer-menu
logging
```

```{toctree}
:maxdepth: 2
:caption: Protocols

protocol/README
protocol/reference
protocol/frame-layouts
kinomap-pairing-observed
```

```{toctree}
:maxdepth: 2
:caption: Reference

api
test-log
ux-review
```

## How it fits together

```text
  WalkingPad R1 Pro                M5Stack CoreS3                 Kinomap
  (KingSmith WiLink)                 (bridge)                   (Fairphone)

   FE00 service                   BLE central  ──┐
   FE01 notify  ──── status ────▶ BeltLink      │
   FE02 write   ◀─── commands ─── BeltGate      │  SpeedPolicy
                                                 ├─ BridgeState ──┐
                                   BLE peripheral│                │
                                   FtmsServer  ◀─┘                │
                                     2ACD  ──── speed/distance ───┼──▶ displays
                                     2AD9  ◀─── control point ────┘
```

The belt speaks KingSmith's proprietary **WiLink** protocol; Kinomap speaks
**FTMS**. The bridge is a BLE dual-role device: central to the belt, peripheral
to the phone, translating between the two and applying its own policy
(deadband, command pacing, the start sequence, and an arming gate) in between.

## What each side actually does

Verified on hardware over an eight-minute walk:

- **The bridge drives the belt.** Speed is set from the ESP's `+`/`−` buttons or
  the serial console, and the belt follows.
- **Kinomap reads it.** Live speed, distance and power, correctly integrated —
  169 m measured against 170.6 m predicted over five minutes at 2.0 km/h.

Kinomap does **not** command speed for a generic FTMS treadmill. That is a
product decision on their side, not a limitation here: the bridge honours FTMS
`0x02` Set Target Speed correctly when one arrives. See
[the test log](test-log.md) for the counted evidence and
[issue #2](https://github.com/ckeller42/walkingpad-kinomap-bridge/issues/2) for
what a Domyos protocol mode would involve.

## Safety

- `0xE8` is the WiLink OTA command group and **can brick the belt**. The bridge
  only ever emits `0xA2`, enforced at the single write choke point.
- The Telink OTA service `00010203-0405-0607-0809-0a0b0c0d1912` is never
  connected to, written to, or subscribed on.
- Nothing moves the belt unless the bridge is **armed**, and STOP works whether
  armed or not.
