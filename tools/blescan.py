#!/usr/bin/env python3
"""BLE recon from this Mac, so the bridge can be verified without the phone.

  blescan.py scan [seconds]        list every advertiser, flagging the ones we care about
  blescan.py gatt <addr-or-name>   connect and dump the full GATT table
  blescan.py watch <addr-or-name>  subscribe to every notifiable characteristic, print frames

macOS note: CoreBluetooth hides BD_ADDRs and gives UUID handles instead, so match
by name where you can. The handle is stable per-Mac, so it is fine to paste back in.
"""
import asyncio
import sys

from bleak import BleakClient, BleakScanner

# Things worth calling out in a scan.
KNOWN = {
    "0000fe00-0000-1000-8000-00805f9b34fb": "KingSmith WiLink (the belt)",
    "00001826-0000-1000-8000-00805f9b34fb": "FTMS (what Kinomap looks for)",
    "0000180d-0000-1000-8000-00805f9b34fb": "Heart Rate",
    "0000180a-0000-1000-8000-00805f9b34fb": "Device Information",
}
NAME_HINTS = ("walkingpad", "kingsmith", "ks-", "r1", "wp-", "bridge", "cores3")


def interesting(dev, adv):
    name = (adv.local_name or dev.name or "").lower()
    if any(h in name for h in NAME_HINTS):
        return True
    return any(u.lower() in KNOWN for u in adv.service_uuids or [])


async def cmd_scan(seconds):
    print(f"scanning {seconds}s …\n")
    found = await BleakScanner.discover(timeout=seconds, return_adv=True)
    rows = sorted(found.values(), key=lambda t: -(t[1].rssi or -999))
    hits = []
    for dev, adv in rows:
        name = adv.local_name or dev.name or "(no name)"
        flag = "  <<<" if interesting(dev, adv) else ""
        print(f"{adv.rssi:>5} dBm  {dev.address}  {name}{flag}")
        for u in adv.service_uuids or []:
            if u.lower() in KNOWN:
                print(f"              service {u}  = {KNOWN[u.lower()]}")
        if flag:
            hits.append((dev, adv))
    print(f"\n{len(rows)} advertisers, {len(hits)} interesting")
    if not hits:
        print("nothing matched — is the belt powered on and not already "
              "connected to a phone? BLE allows only one connection.")


async def resolve(target):
    """Accept a CoreBluetooth UUID handle or a (partial, case-insensitive) name."""
    found = await BleakScanner.discover(timeout=8.0, return_adv=True)
    for dev, adv in found.values():
        if dev.address.lower() == target.lower():
            return dev
    for dev, adv in found.values():
        name = (adv.local_name or dev.name or "").lower()
        if target.lower() in name:
            return dev
    raise SystemExit(f"no advertiser matched {target!r}")


async def cmd_gatt(target):
    dev = await resolve(target)
    print(f"connecting to {dev.address}  {dev.name}\n")
    async with BleakClient(dev) as c:
        for svc in c.services:
            print(f"service {svc.uuid}  {svc.description}")
            for ch in svc.characteristics:
                print(f"   char {ch.uuid}  {','.join(ch.properties)}  {ch.description}")
                for d in ch.descriptors:
                    print(f"       desc {d.uuid}")


async def cmd_watch(target):
    dev = await resolve(target)
    print(f"connecting to {dev.address}  {dev.name}\n")

    def on_notify(ch, data):
        print(f"{ch.uuid}  {data.hex(' ')}")

    async with BleakClient(dev) as c:
        n = 0
        for svc in c.services:
            for ch in svc.characteristics:
                if "notify" in ch.properties or "indicate" in ch.properties:
                    try:
                        await c.start_notify(ch, on_notify)
                        n += 1
                    except Exception as e:  # some chars refuse; keep going
                        print(f"  (skipped {ch.uuid}: {e})")
        print(f"subscribed to {n} characteristics — Ctrl-C to stop\n")
        while True:
            await asyncio.sleep(1)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    cmd = sys.argv[1]
    if cmd == "scan":
        secs = float(sys.argv[2]) if len(sys.argv) > 2 else 10.0
        asyncio.run(cmd_scan(secs))
    elif cmd == "gatt":
        asyncio.run(cmd_gatt(sys.argv[2]))
    elif cmd == "watch":
        asyncio.run(cmd_watch(sys.argv[2]))
    else:
        print(__doc__)


if __name__ == "__main__":
    main()
