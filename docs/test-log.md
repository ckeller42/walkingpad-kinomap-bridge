# Test log

## Stage 2 (partial) — live belt GATT capture, 2026-10-21

Taken from this Mac with `tools/blescan.py` while the belt was in pairing mode.
No firmware involved; no commands were written to the belt.

### Advertisement

| | |
|---|---|
| Advertised name | **`RE`** |
| CoreBluetooth handle (this Mac) | `202FB137-0854-BDD4-C086-789C12A093CA` |
| RSSI at capture | −53 dBm |
| Advertised service | `0000fe00-0000-1000-8000-00805f9b34fb` |

### GATT table

```
service 0000180a   Device Information
   char 00002a25   read                      Serial Number String
   char 00002a26   read                      Firmware Revision String
   char 00002a28   read                      Software Revision String
   char 00002a24   read                      Model Number String
   char 00002a29   read                      Manufacturer Name String
service 0000fe00   Vendor specific
   char 0000fe01   notify, read              status
       desc 00002902 (CCCD)
   char 0000fe02   write-without-response    commands
service 00010203-0405-0607-0809-0a0b0c0d1912   (OTA)
   char 00010203-0405-0607-0809-0a0b0c0d2b12   read, write-without-response
       desc 00002901
```

### Device Information strings as read

| Characteristic | Value |
|---|---|
| Firmware Revision (`2A26`) | `M26_V187.1.2` |
| Model Number (`2A24`) | `WLT8266M` |
| Serial Number (`2A25`) | `"Serial Number"` |
| Software Revision (`2A28`) | `"Hardware Revision"` |
| Manufacturer Name (`2A29`) | `"Software Revision"` |

### What this confirms, and what it changes

**Confirms** — `FE00` / `FE01` / `FE02` exist on this unit with the roles the
design assumed, so the WiLink protocol reference is correct about the transport.
There is **no FTMS (`1826`) service**: the belt is WiLink-only, as expected.

**Changes the plan — `FE02` is `write-without-response`.** Task 14's `BeltLink`
must write without response and will get **no acknowledgement and no flow
control** from the belt. Combined with the belt silently dropping commands sent
less than ~0.7 s apart, this means the bridge cannot know a command landed except
by polling `FE01` and watching the speed change. The existing rate limit is the
only backpressure there is.

**Changes the plan — the belt does not push status.** Subscribing to `FE01` and
waiting 10 s yielded **zero** unsolicited notifications. Status must be
*requested* (`F7 A2 00 00 A2 FD`) and the reply read from `FE01`. The 1 Hz poll
in the design is therefore required, not merely a convenience.

**Worth knowing** — three of the five Device Information strings are mis-wired in
the belt's own firmware: Serial Number returns the literal text "Serial Number",
Software Revision returns "Hardware Revision", and Manufacturer Name returns
"Software Revision". Only Firmware Revision and Model Number carry real values,
and `WLT8266M` identifies the Bluetooth module rather than the treadmill. Do not
rely on any of these to identify the machine.

### Still outstanding from Stage 2

- **The stop frame has not been captured.** It needs either an HCI snoop log of
  the KS Fit app stopping the belt (Developer Options toggle, cannot be set over
  ADB), or an active probe writing candidate frames to `FE02` and watching the
  belt — which nobody has authorised.
- Status frame byte offsets for distance and steps are still unverified: that
  needs the belt moving, which this capture deliberately did not do.
