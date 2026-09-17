// The Prometheia source plugin's interface (EPHEMERIS_PLUGINS_PLAN.md
// 4.2, Appendix C, phase 7). Everything in this file exists only when
// PROMETHEIA is compiled in -- the makefiles define that when
// `pkg-config prometheia` resolves -- so the default build never
// references the library, and a checkout without it compiles this header
// to nothing.
//
// The plugin is written against the version 4 semantics of the locked
// protocol (ephsrv/ephproto.h, read but never modified here) and against
// Prometheia's C ABI (prometheia/prometheia.h; docs/C_API.md in that
// tree). The source registry of phase 3 (EPHSRCDEF in ephem.h) is being
// built in parallel on branch eph3; until it lands, this module carries
// the parts of the source model it can own alone -- the EPHPARAM rows of
// 4.3, availability and state, the engine, the question-block compute
// of 3.4 and the LOOKUP grammar of 3.5a -- with the shapes the 4.1
// sketch names, so registering it into the shared table is glue and not
// a rewrite.
#ifndef ephprom_H
#define ephprom_H

#ifdef PROMETHEIA

#include "ephsrv/ephproto.h"
#include <prometheia/prometheia.h>

// The three parameters of 4.2/4.3, in one local index space until phase
// 3's shared one exists (keys are the contract, not the numbers).
enum {
  epPromEphemeris, epPromCatalog, epPromPerturbers, cepPromParam
};

typedef struct _ephpromparam {
  CONST char *szKey;     // "prometheia.ephemeris"
  CONST char *szLabel;   // the Ephemeris Settings dialog row
  int nKind;             // all three are files: epkFile at integration
  CONST char *szDefault; // "" means the source's own default
  char szValue[256];     // current value; "" means the default is used
} EPHPROMPARAM;

extern EPHPROMPARAM rgEphPromParam[cepPromParam];

// Set one parameter; an empty string restores the default. Any value the
// engine has open with is dropped, so the next question reopens from the
// new settings.
void EphPromSetParam(int iParam, CONST char *szValue);

// Where one of the source's data files really is: the search of
// FFindDataFile above, exposed so the settings dialog and the suite can
// resolve a bare file name the same way the engine does.
flag FEphPromFindFile(CONST char *szFile, char *szPath, int cch);

// A parameter's current value; "" where the default would be used.
CONST char *SzEphPromParam(int iParam);

// 4.1's FAvailable: can this source answer at all? The reason names the
// first thing missing (the library itself, the ephemeris file, a named
// catalog). Never runs the engine.
flag FEphPromAvailable(char *szWhy, int cch);

// 4.1's Start and State. Start opens the engine from the parameters and
// returns fFalse, with the reason in szWhy, when it cannot. State writes
// one of "ready", "unavailable", "failed: <reason>" and returns it
// (0 ready, 1 unavailable, 2 failed) for the settings dialog.
flag FEphPromStart(char *szWhy, int cch);
void EphPromStop(void);
int NEphPromState(char *sz, int cch);

// 3.4's question block, in the host-side form: the question of one cast,
// exactly as the wire wires it, with eph::Profile and eph::Object from
// the locked header so the plugin and the wire cannot drift apart. The
// grid case reproduces 3.5's own arithmetic (the i64 product, the TIME
// sum); the list case hands the instants over already summed.
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

// 4.1's NLookup and 3.5a's grammar. Queries the catalog bodies
// (engine_lookup) and the star namespace (star_lookup) together;
// rgm[i].nKind says which (eph::kObjBody or kObjStar). A star query
// whose best quality is shared by several objects -- "Beta Sco" for
// beta1 and beta2 Sco -- returns BOTH, which compute turns into
// per-object error 6, as 3.5a says. Returns the number written.
typedef struct _ephprommatch {
  char szName[64];          // what matched
  int naif;                 // catalog body: its SPK-ID; star: its index
  int nKind;                // eph::kObjBody or eph::kObjStar
  int nQuality;             // PROMETHEIA_MATCH_*
} EPHPROMMATCH;

int NEphPromLookup(CONST char *sz, EPHPROMMATCH *rgm, int cMax);

// One star name, resolved under 3.5a's grammar: fTrue with *pidx set,
// fFalse with *pnErr = eph::kOErrAmbiguous (6) or kOErrUnknownBody (1)
// and the reason in szErr.
flag FEphPromStarResolve(CONST char *sz, int *pidx, uint16_t *pnErr,
  char *szErr, int cch);

#endif // PROMETHEIA

#endif // ephprom_H
