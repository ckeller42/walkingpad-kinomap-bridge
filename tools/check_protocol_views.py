#!/usr/bin/env python3
"""Staleness gate: fail if a committed protocol view differs from a fresh regeneration.

Regenerates every view into a temporary directory and compares it with the
committed copy in docs/protocol/. Exits 0 when all match, 1 on any difference
(missing file, stale content, or a stray extra generated file), 2 on a
generator failure. Prints a unified diff so the failure is self-explanatory.

    python tools/check_protocol_views.py

Needs PyYAML (pip install pyyaml). No other dependencies, no hardware.
"""
import difflib
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
COMMITTED = HERE.parent / "docs" / "protocol"

# generator script -> view file it writes
GENERATORS = {
    "gen_protocol_reference.py": "reference.md",
    "gen_frame_layouts.py": "frame-layouts.md",
}
HAND_WRITTEN = {"README.md"}
BANNER_MARK = "**GENERATED**"


def main():
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        for script, view in GENERATORS.items():
            proc = subprocess.run(
                [sys.executable, str(HERE / script), "--out", tmp],
                capture_output=True, text=True, cwd=str(HERE),
            )
            if proc.returncode != 0:
                print(f"FAIL: {script} crashed:\n{proc.stderr}", file=sys.stderr)
                return 2
            fresh = (Path(tmp) / view).read_text(encoding="utf-8")
            path = COMMITTED / view
            if not path.exists():
                print(f"FAIL: {view} is not committed; run: python tools/{script}")
                failures += 1
                continue
            committed = path.read_text(encoding="utf-8")
            if committed != fresh:
                failures += 1
                print(f"FAIL: docs/protocol/{view} is stale; run: python tools/{script}")
                sys.stdout.writelines(difflib.unified_diff(
                    committed.splitlines(keepends=True), fresh.splitlines(keepends=True),
                    fromfile=f"committed/{view}", tofile=f"regenerated/{view}", n=2))
        # A generated-looking file nobody produces means a generator was dropped.
        known = set(GENERATORS.values()) | HAND_WRITTEN
        for extra in sorted(COMMITTED.glob("*.md")):
            if extra.name not in known and BANNER_MARK in extra.read_text(encoding="utf-8")[:400]:
                print(f"FAIL: docs/protocol/{extra.name} carries a GENERATED banner but no generator produces it")
                failures += 1
    if failures:
        print(f"\n{failures} protocol view(s) out of date.")
        return 1
    print("protocol views up to date")
    return 0


if __name__ == "__main__":
    sys.exit(main())
