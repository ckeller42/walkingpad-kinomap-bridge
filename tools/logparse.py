#!/usr/bin/env python3
"""Decode a captured bridge log into a readable timeline.

    tools/logparse.py run.log            # timeline + summary
    tools/logparse.py run.log --summary  # summary only

Reads what logcat.py captured over UDP, or a bridge-NNN.log lifted off the SD
card. Both carry the same lines.

The raw log is deliberately verbatim hex, because a decoder that silently
mis-reads a frame is worse than no decoder. This turns it into prose using the
field layout recorded in protocol/wilink.yaml and protocol/ftms.yaml, and
flags anything it cannot account for rather than skipping it.
"""
import argparse
import collections
import pathlib
import re
import sys

BELT_STATE = {
    0: "idle", 1: "running", 3: "waking", 4: "standby",
    6: "starting/decel", 7: "countdown", 9: "pre-countdown",
}
MODE = {0: "auto", 1: "manual", 2: "standby"}
WILINK_FN = {0x00: "request status", 0x01: "set speed", 0x02: "set mode", 0x04: "START"}
CP_OP = {
    0x00: "Request Control", 0x02: "Set Target Speed", 0x03: "Set Target Inclination",
    0x07: "Start/Resume", 0x08: "Stop/Pause",
}

LINE = re.compile(r"^(?:\S+\s+)?(\d+)\s+(\S+)\s*(.*)$")


def hexbytes(rest):
    try:
        return bytes(int(b, 16) for b in rest.split())
    except ValueError:
        return None


def describe_tx(b):
    """A command the bridge wrote to the belt (FE02)."""
    if len(b) < 6 or b[0] != 0xF7:
        return f"malformed tx: {b.hex(' ')}"
    group, fn, arg = b[1], b[2], b[3]
    if group != 0xA2:
        return f"!! NON-0xA2 GROUP {group:#04x} - must never happen"
    name = WILINK_FN.get(fn, f"fn {fn:#04x}")
    if fn == 0x01:
        return f"{name} {arg / 10:.1f} km/h"
    if fn == 0x02:
        return f"{name} {MODE.get(arg, arg)}" + (" (= STOP)" if arg == 2 else "")
    return name


def describe_rx(b):
    """A status frame the belt sent (FE01)."""
    if len(b) < 5 or b[0] != 0xF8:
        return None, f"malformed rx: {b.hex(' ')}"
    st, speed, mode = b[2], b[3], b[4]
    return (st, speed), (f"state {st} ({BELT_STATE.get(st, '?')})  "
                         f"{speed / 10:.1f} km/h  mode {MODE.get(mode, mode)}")


def describe_cp(b):
    """A Control Point write from the phone (2AD9)."""
    if not b:
        return "empty write"
    op = CP_OP.get(b[0], f"op {b[0]:#04x}")
    if b[0] == 0x02 and len(b) >= 3:
        return f"{op} {(b[1] | (b[2] << 8)) / 100:.2f} km/h"
    if b[0] == 0x03 and len(b) >= 3:
        val = b[1] | (b[2] << 8)
        if val >= 0x8000:
            val -= 0x10000
        return f"{op} {val / 10:.1f} %"
    return op


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log", type=pathlib.Path)
    ap.add_argument("--summary", action="store_true", help="counts only")
    args = ap.parse_args()

    if not args.log.exists():
        sys.exit(f"no such log: {args.log}")

    counts = collections.Counter()
    cp_ops = collections.Counter()
    commands = collections.Counter()
    speeds = []
    states = []
    unknown = []
    t0 = None
    rows = []

    for raw in args.log.read_text(errors="replace").splitlines():
        if not raw.strip():
            continue
        m = LINE.match(raw.strip())
        if not m:
            unknown.append(raw)
            continue
        t, kind, rest = int(m.group(1)), m.group(2), m.group(3)
        t0 = t if t0 is None else t0
        secs = (t - t0) / 1000.0
        counts[kind] += 1

        if kind in ("tx", "tx-fail", "tx-nolink"):
            b = hexbytes(rest)
            if b is None:
                unknown.append(raw)
                continue
            text = describe_tx(b)
            commands[text.split(" km/h")[0]] += 1
            rows.append((secs, "bridge -> belt", text + ("" if kind == "tx" else f"  [{kind}]")))
        elif kind == "rx":
            b = hexbytes(rest)
            if b is None:
                unknown.append(raw)
                continue
            pair, text = describe_rx(b)
            if pair:
                states.append(pair[0])
                speeds.append(pair[1] / 10.0)
            rows.append((secs, "belt -> bridge", text))
        elif kind == "cp":
            b = hexbytes(rest)
            if b is None:
                unknown.append(raw)
                continue
            cp_ops[b[0] if b else -1] += 1
            rows.append((secs, "phone -> bridge", describe_cp(b)))
        elif kind == "emit":
            rows.append((secs, "policy", rest))
        elif kind == "udp-seq":  # the sink's per-datagram header; a jump in n is a lost datagram
            rows.append((secs, kind, rest))
        else:
            rows.append((secs, kind, rest))

    if not args.summary:
        print(f"{'time':>8}  {'direction':<16} what")
        print(f"{'-'*8}  {'-'*16} {'-'*44}")
        for secs, direction, text in rows:
            print(f"{secs:8.1f}  {direction:<16} {text}")
        print()

    print("=== summary ===")
    print(f"duration        {(rows[-1][0] if rows else 0):.1f} s, {sum(counts.values())} events")
    print(f"event kinds     {dict(counts)}")
    if commands:
        print("commands sent:")
        for c, n in commands.most_common():
            print(f"   {n:>3}  {c}")
    if cp_ops:
        print("control point writes from the phone:")
        for op, n in sorted(cp_ops.items()):
            print(f"   {n:>3}  {CP_OP.get(op, f'op {op:#04x}')}")
        if 0x02 not in cp_ops:
            print("   note: no 0x02 Set Target Speed - expected for Kinomap, which")
            print("         does not command speed for a generic FTMS treadmill")
    if speeds:
        print(f"belt speed      min {min(speeds):.1f}  max {max(speeds):.1f} km/h")
    if states:
        seen = []
        for st in states:
            if not seen or seen[-1] != st:
                seen.append(st)
        print("belt states     " + " -> ".join(f"{s}({BELT_STATE.get(s,'?')})" for s in seen))
    if unknown:
        print(f"unparsed lines  {len(unknown)} (shown so a decoder gap is visible, not hidden)")
        for u in unknown[:5]:
            print(f"   {u}")


if __name__ == "__main__":
    main()
