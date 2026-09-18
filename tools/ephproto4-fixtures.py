#!/usr/bin/env python3
"""Ephemeris protocol version 4 conformance fixtures.

An independent reference encoder, written from EPHEMERIS_PLUGINS_PLAN.md
section 3 and nothing else -- not from ephsrv/ephproto.h -- so the C++
codecs (Astrolog's and Ephemeris Prometheia's) are checked against a second
reading of the specification rather than against themselves.

    tools/ephproto4-fixtures.py            # (re)write ephsrv/conformance/
    tools/ephproto4-fixtures.py --check    # exit 1 if the files differ

Each fixture is one complete message (envelope included) as lowercase hex,
64 digits a line, in ephsrv/conformance/<name>.hex. MANIFEST.tsv lists
file, direction (c2s/s2c), message type, expected outcome and a note. The
outcomes are the spec's (3.10): "ok" (parses; re-encodes to the same bytes),
"malformed" (ERROR 1) and "unsupported" (ERROR 11).
"""

import hashlib
import math
import os
import struct
import sys

MAGIC = 0x1EF0
V4 = 4
NAN = struct.unpack("<d", bytes.fromhex("000000000000f87f"))[0]

# A.1 message types
HELLO, WELCOME, REQUEST, DATA, ERROR, PING, PONG, CANCEL = 1, 2, 3, 4, 5, 6, 7, 8
LOOKUP, LOOKUP_RESULT, SEGDATA = 9, 10, 15


# ---- primitives ----------------------------------------------------------

def u8(v): return struct.pack("<B", v)
def u16(v): return struct.pack("<H", v)
def u32(v): return struct.pack("<I", v)
def i32(v): return struct.pack("<i", v)
def i64(v): return struct.pack("<q", v)
def f32(v): return struct.pack("<f", v)
def f64(v): return struct.pack("<d", v)
def raw_f64(hex_le): return bytes.fromhex(hex_le)


def str8(s):
    b = s.encode("utf-8") if isinstance(s, str) else s
    assert len(b) <= 255
    return u8(len(b)) + b


def time(jd1, jd2=0.0):
    return f64(jd1) + f64(jd2)


def tlv(entries, sort=True):
    """entries: list of (tag, payload bytes). sort=False keeps a bad order."""
    if sort:
        entries = sorted(entries, key=lambda e: e[0])
    body = b"".join(u16(t) + u16(len(p)) + p for t, p in entries)
    return u16(len(body)) + body


def envelope(mtype, payload, request_id=0, version=V4, flags=0, reserved=0,
             magic=MAGIC):
    return (u16(magic) + u8(version) + u8(flags) + u16(mtype) + u16(reserved) +
            u32(request_id) + u32(len(payload)) + payload)


# ---- messages --------------------------------------------------------------

def hello(proto_max=4, proto_min=4, build=0, caps=0, name="Astrolog 8.00-qt.25",
          token="", ext=None):
    return (u32(proto_max) + u32(proto_min) + u32(build) + u32(caps) +
            str8(name) + str8(token) + tlv(ext or []))


def welcome(caps, engine, dataset, ext, server="astrolog-ephd/2.0",
            limits=(64, 20000, 500, 4 * 1024 * 1024, 100000), max_profiles=16):
    mo, mr, mc, mp, mcells = limits
    return (u32(4) + u32(caps) + u32(mo) + u32(mr) + u32(mc) + u32(mp) +
            u32(mcells) + u8(max_profiles) + u8(0) + u16(0) +
            str8(server) + str8(engine) + str8(dataset) + tlv(ext))


def delivery(precision=0, priority=0, representation=0, chunk_rows=0,
             seg_err=0.0, max_degree=0, deadline_ms=0):
    # 16 bytes: deadlineMs closes it, advisory and outside the cache key.
    return (u8(precision) + u8(priority) + u8(representation) + u8(max_degree) +
            u32(chunk_rows) + f32(seg_err) + u32(deadline_ms))


def grid(start_jd1, start_jd2, step_ns, n):
    return u8(0) + u16(0) + time(start_jd1, start_jd2) + i64(step_ns) + u32(n)


def time_block(scale, mode_bytes):
    # mode_bytes starts with the timeMode byte
    return u8(scale) + mode_bytes


def grid_block(scale, jd1, jd2, step_ns, n):
    return u8(scale) + u8(0) + u16(0) + time(jd1, jd2) + i64(step_ns) + u32(n)


def list_block(scale, instants):
    b = u8(scale) + u8(1) + u16(0) + u32(len(instants))
    for jd1, jd2 in instants:
        b += time(jd1, jd2)
    return b


def profile(observer=0, plane=0, form=0, frame=0, corrections=7, speeds=1,
            sidereal_plane=0, reserved=0, observer_body=0, site=(0.0, 0.0, 0.0),
            anchor=(0.0, 0.0), anchor_ayan=0.0, columns=0, zodiac="", ext=None):
    return (u8(observer) + u8(plane) + u8(form) + u8(frame) + u8(corrections) +
            u8(speeds) + u8(sidereal_plane) + u8(reserved) + i32(observer_body) +
            f64(site[0]) + f64(site[1]) + f64(site[2]) +
            time(anchor[0], anchor[1]) + f64(anchor_ayan) + u32(columns) +
            str8(zodiac) + tlv(ext or []))


def obj_head(kind, prof=0, reserved=0):
    return u8(kind) + u8(prof) + u16(reserved)


def obj_body(naif, prof=0):
    return obj_head(0, prof) + i32(naif)


def obj_orbit(naif, point, method, prof=0):
    return obj_head(1, prof) + i32(naif) + u8(point) + u8(method) + u16(0)


def obj_star(name, prof=0):
    return obj_head(2, prof) + str8(name)


def obj_hypo(token, prof=0):
    return obj_head(3, prof) + str8(token)


def obj_elements(epoch, equinox, centre, terms, name, equinox_jd=0.0, prof=0):
    """terms: list of 6 lists (M, a, e, argp, node, incl), each nTerms long."""
    n = len(terms[0])
    assert all(len(t) == n for t in terms)
    b = obj_head(4, prof) + time(*epoch) + u8(equinox) + u8(centre) + u8(n) + u8(0)
    b += f64(equinox_jd)
    for element in terms:
        for c in element:
            b += f64(c)
    return b + str8(name)


def obj_designation(s, prof=0):
    return obj_head(5, prof) + str8(s)


def question(time_bytes, profiles, objects, delta_t=NAN, ext=None,
             delta_t_raw=None):
    b = time_bytes
    b += raw_f64(delta_t_raw) if delta_t_raw else f64(delta_t)
    b += u8(len(profiles)) + b"".join(profiles)
    b += u16(len(objects)) + b"".join(objects)
    return b + tlv(ext or [])


def meta(rows_ok, err=0, source_idx=0, flags=0, corr_applied=0, resolved=-2**31,
         first_failed=0xFFFFFFFF, name="", err_text=""):
    return (i32(rows_ok) + u16(err) + u8(source_idx) + u8(flags) + u8(corr_applied) +
            i32(resolved) + u32(first_failed) + str8(name) + str8(err_text))


def sources(names):
    return u8(len(names)) + b"".join(str8(n) for n in names)


def data_chunk(chunk_index, i_time, rows, total_rows, precision, flags, columns,
               meta_bytes, values):
    """values: per object, per row, per column floats."""
    n_obj = len(values)
    b = (u32(chunk_index) + u32(i_time) + u32(rows) + u32(total_rows) +
         u8(precision) + u8(flags) + u16(n_obj) + u32(columns))
    b += meta_bytes
    pack = f32 if precision == 1 else f64
    for obj in values:
        assert len(obj) == rows
        for row in obj:
            for v in row:
                b += pack(v)
    return b


def error(code, flags=0, retry_ms=0, text="", ext=None):
    return u16(code) + u16(flags) + u32(retry_ms) + str8(text) + tlv(ext or [])


# ---- the capability TLVs of two example servers ------------------------------

def caps_swiss():
    zodiacs = ["fagan-bradley", "lahiri", "raman", "krishnamurti", "user"]
    return [
        (0x0001, u32(0b111111)),                 # all six kinds
        (0x0002, u32(0b11111)),                  # all observers
        (0x0003, u32(0b11) + u32(0b11) + u32(0b1111)),
        # Per-observer correction masks: everything for geocentric,
        # topocentric and body observers; light time and geometric for the
        # heliocentric and barycentric ones.
        (0x0004, u8(5) + u32(0b10011) + u8(7) + u32(0b11111) + u8(0) +
                 u32(0b10011) + u8(3) + u32(0b10011) + u8(5) + u32(0b11111) + u8(1)),
        (0x0005, u32(0b1111) + u32(0b10111)),   # not method 3
        (0x0006, u32(0b1110)),                   # no sigma
        (0x0007, u16(len(zodiacs)) + b"".join(str8(z) for z in zodiacs)),
        (0x0008, u32(0b111)),
        (0x0009, u32(0b011)),                    # UT1, TT
        (0x000A, u16(1) + str8("sepl_18") + time(2378496.5) + time(2524624.5)),
        (0x000C, str8("swiss-2.10.03")),
        (0x000E, u32(10000) + u32(100000)),
        (0x0010, u16(32)),
        (0x0011, u16(2) + str8("cupido") + str8("vulcan")),
        (0x0012, u32(0b11111)),
        (0x0013, f32(5e-05) + f32(0.0001)),      # rates bound

    ]


def caps_prometheia():
    zodiacs = ["fagan-bradley", "lahiri", "user"]
    return [
        (0x0001, u32(0b100011)),                 # body, orbit point, designation
        (0x0002, u32(0b11111)),
        (0x0003, u32(0b11) + u32(0b11) + u32(0b1111)),
        (0x0004, u8(1) + u32(0b11111) + u8(7)),  # every observer, the full mask
        (0x0005, u32(0b1111) + u32(0b11)),       # mean, osculating
        (0x0006, u32(0b0111)),                   # sigma, ayanamsa, light time
        (0x0007, u16(len(zodiacs)) + b"".join(str8(z) for z in zodiacs)),
        (0x0008, u32(0b001)),
        (0x0009, u32(0b111)),                    # UT1, TT, TDB
        (0x000A, u16(1) + str8("DE440") + time(2287184.5) + time(2688976.5)),
        (0x000B, u16(1) + str8("sbdb") + str8("2026-09-16")),
        (0x000C, str8("usno-observed+smh2016")),
        (0x000D, u16(2) + str8("iau2006") + str8("vondrak2011")),
        # maxDegree, reserved, maxSegmentsPerObject, minErrArcsec, kinds,
        # maxSegSpanDays -- the widest span it will fit in one request.
        (0x000F, u8(15) + b"\0\0\0" + u32(4096) + f32(0.0001) + u32(0b000011) +
                 u32(3653)),
        (0x0010, u16(64)),
    ]


# ---- the fixtures -------------------------------------------------------------

J2000 = 2451545.0
DAY_NS = 86400 * 10**9


def fixtures():
    F = []

    def add(name, direction, mtype, expect, note, msg):
        F.append((name, direction, mtype, expect, note, msg))

    geo = profile()
    helio = profile(observer=2)
    topo_sid = profile(observer=1, site=(-122.3, 47.6, 50.0), zodiac="fagan-bradley",
                       sidereal_plane=2, columns=0b0010)
    q_basic = question(grid_block(1, J2000, 0.0, 3600 * 10**9, 24), [geo],
                       [obj_body(10), obj_body(301)])

    # -- accepted -------------------------------------------------------------
    add("hello_min", "c2s", HELLO, "ok", "no token, no caps",
        envelope(HELLO, hello()))
    add("hello_token_caps", "c2s", HELLO, "ok", "token and f32|cancel|lookup|segments caps",
        envelope(HELLO, hello(caps=0b1001101, token="tok en-1")))
    add("hello_future_client", "c2s", HELLO, "ok",
        "a version-9 client: envelope 9, protoMax 9, protoMin 4; the server answers v4",
        envelope(HELLO, hello(proto_max=9, proto_min=4, name="Astrolog 9"), version=9))
    add("welcome_swiss", "s2c", WELCOME, "ok", "astrolog-ephd-like capabilities",
        envelope(WELCOME, welcome(0b10011101, "Swiss Ephemeris 2.10.03 files",
                                  "swiss-2.10.03/sepl_18", caps_swiss())))
    add("welcome_prometheia", "s2c", WELCOME, "ok", "prometheiad-like, with segments",
        envelope(WELCOME, welcome(0b11011101, "Prometheia 0.2, JPL DE440 + SBDB 2026-09-16",
                                  "prom-0.2/de440/sbdb-20260916", caps_prometheia(),
                                  server="prometheiad/0.2")))
    add("request_basic", "c2s", REQUEST, "ok", "Sun and Moon, TT hourly grid, geocentric",
        envelope(REQUEST, delivery() + q_basic, request_id=1))

    # -- A.3 0x0014, corrections by kind (the per-kind drop) -------------------
    # "Absent" needs no fixture of its own: welcome_swiss and
    # welcome_prometheia above carry no 0x0014, which IS the absent case, and
    # a third copy of it would test nothing new.
    #
    # The drop's request-side cases (a single profile shared by a body and an
    # orbit point; the same split across two profiles; an unreferenced
    # profile) are WELL-FORMED REQUESTS in every case. Whether one is refused
    # with ERROR 11 depends on the SERVER'S capabilities, which a standalone
    # message carries none of -- so the verdict cannot live in this set, and
    # the shapes are added as "ok" with the policy stated in the note. The
    # refusal itself is checked in tools/crosstest-prom.sh, where a real
    # WELCOME exists.
    ck_entry = u32(0b01100) + u32(1 << 1) + u8(7)     # helio+bary, orbit point
    add("welcome_corrkind", "s2c", WELCOME, "ok",
        "0x0014 carrying only exceptions: an orbit point from the Sun's "
        "centre or the barycentre honours the full mask where a body cannot",
        envelope(WELCOME, welcome(0b10011101, "Swiss Ephemeris 2.10.03 files",
                                  "swiss-2.10.03/sepl_18",
                                  caps_swiss() + [(0x0014, u8(1) + ck_entry)])))
    add("welcome_corrkind_empty", "s2c", WELCOME, "ok",
        "0x0014 with n = 0: legal, and means exactly what its absence means",
        envelope(WELCOME, welcome(0b10011101, "Swiss Ephemeris 2.10.03 files",
                                  "swiss-2.10.03/sepl_18",
                                  caps_swiss() + [(0x0014, u8(0))])))
    add("welcome_corrkind_unknown_bits", "s2c", WELCOME, "ok",
        "unknown observer and kind bits in 0x0014 are IGNORED, because both "
        "registries are open and a future member must not break an older "
        "client",
        envelope(WELCOME, welcome(0b10011101, "Swiss Ephemeris 2.10.03 files",
                                  "swiss-2.10.03/sepl_18",
                                  caps_swiss() +
                                  [(0x0014, u8(1) + u32(0x80000004) +
                                    u32(0x40000002) + u8(1))])))
    add("welcome_corrkind_reserved", "s2c", WELCOME, "malformed",
        "a 0x0014 mask with a bit above 0x07: reserved, MUST be zero, and "
        "3.1 says reject rather than normalise",
        envelope(WELCOME, welcome(0b10011101, "Swiss Ephemeris 2.10.03 files",
                                  "swiss-2.10.03/sepl_18",
                                  caps_swiss() +
                                  [(0x0014, u8(1) + u32(0b01100) +
                                    u32(1 << 1) + u8(0x09))])))
    ck_prof_shared = profile(observer=2, corrections=7)
    add("request_corrkind_shared_profile", "c2s", REQUEST, "ok",
        "one heliocentric profile at mask 7 referenced by a BODY and an "
        "ORBIT POINT: well formed, and a server whose 0x0014 grants the mask "
        "to the point but not the body refuses it whole with ERROR 11",
        envelope(REQUEST, delivery() +
                 question(grid_block(1, J2000, 0.0, 0, 1), [ck_prof_shared],
                          [obj_body(4), obj_orbit(4, 2, 0)]), request_id=1))
    add("request_corrkind_split_profiles", "c2s", REQUEST, "ok",
        "the same two objects across TWO heliocentric profiles, mask 1 for "
        "the body and mask 7 for the orbit point: the served form of the "
        "case above, and what a client does when kinds disagree",
        envelope(REQUEST, delivery() +
                 question(grid_block(1, J2000, 0.0, 0, 1),
                          [profile(observer=2, corrections=1),
                           profile(observer=2, corrections=7)],
                          [obj_body(4), obj_orbit(4, 2, 0, prof=1)]),
                 request_id=1))
    add("request_corrkind_unreferenced_profile", "c2s", REQUEST, "ok",
        "a second profile no object references: checked against 0x0004 "
        "alone, since nothing is computed from it",
        envelope(REQUEST, delivery() +
                 question(grid_block(1, J2000, 0.0, 0, 1),
                          [profile(observer=0, corrections=7),
                           profile(observer=2, corrections=7)],
                          [obj_body(10)]), request_id=1))
    cast = [obj_body(10), obj_body(301), obj_body(4, prof=1), obj_body(20000001),
            obj_body(20002060), obj_orbit(301, 0, 1), obj_orbit(301, 3, 0, prof=2),
            obj_star("Aldebaran"), obj_hypo("cupido"),
            obj_elements((J2000, 0.0), 0, 0,
                         [[252.8987988, 707550.7341], [0.13744, 0.0], [0.019, 0.0],
                          [322.212069, 1670.056], [47.787931, -1670.056], [7.5, 0.0]],
                         "Vulcan"),
            obj_designation("1P/Halley")]
    add("request_cast_profiles", "c2s", REQUEST, "ok",
        "one cast: three profiles (geo; helio; topocentric Fagan-Bradley on the invariable plane with the ayanamsa column) and all six object kinds",
        envelope(REQUEST, delivery(precision=1, chunk_rows=500) +
                 question(grid_block(1, 2447963.0, 0.5, 0, 1), [geo, helio, topo_sid], cast,
                          delta_t=56.9),
                 request_id=2))
    add("request_deadline", "c2s", REQUEST, "ok",
        "a prefetch window with a 250 ms deadline: advisory, and outside the cache key",
        envelope(REQUEST, delivery(priority=1, chunk_rows=64, deadline_ms=250) +
                 question(grid_block(1, J2000, 0.0, 3600 * 10**9, 24), [geo],
                          [obj_body(301)]), request_id=4))
    add("request_list_ut", "c2s", REQUEST, "ok", "instant list in UT1, server's delta T",
        envelope(REQUEST, delivery() +
                 question(list_block(0, [(2461300.0, 0.5), (2461300.0, 0.25), (2461300.0, 0.5)]),
                          [geo], [obj_body(5)]), request_id=3))
    add("request_backward_grid", "c2s", REQUEST, "ok", "negative step (animation running backwards)",
        envelope(REQUEST, delivery(priority=1) +
                 question(grid_block(1, J2000, 0.0, -DAY_NS, 10), [geo], [obj_body(6)]),
                 request_id=4))
    add("request_user_zodiac", "c2s", REQUEST, "ok", "user ayanamsha anchored at J2000",
        envelope(REQUEST, delivery() +
                 question(grid_block(1, J2000, 0.0, 0, 1),
                          [profile(zodiac="user", anchor=(J2000, 0.0), anchor_ayan=23.85)],
                          [obj_body(10)]), request_id=5))
    add("request_segments", "c2s", REQUEST, "ok", "segments over 30 days, rectangular, 0.1 arcsec target",
        envelope(REQUEST, delivery(representation=1, seg_err=0.1) +
                 question(grid_block(1, J2000, 0.0, DAY_NS, 31), [profile(form=1)],
                          [obj_body(301), obj_body(10)]), request_id=6))
    add("request_pins", "c2s", REQUEST, "ok", "precession model plus critical ephemeris and catalog pins, ascending tags",
        envelope(REQUEST, delivery() +
                 question(grid_block(1, J2000, 0.0, 0, 1), [geo], [obj_body(20000001)],
                          ext=[(0x0003, str8("iau2006")), (0x8001, str8("DE440")),
                               (0x8002, str8("sbdb/2026-09-16"))]), request_id=7))
    m = sources(["JPL DE440", "SBDB 2026-09-16"])
    m += meta(2, source_idx=0, name="Sun", resolved=10, corr_applied=7)
    m += meta(1, err=3, source_idx=1, flags=(1 << 2) | (1 << 3), resolved=20000001,
              first_failed=1, name="Ceres", err_text="outside catalog coverage",
              corr_applied=7)
    add("data_chunk0_meta", "s2c", DATA, "ok",
        "f64, sigma+ayanamsa columns, a partial object with a NaN row",
        envelope(DATA, data_chunk(0, 0, 2, 2, 0, 0b101, 0b0011, m,
                                  [[[280.1, 0.0, 0.983, 1.019, 0.0, 0.0, 0.0, 24.1],
                                    [281.1, 0.0, 0.983, 1.019, 0.0, 0.0, 0.0, 24.1]],
                                   [[45.0, 2.0, 2.1, 0.2, 0.01, 0.001, 0.003, 24.1],
                                    [NAN] * 8]]), request_id=7))
    add("data_chunk1_nometa_f32", "s2c", DATA, "ok", "a later f32 chunk without metadata",
        envelope(DATA, data_chunk(1, 500, 1, 501, 1, 0b001, 0, b"",
                                  [[[12.5, -1.25, 1.5, 0.5, 0.0, 0.0]]]), request_id=2))
    seg = (time(J2000 + 15.0, 0.0) + f64(15.5) + u8(2) + b"\0\0\0" + f32(0.05) +
           f32(1e-7) + f32(0.002) + b"".join(f64(c) for c in
                                [0.001, 0.0005, 0.00001, -0.0021, 0.0001, 0.0, 0.0002, 0.0, 0.0]))
    segd = (u32(0) + u8(0b101) + u8(0) + u16(1) + u16(0) + u16(1) + u32(0) +
            sources(["JPL DE440"]) + meta(1, name="Moon", resolved=301, corr_applied=1) +
            u8(0) + u32(1) + seg)
    add("segdata_moon", "s2c", SEGDATA, "ok", "one degree-2 segment spanning 31 days, no zodiac",
        envelope(SEGDATA, segd, request_id=6))
    ayanseg = (time(J2000 + 15.0, 0.0) + f64(15.5) + u8(1) + b"\0\0\0" + f32(0.01) +
               f64(24.74) + f64(0.00057))
    segd_sid = (u32(0) + u8(0b101) + u8(0) + u16(1) + u16(0) + u16(1) + u32(0) +
                sources(["JPL DE440"]) + meta(1, name="Moon", resolved=301, corr_applied=1) +
                u8(1) + u8(0) + u32(1) + ayanseg + u32(1) + seg)
    add("segdata_sidereal_ayanamsa", "s2c", SEGDATA, "ok",
        "tropical coefficients with the profile's ayanamsa series beside them",
        envelope(SEGDATA, segd_sid, request_id=6))
    def deltat_table(entries):
        b = u32(len(entries))
        for jd, dt in entries:
            b += time(jd, 0.0) + f64(dt)
        return b

    add("request_deltat_table", "c2s", REQUEST, "ok",
        "a UT1 grid with the client's own delta T table (TT instants, ascending)",
        envelope(REQUEST, delivery() +
                 question(grid_block(0, 2415020.5, 0.0, DAY_NS, 365), [geo], [obj_body(301)],
                          ext=[(0x8004, deltat_table([(2415020.5, -2.72), (2451545.0, 63.83),
                                                      (2488070.0, 77.0)]))]),
                 request_id=9))
    add("request_dataset_pin", "c2s", REQUEST, "ok", "a critical datasetId pin",
        envelope(REQUEST, delivery() +
                 question(grid_block(1, J2000, 0.0, 0, 1), [geo], [obj_body(10)],
                          ext=[(0x8003, str8("prometheiad 0.2/de440/sbdb#1a2b3c4d"))]),
                 request_id=10))
    m2 = sources(["Swiss Ephemeris files"])
    m2 += meta(1, source_idx=0, flags=(1 << 5) | (1 << 6), name="Aldebaran",
               corr_applied=6)
    add("data_star_nodistance", "s2c", DATA, "ok",
        "a star without a parallax: noDistance and ratesApprox",
        envelope(DATA, data_chunk(0, 0, 1, 1, 0, 0b101, 0, m2,
                                  [[[69.7, -5.46, 1e9, 0.0, 0.0, 0.0]]]), request_id=11))
    # 3.1: a client tolerates registry values a newer server sends.
    m3 = sources(["Swiss Ephemeris files"])
    m3 += meta(1, source_idx=0, flags=(1 << 7), name="Sun", corr_applied=0x87)
    add("data_meta_unknown_flag", "s2c", DATA, "ok",
        "META with a flag bit and a corrApplied bit past bit 2: tolerated",
        envelope(DATA, data_chunk(0, 0, 1, 1, 0, 0b101, 0, m3,
                                  [[[280.1, 0.0, 0.983, 1.019, 0.0, 0.0]]]), request_id=12))
    add("data_chunk_future_flag", "s2c", DATA, "ok",
        "a chunkFlags bit this version does not define: tolerated",
        envelope(DATA, data_chunk(0, 0, 1, 1, 0, 0b1000101, 0, m3,
                                  [[[280.1, 0.0, 0.983, 1.019, 0.0, 0.0]]]), request_id=12))
    add("error_unregistered_code", "s2c", ERROR, "ok",
        "an ERROR code past the registry: still an error, still retryable",
        envelope(ERROR, error(200, flags=0b10, text="something new went wrong"), request_id=13))
    add("error_rate_limited", "s2c", ERROR, "ok", "ERROR 6, retryable, retry in 1500 ms",
        envelope(ERROR, error(6, flags=0b10, retry_ms=1500,
                              text="rate limited: 10000 cells a second"), request_id=3))
    add("error_version_closing", "s2c", ERROR, "ok", "ERROR 8, closing",
        envelope(ERROR, error(8, flags=0b01, text="this client speaks 9..9; this server 4..4")))
    add("error_v3_legacy", "s2c", ERROR, "ok",
        "ERROR 8 to a version-3 client in that client's own layout (envelope version 3)",
        envelope(ERROR, u32(0) + i32(8) + b"this server needs protocol 4 -- update Astrolog\0",
                 version=3))
    add("cancel", "c2s", CANCEL, "ok", "cancel request 4", envelope(CANCEL, b"", request_id=4))
    add("ping", "c2s", PING, "ok", "", envelope(PING, b""))
    add("pong", "s2c", PONG, "ok", "", envelope(PONG, b""))
    def lookup(max_matches, flags, queries, ext=None):
        b = u16(max_matches) + u8(flags) + u8(len(queries))
        b += b"".join(str8(q) for q in queries)
        return b + tlv(ext or [])

    add("lookup_prefix", "c2s", LOOKUP, "ok", "prefix, hypotheticals and stars included",
        envelope(LOOKUP, lookup(8, 0b111, ["Lilith"]), request_id=8))
    add("lookup_batch", "c2s", LOOKUP, "ok",
        "four names in one message; maxMatches is the budget for the WHOLE answer",
        envelope(LOOKUP, lookup(16, 0, ["Ceres", "Chiron", "Aldebaran", "1P/Halley"]),
                 request_id=9))
    def match(quality, source_idx, obj_bytes, canonical, designation="",
              valid=((0.0, 0.0), (0.0, 0.0)), match_len=None):
        body = (obj_bytes + str8(canonical) + str8(designation) +
                time(*valid[0]) + time(*valid[1]))
        n = len(body) if match_len is None else match_len
        return u8(quality) + u8(source_idx) + u16(n) + body

    def lookup_result(flags, source_names, per_query):
        """per_query: a list of lists of encoded MATCHes."""
        b = u8(len(per_query)) + u8(flags) + sources(source_names)
        for matches in per_query:
            b += u16(len(matches)) + b"".join(matches)
        return b

    lr = lookup_result(0, ["SBDB 2026-09-16", "Swiss Ephemeris"], [[
        match(0, 0, obj_body(20001181), "1181 Lilith", "1181"),
        match(1, 1, obj_orbit(301, 3, 0), "Moon mean apogee"),
        match(1, 1, obj_hypo("waldemath"), "Waldemath"),
    ]])
    add("lookup_result_lilith", "s2c", LOOKUP_RESULT, "ok",
        "three kinds answer one name", envelope(LOOKUP_RESULT, lr, request_id=8))
    lr_batch = lookup_result(0b1, ["Swiss Ephemeris files"], [
        [match(0, 0, obj_body(20000001), "Ceres", "1")],
        [match(0, 0, obj_body(20002060), "Chiron", "2060")],
        [],                                    # no match for this query
        [match(2, 0, obj_star("Aldebaran"), "Aldebaran")],
    ])
    add("lookup_result_batch", "s2c", LOOKUP_RESULT, "ok",
        "four queries answered in order, one with no match, the message budget spent",
        envelope(LOOKUP_RESULT, lr_batch, request_id=9))
    # 3.1 and 3.4: matchLen is what lets a client skip a kind added after it
    # shipped and keep the matches on either side of it.
    lr2 = lookup_result(0, ["Swiss Ephemeris files"], [[
        match(0, 0, obj_body(10), "Sun"),
        match(1, 0, obj_head(7) + i32(12345) + str8("something new"), "New Thing"),
        match(1, 0, obj_body(301), "Moon"),
    ]])
    add("lookup_result_future_kind", "s2c", LOOKUP_RESULT, "ok",
        "a match of an unregistered kind, skipped by its matchLen, between two readable ones",
        envelope(LOOKUP_RESULT, lr2, request_id=8))
    lr3 = lookup_result(0, ["Swiss Ephemeris files"],
                        [[match(0, 0, obj_body(10), "Sun", match_len=4)]])
    add("lookup_result_bad_matchlen", "s2c", LOOKUP_RESULT, "malformed",
        "matchLen disagrees with a match the reader can read",
        envelope(LOOKUP_RESULT, lr3, request_id=8))

    # -- refused: malformed -------------------------------------------------------
    def bad(name, note, msg, expect="malformed", mtype=REQUEST, direction="c2s"):
        add(name, direction, mtype, expect, note, msg)

    bad("env_bad_magic", "magic 0x1EF1", envelope(PING, b"", magic=0x1EF1), mtype=PING)
    bad("env_reserved_nonzero", "envelope reserved u16 = 1", envelope(PING, b"", reserved=1), mtype=PING)
    bad("env_flag_bit", "envelope flag bit 3", envelope(PING, b"", flags=0b1000), mtype=PING)
    bad("hello_min_gt_max", "protoMin 5 > protoMax 4",
        envelope(HELLO, hello(proto_max=4, proto_min=5)), mtype=HELLO)
    bad("ping_payload", "PING with a payload", envelope(PING, b"\0"), mtype=PING)
    bad("trailing_bytes", "one byte after the TLV area",
        envelope(REQUEST, delivery() + q_basic + b"\0", request_id=1))
    bad("tlv_unsorted", "tags 0x8001 before 0x0003",
        envelope(REQUEST, delivery() + question(grid_block(1, J2000, 0.0, 0, 1), [geo], [obj_body(10)])[:-2] +
                 tlv([(0x8001, str8("DE440")), (0x0003, str8("iau2006"))], sort=False), request_id=1))
    bad("tlv_duplicate", "tag 0x0003 twice",
        envelope(REQUEST, delivery() + question(grid_block(1, J2000, 0.0, 0, 1), [geo], [obj_body(10)])[:-2] +
                 tlv([(0x0003, str8("iau2006")), (0x0003, str8("iau2006"))], sort=False), request_id=1))
    q_len_bad = question(grid_block(1, J2000, 0.0, 0, 1), [geo], [obj_body(10)])[:-2] + u16(4)
    bad("tlv_total_mismatch", "TLV totalLen 4 with no entries",
        envelope(REQUEST, delivery() + q_len_bad, request_id=1))
    bad("str8_bad_utf8", "zodiac bytes 0xC3 0x28",
        envelope(REQUEST, delivery() + question(grid_block(1, J2000, 0.0, 0, 1),
                                                [profile(zodiac=b"\xc3\x28")], [obj_body(10)]),
                 request_id=1))
    bad("str8_control", "star name with a newline",
        envelope(REQUEST, delivery() + question(grid_block(1, J2000, 0.0, 0, 1), [geo],
                                                [obj_star("Alde\nbaran")]), request_id=1))
    bad("deltat_signalling_nan", "delta T 0x7FF0000000000001",
        envelope(REQUEST, delivery() + question(grid_block(1, J2000, 0.0, 0, 1), [geo], [obj_body(10)],
                                                delta_t_raw="010000000000f07f"), request_id=1))
    bad("site_nan", "canonical NaN in a site field (allowed only in delta T)",
        envelope(REQUEST, delivery() + question(grid_block(1, J2000, 0.0, 0, 1),
                                                [profile(observer=1, site=(NAN, 0.0, 0.0))],
                                                [obj_body(10)]), request_id=1))
    bad("ntime_zero", "grid nTime 0",
        envelope(REQUEST, delivery() + question(grid_block(1, J2000, 0.0, 0, 0), [geo], [obj_body(10)]),
                 request_id=1))
    bad("step_zero_many", "stepNs 0 with nTime 2",
        envelope(REQUEST, delivery() + question(grid_block(1, J2000, 0.0, 0, 2), [geo], [obj_body(10)]),
                 request_id=1))
    bad("step_nonzero_one", "stepNs 1 day with nTime 1",
        envelope(REQUEST, delivery() + question(grid_block(1, J2000, 0.0, DAY_NS, 1), [geo], [obj_body(10)]),
                 request_id=1))
    bad("step_overflow", "(nTime-1) x |stepNs| overflows i64",
        envelope(REQUEST, delivery() + question(grid_block(1, J2000, 0.0, 2**62, 3), [geo], [obj_body(10)]),
                 request_id=1))
    bad("time_out_of_range", "jd1 = 2e8",
        envelope(REQUEST, delivery() + question(grid_block(1, 2e8, 0.0, 0, 1), [geo], [obj_body(10)]),
                 request_id=1))
    bad("profiles_zero", "nProfiles 0",
        envelope(REQUEST, delivery() + question(grid_block(1, J2000, 0.0, 0, 1), [], [obj_body(10)]),
                 request_id=1))
    bad("objects_zero", "nObj 0",
        envelope(REQUEST, delivery() + question(grid_block(1, J2000, 0.0, 0, 1), [geo], []), request_id=1))
    bad("profile_index_range", "object names profile 1 of 1",
        envelope(REQUEST, delivery() + question(grid_block(1, J2000, 0.0, 0, 1), [geo], [obj_body(10, prof=1)]),
                 request_id=1))
    bad("profile_reserved", "profile reserved byte 1",
        envelope(REQUEST, delivery() + question(grid_block(1, J2000, 0.0, 0, 1), [profile(reserved=1)],
                                                [obj_body(10)]), request_id=1))
    bad("object_reserved", "object head reserved u16 = 1",
        envelope(REQUEST, delivery() + question(grid_block(1, J2000, 0.0, 0, 1), [geo],
                                                [u8(0) + u8(0) + u16(1) + i32(10)]), request_id=1))
    bad("observer_body_unused", "observerBody 5 with a geocentric observer",
        envelope(REQUEST, delivery() + question(grid_block(1, J2000, 0.0, 0, 1), [profile(observer_body=5)],
                                                [obj_body(10)]), request_id=1))
    bad("site_unused", "a site with a heliocentric observer",
        envelope(REQUEST, delivery() + question(grid_block(1, J2000, 0.0, 0, 1),
                                                [profile(observer=2, site=(10.0, 20.0, 0.0))],
                                                [obj_body(10)]), request_id=1))
    bad("site_lat_range", "site latitude 91",
        envelope(REQUEST, delivery() + question(grid_block(1, J2000, 0.0, 0, 1),
                                                [profile(observer=1, site=(0.0, 91.0, 0.0))],
                                                [obj_body(10)]), request_id=1))
    bad("tropical_with_plane", "zodiac \"\" with siderealPlane 1",
        envelope(REQUEST, delivery() + question(grid_block(1, J2000, 0.0, 0, 1),
                                                [profile(sidereal_plane=1)], [obj_body(10)]), request_id=1))
    bad("user_zodiac_no_anchor", "zodiac user with a zero anchor epoch",
        envelope(REQUEST, delivery() + question(grid_block(1, J2000, 0.0, 0, 1),
                                                [profile(zodiac="user")], [obj_body(10)]), request_id=1))
    bad("anchor_unused", "anchor set with a named zodiac",
        envelope(REQUEST, delivery() + question(grid_block(1, J2000, 0.0, 0, 1),
                                                [profile(zodiac="lahiri", anchor=(J2000, 0.0))],
                                                [obj_body(10)]), request_id=1))
    bad("speeds_not_bool", "speeds = 2",
        envelope(REQUEST, delivery() + question(grid_block(1, J2000, 0.0, 0, 1), [profile(speeds=2)],
                                                [obj_body(10)]), request_id=1))
    bad("elements_no_terms", "elements with nTerms 0",
        envelope(REQUEST, delivery() + question(grid_block(1, J2000, 0.0, 0, 1), [geo],
                                                [obj_head(4) + time(J2000) + u8(0) + u8(0) + u8(0) + u8(0) +
                                                 f64(0.0) + str8("x")]), request_id=1))
    bad("equinox_jd_unused", "equinoxJd set with equinox J2000",
        envelope(REQUEST, delivery() + question(grid_block(1, J2000, 0.0, 0, 1), [geo],
                                                [obj_elements((J2000, 0.0), 0, 0, [[0.0]] * 6, "x",
                                                              equinox_jd=J2000)]), request_id=1))
    bad("seg_err_with_samples", "segTargetErrArcsec 1 with representation 0",
        envelope(REQUEST, delivery(seg_err=1.0) + q_basic, request_id=1))
    bad("max_degree_with_samples", "maxDegreeHint 8 with representation 0",
        envelope(REQUEST, delivery(max_degree=8) + q_basic, request_id=1))
    bad("deltat_table_and_value", "a delta T table and a finite deltaTSec",
        envelope(REQUEST, delivery() +
                 question(grid_block(0, J2000, 0.0, DAY_NS, 2), [geo], [obj_body(10)], delta_t=69.0,
                          ext=[(0x8004, deltat_table([(J2000, 69.0), (J2000 + 10, 69.1)]))]),
                 request_id=1))
    bad("deltat_table_unsorted", "a delta T table whose instants descend",
        envelope(REQUEST, delivery() +
                 question(grid_block(0, J2000, 0.0, DAY_NS, 2), [geo], [obj_body(10)],
                          ext=[(0x8004, deltat_table([(J2000 + 10, 69.1), (J2000, 69.0)]))]),
                 request_id=1))
    bad("deltat_table_one_entry", "a delta T table of one entry",
        envelope(REQUEST, delivery() +
                 question(grid_block(0, J2000, 0.0, DAY_NS, 2), [geo], [obj_body(10)],
                          ext=[(0x8004, deltat_table([(J2000, 69.0)]))]), request_id=1))
    bad("sidereal_on_the_equator", "a sidereal zodiac with plane = equator",
        envelope(REQUEST, delivery() +
                 question(grid_block(1, J2000, 0.0, 0, 1),
                          [profile(plane=1, zodiac="lahiri")], [obj_body(10)]), request_id=1))
    bad("request_id_zero", "a REQUEST with requestId 0",
        envelope(REQUEST, delivery() + q_basic, request_id=0))
    bad("hello_request_id", "a HELLO with requestId 1",
        envelope(HELLO, hello(), request_id=1), mtype=HELLO)
    bad("lookup_id_zero", "a LOOKUP with requestId 0",
        envelope(LOOKUP, lookup(4, 0, ["Ceres"]), request_id=0), mtype=LOOKUP)
    bad("lookup_no_queries", "a LOOKUP carrying no query at all",
        envelope(LOOKUP, u16(4) + u8(0) + u8(0) + tlv([]), request_id=8), mtype=LOOKUP)
    bad("lookup_budget_zero", "maxMatches 0: a budget of nothing",
        envelope(LOOKUP, lookup(0, 0, ["Ceres"]), request_id=8), mtype=LOOKUP)
    bad("lookup_result_no_queries", "a LOOKUP_RESULT answering no query",
        envelope(LOOKUP_RESULT, u8(0) + u8(0) + sources(["Swiss Ephemeris files"]),
                 request_id=8),
        mtype=LOOKUP_RESULT, direction="s2c")
    bad("data_chunk0_no_meta", "DATA chunk 0 with the meta flag clear",
        envelope(DATA, data_chunk(0, 0, 1, 1, 0, 0b001, 0, b"",
                                  [[[280.1, 0.0, 0.983, 1.019, 0.0, 0.0]]]), request_id=1),
        mtype=DATA, direction="s2c")
    bad("segdata_chunk0_no_meta", "SEGDATA chunk 0 with the meta flag clear",
        envelope(SEGDATA, u32(0) + u8(0b001) + u8(0) + u16(1) + u16(0) + u16(1) + u32(0) + u32(0),
                 request_id=6), mtype=SEGDATA, direction="s2c")
    bad("lookup_match_profile", "a LOOKUP_RESULT match whose object names profile 1",
        envelope(LOOKUP_RESULT,
                 u16(1) + u8(0) + sources(["Swiss Ephemeris files"]) +
                 match(0, 0, obj_body(10, prof=1), "Sun"), request_id=8),
        mtype=LOOKUP_RESULT, direction="s2c")

    # -- refused: unsupported (registry values or capabilities not implemented) -----
    bad("unknown_critical_tag", "critical REQUEST tag 0x8FFF",
        envelope(REQUEST, delivery() + question(grid_block(1, J2000, 0.0, 0, 1), [geo], [obj_body(10)],
                                                ext=[(0x8FFF, b"")]), request_id=1),
        expect="unsupported")
    bad("observer_unregistered", "observer 9",
        envelope(REQUEST, delivery() + question(grid_block(1, J2000, 0.0, 0, 1), [profile(observer=9)],
                                                [obj_body(10)]), request_id=1),
        expect="unsupported")
    bad("correction_bit_unregistered", "corrections bit 8",
        envelope(REQUEST, delivery() + question(grid_block(1, J2000, 0.0, 0, 1),
                                                [profile(corrections=15)], [obj_body(10)]), request_id=1),
        expect="unsupported")
    bad("kind_unregistered", "object kind 6",
        envelope(REQUEST, delivery() + question(grid_block(1, J2000, 0.0, 0, 1), [geo],
                                                [obj_head(6) + i32(10)]), request_id=1),
        expect="unsupported")
    bad("segments_spherical", "segments with form 0",
        envelope(REQUEST, delivery(representation=1, seg_err=0.1) +
                 question(grid_block(1, J2000, 0.0, DAY_NS, 2), [geo], [obj_body(301)]), request_id=1),
        expect="unsupported")
    bad("segments_list", "segments over an instant list",
        envelope(REQUEST, delivery(representation=1, seg_err=0.1) +
                 question(list_block(1, [(J2000, 0.0), (J2000, 1.0)]), [profile(form=1)],
                          [obj_body(301)]), request_id=1),
        expect="unsupported")
    bad("segments_columns", "segments with the ayanamsa column",
        envelope(REQUEST, delivery(representation=1, seg_err=0.1) +
                 question(grid_block(1, J2000, 0.0, DAY_NS, 2), [profile(form=1, columns=0b10)],
                          [obj_body(301)]), request_id=1),
        expect="unsupported")
    bad("elements_centre_unregistered", "elements centre 2 (A.21 has 0 and 1)",
        envelope(REQUEST, delivery() + question(grid_block(1, J2000, 0.0, 0, 1), [geo],
                                                [obj_elements((J2000, 0.0), 0, 2, [[0.0]] * 6, "x")]),
                 request_id=1), expect="unsupported")
    bad("zodiac_unregistered", "zodiac token \"martian\"",
        envelope(REQUEST, delivery() + question(grid_block(1, J2000, 0.0, 0, 1),
                                                [profile(zodiac="martian")], [obj_body(10)]), request_id=1),
        expect="unsupported")
    return F


def hexlines(b):
    h = b.hex()
    return "\n".join(h[i:i + 64] for i in range(0, len(h), 64)) + "\n"


def main():
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    out = os.path.join(root, "ephsrv", "conformance")
    check = "--check" in sys.argv[1:]
    files = {}
    rows = []
    names = set()
    digest = hashlib.sha256()
    for name, direction, mtype, expect, note, msg in fixtures():
        assert name not in names, name
        names.add(name)
        # Every fixture's envelope length must match its payload.
        assert struct.unpack("<I", msg[12:16])[0] == len(msg) - 16, name
        files[name + ".hex"] = hexlines(msg)
        rows.append("\t".join([name + ".hex", direction, str(mtype), expect, note]))
        # 3.10: the FILE's bytes as committed -- hex digits, line breaks and
        # the trailing newline -- not the message they decode to. Nobody has
        # to decode anything to check a set, and a file rewritten with
        # different formatting, a semantic no-op, still changes the digest.
        digest.update(files[name + ".hex"].encode("ascii"))
    # The set's identity, over every fixture file in manifest order: a reader
    # can say "I have a consistent set" rather than inferring it from
    # timestamps, and a half-written directory is visibly half-written.
    header = ["# file\tdirection\ttype\texpect\tnote",
              "# set-sha256 %s" % digest.hexdigest()]
    files["MANIFEST.tsv"] = "\n".join(header + rows) + "\n"
    if check:
        bad = [f for f, text in files.items()
               if not os.path.exists(os.path.join(out, f)) or
               open(os.path.join(out, f)).read() != text]
        extra = [f for f in (os.listdir(out) if os.path.isdir(out) else []) if f not in files]
        if bad or extra:
            print("ephproto4 fixtures out of date: %s" % ", ".join(sorted(bad + extra)))
            return 1
        print("ephproto4 fixtures: %d files current" % len(files))
        return 0
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):
        if f not in files:
            os.remove(os.path.join(out, f))
    # The manifest LAST, and only once every fixture it names is on disk: it
    # is what a consumer reads the set through, so its mtime is the set's and
    # its digest covers what was written (a reader that caught the directory
    # mid-regeneration otherwise saw new fixtures beside an old manifest).
    for f, text in files.items():
        if f == "MANIFEST.tsv":
            continue
        with open(os.path.join(out, f), "w", newline="\n") as fh:
            fh.write(text)
    with open(os.path.join(out, "MANIFEST.tsv"), "w", newline="\n") as fh:
        fh.write(files["MANIFEST.tsv"])
    print("ephproto4 fixtures: wrote %d files to %s" % (len(files), os.path.relpath(out, root)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
