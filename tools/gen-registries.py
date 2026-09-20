#!/usr/bin/env python3
"""ephsrv/registries.json, generated from EPHEMERIS_PLUGINS_PLAN.md Appendix A.

The protocol's numbers live in three places -- the specification's prose, the
C++ codec (ephsrv/ephproto.h) and whatever a second implementation writes --
and nothing compared them. This makes the prose the source: it parses
Appendix A and writes the registries as JSON, `make check` regenerates and
diffs it like every other generated table, and ephsrv/ephproto_test.cpp
requires the codec's own constants to agree with the file. Ephemeris
Prometheia vendors it with the fixture set's checksum, so it is part of the
pinned interface: nothing Astrolog-specific goes in, entries are sorted, and
values are appended, never reused (3.6).

    tools/gen-registries.py                      # write ephsrv/registries.json
    tools/gen-registries.py --stdout             # print it (make check diffs this)

A section whose shape in the document changes stops this loudly rather than
writing a short file: every registry declares how many entries it must yield
and every value must parse.
"""

import json
import os
import re
import sys

PLAN = "EPHEMERIS_PLUGINS_PLAN.md"
OUT = os.path.join("ephsrv", "registries.json")

DASHES = "–—-"          # en dash, em dash, hyphen: the document uses all three


def die(msg):
    sys.stderr.write("gen-registries: %s\n" % msg)
    sys.exit(1)


# ---- finding a subsection -------------------------------------------------

# Every label subsection() is asked for. Appendix A's own headings are
# checked against this at the end of registries(), so a registry ADDED to
# the appendix and not to this generator cannot ship unpinned.
SEEN = set()


def subsection(text, label):
    """The lines of '**A.n ...**' up to the next such heading or '## '."""
    SEEN.add(label)
    lines = text.split("\n")
    start = None
    for i, line in enumerate(lines):
        if line.startswith("**%s " % label) or line.startswith("**%s." % label):
            start = i
            break
    if start is None:
        die("%s not found in %s" % (label, PLAN))
    out = []
    for line in lines[start:]:
        if out and (re.match(r"^\*\*A\.\d+[ .]", line) or line.startswith("## ")):
            break
        out.append(line)
    while out and not out[-1].strip():
        out.pop()
    return out


def bullets(lines):
    """The '- ' items of a block, each with its continuation lines joined."""
    out = []
    for line in lines:
        if line.startswith("- "):
            out.append(line[2:].strip())
        elif out and line.startswith("  ") and line.strip():
            out[-1] += " " + line.strip()
    return out


def table_rows(lines):
    """The data rows of the one markdown table in a block, as cell lists."""
    rows = []
    for line in lines:
        if not line.startswith("|"):
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if all(re.fullmatch(r":?-{2,}:?", c) for c in cells):
            continue
        rows.append(cells)
    if not rows:
        die("no table rows found")
    return rows[1:]          # drop the header


def inline_items(lines, label):
    """'**A.n Title:** 0 a, 1 b, 2 c.' -- the enumeration after the colon."""
    text = " ".join(l.strip() for l in lines)
    at = text.find(":")
    if at < 0:
        die("%s: no ':' before its enumeration" % label)
    # The colon may sit inside the bold heading ("**A.7 Correction bits:**").
    text = text[at + 1:].lstrip("*").strip()
    # The enumeration is the first sentence; what follows it is prose.
    cut = re.search(r"\.(\s|$)", text)
    if cut:
        text = text[:cut.start()]
    return [s.strip() for s in text.split(",") if s.strip()]


# ---- parsing one item -----------------------------------------------------

def number(tok):
    tok = tok.strip()
    try:
        return int(tok, 16) if tok.lower().startswith("0x") else int(tok, 10)
    except ValueError:
        die("not a number: %r" % tok)


def value_item(item, prefix=""):
    """'1 HELLO', '16-31 event searches*', 'bit 0 approximated (...)'."""
    s = item.strip()
    if prefix:
        if not s.startswith(prefix):
            die("expected %r to start with %r" % (s, prefix))
        s = s[len(prefix):].lstrip().lstrip(":").lstrip()
    m = re.match(r"^([0-9A-Fa-fxX]+)\s*[%s]\s*([0-9A-Fa-fxX]+)\s+(.*)$" % DASHES, s)
    if m:
        name = m.group(3).strip().replace("`", "")
        e = {"first": number(m.group(1)), "last": number(m.group(2)), "name": name.rstrip("*")}
        if name.endswith("*"):
            e["reserved"] = True
        return e
    m = re.match(r"^([0-9A-Fa-fxX]+)\s*:?\s+(.*)$", s)
    if not m:
        die("not '<value> <name>': %r" % item)
    name = m.group(2).strip().replace("`", "")
    e = {"value": number(m.group(1)), "name": name.rstrip("*")}
    if name.endswith("*"):
        e["reserved"] = True
    return e


def tokens_of(text):
    """Every `backticked` token of a line, in order."""
    return re.findall(r"`([^`]+)`", text)


# ---- the registries -------------------------------------------------------

def registries(text):
    reg = {}

    def put(key, label, title, kind, entries, count=None):
        if count is not None and len(entries) != count:
            die("%s: parsed %d entries, expected %d" % (label, len(entries), count))
        if not entries:
            die("%s: no entries parsed" % label)
        reg[key] = {"appendix": label, "title": title, "kind": kind, "entries": entries}

    # A.1 message types: values and two reserved ranges.
    put("message_types", "A.1", "Message types", "value",
        [value_item(b) for b in bullets(subsection(text, "A.1"))])

    # A.2 caps bits: bit indices in HELLO clientCaps and WELCOME caps.
    caps = []
    for b in bullets(subsection(text, "A.2")):
        e = value_item(b)
        caps.append({"bit": e["value"], "mask": 1 << e["value"], "name": e["name"]})
    put("caps_bits", "A.2", "caps bits", "bit", caps)

    # A.3 WELCOME capability TLVs: a table of tag and payload.
    tlv = []
    for row in table_rows(subsection(text, "A.3")):
        tag = number(row[0])
        tlv.append({"tag": tag, "critical": bool(tag & 0x8000), "payload": row[1]})
    put("welcome_capability_tlvs", "A.3", "WELCOME capability TLVs", "tlv", tlv)

    # A.4 REQUEST TLVs: bullets, each '<tag> <description>'.
    req = []
    for b in bullets(subsection(text, "A.4")):
        e = value_item(b)
        req.append({"tag": e["value"], "critical": bool(e["value"] & 0x8000),
                    "payload": e["name"]})
    put("request_tlvs", "A.4", "REQUEST TLVs", "tlv", req)

    put("observers", "A.5", "Observers", "value",
        [value_item(b) for b in bullets(subsection(text, "A.5"))], count=5)

    # A.6 planes, forms and frames: three enumerations, one bullet each.
    pff = subsection(text, "A.6")
    for b in bullets(pff):
        head, rest = b.split(":", 1)
        key = {"Planes": "planes", "Forms": "forms", "Frames": "frames"}.get(head.strip())
        if key is None:
            die("A.6: unexpected bullet %r" % head)
        items = [s.strip() for s in rest.strip().rstrip(".").split(",")]
        put(key, "A.6", head.strip(), "value", [value_item(i) for i in items])
    for key in ("planes", "forms", "frames"):
        if key not in reg:
            die("A.6: %s missing" % key)

    put("correction_bits", "A.7", "Correction bits", "mask",
        [value_item(i) for i in inline_items(subsection(text, "A.7"), "A.7")], count=3)
    put("sidereal_planes", "A.8", "Sidereal planes", "value",
        [value_item(i) for i in inline_items(subsection(text, "A.8"), "A.8")], count=3)
    put("time_scales", "A.9", "Time scales", "value",
        [value_item(i) for i in inline_items(subsection(text, "A.9"), "A.9")], count=3)

    cols = []
    for b in bullets(subsection(text, "A.10")):
        e = value_item(b, prefix="bit")
        cols.append({"bit": e["value"], "mask": 1 << e["value"], "name": e["name"]})
    put("extra_column_bits", "A.10", "Extra column bits", "bit", cols)

    # A.11 zodiac tokens: every backticked token of the bullets, in order.
    zod = []
    for b in bullets(subsection(text, "A.11")):
        for t in tokens_of(b):
            zod.append({"index": len(zod), "token": t})
    put("zodiac_tokens", "A.11", "Zodiac tokens", "token", zod)

    put("object_kinds", "A.12", "Object kinds", "value",
        [value_item(b) for b in bullets(subsection(text, "A.12"))], count=6)
    put("orbit_points", "A.13", "Orbit points", "value",
        [value_item(i) for i in inline_items(subsection(text, "A.13"), "A.13")], count=4)
    put("orbit_methods", "A.14", "Orbit methods", "value",
        [value_item(b) for b in bullets(subsection(text, "A.14"))], count=5)

    # A.15 named hypotheticals: a table whose first cell holds the tokens of
    # one seorbel.txt entry, in that file's order.
    hyp = []
    for row in table_rows(subsection(text, "A.15")):
        for t in tokens_of(row[0]):
            hyp.append({"index": len(hyp), "token": t, "seorbel_entry": row[1]})
    put("hypothetical_tokens", "A.15", "Named hypotheticals", "token", hyp)

    put("element_equinoxes", "A.16", "Element equinoxes", "value",
        [value_item(i) for i in inline_items(subsection(text, "A.16"), "A.16")], count=5)
    put("element_centres", "A.21", "Element centres", "value",
        [value_item(i) for i in inline_items(subsection(text, "A.21"), "A.21")], count=2)
    put("object_error_codes", "A.17", "Per-object error codes", "value",
        [value_item(b) for b in bullets(subsection(text, "A.17"))])

    meta = []
    for b in bullets(subsection(text, "A.18")):
        e = value_item(b, prefix="bit")
        meta.append({"bit": e["value"], "mask": 1 << e["value"], "name": e["name"]})
    put("meta_flags", "A.18", "META flags", "bit", meta)

    put("error_codes", "A.19", "ERROR codes", "value",
        [value_item(b) for b in bullets(subsection(text, "A.19"))])

    prec = []
    for b in bullets(subsection(text, "A.20")):
        t = tokens_of(b)
        if not t:
            die("A.20: no token in %r" % b)
        note = b.split("`%s`" % t[0], 1)[1].strip().lstrip(DASHES).strip()
        prec.append({"index": len(prec), "token": t[0], "note": note})
    put("precession_model_tokens", "A.20", "Precession model tokens", "token", prec)
    return reg


def build():
    with open(PLAN, encoding="utf-8") as f:
        text = f.read()
    reg = registries(text)
    doc = {
        "$comment": "The protocol version 4 registries, generated from "
                    "EPHEMERIS_PLUGINS_PLAN.md Appendix A by tools/gen-registries.py. "
                    "Do not edit by hand: edit the appendix and regenerate. Values are "
                    "appended and never reused (3.6), so a consumer may pin this file.",
        "protocol": 4,
        "source": PLAN + " Appendix A",
        "registries": dict(sorted(reg.items())),
    }
    # APPENDIX A'S OWN HEADINGS ARE THE LIST. subsection() dies when a
    # label it is asked for is absent, so a registry REMOVED from the
    # appendix already fails loudly. The other direction was invisible:
    # the labels above are written out by hand, so a registry ADDED to
    # the appendix is simply never asked for, never lands in
    # registries.json, and the make check diff passes because the
    # committed file does not have it either. It would ship unpinned, and
    # a second implementation vendoring this file would never see it.
    #
    # Reported by the Prometheia project, who had the same blind
    # direction in the checker that pins their vendored copy of this
    # file: its loop ran over the JSON's own keys, so a registry the file
    # stopped carrying dropped out of the loop and out of the count in
    # one move, and it still printed "22 of 23 registries checked".
    #
    # The second comparison is not decoration. Scanning for headings is a
    # pattern, and a pattern that stops matching finds FEWER -- which
    # would satisfy the first check trivially. Requiring at least as many
    # headings as labels actually consumed makes that direction fail
    # instead: the two counts cannot both shrink.
    heads = set(re.findall(r"^\*\*(A\.\d+)[ .]", text, re.M))
    missing = sorted(heads - SEEN, key=lambda s: int(s.split(".")[1]))
    if missing:
        die("Appendix A carries %s, which this generator never asks for: "
            "add %s to registries() or the registry ships unpinned"
            % (", ".join(missing), ", ".join(missing)))
    if len(heads) < len(SEEN):
        die("found %d '**A.n' headings but consumed %d labels -- the "
            "heading scan is matching fewer than it should, which would "
            "hide a missing registry" % (len(heads), len(SEEN)))
    return json.dumps(doc, indent=2, sort_keys=False, ensure_ascii=True) + "\n"


def main():
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    os.chdir(root)
    text = build()
    if "--stdout" in sys.argv[1:]:
        sys.stdout.write(text)
        return 0
    with open(OUT, "w", newline="\n", encoding="ascii") as f:
        f.write(text)
    sys.stderr.write("gen-registries: wrote %s (%d registries)\n"
                     % (OUT, len(json.loads(text)["registries"])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
