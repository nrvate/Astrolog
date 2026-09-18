#!/usr/bin/env python3
"""Record JPL Horizons replies as committed fixtures, once and politely.

  tools/horizons-fetch.py --list          # what would be asked, no network
  tools/horizons-fetch.py                 # record the corpus

This exists so the `horizons` source can be developed and regression-tested
OFFLINE. It is the only thing in this tree that talks to JPL, it is run BY
HAND, and it is not part of any gate -- `make check` and the suite must
never reach the network.

WHERE THE URLS COME FROM, which is the point of the design: not from here.
They are printed by the test binary itself, which builds them with
SzUrlJPLHorizons() -- the same function GetJPLHorizons() calls. A fixture
that answers a question the client never asks proves nothing, and a second
implementation of the query in Python would drift from the first the day
somebody changed one of them. The `horizons` suite group then asserts that
every committed URL is still the one the builder produces today, so a
change to the query shape turns the manifest red rather than leaving the
fixtures quietly stale.

POLICY. The maintainer's, stated for this project: "please respect the
public server and treat it kindly, i dont want to get ip blocked or cause
them any harm". Enforced here rather than remembered:

  - Strictly sequential. One request in flight. No threads, no pools.
  - At least SEC_BETWEEN between requests, measured from the END of one to
    the START of the next, so a slow reply never shortens the gap.
  - An identifying User-Agent, so JPL can see who this is.
  - Backoff 10/20/40s, three retries, then give up. A retry NEVER shortens
    the gap; that is the behaviour that gets an IP blocked, because the
    server is most loaded exactly when it is erroring.
  - A Retry-After header outranks all of the above.
  - A hard ceiling per run. Raising CAP is a conversation with the
    maintainer, not a flag.
  - Already-recorded cases are skipped, so an interrupted run resumes
    without asking JPL the same question twice.

The pacing, the User-Agent shape and the resume-on-rerun property follow
what the Prometheia project measured on 2026-09-17 fetching its own corpus
of 40: strictly sequential at 5s, no throttling and no Retry-After seen.
That is evidence about 40 requests at 5s, not a statement that JPL has no
threshold -- so if a Retry-After does arrive, it wins.

One precaution, not an observed fact: a malformed query may come back 200
with an error in the body rather than as an HTTP status. These requests are
format=text, so that would arrive as a text error page; it is recorded as
whatever it is and the `horizons` group asserts the parser REFUSES it,
which is the behaviour that matters.
"""

import argparse
import hashlib
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request

SEC_BETWEEN = 5.0
SEC_BACKOFF = (10, 20, 40)
SEC_TIMEOUT = 90.0
CAP = 25

UA = ("astrolog-horizons-fetch/1.0 (Astrolog 8.00-qt ephemeris source "
      "fixture corpus; sequential, <=%d requests, >=%.0fs apart; "
      "https://github.com/nrvate/Astrolog)" % (CAP, SEC_BETWEEN))

DIR = "ephsrv/horizons"
BIN = "./astrolog-qt-test"


def cases():
  """Ask the client binary what it would send. (name, expect, url)."""
  if not os.path.exists(BIN):
    sys.exit("%s is not built; run \"make qt-test -j4\" first -- the URLs "
             "come from the client itself, not from this script" % BIN)
  env = dict(os.environ)
  env.update({"ASTROLOG_HORIZONS_URLS": "1", "ASTROLOG_QT_TESTS": "horizons",
              "QT_QPA_PLATFORM": "offscreen", "QT_QPA_PLATFORMTHEME": ""})
  env.pop("DISPLAY", None)
  out = subprocess.run([BIN, "-Yi1", "ephem"], env=env, check=True,
                       capture_output=True, text=True).stdout
  rg = []
  for line in out.splitlines():
    f = line.split("\t")
    if len(f) == 3 and f[2].startswith("https://"):
      rg.append((f[0], f[1], f[2]))
  if not rg:
    sys.exit("the binary printed no URLs; is the horizons group compiled "
             "in (JPLWEB)?")
  return rg


class Pacer:
  def __init__(self):
    self.tLast = None
    self.cReq = 0

  def wait(self, secExtra=0.0):
    sec = max(SEC_BETWEEN, secExtra)
    if self.tLast is not None:
      rest = sec - (time.monotonic() - self.tLast)
      if rest > 0:
        time.sleep(rest)
    elif secExtra > 0:
      time.sleep(secExtra)

  def done(self):
    # From the END of the request: a slow reply must not eat the next gap.
    self.tLast = time.monotonic()
    self.cReq += 1


def fetch(pacer, url, cap):
  if pacer.cReq >= cap:
    raise SystemExit("request cap of %d reached -- stopping rather than "
                     "exceeding it" % cap)
  secAfter, last = 0.0, None
  for i, secBack in enumerate((0,) + SEC_BACKOFF):
    if secBack:
      print("      backing off %ds after: %s" % (secBack, last),
            file=sys.stderr)
      time.sleep(secBack)
    pacer.wait(secAfter)
    secAfter = 0.0
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    try:
      with urllib.request.urlopen(req, timeout=SEC_TIMEOUT) as f:
        by = f.read()
      pacer.done()
      return by
    except urllib.error.HTTPError as e:
      pacer.done()
      last = "HTTP %s" % e.code
      # A Retry-After outranks our own schedule.
      ra = e.headers.get("Retry-After") if e.headers else None
      if ra:
        try:
          secAfter = float(ra)
          print("      JPL asked for %ss; honouring that" % ra,
                file=sys.stderr)
        except ValueError:
          secAfter = 60.0
      elif 400 <= e.code < 500 and e.code != 429:
        # Our mistake, not theirs. The same malformed question gets the
        # same answer, so retrying it is rude and pointless -- but the
        # body is still the fixture we wanted for an error case.
        return e.read()
    except (urllib.error.URLError, OSError) as e:
      pacer.done()
      last = e
  raise SystemExit("gave up after %d retries: %s" % (len(SEC_BACKOFF), last))


def main():
  ap = argparse.ArgumentParser(description=__doc__,
    formatter_class=argparse.RawDescriptionHelpFormatter)
  ap.add_argument("--list", action="store_true",
    help="print what would be asked and exit, touching no network")
  ap.add_argument("--max", type=int, default=CAP,
    help="ceiling on requests this run may make (default and maximum: %d)"
         % CAP)
  args = ap.parse_args()
  if args.max > CAP:
    ap.error("--max above %d is a conversation with the maintainer, not a "
             "flag; see this script's header" % CAP)

  rg = cases()
  if args.list:
    for name, expect, url in rg:
      print("%-14s %-5s %s" % (name, expect, url))
    print("\n%d requests, >=%.0fs apart, sequential. Nothing was fetched."
          % (len(rg), SEC_BETWEEN))
    return

  os.makedirs(DIR, exist_ok=True)
  pacer, rows, cSkip = Pacer(), [], 0
  todo = [c for c in rg if not os.path.exists(os.path.join(DIR, c[0]+".txt"))]
  cSkip = len(rg) - len(todo)
  if cSkip:
    print("%d already recorded, skipping them" % cSkip, file=sys.stderr)
  if len(todo) > args.max:
    sys.exit("%d still to record but the cap is %d; raise it deliberately "
             "or record in two sittings" % (len(todo), args.max))
  print("Contacting JPL: %d requests, >=%.0fs apart, sequential."
        % (len(todo), SEC_BETWEEN), file=sys.stderr)

  for i, (name, expect, url) in enumerate(todo):
    print("  [%2d/%2d] %s" % (i+1, len(todo), name), file=sys.stderr)
    by = fetch(pacer, url, args.max)
    with open(os.path.join(DIR, name + ".txt"), "wb") as f:
      f.write(by)
    print("      %d bytes" % len(by), file=sys.stderr)

  # The manifest carries the QUESTION beside every answer. A recorded reply
  # whose question was not recorded cannot be replayed, only eyeballed.
  for name, expect, url in rg:
    path = os.path.join(DIR, name + ".txt")
    if not os.path.exists(path):
      continue
    with open(path, "rb") as f:
      by = f.read()
    rows.append((name, expect, hashlib.sha256(by).hexdigest(), url))
  with open(os.path.join(DIR, "MANIFEST.tsv"), "w") as f:
    f.write("# JPL Horizons replies, recorded once by tools/horizons-fetch.py.\n")
    f.write("# name\texpect\tsha256\turl\n")
    for r in rows:
      f.write("\t".join(r) + "\n")
  print("\n%d requests made this run, %d fixtures in %s."
        % (pacer.cReq, len(rows), DIR), file=sys.stderr)


if __name__ == "__main__":
  main()
