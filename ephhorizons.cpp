/*
** Astrolog (Version 8.00) File: ephhorizons.cpp
**
** The JPL Horizons source: positions fetched from NASA/JPL's Horizons
** web service, one request per body.
** EPHEMERIS_PLUGINS_PLAN.md section 4.2, phase 6h step 3.
**
** This file is the SOURCE. The question and the reply are io.cpp's --
** SzUrlJPLHorizons() composes the URL, FParseJPLHorizons() reads the
** answer, and GetJPLHorizons() is the pair with the fetch between them.
** Nothing about the HTTP is decided here, and nothing about the object
** mapping is decided there.
**
** Two things make this source different from every other one in the
** registry, and both are declared rather than assumed:
**
**  - Its rows come back GEOCENTRIC and light-time-uncorrected whatever
**    the cast's center is, because that is the question Astrolog asks
**    Horizons. GetCaps() says so with fGeoUncorrected, and the host
**    re-centers the whole row set afterwards (EphEmulateGeoRows() in
**    calc.cpp). Until 2026-09-20 that convention lived in five separate
**    negative conditions inside ComputeEphem(), each naming this source
**    by name; a capability is one statement in one place.
**
**  - Its coverage is PER OBJECT, not per source. The recorded corpus
**    settles it: Mars is refused before 1600 and Pluto before 1800, and
**    Pluto's system BARYCENTRE answers at an instant its body centre
**    does not, because a body centre needs the satellite solution on
**    top of the planetary ephemeris. So FAvailable() cannot carry a
**    coverage range and a refusal is reported per object.
*/

#include "astrolog.h"
#include "ephem.h"

#ifdef JPLWEB


/*
******************************************************************************
** The Source.
******************************************************************************
*/

// Compiled in or not. Reachability is deliberately NOT tested here: a
// source is unavailable when it cannot work at all, and a network that
// is down is a per-object failure at cast time (section 4.2), the same
// rule the Swiss sources follow for a missing data file. Probing the
// service to answer a dialog would also be a request nobody asked for,
// against a public service this project fetches from exactly once.

static flag FAvailableHorizons(char *szWhy, int cch)
{
  return fTrue;
}


// What this source can be asked for. Bodies only: Horizons has no fixed
// star catalogue of the kind -XU wants, and an orbit point is a property
// of an orbit rather than a target id it accepts. Saying less here than
// the source can do would make the chain walk skip objects it could have
// answered; saying more would make it claim objects it then fails, and a
// claimed failure is one the fallback never gets to try.

static void GetCapsHorizons(EPHCAPS *pcaps)
{
  ClearB((pbyte)pcaps, sizeof(EPHCAPS));
  pcaps->fBody = fTrue;
  pcaps->fSpeeds = fTrue;
  // The one that deletes ComputeEphem()'s five negative rules.
  pcaps->fGeoUncorrected = fTrue;
}


static int StateHorizonsSrc(char *sz, int cch)
{
  if (sz != NULL)
    sprintf2(sz, cch, "%s", "ready (fetched per cast from ssd.jpl.nasa.gov)");
  return esReady;
}


static void StartHorizons()
{
}


static void StopHorizons()
{
}


// The Horizons target id for one Astrolog object, or ephNoIdHor for
// "this source does not serve it". The two rules are ComputeEphem()'s
// own, from the branch this source replaces: a custom object whose Swiss
// type is 4 is a Horizons body carrying its own id, and a main object is
// served when rgObjJPL[] has a nonzero entry for it.
//
// The sentinel is NOT zero, and that matters here where it would not in
// most tables: Horizons' id 0 is the SOLAR SYSTEM BARYCENTRE, which is a
// real target and the one a barycentric Sun is asked for. rgObjJPL[]
// uses 0 for "no entry", so the two meanings have to be told apart
// before the lookup rather than after it.
//
// rgObjJPL[oEar] is 0 on purpose and that is not an omission. The Earth
// is the CENTER the queries are made from, so asking Horizons for it is
// degenerate and JPL refuses it outright -- "earth-1990" in the recorded
// corpus is that refusal, kept as a fixture precisely because the code
// used to skip the Earth without ever saying why.

int NEphHorizonsId(int iobj)
{
  if (FCust(iobj))
    return rgTypSwiss[iobj - custLo] == 4 ?
      rgObjSwiss[iobj - custLo] : ephNoIdHor;
  if (!FBetween(iobj, 0, cThing))
    return ephNoIdHor;
  // The barycentric Sun is Horizons' own 0, the solar system barycentre.
  if (iobj == oSun && us.fBarycenter)
    return 0;
  return rgObjJPL[iobj] > 0 ? rgObjJPL[iobj] : ephNoIdHor;
}


// Answer the query's open objects, one fetch each. Synchronous, like the
// local sources: the fetch blocks, which is what GetJPLHorizons() has
// always done, and the host's submit-once/read-later shape is what a
// later increment needs to make these concurrent rather than serial.
//
// The INSTANT comes from the query, not from ciCore. GetJPLHorizons()
// reads the global, which is correct today only because ComputeEphem()
// is called once per cast with the same moment ciCore carries -- an
// equivalence nothing stated and nothing checked. The suite checks it
// now (TestHorizonsInstantQt), and a source that takes its instant as an
// argument is one that can be asked for a second moment.

static flag FSubmitHorizons(EPHQUERY *pq)
{
  EPHROW *prow;
  CI ci;
  real r1, r2, r3, r4, r5, r6;
  int i, id;
  flag fSavTrue, fAny = fFalse;

  CiFromJulianEph(pq->rJD, &ci);
  // us.fTruePos is forced on for the fetch whenever the cast's center is
  // not the Earth, because the re-centring emulation wants the true
  // position and applies the light-time shift itself afterwards. This is
  // the borrow ComputeEphem()'s own Horizons branch made around each
  // call, moved here with the call.
  fSavTrue = us.fTruePos;
  if (us.objCenter != oEar)
    us.fTruePos = fTrue;

  for (i = 0; i < pq->cobj; i++) {
    if (pq->rgisrc[i] != ephSrcNone)
      continue;   // Another source in the walk already answered this one.
    prow = &pq->rgrow[i];
    // A fixed star, or an orbit point: not this source's, and saying so
    // rather than failing lets the chain reach one whose it is.
    if (pq->rgszName[i] != NULL) {
      prow->nErr = ephErrUnsupported;
      continue;
    }
    id = NEphHorizonsId(pq->rgobj[i]);
    if (id == ephNoIdHor) {
      prow->nErr = ephErrUnsupported;
      continue;
    }
    fAny = fTrue;
    if (!GetJPLHorizonsAt(id, &ci, &r1, &r2, &r3, &r4, &r5, &r6, NULL)) {
      // A refusal and a network failure look identical from here: both
      // are "no three rows came back". The corpus says JPL's refusals
      // are per object and per target id -- Mars before 1600, Pluto
      // before 1800, and the barycentre answering where the body centre
      // does not -- so this cannot be narrowed to a coverage error
      // without parsing the reason out of the reply, which the next
      // increment does. ephErrDataUnavailable is the honest wider one,
      // and the chain walk treats every failure the same way.
      prow->nErr = ephErrDataUnavailable;
      continue;
    }
    // The six in the PROTOCOL's column order. GetJPLHorizons() hands
    // back FSwissPlanet()'s argument order, so the transposition is
    // here, as it is in every other source.
    prow->rg[0] = r1;   // longitude
    prow->rg[1] = r2;   // latitude
    prow->rg[2] = r4;   // distance
    prow->rg[3] = r3;   // longitude rate
    prow->rg[4] = r5;   // latitude rate
    prow->rg[5] = r6;   // distance rate
    prow->nErr = ephErrNone;
    prow->nNativeRes = id;
  }
  us.fTruePos = fSavTrue;
  // False means this source attempted NOTHING, so every object stays
  // open for the next in the chain with no row of its own. Returning
  // true with rows of ephErrUnsupported would be the same outcome by a
  // longer road, but it would also claim a cast in which this source
  // answered nothing, which the fallback notice reads.
  return fAny;
}


static flag FReadHorizons(CONST EPHQUERY *pq, int iObj, int iRow,
  EPHROW *prow)
{
  if (iObj < 0 || iObj >= pq->cobj || pq->rgisrc[iObj] == ephSrcNone ||
    iRow != 0)
    return fFalse;
  *prow = pq->rgrow[iObj];
  return fTrue;
}


static void HintHorizons(CONST EPHQUERY *pq)
{
  // Prefetching the next window would mean a second request per body
  // per animation step against a public service. Not without a cache
  // that outlives the cast, which is the next increment's.
}


static int NLookupHorizons(CONST char *sz, EPHMATCH *rgm, int cMax)
{
  int iobj, c = 0;

  // The names this source can resolve are the objects it serves, and
  // Astrolog already knows what each of them is called. Exact matches
  // first and no prefix walk: a Horizons id is not something a user
  // guesses at, and an ambiguous answer here would pick a body.
  for (iobj = 0; iobj <= cThing && c < cMax; iobj++) {
    if (NEphHorizonsId(iobj) == ephNoIdHor ||
      NCompareSzI(sz, szObjName[iobj]) != 0)
      continue;
    rgm[c].nQuality = 0;
    rgm[c].nNative = NEphHorizonsId(iobj);
    c++;
  }
  return c;
}


EPHSRCDEF ephsrcHorizons = {
  "horizons", "JPL Horizons",
  "NASA/JPL's Horizons service, fetched over the network per cast.",
  FAvailableHorizons, GetCapsHorizons, StateHorizonsSrc, StartHorizons,
  StopHorizons, FSubmitHorizons, FReadHorizons, HintHorizons,
  NLookupHorizons
};

#endif // JPLWEB

/* ephhorizons.cpp */
