#!/usr/bin/env python3
"""The one place the Linux distribution matrix is defined.

    python3 tools/distros.py rpm          # GitHub Actions matrix, as JSON
    python3 tools/distros.py deb          # ditto for the .deb rows
    python3 tools/distros.py dists        # jammy noble fc43 fc44 el9 el10
    python3 tools/distros.py image fc43   # the container image for a dist
    python3 tools/distros.py count        # how many Linux packages a
                                          # release ships (deb + rpm)

Before this, every distribution row was written out by hand in four
files -- ci.yml, release.yml, ci-verify-repo.sh and ci-verify-live-repo.sh
-- and they had to be edited in step. They were not: on 2026-09-03 both
workflows still built Fedora 42, which had been end-of-life since May,
and neither built Fedora 44, which had been current since April. A
release built for a distribution that no longer receives updates, and
not for the one people are installing, is the shape of failure a
hand-maintained list produces on schedule. Fedora produces a release
every six months; the list rotted in one.

So Fedora is ASKED, not written down. Bodhi -- Fedora's own update
system, the service that decides what "current" means for Fedora's
infrastructure -- lists the releases in state "current", and this takes
the two highest. That is the maintainer's rule: the newest release and
the one before it, which is what Fedora supports at any moment (each
release is maintained until a month after the next-but-one ships).

NOT Docker Hub's tag list, which was the first idea. It carries 45 and
46 today as prerelease images, alongside 44; a script picking "the two
highest tags" would build for Rawhide and call it a release.

If Bodhi cannot be reached, endoflife.date's Fedora table is asked
instead -- a static file on a CDN, kept by hand, that agreed with Bodhi
on the day this was written. The fallback exists for an outage, not for
a disagreement: when both answer, Bodhi's answer is used and the other
is not consulted. FEDORA_RELEASES="43 44" in the environment overrides
both, for a machine with no network or for reproducing an old matrix.

The Enterprise Linux rows and the Ubuntu rows stay static. EL releases
every three years and is supported for ten, and Ubuntu's .deb rows are
bounded by which images GitHub offers as runners rather than by what
Canonical supports -- runs-on: ubuntu-26.04 either exists or does not,
and no API this script trusts can say which. Change those by hand, here,
and nowhere else.

THE QT ON FEDORA IS QT6, unlike EL9's Qt5. Fedora still packages both,
and the choice was made for the reason the whole script exists: Fedora
will drop Qt5 before EL9 does, and a row that auto-selects the release
should not depend on a library that release is on its way to removing.
Makefile.qt already picks the Qt it finds, so the package name is the
only thing that differs.
"""

import json
import os
import sys
import urllib.request
from datetime import date

BODHI = os.environ.get(
    "DISTROS_BODHI_URL",
    "https://bodhi.fedoraproject.org/releases/?state=current&rows_per_page=100")
EOL = os.environ.get(
    "DISTROS_EOL_URL", "https://endoflife.date/api/fedora.json")
TIMEOUT = 25

# How many Fedora releases: the current one and the one before it.
FEDORA_COUNT = 2

# The rows that are written down rather than asked for.
EL = [
    {"release": "el9",  "dist": "el9",
     "image": "quay.io/rockylinux/rockylinux:9",  "qt": "qt5-qtbase-devel"},
    {"release": "el10", "dist": "el10",
     "image": "quay.io/rockylinux/rockylinux:10", "qt": "qt6-qtbase-devel"},
]
DEB = [
    {"image": "ubuntu-22.04", "container": "ubuntu:22.04", "codename": "jammy"},
    {"image": "ubuntu-24.04", "container": "ubuntu:24.04", "codename": "noble"},
]


def fetch(url):
    req = urllib.request.Request(url, headers={"User-Agent": "astrolog-distros"})
    with urllib.request.urlopen(req, timeout=TIMEOUT) as r:
        return json.load(r)


def fedora_from_bodhi():
    data = fetch(BODHI)
    versions = set()
    for rel in data.get("releases", []):
        if rel.get("id_prefix") != "FEDORA":       # not EPEL, not containers
            continue
        if rel.get("state") != "current":
            continue
        try:
            versions.add(int(rel["version"]))
        except (KeyError, ValueError):
            continue
    if not versions:
        raise RuntimeError("Bodhi listed no current Fedora release")
    return sorted(versions)[-FEDORA_COUNT:]


def fedora_from_eol():
    today = date.today().isoformat()
    versions = []
    for cycle in fetch(EOL):
        eol = cycle.get("eol")
        # eol is an ISO date, or a boolean for "not yet scheduled".
        alive = (eol is False) or (isinstance(eol, str) and eol > today)
        released = cycle.get("releaseDate", "9999") <= today
        if alive and released:
            try:
                versions.append(int(cycle["cycle"]))
            except ValueError:
                continue
    if not versions:
        raise RuntimeError("endoflife.date listed no live Fedora release")
    return sorted(versions)[-FEDORA_COUNT:]


def fedora_releases():
    forced = os.environ.get("FEDORA_RELEASES")
    if forced:
        vs = sorted(int(v) for v in forced.split())
        sys.stderr.write("distros: FEDORA_RELEASES forced to %s\n" % vs)
        return vs
    try:
        vs = fedora_from_bodhi()
        sys.stderr.write("distros: Fedora %s, from Bodhi\n" % vs)
        return vs
    except Exception as e:                           # outage, not disagreement
        sys.stderr.write("distros: Bodhi unavailable (%s); asking endoflife.date\n" % e)
    try:
        vs = fedora_from_eol()
        sys.stderr.write("distros: Fedora %s, from endoflife.date\n" % vs)
        return vs
    except Exception as e:
        sys.stderr.write("distros: endoflife.date unavailable too (%s)\n" % e)
    sys.stderr.write(
        "distros: cannot determine the current Fedora releases. Set\n"
        "FEDORA_RELEASES=\"43 44\" to say so by hand; not guessing.\n")
    sys.exit(1)


def rpm_rows():
    rows = []
    for v in fedora_releases():
        rows.append({"release": "fedora-%d" % v, "dist": "fc%d" % v,
                     "image": "fedora:%d" % v, "qt": "qt6-qtbase-devel"})
    return rows + EL


def image_for(dist):
    for r in DEB:
        if r["codename"] == dist:
            return r["container"]
    for r in rpm_rows():
        if r["dist"] == dist:
            return r["image"]
    return ""


def main(argv):
    what = argv[1] if len(argv) > 1 else ""
    if what == "rpm":
        print(json.dumps({"include": rpm_rows()}))
    elif what == "deb":
        print(json.dumps({"include": DEB}))
    elif what == "dists":
        print(" ".join([r["codename"] for r in DEB] + [r["dist"] for r in rpm_rows()]))
    elif what == "image":
        if len(argv) < 3:
            sys.stderr.write("usage: distros.py image <dist>\n")
            return 2
        img = image_for(argv[2])
        if not img:
            return 1
        print(img)
    elif what == "count":
        print(len(DEB) + len(rpm_rows()))
    else:
        sys.stderr.write(__doc__.split("\n\n")[0] + "\n")
        sys.stderr.write("usage: distros.py rpm|deb|dists|image <dist>|count\n")
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
