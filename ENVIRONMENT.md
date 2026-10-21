# Working environment for the WalkingPad → Kinomap bridge

Everything needed to build, flash and verify without leaving this Mac.

## Hardware

| | |
|---|---|
| Bridge board | M5Stack CoreS3 (ESP32-S3), MAC `44:1b:f6:e1:f5:88` |
| Serial port | `/dev/cu.usbmodem101` — Espressif USB JTAG/serial, VID `0x303A` PID `0x1001` |
| Treadmill | KingSmith WalkingPad R1 Pro (legacy WiLink, service `FE00`) |
| Test phone | Fairphone 6, Android 16, ADB over Tailscale `100.126.233.97:46363` |

## Toolchain (all in one venv, no Homebrew)

    ~/tools/venv-ble/bin/{python,pip,pio,esptool}

- **PlatformIO 6.2.0** — builds for both Arduino and ESP-IDF frameworks.
- **esptool 5.6.0** — **must** pass `--before usb-reset` on the CoreS3; the default
  `default-reset` fails with "No serial data received" because the S3 has no
  USB-UART bridge to toggle DTR/RTS on.

      ~/tools/venv-ble/bin/esptool --port /dev/cu.usbmodem101 --before usb-reset chip-id

- **bleak** — BLE from macOS CoreBluetooth, used by `tools/blescan.py`.

## Verifying BLE without the phone

    ~/tools/venv-ble/bin/python tools/blescan.py scan 10      # who is advertising
    ~/tools/venv-ble/bin/python tools/blescan.py gatt <name>  # dump a GATT table
    ~/tools/venv-ble/bin/python tools/blescan.py watch <name> # subscribe, print frames

`scan` flags anything advertising `FE00` (the belt) or `1826` (FTMS — what the
bridge must look like to Kinomap). macOS hides BD_ADDRs behind per-Mac
CoreBluetooth UUIDs, so match by name.

**BLE allows one connection at a time.** If the belt is paired to the iPhone it
will not appear here — disconnect it there first.

## Phone side

    export PATH=$PATH:~/tools/scrcpy          # adb lives here
    adb -s 100.126.233.97:46363 shell ...

Installed for testing: **Kinomap** (`com.kinomap.training`) and **KS Fit**
(`com.kingsmith.xiaojin`, 6.6.0 — the vendor app, kept for A/B comparison).

Driving the UI headlessly: `adb shell uiautomator dump` to find elements,
`adb shell input tap X Y`, `adb exec-out screencap -p > shot.png`. Re-pair ADB
if it goes stale — wireless-debugging ports are random (see the memory note).

## Related

Vendor-app analysis lives in the separate private repo `~/src/ksfit-decompile`
(binaries in `~/apks/ksfit`). Nothing vendor-derived belongs in this repo.
