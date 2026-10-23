#!/usr/bin/env python3
"""End-to-end scenarios against the bridge running the simulated belt.

    tools/e2e_sim.py                                   # every scenario, live
    tools/e2e_sim.py --scenario boot,ui_start_stop
    tools/e2e_sim.py --replay docs/captures/e2e-sim-2026-10-23
    tools/e2e_sim.py --record-to docs/captures/e2e-sim-2026-10-23
    tools/e2e_sim.py --flash                           # build + upload cores3_sim first
    tools/e2e_sim.py --list

What it proves: with the `cores3_sim` firmware, the whole bridge (console,
policy, belt link, FTMS server, log sink) does what the scenarios say, end to
end, on the real ESP32-S3 and over real BLE, with no treadmill attached. It
does not prove anything about the belt itself; the simulated belt is the
bridge's model of it.

The `src=sim` gate: before any other command the runner asks `status` and
stops (exit 2) unless the answer says `src=sim`. A harness that sends `arm`
and `start` must never reach firmware wired to a real belt.

Three streams, one timeline:

  console  the serial port, opened once per run (opening it reboots the
           ESP32-S3). Control: commands and their reply lines.
  ftms     bleak on the host, standing in for Kinomap: 2ACD speeds, 2AD9
           writes and indications, each timestamped.
  udp      the logsink broadcast on port 9999: evidence, verbatim.

Every record carries host time `t`, so a check like "udp line within 2 s of
the last command" is a predicate over the recorded timeline. That is what
makes `--replay` possible: the same scenarios re-evaluated from
`timeline.jsonl` with no hardware, which is how CI keeps a recording honest.
A check whose stream recorded nothing at all reports `stream silent`, never
PASS: a dead stream proves nothing.

Exit status: 0 all checks pass, 1 a check failed, 2 a precondition failed
(no port, not `src=sim`, no recording).
"""
import argparse
import json
import pathlib
import re
import sys
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple

POLL_S = 0.1  # how often a windowed check re-reads the timeline while waiting
EPS = 1e-9


# --------------------------------------------------------------------------- parsing

def parse_status(line: str) -> Dict[str, str]:
    """`status armed=1 speed=20 ...` to {"armed": "1", "speed": "20", ...}; unknown keys kept."""
    if not line.startswith("status "):
        raise ValueError(f"not a status line: {line!r}")
    out = {}
    for tok in line.split()[1:]:
        k, _, v = tok.partition("=")
        out[k] = v
    return out


def ftms_speed_kmh(hexs: str) -> Optional[float]:
    """2ACD Treadmill Data (flags u16, speed u16 LE in 0.01 km/h) to km/h; None if too short."""
    b = bytes.fromhex(hexs)
    if len(b) < 4:
        return None
    return int.from_bytes(b[2:4], "little") / 100


def _norm_hex(hexs: str) -> str:
    return bytes.fromhex(hexs).hex(" ")


def _uuid_is(rec_uuid: str, short: str) -> bool:
    """Match a recorded UUID (short `2acd` or full 128-bit) against a 16-bit short form."""
    u = rec_uuid.lower()
    return u == short or u.startswith(f"0000{short}-")


def _secs(x: float) -> str:
    return f"{x:g}"


# --------------------------------------------------------------------------- steps

@dataclass
class Con:
    """Send a console command; `expect` is the exact terminating line, or `*` for ok / any err."""
    cmd: str
    expect: str = "ok"

    def __str__(self):
        return f"con {self.cmd}" + ("" if self.expect == "ok" else f" -> {self.expect}")


@dataclass
class Wait:
    """Host-side pause. Not an anchor for `within` windows."""
    seconds: float

    def __str__(self):
        return f"wait {_secs(self.seconds)} s"


@dataclass
class FtmsConnect:
    def __str__(self):
        return "ftms connect"


@dataclass
class FtmsDisconnect:
    def __str__(self):
        return "ftms disconnect"


@dataclass
class FtmsCp:
    """Write to the 2AD9 control point."""
    hex: str

    def __str__(self):
        return f"ftms cp {self.hex}"


class Status:
    """Check key=value pairs in the most recent `status` reply."""

    def __init__(self, **kv: str):
        self.kv = kv

    def __str__(self):
        return "status " + " ".join(f"{k}={v}" for k, v in self.kv.items())

    def __repr__(self):
        return f"Status({', '.join(f'{k}={v!r}' for k, v in self.kv.items())})"


@dataclass
class Udp:
    """A udp log line matching `regex` within `within` s of the last control step."""
    regex: str
    within: float

    def __str__(self):
        return f"udp /{self.regex}/ within {_secs(self.within)} s"


@dataclass
class UdpAbsent:
    """No udp log line matching `regex` since the last control step."""
    regex: str

    def __str__(self):
        return f"udp absent /{self.regex}/"


@dataclass
class SpeedAtLeast:
    """A 2ACD notification at or above `kmh` within `within` s of the last control step."""
    kmh: float
    within: float

    def __str__(self):
        return f"2acd >= {self.kmh:.2f} within {_secs(self.within)} s"


@dataclass
class SpeedZero:
    """A 2ACD notification reporting 0 km/h within `within` s of the last control step."""
    within: float

    def __str__(self):
        return f"2acd == 0 within {_secs(self.within)} s"


@dataclass
class Indication:
    """A 2AD9 indication with exactly these bytes within `within` s of the last control step."""
    hex: str
    within: float = 5.0

    def __str__(self):
        return f"indication {self.hex} within {_secs(self.within)} s"


@dataclass
class Scenario:
    name: str
    steps: list


@dataclass
class CheckResult:
    step: str
    ok: bool
    why: str


# --------------------------------------------------------------------------- sessions

class StepError(Exception):
    """A control step the session could not carry out; ends the scenario."""


class Session:
    """The three streams behind one interface; times are host seconds."""

    def begin_scenario(self, name: str) -> None:
        """Called before a scenario's first step; a live session records a `scenario` marker."""

    def console(self, cmd: str) -> Tuple[float, List[str]]:
        """Send `cmd`; return the send time and every reply line up to `ok` / `err ...`."""
        raise NotImplementedError

    def ftms_connect(self) -> float:
        """Connect to the bridge as the FTMS client; return the connect time."""
        raise NotImplementedError

    def ftms_disconnect(self) -> float:
        """Drop the FTMS link; return the disconnect time."""
        raise NotImplementedError

    def ftms_cp(self, hexs: str) -> float:
        """Write to 2AD9; return the write time."""
        raise NotImplementedError

    def wait(self, seconds: float) -> None:
        raise NotImplementedError

    def now(self) -> float:
        raise NotImplementedError

    def timeline(self) -> List[dict]:
        """Every record so far, ordered by `t`."""
        raise NotImplementedError


def _valid(rec) -> bool:
    """True if `rec` has the shape the checks rely on (so a bad record cannot crash a check)."""
    if not isinstance(rec, dict) or not isinstance(rec.get("t"), (int, float)):
        return False
    stream = rec.get("stream")
    if stream == "console":
        return rec.get("dir") in (">", "<") and isinstance(rec.get("line"), str)
    if stream == "udp":
        return isinstance(rec.get("line"), str)
    if stream == "scenario":
        return isinstance(rec.get("name"), str)
    if stream == "ftms":
        if rec.get("kind") not in ("write", "indication", "notify", "connect", "disconnect"):
            return False
        try:
            bytes.fromhex(rec.get("hex", ""))
        except (TypeError, ValueError):
            return False
        return isinstance(rec.get("uuid", ""), str)
    return False


def split_records(records) -> Tuple[List[dict], List[str]]:
    """(usable records, the rest as text) so a bad record is reported, not fatal."""
    good, bad = [], []
    for r in records:
        if _valid(r):
            good.append(r)
        else:
            bad.append(json.dumps(r) if isinstance(r, dict) else str(r))
    return good, bad


def load_timeline(path) -> Tuple[List[dict], List[str]]:
    """Read `timeline.jsonl`: (usable records, unparsed lines). A truncated last line is unparsed."""
    parsed, bad = [], []
    with open(path) as f:
        for line in f:
            if not line.strip():
                continue
            try:
                parsed.append(json.loads(line))
            except ValueError:
                bad.append(line.rstrip("\n"))
    good, invalid = split_records(parsed)
    return good, bad + invalid


INF = float("inf")


class ReplaySession(Session):
    """A recorded `timeline.jsonl` played back: control steps must match it in order.

    Cursors (console, ftms write, ftms link) move forward only. A popped control
    record earlier than the last control step is refused: it belongs to an
    earlier step the scenario did not ask for, so the replay is misaligned. If
    the recording carries `scenario` markers (live runs write one per
    scenario), `begin_scenario` moves every cursor to that scenario's marker
    and control steps cannot read past the next marker, so a scenario that
    aborted live (or mismatched in replay) does not shift the ones after it.
    Hand-built timelines without markers replay as one continuous run.
    `unparsed` holds the lines that could not be used.
    """

    def __init__(self, path):
        self._init(*load_timeline(path))

    @classmethod
    def from_records(cls, records: List[dict]) -> "ReplaySession":
        s = cls.__new__(cls)
        s._init(*split_records(records))
        return s

    def _init(self, records, unparsed):
        self.unparsed = unparsed
        self._records = sorted(records, key=lambda r: r["t"])
        self._console = [r for r in self._records if r["stream"] == "console"]
        self._writes = [r for r in self._records if r["stream"] == "ftms" and r["kind"] == "write"]
        self._links = [r for r in self._records
                       if r["stream"] == "ftms" and r["kind"] in ("connect", "disconnect")]
        self._marks = [r for r in self._records if r["stream"] == "scenario"]
        self._ci = self._wi = self._li = self._mi = 0
        self._cursor = 0.0
        self._floor = -INF   # t of the last popped control record
        self._end = INF      # t of the next scenario marker
        self._scenario = None

    def _advance(self, t):
        self._cursor = max(self._cursor, t)

    def begin_scenario(self, name):
        if not self._marks:
            return
        self._scenario = name
        i = next((i for i in range(self._mi, len(self._marks)) if self._marks[i]["name"] == name), None)
        if i is None:
            self._end = -INF  # every control step reports "recording ended"
            return
        start = self._marks[i]["t"]
        self._end = self._marks[i + 1]["t"] if i + 1 < len(self._marks) else INF
        self._mi = i + 1

        def first_at(recs):
            return next((k for k, r in enumerate(recs) if r["t"] >= start), len(recs))
        self._ci, self._wi, self._li = first_at(self._console), first_at(self._writes), first_at(self._links)
        self._floor = start
        self._advance(start)

    def _ended(self, what):
        where = f" in scenario '{self._scenario}'" if self._scenario else ""
        return StepError(f"recording ended{where} before {what}")

    def _take(self, rec, what):
        """Accept a popped control record, refusing one that goes back in time."""
        if rec["t"] < self._floor - EPS:
            raise StepError(f"recording has {what} at t={rec['t']:.3f}, before the last step "
                            f"at t={self._floor:.3f}: replay is misaligned")
        self._floor = rec["t"]
        self._advance(rec["t"])

    def console(self, cmd):
        while (self._ci < len(self._console) and self._console[self._ci]["dir"] != ">"
               and self._console[self._ci]["t"] < self._end):
            self._ci += 1  # reply lines with no command before them (boot noise)
        if self._ci >= len(self._console) or self._console[self._ci]["t"] >= self._end:
            raise self._ended(f"'{cmd}'")
        rec = self._console[self._ci]
        if rec["line"] != cmd:
            raise StepError(f"recording sent '{rec['line']}', scenario expects '{cmd}'")
        self._take(rec, f"'{cmd}'")
        self._ci += 1
        reply = []
        while (self._ci < len(self._console) and self._console[self._ci]["dir"] == "<"
               and self._console[self._ci]["t"] < self._end):
            reply.append(self._console[self._ci]["line"])
            self._advance(self._console[self._ci]["t"])
            self._ci += 1
        return rec["t"], reply

    def _link(self, kind):
        if self._li >= len(self._links) or self._links[self._li]["t"] >= self._end:
            raise self._ended(f"ftms {kind}")
        rec = self._links[self._li]
        if rec["kind"] != kind:
            raise StepError(f"recording sent 'ftms {rec['kind']}', scenario expects 'ftms {kind}'")
        self._take(rec, f"ftms {kind}")
        self._li += 1
        return rec["t"]

    def ftms_connect(self):
        return self._link("connect")

    def ftms_disconnect(self):
        return self._link("disconnect")

    def ftms_cp(self, hexs):
        if self._wi >= len(self._writes) or self._writes[self._wi]["t"] >= self._end:
            raise self._ended(f"cp {hexs}")
        rec = self._writes[self._wi]
        if _norm_hex(rec["hex"]) != _norm_hex(hexs):
            raise StepError(f"recording sent '{rec['hex']}', scenario expects '{hexs}'")
        self._take(rec, f"cp {hexs}")
        self._wi += 1
        return rec["t"]

    def wait(self, seconds):
        self._cursor += seconds

    def now(self):
        return self._cursor

    def timeline(self):
        return self._records


# --------------------------------------------------------------------------- live session

BRIDGE_NAME = "WalkingPad Bridge"
UDP_PORT = 9999
CONSOLE_TIMEOUT_S = 5.0
BLE_TIMEOUT_S = 20.0
# The UUIDs of tools/ftms_client.py, which cannot be imported (it runs at import).
FTMS_CP = "00002ad9-0000-1000-8000-00805f9b34fb"      # Control Point (indications)
FTMS_TD = "00002acd-0000-1000-8000-00805f9b34fb"      # Treadmill Data (notifications)
FTMS_STATUS = "00002ada-0000-1000-8000-00805f9b34fb"  # Machine Status (notifications)
_UDP_LINE = re.compile(r"(\d+) (.*)$")


class LiveSession(Session):
    """The bench: serial console, UDP log and bleak, recorded into one timeline.

    Three threads append to the timeline (serial reader, UDP listener, the
    asyncio loop that runs bleak). `t` is taken under the same lock as the
    append, so the list stays ordered by `t` without sorting. The port is
    opened by `preconditions()`, not here, because opening it reboots the board.
    pyserial and bleak are imported only on these paths, so `--replay` needs neither.
    """

    def __init__(self, port: str, out_dir: pathlib.Path):
        import queue
        import threading
        import time

        self.port = port
        self.out_dir = pathlib.Path(out_dir)
        self.notes: List[str] = []   # precondition lines and stream events, for the report
        self.udp_unparsed = 0
        self._time = time
        self._t0 = time.monotonic()
        self._lock = threading.Lock()
        self._records: List[dict] = []
        self._replies = queue.Queue()  # console lines, consumed by console()
        self._stop = threading.Event()
        self._threads = []
        self._ser = None
        self._sock = None
        self._loop = None
        self._client = None
        self._closing_ble = False

    # ---- recording

    def _add(self, **fields) -> dict:
        with self._lock:
            rec = {"t": round(self._time.monotonic() - self._t0, 6), **fields}
            self._records.append(rec)
        return rec

    def now(self):
        return self._time.monotonic() - self._t0

    def wait(self, seconds):
        if seconds > 0:
            self._time.sleep(seconds)

    def timeline(self):
        with self._lock:
            return list(self._records)

    def begin_scenario(self, name):
        self._add(stream="scenario", name=name)

    def _thread(self, target, name):
        import threading
        th = threading.Thread(target=target, name=name, daemon=True)
        th.start()
        self._threads.append(th)

    # ---- console

    def _open_console(self) -> None:
        from console import open_port
        self._ser = open_port(self.port)
        self._thread(self._read_serial, "serial")

    def _read_serial(self):
        from console import Reader, keep_line
        st = Reader()
        while not self._stop.is_set():
            try:
                st.buf += self._ser.read(4096)
            except Exception as e:  # port gone (USB reset) or closed by close()
                if not self._stop.is_set():
                    self.notes.append(f"serial reader stopped at t={self.now():.3f}: {e}")
                return
            while b"\n" in st.buf:
                raw, st.buf = st.buf.split(b"\n", 1)
                line = raw.decode("utf-8", "replace").strip()
                if not line or not keep_line(line, st):
                    continue
                self._add(stream="console", dir="<", line=line)
                self._replies.put(line)

    def console(self, cmd):
        import queue
        while True:  # unsolicited lines are already recorded; they are not this reply
            try:
                self._replies.get_nowait()
            except queue.Empty:
                break
        t = self._add(stream="console", dir=">", line=cmd)["t"]
        self._ser.write(cmd.encode() + b"\n")
        self._ser.flush()
        reply = []
        deadline = self._time.monotonic() + CONSOLE_TIMEOUT_S
        while True:
            left = deadline - self._time.monotonic()
            if left <= 0:
                # Made up by the host, so not recorded: the timeline holds what the device said.
                return t, reply + ["err timeout"]
            try:
                line = self._replies.get(timeout=left)
            except queue.Empty:
                continue
            reply.append(line)
            if _is_terminator(line):
                return t, reply

    # ---- udp

    def _open_udp(self) -> None:
        import socket
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        except (AttributeError, OSError):
            pass
        s.bind(("0.0.0.0", UDP_PORT))
        s.settimeout(0.5)
        self._sock = s
        self._thread(self._read_udp, "udp")

    def _read_udp(self):
        import socket
        while not self._stop.is_set():
            try:
                data, addr = self._sock.recvfrom(2048)
            except socket.timeout:
                continue
            except OSError:
                return  # closed by close()
            for ln in data.decode("utf-8", "replace").splitlines():
                if not ln:
                    continue
                m = _UDP_LINE.match(ln)
                if m:
                    self._add(stream="udp", ip=addr[0], millis=int(m.group(1)), line=m.group(2))
                else:  # kept verbatim, without millis, and counted
                    self.udp_unparsed += 1
                    self._add(stream="udp", ip=addr[0], line=ln)

    # ---- ftms

    def _ble(self, coro, timeout=BLE_TIMEOUT_S):
        """Run `coro` on the bleak loop thread and wait for its result."""
        import asyncio
        if self._loop is None:
            self._loop = asyncio.new_event_loop()
            self._thread(self._loop.run_forever, "bleak")
        return asyncio.run_coroutine_threadsafe(coro, self._loop).result(timeout=timeout)

    def _on_data(self, short, kind):
        def cb(_char, data):
            self._add(stream="ftms", kind=kind, uuid=short, hex=bytes(data).hex(" "))
        return cb

    def _on_drop(self, _client):
        if not self._closing_ble:
            self.notes.append(f"ftms link dropped by the bridge at t={self.now():.3f}")

    async def _find(self, scan_seconds):
        from bleak import BleakScanner
        return await BleakScanner.find_device_by_filter(
            lambda d, adv: BRIDGE_NAME in (adv.local_name or d.name or ""), timeout=scan_seconds)

    async def _connect(self, dev):
        from bleak import BleakClient
        c = BleakClient(dev, disconnected_callback=self._on_drop)
        await c.connect()
        self._client = c
        self._closing_ble = False
        t = self._add(stream="ftms", kind="connect", uuid="", hex="")["t"]
        await c.start_notify(FTMS_CP, self._on_data("2ad9", "indication"))
        await c.start_notify(FTMS_TD, self._on_data("2acd", "notify"))
        await c.start_notify(FTMS_STATUS, self._on_data("2ada", "notify"))
        return t

    async def _disconnect(self):
        c, self._client = self._client, None
        self._closing_ble = True
        if c is not None:
            await c.disconnect()
        return self._add(stream="ftms", kind="disconnect", uuid="", hex="")["t"]

    async def _write_cp(self, payload):
        # Recorded before the write: the indication can arrive before the write response.
        t = self._add(stream="ftms", kind="write", uuid="2ad9", hex=payload.hex(" "))["t"]
        await self._client.write_gatt_char(FTMS_CP, payload, response=True)
        return t

    def ftms_connect(self, scan_seconds: float = 15.0):
        try:
            dev = self._ble(self._find(scan_seconds), timeout=scan_seconds + 5)
            if dev is None:
                raise StepError(f"'{BRIDGE_NAME}' not found in a {_secs(scan_seconds)} s scan")
            return self._ble(self._connect(dev))
        except StepError:
            raise
        except Exception as e:
            raise StepError(f"ftms connect failed: {type(e).__name__}: {e}")

    def ftms_disconnect(self):
        try:
            return self._ble(self._disconnect())
        except Exception as e:
            raise StepError(f"ftms disconnect failed: {type(e).__name__}: {e}")

    def ftms_cp(self, hexs):
        if self._client is None:
            raise StepError("ftms cp without an ftms link")
        try:
            return self._ble(self._write_cp(bytes.fromhex(hexs)))
        except Exception as e:
            raise StepError(f"ftms cp {hexs} failed: {type(e).__name__}: {e}")

    # ---- preconditions

    def _pre(self, ok: bool, what: str) -> None:
        line = f"{'PASS' if ok else 'FAIL'}  {'precondition':<14} {what}"
        self.notes.append(line)
        print(line, flush=True)

    def _status(self) -> Optional[str]:
        _, reply = self.console("status")
        return next((ln for ln in reply if ln.startswith("status ")), None)

    def preconditions(self, scan_seconds: float = 15) -> Optional[str]:
        """Bring the bench to a known state: the failure text, or None when every step held."""
        import os

        def fail(text, detail=""):
            self._pre(False, text + (f" ({detail})" if detail else ""))
            return text

        # (1) the port
        if not self.port or not os.path.exists(self.port):
            return fail(f"no serial port: {self.port or 'none found; pass --port'}")
        self._pre(True, f"port {self.port} exists")
        try:
            self._open_udp()  # first, so nothing broadcast after the reset is missed
        except OSError as e:
            return fail(f"cannot listen on udp/{UDP_PORT}: {e}")
        try:
            self._open_console()
        except Exception as e:  # pyserial's SerialException, or OSError
            return fail(f"cannot open {self.port}: {e}")

        # (2) status answers and says src=sim; otherwise nothing else is sent
        status = self._status()
        if status is None:
            return fail("no reply from console (port reset but silent?)")
        got = parse_status(status)
        if got.get("src") != "sim":
            return fail(f"refusing: src={got.get('src', '')} is not the simulated belt")
        self._pre(True, f"src=sim: {status}")

        # (3) a fresh device starts in the wizard: setup, not a check
        wizard, presses = got.get("wizard", ""), 0
        while wizard != "off" and presses < 6:
            self.console("wizard next")
            presses += 1
            status = self._status()
            wizard = parse_status(status).get("wizard", "") if status else ""
        if wizard != "off":
            return fail(f"wizard still '{wizard}' after {presses} presses")
        if presses:
            self._pre(True, f"clicked through wizard ({presses} presses)")

        # (4) the udp log is alive
        t, reply = self.console("log udp on")
        hit = None
        while hit is None and self.now() < t + 5.0:
            self.wait(POLL_S)
            hit = next((r for r in self.timeline() if r["stream"] == "udp" and r["t"] >= t - EPS
                        and "udp log ON" in r["line"]), None)
        if hit is None:
            _, log = self.console("log")
            return fail("no udp line 'udp log ON' within 5 s",
                        f"'log udp on' replied: {' / '.join(reply)}; 'log' replied: {' / '.join(log)}")
        self._pre(True, f"udp alive: {hit.get('ip', '?')} t={hit['t']:.3f} {hit['line']}")

        # (5) the bridge advertises; scenarios start unconnected
        try:
            self.ftms_connect(scan_seconds)
            self.ftms_disconnect()
        except StepError as e:
            return fail("bridge not advertising; is a phone holding the link?", str(e))
        self._pre(True, f"'{BRIDGE_NAME}' advertises: ftms connect + disconnect")
        return None

    # ---- shutdown and files

    def close(self) -> None:
        """Stop every stream, then write console.txt, udp.log, ftms.jsonl, timeline.jsonl."""
        if self._loop is not None:
            if self._client is not None:
                try:
                    self.ftms_disconnect()
                except StepError as e:
                    self.notes.append(str(e))
            self._loop.call_soon_threadsafe(self._loop.stop)
        self._stop.set()
        if self._sock is not None:
            self._sock.close()
        for th in self._threads:
            th.join(timeout=2)
        if self._ser is not None:
            self._ser.close()

        recs = self.timeline()
        self.out_dir.mkdir(parents=True, exist_ok=True)
        with open(self.out_dir / "timeline.jsonl", "w") as f:
            f.writelines(json.dumps(r) + "\n" for r in recs)
        with open(self.out_dir / "ftms.jsonl", "w") as f:
            f.writelines(json.dumps(r) + "\n" for r in recs if r["stream"] == "ftms")
        with open(self.out_dir / "console.txt", "w") as f:
            f.writelines(f"{r['t']:.3f} {r['dir']} {r['line']}\n" for r in recs if r["stream"] == "console")
        with open(self.out_dir / "udp.log", "w") as f:  # logcat.py's format: "<ip>  <millis> <rest>"
            for r in recs:
                if r["stream"] == "udp":
                    rest = f"{r['millis']} {r['line']}" if "millis" in r else r["line"]
                    f.write(f"{r.get('ip', '?')}  {rest}\n")


# --------------------------------------------------------------------------- checks

def _await(s: Session, anchor: float, within: float, find):
    """Re-run `find(records in [anchor, upper])` until it returns a hit or the window closes.

    Returns (hit, records in the full window). Live sessions grow the timeline
    while we wait; a replay session only moves its cursor.
    """
    deadline = anchor + within
    while True:
        closed = s.now() >= deadline - EPS
        upper = deadline if closed else s.now()
        window = [r for r in s.timeline() if anchor - EPS <= r["t"] <= upper + EPS]
        hit = find(window)
        if hit is not None or closed:
            return hit, window
        s.wait(min(POLL_S, deadline - s.now()))


def _udp(records):
    return [r for r in records if r["stream"] == "udp"]


def _speeds(records):
    out = []
    for r in records:
        if r["stream"] == "ftms" and r.get("kind") == "notify" and _uuid_is(r.get("uuid", ""), "2acd"):
            v = ftms_speed_kmh(r["hex"])
            if v is not None:
                out.append((r, v))
    return out


def _indications(records):
    return [r for r in records
            if r["stream"] == "ftms" and r.get("kind") == "indication" and _uuid_is(r.get("uuid", ""), "2ad9")]


def _ev(r):
    return f"t={r['t']:.3f} {r.get('line', r.get('hex', ''))}"


def _check_udp(step, s, anchor):
    rx = re.compile(step.regex)
    hit, _ = _await(s, anchor, step.within,
                    lambda w: next((r for r in _udp(w) if rx.search(r["line"])), None))
    if hit is not None:
        return True, _ev(hit)
    if not _udp(s.timeline()):
        return False, "stream silent: no udp line recorded"
    return False, f"no match in {_secs(step.within)} s"


def _check_udp_absent(step, s, anchor):
    if not _udp(s.timeline()):
        return False, "stream silent: no udp line recorded"
    rx = re.compile(step.regex)
    window = [r for r in _udp(s.timeline()) if anchor - EPS <= r["t"] <= s.now() + EPS]
    hit = next((r for r in window if rx.search(r["line"])), None)
    if hit is not None:
        return False, f"matched {_ev(hit)}"
    return True, f"no match in {s.now() - anchor:.2f} s ({len(window)} udp lines)"


def _check_speed(s, anchor, within, ok, describe):
    hit, window = _await(s, anchor, within,
                         lambda w: next(((r, v) for r, v in _speeds(w) if ok(v)), None))
    if hit is not None:
        r, v = hit
        return True, f"t={r['t']:.3f} {v:.2f} km/h ({r['hex']})"
    if not _speeds(s.timeline()):
        return False, "stream silent: no 2acd notification recorded"
    seen = [v for _, v in _speeds(window)]
    if not seen:
        return False, f"no match in {_secs(within)} s: no 2acd notification in the window"
    return False, f"no match in {_secs(within)} s: {describe(seen)}"


def _check_indication(step, s, anchor):
    want = _norm_hex(step.hex)
    hit, window = _await(s, anchor, step.within,
                         lambda w: next((r for r in _indications(w) if _norm_hex(r["hex"]) == want), None))
    if hit is not None:
        return True, _ev(hit)
    if not _indications(s.timeline()):
        return False, "stream silent: no 2ad9 indication recorded"
    seen = ", ".join(_norm_hex(r["hex"]) for r in _indications(window)) or "none"
    return False, f"no match in {_secs(step.within)} s (saw: {seen})"


def _check_status(step, last_status):
    if last_status is None:
        return False, "no status reply before this check"
    got = parse_status(last_status)
    bad = []
    for k, v in step.kv.items():
        if k not in got:
            bad.append(f"key absent: {k}")
        elif got[k] != v:
            bad.append(f"{k}={got[k]}, expected {v}")
    return (not bad), ("; ".join(bad) if bad else last_status)


def _is_terminator(line):
    return line == "ok" or line.startswith("err ")


def _con_ok(expect, reply):
    """Compare the first `ok` / `err ...` line with `expect`; later lines are log noise."""
    end = next((ln for ln in reply if _is_terminator(ln)), None)
    if end is None:
        return False, f"no terminating line (got {len(reply)} line(s): {' / '.join(reply) or 'none'})"
    ok = expect == "*" or end == expect
    return ok, end if ok else f"got '{end}', expected '{expect}'"


def run_scenario(sc: Scenario, s: Session, on_result=None) -> List[CheckResult]:
    """Run every step in order. A control step that cannot be carried out ends the scenario.

    `on_result(result)` is called as each step is judged, so a live run reports as it goes.
    """
    results = []

    def judged(r):
        results.append(r)
        if on_result is not None:
            on_result(r)
    s.begin_scenario(sc.name)
    anchor = s.now()
    last_status = None  # the status line of the most recent control step only
    for step in sc.steps:
        label = str(step)
        try:
            if isinstance(step, Con):
                last_status = None
                anchor, reply = s.console(step.cmd)
                body = []
                for ln in reply:
                    if _is_terminator(ln):
                        break
                    body.append(ln)
                status = [ln for ln in body if ln.startswith("status ")]
                if status:
                    last_status = status[-1]
                ok, why = _con_ok(step.expect, reply)
            elif isinstance(step, Wait):
                s.wait(step.seconds)
                continue
            elif isinstance(step, FtmsConnect):
                last_status = None
                anchor = s.ftms_connect()
                ok, why = True, f"connected at t={anchor:.3f}"
            elif isinstance(step, FtmsDisconnect):
                last_status = None
                anchor = s.ftms_disconnect()
                ok, why = True, f"disconnected at t={anchor:.3f}"
            elif isinstance(step, FtmsCp):
                last_status = None
                anchor = s.ftms_cp(step.hex)
                ok, why = True, f"written at t={anchor:.3f}"
            elif isinstance(step, Status):
                ok, why = _check_status(step, last_status)
            elif isinstance(step, Udp):
                ok, why = _check_udp(step, s, anchor)
            elif isinstance(step, UdpAbsent):
                ok, why = _check_udp_absent(step, s, anchor)
            elif isinstance(step, SpeedAtLeast):
                ok, why = _check_speed(s, anchor, step.within, lambda v: v >= step.kmh - EPS,
                                       lambda seen: f"max {max(seen):.2f}")
            elif isinstance(step, SpeedZero):
                ok, why = _check_speed(s, anchor, step.within, lambda v: v == 0,
                                       lambda seen: f"min {min(seen):.2f}")
            elif isinstance(step, Indication):
                ok, why = _check_indication(step, s, anchor)
            else:
                raise TypeError(f"unknown step {step!r}")
        except StepError as e:
            judged(CheckResult(label, False, str(e)))
            break
        judged(CheckResult(label, ok, why))
    return results


# --------------------------------------------------------------------------- report

def _cell(text):
    return str(text).replace("|", "\\|")


def write_report(results_by_scenario: Dict[str, List[CheckResult]], path) -> bool:
    """Markdown PASS/FAIL table per scenario, with the evidence for each step. Returns all-ok."""
    all_ok = True
    lines = ["# e2e_sim report", ""]
    for name, results in results_by_scenario.items():
        ok = bool(results) and all(r.ok for r in results)
        all_ok = all_ok and ok
        lines += [f"## {name}: {'PASS' if ok else 'FAIL'}", "",
                  "| step | result | evidence |", "|---|---|---|"]
        lines += [f"| `{_cell(r.step)}` | {'PASS' if r.ok else 'FAIL'} | {_cell(r.why)} |" for r in results]
        lines.append("")
    pathlib.Path(path).write_text("\n".join(lines))
    return all_ok


def exit_code(all_ok: bool, precondition_failed: bool) -> int:
    if precondition_failed:
        return 2
    return 0 if all_ok else 1


def print_result(name: str, r: CheckResult) -> None:
    print(f"{'PASS' if r.ok else 'FAIL'}  {name:<14} {r.step:<40} {r.why}", flush=True)


def print_results(name: str, results: List[CheckResult]) -> None:
    for r in results:
        print_result(name, r)


# --------------------------------------------------------------------------- scenarios

def _scenarios(*scs: Scenario) -> Dict[str, Scenario]:
    return {sc.name: sc for sc in scs}


# Provisional: the full set arrives with the next task. A Status check reads the
# reply of the step before it, so `status` is sent first.
SCENARIOS: Dict[str, Scenario] = _scenarios(
    Scenario("boot", [Con("status"),
                      Status(src="sim", belt_link="1", phone_link="0", armed="0", running="0")]),
)


def from_known_state(sc: Scenario) -> Scenario:
    """The scenario as the runner sends it: `stop` first (ok or any err), so it starts from rest."""
    return Scenario(sc.name, [Con("stop", expect="*")] + list(sc.steps))


def run_all(names: List[str], s: Session) -> Dict[str, List[CheckResult]]:
    out = {}
    for n in names:
        out[n] = run_scenario(from_known_state(SCENARIOS[n]), s, lambda r, n=n: print_result(n, r))
    return out


def _all_ok(results: Dict[str, List[CheckResult]]) -> bool:
    return all(bool(rs) and all(r.ok for r in rs) for rs in results.values())


def _flash(port: str) -> bool:
    """Build and upload cores3_sim to `port`; True when pio succeeded and the port is back."""
    import shutil
    import subprocess
    import time

    venv_pio = pathlib.Path(sys.executable).with_name("pio")
    pio = str(venv_pio) if venv_pio.exists() else (shutil.which("pio") or "pio")
    cmd = [pio, "run", "-e", "cores3_sim", "-t", "upload", "--upload-port", port]
    print("flash: " + " ".join(cmd), flush=True)
    if subprocess.run(cmd, cwd=pathlib.Path(__file__).resolve().parent.parent).returncode != 0:
        return False
    deadline = time.monotonic() + 15  # the USB port re-enumerates after the reset
    while not pathlib.Path(port).exists() and time.monotonic() < deadline:
        time.sleep(0.5)
    return pathlib.Path(port).exists()


def _default_port() -> Optional[str]:
    from console import default_port
    try:
        return default_port()
    except SystemExit:  # no port found; the precondition says so
        return None


def run_live(names: List[str], port: Optional[str], out_dir: pathlib.Path,
             record_to: Optional[str], flash: bool) -> int:
    if flash:
        if not port:
            print("FAIL  precondition   --flash needs a serial port", flush=True)
            return 2
        if not _flash(port):
            print(f"FAIL  precondition   flashing cores3_sim to {port} failed", flush=True)
            return 2
    s = LiveSession(port, out_dir)
    results: Dict[str, List[CheckResult]] = {}
    failed = None
    try:
        failed = s.preconditions()
        if failed is None:
            results = run_all(names, s)
    finally:
        s.close()
    all_ok = write_report(results, out_dir / "report.md")
    with open(out_dir / "report.md", "a") as f:
        f.write("\n## preconditions\n\n")
        f.writelines(f"- {n}\n" for n in s.notes)
        f.write(f"\n{s.udp_unparsed} unparsed udp line(s)\n")
    if s.udp_unparsed:
        print(f"{s.udp_unparsed} udp line(s) without millis, kept verbatim")
    print(f"recording: {out_dir}")
    if record_to:
        import shutil
        shutil.copytree(out_dir, record_to, dirs_exist_ok=True)
        print(f"copied to: {record_to}")
    return exit_code(all_ok, failed is not None)


def run_replay(names: List[str], replay_dir: str) -> int:
    path = pathlib.Path(replay_dir) / "timeline.jsonl"
    if not path.is_file():
        print(f"no recording: {path}", file=sys.stderr)
        return 2
    s = ReplaySession(path)
    print(f"{path}: {len(s.timeline())} records, {len(s.unparsed)} unparsed")
    for u in s.unparsed:
        print(f"   unparsed: {u}")
    return exit_code(_all_ok(run_all(names, s)), False)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port (default: the first /dev/cu.usbmodem* or /dev/ttyACM*)")
    ap.add_argument("--scenario", help="comma-separated scenario names (default: all, in order)")
    ap.add_argument("--out", metavar="DIR", help="where the run is recorded (default: build/e2e/<UTC stamp>)")
    ap.add_argument("--replay", metavar="DIR", help="re-evaluate DIR/timeline.jsonl, no hardware")
    ap.add_argument("--record-to", metavar="DIR", help="copy the run's recording to DIR")
    ap.add_argument("--flash", action="store_true", help="build and upload cores3_sim first")
    ap.add_argument("--list", action="store_true", help="print the scenarios and exit")
    a = ap.parse_args()

    if a.list:
        for sc in SCENARIOS.values():
            print(f"{sc.name:<14} {len(sc.steps)} steps")
        return 0
    names = a.scenario.split(",") if a.scenario else list(SCENARIOS)
    unknown = [n for n in names if n not in SCENARIOS]
    if unknown:
        print(f"unknown scenario(s): {', '.join(unknown)}  (see --list)", file=sys.stderr)
        return 2
    if not names:
        print("no scenarios to run", file=sys.stderr)
        return 2
    if a.replay:
        return run_replay(names, a.replay)

    import time
    out_dir = pathlib.Path(a.out or f"build/e2e/{time.strftime('%Y%m%dT%H%M%SZ', time.gmtime())}")
    return run_live(names, a.port or _default_port(), out_dir, a.record_to, a.flash)


if __name__ == "__main__":
    sys.exit(main())
