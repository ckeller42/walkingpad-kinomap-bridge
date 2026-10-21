#!/usr/bin/env python3
"""Decode serial screenshot frames (src/shot.h) into PNG files.

    decode_shots.py capture.txt -o docs/screens [--names a,b,c] [--prefix shot]

Frame format:  [SHOT w h RLE16 n] / "S:<base64>" lines / [/SHOT]
The payload is n runs of (count, pixel hi, pixel lo), RGB565, row-major.
Log lines interleaved in the capture are ignored. A frame that is cut off
(capture too short) or whose run total does not match w*h is reported and
skipped, never written as a broken PNG. Identical frames yield one PNG.
Needs Pillow.
"""
import argparse
import base64
import binascii
import hashlib
import re
import sys
from pathlib import Path

HEAD = re.compile(r"\[SHOT (\d+) (\d+) RLE16 (\d+)\]")


def parse_frames(text):
    """Yield (w, h, n, payload_b64 or None) for every frame start found."""
    lines = text.splitlines()
    i = 0
    while i < len(lines):
        m = HEAD.search(lines[i])
        i += 1
        if not m:
            continue
        w, h, n = (int(x) for x in m.groups())
        chunks, closed = [], False
        while i < len(lines):
            ln = lines[i].strip()
            if HEAD.search(ln):  # next frame began: this one was cut off
                break
            i += 1
            if ln.startswith("[/SHOT]"):
                closed = True
                break
            if ln.startswith("S:"):
                chunks.append(ln[2:])
        yield w, h, n, ("".join(chunks) if closed else None)


def rgb565_to_rgb(v):
    r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
    return (r * 255 // 31, g * 255 // 63, b * 255 // 31)


def decode_frame(w, h, n, b64):
    """Return a Pillow image, or raise ValueError describing the damage."""
    from PIL import Image

    try:
        raw = base64.b64decode(b64, validate=True)
    except (binascii.Error, ValueError) as e:
        raise ValueError(f"bad base64: {e}")
    if len(raw) != 3 * n:
        raise ValueError(f"payload has {len(raw)} bytes, header says {n} runs ({3 * n} bytes)")
    px = bytearray()
    cache = {}
    total = 0
    for k in range(0, len(raw), 3):
        cnt, v = raw[k], (raw[k + 1] << 8) | raw[k + 2]
        if cnt == 0:
            raise ValueError("zero-length run")
        rgb = cache.get(v)
        if rgb is None:
            rgb = cache[v] = bytes(rgb565_to_rgb(v))
        px += rgb * cnt
        total += cnt
    if total != w * h:
        raise ValueError(f"runs cover {total} pixels, expected {w * h}")
    return Image.frombytes("RGB", (w, h), bytes(px))


def decode_text(text, outdir, names=(), prefix="shot"):
    outdir.mkdir(parents=True, exist_ok=True)
    seen, written, bad = set(), [], 0
    for w, h, n, b64 in parse_frames(text):
        if b64 is None:
            print(f"skip: truncated frame {w}x{h} ({n} runs); capture longer", file=sys.stderr)
            bad += 1
            continue
        try:
            img = decode_frame(w, h, n, b64)
        except ValueError as e:
            print(f"skip: damaged frame: {e}", file=sys.stderr)
            bad += 1
            continue
        digest = hashlib.sha1(img.tobytes()).hexdigest()
        if digest in seen:
            continue
        seen.add(digest)
        idx = len(written)
        stem = names[idx] if idx < len(names) else f"{prefix}-{idx + 1:02d}"
        path = outdir / f"{stem}.png"
        img.save(path)
        written.append(path)
        print(f"wrote {path}")
    return written, bad


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("capture", help="serial capture text file, or - for stdin")
    ap.add_argument("-o", "--outdir", default="docs/screens", type=Path)
    ap.add_argument("--names", default="", help="comma-separated file stems, in frame order")
    ap.add_argument("--prefix", default="shot")
    a = ap.parse_args()
    text = sys.stdin.read() if a.capture == "-" else Path(a.capture).read_text(errors="replace")
    names = [x for x in a.names.split(",") if x]
    written, bad = decode_text(text, a.outdir, names, a.prefix)
    if not written:
        print("no complete frames found", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
