// The Prometheia source plugin (EPHEMERIS_PLUGINS_PLAN.md 4.2, Appendix
// C, phase 7). The `prometheia` source: a local library over JPL DE and
// SBDB files, an optional dependency detected with
// `pkg-config prometheia`. Without PROMETHEIA defined this file is
// empty, nothing in the default build references the library, and the
// source simply does not exist; with it, the source answers through
// FAvailable when its files are missing -- never a build failure, never
// a crash.
//
// The binding is to the locked version 4 semantics, not to Swiss's:
// objects named by NAIF/SPK-IDs, profiles per 3.4/3.5a, orbit points per
// 3.5a (including the carve-out: a proper subset of the correction terms
// is this engine's own convention and MUST NOT be compared across
// servers), fixed stars under 3.5a's name grammar with ambiguity as
// error 6. As a registered source it carries ephswiss.cpp's delegation
// philosophy: what an object IS is decided by FSwissPlanetSpec(), the
// one function the program has always used, and converted to
// Prometheia's options -- never re-derived here.
//
// Thread safety: one engine handle, one cast at a time, like the Swiss
// path -- the library's engine is not safe for concurrent use, and
// nothing here takes a second one.
#ifdef PROMETHEIA

#include "astrolog.h"
#include "swephexp.h"
#include "ephprom.h"
#include <prometheia/prometheia.h>
#include <math.h>
#include <string.h>

// --------------------------------------------------------------------------
// Parameters (4.2/4.3). The DECLARATIONS are rows of the generated table
// (ephparam.h) and the VALUES are us.rgszEphParam[], both shared with
// every other source -- this file holds neither, and that is the fix:
// holding its own copy of either is what made the command line, the
// settings file and the dialog all miss this source entirely.

static CONST int rgiepPromShared[cepPromParam] = {
  epPrometheiaEphemeris, epPrometheiaCatalog, epPrometheiaPerturbers
};

// This source's index 0..2 to the shared parameter space. The one place
// the two numberings meet.
static int IepPromShared(int iParam)
{
  return FBetween(iParam, 0, cepPromParam-1) ? rgiepPromShared[iParam] : -1;
}

void EphPromSetParam(int iParam, CONST char *szValue)
{
  int iep = IepPromShared(iParam);

  if (iep < 0)
    return;
  // FEphParamSet() drops what the owning source has open when the value
  // actually changes, so the reopen this used to do by hand is the
  // shared path's business now and happens however the value was set.
  FEphParamSet(iep, szValue);
}

CONST char *SzEphPromParam(int iParam)
{
  int iep = IepPromShared(iParam);

  return iep < 0 ? "" : SzSet(us.rgszEphParam[iep]);
}

// --------------------------------------------------------------------------
// The engine.

static prometheia_engine *pephProm = NULL;
static flag fEphPromOpen = fFalse;      // resolved and opened
static char szEphPromState[512];        // the current state text
static char szEphPromEphe[300], szEphPromCat[300], szEphPromPert[300];

// The Delta T hook, bound to Astrolog's own: a finite per-cast value
// when the question carries one, else the user's -Yz override, else the
// chart's model -- swe_deltat(), iterated once so the answer is
// Astrolog's value AT the instant asked and not at that instant's UT
// guess of it. What calc_ut asks is TT - UT1 in seconds at a TT Julian
// date, which is what both of those are.
static real g_rDeltaTSec = rInvalid;

static double FEnumDeltaTProm(void *puser, double jd)
{
  (void)puser;
  if (g_rDeltaTSec != rInvalid)
    return (double)g_rDeltaTSec;
  // The user's -Yz0 override binds both sources to the same TT instant:
  // FSwissPlanet applies it as us.rDeltaT/86400 (calc.cpp), so the same
  // seconds, not the model's, are what this hook must answer too.
  if (us.rDeltaT != rInvalid)
    return (double)us.rDeltaT;
  // swe_deltat() answers in DAYS; the hook is seconds of TT - UT1.
  return swe_deltat(jd - swe_deltat(jd) / 86400.0) * 86400.0;
}

void EphPromStop(void)
{
  prometheia_engine_close(pephProm);
  pephProm = NULL;
  fEphPromOpen = fFalse;
  sprintf2(S(szEphPromState), "unavailable");
}

// Search a bare file name over Astrolog's own -Yi ephemeris directories
// -- the same list SwissEnsurePath() walks -- then over ephe/ beside the
// program, which is where a Prometheia checkout keeps its kernels. A
// name with a directory in it is tried as given. The first hit wins;
// fFalse leaves szPath empty.
flag FEphPromFindFile(CONST char *szFile, char *szPath, int cch)
{
  char szTry[300];
  int i;

  if (!FSzSet(szFile))
    return fFalse;
  if (strchr(szFile, chDirSep) != NULL) {
    if (!FFileExists(szFile))
      return fFalse;
    sprintf2(szPath, cch, "%s", szFile);
    return fTrue;
  }
  for (i = 0; i < 10; i++) {
    if (!FSzSet(us.rgszPath[i]))
      continue;
    sprintf2(S(szTry), "%s%c%s", us.rgszPath[i], chDirSep, szFile);
    if (FFileExists(szTry)) {
      sprintf2(szPath, cch, "%s", szTry);
      return fTrue;
    }
  }
  sprintf2(S(szTry), "ephe%c%s", chDirSep, szFile);
  if (FFileExists(szTry)) {
    sprintf2(szPath, cch, "%s", szTry);
    return fTrue;
  }
  return fFalse;
}

// The ephemeris, catalog and perturbers resolved to paths. An empty
// ephemeris parameter is the source's own default: the DE440 files the
// C API's own example names, searched over the paths above; an empty
// catalog or perturbers means none loaded. fFalse names in szWhy the
// first thing missing.
static flag FEphPromResolve(char *szWhy, int cch)
{
  static CONST char *rgszEphe[] = {"linux_p1550p2650.440", "de440s.bsp"};
  int cEphe = (int)(sizeof(rgszEphe)/sizeof(CONST char *)), i;

  if (FSzSet(SzEphPromParam(epPromEphemeris))) {
    if (!FEphPromFindFile(SzEphPromParam(epPromEphemeris), szEphPromEphe,
      (int)sizeof(szEphPromEphe))) {
      sprintf2(S(szEphPromState), "ephemeris '%s' not found",
        SzEphPromParam(epPromEphemeris));
      if (szWhy != NULL)
        sprintf2(szWhy, cch, "ephemeris '%s' not found",
          SzEphPromParam(epPromEphemeris));
      return fFalse;
    }
  } else {
    for (i = 0; i < cEphe &&
      !FEphPromFindFile(rgszEphe[i], szEphPromEphe,
        (int)sizeof(szEphPromEphe)); i++)
      ;
    if (i >= cEphe) {
      sprintf2(S(szEphPromState), "no DE440 ephemeris on the -Yi paths; "
        "set prometheia.ephemeris");
      if (szWhy != NULL)
        sprintf2(szWhy, cch, "no DE440 ephemeris on the -Yi paths "
          "(set prometheia.ephemeris)");
      return fFalse;
    }
  }
  *szEphPromCat = chNull;
  if (FSzSet(SzEphPromParam(epPromCatalog)) &&
    !FEphPromFindFile(SzEphPromParam(epPromCatalog), szEphPromCat,
      (int)sizeof(szEphPromCat))) {
    sprintf2(S(szEphPromState), "catalog '%s' not found",
      SzEphPromParam(epPromCatalog));
    if (szWhy != NULL)
      sprintf2(szWhy, cch, "catalog '%s' not found",
        SzEphPromParam(epPromCatalog));
    return fFalse;
  }
  *szEphPromPert = chNull;
  if (FSzSet(SzEphPromParam(epPromPerturbers)) &&
    !FEphPromFindFile(SzEphPromParam(epPromPerturbers), szEphPromPert,
      (int)sizeof(szEphPromPert))) {
    sprintf2(S(szEphPromState), "perturbers '%s' not found",
      SzEphPromParam(epPromPerturbers));
    if (szWhy != NULL)
      sprintf2(szWhy, cch, "perturbers '%s' not found",
        SzEphPromParam(epPromPerturbers));
    return fFalse;
  }
  return fTrue;
}

flag FEphPromAvailable(char *szWhy, int cch)
{
  char szWhy2[256];

  if (!FEphPromResolve(szWhy2, (int)sizeof(szWhy2))) {
    if (szWhy != NULL)
      sprintf2(szWhy, cch, "%s", szWhy2);
    return fFalse;
  }
  return fTrue;
}

// What the open engine was opened FROM, so a parameter that has moved
// since is noticed here rather than announced from elsewhere. A
// notification can be missed by any path that writes the value another
// way; comparing at the point of use cannot.
// Not a fixed buffer: the values are us.rgszEphParam[] entries, which are
// FCloneSz'd and unbounded, and a truncated COPY can never compare equal
// to its original -- so a path of 256 characters or more, which an
// absolute path easily is, would take the reopen branch on every single
// call for the life of the process (phase 8 review, D6). Cloned, so the
// comparison is against the whole value.
static char *rgszEphPromOpenedFrom[cepPromParam] = {NULL, NULL, NULL};

flag FEphPromStart(char *szWhy, int cch)
{
  char szWhy2[256];
  prometheia_error err;
  int iep;

  if (fEphPromOpen) {
    for (iep = 0; iep < cepPromParam; iep++)
      if (!FEqSz(SzSet(rgszEphPromOpenedFrom[iep]), SzEphPromParam(iep))) {
        // The engine is open on files the settings no longer name. Close
        // and reopen: 0.x has no way to drop or swap a catalog, and
        // close-and-reopen is the path the library's authors sanction.
        EphPromStop();
        break;
      }
    if (fEphPromOpen)
      return fTrue;
  }
  if (!FEphPromResolve(szWhy2, (int)sizeof(szWhy2))) {
    if (szWhy != NULL)
      sprintf2(szWhy, cch, "%s", szWhy2);
    return fFalse;
  }
  for (iep = 0; iep < cepPromParam; iep++)
    FCloneSz(SzEphPromParam(iep), &rgszEphPromOpenedFrom[iep]);
  // The ABI the library in front of us actually has, against the one
  // these headers describe. EXACTLY equal, not "at least": under 0.x the
  // library grows prometheia_options by APPENDING fields, and a plugin
  // hands over its own struct -- so a NEWER library reads past the end
  // of the older, smaller one this build allocates. A >= test would
  // permit exactly that, and this had no test at all.
  //
  // It cannot bite today because the package is a static library, linked
  // into the binary that compiled against these headers, so the two can
  // never disagree. It would bite the day BUILD_SHARED_LIBS is set, and
  // it would bite as a read of uninitialised stack rather than as
  // anything that names itself. Refusing to bind is the whole fix, and
  // the source then reports unavailable with the reason, like any other
  // dependency that is not there.
  if (prometheia_abi_version() != PROMETHEIA_ABI_VERSION) {
    pephProm = NULL;
    sprintf2(S(szEphPromState), "failed: ABI %d, built for %d",
      prometheia_abi_version(), PROMETHEIA_ABI_VERSION);
    if (szWhy != NULL)
      sprintf2(szWhy, cch, "the Prometheia library reports ABI version %d "
        "but this build was compiled for %d; refusing to bind",
        prometheia_abi_version(), PROMETHEIA_ABI_VERSION);
    return fFalse;
  }
  if (prometheia_engine_open(szEphPromEphe, &pephProm, &err) !=
    PROMETHEIA_OK) {
    pephProm = NULL;
    sprintf2(S(szEphPromState), "failed: %s", err.message);
    if (szWhy != NULL)
      sprintf2(szWhy, cch, "the ephemeris did not open: %s", err.message);
    return fFalse;
  }
  if (FSzSet(szEphPromCat) &&
    prometheia_engine_add_catalog(pephProm, szEphPromCat, &err) !=
    PROMETHEIA_OK) {
    EphPromStop();
    sprintf2(S(szEphPromState), "failed: %s", err.message);
    if (szWhy != NULL)
      sprintf2(szWhy, cch, "the catalog did not open: %s", err.message);
    return fFalse;
  }
  if (FSzSet(szEphPromPert) &&
    prometheia_engine_add_perturbers(pephProm, szEphPromPert, &err) !=
    PROMETHEIA_OK) {
    EphPromStop();
    sprintf2(S(szEphPromState), "failed: %s", err.message);
    if (szWhy != NULL)
      sprintf2(szWhy, cch, "the perturber kernel did not open: %s",
        err.message);
    return fFalse;
  }
  prometheia_engine_set_delta_t(pephProm, FEnumDeltaTProm, NULL);
  fEphPromOpen = fTrue;
  sprintf2(S(szEphPromState), "ready: %s",
    prometheia_engine_source(pephProm));
  return fTrue;
}

int NEphPromState(char *sz, int cch)
{
  if (sz != NULL)
    sprintf2(sz, cch, "%s", szEphPromState);
  return fEphPromOpen ? 0 :
    (strncmp(szEphPromState, "failed", 6) == 0 ? 2 : 1);
}

// --------------------------------------------------------------------------
// Profiles (3.4, 3.5a) to Prometheia options: Appendix C's mapping, with
// the one trap it carries. The protocol numbers its frames 0 true of
// date, 1 mean of date, 2 J2000, 3 ICRF; Prometheia numbers its own
// 0 ICRF, 1 J2000, 2 mean of date, 3 true of date -- the same two lists
// in opposite orders, and a straight copy of the field binds the wrong
// frame in every case.

// Three of this file's mappings are STRAIGHT COPIES of a protocol
// enumeration into a library one, on the grounds that the two lists
// agree. That was a comment, and a comment cannot notice the day one of
// the two lists gains a member or reorders. These make the compiler
// notice, at no runtime cost -- and they are the answer to the review's
// Q2 and Q3, which asked whether the orders match: they do, and now
// nothing can quietly change that. The FRAME lists deliberately run in
// opposite orders and are switched member by member below rather than
// copied, which is the one trap this file's header already names.
static_assert(eph::kObsGeo   == PROMETHEIA_CENTER_GEOCENTRIC &&
              eph::kObsTopo  == PROMETHEIA_CENTER_TOPOCENTRIC &&
              eph::kObsHelio == PROMETHEIA_CENTER_HELIOCENTRIC &&
              eph::kObsBary  == PROMETHEIA_CENTER_BARYCENTRIC &&
              eph::kObsBody  == PROMETHEIA_CENTER_BODY,
  "the protocol's observers and Prometheia's centers must agree, because "
  "popts->center is a straight copy of ppf->observer");
static_assert(eph::kPlaneEcliptic == PROMETHEIA_COORDS_ECLIPTIC &&
              eph::kPlaneEquator  == PROMETHEIA_COORDS_EQUATORIAL,
  "the protocol's planes and Prometheia's coords must agree, because "
  "popts->coords is a straight copy of ppf->plane");
static_assert(eph::kPtAscNode  == PROMETHEIA_ORBIT_ASCENDING_NODE &&
              eph::kPtDescNode == PROMETHEIA_ORBIT_DESCENDING_NODE &&
              eph::kPtPeri     == PROMETHEIA_ORBIT_PERIHELION &&
              eph::kPtApo      == PROMETHEIA_ORBIT_APHELION,
  "the protocol's orbit points and Prometheia's must agree, because "
  "po->point is passed straight through");

flag FEphPromOptions(CONST eph::Profile *ppf, prometheia_options *popts)
{
  prometheia_options_init(popts);
  if (ppf->observer > eph::kObserverMax || ppf->plane > eph::kPlaneEquator ||
    ppf->form > eph::kFormRectangular || ppf->frame > eph::kFrameMax ||
    (ppf->corrections & ~eph::kCorrMask) != 0)
    return fFalse;

  popts->center = ppf->observer;           // the two lists agree 0-4
  switch (ppf->frame) {
  case eph::kFrameTrueOfDate:  popts->frame = PROMETHEIA_FRAME_TRUE_OF_DATE;  break;
  case eph::kFrameMeanOfDate:  popts->frame = PROMETHEIA_FRAME_MEAN_OF_DATE;  break;
  case eph::kFrameJ2000:       popts->frame = PROMETHEIA_FRAME_J2000;         break;
  default:                     popts->frame = PROMETHEIA_FRAME_ICRF;         break;
  }
  popts->coords = ppf->plane;              // 0 ecliptic, 1 equator, both
  popts->light_time = (ppf->corrections & eph::kCorrLightTime) != 0;
  popts->deflection = (ppf->corrections & eph::kCorrDeflection) != 0;
  popts->aberration = (ppf->corrections & eph::kCorrAberration) != 0;
  popts->speed = ppf->speeds != 0;
  popts->sigma = (ppf->columns & 1) != 0;  // A.10's sigma bit
  if (ppf->observer == eph::kObsTopo) {
    popts->site_lon_deg = ppf->siteLonEastDeg;
    popts->site_lat_deg = ppf->siteLatDeg;
    popts->site_height_m = ppf->siteHeightM;
  } else if (ppf->observer == eph::kObsBody)
    popts->center_body = ppf->observerBody;

  // Sidereal zodiacs: ecliptic plane only (3.5), and of the sidereal
  // planes only 0, the ecliptic of date -- Appendix C says sidereal
  // plane 0 only, so 1 and 2 are unsupported here, not malformed.
  if (FSzSet(ppf->zodiac.c_str())) {
    if (ppf->plane == eph::kPlaneEquator || ppf->siderealPlane !=
      eph::kSidPlaneDate)
      return fFalse;
    if (FEqSz(ppf->zodiac.c_str(), "fagan-bradley"))
      popts->sidereal = PROMETHEIA_SIDEREAL_FAGAN_BRADLEY;
    else if (FEqSz(ppf->zodiac.c_str(), "lahiri"))
      popts->sidereal = PROMETHEIA_SIDEREAL_LAHIRI;
    else if (FEqSz(ppf->zodiac.c_str(), "user")) {
      if (ppf->anchorEpoch.IsZero())
        return fFalse;                     // 3.5: a nonzero anchor required
      popts->sidereal = PROMETHEIA_SIDEREAL_USER;
      popts->sidereal_epoch_jd = ppf->anchorEpoch.Sum();
      popts->sidereal_ayanamsa_deg = ppf->anchorAyanamsaDeg;
    } else
      return fFalse;                       // an A.11 token not served here
  }
  return fTrue;
}

// The A.10 bits a question under this profile can answer with, so a
// caller can size its value block: sigma, ayanamsa, light time, delta T
// -- the last only for UT1 questions, the only ones that use one.
uint32_t EphPromColumns(CONST eph::Profile *ppf, int nTs)
{
  uint32_t cols = 4;                       // light time, always
  if (ppf->columns & 1)
    cols |= 1;
  if ((ppf->columns & 2) && FSzSet(ppf->zodiac.c_str()))
    cols |= 2;
  if ((ppf->columns & 8) && nTs == eph::kTimeUT1)
    cols |= 8;
  return cols;
}

// --------------------------------------------------------------------------
// Names: 3.5a's star grammar, LOOKUP, designations.

// One star name under 3.5a: the best matches of star_lookup() win, and
// a best quality shared by several objects is error 6, exactly as for
// designations ("Beta Sco" for beta1 and beta2 Sco). Nothing is error 1.
flag FEphPromStarResolve(CONST char *sz, int *pidx, uint16_t *pnErr,
  char *szErr, int cch)
{
  prometheia_star_match rgm[32];
  prometheia_error err;
  int cms, i, q = PROMETHEIA_MATCH_PREFIX, cm = 0, istar = -1;

  cms = prometheia_star_lookup(sz, 0, rgm, (int)(sizeof(rgm)/sizeof(*rgm)));
  if (cms == 0) {
    // star_lookup's exact matching does not know every form of 3.5a's
    // grammar star_find does (a Flamsteed number among them); when it
    // answers nothing, star_find's one-object answer is the lookup.
    prometheia_status sFind = prometheia_star_find(sz, &istar, &err);
    if (sFind == PROMETHEIA_OK) {
      *pidx = istar;
      return fTrue;
    }
    // Every failure here used to be "unknown body". star_find answers one
    // object, so its ARGUMENT refusal is a name it could not narrow to
    // one -- which is ambiguity (6), a different thing from a name
    // nothing answers (1), and the one a caller can act on by asking
    // LOOKUP. NOT_FOUND stays unknown body.
    *pnErr = sFind == PROMETHEIA_ERROR_ARGUMENT ? eph::kOErrAmbiguous :
      eph::kOErrUnknownBody;
    sprintf2(szErr, cch, "%s", err.message);
    return fFalse;
  }
  for (i = 0; i < cms; i++)
    q = Min(q, rgm[i].quality);
  for (i = 0; i < cms; i++)
    if (rgm[i].quality == q) {
      cm++;
      istar = rgm[i].index;
    }
  if (cm == 0) {
    *pnErr = eph::kOErrUnknownBody;
    sprintf2(szErr, cch, "no star answers '%s'", sz);
    return fFalse;
  }
  if (cm > 1) {
    *pnErr = eph::kOErrAmbiguous;
    sprintf2(szErr, cch, "'%s' names %d stars; use LOOKUP", sz, cm);
    return fFalse;
  }
  *pidx = istar;
  return fTrue;
}

// --------------------------------------------------------------------------
// The question (3.4) and its answer (3.5).

// One row's instant, as 3.5 defines it: a grid row is the TIME's two
// parts plus the i64 product of row and step, converted once to double,
// exactly as the spec's own arithmetic; a list row was summed by the
// host.
static double RInstantRow(CONST EPHPROMQ *pq, int iRow)
{
  if (pq->prgJd != NULL)
    return pq->prgJd[iRow];
  return pq->jd1 + (pq->jd2 +
    (double)((int64_t)iRow * pq->stepNs) / 86400000000000.0);
}

// Map the library's argument-refusal to the A.17 code it means. The
// status alone cannot say: PROMETHEIA_ERROR_ARGUMENT covers a time
// outside coverage, an observer standing on the object, and an undefined
// point alike.
//
// So the first question asked is OUR OWN: what did we ask for? The
// header documents ARGUMENT for the orbit-point entry points as exactly
// the undefined point -- a node of an orbit in the reference plane, an
// apsis of a circular one -- and that is knowledge about our request
// rather than about their prose.
//
// The message text is then a HINT and nothing more, because it is not
// part of the contract: the Prometheia project reworded its own server
// for this reason, having found that classifying on message text let
// client input change the error codes. This used to DEFAULT to undefined
// point, so a rewording on their side could silently turn any refusal
// into a claim about orbital geometry. The default is now unsupported,
// which is the honest "we cannot say more", and the text can only ever
// sharpen that into coverage -- never invent a specific geometric claim.
static uint16_t NObjErrFromArgument(CONST char *szMsg, flag fOrbitPoint)
{
  if (fOrbitPoint)
    return eph::kOErrUndefinedPoint;
  if (strstr(szMsg, "outside") != NULL || strstr(szMsg, "coverage") != NULL)
    return eph::kOErrCoverage;
  return eph::kOErrUnsupported;
}

// One row, laid out per 3.5's column order: the base six, then the
// extra columns in A.10 bit order, zero-filled to the stride.
static void FillRow(CONST eph::Profile *ppf, CONST prometheia_result *pr,
  EPHPROMANSWER *pa, real rDeltaT, int iRow)
{
  // The row's own slot. ephprom.h states the layout -- prgVal +
  // ((i * cRow) + r) * kEphPromStride -- and this wrote prgVal, row 0,
  // for every row. A multi-row question therefore landed every
  // successful row on top of row 0, left rows 1..n-1 holding whatever
  // was there before, and reported rowsOk = n with no error: wrong
  // numbers, confidently. FSubmitProm asks for one row, so no cast could
  // reach it; the failure path a few lines below already had the offset,
  // which is what made the asymmetry visible.
  double *pv = pa->prgVal + iRow * kEphPromStride;
  real rDist = pr->dist_au;

  // A star without a parallax answers with no distance (3.5): the
  // library marks that with a huge sentinel, the protocol with a zero
  // column and META's noDistance.
  if (rDist >= 1.0e9) {
    rDist = 0.0;
    pa->metaFlags |= eph::kMetaNoDistance;
  }
  if (ppf->form == eph::kFormRectangular) {
    pv[0] = pr->xyz_au[0]; pv[1] = pr->xyz_au[1]; pv[2] = pr->xyz_au[2];
    pv[3] = pr->vel_au_day[0]; pv[4] = pr->vel_au_day[1];
    pv[5] = pr->vel_au_day[2];
  } else {
    pv[0] = pr->lon_deg; pv[1] = pr->lat_deg; pv[2] = (double)rDist;
    pv[3] = pr->lon_speed; pv[4] = pr->lat_speed; pv[5] = pr->dist_speed;
  }
  for (int i = 6; i < kEphPromStride; i++)
    pv[i] = 0.0;
  if (pa->columns & 1) {
    if (pr->flags & PROMETHEIA_HAS_SIGMA) {
      pv[6] = pr->sigma_arcsec;
      pa->metaFlags |= eph::kMetaHasSigma;
    }
  }
  if (pa->columns & 2)
    pv[7] = (pr->flags & PROMETHEIA_HAS_AYANAMSA) ? pr->ayanamsa_deg : 0.0;
  if (pa->columns & 4)
    pv[8] = pr->light_time_days;
  if (pa->columns & 8)
    pv[9] = (double)rDeltaT;
}

// One row of one body-like object: the kind 0 call, or the star's
// calc_star. TDB instants convert to TT by the library's own series.
static prometheia_status CalcBodyRow(prometheia_engine *peph, int naif,
  flag fStar, int nTs, double jd, CONST prometheia_options *popts,
  prometheia_result *pr, prometheia_error *perr)
{
  if (nTs == eph::kTimeUT1)
    return fStar ? prometheia_calc_star_ut(peph, naif, jd, popts, pr, perr) :
      prometheia_calc_ut(peph, naif, jd, popts, pr, perr);
  if (nTs == eph::kTimeTDB)
    jd -= prometheia_tdb_minus_tt(jd) / 86400.0;
  return fStar ? prometheia_calc_star(peph, naif, jd, popts, pr, perr) :
    prometheia_calc(peph, naif, jd, popts, pr, perr);
}

flag FEphPromCompute(CONST EPHPROMQ *pq, EPHPROMANSWER rga[])
{
  if (pephProm == NULL)
    return fFalse;
  g_rDeltaTSec = pq->rDeltaTSec;
  for (int iObj = 0; iObj < pq->cobj; iObj++) {
    CONST eph::Object *po = &pq->pargobj[iObj];
    EPHPROMANSWER *pa = &rga[iObj];
    CONST eph::Profile *ppf;
    prometheia_options opts;
    flag fStar = fFalse;
    int naif = po->naif, iStar = -1;

    pa->rowsOk = 0;
    pa->errCode = eph::kOErrNone;
    *pa->szErr = chNull;
    pa->naif = eph::kNaifNone;
    pa->metaFlags = 0;
    pa->corrApplied = eph::kCorrMask;    // all three live, every kind served
    pa->iRowFailed = eph::kRowNone;
    pa->columns = 0;

    if (!FBetween(po->profile, 0, pq->cprof-1)) {
      pa->errCode = eph::kOErrUnsupported;
      sprintf2(S(pa->szErr), "profile %d is not one of the question's",
        po->profile);
      continue;
    }
    ppf = &pq->pargprof[po->profile];
    pa->columns = EphPromColumns(ppf, pq->nTs) & ppf->columns;
    if (!FEphPromOptions(ppf, &opts)) {
      pa->errCode = eph::kOErrUnsupported;
      sprintf2(S(pa->szErr), "this engine does not serve that option "
        "combination (A.3 0x0004/0x0007/0x0008)");
      continue;
    }
    if (ppf->speeds == 0)
      pa->metaFlags |= eph::kMetaNoSpeeds;

    // What the object IS, per Appendix C.
    switch (po->kind) {
    case eph::kObjBody:
      pa->naif = naif;
      if (ppf->observer == eph::kObsBody && ppf->observerBody == naif) {
        pa->errCode = eph::kOErrUnsupported;
        sprintf2(S(pa->szErr), "the observer is the object (3.5)");
        continue;
      }
      break;
    case eph::kObjStar:
      fStar = fTrue;
      if (!FEphPromStarResolve(po->name.c_str(), &iStar, &pa->errCode,
        pa->szErr, (int)sizeof(pa->szErr)))
        continue;
      naif = iStar;
      break;
    case eph::kObjDesignation: {
      prometheia_error err;
      int iStar2 = -1;
      uint16_t nErr = 0;
      if (prometheia_engine_lookup(pephProm, po->name.c_str(), &naif,
        &err) != PROMETHEIA_OK) {
        // Not a catalog body; resolve in the star namespace, where the
        // same ambiguity rule holds (3.5).
        if (!FEphPromStarResolve(po->name.c_str(), &iStar2, &nErr,
          pa->szErr, (int)sizeof(pa->szErr))) {
          pa->errCode = nErr;
          continue;
        }
        fStar = fTrue;
        naif = iStar2;
      }
      if (!fStar)
        pa->naif = naif;
      break;
    }
    case eph::kObjOrbitPoint:
      pa->naif = naif;
      if (naif == PROMETHEIA_SUN || naif == PROMETHEIA_SOLAR_SYSTEM_BARY) {
        pa->errCode = eph::kOErrUnsupported;
        sprintf2(S(pa->szErr), "the Sun and the barycentre have no orbit "
          "(3.5a: per-object error 2)");
        continue;
      }
      if (po->point > eph::kOrbitPointMax || (po->method != eph::kMethMean &&
        po->method != eph::kMethOsculating)) {
        pa->errCode = eph::kOErrUnsupported;
        sprintf2(S(pa->szErr), "this engine serves orbit methods 0 and 1 "
          "and points 0-3 only (A.13/A.14)");
        continue;
      }
      break;
    default:                             // kinds 3 and 4: not served
      pa->errCode = eph::kOErrUnsupported;
      pa->corrApplied = 0;
      if (po->kind == eph::kObjHypothetical)
        sprintf2(S(pa->szErr), "this engine serves no named hypotheticals");
      else
        sprintf2(S(pa->szErr), "this engine's Kepler elements are not "
          "implemented");
      continue;
    }

    // The rows. One row's failure keeps the object's computed rows,
    // with NaN in that row's columns and the first failure's code (3.5).
    for (int iRow = 0; iRow < pq->cRow; iRow++) {
      double jd = RInstantRow(pq, iRow), rDeltaT = 0.0;
      prometheia_result r;
      prometheia_error err;
      prometheia_status s;
      double *pv;

      // Through the hook itself, so the column reports the value that
      // was APPLIED. Computed separately, it repeated two of the hook's
      // three branches and dropped the third -- the user's -Yz0
      // us.rDeltaT override -- so an overridden cast reported the
      // model's delta-T while being computed with the user's.
      if (pq->nTs == eph::kTimeUT1)
        rDeltaT = (real)FEnumDeltaTProm(NULL, jd);
      if (po->kind == eph::kObjOrbitPoint) {
        // The scale-matched entry point, as bodies and stars already
        // take. This always called the TT one, so a UT1 question -- which
        // every cast is -- computed its nodes and apsides about delta-T
        // late, some 69 seconds today. Negligible for a mean node;
        // arcseconds for the osculating lunar apogee, which moves fast.
        // And the delta-T column still claimed UT1 had been applied.
        int nElem = po->method == eph::kMethMean ? PROMETHEIA_ELEMENTS_MEAN :
          PROMETHEIA_ELEMENTS_OSCULATING;
        s = pq->nTs == eph::kTimeUT1 ?
          prometheia_calc_orbit_point_ut(pephProm, naif, po->point, nElem,
            jd, &opts, &r, &err) :
          prometheia_calc_orbit_point(pephProm, naif, po->point, nElem,
            jd, &opts, &r, &err);
      } else
        s = CalcBodyRow(pephProm, naif, fStar, pq->nTs, jd, &opts, &r,
          &err);
      if (s != PROMETHEIA_OK) {
        pv = pa->prgVal + iRow * kEphPromStride;
        for (int i = 0; i < kEphPromStride; i++)
          pv[i] = NAN;
        // The FIRST failed row, whether or not a row succeeded before
        // it. This was guarded on rowsOk == 0, which got both halves
        // wrong: a failure after any success was dropped entirely, so
        // the answer carried errCode None and firstFailedRow none with
        // NaNs sitting in the values; and where several rows failed
        // before the first success, the LAST of them won rather than the
        // first. iRowFailed was never assigned at all.
        if (pa->iRowFailed == eph::kRowNone) {
          pa->iRowFailed = (uint32_t)iRow;
          pa->errCode = s == PROMETHEIA_ERROR_ARGUMENT ?
            NObjErrFromArgument(err.message,
              po->kind == eph::kObjOrbitPoint) :
            (s == PROMETHEIA_ERROR_NOT_FOUND ? eph::kOErrUnknownBody :
            (s == PROMETHEIA_ERROR_INTERNAL ? eph::kOErrInternal :
            eph::kOErrDataMissing));
          sprintf2(S(pa->szErr), "%s", err.message);
        }
        continue;
      }
      FillRow(ppf, &r, pa, rDeltaT, iRow);
      if (!fStar && pa->rowsOk == 0)
        pa->naif = naif;
      pa->rowsOk++;
    }
  }
  g_rDeltaTSec = rInvalid;
  return fTrue;
}

// --------------------------------------------------------------------------
// The source (4.1): the EPHSRCDEF the registry holds. The decisions about
// what an object IS come from FSwissPlanetSpec() -- the same function the
// local Swiss path has always used -- and are converted to Prometheia's
// options, so a cast through this source answers the question the local
// path answers, and the row arithmetic is FSwissPlanet()'s own: the
// answer's longitude carries is.rSid subtracted, which the host re-adds,
// because that is how the program's two halves have always met.

// A Swiss body id to the NAIF/SPK-ID the plugin computes. The system
// barycentres from Mars on are Prometheia's own convention (C_API.md:
// PROMETHEIA_MARS is 4, not 499); the small bodies are their SPK-IDs,
// 20000000 + the asteroid's number. Bodies the engine does not serve --
// the fictitious points, the planetary moons -- return ephNativeNone.
static int NNaifFromSwiss(int iobj)
{
  switch (iobj) {
  case SE_SUN:       return PROMETHEIA_SUN;
  case SE_MOON:      return PROMETHEIA_MOON;
  case SE_MERCURY:   return PROMETHEIA_MERCURY;
  case SE_VENUS:     return PROMETHEIA_VENUS;
  case SE_EARTH:     return PROMETHEIA_EARTH;
  case SE_MARS:      return PROMETHEIA_MARS;
  case SE_JUPITER:   return PROMETHEIA_JUPITER;
  case SE_SATURN:    return PROMETHEIA_SATURN;
  case SE_URANUS:    return PROMETHEIA_URANUS;
  case SE_NEPTUNE:   return PROMETHEIA_NEPTUNE;
  case SE_PLUTO:     return PROMETHEIA_PLUTO;
  case SE_CHIRON:    return 20002060;
  case SE_CERES:     return 20000001;
  case SE_PALLAS:    return 20000002;
  case SE_JUNO:      return 20000003;
  case SE_VESTA:     return 20000004;
  default:
    if (iobj > SE_AST_OFFSET && iobj < SE_VARUNA + 99000)
      return 20000000 + (iobj - SE_AST_OFFSET);
    return ephNativeNone;
  }
}

static flag FAvailableProm(char *szWhy, int cch)
{
  // Compiled in is available: a missing data file is a per-object
  // failure, or a failed submit that walks on, exactly as the Swiss
  // sources treat their files (ephswiss.cpp).
  if (szWhy != NULL)
    sprintf2(szWhy, cch, "%s", "compiled in");
  return fTrue;
}

static void GetCapsProm(EPHCAPS *pcaps)
{
  ClearB((pbyte)pcaps, sizeof(EPHCAPS));
  pcaps->fBody = fTrue;
  pcaps->fOrbitPoint = fTrue;
  pcaps->fStar = fTrue;
  pcaps->fSpeeds = fTrue;
}

static int StateProm(char *sz, int cch)
{
  int nState = NEphPromState(sz, cch);

  return nState;   // NEphPromState(NULL is not allowed) writes the text
}

static void StartProm()
{
  char szWhy[256];

  FEphPromStart(szWhy, (int)sizeof(szWhy));   // a failed open is FSubmit's
                                              // fFalse, walking on
}

static void StopProm()
{
  EphPromStop();
}

// One EPHQUERY: one instant, objects by Astrolog index, settings by the
// program's own fields. The internal layer answers each object through
// one profile built from FSwissPlanetSpec()'s decisions; the row is the
// answer's first six in the protocol's order, longitude carrying
// is.rSid subtracted like FSwissPlanet()'s own output.
static flag FSubmitProm(EPHQUERY *pq)
{
  char szWhy[256], szStar[cchSzDef];
  char *pch;
  eph::Profile rgpf[objMax];
  eph::Object rgobj[objMax];
  double rgval[objMax * kEphPromStride];
  EPHPROMANSWER rga[objMax];
  EPHPROMQ q;
  SWISSSPEC ss;
  EPHROW *prow;
  int i, naif, naifCent;
  flag fSiderealBad = fFalse;

  if (!FEphPromStart(szWhy, (int)sizeof(szWhy)))
    return fFalse;               // the walk asks the next source

  for (i = 0; i < pq->cobj; i++) {
    rgpf[i] = eph::Profile();
    rgobj[i] = eph::Object();
    // Each object names ITS OWN profile. The question carries one
    // profile per object (q.cprof = pq->cobj), and this was never
    // assigned -- eph::Object::profile defaults to 0, so every object in
    // the cast was computed under OBJECT 0's profile, and the bounds
    // check passed because 0 is always in range. A heliocentric chart
    // computed the lunar node heliocentrically (FSwissPlanetSpec strips
    // SEFLG_HELCTR for nodal objects, so the node's own profile is
    // geocentric and object 0's is not), and the same for a custom
    // object whose own flags invert the chart's, a topocentric object
    // beside a non-topocentric one, and a star beside bodies. Wrong
    // positions, no error, nothing said (phase 8 review, E2).
    rgobj[i].profile = (uint8_t)i;
    rga[i].prgVal = rgval + i * kEphPromStride;
    prow = &pq->rgrow[i];
    if (pq->rgisrc[i] != ephSrcNone)
      continue;                  // another source in the walk won it

    if (pq->rgszName[i] != NULL) {
      // A fixed star. rgszName carries the Swiss canonical form,
      // "Name,bayer"; the plugin resolves the name part in Prometheia's
      // own namespace, which knows the same IAU names. The center and
      // the corrections are FSwissStar()'s: GetSwissFlags() and the
      // heliocentric bit it adds for a non-Earth center.
      sprintf2(S(szStar), "%s", pq->rgszName[i]);
      pch = strchr(szStar, ',');
      if (pch != NULL)
        *pch = chNull;           // the name alone
      rgobj[i].kind = eph::kObjStar;
      rgobj[i].name = szStar;
      if (us.objCenter == oEar)
        rgpf[i].observer = eph::kObsGeo;
      else
        rgpf[i].observer = us.fBarycenter ? eph::kObsBary : eph::kObsHelio;
      rgpf[i].corrections = us.fTruePos ? 0 : eph::kCorrMask;
      rgpf[i].frame = us.fNoNutation ? eph::kFrameMeanOfDate :
        eph::kFrameTrueOfDate;
      if (us.fSidereal) {
        if (us.fSidereal2)
          fSiderealBad = fTrue;  // the solar-system plane: not served
        else
          rgpf[i].zodiac = "fagan-bradley";
      }
      continue;
    }

    if (!FSwissPlanetSpec(pq->rgobj[i], pq->rgcent[i], &ss)) {
      // A body the program itself cannot map: the South Node, a custom
      // body that maps to nothing. ephswiss.cpp answers these
      // "data unavailable" because its delegation cannot tell; this
      // conversion can -- nothing was mapped, so it is unsupported.
      prow->nErr = ephErrUnsupported;
      continue;
    }
    if (ss.nSidMode != SE_SIDM_FAGAN_BRADLEY && ss.iflag & SEFLG_SIDEREAL)
      fSiderealBad = fTrue;      // the solar-system plane: not served

    if (ss.iobj == SE_TRUE_NODE || ss.iobj == SE_MEAN_NODE) {
      // The Moon's named node bodies are kind 1 here: the ascending
      // node, osculating or mean as the setting chose.
      rgobj[i].kind = eph::kObjOrbitPoint;
      rgobj[i].naif = PROMETHEIA_MOON;
      rgobj[i].point = eph::kPtAscNode;
      rgobj[i].method = ss.iobj == SE_TRUE_NODE ? eph::kMethOsculating :
        eph::kMethMean;
    } else if (ss.iobj == SE_OSCU_APOG || ss.iobj == SE_MEAN_APOG) {
      rgobj[i].kind = eph::kObjOrbitPoint;
      rgobj[i].naif = PROMETHEIA_MOON;
      rgobj[i].point = eph::kPtApo;
      rgobj[i].method = ss.iobj == SE_OSCU_APOG ? eph::kMethOsculating :
        eph::kMethMean;
    } else if (ss.iobj == SE_INTP_APOG || ss.iobj == SE_INTP_PERG) {
      // The "natural" apogee and perigee: Prometheia serves no
      // interpolated points (A.14's method 2 is unanswered).
      prow->nErr = ephErrUnsupported;
      continue;
    } else if (ss.nPnt > 0) {
      // A node or apsis of a planet, through a customized object.
      rgobj[i].kind = eph::kObjOrbitPoint;
      naif = NNaifFromSwiss(ss.iobj);
      if (naif == ephNativeNone) {
        prow->nErr = ephErrUnsupported;
        continue;
      }
      rgobj[i].naif = naif;
      rgobj[i].point = (uint8_t)(ss.nPnt - 1);
      rgobj[i].method = ss.nNodMethod == SE_NODBIT_OSCU ? eph::kMethOsculating :
        eph::kMethMean;
    } else {
      rgobj[i].kind = eph::kObjBody;
      naif = NNaifFromSwiss(ss.iobj);
      if (naif == ephNativeNone) {
        prow->nErr = ephErrUnsupported;
        continue;
      }
      rgobj[i].naif = naif;
    }

    // The profile the spec's flags spell.
    if (ss.iobjCent >= 0) {
      rgpf[i].observer = eph::kObsBody;
      naifCent = NNaifFromSwiss(ss.iobjCent);
      if (naifCent == ephNativeNone) {
        prow->nErr = ephErrUnsupported;
        continue;
      }
      rgpf[i].observerBody = naifCent;
    } else if (ss.iflag & SEFLG_HELCTR)
      rgpf[i].observer = eph::kObsHelio;
    else if (ss.iflag & SEFLG_BARYCTR)
      rgpf[i].observer = eph::kObsBary;
    else if (ss.iflag & SEFLG_TOPOCTR) {
      rgpf[i].observer = eph::kObsTopo;
      rgpf[i].siteLonEastDeg = -ciCore.lon;
      rgpf[i].siteLatDeg = ciCore.lat;
      rgpf[i].siteHeightM = us.elvDef;
    }
    // Swiss's TRUEPOS is no corrections at all -- the protocol's mask 0
    // (ephswiss.h's own mapping, case 0 = SEFLG_TRUEPOS), not light time
    // alone, which is the astrometric quantity Swiss never computes there.
    rgpf[i].corrections = (ss.iflag & SEFLG_TRUEPOS) ? 0 : eph::kCorrMask;
    rgpf[i].frame = (ss.iflag & SEFLG_NONUT) ? eph::kFrameMeanOfDate :
      eph::kFrameTrueOfDate;
    rgpf[i].speeds = (ss.iflag & SEFLG_SPEED) != 0;
    if (ss.iflag & SEFLG_SIDEREAL)
      rgpf[i].zodiac = "fagan-bradley";
  }

  // One internal question: one row per object, at the query's instant,
  // on the UT1 scale rJD already is (it is the instant FSwissPlanet()
  // consumes, and the plugin's delta T hook is bound to the chart's own
  // model).
  q.nTs = eph::kTimeUT1; q.fList = fFalse;
  q.jd1 = pq->rJD; q.jd2 = 0.0; q.stepNs = 0; q.cRow = 1;
  q.prgJd = NULL;
  q.rDeltaTSec = rInvalid;
  q.cprof = pq->cobj; q.pargprof = rgpf;
  q.cobj = pq->cobj; q.pargobj = rgobj;
  if (!FEphPromCompute(&q, rga))
    return fFalse;               // the engine could not serve the cast

  for (i = 0; i < pq->cobj; i++) {
    if (pq->rgisrc[i] != ephSrcNone)
      continue;
    prow = &pq->rgrow[i];
    // A row the FIRST loop already refused stays refused. It skipped
    // only on rgisrc, which the walk does not set until FSubmit returns
    // -- so an object this conversion had declared unsupported was
    // handed to the engine anyway, with a default or half-built object,
    // and if the engine answered it the refusal was overwritten with
    // ephErrNone and a computed row. The walk then CLAIMS that row and
    // never asks the source behind it. Worst with a naif of 0, which is
    // the solar-system barycentre and which the engine does serve: the
    // South Node could come back as the SSB's position, marked answered.
    if (prow->nErr != ephErrNone)
      continue;
    if (fSiderealBad && rga[i].errCode == ephErrNone) {
      prow->nErr = ephErrUnsupported;
      continue;
    }
    if (rga[i].rowsOk < 1) {
      prow->nErr = rga[i].errCode;   // the A.17 values are the ephErr* ones
      continue;
    }
    // Star rows are the entry point's own six (ephem.h), and they carry
    // no is.rSid where the body rows mirror FSwissPlanet's xx[0] -
    // is.rSid. The reason is the CONSUMER, not the row: ComputeEphem()
    // re-adds is.rSid to a body row (calc.cpp), and the star callers do
    // not, so each row is pre-adjusted for the one that reads it.
    //
    // A previous version of this comment justified it differently and
    // was WRONG: it said the library's zodiac option is "a label, not a
    // transform". It is a transform, and this was measured rather than
    // argued -- Sirius at 1990-06-15, through this source, reads
    // 104.449916 tropical and 79.334059 under fagan-bradley, and the
    // swiss source returns the same two numbers to 0.0000 arcsec. Both
    // sources hand the star consumer a row already in its zodiac, the
    // consumer shifts nothing further, and the chart is right.
    //
    // The wrong reason mattered: it invited a "fix" that would have
    // subtracted is.rSid here and put every sidereal star about 25
    // degrees out -- which is exactly the defect the server source had
    // as P1. The prometheia group's star-parity leg is the net, and it
    // checks provenance so a swiss fallback cannot answer for both.
    prow->rg[0] = rgobj[i].kind == eph::kObjStar ? rga[i].prgVal[0] :
      rga[i].prgVal[0] - is.rSid;
    prow->rg[1] = rga[i].prgVal[1];
    prow->rg[2] = rga[i].prgVal[2];
    prow->rg[3] = rga[i].prgVal[3];
    prow->rg[4] = rga[i].prgVal[4];
    prow->rg[5] = rga[i].prgVal[5];
    prow->nErr = ephErrNone;
    prow->nNativeRes = rga[i].naif == eph::kNaifNone ? ephNativeNone :
      rga[i].naif;
    prow->fApprox = fFalse;
  }
  return fTrue;
}

static flag FReadProm(CONST EPHQUERY *pq, int iObj, int iRow, EPHROW *prow)
{
  if (iObj < 0 || iObj >= pq->cobj || pq->rgisrc[iObj] == ephSrcNone ||
    iRow != 0)
    return fFalse;
  *prow = pq->rgrow[iObj];
  return fTrue;
}

static void HintProm(CONST EPHQUERY *pq)
{
}

// LOOKUP: the catalog bodies when the engine is open, then the star
// namespace, which needs no engine at all. nNative carries the SPK-ID
// or the star index -- the source's own id for the body (ephem.h).
static int NLookupProm(CONST char *sz, EPHMATCH *rgm, int cMax)
{
  prometheia_star_match rgmStar[32];
  int cms, i, cm = 0;
  prometheia_error err;

  if (sz == NULL || rgm == NULL || cMax <= 0)
    return 0;
  if (pephProm != NULL) {
    int naif;
    if (prometheia_engine_lookup(pephProm, sz, &naif, &err) ==
      PROMETHEIA_OK && cm < cMax) {
      rgm[cm].nQuality = PROMETHEIA_MATCH_EXACT;
      rgm[cm].nNative = naif;
      cm++;
    }
  }
  cms = prometheia_star_lookup(sz, 0, rgmStar,
    (int)(sizeof(rgmStar)/sizeof(*rgmStar)));
  for (i = 0; i < cms && cm < cMax; i++) {
    rgm[cm].nQuality = rgmStar[i].quality;
    rgm[cm].nNative = rgmStar[i].index;
    cm++;
  }
  return cm;
}

EPHSRCDEF ephsrcPrometheia = {
  "prometheia", "Ephemeris Prometheia",
  "The cleanroom Prometheia engine over JPL DE and SBDB files.",
  FAvailableProm, GetCapsProm, StateProm, StartProm, StopProm,
  FSubmitProm, FReadProm, HintProm, NLookupProm
};

#endif // PROMETHEIA
