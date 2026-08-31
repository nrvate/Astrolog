/*
** Astrolog (Version 8.00) File: switch.cpp
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
** Additional ephemeris databases and formulas are from the calculation
** routines in the program PLACALC and are programmed and Copyright (C)
** 1989,1991,1993 by Astrodienst AG and Alois Treindl (alois@astro.ch). The
** use of that source code is subject to regulations made by Astrodienst
** Zurich, and the code is not in the public domain. This copyright notice
** must not be changed or removed by any user of this program.
**
** The original planetary calculation routines used in this program have
** been copyrighted and the initial core of this program was mostly a
** conversion to C of the routines created by James Neely as listed in
** 'Manual of Computer Programming for Astrologers', by Michael Erlewine,
** available from Matrix Software.
**
** Atlas composed using data from https://www.geonames.org/ licensed under a
** Creative Commons Attribution 4.0 License. Time zone changes composed using
** public domain TZ database: https://data.iana.org/time-zones/tz-link.html
**
** The PostScript code within the core graphics routines are programmed
** and Copyright (C) 1992-1993 by Brian D. Willoughby (brianw@sounds.wa.com).
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
**
** Initial programming 8/28-30/1991.
** X Window graphics initially programmed 10/23-29/1991.
** PostScript graphics initially programmed 11/29-30/1992.
** Last code change made 5/28/2026.
*/

#include "astrolog.h"


/*
******************************************************************************
** The Switch Registry.
******************************************************************************
*/

// One row per switch, replacing hand-written parser cases a family at a
// time (REFACTORING.md, T3). FProcessSwitches() consults the registry by
// the switch's exact full spelling before its own big switch, so a
// migrated switch behaves identically from the command line, from
// settings files, and from macros, in every build. A name not present
// falls through to the remaining cases unchanged.
//
// Deliberate strictness: only spellings listed in -HY exist here. The
// retired cases happened to accept garbage suffixes as aliases (-YJq
// acted as -YJ7, -YAz as -YAa, -YRx as -YR, -Ykx as -Yk); those now
// fail as any unknown switch.

#define nSwitchAbsent (-2)      // Name not in the registry.
#define nSwitchStop   (-3)      // Success, and stop parsing the rest of
                                // the line (the WIN -c screensaver case).

#define grfSwPrefix   0x1  // Match szName as a prefix; the handler
                           // parses the rest of the spelling itself
                           // (-Ye's suffix soup).
#define grfSwGraphics 0x2  // The -X family: refused when -0X has locked
                           // graphics away, and turns graphics mode on
                           // whenever the switch succeeds -- behavior
                           // the retired case 'X' applied around its
                           // whole sub-parser.

// A flag switch: the =/_/-/: prefix semantics applied to one boolean.
// The simplest and most numerous row shape.

typedef struct _switchflag {
  CONST char *szName;
  flag *pf;
  word grf;       // grfSw* bits; trailing so plain rows omit it.
  flag *pf2;      // A second flag the same prefix semantics apply to
                  // after pf -- the "-l0 also enables -l" family shape.
} SWITCHFLAG;

static CONST SWITCHFLAG rgswflag[] = {
  {"Y",   &us.fSwitchRare},  {"YT",  &us.fTruePos},
  {"YV",  &us.fTopoPos},     {"Yf",  &us.fRefract},
  {"Yh",  &us.fBarycenter},  {"Ym",  &us.fMoonMove},
  {"Yn",  &us.fTrueNode},    {"Yn0", &us.fNoNutation},
  {"Ynn", &us.fNaturalNode}, {"Yd",  &us.fEuroDate},
  {"Yt",  &us.fEuroTime},    {"Yv",  &us.fEuroDist},
  {"Yr",  &us.fRound},       {"YC",  &us.fSmartCusp},
  {"YO",  &us.fSmartSave},   {"Y8",  &us.fClip80},
  {"Yo",  &us.fWriteOld},    {"Yp",  &us.fPolarAsc},
  {"Y0",  &us.fNoDisplay},   {"Yz1", &us.fOffsetOnly},
#ifdef GRAPH
  {"YXe", &gs.fEcliptic},
  {"Xm",  &gs.fColor,       grfSwGraphics},
  {"Xi",  &gs.fAlt,         grfSwGraphics},
  {"Xt",  &gs.fText,        grfSwGraphics},
  {"Xu",  &gs.fBorder,      grfSwGraphics},
  {"Xl",  &gs.fLabel,       grfSwGraphics},
  {"XA",  &gs.fLabelAsp,    grfSwGraphics},
  {"Xj",  &gs.fJetTrail,    grfSwGraphics},
  {"Xe",  &gs.fEquator,     grfSwGraphics},
  {"XC",  &gs.fHouseExtra,  grfSwGraphics},
  {"XJ",  &gs.fIndianWheel, grfSwGraphics},
  {"X8",  &gs.fMoonWheel,   grfSwGraphics},
  {"XQ",  &gs.fKeepSquare,  grfSwGraphics},
  {"XQ0", &gs.fAutoScale,   grfSwGraphics},
  {"Xx",  &gs.fThick,       grfSwGraphics},
  {"Xx0", &gs.fAntialias,   grfSwGraphics},
  {"Xv0", &gs.fDoSidebar,   grfSwGraphics},
#ifdef ISG
  {"XN",  &gs.fAnimMap,     grfSwGraphics},
#endif
#endif
  {"3",   &us.fDecan},       {"9",   &us.fNavamsa},
  {"f",   &us.fFlip},        {"G",   &us.fGeodetic},
  {"J",   &us.fIndian},      {"S",   &us.fOrbit},
  {"D",   &us.fInDayInf},    {"7",   &us.fEsoteric},
  {"?",   &us.fSwitch},
  {"l",   &us.fSector},       {"l0",  &us.fSectorApprox, 0, &us.fSector},
  {"j",   &us.fInfluence},    {"j0",  &us.fInfluenceSign, 0, &us.fInfluence},
  {"K",   &us.fCalendar},     {"Ky",  &us.fCalendarYear, 0, &us.fCalendar},
  {"Q",   &us.fLoop},         {"Q0",  &us.fLoopInit, 0, &us.fLoop},
  {"8",   &us.fMoonChart},    {"80",  &us.fMoonChartSep, 0, &us.fMoonChart},
  {"v0",  &us.fVelocity}};

// A ranged setter: "<name> <lo> <hi> <v1>..<vn>", writing hi-lo+1
// values into consecutive slots of a table -- the shape shared by the
// orb, angle, influence, restriction, Ray and color families. The value
// kinds are the conventions the retired cases actually used, error
// reporting included:

enum _rangedvaluekind {
  vtReal = 0,   // RFromSz() into a real slot, unchecked.
  vtBool,       // NFromSz() != 0 into a byte slot, unchecked.
  vtRay,        // NFromSz() into an int slot, checked against
                // [nValMin, nValMax], errors naming parameter 3+k-i.
  vtColor,      // NParseSz(pmColor) into an int slot, checked against
                // [-0xffffff, nValMax], errors naming parameter 0.
};

typedef struct _switchranged {
  CONST char *szName;   // Full spelling after the dash: "YAm".
  CONST char *szErr;    // Label today's error messages use: "YA".
  int pm;               // Parse mode for the two range indexes.
  int iMin;             // Lowest legal index: aspects 1, all else 0.
  int iMax;             // Highest legal index.
  void *pvBase;         // First slot...
  int cbStride;         // ...and the byte distance to the next.
  int vt;               // Value kind above.
  int nValMin, nValMax; // Value bounds for the checked kinds.
  void (*pfnAfter)(void);  // Post-store hook, or NULL.
} SWITCHRANGED;

static CONST SWITCHRANGED rgswranged[] = {
  {"YAo", "YA", pmAspect, 1, cAspect, rAspOrb.rgn,       sizeof(real),
    vtReal, 0, 0, NULL},
  {"YAa", "YA", pmAspect, 1, cAspect, rAspAngle.rgn,     sizeof(real),
    vtReal, 0, 0, NULL},
  {"YAm", "YA", pmObject, 0, oNorm+1, &rgobjset[0].orb,  sizeof(OBJSET),
    vtReal, 0, 0, NULL},
  {"YAd", "YA", pmObject, 0, oNorm+1, &rgobjset[0].add,  sizeof(OBJSET),
    vtReal, 0, 0, NULL},
  {"Yj",  "Yj", pmObject, 0, oNorm1,  &rgobjset[0].inf,  sizeof(OBJSET),
    vtReal, 0, 0, NULL},
  {"YjT", "Yj", pmObject, 0, oNorm1,  &rgobjset[0].tinf, sizeof(OBJSET),
    vtReal, 0, 0, NULL},
  {"YjC", "Yj", pmSign,   0, cSign,   &rHouseInf[0],     sizeof(real),
    vtReal, 0, 0, NULL},
  {"YjA", "Yj", pmAspect, 0, cAspect, rAspInf.rgn,       sizeof(real),
    vtReal, 0, 0, NULL},
  {"YR",  "YR", pmObject, 0, cObj,    ignore,            sizeof(byte),
    vtBool, 0, 0, RedoRestrictions},
  {"YRT", "YR", pmObject, 0, cObj,    ignore2,           sizeof(byte),
    vtBool, 0, 0, RedoRestrictions},
  {"Y7O", "Y7", pmObject, 0, oNorm,   rgObjRay.rgn,      sizeof(int),
    vtRay, 0, 7, NULL},
  {"Y7C", "Y7", pmSign,   1, cSign,   rgSignRay.rgn,     sizeof(int),
    vtRay, 1, 1234567, NULL},
  {"YkO", "Yk", pmObject, 0, starLo,  &rgobjset[0].kolor, sizeof(OBJSET),
    vtColor, 0, kMax-1, NULL},
  {"YkA", "Yk", pmAspect, 1, cAspect, kAspA.rgn,         sizeof(int),
    vtColor, 0, cColor2-1, NULL},
  {"Yk0", "Yk", 0,        1, cRainbow, kRainbowA,        sizeof(int),
    vtColor, 0, cColor2-1, NULL},
  {"Yk7", "Yk", 0,        1, cRainbow, kRayA,            sizeof(int),
    vtColor, 0, cColor2-1, NULL},
  {"Yk",  "Yk", 0,        0, 8,       kMainA,            sizeof(int),
    vtColor, 0, cColor2-1, NULL}};

static int NProcessSwitchRanged(CONST SWITCHRANGED *psr, int argc,
  char **argv)
{
  pbyte pb;
  int i, j, k, l;

  if (FErrorArgc(psr->szErr, argc, 2))
    return tcError;
  i = NParseSz(argv[1], psr->pm); j = NParseSz(argv[2], psr->pm);
  if (FErrorValN(psr->szErr, !FBetween(i, psr->iMin, psr->iMax), i, 1))
    return tcError;
  if (FErrorValN(psr->szErr, !FBetween(j, 0, psr->iMax) || j < i, j, 2))
    return tcError;
  if (FErrorArgc(psr->szErr, argc, 3+j-i))
    return tcError;
  for (k = i; k <= j; k++) {
    pb = (pbyte)psr->pvBase + (size_t)k * psr->cbStride;
    switch (psr->vt) {
    case vtReal:
      *(real *)pb = RFromSz(argv[3+k-i]);
      break;
    case vtBool:
      *pb = NFromSz(argv[3+k-i]) != 0;
      break;
    case vtRay:
      l = NFromSz(argv[3+k-i]);
      if (FErrorValN(psr->szErr,
        !FBetween(l, psr->nValMin, psr->nValMax), l, 3+k-i))
        return tcError;
      *(int *)pb = l;
      break;
    case vtColor:
      l = NParseSz(argv[3+k-i], pmColor);
      if (FErrorValN(psr->szErr,
        !FBetween(l, -0xffffff, psr->nValMax), l, 0))
        return tcError;
      *(int *)pb = l;
      break;
    }
  }
  if (psr->pfnAfter != NULL)
    psr->pfnAfter();
  return 3+j-i;
}

// -Yj0 and -Yj7: the rulership bonus influences, split between
// rgrBonusInf[] and the slots past the houses in rHouseInf[] the same
// way FOutputSettings() writes them.

static int NSwYj0(CONST char *szSwitch, PARSEIN *pin)
{
  if (FErrorArgc("Yj", pin->argc, 4))
    return tcError;
  rgrBonusInf[1]     = RFromSz(pin->argv[1]);
  rgrBonusInf[2]     = RFromSz(pin->argv[2]);
  rHouseInf[cSign+1] = RFromSz(pin->argv[3]);
  rHouseInf[cSign+2] = RFromSz(pin->argv[4]);
  return 4;
}

static int NSwYj7(CONST char *szSwitch, PARSEIN *pin)
{
  if (FErrorArgc("Yj", pin->argc, 6))
    return tcError;
  rgrBonusInf[3]     = RFromSz(pin->argv[1]);
  rgrBonusInf[4]     = RFromSz(pin->argv[2]);
  rgrBonusInf[5]     = RFromSz(pin->argv[3]);
  rHouseInf[cSign+3] = RFromSz(pin->argv[4]);
  rHouseInf[cSign+4] = RFromSz(pin->argv[5]);
  rHouseInf[cSign+5] = RFromSz(pin->argv[6]);
  return 6;
}

// -YJ, -YJ7 and -YJ70 share one shape: object, sign, cosign, stored to
// the object-keyed pair and mirrored into the sign-keyed pair for the
// planets those tables cover. The suite's "rulership" group pins the
// mirror's invariants.

static int NSwRulershipCore(int argc, char **argv, TBLOBJ &rgObj1,
  TBLOBJ &rgObj2, TBLSIG &rgSign1, TBLSIG &rgSign2, flag fWithEarthVulcan)
{
  int i, j, k;

  if (FErrorArgc("YJ", argc, 3))
    return tcError;
  i = NParseSz(argv[1], pmObject);
  if (FErrorValN("YJ", !FNorm(i), i, 1))
    return tcError;
  j = NParseSz(argv[2], pmSign);
  if (FErrorValN("YJ", !FBetween(j, 0, cSign), j, 2))
    return tcError;
  k = NParseSz(argv[3], pmSign);
  if (FErrorValN("YJ", !FBetween(k, 0, cSign), k, 3))
    return tcError;
  if (j <= 0)
    j = k;
  if (j == k)
    k = 0;
  rgObj1[OBJT(i)] = j;
  rgObj2[OBJT(i)] = k;
  if (fWithEarthVulcan ? (FBetween(i, 0, oPlu) || i == oVul) :
    FBetween(i, 1, oPlu)) {
    AdjustRulership(rgSign1, rgSign2, k, i, fFalse);
    AdjustRulership(rgSign1, rgSign2, j, i, fTrue);
  }
  return 3;
}

static int NSwYJ(CONST char *szSwitch, PARSEIN *pin)
{
  return NSwRulershipCore(pin->argc, pin->argv, ruler1, ruler2, rules, rules2,
    fFalse);
}

static int NSwYJ7(CONST char *szSwitch, PARSEIN *pin)
{
  return NSwRulershipCore(pin->argc, pin->argv, rgObjEso1, rgObjEso2, rgSignEso1,
    rgSignEso2, fTrue);
}

static int NSwYJ70(CONST char *szSwitch, PARSEIN *pin)
{
  return NSwRulershipCore(pin->argc, pin->argv, rgObjHie1, rgObjHie2, rgSignHie1,
    rgSignHie2, fTrue);
}

static int NSwYJ0(CONST char *szSwitch, PARSEIN *pin)
{
  int i, j;

  if (FErrorArgc("YJ", pin->argc, 2))
    return tcError;
  i = NParseSz(pin->argv[1], pmObject);
  if (FErrorValN("YJ", !FNorm(i), i, 1))
    return tcError;
  j = NParseSz(pin->argv[2], pmSign);
  if (FErrorValN("YJ", !FBetween(j, 0, cSign), j, 2))
    return tcError;
  exalt[OBJT(i)] = j;
  return 2;
}

static int NSwYAD(CONST char *szSwitch, PARSEIN *pin)
{
  int i;

  i = NParseSz(pin->argv[1], pmAspect);
  if (FErrorValN("YAD", !FAspect2(i), i, 1))
    return tcError;
  FCloneSzCore(CchSz(pin->argv[2]) >= 3 ? pin->argv[2] : szAspectName[i],
    (char **)&szAspectDisp[i], szAspectDisp[i] == szAspectName[i]);
  FCloneSzCore(CchSz(pin->argv[3]) >= 3 ? pin->argv[3] : szAspectAbbrev[i],
    (char **)&szAspectAbbrevDisp[i],
    szAspectAbbrevDisp[i] == szAspectAbbrev[i]);
  FCloneSzCore(CchSz(pin->argv[4]) >= 3 ? pin->argv[4] : szAspectGlyph[i],
    (char **)&szAspectGlyphDisp[i],
    szAspectGlyphDisp[i] == szAspectGlyph[i]);
  return 4;
}

// The -YR sub-switch pairs parse their booleans through pmObject the
// way the retired case did, quirks included.

static int NSwYRPair(int argc, char **argv, flag *pf1, flag *pf2)
{
  if (FErrorArgc("YR", argc, 2))
    return tcError;
  *pf1 = NParseSz(argv[1], pmObject) != 0;
  *pf2 = NParseSz(argv[2], pmObject) != 0;
  return 2;
}

static int NSwYR0(CONST char *szSwitch, PARSEIN *pin)
{
  return NSwYRPair(pin->argc, pin->argv, &us.fIgnoreSign, &us.fIgnoreDir);
}

static int NSwYR1(CONST char *szSwitch, PARSEIN *pin)
{
  return NSwYRPair(pin->argc, pin->argv, &us.fIgnoreDiralt, &us.fIgnoreDirlen);
}

static int NSwYR2(CONST char *szSwitch, PARSEIN *pin)
{
  return NSwYRPair(pin->argc, pin->argv, &us.fIgnoreAlt0, &us.fIgnoreDisequ);
}

static int NSwYRp(CONST char *szSwitch, PARSEIN *pin)
{
  if (FErrorArgc("YR", pin->argc, 2))
    return tcError;
  ignorez[arVer] = NParseSz(pin->argv[1], pmObject) != 0;
  ignorez[arAnt] = NParseSz(pin->argv[2], pmObject) != 0;
  return 2;
}

static int NSwYRZ(CONST char *szSwitch, PARSEIN *pin)
{
  if (FErrorArgc("YR", pin->argc, 4))
    return tcError;
  ignorez[arAsc] = NParseSz(pin->argv[1], pmObject) != 0;
  ignorez[arMC]  = NParseSz(pin->argv[2], pmObject) != 0;
  ignorez[arDes] = NFromSz(pin->argv[3]) != 0;
  ignorez[arIC]  = NFromSz(pin->argv[4]) != 0;
  return 4;
}

static int NSwYR7(CONST char *szSwitch, PARSEIN *pin)
{
  if (FErrorArgc("YR", pin->argc, 5))
    return tcError;
  ignore7[rrStd] = NParseSz(pin->argv[1], pmObject) != 0;
  ignore7[rrEso] = NParseSz(pin->argv[2], pmObject) != 0;
  ignore7[rrHie] = NFromSz(pin->argv[3]) != 0;
  ignore7[rrExa] = NFromSz(pin->argv[4]) != 0;
  ignore7[rrRay] = NFromSz(pin->argv[5]) != 0;
  if (!ignore7[rrRay])
    EnsureRay();
  return 5;
}

static int NSwYRd(CONST char *szSwitch, PARSEIN *pin)
{
  us.nSignDiv = NFromSz(pin->argv[1]);
  return 1;
}

static int NSwYRh(CONST char *szSwitch, PARSEIN *pin)
{
  SwitchF(us.fIgnoreAuto);
  return 0;
}

static int NSwYRo(CONST char *szSwitch, PARSEIN *pin)
{
  InitRestrictions(fTrue);
  return 0;
}

static int NSwYRi(CONST char *szSwitch, PARSEIN *pin)
{
  InitRestrictions(fFalse);
  AdjustRestrictions();
  AdjustAspectCount();
  return 0;
}

static int NSwYRU(CONST char *szSwitch, PARSEIN *pin)
{
  us.fStarsList = fFalse;
  FCloneSz(pin->argv[1], &us.szStarsList);
  return 1;
}

static int NSwYRU0(CONST char *szSwitch, PARSEIN *pin)
{
  if (FErrorArgc("YRU", pin->argc, 1))
    return tcError;
  us.fStarsList = fTrue;
  FCloneSz(pin->argv[1], &us.szStarsList);
  return 1;
}

static int NSwYkU(CONST char *szSwitch, PARSEIN *pin)
{
  FCloneSz(pin->argv[1], &us.szStarsColor);
  return 1;
}

static int NSwYkE(CONST char *szSwitch, PARSEIN *pin)
{
  FCloneSz(pin->argv[1], &us.szAstColor);
  return 1;
}

static int NSwYkC(CONST char *szSwitch, PARSEIN *pin)
{
  int k, l;

  if (FErrorArgc("Yk", pin->argc, 4))
    return tcError;
  for (k = 0; k < cElem; k++) {
    l = NParseSz(pin->argv[1+k], pmColor);
    if (FErrorValN("Yk", !FValidColorA(l), l, 1+k))
      return tcError;
    kElemA[k] = l;
  }
  return 4;
}

static int NSwYD(CONST char *szSwitch, PARSEIN *pin)
{
  int i;

  i = NParseSz(pin->argv[1], pmObject);
  if (FErrorValN("YD", !FItem(i), i, 1))
    return tcError;
  SetObjDisp(i, CchSz(pin->argv[2]) >= 2 ? pin->argv[2] : szObjName[i]);
  return 2;
}

static int NSwYS(CONST char *szSwitch, PARSEIN *pin)
{
  int i;
  real r;

  i = NParseSz(pin->argv[1], pmObject);
  if (FErrorValN("YS", !FNorm(i), i, 1))
    return tcError;
  r = RParseSz(pin->argv[2], pmDist);
  if (FErrorValR("YS", r < 0.0, r, 2))
    return tcError;
  rObjDiam[i] = r;
  return 2;
}

static int NSwYU(CONST char *szSwitch, PARSEIN *pin)
{
  int i;

  i = NParseSz(pin->argv[1], pmObject);
  if (FErrorValN("YU", !FStar(i), i, 1))
    return tcError;
  FCloneSz(pin->argv[2], &szStarCustom[i-oNorm]);
  rStarBrightDef[0] = -1.0;                    // Recompute brightness
  return 2;
}

static int NSwYUb(CONST char *szSwitch, PARSEIN *pin)
{
  SwitchF(us.fStarMagDist);
  return 0;
}

static int NSwYUb0(CONST char *szSwitch, PARSEIN *pin)
{
  SwitchF(us.fStarMagDist);
  SwitchF(us.fStarMagAbs);
  return 0;
}

static int NSwYUx(CONST char *szSwitch, PARSEIN *pin)
{
  FCloneSz(pin->argv[1], &us.szExoList);
  return 1;
}

static int NSwYF(CONST char *szSwitch, PARSEIN *pin)
{
  int i, j;
  real r;

  is.fHaveInfo = fTrue;
  i = NParseSz(pin->argv[1], pmObject);
  if (FErrorValN("YF", !FItem(i), i, 1))
    return tcError;
  r = Mod((real)(NFromSz(pin->argv[2]) +
    (NParseSz(pin->argv[3], pmSign)-1)*30) + RFromSz(pin->argv[4])/60.0);
  planet[i] = r;
  if (FCusp(i) && i != oAsc && i != oMC) {
    chouse[i-(cuspLo-1)] = r;
    if (i == oDes)
      chouse[sAri] = Mod(chouse[sLib] + rDegHalf);
    else if (i == oNad)
      chouse[sCap] = Mod(chouse[sCan] + rDegHalf);
  }
  j = NFromSz(pin->argv[5]);
  r = (j < 0 ? -1.0 : 1.0)*((real)NAbs(j) + RFromSz(pin->argv[6])/60.0);
  planetalt[i] = Mod((r + rDegQuad) * 2.0) / 2.0 - rDegQuad;
  ret[i] = RFromSz(pin->argv[7]);
  if (i <= oNorm)
    SphToRec(RFromSz(pin->argv[8]), planet[i], planetalt[i],
      &space[i].x, &space[i].y, &space[i].z);
  MM = -1;    // Assume a chart position file is being loaded.
  return 8;
}

#ifdef SWISS
// -Ye is a prefix row: the type, point, and flag letters ride in the
// switch spelling itself (-YemnHS...), scanned out of szSwitch just as
// the retired case scanned argv[0].

static int NSwYe(CONST char *szSwitch, PARSEIN *pin)
{
  OBJDEF od;
  char szName[cchSzDef], ch1, ch2, *pch;
  int i, j, k, l;

  if (FErrorArgc("Ye", pin->argc, 2))
    return tcError;
  i = NParseSz(pin->argv[1], pmObject);
  if (FErrorValN("Ye", !FCust(i), i, 1))
    return tcError;
  i -= custLo;
  ch1 = szSwitch[2];
  ch2 = ch1 == chNull ? chNull : szSwitch[3];
  j = (ch1 == 'b') + (ch1 == 'O')*2 + (ch1 == 'm')*3 + (ch1 == 'j')*4 +
    (ch1 == 'A')*5;
  if (j > 0)
    ch1 = ch2;
  k = (j == 2 ? NParseSz(pin->argv[2], pmObject) : NFromSz(pin->argv[2]));
  if (FErrorValN("Ye", !FValidCustom(k, j), k, 2))
    return tcError;
  od.nTyp = j; od.nObj = k;
  od.nPnt = (ch1 == 'n') + (ch1 == 's')*2 + (ch1 == 'p')*3 +
    (ch1 == 'a')*4;
  od.nFlg = 0;
  l = 2;
  do {
    ch2 = szSwitch[l++];
    od.nFlg |= (ch2 == 'H') + (ch2 == 'S')*2 + (ch2 == 'B')*4 +
      (ch2 == 'N')*8 + (ch2 == 'T')*16 + (ch2 == 'V')*32;
  } while (ch2);
  // The store, and the glyph rule with it, are ObjDefSet()'s. One
  // deliberate change from the old open-coded store: re-asserting a
  // slot's existing definition no longer drops its glyph, since the
  // slot still is that body.
  ObjDefSet(i + custLo, &od);
  if (j <= 1)
    SwissGetObjName(szName, j <= 0 ? -k : k);
  else if (j == 5)
    sprintf(szName, "%s", FValidPart(k) ? ai[k-1].name : szObjUnknown);
  else {
    if (j == 3)
      k = ObjMoons(k);
    sprintf(szName, "%s", FItem(k) ? szObjName[k] : szObjUnknown);
  }
  k = od.nPnt;
  if (k > 0) {
    for (pch = szName; *pch; pch++)
      ;
    sprintf(szName + Min(3, pch-szName), "%s",
      k == 1 ? "Nor" : (k == 2 ? "Sou" : (k == 3 ? "Per" : "Api")));
  }
  SetObjDisp(i + custLo, szName);
  return 2;
}
#endif

#ifdef MATRIX
static int NSwYE(CONST char *szSwitch, PARSEIN *pin)
{
  OE oe;
  int i;

  if (FErrorArgc("YE", pin->argc, 17))
    return tcError;
  i = NParseSz(pin->argv[1], pmObject);
  if (FErrorValN("YE", !FHelio(i), i, 1))
    return tcError;
  oe.sma = RFromSz(pin->argv[2]);
  oe.ec0 = atof(pin->argv[3]);  oe.ec1 = atof(pin->argv[4]);  oe.ec2 = atof(pin->argv[5]);
  oe.in0 = atof(pin->argv[6]);  oe.in1 = atof(pin->argv[7]);  oe.in2 = atof(pin->argv[8]);
  oe.ap0 = atof(pin->argv[9]);  oe.ap1 = atof(pin->argv[10]); oe.ap2 = atof(pin->argv[11]);
  oe.an0 = atof(pin->argv[12]); oe.an1 = atof(pin->argv[13]); oe.an2 = atof(pin->argv[14]);
  oe.ma0 = atof(pin->argv[15]); oe.ma1 = atof(pin->argv[16]); oe.ma2 = atof(pin->argv[17]);
  rgoe[IoeFromObj(i)] = oe;
  return 17;
}
#endif

#ifdef INTERPRET
// The six -YI spellings differ only in which phrase table and index
// domain they address.

static int NSwYIStore(int argc, char **argv, int pm, int iMin, int iMax,
  CONST char **rgsz, CONST char **rgszDef)
{
  int i;

  if (FErrorArgc("YI", argc, 2))
    return tcError;
  i = NParseSz(argv[1], pm);
  if (FErrorValN("YI", !FBetween(i, iMin, iMax), i, 1))
    return tcError;
  FCloneSzCore(argv[2], (char **)&rgsz[i], rgsz[i] == rgszDef[i]);
  return 2;
}

static int NSwYI(CONST char *szSwitch, PARSEIN *pin)
{
  return NSwYIStore(pin->argc, pin->argv, pmObject, 0, cObj,
    szMindPart, szMindPartDef);
}

static int NSwYIa(CONST char *szSwitch, PARSEIN *pin)
{
  return NSwYIStore(pin->argc, pin->argv, pmSign, 1, cSign, szDesc, szDescDef);
}

static int NSwYIv(CONST char *szSwitch, PARSEIN *pin)
{
  return NSwYIStore(pin->argc, pin->argv, pmSign, 1, cSign, szDesire, szDesireDef);
}

static int NSwYIC(CONST char *szSwitch, PARSEIN *pin)
{
  return NSwYIStore(pin->argc, pin->argv, pmSign, 1, cSign,
    szLifeArea, szLifeAreaDef);
}

static int NSwYIA(CONST char *szSwitch, PARSEIN *pin)
{
  return NSwYIStore(pin->argc, pin->argv, pmAspect, 1, cAspect,
    szInteract, szInteractDef);
}

static int NSwYIA0(CONST char *szSwitch, PARSEIN *pin)
{
  return NSwYIStore(pin->argc, pin->argv, pmAspect, 1, cAspect,
    szTherefore, szThereforeDef);
}
#endif

static int NSwYYt(CONST char *szSwitch, PARSEIN *pin)
{
  if (!us.fGraphics)
    PrintSzFormat(pin->argv[1], fFalse);
  return 1;
}

static int NSwYYT(CONST char *szSwitch, PARSEIN *pin)
{
  PrintSzFormat(pin->argv[1], fTrue);
  return 1;
}

#ifdef INTERPRET
// -YYI is documented in -HY and had never worked: its implementation
// sat behind a misspelled "#ifdef INTRPRET" upstream, so the spelling
// fell through to the atlas payload branch (pre-M3) or errored as
// unknown (post-M3). The registry row does what the help always said.

static int NSwYYI(CONST char *szSwitch, PARSEIN *pin)
{
  FieldWord(pin->argv[1]);
  return 1;
}
#endif

#ifdef ATLAS
// The -YY payload family reads the rest of the switch file being
// parsed, through the parse context the file parser passes down --
// which is why these only work in file context.

static int NSwYYLoad(int argc, char **argv, PARSECTX *pctx, int nTyp)
{
  char ch;

  if (FErrorArgc("YY", argc, 1 + (nTyp == 1 || nTyp == 2)))
    return tcError;
  if (pctx == NULL || pctx->fileIn == NULL) {
    PrintError("Switch only allowed in file context.");
    return tcError;
  }
  ch = getc(pctx->fileIn);
  if (ch >= ' ')
    ungetc(ch, pctx->fileIn);
  if (nTyp <= 0) {
    if (!FLoadAtlas(pctx->fileIn, NFromSz(argv[1])))
      return tcError;
  } else if (nTyp == 1) {
    if (!FLoadZoneRules(pctx->fileIn, NFromSz(argv[1]), NFromSz(argv[2])))
      return tcError;
  } else if (nTyp == 2) {
    if (!FLoadZoneChanges(pctx->fileIn, NFromSz(argv[1]),
      NFromSz(argv[2])))
      return tcError;
  } else {
    if (!FLoadZoneLinks(pctx->fileIn, NFromSz(argv[1])))
      return tcError;
  }
  return 1 + (nTyp == 1 || nTyp == 2);
}

static int NSwYY(CONST char *szSwitch, PARSEIN *pin)
{
  return NSwYYLoad(pin->argc, pin->argv, pin->pctx, 0);
}

static int NSwYY1(CONST char *szSwitch, PARSEIN *pin)
{
  return NSwYYLoad(pin->argc, pin->argv, pin->pctx, 1);
}

static int NSwYY2(CONST char *szSwitch, PARSEIN *pin)
{
  return NSwYYLoad(pin->argc, pin->argv, pin->pctx, 2);
}

static int NSwYY3(CONST char *szSwitch, PARSEIN *pin)
{
  return NSwYYLoad(pin->argc, pin->argv, pin->pctx, 3);
}
#endif

static int NSwYu(CONST char *szSwitch, PARSEIN *pin)
{
  // Settings files pack fEclipseAny into the "0" suffix, so an explicit
  // "=Yu" or "_Yu" means it is off; only a bare toggling -Yu leaves it
  // alone (see the -Yu fixed-point fix, work log item 66).
  if (pin->fOr || pin->fAnd)
    us.fEclipseAny = fFalse;
  SwitchF(us.fEclipse);
  return 0;
}

static int NSwYu0(CONST char *szSwitch, PARSEIN *pin)
{
  SwitchF(us.fEclipseAny);
  SwitchF(us.fEclipse);
  return 0;
}

static int NSwYs(CONST char *szSwitch, PARSEIN *pin)
{
  real r;

  if (pin->argc > 1 && (r = RParseSz(pin->argv[1], pmOffset)) != rLarge) {
    if (FErrorValR("Ys", !FValidOffset(r), r, 0))
      return 0;    // The retired case "continued" here, consuming nothing.
    us.rZodiacOffsetAll = r;
    SwitchF(us.fSidereal2);
    return 1;
  }
  SwitchF(us.fSidereal2);
  return 0;
}

static int NSwYc(CONST char *szSwitch, PARSEIN *pin)
{
  int i;

  SwitchF(us.fHouseAngle);
  for (i = 0; i < 4; i++)
    SetObjDisp(oAsc + i*3, us.fHouseAngle ? szObjName[objMax + i] :
      szObjName[oAsc + i*3]);
  return 0;
}

static int NSwYl(CONST char *szSwitch, PARSEIN *pin)
{
  int i;

  i = NFromSz(pin->argv[1]);
  if (FErrorValN("Yl", !FSector(i), i, 0))
    return tcError;
  SwitchF(pluszone[i]);
  return 1;
}

static int NSwY1Core(int argc, char **argv, flag fAnd, flag fZero)
{
  int i, j;

  if (FErrorArgc("Y1", argc, 2))
    return tcError;
  i = NParseSz(argv[1], pmObject);
  if (FErrorValN("Y1", !FItem(i), i, 1))
    return tcError;
  j = NParseSz(argv[2], pmObject);
  if (FErrorValN("Y1", !FItem(j), j, 2))
    return tcError;
  us.fObjRotWhole = fZero && !fAnd;
  us.objRot1 = i;
  us.objRot2 = j;
  return 2;
}

static int NSwY1(CONST char *szSwitch, PARSEIN *pin)
{
  return NSwY1Core(pin->argc, pin->argv, pin->fAnd, fFalse);
}

static int NSwY10(CONST char *szSwitch, PARSEIN *pin)
{
  return NSwY1Core(pin->argc, pin->argv, pin->fAnd, fTrue);
}

static int NSwYz(CONST char *szSwitch, PARSEIN *pin)
{
  us.lTimeAddition = NFromSz(pin->argv[1]);
  return 1;
}

static int NSwYz0(CONST char *szSwitch, PARSEIN *pin)
{
  if (pin->fAnd) {
    us.rDeltaT = rInvalid;   // "_Yz0" alone restores automatic Delta-T.
    return 0;
  }
  if (FErrorArgc("Yz", pin->argc, 1))
    return tcError;
  us.rDeltaT = RFromSz(pin->argv[1]);
  return 1;
}

static int NSwYzO(CONST char *szSwitch, PARSEIN *pin)
{
  if (FErrorArgc("Yz", pin->argc, 1))
    return tcError;
  us.rObjAddition = RFromSz(pin->argv[1]);
  return 1;
}

static int NSwYzC(CONST char *szSwitch, PARSEIN *pin)
{
  if (FErrorArgc("Yz", pin->argc, 1))
    return tcError;
  us.rCuspAddition = RFromSz(pin->argv[1]);
  return 1;
}

static int NSwYQ(CONST char *szSwitch, PARSEIN *pin)
{
  int i;

  i = NFromSz(pin->argv[1]);
  if (FErrorValN("YQ", i < 0, i, 0))
    return tcError;
  us.nScrollRow = i;
  return 1;
}

static int NSwYw(CONST char *szSwitch, PARSEIN *pin)
{
  real r;

  r = RFromSz(pin->argv[1]);
  if (FErrorValR("Yw", r < 0.0, r, 0))
    return tcError;
  us.rStation = r;
  return 1;
}

static int NSwYZ(CONST char *szSwitch, PARSEIN *pin)
{
  int i;

  i = NFromSz(pin->argv[1]);
  if (FErrorValN("YZ", !FBetween(i, 0, 7), i, 0))
    return tcError;
  us.nHorizon = i;
  return 1;
}

static int NSwYb(CONST char *szSwitch, PARSEIN *pin)
{
  int i;

  i = NFromSz(pin->argv[1]);
  if (FErrorValN("Yb", !FValidBioday(i), i, 0))
    return tcError;
  us.nBioday = i;
  return 1;
}

#ifdef ARABIC
static int NSwYP(CONST char *szSwitch, PARSEIN *pin)
{
  int i;

  i = NFromSz(pin->argv[1]);
  if (FErrorValN("YP", !FBetween(i, -1, 1), i, 0))
    return tcError;
  us.nArabicNight = i;
  return 1;
}
#endif

static int NSwYB(CONST char *szSwitch, PARSEIN *pin)
{
#ifndef WIN
  putchar(chBell);
#else
  MessageBeep((UINT)-1);
#endif
  return 0;
}

static int NSwY5i(CONST char *szSwitch, PARSEIN *pin)
{
  FCloneSz(pin->argv[1], &us.szADB);
  return 1;
}

static int NSwY5I(CONST char *szSwitch, PARSEIN *pin)
{
  int i;

  i = NFromSz(pin->argv[1]);
  if (FErrorValN("Y5I", i < 0, i, 1))
    return tcError;
  us.iExpADB = i;
  us.cExpADB = NFromSz(pin->argv[2]);
  return 2;
}

// The prefix rows below scan their parameters out of the spelling, at
// the same offsets the retired cases read them from argv[0]: the
// letter after the family name is szSwitch[2].

static int NSwY5(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1 = szSwitch[2];

  FEnumerateCIList(1 + (ch1 == '2') + (ch1 == '3')*2 + (ch1 == '4')*3);
  return 0;
}

static int NSwYa(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1 = szSwitch[2];
  int i;

  if (ch1 == 'o') {
    i = szSwitch[3] - '0';
    if (!FBetween(i, 0, 3))
      i = 0;
    us.nCharsetOut = i;
  } else {
    i = ch1 - '0';
    if (!FBetween(i, 0, 3))
      i = 0;
    us.nCharset = i;
  }
  return 0;
}

static int NSwYq(CONST char *szSwitch, PARSEIN *pin)
{
  int i;

  i = szSwitch[2] - '0';
  if (!FBetween(i, 0, 9))
    i = 0;
  if (FErrorArgc("Yq", pin->argc, i))
    return tcError;
  us.cSequenceLine = i;
  for (i = 0; i < us.cSequenceLine; i++)
    FCloneSz(pin->argv[i+1], &is.rgszLine[i]);
  return i;
}

static int NSwYi(CONST char *szSwitch, PARSEIN *pin)
{
  int i;

  if (FErrorArgc("Yi", pin->argc, 1))
    return tcError;
  i = szSwitch[2] - '0';
  if (!FBetween(i, 0, 9))
    i = 0;
  FCloneSz(pin->argv[1], &us.rgszPath[i]);
  is.fSwissPathSet = fFalse;
  return 1;
}

#ifdef GRAPH
// The -YX graphics family, migrated whole from the retired
// NProcessSwitchesRareX() in xscreen.cpp.

// -YX itself is no longer implemented, but still skips 2 parameters and
// does nothing, for compatibility with old astrolog.as files.

static int NSwYXNull(CONST char *szSwitch, PARSEIN *pin)
{
  return 2;
}

static int NSwYXG(CONST char *szSwitch, PARSEIN *pin)
{
  int i;

  if (FErrorArgc("YXG", pin->argc, 1))
    return tcError;
  i = NFromSz(pin->argv[1]);
  switch (szSwitch[3]) {
  case 'c':
    if (FErrorValN("YXGc", !FBetween(i, 1, 2), i, 0))
      return tcError;
    gs.nGlyphCap = i;
    break;
  case 'u':
    if (FErrorValN("YXGu", !FBetween(i, 1, 2), i, 0))
      return tcError;
    gs.nGlyphUra = i;
    break;
  case 'p':
    if (FErrorValN("YXGp", !FBetween(i, 1, 3), i, 0))
      return tcError;
    gs.nGlyphPlu = i;
    break;
  case 'l':
    if (FErrorValN("YXGl", !FBetween(i, 1, 2), i, 0))
      return tcError;
    gs.nGlyphLil = i;
    break;
  case 'v':
    if (FErrorValN("YXGv", !FBetween(i, 1, 2), i, 0))
      return tcError;
    gs.nGlyphVer = i;
    break;
  case 'e':
    if (FErrorValN("YXGe", !FBetween(i, 1, 2), i, 0))
      return tcError;
    gs.nGlyphEri = i;
    break;
  default:
    if (FErrorValN("YXG", !FValidGlyphs(i), i, 0))
      return tcError;
    if (FBetween(i/100000,   1, 2)) gs.nGlyphCap = i/100000;
    if (FBetween(i/10000%10, 1, 2)) gs.nGlyphUra = i/10000%10;
    if (FBetween(i/1000%10,  1, 3)) gs.nGlyphPlu = i/1000%10;
    if (FBetween(i/100%10,   1, 2)) gs.nGlyphLil = i/100%10;
    if (FBetween(i/10%10,    1, 2)) gs.nGlyphVer = i/10%10;
    if (FBetween(i%10,       1, 2)) gs.nGlyphEri = i%10;
    break;
  }
  return 1;
}

static int NSwYXDCore(int argc, char **argv, char chVar)
{
  int i, j;

  if (FErrorArgc("YXD", argc, 3 - (chVar == '1' || chVar == 'D')))
    return tcError;
  i = NParseSz(argv[1], pmObject);
  if (FErrorValN("YXD", !FItem(i), i, 1))
    return tcError;
  if (chVar == 'D') {
    j = NParseSz(argv[2], pmObject);
    if (FErrorValN("YXDD", !FItem(j), j, 2))
      return tcError;
    FCloneSz(szDrawObject[j], (char **)&szDrawObject[i]);
    FCloneSz(szDrawObject2[j], (char **)&szDrawObject2[i]);
  } else {
    FCloneSzCore(argv[2][0] ? argv[2] : szDrawObjectDef[i],
      (char **)&szDrawObject[i], szDrawObject[i] == szDrawObjectDef[i]);
    FCloneSzCore(
      chVar == '1' ? "" : (argv[3][0] ? argv[3] : szDrawObjectDef2[i]),
      (char **)&szDrawObject2[i], szDrawObject2[i] == szDrawObjectDef2[i]);
  }
  return 3 - (chVar == '1' || chVar == 'D');
}

static int NSwYXD(CONST char *szSwitch, PARSEIN *pin)
{
  return NSwYXDCore(pin->argc, pin->argv, chNull);
}

static int NSwYXD1(CONST char *szSwitch, PARSEIN *pin)
{
  return NSwYXDCore(pin->argc, pin->argv, '1');
}

static int NSwYXDD(CONST char *szSwitch, PARSEIN *pin)
{
  return NSwYXDCore(pin->argc, pin->argv, 'D');
}

static int NSwYXACore(int argc, char **argv, flag fSingle)
{
  int i;

  if (FErrorArgc("YXA", argc, 3 - fSingle))
    return tcError;
  i = NParseSz(argv[1], pmAspect);
  if (FErrorValN("YXA", !FAspect3(i), i, 0))
    return tcError;
  FCloneSzCore(argv[2][0] ? argv[2] : szDrawAspectDef[i],
    (char **)&szDrawAspect[i], szDrawAspect[i] == szDrawAspectDef[i]);
  FCloneSzCore(
    fSingle ? "" : (argv[3][0] ? argv[3] : szDrawAspectDef2[i]),
    (char **)&szDrawAspect2[i], szDrawAspect2[i] == szDrawAspectDef2[i]);
  return 3 - fSingle;
}

static int NSwYXA(CONST char *szSwitch, PARSEIN *pin)
{
  return NSwYXACore(pin->argc, pin->argv, fFalse);
}

static int NSwYXA1(CONST char *szSwitch, PARSEIN *pin)
{
  return NSwYXACore(pin->argc, pin->argv, fTrue);
}

static int NSwYXv(CONST char *szSwitch, PARSEIN *pin)
{
  int i, darg = 0;

  if (FErrorArgc("YXv", pin->argc, 1))
    return tcError;
  i = NFromSz(pin->argv[1]);
  if (FErrorValN("YXv", !FValidDecaType(i), i, 1))
    return tcError;
  gs.nDecaType = i;
  darg++;
  if (pin->argc > 2 && FNumCh(pin->argv[2][0])) {
    i = NFromSz(pin->argv[2]);
    if (FErrorValN("YXv", !FValidDecaSize(i), i, 2))
      return tcError;
    gs.nDecaSize = i;
    darg++;
    if (pin->argc > 3 && FNumCh(pin->argv[3][0])) {
      i = NFromSz(pin->argv[3]);
      if (FErrorValN("YXv", !FValidDecaLine(i), i, 3))
        return tcError;
      gs.nDecaLine = i;
      darg++;
    }
  }
  return darg;
}

static int NSwYXt(CONST char *szSwitch, PARSEIN *pin)
{
  FCloneSz(pin->argv[1], &gs.szSidebar);
  return 1;
}

static int NSwYXg(CONST char *szSwitch, PARSEIN *pin)
{
  int i;

  i = NFromSz(pin->argv[1]);
  if (FErrorValN("YXg", !FValidGrid(i), i, 0))
    return tcError;
  gs.nGridCell = i;
  return 1;
}

static int NSwYXS(CONST char *szSwitch, PARSEIN *pin)
{
  real rT;

  rT = RFromSz(pin->argv[1]);
  if (FErrorValR("YXS", !FValidZoom(rT), rT, 0))
    return tcError;
  gs.rspace = rT;
  return 1;
}

static int NSwYXj(CONST char *szSwitch, PARSEIN *pin)
{
  gs.cspace = NFromSz(pin->argv[1]);
  if (gi.rgspace != NULL) {
    DeallocateP(gi.rgspace);
    gi.rgspace = NULL;
  }
  return 1;
}

static int NSwYXj0(CONST char *szSwitch, PARSEIN *pin)
{
  if (FErrorArgc("YXj", pin->argc, 1))
    return tcError;
  gs.zspace = NFromSz(pin->argv[1]);
  return 1;
}

static int NSwYX7(CONST char *szSwitch, PARSEIN *pin)
{
  int i;

  i = NFromSz(pin->argv[1]);
  if (FErrorValN("YX7", !FValidEsoteric(i), i, 0))
    return tcError;
  gs.nRayWidth = i;
  return 1;
}

static int NSwYXk(CONST char *szSwitch, PARSEIN *pin)
{
  SwitchF(gs.fColorSign);
  return 0;
}

static int NSwYXk0(CONST char *szSwitch, PARSEIN *pin)
{
  SwitchF(gs.fColorSign);
  SwitchF(gs.fColorHouse);
  return 0;
}

static int NSwYXK(CONST char *szSwitch, PARSEIN *pin)
{
  int i;

  i = NParseSz(pin->argv[1], pmColor);
  if (FErrorValN("YXK", !FValidColor(i), i, 0))
    return tcError;
  rgbbmp[i] = NParseSz(pin->argv[2], pmRGB);
  return 2;
}

static int NSwYXK0(CONST char *szSwitch, PARSEIN *pin)
{
  SwitchF(gs.fAltPalette);
  InitColorPalette(gs.fInverse);
  return 0;
}

static int NSwYXa(CONST char *szSwitch, PARSEIN *pin)
{
  gs.nDashMax = NFromSz(pin->argv[1]);
  return 1;
}

static int NSwYXx(CONST char *szSwitch, PARSEIN *pin)
{
  gs.nThickAdjust = NFromSz(pin->argv[1]);
  return 1;
}

static int NSwYXW(CONST char *szSwitch, PARSEIN *pin)
{
  // The retired case read pin->argv[1] with no arity check at all -- the one
  // divergence in this family that is a fix, not a transliteration.
  if (FErrorArgc("YXW", pin->argc, 1))
    return tcError;
  gs.nTriangles = NFromSz(pin->argv[1]);
  return 1;
}

#ifdef SWISS
static int NSwYXU(CONST char *szSwitch, PARSEIN *pin)
{
  if (!FProcessYXU(pin->argv[1], pin->argv[2], fFalse))
    return tcError;
  return 2;
}

static int NSwYXU0(CONST char *szSwitch, PARSEIN *pin)
{
  flag fAdd;

  if (FErrorArgc("YXU", pin->argc, 2))
    return tcError;
  fAdd = FSzSet(gs.szStarsLin) && FSzSet(gs.szStarsLnk);
  if (!FProcessYXU(pin->argv[1], pin->argv[2], fAdd))
    return tcError;
  return 2;
}

static int NSwYXU1(CONST char *szSwitch, PARSEIN *pin)
{
#ifdef CONSTEL
  CONST char **ppch;

  for (ppch = szDrawConstelLine; *ppch != NULL; ppch += 2) {
    if (!FProcessYXU(ppch[0], ppch[1], ppch != szDrawConstelLine))
      return tcError;
  }
#endif
  return 0;
}
#endif // SWISS

static int NSwYXf(CONST char *szSwitch, PARSEIN *pin)
{
  int i;

  if (FErrorArgc("YXf", pin->argc, 1))
    return tcError;
  i = NFromSz(pin->argv[1]);
  switch (szSwitch[3]) {
  case 't':
    if (FErrorValN("YXft", !FValidFont(0, i), i, 0))
      return tcError;
    gs.nFontTxt = i;
    break;
  case 's':
    if (FErrorValN("YXfs", !FValidFont(1, i), i, 0))
      return tcError;
    gs.nFontSig = i;
    break;
  case 'h':
    if (FErrorValN("YXfh", !FValidFont(2, i), i, 0))
      return tcError;
    gs.nFontHou = i;
    break;
  case 'o':
    if (FErrorValN("YXfo", !FValidFont(3, i), i, 0))
      return tcError;
    gs.nFontObj = i;
    break;
  case 'a':
    if (FErrorValN("YXfa", !FValidFont(4, i), i, 0))
      return tcError;
    gs.nFontAsp = i;
    break;
  case 'n':
    if (FErrorValN("YXfn", !FValidFont(5, i), i, 0))
      return tcError;
    gs.nFontNak = i;
    break;
  default:
    if (FErrorValN("YXf", !FBetween(i, 0, 0xffffff), i, 0))
      return tcError;
    gs.nFontTxt = i/0x100000;
    gs.nFontSig = i/0x10000 % 0x10;
    gs.nFontHou = i/0x1000 % 0x10;
    gs.nFontObj = i/0x100 % 0x10;
    gs.nFontAsp = i/0x10 % 0x10;
    gs.nFontNak = i%0x10;
    if (!FValidFont(0, gs.nFontTxt)) gs.nFontTxt = 0;
    if (!FValidFont(1, gs.nFontSig)) gs.nFontSig = 0;
    if (!FValidFont(2, gs.nFontHou)) gs.nFontHou = 0;
    if (!FValidFont(3, gs.nFontObj)) gs.nFontObj = 0;
    if (!FValidFont(4, gs.nFontAsp)) gs.nFontAsp = 0;
    if (!FValidFont(5, gs.nFontNak)) gs.nFontNak = 0;
    break;
  }
  gs.nFontAll = gs.nFontTxt*0x100000 + gs.nFontSig*0x10000 +
    gs.nFontHou*0x1000 + gs.nFontObj*0x100 + gs.nFontAsp*0x10 +
    gs.nFontNak;
  if (gs.nFontAll != 0)
    gi.nFontPrev = gs.nFontAll;
  return 1;
}

#ifdef PSCRIPT
static int NSwYXp(CONST char *szSwitch, PARSEIN *pin)
{
  gs.nOrient = NFromSz(pin->argv[1]);
  return 1;
}

static int NSwYXp0(CONST char *szSwitch, PARSEIN *pin)
{
  // pmLength honors the "cm" suffix SzLength() writes and the dialogs
  // already parse; a bare RFromSz() read "21.59cm" as 21.59 inches, so
  // each metric save/load cycle multiplied by 2.54.
  gs.xInch = RParseSz(pin->argv[1], pmLength);
  gs.yInch = RParseSz(pin->argv[2], pmLength);
  return 2;
}
#endif // PSCRIPT
#endif // GRAPH

#ifdef GRAPH
// The -X graphics family, migrated whole from the retired
// NProcessSwitchesX() in xscreen.cpp. Every row carries grfSwGraphics:
// the dispatch refuses it under -0X and turns graphics mode on when it
// succeeds, as the retired case 'X' did around its whole sub-parser.

static int NSwX(CONST char *szSwitch, PARSEIN *pin)
{
  return 0;    // Bare -X: the grfSwGraphics epilogue is the whole point.
}

static int NSwXb(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1;

  if (us.fNoWrite || is.fSzInteract) {
    ErrorArgv("Xb");
    return tcError;
  }
  ch1 = ChCap(szSwitch[2]);
  if (ch1 == 'B')
    gi.fBmp = fFalse;
  else if (ch1 == 'W') {
    ch1 = 'B';
    gi.fBmp = fTrue;
  } else if (ch1 == 'P')
    gi.fBmp = fTrue;
  if (FValidBmpmode(ch1))
    gs.chBmpMode = ch1;
  gs.ft = FSwitchF2(gs.ft == ftBmp) * ftBmp;
  return 0;
}

#ifdef PSCRIPT
static int NSwXp(CONST char *szSwitch, PARSEIN *pin)
{
  if (us.fNoWrite || is.fSzInteract) {
    ErrorArgv("Xp");
    return tcError;
  }
  gs.ft = FSwitchF2(gs.ft == ftPS) * ftPS;
  gs.fPSComplete = (szSwitch[2] == '0');
  return 0;
}
#endif

static int NSwXM(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1 = szSwitch[2], ch2 = ch1 == chNull ? chNull : szSwitch[3];
  int i, j;

  if (FBetween(ch1, '1', '0' + cRing)) {
    i = (ch1 - '0') + (ch2 == '0');
    if (FErrorArgc("XM", pin->argc, i))
      return tcError;
    for (j = 1; j <= i; j++)
      FCloneSz(pin->argv[j], &szWheelX[(ch2 == '0' && j >= i) ? 0 : j]);
    return i;
  }
#ifdef METAFILE
  if (us.fNoWrite || is.fSzInteract) {
    ErrorArgv("XM");
    return tcError;
  }
  gs.ft = FSwitchF2(gs.ft == ftWmf) * ftWmf;
#endif
  return 0;
}

#ifdef SVG
static int NSwXV(CONST char *szSwitch, PARSEIN *pin)
{
  if (us.fNoWrite || is.fSzInteract) {
    ErrorArgv("XV");
    return tcError;
  }
  gs.ft = FSwitchF2(gs.ft == ftSVG) * ftSVG;
  return 0;
}
#endif

#ifdef WIRE
static int NSwX3(CONST char *szSwitch, PARSEIN *pin)
{
  if (us.fNoWrite || is.fSzInteract) {
    ErrorArgv("X3");
    return tcError;
  }
  gs.ft = FSwitchF2(gs.ft == ftWire) * ftWire;
  return 0;
}
#endif

static int NSwXo(CONST char *szSwitch, PARSEIN *pin)
{
  if (us.fNoWrite || is.fSzInteract) {
    ErrorArgv("Xo");
    return tcError;
  }
  if (FErrorArgc("Xo", pin->argc, 1))
    return tcError;
  if (gs.ft == ftNone)
    gs.ft = ftBmp;
  FCloneSz(pin->argv[1], &gi.szFileOut);
  return 1;
}

#ifdef X11
static int NSwXB(CONST char *szSwitch, PARSEIN *pin)
{
  if (is.fSzInteract) {
    ErrorArgv("XB");
    return tcError;
  }
  SwitchF(gs.fRoot);
  return 0;
}

static int NSwXd(CONST char *szSwitch, PARSEIN *pin)
{
  if (is.fSzInteract) {
    ErrorArgv("Xd");
    return tcError;
  }
  if (FErrorArgc("Xd", pin->argc, 1))
    return tcError;
  FCloneSz(pin->argv[1], &gs.szDisplay);
  return 1;
}
#endif

static int NSwXI(CONST char *szSwitch, PARSEIN *pin)
{
  FLoadBmp(pin->argv[1], &gi.bmpBack, fFalse);
  return 1;
}

static int NSwXI0(CONST char *szSwitch, PARSEIN *pin)
{
  real rT;
  int i;

  SwitchF2(gs.fBackDraw);
  if (pin->fAnd)
    return 0;
  if (FErrorArgc("XI0", pin->argc, 2))
    return tcError;
  rT = RFromSz(pin->argv[1]);
  i = NFromSz(pin->argv[2]);
  if (FErrorValR("XI0", !FValidBackPct(rT), rT, 1))
    return tcError;
  if (FErrorValN("XI0", !FValidBackOrient(i), i, 2))
    return tcError;
  gs.rBackPct = rT;
  gs.nBackOrient = i;
  return 2;
}

static int NSwXIW(CONST char *szSwitch, PARSEIN *pin)
{
  FLoadBmp(pin->argv[1], &gi.bmpWorld, fFalse);
  return 1;
}

static int NSwXr(CONST char *szSwitch, PARSEIN *pin)
{
  SwitchF(gs.fInverse);
  InitColorPalette(gs.fInverse);
  return 0;
}

static int NSwXw(CONST char *szSwitch, PARSEIN *pin)
{
  int i, j, darg = 0;

  if (FErrorArgc("Xw", pin->argc, 1))
    return tcError;
  i = NFromSz(pin->argv[1]);
  if (pin->argc > 2 && ((j = NFromSz(pin->argv[2])) || pin->argv[2][0] == '0'))
    darg++;
  else
    j = i;
  if (FErrorValN("Xw", !FValidGraphX(i), i, 1))
    return tcError;
  if (FErrorValN("Xw", !FValidGraphY(j), j, 2))
    return tcError;
  // gs.xWin includes the sidebar everywhere else in this program -- the
  // places that want the chart alone subtract it and add it back -- and
  // FOutputSettings() writes this switch without it, saying so in the
  // comment beside the value. This did not add it back once, so every
  // save-and-reload shrank the window by one sidebar (work log).
  gs.xWin = i; gs.yWin = j;
  if (fSidebar)
    gs.xWin += (SIDESIZE * gi.nScaleText) >> 1;
  return darg + 1;
}

static int NSwXs(CONST char *szSwitch, PARSEIN *pin)
{
  int i;

  i = NFromSz(pin->argv[1]);
  if (i < 100)
    i *= 100;
  if (FErrorValN("Xs", !FValidScale(i), i, 0))
    return tcError;
  gs.nScale = i;
  gi.nScale = gs.nScale/100;   // Refresh so -Xs within -XM2 works
  return 1;
}

static int NSwXS(CONST char *szSwitch, PARSEIN *pin)
{
  int i;

  i = NFromSz(pin->argv[1]);
  if (i < 100)
    i *= 100;
  if (FErrorValN("XS", !FValidScaleText(i), i, 0))
    return tcError;
  gs.nScaleText = i;
  AdjustTextScale();    // Refresh so changing -XS works
  return 1;
}

static int NSwXU(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1 = szSwitch[2];

  if (ch1 == 'x') {
    SwitchF(gs.fAllExo);
    return 0;
  }
  SwitchF(gs.fAllStar);
  if (FBetween(ch1, '0', '3'))
    gs.nAllStar = (ch1 - '0');
  return 0;
}

static int NSwXE(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1 = szSwitch[2];

  if (FErrorArgc("XE", pin->argc, 2))
    return tcError;
  if (FBetween(ch1, '0', '3'))
    gs.nAstLabel = (ch1 - '0');
  gs.nAstLo = NFromSz(pin->argv[1]);
  gs.nAstHi = NFromSz(pin->argv[2]);
  return 2;
}

#ifdef ATLAS
static int NSwXL(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1 = szSwitch[2];

  SwitchF(gs.fLabelCity);
  if (FBetween(ch1, '1', '5'))
    gs.nLabelCity = (ch1 - '0');
  return 0;
}
#endif

static int NSwX1Or2(int argc, char **argv, flag fAnd, flag fSecond)
{
  int i;

  if (fAnd) {
    gs.objLeft = 0;
    return 0;
  }
  if (FErrorArgc(fSecond ? "X2" : "X1", argc, 1))
    return tcError;
  i = NParseSz(argv[1], pmObject);
  if (FErrorValN(fSecond ? "X2" : "X1", !FItem(i), i, 0))
    return tcError;
  gs.objLeft = fSecond ? -i-1 : i+1;
  return 1;
}

static int NSwXOne(CONST char *szSwitch, PARSEIN *pin)
{
  return NSwX1Or2(pin->argc, pin->argv, pin->fAnd, fFalse);
}

static int NSwXTwo(CONST char *szSwitch, PARSEIN *pin)
{
  return NSwX1Or2(pin->argc, pin->argv, pin->fAnd, fTrue);
}

static int NSwXv(CONST char *szSwitch, PARSEIN *pin)
{
  int i;

  i = NFromSz(pin->argv[1]);
  if (FErrorValN("Xv", !FValidDecaFill(i), i, 0))
    return tcError;
  gs.nDecaFill = i;
  return 1;
}

// The -XX/-XW/-XG/-XP/-XZ chart modes share a shape: optional numeric
// rotation (and tilt) arguments, then toggle the mode, with a "0" (or
// "v") suffix flipping a related flag.

static int NSwXX(CONST char *szSwitch, PARSEIN *pin)
{
  real rT;
  int darg = 0;

  if (pin->argc > 1 && ((rT = RFromSz(pin->argv[1])) || pin->argv[1][0] == '0')) {
    darg++;
    if (FErrorValR("XX", !FValidRotation(rT), rT, 1))
      return tcError;
    gs.rRot = rT;
    if (pin->argc > 2 && ((rT = RFromSz(pin->argv[2])) || pin->argv[2][0] == '0')) {
      darg++;
      if (FErrorValR("XX", !FValidTilt(rT), rT, 2))
        return tcError;
      gs.rTilt = rT;
    }
  }
  gi.nMode = FSwitchF2(gi.nMode == gSphere) * gSphere;
  if (szSwitch[2] == '0')
    SwitchF(gs.fSouth);
  return darg;
}

static int NSwXW(CONST char *szSwitch, PARSEIN *pin)
{
  real rT;
  int darg = 0;

  if (pin->argc > 1 && ((rT = RFromSz(pin->argv[1])) || pin->argv[1][0] == '0')) {
    darg++;
    if (FErrorValR("XW", !FValidRotation(rT), rT, 0))
      return tcError;
    gs.rRot = rT;
  }
  gi.nMode = FSwitchF2(gi.nMode == gWorldMap) * gWorldMap;
  if (szSwitch[2] == '0')
    SwitchF(gs.fMollweide);
  is.fHaveInfo |= gs.fAlt;
  return darg;
}

static int NSwXG(CONST char *szSwitch, PARSEIN *pin)
{
  real rT;
  int darg = 0;

  if (pin->argc > 1 && ((rT = RFromSz(pin->argv[1])) || pin->argv[1][0] == '0')) {
    darg++;
    if (FErrorValR("XG", !FValidRotation(rT), rT, 1))
      return tcError;
    gs.rRot = rT;
    if (pin->argc > 2 && ((rT = RFromSz(pin->argv[2])) || pin->argv[2][0] == '0')) {
      darg++;
      if (FErrorValR("XG", !FValidTilt(rT), rT, 2))
        return tcError;
      gs.rTilt = rT;
    }
  }
  gi.nMode = FSwitchF2(gi.nMode == gGlobe) * gGlobe;
  if (szSwitch[2] == '0')
    SwitchF(gs.fSouth);
  is.fHaveInfo |= gs.fAlt;
  return darg;
}

static int NSwXP(CONST char *szSwitch, PARSEIN *pin)
{
  real rT;
  int darg = 0;

  if (pin->argc > 1 && ((rT = RFromSz(pin->argv[1])) || pin->argv[1][0] == '0')) {
    darg++;
    if (FErrorValR("XP", !FValidRotation(rT), rT, 0))
      return tcError;
  } else
    rT = 0.0;
  gs.rRot = rT;
  gi.nMode = FSwitchF2(gi.nMode == gPolar) * gPolar;
  if (szSwitch[2] == '0')
    SwitchF(gs.fSouth);
  else if (szSwitch[2] == 'v')
    SwitchF(gs.fPrintMap);
  is.fHaveInfo |= gs.fAlt;
  return darg;
}

static int NSwXZ(CONST char *szSwitch, PARSEIN *pin)
{
  int i, darg = 0;

  if (pin->argc > 1 && (i = NParseSz(pin->argv[1], pmObject)) != -1) {
    darg++;
    if (!FValidObj(i))
      i = -1;
    gs.objTrack = i;
    if (pin->fAnd)
      return darg;
  }
  gi.nMode = FSwitchF2(gi.nMode == gTelescope) * gTelescope;
  return darg;
}

#ifdef CONSTEL
static int NSwXF(CONST char *szSwitch, PARSEIN *pin)
{
  if (gi.nMode != gHorizon && gi.nMode != gSphere && gi.nMode != gGlobe &&
    gi.nMode != gPolar && gi.nMode != gTelescope)
    gi.nMode = FSwitchF2(gi.nMode == gWorldMap) * gWorldMap;
  SwitchF(gs.fConstel);
  is.fHaveInfo |= gs.fAlt;
  return 0;
}
#endif

static int NSwXk(CONST char *szSwitch, PARSEIN *pin)
{
  int i;

  if (FErrorArgc("Xk", pin->argc, 1))
    return tcError;
  i = NParseSz(pin->argv[1], pmColor);
#ifdef ISG
  if (szSwitch[2] != 'v') {
    if (FErrorValN("Xk", !FValidColorA(i), i, 0))
      return tcError;
    gi.kiPen = i;
  } else
#endif
  {
    if (FErrorValN("Xkv", !FValidColorA(i) && i != kMax, i, 0))
      return tcError;
    gs.kiDeca = i;
  }
  return 1;
}

#ifdef ISG
static int NSwXn(CONST char *szSwitch, PARSEIN *pin)
{
  int i, darg = 0;

  if (pin->argc > 1 && (i = NFromSz(pin->argv[1])))
    darg++;
  else
    i = 10;
  if (FErrorValN("Xn", !FBetween(i, 1, 13), i, 0))
    return tcError;
  gs.nAnim = (pin->fOr || pin->fNot ? i : -i);
  return darg;
}

static int NSwXnp(CONST char *szSwitch, PARSEIN *pin)
{
  SwitchF(gi.fPause);
  return 0;
}

static int NSwXnf(CONST char *szSwitch, PARSEIN *pin)
{
  int i;

  i = NFromSz(pin->argv[1]);
  if (FErrorValN("Xnf", !FBetween(i, 1, 9), i, 0))
    return tcError;
  gi.nDir = i;
  return 1;
}
#endif // ISG
#endif // GRAPH

// The -R restriction family: category presets by suffix, then a
// variadic object list. Quirks preserved exactly, including the stale
// second letter: "-RTu0" reads its ch2 from the position "-Ru0" keeps
// it at, so it acts as "-RTu" -- as it always has.

static int NSwR(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1, ch2, *pch;
  int argcIn = pin->argc, argc = argcIn, i;
  char **argv = pin->argv;
  flag fT;

  ch1 = szSwitch[1];
  ch2 = szSwitch[2];
  if (ch1 == 'A') {
    if (ch2 == '0')
      for (i = 1; i <= cAspect; i++)
        ignorea[ASPT(i)] = fTrue;
    else if (ch2 == '1')
      for (i = 1; i <= cAspect; i++)
        ignorea[ASPT(i)] = fFalse;
    while (argc > 1 && (i = NParseSz(argv[1], pmAspect)))
      if (FErrorValN("RA", !FAspect(i), i, 0))
        return tcError;
      else {
        SwitchF(ignorea[ASPT(i)]);
        argc--; argv++;
      }
    AdjustAspectCount();
    return argcIn - argc;
  }
  if (ch1 == 'O') {
    if (FErrorArgc("RO", argc, 1))
      return tcError;
    i = NParseSz(argv[1], pmObject);
    if (FErrorValN("RO", !FBetween(i, -1, cObj), i, 0))
      return tcError;
    us.objRequire = i;
    return 1;
  }
  fT = (ch1 == 'T');
  if (fT) {
    pch = (char *)ignore2;
    ch1 = szSwitch[2];
  } else
    pch = (char *)ignore;
  if (ch1 == '0')
    for (i = 0; i <= cObj; i++)
      pch[i] = fTrue;
  else if (ch1 == '1')
    for (i = 0; i <= cObj; i++)
      pch[i] = fFalse;
  else if (ch1 == '2')
    for (i = 0; i <= cObj; i++)
      pch[i] = ((char *)(pch == (char *)ignore2 ? ignore : ignore2))[i];
  else if (ch1 == 'C')
    for (i = cuspLo; i <= cuspHi; i++)
      SwitchF(pch[i]);
  else if (ch1 == 'u' && ch2 == '0')      // Must be before Uranian check
    for (i = dwarfLo; i <= dwarfHi; i++)
      SwitchF(pch[i]);
  else if (ch1 == 'u')
    for (i = uranLo; i <= uranHi; i++)
      SwitchF(pch[i]);
  else if (ch1 == '8')
    for (i = moonsLo; i <= moonsHi; i++)
      SwitchF(pch[i]);
  else if (ch1 == 'b')
    for (i = cobLo; i <= cobHi; i++)
      SwitchF(pch[i]);
  else if (ch1 == 'U')
    for (i = starLo; i <= starHi; i++)
      SwitchF(pch[i]);
  else if (argc <= 1 || NParseSz(argv[1], pmObject) < 0)
    for (i = oChi; i <= oEP; i++)
      if (i != oNod)
        SwitchF(pch[i]);
  while (argc > 1 && (i = NParseSz(argv[1], pmObject)) >= 0)
    if (FErrorValN("R", !FItem(i), i, 0))
      return tcError;
    else {
      if (ch1 != 'C' && ch1 != 'u' && ch1 != 'U')
        SwitchF(pch[i]);
      else
        inv(pch[i]);
      argc--; argv++;
    }
  RedoRestrictions();
  return argcIn - argc;
}

// -C, -u, -u0, -u8, -ub: toggle an object category's master flag and
// sync its restriction range when the flag actually changed.

static int NSwCategory(flag *pf, int ilo, int ihi, PARSEIN *pin)
{
  flag j;
  int i;

  j = *pf;
  SwitchF(*pf);
  if (j != *pf)
    for (i = ilo; i <= ihi; i++)
      ignore[i] = j;
  AdjustRestrictions();
  return 0;
}

static int NSwC(CONST char *szSwitch, PARSEIN *pin)
{
  return NSwCategory(&us.fCusp, cuspLo, cuspHi, pin);
}

static int NSwu(CONST char *szSwitch, PARSEIN *pin)
{
  return NSwCategory(&us.fUranian, uranLo, uranHi, pin);
}

static int NSwu0(CONST char *szSwitch, PARSEIN *pin)
{
  return NSwCategory(&us.fDwarf, dwarfLo, dwarfHi, pin);
}

static int NSwu8(CONST char *szSwitch, PARSEIN *pin)
{
  return NSwCategory(&us.fMoons, moonsLo, moonsHi, pin);
}

static int NSwub(CONST char *szSwitch, PARSEIN *pin)
{
  return NSwCategory(&us.fCOB, cobLo, cobHi, pin);
}

static int NSwU(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1 = szSwitch[1], ch2;
  flag j;
  int i;

  if (ch1 == 'x') {
    ch2 = szSwitch[2];
    SwitchF(us.fExoTransit);
    if (ch2 == 'd' || ch2 == 'm' || ch2 == 'y' || ch2 == 'Y') {
      us.fParallel = fTrue;
      us.fInDayMonth = (ch2 != 'd');
      us.fInDayYear = us.fInDayMonth && (ch2 == 'y' || ch2 == 'Y');
      us.nEphemYears = (ch2 == 'Y' ? 5 : 1);
    }
    return 0;
  }
  j = us.fStar;
  if (ch1 == 'i' || ch1 == 'z' || ch1 == 'l' || ch1 == 'n' ||
    ch1 == 'b' || ch1 == 'd' || ch1 == 'v')
    us.nStarSort = (ch1 != 'i' ? ch1 : 0);
  SwitchF(us.fStar);
  if (j != us.fStar)
    for (i = starLo; i <= starHi; i++)
      ignore[i] = j;
  AdjustRestrictions();
  return 0;
}

static int NSwA(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1 = szSwitch[1];
  real rT;
  int i, j;

  if (ch1 == '3') {
    SwitchF(us.fAspect3D);
    return 0;
  } else if (ch1 == 'p') {
    SwitchF(us.fAspectLat);
    return 0;
  } else if (ch1 == 'P') {
    SwitchF(us.fParallel2);
    return 0;
  } else if (ch1 != 'o' && ch1 != 'm' && ch1 != 'd' && ch1 != 'a') {
    if (FErrorArgc("A", pin->argc, 1))
      return tcError;
    i = NParseSz(pin->argv[1], pmAspect);
    if (FErrorValN("A", !FValidAspect(i), i, 0))
      return tcError;
    for (j = us.nAsp + 1; j <= i; j++)
      ignorea[ASPT(j)] = fFalse;
    for (j = i + 1; j <= cAspect; j++)
      ignorea[ASPT(j)] = fTrue;
    us.nAsp = i;
    return 1;
  }
  if (FErrorArgc("A", pin->argc, 2))
    return tcError;
  i = NParseSz(pin->argv[1], ch1 == 'o' || ch1 == 'a' ? pmAspect : pmObject);
  if (FErrorValN("A", i < (int)(ch1 == 'o' || ch1 == 'a') ||
    i > (ch1 == 'o' || ch1 == 'a' ? cAspect : oNorm+1), i, 1))
    return tcError;
  rT = RParseSz(pin->argv[2], 0);
  if (FErrorValR("A", rT < -rDegMax || rT > rDegMax, rT, 2))
    return tcError;
  if (ch1 == 'o')
    rAspOrb[ASPT(i)] = rT;
  else if (ch1 == 'm')
    rgobjset[i].orb = rT;
  else if (ch1 == 'd')
    rgobjset[i].add = rT;
  else
    rAspAngle[ASPT(i)] = rT;
  return 2;
}

// The -b ephemeris-selection family: the digit suffixes are display
// toggles that stand alone; every other spelling also turns ephemeris
// files on, exactly as the retired case fell through to.

static int NSwb(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1 = szSwitch[1];

  if (ch1 == '0') {
    SwitchF(us.fSeconds);
    return 0;
  } else if (ch1 == '1') {
    SwitchF(us.fSecond1K);
    return 0;
  } else if (ch1 == '2') {
    SwitchF(us.fSecondHide);
    return 0;
  } else if (ch1 == 'j')
    us.nSwissEph = FSwitchF(us.nSwissEph == 2) * 2;
  else if (ch1 == 's')
    us.nSwissEph = FSwitchF(us.nSwissEph == 1);
  else if (ch1 == 'p' && !us.fNoPlacalc)
    SwitchF(us.fPlacalcPla);
  else if (ch1 == 'm' && !us.fNoPlacalc)
    SwitchF(us.fMatrixPla);
  else if (ch1 == 'a')
    SwitchF(us.fPlacalcAst);
  else if (ch1 == 'U')
    SwitchF(us.fMatrixStar);
  else if (ch1 == 'J' && !us.fNoNetwork)
    us.nSwissEph = FSwitchF(us.nSwissEph == 3) * 3;
  SwitchF(us.fEphemFiles);
  return 0;
}

static int NSwc(CONST char *szSwitch, PARSEIN *pin)
{
  int i;

  if (szSwitch[1] == '3') {
    if (pin->argc > 1 && ((i = NFromSz(pin->argv[1])) != 0 || FNumCh(pin->argv[1][0]) ||
      pin->argv[1][0] == '~')) {
      if (FErrorValN("c3", !FValidMethod(i), i, 0))
        return tcError;
      if (i > 0)
        us.nHouse3D = i;
      else {
        us.fHouse3D = fFalse;
        return 1;
      }
      SwitchF(us.fHouse3D);
      return 1;
    }
    SwitchF(us.fHouse3D);
    return 0;
  }
#ifdef WIN
  if (pin->argc <= 1 && wi.fSaverCfg)
    return nSwitchStop;
#endif
  if (FErrorArgc("c", pin->argc, 1))
    return tcError;
  i = NParseSz(pin->argv[1], pmSystem);
  if (FErrorValN("c", !FValidSystem(i), i, 0))
    return tcError;
  us.nHouseSystem = i;
  return 1;
}

static int NSws(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1 = szSwitch[1], ch2 = ch1 == chNull ? chNull : szSwitch[2];
  real rT;
  int darg = 0;
#ifdef WSETUP
  int i;
#endif

  if (pin->argc > 1 && (rT = RParseSz(pin->argv[1], pmOffset)) != rLarge) {
    if (FErrorValR("s", !FValidOffset(rT), rT, 0))
      return tcError;
    darg++;
    us.rZodiacOffset = rT;
  }
  if (ch1 == 'r') {
    if (ch2 != '0')
      SwitchF(us.fEquator);
    SwitchF(us.fEquator2);
  } else if (ch1 == 'z')
    us.nDegForm = dfZod;
  else if (ch1 == 'h')
    us.nDegForm = dfHM;
  else if (ch1 == 'd')
    us.nDegForm = df360;
  else if (ch1 == 'n')
    us.nDegForm = dfNak;
#ifdef WSETUP
  else if (ch1 == 'e')    // -setup switch for Windows
    i = FCreateProgramGroup(fFalse) && FCreateDesktopIcon() &&
      FRegisterExtensions();
#endif
  else
    SwitchF(us.fSidereal);
  return darg;
}

static int NSwh(CONST char *szSwitch, PARSEIN *pin)
{
  int i, darg = 0;

  if (pin->argc > 1 && (i = NParseSz(pin->argv[1], pmObject)) >= 0)
    darg++;
  else
    i = FSwitchF(us.objCenter != 0);
  if (FErrorValN("h", !FValidCenter(i), i, 0))
    return tcError;
  SetCentric(i);
  return darg;
}

static int NSwp(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1 = szSwitch[1], ch2 = ch1 == chNull ? chNull : szSwitch[2];
  real rT;
  int i;

  us.nProgress = (ch1 == '0') + 2*(ch1 == '1');
  if (us.nProgress)
    ch1 = ch2;
  if (pin->fAnd && ch1 != 'c') {
    us.fProgress = fFalse;
    return 0;
  } else if (ch1 == 'd') {
    if (FErrorArgc("pd", pin->argc, 1))
      return tcError;
    i = (ChCap(pin->argv[1][0]) == 'X');
    rT = RFromSz(pin->argv[1] + i);
    if (i != 0 && rT != 0.0)
      rT = rDayInYear / rT;
    if (FErrorValR("pd", rT == 0.0, rT, 0))
      return tcError;
    us.rProgDay = rT;
    return 1;
  } else if (ch1 == 'C') {
    if (FErrorArgc("pC", pin->argc, 1))
      return tcError;
    rT = RFromSz(pin->argv[1]);
    if (FErrorValR("pC", rT == 0.0, rT, 0))
      return tcError;
    us.rProgCusp = rT;
    return 1;
  } else if (ch1 == 'O') {
    if (FErrorArgc("pO", pin->argc, 1))
      return tcError;
    i = NParseSz(pin->argv[1], pmObject);
    if (FErrorValN("pO", !FValidProgArc(i), i, 0))
      return tcError;
    us.objProgArc = i;
    return 1;
  } else if (ch1 == 'c') {
    SwitchF(us.fProgRAMC);
    return 0;
  }
  SwitchF(us.fProgress);
#ifdef TIMEFUNC
  if (ch1 == 'n') {
    GetTimeNow(&MonT, &DayT, &YeaT, &TimT, ciDefa.dst, ciDefa.zon);
    is.JDp = MdytszToJulian(MonT, DayT, YeaT, TimT,
      ciDefa.dst, ciDefa.zon);
    return 0;
  }
#endif
  i = 3 + (ch1 == 't');
  if (FErrorArgc("p", pin->argc, i))
    return tcError;
  MonT = NParseSz(pin->argv[1], pmMon);
  DayT = NParseSz(pin->argv[2], pmDay);
  YeaT = NParseSz(pin->argv[3], pmYea);
  TimT = ch1 == 't' ? RParseSz(pin->argv[4], pmTim) : 0.0;
  if (FErrorValN("p", !FValidMon(MonT), MonT, 1))
    return tcError;
  else if (FErrorValN("p", !FValidDay(DayT, MonT, YeaT), DayT, 2))
    return tcError;
  else if (FErrorValN("p", !FValidYea(YeaT), YeaT, 3))
    return tcError;
  else if (ch1 == 't' && FErrorValR("p", !FValidTim(TimT), TimT, 4))
    return tcError;
  is.JDp = MdytszToJulian(MonT, DayT, YeaT, TimT, ciDefa.dst, ciDefa.zon);
  return i;
}

static int NSwx(CONST char *szSwitch, PARSEIN *pin)
{
  real rT;
  int i;

  i = ChCap(pin->argv[1][0]) == 'D';
  rT = RFromSz(pin->argv[1] + i);
  if (i != 0 && rT != 0.0)
    rT = rDegMax / rT;
  if (FErrorValR("x", !FValidHarmonic(rT), rT, 0))
    return tcError;
  us.rHarmonic = rT;
  return 1;
}

static int NSwOnAsc(int argc, char **argv, flag fAnd, flag fZero,
  flag fSecond)
{
  int i, darg = 0;

  if (argc > 1 && (i = NParseSz(argv[1], pmObject)) >= 0)
    darg++;
  else
    i = oSun;
  if (FErrorValN(fSecond ? "2" : "1", !FItem(i), i, 0))
    return tcError;
  us.fSolarWhole = (fZero && !fAnd);
  us.objOnAsc = fAnd ? 0 : (fSecond ? -(i+1) : i+1);
  return darg;
}

static int NSwOne(CONST char *szSwitch, PARSEIN *pin)
{
  return NSwOnAsc(pin->argc, pin->argv, pin->fAnd, fFalse, fFalse);
}

static int NSwOne0(CONST char *szSwitch, PARSEIN *pin)
{
  return NSwOnAsc(pin->argc, pin->argv, pin->fAnd, fTrue, fFalse);
}

static int NSwTwo(CONST char *szSwitch, PARSEIN *pin)
{
  return NSwOnAsc(pin->argc, pin->argv, pin->fAnd, fFalse, fTrue);
}

static int NSwTwo0(CONST char *szSwitch, PARSEIN *pin)
{
  return NSwOnAsc(pin->argc, pin->argv, pin->fAnd, fTrue, fTrue);
}

static int NSwFour(CONST char *szSwitch, PARSEIN *pin)
{
  int i, darg = 0;

  if (pin->argc > 1 && (i = NFromSz(pin->argv[1])) >= 0)
    darg++;
  else
    i = 1;
  if (FErrorValN("4", !FValidDwad(i), i, 0))
    return tcError;
  us.nDwad = pin->fAnd ? 0 : i;
  return darg;
}

static int NSwF(CONST char *szSwitch, PARSEIN *pin)
{
  int i, j, k;

  if (FErrorArgc("F", pin->argc, !pin->fAnd ? 3 : 1))
    return tcError;
  i = NParseSz(pin->argv[1], pmObject);
  if (FErrorValN("F", !FItem(i), i, 1))
    return tcError;
  if (pin->fAnd) {
    force[i] = 0.0;
    return 1;
  }
  if (szSwitch[1] != 'm') {
    force[i] = ZD(NParseSz(pin->argv[2], pmSign), RFromSz(pin->argv[3]));
    if (FErrorValR("F", force[i] < 0.0 || force[i] >= rDegMax,
      force[i], 0))
      return tcError;
    force[i] = ForcePos(force[i]);
  } else {
    j = NParseSz(pin->argv[2], pmObject);
    if (FErrorValN("Fm", !FItem(j), j, 2))
      return tcError;
    k = NParseSz(pin->argv[3], pmObject);
    if (FErrorValN("Fm", !FItem(k), k, 3))
      return tcError;
    force[i] = ForceMid(j, k);
  }
  AdjustRestrictions();
  return 3;
}

// The chart-type letters. Prefix rows, since nearly every one packs
// options into its spelling; the handlers that used to walk argv[0]
// with ich walk szSwitch with a local cursor instead.

static int NSwv(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1 = szSwitch[1];
  int i;

  if (ch1 == '3') {
    SwitchF(us.fListDecan);
    if (pin->argc > 1 && ((i = NFromSz(pin->argv[1])) > 0 || FNumCh(pin->argv[1][0]) ||
      pin->argv[1][0] == '~')) {
      if (FErrorValN("v3", !FValidDecan(i), i, 0))
        return tcError;
      if (i <= 0)
        us.fListDecan = fFalse;
      else
        us.nDecanType = i;
      return 1;
    }
    return 0;
  }
  SwitchF(us.fListing);
  return 0;
}

static int NSww(CONST char *szSwitch, PARSEIN *pin)
{
  int i, darg = 0;

  if (szSwitch[1] == '0')
    SwitchF(us.fWheelReverse);
  if (pin->argc > 1 && ((i = NFromSz(pin->argv[1])) > 0 || FNumCh(pin->argv[1][0]))) {
    darg++;
    if (FErrorValN("w", !FValidWheel(i), i, 0))
      return tcError;
    us.nWheelRows = i;
  }
  SwitchF(us.fWheel);
  return darg;
}

static int NSwg(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1 = szSwitch[1], ch2 = ch1 == chNull ? chNull : szSwitch[2];
  int i, darg = 0;

  if (ch1 == '0' || ch2 == '0')
    SwitchF(us.fGridConfig);
  else if (ch1 == 'm' || ch2 == 'm')
    SwitchF(us.fGridMidpoint);
  if (ch1 == 'p')
    SwitchF(us.fParallel);
  else if (ch1 == 'd')
    SwitchF(us.fDistance);
  else if (ch1 == 'a')
    us.nAppSep = FSwitchF(us.nAppSep);
  else if (ch1 == 'x')
    us.nAppSep = FSwitchF(us.nAppSep) * 2;
  else if (ch1 == 's') {
    if (FErrorArgc("gs", pin->argc, 1))
      return tcError;
    i = NFromSz(pin->argv[1]);
    if (FErrorValN("gs", !FValidAppSep(i), i, 0))
      return tcError;
    us.nAppSep = i;
    darg++;
  }
#ifdef X11
  else if (ch1 == 'e') {
    if (FErrorArgc("geometry", pin->argc, 1))
      return tcError;
    gs.xWin = NFromSz(pin->argv[1]);
    if (pin->argc > 2 && (gs.yWin = NFromSz(pin->argv[2])))
      darg++;
    else
      gs.yWin = gs.xWin;
    if (FErrorValN("geometry", !FValidGraphX(gs.xWin), gs.xWin, 1))
      return tcError;
    if (FErrorValN("geometry", !FValidGraphY(gs.yWin), gs.yWin, 2))
      return tcError;
    return darg + 1;
  }
#endif
  SwitchF(us.fGrid);
  return darg;
}

static int NSwa(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1 = szSwitch[1], ch2 = ch1 == chNull ? chNull : szSwitch[2];
  int i, darg = 0;

  SwitchF(us.fAspList);
  if (ch1 == '0') {
    SwitchF(us.fAspSummary);
    ch1 = ch2;
  }
  if (ch1 == 'p') {
    SwitchF(us.fParallel);
    ch1 = ch2;
  } else if (ch1 == 'd') {
    SwitchF(us.fDistance);
    ch1 = ch2;
  } else if (ch1 == 'a') {
    us.nAppSep = FSwitchF(us.nAppSep);
    ch1 = ch2;
  } else if (ch1 == 'x') {
    us.nAppSep = FSwitchF(us.nAppSep) * 2;
    ch1 = ch2;
  } else if (ch1 == 's') {
    if (FErrorArgc("as", pin->argc, 1))
      return tcError;
    i = NFromSz(pin->argv[1]);
    if (FErrorValN("as", !FValidAppSep(i), i, 0))
      return tcError;
    us.nAppSep = i;
    darg++;
  }
  switch (ch1) {
  case 'j': us.nAspectSort = asj; break;
  case 'o': us.nAspectSort = aso; break;
  case 'n': us.nAspectSort = asn; break;
  case 'O': us.nAspectSort = asO; break;
  case 'P': us.nAspectSort = asP; break;
  case 'A': us.nAspectSort = asA; break;
  case 'C': us.nAspectSort = asC; break;
  case 'D': us.nAspectSort = asD; break;
  case 'm': us.nAspectSort = asM; break;
  }
  return darg;
}

static int NSwm(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1 = szSwitch[1], ch2 = ch1 == chNull ? chNull : szSwitch[2];

  if (ch1 == '0' || ch2 == '0')
    SwitchF(us.fMidSummary);
  if (ch1 == 'a')
    SwitchF(us.fMidAspect);
  SwitchF(us.fMidpoint);
  return 0;
}

static int NSwZ(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1 = szSwitch[1], ch2 = ch1 == chNull ? chNull : szSwitch[2];
  int i, darg = 0;

  if (ch1 == 'd') {
    if (ch2 == 'm' || ch2 == 'y' || ch2 == 'Y') {
      if (ch2 == 'y')
        us.nEphemYears = 1;
      else if (ch2 == 'Y') {
        if (FErrorArgc("ZdY", pin->argc, 1))
          return tcError;
        i = NFromSz(pin->argv[1]);
        if (FErrorValN("ZdY", i < 1, i, 0))
          return tcError;
        us.nEphemYears = i;
        darg++;
      }
      SwitchF(us.fInDayMonth);
      us.fInDayYear = us.fInDayMonth && (ch2 != 'm');
    }
    SwitchF(us.fHorizonSearch);
    return darg;
  }
  if (ch1 == '0')
    SwitchF(us.fPrimeVert);
  SwitchF(us.fHorizon);
  return 0;
}

static int NSwL(CONST char *szSwitch, PARSEIN *pin)
{
  int i, darg = 0;

  if (szSwitch[1] == '0')
    SwitchF(us.fLatitudeCross);
  if (pin->argc > 1 && (i = NFromSz(pin->argv[1]))) {
    darg++;
    if (FErrorValN("L", !FValidAstrograph(i), i, 1))
      return tcError;
    us.nAstroGraphStep = i;
    if (pin->argc > 2 && ((i = NFromSz(pin->argv[2])) != 0 || FNumCh(pin->argv[2][0]))) {
      darg++;
      if (FErrorValN("L0", !FValidAstrograph2(i), i, 2))
        return tcError;
      us.nAstroGraphDist = i;
    }
  }
  SwitchF(us.fAstroGraph);
  return darg;
}

static int NSwd(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1 = szSwitch[1], ch2 = ch1 == chNull ? chNull : szSwitch[2];
  int i, j, l = 1, darg = 0;

  if (ch1 == 'p') {
    us.nProgress = (ch2 == '0') + 2*(ch2 == '1');
    if (us.nProgress)
      ch2 = szSwitch[++l + 1];
    i = (ch2 == 'y') + 2*(ch2 == 'Y');
#ifdef TIMEFUNC
    j = i < 2 && (szSwitch[l+i+1] == 'n');
#else
    j = fFalse;
#endif
    if (!j && FErrorArgc("dp", pin->argc, 2-(i&1)))
      return tcError;
    is.fProgress = us.fInDayMonth = fTrue;
    DstT = ciDefa.dst; ZonT = ciDefa.zon;
    LonT = ciDefa.lon; LatT = ciDefa.lat;
#ifdef TIMEFUNC
    if (j)
      GetTimeNow(&MonT, &DayT, &YeaT, &TimT, DstT, ZonT);
#endif
    if (i) {
      us.fInDayYear = fTrue;
      if (!j)
        YeaT = NParseSz(pin->argv[1], pmYea);
      us.nEphemYears = i == 2 ? NFromSz(pin->argv[2]) : 1;
    } else {
      if (!j) {
        MonT = NParseSz(pin->argv[1], pmMon);
        YeaT = NParseSz(pin->argv[2], pmYea);
        if (FErrorValN("dp", !FValidMon(MonT), MonT, 1))
          return tcError;
      }
    }
    if (FErrorValN("dp", !FValidYea(YeaT), YeaT, i ? 1 : 2))
      return tcError;
    if (!j)
      darg = 2-(i&1);
    SwitchF(us.fInDay);
    return darg;
  } else if (ch1 == 'm' || ch1 == 'y' || ch1 == 'Y') {
    is.fProgress = fFalse;
    if (ch1 == 'y')
      us.nEphemYears = 1;
    else if (ch1 == 'Y') {
      if (FErrorArgc("dY", pin->argc, 1))
        return tcError;
      i = NFromSz(pin->argv[1]);
      if (FErrorValN("dY", i < 1, i, 1))
        return tcError;
      us.nEphemYears = i;
      darg++;
    }
    SwitchF(us.fInDayMonth);
    us.fInDayYear = us.fInDayMonth && (ch1 != 'm');
  }
#ifdef X11
  else if (ch1 == 'i') {    // -display switch for X
    if (FErrorArgc("display", pin->argc, 1))
      return tcError;
    FCloneSz(pin->argv[1], &gs.szDisplay);
    return 1;
  }
#endif
  else if (pin->argc > 1 && (i = NFromSz(pin->argv[1]))) {
    if (FErrorValN("d", !FValidDivision(i), i, 0))
      return tcError;
    us.nDivision = i;
    darg++;
  }
  SwitchF(us.fInDay);
  return darg;
}

static int NSwE(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1 = szSwitch[1], ch2 = ch1 == chNull ? chNull : szSwitch[2];
  int i, j, darg = 0;

  j = ch1 == '0' || ch2 == '0';
  if (FErrorArgc("E", pin->argc, (ch1 == 'Y') + j))
    return tcError;
  SwitchF(us.fEphemeris);
  if (ch1 == 'y')
    us.nEphemYears = us.fEphemeris ? 1 : 0;
  else if (ch1 == 'Y') {
    i = NFromSz(pin->argv[1]);
    if (FErrorValN("EY", i < 1, i, 1))
      return tcError;
    us.nEphemYears = i;
    darg++;
  }
  if (j) {
    ch1 = pin->argv[darg+1][0];
    if (ch1) {
      us.nEphemRate = (ch1 == 'n' ? -2 : (ch1 == 'h' ? -1 :
        (ch1 == 'm' ? 1 : (ch1 == 'y' ? 2 : 0))));
      i = NFromSz(&pin->argv[darg+1][1]);
      us.nEphemFactor = Max(i, 1);
    }
    darg++;
  }
  return darg;
}

static int NSwe(CONST char *szSwitch, PARSEIN *pin)
{
  SwitchF(us.fListing); SwitchF(us.fWheel);
  SwitchF(us.fGrid); SwitchF(us.fAspList); SwitchF(us.fMidpoint);
  SwitchF(us.fHorizon); SwitchF(us.fOrbit); SwitchF(us.fSector);
  SwitchF(us.fInfluence); SwitchF(us.fEsoteric); SwitchF(us.fAstroGraph);
  SwitchF(us.fCalendar); SwitchF(us.fHorizonSearch);
  SwitchF(us.fInDay); SwitchF(us.fInDayInf); SwitchF(us.fInDayGra);
  SwitchF(us.fEphemeris); SwitchF(us.fArabic);
  SwitchF(us.fMoonChart); SwitchF(us.fExoTransit);
  return 0;
}

static int NSwt(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1 = szSwitch[1];
  int i, l = 1;

  SwitchF(us.fTransit);
  ZonT = ciDefa.zon; DstT = ciDefa.dst;
  LonT = ciDefa.lon; LatT = ciDefa.lat;
  if (ch1 == 'p') {
    us.nProgress = (szSwitch[2] == '0') + 2*(szSwitch[2] == '1');
    if (us.nProgress)
      l++;
    is.fProgress = fTrue;
    ch1 = szSwitch[++l];
  } else
    is.fProgress = fFalse;
  if (ch1 == 'r') {
    SwitchF(is.fReturn);
    ch1 = szSwitch[++l];
  }
  i = (ch1 == 'y') + 2*(ch1 == 'Y') - (ch1 == 'd');
  if (i != 0)
    ch1 = szSwitch[++l];
  us.fInDayMonth = (i >= 0);
  us.fInDayYear = (i >= 1);
#ifdef TIMEFUNC
  if (ch1 == 'n') {
    GetTimeNow(&MonT, &DayT, &YeaT, &TimT, DstT, ZonT);
    if (i >= 2) {
      if (FErrorArgc("tYn", pin->argc, 1))
        return tcError;
      us.nEphemYears = NFromSz(pin->argv[1]);
      return 1;
    }
    return 0;
  }
#endif
  if (FErrorArgc("t", pin->argc, 2 - (i == 1) + (i < 0)))
    return tcError;
  YeaT = NParseSz(pin->argv[2 - (i > 0) + (i < 0)], pmYea);
  if (FErrorValN("t", !FValidYea(YeaT), YeaT, 2 - (i > 0) + (i < 0)))
    return tcError;
  if (i <= 0) {
    MonT = NParseSz(pin->argv[1], pmMon);
    if (FErrorValN("t", !FValidMon(MonT), MonT, 1))
      return tcError;
  }
  if (i < 0) {
    DayT = NParseSz(pin->argv[2], pmDay);
    if (FErrorValN("td", !FValidDay(DayT, MonT, YeaT), DayT, 2))
      return tcError;
  }
  if (i > 1)
    us.nEphemYears = NFromSz(pin->argv[2]);
  return 2 - (i == 1) + (i < 0);
}

static int NSwT(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1 = szSwitch[1];
  int i, l = 1;

  SwitchF(us.fTransitInf);
  ZonT = ciDefa.zon; DstT = ciDefa.dst;
  LonT = ciDefa.lon; LatT = ciDefa.lat;
  i = (ch1 == 't');
  if (i > 0)
    ch1 = szSwitch[++l];
  if (ch1 == 'p') {
    is.fProgress = fTrue;
    ch1 = szSwitch[++l];
  } else
    is.fProgress = fFalse;
  if (ch1 == 'r') {
    SwitchF(is.fReturn);
    ch1 = szSwitch[++l];
  }
#ifdef TIMEFUNC
  if (ch1 == 'n') {
    GetTimeNow(&MonT, &DayT, &YeaT, &TimT, DstT, ZonT);
    return 0;
  }
#endif
  if (FErrorArgc("T", pin->argc, 3 + i))
    return tcError;
  MonT = NParseSz(pin->argv[1], pmMon);
  DayT = NParseSz(pin->argv[2], pmDay);
  YeaT = NParseSz(pin->argv[3], pmYea);
  TimT = i > 0 ? RParseSz(pin->argv[4], pmTim) : 0.0;
  if (FErrorValN("T", !FValidMon(MonT), MonT, 1))
    return tcError;
  if (FErrorValN("T", !FValidDay(DayT, MonT, YeaT), DayT, 2))
    return tcError;
  if (FErrorValN("T", !FValidYea(YeaT), YeaT, 3))
    return tcError;
  if (i > 0 && FErrorValR("T", !FValidTim(TimT), TimT, 4))
    return tcError;
  return 3 + i;
}

static int NSwB(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1 = szSwitch[1], ch2 = ch1 == chNull ? chNull : szSwitch[2];
  int l = 1;

  SwitchF(us.fInDayGra);
  if (ch1 == 'p') {
    is.fProgress = fTrue;
    ch1 = szSwitch[++l];
  } else
    is.fProgress = fFalse;
  if (ch1 == 'm' || ch1 == 'y' || ch1 == 'Y') {
    if (ch1 == 'y')
      us.nEphemYears = 1;
    else if (ch1 == 'Y')
      us.nEphemYears = 5;
    SwitchF(us.fInDayMonth);
    us.fInDayYear = us.fInDayMonth && (ch1 != 'm');
  }
  if (ch1 == '0' || ch2 == '0')
    SwitchF(us.fGraphAll);
  return 0;
}

static int NSwV(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1 = szSwitch[1];
  int i, j, l = 1;

  SwitchF(us.fTransitGra);
  ZonT = ciDefa.zon; DstT = ciDefa.dst;
  LonT = ciDefa.lon; LatT = ciDefa.lat;
  if (ch1 == 'p') {
    is.fProgress = fTrue;
    ch1 = szSwitch[++l];
  } else
    is.fProgress = fFalse;
  if (ch1 == 'r') {
    SwitchF(is.fReturn);
    ch1 = szSwitch[++l];
  }
  if (i = (ch1 == 'd') + 2*(ch1 == 'm') + 3*(ch1 == 'y') + 4*(ch1 == 'Y'))
    ch1 = szSwitch[++l];
  if (i < 1)
    i = 2;
  if (ch1 == '0') {
    SwitchF(us.fGraphAll);
    ch1 = szSwitch[++l];
  }
  if (i >= 2) {
    if (i == 3)
      us.nEphemYears = 1;
    else if (i == 4)
      us.nEphemYears = 5;
    SwitchF(us.fInDayMonth);
    if (i >= 3)
      us.fInDayYear = us.fInDayMonth;
  }
#ifdef TIMEFUNC
  if (ch1 == 'n') {
    GetTimeNow(&MonT, &DayT, &YeaT, &TimT, DstT, ZonT);
    if (i >= 3)
      us.fInDayYear = us.fInDayMonth;
    return 0;
  }
#endif
  j = i < 2 ? 3 : (i == 2 ? 2 : 1);
  if (FErrorArgc("V", pin->argc, j))
    return tcError;
  if (i == 1) {
    MonT = NParseSz(pin->argv[1], pmMon);
    DayT = NParseSz(pin->argv[2], pmDay);
    YeaT = NParseSz(pin->argv[3], pmYea);
  } else if (i == 2) {
    MonT = NParseSz(pin->argv[1], pmMon);
    YeaT = NParseSz(pin->argv[2], pmYea);
    DayT = 1;
  } else {
    YeaT = NParseSz(pin->argv[1], pmYea);
    MonT = DayT = 1;
  }
  if (FErrorValN("V", !FValidMon(MonT), MonT, 1))
    return tcError;
  if (FErrorValN("V", !FValidYea(YeaT), YeaT, j))
    return tcError;
  if (FErrorValN("V", !FValidDay(DayT, MonT, YeaT), DayT, 2))
    return tcError;
  return j;
}

#ifdef ARABIC
static int NSwP(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1 = szSwitch[1], ch2 = ch1 == chNull ? chNull : szSwitch[2];
  int i, darg = 0;

  if (pin->argc > 1 && (i = NFromSz(pin->argv[1]))) {
    darg++;
    if (FErrorValN("P", !FValidPart(i), i, 0))
      return tcError;
    us.nArabicParts = i;
  }
  if (ch1 == 'i' || ch1 == 'z' || ch1 == 'n' || ch1 == 'f') {
    us.nArabicSort = (ch1 != 'i' ? ch1 : 0);
    ch1 = ch2;
  }
  SwitchF(us.fArabic);
  if (ch1 == '0')
    SwitchF(us.fArabicFlip);
  return darg;
}
#endif

static int NSwN(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1 = szSwitch[1];
  int i, darg = 0;

  if (pin->argc > 1 && (i = NFromSz(pin->argv[1]))) {
    darg++;
    us.nAtlasList = i;
  }
  if (ch1 == 'z')
    SwitchF(us.fZoneChange);
  else if (ch1 == 'l')
    SwitchF(us.fAtlasNear);
  else
    SwitchF(us.fAtlasLook);
  return darg;
}

static int NSwI(CONST char *szSwitch, PARSEIN *pin)
{
  int i, darg = 0;

  if (pin->argc > 1 && (i = NFromSz(pin->argv[1]))) {
    darg++;
    if (FErrorValN("I", !FValidScreen(i), i, 0))
      return tcError;
    us.nScreenWidth = i;
  }
  if (szSwitch[1] != '0')
    SwitchF(us.fInterpret);
  else
    SwitchF(us.fSabian);
  return darg;
}

// The last of the main parser's cases: help, macros, chart info, I/O,
// day arithmetic, relationship charts, the chart list, and the
// AstroExpression hooks.

static int NSwH(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1 = szSwitch[1];

  if (ch1 == 'c')
    SwitchF(us.fCredit);
  else if (ch1 == 'Y')
    SwitchF(us.fSwitchRare);
#ifdef ISG
  else if (ch1 == 'X')
    SwitchF(us.fKeyGraph);
#endif
  else if (ch1 == 'C')
    SwitchF(us.fSign);
  else if (ch1 == 'O')
    SwitchF(us.fObject);
  else if (ch1 == 'A')
    SwitchF(us.fAspect);
  else if (ch1 == 'F')
    SwitchF(us.fConstel);
  else if (ch1 == 'S')
    SwitchF(us.fOrbitData);
  else if (ch1 == '7')
    SwitchF(us.fRay);
  else if (ch1 == 'I')
    SwitchF(us.fMeaning);
  else if (ch1 == 'e') {
    SwitchF(us.fCredit); SwitchF(us.fSwitch); SwitchF(us.fSwitchRare);
    SwitchF(us.fKeyGraph); SwitchF(us.fSign); SwitchF(us.fObject);
    SwitchF(us.fAspect); SwitchF(us.fConstel); SwitchF(us.fOrbitData);
    SwitchF(us.fRay); SwitchF(us.fMeaning);
  } else
    SwitchF(us.fSwitch);
  return 0;
}

static int NSwM(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1 = szSwitch[1], ch2 = ch1 == chNull ? chNull : szSwitch[2];
  int i, j;

  if (FBetween(ch1, '1', '0' + cRing)) {
    i = (ch1 - '0') + (ch2 == '0');
    if (FErrorArgc("M", pin->argc, i))
      return tcError;
    for (j = 1; j <= i; j++)
      FCloneSz(pin->argv[j], &szWheel[(ch2 == '0' && j >= i) ? 0 : j]);
    return i;
  }
  i = (ch1 == '0');
  if (FErrorArgc("M", pin->argc, 1+i))
    return tcError;
  j = NFromSz(pin->argv[1]);
  if (FErrorValN("M", !FValidMacro(j), j, 1))
    return tcError;
  if (i) {
    if (FEnsureMacro(j+1))
      FCloneSz(pin->argv[2], &is.rgszMacro[j]);
  } else if (j < is.cszMacro)
    FProcessCommandLine(is.rgszMacro[j]);
  return 1+i;
}

// The chart-info slot stores shared by -n, -q and -i: a trailing letter
// or digit files the chart away and puts the previous one back.

static void SwSlotStore(char ch, CI *pci)
{
  if (FBetween(ch, '1', '0' + cRing)) {
    *rgpci[ch - '0'] = ciCore;
    ciCore = *pci;
  } else if (ch == 'D') {
    ciDefa = ciCore;
    ciCore = *pci;
  } else if (ch == 't') {
    ciTran = ciCore;
    ciCore = *pci;
  } else if (ch == 's') {
    ciSave = ciCore;
    ciCore = *pci;
  } else if (ch == 'g') {
    ciGreg = ciCore;
    ciCore = *pci;
  } else if (ch == 'l')
    FAppendCIList(&ciCore);
}

#ifdef TIMEFUNC
static int NSwn(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1 = szSwitch[1], ch2 = ch1 == chNull ? chNull : szSwitch[2];
  CI ci;

  ci = ciCore;
  FInputData(szNowCore);
  if (ch1 == 'd')
    TT = 0.0;
  else if (ch1 == 'm') {
    DD = 1; TT = 0.0;
  } else if (ch1 == 'y') {
    MM = DD = 1; TT = 0.0;
  } else
    ch2 = ch1;
  if (FBetween(ch2, '1', '0' + cRing)) {
    *rgpci[ch2 - '0'] = ciCore;
    ciCore = ci;
  }
  return 0;
}
#endif

static int NSwz(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1 = szSwitch[1];
  real rT;
  int i;

  if (ch1 == '0') {
    if (pin->argc <= 1 || RParseSz(pin->argv[1], pmDst) == rLarge) {
      i = (ciDefa.dst != 0.0);
      SwitchF(i);
      SS = ciDefa.dst = (i ? 1.0 : 0.0);
      return 0;
    }
    rT = RParseSz(pin->argv[1], pmDst);
    if (FErrorValR("z0", !FValidDst(rT), rT, 0))
      return tcError;
    SS = ciDefa.dst = rT;
    return 1;
  } else if (ch1 == 'l') {
    if (FErrorArgc("zl", pin->argc, 2))
      return tcError;
    rT = RParseSz(pin->argv[1], pmLon);
    if (FErrorValR("zl", !FValidLon(rT), rT, 1))
      return tcError;
    OO = ciDefa.lon = rT;
    rT = RParseSz(pin->argv[2], pmLat);
    if (FErrorValR("zl", !FValidLat(rT), rT, 2))
      return tcError;
    AA = ciDefa.lat = rT;
    return 2;
  } else if (ch1 == 'v') {
    if (FErrorArgc("zv", pin->argc, 1))
      return tcError;
    us.elvDef = RParseSz(pin->argv[1], pmElv);
    return 1;
  } else if (ch1 == 'f') {
    if (FErrorArgc("zf", pin->argc, 1))
      return tcError;
    us.tmpDef = RParseSz(pin->argv[1], pmTmp);
    return 1;
  } else if (ch1 == 'j') {
    if (FErrorArgc("zj", pin->argc, 2))
      return tcError;
    ciDefa.nam = SzClone(pin->argv[1]);
    ciDefa.loc = SzClone(pin->argv[2]);
    return 2;
  } else if (ch1 == 't') {
    if (FErrorArgc("zt", pin->argc, 1))
      return tcError;
    rT = RParseSz(pin->argv[1], pmTim);
    if (FErrorValR("zt", !FValidTim(rT), rT, 0))
      return tcError;
    TT = rT;
    return 1;
  } else if (ch1 == 'd') {
    if (FErrorArgc("zd", pin->argc, 1))
      return tcError;
    i = NParseSz(pin->argv[1], pmDay);
    if (FErrorValN("zd", !FValidDay(i, MM, YY), i, 0))
      return tcError;
    DD = i;
    return 1;
  } else if (ch1 == 'm') {
    if (FErrorArgc("zm", pin->argc, 1))
      return tcError;
    i = NParseSz(pin->argv[1], pmMon);
    if (FErrorValN("zm", !FValidMon(i), i, 0))
      return tcError;
    MM = i;
    return 1;
  } else if (ch1 == 'y') {
    if (FErrorArgc("zy", pin->argc, 1))
      return tcError;
    i = NParseSz(pin->argv[1], pmYea);
    if (FErrorValN("zy", !FValidYea(i), i, 0))
      return tcError;
    YY = i;
    return 1;
  } else if (ch1 == 'i') {
    if (FErrorArgc("zi", pin->argc, 2))
      return tcError;
    ciCore.nam = SzClone(pin->argv[1]);
    ciCore.loc = SzClone(pin->argv[2]);
    return 2;
  }
#ifdef ATLAS
  else if (ch1 == 'L') {
    if (FErrorArgc("zL", pin->argc, 1))
      return tcError;
    if (!DisplayAtlasLookup(pin->argv[1], 0, &i))
      PrintWarning("City doesn't match anything in atlas.");
    else {
      ciDefa.lon = OO; ciDefa.lat = AA;
    }
    return 1;
  } else if (ch1 == 'N') {
    if (FErrorArgc("zN", pin->argc, 1))
      return tcError;
    if (!DisplayAtlasLookup(pin->argv[1], 0, &i))
      PrintWarning("City doesn't match anything in atlas.");
    else if (!DisplayTimezoneChanges(is.rgae[i].izn, 0, &ciCore))
      PrintWarning("Couldn't get time zone data!");
    else {
      ciDefa.dst = SS; ciDefa.zon = ZZ;
      ciDefa.lon = OO; ciDefa.lat = AA;
      is.fDst = (SS > 0.0);
    }
    return 1;
  }
#endif
  else if (ch1 == 'Z') {
    if (FErrorArgc("zZ", pin->argc, 2))
      return tcError;
    rT = RParseSz(pin->argv[1], pmZon);
    if (FErrorValR("z", !FValidZon(rT), rT, 1))
      return tcError;
    AdjustTimeZone(&ciCore, rT, RParseSz(pin->argv[2], pmDst));
    return 2;
  }
  if (pin->argc <= 1 || RParseSz(pin->argv[1], pmZon) == rLarge) {
    ZZ -= 1.0;
    return 0;
  }
  rT = RParseSz(pin->argv[1], pmZon);
  if (FErrorValR("z", !FValidZon(rT), rT, 0))
    return tcError;
  ZZ = ciDefa.zon = rT;
  return 1;
}

static int NSwq(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1 = szSwitch[1], ch2 = ch1 == chNull ? chNull : szSwitch[2];
  CI ci;
  int i, j;

  i = (ch1 == 'y' || ch1 == 'j' || ch1 == 'L') + 2*(ch1 == 'm') +
    3*(ch1 == 'd') + 7*(ch1 == 'a') + 8*(ch1 == 'b') + 10*(ch1 == 'c');
  if (i <= 0) {
    i = 4;
    ch2 = ch1;
  }
  if (FErrorArgc("q", pin->argc, i))
    return tcError;
  is.fHaveInfo = fTrue;
  ci = ciCore;
  if (ch1 == 'j') {
    is.JD = RFromSz(pin->argv[1]) + rRound;
    TT = RFract(is.JD);
    JulianToMdy(is.JD - TT, &MM, &DD, &YY);
    TT *= 24.0;
    SS = ZZ = 0.0; OO = ciDefa.lon; AA = ciDefa.lat;
  } else if (ch1 == 'L') {
    j = NFromSz(pin->argv[1]);
    if (FErrorValN("qL", !FValidList(j), j, 0))
      return tcError;
    ciCore = is.rgci[j];
    is.iciCur = j;
  } else {
    MM = i > 1 ? NParseSz(pin->argv[1], pmMon) : 1;
    DD = i > 2 ? NParseSz(pin->argv[2], pmDay) : 1;
    YY = NParseSz(pin->argv[3 - (i < 3) - (i < 2)], pmYea);
    TT = i > 3 ? RParseSz(pin->argv[4], pmTim) : (i < 3 ? 0.0 : 12.0);
    SS = i > 7 ? RParseSz(pin->argv[5], pmDst) : (i > 6 ? 0.0 : ciDefa.dst);
    ZZ = i > 6 ? RParseSz(pin->argv[5 + (i > 7)], pmZon) : ciDefa.zon;
    OO = i > 6 ? RParseSz(pin->argv[6 + (i > 7)], pmLon) : ciDefa.lon;
    AA = i > 6 ? RParseSz(pin->argv[7 + (i > 7)], pmLat) : ciDefa.lat;
    if (FErrorValN("q", !FValidMon(MM), MM, 1))
      return tcError;
    else if (FErrorValN("q", !FValidDay(DD, MM, YY), DD, 2))
      return tcError;
    else if (FErrorValN("q", !FValidYea(YY), YY, 3 - (i < 3) - (i < 2)))
      return tcError;
    else if (FErrorValR("q", !FValidTim(TT), TT, 4))
      return tcError;
    else if (FErrorValR("q", !FValidDst(SS), SS, 5))
      return tcError;
    else if (FErrorValR("q", !FValidZon(ZZ), ZZ, 5 + (i > 7)))
      return tcError;
    else if (FErrorValR("q", !FValidLon(OO), OO, 6 + (i > 7)))
      return tcError;
    else if (FErrorValR("q", !FValidLat(AA), AA, 7 + (i > 7)))
      return tcError;
    if (i > 9) {
      ciCore.nam = SzClone(pin->argv[9]);
      ciCore.loc = SzClone(pin->argv[10]);
    }
  }
  SwSlotStore(ch2, &ci);
  return i;
}

static int NSwi(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1 = szSwitch[1], ch2 = ch1 == chNull ? chNull : szSwitch[2];
  CI ci;

  if (ch1 == 'x') {
    SwapTemp(ciCore, ciTwin, ci);
    return 0;
  }
  if (us.fNoRead) {
    ErrorArgv("i");
    // The retired case's "return tcError" left FProcessSwitches()
    // returning nonzero, which callers read as success-and-stop.
    return nSwitchStop;
  }
  if (FErrorArgc("i", pin->argc, 1))
    return tcError;
  if (ch1 == 'd') {
    OpenDir(pin->argv[1]);
    return 1;
  }
  ci = ciCore;
  if (!FInputData(pin->argv[1]))
    return tcError;
  if (ch1 == 'l' || ch2 == 'l')
    FAppendCIList(&ciCore);
  if (FBetween(ch1, '1', '0' + cRing)) {
    *rgpci[ch1 - '0'] = ciCore;
    ciCore = ci;
    *rgpcp[ch1 - '0'] = cp0;
  } else if (ch1 == 'D') {
    ciDefa = ciCore;
    ciCore = ci;
  } else if (ch1 == 't') {
    ciTran = ciCore;
    ciCore = ci;
    is.JDp = MdytszToJulian(MonT, DayT, YeaT, TimT,
      ciDefa.dst, ciDefa.zon);
  } else if (ch1 == 's') {
    ciSave = ciCore;
    ciCore = ci;
  } else if (ch1 == 'g') {
    ciGreg = ciCore;
    ciCore = ci;
  }
  return 1;
}

static int NSwoCore(char ch1, PARSEIN *pin)
{
  int i, c;

  if (us.fNoWrite) {
    ErrorArgv("o");
    return nSwitchStop;   // See NSwi(): success-and-stop, as before.
  }
  if (FErrorArgc("o", pin->argc, 1))
    return tcError;
  if (ch1 == 's') {
    FCloneSz(pin->argv[1], &is.szFileScreen);
    return 1;
  } else if (ch1 == '0' || ch1 == 'd' || ch1 == 'l' ||
    ch1 == 'a' || ch1 == 'q' || ch1 == 'c' || ch1 == 'x')
    us.nWriteFormat = FSwitchF2(us.nWriteFormat == ch1) * ch1;
  SwitchF(us.fWriteFile);
  FCloneSz(pin->argv[1], &is.szFileOut);
  if (is.rgszComment != NULL) {
    for (i = 0; i < is.cszComment; i++)
      DeallocatePIf(is.rgszComment[i]);
    DeallocateP(is.rgszComment);
    is.rgszComment = NULL;
  }
  for (c = 0; pin->argc - 1 - c > 1 && !FChSwitch(pin->argv[2 + c][0]); c++)
    ;
  is.cszComment = c;
  if (c > 0) {
    is.rgszComment = RgAllocate(c, char *, "comment list");
    ClearB((pbyte)is.rgszComment, c * sizeof(char *));
    for (i = 0; i < c; i++)
      FCloneSz(pin->argv[2 + i], &is.rgszComment[i]);
  }
  return 1 + c;
}

static int NSwo(CONST char *szSwitch, PARSEIN *pin)
{
  return NSwoCore(szSwitch[1], pin);
}

static int NSwGreater(CONST char *szSwitch, PARSEIN *pin)
{
  return NSwoCore('s', pin);
}

// Day arithmetic: bare "-" and "+" (and any lone switch character) move
// the chart date, with t/m/y suffixes selecting the unit.

static int NSwDay(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1;
  real rT;
  int i, j, darg = 0;

  if (szSwitch == pin->argv[0] && szSwitch[0] == chNull)
    return 0;    // A genuinely empty token is a no-op, as it was.
  ch1 = szSwitch[1];
  if (pin->argc > 1 && ((rT = RFromSz(pin->argv[1])) != 0.0 || FNumCh(pin->argv[1][0]) ||
    pin->argv[1][0] == '~'))
    darg++;
  else
    rT = 1.0;
  if (szSwitch[0] != '+')
    neg(rT);
  i = (int)rT;
  if (ch1 == 't') {
    i /= 24;
    rT -= (real)(i*24);
    TT += rT;
    AddTime(&ciCore, 3, 0);
  } else if (ch1 == 'm') {
    AddTime(&ciCore, 5, i%12);
    AddTime(&ciCore, 6, i/12);
    return darg;
  } else if (ch1 == 'y') {
    AddTime(&ciCore, 6, i);
    return darg;
  }
  j = MdyToJulian(MM, DD + i, YY);
  JulianToMdy((real)j - 0.5, &MM, &DD, &YY);
  return darg;
}

static int NSwr(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1 = szSwitch[1], ch2 = ch1 == chNull ? chNull : szSwitch[2];
  CI ci;
  int i, j, k;

  if (ch1 == 'P') {
    if (FErrorArgc("r", pin->argc, 1))
      return tcError;
    j = NFromSz(pin->argv[1]);
    if (FErrorValN("rP", !FBetween(j, 2, cRing), j, 0))
      return tcError;
    SwitchF(rgfProg[j]);
    return 1;
  }
  if (pin->fAnd) {
    us.nRel = rcNone;
    return 0;
  } else if (FBetween(ch1, '1', '0' + cRing)) {
    us.nRel = -(int)(ch1-'1');
    return 0;
  }
  i = 2 + 2*((ch1 == 'c' || ch1 == 'm') && ch2 == '0');
  if (FErrorArgc("r", pin->argc, i))
    return tcError;
  if (ch1 == 'c')
    us.nRel = rcComposite;
  else if (ch1 == 'm')
    us.nRel = rcMidpoint;
  else if (ch1 == 'd')
    us.nRel = rcDifference;
#ifdef BIORHYTHM
  else if (ch1 == 'b')
    us.nRel = rcBiorhythm;
#endif
  else if (ch1 == '0')
    us.nRel = rcDual;
  else if (ch1 == 't')
    us.nRel = rcTransit;
  else if (ch1 == 'p') {
    us.nRel = rcProgress;
    us.nProgress = (ch2 == '0') + 2*(ch2 == '1');
  } else
    us.nRel = rcSynastry;
  ci = ciCore;
  ciCore = ciTwin;
  if (!FInputData(pin->argv[2]))
    return tcError;
  cp2 = cp0;
  ciTwin = ciCore;
  ciCore = ci;
  if (!FInputData(pin->argv[1]))
    return tcError;
  cp1 = cp0;
  if (i > 2) {
    j = NFromSz(pin->argv[3]);
    if (j < 0)
      us.rRatio = RFromSz(pin->argv[4]);
    else {
      k = NFromSz(pin->argv[4]);
      if (j + k == 0)
        j = k = 1;
      us.rRatio = (real)j / (real)(j + k);
    }
  }
  return i;
}

#ifdef TIMEFUNC
static int NSwy(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1 = szSwitch[1], ch2 = ch1 == chNull ? chNull : szSwitch[2];

  if (FErrorArgc("y", pin->argc, 1))
    return tcError;
  if (ch1 == 'd')
    us.nRel = rcDifference;
#ifdef BIORHYTHM
  else if (ch1 == 'b')
    us.nRel = rcBiorhythm;
#endif
  else if (ch1 == 't')
    us.nRel = rcTransit;
  else if (ch1 == 'p') {
    us.nRel = rcProgress;
    us.nProgress = (ch2 == '0') + 2*(ch2 == '1');
  } else
    us.nRel = rcDual;
  if (!FInputData(szNowCore))
    return tcError;
  ciTwin = ciCore;
  if (!FInputData(pin->argv[1]))
    return tcError;
  return 1;
}
#endif

static int NSwFive(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1 = szSwitch[1], ch2 = ch1 == chNull ? chNull : szSwitch[2];
  int i;

  if (ch1 == 'e' || ch1 == 'Y') {
    i = 1 + (ch2 == '2') + (ch2 == '3')*2 + (ch2 == '4')*3;
    if (ch1 == 'e')
      us.nListAll = FSwitchF(us.nListAll == i) * i;
    else
      FEnumerateCIList(i);   // -5Y does the same as -Y5
  } else if (ch1 == 'd')
    FSortCIList(0);
  else if (ch1 == 'x')
    FSortCIList(1);
  else if (ch1 == 'y')
    FSortCIList(2);
  else if (ch1 == 'n')
    FSortCIList(3);
  else if (ch1 == 'l')
    FSortCIList(4);
  else if (ch1 == 's')
    FSortCIList(5);
  else if (ch1 == '0')
    is.cci = 0;
  else if (ch1 == 'f') {
    if (FErrorArgc("5f", pin->argc, 2))
      return tcError;
    FilterCIList(pin->argv[1], pin->argv[2]);
    return 2;
  } else if (ch1 == chNull)
    SwitchF(us.fListAuto);
  else {
    FErrorSubswitch("5", ch1, fTrue);
    return tcError;
  }
  return 0;
}

static int NSwk(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1 = szSwitch[1];

  if (ch1 == 'h') {
    SwitchF(us.fTextHTML);
    return 0;
  }
  if (ch1 == '1') {     // Undocumented subswitch.
    us.fAnsiColor = 2;
    us.fAnsiChar  = 1;
  } else {
    if (ch1 != '0')
      SwitchF(us.fAnsiColor);
    SwitchF(us.fAnsiChar);
  }
  return 0;
}

#if defined(GRAPH) && !defined(WIN) && !defined(QT)
static int NProcessSwitchesNullW(int argc, char **argv, int pos);
#endif

#ifdef GRAPH
static int NSwW(CONST char *szSwitch, PARSEIN *pin)
{
  int pos = (int)(szSwitch - pin->argv[0]) + 1;

#ifdef WIN
  return NProcessSwitchesW(pos, pin);
#elif defined(QT)
  return NProcessSwitchesQt(pos, pin);
#else
  return NProcessSwitchesNullW(pin->argc, pin->argv, pos);
#endif
}
#endif

static int NSwZero(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1;
  int l = 1;

  if (pin->fAnd)    // _0 should do nothing.
    return 0;
  ch1 = szSwitch[l];
  while (ch1 != chNull) {
    switch (ch1) {
    case 'o': us.fNoWrite    = fTrue; break;
    case 'i': us.fNoRead     = fTrue; break;
    case 'q': us.fNoQuit     = fTrue; break;
    case 'X': us.fNoGraphics = fTrue; break;
    case 'b': us.fNoPlacalc  = fTrue; break;
    case 'n': us.fNoNetwork  = fTrue; break;
    case '~': us.fNoExp      = fTrue; break;
    default: FErrorSubswitch("0", ch1, fTrue); return tcError;
    }
    ch1 = szSwitch[++l];
  }
  return 0;
}

static int NSwSemi(CONST char *szSwitch, PARSEIN *pin)
{
  return nSwitchStop;   // -; means don't process the rest of the line.
}

static int NSwAt(CONST char *szSwitch, PARSEIN *pin)
{
  return 0;   // The -@ switch is just a system flag indicator no-op.
}

static int NSwDot(CONST char *szSwitch, PARSEIN *pin)
{
  Terminate(tcForce);   // "-." is usually used to exit the -Q loop.
  return 0;
}

#ifdef EXPRESS
static int NSwTilde(CONST char *szSwitch, PARSEIN *pin)
{
  char ch1 = szSwitch[1], ch2 = ch1 == chNull ? chNull : szSwitch[2];
  int i, j;

  if (ch1 == '0') {
    SwitchF(us.fExpOff);
    return 0;
  }
  i = 1 + (ch1 == 'M' || ch1 == '2' || ch1 == '3');
  if (FErrorArgc("~", pin->argc, i))
    return tcError;
  if (ch1 == 'M') {
    j = NFromSz(pin->argv[1]);
    if (FErrorValN("~M", j < 0, j, 1))
      return tcError;
    ExpSetMacro(j, pin->argv[2]);
  } else if (ch1 == '1')
    ParseExpression(pin->argv[1]);
  else if (ch1 == '2') {
    j = NFromSz(pin->argv[1]);
    if (FErrorValN("~2", j < 0, j, 1))
      return tcError;
    ExpSetString(j, pin->argv[2], ch2 == '0');
  } else if (ch1 == '3') {
    j = NFromSz(pin->argv[1]);
    if (FErrorValN("~3", j < 0, j, 1))
      return tcError;
    ExpSetNums(j, pin->argv[2]);
  } else if (FErrorSubswitch("~", ch1, ch1 != chNull))
    return tcError;
  else
    ShowParseExpression(pin->argv[1]);
  return i;
}
#endif


// An AstroExpression hook switch: -~x "<expr>" stores one string into
// one us.szExp* slot, all with the same shape -- one argument, error
// label "~", prefix flags ignored. The whole family is data; the
// residual NSwTilde() handler keeps only the imperative forms (~0 ~M
// ~1 ~2 ~3 and bare ~) and the unknown-suffix error. Promoting these
// to exact rows tightened former garbage-suffix aliases (-~gz acted
// as -~g) into unknown-subswitch errors, per the strictness policy.

typedef struct _switchtilde {
  CONST char *szName;   // The full spelling, tilde included.
  char **ppch;          // The expression slot the argument lands in.
} SWITCHTILDE;

#ifdef EXPRESS
static CONST SWITCHTILDE rgswtilde[] = {
  {"~g", &us.szExpConfig}, {"~a", &us.szExpAspList},
  {"~a0", &us.szExpAspSumm}, {"~m", &us.szExpMid},
  {"~ma", &us.szExpMidAsp}, {"~j", &us.szExpInf},
  {"~j0", &us.szExpInf0}, {"~7", &us.szExpEso},
  {"~L", &us.szExpCross}, {"~E", &us.szExpEph},
  {"~Zd", &us.szExpRis}, {"~d", &us.szExpDay},
  {"~dv", &us.szExpVoid}, {"~t", &us.szExpTra},
  {"~P", &us.szExpPart}, {"~O", &us.szExpObj},
  {"~C", &us.szExpHou}, {"~A", &us.szExpAsp},
  {"~p", &us.szExpProg}, {"~p0", &us.szExpProg0},
  {"~kO", &us.szExpColObj}, {"~kA", &us.szExpColAsp},
  {"~kv", &us.szExpColFill}, {"~F", &us.szExpFontSig},
  {"~FC", &us.szExpFontHou}, {"~FO", &us.szExpFontObj},
  {"~FA", &us.szExpFontAsp}, {"~FN", &us.szExpFontNak},
  {"~FT", &us.szExpFontTxt}, {"~v", &us.szExpSort},
  {"~v3", &us.szExpDecan}, {"~v30", &us.szExpDecan2},
  {"~sd", &us.szExpDegree}, {"~Ux", &us.szExpExo},
  {"~U", &us.szExpStar}, {"~U0", &us.szExpAst},
  {"~Iv", &us.szExpIntV}, {"~IV", &us.szExpIntV2},
  {"~Ia", &us.szExpIntA}, {"~IA", &us.szExpIntA2},
  {"~Xv", &us.szExpCorner}, {"~XL", &us.szExpCity},
  {"~Xt", &us.szExpSidebar}, {"~XQ", &us.szExpKey},
  {"~WQ", &us.szExpMenu}, {"~q1", &us.szExpCast1},
  {"~q2", &us.szExpCast2}, {"~Q1", &us.szExpDisp1},
  {"~Q2", &us.szExpDisp2}, {"~Q3", &us.szExpDisp3},
  {"~5s", &us.szExpListS}, {"~5f", &us.szExpListF},
  {"~5Y", &us.szExpListY}, {"~5i", &us.szExpADB}
};
#endif

// De-soup verdicts (phase 2, P4, 2026-08-30): every prefix row below
// was read and sorted. Promoted out of here: the -~ hooks (rgswtilde),
// the two-flag chart families (-l -j -K -Q -8, now pf2 flag rows), and
// -v0. What stays, stays for cause: -m's suffixes combine (-ma0 is
// summary+aspects+midpoints, a meaningful spelling no row can carry);
// -b's backend suffixes share the fEphemFiles fall-through toggle;
// -z/-q/-d/-p/-r parse chart info and progression arguments where
// suffixes choose argument shapes; -R/-A/-E walk variable-length
// object lists; -w/-L/-N take optional leading numbers; -Y* and -X*
// prefix rows carry per-suffix arity and error labels. Promoting any
// of these would scatter coupled semantics across rows.
// The registry proper: switches whose shape doesn't fit a family table
// carry a handler. The handler owns arity, parsing, and stores, and
// returns the count of arguments it consumed, or tcError.

typedef struct _switchdef {
  CONST char *szName;
  word grf;
  int (*pfn)(CONST char *szSwitch, PARSEIN *pin);
  int carg;             // Minimum argument count; checked centrally
                        // by the dispatch when > 0, with szName as the
                        // error label. Rows whose handler needs a
                        // different label or per-suffix arity keep the
                        // check in the handler and leave this 0.
} SWITCHDEF;

static CONST SWITCHDEF rgswitchdef[] = {
  {"Yj0",  0,      NSwYj0},  {"Yj7",  0,      NSwYj7},
  {"YAD",  0,      NSwYAD, 4},  {"YJ",   0,      NSwYJ},
  {"YJ0",  0,      NSwYJ0},  {"YJ7",  0,      NSwYJ7},
  {"YJ70", 0, NSwYJ70},
  {"YR0",  0,      NSwYR0},  {"YR1",  0,      NSwYR1},
  {"YR2",  0,      NSwYR2},  {"YRp",  0,      NSwYRp},
  {"YRZ",  0,      NSwYRZ},  {"YR7",  0,      NSwYR7},
  {"YRd",  0,      NSwYRd, 1},  {"YRh",  0,      NSwYRh},
  {"YRo",  0,      NSwYRo},  {"YRi",  0,      NSwYRi},
  {"YRU",  0,      NSwYRU, 1},  {"YRU0", 0, NSwYRU0},
  {"YkU",  0,      NSwYkU, 1},  {"YkE",  0,      NSwYkE, 1},
  {"YkC",  0,      NSwYkC},
  {"YD",   0,      NSwYD, 2},   {"YS",   0,      NSwYS, 2},
  {"YU",   0,      NSwYU, 2},   {"YUb",  0,      NSwYUb},
  {"YUb0", 0, NSwYUb0}, {"YUx",  0,      NSwYUx, 1},
  {"YF",   0,      NSwYF, 8},
#ifdef SWISS
  {"Ye",   grfSwPrefix, NSwYe},
#endif
#ifdef MATRIX
  {"YE",   0,      NSwYE},
#endif
#ifdef INTERPRET
  {"YI",   0,      NSwYI},   {"YIa",  0,      NSwYIa},
  {"YIv",  0,      NSwYIv},  {"YIC",  0,      NSwYIC},
  {"YIA",  0,      NSwYIA},  {"YIA0", 0, NSwYIA0},
#endif
  {"YYt",  0,      NSwYYt, 1},  {"YYT",  0,      NSwYYT, 1},
#ifdef INTERPRET
  {"YYI",  0,      NSwYYI, 1},
#endif
#ifdef ATLAS
  {"YY",   0,      NSwYY},   {"YY1",  0,      NSwYY1},
  {"YY2",  0,      NSwYY2},  {"YY3",  0,      NSwYY3},
#endif
  {"Yu",   0,      NSwYu},   {"Yu0",  0,      NSwYu0},
  {"Ys",   0,      NSwYs},   {"Yc",   0,      NSwYc},
  {"Yl",   0,      NSwYl, 1},   {"Y1",   0,      NSwY1},
  {"Y10",  0,      NSwY10},  {"Yz",   0,      NSwYz, 1},
  {"Yz0",  0,      NSwYz0},  {"YzO",  0,      NSwYzO},
  {"YzC",  0,      NSwYzC},  {"YQ",   0,      NSwYQ, 1},
  {"Yw",   0,      NSwYw, 1},   {"YZ",   0,      NSwYZ, 1},
  {"Yb",   0,      NSwYb, 1},
#ifdef ARABIC
  {"YP",   0,      NSwYP, 1},
#endif
  {"YB",   0,      NSwYB},
  {"Y5i",  0,      NSwY5i, 1},  {"Y5I",  0,      NSwY5I, 2},
  // Prefix rows last, so exact spellings above always win.
  {"Y5",   grfSwPrefix, NSwY5},   {"Ya",   grfSwPrefix, NSwYa},
  {"Yq",   grfSwPrefix, NSwYq},   {"Yi",   grfSwPrefix, NSwYi},
#ifdef GRAPH
  {"YX",   0,      NSwYXNull, 2},
  {"YXD",  0,      NSwYXD},   {"YXD1", 0, NSwYXD1},
  {"YXDD", 0, NSwYXDD},  {"YXA",  0,      NSwYXA},
  {"YXA1", 0, NSwYXA1},  {"YXv",  0,      NSwYXv},
  {"YXt",  0,      NSwYXt, 1},   {"YXg",  0,      NSwYXg, 1},
  {"YXS",  0,      NSwYXS, 1},   {"YXj",  0,      NSwYXj, 1},
  {"YXj0", 0, NSwYXj0},  {"YX7",  0,      NSwYX7, 1},
  {"YXk",  0,      NSwYXk},   {"YXk0", 0, NSwYXk0},
  {"YXK",  0,      NSwYXK, 2},   {"YXK0", 0, NSwYXK0},
  {"YXa",  0,      NSwYXa, 1},   {"YXx",  0,      NSwYXx, 1},
  {"YXW",  0,      NSwYXW},
#ifdef SWISS
  {"YXU",  0,      NSwYXU, 2},   {"YXU0", 0, NSwYXU0},
  {"YXU1", 0, NSwYXU1},
#endif
#ifdef PSCRIPT
  {"YXp",  0,      NSwYXp, 1},   {"YXp0", 0, NSwYXp0, 2},
#endif
  {"YXG",  grfSwPrefix, NSwYXG},   {"YXf",  grfSwPrefix, NSwYXf},
  {"X",    grfSwGraphics, NSwX},
  {"Xo",   grfSwGraphics, NSwXo},
  {"XI",   grfSwGraphics, NSwXI, 1},
  {"XI0",  grfSwGraphics, NSwXI0},
  {"XIW",  grfSwGraphics, NSwXIW, 1},
  {"Xr",   grfSwGraphics, NSwXr},
  {"Xw",   grfSwGraphics, NSwXw},
  {"Xs",   grfSwGraphics, NSwXs, 1},
  {"XS",   grfSwGraphics, NSwXS, 1},
  {"X1",   grfSwGraphics, NSwXOne},
  {"X2",   grfSwGraphics, NSwXTwo},
  {"Xv",   grfSwGraphics, NSwXv, 1},
  {"XX",   grfSwGraphics, NSwXX},
  {"XX0",  grfSwGraphics, NSwXX},
  {"XW",   grfSwGraphics, NSwXW},
  {"XW0",  grfSwGraphics, NSwXW},
  {"XG",   grfSwGraphics, NSwXG},
  {"XG0",  grfSwGraphics, NSwXG},
  {"XP",   grfSwGraphics, NSwXP},
  {"XP0",  grfSwGraphics, NSwXP},
  {"XPv",  grfSwGraphics, NSwXP},
  {"XZ",   grfSwGraphics, NSwXZ},
  {"Xk",   grfSwGraphics, NSwXk},
  {"Xkv",  grfSwGraphics, NSwXk},
#ifdef PSCRIPT
  {"Xp",   grfSwGraphics, NSwXp},
  {"Xp0",  grfSwGraphics, NSwXp},
#endif
#ifdef SVG
  {"XV",   grfSwGraphics, NSwXV},
#endif
#ifdef WIRE
  {"X3",   grfSwGraphics, NSwX3},
#endif
#ifdef X11
  {"XB",   grfSwGraphics, NSwXB},
  {"Xd",   grfSwGraphics, NSwXd},
#endif
#ifdef CONSTEL
  {"XF",   grfSwGraphics, NSwXF},
#endif
#ifdef ISG
  {"Xn",   grfSwGraphics, NSwXn},
  {"Xnp",  grfSwGraphics, NSwXnp},
  {"Xnf",  grfSwGraphics, NSwXnf, 1},
#endif
  // Prefix rows for the sub-lettered X spellings.
  {"XE",   grfSwPrefix | grfSwGraphics, NSwXE},
  {"Xb",   grfSwPrefix | grfSwGraphics, NSwXb},
  {"XM",   grfSwPrefix | grfSwGraphics, NSwXM},
  {"XU",   grfSwPrefix | grfSwGraphics, NSwXU},
#ifdef ATLAS
  {"XL",   grfSwPrefix | grfSwGraphics, NSwXL},
#endif
#endif
  {"C",    0,           NSwC},
  {"u",    0,           NSwu},   {"u0",   0,           NSwu0},
  {"u8",   0,           NSwu8},  {"ub",   0,           NSwub},
  {"R",    grfSwPrefix, NSwR},
  {"U",    grfSwPrefix, NSwU},
  {"A",    grfSwPrefix, NSwA},
  {"h",    0,           NSwh},   {"x",    0,           NSwx, 1},
  {"1",    0,           NSwOne}, {"10",   0,           NSwOne0},
  {"2",    0,           NSwTwo}, {"20",   0,           NSwTwo0},
  {"4",    0,           NSwFour},
  {"b",    grfSwPrefix, NSwb},
  {"c",    grfSwPrefix, NSwc},
  {"s",    grfSwPrefix, NSws},
  {"p",    grfSwPrefix, NSwp},
  {"F",    grfSwPrefix, NSwF},
  {"e",    0,           NSwe},
  {"v",    grfSwPrefix, NSwv},  {"w",    grfSwPrefix, NSww},
  {"g",    grfSwPrefix, NSwg},  {"a",    grfSwPrefix, NSwa},
  {"m",    grfSwPrefix, NSwm},  {"Z",    grfSwPrefix, NSwZ},
    
  {"L",    grfSwPrefix, NSwL},  
  {"d",    grfSwPrefix, NSwd},  {"E",    grfSwPrefix, NSwE},
  {"t",    grfSwPrefix, NSwt},  {"T",    grfSwPrefix, NSwT},
  {"B",    grfSwPrefix, NSwB},  {"V",    grfSwPrefix, NSwV},
#ifdef ARABIC
  {"P",    grfSwPrefix, NSwP},
#endif
  {"N",    grfSwPrefix, NSwN},  {"I",    grfSwPrefix, NSwI},
  {"H",    grfSwPrefix, NSwH},  
  {"M",    grfSwPrefix, NSwM},
#ifdef TIMEFUNC
  {"n",    grfSwPrefix, NSwn},  {"y",    grfSwPrefix, NSwy},
#endif
  {"z",    grfSwPrefix, NSwz},  {"q",    grfSwPrefix, NSwq},
  {"i",    grfSwPrefix, NSwi},  {"o",    grfSwPrefix, NSwo},
  {">",    grfSwPrefix, NSwGreater},
  {"",     0,           NSwDay},
  {"+",    grfSwPrefix, NSwDay}, {"-",   grfSwPrefix, NSwDay},
  {"r",    grfSwPrefix, NSwr},  {"5",    grfSwPrefix, NSwFive},
  {"k",    grfSwPrefix, NSwk},
#ifdef GRAPH
  {"W",    grfSwPrefix, NSwW},
#endif
  {"0",    grfSwPrefix, NSwZero},
  {";",    grfSwPrefix, NSwSemi},
  {"@",    grfSwPrefix, NSwAt},
  {".",    grfSwPrefix, NSwDot},
#ifdef EXPRESS
  {"~",    grfSwPrefix, NSwTilde},
#endif
  };

// Look up a switch by its full spelling and run it. Returns the count of
// extra arguments consumed, tcError on a bad invocation, or
// nSwitchAbsent when the name isn't in the registry yet.

static flag FSwitchPrefix(CONST char *szName, CONST char *szPrefix)
{
  while (*szPrefix && *szName == *szPrefix)
    szName++, szPrefix++;
  return *szPrefix == chNull;
}

static int NProcessSwitchTable(CONST char *szName, PARSEIN *pin)
{
  CONST SWITCHFLAG *psf;
  CONST SWITCHRANGED *psr;
  CONST SWITCHDEF *psd;
  int i;

  for (psf = rgswflag;
    psf < rgswflag + sizeof(rgswflag)/sizeof(*rgswflag); psf++)
    if (FEqSz(szName, psf->szName)) {
      if (psf->grf & grfSwGraphics) {
        if (us.fNoGraphics) {
          ErrorArgv("X");
          return tcError;
        }
        SwitchF(*psf->pf);
        SwitchF2(us.fGraphics);
      } else
        SwitchF(*psf->pf);
      if (psf->pf2 != NULL)
        SwitchF(*psf->pf2);
      return 0;
    }
  for (psr = rgswranged;
    psr < rgswranged + sizeof(rgswranged)/sizeof(*rgswranged); psr++)
    if (FEqSz(szName, psr->szName))
      return NProcessSwitchRanged(psr, pin->argc, pin->argv);
#ifdef EXPRESS
  if (szName[0] == '~') {
    CONST SWITCHTILDE *pst;
    for (pst = rgswtilde;
      pst < rgswtilde + sizeof(rgswtilde)/sizeof(*rgswtilde); pst++)
      if (FEqSz(szName, pst->szName)) {
        if (FErrorArgc("~", pin->argc, 1))
          return tcError;
        FCloneSz(pin->argv[1], pst->ppch);
        return 1;
      }
  }
#endif
  for (psd = rgswitchdef;
    psd < rgswitchdef + sizeof(rgswitchdef)/sizeof(*rgswitchdef); psd++)
    if ((psd->grf & grfSwPrefix) ? FSwitchPrefix(szName, psd->szName) :
      FEqSz(szName, psd->szName)) {
      if ((psd->grf & grfSwGraphics) && us.fNoGraphics) {
        ErrorArgv("X");
        return tcError;
      }
      if (psd->carg > 0 && FErrorArgc(psd->szName, pin->argc, psd->carg))
        return tcError;
      i = psd->pfn(szName, pin);
      if (i >= 0 && (psd->grf & grfSwGraphics))
        SwitchF2(us.fGraphics);
      return i;
    }
  return nSwitchAbsent;
}


// Enumerate the registry for tests and tools: row i's spelling, grf
// bits, and which table it lives in (0 flag, 1 ranged, 2 handler), in
// exactly the order the dispatch scans them. Returns fFalse past the
// end. The suite's "registry" group pins the structural invariants
// (unique spellings, prefix rows never shadowing an exact row).

flag FSwitchRegistryRow(int i, CONST char **pszName, int *pgrf,
  int *pnTable)
{
  int cflag = sizeof(rgswflag)/sizeof(*rgswflag),
    cranged = sizeof(rgswranged)/sizeof(*rgswranged),
    cdef = sizeof(rgswitchdef)/sizeof(*rgswitchdef);

  if (i < cflag) {
    *pszName = rgswflag[i].szName;
    *pgrf = rgswflag[i].grf;
    *pnTable = 0;
    return fTrue;
  }
  i -= cflag;
  if (i < cranged) {
    *pszName = rgswranged[i].szName;
    *pgrf = 0;
    *pnTable = 1;
    return fTrue;
  }
  i -= cranged;
#ifdef EXPRESS
  {
    int ctilde = sizeof(rgswtilde)/sizeof(*rgswtilde);
    if (i < ctilde) {
      *pszName = rgswtilde[i].szName;
      *pgrf = 0;
      *pnTable = 3;
      return fTrue;
    }
    i -= ctilde;
  }
#endif
  if (i < cdef) {
    *pszName = rgswitchdef[i].szName;
    *pgrf = rgswitchdef[i].grf;
    *pnTable = 2;
    return fTrue;
  }
  return fFalse;
}


// Process a command switch line passed to the program. Read each entry in
// the argument list and set all the program modes and charts to display.

#if !defined(WIN) && !defined(QT)
// Consume a -W switch and its arguments without doing anything.
//
// These control a graphical interface this build does not have, so there
// is nothing to apply -- but they must still be *parsed*, because the
// alternative is ErrorSwitch() and abandoning the rest of the settings
// file. The argument counts mirror NProcessSwitchesW()/Qt() exactly; if a
// switch there grows an argument, it has to grow one here too or this
// build will read the next line as though it were a switch.

static int NProcessSwitchesNullW(int argc, char **argv, int pos)
{
  int carg;

  switch (argv[0][pos]) {
  case 'M':                       // -WM <n> "<name>", -WM0 <n> "<name>"
  case 'w':                       // -Ww <x> <y>
  case 'B':                       // -WB <x> <y>
    carg = 2;
    break;
  case chNull:                    // -W <command id>
  case 'N':                       // -WN <ms>
  case 'T':                       // -WT "<title>"
  case 'x':                       // -Wx <level>
    carg = 1;
    break;
  default:                        // -Wh, -Wn, -Wt, -Wb, -WZ, -Wo*, -WS*
    carg = 0;
    break;
  }
  if (carg > 0 && FErrorArgc("W", argc, carg))
    return tcError;
  return carg;
}
#endif


flag FProcessSwitches(int argc, char **argv, PARSECTX *pctx)
{
  int ich, i, j, k;
  flag fNot, fOr, fAnd;
  real rT;
  char ch1, ch2, *pch;
  CI ci;
#ifdef EXPRESS
  char **ppch;
#endif

  argc--; argv++;
  while (argc) {
    ch1 = argv[0][0];
    fNot = fOr = fAnd = fFalse;
    switch (ch1) {
    case '=': fOr  = fTrue; break;
    case '_': fAnd = fTrue; break;
    case ':':               break;
    default:  fNot = fTrue; break;
    }
    ich = 1 + FChSwitch(ch1);    // Leading dash?
    ch1 = argv[0][ich];
    ch2 = (ch1 == chNull ? chNull : argv[0][ich+1]);
    // Registry-migrated switches resolve here by exact name; anything
    // else falls through to the cases below unchanged.
    PARSEIN pin;
    pin.argc = argc; pin.argv = argv;
    pin.fOr = fOr; pin.fAnd = fAnd; pin.fNot = fNot;
    pin.pctx = pctx;
    i = NProcessSwitchTable(&argv[0][ich-1], &pin);
    if (i != nSwitchAbsent) {
      if (i == nSwitchStop)
        return fTrue;
      if (i < 0)
        return fFalse;
      argc -= i; argv += i;
      argc--; argv++;
      continue;
    }

    // Every switch lives in the registry now; an unmatched name is
    // simply unknown.
    ErrorSwitch(argv[0]);
    return fFalse;
  }
  return fTrue;
}

