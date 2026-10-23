# Fixture: UDP log cut before the first start

Synthetic, not a recording. This is `docs/captures/e2e-sim-2026-10-23/timeline.jsonl`
with every `udp` record at and after the first `emit StartSequence` deleted
(console and `ftms` records are untouched). `tools/e2e_sim.py --replay` on this
directory must exit 1, and the check this fixture exists for is
`ui_start_stop udp /emit StartSequence tenths=20/ within 3 s`. Because the cut
removes the rest of the UDP stream, every later positive `udp` check fails with
it, necessarily: `emit NotifyStarted` in `ui_start_stop`, `emit StartSequence`
and `emit SetSpeed tenths=25` in `phone_start`, and the four `udp` checks of
`auto_mode`. No console or `ftms` check fails.

It also shows a known weakness, on purpose: `auto_mode udp absent /emit SetSpeed/`
still passes (`no match ... (0 udp lines)`), because an absence check cannot
tell "the bridge emitted nothing" from "the log carried nothing". The same
happens live when one UDP datagram is lost; see "End to end on the bench" in
`docs/developing.md`.
