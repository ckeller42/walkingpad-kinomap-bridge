"""Host tests for tools/e2e_sim.py: parsing, checks and replay, no hardware.

    ~/tools/venv-ble/bin/python -m unittest tools/test_e2e_sim.py
"""
import json
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from e2e_sim import (  # noqa: E402
    Con, ReplaySession, Scenario, SpeedAtLeast, Status, Udp,
    exit_code, ftms_speed_kmh, parse_status, run_scenario, write_report,
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


if __name__ == "__main__":
    unittest.main()
