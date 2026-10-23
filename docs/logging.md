# Session logging

Opening the serial port **reboots the ESP32-S3**. That drops the belt link and
the phone's GATT link, which is exactly what leaves Kinomap stuck in
"Your equipment is connecting…". So the console — which is otherwise the best
diagnostic the bridge has — is unusable during precisely the sessions worth
watching.

For one whole afternoon that meant every finding from a real walk had to be
inferred from the phone's screen instead of counted, and one claim in the test
log had to be downgraded afterwards because the control-point counters had
never been readable.

Two sinks close that gap. Neither touches the serial port.

| Sink | Good for | Survives a reboot |
|---|---|---|
| **UDP broadcast** | watching a live session | no |
| **SD card** | what happened before it died | yes |

Both are fed from one ring buffer, and all I/O happens in `loop()` — never in a
BLE callback, where an SD write of tens of milliseconds would stall the link it
is trying to observe.

## Turning it on

Logging with a radio is a **debug feature**, so a shipped firmware never brings
Wi-Fi up at all. Two persisted flags gate it, because they are different
scopes:

- **`dev_mode`** — the master switch for anything with a radio (Wi-Fi pairing,
  the UDP broadcast, OTA later).
- **`log_udp`** — the broadcast itself, inside developer mode.

One flag would have meant switching Wi-Fi on just to reach the menu that
switches Wi-Fi off. Both default to false and live in NVS.

Over USB:

```text
dev on              # master switch; brings Wi-Fi up if credentials are stored
log udp on          # the broadcast. Answers "err dev-mode-off" if dev is off
log                 # sinks, SD state, IP, dropped-line count
dev off             # takes the radio down immediately
```

On the device: tap the **PHONE** indicator on the main screen to open the
developer menu (the **BELT** indicator re-opens setup), where the same switches
and Wi-Fi pairing live.

## Wi-Fi credentials

They live in NVS on the device and never in this repository.

```sh
tools/set_wifi.sh Insel      # prompts for the password in a hidden dialog
```

The dialog is `zenity` where present and `osascript` on macOS. The password is
never echoed, never written to a file, and the console's reply deliberately
prints the SSID only. The parser reads the **raw** console line rather than the
normalised one, because normalising lower-cases and collapses whitespace —
either of which silently stores a credential that never works.

You can also pair from the device itself, with a network scan and an on-screen
keyboard, under the developer menu.

## Reading a live session

```sh
tools/logcat.py                 # UDP broadcast on port 9999
tools/logcat.py run.log         # and append to a file
```

The bridge broadcasts to the subnet address, so nothing needs configuring on
the listening side beyond being on the same network. If nothing arrives, check
`log` on the console for the Wi-Fi state; macOS also asks once for local-network
permission.

The broadcast is **not lossless**. About one second of lines goes out as one
datagram, and broadcast frames are not acknowledged, so a dropped datagram is a
silent one-second gap. When completeness matters, read the SD sink as well.

### Sequence numbers and unicast

Because a broadcast frame is unacknowledged, one lost frame loses a second of
evidence, and before sequence numbers nothing showed that it had. Every
datagram now opens with a header line, which parses like any other log line:

```text
48211 udp-seq 57      n counts datagrams from 1 per boot; millis is the first line's
48211 emit SetSpeed tenths=25
```

A jump in `n` is a lost datagram, so a gap is visible instead of silent. To
make loss rare as well, the sink's socket is bound to port 9999 and listens: a
host that sends a datagram starting `hello` to `<bridge ip>:9999` becomes the
unicast target for 120 s, and while it is, every datagram goes out twice,
broadcast as before and unicast to that host. The unicast copy goes to the
sender's IP on port 9999, not to the port it said hello from; that works
because a listener binds 9999 to hear the broadcast anyway. Unicast frames are
acknowledged and retried by the Wi-Fi MAC. `log` shows the target as
`unicast=<ip>` (or `unicast=-`). The listener then sees each datagram twice
and drops the second copy by its header. A flush with nothing to send gives
the target alone a header-only heartbeat that repeats the last `n`, so a quiet
log can be told from a lost datagram without broadcast listeners seeing a gap.
`tools/e2e_sim.py` does all of this (hello on the first datagram, then every
60 s). A check whose window overlaps a gap still fails, and its evidence ends
in `; udp lost (seq a->b)`, which says why. `tools/logcat.py` only listens, so
it gets the broadcast and prints the header lines as they are. Two listeners
on one Mac (say `logcat.py` and the harness, both bound to 9999) each get
every broadcast, but the unicast copy reaches only one of them, so either may
see only some of the unicast copies.

## What is recorded

At the choke points, so nothing slips past:

```text
8176  tx f7 a2 00 00 a2 fd        every FE02 write, verbatim
8252  rx f8 a2 04 00 02 ... fd    every FE01 status frame, verbatim
53778 cp 00                       every FTMS Control Point write
55810 emit StartSequence tenths=25  every policy decision
```

Frames are logged as **verbatim hex, not decoded values**. This is deliberate:
a decoder in the logging path throws away whatever it fails to understand, and
what it fails to understand is exactly what you need. The fragmented status
frame in issue #3 — a `F8 A2` frame split across two BLE notifications — was
only ever visible because the raw bytes were written down.

## Decoding it afterwards

```sh
tools/logparse.py run.log             # timeline and summary
tools/logparse.py run.log --summary   # summary only
```

It decodes using the field layouts in `protocol/wilink.yaml` and
`protocol/ftms.yaml`, and **prints the lines it could not parse** rather than
skipping them, so a gap in the decoder is visible instead of silent.

```text
    time  direction        what
   18.3  phone -> bridge  Set Target Speed 2.50 km/h
   18.3  policy           StartSequence tenths=25
   19.2  bridge -> belt   set mode manual
   20.2  bridge -> belt   START
   25.3  belt -> bridge   state 1 (running)  0.4 km/h  mode manual
   26.3  bridge -> belt   set speed 2.5 km/h

=== summary ===
control point writes from the phone:
     1  Request Control
     1  Set Target Speed
belt states     4(standby) -> 9(pre-countdown) -> 8(?) -> 6(starting/decel) -> 1(running)
```

The summary calls out a missing `0x02` explicitly, because for Kinomap its
absence is expected rather than a fault — see the [test log](test-log.md).

## SD card

Pins come from M5Unified's own per-board table rather than being hardcoded; on
a CoreS3 that is `sck=36 miso=35 mosi=37 cs=4`. The mount is retried at 25, 10,
4 and 1 MHz.

`log` reports `type=0` when no card responds **at all**. That is card
presence — it happens before any filesystem is read — so reformatting will not
help. An exFAT card looks different: it is detected (`type` 1, 2 or 3) and then
fails to mount, because the ESP32 SD library reads FAT16 and FAT32 only, and
most cards above 32 GB ship exFAT.
