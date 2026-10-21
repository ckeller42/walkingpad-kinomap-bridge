---
name: protocol-fact
description: Use when adding or changing anything about the WiLink or FTMS wire protocols - new opcode, belt state, constraint or corrected field - so the YAML stays the single source of truth, the generated docs regenerate, and provenance is recorded.
---

# Changing a protocol fact

`protocol/wilink.yaml` and `protocol/ftms.yaml` are the **single source of
truth**. `docs/protocol/*.md` is generated from them and must never be
hand-edited; CI fails on stale output.

## The loop

```sh
# 1. edit protocol/wilink.yaml or protocol/ftms.yaml
python3 tools/gen_protocol_reference.py
python3 tools/gen_frame_layouts.py
python3 tools/check_protocol_views.py     # what CI runs
```

If the code changed too, run the host tests: `pio test -e native`.

## Every fact carries its provenance

```yaml
- {value: 8, name: spin_up, meaning: "Seen between pre-countdown and starting",
   provenance: measured,
   evidence: "test-log: Stage 3, decoded from a captured UDP log"}
```

- **`measured`** — seen on this hardware. `evidence:` says exactly where, so a
  later reader can re-check it.
- **`community`** — from the decompile, a forum, or another project, and **not
  confirmed here**.

This distinction has already paid for itself twice. `start_belt` sat at
`community` for weeks, inherited from the A1/C1 command set and never checked
on an R1 Pro; when it was finally probed it turned out correct, but the
surrounding assumptions about timing did not. Meanwhile a claim written from
the decompile — that Kinomap never sends FTMS Start — was disproven by a
counter.

**Never promote `community` to `measured` without a capture.** Say which test
produced it.

## Where facts come from

- `tools/logparse.py <log>` — decodes a captured session and prints what it
  could not parse. Two belt states and one latent defect came from this.
- `tools/ftms_client.py <km/h>` — acts as an FTMS client against the bridge,
  for exercising paths a phone will not.
- A direct FE02 probe from the Mac, for isolating belt behaviour from the
  bridge's own logic. Group `0xA2` only; never `0xE8`, never the OTA service.

## Things that are settled - do not re-derive them

- The only working stop is **set mode standby**. Set-speed-0 does not stop the
  belt.
- The belt **ignores unrecognised commands silently**, so a wrong guess
  produces no error and looks like nothing happened.
- The belt's console **shows the setpoint, not motion**.
- A status frame can arrive **split across two notifications** (issue #3).
- There is **no incline opcode**; the R1 Pro has no incline mechanism, and
  `2AD5` is deliberately not exposed so Kinomap clamps its targets to zero.
- Kinomap **does not command speed** on a generic FTMS treadmill (issue #2).

## Safety invariants that constrain any new command

- Group `0xE8` is the WiLink OTA group and **can brick the belt**. Only `0xA2`
  is ever emitted, enforced in `BeltLink::send()`.
- The Telink OTA service `00010203-0405-0607-0809-0a0b0c0d1912` is never
  named, connected to, written to or subscribed on.
- The manufacturer string must never be `decathlon`.

If a change would touch any of these, stop and raise it rather than working
around it.
