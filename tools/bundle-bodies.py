#!/usr/bin/env python3
"""The small bodies Astrolog ships for Ephemeris Prometheia: one catalog cut
from a full one, and listings of what is in it.

  tools/bundle-bodies.py --catalog FULL.epm [--out DIR] [--work DIR]
                         [--umax 4] [--research FILE]

What ships -- the maintainer's choices of 2026-10-25, QT_GUI_PLAN.md item 291:
  - every numbered asteroid 1-10000 whose aphelion is inside 7.5 AU: the
    main belt, with the near-Earth asteroids, Hildas and Jupiter trojans
    among those numbers;
  - every SBDB asteroid whose aphelion is beyond 7.5 AU and whose orbit
    condition code U is at most --umax (4). Measured against Prometheia's
    full covariance propagation over 56 bodies, U <= 4 stayed within about
    7' (1 sigma) anywhere in 1900-2100 -- one centaur reached 51' -- while
    U 5 reached 89' and U 6 32 degrees. A body with no U is left out;
  - only what --catalog has, so a body designated since it was made is left
    out; and never Pluto (134340), which is the planetary ephemeris's.
The names are the catalog's, which are JPL's, the IAU's names: never
astromcp's, which holds the maintainer's own unofficial ones.

Writes into --out (default ephem/):
  astrolog-bodies.epm                  every group, one catalog
  astrolog-bodies-mainbelt.txt         numbered 1-10000, aphelion inside 7.5 AU
  astrolog-bodies-distant-named.txt    aphelion beyond 7.5 AU, IAU-named
  astrolog-bodies-distant-numbered.txt ... numbered, not named
  astrolog-bodies-distant-unnumbered.txt ... provisional designation only
Each listing is sorted by absolute magnitude H, brightest (largest) first;
bodies with no H follow, numbered ones by number, then the rest by name.
--research FILE also writes every body beyond 7.5 AU, whatever its orbit,
with a "shipped" column -- the list for looking through, not for shipping.

The inputs are fetched once into --work and reused from there: two SBDB
query-API requests (the bodies beyond 7.5 AU, and numbered 1-10000, with
full precision) and Johnston's TNO list, two seconds apart. Delete --work
to fetch afresh.

Sizes, in order of preference: Johnston's measured diameter (his table
marks an estimate with "?"), SBDB's measured diameter, then an estimate
from H, D = 1329 km / sqrt(p) * 10^(-H/5), with Johnston's assumed albedo
for his dynamical class, or for the class the elements suggest (marked
"~"), or for the main belt SBDB's albedo or 0.14. JPL's own diameters for
these bodies are mostly from 1998-2002 (Varuna 900 km, against 668 since
Herschel), which is why Johnston's come first.

The catalog is cut with Prometheia's own prometheia-catalog-subset, built
from the submodule by tools/prometheia-tool.sh; a body --catalog lacks is
named and left out (--skip-missing).
"""
import argparse
import datetime
import html
import json
import math
import os
import re
import subprocess
import sys
import time
import urllib.parse
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UA = 'Astrolog tools/bundle-bodies.py (https://github.com/nrvate/Astrolog)'
SBDB = 'https://ssd-api.jpl.nasa.gov/sbdb_query.api'
JOHNSTON = 'https://www.johnstonsarchive.net/astro/tnoslist.html'
FIELDS = ('spkid,full_name,pdes,name,prefix,class,H,H_sigma,G,diameter,'
          'diameter_sigma,albedo,a,e,i,q,ad,epoch,condition_code,data_arc,'
          'n_obs_used,first_obs,last_obs,source,orbit_id,soln_date')
QUERIES = {
    'outer.json': [('fields', FIELDS), ('sb-kind', 'a'),
                   ('sb-cdata', '{"AND":["ad|GT|7.5"]}'), ('full-prec', '1')],
    'num10k.json': [('fields', FIELDS), ('sb-kind', 'a'), ('sb-ns', 'n'),
                    ('sort', 'spkid'), ('limit', '10000'), ('full-prec', '1')],
}
GROUPS = [
    ('mainbelt', 'Numbered asteroids 1-10000 with aphelion inside 7.5 AU'),
    ('distant-named', 'Named bodies with aphelion beyond 7.5 AU'),
    ('distant-numbered', 'Numbered, unnamed bodies with aphelion beyond 7.5 AU'),
    ('distant-unnumbered', 'Unnumbered bodies with aphelion beyond 7.5 AU'),
]


def fetch(work):
    """The three inputs, fetched once, sequentially, two seconds apart."""
    os.makedirs(work, exist_ok=True)
    jobs = [(name, SBDB + '?' + urllib.parse.urlencode(q)) for name, q in QUERIES.items()]
    jobs.append(('tnoslist.html', JOHNSTON))
    stamp = os.path.join(work, 'fetched.txt')
    first = True
    for name, url in jobs:
        path = os.path.join(work, name)
        if os.path.exists(path):
            continue
        if not first:
            time.sleep(2)
        first = False
        print('fetching %s' % url[:100], file=sys.stderr)
        req = urllib.request.Request(url, headers={'User-Agent': UA})
        with urllib.request.urlopen(req, timeout=600) as r:
            data = r.read()
        with open(path + '.part', 'wb') as f:
            f.write(data)
        os.replace(path + '.part', path)
        with open(stamp, 'a') as f:
            f.write('%s %s\n' % (datetime.date.today().isoformat(), name))
    dates = {}
    if os.path.exists(stamp):
        for ln in open(stamp):
            d, n = ln.split()
            dates[n] = d
    return dates


def load(path):
    j = json.load(open(path))
    out = []
    for r in j['data']:
        o = dict(zip(j['fields'], r))
        for k in ('H', 'diameter', 'albedo', 'a', 'e', 'i', 'q', 'ad', 'data_arc'):
            o[k] = float(o[k]) if o.get(k) not in (None, '') else None
        o['n_obs_used'] = int(o['n_obs_used']) if o.get('n_obs_used') else None
        o['U'] = int(o['condition_code']) if o.get('condition_code') not in (None, '') else None
        o['pdes'] = str(o['pdes'])
        o['spkid'] = str(o['spkid'])
        out.append(o)
    return out


def johnston(path):
    """Johnston's rows by key (MPC number, or provisional designation)."""
    t = html.unescape(re.sub(r'<[^>]+>', '', open(path, encoding='latin-1').read()))
    rows = {}
    tail = re.compile(r'^\s*([\d.]+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)'
                      r'\s+([\d.]+)\s*(\?)?\s+([\d.]+|\?)\s*(\(([\d.]+)\))?')
    # Columns by position, from the table's header: name 0-35, provisional
    # designation 36-50, dynamical class 51-66, then a e q Q i, diameter
    # ("?" after an estimate), albedo, and the assumed albedo in brackets.
    for s in t[t.index('number/name'):].split('\n'):
        name, prov, cls = s[:36].strip(), s[36:51].strip(), s[51:67].strip()
        m = re.match(r'\((\d+)\)', name)
        key = m.group(1) if m else prov
        mt = tail.match(s[67:])
        if not key or not mt:
            continue
        rows[key] = {
            'jclass': cls,
            'jdiam': float(mt.group(6)),
            'jmeasured': mt.group(7) is None,
            'jalb': float(mt.group(8)) if mt.group(8) != '?' else None,
            'jassum': float(mt.group(10)) if mt.group(10) else None,
        }
    return rows


def guess_class(o):
    """Johnston's albedo scheme, keyed on a class guessed from the elements."""
    a, i, q = o['a'], o['i'], o['q']
    if o['class'] not in ('TNO', 'CEN'):
        return 'unusual~', 0.051
    if o['class'] == 'CEN' or a < 30.07:
        return 'centaur~', 0.058
    if 39.0 <= a <= 39.8:
        return 'plutino~', 0.074
    if 42.0 <= a <= 48.0 and q >= 35.0:
        return ('cubewano-cold~', 0.152) if i < 5.0 else ('cubewano-hot~', 0.079)
    if a > 50.0:
        return 'SDO~', 0.124
    return 'other TNO~', 0.130


def diam(H, p):
    return 1329.0 / math.sqrt(p) * 10 ** (-H / 5.0)


def prov_of(o):
    m = re.search(r'\(([^)]+)\)\s*$', o['full_name'].strip())
    return m.group(1) if m else ''


def size(o, J, mainbelt):
    j = J.get(o['pdes']) or J.get(prov_of(o))
    o['jclass'] = j['jclass'] if j else ''
    if j and j['jmeasured']:
        return j['jdiam'], j['jalb'], 'measured (Johnston)'
    if o['diameter']:
        return o['diameter'], o['albedo'], 'measured (JPL)'
    if o['H'] is None:
        return None, None, ''
    if mainbelt:
        p, why = (o['albedo'], 'JPL albedo') if o['albedo'] else (0.14, 'assumed')
        return diam(o['H'], p), p, 'est. %s' % why
    if j and j['jassum']:
        return diam(o['H'], j['jassum']), j['jassum'], 'est. (%s)' % j['jclass']
    c, p = guess_class(o)
    return diam(o['H'], p), p, 'est. (%s)' % c


def sortkey(o):
    if o['H'] is not None:
        return (0, o['H'], 0, '')
    if o['pdes'].isdigit():
        return (1, 0, int(o['pdes']), '')
    return (2, 0, 0, o['pdes'])


COLS = ['number', 'name', 'designation', 'class', 'jclass', 'H', 'D_km', 'albedo',
        'D_source', 'a', 'e', 'i', 'q', 'Q', 'U', 'arc_yr', 'n_obs', 'first_obs', 'last_obs', 'spkid']
SHOWN = 15   # the .txt shows the columns through U


def row(o, shipped):
    num = o['pdes'] if o['pdes'].isdigit() else ''
    des = prov_of(o) if num else o['pdes']
    f = lambda v, fmt: (fmt % v) if v is not None else ''
    d = o['D']
    r = [num, o['name'] or '', des, o['class'], o['jclass'], f(o['H'], '%.2f'),
         f(d, '%.0f' if (d or 0) >= 10 else '%.1f' if (d or 0) >= 1 else '%.3f'),
         f(o['alb'], '%.3f'), o['Dsrc'], f(o['a'], '%.3f'), f(o['e'], '%.3f'),
         f(o['i'], '%.2f'), f(o['q'], '%.3f'), f(o['ad'], '%.2f'),
         '' if o['U'] is None else str(o['U']),
         f(o['data_arc'] / 365.25 if o['data_arc'] else None, '%.1f'),
         f(o['n_obs_used'], '%d'), o['first_obs'] or '', o['last_obs'] or '', o['spkid']]
    if shipped is not None:
        r.insert(SHOWN, 'yes' if o['spkid'] in shipped else 'no')
    return r


def listing(path, title, bodies, sources, shipped=None):
    cols = COLS[:SHOWN] + (['shipped'] if shipped is not None else [])
    rows = [row(o, shipped) for o in sorted(bodies, key=sortkey)]
    n = len(cols)
    w = [min(24, max([len(c)] + [len(r[k]) for r in rows])) for k, c in enumerate(cols)]
    with open(path, 'w') as f:
        f.write('%s\n%d bodies, sorted by absolute magnitude H, brightest (largest) first.\n'
                % (title, len(rows)))
        f.write('D_km: measured where a source gives one, else estimated from H and the albedo\n'
                'shown (class in brackets; "~" = class guessed from the elements).\n'
                'U: the orbit condition code, 0 best .. 9 worst.\n' + sources + '\n')
        f.write('%5s  %s\n' % ('#', '  '.join(c.ljust(w[k]) for k, c in enumerate(cols))))
        for k, r in enumerate(rows, 1):
            f.write('%5d  %s\n' % (k, '  '.join(r[x][:w[x]].ljust(w[x]) for x in range(n)).rstrip()))


def cut(subset, catalog, bodies, work, out):
    """Cut the catalog to the bodies, leaving out any it has not got.

    --skip-missing (Prometheia's 624c96d, asked for by this tool) prints each
    list line that matches no body, or more than one, with "(skipped)" and
    goes on. Before it the cut stopped at the first and was rerun per body."""
    listpath = os.path.join(work, 'bundle.list')
    names = [o['pdes'] for o in bodies]
    with open(listpath, 'w') as f:
        f.write('\n'.join(names) + '\n')
    p = subprocess.run([subset, catalog, listpath, '--skip-missing', '-o', out],
                       capture_output=True, text=True)
    if p.returncode != 0:
        sys.exit('prometheia-catalog-subset failed:\n' + p.stderr)
    dropped = []
    for ln in (p.stdout + p.stderr).splitlines():
        m = re.match(r"^.*:(\d+): '(.*)' matches .* \(skipped\)$", ln)
        if m:
            k = int(m.group(1)) - 1
            assert names[k] == m.group(2), (names[k], m.group(2))
            dropped.append(names[k])
    # A 1.2 catalog serves a line naming a numbered body by its old
    # provisional designation, and says "'2015 RR245' -> 523794 (old
    # designation)" (Prometheia's a5abc8e). That body was numbered after the
    # SBDB fetch in the work directory, so the listings would show it
    # unnumbered: stop, and have the fetch redone.
    renamed = re.findall(r"'(.*)' -> (\d+) \(old designation\)", p.stdout + p.stderr)
    if renamed:
        sys.exit('numbered since the SBDB fetch in %s: %s\n'
                 'delete outer.json and num10k.json there and run again'
                 % (work, ', '.join('%s = %s' % r for r in renamed)))
    said = [ln for ln in (p.stdout + p.stderr).splitlines() if '(skipped)' not in ln]
    served = set(names) - set(dropped)
    with open(listpath, 'w') as f:
        f.write('\n'.join(n for n in names if n in served) + '\n')
    return served, dropped, '\n'.join(said)

def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--catalog', required=True, help='the full Prometheia catalog to cut from')
    ap.add_argument('--out', default=os.path.join(ROOT, 'ephem'))
    ap.add_argument('--work', default=os.path.join(ROOT, 'obj-prometheia', 'bundle-work'))
    ap.add_argument('--umax', type=int, default=4)
    ap.add_argument('--research', help='also write every body beyond 7.5 AU here')
    args = ap.parse_args()

    dates = fetch(args.work)
    J = johnston(os.path.join(args.work, 'tnoslist.html'))
    outer = load(os.path.join(args.work, 'outer.json'))
    num = load(os.path.join(args.work, 'num10k.json'))
    distant_ids = {o['spkid'] for o in outer}
    mainbelt = [o for o in num if o['spkid'] not in distant_ids]
    for o in outer:
        o['D'], o['alb'], o['Dsrc'] = size(o, J, False)
    for o in mainbelt:
        o['D'], o['alb'], o['Dsrc'] = size(o, J, True)
    distant = [o for o in outer if o['U'] is not None and o['U'] <= args.umax and o['pdes'] != '134340']

    subset = subprocess.run([os.path.join(ROOT, 'tools', 'prometheia-tool.sh'), 'catalog-subset'],
                            check=True, capture_output=True, text=True).stdout.strip()
    os.makedirs(args.out, exist_ok=True)
    epm = os.path.join(args.out, 'astrolog-bodies.epm')
    served, dropped, said = cut(subset, args.catalog, mainbelt + distant, args.work, epm)
    print(said, file=sys.stderr)
    print('not in the catalog, left out: %d%s' % (len(dropped), (' (' + ', '.join(dropped[:20]) +
          (' ...' if len(dropped) > 20 else '') + ')') if dropped else ''), file=sys.stderr)

    sbdb_date = dates.get('outer.json', '?')
    sources = ('Orbits, H, U and names: NASA/JPL Small-Body Database (ssd.jpl.nasa.gov), %s;\n'
               'the catalog\'s records: %s.\n'
               'Sizes marked (Johnston), and the classes and assumed albedos in jclass: Wm. Robert\n'
               'Johnston, "List of known trans-Neptunian objects", %s,\n'
               'retrieved %s; his sources are at johnstonsarchive.net/astro/tnodiam.html.\n'
               % (sbdb_date, os.path.basename(args.catalog), JOHNSTON.replace('https://www.', ''),
                  dates.get('tnoslist.html', '?')))
    kept = [o for o in distant if o['pdes'] in served]
    groups = {
        'mainbelt': [o for o in mainbelt if o['pdes'] in served],
        'distant-named': [o for o in kept if o['name']],
        'distant-numbered': [o for o in kept if not o['name'] and o['pdes'].isdigit()],
        'distant-unnumbered': [o for o in kept if not o['name'] and not o['pdes'].isdigit()],
    }
    for g, title in GROUPS:
        if g != 'mainbelt':
            title += ', U <= %d' % args.umax
        listing(os.path.join(args.out, 'astrolog-bodies-%s.txt' % g), title, groups[g], sources)
        print('%-20s %5d' % (g, len(groups[g])), file=sys.stderr)
    if args.research:
        ids = {o['spkid'] for o in kept}
        listing(args.research, 'Every body SBDB lists with aphelion beyond 7.5 AU, any orbit quality.\n'
                '"shipped" = in astrolog-bodies.epm: U <= %d and in %s (Pluto comes from DE441)'
                % (args.umax, os.path.basename(args.catalog)), outer, sources, ids)


if __name__ == '__main__':
    main()
