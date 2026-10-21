# Protocol documentation

The protocol facts for this bridge live in exactly one place:

- [`protocol/wilink.yaml`](../../protocol/wilink.yaml): the treadmill side (service `FE00`)
- [`protocol/ftms.yaml`](../../protocol/ftms.yaml): the Kinomap side (service `1826`)
- [`protocol/domyos.yaml`](../../protocol/domyos.yaml): the second Kinomap side,
  the proprietary profile of the optional Domyos mode (service
  `638AF000-7BDE-3E25-FFC5-9DE9B2A0197A`). Nothing in it has been measured yet.

Every other file in this directory except this README is **generated** from
those three files. Do not hand-edit a generated file; edit the YAML and
regenerate. Where the YAML and [`docs/test-log.md`](../test-log.md) differ, the
test log wins: it is the measured record.

Each fact in the YAML carries a provenance: `measured` (observed on real
hardware), `community` (reverse-engineering projects, a decompile or the design
doc), `vendor-app` (the KS Fit and Kinomap decompiles) or `disputed` (two
sources disagree, both readings are recorded and neither is implemented until a
measurement settles it). The provenance survives into the generated views.

## Views

| View | What it is | Regenerate with |
|---|---|---|
| [reference.md](reference.md) | Commands, characteristics, enumerations, behavioural constraints, the OTA prohibition | `python tools/gen_protocol_reference.py` |
| [frame-layouts.md](frame-layouts.md) | Byte-offset tables for WiLink command and status frames and the FTMS payloads, with worked examples | `python tools/gen_frame_layouts.py` |

Both generators accept `--out DIR` to write somewhere other than
`docs/protocol/`. They need Python 3 and PyYAML. On the author's Mac that is
`~/tools/venv-ble/bin/python`. Regenerate everything at once:

```sh
python tools/gen_protocol_reference.py && python tools/gen_frame_layouts.py
```

Output is deterministic (no timestamps, no absolute paths), so regenerating
twice produces identical bytes.

## Staleness gate

`tools/check_protocol_views.py` regenerates every view into a temporary
directory and diffs it against the committed copy. It exits non-zero, with a
unified diff, if any view is stale, missing, or an orphaned generated file. It
also fails if a `protocol/*.yaml` exists that no view reads, so adding a
protocol and forgetting to wire up the generators cannot pass quietly; the list
of inputs is `PROTOCOLS` in `tools/protocol_common.py`.
This is the analogue of a pytest that fails when a committed view does not
match a fresh regeneration; PlatformIO/Unity cannot check markdown, so it is a
plain script.

```sh
python tools/check_protocol_views.py
```

### CI job to add

CI is owned by a separate change under `.github/`, so this directory does not
edit it. Add one step (after checkout, with Python 3 available) to a job that
runs on every push and pull request:

```yaml
      - name: Protocol views are up to date
        run: |
          python3 -m pip install pyyaml
          python3 tools/check_protocol_views.py
```

The check is hardware-free and takes well under a second.

## Adding or changing a protocol fact

1. Edit the YAML. Give the fact a `provenance` and an `evidence` pointer.
2. Run the regeneration command above. The generators validate the YAML
   (contiguous byte offsets, frame lengths, enum references, CRCs of the
   captured status frame) and refuse to write if it is inconsistent.
3. Commit the YAML and the regenerated views together.
