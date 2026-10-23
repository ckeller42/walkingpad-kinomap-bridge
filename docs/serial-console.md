# Serial console and screenshots

The CoreS3 has no remote input channel except this one, so it is the supported
way to drive the bridge for testing. It rides on the USB serial port
(`-DARDUINO_USB_CDC_ON_BOOT=1` makes Arduino `Serial` the USB-Serial-JTAG
port; UART0 goes nowhere on this board).

## Console

One command per line. Every command prints zero or more information lines and
then exactly one terminating line, `ok` or `err <reason>`.

| Command | Effect |
| --- | --- |
| `status` | one line of `key=value`: `armed running belt_link phone_link belt_state speed target wizard src setup_done` (`speed` in 0.1 km/h, `target` in 0.01 km/h as Kinomap sends it, `belt_state=-1` when the reading is stale) |
| `arm` / `disarm` | like the ARM button; `arm` is `err wizard-active` during setup |
| `start` | like the START button; `err not-armed`, `err already-running`, `err belt-down`, `err wizard-active` |
| `stop` | like STOP (also disarms) |
| `sim on` / `sim off` | `ok` if already so, otherwise `err reboot-required` (the belt source is bound at boot) |
| `wizard` | re-enter setup (`err belt-running` if the belt is moving); `wizard next` / `wizard back` press the right / left button |
| `speed up` / `speed down` / `speed <km/h>` | like the speed buttons; `err not-armed`, `err belt-down`, `err wizard-active`, `err bad-value`, `err out-of-range`, `err auto-mode` (the belt sets its own speed in Auto) |
| `mode auto` / `mode manual` | asks the belt to switch mode, like tapping the badge; `err wizard-active`, `err not-armed`, `err belt-down`, `err belt-moving` (belt not parked or a start in flight), `err refused` |
| `shot` | emit one screenshot frame now (`err not-built` outside the `cores3_shot` build) |
| `help` | list commands |

Anything else is `err unknown-command`. All commands go through the same
`BridgeState`, `applyOutcome` and `BeltGate` paths as the touch UI, so
`start` while disarmed is refused exactly as the button is.

```sh
~/tools/venv-ble/bin/python tools/console.py status
~/tools/venv-ble/bin/python tools/console.py "arm; start; wait 8; status; stop"
```

`tools/console.py` exits 0 on `ok`, 1 on `err`, 2 on timeout. It opens the port
the way the kws-de `console-bar.sh` does (DTR and RTS low before and after
open, 3 s settle, drain). **Opening the port resets the board**, so armed,
running and wizard state do not survive between invocations: run a whole
scenario in one invocation (commands separated by `;`, plus a host-side
`wait <seconds>`). Only the stored `setup_done` flag persists.

## Screenshots

`pio run -e cores3_shot` is the simulated-belt firmware plus `-DUI_SCREENSHOT=1`.
It emits one frame whenever the rendered screen changes phase (each wizard
screen, each main-screen face) and on `shot`:

```text
[SHOT 320 240 RLE16 <n>]
S:<base64>            (128 characters per line)
[/SHOT]
```

The base64 decodes to `n` runs of 3 bytes: count (1 to 255), then the RGB565
pixel as high byte, low byte, row-major. Frames are about 20 to 45 KB of
base64. Every payload line is one write with an `S:` prefix, so an interleaved
log line cannot corrupt it. After boot the shot build plays a scripted tour of
every screen (4 s each); any console command ends the tour.

```sh
~/tools/venv-ble/bin/pio run -e cores3_shot -t upload --upload-port /dev/cu.usbmodem101
~/tools/venv-ble/bin/python tools/capture_shots.py --port /dev/cu.usbmodem101 \
    --seconds 62 --outdir docs/screens --names a,b,c
# deterministic single frames instead of the tour:
~/tools/venv-ble/bin/python tools/console.py --keep-frames "arm; start; wait 12; shot" > cap.txt
~/tools/venv-ble/bin/python tools/decode_shots.py cap.txt -o docs/screens
```

`tools/decode_shots.py` writes one PNG per distinct complete frame and reports
truncated or damaged ones (capture at least 12 s per frame, and the whole tour
needs about 60 s). The shot build is for documentation only: it never writes
the `setup_done` flag.

## Status fields for split status frames

`status` reports two counters from the belt link:

```text
rx_join=0 rx_lost=0
```

- **`rx_join`** — status frames rejoined from two or more notifications. The
  belt can split its 20-byte frame across two FE01 notifications, and parsing
  each alone would reject both halves and lose the reading.
- **`rx_lost`** — fragments abandoned without ever completing, for instance
  when the tail never arrived and a fresh frame started instead.

Both read 0 on the simulated belt. A steadily climbing `rx_lost` would mean
readings are being dropped, which matters because `STATUS_STALE_MS` is 5000:
enough consecutive losses and the bridge declares a running belt down.
