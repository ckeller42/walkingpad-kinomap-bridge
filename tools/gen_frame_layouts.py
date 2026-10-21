#!/usr/bin/env python3
"""Generate docs/protocol/frame-layouts.md from the files in protocol/.

Reads protocol/wilink.yaml, protocol/ftms.yaml and protocol/domyos.yaml.

Byte-offset tables (these protocols are byte-oriented, not bit-packed), plus
worked examples whose CRCs are computed and whose decodings are produced by
parsing the example bytes with the declared layout.

    python tools/gen_frame_layouts.py [--out DIR]
"""
import protocol_common as pc

GENERATOR = "gen_frame_layouts.py"


def _field_rows(fields, enums, parent):
    rows = []
    for f in fields:
        size = f.get("size", 1)
        end = f["offset"] + size - 1
        span = str(f["offset"]) if size == 1 else f"{f['offset']}–{end}"
        if "const" in f:
            meaning = f"constant `{pc.hexb(f['const'], 2 * size)}`"
        elif "value" in f:
            meaning = f"value `{pc.hexb(f['value'], 2 * size)}`"
        else:
            meaning = ""
        rows.append([
            span, str(size), f["name"], f.get("type", "const" if "const" in f else ""),
            pc.scale_text(f) if ("unit" in f) else "",
            (meaning + ("; " if meaning and f.get("note") else "") + f.get("note", "")).strip(),
            pc.prov(f, parent),
        ])
    return rows


def _decode_rows(fields, data, enums):
    return [[str(f["offset"]) if f.get("size", 1) == 1 else f"{f['offset']}–{f['offset'] + f['size'] - 1}",
             f["name"], f"`{pc.hexbytes(data[f['offset']:f['offset'] + f['size']])}`",
             pc.decode_field(f, data, enums)] for f in fields]


def _bits(field):
    rows = [[str(b["bit"]), b["name"], b["state"], b.get("note", "")] for b in field.get("bits", [])]
    return pc.table(["Bit", "Name", "State", "Note"], rows) if rows else ""


def _wilink(doc):
    out = ["## WiLink\n"]
    ff = doc["frame_format"]["command"]
    out.append("### Command frame\n")
    out.append(f"{ff['length']} bytes, written to `FE02`. Provenance: {ff['provenance']} ({ff['evidence']}).\n")
    rows = []
    for f in ff["fields"]:
        rows.append([str(f["offset"]), f["name"],
                     f"`{pc.hexb(f['const'])}`" if "const" in f else f.get("text", "variable"), f.get("note", "")])
    out.append(pc.table(["Offset", "Field", "Value", "Note"], rows))
    out.append(f"CRC: {doc['frame_format']['crc']['rule']}\n")

    out.append("### Worked command frames\n")
    out.append("CRC computed by the generator; `Argument` is the raw byte.\n")
    rows = []
    for c in doc["commands"]:
        for ex in c["examples"]:
            frame = pc.command_frame(c["group"], c["function"], ex["argument"])
            rows.append([c["name"], ex["label"], pc.hexb(ex["argument"]), f"`{pc.hexbytes(frame)}`",
                         pc.hexb(frame[4])])
    out.append(pc.table(["Command", "Example", "Argument", "Frame", "CRC"], rows))

    sf = doc["status_frame"]
    out.append("### Status frame\n")
    out.append(f"{sf['length']} bytes, read from `FE01`. Provenance: {sf['provenance']} ({sf['evidence']}).\n")
    out.append(pc.table(["Offset", "Size", "Field", "Type", "Unit", "Meaning", "Provenance"],
                        _field_rows(sf["fields"], doc["enums"], sf)))
    for ex in sf["examples"]:
        data = pc.parse_hex(ex["hex"])
        out.append(f"#### Decoded example: {ex['label']}\n")
        out.append(f"`{pc.hexbytes(data)}` — {ex['provenance']} ({ex['evidence']}). {ex['note']}\n")
        out.append(pc.table(["Offset", "Field", "Bytes", "Decoded"], _decode_rows(sf["fields"], data, doc["enums"])))
    return "\n".join(out)


def _payload_sections(doc, title, preamble=None):
    """One subsection per declared payload. Shared by every payload-shaped protocol."""
    out = [f"## {title}\n"]
    if preamble:
        out.append(f"{preamble}\n")
    for p in doc["payloads"]:
        out.append(f"### {p['title']}\n")
        out.append(
            f"Direction: {p['direction']}. Length: {p['length']}"
            f"{'' if p['length'] == 'variable' else (' byte' if p['length'] == 1 else ' bytes')}. "
            f"Provenance: {p['provenance']} ({p['evidence']}).\n"
        )
        out.append(pc.table(["Offset", "Size", "Field", "Type", "Unit", "Meaning", "Provenance"],
                            _field_rows(p["fields"], doc.get("enums", {}), p)))
        for f in p["fields"]:
            if "bits" in f:
                out.append(f"Bits of `{f['name']}`:\n")
                out.append(_bits(f))
        data = pc.parse_hex(p["example"])
        note = f" ({p['example_note']})" if "example_note" in p else ""
        out.append(f"Example{note}: `{pc.hexbytes(data)}`\n")
        shown = [f for f in p["fields"] if f["offset"] < len(data)]
        out.append(pc.table(["Offset", "Field", "Bytes", "Decoded"], _decode_rows(shown, data, doc["enums"])))
    return "\n".join(out)


def generate():
    wilink = pc.load("wilink")
    ftms = pc.load("ftms")
    domyos = pc.load("domyos")
    parts = [
        pc.banner(GENERATOR),
        "# Frame layouts\n",
        "Byte-offset layouts. All multi-byte FTMS and Domyos-mode fields are little-endian; "
        "WiLink 24-bit counters are big-endian. No bit grid is used because none of these "
        "protocols is bit-packed.\n",
        _wilink(wilink),
        _payload_sections(ftms, "FTMS"),
        _payload_sections(
            domyos, "Domyos mode",
            "Nothing below has been measured, and the examples are illustrative values in the "
            "declared layout rather than captures. A payload marked `disputed` has two "
            "incompatible readings; see [reference.md](reference.md) for both."),
    ]
    text = "\n".join(parts)
    while "\n\n\n" in text:
        text = text.replace("\n\n\n", "\n\n")
    return "frame-layouts.md", text.rstrip("\n") + "\n"


if __name__ == "__main__":
    pc.cli(generate, __doc__)
