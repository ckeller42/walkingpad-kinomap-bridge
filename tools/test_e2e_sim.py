"""Host tests for tools/e2e_sim.py: parsing, checks and replay, no hardware.

    ~/tools/venv-ble/bin/python -m unittest tools/test_e2e_sim.py
"""
import json
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import e2e_sim  # noqa: E402
from e2e_sim import (  # noqa: E402
    Con, FtmsConnect, LiveSession, ReplaySession, Scenario, SpeedAtLeast, Status, Udp,
    UdpAbsent, Wait, exit_code, from_known_state, ftms_speed_kmh, parse_status, run_scenario, write_report,
)


def sent(t, line):
    return {"t": t, "stream": "console", "dir": ">", "line": line}


def got(t, line):
    return {"t": t, "stream": "console", "dir": "<", "line": line}


def udp(t, line, millis=1000):
    return {"t": t, "stream": "udp", "ip": "192.168.178.50", "millis": millis, "line": line}


def notify(t, uuid, hexs):
    return {"t": t, "stream": "ftms", "kind": "notify", "uuid": uuid, "hex": hexs}


def run(records, steps):
    return run_scenario(Scenario("t", steps), ReplaySession.from_records(records))


class ParseTest(unittest.TestCase):
    def test_parse_status_keeps_unknown_keys(self):
        self.assertEqual(parse_status("status armed=1 speed=20 zz=7")["zz"], "7")
        with self.assertRaises(ValueError):
            parse_status("ok")

    def test_ftms_speed(self):
        self.assertEqual(ftms_speed_kmh("00 00 fa 00"), 2.5)
        self.assertIsNone(ftms_speed_kmh("00"))


class CheckTest(unittest.TestCase):
    ARMED = [sent(4.0, "arm"), got(4.0, "ok"), udp(5.0, "emit StartSequence tenths=20")]

    def test_udp_window_includes_only_window(self):
        r = run(self.ARMED, [Con("arm"), Udp("emit StartSequence", within=2)])
        self.assertTrue(r[1].ok, r[1].why)
        r = run(self.ARMED, [Con("arm"), Udp("emit StartSequence", within=0.5)])
        self.assertFalse(r[1].ok)
        self.assertIn("no match in 0.5 s", r[1].why)

    def test_silent_stream_is_fail(self):
        r = run([sent(1.0, "arm"), got(1.0, "ok")], [Con("arm"), Udp(".", within=1)])
        self.assertTrue(r[0].ok, r[0].why)
        self.assertFalse(r[1].ok)
        self.assertIn("stream silent", r[1].why)

    def test_speed_at_least_reads_2acd(self):
        recs = [sent(4.0, "arm"), got(4.0, "ok"), notify(6.0, "2acd", "00 00 c8 00")]
        r = run(recs, [Con("arm"), SpeedAtLeast(1.9, within=12)])
        self.assertTrue(r[1].ok, r[1].why)
        r = run(recs, [Con("arm"), SpeedAtLeast(2.1, 12)])
        self.assertFalse(r[1].ok)
        self.assertIn("max 2.00", r[1].why)

    def test_status_missing_key(self):
        recs = [sent(1.0, "status"), got(1.0, "status armed=1"), got(1.0, "ok")]
        r = run(recs, [Con("status"), Status(running="0")])
        self.assertTrue(r[0].ok, r[0].why)
        self.assertFalse(r[1].ok)
        self.assertIn("key absent: running", r[1].why)

    def test_con_wrong_reply_is_fail(self):
        recs = [sent(1.0, "start"), got(1.0, "err not-armed")]
        self.assertFalse(run(recs, [Con("start")])[0].ok)
        r = run(recs, [Con("start", expect="*")])
        self.assertTrue(r[0].ok, r[0].why)


def mark(t, name):
    return {"t": t, "stream": "scenario", "name": name}


def link(t, kind):
    return {"t": t, "stream": "ftms", "kind": kind, "uuid": "", "hex": ""}


class FixRound1Test(unittest.TestCase):
    def test_con_ignores_lines_after_terminator(self):
        recs = [sent(1.0, "arm"), got(1.0, "ok"), got(1.2, "belt mode 2 -> 1 raw=0")]
        r = run(recs, [Con("arm")])
        self.assertTrue(r[0].ok, r[0].why)

    def test_con_without_terminator_fails(self):
        r = run([sent(1.0, "arm"), got(1.0, "armed")], [Con("arm")])
        self.assertFalse(r[0].ok)
        self.assertIn("no terminating line", r[0].why)

    def test_unparsable_lines_are_counted(self):
        good = [sent(1.0, "arm"), got(1.1, "ok")]
        with tempfile.TemporaryDirectory() as d:
            path = pathlib.Path(d) / "timeline.jsonl"
            path.write_text("".join(json.dumps(r) + "\n" for r in good)
                            + '{"t": 2.0, "stream": "udp", "li\n'          # truncated
                            + '{"stream": "udp", "line": "no t"}\n'         # missing t
                            + '{"t": 3.0, "line": "no stream"}\n'           # missing stream
                            + json.dumps(notify(4.0, "2acd", "zz")) + "\n")  # bad hex
            s = ReplaySession(path)
        self.assertEqual(len(s.unparsed), 4)
        r = run_scenario(Scenario("x", [Con("arm")]), s)
        self.assertTrue(r[0].ok, r[0].why)

    def test_control_record_before_last_anchor_is_refused(self):
        # Scenario "a" recorded arm, then its connect failed live (nothing recorded);
        # "b" recorded stop at t=5 and a connect at t=6. Without markers, replaying
        # "a" takes b's connect, and the console pop that follows goes back in time.
        recs = [sent(1.0, "arm"), got(1.0, "ok"), sent(5.0, "stop"), got(5.0, "ok"), link(6.0, "connect")]
        r = run(recs, [Con("arm"), FtmsConnect(), Con("stop")])
        self.assertFalse(r[2].ok)
        self.assertIn("before the last step", r[2].why)

    def test_scenario_markers_keep_scenarios_apart(self):
        recs = [mark(0.5, "a"), sent(1.0, "arm"), got(1.0, "ok"),
                mark(4.5, "b"), sent(5.0, "stop"), got(5.0, "ok"), link(6.0, "connect")]
        s = ReplaySession.from_records(recs)
        a = run_scenario(Scenario("a", [Con("arm"), FtmsConnect(), Con("status")]), s)
        self.assertEqual([x.ok for x in a], [True, False])
        self.assertIn("recording ended", a[1].why)
        b = run_scenario(Scenario("b", [Con("stop"), FtmsConnect()]), s)
        self.assertEqual([x.ok for x in b], [True, True], [x.why for x in b])

    def test_marker_resyncs_after_mismatch(self):
        recs = [mark(0.5, "a"), sent(1.0, "arm"), got(1.0, "ok"),
                mark(4.5, "b"), sent(5.0, "stop"), got(5.0, "ok")]
        s = ReplaySession.from_records(recs)
        a = run_scenario(Scenario("a", [Con("status")]), s)
        self.assertIn("recording sent 'arm'", a[0].why)
        b = run_scenario(Scenario("b", [Con("stop")]), s)
        self.assertTrue(b[0].ok, b[0].why)

    def test_ftms_connect_anchor_is_record_time(self):
        recs = [link(2.0, "connect"), udp(2.5, "ftms connected")]
        s = ReplaySession.from_records(recs)
        s.wait(10.0)  # host time well past the record; the anchor must still be t=2.0
        r = run_scenario(Scenario("x", [FtmsConnect(), Udp("ftms connected", 1)]), s)
        self.assertEqual([x.ok for x in r], [True, True], [x.why for x in r])

    def test_status_ignores_stale_reply(self):
        recs = [sent(1.0, "status"), got(1.0, "status running=0"), got(1.0, "ok"),
                sent(2.0, "start"), got(2.0, "ok")]
        r = run(recs, [Con("status"), Con("start"), Status(running="0")])
        self.assertFalse(r[2].ok)
        self.assertIn("no status reply", r[2].why)


class ReplayTest(unittest.TestCase):
    def test_replay_mismatch(self):
        r = run([sent(1.0, "arm"), got(1.0, "ok")], [Con("status")])
        self.assertFalse(r[0].ok)
        self.assertIn("recording sent 'arm'", r[0].why)

    def test_replay_empty_recording(self):
        r = run([], [Con("status")])
        self.assertFalse(r[0].ok)
        self.assertIn("recording ended", r[0].why)

    def test_replay_round_trip(self):
        recs = [sent(1.0, "arm"), got(1.1, "ok"), udp(1.5, "emit SyncPolicy armed=1")]
        with tempfile.TemporaryDirectory() as d:
            path = pathlib.Path(d) / "timeline.jsonl"
            path.write_text("".join(json.dumps(r) + "\n" for r in recs))
            s = ReplaySession(path)
            r = run_scenario(Scenario("x", [Con("arm"), Udp("emit SyncPolicy|emit", 2)]), s)
        self.assertEqual([x.ok for x in r], [True, True], [x.why for x in r])


class ReportTest(unittest.TestCase):
    def test_exit_codes(self):
        self.assertEqual(exit_code(True, False), 0)
        self.assertEqual(exit_code(False, False), 1)
        self.assertEqual(exit_code(True, True), 2)
        self.assertEqual(exit_code(False, True), 2)

    def test_write_report_returns_all_ok(self):
        good = run([sent(1.0, "arm"), got(1.0, "ok")], [Con("arm")])
        bad = run([], [Con("arm")])
        with tempfile.TemporaryDirectory() as d:
            path = pathlib.Path(d) / "report.md"
            self.assertTrue(write_report({"a": good}, path))
            self.assertFalse(write_report({"a": good, "b": bad}, path))
            text = path.read_text()
        self.assertIn("| FAIL |", text)
        self.assertIn("recording ended", text)


class FakeSerial:
    """Stands in for the port: records writes, optionally fails them."""

    def __init__(self, fail=False):
        self.fail, self.written = fail, []

    def write(self, b):
        if self.fail:
            raise OSError(6, "Device not configured")
        self.written.append(b)

    def flush(self):
        pass

    def close(self):
        pass


class LiveConsoleTest(unittest.TestCase):
    """LiveSession's console path with a fake port and no reader thread (nothing answers)."""

    def setUp(self):
        self._timeouts = e2e_sim.CONSOLE_TIMEOUT_S, e2e_sim.UDP_ALIVE_S
        e2e_sim.CONSOLE_TIMEOUT_S = e2e_sim.UDP_ALIVE_S = 0.2
        self.dir = tempfile.TemporaryDirectory()

    def tearDown(self):
        e2e_sim.CONSOLE_TIMEOUT_S, e2e_sim.UDP_ALIVE_S = self._timeouts
        self.dir.cleanup()

    def live(self, ser):
        s = LiveSession("/dev/fake", pathlib.Path(self.dir.name))
        s._ser = ser
        return s

    def test_timeout_fails_alike_live_and_replay(self):
        sc = from_known_state(Scenario("t", [Con("status")]))
        s = self.live(FakeSerial())
        live = run_scenario(sc, s)
        s._add(stream="console", dir="<", line="ok")  # a late reply must not rescue the replay
        s.close()
        replay = run_scenario(sc, ReplaySession(pathlib.Path(self.dir.name) / "timeline.jsonl"))
        self.assertEqual([(r.step, r.ok, r.why) for r in live], [(r.step, r.ok, r.why) for r in replay])
        # The gate's `status` is the first thing sent; a silent console fails it as a step.
        self.assertEqual([(r.ok, r.why) for r in live], [(False, "no ok/err to 'status' within 0.2 s")])
        self.assertIsNone(s.broken)  # a silent console is a failed check, not a broken bench
        self.assertIn("! no ok/err to 'status'", (pathlib.Path(self.dir.name) / "console.txt").read_text())

    def test_serial_write_failure_is_a_step_error_and_marks_the_bench_broken(self):
        s = self.live(FakeSerial(fail=True))
        r = run_scenario(Scenario("t", [Con("status")]), s)
        self.assertEqual([x.ok for x in r], [False])
        self.assertIn("bench broken: serial write failed", r[0].why)
        self.assertIn("Device not configured", s.broken)
        self.assertEqual(exit_code(False, bool(s.broken)), 2)

    def test_udp_precondition_failure_text_carries_the_log_reply(self):
        s = self.live(FakeSerial())
        replies = {"status": ["status armed=0 wizard=off src=sim", "ok"], "log udp on": ["ok"],
                   "log": ["log dev=1 udp=1 wifi=0 ip=0.0.0.0", "ok"]}
        s._open_udp = lambda: None
        s._open_console = lambda: None
        s.console = lambda cmd: (s.now(), replies[cmd])
        s.port = __file__  # exists
        e2e_sim.print = lambda *a, **k: None
        try:
            failed = s.preconditions()
        finally:
            del e2e_sim.print
        self.assertIn("no udp line 'udp log ON' within 0.2 s", failed)
        self.assertIn("'log' replied: log dev=1 udp=1 wifi=0 ip=0.0.0.0 / ok", failed)


def seq(t, n, millis=1000):
    return udp(t, f"udp-seq {n}", millis)


def idle(t, n, millis):
    """The bridge's heartbeat: nothing to send; datagrams up to n were sent."""
    return udp(t, f"udp-seq {n} idle", millis)


REPO = pathlib.Path(__file__).resolve().parent.parent
RECORDING = REPO / "docs/captures/e2e-sim-2026-10-23"


class SeqGapTest(unittest.TestCase):
    """udp-seq headers make a lost datagram visible; a failed check inside the gap says so."""

    ARMED = [sent(1.0, "arm"), got(1.0, "ok"), seq(1.5, 3, 1000), udp(1.5, "emit SyncPolicy", 1000),
             seq(3.5, 5, 3000), udp(3.5, "emit SyncPolicy", 3000)]

    def test_seq_gap_annotates_failed_udp_check(self):
        r = run(self.ARMED, [Con("arm"), Udp("emit StartSequence", within=3)])
        self.assertFalse(r[1].ok)
        self.assertTrue(r[1].why.endswith("; udp lost (seq 3->5)"), r[1].why)
        self.assertTrue(r[1].why.startswith("no match in 3 s"), r[1].why)

    def test_absent_inside_gap_is_inconclusive(self):
        r = run(self.ARMED, [Con("arm"), Wait(3), UdpAbsent("emit StartSequence")])
        self.assertEqual(len(r), 2)  # a wait is not judged
        self.assertFalse(r[1].ok)
        self.assertIn("inconclusive: udp lost (seq 3->5)", r[1].why)

    def test_recording_without_seq_is_unchanged(self):
        # The committed recording predates udp-seq and the per-scenario gate:
        # its replay renders the tables the live run wrote, except the rows
        # this fix wave changed on purpose: `udp absent` now holds 2.5 s
        # (finding 1), and auto_mode's closing stop expects `ok` (finding 7).
        s = ReplaySession(RECORDING / "timeline.jsonl")
        self.assertFalse([r for r in s.timeline() if r["stream"] == "udp" and r["line"].startswith("udp-seq")])
        e2e_sim.print = lambda *a, **k: None
        try:
            results = e2e_sim.run_all(list(e2e_sim.SCENARIOS), s)
        finally:
            del e2e_sim.print
        with tempfile.TemporaryDirectory() as d:
            path = pathlib.Path(d) / "report.md"
            self.assertTrue(write_report(results, path))
            replayed = path.read_text().rstrip("\n").split("\n")
        live = (RECORDING / "report.md").read_text().split("\n## preconditions and bench")[0].rstrip("\n").split("\n")
        self.assertEqual(len(replayed), len(live))
        for new, old in zip(replayed, live):
            if new.startswith("| `udp absent"):
                self.assertEqual(new.split("|")[1], old.split("|")[1])
                self.assertIn("| PASS | no match in 2.50 s (", new)
            elif new.startswith("| `con stop` |") and old.startswith("| `con stop -> *` |"):
                self.assertEqual(new.split("|")[2:], old.split("|")[2:])
            else:
                self.assertEqual(new, old)

    def test_recording_with_seq_replays_green(self):
        # The 2026-10-23-seq recording: HEAD firmware, udp-seq headers, the gate.
        s = ReplaySession(RECORDING.with_name(RECORDING.name + "-seq") / "timeline.jsonl")
        heads = e2e_sim._seq_headers(s.timeline())
        self.assertTrue([h for h in heads if not h[3]])  # data-bearing headers
        self.assertTrue([h for h in heads if h[3]])      # and `idle` heartbeats
        e2e_sim.print = lambda *a, **k: None
        try:
            results = e2e_sim.run_all(list(e2e_sim.SCENARIOS), s)
        finally:
            del e2e_sim.print
        self.assertEqual(list(results), list(e2e_sim.SCENARIOS))
        self.assertTrue(all(e2e_sim.scenario_ok(r) for r in results.values()))
        self.assertTrue(all(r[0].step == "gate src=sim" for r in results.values()))
        self.assertIsNone(s.broken)

    def test_passed_check_in_gap_is_not_annotated(self):
        recs = self.ARMED + [udp(2.0, "emit StartSequence tenths=20", 1500)]
        r = run(recs, [Con("arm"), Udp("emit StartSequence", within=3)])
        self.assertTrue(r[1].ok, r[1].why)
        self.assertNotIn("udp lost", r[1].why)

    def test_failed_check_outside_gap_is_not_annotated(self):
        recs = self.ARMED + [sent(10.0, "start"), got(10.0, "ok"), seq(10.5, 6, 4000), seq(11.5, 7, 5000)]
        r = run(recs, [Con("arm"), Con("start"), Udp("emit StartSequence", within=3)])
        self.assertFalse(r[2].ok)
        self.assertNotIn("udp lost", r[2].why)

    def test_reboot_is_not_a_gap(self):
        # seq restarts at 1 with a smaller millis: a new boot, nothing lost.
        recs = [sent(1.0, "arm"), got(1.0, "ok"), seq(1.5, 40, 90000), seq(2.5, 1, 500), seq(3.5, 2, 1500)]
        self.assertEqual(e2e_sim.seq_gaps(recs), [])
        r = run(recs, [Con("arm"), Udp("emit StartSequence", within=3)])
        self.assertNotIn("udp lost", r[1].why)

    def test_gaps_tolerate_duplicates_and_reordering(self):
        recs = [seq(1.0, 3, 1000), seq(1.0, 3, 1000), seq(2.0, 5, 3000), seq(2.1, 4, 2000), seq(3.0, 8, 6000)]
        self.assertEqual([(g.a, g.b) for g in e2e_sim.seq_gaps(recs)], [(5, 8)])

    def test_speed_check_in_gap_is_annotated(self):
        recs = self.ARMED + [notify(2.0, "2acd", "00 00 64 00")]
        r = run(recs, [Con("arm"), SpeedAtLeast(1.9, within=3)])
        self.assertFalse(r[1].ok)
        self.assertTrue(r[1].why.endswith("; udp lost (seq 3->5)"), r[1].why)


class AbsentHoldTest(unittest.TestCase):
    """`udp absent` holds until a datagram logged a flush period after the command arrives."""

    START = [seq(0.5, 1, 500), udp(0.5, "emit ResetPolicy tenths=0", 500),
             sent(1.0, "start"), got(1.0, "err not-armed")]
    STEPS = [Con("start", expect="err not-armed"), UdpAbsent("emit StartSequence")]

    def test_line_arriving_after_the_command_now_fails(self):
        # Logged right after the command, flushed 0.8 s later: the old [anchor, now]
        # window (a few ms) passed over it.
        recs = self.START + [seq(1.8, 2, 1050), udp(1.8, "emit StartSequence tenths=20", 1050),
                             idle(2.8, 2, 2300)]
        r = run(recs, self.STEPS)
        self.assertFalse(r[1].ok)
        self.assertIn("matched t=1.800 emit StartSequence", r[1].why)

    def test_covered_window_passes_and_judges_lines_in_the_hold(self):
        recs = self.START + [seq(1.8, 2, 1050), udp(1.8, "emit Stop tenths=0", 1050), idle(2.8, 2, 2300)]
        r = run(recs, self.STEPS)
        self.assertTrue(r[1].ok, r[1].why)
        self.assertEqual(r[1].why, "no match in 1.80 s (3 udp lines)")

    def test_lost_datagram_behind_a_heartbeat_is_a_gap_and_inconclusive(self):
        # Datagram 2 (carrying the forbidden line) lost on both copies; heartbeat
        # "2 idle" says it was sent. It must neither hide the loss nor close the hold.
        recs = self.START + [idle(2.8, 2, 2300)]
        gaps = e2e_sim.seq_gaps(recs)
        self.assertEqual([(g.a, g.b, g.t_b) for g in gaps], [(1, 3, 2.8)])
        r = run(recs, self.STEPS)
        self.assertFalse(r[1].ok)
        self.assertTrue(r[1].why.startswith("inconclusive: datagram 2 lost"), r[1].why)
        self.assertTrue(r[1].why.endswith("; udp lost (seq 1->3)"), r[1].why)

    def test_heartbeat_after_received_datagram_closes_the_hold(self):
        recs = self.START + [seq(1.8, 2, 1050), udp(1.8, "emit Stop tenths=0", 1050), idle(2.8, 2, 2300)]
        self.assertEqual(e2e_sim.seq_gaps(recs), [])
        r = run(recs, self.STEPS)
        self.assertTrue(r[1].ok, r[1].why)
        self.assertEqual(r[1].why, "no match in 1.80 s (3 udp lines)")

    def test_hold_with_no_covering_header_is_inconclusive(self):
        recs = self.START + [seq(1.8, 2, 1500)]  # 1500 < 1000 + 500 + 1000: not a flush past the anchor
        r = run(recs, self.STEPS)
        self.assertFalse(r[1].ok)
        self.assertTrue(r[1].why.startswith("inconclusive: no datagram covered the window"), r[1].why)

    def test_recording_without_seq_holds_2_5_s(self):
        recs = [udp(0.5, "emit ResetPolicy tenths=0", 500), sent(1.0, "start"), got(1.0, "err not-armed"),
                udp(3.4, "emit StartSequence tenths=20", 3400), udp(3.6, "emit StartSequence tenths=20", 3600)]
        r = run(recs, self.STEPS)
        self.assertFalse(r[1].ok)
        self.assertIn("matched t=3.400", r[1].why)
        r = run([x for x in recs if x["t"] != 3.4], self.STEPS)
        self.assertTrue(r[1].ok, r[1].why)
        self.assertEqual(r[1].why, "no match in 2.50 s (0 udp lines)")

    def test_live_and_replay_agree(self):
        # A clocked session sees records only once its clock has passed them and
        # polls with odd steps, as a live one does; it must judge the same window.
        class Clocked(ReplaySession):
            def timeline(self):
                return [r for r in self._records if r["t"] <= self._cursor + 1e-9]

            def wait(self, seconds):
                self._cursor += min(seconds, 0.037)

        timelines = [
            self.START + [seq(1.8, 2, 1050), udp(1.8, "emit Stop tenths=0", 1050), idle(2.8, 2, 2300)],
            self.START + [seq(1.8, 2, 1050), udp(1.8, "emit StartSequence tenths=20", 1050), idle(2.8, 2, 2300)],
            self.START + [seq(1.8, 2, 1500)],
            self.START + [idle(2.8, 2, 2300)],
            [x for x in self.START if not x["line"].startswith("udp-seq")] + [udp(2.4, "x", 2400)],
        ]
        for recs in timelines:
            live = run_scenario(Scenario("t", self.STEPS), Clocked.from_records(recs))
            replay = run(recs, self.STEPS)
            self.assertEqual([(x.ok, x.why) for x in live], [(x.ok, x.why) for x in replay])


class FakeConsole:
    """A port that answers like the firmware: `answers` maps a command to the bytes it gets back."""

    def __init__(self, answers):
        import queue
        self.answers, self.written, self._out = answers, [], queue.Queue()

    def write(self, b):
        self.written.append(b)
        self._out.put(self.answers.get(b.decode().strip(), b"ok\n"))

    def read(self, n):
        import queue
        try:
            return self._out.get(timeout=0.05)
        except queue.Empty:
            return b""

    def flush(self):
        pass

    def close(self):
        pass


class SrcGateTest(unittest.TestCase):
    """The src=sim gate before every scenario, and a reboot seen on the console."""

    SIM = b"status armed=0 running=0 wizard=off src=sim\nok\n"

    def setUp(self):
        self._t = e2e_sim.CONSOLE_TIMEOUT_S
        e2e_sim.CONSOLE_TIMEOUT_S = 1.0
        self.dir = tempfile.TemporaryDirectory()
        e2e_sim.print = lambda *a, **k: None

    def tearDown(self):
        e2e_sim.CONSOLE_TIMEOUT_S = self._t
        del e2e_sim.print
        self.dir.cleanup()

    def live(self, answers):
        s = LiveSession("/dev/fake", pathlib.Path(self.dir.name))
        s._ser = FakeConsole(answers)
        s._thread(s._read_serial, "serial")
        s._watch_boot = True  # as after the preconditions
        return s

    def test_src_real_sends_only_status_and_exits_2(self):
        s = self.live({"status": b"status armed=0 running=0 wizard=off src=real\nok\n"})
        try:
            results = e2e_sim.run_all(list(e2e_sim.SCENARIOS), s)
        finally:
            s.close()
        self.assertEqual(s._ser.written, [b"status\n"])
        self.assertIn("src=real is not the simulated belt", s.broken)
        self.assertEqual(list(results), ["boot"])
        self.assertEqual(exit_code(all(e2e_sim.scenario_ok(r) for r in results.values()), bool(s.broken)), 2)

    def test_boot_banner_mid_run_breaks_the_bench(self):
        # `arm` gets no ok: the board resets and prints its banner instead.
        banner = b"ESP-ROM:esp32s3-20210327\nbelt source: BeltLink\nsetup: already done\n"
        s = self.live({"status": self.SIM, "arm": banner})
        sc = Scenario("t", [Con("arm"), Con("start")])
        try:
            r = run_scenario(from_known_state(sc), s)
        finally:
            s.close()
        self.assertEqual(s._ser.written, [b"status\n", b"stop\n", b"arm\n"])
        self.assertEqual([x.ok for x in r], [True, True, False])
        self.assertIn("bench broken: device rebooted", r[2].why)
        self.assertIn("ESP-ROM:esp32s3", s.broken)
        self.assertEqual(exit_code(e2e_sim.scenario_ok(r), bool(s.broken)), 2)

    def test_gate_is_skipped_only_for_recordings_without_it(self):
        old = [sent(1.0, "stop"), got(1.0, "ok")]
        r = run_scenario(from_known_state(Scenario("t", [])), ReplaySession.from_records(old))
        self.assertEqual([x.step for x in r], ["con stop -> *"])
        new = [sent(1.0, "status"), got(1.0, "status src=real"), got(1.0, "ok"), sent(2.0, "stop"), got(2.0, "ok")]
        s = ReplaySession.from_records(new)
        r = run_scenario(from_known_state(Scenario("t", [])), s)
        self.assertEqual([(x.step, x.ok) for x in r], [("gate src=sim", False)])
        self.assertIn("src=real", s.broken)


class FakeSock:
    def __init__(self):
        self.sent = []

    def sendto(self, data, addr):
        self.sent.append((data, addr))


class LiveUdpTest(unittest.TestCase):
    """LiveSession's datagram handling with no socket thread."""

    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.s = LiveSession("/dev/fake", pathlib.Path(self.dir.name))
        self.s._sock = FakeSock()

    def tearDown(self):
        self.dir.cleanup()

    def test_hello_goes_to_the_bridge_on_first_datagram_and_every_60_s(self):
        ip = ("192.168.178.96", 9999)
        self.s._on_datagram(b"1000 udp-seq 1\n1000 udp log ON\n", ip)
        self.assertEqual(self.s._sock.sent, [(b"hello", ("192.168.178.96", 9999))])
        self.s._maybe_hello(self.s._hello_at + 30)
        self.assertEqual(len(self.s._sock.sent), 1)
        self.s._maybe_hello(self.s._hello_at + 60)
        self.assertEqual(len(self.s._sock.sent), 2)

    def test_duplicate_datagram_is_recorded_once(self):
        # Broadcast and unicast copies of one datagram both reach the listener.
        d = b"1000 udp-seq 7\n1000 emit Stop tenths=0\n"
        self.s._on_datagram(d, ("192.168.178.96", 9999))
        self.s._on_datagram(d, ("192.168.178.96", 9999))
        lines = [r["line"] for r in self.s.timeline() if r["stream"] == "udp"]
        self.assertEqual(lines, ["udp-seq 7", "emit Stop tenths=0"])
        self.assertEqual(self.s.udp_duplicates, 1)
        self.assertEqual(len(self.s._udp_raw), 2)


if __name__ == "__main__":
    unittest.main()
