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
// The fictitious bodies' elements are compiled into every build, not only
// one with the library: the server route asks a v4 server for those bodies
// with the same elements this source computes them from (ephreq.h,
// FEphV4FromSwiss), so the two routes cannot ask different questions.
#include "astrolog.h"
#include "swephexp.h"
#include "ephpromelem.h"
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>

// --------------------------------------------------------------------------
// The fictitious bodies' elements: Swiss's seorbel.txt, read here.
//
// Swiss computes Cupido, Vulcanus and the rest from the elements in the
// FIRST seorbel.txt on its search path, so a user who edits that file has
// changed those bodies. This source used to compute them from the stock
// file compiled in (ephpromelem.h), and so gave the user's chart a
// different Cupido than the Swiss source, the server and every other build
// gave it -- silently, on a change of source. It now reads the same file
// the same way and keeps the compiled set only as the fallback, for a
// search path that holds none or a file it cannot follow.
//
// The grammar is tools/seorbel2prom.py's, which generated the compiled set,
// and the suite requires the two to agree on the stock file (REphPromElemDiff).
// A set is one non-comment line: epoch, equinox, the six elements, name, and
// an optional "geo". Each element is a polynomial in T, "c0 + c1 * T + c2 * T2".

#define cElemUserMax 128
static EPHPROMELEM rgElemUser[cElemUserMax];
static char rgszElemUser[cElemUserMax][cchSzDef];
static int cElemUser = 0;
static char szElemFile[cchSzMax + 16] = "";    // the file the sets came from
static long lElemSize = -1;
static time_t tElemMod = 0;
static flag fElemUser = fFalse;           // the sets in use are the file's

// One polynomial, "252.89 + 707550.7 * T + 0.1 * T2", into five
// coefficients. fFalse if any character is not part of one.
static flag FElemPoly(CONST char *szIn, double *rg)
{
  char sz[cchSzMax], *pch;
  CONST char *p;
  int i, c = 0;

  for (p = szIn; *p && c < (int)sizeof(sz) - 1; p++)
    if (*p != ' ' && *p != '\t' && *p != '\r')
      sz[c++] = *p;
  sz[c] = chNull;
  for (i = 0; i < 5; i++)
    rg[i] = 0.0;
  p = sz;
  if (*p == chNull)
    return fFalse;
  while (*p) {
    double v;
    int n = 0;

    i = 1;
    if (*p == '+' || *p == '-') {
      if (*p == '-')
        i = -1;
      p++;
    }
    if (!((*p >= '0' && *p <= '9') || *p == '.'))
      return fFalse;
    v = strtod(p, &pch);
    if (pch == p)
      return fFalse;
    p = pch;
    if (p[0] == '*' && p[1] == 'T') {
      n = 1;
      p += 2;
      if (*p >= '0' && *p <= '9') {
        n = *p - '0';
        p++;
      }
    }
    if (n > 4)
      return fFalse;
    rg[n] += i * v;
  }
  return fTrue;
}

// Julian Day of an epoch or equinox field: J1900, B1950, J2000 or a number.
static flag FElemEpoch(CONST char *sz, double *pjd)
{
  char *pch;

  if (FEqSz(sz, "J1900")) { *pjd = 2415020.0; return fTrue; }
  if (FEqSz(sz, "B1950")) { *pjd = 2433282.42345905; return fTrue; }
  if (FEqSz(sz, "J2000")) { *pjd = 2451545.0; return fTrue; }
  *pjd = strtod(sz, &pch);
  return pch != sz && *pch == chNull;
}

// The trimmed copy of a comma-separated field.
static void ElemField(char *pchIn, char *szOut, int cch)
{
  char *pchEnd;

  while (*pchIn == ' ' || *pchIn == '\t')
    pchIn++;
  pchEnd = pchIn + CchSz(pchIn);
  while (pchEnd > pchIn && (pchEnd[-1] == ' ' || pchEnd[-1] == '\t' ||
    pchEnd[-1] == '\r' || pchEnd[-1] == '\n'))
    pchEnd--;
  cch = Min(cch - 1, (int)(pchEnd - pchIn));
  memcpy(szOut, pchIn, cch);
  szOut[cch] = chNull;
}

// Parse a whole seorbel.txt into prgel[]/prgsz[], up to cMax sets. Returns
// the number of sets, or -1 if any line is not one Swiss's grammar and this
// one both accept.
static int CElemParse(CONST char *szFile, EPHPROMELEM *prgel,
  char (*prgsz)[cchSzDef], int cMax)
{
  FILE *file = fopen(szFile, "r");
  char szLine[1024], rgsz[10][cchSzDef], *pch, *pchNext;
  int c = 0, k, ie, it;
  double jd, rgc[5];
  flag fBad = fFalse;

  if (file == NULL)
    return -1;
  while (!fBad && fgets(szLine, sizeof(szLine), file) != NULL) {
    for (pch = szLine; *pch == ' ' || *pch == '\t'; pch++)
      ;
    if (*pch == chNull || *pch == '#' || *pch == '\r' || *pch == '\n')
      continue;
    pchNext = strchr(pch, '#');
    if (pchNext != NULL)
      *pchNext = chNull;
    for (k = 0; k < 10; k++)
      rgsz[k][0] = chNull;
    for (k = 0; k < 10 && pch != NULL; k++) {
      pchNext = strchr(pch, ',');
      if (pchNext != NULL)
        *pchNext++ = chNull;
      ElemField(pch, rgsz[k], cchSzDef);
      pch = pchNext;
    }
    if (k < 9 || c >= cMax) {
      fBad = fTrue;
      break;
    }
    ClearB((pbyte)&prgel[c], sizeof(EPHPROMELEM));
    if (!FElemEpoch(rgsz[0], &jd)) { fBad = fTrue; break; }
    prgel[c].epoch = jd;
    if (FEqSz(rgsz[1], "JDATE")) {
      prgel[c].equinox = 3;
    } else if (FEqSz(rgsz[1], "J2000")) {
      prgel[c].equinox = 0;
    } else if (FEqSz(rgsz[1], "B1950")) {
      prgel[c].equinox = 1;
    } else if (FEqSz(rgsz[1], "J1900")) {
      prgel[c].equinox = 2;
    } else {
      prgel[c].equinox = 4;
      if (!FElemEpoch(rgsz[1], &jd)) { fBad = fTrue; break; }
      prgel[c].equinoxJd = jd;
    }
    for (ie = 0; ie < 6 && !fBad; ie++) {
      if (!FElemPoly(rgsz[2 + ie], rgc)) { fBad = fTrue; break; }
      for (it = 0; it < 5; it++)
        prgel[c].el[ie][it] = rgc[it];
    }
    if (fBad)
      break;
    prgel[c].centre = k > 9 && FEqSz(rgsz[9], "geo") ? 1 : 0;
    sprintf2(prgsz[c], cchSzDef, "%s", rgsz[8]);
    prgel[c].szName = prgsz[c];
    c++;
  }
  fclose(file);
  return fBad ? -1 : c;
}

// The first seorbel.txt on the path Swiss was handed.
static flag FElemFind(char *szOut, int cch)
{
  CONST char *szPath = SzSwissPathSet();
  char szDir[cchSzMax], szTry[cchSzMax + 16];
  int cchDir;

  while (szPath != NULL && *szPath) {
    for (cchDir = 0; szPath[cchDir] && szPath[cchDir] != (PATH_SEPARATOR[0]); cchDir++)
      ;
    cchDir = Min(cchDir, cchSzMax - 1);
    memcpy(szDir, szPath, cchDir);
    szDir[cchDir] = chNull;
    if (cchDir > 0) {
      sprintf2(S(szTry), "%s%c%s", szDir, chDirSep, "seorbel.txt");
      if (FFileExists(szTry)) {
        sprintf2(szOut, cch, "%s", szTry);
        return fTrue;
      }
    }
    szPath += cchDir;
    if (*szPath == (PATH_SEPARATOR[0]))
      szPath++;
  }
  return fFalse;
}

// Element set i of Swiss's fictitious body SE_FICT_OFFSET + i, or NULL if
// there is no such set. Reloads when the file on the path, its size or its
// modification time is not the one the sets came from.
static CONST EPHPROMELEM *PeEphPromElem(int i)
{
  char szFile[cchSzMax + 16];
  struct stat st;
  int c;

  if (i < 0)
    return NULL;
  if (FElemFind(szFile, sizeof(szFile)) && stat(szFile, &st) == 0) {
    if (!FEqSz(szFile, szElemFile) || (long)st.st_size != lElemSize ||
      st.st_mtime != tElemMod) {
      c = CElemParse(szFile, rgElemUser, rgszElemUser, cElemUserMax);
      sprintf2(S(szElemFile), "%s", szFile);
      lElemSize = (long)st.st_size;
      tElemMod = st.st_mtime;
      cElemUser = c;
      fElemUser = c >= 0;
      if (c < 0) {
        char szT[cchSzMax + 16 + 120];

        sprintf2(S(szT), "%s has a line Astrolog cannot read as elements; "
          "a v4 engine is given the stock elements for the fictitious bodies "
          "instead.", szFile);
        PrintWarning(szT);
      }
    }
  } else {
    szElemFile[0] = chNull;
    lElemSize = -1;
    tElemMod = 0;
    fElemUser = fFalse;
  }
  if (fElemUser)
    return i < cElemUser ? &rgElemUser[i] : NULL;
  return i < cEphPromElem ? &rgEphPromElem[i] : NULL;
}

int CEphPromElemSets(char *szFile, int cch)
{
  CONST EPHPROMELEM *pe = PeEphPromElem(0);

  sprintf2(szFile, cch, "%s", fElemUser ? szElemFile : "");
  return pe == NULL ? 0 : fElemUser ? cElemUser : cEphPromElem;
}

// Element set i of fictitious body SE_FICT_OFFSET + i -- the set
// PeEphPromElem() gives this source -- as plain values, for the one v4
// translation (ephreq.h, FEphV4FromSwiss) that puts a kind 4 object on
// every route into a v4 engine. fFalse when there is no such set.
flag FEphElemGet(int i, double *pjdEpoch, int *pnEquinox, double *pjdEquinox,
  int *pnCentre, double rgel[6][5], CONST char **pszName)
{
  CONST EPHPROMELEM *pe = PeEphPromElem(i);
  int ie, it;

  if (pe == NULL)
    return fFalse;
  *pjdEpoch = pe->epoch;
  *pnEquinox = pe->equinox;
  *pjdEquinox = pe->equinoxJd;
  *pnCentre = pe->centre;
  for (ie = 0; ie < 6; ie++)
    for (it = 0; it < 5; it++)
      rgel[ie][it] = pe->el[ie][it];
  *pszName = pe->szName;
  return fTrue;
}

// Two names are the same if they agree up to the first non-ASCII byte in
// either: the generator wrote a byte it could not decode (the e-grave of
// Kore, Latin-1 in the file) as a multi-byte placeholder, and this reads the
// byte as it is.
static flag FElemNameSame(CONST char *sz1, CONST char *sz2)
{
  for (; *sz1 && *sz2 && (byte)*sz1 < 0x80 && (byte)*sz2 < 0x80; sz1++, sz2++)
    if (*sz1 != *sz2)
      return fFalse;
  return fTrue;
}

double REphPromElemDiff(CONST char *szFile)
{
  static EPHPROMELEM rgel[cElemUserMax];
  static char rgsz[cElemUserMax][cchSzDef];
  int c = CElemParse(szFile, rgel, rgsz, cElemUserMax), i, ie, it;
  double r, rMax = 0.0;

  if (c != cEphPromElem)
    return -1.0;
  for (i = 0; i < c; i++) {
    if (rgel[i].equinox != rgEphPromElem[i].equinox ||
      rgel[i].centre != rgEphPromElem[i].centre ||
      !FElemNameSame(rgel[i].szName, rgEphPromElem[i].szName))
      return -2.0;
    r = RAbs(rgel[i].epoch - rgEphPromElem[i].epoch);
    rMax = Max(rMax, r);
    r = RAbs(rgel[i].equinoxJd - rgEphPromElem[i].equinoxJd);
    rMax = Max(rMax, r);
    for (ie = 0; ie < 6; ie++)
      for (it = 0; it < 5; it++) {
        r = RAbs(rgel[i].el[ie][it] - rgEphPromElem[i].el[ie][it]);
        rMax = Max(rMax, r);
      }
  }
  return rMax;
}

#ifdef PROMETHEIA

#include "ephprom.h"
#include "ephreq.h"
#include <prometheia/prometheia.h>
#include <math.h>
#include <stdarg.h>
#include <time.h>
#include <chrono>
#include <set>
#include <string>
#include <vector>

// --------------------------------------------------------------------------
// Parameters (4.2/4.3). The DECLARATIONS are rows of the generated table
// (ephparam.h) and the VALUES are us.rgszEphParam[], both shared with
// every other source -- this file holds neither, and that is the fix:
// holding its own copy of either is what made the command line, the
// settings file and the dialog all miss this source entirely.

static CONST int rgiepPromShared[cepPromParam] = {
  epPrometheiaEphemeris, epPrometheiaCatalog, epPrometheiaPerturbers,
  epPrometheiaLongspan
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
static char szEphPromEphe[300], szEphPromCat[300], szEphPromPert[300],
  szEphPromLong[300];

// The Debug window's statistics (SzEphPromDebug(), below): kept by the
// engine's open, FEphPromCompute() per object and FSubmitProm() per cast.
typedef std::chrono::steady_clock EPHPROMCLOCK;

enum {
  epkPlanet, epkSmall, epkPoint, epkHypo, epkStar, epkMoon, epkCentre,
  cepkStat
};
static CONST char *rgszEpkStat[cepkStat] = {"planets, Sun, Moon",
  "catalog bodies", "nodes and apsides", "hypotheticals", "fixed stars",
  "planetary moons", "planet centres"};
static CONST char *rgszEpkShort[cepkStat] = {"planet", "catalog",
  "node/apse", "hypothet.", "star", "moon", "centre"};

#define cEphPromHist 24    // log2 microsecond buckets: <1us .. >=8s

typedef struct _ephpromlast {
  int naif, nKind;
  int iObjQuery;           // the Astrolog query's index, -1 if none
  double rUs, rSigma;      // rSigma < 0: no covariance
  flag fSigmaAsked;        // the Debug window has asked for rSigma
  uint16_t nErr;
  char szName[48], szSrc[160], szErr[256];
} EPHPROMLAST;

static struct {
  long cObj, cOk, rgcKind[cepkStat], rgcErr[16];
  long rgcHist[cEphPromHist];
  double rUsTotal, rUsMax, rgUsKind[cepkStat];
  char szMax[64];
  long cCold, cWarm;               // catalog bodies, first time and after
  double rUsCold, rUsWarm;
  long cCast, cCastObj, cHandedOn, cNoForm;
  double rUsCast, rUsCastMax;
  long cOpen;
  double rMsOpen;
  time_t tOpen, tReset;
  double rCastJd;
  int cLastObj;
  double rUsLastCast;
} sEPS;
static std::set<int> setEphPromSeen;   // catalog bodies computed so far
static std::vector<EPHPROMLAST> rgEphPromLast, rgEphPromCur;

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
static char *rgszEphPromOpenedFrom[cepPromParam] = {NULL, NULL, NULL, NULL};

// The Delta T hook, bound to Astrolog's own: a finite per-cast value
// when the question carries one, else the user's -Yz override, else the
// chart's model -- swe_deltat(), iterated once so the answer is
// Astrolog's value AT the instant asked and not at that instant's UT
// guess of it. What calc_ut asks is TT - UT1 in seconds at a TT Julian
// date, which is what both of those are.
static real g_rDeltaTSec = rInvalid;

// The lunar tidal acceleration that belongs with the ephemeris this
// engine answers from, or 0 when it is not known. Swiss's Delta-T model
// is adjusted to the tidal term of the lunar ephemeris answering, so UT
// and that Moon agree (swi_adjust_for_tidacc); the term is therefore part
// of the PAIRING, not a property of Swiss. This hook used to take whatever
// term Swiss had last loaded -- DE431's from the semo files, or DE441's if
// a JPL file had just been opened -- so Prometheia's DE441 Moon at the
// year 1000 read 203.4918752 or 203.4934645 depending on what ran before
// it (an 11.3 s Delta-T, 5.7"), measured 2026-10-24 and confirmed with the
// Swiss Ephemeris fork's session: 203.4934645 is the self-consistent
// DE441 Moon, the other DE441 at DE431's TT.
static double rEphPromTidAcc = 0.0;

// Swiss's DE number to tidal term, as swi_get_tid_acc() maps it (ts.20
// swephlib.c; the constants are swephexp.h's). 0 for a DE number it does
// not list, which leaves Swiss's own term in force as before.
static double RTidAccOfDenum(int denum)
{
  switch (denum) {
  case 200: return SE_TIDAL_DE200;
  case 403: return SE_TIDAL_DE403;
  case 404: return SE_TIDAL_DE404;
  case 405: return SE_TIDAL_DE405;
  case 406: return SE_TIDAL_DE406;
  case 421: return SE_TIDAL_DE421;
  case 422: return SE_TIDAL_DE422;
  case 430: return SE_TIDAL_DE430;
  case 431: return SE_TIDAL_DE431;
  case 440: case 441: return SE_TIDAL_DE441;
  }
  return 0.0;
}

static double FEnumDeltaTProm(void *puser, double jd)
{
  double r;

  (void)puser;
  if (g_rDeltaTSec != rInvalid)
    return (double)g_rDeltaTSec;
  // The user's -Yz0 override binds both sources to the same TT instant:
  // FSwissPlanet applies it as us.rDeltaT/86400 (calc.cpp), so the same
  // seconds, not the model's, are what this hook must answer too.
  if (us.rDeltaT != rInvalid)
    return (double)us.rDeltaT;
  // swe_deltat() answers in DAYS; the hook is seconds of TT - UT1. With
  // this engine's own tidal term set around the call and automatic
  // restored straight after, as the fork's session advised: nothing runs
  // between the two, so no other calculation sees the term.
  if (rEphPromTidAcc != 0.0)
    swe_set_tid_acc(rEphPromTidAcc);
  r = swe_deltat(jd - swe_deltat(jd) / 86400.0) * 86400.0;
  if (rEphPromTidAcc != 0.0)
    swe_set_tid_acc(SE_TIDAL_AUTOMATIC);
  return r;
}

void EphPromStop(void)
{
  int iep;

  prometheia_engine_close(pephProm);
  pephProm = NULL;
  fEphPromOpen = fFalse;
  rEphPromTidAcc = 0.0;
  // What the engine was opened from describes an OPEN engine, so it goes
  // with it. Held past the close, these were three of Astrolog's own
  // allocations that nothing ever released: every cast through this
  // source ended in "Number of memory allocations not freed: 3".
  for (iep = 0; iep < cepPromParam; iep++) {
    DeallocatePIf(rgszEphPromOpenedFrom[iep]);
    rgszEphPromOpenedFrom[iep] = NULL;
  }
  sprintf2(S(szEphPromState), "closed");
}

// Search a bare file name over Astrolog's own -Yi ephemeris directories
// -- the same list SwissEnsurePath() walks -- then the program's own
// ephem/ folder, then over ephe/ beside the program, which is where a
// Prometheia checkout keeps its kernels. A name with a directory in it is
// tried as given. The first hit wins; fFalse leaves szPath empty.
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
    char szDir[cchSzMax];
    if (!FSzSet(us.rgszPath[i]))
      continue;
    // Resolved as SwissEnsurePath() resolves it: a relative directory is
    // the executable's, not the working directory's. Read as given, the
    // default "-Yi1 ephem" found the kernels only when the program was
    // started from its own folder.
    SzPathDirAbs(us.rgszPath[i], S(szDir));
    sprintf2(S(szTry), "%s%c%s", szDir, chDirSep, szFile);
    if (FFileExists(szTry)) {
      sprintf2(szPath, cch, "%s", szTry);
      return fTrue;
    }
  }
  // The program's own ephem/, whatever -Yi says. The files shipped with
  // the program live there -- the DE441 cut, its kernel and the body
  // catalog (QT_GUI_PLAN.md item 291) -- and a settings file that points
  // -Yi elsewhere must not hide them: nrvate.as names /swe three times,
  // and under it this source found no planets file at all.
  {
    char szDir[cchSzMax];

    SzPathDirAbs("ephem", S(szDir));
    sprintf2(S(szTry), "%s%c%s", szDir, chDirSep, szFile);
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
// parameter is the source's own default: the first of its files below
// found over the paths above. fFalse names in szWhy the first thing
// missing.
// The files an EMPTY parameter means, named once: the resolution below
// searches for them, and Ephemeris Settings shows them greyed in the empty
// field (SzEphPromEmpty()), so the two cannot name different files. Each
// list runs widest first and ends with the file shipped in ephem/
// (QT_GUI_PLAN.md item 291): JPL's DE441 cut to 1900-2100, the 16
// asteroids' kernel cut to the same years, and the catalog of the bodies
// Astrolog ships (tools/bundle-bodies.py). So a clone casts with nothing
// set, and a fuller file on the -Yi paths is preferred by being there.
static CONST char *rgszEphe[] = {"linux_p1550p2650.440", "de440s.bsp",
  "linux_p1900p2100.441"};
static CONST char szEphPromLongDefault[] = "linux_m13000p17000.441";
static CONST char *rgszEphPromCat[] = {"astrolog-bodies.epm"};
static CONST char *rgszEphPromPert[] = {"sb441-n16.bsp",
  "sb441-n16-de440span.bsp", "sb441-p1900p2100.bsp"};
#define CSZ(rgsz) ((int)(sizeof(rgsz)/sizeof(CONST char *)))

// The first of a default list found over the paths, or fFalse.
static flag FEphPromFindFirst(CONST char **rgsz, int csz, char *szPath,
  int cch)
{
  int i;

  for (i = 0; i < csz; i++)
    if (FEphPromFindFile(rgsz[i], szPath, cch))
      return fTrue;
  *szPath = chNull;
  return fFalse;
}

// What an empty field means, said with the file it actually is: the
// search runs here, so the field names what it found, or says nothing was.
// "empty: a DE440 file in the folders" said neither which file nor which
// folders (2026-10-25, the maintainer: "what??!!?").
void SzEphPromEmpty(int iep, flag fLong, char *sz, int cch)
{
  char szPath[cchSzMax];
  CONST char *pchName;

  szPath[0] = chNull;
  if (iep == epPrometheiaEphemeris)
    FEphPromFindFirst(rgszEphe, CSZ(rgszEphe), S(szPath));
  else if (iep == epPrometheiaLongspan)
    FEphPromFindFile(szEphPromLongDefault, S(szPath));
  else if (iep == epPrometheiaCatalog)
    FEphPromFindFirst(rgszEphPromCat, CSZ(rgszEphPromCat), S(szPath));
  else
    FEphPromFindFirst(rgszEphPromPert, CSZ(rgszEphPromPert), S(szPath));
  for (pchName = szPath + CchSz(szPath); pchName > szPath &&
    pchName[-1] != chDirSep && pchName[-1] != '/'; pchName--)
    ;
  if (FSzSet(szPath))
    sprintf2(sz, cch, !fLong ? "empty: uses %s" : "Left empty, %s is "
      "used: it was found where the program looks. Browse to pick "
      "another.", !fLong ? pchName : szPath);
  // Nothing found. Each says what it costs and what was looked for, by
  // name, since the names are the whole of what "empty" means.
  else if (iep == epPrometheiaEphemeris)
    sprintf2(sz, cch, !fLong ? "%s" : "Left empty, %s, %s or %s is "
      "looked for, and none was found, so this source answers nothing. "
      "Browse for a JPL DE file.", !fLong ? "empty: none found; Browse "
      "for one" : rgszEphe[0], rgszEphe[1], rgszEphe[2]);
  else if (iep == epPrometheiaLongspan)
    sprintf2(sz, cch, !fLong ? "%s" : "Left empty, a date outside the "
      "planets file's own years goes to the next source in the chain: %s, "
      "which covers -13000 to 17000, was not found.", !fLong ? "empty: "
      "none; Planets' years only" : szEphPromLongDefault);
  else if (iep == epPrometheiaCatalog)
    sprintf2(sz, cch, !fLong ? "%s" : "Left empty, %s is looked for, and "
      "was not found, so no asteroids or centaurs are read, Chiron among "
      "them.", !fLong ? "empty: none, so no asteroids" :
      rgszEphPromCat[0]);
  else
    sprintf2(sz, cch, !fLong ? "%s" : "Left empty, %s, %s or %s is "
      "looked for, and none was found, so an asteroid is computed only "
      "within 100 years of its catalog date.", !fLong ? "empty: asteroids "
      "within 100 years" : rgszEphPromPert[0],
      rgszEphPromPert[1], rgszEphPromPert[2]);
}

// Which years the open engine's planets files answer, said for the Test:
// "Planets from linux_p1900p2100.441, December 4, 1899 to January 12,
// 2100; other years go to the next source in the chain." From the library's own
// record of each file (prometheia_engine_ephemeris_span(), C API since
// their 624c96d, asked for by this dialog): a file cut to 1900-2100 is
// padded out to whole 32-day records, so its name's years are not quite
// the dates it answers. Empty when the engine is not open.
CONST char *SzEphPromCatalogUsed()
{
  return szEphPromCat;
}


void SzEphPromSpans(char *sz, int cch)
{
  char rgszDate[2][24], *pch = sz;
  CONST char *rgszFile[2], *pchName;
  int cFile, i, j, mon, day, yea;

  *sz = chNull;
  if (pephProm == NULL)
    return;
  rgszFile[0] = szEphPromEphe;
  rgszFile[1] = szEphPromLong;
  cFile = Min(prometheia_engine_ephemeris_count(pephProm), 2);
  for (i = 0; i < cFile; i++) {
    double rgjd[2];
    prometheia_error err;

    if (prometheia_engine_ephemeris_span(pephProm, i, &rgjd[0], &rgjd[1],
      NULL, &err) != PROMETHEIA_OK)
      continue;
    for (j = 0; j < 2; j++) {
      JulianToMdy(rgjd[j], &mon, &day, &yea);
      // Format 1, the prose one: "December 4, 1899", or "4 December 1899"
      // under -Yd; format 0 pads its columns ("12/ 4/1899").
      sprintf2(S(rgszDate[j]), "%s", SzDate(mon, day, yea, 1));
    }
    for (pchName = rgszFile[i] + CchSz(rgszFile[i]); pchName > rgszFile[i]
      && pchName[-1] != chDirSep && pchName[-1] != '/'; pchName--)
      ;
    sprintf2(pch, cch - (int)(pch - sz), "%s %s, %s to %s", i == 0 ?
      "Planets from" : "; outside it from", pchName, rgszDate[0],
      rgszDate[1]);
    pch += CchSz(pch);
  }
  if (*sz)
    sprintf2(pch, cch - (int)(pch - sz), "%s", "; other years go to the "
      "next source in the chain");
}

static flag FEphPromResolve(char *szWhy, int cch)
{
  if (FSzSet(SzEphPromParam(epPromEphemeris))) {
    if (!FEphPromFindFile(SzEphPromParam(epPromEphemeris), szEphPromEphe,
      (int)sizeof(szEphPromEphe))) {
      // In the dialog's words, row by row: "ephemeris '...' not found"
      // was the engine's word for a field labelled otherwise.
      sprintf2(S(szEphPromState), "the planets file '%s' was not found",
        SzEphPromParam(epPromEphemeris));
      if (szWhy != NULL)
        sprintf2(szWhy, cch, "%s", szEphPromState);
      return fFalse;
    }
  } else {
    if (!FEphPromFindFirst(rgszEphe, CSZ(rgszEphe), szEphPromEphe,
      (int)sizeof(szEphPromEphe))) {
      // Said for both places it can be fixed from: the dialog's field
      // and the switch. The file names are the ones searched for, so the
      // reader knows what "a planets file" means here.
      sprintf2(S(szEphPromState), "no planets file was found (%s, %s or "
        "%s was looked for); pick one in \"Planets\", or set -bP "
        "prometheia.ephemeris", rgszEphe[0], rgszEphe[1], rgszEphe[2]);
      if (szWhy != NULL)
        sprintf2(szWhy, cch, "%s", szEphPromState);
      return fFalse;
    }
  }
  // The catalog and the perturbers: named, they must be found; empty,
  // the first of their defaults found, and none if none is.
  *szEphPromCat = chNull;
  if (!FSzSet(SzEphPromParam(epPromCatalog)))
    FEphPromFindFirst(rgszEphPromCat, CSZ(rgszEphPromCat), szEphPromCat,
      (int)sizeof(szEphPromCat));
  else if (!FEphPromFindFile(SzEphPromParam(epPromCatalog), szEphPromCat,
      (int)sizeof(szEphPromCat))) {
    sprintf2(S(szEphPromState), "the asteroid catalog '%s' was not found",
      SzEphPromParam(epPromCatalog));
    if (szWhy != NULL)
      sprintf2(szWhy, cch, "%s", szEphPromState);
    return fFalse;
  }
  // The long-span file, behind the first and consulted outside its span.
  // Named: it must be found. Empty: JPL's DE441 by its file name, if it is
  // on the -Yi paths, and nothing otherwise -- a missing long-span file is
  // not a missing ephemeris, it only means dates outside the first file's
  // span fall through the chain to the next source, as they always have.
  *szEphPromLong = chNull;
  if (FSzSet(SzEphPromParam(epPromLongspan))) {
    if (!FEphPromFindFile(SzEphPromParam(epPromLongspan), szEphPromLong,
      (int)sizeof(szEphPromLong))) {
      sprintf2(S(szEphPromState), "the planets file for other years '%s' "
        "was not found", SzEphPromParam(epPromLongspan));
      if (szWhy != NULL)
        sprintf2(szWhy, cch, "%s", szEphPromState);
      return fFalse;
    }
  } else if (!FEphPromFindFile(szEphPromLongDefault, szEphPromLong,
    (int)sizeof(szEphPromLong)))
    *szEphPromLong = chNull;
  *szEphPromPert = chNull;
  if (!FSzSet(SzEphPromParam(epPromPerturbers)))
    FEphPromFindFirst(rgszEphPromPert, CSZ(rgszEphPromPert), szEphPromPert,
      (int)sizeof(szEphPromPert));
  else if (!FEphPromFindFile(SzEphPromParam(epPromPerturbers), szEphPromPert,
      (int)sizeof(szEphPromPert))) {
    sprintf2(S(szEphPromState), "the asteroid perturbers file '%s' was not "
      "found", SzEphPromParam(epPromPerturbers));
    if (szWhy != NULL)
      sprintf2(szWhy, cch, "%s", szEphPromState);
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

// The -Yi directories as one string, for noticing that they changed: an
// empty parameter is a file searched for over them, so a different folder
// can mean different files under the same parameters.
static char szEphPromPathsOpened[cchSzMax * 4];

static void SzEphPromPaths(char *sz, int cch)
{
  char *pch = sz;
  int i;

  *sz = chNull;
  for (i = 0; i < 10; i++) {
    sprintf2(pch, cch - (int)(pch - sz), "%s;", SzSet(us.rgszPath[i]));
    pch += CchSz(pch);
  }
}

flag FEphPromStart(char *szWhy, int cch)
{
  char szWhy2[256], szPaths[cchSzMax * 4];
  prometheia_error err;
  int iep;

  SzEphPromPaths(S(szPaths));
  if (fEphPromOpen) {
    for (iep = 0; iep < cepPromParam; iep++)
      if (!FEqSz(SzSet(rgszEphPromOpenedFrom[iep]), SzEphPromParam(iep))) {
        // The engine is open on files the settings no longer name. Close
        // and reopen: 0.x has no way to drop or swap a catalog, and
        // close-and-reopen is the path the library's authors sanction.
        EphPromStop();
        break;
      }
    // Or on files found in a folder the settings no longer name.
    if (fEphPromOpen && !FEqSz(szPaths, szEphPromPathsOpened))
      EphPromStop();
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
  sprintf2(S(szEphPromPathsOpened), "%s", szPaths);
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
  EPHPROMCLOCK::time_point tOpen0 = EPHPROMCLOCK::now();
  if (prometheia_engine_open(szEphPromEphe, &pephProm, &err) !=
    PROMETHEIA_OK) {
    pephProm = NULL;
    sprintf2(S(szEphPromState), "failed: %s", err.message);
    if (szWhy != NULL)
      sprintf2(szWhy, cch, "the ephemeris did not open: %s", err.message);
    return fFalse;
  }
  if (FSzSet(szEphPromLong) &&
    prometheia_engine_add_ephemeris(pephProm, szEphPromLong, &err) !=
    PROMETHEIA_OK) {
    EphPromStop();
    sprintf2(S(szEphPromState), "failed: %s", err.message);
    if (szWhy != NULL)
      sprintf2(szWhy, cch, "the long-span ephemeris did not open: %s",
        err.message);
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
  // Which DE answers, asked of the engine rather than guessed from file
  // names: a result carries its ephemeris's DE number. J2000 is inside
  // every DE440 file this source looks for; the year 1000 is outside them
  // and inside DE441, so it reaches a long-span file when one is open.
  // The two terms are the same for DE440 and DE441; should a long-span
  // file of another lunar solution be paired with the main one, the
  // main's term is used everywhere and the state says so, rather than
  // the difference passing unremarked.
  {
    prometheia_result res;
    prometheia_error errT;
    double rLong = 0.0;

    rEphPromTidAcc = 0.0;
    if (prometheia_calc(pephProm, PROMETHEIA_SUN, 2451545.0, NULL, &res,
      &errT) == PROMETHEIA_OK)
      rEphPromTidAcc = RTidAccOfDenum(res.denum);
    if (FSzSet(szEphPromLong) && prometheia_calc(pephProm, PROMETHEIA_SUN,
      2086308.5, NULL, &res, &errT) == PROMETHEIA_OK)
      rLong = RTidAccOfDenum(res.denum);
    prometheia_engine_set_delta_t(pephProm, FEnumDeltaTProm, NULL);
    fEphPromOpen = fTrue;
    sEPS.cOpen++;
    sEPS.tOpen = time(NULL);
    sEPS.rMsOpen = std::chrono::duration<double, std::milli>(
      EPHPROMCLOCK::now() - tOpen0).count();
    if (rLong != 0.0 && rLong != rEphPromTidAcc)
      sprintf2(S(szEphPromState), "ready: %s (the long-span file's lunar "
        "tidal term differs; Delta-T uses the main file's)",
        prometheia_engine_source(pephProm));
    else
      sprintf2(S(szEphPromState), "ready: %s",
        prometheia_engine_source(pephProm));
  }
  return fTrue;
}

// A local engine is ready or it has failed (ephem.h): closed, it opens on
// the next query, and any other text is why the last open did not. This
// returned esConnecting for everything but "failed: ..." -- so a missing
// file ("ephemeris '...' not found", "no DE440 ephemeris ...") read as a
// source still connecting, and CastChart() withheld its "No ephemeris
// source could answer" warning for a remote server that was about to
// call back. Nothing was going to: every body read 0Ari00'00" in silence.
int NEphPromState(char *sz, int cch)
{
  flag fClosed = !*szEphPromState || FEqSz(szEphPromState, "closed");

  if (sz != NULL)
    sprintf2(sz, cch, "%s", fClosed ? "closed" : szEphPromState);
  return fEphPromOpen || fClosed ? esReady : esFailed;
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

  // Sidereal zodiacs: ecliptic plane only (3.5). All three of A.8's
  // sidereal planes are served since C ABI 6, which appended
  // options.sidereal_plane; before it, only plane 0 was reachable and
  // 1 and 2 were unsupported here. The C boundary refuses a fixed plane
  // with equatorial coordinates, and 3.5 refuses equatorial with any
  // sidereal zodiac at all, so the wider rule is the one to apply.
  if (FSzSet(ppf->zodiac.c_str())) {
    if (ppf->plane == eph::kPlaneEquator)
      return fFalse;
    switch (ppf->siderealPlane) {
    case eph::kSidPlaneDate:
      popts->sidereal_plane = PROMETHEIA_SIDEREAL_PLANE_DATE;       break;
    case eph::kSidPlaneAnchor:
      popts->sidereal_plane = PROMETHEIA_SIDEREAL_PLANE_ANCHOR;     break;
    case eph::kSidPlaneInvariable:
      popts->sidereal_plane = PROMETHEIA_SIDEREAL_PLANE_INVARIABLE; break;
    default:
      return fFalse;             // an A.8 plane this ABI does not name
    }
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
  prometheia_error err;
  int istar = -1;
  prometheia_status s;

  // ONE decision, and it is the library's own. This used to call
  // prometheia_star_lookup first and then re-derive the answer from the
  // match qualities: take the best quality, and call it ambiguous if
  // more than one match tied there. That reimplemented a judgement the
  // library already makes, and the two parted company.
  //
  // Found by probing prometheiad with our own wire client: "Beta Sco"
  // came back from the SERVER as Acrab, with a position, while this
  // function called the same name ambiguous. Their grammar is that a
  // Bayer designation without a component number answers every
  // component and find takes the brightest; only DIFFERENT designations
  // tying are ambiguous. star_find implements that, prometheiad uses
  // star_find, and so now do we -- which makes the whole class of
  // divergence impossible rather than fixing one instance of it.
  //
  // lookup stays where ranking is the point: NLookupProm's completion
  // list, which wants every match and their qualities.
  s = prometheia_star_find(sz, &istar, &err);
  if (s == PROMETHEIA_OK) {
    *pidx = istar;
    return fTrue;
  }
  // star_find answers one object, so its ARGUMENT refusal is a name it
  // could not narrow to one -- ambiguity (6), which a caller can act on
  // by asking LOOKUP. NOT_FOUND is a name nothing answers (1).
  *pnErr = s == PROMETHEIA_ERROR_ARGUMENT ? eph::kOErrAmbiguous :
    eph::kOErrUnknownBody;
  sprintf2(szErr, cch, "%s", err.message);
  return fFalse;
}

// --------------------------------------------------------------------------
// The question (3.4) and its answer (3.5).

// One row's instant, as 3.5 defines it: a grid row is the TIME's two
// parts plus the i64 product of row and step, converted once to double,
// exactly as the spec's own arithmetic; a list row was summed by the
// host.
// --------------------------------------------------------------------------
// What the engine has done this session, for Ephemeris Settings' Debug
// window (SzEphPromDebug() below). Kept by FEphPromCompute() per object and
// by FSubmitProm() per cast; cheap enough to keep always -- two clock reads
// an object -- and reset only when asked.


void EphPromDebugReset(void)
{
  time_t tOpen = sEPS.tOpen;
  double rMsOpen = sEPS.rMsOpen;
  long cOpen = sEPS.cOpen;

  memset(&sEPS, 0, sizeof(sEPS));
  setEphPromSeen.clear();
  rgEphPromLast.clear();
  sEPS.tReset = time(NULL);
  // The engine's opening is not a statistic of the session's casts: it
  // stays, so a reset does not make the files look never opened.
  sEPS.tOpen = tOpen; sEPS.rMsOpen = rMsOpen; sEPS.cOpen = cOpen;
}

static int NEpkOf(CONST eph::Object *po, int naif)
{
  switch (po->kind) {
  case eph::kObjOrbitPoint: return epkPoint;
  case eph::kObjHypothetical: case eph::kObjElements: return epkHypo;
  case eph::kObjStar: return epkStar;
  default:
    if (naif >= 1000000)
      return epkSmall;
    // NAIF's numbering: a planet's centre is its barycentre's digit and
    // 99, its moons the same digit and 01 to 98. Mercury, Venus and the
    // Earth are their own centres, and the Moon is 301.
    if (naif >= 400 && naif <= 999 && naif % 100 == 99)
      return epkCentre;
    if (naif >= 401 && naif <= 998)
      return epkMoon;
    return epkPlanet;
  }
}

static void EphPromStatObj(CONST eph::Object *po, CONST EPHPROMANSWER *pa,
  double rUs, CONST char *szSrc, double rSigma, flag fStar)
{
  EPHPROMLAST el;
  int nKind = fStar ? epkStar : NEpkOf(po, pa->naif), iB;
  double r;

  sEPS.cObj++;
  if (pa->rowsOk > 0) {
    sEPS.cOk++;
    sEPS.rgcKind[nKind]++;
    sEPS.rgUsKind[nKind] += rUs;
  } else
    sEPS.rgcErr[pa->errCode < 16 ? pa->errCode : 15]++;
  sEPS.rUsTotal += rUs;
  for (iB = 0, r = 1.0; iB < cEphPromHist - 1 && rUs >= r; iB++, r *= 2.0)
    ;
  sEPS.rgcHist[iB]++;
  el.naif = fStar ? -1 : pa->naif;
  el.nKind = nKind;
  el.iObjQuery = -1;
  el.rUs = rUs;
  el.rSigma = rSigma;
  el.fSigmaAsked = rSigma >= 0.0;
  el.nErr = pa->rowsOk > 0 ? 0 : pa->errCode;
  sprintf2(S(el.szName), "%s", po->name.c_str());
  if (!FSzSet(el.szName) && !fStar)
    sprintf2(S(el.szName), "%d", (int)po->naif);
  sprintf2(S(el.szSrc), "%s", szSrc != NULL ? szSrc : "");
  sprintf2(S(el.szErr), "%s", pa->rowsOk > 0 ? "" : pa->szErr);
  if (rUs > sEPS.rUsMax) {
    sEPS.rUsMax = rUs;
    sprintf2(S(sEPS.szMax), "%s", el.szName);
  }
  // A catalog body's first position integrates its orbit from the
  // record's epoch; later ones reuse the trajectory. The two are kept
  // apart, because together they average into a number that is neither.
  if (nKind == epkSmall && pa->rowsOk > 0) {
    if (setEphPromSeen.insert(pa->naif).second) {
      sEPS.cCold++; sEPS.rUsCold += rUs;
    } else {
      sEPS.cWarm++; sEPS.rUsWarm += rUs;
    }
  }
  rgEphPromCur.push_back(el);
}

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
// PROMETHEIA_ERROR_COVERAGE, announced by the Prometheia project on
// 2026-09-29 and appended to prometheia_status as 7 with no ABI change: an
// instant outside the loaded data's span, which used to come back as
// ARGUMENT and is classified from the message text above. Compared by
// value, because a header from before the change has no such enumerator;
// a library from before it never returns 7.
#define kPromErrCoverage ((prometheia_status)7)

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
  rgEphPromCur.clear();
  for (int iObj = 0; iObj < pq->cobj; iObj++) {
    CONST eph::Object *po = &pq->pargobj[iObj];
    EPHPROMANSWER *pa = &rga[iObj];
    CONST eph::Profile *ppf;
    prometheia_options opts;
    flag fStar = fFalse;
    int naif = po->naif, iStar = -1;
    // The Debug window's record of this object, written however the body
    // below leaves it -- most of its exits are a continue.
    struct STATGUARD {
      CONST eph::Object *po; EPHPROMANSWER *pa; flag *pfStar;
      EPHPROMCLOCK::time_point t0;
      char szSrc[160]; double rSigma;
      ~STATGUARD() {
        EphPromStatObj(po, pa, std::chrono::duration<double, std::micro>(
          EPHPROMCLOCK::now() - t0).count(), szSrc, rSigma, *pfStar);
      }
    } sg = {po, pa, &fStar, EPHPROMCLOCK::now(), "", -1.0};

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
    case eph::kObjHypothetical:          // kind 3, by A.15 token
    case eph::kObjElements:              // kind 4, two-body elements
      break;
    default:
      pa->errCode = eph::kOErrUnsupported;
      pa->corrApplied = 0;
      sprintf2(S(pa->szErr), "object kind %d is not served", (int)po->kind);
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
      } else if (po->kind == eph::kObjElements) {
        prometheia_elements el;
        int ie, it, nT = po->nTerms > 5 ? 5 : po->nTerms;
        memset(&el, 0, sizeof(el));
        el.epoch_jd_tt = po->epoch.Sum();
        el.equinox = po->equinox;
        el.equinox_jd_tt = po->equinoxJd;
        el.origin = po->centre;
        el.n_terms = nT;
        for (it = 0; it < nT; it++)
          for (ie = 0; ie < 6; ie++) {
            size_t ic = (size_t)ie * po->nTerms + it;
            double v = ic < po->coef.size() ? po->coef[ic] : 0.0;
            double *pd = ie == 0 ? el.mean_anomaly : ie == 1 ?
              el.semi_major_axis : ie == 2 ? el.eccentricity : ie == 3 ?
              el.arg_perihelion : ie == 4 ? el.ascending_node : el.inclination;
            pd[it] = v;
          }
        s = pq->nTs == eph::kTimeUT1 ?
          prometheia_calc_elements_ut(pephProm, &el, jd, &opts, &r, &err) :
          prometheia_calc_elements(pephProm, &el, jd, &opts, &r, &err);
      } else if (po->kind == eph::kObjHypothetical)
        s = pq->nTs == eph::kTimeUT1 ?
          prometheia_calc_hypothetical_ut(pephProm, po->name.c_str(), jd,
            &opts, &r, &err) :
          prometheia_calc_hypothetical(pephProm, po->name.c_str(), jd,
            &opts, &r, &err);
      else
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
            (s == kPromErrCoverage ? eph::kOErrCoverage :
            eph::kOErrDataMissing)));
          sprintf2(S(pa->szErr), "%s", err.message);
        }
        continue;
      }
      FillRow(ppf, &r, pa, rDeltaT, iRow);
      sprintf2(S(sg.szSrc), "%s", r.source != NULL ? r.source : "");
      sg.rSigma = (r.flags & PROMETHEIA_HAS_SIGMA) ? r.sigma_arcsec : -1.0;
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
// program's own fields. The question is the one every route into a v4
// engine asks (ephreq.h, CEphRequestFromQuery()): the same eph::Request the
// server route sends prometheiad, with the same Delta T, computed here by
// the same engine. This had a translation of its own until 2026-10-25,
// and the two had parted -- a Mars-centred chart, Vulcan, the Delta T
// before 1955 (QT_GUI_PLAN.md item 289) -- so now there is one, and the
// only difference between the two routes is the transport. The row is the
// answer's first six in the protocol's order, longitude carrying is.rSid
// subtracted like FSwissPlanet()'s own output.
static flag FSubmitProm(EPHQUERY *pq)
{
  char szWhy[256];
  eph::Request req;
  EPHREQMAP rgmap[objMax];
  double rgval[objMax * kEphPromStride];
  EPHPROMANSWER rga[objMax];
  EPHPROMQ q;
  EPHROW *prow;
  flag rgfAsked[objMax];
  int cMap, iM, i, iO;

  if (!FEphPromStart(szWhy, (int)sizeof(szWhy))) {
    // Why, in the plugin's words, for the walk to report if nothing
    // behind this source answers -- "attempted nothing" is all Ephemeris
    // Settings' Test could say about a missing ephemeris file otherwise.
    SetEphSubmitWhy(szWhy);
    return fFalse;               // the walk asks the next source
  }

  // The Delta T paired with THIS engine's Moon: rEphPromTidAcc is the
  // tidal term of the file it opened, which the server route has to
  // assume for a server (qtdriver.cpp, JdeSrvQt()).
  cMap = CEphRequestFromQuery(pq, RDeltaTV4Sec(pq->rJD, rEphPromTidAcc),
    &req, rgmap);
  for (i = 0; i < pq->cobj; i++)
    rgfAsked[i] = fFalse;
  for (iM = 0; iM < cMap; iM++)
    rgfAsked[rgmap[iM].iObjQuery] = fTrue;
  // An object with no version 4 form -- the South Node, a custom body that
  // maps to nothing, a sidereal mode or flag the protocol cannot spell --
  // is unsupported here, and the walk offers it to the next source.
  for (i = 0; i < pq->cobj; i++)
    if (pq->rgisrc[i] == ephSrcNone && !rgfAsked[i])
      pq->rgrow[i].nErr = ephErrUnsupported;
  if (cMap == 0)
    return fTrue;

  // One internal question, as the request states it: one row, at its TT
  // instant, with its Delta T, which the engine's hook answers for
  // anything that needs UT (FEnumDeltaTProm()).
  for (iO = 0; iO < (int)req.objs.size(); iO++)
    rga[iO].prgVal = rgval + iO * kEphPromStride;
  q.nTs = eph::kTimeTT; q.fList = fFalse;
  q.jd1 = req.start.jd1; q.jd2 = req.start.jd2; q.stepNs = 0; q.cRow = 1;
  q.prgJd = NULL;
  q.rDeltaTSec = req.deltaTSec;
  q.cprof = (int)req.profiles.size(); q.pargprof = req.profiles.data();
  q.cobj = (int)req.objs.size(); q.pargobj = req.objs.data();
  {
    EPHPROMCLOCK::time_point t0 = EPHPROMCLOCK::now();
    flag fOk = FEphPromCompute(&q, rga);
    double rUs = std::chrono::duration<double, std::micro>(
      EPHPROMCLOCK::now() - t0).count();
    if (!fOk)
      return fFalse;             // the engine could not serve the cast
    // The Debug window's cast: its objects named as the chart names them.
    sEPS.cCast++;
    sEPS.cCastObj += (long)rgEphPromCur.size();
    sEPS.rUsCast += rUs;
    if (rUs > sEPS.rUsCastMax)
      sEPS.rUsCastMax = rUs;
    sEPS.rUsLastCast = rUs;
    sEPS.rCastJd = pq->rJD;
    for (i = 0; i < pq->cobj; i++)
      if (!rgfAsked[i])
        sEPS.cNoForm++;
    for (iM = 0; iM < cMap; iM++)
      if (rgmap[iM].iObjReq < (int)rgEphPromCur.size())
        rgEphPromCur[rgmap[iM].iObjReq].iObjQuery = pq->rgobj[
          rgmap[iM].iObjQuery];
    rgEphPromLast = rgEphPromCur;
    sEPS.cLastObj = (int)rgEphPromLast.size();
  }

  for (iM = 0; iM < cMap; iM++) {
    i = rgmap[iM].iObjQuery;
    iO = rgmap[iM].iObjReq;
    prow = &pq->rgrow[i];
    if (rga[iO].rowsOk < 1) {
      prow->nErr = rga[iO].errCode;   // the A.17 values are the ephErr* ones
      sEPS.cHandedOn++;
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
    prow->rg[0] = req.objs[iO].kind == eph::kObjStar ? rga[iO].prgVal[0] :
      rga[iO].prgVal[0] - is.rSid;
    prow->rg[1] = rga[iO].prgVal[1];
    prow->rg[2] = rga[iO].prgVal[2];
    prow->rg[3] = rga[iO].prgVal[3];
    prow->rg[4] = rga[iO].prgVal[4];
    prow->rg[5] = rga[iO].prgVal[5];
    prow->nErr = ephErrNone;
    prow->nNativeRes = rga[iO].naif == eph::kNaifNone ? ephNativeNone :
      rga[iO].naif;
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

// --------------------------------------------------------------------------
// The Debug window's report: everything this program knows about the
// engine, as text a user can read and paste into a bug report. Ephemeris
// Settings' Debug button shows it (qtdialog.cpp), refreshing it live.

static std::string strEphPromDebug;

static void DbgAdd(CONST char *szFmt, ...)
{
  char sz[1024];
  va_list ap;

  va_start(ap, szFmt);
  vsnprintf(sz, sizeof(sz), szFmt, ap);
  va_end(ap);
  strEphPromDebug += sz;
}

static void DbgDate(double jd, char *sz, int cch)
{
  int y, m;
  double day;

  prometheia_civil_from_jd(jd, &y, &m, &day);
  sprintf2(sz, cch, "%04d-%02d-%02d", y, m, (int)day);
}

static void DbgCount(long l, char *sz, int cch)
{
  char szT[32];
  int cch0, i, j = 0;

  sprintf2(S(szT), "%ld", l);
  cch0 = CchSz(szT);
  for (i = 0; i < cch0 && j < cch - 1; i++) {
    if (i > 0 && (cch0 - i) % 3 == 0 && szT[i-1] != '-' && j < cch - 2)
      sz[j++] = ',';
    sz[j++] = szT[i];
  }
  sz[j] = chNull;
}

// A time in microseconds, in the unit that reads best.
static void DbgUs(double rUs, char *sz, int cch)
{
  if (rUs < 1000.0)
    sprintf2(sz, cch, "%.1f us", rUs);
  else if (rUs < 1000000.0)
    sprintf2(sz, cch, "%.2f ms", rUs / 1000.0);
  else
    sprintf2(sz, cch, "%.2f s", rUs / 1000000.0);
}

// One file: where it is, its size and date, and what the setting says.
static void DbgFile(CONST char *szLabel, CONST char *szPath, int iParam)
{
  struct stat st;
  char szT[32];
  CONST char *szSet = iParam >= 0 ? SzEphPromParam(iParam) : "";

  if (!FSzSet(szPath)) {
    DbgAdd("  %-20s (none)%s\n", szLabel, FSzSet(szSet) ? "" :
      "  -- empty setting, and no default file found");
    return;
  }
  DbgAdd("  %-20s %s\n", szLabel, szPath);
  if (stat(szPath, &st) == 0) {
    strftime(szT, sizeof(szT), "%Y-%m-%d %H:%M", localtime(&st.st_mtime));
    DbgAdd("  %-20s %.2f MB, modified %s\n", "",
      (double)st.st_size / 1048576.0, szT);
  }
  if (iParam >= 0)
    DbgAdd("  %-20s %s%s%s\n", "", FSzSet(szSet) ? "named in settings: \"" :
      "settings: empty, so the shipped default", szSet, FSzSet(szSet) ?
      "\"" : "");
}

static CONST char *SzDbgErr(int nErr)
{
  static CONST char *rgsz[] = {"none", "unknown body", "unsupported",
    "outside coverage", "data missing", "undefined point", "ambiguous",
    "numerical failure", "internal error", "undefined here"};

  return nErr >= 0 && nErr < (int)(sizeof(rgsz)/sizeof(*rgsz)) ?
    rgsz[nErr] : "other";
}

CONST char *SzEphPromDebug(void)
{
  char sz[cchSzMax*2], szA[48], szB[48], szC[48];
  int rgisrc[cEphSrcBuiltIn], cisrc, i, iB, cHist;
  long cWon, cFell, cNone, lHistMax;
  time_t tNow = time(NULL);

  strEphPromDebug.clear();
  strftime(szA, sizeof(szA), "%Y-%m-%d %H:%M:%S", localtime(&tNow));
  DbgAdd("EPHEMERIS PROMETHEIA -- DEBUG                   %s\n", szA);
  DbgAdd("The settings in force. What is typed in Ephemeris Settings takes "
    "effect on OK.\n\n");

  // The library and the engine.
  DbgAdd("LIBRARY AND ENGINE\n");
  DbgAdd("  %-20s Prometheia %s, ABI %d (this build compiled for %d)\n",
    "library", prometheia_version(), prometheia_abi_version(),
    PROMETHEIA_ABI_VERSION);
  NEphPromState(S(sz));
  DbgAdd("  %-20s %s\n", "state", FSzSet(sz) ? sz : "closed; it opens on "
    "the next chart");
  if (sEPS.cOpen > 0) {
    strftime(szA, sizeof(szA), "%H:%M:%S", localtime(&sEPS.tOpen));
    DbgAdd("  %-20s %ld time%s this session, the last at %s in %.1f ms\n",
      "opened", sEPS.cOpen, sEPS.cOpen == 1 ? "" : "s", szA, sEPS.rMsOpen);
  }
  DbgAdd("  %-20s %d named fixed stars, built in\n", "stars",
    prometheia_star_count());
  if (pephProm != NULL)
    DbgAdd("  %-20s %d named by token\n", "hypotheticals",
      prometheia_hypothetical_count(pephProm));
  {
    char szFile[cchSzMax + 16];
    int cSets = CEphPromElemSets(S(szFile));
    DbgAdd("  %-20s %d element sets, %s\n", "fictitious bodies", cSets,
      FSzSet(szFile) ? szFile : "compiled in (no seorbel.txt found)");
  }
  DbgAdd("\n");

  // The files.
  DbgAdd("FILES\n");
  DbgFile("Planets", fEphPromOpen ? szEphPromEphe : NULL, epPromEphemeris);
  DbgFile("Planets, other years", fEphPromOpen ? szEphPromLong : NULL,
    epPromLongspan);
  DbgFile("Asteroid catalog", fEphPromOpen ? szEphPromCat : NULL,
    epPromCatalog);
  DbgFile("Asteroid perturbers", fEphPromOpen ? szEphPromPert : NULL,
    epPromPerturbers);
  if (pephProm != NULL) {
    int c = prometheia_engine_ephemeris_count(pephProm);
    for (i = 0; i < c; i++) {
      double jd0, jd1;
      CONST char *szSrc = NULL;
      prometheia_error err;
      if (prometheia_engine_ephemeris_span(pephProm, i, &jd0, &jd1, &szSrc,
        &err) != PROMETHEIA_OK)
        continue;
      DbgDate(jd0, S(szA)); DbgDate(jd1, S(szB));
      DbgAdd("  %-20s %s to %s (JD %.1f to %.1f)%s%s\n", i == 0 ?
        "planets answer" : "then", szA, szB, jd0, jd1, FSzSet(szSrc) ?
        ": " : "", SzSet(szSrc));
    }
    if (c > 0)
      DbgAdd("  %-20s %s\n", "", "outside these, the next source in the "
        "chain answers");
  }
  if (!fEphPromOpen)
    DbgAdd("  (the engine is not open: files are resolved when it opens)\n");
  DbgAdd("\n");

  // Where a bare file name is looked for, in order (FEphPromFindFile()).
  DbgAdd("SEARCH PATH (a bare file name is looked for here, in order)\n");
  for (i = 0, iB = 1; i < 10; i++) {
    char szDir[cchSzMax];
    if (!FSzSet(us.rgszPath[i]))
      continue;
    SzPathDirAbs(us.rgszPath[i], S(szDir));
    DbgAdd("  %2d. %-50s -Yi%d%s\n", iB++, szDir, i, FDirExists(szDir) ? "" :
      "  (not there)");
  }
  {
    char szDir[cchSzMax];
    SzPathDirAbs("ephem", S(szDir));
    DbgAdd("  %2d. %-50s the program's own%s\n", iB++, szDir,
      FDirExists(szDir) ? "" : "  (not there)");
  }
  DbgAdd("  %2d. %-50s the working directory's\n\n", iB, "ephe");

  // The chain, and what each source in it has answered.
  DbgAdd("EPHEMERIS SOURCE CHAIN\n");
  DbgAdd("  %-20s %s\n", "setting", SzSet(us.szEphemSource));
  cisrc = CEphChainSrc(us.szEphemSource, rgisrc, cEphSrcBuiltIn);
  for (i = 0; i < cisrc; i++) {
    EPHSRCDEF *pes = PephsrcGet(rgisrc[i]);
    if (pes == NULL)
      continue;
    EphSrcTally(rgisrc[i], &cWon, &cFell, NULL);
    DbgCount(cWon, S(szA)); DbgCount(cFell, S(szB));
    DbgAdd("  %d. %-17s answered %s position%s%s%s%s\n", i + 1, pes->szName,
      szA, cWon == 1 ? "" : "s", i > 0 && cWon > 0 ? ", " : "",
      i > 0 && cWon > 0 ? szB : "", i > 0 && cWon > 0 ?
      " after a source ahead of it declined" : "");
  }
  EphSrcTally(-1, NULL, NULL, &cNone);
  if (cNone > 0) {
    DbgCount(cNone, S(szA));
    DbgAdd("  %-20s %s position%s no source could answer\n", "", szA,
      cNone == 1 ? "" : "s");
  }
  DbgAdd("\n");

  // Time.
  DbgAdd("TIME\n");
  DbgDate(is.JD, S(szA));
  DbgAdd("  %-20s %s, JD %.5f UT\n", "current chart", szA, (double)is.JD);
  DbgAdd("  %-20s %.3f s at the current chart (tidal term %.2f\"/cy^2, "
    "matched to the open DE)\n", "Delta T", (double)RDeltaTV4Sec(is.JD,
    rEphPromTidAcc), rEphPromTidAcc);
  DbgAdd("\n");

  // The statistics.
  if (sEPS.tReset != 0) {
    strftime(szA, sizeof(szA), "%H:%M:%S", localtime(&sEPS.tReset));
    DbgAdd("STATISTICS (since reset at %s)\n", szA);
  } else
    DbgAdd("STATISTICS (since the program started)\n");
  DbgCount(sEPS.cCast, S(szA)); DbgCount(sEPS.cObj, S(szB));
  DbgAdd("  %-20s %s cast%s, %s position%s computed\n", "work", szA,
    sEPS.cCast == 1 ? "" : "s", szB, sEPS.cObj == 1 ? "" : "s");
  if (sEPS.cObj > 0) {
    DbgCount(sEPS.cOk, S(szA)); DbgCount(sEPS.cObj - sEPS.cOk, S(szB));
    DbgAdd("  %-20s %s answered, %s declined and handed on\n", "outcome",
      szA, szB);
    for (i = 1; i < 16; i++)
      if (sEPS.rgcErr[i] > 0) {
        DbgCount(sEPS.rgcErr[i], S(szA));
        DbgAdd("  %-20s   %s %s\n", "", szA, SzDbgErr(i));
      }
    if (sEPS.cNoForm > 0) {
      DbgCount(sEPS.cNoForm, S(szA));
      DbgAdd("  %-20s %s object%s with no form this engine can be asked "
        "for (the South Node, ...), passed straight on\n", "", szA,
        sEPS.cNoForm == 1 ? "" : "s");
    }
    DbgUs(sEPS.rUsTotal / sEPS.cObj, S(szA)); DbgUs(sEPS.rUsMax, S(szB));
    DbgAdd("  %-20s %s mean per position; slowest %s (%s)\n", "speed", szA,
      szB, sEPS.szMax);
    if (sEPS.rUsTotal > 0.0)
      DbgAdd("  %-20s %.0f positions a second of engine time\n", "",
        sEPS.cObj / (sEPS.rUsTotal / 1000000.0));
    if (sEPS.cCast > 0) {
      DbgUs(sEPS.rUsCast / sEPS.cCast, S(szA)); DbgUs(sEPS.rUsCastMax,
        S(szB));
      DbgAdd("  %-20s %s mean per cast, %s the longest\n", "", szA, szB);
    }
    DbgAdd("  %-20s\n", "by kind");
    for (i = 0; i < cepkStat; i++)
      if (sEPS.rgcKind[i] > 0) {
        DbgCount(sEPS.rgcKind[i], S(szA));
        DbgUs(sEPS.rgUsKind[i] / sEPS.rgcKind[i], S(szB));
        DbgAdd("  %-20s   %-20s %10s, %s each\n", "", rgszEpkStat[i], szA,
          szB);
      }
    if (sEPS.cCold > 0) {
      DbgUs(sEPS.rUsCold / sEPS.cCold, S(szA));
      if (sEPS.cWarm > 0)
        DbgUs(sEPS.rUsWarm / sEPS.cWarm, S(szB));
      DbgCount(sEPS.cCold, S(szC));
      DbgAdd("  %-20s %s catalog bod%s integrated, %s each the first time"
        "%s%s%s\n", "trajectories", szC, sEPS.cCold == 1 ? "y" : "ies", szA,
        sEPS.cWarm > 0 ? "; " : "", sEPS.cWarm > 0 ? szB : "",
        sEPS.cWarm > 0 ? " each after that, from the cached trajectory" :
        "");
    }

    // The distribution, as a histogram of log2 microsecond buckets, and
    // the percentiles read off it.
    for (iB = cEphPromHist - 1; iB > 0 && sEPS.rgcHist[iB] == 0; iB--)
      ;
    cHist = iB + 1;
    for (i = 0; i < cHist && sEPS.rgcHist[i] == 0; i++)
      ;
    lHistMax = 1;
    for (iB = 0; iB < cHist; iB++)
      if (sEPS.rgcHist[iB] > lHistMax)
        lHistMax = sEPS.rgcHist[iB];
    {
      CONST double rgrP[3] = {0.50, 0.90, 0.99};
      CONST char *rgszP[3] = {"p50", "p90", "p99"};
      char szP[3][24];
      for (int ip = 0; ip < 3; ip++) {
        long lAt = 0, lWant = (long)ceil(rgrP[ip] * sEPS.cObj);
        for (iB = 0; iB < cHist; iB++) {
          lAt += sEPS.rgcHist[iB];
          if (lAt >= lWant)
            break;
        }
        DbgUs(ldexp(1.0, iB), S(szP[ip]));
      }
      DbgAdd("  %-20s %s under %s, %s under %s, %s under %s\n",
        "percentiles", rgszP[0], szP[0], rgszP[1], szP[1], rgszP[2], szP[2]);
    }
    DbgAdd("  %-20s\n", "time per position");
    for (iB = i; iB < cHist; iB++) {
      char szBar[52];
      int cBar = (int)(50.0 * sEPS.rgcHist[iB] / lHistMax + 0.5);
      if (sEPS.rgcHist[iB] > 0 && cBar == 0)
        cBar = 1;
      memset(szBar, '#', cBar);
      szBar[cBar] = chNull;
      DbgUs(ldexp(1.0, iB), S(szA));
      DbgCount(sEPS.rgcHist[iB], S(szB));
      DbgAdd("  %-14s<%9s %9s %s\n", "", szA, szB, szBar);
    }
  }
  DbgAdd("\n");

  // The last cast, object by object.
  if (!rgEphPromLast.empty()) {
    std::vector<std::string> rgstrSrc;
    flag fNoCov = fFalse;
    // Each catalog body's uncertainty, asked for here and once: a cast
    // does not ask -- it costs the library twelve extra integrations a
    // body -- so the Debug window does, for the bodies of the cast it
    // shows, and keeps the answer with the row.
    for (i = 0; i < (int)rgEphPromLast.size(); i++) {
      EPHPROMLAST *pl = &rgEphPromLast[i];
      prometheia_options opt;
      prometheia_result res;
      prometheia_error err;
      if (pl->fSigmaAsked || pl->nKind != epkSmall || pl->nErr != 0 ||
        pephProm == NULL)
        continue;
      pl->fSigmaAsked = fTrue;
      prometheia_options_init(&opt);
      opt.sigma = 1;
      if (prometheia_calc_ut(pephProm, pl->naif, sEPS.rCastJd, &opt, &res,
        &err) == PROMETHEIA_OK && (res.flags & PROMETHEIA_HAS_SIGMA))
        pl->rSigma = res.sigma_arcsec;
    }
    DbgDate(sEPS.rCastJd, S(szA));
    DbgUs(sEPS.rUsLastCast, S(szB));
    DbgAdd("LAST CAST (%s, JD %.5f UT, %d object%s in %s)\n", szA,
      sEPS.rCastJd, sEPS.cLastObj, sEPS.cLastObj == 1 ? "" : "s", szB);
    DbgAdd("  %-14s %10s  %-10s %10s %11s  %s\n", "object", "NAIF/SPK",
      "kind", "time", "1 sigma", "answered by");
    for (i = 0; i < (int)rgEphPromLast.size(); i++) {
      CONST EPHPROMLAST *pl = &rgEphPromLast[i];
      char szN[24], szS[24];
      int iSrc;
      if (pl->naif > 0 && pl->naif != (int)eph::kNaifNone)
        sprintf2(S(szN), "%d", pl->naif);
      else
        sprintf2(S(szN), "%s", "-");
      if (pl->rSigma >= 0.0)
        sprintf2(S(szS), "%.4f\"", pl->rSigma);
      else {
        sprintf2(S(szS), "%s", pl->nKind == epkSmall && pl->nErr == 0 ?
          "n/a *" : "");
        if (pl->nKind == epkSmall && pl->nErr == 0)
          fNoCov = fTrue;
      }
      DbgUs(pl->rUs, S(szC));
      if (pl->nErr == 0) {
        // The engine's description of what answered, by a letter: the
        // same few long strings would otherwise fill every row.
        for (iSrc = 0; iSrc < (int)rgstrSrc.size() &&
          rgstrSrc[iSrc] != pl->szSrc; iSrc++)
          ;
        if (iSrc == (int)rgstrSrc.size())
          rgstrSrc.push_back(pl->szSrc);
        sprintf2(S(sz), "[%c]", 'A' + Min(iSrc, 25));
      } else if (pl->nErr == eph::kOErrUnknownBody)
        sprintf2(S(sz), "declined: %s, to the next source",
          SzDbgErr(pl->nErr));
      else
        sprintf2(S(sz), "declined: %s (%s)", SzDbgErr(pl->nErr),
          pl->szErr);
      DbgAdd("  %-14.14s %10s  %-10s %10s %11s  %s\n", pl->iObjQuery >= 0 ?
        szObjDisp[pl->iObjQuery] : pl->szName, szN, rgszEpkShort[pl->nKind],
        szC, szS, sz);
    }
    for (i = 0; i < (int)rgstrSrc.size(); i++)
      DbgAdd("  %s[%c] %s\n", i == 0 ? "\n" : "", 'A' + Min(i, 25),
        rgstrSrc[i].c_str());
    if (fNoCov)
      DbgAdd("  * The catalog's record carries no covariance for this body, "
        "so the library\n    cannot say how far its orbit could be off.\n");
  } else
    DbgAdd("LAST CAST\n  (none yet: Prometheia has not been asked for a "
      "chart this session)\n");
  return strEphPromDebug.c_str();
}

EPHSRCDEF ephsrcPrometheia = {
  "prometheia", "Ephemeris Prometheia",
  "The Ephemeris Prometheia engine: the planets from JPL DE files, the "
  "asteroids from a catalog.",
  FAvailableProm, GetCapsProm, StateProm, StartProm, StopProm,
  FSubmitProm, FReadProm, HintProm, NLookupProm
};

#endif // PROMETHEIA
