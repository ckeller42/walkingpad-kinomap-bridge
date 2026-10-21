# Troubleshooting

Most of these were found the hard way during bring-up. They look like faults
and are not.

## The app will not pair

Three preconditions must all hold, and the app's error message distinguishes
none of them.

1. **The belt is switched on and linked.** `status` shows `belt_link=1`.
2. **Nothing else holds the belt.** The belt accepts **one BLE connection**, so
   the KS Fit app, or Kinomap on your other phone, blocks the bridge in a way
   that is indistinguishable from the belt being off.
3. **The belt is moving during the confirmation step.** Kinomap's check reads
   live values, and a belt sitting at 0.00 km/h presents the same thing as no
   data at all. **ARM, then START, then pair.**

The setup wizard's last screen says so, and a separate screen appears when the
belt is off.

## The app connects but the training will not start

The belt almost certainly **stopped by itself**. With nobody walking on it, the
R1 Pro auto-stops about 15 seconds after starting. Kinomap then sees 0.00 km/h
and waits at "Begin running to start the training…".

ARM and START again, and step on.

## I cannot set the speed from the app

Expected. Kinomap does not command treadmill speed for a generic FTMS machine —
see [How it works](architecture.md). Use the `+` and `−` buttons on the bridge,
or `speed <km/h>` on the console.

## Nothing happens when I touch the screen

Check which firmware is on the device. The **screenshot build**
(`cores3_shot`) plays a scripted tour and is not meant for interactive use.
Flash the normal build:

```sh
pio run -e cores3 -t upload --upload-port /dev/cu.usbmodem101
```

The main screen's controls are also deliberately inert while the setup wizard
or the developer menu is open. Both have a way out on the bottom bar.

## The bridge reboots whenever I connect over USB

That is the hardware, not a crash: **opening the serial port resets the
ESP32-S3**. It drops the belt link and the phone's GATT link, which puts
Kinomap into a "Your equipment is connecting…" loop that a stale session does
not recover from — restart the workout.

To watch a live session, use the [UDP log](logging.md) instead. It needs no
serial port.

## `status` says `belt_link=0`

The belt is off, asleep, or connected to something else. Wake it and give the
bridge about ten seconds to find it. A scan will show whether it is on the air:

```sh
tools/blescan.py scan 12      # the belt advertises as "RE" with service FE00
```

## The SD card is not detected

`log` reporting `type=0` means **no card responded at all**, which happens
before any filesystem is read — so reformatting will not help. Reseat the card
until it clicks, or try another one.

An exFAT card looks different: it is detected (`type` 1, 2 or 3) and then fails
to *mount*, because the ESP32 SD library reads FAT16 and FAT32 only. Most cards
above 32 GB ship exFAT.

## Nothing arrives on the UDP log

```text
log        # check dev=1, udp=1 and that an ip is shown
```

Developer mode is the master switch; with it off the radio never comes up,
whatever `log_udp` says. Also check the listener is on the same network, and
note that macOS asks once for local-network permission.

## An OTA update failed

The running firmware is untouched — the image is written to the inactive slot
and only committed after its SHA-256 matches the manifest. `ota` shows what
went wrong:

| Message | Meaning |
|---|---|
| `no network` | developer mode off, or Wi-Fi not associated |
| `manifest http 404` | the URL is wrong, or the release has no manifest |
| `sha256 mismatch` | the download does not match the manifest; nothing installed |
| `short download` | the connection dropped partway |

A private repository's release assets need authentication, which the device
does not have. Serve the manifest and binary from somewhere the device can
reach.

## Recovering a bricked-looking bridge

The bridge cannot brick the belt — it only ever emits command group `0xA2`,
enforced at a single write choke point. The bridge itself is always
recoverable over USB:

```sh
pio run -e cores3 -t upload --upload-port /dev/cu.usbmodem101
```

If the board does not appear, power-cycle it first; the CoreS3's flash chip
occasionally comes up unresponsive after an abrupt reset.
