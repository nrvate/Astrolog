/*
** Astrolog (Version 8.00) File: ephswiss.cpp
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

// The three local Swiss sources -- swiss, moshier and jpl -- and the
// matrix source. EPHEMERIS_PLUGINS_PLAN.md section 4.2.
//
// The three Swiss sources are ONE shared implementation: the SEFLG
// ephemeris bit that tells them apart is not decided here at all, but
// inside FSwissPlanet()'s own GetSwissFlags() from the same us.nSwissEph
// field the selection was read from -- so a cast through the host under
// nSwissEph 0, 1 or 2 makes exactly the calls the Swiss branch of
// ComputeEphem() used to make, bits and all, and is byte-identical by
// construction rather than by re-derivation. The source decides nothing;
// it delegates the whole decision and the whole call to the functions the
// program has always used. A later increment may move the execution half
// onto ephswiss.h's SwissCall like the Ephemeris Server does; not this
// one.
//
// FSwissPlanet() prints its own warning and latches is.fNoEphFile on a
// failure, exactly as it always has -- the source does not add a second
// warning, and the fallback walk does not make one either, because under
// today's settings the chain holds the one live source.

#include "astrolog.h"


/*
******************************************************************************
** The Swiss Sources: swiss, moshier and jpl.
******************************************************************************
*/

// Availability is about being compiled in and reachable, not about data
// files (section 4.2: unavailable sources are skipped at cast time, not
// at selection time -- a missing file is a per-object failure, which is
// FSwissPlanet()'s own report).

static flag FAvailableSwissLocal(char *szWhy, int cch)
{
#ifndef SWISS
  if (szWhy != NULL)
    sprintf2(szWhy, cch, "%s", "the Swiss Ephemeris is not compiled in");
  return fFalse;
#else
  return fTrue;
#endif
}


static void GetCapsSwissLocal(EPHCAPS *pcaps)
{
  ClearB((pbyte)pcaps, sizeof(EPHCAPS));
  pcaps->fBody = fTrue;
  pcaps->fOrbitPoint = fTrue;
  pcaps->fSpeeds = fTrue;
}


static int StateSwissLocal(char *sz, int cch)
{
  if (sz != NULL)
    sprintf2(sz, cch, "%s", "ready");
  return esReady;
}


static void StartSwissLocal()
{
}


static void StopSwissLocal()
{
}


// Answer the query's open objects, synchronously, one FSwissPlanet() call
// each, with the same three arguments ComputeEphem()'s Swiss branch hands
// it today: the object index, the instant, and the orbit's centre. The
// row's six reals are the answer columns in the protocol's order; the one
// transposition from FSwissPlanet()'s argument order lives here and in
// FEphRead(), named in both.
//
// A failure carries ephErrDataUnavailable. FSwissPlanet() also refuses
// objects it cannot map (the South Node, a custom body that maps to
// nothing), which is "unsupported" rather than "data"; the delegation
// cannot tell the two apart without re-deriving its decision, which this
// phase does not do. The fallback walk treats every failure the same way
// -- next source -- and under today's one-source chains the next source
// fails identically, so the code only names what the phase 6 wire will
// report.

static flag FSubmitSwissLocal(EPHQUERY *pq)
{
  EPHROW *prow;
  real r1, r2, r3, r4, r5, r6, rgxx[6];
  int i, ix;

  for (i = 0; i < pq->cobj; i++) {
    if (pq->rgisrc[i] != ephSrcNone)
      continue;   // Another source in the walk already answered this one.
    prow = &pq->rgrow[i];
    if (pq->rgszName[i] != NULL) {
      // A fixed star: FSwissStar()'s pair answers, and the row is the
      // entry point's own six -- already the answer columns in the
      // protocol's order, so no transposition.
      if (!FSwissStar(pq->rgszName[i], pq->rJD, rgxx)) {
        prow->nErr = ephErrDataUnavailable;
        continue;
      }
      for (ix = 0; ix < 6; ix++)
        prow->rg[ix] = rgxx[ix];
    } else {
      if (!FSwissPlanet(pq->rgobj[i], pq->rJD, pq->rgcent[i],
        &r1, &r2, &r3, &r4, &r5, &r6)) {
        prow->nErr = ephErrDataUnavailable;
        continue;
      }
      prow->rg[0] = r1;   // longitude
      prow->rg[1] = r2;   // latitude
      prow->rg[2] = r4;   // distance
      prow->rg[3] = r3;   // longitude rate
      prow->rg[4] = r5;   // latitude rate
      prow->rg[5] = r6;   // distance rate
    }
    prow->nErr = ephErrNone;
    prow->nNativeRes = pq->rgnNative[i];
  }
  return fTrue;
}


static flag FReadSwissLocal(CONST EPHQUERY *pq, int iObj, int iRow,
  EPHROW *prow)
{
  if (iObj < 0 || iObj >= pq->cobj || pq->rgisrc[iObj] == ephSrcNone ||
    iRow != 0)
    return fFalse;
  *prow = pq->rgrow[iObj];
  return fTrue;
}


static void HintSwissLocal(CONST EPHQUERY *pq)
{
}


static int NLookupSwissLocal(CONST char *sz, EPHMATCH *rgm, int cMax)
{
  return 0;   // LOOKUP is the phase 6 remote sources' work; nothing here
              // advertises it.
}


// The three sources differ in their names and in the one declared
// parameter jpl owns (section 4.2); the file name has no setting today --
// the library's own default, de431.eph, is what runs -- so the parameter
// is declared with the empty default that means "the source's own".

static CONST EPHPARAM rgparamJpl[] = {
  {"file", "JPL file", epkFile, ""},
};

#define cparamJpl (int)(sizeof(rgparamJpl) / sizeof(EPHPARAM))


EPHSRCDEF ephsrcSwiss = {
  "swiss", "Swiss Ephemeris files",
  "The Swiss Ephemeris over its own data files.",
  NULL, 0,
  FAvailableSwissLocal, GetCapsSwissLocal, StateSwissLocal, StartSwissLocal,
  StopSwissLocal, FSubmitSwissLocal, FReadSwissLocal, HintSwissLocal,
  NLookupSwissLocal
};

EPHSRCDEF ephsrcMoshier = {
  "moshier", "Moshier ephemeris",
  "The Moshier analytic formulas; major planets and Moon.",
  NULL, 0,
  FAvailableSwissLocal, GetCapsSwissLocal, StateSwissLocal, StartSwissLocal,
  StopSwissLocal, FSubmitSwissLocal, FReadSwissLocal, HintSwissLocal,
  NLookupSwissLocal
};

EPHSRCDEF ephsrcJpl = {
  "jpl", "JPL ephemeris file",
  "The Swiss Ephemeris over a JPL_DE file.",
  rgparamJpl, cparamJpl,
  FAvailableSwissLocal, GetCapsSwissLocal, StateSwissLocal, StartSwissLocal,
  StopSwissLocal, FSubmitSwissLocal, FReadSwissLocal, HintSwissLocal,
  NLookupSwissLocal
};


/*
******************************************************************************
** The Matrix Source.
******************************************************************************
*/

// The Matrix formulas keep their legacy cast: ComputePlanets() and
// ComputeLunar() inside CastChart(), plus Matrix dates and houses, do the
// work exactly as they always have (section 4.1). Asked through the
// registry for a position, the source truthfully refuses every object --
// that is what routes a cast whose chain reaches here to the legacy hook
// instead, which is the same hook that runs today.

static flag FAvailableMatrix(char *szWhy, int cch)
{
#ifndef MATRIX
  if (szWhy != NULL)
    sprintf2(szWhy, cch, "%s", "the Matrix formulas are not compiled in");
  return fFalse;
#else
  return fTrue;
#endif
}


static void GetCapsMatrix(EPHCAPS *pcaps)
{
  ClearB((pbyte)pcaps, sizeof(EPHCAPS));
  pcaps->fLegacyCast = fTrue;
}


static int StateMatrix(char *sz, int cch)
{
  if (sz != NULL)
    sprintf2(sz, cch, "%s", "ready (legacy cast)");
  return esReady;
}


static void StartMatrix()
{
}


static void StopMatrix()
{
}


static flag FSubmitMatrix(EPHQUERY *pq)
{
  int i;

  for (i = 0; i < pq->cobj; i++)
    if (pq->rgisrc[i] == ephSrcNone)
      pq->rgrow[i].nErr = ephErrUnsupported;
  return fTrue;
}


static flag FReadMatrix(CONST EPHQUERY *pq, int iObj, int iRow, EPHROW *prow)
{
  return fFalse;
}


static void HintMatrix(CONST EPHQUERY *pq)
{
}


static int NLookupMatrix(CONST char *sz, EPHMATCH *rgm, int cMax)
{
  return 0;
}


EPHSRCDEF ephsrcMatrix = {
  "matrix", "Matrix formula",
  "Astrolog's own Matrix formulas; the legacy cast hook.",
  NULL, 0,
  FAvailableMatrix, GetCapsMatrix, StateMatrix, StartMatrix, StopMatrix,
  FSubmitMatrix, FReadMatrix, HintMatrix, NLookupMatrix
};
