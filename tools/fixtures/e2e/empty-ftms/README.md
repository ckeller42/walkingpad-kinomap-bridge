# Fixture: empty FTMS evidence

Synthetic, not a recording. This is `docs/captures/e2e-sim-2026-10-23/timeline.jsonl`
with every `ftms` record deleted except `connect`, `disconnect` and `write`: no
`2ACD` notification and no `2AD9` indication is left, as if the host had
written to the bridge but heard nothing back. `tools/e2e_sim.py --replay` on
this directory must exit 1 with the speed checks reporting
`stream silent: no 2acd notification recorded` (the first is
`ui_start_stop 2acd >= 1.90 within 12 s`), never PASS. The `2AD9` indication
checks of `phone_start` necessarily go red with them, as
`stream silent: no 2ad9 indication recorded`. The `ftms connect`, `ftms cp`
and `ftms disconnect` steps, and every console and `udp` check, stay green.
