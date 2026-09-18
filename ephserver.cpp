/*
** Astrolog (Version 8.00) File: ephserver.cpp
**
** The Ephemeris Server source: a remote ephemeris over protocol version
** 4, from either astrolog-ephd or Prometheia's prometheiad.
** EPHEMERIS_PLUGINS_PLAN.md section 4.2, phase 6.
**
** This file is the SOURCE, not the client. Everything here is the
** registry's business -- availability, capabilities, state, and turning
** an EPHQUERY into a submit and a read. How bytes reach the server is a
** TRANSPORT, registered by whichever backend has one (ephem.h, EPHTRANS),
** so this compiles identically in every build and contains no "#ifdef"
** for a platform. A build with no transport registered has the source
** listed and unavailable, with that as its reason, exactly as an
** uncompiled plugin or a missing data file is.
*/

#include "astrolog.h"
#include "ephem.h"


/*
******************************************************************************
** The Transport.
******************************************************************************
*/

// At most one transport per build, and it registers itself before the
// first cast -- the Qt backend from its own startup, the console client
// from its. There is no ordering hazard here worth a registry of its
// own: a source asked before any transport registered answers
// "unavailable", which is what it is.

static CONST EPHTRANS *s_ptrans = NULL;

void EphSrvTransportSet(CONST EPHTRANS *ptrans)
{
  s_ptrans = ptrans;
}


CONST EPHTRANS *PephtransGet()
{
  return s_ptrans;
}


/*
******************************************************************************
** The Source.
******************************************************************************
*/

static flag FAvailableServer(char *szWhy, int cch)
{
  char szT[cchSzMax];

  if (s_ptrans == NULL) {
    if (szWhy != NULL)
      sprintf2(szWhy, cch, "%s",
        "There is no transport in this build.");
    return fFalse;
  }
  if (s_ptrans->FAvail != NULL && !s_ptrans->FAvail(S(szT))) {
    // The transport's own reason, not a paraphrase of it: it knows
    // whether the address is unset, the URL unparseable, or TLS absent.
    if (szWhy != NULL)
      sprintf2(szWhy, cch, "%s", szT);
    return fFalse;
  }
  return fTrue;
}


// What a version 4 server can be asked for. This is the PROTOCOL's
// shape, not a particular server's -- which bodies and points the server
// at the other end actually serves is in its WELCOME, and an object it
// refuses comes back as a per-object error like any other. Saying less
// here than the protocol allows would make the chain walk skip objects
// this source could have answered.
static void GetCapsServer(EPHCAPS *pcaps)
{
  ClearB((pbyte)pcaps, sizeof(EPHCAPS));
  pcaps->fBody = fTrue;
  pcaps->fOrbitPoint = fTrue;
  pcaps->fStar = fTrue;
  pcaps->fSpeeds = fTrue;
}


static int StateServer(char *sz, int cch)
{
  char szT[cchSzMax];

  if (s_ptrans == NULL) {
    if (sz != NULL)
      sprintf2(sz, cch, "%s", "There is no transport in this build.");
    return esFailed;
  }
  if (s_ptrans->State == NULL) {
    if (sz != NULL)
      sprintf2(sz, cch, "%s", s_ptrans->szName);
    return esReady;
  }
  {
    int es = s_ptrans->State(S(szT));
    if (sz != NULL)
      sprintf2(sz, cch, "%s", szT);
    return es;
  }
}


static void StartServer()
{
  if (s_ptrans != NULL && s_ptrans->Start != NULL)
    s_ptrans->Start();
}


static void StopServer()
{
  if (s_ptrans != NULL && s_ptrans->Stop != NULL)
    s_ptrans->Stop();
}


// One query is one cast: the whole question goes in a single submit, and
// the loop reads it back per object afterwards. That ordering is the
// whole point of the host API for a remote source -- a per-object fetch
// is a round trip per body (EPHEMERIS_CLIENT_PLAN.md lesson 1).
static flag FSubmitServer(EPHQUERY *pq)
{
  int i;

  if (s_ptrans == NULL || s_ptrans->FSubmit == NULL ||
    s_ptrans->FRead == NULL)
    return fFalse;
  // The transport attempts the whole query at once -- a per-object fetch
  // is a round trip per body -- and returns once the answer is there or
  // is not coming. False means it attempted NOTHING, a transport that is
  // down, and every object stays open for the next source in the chain.
  if (!s_ptrans->FSubmit(pq))
    return fFalse;
  // Returning true obliges this source to have filled a row for every
  // object still open: a success, or a per-object error. An object the
  // server did not answer must carry an error rather than a zero row,
  // because the chain walk claims any row whose nErr is ephErrNone --
  // so a silent zero would be claimed as an ANSWER and the fallback
  // would never be reached.
  for (i = 0; i < pq->cobj; i++) {
    if (pq->rgisrc[i] != ephSrcNone)
      continue;
    if (!s_ptrans->FRead(pq, i, &pq->rgrow[i])) {
      ClearB((pbyte)&pq->rgrow[i], sizeof(EPHROW));
      pq->rgrow[i].nErr = ephErrDataUnavailable;
      pq->rgrow[i].nNativeRes = ephNativeNone;
    }
  }
  return fTrue;
}


static flag FReadServer(CONST EPHQUERY *pq, int iObj, int iRow, EPHROW *prow)
{
  // The rows were filled by FSubmit above, like every other source: one
  // instant per query, so there is one row per object.
  if (iObj < 0 || iObj >= pq->cobj || pq->rgisrc[iObj] == ephSrcNone ||
    iRow != 0)
    return fFalse;
  *prow = pq->rgrow[iObj];
  return fTrue;
}


static void HintServer(CONST EPHQUERY *pq)
{
  // The prefetch of the next window is the transport's own, driven by
  // the animation it can see; there is nothing useful to pass down from
  // here yet. Phase 6's remaining increments give this a body.
}


static int NLookupServer(CONST char *sz, EPHMATCH *rgm, int cMax)
{
  // LOOKUP is a protocol message (section 3.9) and the transport has to
  // round-trip it. Until a transport implements it, no matches -- which
  // is what a source that cannot resolve a name says.
  return 0;
}


EPHSRCDEF ephsrcServer = {
  "server", "Ephemeris Server",
  "A version 4 ephemeris server over the network.",
  FAvailableServer, GetCapsServer, StateServer, StartServer,
  StopServer, FSubmitServer, FReadServer, HintServer, NLookupServer
};

/* ephserver.cpp */
