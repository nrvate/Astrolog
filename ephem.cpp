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
  &ephsrcServer,
#ifdef JPLWEB
  // The other remote one, beside the server and before none for the same
  // reason: a chain that reaches none is over.
  &ephsrcHorizons,
#endif
  &ephsrcNone
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
#ifndef JPLWEB
  // Only while the fetcher is absent, for the same reason as below: with
  // JPLWEB the registry supplies this key itself.
  "horizons",
#endif
#ifndef PROMETHEIA
  // Only while it is genuinely absent: with the plugin compiled in, the
  // registry supplies this key and naming it here as well would be a
  // second answer to the same question.
  "prometheia",
#endif
  // The end of the list -- and what keeps it from being EMPTY when every
  // source above is compiled in, as in every product build since
  // Prometheia is: an empty initializer is a zero-size array, which GCC
  // accepts as an extension and MSVC refuses (C2466, the first release
  // dry run with both compiled in).
  NULL
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
  for (isrc = 0; rgszEphSrcFuture[isrc] != NULL; isrc++)
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


// The default chain. Ephemeris Prometheia first wherever it is compiled
// in -- every product build since 2026-10-25, the maintainer's decision to
// couple this fork to it (QT_GUI_PLAN.md item 291) -- with the Swiss
// Ephemeris behind it, so what Prometheia cannot answer (a date outside
// the planets file it found, a body its catalog lacks, a planetary moon)
// is still answered. The Swiss Ephemeris alone where Prometheia is not
// compiled, which is the Win32 oracle; the Matrix legacy cast without it.
CONST char *SzEphSourceDefault()
{
#ifdef EPHEM
#ifdef PROMETHEIA
  return "prometheia,swiss";
#else
  return "swiss";
#endif
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


// The query's instant as a CI, for a source whose question is asked in
// calendar terms rather than in Julian days -- JPL Horizons' URL is the
// only one today. The PLACE is ciCore's, because a topocentric question
// is asked from the chart's own site and the query carries no site of
// its own; only the moment is replaced.
//
// The conversion is the exact inverse of the one that built rJD.
// CastChart() writes UT back into ciCore.tim (TT = TT + (ZZ - SS)), sets
// is.T = MdyToJulian(...) + TT/24, and then rescales it, so
// JulianDayFromTime() undoes the rescale and leaves a half-day offset:
// Astrolog counts a day from midnight where MdyToJulian() hands back the
// noon-based integer. Hence the +0.5 here and the -0.5 into
// JulianToMdy(), which is the same pairing FGetTimeNow() uses.
//
// zon and dst come out ZERO on purpose. The moment is already UT by the
// time any of this runs, and leaving the chart's offsets in place would
// make SzUrlJPLHorizons() shift it a second time.

void CiFromJulianEph(real rJD, CI *pci)
{
  real jd = rJD + 0.5;

  *pci = ciCore;
  pci->tim = (jd - RFloor(jd)) * 24.0;
  JulianToMdy(jd - 0.5, &pci->mon, &pci->day, &pci->yea);
  pci->zon = 0.0;
  pci->dst = 0.0;
}


// Whether the chain's head answers in the Earth's frame with no
// light-time correction applied, so the host has to re-center the whole
// row set itself (EPHCAPS fGeoUncorrected; ComputeEphem()'s
// EphEmulateGeoRows()). ComputeEphem() asked this question five separate
// times and spelled the answer "the chain's head is horizons" each time.
//
// It is an ordinary capability read since phase 6h step 3 registered the
// horizons plugin. It used to end with "or the head's text is literally
// horizons", because the key resolved to no source while the plugin was
// unwritten; that clause is gone, and the suite's check survived its
// removal unchanged because it asks the capability rather than the text.
//
// A head naming a source this build does not have answers false, which
// is right: an unresolvable head cannot be a geocentric-uncorrected one,
// and the chain walk skips it in the same breath.

// The registry index of the source that answers in the Earth's frame
// uncorrected, or ephSrcNone. Found by its CAPABILITY, not its key, so
// the day a second such source exists this keeps working and nothing
// here learns a name.
//
// ComputeEphem() needs the index rather than the flag because it submits
// those objects down a chain of exactly this one source. Putting it in
// the cast's own chain instead would make it a FALLBACK for every other
// object too, and a Swiss row that failed would quietly become a network
// fetch -- which is not what the selection asked for and not what the
// program did before.

int IEphSrcGeoUncorrected()
{
  EPHSRCDEF *pes;
  EPHCAPS caps;
  int isrc;

  for (isrc = 0; isrc < cEphSrcBuiltIn; isrc++) {
    pes = PephsrcGet(isrc);
    if (pes == NULL)
      continue;
    ClearB((pbyte)&caps, sizeof(EPHCAPS));
    (pes->GetCaps)(&caps);
    if (caps.fGeoUncorrected)
      return isrc;
  }
  return ephSrcNone;
}


flag FEphGeoUncorrected()
{
  char sz[cchSzDef];
  EPHSRCDEF *pes;
  EPHCAPS caps;
  int isrc;

  SzEphChainHead(us.szEphemSource, S(sz));
  isrc = IEphSrcFromKey(sz);
  if (isrc == ephSrcNone)
    return fFalse;
  pes = PephsrcGet(isrc);
  if (pes == NULL)
    return fFalse;
  ClearB((pbyte)&caps, sizeof(EPHCAPS));
  (pes->GetCaps)(&caps);
  return caps.fGeoUncorrected;
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


// The rows Ephemeris Settings shows for ONE source: the folder its data
// files are searched for in, when it searches the -Yi folders for them --
// Swiss, JPL, Moshier and Horizons (iepRowPath) -- then its own
// parameters. Swiss had no row at all before, so the dialog's commonest
// source showed "Parameters:" over nothing and the one setting that
// decides whether it works -- where its files are -- could only be typed
// into astrolog.as.

int CEphDialogRowsOf(CONST char *szKey, int *rgiep, int cMax)
{
  int cRow = 0;

  if (rgiep == NULL || cMax <= 0)
    return 0;
  // Moshier too: its formulas cover the Sun, Moon and planets, and every
  // asteroid and centaur still comes from a Swiss file in these folders --
  // with none there Chiron reads 0Ari00 under it. And Horizons: what it
  // has no target for -- the nodes, Lilith, most asteroids -- the Swiss
  // Ephemeris computes (FEphSubmit()), from these folders.
  //
  // Not Prometheia. Its files are named one by one, and the folder row
  // above "Ephemeris File" read as two ways to say one thing -- it is the
  // Swiss folder, and it only fed a search the dialog never showed
  // (2026-10-25, the maintainer: "ephemeris folder then ephemeris file is
  // confusing"). An empty Prometheia field now says which file that
  // search finds (SzEphPromEmpty()).
  if (FEqSz(szKey, "swiss") || FEqSz(szKey, "jpl") ||
    FEqSz(szKey, "moshier") || FEqSz(szKey, "horizons"))
    rgiep[cRow++] = iepRowPath;
  if (FEqSz(szKey, "prometheia")) {
    // In the order a reader wants them: the planets, the planets outside
    // that file's years, then the asteroids. The table's own order is
    // the settings file's index space and is appended to, never sorted.
    CONST int rgiepProm[] = {epPrometheiaEphemeris, epPrometheiaLongspan,
      epPrometheiaCatalog, epPrometheiaPerturbers};
    int i;
    for (i = 0; i < (int)(sizeof(rgiepProm)/sizeof(int)) && cRow < cMax;
      i++)
      rgiep[cRow++] = rgiepProm[i];
    return cRow;
  }
  return cRow + CEphParamRowsOf(szKey, rgiep + cRow, cMax - cRow);
}


// What an EMPTY row means, for Ephemeris Settings: an empty value is never
// "nothing", it is the source's own default, and a blank box said none of
// that. Two lengths. The short one is greyed in the field itself, which is
// 148 dialog units -- about 36 characters -- so it names the default and
// no more. The long one is a sentence, for the field's tooltip and, in the
// Windows build, the line under the rows while the field has focus.

void SzEphRowEmpty(int iep, flag fLong, char *sz, int cch)
{
  *sz = chNull;
  if (iep == iepRowPath)
    sprintf2(sz, cch, "%s", !fLong ? "empty: the program's own folder" :
      "Left empty, the program's own folder and the current folder are "
      "searched, and any other folders the settings name (-Yi).");
#ifdef SWISS
  else if (iep == epJplFile) {
    // The file it will be, found, or that it is not there -- not the
    // name looked for, which said neither (2026-10-25).
    char szFound[cchSzMax];
    if (FSwissFindFile(SzJplFileDefault(), S(szFound)))
      sprintf2(sz, cch, !fLong ? "empty: uses %s" : "Left empty, %s is "
        "used: it was found where the program looks. Browse to pick "
        "another.", !fLong ? SzJplFileDefault() : szFound);
    else
      sprintf2(sz, cch, !fLong ? "empty: %s, not found" : "Left empty, %s "
        "is looked for in the ephemeris folders, and it is not there, so "
        "the Swiss files answer instead. Browse for a JPL DE file.",
        SzJplFileDefault());
  }
#endif
  else if (iep == epServerUrl)
    sprintf2(sz, cch, !fLong ? "empty: localhost:%d" : "Left empty, the "
      "server on this computer is used: localhost:%d.", nEphSrvPortDefault);
  else if (iep == epServerToken)
    sprintf2(sz, cch, "%s", !fLong ? "empty: none" :
      "Left empty, no token is sent, which a server that requires one "
      "refuses.");
#ifdef PROMETHEIA
  else if (iep >= 0 && iep < cEphParam &&
    FEqSz(rgephparam[iep].szSrc, "prometheia"))
    SzEphPromEmpty(iep, fLong, sz, cch);
#endif
}


// Test one source, ALONE: ask it for the Sun, the Moon and Chiron at the
// current chart's moment, through the same chain walk a cast uses but
// with a chain of that source only, and write what came back -- each
// body's position, or why it has none. True when it works: the Sun and
// the Moon answered, from the ephemeris asked for.
//
// "From the ephemeris asked for" is the half that matters. With its files
// missing the Swiss Ephemeris still answers the Sun and Moon -- from the
// Moshier formulas, and successfully -- so a check that only asked
// whether positions came back would pass a selection with no files at
// all. FSwissEphSubstituted() says when that happened. Chiron is reported
// but not required: it needs an asteroid file (or Prometheia's catalog)
// even when the planets do not, so its absence reads "Works, except for
// Chiron." rather than a failure. The warnings a source raises while
// answering are captured into the result rather than shown, so a missing
// file is one line here and not a box per body.
//
// The caller decides which settings the source sees: Ephemeris Settings
// puts the values as typed into force around this call and restores
// them after, so Test answers for what is on screen, not what was saved.

static CONST char *SzEphErr(int nErr)
{
  switch (nErr) {
  case ephErrUnknownBody:     return "unknown body";
  case ephErrUnsupported:     return "this source does not compute it";
  case ephErrOutsideCover:    return "outside the data's time coverage";
  case ephErrDataUnavailable: return "data unavailable";
  case ephErrPointUndefined:  return "undefined for this orbit";
  case ephErrAmbiguous:       return "ambiguous name";
  case ephErrNumerical:       return "numerical failure";
  default:                    return "internal error";
  }
}

// Append to a report being built in sz (of cch), sentence by sentence.
// Each one starts with a capital and ends with a full stop whatever the
// text it came from does: the reasons are the sources' own, written for a
// command line ("ephemeris '...' not found"), and read as sentences here.
static void EphTestAdd(char *sz, int cch, CONST char *szAdd)
{
  int cchAt = CchSz(sz);

  if (!FSzSet(szAdd))
    return;
  sprintf2(sz + cchAt, cch - cchAt, "%s%c%s%s", cchAt > 0 ? " " : "",
    ChCap(szAdd[0]), szAdd + 1, szAdd[CchSz(szAdd)-1] == '.' ? "" : ".");
}

// The instant Test asks about, said as a date and a UT time, so the
// positions can be held against a chart or another program: "the chart's
// moment" alone named nothing anyone could check.
// A Swiss Ephemeris "SwissEph file 'seas_18.se1' not found in PATH './'"
// as a sentence for someone looking at a dialog: the file, and that it is
// not in the folders searched -- which the result names once, at its end,
// rather than in the library's spelling of the path ("./" for the current
// folder) once per body.
static void SzEphTestReason(CONST char *szCap, char *sz, int cch)
{
  CONST char *pch, *pchEnd;

  for (pch = szCap; *pch && !FEqRgch(pch, "SwissEph file '", 15, fFalse);
    pch++)
    ;
  if (*pch) {
    pch += 15;
    for (pchEnd = pch; *pchEnd && *pchEnd != '\''; pchEnd++)
      ;
    if (*pchEnd && FEqRgch(pchEnd, "' not found in PATH", 19, fFalse)) {
      sprintf2(sz, cch, "%.*s is not in the folders searched",
        (int)(pchEnd - pch), pch);
      return;
    }
  }
  sprintf2(sz, cch, "%s", szCap);
}

static CONST char *SzEphTestMoment()
{
  static char szMoment[cchSzDef];
  real jd = JulianDayFromTime(is.T) + 0.5;
  int mon, day, yea, nMin;

  // To the nearest minute, before anything is split off: 11:01 is
  // 19.01666... hours, and its minutes truncated read 19:00.
  jd = RFloor(jd * (24.0*60.0) + 0.5) / (24.0*60.0);
  nMin = (int)RFloor((jd - RFloor(jd)) * (24.0*60.0) + 0.5);
  JulianToMdy(jd - 0.5, &mon, &day, &yea);
  sprintf2(S(szMoment), "%d-%02d-%02d %02d:%02d UT, the chart's moment",
    yea, mon, day, nMin / 60, nMin % 60);
  return szMoment;
}

flag FEphTestSource(CONST char *szKey, char *sz, int cch)
{
  static EPHQUERY eq;   // Big; and the test is modal, one at a time.
  CONST int rgobj[3] = {oSun, oMoo, oChi};
  EPHSRCDEF *pes;
  EPHCAPS caps;
  char szWhy[cchSzMax*2], szCap[cchSzMax*2], szT[cchSzMax*3];
  char *pch;
  int isrc, i, nState;
  flag fNoEphSav, rgfOk[3], fSubst = fFalse, fOk, fAny;

  *sz = chNull;
  isrc = IEphSrcFromKey(szKey);
  pes = PephsrcGet(isrc);
  if (pes == NULL) {
    sprintf2(sz, cch, "Does not work. '%s' is not built into this program.",
      szKey);
    return fFalse;
  }
  if (!pes->FAvailable(S(szWhy))) {
    sprintf2(sz, cch, "%s", "Does not work.");
    EphTestAdd(sz, cch, szWhy);
    return fFalse;
  }
  ClearB((pbyte)&caps, sizeof(EPHCAPS));
  pes->GetCaps(&caps);
  if (caps.fLegacyCast) {
    // Matrix and None answer through CastChart()'s own hook, not a query.
    if (FEqSz(szKey, "none")) {
      sprintf2(sz, cch, "%s", "Does not work. None computes no positions: "
        "every body reads 0Ari00'00\".");
      return fFalse;
    }
#ifdef MATRIX
    // Matrix: nothing can be missing, but its answer can still be held
    // against a chart. Its formulas run as the cast runs them, into the
    // chart's own arrays (cp0, which planet[] and space[] name), so those
    // are put back after: the chart on screen is drawn from them. Chiron
    // is asked for even when the chart leaves it out, and from the Earth
    // whatever the chart's centre, as the other sources are asked.
    {
      CP cpSav = cp0;
      flag fIgnChiSav = ignore[oChi];
      int objCenterSav = us.objCenter;
      real rgr[3], rNod, rNodAlt;

      ignore[oChi] = fFalse;
      us.objCenter = oEar;
      ComputePlanets();
      ComputeLunar(&planet[oMoo], &planetalt[oMoo], &rNod, &rNodAlt);
      rgr[0] = planet[oSun]; rgr[1] = planet[oMoo]; rgr[2] = planet[oChi];
      cp0 = cpSav;
      ignore[oChi] = fIgnChiSav;
      us.objCenter = objCenterSav;
      sprintf2(sz, cch, "%s", "Works.");
      pch = szT;
      *pch = chNull;
      for (i = 0; i < 3; i++) {
        CONST char *szPos;
        for (szPos = SzZodiac(rgr[i]); *szPos == ' '; szPos++)
          ;
        sprintf2(pch, (int)sizeof(szT) - (int)(pch - szT), "%s%s %s",
          i > 0 ? ", " : "", szObjDisp[rgobj[i]], szPos);
        pch += CchSz(pch);
      }
      sprintf2(pch, (int)sizeof(szT) - (int)(pch - szT), " at %s",
        SzEphTestMoment());
      EphTestAdd(sz, cch, szT);
      EphTestAdd(sz, cch, "From formulas built into the program, so there "
        "is no file or connection that could be missing");
      return fTrue;
    }
#else
    sprintf2(sz, cch, "%s", "Works. The Matrix formulas are built into "
      "the program; there are no files or connection that could be "
      "missing.");
    return fTrue;
#endif
  }

  EphQueryInit(&eq, JulianDayFromTime(is.T));
  for (i = 0; i < 3; i++)
    FEphQueryAdd(&eq, rgobj[i], 0, oEar, NULL);
  fNoEphSav = is.fNoEphFile;
  is.fNoEphFile = fFalse;   // So a missing file is said, here, once.
#ifdef SWISS
  FSwissEphSubstituted(fTrue);
#endif
  SetWarningCapture(szCap, sizeof(szCap));
  FEphSubmitChain(&eq, &isrc, 1);
  SetWarningCapture(NULL, 0);
#ifdef SWISS
  fSubst = FSwissEphSubstituted(fTrue);
#endif
  is.fNoEphFile = fNoEphSav;

  fAny = fFalse;
  for (i = 0; i < 3; i++) {
    rgfOk[i] = eq.rgisrc[i] != ephSrcNone && eq.rgrow[i].nErr == ephErrNone;
    fAny |= rgfOk[i];
  }
  fOk = rgfOk[0] && rgfOk[1] && !fSubst;
  sprintf2(sz, cch, "%s", !fOk ? "Does not work." : rgfOk[2] ? "Works." :
    "Works, except for Chiron.");

  // What came back, at one instant, so it can be held against a chart --
  // but not when it came from another ephemeris than the one asked for:
  // those positions are right and say nothing about this selection, and
  // shown they read as its answer.
  if (fAny && !fSubst) {
    pch = szT;
    *pch = chNull;
    for (i = 0; i < 3; i++) {
      CONST char *szPos;
      if (!rgfOk[i])
        continue;
      // SzZodiac() pads its degrees to two places for a column.
      for (szPos = SzZodiac(Mod(eq.rgrow[i].rg[0] + is.rSid));
        *szPos == ' '; szPos++)
        ;
      sprintf2(pch, (int)sizeof(szT) - (int)(pch - szT), "%s%s %s",
        pch > szT ? ", " : "", szObjDisp[rgobj[i]], szPos);
      pch += CchSz(pch);
    }
    sprintf2(pch, (int)sizeof(szT) - (int)(pch - szT), " at %s",
      SzEphTestMoment());
    EphTestAdd(sz, cch, szT);
  }

#ifdef SWISS
  // The files asked for were not there, and the library answered from
  // another ephemeris without failing: the one case where positions came
  // back and the selection still does not work. Name what was looked for.
  if (fSubst) {
    if (FEqSz(szKey, "jpl")) {
      CONST char *pchF = SzSet(us.rgszEphParam[epJplFile]), *pchB;
      for (pchB = pchF + CchSz(pchF); pchB > pchF && pchB[-1] != chDirSep &&
        pchB[-1] != '/'; pchB--)
        ;
      sprintf2(S(szT), "The JPL file %s is not in the folders searched, so "
        "the Sun and Moon came from the Swiss Ephemeris files instead",
        FSzSet(pchB) ? pchB : SzJplFileDefault());
    } else
      sprintf2(S(szT), "%s", "No Swiss Ephemeris planet file is in the "
        "folders searched, so the Sun and Moon came from the Moshier "
        "formulas instead");
    EphTestAdd(sz, cch, szT);
  }
#endif

  if (!fAny) {
    // Nothing answered: one reason, the most specific there is. A source
    // that is not connected says why in its state (the server's own
    // error); otherwise the source's own warning; otherwise why the walk
    // got nothing from it, without the walk's "key (...)" wrapping.
    nState = pes->State(S(szWhy));
    if (nState != esReady && FSzSet(szWhy)) {
      // The server's state reads "Ephemeris Server <addr>: retry in Ns --
      // <error>"; the retry is the live connection's business, not this
      // test's, which drops it afterwards.
      for (pch = szWhy; *pch && !FEqRgch(pch, " -- ", 4, fFalse); pch++)
        ;
      if (*pch)
        EphTestAdd(sz, cch, pch + 4);
      else
        EphTestAdd(sz, cch, szWhy);
    } else if (FSzSet(szCap))
      EphTestAdd(sz, cch, szCap);
    else if (FSzSet(SzEphNoSourceWhy())) {
      CONST char *pchW = SzEphNoSourceWhy();
      i = CchSz(szKey);
      if (FEqRgch(pchW, szKey, i, fFalse) && pchW[i] == ' ' &&
        pchW[i+1] == '(')
        sprintf2(S(szT), "%.*s", CchSz(pchW) - i - 3, pchW + i + 2);
      else
        sprintf2(S(szT), "%s", pchW);
      EphTestAdd(sz, cch, szT);
    } else {
      sprintf2(S(szT), "The Sun: %s", SzEphErr(eq.rgrow[0].nErr));
      EphTestAdd(sz, cch, szT);
      // A file that does not reach the date: Prometheia has a field for
      // exactly this, and the reader should be told it is the fix.
      if (eq.rgrow[0].nErr == ephErrOutsideCover &&
        FEqSz(szKey, "prometheia"))
        EphTestAdd(sz, cch, "A DE441 file in \"Planets, Other Years\", "
          "such as linux_m13000p17000.441, covers -13000 to 17000");
    }
  } else {
    // Some answered: say what each one that did not lacked.
    for (i = 0; i < 3; i++) {
      if (rgfOk[i])
        continue;
      if (eq.rgrow[i].nErr == ephErrUnknownBody && FEqSz(szKey, "prometheia")
        && !FSzSet(us.rgszEphParam[epPrometheiaCatalog]))
        sprintf2(S(szT), "%s: no data -- asteroids and centaurs come from "
          "the Asteroid Catalog, and none is set", szObjDisp[rgobj[i]]);
      else if (FSzSet(szCap)) {
        char szR[cchSzMax*2];
        SzEphTestReason(szCap, S(szR));
        sprintf2(S(szT), "%s: %s", szObjDisp[rgobj[i]], szR);
      } else
        sprintf2(S(szT), "%s: %s", szObjDisp[rgobj[i]],
          SzEphErr(eq.rgrow[i].nErr));
      EphTestAdd(sz, cch, szT);
      szCap[0] = chNull;   // One source warning is said once.
    }
  }

#ifdef SWISS
  if (FEqSz(szKey, "swiss") || FEqSz(szKey, "jpl") ||
    FEqSz(szKey, "moshier")) {
    char szDir[cchSzMax*2];
    // The Ephemeris Folder is -Yi1, and the search takes every -Yi folder
    // and the program's own: a folder typed here that holds nothing is
    // masked by another that does, and Test would say Works about a
    // setting that did nothing. Say so.
    if (FSzSet(us.rgszPath[1])) {
      SzPathDirAbs(us.rgszPath[1], S(szDir));
      if (!FDirExists(szDir)) {
        sprintf2(S(szT), "The Ephemeris Folder %s does not exist", szDir);
        EphTestAdd(sz, cch, szT);
      } else if (!FDirHasEphem(szDir)) {
        sprintf2(S(szT), "The Ephemeris Folder %s holds no Swiss Ephemeris "
          "files", szDir);
        EphTestAdd(sz, cch, szT);
      }
    }
    SzSwissPathShown(S(szDir));
    sprintf2(S(szT), "Searched: %s", FSzSet(szDir) ? szDir : "no folder");
    EphTestAdd(sz, cch, szT);
  }
#endif
  return fOk;
}


// The line under Ephemeris Settings' rows for the picked source, until a
// Test replaces it. Both builds' dialogs show this, so it is said once.
//
// Only the source IN USE has a state worth showing, and only while the
// rows hold what it is using (fTyped false): any other source's state is
// about settings it is not running on, and a local source's "ready" says
// nothing about whether its files are there -- only Test can say that.
// A connecting server's dots cycle with nTick, because a wait that stands
// still reads as broken.

void SzEphDialogStatus(CONST char *szKey, flag fTyped, int nTick, char *sz,
  int cch)
{
  EPHSRCDEF *pes = PephsrcGet(IEphSrcFromKey(szKey));
  char szState[cchSzMax], szHead[cchSzDef];
  int nState;

  *sz = chNull;
  if (pes == NULL) {
    sprintf2(sz, cch, "%s", "Pick an ephemeris.");
    return;
  }
  if (!pes->FAvailable(S(szState))) {
    EphTestAdd(sz, cch, szState);
    EphTestAdd(sz, cch, "Pick another ephemeris");
    return;
  }
  SzEphChainHead(us.szEphemSource, S(szHead));
  if (fTyped || !FEqSz(szHead, szKey)) {
    sprintf2(sz, cch, "%s", "Press Test to check that it works.");
    return;
  }
  nState = pes->State(S(szState));
  if (nState == esConnecting)
    sprintf2(sz, cch, "In use. %s%.*s", szState, nTick % 3 + 1, "...");
  else {
    if (nState == esFailed)
      sprintf2(sz, cch, "In use, and failing: %s", szState);
    else if (FEqRgch(szState, "ready: ", 7, fFalse))
      sprintf2(sz, cch, "In use: %s", szState + 7);
    else if (FEqSz(szState, "ready") || FEqRgch(szState, "ready (", 7,
      fFalse) || FEqSz(szState, "closed") || !FSzSet(szState))
      sprintf2(sz, cch, "%s", "In use");
    else
      sprintf2(sz, cch, "In use. %s", szState);
    if (sz[CchSz(sz)-1] != '.')
      sprintf2(sz + CchSz(sz), cch - CchSz(sz), "%s", ".");
    sprintf2(sz + CchSz(sz), cch - CchSz(sz), "%s",
      " Press Test to check that it works.");
  }
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
      // Files on and no backend of the old three named: the default chain,
      // which the old fields meant by it -- "the ephemeris files" -- and
      // which is Prometheia's then Swiss's since it is compiled in. Spelt
      // "swiss" here, every settings file carrying "=b" (nrvate.as, and
      // the shipped astrolog.as before its -bE line) cast from Swiss alone.
      sprintf2(S(sz), "%s", SzEphSourceDefault()); break;
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


// Why a source that returned false from FSubmit() attempted nothing, in
// its own words. The walk clears it before each submit and reports it in
// place of "attempted nothing" when the source filled it in, so a chain
// with no source behind the one that declined says why it declined.
static char szEphSubmitWhy[cchSzDef] = "";

void SetEphSubmitWhy(CONST char *sz)
{
  sprintf2(S(szEphSubmitWhy), "%s", sz);
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
    szEphSubmitWhy[0] = chNull;
    if (!pephsrc->FSubmit(pq)) {
      if (cWhy < 2) {
        int cchAt = CchSz(szEphNoSourceWhy);
        sprintf2(szEphNoSourceWhy + cchAt, cchSzMax - cchAt,
          "%s%s (%s)", cWhy > 0 ? "; " : "", pephsrc->szKey,
          FSzSet(szEphSubmitWhy) ? szEphSubmitWhy : "attempted nothing");
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


// Submit the cast's query down the chain us.szEphemSource names.
//
// With Horizons at the head (the source answering in the Earth's frame),
// ComputeEphem() has already sent it every object it has a target for, so
// this query holds only the REST: the nodes, Lilith, the Uranians, any
// asteroid with no Horizons id. Asking Horizons for those attempts
// nothing. Upstream's Horizons selection (nSwissEph 3) computed them with
// FSwissPlanet() under SEFLG_JPLEPH -- the Swiss Ephemeris over the JPL
// file, which falls back to the Swiss files and then Moshier when the file
// is not there -- and that is the "jpl" source, as the legacy spelling's
// chain already says ("horizons,jpl", EphSourceSetShadow()). So the head
// is left out, and when nothing is named after it, "jpl" answers. Without
// this, Horizons alone -- what the Ephemeris Settings dialog writes --
// cast both nodes at 0Ari00'00" and warned that every body read that
// (found 2026-10-25, by the maintainer).

flag FEphSubmit(EPHQUERY *pq)
{
  int rgisrc[cEphSrcBuiltIn], cisrc, isrcGeo, i, j;

  cisrc = CEphChainSrc(us.szEphemSource, rgisrc, cEphSrcBuiltIn);
  if (FEphGeoUncorrected()) {
    isrcGeo = IEphSrcGeoUncorrected();
    for (i = j = 0; i < cisrc; i++)
      if (rgisrc[i] != isrcGeo)
        rgisrc[j++] = rgisrc[i];
    cisrc = j;
    if (cisrc == 0 && (rgisrc[0] = IEphSrcFromKey("jpl")) >= 0)
      cisrc = 1;
  }
  return FEphSubmitChain(pq, rgisrc, cisrc);
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
  "none", "None", "No ephemeris: every body reads 0Ari00'00\"; the house cusps "
  "are still computed.",
  FAvailableNone, GetCapsNone, StateNone, StartNone, StopNone,
  FSubmitNone, FReadNone, HintNone, NLookupNone
};
