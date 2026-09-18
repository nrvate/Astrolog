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
    eph::Profile pf;
    eph::Object obj;
    std::vector<uint8_t> rgbThis;
    double topo[3];
    int ip;

    // A star is named, not numbered, and needs no Swiss spec.
    if (pq->rgszName[i] != NULL) {
      obj = eph::Object();
      obj.kind = eph::kObjStar;
      obj.name = pq->rgszName[i];
      pf = eph::Profile();
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
