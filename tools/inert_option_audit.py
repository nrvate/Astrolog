#!/usr/bin/env python3
"""Every option in the graphics matrix must change at least one render.

    python3 tools/inert_option_audit.py [matrix-output-file]

Without an argument this runs tools/graphics-matrix.sh itself (~10s).

WHY THIS EXISTS. graphics-matrix.sh carried "-XE 1 20" from the day it was
written, and it rendered BYTE-IDENTICALLY to no -XE at all -- the asteroid
loop stops at the first body it cannot compute, and asteroid 9 has no
ephemeris file, so the range drew nothing. It sat there looking like
coverage. An inert entry still diffs to zero against another build, which
is exactly what a passing differential looks like, so nothing could
notice.

The check: for each option, compare its checksum against the BARE render
of the same base mode. An option identical to bare under every base it
appears in has changed nothing, and is either a bug like -XE 1 20 or
needs a line in ALLOWED saying why not.

ALLOWED is deliberately annotated. "It was already failing" is not a
reason; each entry below says what makes that switch a no-op for a single
static bitmap, and any of them could be wrong -- the point is that the
claim is written down where it can be checked.
"""
import re, subprocess, sys, collections

# Option -> why it cannot move a single static bitmap render.
ALLOWED = {
    "-Xb":     "the bitmap writer itself, which the harness already applies",
    "-Xbw":    "same writer, Windows-bitmap variant",
    "-XS 100": "100% is the default text size; -XS 400 in the same family is live",
    "-Xv 1":   "the default fill style; -Xv 0 and 2..7 in the same family are live",
    "-XQ":     "only matters when the bitmap is not square; the harness uses the default size",
    "-XQ0":    "same, the off form",
    "-Xj":     "draws trails BETWEEN chart updates -- animation only, not one render",
    "-XN":     "animates map time instead of rotating -- animation only",
    "-Xk 5":   "sets the interactive scribble pen colour; nothing scribbles here",
    "-Xkv 9":  "same scribble family",
    "-v":      "a text chart selector; a graphics render ignores it",
    "-d":      "a text chart selector; a graphics render ignores it",
    "-5":      "chart-list behaviour, not drawing",
    "-k0":     "Ansi text colouring, not drawing",
}

BASES = ("-XG", "-XW", "-g")


def pairs(text):
    out, cur = [], None
    for ln in text.splitlines():
        if ln.startswith("== "):
            cur = ln[3:]
        elif cur is not None:
            m = re.match(r"\s+([0-9a-f]{32})\s", ln)
            if m:
                out.append((cur, m.group(1)))
                cur = None
    return out


def main():
    if len(sys.argv) > 1:
        text = open(sys.argv[1]).read()
    else:
        text = subprocess.run(["tools/graphics-matrix.sh", "./astrolog"],
                              capture_output=True, text=True).stdout
    ps = pairs(text)
    if not ps:
        print("no renders parsed -- is ./astrolog built?")
        return 2

    base = {}
    for args, ck in ps:
        t = args.strip()
        if (t == "" or t in BASES) and t not in base:
            base[t] = ck
    if "" not in base:
        print("no bare render in the matrix output; cannot compare")
        return 2

    opts = collections.defaultdict(list)
    for args, ck in ps:
        t = args.strip()
        if t == "" or t in BASES:
            continue
        b = ""
        for cand in BASES:
            if t.startswith(cand + " "):
                b, t = cand, t[len(cand) + 1:]
                break
        opts[t].append((b, ck))

    inert = [o for o, v in opts.items()
             if all(base.get(b) == ck for b, ck in v)]
    unexplained = sorted(o for o in inert if o not in ALLOWED)
    stale = sorted(o for o in ALLOWED if o not in inert)

    print(f"graphics matrix: {len(ps)} renders, {len(opts)} distinct options, "
          f"{len(inert)} inert")
    if stale:
        print("ALLOWLIST IS STALE -- these are listed as inert but now move a render:")
        for o in stale:
            print(f"  {o}   ({ALLOWED[o]})")
        print("Remove them: an allowlist that outlives its reason hides the next one.")
    if unexplained:
        print("INERT OPTIONS WITH NO EXPLANATION -- each renders exactly like")
        print("its base mode, so it is testing nothing:")
        for o in unexplained:
            print(f"  {o}")
        print("Either the invocation is wrong -- '-XE 1 20' drew nothing because")
        print("the asteroid loop stops at the first body with no file -- or it")
        print("belongs in ALLOWED with a reason.")
    if stale or unexplained:
        return 1
    print("every graphics-matrix option either moves a render or says why not")
    return 0


sys.exit(main())
