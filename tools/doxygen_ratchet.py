#!/usr/bin/env python3
"""Hold Doxygen's undocumented-symbol count to a baseline, so it can only fall.

Documenting every public symbol in one change is a large mechanical edit, and a
CI gate that fails until that is finished tends to get switched off. This counts
the warnings instead and fails only when the number goes UP, so new code must be
documented while the existing gaps are paid off at whatever pace suits.

    tools/doxygen_ratchet.py build/doxygen/warnings.txt .doxygen-baseline

Lower the baseline whenever the count drops; the script prints the new value.
"""
import pathlib
import sys


def main():
    if len(sys.argv) < 3:
        sys.exit("usage: doxygen_ratchet.py <warnings.txt> <baseline-file>")
    warn_path, base_path = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])

    text = warn_path.read_text(errors="replace") if warn_path.exists() else ""
    lines = [ln for ln in text.splitlines() if ln.strip()]
    count = len(lines)

    baseline = int(base_path.read_text().strip()) if base_path.exists() else count
    print(f"doxygen warnings: {count}  baseline: {baseline}")

    if count > baseline:
        print("\nThe undocumented-symbol count went UP. New or changed public")
        print("symbols need a doc comment. Recent warnings:\n")
        for ln in lines[-40:]:
            print("  " + ln)
        return 1
    if count < baseline:
        print(f"\nImproved: lower the baseline in {base_path} to {count}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
