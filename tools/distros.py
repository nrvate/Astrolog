#!/usr/bin/env python3
"""The one place the Linux distribution matrix is defined.

    python3 tools/distros.py rpm          # GitHub Actions matrix, as JSON
    python3 tools/distros.py deb          # ditto for the .deb rows: every
                                          # Ubuntu LTS in standard support
    python3 tools/distros.py dists        # jammy noble resolute fc43 fc44 el9 el10
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

Ubuntu is asked too, since 2026-09-04 (later the same day). The rule is
Canonical's own: an LTS release -- xx.04 with an even year -- is in
standard support for five years from its release date. Launchpad's
series API gives every release with its date and status, so "every LTS
still in standard support" is computed from it: 22.04, 24.04 and 26.04
on the day this was written, and 22.04 drops out by itself in April
2027. NOT Launchpad's own "supported" flag, which is true for 20.04 as
well because Ubuntu Pro extends it; that is support a user pays for, not
what "supported" means here. endoflife.date is the fallback, with its
lts flag and standard-support eol date; UBUNTU_RELEASES="24.04 26.04"
overrides both.

This became possible when the .deb jobs stopped running ON GitHub's
runner images and started building INSIDE ubuntu:<version> containers,
the way the rpm jobs always built inside fedora:<version> ones
(tools/build-in-container.sh). Until then the Ubuntu rows were bounded
by which runner images existed, and Ubuntu 26.04 LTS had been out for
five months with no ubuntu-26.04 runner in sight.

THE QT ON UBUNTU IS QT5 UP TO 24.04 AND QT6 FROM 26.04. 22.04's
qt6-base-dev ships no pkg-config files at all (measured in the image),
so it cannot build Qt6; 24.04 could, and keeps Qt5 because that is what
the maintainer's own machine runs and a Qt5 .deb lane is worth having
while distributions still ship it. 26.04 has both and gets Qt6 for the
same reason Fedora does. Measured 2026-09-04 in ubuntu:26.04: Qt 6.10.2,
14 .pc files, builds, packages, installs into a clean image, computes
Chiron.

The Enterprise Linux rows stay static. EL releases every three years and
is supported for ten. Change those by hand, here, and nowhere else.

THE QT ON FEDORA IS QT6, unlike EL9's Qt5. Fedora still packages both,
and the choice was made for the reason the whole script exists: Fedora
will drop Qt5 before EL9 does, and a row that auto-selects the release
should not depend on a library that release is on its way to removing.
Makefile.qt already picks the Qt it finds, so the package name is the
only thing that differs.
"""

import json
import os
import re
import sys
import urllib.request
from datetime import date

BODHI = os.environ.get(
    "DISTROS_BODHI_URL",
    "https://bodhi.fedoraproject.org/releases/?state=current&rows_per_page=100")
EOL = os.environ.get(
    "DISTROS_EOL_URL", "https://endoflife.date/api/fedora.json")
LAUNCHPAD = os.environ.get(
    "DISTROS_LAUNCHPAD_URL", "https://api.launchpad.net/devel/ubuntu/series")
EOL_UBUNTU = os.environ.get(
    "DISTROS_EOL_UBUNTU_URL", "https://endoflife.date/api/ubuntu.json")
TIMEOUT = 25

# Canonical's rule: an LTS gets five years of standard support.
UBUNTU_LTS_YEARS = 5

# How many Fedora releases: the current one and the one before it.
FEDORA_COUNT = 2

# The rows that are written down rather than asked for.
EL = [
    {"release": "el9",  "dist": "el9",
     "image": "quay.io/rockylinux/rockylinux:9",  "qt": "qt5-qtbase-devel"},
    {"release": "el10", "dist": "el10",
     "image": "quay.io/rockylinux/rockylinux:10", "qt": "qt6-qtbase-devel"},
]
# Codenames, for the .deb version suffix and the apt suite. Launchpad
# supplies them; endoflife.date's "codename" field is the full name
# ("Noble Numbat"), of which the first word, lowercased, is the one apt
# uses. Written here as well so an override (UBUNTU_RELEASES) can name a
# release without asking anyone.
UBUNTU_CODENAMES = {"20.04": "focal", "22.04": "jammy", "24.04": "noble",
                    "26.04": "resolute", "28.04": None}


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


def _is_lts(version):
    m = re.match(r"^(\d\d)\.(\d\d)$", version)
    return bool(m) and m.group(2) == "04" and int(m.group(1)) % 2 == 0


def _add_years(iso, years):
    return "%04d%s" % (int(iso[:4]) + years, iso[4:10])


def ubuntu_from_launchpad():
    """Every LTS whose five years of standard support have not ended."""
    today = date.today().isoformat()
    data = fetch(LAUNCHPAD)
    rows = []
    for e in data.get("entries", []):
        v = e.get("version") or ""
        released = (e.get("datereleased") or "")[:10]
        if not _is_lts(v) or not released or released > today:
            continue
        if _add_years(released, UBUNTU_LTS_YEARS) <= today:
            continue
        rows.append((v, e.get("name")))
    if not rows:
        raise RuntimeError("Launchpad listed no supported Ubuntu LTS")
    return sorted(rows)


def ubuntu_from_eol():
    today = date.today().isoformat()
    rows = []
    for cycle in fetch(EOL_UBUNTU):
        v = cycle.get("cycle", "")
        eol = cycle.get("eol")
        alive = (eol is False) or (isinstance(eol, str) and eol > today)
        released = cycle.get("releaseDate", "9999") <= today
        if cycle.get("lts") and alive and released and _is_lts(v):
            name = (cycle.get("codename") or "").split(" ")[0].lower() or None
            rows.append((v, name))
    if not rows:
        raise RuntimeError("endoflife.date listed no supported Ubuntu LTS")
    return sorted(rows)


def ubuntu_releases():
    """[(version, codename)] for every LTS in standard support."""
    forced = os.environ.get("UBUNTU_RELEASES")
    if forced:
        rows = sorted((v, UBUNTU_CODENAMES.get(v)) for v in forced.split())
        sys.stderr.write("distros: UBUNTU_RELEASES forced to %s\n" % [v for v, _ in rows])
    else:
        rows = None
        try:
            rows = ubuntu_from_launchpad()
            sys.stderr.write("distros: Ubuntu %s, from Launchpad\n" % [v for v, _ in rows])
        except Exception as e:
            sys.stderr.write("distros: Launchpad unavailable (%s); asking endoflife.date\n" % e)
        if rows is None:
            try:
                rows = ubuntu_from_eol()
                sys.stderr.write("distros: Ubuntu %s, from endoflife.date\n" % [v for v, _ in rows])
            except Exception as e:
                sys.stderr.write("distros: endoflife.date unavailable too (%s)\n" % e)
                sys.stderr.write(
                    "distros: cannot determine the supported Ubuntu LTS releases. Set\n"
                    "UBUNTU_RELEASES=\"24.04 26.04\" to say so by hand; not guessing.\n")
                sys.exit(1)
    out = []
    for v, name in rows:
        name = name or UBUNTU_CODENAMES.get(v)
        if not name:
            sys.stderr.write("distros: no codename known for Ubuntu %s; add it to UBUNTU_CODENAMES\n" % v)
            sys.exit(1)
        out.append((v, name))
    return out


def deb_rows():
    rows = []
    for v, name in ubuntu_releases():
        qt = "qtbase5-dev" if float(v) <= 24.04 else "qt6-base-dev"
        rows.append({"image": "ubuntu-%s" % v, "version": v, "codename": name,
                     "container": "ubuntu:%s" % v, "qt": qt})
    return rows


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
    for r in deb_rows():
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
        print(json.dumps({"include": deb_rows()}))
    elif what == "dists":
        print(" ".join([r["codename"] for r in deb_rows()] + [r["dist"] for r in rpm_rows()]))
    elif what == "image":
        if len(argv) < 3:
            sys.stderr.write("usage: distros.py image <dist>\n")
            return 2
        img = image_for(argv[2])
        if not img:
            return 1
        print(img)
    elif what == "count":
        print(len(deb_rows()) + len(rpm_rows()))
    else:
        sys.stderr.write(__doc__.split("\n\n")[0] + "\n")
        sys.stderr.write("usage: distros.py rpm|deb|dists|image <dist>|count\n")
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
