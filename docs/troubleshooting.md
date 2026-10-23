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

## The touchscreen stops responding entirely

**Power the device off completely and back on.** Not a reset, not a re-flash:
unplug USB and hold the power button for about six seconds so the AXP2101
power-management chip actually cycles.

The CoreS3's touch controller shares an I2C bus with the PMU and the AW9523 IO
expander, and after many abrupt resets - a day of flashing, for instance - it
can latch in a state where it answers nothing. The display keeps working, which
makes it look like a software fault.

How to tell this apart from a bug in the firmware, rather than guessing:

```text
touch       # on the console
```

```text
touch enabled=1 count=0 max=0 samples=803 ... board=10
```

- **`samples` climbing** - the panel is being polled; the loop is alive.
- **`max` still 0** - no contact has been seen since boot, at any point.
- **`enabled=1`, `board=10`** - the driver is up and the board is correctly
  detected as a CoreS3.

That combination means the controller is being asked and is answering nothing,
which is the latch-up signature. A fault in the touch zones or the press
latching would show `max` above 0.

To settle it beyond doubt, flash the minimal probe, which contains none of this
project's code, no BLE, no Wi-Fi and the stock partition table:

```sh
pio run -e cores3_touchtest -t upload --upload-port /dev/cu.usbmodem101
```

Touch the screen; green dots follow your finger when the hardware is working.
Then restore the bridge with `pio run -e cores3 -t upload`.

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
and only committed after its SHA-256 matches the manifest. The Update screen's
message row (its first 23 characters) and `ota` on the console show what went
wrong:

| Message | Meaning |
|---|---|
| `no network` | developer mode off, or Wi-Fi not associated |
| `manifest http 404` | the URL is wrong, or the release has no manifest |
| `sha256 mismatch` | the download does not match the manifest; nothing installed |
| `short download` | the connection dropped partway |
| `image is for <env>` | the published image was built for another PlatformIO environment |

A `manifest http 404` on the default URL means nothing has been published to
GitHub Pages yet, or Pages is not enabled for the repository. A manifest that
needs authentication, such as a private repository's release asset, fails too:
the device has no credentials. [Serve it
locally](ota.md#serving-locally) instead.

## Recovering a bricked-looking bridge

The bridge cannot brick the belt — it only ever emits command group `0xA2`,
enforced at a single write choke point. The bridge itself is always
recoverable over USB:

```sh
pio run -e cores3 -t upload --upload-port /dev/cu.usbmodem101
```

If the board does not appear, power-cycle it first; the CoreS3's flash chip
occasionally comes up unresponsive after an abrupt reset.
