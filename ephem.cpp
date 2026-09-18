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
  &ephsrcSwiss, &ephsrcJpl, &ephsrcMoshier, &ephsrcMatrix,
#ifdef PROMETHEIA
  &ephsrcPrometheia,
#endif
  &ephsrcServer, &ephsrcNone
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


// The range form, for walking the chain's text: the token between two
// commas is not terminated, so the comparison stops at its length.

static int IEphSrcFromKeyN(CONST char *pch, int cch)
{
  int isrc;

  if (pch == NULL)
    return -1;
  for (isrc = 0; isrc < cEphSrcBuiltIn; isrc++)
    if (FEqSzRange(pch, cch, rgephsrc[isrc]->szKey))
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

static CONST char * CONST rgszEphSrcFuture[] = {
  "horizons",
#ifndef PROMETHEIA
  // Only while it is genuinely absent: with the plugin compiled in, the
  // registry supplies this key and naming it here as well would be a
  // second answer to the same question.
  "prometheia",
#endif
};


// The range form is what the -bE parser needs -- it validates the text
// between the commas without copying it, so no chain token is ever
// truncated into a different key. FEphSrcKeyKnown() is the whole-string
// form, which the test suite's cross-pin uses.

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


/*
******************************************************************************
** The Selection State.
******************************************************************************
*/

// The chain us.szEphemSource holds, and the parameter values beside it,
// are the selection (EPHEMERIS_PLUGINS_PLAN.md 5.1). It is the one
// representation now: the legacy spellings (-b and its suffixes, -bW,
// -bT) update a parse-time shadow of the three old fields the way they
// always did, and re-derive the chain from that shadow after each one
// (EphSourceSetShadow); the dialogs and -bE/-bP set the chain directly.
// A generation counter tells the shadow when the chain changed beneath
// it, so its toggles always start from what is actually selected.

static int nEphSourceGen = 0;


// How many times the chain has been set. NSwb's shadow invalidates on
// any change but its own.

int NEphSourceGen()
{
  return nEphSourceGen;
}


// The default chain: the Swiss Ephemeris under EPHEM, the Matrix legacy
// cast without it.
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
// follows.

void SzEphChainHead(CONST char *szChain, char *sz, int cch)
{
  CONST char *pch;

  szChain = SzSet(szChain);
  for (pch = szChain; *pch && *pch != ','; pch++)
    ;
  sprintf2(sz, cch, "%.*s", (int)(pch - szChain), szChain);
}


// Whether the chain's head is one named source -- what ComputeEphem()
// needs to know to hand the cast to the Ephemeris Server or the JPL
// Horizons branches instead of the host walk, and what the legacy-cast
// gate needs to tell Matrix from None.

flag FSrcChainHead(CONST char *szKey)
{
  char sz[cchSzDef];

  SzEphChainHead(us.szEphemSource, S(sz));
  return FEqSz(sz, szKey);
}


// The two predicates that replaced the FCm* family (section 4.1). Rates
// are available whenever the primary is not one of the two legacy-cast
// sources; the legacy cast -- ComputePlanets and ComputeLunar inside
// CastChart, the Matrix dates and houses -- is theirs. The head's text
// is the test because the head may be a source this build predates
// ("server"), whose capabilities the registry does not carry yet: the
// old predicates read the selection, not the engine's availability.

flag FEphLegacyCast()
{
  return FSrcChainHead("matrix") || FSrcChainHead("none");
}


flag FEphSpeeds()
{
  return !FEphLegacyCast();
}


// Changing the source: the new source's warning latch is not the old
// one's. The Swiss path latch is FSwissPlanet()'s (see below), and the
// server's window cache needs nothing here: it is keyed on the server's
// own datasetId and dropped when the address changes (qtdriver.cpp),
// which is the adapter's own definition of "the source changed".

void EphSourceChanged()
{
  is.fSwissPathSet = fFalse;   // The 4c3 shape, for the bisection.
  is.fNoEphFile = fFalse;
}


// The source parameters' shared index space (4.3), generated into
// ephparam.h. This is the table's one definition, and since 2026-09-18
// its ONLY one: -bP looks keys up here, the dialogs build their rows
// from here, and a source asks for its own with the two functions below.
// A source used to hand a copy to its descriptor as well, which nothing
// read and which had drifted.

CONST EPHPARAMROW rgephparam[cEphParam] = EphParamRowsGenerated();


// A source's parameters, by its key. The rows are grouped by source and
// in the enum's order, but nothing depends on that here: both walk the
// whole table, because a table whose grouping is load-bearing is one an
// append can break silently.

int CEphParamOfSrc(CONST char *szKey)
{
  int iep, c = 0;

  if (szKey == NULL)
    return 0;
  for (iep = 0; iep < cEphParam; iep++)
    if (FEqSz(rgephparam[iep].szSrc, szKey))
      c++;
  return c;
}


// The parameter rows a dialog shows: the parameters of the chain's HEAD
// source, which is the one being configured -- its status, its Connect
// button and its description are all already the dialog's subject. Fills
// rgiep with their shared indexes and returns how many, never more than
// cMax.
//
// Both builds used a hand-written list of four indexes instead, naming
// four of the six parameters the table declares. Two of the four were a
// source's and two another's, so the dialog showed a mixture no single
// selection could use, and the two it left out -- a catalog and a
// perturber kernel -- had no way in from any dialog at all. The count
// four was not the bug; no source declares more than three parameters,
// so four rows are enough. Naming the indexes by hand was.

// The same, for a chain the caller names rather than the one in force --
// a dialog showing the rows for a source the user has PICKED but not yet
// applied needs its own chain, not the live selection.
int CEphParamRowsOf(CONST char *szChain, int *rgiep, int cMax)
{
  char szHead[cchSzDef];
  int c, i, cRow = 0;

  if (rgiep == NULL || cMax <= 0)
    return 0;
  SzEphChainHead(szChain, S(szHead));
  c = CEphParamOfSrc(szHead);
  for (i = 0; i < c && cRow < cMax; i++) {
    int iep = IepOfSrc(szHead, i);
    if (iep >= 0)
      rgiep[cRow++] = iep;
  }
  return cRow;
}


int CEphParamRows(int *rgiep, int cMax)
{
  return CEphParamRowsOf(us.szEphemSource, rgiep, cMax);
}


int IepOfSrc(CONST char *szKey, int i)
{
  int iep, c = 0;

  if (szKey == NULL || i < 0)
    return -1;
  for (iep = 0; iep < cEphParam; iep++)
    if (FEqSz(rgephparam[iep].szSrc, szKey) && c++ == i)
      return iep;
  return -1;
}


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
  CONST char *szOld;
  flag fMoved;

  if (iep < 0 || iep >= cEphParam)
    return fFalse;
  szOld = SzSet(us.rgszEphParam[iep]);
  fMoved = !FEqSz(szOld, szVal != NULL ? szVal : "");
  FCloneSz(szVal != NULL && *szVal ? szVal : NULL,
    &us.rgszEphParam[iep]);
  // No notification to the owning source, deliberately. Telling it to
  // Stop() here was the first shape and it was wrong twice over: a
  // notification can be missed by any path that writes the value another
  // way, and Stop() is far too blunt for a REMOTE source -- it tore down
  // a live connection and its in-flight request merely because the user
  // edited the address, discarding work the adapter is built to carry
  // across a reconnect.
  //
  // A source that holds something open against a parameter compares what
  // it opened with what the parameter says, at the point it uses it.
  // That cannot be missed and cannot fire too hard.
  (void)fMoved;
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
// three fields the old spellings toggled, by the same mapping the old
// selection used. This is the NSwb shadow's lazy re-sync (the generation
// counter above decides when) and nothing else -- nothing else reads
// the triple any more.

void EphShadowFromChain(flag *pfFiles, int *pnSwiss, flag *pfMatrix)
{
  char sz[cchSzDef];

  SzEphChainHead(us.szEphemSource, S(sz));
  if (FEqSz(sz, "moshier")) {
    *pfFiles = fTrue;  *pnSwiss = 1;  *pfMatrix = fFalse;
  } else if (FEqSz(sz, "jpl")) {
    *pfFiles = fTrue;  *pnSwiss = 2;  *pfMatrix = fFalse;
  } else if (FEqSz(sz, "horizons")) {
    *pfFiles = fTrue;  *pnSwiss = 3;  *pfMatrix = fFalse;
  } else if (FEqSz(sz, "server")) {
    *pfFiles = fTrue;  *pnSwiss = 5;  *pfMatrix = fFalse;
  } else if (FEqSz(sz, "matrix")) {
    *pfFiles = fFalse; *pfMatrix = fTrue;
  } else if (FEqSz(sz, "none")) {
    *pfFiles = fFalse; *pfMatrix = fFalse;
  } else {
    // "swiss", and every head the triple cannot name: the Swiss files
    // selection is what the old fields said for it.
    *pfFiles = fTrue;  *pnSwiss = 0;  *pfMatrix = fFalse;
  }
}


// The whole of -bE: set the chain. Resetting the latches happens only
// when the chain really changed -- a second -bE of the same chain is
// idempotent, not a reset.

void EphSourceSet(CONST char *szChain)
{
  if (!FSzSet(szChain))
    szChain = SzEphSourceDefault();
  if (!FEqSz(SzSet(us.szEphemSource), szChain)) {
    FCloneSz(szChain, &us.szEphemSource);
    EphSourceChanged();
  }
  nEphSourceGen++;
}


// The other direction, which every legacy spelling ends with: re-derive
// the chain from the shadow of the three old fields, exactly as the old
// selection mapped them -- including the two selections whose chain
// needs a second entry, for the branches the old fields cannot name.

void EphSourceSetShadow(flag fFiles, int nSwiss, flag fMatrix)
{
  char sz[cchSzMax];

  if (!fFiles)
    sprintf2(S(sz), "%s", fMatrix ? "matrix" : "none");
  else
    switch (nSwiss) {
    case 1:
      sprintf2(S(sz), "%s", "moshier"); break;
    case 2:
      sprintf2(S(sz), "%s", "jpl"); break;
    case 3:
      // The Horizons selection: its own branch answers the objects it
      // covers, and what is left reaches the JPL-file source, which is
      // where the old IEphSrcPrimary() always sent the residue.
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
  EphSourceSet(sz);
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

// The chain us.szEphemSource names, as registry indexes: each token in
// order, with a key this build cannot resolve -- a source a newer build
// names -- skipped, because that is exactly how the walk treats it.
// Returns the count, at most cisrcMax.

int CEphChainSrc(CONST char *szChain, int rgisrc[], int cisrcMax)
{
  CONST char *pch, *pchTok;
  int cisrc = 0, isrc;

  szChain = SzSet(szChain);
  for (pchTok = szChain; *pchTok; pchTok = *pch ? pch + 1 : pch) {
    for (pch = pchTok; *pch && *pch != ','; pch++)
      ;
    isrc = IEphSrcFromKeyN(pchTok, (int)(pch - pchTok));
    if (isrc >= 0 && cisrc < cisrcMax)
      rgisrc[cisrc++] = isrc;
    if (!*pch)
      break;
  }
  return cisrc;
}


// The Swiss Ephemeris bit the current Swiss-family work runs under:
// 0 files, 1 Moshier, 2 JPL. Outside a borrow it is the chain's own
// first Swiss-family source's; the delegated calls of a chain's SECOND
// source borrow their own around the loop (ephswiss.cpp), which is what
// makes a multi-source chain two genuinely different engines. The
// borrow is a plain save/set/restore on this static, because the
// delegated calls are synchronous.

static int nSwissEphemBorrow = -1;


int NSwissEphem()
{
  CONST char *pch, *pchTok;
  flag fFiles = fFalse, fMosh = fFalse, fJpl = fFalse;

  if (nSwissEphemBorrow >= 0)
    return nSwissEphemBorrow;
  for (pchTok = SzSet(us.szEphemSource); *pchTok;
      pchTok = *pch ? pch + 1 : pch) {
    for (pch = pchTok; *pch && *pch != ','; pch++)
      ;
    // First match wins: the chain's Swiss-family order is the bit's.
    if (!fFiles && FEqSzRange(pchTok, (int)(pch - pchTok), "swiss"))
      fFiles = fTrue;
    else if (!fMosh && FEqSzRange(pchTok, (int)(pch - pchTok), "moshier"))
      fMosh = fTrue;
    else if (!fJpl && FEqSzRange(pchTok, (int)(pch - pchTok), "jpl"))
      fJpl = fTrue;
    if (!*pch)
      break;
  }
  // No Swiss-family source in the chain: the files, which is the
  // default GetSwissFlags() has always carried.
  return fFiles ? 0 : (fMosh ? 1 : (fJpl ? 2 : 0));
}


// Set the borrowed bit, and answer with what was borrowed before (-1
// for none). The restore takes that answer back, so a submit inside a
// borrow unwinds to the borrow, and one outside unwinds to the chain.
// Returning the previous value instead of making the caller read
// NSwissEphem() first is the point: NSwissEphem() answers the DERIVED
// bit when nothing is borrowed, and a restore of that would pin the
// static to a chain's answer forever.

int SwissSetEphemCast(int n)
{
  int nSav = nSwissEphemBorrow;

  nSwissEphemBorrow = n;
  return nSav;
}


void SwissRestoreEphemCast(int n)
{
  nSwissEphemBorrow = n;
}


// Submit a side-call query: one query per instant (the side calls take
// their instants one at a time this phase), down the user's own chain
// order. The side calls keep their one assumption from before the
// registry existed (section 1, item 7): when the chain names nothing
// that serves them -- a Matrix or None primary -- they end at the Swiss
// files source, which is what they have always asked. A chain whose
// Swiss-family source is its own (swiss, moshier, jpl, or one named
// beside another) is walked as it stands, so a Moshier selection's side
// calls fail exactly as they always did rather than falling back to
// something the user did not select.

flag FEphSubmitSide(EPHQUERY *pq)
{
  int rgisrc[cEphSrcBuiltIn + 1];
  int cisrc, isrc, isrcSwiss;
  flag fSwiss = fFalse;

  cisrc = CEphChainSrc(us.szEphemSource, rgisrc, cEphSrcBuiltIn);
  isrcSwiss = IEphSrcFromKey("swiss");
  for (isrc = 0; isrc < cisrc; isrc++)
    if (rgisrc[isrc] == isrcSwiss ||
      rgisrc[isrc] == IEphSrcFromKey("moshier") ||
      rgisrc[isrc] == IEphSrcFromKey("jpl"))
      fSwiss = fTrue;
  if (!fSwiss && cisrc <= cEphSrcBuiltIn)
    rgisrc[cisrc++] = isrcSwiss;
  return FEphSubmitChain(pq, rgisrc, cisrc);
}


// One quiet notice per cast that a fallback served something. The flag
// lives for the walk below and is read by FEphFallbackNotice(); showing
// it is the Ephemeris Settings status line, which is phase 5. Per walk,
// because a walk is one submit -- the per-cast once of section 4.1 is a
// phase 6 refinement, when the host really does submit once per cast.
// Under today's single-source chains it can only ever be false.

static flag fEphFallbackServed = fFalse;

// Why nothing answered, when nothing did: the sources' own reasons, in
// chain order. A chain of unavailable sources is the one failure that
// looks exactly like a successful cast -- every body at 0Ari00'00", no
// error, the houses correct because they are computed from the time and
// place rather than from an ephemeris. CLAUDE.md names that reading as
// the trap it is; this is the host saying so out loud.
char szEphNoSourceWhy[cchSzMax] = "";

CONST char *SzEphNoSourceWhy()
{
  return szEphNoSourceWhy;
}


// Is some source in the chain still CONNECTING? A remote source answers
// nothing while its socket is coming up, and the host must not say "no
// source could answer" about a cast that is about to be answered: the Qt
// backend recasts when the WELCOME lands, so the first chart of a session
// selecting the server would otherwise print an alarming warning and then
// quietly draw the right chart a moment later.
flag FEphChainConnecting()
{
  int rgisrc[cEphSrcBuiltIn], cisrc, isrc;
  char szT[cchSzDef];

  cisrc = CEphChainSrc(us.szEphemSource, rgisrc, cEphSrcBuiltIn);
  for (isrc = 0; isrc < cisrc; isrc++)
    if (rgisrc[isrc] >= 0 && rgisrc[isrc] < cEphSrcBuiltIn &&
      PephsrcGet(rgisrc[isrc])->State(S(szT)) == esConnecting)
      return fTrue;
  return fFalse;
}


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
  int isrc, i, cWhy = 0;
  flag fFallback = fFalse, fAny = fFalse;

  fEphFallbackServed = fFalse;
  szEphNoSourceWhy[0] = chNull;
  for (isrc = 0; isrc < cisrc; isrc++) {
    if (rgisrcChain[isrc] < 0 || rgisrcChain[isrc] >= cEphSrcBuiltIn)
      continue;
    pephsrc = PephsrcGet(rgisrcChain[isrc]);
    if (!pephsrc->FAvailable(szWhy, cchSzDef)) {
      // Why, in the source's own words, in case NOTHING answers: a chain
      // whose every source is unavailable used to cast a chart of zeros
      // and say nothing at all.
      // APPENDED, not re-formatted through itself: passing the destination
      // as one of its own arguments is undefined, and the compiler said so
      // (-Wformat-truncation) the moment the first draft was audited.
      if (cWhy < 2) {
        int cchAt = CchSz(szEphNoSourceWhy);
        sprintf2(szEphNoSourceWhy + cchAt, cchSzMax - cchAt, "%s%s (%s)",
          cWhy > 0 ? "; " : "", pephsrc->szKey, szWhy);
      }
      cWhy++;
      continue;
    }
    // A source returning false attempted nothing (a transport that is
    // down): every open object stays open. Returning true, it has filled
    // a row for every open object, success or per-object error.
    if (!pephsrc->FSubmit(pq)) {
      if (cWhy < 2) {
        int cchAt = CchSz(szEphNoSourceWhy);
        sprintf2(szEphNoSourceWhy + cchAt, cchSzMax - cchAt,
          "%s%s (attempted nothing)", cWhy > 0 ? "; " : "", pephsrc->szKey);
      }
      cWhy++;
      continue;
    }
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
  int rgisrc[cEphSrcBuiltIn];

  return FEphSubmitChain(pq, rgisrc,
    CEphChainSrc(us.szEphemSource, rgisrc, cEphSrcBuiltIn));
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
  FAvailableNone, GetCapsNone, StateNone, StartNone, StopNone,
  FSubmitNone, FReadNone, HintNone, NLookupNone
};
