/*
** Astrolog (Version 8.00) File: ephreq.h
**
** The host question as a version 4 REQUEST: EPHQUERY -> eph::Request.
** EPHEMERIS_PLUGINS_PLAN.md sections 3.4 and 4.1, phase 6.
**
** This is the one translation, shared by every transport. It is a HEADER
** and not part of the core group on purpose: core stays free of the
** protocol (ephem.h says why -- EPHQUERY exists so that the plugin API
** does not drag ephproto.h into every build), and a transport is the only
** thing that needs both sides. Include it from a transport, nowhere else.
**
** What the translation is: for each object in the query, ask
** FSwissPlanetSpec() what Swiss call today's settings would make, then let
** ephswiss.h turn that call into the version 4 object and profile that
** make a server make the SAME call. That is what keeps a remote answer
** comparable with the local one rather than merely plausible -- the
** server is asked the question Astrolog would have asked itself.
*/

#ifndef _EPHREQ_H
#define _EPHREQ_H

#include "ephsrv/ephproto.h"
#include "ephsrv/ephswiss.h"

// The largest correction mask this observer is ADVERTISED to honour that
// asks for no more than m, or 0 when it lists none.
//
// A client must send only advertised capabilities, and 3.5a makes an
// unlisted mask ERROR 11 -- the whole request, not one object -- so asking
// for a term an observer does not list is not merely unhelpful, it fails
// the cast on a conformant server. Astrolog sent the full mask for a
// heliocentric cast and got away with it only because astrolog-ephd
// happened to be lenient about what it accepted; against Prometheia's
// server the same cast was refused outright.
//
// THIS LIVES HERE AND NOT ON eph::Capabilities, WHERE IT BELONGS BY
// SHAPE. ephsrv/ephproto.h is one of the three locked artifacts: it is
// byte-untouchable, and a change to it is a new named drop and never an
// edit. That rule exists because the other project VENDORS the file and
// compares it byte for byte -- which is exactly how this was caught, by
// them asking whether a 19-line addition of mine meant a drop was coming.
// It did not move a single wire byte, and it was still a violation: the
// lock is on the bytes, not on the format. A free function over the
// already-parsed capabilities does the same work outside the lock.
inline uint8_t NBestCorrMaskEph(CONST eph::Capabilities &caps,
  uint8_t observer, uint8_t m)
{
  uint32_t best = 0;
  bool found = false;

  for (size_t i = 0; i < caps.corrMasks.size(); i++) {
    uint32_t obs = caps.corrMasks[i].first, mask = caps.corrMasks[i].second;
    if (observer < 32 && ((obs >> observer) & 1u) != 0 &&
      (mask & ~(uint32_t)m) == 0 && (!found || mask > best)) {
      best = mask;
      found = true;
    }
  }
  return found ? (uint8_t)best : (uint8_t)0;
}


// One object of the query, as the request carries it. The host keeps the
// mapping so a DATA row can be read back to the query's object order: the
// request's objects are only those the translation could express, which
// is not every object the query asked for.
typedef struct _EphReqMap {
  int iObjQuery;   // Index into EPHQUERY's rgobj[], for reading back.
  int iObjReq;     // Index into eph::Request::objs.
} EPHREQMAP;


// Build the request. Returns the number of objects it carries, which is 0
// when nothing in the query has a version 4 form -- a caller with 0 has
// no request to send, which is not an error but the end of the matter.
//
// rgmap must hold objMax entries. deltaT is seconds, the value the host
// used; the request states it (3.5) so a server's own model cannot
// silently become part of the answer. That is not tidiness: a Delta-T
// difference is indistinguishable from a position difference at the
// wire, and this project has already spent a day on one.
inline int CEphRequestFromQuery(CONST EPHQUERY *pq, real rDeltaTSec,
  eph::Request *preq, EPHREQMAP *rgmap)
{
  std::vector<std::vector<uint8_t> > rgbProf;   // each profile's bytes
  int i, cMap = 0;

  preq->profiles.clear();
  preq->objs.clear();

  // One instant, stated in TT with the Delta-T that got us there. The
  // query's own rJD is UT1 (ephem.h), so the conversion happens here and
  // exactly once.
  preq->timeScale = eph::kTimeTT;
  preq->timeMode = eph::kTimeGrid;
  preq->start = eph::Time{pq->rJD + rDeltaTSec / 86400.0, 0.0};
  preq->stepNs = 0;
  preq->nTime = 1;
  preq->deltaTSec = rDeltaTSec;

  for (i = 0; i < pq->cobj; i++) {
    SWISSSPEC ss;
    // Already answered by an earlier source in the chain: not ours to
    // ask about (ephem.h). Sending it anyway is a wasted round trip and
    // its answer is discarded.
    if (pq->rgisrc[i] != ephSrcNone)
      continue;
    eph::Profile pf;
    eph::Object obj;
    std::vector<uint8_t> rgbThis;
    double topo[3];
    int ip;

    // A star is named rather than numbered, but it is computed with the
    // same settings as anything else and must be ASKED for with them.
    // This built a default profile until the phase 8 review: a sidereal
    // or heliocentric chart sent the server a tropical geocentric
    // question, and FReadTransQt marked the answer ephErrNone, so the
    // chain claimed the row and the fallback to the local files never
    // ran. Wrong star longitudes, silently. SwissStarSpec() is the one
    // expression FSwissStar() computes from too.
    if (pq->rgszName[i] != NULL) {
      SWISSSPEC ssStar;
      double topoStar[3];

      obj = eph::Object();
      obj.kind = eph::kObjStar;
      obj.name = pq->rgszName[i];
      SwissStarSpec(&ssStar);
      topoStar[0] = ssStar.topoLon; topoStar[1] = ssStar.topoLat;
      topoStar[2] = ssStar.topoElv;
      if (!eph::swiss::ProfileFromSwiss(ssStar.iflag, ssStar.iobjCent,
        ssStar.nSidMode, topoStar, &pf))
        continue;
    } else {
      if (!FSwissPlanetSpec(pq->rgobj[i], pq->rgcent[i], &ss))
        continue;
      // Which ephemeris a server reads is the server's business, and its
      // datasetId says which it was; the request carries the QUESTION,
      // not our local backend's bit.
      ss.iflag = (ss.iflag & ~(SEFLG_JPLEPH | SEFLG_MOSEPH)) | SEFLG_SWIEPH;
      topo[0] = ss.topoLon; topo[1] = ss.topoLat; topo[2] = ss.topoElv;
      if (!eph::swiss::ProfileFromSwiss(ss.iflag, ss.iobjCent, ss.nSidMode,
        topo, &pf) ||
        !eph::swiss::ObjectFromSwiss(ss.iobj, ss.nPnt > 0 ? ss.nPnt - 1 : -1,
        ss.nNodMethod, &obj))
        // No version 4 form for this call. Not silent: the object stays
        // open and the chain walk offers it to the next source.
        continue;
    }

    // Profiles are deduplicated by their ENCODED BYTES rather than by
    // comparing fields, so two profiles that differ in a field no
    // encoder writes are one profile, which is what the wire will make
    // of them anyway.
    {
      eph::Writer w(&rgbThis);
      eph::WriteProfile(w, pf);
    }
    for (ip = 0; ip < (int)rgbProf.size(); ip++)
      if (rgbProf[ip] == rgbThis)
        break;
    if (ip >= (int)rgbProf.size()) {
      rgbProf.push_back(rgbThis);
      preq->profiles.push_back(pf);
    }
    obj.profile = (uint8_t)ip;
    rgmap[cMap].iObjQuery = i;
    rgmap[cMap].iObjReq = (int)preq->objs.size();
    cMap++;
    preq->objs.push_back(obj);
  }
  return cMap;
}

#endif // _EPHREQ_H
