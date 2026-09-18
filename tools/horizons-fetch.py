#!/usr/bin/env python3
"""Record JPL Horizons responses as committed fixtures, once and politely.

This script exists so that the `horizons` source plugin can be developed and
regression-tested OFFLINE. It is the only thing in this tree that talks to
JPL's public service, it is run BY HAND, and it is not part of any gate --
`make check` must never reach the network, and neither must the suite.

The policy below is the maintainer's, stated for this project: "please
respect the public server and treat it kindly, i dont want to get ip blocked
or cause them any harm". It is enforced here rather than remembered:

  - Strictly sequential. One request in flight. No threads, no pools.
  - At least SEC_BETWEEN seconds between requests, measured from the END of
    one to the START of the next, so a slow response never shortens the gap.
  - An identifying User-Agent, so JPL can see who this is and complain to
    the right project.
  - Backoff on error: 5, 10, 20, 40 seconds, then give up. A retry NEVER
    shortens the gap -- that is the failure mode that gets an IP blocked,
    because the server is most loaded exactly when it is erroring.
  - A hard ceiling on the number of requests a single run may make. Passing
    --max above CAP_ASKED is refused: raising it is a conversation with the
    maintainer, not a flag.

Epochs are batched into one request per body with TLIST where the query
shape allows it, because that is the difference between ~16 requests and
~40 for the same coverage.

Output is one file per request under the corpus directory: the raw response
bytes, and a sidecar .json naming the exact parameters that produced it.
The parameters are part of the fixture -- a recorded response whose question
was not recorded proves nothing later.
"""

import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

# --- Policy. Changing these is a maintainer decision, not a tuning knob. ---

API = "https://ssd.jpl.nasa.gov/api/horizons.api"

SEC_BETWEEN = 5.0          # minimum gap, end of one request to start of next
SEC_BACKOFF = (5, 10, 20, 40)   # then give up
SEC_TIMEOUT = 60.0
CAP_ASKED = 25             # the ceiling the maintainer set for this corpus

UA = ("Astrolog/8.00-qt (ephemeris source plugin fixture recording; "
      "https://github.com/nrvate/Astrolog)")


class Pacer:
  """One request in flight, never closer together than SEC_BETWEEN."""

  def __init__(self, sec_between=SEC_BETWEEN):
    self.sec_between = sec_between
    self.tLast = None
    self.cReq = 0

  def wait(self):
    if self.tLast is None:
      return
    rest = self.sec_between - (time.monotonic() - self.tLast)
    if rest > 0:
      time.sleep(rest)

  def done(self):
    # Measured from the END of the request: a slow response must not be
    # allowed to eat into the next gap.
    self.tLast = time.monotonic()
    self.cReq += 1


def fetch(pacer, params, cap):
  """One GET, paced and backed off. Returns (bytes, url) or raises."""
  if pacer.cReq >= cap:
    raise RuntimeError(
      "request cap of %d reached -- stopping rather than exceeding it" % cap)
  url = API + "?" + urllib.parse.urlencode(params)
  last = None
  for i, secBack in enumerate((0,) + SEC_BACKOFF):
    if secBack:
      print("    backing off %ds after: %s" % (secBack, last), file=sys.stderr)
      time.sleep(secBack)
    pacer.wait()
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    try:
      with urllib.request.urlopen(req, timeout=SEC_TIMEOUT) as f:
        by = f.read()
      pacer.done()
      return by, url
    except (urllib.error.URLError, OSError) as e:
      # A failed attempt still counted against the server: pace after it
      # exactly as after a success.
      pacer.done()
      last = e
      # An HTTP 4xx is our mistake, not theirs, and retrying it is rude and
      # pointless -- the same malformed question gets the same answer.
      if isinstance(e, urllib.error.HTTPError) and 400 <= e.code < 500 and \
         e.code not in (429,):
        raise
  raise RuntimeError("gave up after %d attempts: %s" % (len(SEC_BACKOFF), last))


def write_fixture(dirOut, name, by, url, params):
  os.makedirs(dirOut, exist_ok=True)
  pathBy = os.path.join(dirOut, name + ".txt")
  with open(pathBy, "wb") as f:
    f.write(by)
  with open(os.path.join(dirOut, name + ".json"), "w") as f:
    # The question is part of the fixture. A recorded answer whose question
    # was not recorded cannot be replayed against a rewritten client, only
    # eyeballed.
    json.dump({"url": url, "params": params, "bytes": len(by),
               "recorded": time.strftime("%Y-%m-%d")}, f,
              indent=2, sort_keys=True)
    f.write("\n")
  return pathBy


def main():
  ap = argparse.ArgumentParser(description=__doc__,
    formatter_class=argparse.RawDescriptionHelpFormatter)
  ap.add_argument("--out", default="ephsrv/horizons",
    help="corpus directory (default: ephsrv/horizons)")
  ap.add_argument("--max", type=int, default=CAP_ASKED,
    help="hard ceiling on requests this run may make (default and "
         "maximum: %d)" % CAP_ASKED)
  ap.add_argument("--dry-run", action="store_true",
    help="print the requests that WOULD be made and exit, touching nothing")
  args = ap.parse_args()

  if args.max > CAP_ASKED:
    ap.error("--max above %d is a conversation with the maintainer, not a "
             "flag; see this script's header" % CAP_ASKED)

  print("This script contacts JPL's public Horizons service.",
        file=sys.stderr)
  print("Cap %d requests, >=%.0fs apart, sequential." % (args.max, SEC_BETWEEN),
        file=sys.stderr)
  # The request list is built by the corpus definition, which is filled in
  # from the shape of the queries GetJPLHorizons() actually builds -- the
  # fixtures are only worth anything if they answer the questions the client
  # asks. See EPHEMERIS_PLUGINS_PLAN.md, phase 6h.
  raise SystemExit("corpus definition not yet filled in; see header")


if __name__ == "__main__":
  main()
