#!/bin/zsh
# PostToolUse hook: docs/protocol/*.md is generated from protocol/*.yaml, and
# CI fails on stale output. Catching it at the edit is far cheaper than finding
# it in CI twenty minutes later.
#
# Unlike the screenshot check this needs no hardware, so it runs on every edit
# and is quick enough not to be noticed.
#
# Exit 2 blocks and feeds stderr back to the agent.

set -u
cd "${CLAUDE_PROJECT_DIR:-.}" || exit 0
[[ -f tools/check_protocol_views.py ]] || exit 0
command -v git >/dev/null 2>&1 || exit 0
git rev-parse --git-dir >/dev/null 2>&1 || exit 0

# Only when the source of truth has actually been touched.
changed=$( { git diff --name-only HEAD 2>/dev/null; git ls-files --others --exclude-standard 2>/dev/null; } | sort -u )
print -r -- "$changed" | grep -qE '^protocol/.*\.ya?ml$' || exit 0

out=$(python3 tools/check_protocol_views.py 2>&1)
status=$?
[[ $status -eq 0 ]] && exit 0

print -u2 "protocol/*.yaml changed but the generated docs are stale:"
print -u2 "$out"
print -u2 ""
print -u2 "Regenerate before finishing (CI runs the same check):"
print -u2 "  python3 tools/gen_protocol_reference.py"
print -u2 "  python3 tools/gen_frame_layouts.py"
print -u2 "  python3 tools/check_protocol_views.py"
print -u2 ""
print -u2 "Never hand-edit docs/protocol/*.md - the YAML is the source of truth."
exit 2
