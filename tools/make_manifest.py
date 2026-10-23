#!/usr/bin/env python3
"""Write, or verify, the OTA manifest that sits next to firmware.bin on Pages.

    tools/make_manifest.py .pio/build/cores3/firmware.bin --version v1.2-3-gabc1234 \\
        --env cores3 --url https://ckeller42.github.io/walkingpad-kinomap-bridge/firmware/firmware.bin \\
        > manifest.json
    tools/make_manifest.py --check manifest.json .pio/build/cores3/firmware.bin

The device's Update screen fetches the manifest, compares `version` with its
own build, refuses an image built for another `env` (cores3 vs cores3_sim),
then streams `url` and accepts it only if the bytes hash to `sha256`. So the
manifest is a promise about one specific file, and a manifest that does not
describe the file published beside it makes every device refuse the update -
or, worse, sends it to a stale image. `--check` turns that promise into a CI
step: it re-hashes the file and fails, naming the field, on any mismatch or on
a field that is missing or empty, so a broken pair is never deployed.

The JSON is flat with sorted keys because the firmware's extractor
(`src/ota.cpp` `field()`) is a flat-object scanner, not a JSON parser.
Stdlib only, Python 3.9.
"""
import argparse
import hashlib
import json
import pathlib
import sys

FIELDS = ("env", "sha256", "size", "url", "version")
# src/ota.cpp holds the version in char availVer[32] and field() truncates
# silently; a truncated version never equals the running one, so the device
# would offer the same image forever.
VERSION_MAX = 31


def version_problem(version):
    """Return a problem string if `version` will not fit the device's buffer, else None."""
    if isinstance(version, str) and len(version) > VERSION_MAX:
        return f"version: {len(version)} characters, the device holds at most {VERSION_MAX}"
    return None


def describe(firmware):
    """Return (sha256 hex, size in bytes) of the file at `firmware`."""
    digest = hashlib.sha256()
    size = 0
    with open(firmware, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 16), b""):
            digest.update(chunk)
            size += len(chunk)
    return digest.hexdigest(), size


def make(firmware, version, env, url):
    """Return the manifest dict for `firmware`."""
    sha256, size = describe(firmware)
    return {"env": env, "sha256": sha256, "size": size, "url": url, "version": version}


def render(manifest):
    """Serialise a manifest exactly as it is published: sorted keys, trailing newline."""
    return json.dumps(manifest, indent=2, sort_keys=True) + "\n"


def check(manifest, firmware):
    """Return a list of problems with `manifest` (a dict) against `firmware`; empty means valid."""
    problems = []
    for key in FIELDS:
        value = manifest.get(key)
        if value is None or value == "":
            problems.append(f"{key}: missing or empty")
    too_long = version_problem(manifest.get("version"))
    if too_long:
        problems.append(too_long)
    sha256, size = describe(firmware)
    if "sha256" in manifest and manifest["sha256"] not in (None, "") and manifest["sha256"] != sha256:
        problems.append(f"sha256: manifest {manifest['sha256']}, file {sha256}")
    if "size" in manifest and manifest["size"] not in (None, "") and manifest["size"] != size:
        problems.append(f"size: manifest {manifest['size']!r}, file {size}")
    return problems


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--check", nargs=2, metavar=("MANIFEST", "FIRMWARE"),
                    help="verify MANIFEST describes FIRMWARE; exit 1 naming the field otherwise")
    ap.add_argument("firmware", nargs="?", help="firmware image to describe")
    ap.add_argument("--version", help="firmware version string (git describe)")
    ap.add_argument("--env", help="PlatformIO environment the image was built for")
    ap.add_argument("--url", help="where the device downloads the image")
    args = ap.parse_args(argv)

    if args.check:
        manifest_path, firmware = args.check
        stray = [opt for opt in ("firmware", "version", "env", "url") if getattr(args, opt) is not None]
        if stray:
            # A CI step that passes both shapes meant one of them; silently
            # checking and ignoring --version would hide a wrong manifest.
            ap.error("--check takes only MANIFEST FIRMWARE; also given: " + ", ".join(stray))
        try:
            manifest = json.loads(pathlib.Path(manifest_path).read_text())
        except (OSError, ValueError) as exc:
            print(f"FAIL {manifest_path}: unreadable manifest: {exc}")
            return 1
        if not isinstance(manifest, dict):
            print(f"FAIL {manifest_path}: not a JSON object")
            return 1
        try:
            problems = check(manifest, firmware)
        except OSError as exc:
            print(f"FAIL {manifest_path}: cannot read firmware {firmware}: {exc.strerror or exc}")
            return 1
        for problem in problems:
            print(f"FAIL {manifest_path}: {problem}")
        if problems:
            return 1
        print(f"OK {manifest_path} matches {firmware} ({manifest['size']} bytes, {manifest['version']})")
        return 0

    missing = [opt for opt in ("firmware", "version", "env", "url") if not getattr(args, opt)]
    if missing:
        ap.error("missing " + ", ".join(missing) + " (or use --check MANIFEST FIRMWARE)")
    too_long = version_problem(args.version)
    if too_long:
        print(f"FAIL {too_long}", file=sys.stderr)
        return 1
    try:
        manifest = make(args.firmware, args.version, args.env, args.url)
    except OSError as exc:
        print(f"FAIL cannot read firmware {args.firmware}: {exc.strerror or exc}", file=sys.stderr)
        return 1
    sys.stdout.write(render(manifest))
    return 0


if __name__ == "__main__":
    sys.exit(main())
