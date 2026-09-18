// The Prometheia source plugin's interface (EPHEMERIS_PLUGINS_PLAN.md
// 4.2, Appendix C, phase 7). Everything in this file exists only when
// PROMETHEIA is compiled in -- the makefiles define that when
// `pkg-config prometheia` resolves -- so the default build never
// references the library, and a checkout without it compiles this header
// to nothing.
//
// The plugin is written against the locked version 4 semantics
// (ephsrv/ephproto.h, read but never modified here) and against
// Prometheia's C ABI (prometheia/prometheia.h; docs/C_API.md in that
// tree). It is a registered source: the EPHSRCDEF the phase 3 registry
// (ephem.h) holds, with the same delegation philosophy ephswiss.cpp
// states -- the decisions about what an object IS come from
// FSwissPlanetSpec(), the one function the program has always used, so
// a cast through this source answers the same question the local Swiss
// path answers, converted to Prometheia's options rather than
// re-derived. The internal layer below -- the question block of 3.4
// over the locked header's own eph::Profile and eph::Object -- is what
// the suite pins the frame, observer and corrections bindings on.
#ifndef ephprom_H
#define ephprom_H

#ifdef PROMETHEIA

#include "ephem.h"
#include "ephsrv/ephproto.h"
#include <prometheia/prometheia.h>

// The three parameters of 4.2. Their declarations are rows of the
// generated table (ephparam.h) like every other source's, and their
// VALUES are us.rgszEphParam[] like every other source's -- which is
// what makes "-bP prometheia.ephemeris", the settings file's line and
// the Ephemeris Settings dialog reach this source at all.
//
// They did not, until 2026-09-18. The values lived in a private array
// here whose only writer was EphPromSetParam(), which nothing outside
// this file and the suite ever called, so every configured path was
// dropped and the engine always opened on its default search. Phase 5's
// dialog made it visible by shipping a control that did nothing.
//
// These indexes are this source's own, 0..2, for its internal use; the
// map to the shared space is IepPromShared(), one place.
enum {
  epPromEphemeris, epPromCatalog, epPromPerturbers, cepPromParam
};

// Set one parameter; an empty string restores the default. Any engine
// the source has open is dropped, so the next question reopens from the
// new settings.
void EphPromSetParam(int iParam, CONST char *szValue);

// A parameter's current value; "" where the default would be used.
CONST char *SzEphPromParam(int iParam);

// Where one of the source's data files really is: the search the engine
// does, exposed so the suite can resolve a bare file name the same way
// the engine does. A name with a directory in it is tried as given; a
// bare one is searched over Astrolog's own -Yi ephemeris directories
// (the same list SwissEnsurePath() walks) and then over ephe/ beside
// the program.
flag FEphPromFindFile(CONST char *szFile, char *szPath, int cch);

// 4.1's FAvailable: can this source answer at all? The reason names the
// first thing missing (the ephemeris file, a named catalog). The
// EPHSRCDEF's own FAvailable is the compiled-in answer; this one adds
// the files.
flag FEphPromAvailable(char *szWhy, int cch);

// 4.1's Start and State. Start opens the engine from the parameters and
// returns fFalse, with the reason in szWhy, when it cannot. State writes
// one of "ready: <engine>", "unavailable", "failed: <reason>" and
// returns it (esReady / esConnecting / esFailed) for the settings
// dialog.
flag FEphPromStart(char *szWhy, int cch);
void EphPromStop(void);
int NEphPromState(char *sz, int cch);

// 3.4's question block, in the host-side form the internal layer
// answers: the question of one cast, exactly as the wire wires it, with
// eph::Profile and eph::Object from the locked header so the plugin and
// the wire cannot drift apart. The grid case reproduces 3.5's own
// arithmetic (the i64 product, the TIME sum); the list case hands the
// instants over already summed.
typedef struct _ephpromq {
  int nTs;                  // eph::kTimeUT1, kTimeTT or kTimeTDB
  flag fList;               // timeMode: fFalse grid, fTrue list
  double jd1, jd2;          // grid: the TIME start
  int64_t stepNs;           // grid: signed; 0 only when cRow is 1
  int cRow;                 // nTime
  CONST double *prgJd;      // list: cRow instants, each jd1+jd2-summed
  real rDeltaTSec;          // finite: that one value; rInvalid: plugin's
  int cprof;
  CONST eph::Profile *pargprof;
  int cobj;
  CONST eph::Object *pargobj;
} EPHPROMQ;

// One object's answer: 3.4's META fields plus a pointer into the
// caller's value block. Row r of object i writes NEphPromCols(i) doubles
// of the answer, then zeros to the stride, at
// prgVal + ((i * pq->cRow) + r) * kEphPromStride.
#define kEphPromStride 10

typedef struct _ephproma {
  int rowsOk;               // rows of this object that computed
  uint16_t errCode;         // A.17; the first failure's code
  char szErr[256];          // 3.5a: never quotes an instant or a place
  int32_t naif;             // resolvedNaif; eph::kNaifNone if none
  uint8_t metaFlags;        // A.18 bits set by this engine
  uint8_t corrApplied;      // A.7 bits live here, the capability set
  uint32_t columns;         // A.10 bits actually present, per object
  uint32_t iRowFailed;      // firstFailedRow; eph::kRowNone if none
  double *prgVal;           // the caller's block, stride kEphPromStride
} EPHPROMANSWER;

// One pass per cast: computes every object of the question, in order,
// into rga[]. Returns fFalse when the source cannot serve the cast at
// all (the engine is not open, the ephemeris is outside coverage): the
// host then falls back per 4.1. One object's own failure never does
// that -- it lands in that object's errCode, as 3.5 says.
flag FEphPromCompute(CONST EPHPROMQ *pq, EPHPROMANSWER rga[]);

// The A.10 bits a profile's questions can answer with, so a caller can
// size its value block: sigma (1), ayanamsa (2), light time (4), delta T
// (8) -- delta T only for UT1 questions.
uint32_t EphPromColumns(CONST eph::Profile *ppf, int nTs);

// The options struct one profile maps to: exposed so the suite can pin
// the mapping from the wire's enumerations to Prometheia's -- whose
// frame numbers run the OTHER way round from the protocol's -- without
// an engine or a data file.
flag FEphPromOptions(CONST eph::Profile *ppf, prometheia_options *popts);

// One star name, resolved under 3.5a's grammar: fTrue with *pidx set,
// fFalse with *pnErr = eph::kOErrAmbiguous (6) or eph::kOErrUnknownBody
// (1) and the reason in szErr.
flag FEphPromStarResolve(CONST char *sz, int *pidx, uint16_t *pnErr,
  char *szErr, int cch);

// The source itself: registered in ephem.cpp's table, before none.
extern EPHSRCDEF ephsrcPrometheia;

#endif // PROMETHEIA

#endif // ephprom_H
