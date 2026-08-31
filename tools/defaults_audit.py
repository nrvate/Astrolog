#!/usr/bin/env python3
"""Audit data.cpp's compile-time defaults against astrolog.as.

The failure class this exists to catch is work log item 63's: an
anonymous initializer run one value short (rObjOrb[] missed Lilith's
entry), silently misaligning every later slot for years. Two legs:

1. COUNT: every audited data.cpp initializer must hold exactly as many
   values as its declared dimension (evaluated from astrolog.h), and
   every range switch line in the .as file must supply exactly
   hi-lo+1 values. Zero-fill and silent truncation are how this class
   of bug hides; counting is how it was finally found.

2. VALUE: upstream's astrolog.as restates the shipped defaults switch
   by switch; machine-diff them against the compiled tables. Upstream
   *deliberately* ships some preferences that differ from compiled
   defaults, so known deltas live in ALLOWED below, each one carried
   with the pair of values it excuses -- a new delta, or an old one
   changing, fails the audit.

Usage:
  python3 tools/defaults_audit.py             # full audit vs astrolog.as
  python3 tools/defaults_audit.py FILE.as     # count leg only, any .as

Exit 0 when clean, 1 on any unexplained finding.
"""

import re
import sys
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read(name):
    with open(os.path.join(ROOT, name), newline='') as f:
        return f.read()


# ---------------------------------------------------------------------------
# Symbol table: enum members and simple #defines from astrolog.h.

def build_symbols(header):
    syms = {}
    enums = re.findall(r'enum\s+\w+\s*\{(.*?)\};', header, re.S)
    defines = re.findall(
        r'^#define\s+(\w+)\s+([^\s/]+(?:[^/\n]*?)?)\s*(?://.*)?$',
        header, re.M)
    defines = [(n, v.strip()) for n, v in defines if '(' not in n]

    def try_eval(expr):
        e = re.sub(r'\b([a-zA-Z_]\w*)\b',
                   lambda m: str(syms[m.group(1)]) if m.group(1) in syms
                   else m.group(1), expr)
        try:
            v = eval(e, {"__builtins__": {}}, {})
        except Exception:
            return None
        if isinstance(v, bool) or not isinstance(v, (int, float)):
            return None
        return v

    # Enum members may reference #defines and vice versa; iterate globally.
    for _ in range(8):
        for name, expr in defines:
            if name not in syms:
                v = try_eval(expr)
                if v is not None:
                    syms[name] = v
        for body in enums:
            body = re.sub(r'//.*', '', body)
            nxt = 0
            for part in body.split(','):
                part = part.strip()
                if not part:
                    continue
                m = re.match(r'^(\w+)\s*=\s*(.+)$', part)
                if m:
                    name = m.group(1)
                    v = try_eval(m.group(2))
                    if v is None:
                        break     # retry this enum on a later round
                    nxt = v
                else:
                    m = re.match(r'^(\w+)$', part)
                    if not m:
                        continue
                    name = m.group(1)
                syms[name] = nxt
                nxt += 1
    return syms


# ---------------------------------------------------------------------------
# data.cpp table extraction.

def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    return re.sub(r'//[^\n]*', ' ', text)


# The domain-checked tables (astrolog.h TBLSIG/TBLOBJ, T2 step 3) declare
# as "TBLOBJ name = {...}" with the dimension implied by the type; this
# maps each checked type back to its dimension expression so those
# initializers audit exactly like the plain "int name[dim]" ones.
CHECKED_TABLE_DIMS = {
    'TBLSIG': 'cSign+1',
    'TBLOBJ': 'oNorm+1',
    'TBLASP': 'cAspect+1',
    'TBLASPR': 'cAspect+1',
    'TBLASPB': 'cAspect+1',
    'TBLASPK': 'cAspect+1',
    'TBLRAY': 'cRay+2',
    'TBLRAYK': 'cRay+2',
    'TBLRAYV': 'cRay+2',
    'TBLRAYSZ': 'cRay+1',
}


def parse_array(source, syms, name):
    """Return (values, declared_size) for a flat numeric initializer."""
    m = re.search(r'\b' + re.escape(name) + r'\[([^\]]*)\]\s*=\s*\{', source)
    if not m:
        m = re.search(r'\b(' + '|'.join(
                          sorted(CHECKED_TABLE_DIMS, key=len, reverse=True)) +
                      r')\s+' + re.escape(name) + r'\s*=\s*\{', source)
        if not m:
            raise KeyError(name)
        dim = CHECKED_TABLE_DIMS[m.group(1)]
    else:
        dim = m.group(1).strip()
    size = None
    if dim:
        e = re.sub(r'\b(\w+)\b',
                   lambda g: str(syms.get(g.group(1), g.group(1))), dim)
        size = int(eval(e, {"__builtins__": {}}, {}))
    body = source[m.end():source.index('};', m.end())]
    body = strip_comments(body)
    vals = []
    for tok in body.split(','):
        tok = tok.strip()
        if not tok:
            continue
        if tok in syms:
            vals.append(float(syms[tok]))
        else:
            e = re.sub(r'\b([a-zA-Z_]\w*)\b',
                       lambda g: str(syms[g.group(1)]), tok)
            vals.append(float(eval(e, {"__builtins__": {}}, {})))
    return vals, size


def parse_objset(source, syms):
    """rgobjset[] rows -> five parallel lists (orb, add, inf, tinf, k)."""
    m = re.search(r'OBJSET\s+rgobjset\[([^\]]*)\]\s*=\s*\{', source)
    body = source[m.end():source.index('\n};', m.end())]
    dim = re.sub(r'\b(\w+)\b',
                 lambda g: str(syms.get(g.group(1), g.group(1))),
                 m.group(1))
    size = int(eval(dim, {"__builtins__": {}}, {}))
    cols = ([], [], [], [], [])
    for row in re.findall(r'\{([^{}]*)\}', strip_comments(body)):
        toks = [t.strip() for t in row.split(',')]
        for i, tok in enumerate(toks):
            cols[i].append(float(syms[tok]) if tok in syms else float(tok))
    return cols, size


def parse_string_array(source, name):
    m = re.search(r'\b' + re.escape(name) + r'\[[^\]]*\]\s*=\s*\{', source)
    body = source[m.end():source.index('};', m.end())]
    return re.findall(r'"([^"]*)"', strip_comments(body))


# ---------------------------------------------------------------------------
# astrolog.as switch parsing.

def parse_as(text):
    """Return {switch: {index: token}} plus count-leg errors."""
    fams = {}
    errors = []
    RANGED = {'-YR', '-YRT', '-YAo', '-YAm', '-YAd', '-Yj', '-YjT', '-YjC',
              '-YjA', '-Y7C', '-Y7O', '-YkO', '-YkA', '-Yk7', '-Yk0', '-Yk'}
    for lineno, line in enumerate(text.splitlines(), 1):
        line = line.split(';')[0].strip()
        if not line or line[0] not in '-=_:':
            continue
        toks = line.split()
        sw = toks[0].replace('=', '-').replace('_', '-').replace(':', '-')
        if sw in RANGED and len(toks) >= 3:
            try:
                lo, hi = int(toks[1]), int(toks[2])
            except ValueError:
                continue
            vals = toks[3:]
            want = hi - lo + 1
            if len(vals) != want:
                errors.append(f"{lineno}: {sw} {lo} {hi} supplies "
                              f"{len(vals)} values, range needs {want}")
                continue
            fam = fams.setdefault(sw, {})
            for i, v in enumerate(vals):
                fam[lo + i] = v
        elif sw in ('-YR7', '-YRZ', '-Yj0', '-Yj7', '-YkC'):
            fams.setdefault(sw, {}).update(
                {i: v for i, v in enumerate(toks[1:])})
        elif sw in ('-YJ', '-YJ0'):
            fams.setdefault(sw, []).append((lineno, toks[1:]))
    return fams, errors


def match_name(abbrev, names):
    """NParseSz-style: first name the abbreviation is a prefix of."""
    low = abbrev.lower()
    for i, name in enumerate(names):
        if name.lower().startswith(low):
            return i
    raise ValueError(f"unresolvable name {abbrev!r}")


# ---------------------------------------------------------------------------
# Known deliberate deltas: astrolog.as ships these preferences over the
# compiled defaults. Format: (family, index, as_value, code_value).
# A new delta, or one of these changing on either side, fails the audit.

ALLOWED = set()


def allow(fam, idx, as_v, code_v):
    ALLOWED.add((fam, idx, as_v, code_v))


# All entries below verified 2026-08-29: at each index, upstream
# CruiserOne master compiles the same value this fork does, and upstream's
# own astrolog.as ships the differing value -- deliberate preferences in
# the shipped config, not code drift on either side.

# Restrictions: the .as restricts the minor planets and South Node that
# the compiled defaults leave on, and turns on the Ascendant and
# Midheaven cusps that the compiled defaults leave off.
for _i in (11, 12, 13, 14, 15, 18, 19, 20, 21):
    allow('-YR', _i, 1.0, 0.0)
allow('-YR', 22, 0.0, 1.0)
allow('-YR', 31, 0.0, 1.0)
for _i in (11, 12, 13, 14, 15, 18):
    allow('-YRT', _i, 1.0, 0.0)

# Earth gets a one-degree orb addition in the .as only.
allow('-YAd', 0, 1.0, 0.0)

# Influence tweaks: outer planets 8 vs 10, Chiron 6 vs 5, the four
# minor points 4 vs 5, Vulcan's transit influence 2 vs 6.
for _i in (8, 9, 10):
    allow('-Yj', _i, 8.0, 10.0)
allow('-Yj', 11, 6.0, 5.0)
for _i in (18, 19, 20, 21):
    allow('-Yj', _i, 4.0, 5.0)
allow('-YjT', 34, 2.0, 6.0)


FAIL = []


def report(msg):
    FAIL.append(msg)
    print("MISMATCH " + msg)


def check(fam, idx, as_val, code_val, tol=1e-9):
    try:
        av = float(as_val)
    except ValueError:
        report(f"{fam} {idx}: unparseable value {as_val!r}")
        return
    if abs(av - code_val) <= tol:
        return
    if (fam, idx, av, code_val) in ALLOWED:
        return
    report(f"{fam} {idx}: .as says {as_val}, data.cpp compiles {code_val:g}")


def main():
    header = read('astrolog.h')
    syms = build_symbols(read('astrolog.h') + '\n' + read('extern.h'))
    data = strip_comments(read('data.cpp'))
    asfile = sys.argv[1] if len(sys.argv) > 1 else 'astrolog.as'
    counts_only = len(sys.argv) > 1
    fams, count_errors = parse_as(read(asfile))
    for e in count_errors:
        report(f"{asfile}:{e}")

    # ---- COUNT leg over data.cpp ----
    audited = ['ignore', 'ignore2', 'ignorea', 'ignorez', 'ignore7',
               'pluszone', 'rAspAngle', 'rAspOrb', 'rAspInf', 'rHouseInf',
               'rgrBonusInf', 'ruler1', 'ruler2', 'exalt', 'rules', 'rules2',
               'rgObjRay', 'rgSignRay', 'rgObjEso1', 'rgObjEso2', 'rgObjHie1',
               'rgObjHie2', 'rgSignEso1', 'rgSignEso2', 'rgSignHie1',
               'rgSignHie2', 'kMainA', 'kRainbowA', 'kElemA', 'kAspA',
               'kRayA']
    tables = {}
    for name in audited:
        vals, size = parse_array(data, syms, name)
        tables[name] = vals
        if size is not None and len(vals) != size:
            report(f"data.cpp {name}[]: {len(vals)} values in a "
                   f"{size}-slot initializer")
    objset, objset_size = parse_objset(data, syms)
    if len(objset[0]) != objset_size:
        report(f"data.cpp rgobjset[]: {len(objset[0])} rows declared "
               f"{objset_size}")

    if counts_only:
        return finish(asfile, count_only=True)

    # ---- VALUE leg vs astrolog.as ----
    szObjName = parse_string_array(data, 'szObjName')
    szSignName = parse_string_array(data, 'szSignName')
    szColor = parse_string_array(data, 'szColor')

    orb, add, inf, tinf, kobj = objset
    simple = [
        ('-YR',  tables['ignore'],  0),
        ('-YRT', tables['ignore2'], 0),
        ('-YAo', tables['rAspOrb'], 0),   # 1-based switch, table has [0]
        ('-YAm', orb, 0),
        ('-YAd', add, 0),
        ('-Yj',  inf, 0),
        ('-YjT', tinf, 0),
        ('-YjC', tables['rHouseInf'], 0),
        ('-YjA', tables['rAspInf'], 0),
        ('-Y7C', tables['rgSignRay'], 0),
        ('-Y7O', tables['rgObjRay'], 0),
    ]
    for fam, table, base in simple:
        for idx, tok in sorted(fams.get(fam, {}).items()):
            if idx + base >= len(table):
                report(f"{fam} {idx}: index outside {len(table)}-entry table")
                continue
            check(fam, idx, tok, table[idx + base])

    # Bonus influences ride split between two tables (io.cpp:1784-1791).
    bonus_map = {'-Yj0': [('rgrBonusInf', 1), ('rgrBonusInf', 2),
                          ('rHouseInf', syms['cSign'] + 1),
                          ('rHouseInf', syms['cSign'] + 2)],
                 '-Yj7': [('rgrBonusInf', 3), ('rgrBonusInf', 4),
                          ('rgrBonusInf', 5),
                          ('rHouseInf', syms['cSign'] + 3),
                          ('rHouseInf', syms['cSign'] + 4),
                          ('rHouseInf', syms['cSign'] + 5)]}
    for fam, targets in bonus_map.items():
        for i, tok in sorted(fams.get(fam, {}).items()):
            if i < len(targets):
                tname, tidx = targets[i]
                check(fam, i, tok, tables[tname][tidx])

    # -YR7 and -YRZ set their tables head-first.
    for fam, tname in (('-YR7', 'ignore7'), ('-YRZ', 'ignorez')):
        for i, tok in sorted(fams.get(fam, {}).items()):
            check(fam, i, tok, tables[tname][i])

    # -YkC element colors, 0-based, exactly four.
    for i, tok in sorted(fams.get('-YkC', {}).items()):
        check('-YkC', i, match_name(tok, szColor), tables['kElemA'][i])

    # Color families resolve names through szColor prefix match.
    for fam, table in (('-YkO', kobj), ('-YkA', tables['kAspA']),
                       ('-Yk7', tables['kRayA']),
                       ('-Yk0', tables['kRainbowA']),
                       ('-Yk', tables['kMainA'])):
        for idx, tok in sorted(fams.get(fam, {}).items()):
            check(fam, idx, match_name(tok, szColor), table[idx])

    # -YJ / -YJ0 rulerships and exaltations by name.
    for lineno, toks in fams.get('-YJ', []):
        obj = match_name(toks[0], szObjName)
        s1 = 0 if toks[1] == '0' else match_name(toks[1], szSignName)
        check('-YJ ruler1', szObjName[obj], s1, tables['ruler1'][obj])
        if len(toks) > 2:
            s2 = 0 if toks[2] == '0' else match_name(toks[2], szSignName)
            check('-YJ ruler2', szObjName[obj], s2, tables['ruler2'][obj])
    for lineno, toks in fams.get('-YJ0', []):
        obj = match_name(toks[0], szObjName)
        s1 = 0 if toks[1] == '0' else match_name(toks[1], szSignName)
        check('-YJ0 exalt', szObjName[obj], s1, tables['exalt'][obj])

    return finish(asfile)


def finish(asfile, count_only=False):
    leg = "count leg" if count_only else "count+value legs"
    if FAIL:
        print(f"FAIL: {len(FAIL)} finding(s), {leg}, vs {asfile}")
        return 1
    print(f"OK: defaults audit clean ({leg}, vs {asfile})")
    return 0


if __name__ == '__main__':
    sys.exit(main())
