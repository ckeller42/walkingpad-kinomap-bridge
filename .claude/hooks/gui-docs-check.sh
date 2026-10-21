#!/bin/zsh
# Stop hook: refuse to finish a turn that leaves the GUI newer than its
# screenshots.
#
# The screenshots in docs/screens are real captures from a cores3_shot build,
# not mock-ups. That is what makes them worth having, and what makes them rot
# invisibly: a wizard screen whose wording changed still looks authoritative in
# a stale PNG, and nothing in CI can tell.
#
# It compares the last COMMIT touching GUI sources against the last commit
# touching the screenshots, and also looks at the working tree. Checking only
# uncommitted changes would be satisfied by committing and finishing, which is
# exactly what an agent does by default.
#
# Exit 2 blocks and feeds stderr back to the agent. There is a deliberate
# escape, because regenerating needs the physical device: create
# .gui-shots-pending with a one-line reason, and say so in the reply.

set -u
cd "${CLAUDE_PROJECT_DIR:-.}" || exit 0
command -v git >/dev/null 2>&1 || exit 0
git rev-parse --git-dir >/dev/null 2>&1 || exit 0

GUI=(src/ui.cpp src/ui.h lib/wizard lib/devmenu)
SHOTS=(docs/screens docs/setup-wizard.md)

if [[ -f .gui-shots-pending ]]; then
  print -u2 "GUI screenshots are marked pending: $(head -1 .gui-shots-pending)"
  print -u2 "Tell the user they are still outstanding."
  exit 0
fi

gui_t=$(git log -1 --format=%ct -- "${GUI[@]}" 2>/dev/null)
shot_t=$(git log -1 --format=%ct -- "${SHOTS[@]}" 2>/dev/null)
[[ -z "$gui_t" ]] && exit 0
[[ -z "$shot_t" ]] && shot_t=0

# Uncommitted work counts as newer than any commit.
dirty=$( { git diff --name-only HEAD 2>/dev/null; git ls-files --others --exclude-standard 2>/dev/null; } | sort -u )
gui_dirty=0 shot_dirty=0
for p in "${GUI[@]}"; do
  print -r -- "$dirty" | grep -q "^${p}" && gui_dirty=1
done
for p in "${SHOTS[@]}"; do
  print -r -- "$dirty" | grep -q "^${p}" && shot_dirty=1
done

# Fine when the screenshots were refreshed in the same breath as the GUI.
(( gui_dirty && shot_dirty )) && exit 0
(( shot_dirty )) && exit 0
(( gui_dirty == 0 )) && [[ "$gui_t" -le "$shot_t" ]] && exit 0

print -u2 "The GUI is newer than its screenshots."
if (( gui_dirty )); then
  print -u2 "  uncommitted GUI changes, screenshots untouched"
else
  print -u2 "  GUI last changed:        $(date -r "$gui_t" 2>/dev/null || echo "$gui_t")"
  print -u2 "  screenshots last changed: $(date -r "$shot_t" 2>/dev/null || echo "$shot_t")"
fi
print -u2 ""
print -u2 "docs/screens/*.png are real captures, so a wording change leaves a"
print -u2 "stale PNG that still looks authoritative. Regenerate them:"
print -u2 ""
print -u2 "  ~/.platformio/penv/bin/pio run -e cores3_shot -t upload --upload-port /dev/cu.usbmodem101"
print -u2 "  ~/tools/venv-ble/bin/python tools/capture_shots.py --port /dev/cu.usbmodem101 \\"
print -u2 "      --seconds 70 --outdir docs/screens"
print -u2 ""
print -u2 "Then update docs/setup-wizard.md if any wording changed, and re-flash"
print -u2 "the normal build:"
print -u2 "  ~/.platformio/penv/bin/pio run -e cores3 -t upload --upload-port /dev/cu.usbmodem101"
print -u2 ""
print -u2 "If the device is unavailable, create .gui-shots-pending with a"
print -u2 "one-line reason and tell the user the screenshots are outstanding."
exit 2
