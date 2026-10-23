#!/usr/bin/env python3
"""Send one command to the bridge's serial console and print the reply.

    ~/tools/venv-ble/bin/python tools/console.py status
    ~/tools/venv-ble/bin/python tools/console.py --port /dev/cu.usbmodem101 arm

Commands: status arm disarm start stop | sim on|off | wizard [next|back] |
speed up|down|<km/h> | cp | shot | help.  Several commands can share one invocation, separated by ";"
(plus a host-side "wait <seconds>"); the first err stops the sequence.  Every reply ends with exactly one "ok" or "err <reason>" line;
this script prints everything up to and including it and exits 0 on ok, 1 on
err, 2 on timeout / no reply.

NOTE: every open of the port resets the board, so device state (armed, running,
wizard) does NOT survive between invocations: run a whole scenario in ONE
invocation, e.g.  "wizard next; wizard next; wizard next; arm; start; wait 8; status".
Only the stored setup_done flag persists.

Port handling follows the kws-de console-bar.sh recipe, because the ESP32-S3
USB-Serial-JTAG resets the chip on a careless open and can then stay silent:
DTR and RTS are held low before AND after open(), then --settle seconds (3)
let the boot log pass and are drained before the command is sent. Screenshot
frames ([SHOT ...] / "S:" payload lines) are filtered out of the output.
"""
import argparse
import glob
import sys
import time


def default_port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*"))
    if not ports:
        sys.exit("no serial port found; pass --port")
    return ports[0]


class Reader:
    """Line state kept across commands: a frame still streaming when one
    command's ok arrives must not be cut in the middle by the next command."""
    buf = b""
    in_frame = False


def open_port(port, settle=3.0):
    """Open `port` with the console-bar.sh recipe; return it settled, drained and ready."""
    import serial  # pyserial; imported here so default_port() works without it

    s = serial.Serial()
    s.port = port
    s.baudrate = 115200
    s.timeout = 0.2
    s.dtr = False
    s.rts = False
    s.open()
    s.dtr = False  # again, after open (console-bar.sh)
    s.rts = False
    time.sleep(settle)
    s.read(1 << 20)  # drain whatever the (possible) reset logged
    s.write(b"\n")  # flush any half-typed line; a blank line gets no reply
    return s


def keep_line(line, st):
    """False for screenshot frame lines ([SHOT ...] / S: payload / overlong), tracking frame state in `st`."""
    if line.startswith("[SHOT"):
        st.in_frame = True
        return False
    if line.startswith("[/SHOT]"):
        st.in_frame = False
        return False
    return not (st.in_frame or line.startswith("S:") or len(line) > 300)


def run_one(s, cmd, timeout, keep_frames, st):
    """Send one command, print its reply lines. Returns 0 ok, 1 err, 2 timeout."""
    s.write(cmd.encode() + b"\n")
    s.flush()
    end = time.time() + timeout
    while time.time() < end:
        st.buf += s.read(4096)
        while b"\n" in st.buf:
            raw, st.buf = st.buf.split(b"\n", 1)
            line = raw.decode("utf-8", "replace").strip()
            if not keep_frames and not keep_line(line, st):
                continue
            if not line:
                continue
            print(line, flush=True)
            if line == "ok":
                return 0
            if line.startswith("err"):
                return 1
    print("err timeout (no ok/err from device)", file=sys.stderr)
    return 2


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", nargs="+", help='e.g. status, or: sim on, or: "arm; start; wait 5; status"')
    ap.add_argument("--port", default=None)
    ap.add_argument("--timeout", type=float, default=5.0, help="seconds to wait for ok/err")
    ap.add_argument("--settle", type=float, default=3.0, help="seconds to let a reset's boot log pass")
    ap.add_argument("--keep-frames", action="store_true", help="do not filter [SHOT] frame lines")
    a = ap.parse_args()
    cmds = [c.strip() for c in " ".join(a.command).split(";") if c.strip()]

    s = open_port(a.port or default_port(), a.settle)
    try:
        st = Reader()
        for c in cmds:
            if c.startswith("wait "):  # host-side pause between commands
                time.sleep(float(c.split()[1]))
                continue
            print(f"> {c}", flush=True)
            rc = run_one(s, c, a.timeout, a.keep_frames, st)
            if rc:
                return rc
        return 0
    finally:
        s.close()


if __name__ == "__main__":
    sys.exit(main())
