#!/usr/bin/env python3
"""Act as an FTMS client against the bridge and write Set Target Speed.

Kinomap will not send 0x02 unless the account flag controlSpeedTreadmill is on,
which cannot be reached from this account. This stands in for Kinomap and
exercises exactly the same path: Request Control, then 0x02 with a target in
km/h x 100, little-endian, which is the encoding ag8.y0() uses.

Proves the bridge's half: 0x02 in -> belt speed command out.
"""
import asyncio
import struct
import sys
from bleak import BleakClient, BleakScanner

CP = "00002ad9-0000-1000-8000-00805f9b34fb"   # Control Point
TD = "00002acd-0000-1000-8000-00805f9b34fb"   # Treadmill Data
indications = []
speeds = []


def on_cp(_h, data):
    b = bytes(data)
    indications.append(b)
    print(f"  <- indication {b.hex(' ')}", flush=True)


def on_td(_h, data):
    b = bytes(data)
    if len(b) >= 4:
        kmh = struct.unpack_from("<H", b, 2)[0] / 100.0
        speeds.append(kmh)
        print(f"  <- 2ACD speed {kmh:.2f} km/h", flush=True)


async def main():
    target_kmh = float(sys.argv[1]) if len(sys.argv) > 1 else 3.0
    dev = None
    for d, adv in (await BleakScanner.discover(timeout=12.0, return_adv=True)).values():
        if "WalkingPad Bridge" in (adv.local_name or d.name or ""):
            dev = d
            break
    if not dev:
        sys.exit("bridge not advertising (is the phone holding the connection?)")
    print(f"bridge at {dev.address}", flush=True)

    async with BleakClient(dev) as c:
        await c.start_notify(CP, on_cp)
        await c.start_notify(TD, on_td)

        print("-> 00 Request Control", flush=True)
        await c.write_gatt_char(CP, bytes([0x00]), response=True)
        await asyncio.sleep(2.0)

        payload = bytes([0x02]) + struct.pack("<H", int(round(target_kmh * 100)))
        print(f"-> 02 Set Target Speed {target_kmh} km/h  ({payload.hex(' ')})", flush=True)
        await c.write_gatt_char(CP, payload, response=True)
        await asyncio.sleep(12.0)

        print(f"\nindications: {[i.hex(' ') for i in indications]}")
        print(f"2ACD speeds seen: {speeds}")


asyncio.run(main())
