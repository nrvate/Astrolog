#!/usr/bin/env python3
"""The recorded JPL Horizons corpus is intact and fully described.

Three checks over ephsrv/horizons/, none of which touch the network:

  - every manifest row has its file, and every file has its manifest row,
    so a fixture cannot be added or dropped silently;
  - every file's sha256 is the one the manifest recorded when JPL sent it,
    so a fixture cannot be edited into agreement with the code;
  - every row carries the full request URL, because a recorded answer whose
    question was not recorded cannot be replayed, only eyeballed.

The suite's `horizons` group is the other half: it drives each recorded
reply through the client's own parser and requires every committed URL to
still be the one the client builds today. This audit says the bytes are
what JPL sent; that group says they still answer the question we ask.

An absent corpus is a SKIP, not a failure: a checkout without it still
builds and tests, and tools/horizons-fetch.py records it once.
"""

import hashlib
import os
import sys

DIR = "ephsrv/horizons"
MAN = os.path.join(DIR, "MANIFEST.tsv")


def main():
  if not os.path.isdir(DIR) or not os.path.exists(MAN):
    print("horizons_audit: no recorded corpus (skipped)")
    return 0

  rows, bad = {}, []
  with open(MAN) as f:
    for iLine, line in enumerate(f, 1):
      line = line.rstrip("\n")
      if not line or line.startswith("#"):
        continue
      fld = line.split("\t")
      if len(fld) != 4:
        bad.append("line %d: %d fields, want 4 (name/expect/sha256/url)"
                   % (iLine, len(fld)))
        continue
      name, expect, sha, url = fld
      if expect not in ("ok", "error"):
        bad.append("%s: expect is %r, want ok or error" % (name, expect))
      if not url.startswith("https://ssd.jpl.nasa.gov/api/horizons.api?"):
        bad.append("%s: url is not a Horizons API request" % name)
      if len(sha) != 64:
        bad.append("%s: sha256 is not 64 hex digits" % name)
      rows[name] = (expect, sha, url)

  onDisk = {f[:-4] for f in os.listdir(DIR) if f.endswith(".txt")}
  for name in sorted(onDisk - set(rows)):
    bad.append("%s.txt is on disk with no manifest row" % name)
  for name in sorted(set(rows) - onDisk):
    bad.append("%s is in the manifest with no recorded reply" % name)

  for name in sorted(set(rows) & onDisk):
    with open(os.path.join(DIR, name + ".txt"), "rb") as f:
      by = f.read()
    got = hashlib.sha256(by).hexdigest()
    if got != rows[name][1]:
      bad.append("%s: sha256 %s, manifest says %s -- the recorded reply has "
                 "been edited since JPL sent it" % (name, got[:16],
                                                    rows[name][1][:16]))

  if bad:
    for b in bad:
      print("horizons_audit: " + b, file=sys.stderr)
    return 1
  cErr = sum(1 for v in rows.values() if v[0] == "error")
  print("horizons_audit: %d recorded replies intact (%d refusals)"
        % (len(rows), cErr))
  return 0


if __name__ == "__main__":
  sys.exit(main())
