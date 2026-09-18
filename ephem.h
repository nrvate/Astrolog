/*
** Astrolog (Version 8.00) File: ephem.h
**
** IMPORTANT NOTICE: Astrolog and all chart display routines and anything
** not enumerated below used in this program are Copyright (C) 1991-2026 by
** Walter D. Pullen (Astara@msn.com, http://www.astrolog.org/astrolog.htm).
** Permission is granted to freely use, modify, and distribute these
** routines provided these credits and notices remain unmodified with any
** altered or distributed versions of the program.
**
** The main ephemeris databases and calculation routines are from the
** library SWISS EPHEMERIS and are programmed and copyright 1997-2008 by
** Astrodienst AG. Use of that source code is subject to license for Swiss
** Ephemeris Free Edition at https://www.astro.com/swisseph/swephinfo_e.htm.
** This copyright notice must not be changed or removed by any user of this
** program.
**
** Additional ephemeris databases and formulas are from the calculation
** routines in the program PLACALC and are programmed and Copyright (C)
** 1989,1991,1993 by Astrodienst AG and Alois Treindl (alois@astro.ch). The
** use of that source code is subject to regulations made by Astrodienst
** Zurich, and the code is not in the public domain. This copyright notice
** must not be changed or removed by any user of this program.
**
** More formally: This program is free software; you can redistribute it
** and/or modify it under the terms of the GNU General Public License as
** published by the Free Software Foundation; either version 2 of the
** License, or (at your option) any later version. This program is
** distributed in the hope that it will be useful and inspiring, but
** WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
** General Public License for more details, a copy of which is in the
** LICENSE.HTM file included with Astrolog, and at http://www.gnu.org
*/

// The ephemeris source registry: EPHEMERIS_PLUGINS_PLAN.md section 4, in
// code. A source is anything that answers position questions -- a local
// library over data files, formulas, or a remote service. The host casts
// through this registry and its fallback chain; each source declares what
// it can do, and a source that cannot do a thing says so for that object
// while the host asks the next source in the chain.
//
// Every question is an EPHQUERY, the host-side form of the version 4
// question block (EPHEMERIS_PLUGINS_PLAN.md 3.4). One deviation from
// section 4.1, with a reason: there the question type is "declared in
// ephproto.h, so the wire and the plugins cannot drift apart", but
// ephsrv/ephproto.h is locked byte-identical for phases 3-7, so the type
// lives here instead. The shape still follows the question block -- one
// instant, one profile's worth of options per object, per-object results
// -- so the phase 6 remote adapter translates it to the wire directly,
// and the per-object error codes below are the protocol's A.17 values,
// pinned to the codec's own constants by the test suite.
//
// This header is included by astrolog.h just before extern.h, so the
// types are visible wherever Astrolog's own are; it includes nothing.

#ifndef _EPHEM_H
#define _EPHEM_H

// Per-object failure codes: the protocol's A.17 (section 6), so a local
// row failure and a remote one read the same to the host. The test suite
// checks these against ephproto.h's codec constants by name.

#define ephErrNone             0  // The row answered.
#define ephErrUnknownBody      1  // Unknown body or name.
#define ephErrUnsupported      2  // This source cannot do this object.
#define ephErrOutsideCover     3  // Outside the data's time coverage.
#define ephErrDataUnavailable  4  // Data unavailable, e.g. a missing file.
#define ephErrPointUndefined   5  // Point undefined for this orbit.
#define ephErrAmbiguous        6  // Ambiguous name.
#define ephErrNumerical        7  // Numerical failure.
#define ephErrInternal         8  // Internal.

// Source state, for a status line that does not block a cast. A local
// source is esReady or esFailed; esConnecting is the remote adapter's.

#define esReady      0
#define esConnecting 1
#define esFailed     2

// Parameter kinds (section 4.3). A dialog masks epkToken; a Browse button
// is offered for epkPath and epkFile.

#define epkText  0
#define epkPath  1
#define epkFile  2
#define epkUrl   3
#define epkToken 4

// One declared setting of one source (section 4.3). All parameters of all
// sources share one generated index space, and their values will live in
// us.rgszEphParam[] when selection re-plumbs in phase 4 -- until then the
// table is data only, and the built-in local sources run on their defaults.

typedef struct _EphParam {
  CONST char *szKey;      // "url" -- the command-line key is source.key.
  CONST char *szLabel;    // "Server Address" -- dialogs.
  int nKind;              // An epk* value.
  CONST char *szDefault;  // "" means the source's own default.
} EPHPARAM;

// One row of the shared parameter space: the source that owns the
// parameter, and its declaration. The enum (cEphParam with it) and the
// rows themselves come from ephparam.h, generated by
// tools/gen-eph-params.py and diffed by make check; this type and the
// table's extern are the hand-written half, because they name EPHPARAM,
// which this header defines -- and astrolog.h includes ephparam.h before
// this header, because US sizes us.rgszEphParam[] with cEphParam.
//
// The rows are in the enum's order, so rgephparam[epServerUrl].ep is the
// url parameter's own EPHPARAM. A source definition hands its slice of
// this table to its rgParam (ephswiss.cpp does, for jpl), so the
// dialogs' rows and the command line read the same declaration.

typedef struct _EphParamRow {
  CONST char *szSrc;  // The owning source's key, "jpl".
  EPHPARAM ep;        // The declared setting (section 4.3).
} EPHPARAMROW;

extern CONST EPHPARAMROW rgephparam[cEphParam];

// A source's parameters are reached by its key, with CEphParamOfSrc()
// and IepOfSrc() -- declared in extern.h with every other function,
// since the P(()) macro is not in scope here. That pair is the ONLY way
// to ask, so this generated table is the only answer.

// What a source advertises: the WELCOME capability model (section 3.4),
// at the granularity the host casts at. Advertised is promised -- a bit
// whose behaviour no test exercises stays dark (section 3.9a).

typedef struct _EphCaps {
  flag fBody;        // Whole bodies at an instant.
  flag fOrbitPoint;  // Nodes and apsides of an orbit.
  flag fStar;        // Fixed stars from a catalogue.
  flag fSpeeds;      // The three rate columns are real rates.
  flag fLegacyCast;  // Matrix and None: CastChart()'s legacy hook answers;
                     // the source itself computes nothing.
} EPHCAPS;

// One match of a name lookup (section 3.4 LOOKUP). A match whose kind a
// client cannot read is skipped by its own length on the wire; here a
// count of 0 from NLookup is the whole story until phase 6.

typedef struct _EphMatch {
  int nQuality;  // 0 canonical name, 1 alias or designation, 2 prefix.
  int nNative;   // The source's own id for the body, ephNativeNone if the
                 // match is for a kind this header does not carry.
} EPHMATCH;

#define ephNativeNone (-1)

// One object's answer. The six reals are the version 4 answer columns in
// the protocol's order (3.4): ecliptic longitude and latitude (deg),
// distance (AU), then longitude rate, latitude rate and distance rate.
// FSwissPlanet()'s six output arguments carry the same six in ITS order
// (longitude, latitude, longitude rate, distance, latitude rate, distance
// rate) -- FEphRead() hands them back in that order, so a caller reading
// through the host swaps nothing, and the one transposition lives here.

typedef struct _EphRow {
  real rg[6];      // The six columns, as above.
  int nErr;        // An ephErr* value; ephErrNone when the row answered.
  int nNativeRes;  // The native id actually computed, ephNativeNone when
                   // not applicable.
  flag fApprox;    // The body answered is not the body asked.
  CONST char *szSrc;  // Key of the source that answered, NULL if none.
} EPHROW;

// One cast's question: the host-side form of the version 4 question block
// (section 3.4). Phase 3 keeps it minimal but real: one instant per query
// (the cast's t, as JulianDayFromTime() makes it), the object list with
// their Astrolog indices, and per-object results. The one-instant shape
// reaches "one pass per cast, submit once, collect once" by submitting
// one query per instant list entry; the remote sources of phase 6 want
// instants in batch, which is the row dimension of FRead().
//
// rgisrc holds, per object, the registry index of the source that
// answered it, ephSrcNone while the object is still open. The fallback
// walk (FEphSubmitChain) asks each source in the chain for the objects
// still open; a source answers what it can and the host moves a failed
// object on to the next source, never asking a source twice for an
// object it has already failed this cast.

typedef struct _EphQuery {
  real rJD;             // The instant in UT1, as JulianDayFromTime() makes
                        // it -- NOT TT. A source converts if it needs TT:
                        // the Swiss family hands this straight to
                        // FSwissPlanet(), which adds Delta-T itself, and
                        // the Prometheia source declares kTimeUT1 with it.
                        // This said "in TT" until 2026-09-18, which was
                        // wrong and is the sort of wrong that a new
                        // source implements faithfully -- ephprom.cpp got
                        // it right by reading the callers instead.
  int cobj;             // Objects in the query.
  int rgobj[objMax];    // The Astrolog object index of each object.
  int rgcent[objMax];   // The orbit's centre: FSwissPlanet()'s indCent.
  int rgnNative[objMax];// Host-only native hint, 0 for "the source decides".
                        // Never on the wire: the local Swiss source uses it
                        // to make exactly today's call (section 4.1).
  int rgisrc[objMax];   // Registry index of the source that answered, or
                        // ephSrcNone.
  char *rgszName[objMax];
                        // A fixed star's resolved Swiss Ephemeris name, or
                        // NULL for a body. Mutable on purpose: the entry
                        // point rewrites it to the star's canonical form,
                        // which is the name the callers display. A star
                        // row carries the entry point's own six -- which
                        // are already the answer columns in the
                        // protocol's order -- because the star callers'
                        // post-processing is its own; FSwissPlanet()'s
                        // convention re-associates the sidereal offset,
                        // which can move a bit.
  EPHROW rgrow[objMax]; // Per-object results, in object order.
} EPHQUERY;

#define ephSrcNone (-1)

// A source (section 4.1). All the pointers are non-NULL: a source that
// has nothing to do in a slot points at a no-op, so the host never tests
// a function pointer. Local sources compute synchronously inside FSubmit;
// the "submit once, collect once" shape of section 4.1 is what the remote
// sources need in phase 6, and FRead()'s iRow is their row dimension.

typedef struct _EphSrcDef {
  CONST char *szKey;   // "swiss" -- the CLI and settings key.
  CONST char *szName;  // "Swiss Ephemeris files" -- dialogs.
  CONST char *szDesc;  // One line for the dialog.
  // A source's PARAMETERS are not here. They are rows of the generated
  // table (ephparam.h), owned by the source's key, and reached with
  // CEphParamOfSrc()/IepOfSrc() below. This struct carried a
  // CONST EPHPARAM *rgParam and a count until 2026-09-18, and nothing
  // anywhere read either: -bP and both dialogs go to the generated
  // table. So the copy the Prometheia source handed it was free to
  // drift, and had -- different label AND different kind, which is what
  // decides whether a dialog row offers a Browse button. A field that
  // nothing reads cannot be kept honest, so it is gone rather than
  // audited.
  flag (*FAvailable)(char *szWhy, int cch);  // Compiled, files, transport.
  void (*GetCaps)(EPHCAPS *pcaps);           // The capability model.
  int (*State)(char *sz, int cch);           // An es* value plus text.
  void (*Start)(void);   // Local: open; remote: background connect.
  void (*Stop)(void);
  flag (*FSubmit)(EPHQUERY *pq);  // Non-blocking; local ones compute here.
  flag (*FRead)(CONST EPHQUERY *pq, int iObj, int iRow, EPHROW *prow);
  void (*Hint)(CONST EPHQUERY *pq);  // The next window, for prefetch.
  int (*NLookup)(CONST char *sz, EPHMATCH *rgm, int cMax);
} EPHSRCDEF;

// The built-in sources (section 4.2). swiss, moshier and jpl are one
// shared implementation in ephswiss.cpp differing only in the SEFLG
// ephemeris bit; matrix and none keep their legacy cast. The registry
// itself is a table of pointers to these, in fallback quality order
// (section 4.2), defined in ephem.cpp.

extern EPHSRCDEF ephsrcSwiss;
extern EPHSRCDEF ephsrcMoshier;
extern EPHSRCDEF ephsrcJpl;
extern EPHSRCDEF ephsrcMatrix;
extern EPHSRCDEF ephsrcServer;
extern EPHSRCDEF ephsrcNone;


// A REMOTE SOURCE'S TRANSPORT (section 4.2, phase 6).
//
// The server source is one plugin in every build. What differs between
// builds is only how bytes reach the server: a QWebSocket under Qt,
// WinHTTP's WebSocket on Win32, the socket client of
// ephsrv/eph_wsclient.cpp for the console. So a transport is a table a
// backend REGISTERS, rather than an "#ifdef" inside the plugin --
// ephserver.cpp then compiles identically everywhere and asks whatever
// registered itself.
//
// With nothing registered the source is simply unavailable, carrying
// that as its reason. That is the same shape an uncompiled plugin or a
// missing data file already has, so no part of the registry, the chain
// walk or the dialog has to know which builds have a transport.

typedef struct _EphTrans {
  CONST char *szName;   // "Qt WebSocket" -- the dialog's status line.
  flag (*FAvail)(char *szWhy, int cch);
  int  (*State)(char *sz, int cch);   // esReady / esConnecting / esFailed.
  void (*Start)(void);  // Begin connecting; never blocks.
  void (*Stop)(void);
  flag (*FSubmit)(CONST EPHQUERY *pq);
  flag (*FRead)(CONST EPHQUERY *pq, int iObj, EPHROW *prow);
} EPHTRANS;

// The Prometheia source (phase 7) is compiled in only when PROMETHEIA is
// defined -- the makefiles' pkg-config detection -- and sits in the table
// before none, so the five indexes above never move.
#ifdef PROMETHEIA
extern EPHSRCDEF ephsrcPrometheia;
#define cEphSrcPrometheia 1
#else
#define cEphSrcPrometheia 0
#endif

// The server source (phase 6) sits between the local sources and none.
// Its place in the table is nearly cosmetic -- the default chain is
// built from the documented quality order of the LOCAL sources, and a
// remote one is only ever in a chain because the user named it -- but it
// goes before none, because a chain that reaches none is over.
#define cEphSrcBuiltIn (6 + cEphSrcPrometheia)

// The registry order, the fallback chain's quality order (section 4.2:
// swiss, jpl, moshier, matrix), none last: it serves nothing, and a chain
// that reaches it answers "no" for every object.

extern EPHSRCDEF * CONST rgephsrc[cEphSrcBuiltIn];

#endif // _EPHEM_H
