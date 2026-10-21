"""Shared loading, validation and formatting for the protocol view generators.

The single source of truth is protocol/wilink.yaml and protocol/ftms.yaml.
Everything under docs/protocol/ except README.md is generated from them by
tools/gen_protocol_reference.py and tools/gen_frame_layouts.py, and
tools/check_protocol_views.py fails if a committed view is stale.

Output must be byte-for-byte deterministic: no timestamps, no absolute paths,
no dict-ordering surprises (YAML mappings keep file order).
"""
import argparse
import sys
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parent.parent
PROTOCOL_DIR = ROOT / "protocol"
DEFAULT_OUT = ROOT / "docs" / "protocol"

PROVENANCE = ("measured", "community", "vendor-app")
PROVENANCE_LEGEND = {
    "measured": "observed on real hardware; `docs/test-log.md` is the record",
    "community": "from the reverse-engineering projects or the design doc, not independently captured here",
    "vendor-app": "from the vendor-app decompiles (KS Fit, Kinomap)",
}


class SchemaError(Exception):
    pass


def load(name):
    """Load and validate protocol/<name>.yaml."""
    path = PROTOCOL_DIR / f"{name}.yaml"
    with open(path, encoding="utf-8") as fh:
        doc = yaml.safe_load(fh)
    try:
        _validate(name, doc)
    except SchemaError as exc:
        raise SystemExit(f"{path.name}: schema error: {exc}")
    return doc


def banner(generator, sources=("protocol/wilink.yaml", "protocol/ftms.yaml")):
    src = " and ".join(f"`{s}`" for s in sources)
    return (
        f"> **GENERATED** from {src} by `tools/{generator}` — do not hand-edit; "
        "see [README.md](README.md) for how to regenerate.\n"
    )


def hexb(value, width=2):
    return f"0x{value:0{width}X}"


def hexbytes(data):
    return " ".join(f"{b:02X}" for b in data)


def crc(data):
    """Low byte of the sum of the bytes."""
    return sum(data) & 0xFF


def parse_hex(text):
    return bytes(int(t, 16) for t in text.split())


def command_frame(group, function, argument):
    body = bytes([group, function, argument])
    return bytes([0xF7]) + body + bytes([crc(body), 0xFD])


def status_crc_ok(frame, first_offset=1):
    return frame[-2] == crc(frame[first_offset:-2])


def md_escape(text):
    return str(text).replace("|", "\\|")


def table(headers, rows):
    out = ["| " + " | ".join(headers) + " |", "|" + "|".join("---" for _ in headers) + "|"]
    for row in rows:
        out.append("| " + " | ".join(md_escape(c) for c in row) + " |")
    return "\n".join(out) + "\n"


def prov(fact, parent=None):
    """Provenance of a fact, inheriting from its parent."""
    p = fact.get("provenance") or (parent or {}).get("provenance")
    return p


def evidence(fact, parent=None):
    return fact.get("evidence") or (parent or {}).get("evidence") or ""


def scale_text(field):
    """'x 0.1 km/h' for a scaled field, the plain unit otherwise."""
    unit = field.get("unit", "")
    if "scale" in field:
        return f"× {field['scale']:g} {unit}".strip()
    return unit


def converted(field, raw):
    if "scale" in field:
        scale = field["scale"]
        text = f"{scale:g}"
        decimals = len(text.split(".")[1]) if "." in text else 0
        return f"{raw * scale:.{decimals}f} {field.get('unit', '')}".strip()
    return f"{raw} {field.get('unit', '')}".strip()


def decode_field(field, data, enums):
    """Human-readable decoding of one field out of a frame/payload."""
    chunk = data[field["offset"]: field["offset"] + field["size"]]
    t = field.get("type", "raw")
    if "const" in field or t == "raw":
        return hexbytes(chunk)
    if t in ("u8", "u16le", "u32le"):
        raw = int.from_bytes(chunk, "little")
    elif t == "u24be":
        raw = int.from_bytes(chunk, "big")
    else:
        return hexbytes(chunk)
    if "enum" in field:
        for v in enums[field["enum"]]["values"]:
            if v["value"] == raw:
                return f"{raw} ({v['name']})"
        return f"{raw} (unknown)"
    if t == "u8" and field["name"] == "crc":
        return hexb(raw)
    return converted(field, raw)


def _validate(name, doc):
    def need(cond, msg):
        if not cond:
            raise SchemaError(msg)

    need(doc.get("schema_version") == 1, "schema_version must be 1")
    need(doc.get("protocol") == name, f"protocol must be {name!r}")

    def check_prov(node, where, parent=None):
        p = prov(node, parent)
        need(p in PROVENANCE, f"{where}: provenance {p!r} not one of {PROVENANCE}")

    enums = doc.get("enums", {})

    def walk_fields(fields, length, where, parent):
        pos = 0
        for f in fields:
            need(f["offset"] == pos, f"{where}: field {f['name']} at {f['offset']}, expected {pos}")
            pos += f.get("size", 1)
            if "enum" in f:
                need(f["enum"] in enums, f"{where}: unknown enum {f['enum']}")
            if "provenance" in f:
                check_prov(f, f"{where}.{f['name']}", parent)
        if length != "variable":
            need(pos == length, f"{where}: fields cover {pos} bytes, length is {length}")

    for c in doc.get("constraints", []):
        check_prov(c, f"constraint {c['id']}")
        need(c.get("evidence"), f"constraint {c['id']}: evidence required")
    for ch in doc.get("transport", {}).get("characteristics", []):
        check_prov(ch, f"characteristic {ch['uuid']}", doc["transport"])
    for ch in doc.get("characteristics", []):
        check_prov(ch, f"characteristic {ch['uuid']}")
    for en_name, en in enums.items():
        vals = [v["value"] for v in en["values"]]
        need(len(vals) == len(set(vals)), f"enum {en_name}: duplicate values")
        for v in en["values"]:
            check_prov(v, f"enum {en_name}.{v['name']}")

    if name == "wilink":
        for c in doc["commands"]:
            check_prov(c, f"command {c['id']}")
            need(c["group"] not in [g["group"] for g in doc["prohibited_groups"]],
                 f"command {c['id']} uses a prohibited group")
            for ex in c["examples"]:
                need(0 <= ex["argument"] <= 255, f"command {c['id']}: argument out of range")
                if "const" in c["argument"]:
                    need(ex["argument"] == c["argument"]["const"],
                         f"command {c['id']}: example differs from fixed argument")
        for g in doc["prohibited_groups"]:
            check_prov(g, f"prohibited group {g['group']:#x}")
        sf = doc["status_frame"]
        walk_fields(sf["fields"], sf["length"], "status_frame", sf)
        for ex in sf["examples"]:
            frame = parse_hex(ex["hex"])
            need(len(frame) == sf["length"], "status example: wrong length")
            need(status_crc_ok(frame, doc["frame_format"]["crc"]["first_offset"]),
                 "status example: CRC mismatch")
    else:
        for p in doc["payloads"]:
            check_prov(p, f"payload {p['id']}")
            walk_fields(p["fields"], p["length"], f"payload {p['id']}", p)
            if p["length"] != "variable":
                need(len(parse_hex(p["example"])) == p["length"], f"payload {p['id']}: example length")
        ids = {p["id"] for p in doc["payloads"]}
        for ch in doc["characteristics"]:
            if "payload" in ch:
                need(ch["payload"] in ids, f"characteristic {ch['uuid']}: unknown payload")


def cli(generate, description):
    """Standard CLI: --out DIR writes the generated files there."""
    ap = argparse.ArgumentParser(description=description)
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT, help="output directory (default docs/protocol)")
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    name, text = generate()
    (args.out / name).write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {args.out / name}", file=sys.stderr)
