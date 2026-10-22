#!/bin/zsh
# Prompt for the bridge's Wi-Fi password and store it on the device.
#
#   tools/set_wifi.sh <ssid>
#
# The password is read from a hidden GUI dialog and handed straight to the
# bridge console. It is never echoed, never written to a file, and never
# appears in this repository: the bridge keeps it in NVS. The console's reply
# deliberately prints the ssid only.
#
# zenity is the Linux tool for this; on macOS the equivalent is osascript with
# "hidden answer", which this uses.
set -u

SSID="${1:-}"
if [[ -z "$SSID" ]]; then
  echo "usage: tools/set_wifi.sh <ssid>" >&2
  exit 2
fi

PORT="${BRIDGE_PORT:-/dev/cu.usbmodem101}"
PY="$HOME/tools/venv-ble/bin/python"
HERE="${0:A:h}"

if command -v zenity >/dev/null 2>&1; then
  PASS=$(zenity --password --title="Wi-Fi password for $SSID" 2>/dev/null) || exit 1
else
  PASS=$(osascript -e "display dialog \"Wi-Fi password for $SSID\" default answer \"\" with hidden answer with title \"WalkingPad bridge\"" -e 'text returned of result' 2>/dev/null) || exit 1
fi

if [[ -z "$PASS" ]]; then
  echo "no password entered; nothing stored" >&2
  exit 1
fi

# Opening the port reboots the bridge, so settle first, then store, then read
# back the sink state. Only the console's own output reaches the terminal.
"$PY" "$HERE/console.py" --port "$PORT" --timeout 90 "wait 12; wifi $SSID $PASS; wait 12; log"
rc=$?
unset PASS
exit $rc
