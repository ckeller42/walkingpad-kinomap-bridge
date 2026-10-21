#!/usr/bin/env python3
"""Hold Doxygen's undocumented-symbol count to a baseline, so it can only fall.

Documenting every public symbol in one change is a large mechanical edit, and a
CI gate that fails until that is finished tends to get switched off. This counts
the warnings instead and fails only when the number goes UP, so new code must be
documented while the existing gaps are paid off at whatever pace suits.

    tools/doxygen_ratchet.py build/doxygen/warnings.txt .doxygen-baseline

Two things keep it from crying wolf:

* Only "not documented" warnings are counted. Doxygen emits other diagnostics
  (unresolved references, duplicated names) that come and go with unrelated
  edits and would make the count jitter.
* The baseline file records the Doxygen version that produced it. Versions
  disagree about what counts as undocumented, so a count taken under a
  different version is reported rather than treated as a regression - a gate
  that fails for reasons unconnected to the code is a gate people disable.

Baseline file format, one per line::

    160
    doxygen 1.18.0
"""
import pathlib
import re
import shutil
import subprocess
import sys

UNDOCUMENTED = re.compile(r"(is|are) not documented")


def doxygen_version() -> str:
    exe = shutil.which("doxygen")
    if not exe:
        return "unknown"
    try:
        out = subprocess.run([exe, "--version"], capture_output=True, text=True, timeout=30)
        return "doxygen " + out.stdout.strip().split()[0]
    except (OSError, subprocess.SubprocessError, IndexError):
        return "unknown"


def read_baseline(path: pathlib.Path):
    """Return (count, version) from the baseline file, or (None, None)."""
    if not path.exists():
        return None, None
    lines = [ln.strip() for ln in path.read_text().splitlines() if ln.strip()]
    if not lines:
        return None, None
    try:
        count = int(lines[0])
    except ValueError:
        return None, None
    return count, (lines[1] if len(lines) > 1 else "unknown")


def main() -> int:
    if len(sys.argv) < 3:
        sys.exit("usage: doxygen_ratchet.py <warnings.txt> <baseline-file>")
    warn_path, base_path = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])

    text = warn_path.read_text(errors="replace") if warn_path.exists() else ""
    hits = [ln for ln in text.splitlines() if UNDOCUMENTED.search(ln)]
    count = len(hits)
    version = doxygen_version()

    baseline, base_version = read_baseline(base_path)
    if baseline is None:
        print(f"no baseline yet; recording {count} ({version})")
        base_path.write_text(f"{count}\n{version}\n")
        return 0

    print(f"undocumented: {count} ({version})   baseline: {baseline} ({base_version})")

    if base_version != version and base_version != "unknown":
        print(
            "\nDifferent Doxygen version than the baseline was taken with, so the\n"
            "counts are not comparable. Reporting only.\n"
            f"Re-record with this version by deleting {base_path} and re-running."
        )
        return 0

    if count > baseline:
        print(f"\nUndocumented symbols went UP by {count - baseline}. New or changed")
        print("public symbols need a doc comment, including @param and @return.\n")
        for ln in hits[-40:]:
            print("  " + ln)
        return 1
    if count < baseline:
        print(f"\nImproved by {baseline - count}: update {base_path} to {count}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
