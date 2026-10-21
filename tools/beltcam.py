#!/usr/bin/env python3
"""Photograph the belt's console with the Fairphone, for physical verification.

The belt's status frames are the belt grading its own homework. This captures
what the console actually displays, so a reported speed or distance can be
checked against the machine rather than against its own firmware.

  beltcam.py shot [name]     one photo -> ./out/cam/<name>.jpg plus a cropped
                             close-up of the console at <name>-console.png

Setup assumed: the Fairphone is on Tailscale ADB (see ENVIRONMENT.md) and its
camera is physically pointed at the console. Run `shot` once and look at the
crop before relying on a sequence — the crop window is a fixed fraction of the
frame and only holds while the phone stays put.
"""
import os
import subprocess
import sys
import time

ADB = os.path.expanduser("~/tools/scrcpy/adb")
SERIAL = os.environ.get("BELT_ADB", "100.126.233.97:46363")
SHUTTER = (557, 2063)          # com.fps.camera shutter centre, 1116x2484 portrait
# Console window as a fraction of the EXIF-rotated frame. Re-measure if the
# phone moves: crop too tight and the digits leave the frame entirely.
CROP = (0.34, 0.41, 0.52, 0.53)
OUT = "out/cam"


def adb(*args, **kw):
    return subprocess.run([ADB, "-s", SERIAL, *args],
                          capture_output=True, text=True, **kw)


def shot(name):
    os.makedirs(OUT, exist_ok=True)
    before = adb("shell", "ls", "/sdcard/DCIM/Camera/").stdout.split()
    adb("shell", "input", "tap", str(SHUTTER[0]), str(SHUTTER[1]))
    time.sleep(3.0)
    after = adb("shell", "ls", "/sdcard/DCIM/Camera/").stdout.split()
    new = [f for f in after if f not in before]
    if not new:
        print("no new photo — is the camera app in the foreground?", file=sys.stderr)
        print("  adb -s <serial> shell am start -a android.media.action.STILL_IMAGE_CAMERA",
              file=sys.stderr)
        return 1

    local = f"{OUT}/{name}.jpg"
    adb("pull", f"/sdcard/DCIM/Camera/{new[0]}", local)

    try:
        from PIL import Image, ImageOps
    except ImportError:
        print(f"{local} (install pillow for the console crop)")
        return 0

    im = ImageOps.exif_transpose(Image.open(local))
    w, h = im.size
    box = (int(CROP[0] * w), int(CROP[1] * h), int(CROP[2] * w), int(CROP[3] * h))
    c = im.crop(box)
    c.resize((c.width * 3, c.height * 3), Image.LANCZOS).save(f"{OUT}/{name}-console.png")
    print(f"{local}\n{OUT}/{name}-console.png")
    return 0


if __name__ == "__main__":
    if len(sys.argv) < 2 or sys.argv[1] != "shot":
        print(__doc__)
        sys.exit(0)
    sys.exit(shot(sys.argv[2] if len(sys.argv) > 2 else time.strftime("%H%M%S")))
