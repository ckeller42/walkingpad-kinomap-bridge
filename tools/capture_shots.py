#!/usr/bin/env python3
"""Capture serial screenshots from a cores3_shot build and decode them to PNG.

    ~/tools/venv-ble/bin/pio run -e cores3_shot -t upload --upload-port /dev/cu.usbmodem101
    ~/tools/venv-ble/bin/python tools/capture_shots.py --port /dev/cu.usbmodem101 \\
        --seconds 100 --outdir docs/screens --names welcome,scan,...

The script pulses RTS to hard-reset the chip (the same way esptool does), so the
capture starts with the boot log and the scripted tour begins at the first screen.
A bare open() is not reliable: it sometimes resets and sometimes leaves the port silent.
Capture long enough for the whole tour (about 4 s per screen) and at least 12 s
per frame: a frame cut off by the end of the capture is dropped, not guessed.
The raw text goes to --raw (default /tmp/shots-capture.txt; large, do not commit).
"""
import argparse
import sys
import time
from pathlib import Path

import serial  # pyserial

sys.path.insert(0, str(Path(__file__).resolve().parent))
from decode_shots import decode_text  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True)
    ap.add_argument("--seconds", type=float, default=100)
    ap.add_argument("--outdir", default="docs/screens", type=Path)
    ap.add_argument("--names", default="")
    ap.add_argument("--raw", type=Path, default=Path("/tmp/shots-capture.txt"))
    a = ap.parse_args()

    buf = bytearray()
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = a.port, 115200, 0.2
    s.dtr = False  # GPIO0 high: boot normally, not into the ROM loader
    s.rts = False
    with s:
        s.dtr = False
        s.rts = False
        # Explicit hard reset (RTS drives EN). Relying on the open() itself to
        # reset the chip is the trap: it sometimes does not, and the port stays silent.
        s.rts = True
        time.sleep(0.15)
        s.rts = False
        end = time.time() + a.seconds
        while time.time() < end:
            buf += s.read(65536)
    text = buf.decode("utf-8", "replace")
    a.raw.write_text(text)
    print(f"captured {len(buf)} bytes -> {a.raw}")
    names = [x for x in a.names.split(",") if x]
    written, bad = decode_text(text, a.outdir, names)
    print(f"{len(written)} PNG(s), {bad} damaged/truncated frame(s)")
    return 0 if written and not bad else 1


if __name__ == "__main__":
    sys.exit(main())
