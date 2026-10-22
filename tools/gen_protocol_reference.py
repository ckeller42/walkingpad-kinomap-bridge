#!/usr/bin/env python3
"""Generate docs/protocol/reference.md from the files in protocol/.

Reads protocol/wilink.yaml, protocol/ftms.yaml and protocol/domyos.yaml.

    python tools/gen_protocol_reference.py [--out DIR]
"""
import protocol_common as pc

GENERATOR = "gen_protocol_reference.py"


def _value(v):
    if isinstance(v, bool):
        return "yes" if v else "no"
    return str(v)


def _constraints(doc):
    rows = []
    for c in doc["constraints"]:
        val = _value(c["value"]) + (f" {c['unit']}" if "unit" in c else "")
        rows.append([f"`{c['key']}`", val, c["statement"], c["provenance"], c["evidence"]])
    return pc.table(["Key", "Value", "Statement", "Provenance", "Evidence"], rows)


def _enum_table(name, en):
    rows = []
    for v in en["values"]:
        rows.append([pc.hexb(v["value"]), v["name"], v.get("meaning", ""), v["provenance"], v["evidence"]])
    return f"`{name}` — {en['field']}.\n\n" + pc.table(["Value", "Name", "Meaning", "Provenance", "Evidence"], rows)


def _prohibition(doc):
    out = []
    for g in doc["prohibited_groups"]:
        out.append(
            f"> **DO NOT SEND group `{pc.hexb(g['group'])}` ({g['name']}) — {g['severity']}.**\n"
            f"> {g['rule']}\n"
            f"> Provenance: {g['provenance']} ({g['evidence']}).\n"
        )
    return "\n".join(out)


def _wilink(doc):
    out = []
    ff = doc["frame_format"]
    out.append("## WiLink — the treadmill side\n")
    out.append(f"{doc['summary']} {doc['role']}\n")

    out.append("### Transport\n")
    t = doc["transport"]
    rows = [[t["service"]["uuid"], "service", "", t["service"]["provenance"], t["service"]["evidence"]]]
    for ch in t["characteristics"]:
        rows.append([ch["uuid"], ch["name"], ", ".join(ch["properties"]), ch["provenance"], ch["evidence"]])
    out.append(pc.table(["UUID", "Role", "Properties", "Provenance", "Evidence"], rows))
    out.append("")
    for ch in t["characteristics"]:
        out.append(f"- `{ch['uuid']}` ({ch['name']}): {ch['notes']}")
    for sv in t["other_services"]:
        out.append(f"- `{sv['uuid']}` ({sv['name']}), {sv['provenance']}: {sv['notes']}")
    out.append("")

    out.append("### Framing\n")
    out.append(
        "Command frames are six bytes: `F7 <group> <function> <argument> <crc> FD`. "
        "Byte-level tables are in [frame-layouts.md](frame-layouts.md).\n"
    )
    out.append(f"**CRC** ({ff['crc']['provenance']}): {ff['crc']['rule']}\n")

    out.append("### Commands\n")
    out.append("Example frames carry a CRC computed by the generator from the group, function and argument.\n")
    rows = []
    for c in doc["commands"]:
        frames = "<br>".join(
            f"`{pc.hexbytes(pc.command_frame(c['group'], c['function'], ex['argument']))}` {ex['label']}"
            for ex in c["examples"]
        )
        arg = c["argument"]
        if "const" in arg:
            argtxt = f"fixed `{pc.hexb(arg['const'])}`"
        else:
            argtxt = f"`{arg['name']}`" + (f" ({pc.scale_text(arg)})" if "unit" in arg else "")
            if "enum" in arg:
                argtxt += f", enum `{arg['enum']}`"
        rows.append([c["name"], pc.hexb(c["group"]), pc.hexb(c["function"]), argtxt, frames,
                     c["provenance"], c["evidence"]])
    out.append(pc.table(["Command", "Group", "Function", "Argument", "Example frame", "Provenance", "Evidence"], rows))
    out.append("")
    for c in doc["commands"]:
        out.append(f"- **{c['name']}**: {c['summary']} {c['notes']}")
    out.append("")

    out.append("### Enumerations\n")
    for name, en in doc["enums"].items():
        out.append(_enum_table(name, en))
    out.append("")

    out.append("### Status frame\n")
    sf = doc["status_frame"]
    out.append(
        f"{sf['length']} bytes, received on `FE01` in reply to Request status. "
        f"Provenance: {sf['provenance']} ({sf['evidence']}). "
        "Field offsets are in [frame-layouts.md](frame-layouts.md).\n"
    )

    out.append("### Behavioural constraints\n")
    out.append(_constraints(doc))
    return "\n".join(out)


def _ftms(doc):
    out = []
    out.append("## FTMS — the Kinomap side\n")
    out.append(f"{doc['summary']} {doc['role']}\n")
    adv = doc["advertisement"]
    out.append(
        f"Advertised as `{adv['name']}` with service `{adv['service']}` "
        f"({adv['provenance']}: {adv['evidence']}).\n"
    )

    out.append("### Characteristics\n")
    rows = []
    for ch in doc["characteristics"]:
        extra = ch.get("value")
        rows.append([ch["uuid"], ch["name"], ", ".join(ch["properties"]),
                     f"`{extra}`" if extra else "", ch["provenance"], ch["evidence"]])
    out.append(pc.table(["UUID", "Name", "Properties", "Value", "Provenance", "Evidence"], rows))
    out.append("")
    for ch in doc["characteristics"]:
        if ch.get("notes"):
            out.append(f"- `{ch['uuid']}`: {ch['notes']}")
    out.append("")

    out.append("### Deliberately absent\n")
    rows = [[a["uuid"], a["name"], a["reason"], a["provenance"], a["evidence"]] for a in doc["absent_characteristics"]]
    out.append(pc.table(["UUID", "Name", "Reason", "Provenance", "Evidence"], rows))

    out.append("### Control point opcodes\n")
    for name in ("control_opcode", "status_opcode"):
        en = doc["enums"][name]
        rows = [[pc.hexb(v["value"]), v["name"], v.get("direction", ""), v["meaning"], v["provenance"], v["evidence"]]
                for v in en["values"]]
        out.append(f"`{name}` — {en['field']}.\n")
        out.append(pc.table(["Value", "Name", "Direction", "Meaning", "Provenance", "Evidence"], rows))
        out.append("")
    out.append("Responses are `80 <request-opcode> 01`. Byte-level tables are in [frame-layouts.md](frame-layouts.md).\n")

    out.append("### Behavioural constraints\n")
    out.append(_constraints(doc))
    return "\n".join(out)


def _domyos(doc):
    """The Domyos-mode profile: nothing measured, so provenance carries the weight."""
    out = []
    out.append("## Domyos mode — the second Kinomap side\n")
    out.append(f"{doc['summary']} {doc['role']}\n")
    out.append(
        "> **NOT MEASURED.** Every fact in this section comes from reading two sources and "
        "none from this hardware. `disputed` means the two disagree; both readings are kept and "
        "neither is implemented without a measurement. See "
        "[`protocol/domyos.yaml`](../../protocol/domyos.yaml) for the sources and the naming warnings.\n"
    )
    svc = doc["service"]
    out.append(
        f"Service `{svc['uuid']}` ({svc['name']}), {svc['provenance']}: {svc['evidence']}.\n"
    )
    out.append(f"{svc['notes']}\n")
    adv = doc["advertisement"]
    out.append(f"Advertised service `{adv['service']}`, {adv['provenance']}. {adv['notes']}\n")

    out.append("### Characteristics\n")
    rows = []
    for ch in doc["characteristics"]:
        rows.append([ch["uuid"], ch["name"], ", ".join(ch["properties"]),
                     f"`{ch['descriptor']}`" if "descriptor" in ch else "",
                     ch["provenance"], ch["evidence"]])
    out.append(pc.table(["UUID", "Name", "Properties", "0x2906 descriptor", "Provenance", "Evidence"], rows))
    out.append("")
    for ch in doc["characteristics"]:
        out.append(f"- `{ch['uuid']}` ({ch['name']}): {ch['notes']}")
    out.append("")

    out.append("### Connection chain\n")
    out.append("Ordered, and every step gates the next. A peripheral that skips one shows no data at all.\n")
    rows = [[str(s["step"]), s["action"], s["detail"], s["provenance"], s["evidence"]]
            for s in doc["handshake"]]
    out.append(pc.table(["Step", "Action", "Detail", "Provenance", "Evidence"], rows))

    out.append("### Enumerations\n")
    for name, en in doc["enums"].items():
        out.append(_enum_table(name, en))
    out.append("")

    out.append("### Behavioural constraints\n")
    out.append(_constraints(doc))

    out.append("### Open questions\n")
    out.append("Neither source answers these. A later task must not read the silence as a settled value.\n")
    rows = [[q["id"], q["question"], q["status"]] for q in doc["open_questions"]]
    out.append(pc.table(["Id", "Question", "Status"], rows))
    return "\n".join(out)


def generate():
    wilink = pc.load("wilink")
    ftms = pc.load("ftms")
    domyos = pc.load("domyos")
    legend = pc.table(["Provenance", "Meaning"], [[k, v] for k, v in pc.PROVENANCE_LEGEND.items()])
    parts = [
        pc.banner(GENERATOR),
        "# Protocol reference\n",
        "Every fact below carries a provenance. Where this and `docs/test-log.md` differ, the test log wins.\n",
        _prohibition(wilink),
        legend,
        _wilink(wilink),
        _ftms(ftms),
        _domyos(domyos),
    ]
    text = "\n".join(parts)
    while "\n\n\n" in text:
        text = text.replace("\n\n\n", "\n\n")
    return "reference.md", text.rstrip("\n") + "\n"


if __name__ == "__main__":
    pc.cli(generate, __doc__)
