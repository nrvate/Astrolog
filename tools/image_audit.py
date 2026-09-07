#!/usr/bin/env python3
"""Check that every image Astrolog writes is a valid file of its format.

    python3 tools/image_audit.py [./astrolog]

Eleven output writers reach a file: two BMP depths, PNG, three XBM
variants, ASCII, encapsulated and complete PostScript, SVG, and the
Daedalus wireframe. tools/graphics-matrix.sh renders through all of them
and takes a checksum of each, which is a DIFFERENTIAL: it says the bytes
did not change, and it says nothing at all about whether they were ever
right. A writer that has always emitted a wrong Adler-32, a short row, or
a header length that disagrees with the data behind it diffs to zero
every time.

This asks the other question, against the format specifications rather
than against a previous build:

  * BMP -- "BM", the file size field equals the real file size, the pixel
    array starts where the header says, and the data is exactly
    height * (4-byte-aligned row) bytes for the declared bit depth.
  * PNG -- signature, every chunk's CRC-32, and IDAT that inflates (zlib
    checks the Adler-32 doing it) to exactly height * (width*3 + 1),
    which is one filter byte plus three channels per pixel per row.
  * XBM -- the two #defines, and an element count of
    height * ceil(width / bits-per-element) for the 8-bit and 16-bit
    forms.
  * ASCII -- one line per row, each exactly width characters.
  * PostScript and SVG -- the structural markers a reader looks for, and
    for SVG that it parses as XML.

And one check no single format can make: **every format must agree on
the size of the picture**. That is deliberately not a hard coded number.
Astrolog squares the chart to the smaller of the two -Xw arguments and
adds a sidebar to the width, so writing the expected size down here would
be transcribing a rule that lives somewhere else; requiring the writers
to agree tests the same thing and stays true if the rule changes.
"""

import os
import re
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zlib

# One render each. The name is what the report calls it; the switches go
# on the command line after the shared chart.
WRITERS = [
    ("bmp24",  ["-Xb", "-Xbw"]),
    ("bmp4",   ["-Xb", "-Xbb"]),
    ("png",    ["-Xb", "-Xbp"]),
    ("xbm-n",  ["-Xb", "-Xbn"]),
    ("xbm-c",  ["-Xb", "-Xbc"]),
    ("xbm-v",  ["-Xb", "-Xbv"]),
    ("ascii",  ["-Xb", "-Xba"]),
    ("eps",    ["-Xp"]),
    ("ps",     ["-Xp0"]),
    ("svg",    ["-XV"]),
    ("wire",   ["-X3"]),
]

CHART = ["-qa", "6", "15", "1990", "12:34", "0", "122W19", "47N36"]


def render(binary, ephem, args, path):
    cmd = [binary] + CHART + ["-Yi1", ephem] + args + ["-Xo", path, "-X"]
    p = subprocess.run(cmd, stdin=subprocess.DEVNULL,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if p.returncode != 0:
        return "exited %d: %s" % (p.returncode,
                                  p.stdout.decode("utf-8", "replace")[:200])
    if not os.path.exists(path) or os.path.getsize(path) == 0:
        return "wrote no file"
    return None


def check_bmp(d):
    """Returns (width, height, error)."""
    if d[:2] != b"BM":
        return 0, 0, "no \"BM\" magic"
    cbFile, _, _, ibData = struct.unpack("<IHHI", d[2:14])
    cbHead, w, h, planes, bits = struct.unpack("<IiiHH", d[14:30])
    if cbFile != len(d):
        return w, h, "header says %d bytes, file is %d" % (cbFile, len(d))
    if cbHead != 40 or planes != 1:
        return w, h, "unexpected header: size %d, %d planes" % (cbHead, planes)
    if ibData > len(d):
        return w, h, "pixel data starts at %d, past the end" % ibData
    cbRow = ((w * bits + 31) // 32) * 4
    if len(d) - ibData != cbRow * h:
        return w, h, ("%d bytes of pixels for %dx%d at %d bpp, want %d "
                      "(rows pad to 4 bytes)"
                      % (len(d) - ibData, w, h, bits, cbRow * h))
    return w, h, None


def check_png(d):
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        return 0, 0, "no PNG signature"
    i, idat, w, h = 8, b"", 0, 0
    while i + 8 <= len(d):
        ln, typ = struct.unpack(">I4s", d[i:i + 8])
        body = d[i + 8:i + 8 + ln]
        if i + 12 + ln > len(d):
            return w, h, "chunk %s runs past the end" % typ.decode("latin-1")
        crc, = struct.unpack(">I", d[i + 8 + ln:i + 12 + ln])
        if zlib.crc32(typ + body) & 0xffffffff != crc:
            return w, h, "chunk %s has a bad CRC" % typ.decode("latin-1")
        if typ == b"IHDR":
            w, h = struct.unpack(">II", body[:8])
        elif typ == b"IDAT":
            idat += body
        i += 12 + ln
        if typ == b"IEND":
            break
    try:
        raw = zlib.decompress(idat)      # this validates the Adler-32 too
    except zlib.error as e:
        return w, h, "IDAT does not inflate: %s" % e
    want = h * (w * 3 + 1)
    if len(raw) != want:
        return w, h, "IDAT inflates to %d bytes, want %d" % (len(raw), want)
    return w, h, None


def check_xbm(d, cbitElem):
    t = d.decode("latin-1")
    # The array name is derived from the output FILENAME, so it can hold
    # anything a filename can, hyphens included -- "\w*" here reported a
    # perfectly good file as malformed.
    mw = re.search(r"#define\s+\S*_width\s+(\d+)", t)
    mh = re.search(r"#define\s+\S*_height\s+(\d+)", t)
    if mw is None or mh is None:
        return 0, 0, "missing the _width/_height defines"
    w, h = int(mw.group(1)), int(mh.group(1))
    body = t[t.index("{") + 1:t.rindex("}")] if "{" in t and "}" in t else ""
    if not body:
        return w, h, "no { ... } array"
    cElem = len(re.findall(r"0x[0-9A-Fa-f]+", body))
    want = h * ((w + cbitElem - 1) // cbitElem)
    if cElem != want:
        return w, h, "%d array elements for %dx%d, want %d" % (
            cElem, w, h, want)
    return w, h, None


def check_ascii(d):
    lines = d.decode("latin-1").rstrip("\n").split("\n")
    h = len(lines)
    w = len(lines[0]) if lines else 0
    bad = [i for i, ln in enumerate(lines) if len(ln) != w]
    if bad:
        return w, h, "%d row(s) are not %d characters, first is row %d" % (
            len(bad), w, bad[0])
    return w, h, None


def check_ps(d, fEps):
    t = d.decode("latin-1")
    if not t.startswith("%!PS"):
        return 0, 0, "does not start with %!PS"
    if fEps and "%%BoundingBox:" not in t:
        return 0, 0, "encapsulated PostScript with no %%BoundingBox"
    if "showpage" not in t and not fEps:
        return 0, 0, "complete PostScript with no showpage"
    m = re.search(r"%%BoundingBox:\s*(-?\d+)\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)", t)
    if m is None:
        return 0, 0, None
    return (int(m.group(3)) - int(m.group(1)),
            int(m.group(4)) - int(m.group(2)), None)


# The SVG carries no width/height attributes: its canvas is a viewBox at
# a fixed multiple of the pixel size (8x at the default scale). Returning
# those numbers would put it permanently out of step with every other
# writer, so this returns the viewBox and the caller compares the SHAPE.

def check_svg(d):
    try:
        root = ET.fromstring(d.decode("utf-8", "replace"))
    except ET.ParseError as e:
        return 0, 0, "does not parse as XML: %s" % e
    if not root.tag.endswith("svg"):
        return 0, 0, "root element is <%s>, not <svg>" % root.tag
    m = re.match(r"\s*0\s+0\s+(\d+)\s+(\d+)\s*$", root.get("viewBox", ""))
    if m is None:
        return 0, 0, "no \"0 0 <w> <h>\" viewBox on the root element"
    return int(m.group(1)), int(m.group(2)), None


def check_wire(d):
    t = d.decode("latin-1")
    if not t.strip():
        return 0, 0, "empty"
    return 0, 0, None


# Every assertion above is "this file is well formed", and a checker that
# cannot recognise a malformed file passes all eleven while proving
# nothing. So corrupt each format in the one way its checker is supposed
# to notice, and require the complaint. Run by --selftest, on copies of
# real renders in a temp directory -- nothing in the tree is touched, and
# nothing has to be remembered about having done it once.

def selftest(binary, ephem):
    def bmp_bad(d):                       # file size field no longer true
        return d[:2] + struct.pack("<I", len(d) + 4) + d[6:]

    def png_bad(d):                       # a byte inside a chunk payload
        i = d.index(b"IDAT") + 8
        return d[:i] + bytes([d[i] ^ 0xff]) + d[i + 1:]

    def xbm_bad(d):                       # one array element short
        i = d.rindex(b"0x")
        return d[:i - 1] + d[i + 4:]

    def ascii_bad(d):                     # one row a character shorter
        i = d.index(b"\n")
        return d[:i - 1] + d[i:]

    def svg_bad(d):                       # no longer XML
        return d.replace(b"<svg", b"<svg <", 1)

    CASES = [("bmp24", bmp_bad), ("png", png_bad), ("xbm-n", xbm_bad),
             ("ascii", ascii_bad), ("svg", svg_bad)]
    args = dict(WRITERS)
    cMissed = 0
    with tempfile.TemporaryDirectory(prefix="astrolog-image-self-") as d:
        for name, corrupt in CASES:
            path = os.path.join(d, name + ".out")
            err = render(binary, ephem, args[name], path)
            if err is not None:
                print("  %-8s could not render to corrupt: %s" % (name, err))
                cMissed += 1
                continue
            data = corrupt(open(path, "rb").read())
            if name.startswith("bmp"):
                _, _, err = check_bmp(data)
            elif name == "png":
                _, _, err = check_png(data)
            elif name.startswith("xbm"):
                _, _, err = check_xbm(data, 8)
            elif name == "ascii":
                _, _, err = check_ascii(data)
            else:
                _, _, err = check_svg(data)
            if err is None:
                print("  %-8s corruption NOT detected" % name)
                cMissed += 1
            else:
                print("  %-8s ok: %s" % (name, err))
    if cMissed:
        print("\n%d checker(s) accepted a file they should have refused."
              % cMissed)
        return 1
    print("image audit self-test: %d format(s), every corruption caught"
          % len(CASES))
    return 0


def main():
    if "--selftest" in sys.argv:
        sys.argv.remove("--selftest")
        binary = sys.argv[1] if len(sys.argv) > 1 else "./astrolog"
        return selftest(binary, os.path.join(
            os.path.dirname(os.path.abspath(binary)), "ephem"))
    binary = sys.argv[1] if len(sys.argv) > 1 else "./astrolog"
    if not os.access(binary, os.X_OK):
        print("%s is not executable -- build it first: make" % binary)
        return 1
    ephem = os.path.join(os.path.dirname(os.path.abspath(binary)), "ephem")

    bad, sizes = [], {}
    with tempfile.TemporaryDirectory(prefix="astrolog-image-audit-") as d:
        for name, args in WRITERS:
            path = os.path.join(d, name + ".out")
            err = render(binary, ephem, args, path)
            if err is not None:
                bad.append((name, err))
                continue
            data = open(path, "rb").read()
            if name.startswith("bmp"):
                w, h, err = check_bmp(data)
            elif name == "png":
                w, h, err = check_png(data)
            elif name == "xbm-v":
                w, h, err = check_xbm(data, 16)
            elif name.startswith("xbm"):
                w, h, err = check_xbm(data, 8)
            elif name == "ascii":
                w, h, err = check_ascii(data)
            elif name in ("eps", "ps"):
                w, h, err = check_ps(data, name == "eps")
            elif name == "svg":
                w, h, err = check_svg(data)
            else:
                w, h, err = check_wire(data)
            if err is not None:
                bad.append((name, err))
            elif w > 0 and h > 0:
                sizes[name] = (w, h)

    # Every writer that declares the picture's size in pixels must
    # declare the same one. Two are excluded and each for a reason, not
    # because it was awkward: "ps" is a COMPLETE PostScript file, so its
    # %%BoundingBox is the page rather than the image, and "svg" carries
    # a viewBox at a fixed multiple of the pixel size. The SVG is still
    # checked, one line down, on the shape that multiple cannot change.
    pixels = {k: v for k, v in sizes.items() if k not in ("ps", "svg")}
    if len(set(pixels.values())) > 1:
        for name in sorted(pixels):
            print("  %-8s %dx%d" % (name, pixels[name][0], pixels[name][1]))
        bad.append(("all", "the writers disagree about how big the picture is"))
    elif pixels and "svg" in sizes:
        w, h = list(pixels.values())[0]
        wSvg, hSvg = sizes["svg"]
        if wSvg * h != hSvg * w:
            bad.append(("svg", "viewBox is %dx%d, a different shape from the "
                        "%dx%d every other writer agrees on"
                        % (wSvg, hSvg, w, h)))

    if bad:
        for name, err in bad:
            print("  %-8s %s" % (name, err))
        print("\nEach of these is checked against its format, not against a")
        print("previous build, so a failure here is a malformed file rather")
        print("than a changed one. graphics-matrix.sh cannot see either.")
        return 1

    w, h = list(pixels.values())[0]
    print("image formats clean: %d writers, all structurally valid, "
          "all agreeing on %dx%d" % (len(WRITERS), w, h))
    return 0


sys.exit(main())
