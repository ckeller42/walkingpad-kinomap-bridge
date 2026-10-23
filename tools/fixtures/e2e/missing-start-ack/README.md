# Fixture: missing start acknowledgement

Synthetic, not a recording. This is `docs/captures/e2e-sim-2026-10-23/timeline.jsonl`
with exactly one record deleted: the `ftms` indication `80 07 01` on `2AD9`
(the bridge's success reply to the phone's Start/Resume, `cp 07`, in the
`phone_start` scenario). `tools/e2e_sim.py --replay` on this directory must exit
1 with exactly one check red: `phone_start indication 80 07 01 within 5 s`
(`no match in 5 s`). Every other check stays green, because the indication
stream is otherwise intact and nothing downstream waits on that one record.
