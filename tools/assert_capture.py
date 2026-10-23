#!/usr/bin/env python3
"""Assert the measured protocol claims against a captured bridge log.

    tools/assert_capture.py docs/captures/auto-mode-2026-10-22.log
    tools/assert_capture.py run.log --claims auto_starts_on_step_on,only_group_a2
    tools/assert_capture.py --list

A fact in protocol/wilink.yaml with `provenance: measured` names a capture as
its evidence. This turns the sentence into a check: each claim below is a
predicate over the verbatim frames in the log, so CI re-derives the fact from
the same bytes every time instead of trusting the prose. A claim that stops
holding on a new capture is a finding, not a flaky test: either the belt
behaves differently (firmware, model) or the bridge regressed.

Lines are what logcat.py writes (`<ip> <millis> <kind> <rest>`). Status
frames the firmware received in pieces ("rx f8 a2 00" / "rx fragment
buffered" / "rx 00 00 ...") are reassembled here the way belt_link does it.
A reboot (millis going backwards) starts a new segment; no claim reasons
across a reboot.
"""
import argparse
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from logparse import LINE, hexbytes  # noqa: E402

STATUS_LEN = 20
FN_SET_SPEED, FN_SET_MODE, FN_START = 0x01, 0x02, 0x04
MODE_AUTO, MODE_MANUAL, MODE_STANDBY = 0, 1, 2
STATE_RUNNING, STATE_WAKING = 1, 3


class Event:
    __slots__ = ("seg", "t", "kind", "b", "rest", "line")

    def __init__(self, seg, t, kind, b, rest, line):
        self.seg, self.t, self.kind, self.b, self.rest, self.line = seg, t, kind, b, rest, line

    @property
    def state(self):
        return self.b[2]

    @property
    def speed(self):
        return self.b[3]

    @property
    def mode(self):
        return self.b[4]

    def __str__(self):
        return f"seg{self.seg} t={self.t} {self.kind} {self.b.hex(' ') if self.b else self.rest}"


def crc_ok(b):
    """WiLink CRC: the byte sum of everything between header and CRC, mod 256."""
    return len(b) >= 4 and (sum(b[1:-2]) & 0xFF) == b[-2]


def parse(text):
    """Events in order: tx (complete commands), rx (complete 20-byte status), other."""
    events, unparsed = [], []
    seg, last_t = 0, None
    frag = None  # (seg, t, bytes) of a status frame still being received
    for raw in text.splitlines():
        raw = raw.strip()
        if not raw:
            continue
        m = LINE.match(raw)
        if not m:
            unparsed.append(raw)
            continue
        t, kind, rest = int(m.group(1)), m.group(2), m.group(3)
        if last_t is not None and t < last_t:
            seg += 1
            frag = None
        last_t = t
        if kind == "rx":
            if rest.startswith("fragment"):
                continue
            b = hexbytes(rest)
            if b is None:
                unparsed.append(raw)
                continue
            if frag is not None and b[:1] != b"\xf8":
                b = frag[2] + b
                frag = None
            if b[:1] == b"\xf8" and len(b) < STATUS_LEN:
                frag = (seg, t, b)
                continue
            if len(b) != STATUS_LEN or b[0] != 0xF8 or b[-1] != 0xFD:
                unparsed.append(raw)
                continue
            events.append(Event(seg, t, "rx", b, rest, raw))
        elif kind in ("tx", "tx-fail", "tx-nolink"):
            b = hexbytes(rest)
            if b is None or len(b) < 6 or b[0] != 0xF7:
                unparsed.append(raw)
                continue
            events.append(Event(seg, t, kind, b, rest, raw))
        else:
            events.append(Event(seg, t, kind, None, rest, raw))
    return events, unparsed


# ---- claims ---------------------------------------------------------------
# Each returns (ok, evidence). Names match the fact ids in protocol/wilink.yaml
# where one exists; the rest are invariants this bridge promises.

def rx_before(events, i, within_ms):
    e = events[i]
    for j in range(i - 1, -1, -1):
        p = events[j]
        if p.seg != e.seg or e.t - p.t > within_ms:
            break
        yield p


def rx_after(events, i, within_ms):
    e = events[i]
    for j in range(i + 1, len(events)):
        n = events[j]
        if n.seg != e.seg or n.t - e.t > within_ms:
            break
        yield n


def is_tx(e, fn=None, arg=None):
    return e.kind == "tx" and (fn is None or e.b[2] == fn) and (arg is None or e.b[3] == arg)


def claim_only_group_a2(events):
    bad = [e for e in events if e.kind.startswith("tx") and e.b[1] != 0xA2]
    return not bad, (f"{len(bad)} non-0xA2 write(s): {bad[0]}" if bad else
                     f"{sum(e.kind.startswith('tx') for e in events)} writes, all group 0xA2")


def claim_crc_valid(events):
    bad = [e for e in events if e.b is not None and not crc_ok(e.b)]
    return not bad, (f"{len(bad)} bad CRC, first: {bad[0]}" if bad else
                     f"{sum(e.b is not None for e in events)} frames, every CRC matches")


def claim_auto_mode_in_status(events):
    for e in events:
        if e.kind == "rx" and e.mode == MODE_AUTO:
            return True, f"first status with mode 0: {e}"
    return False, "no status frame reported mode 0"


def claim_set_mode_auto_confirmed(events):
    tried = 0
    for i, e in enumerate(events):
        if not is_tx(e, FN_SET_MODE, MODE_AUTO):
            continue
        tried += 1
        for n in rx_after(events, i, 3000):
            if n.kind == "rx" and n.mode == MODE_AUTO:
                return True, f"{e}  ->  {n}  ({n.t - e.t} ms)"
    return False, (f"{tried} set_mode(0) write(s), none confirmed by a mode-0 status within 3 s"
                   if tried else "no set_mode(0) write in the capture")


def claim_auto_starts_on_step_on(events):
    prev = {}
    for i, e in enumerate(events):
        if e.kind != "rx":
            continue
        p = prev.get(e.seg)
        prev[e.seg] = e
        if p is None or e.mode != MODE_AUTO or e.state != STATE_RUNNING or p.state == STATE_RUNNING:
            continue
        writes = [w for w in rx_before(events, i, 10000)
                  if is_tx(w, FN_START) or is_tx(w, FN_SET_SPEED)]
        if not writes:
            return True, f"{p}  ->  {e}; no START/set_speed in the 10 s before"
    return False, "no idle->running transition in mode 0 without a bridge start/speed write"


def claim_set_speed_muted_in_auto(events):
    mode = {}
    leaks = []
    for e in events:
        if e.kind == "rx":
            mode[e.seg] = e.mode
        elif is_tx(e, FN_SET_SPEED) and mode.get(e.seg) == MODE_AUTO:
            leaks.append(e)
    seen = any(m == MODE_AUTO for m in mode.values()) or any(
        e.kind == "rx" and e.mode == MODE_AUTO for e in events)
    if not seen:
        return False, "capture never shows mode 0, nothing to check"
    return not leaks, (f"{len(leaks)} set_speed write(s) while the belt reported Auto, first: {leaks[0]}"
                       if leaks else "no set_speed write while the belt reported Auto")


def claim_parked_state_after_stop(events):
    stops = 0
    for i, e in enumerate(events):
        if not is_tx(e, FN_SET_MODE, MODE_STANDBY):
            continue
        stops += 1
        for n in rx_after(events, i, 15000):
            if n.kind == "rx" and n.state == STATE_WAKING and n.speed == 0:
                return True, f"{e}  ->  {n}  ({n.t - e.t} ms)"
    return False, (f"{stops} STOP write(s), none followed by state 3 at speed 0 within 15 s"
                   if stops else "no STOP write in the capture")


def claim_stop_leaves_auto(events):
    checked = 0
    for i, e in enumerate(events):
        if not is_tx(e, FN_SET_MODE, MODE_STANDBY):
            continue
        before = [p for p in rx_before(events, i, 5000) if p.kind == "rx"]
        if not before or before[0].mode != MODE_AUTO:
            continue
        checked += 1
        after = [n for n in rx_after(events, i, 3000) if n.kind == "rx"]
        if not after:
            return False, f"no status within 3 s after {e}"
        if after[0].mode == MODE_AUTO:
            return False, f"still mode 0 after STOP: {after[0]}"
    if not checked:
        return False, "no STOP issued while the belt reported Auto"
    return True, f"{checked} STOP(s) from Auto, each answered with a non-Auto mode"


CLAIMS = {
    "only_group_a2": (claim_only_group_a2, "every write is command group 0xA2 (0xE8 is OTA and can brick)"),
    "crc_valid": (claim_crc_valid, "every tx and reassembled rx frame carries a valid WiLink CRC"),
    "auto_mode_in_status": (claim_auto_mode_in_status, "the belt reports Auto as mode 0 in status byte 4"),
    "set_mode_auto_confirmed": (claim_set_mode_auto_confirmed, "set_mode(0) is answered by a mode-0 status within 3 s"),
    "auto_starts_on_step_on": (claim_auto_starts_on_step_on, "in Auto the belt goes running with no START or set_speed from the bridge"),
    "set_speed_muted_in_auto": (claim_set_speed_muted_in_auto, "the bridge never writes set_speed while the belt reports Auto"),
    "parked_state_after_stop": (claim_parked_state_after_stop, "after STOP the belt parks in state 3 at speed 0"),
    "stop_leaves_auto": (claim_stop_leaves_auto, "STOP from Auto leaves Auto (next status is not mode 0)"),
}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log", nargs="?", type=pathlib.Path)
    ap.add_argument("--claims", default="", help="comma-separated subset (default: all)")
    ap.add_argument("--list", action="store_true", help="print the claims and exit")
    a = ap.parse_args()
    if a.list:
        for k, (_, text) in CLAIMS.items():
            print(f"{k:<26} {text}")
        return 0
    if a.log is None or not a.log.exists():
        sys.exit(f"no such log: {a.log}")
    wanted = [c for c in a.claims.split(",") if c] or list(CLAIMS)
    unknown = [c for c in wanted if c not in CLAIMS]
    if unknown:
        sys.exit(f"unknown claim(s): {', '.join(unknown)}  (see --list)")

    events, unparsed = parse(a.log.read_text(errors="replace"))
    n_rx = sum(e.kind == "rx" for e in events)
    n_tx = sum(e.kind.startswith("tx") for e in events)
    print(f"{a.log}: {n_tx} writes, {n_rx} status frames, {events[-1].seg + 1 if events else 0} segment(s), "
          f"{len(unparsed)} unparsed line(s)")
    for u in unparsed[:5]:
        print(f"   unparsed: {u}")

    failed = 0
    for name in wanted:
        fn, _ = CLAIMS[name]
        ok, why = fn(events)
        failed += not ok
        print(f"{'PASS' if ok else 'FAIL'}  {name:<26} {why}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
