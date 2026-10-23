"""Host tests for tools/make_manifest.py: manifest shape and --check, no hardware.

    ~/tools/venv-ble/bin/python -m unittest tools/test_make_manifest.py
"""
import contextlib
import hashlib
import io
import json
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import make_manifest  # noqa: E402

PAYLOAD = b"walkingpad-bridge firmware image\x00\x01\x02\xff"
VERSION = "v1.2.3-4-gabcdef0"
URL = "https://ckeller42.github.io/walkingpad-kinomap-bridge/firmware/firmware.bin"


def run(argv):
    """Run main(argv); return (exit code, stdout, stderr)."""
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        rc = make_manifest.main(argv)
    return rc, out.getvalue(), err.getvalue()


class MakeManifestTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = pathlib.Path(self.tmp.name)
        self.fw = self.dir / "firmware.bin"
        self.fw.write_bytes(PAYLOAD)

    def tearDown(self):
        self.tmp.cleanup()

    def generate(self):
        return run([str(self.fw), "--version", VERSION, "--env", "cores3", "--url", URL])

    def manifest(self, drop=(), **override):
        """Write a generated manifest with fields overridden or dropped; return its path."""
        rc, out, _ = self.generate()
        self.assertEqual(rc, 0)
        doc = json.loads(out)
        doc.update(override)
        for key in drop:
            del doc[key]
        path = self.dir / "manifest.json"
        path.write_text(json.dumps(doc))
        return path

    def test_manifest_fields_and_hash(self):
        rc, out, _ = self.generate()
        self.assertEqual(rc, 0)
        expected = {
            "env": "cores3",
            "sha256": hashlib.sha256(PAYLOAD).hexdigest(),
            "size": len(PAYLOAD),
            "url": URL,
            "version": VERSION,
        }
        self.assertEqual(out, json.dumps(expected, indent=2, sort_keys=True) + "\n")

    def test_check_passes_on_matching_manifest(self):
        rc, _, _ = run(["--check", str(self.manifest()), str(self.fw)])
        self.assertEqual(rc, 0)

    def test_check_fails_on_size_mismatch(self):
        rc, out, err = run(["--check", str(self.manifest(size=len(PAYLOAD) + 1)), str(self.fw)])
        self.assertEqual(rc, 1)
        self.assertIn("size", out + err)

    def test_check_fails_on_missing_env(self):
        rc, out, err = run(["--check", str(self.manifest(drop=("env",))), str(self.fw)])
        self.assertEqual(rc, 1)
        self.assertIn("env", out + err)


if __name__ == "__main__":
    unittest.main()
