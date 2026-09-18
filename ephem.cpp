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


// Whether a chain token names a source at all. The compiled sources'
// own names are the registry's -- read from rgephsrc[], so the two
// lists cannot drift apart -- plus the three keys of section 4.2 whose
// plugins are later phases': a settings file from a build that has them
// must still open here, and the walk (FEphSubmitChain) skips a key it
// cannot resolve, which is the same rule an unavailable source follows.
// A key from NO source at all is a typo, and refusing it is what keeps
// the typo out of the settings file: the chain's head would otherwise
// fall through to the Swiss files, so "-bE mosheir" would cast from
// Swiss and write itself back as "mosheir". When phase 6 registers the
// remote pair and phase 7 the Prometheia plugin, this table is deleted.

static CONST char * CONST rgszEphSrcFuture[] = {"server", "horizons",
  "prometheia"};


// The range form is what the -bE parser needs -- it validates the text
// between the commas without copying it, so no chain token is ever
// truncated into a different key. FEphSrcKeyKnown() is the whole-string
// form, which the test suite's cross-pin uses.

// Whether one range of the chain text equals one key: the same length
// and the same characters. The range is not terminated -- it is the
// text between two commas -- so NCompareSz() would run past it; this
// stops at the length.

static flag FEqSzRange(CONST char *pch, int cch, CONST char *sz)
{
  int ich;

  if (CchSz(sz) != cch)
    return fFalse;
  for (ich = 0; ich < cch; ich++)
    if (pch[ich] != sz[ich])
      return fFalse;
  return fTrue;
}


flag FEphSrcKeyKnownN(CONST char *pch, int cch)
{
  int isrc;

  if (pch == NULL || cch < 0)
    return fFalse;
  for (isrc = 0; isrc < cEphSrcBuiltIn; isrc++)
    if (FEqSzRange(pch, cch, rgephsrc[isrc]->szKey))
      return fTrue;
  for (isrc = 0; isrc < (int)(sizeof(rgszEphSrcFuture) /
    sizeof(*rgszEphSrcFuture)); isrc++)
    if (FEqSzRange(pch, cch, rgszEphSrcFuture[isrc]))
      return fTrue;
  return fFalse;
}


flag FEphSrcKeyKnown(CONST char *szKey)
{
  return FEphSrcKeyKnownN(szKey, szKey == NULL ? -1 : CchSz(szKey));
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


/*
******************************************************************************
** The Selection State.
******************************************************************************
*/

// The chain us.szEphemSource holds, and the parameter values beside it,
// are the selection (EPHEMERIS_PLUGINS_PLAN.md 5.1). While the old
// spellings live -- -b and its suffixes, -bW, -bT, the dialogs' combo --
// the selection has two representations, and every way in writes BOTH:
// a legacy spelling re-derives the chain from its shadow (below), and
// -bE/-bP set the chain and re-derive the shadow. Nothing reads the two
// against each other, so a file written by either half loads whole.

// The default chain: the Swiss Ephemeris under EPHEM, the Matrix legacy
// cast without it -- the same default fEphemFiles carries.
CONST char *SzEphSourceDefault()
{
#ifdef EPHEM
  return "swiss";
#else
  return "matrix";
#endif
}


// A chain's head: the text up to the first comma, which is the primary
// source. Tokens after it are the fallback order, and the walk
// (FEphSubmitChain) skips any of them it cannot resolve -- a source this
// build predates, named by a settings file from a newer one, rides along
// and is skipped, which is the same rule an unavailable compiled source
// follows. That is also why an unknown key here is not an error: it is
// indistinguishable from a source the walk will skip.

void SzEphChainHead(CONST char *szChain, char *sz, int cch)
{
  CONST char *pch;

  szChain = SzSet(szChain);
  for (pch = szChain; *pch && *pch != ','; pch++)
    ;
  sprintf2(sz, cch, "%.*s", (int)(pch - szChain), szChain);
}


// Changing the source: the new source's paths, files and caches are not
// the old one's. is.fSwissPathSet is the Swiss library's path latch, and
// is.fNoEphFile the one warning latch a failed file shares; both belong
// to the source that raised them. The server's window cache needs
// nothing here: it is keyed on the server's own datasetId and dropped
// when the address changes (qtdriver.cpp), which is the adapter's own
// definition of "the source changed".

void EphSourceChanged()
{
  is.fSwissPathSet = fFalse;
  is.fNoEphFile = fFalse;
}


// The source parameters' shared index space (4.3), generated into
// ephparam.h. This is the table's one definition; ephswiss.cpp hands a
// slice of it to the jpl source's rgParam, and -bP looks keys up here.

CONST EPHPARAMROW rgephparam[cEphParam] = EphParamRowsGenerated();


// "server.url" -> epServerUrl, by way of the generated table. -1 when the
// source or the parameter is not one this build declares; -bP refuses
// that, the way it refuses an unknown switch -- a settings file this old
// build cannot name is better refused than silently dropped.

int IEphParamFromKey(CONST char *szKey)
{
  char szSrc[cchSzDef], *pch;
  int iep;
  CONST EPHPARAMROW *pep;

  if (szKey == NULL)
    return -1;
  sprintf2(S(szSrc), "%s", szKey);
  pch = szSrc;
  while (*pch && *pch != '.')
    pch++;
  if (*pch != '.')
    return -1;
  *pch = chNull;
  for (iep = 0, pep = rgephparam; iep < cEphParam; iep++, pep++)
    if (FEqSz(pep->szSrc, szSrc) && FEqSz(pep->ep.szKey, pch + 1))
      return iep;
  return -1;
}


// One parameter's value. "" restores the declared default, and NULL and
// "" are one state to every reader, so an empty value stores NULL.

flag FEphParamSet(int iep, CONST char *szVal)
{
  if (iep < 0 || iep >= cEphParam)
    return fFalse;
  FCloneSz(szVal != NULL && *szVal ? szVal : NULL,
    &us.rgszEphParam[iep]);
  return fTrue;
}


// Whether a parameter sits at its default, which is what the settings
// writer keys its -bP lines on (5.3: one line per non-default parameter).

flag FEphParamDefaulted(int iep)
{
  if (iep < 0 || iep >= cEphParam)
    return fTrue;
  return !FSzSet(us.rgszEphParam[iep]) ||
    FEqSz(us.rgszEphParam[iep], rgephparam[iep].ep.szDefault);
}


// Re-derive the legacy shadow from the chain: the head source names the
// fields the old spellings read, the same mapping IEphSrcPrimary() walks
// -- with the two sources that have no registry row yet spelled out,
// because the legacy fields cannot say "server" or "horizons" without
// also saying what used to be selected beside them. A head this mapping
// does not know (a future source, or the settings sweep's marker) leaves
// the shadow alone: the chain is the new representation, and nothing
// below reads it yet.

void EphLegacyFromChain()
{
  char sz[cchSzDef];

  SzEphChainHead(us.szEphemSource, S(sz));
  if (FEqSz(sz, "moshier")) {
    us.fEphemFiles = fTrue;  us.nSwissEph = 1;  us.fMatrixPla = fFalse;
  } else if (FEqSz(sz, "jpl")) {
    us.fEphemFiles = fTrue;  us.nSwissEph = 2;  us.fMatrixPla = fFalse;
  } else if (FEqSz(sz, "horizons")) {
    us.fEphemFiles = fTrue;  us.nSwissEph = 3;  us.fMatrixPla = fFalse;
  } else if (FEqSz(sz, "server")) {
    us.fEphemFiles = fTrue;  us.nSwissEph = 5;  us.fMatrixPla = fFalse;
  } else if (FEqSz(sz, "matrix")) {
    us.fEphemFiles = fFalse; us.fMatrixPla = fTrue;
  } else if (FEqSz(sz, "none")) {
    us.fEphemFiles = fFalse; us.fMatrixPla = fFalse;
  } else if (!FEqSz(sz, "")) {
    // "swiss", and every key the shadow cannot name: the Swiss files
    // selection is what the old fields say for it.
    us.fEphemFiles = fTrue;  us.nSwissEph = 0;  us.fMatrixPla = fFalse;
  }
}


// The whole of -bE: set the chain, and keep the two representations in
// step. Resetting the latches happens only when the chain really
// changed -- a second -bE of the same chain is idempotent, not a reset.

void EphSourceSet(CONST char *szChain)
{
  flag fChanged;

  if (!FSzSet(szChain))
    szChain = SzEphSourceDefault();
  fChanged = !FEqSz(SzSet(us.szEphemSource), szChain);
  if (fChanged) {
    FCloneSz(szChain, &us.szEphemSource);
    EphSourceChanged();
  }
  EphLegacyFromChain();
}


// The other direction, which every legacy spelling ends with: re-derive
// the chain from the fields the old code wrote, and reset the latches
// when that changed the source. Returns whether it did.

flag FEphChainFromLegacy()
{
  char sz[cchSzDef];
  flag fChanged;

  if (!us.fEphemFiles)
    sprintf2(S(sz), "%s", us.fMatrixPla ? "matrix" : "none");
  else
    switch (us.nSwissEph) {
    case 1:
      sprintf2(S(sz), "%s", "moshier"); break;
    case 2:
      sprintf2(S(sz), "%s", "jpl"); break;
    case 3:
      // The Horizons selection: its own branch answers the objects it
      // covers, and what is left reaches the JPL-file source, which is
      // where IEphSrcPrimary() has always sent the residue.
      sprintf2(S(sz), "%s", "horizons,jpl"); break;
    case 5:
      // The Ephemeris Server selection: the cast's questions go to the
      // server, and the side calls -- a progressed arc, a star, the
      // eclipse Sun -- still ask the local Swiss files, as they always
      // have (section 1, item 7; the chain names that).
      sprintf2(S(sz), "%s", "server,swiss"); break;
    default:
      sprintf2(S(sz), "%s", "swiss"); break;
    }
  fChanged = !FEqSz(SzSet(us.szEphemSource), sz);
  if (fChanged) {
    FCloneSz(sz, &us.szEphemSource);
    EphSourceChanged();
  }
  return fChanged;
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
    pq->rgszName[i] = NULL;
  }
  ClearB((pbyte)pq->rgrow, sizeof(EPHROW) * objMax);
}


// Add one object to a query: its Astrolog index, the host-only native
// hint (0 for "the source decides"), and the orbit's centre -- the
// indCent FSwissPlanet() would have been handed. False when the query is
// full, which cannot happen for a cast: objMax bounds it by construction.

flag FEphQueryAdd(EPHQUERY *pq, int obj, int nNative, int cent,
  char *szName)
{
  int i;

  if (pq->cobj >= objMax)
    return fFalse;
  i = pq->cobj++;
  pq->rgobj[i] = obj;
  pq->rgcent[i] = cent;
  pq->rgnNative[i] = nNative;
  pq->rgisrc[i] = ephSrcNone;
  pq->rgszName[i] = szName;
  return fTrue;
}


// The source the side calls reach. Today's side calls -- a progressed
// arc, the eclipse Sun, the fixed stars, an asteroid listing -- are
// unconditional Swiss calls whose ephemeris bit GetSwissFlags() takes
// from nSwissEph, whatever the cast's selection is (section 1, item 7:
// they assume the local Swiss files). This maps nSwissEph the same way
// IEphSrcPrimary() does, ignoring the files-off branch, so a side call
// makes the call it has always made while the cast keeps its own
// selection. Phase 4's chain re-plumb walks the user's order instead.

int IEphSrcSideCall()
{
  if (us.nSwissEph == 1)
    return IEphSrcFromKey("moshier");
  if (us.nSwissEph == 0 || us.nSwissEph == 5)
    return IEphSrcFromKey("swiss");
  return IEphSrcFromKey("jpl");
}


// Submit a side-call query: one query per instant (the side calls take
// their instants one at a time this phase), down the side-call source.

flag FEphSubmitSide(EPHQUERY *pq)
{
  int rgisrc[1];

  rgisrc[0] = IEphSrcSideCall();
  return FEphSubmitChain(pq, rgisrc, 1);
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


// Read one object's six columns in the protocol's own order -- which for
// a star row is the entry point's raw six. Writes nothing on failure,
// which is swe_fixstar2()'s own contract with its caller's array: a
// star that failed leaves the previous star's six standing, exactly as
// the direct call always has. False when the object is not in the
// query, or nothing answered it.

flag FEphReadRaw(CONST EPHQUERY *pq, int ind, real *rg)
{
  int i, ix;

  for (i = 0; i < pq->cobj; i++)
    if (pq->rgobj[i] == ind)
      break;
  if (i >= pq->cobj || pq->rgisrc[i] == ephSrcNone ||
    pq->rgrow[i].nErr != ephErrNone)
    return fFalse;
  for (ix = 0; ix < 6; ix++)
    rg[ix] = pq->rgrow[i].rg[ix];
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
