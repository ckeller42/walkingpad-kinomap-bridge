#!/usr/bin/env python3
"""Listen to the bridge's UDP session log.

    tools/logcat.py                  print lines as they arrive
    tools/logcat.py run.log          also append them to run.log

The point of this is that it does not touch the serial port. Opening the serial
port resets the ESP32-S3, which drops both the belt link and Kinomap's GATT
link, so the console cannot be used during the sessions actually worth watching.
This can run for an entire workout without disturbing anything.

The bridge broadcasts, so nothing needs configuring here beyond being on the
same network. If nothing arrives: check `log` on the console for the Wi-Fi
state, and remember macOS asks once for local-network permission.
"""
import socket
import sys
import time

PORT = 9999


def main():
    out = None
    if len(sys.argv) > 1:
        out = open(sys.argv[1], "a", buffering=1)
        print(f"appending to {sys.argv[1]}", file=sys.stderr)

    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    except (AttributeError, OSError):
        pass
    s.bind(("", PORT))
    print(f"listening on udp/{PORT} — ctrl-c to stop", file=sys.stderr)

    seen = 0
    last_note = time.time()
    while True:
        try:
            data, addr = s.recvfrom(2048)
        except KeyboardInterrupt:
            print(f"\n{seen} lines", file=sys.stderr)
            return
        text = data.decode("utf-8", "replace")
        for ln in text.splitlines():
            if not ln:
                continue
            seen += 1
            print(f"{addr[0]}  {ln}", flush=True)
            if out:
                out.write(ln + "\n")
        # A long silence usually means the bridge dropped off the network, which
        # is worth saying out loud rather than looking like a quiet session.
        if time.time() - last_note > 30 and seen == 0:
            print("(nothing yet — is the bridge on Wi-Fi? check `log` on the console)",
                  file=sys.stderr)
            last_note = time.time()


if __name__ == "__main__":
    main()
