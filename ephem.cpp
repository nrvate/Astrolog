/*
** Astrolog (Version 8.00) File: ephem.cpp
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

// The ephemeris source registry: the table of compiled-in sources, the
// accessors over it, the question type's host-side helpers, and the
// fallback chain's walk. EPHEMERIS_PLUGINS_PLAN.md section 4. Phase 3's
// chain is derived from the existing selection fields (fEphemFiles,
// nSwissEph, fMatrixPla) and holds one live source; the walk itself takes
// any chain, which is what the multi-source behavior of phase 6 plugs
// into. Under today's settings the chain is one source long, so the walk
// asks it once per object and the cast's bytes cannot move.

#include "astrolog.h"


// The registry, in fallback quality order (section 4.2: swiss, jpl,
// moshier, matrix), then none. None serves nothing: a chain that reaches
// it answers "no" for every object, which is the honest answer of a
// selection that names no source.

EPHSRCDEF * CONST rgephsrc[cEphSrcBuiltIn] = {
  &ephsrcSwiss, &ephsrcJpl, &ephsrcMoshier, &ephsrcMatrix, &ephsrcNone
};


int CEphSrc()
{
  return cEphSrcBuiltIn;
}


EPHSRCDEF *PephsrcGet(int isrc)
{
  if (isrc < 0 || isrc >= cEphSrcBuiltIn)
    return NULL;
  return rgephsrc[isrc];
}


int IEphSrcFromKey(CONST char *szKey)
{
  int isrc;

  if (szKey == NULL)
    return -1;
  for (isrc = 0; isrc < cEphSrcBuiltIn; isrc++)
    if (FEqSz(rgephsrc[isrc]->szKey, szKey))
      return isrc;
  return -1;
}


// The source today's selection fields pick, as the chain's head. Phase 4
// replaces this with us.szEphemSource, the user's own ordered chain; the
// mapping here is exactly today's behavior (the nSwissEph values and the
// GetSwissFlags() comment in calc.cpp): the Ephemeris Server backend,
// nSwissEph 5, is the Swiss files on another machine, and the calls this
// build still makes locally under it ask the local Swiss files too. The
// JPL Horizons selections, nSwissEph 3 and 4, keep their own branch in
// ComputeEphem() this phase; what still reaches the shared Swiss path
// under them reaches it with the JPL ephemeris bit, which is the jpl
// source.

int IEphSrcPrimary()
{
  int isrc;

  if (!us.fEphemFiles)
    isrc = IEphSrcFromKey(us.fMatrixPla ? "matrix" : "none");
  else if (us.nSwissEph == 1)
    isrc = IEphSrcFromKey("moshier");
  else if (us.nSwissEph == 0 || us.nSwissEph == 5)
    isrc = IEphSrcFromKey("swiss");
  else
    isrc = IEphSrcFromKey("jpl");
  return isrc;
}


// Start a query: one instant, no objects yet.

void EphQueryInit(EPHQUERY *pq, real rJD)
{
  int i;

  pq->rJD = rJD;
  pq->cobj = 0;
  for (i = 0; i < objMax; i++) {
    pq->rgobj[i] = 0;
    pq->rgcent[i] = 0;
    pq->rgnNative[i] = 0;
    pq->rgisrc[i] = ephSrcNone;
  }
  ClearB((pbyte)pq->rgrow, sizeof(EPHROW) * objMax);
}


// Add one object to a query: its Astrolog index, the host-only native
// hint (0 for "the source decides"), and the orbit's centre -- the
// indCent FSwissPlanet() would have been handed. False when the query is
// full, which cannot happen for a cast: objMax bounds it by construction.

flag FEphQueryAdd(EPHQUERY *pq, int obj, int nNative, int cent)
{
  int i;

  if (pq->cobj >= objMax)
    return fFalse;
  i = pq->cobj++;
  pq->rgobj[i] = obj;
  pq->rgcent[i] = cent;
  pq->rgnNative[i] = nNative;
  pq->rgisrc[i] = ephSrcNone;
  return fTrue;
}


// One quiet notice per cast that a fallback served something. The flag
// lives for the walk below and is read by FEphFallbackNotice(); showing
// it is the Ephemeris Settings status line, which is phase 5. Per walk,
// because a walk is one submit -- the per-cast once of section 4.1 is a
// phase 6 refinement, when the host really does submit once per cast.
// Under today's single-source chains it can only ever be false.

static flag fEphFallbackServed = fFalse;


flag FEphFallbackNotice()
{
  return fEphFallbackServed;
}


// Walk a query down a chain of registry indexes. For each object the
// first source that is available and answers it wins; a source that
// fails an object leaves it open, and the next source is asked for that
// object. A source that already answered an object is never asked again
// for it (rgisrc marks the win), and FSubmit() skips marked objects, so
// no source is asked twice for a thing it has already failed this cast.
// Returns whether anything at all answered.

flag FEphSubmitChain(EPHQUERY *pq, CONST int *rgisrcChain, int cisrc)
{
  EPHSRCDEF *pephsrc;
  char szWhy[cchSzDef];
  int isrc, i;
  flag fFallback = fFalse, fAny = fFalse;

  fEphFallbackServed = fFalse;
  for (isrc = 0; isrc < cisrc; isrc++) {
    if (rgisrcChain[isrc] < 0 || rgisrcChain[isrc] >= cEphSrcBuiltIn)
      continue;
    pephsrc = PephsrcGet(rgisrcChain[isrc]);
    if (!pephsrc->FAvailable(szWhy, cchSzDef))
      continue;
    // A source returning false attempted nothing (a transport that is
    // down): every open object stays open. Returning true, it has filled
    // a row for every open object, success or per-object error.
    if (!pephsrc->FSubmit(pq))
      continue;
    for (i = 0; i < pq->cobj; i++)
      if (pq->rgisrc[i] == ephSrcNone && pq->rgrow[i].nErr == ephErrNone) {
        pq->rgisrc[i] = rgisrcChain[isrc];
        pq->rgrow[i].szSrc = pephsrc->szKey;
        if (isrc > 0)
          fFallback = fTrue;
      }
  }
  fEphFallbackServed = fFallback;
  for (i = 0; i < pq->cobj; i++)
    if (pq->rgisrc[i] != ephSrcNone)
      fAny = fTrue;
  return fAny;
}


// Submit a query down the chain today's settings derive: the primary
// source, and that is the whole chain in phase 3. Phase 4's us.szEphemSource
// names the order; phase 6's remote sources join the walk.

flag FEphSubmit(EPHQUERY *pq)
{
  int rgisrc[1];

  rgisrc[0] = IEphSrcPrimary();
  return FEphSubmitChain(pq, rgisrc, 1);
}


// Read one object's answer, in FSwissPlanet()'s output argument order, so
// a caller reading through the host swaps nothing against a caller
// reading from FSwissPlanet directly. False when the object is not in the
// query, or nothing answered it.

flag FEphRead(CONST EPHQUERY *pq, int ind, real *obj, real *objalt,
  real *dir, real *dist, real *diralt, real *dirlen)
{
  int i;

  for (i = 0; i < pq->cobj; i++)
    if (pq->rgobj[i] == ind)
      break;
  if (i >= pq->cobj || pq->rgisrc[i] == ephSrcNone ||
    pq->rgrow[i].nErr != ephErrNone)
    return fFalse;
  *obj    = pq->rgrow[i].rg[0];
  *objalt = pq->rgrow[i].rg[1];
  *dir    = pq->rgrow[i].rg[3];
  *dist   = pq->rgrow[i].rg[2];
  *diralt = pq->rgrow[i].rg[4];
  *dirlen = pq->rgrow[i].rg[5];
  return fTrue;
}


/*
******************************************************************************
** The None Source.
******************************************************************************
*/

// No source at all: the selection that names nothing. Its legacy cast is
// CastChart()'s own arithmetic (ComputePlanets()/ComputeLunar()), which
// is why the capability is fLegacyCast and nothing else; asked for a
// position it truthfully refuses every object.

static flag FAvailableNone(char *szWhy, int cch)
{
  if (szWhy != NULL)
    sprintf2(szWhy, cch, "%s", "always available; it computes nothing");
  return fTrue;
}


static void GetCapsNone(EPHCAPS *pcaps)
{
  ClearB((pbyte)pcaps, sizeof(EPHCAPS));
  pcaps->fLegacyCast = fTrue;
}


static int StateNone(char *sz, int cch)
{
  if (sz != NULL)
    sprintf2(sz, cch, "%s", "ready (no source selected)");
  return esReady;
}


static void StartNone()
{
}


static void StopNone()
{
}


static flag FSubmitNone(EPHQUERY *pq)
{
  int i;

  for (i = 0; i < pq->cobj; i++)
    if (pq->rgisrc[i] == ephSrcNone)
      pq->rgrow[i].nErr = ephErrUnsupported;
  return fTrue;
}


static flag FReadNone(CONST EPHQUERY *pq, int iObj, int iRow, EPHROW *prow)
{
  return fFalse;
}


static void HintNone(CONST EPHQUERY *pq)
{
}


static int NLookupNone(CONST char *sz, EPHMATCH *rgm, int cMax)
{
  return 0;
}


EPHSRCDEF ephsrcNone = {
  "none", "None", "No ephemeris source; the Matrix legacy cast only.",
  NULL, 0,
  FAvailableNone, GetCapsNone, StateNone, StartNone, StopNone,
  FSubmitNone, FReadNone, HintNone, NLookupNone
};
