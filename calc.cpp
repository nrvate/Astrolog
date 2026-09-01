/*
** Astrolog (Version 8.00) File: calc.cpp
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
** Julian Day Calculations.
******************************************************************************
*/

// Given a month, day, and year, convert it into a single Julian day value,
// i.e. the number of days passed since a fixed reference date.

long MdyToJulian(int mon, int day, int yea)
{
#ifdef MATRIX
  if (!us.fEphemFiles)
    return MatrixMdyToJulian(mon, day, yea);
#endif
#ifdef EPHEM
  int fGreg = fTrue;
  double jd;

  if (yea < ciGreg.yea || (yea == ciGreg.yea &&
    (mon < ciGreg.mon || (mon == ciGreg.mon && day < ciGreg.day))))
    fGreg = fFalse;
#ifdef SWISS
  if (!us.fPlacalcPla)
    jd = SwissJulDay(mon, day, yea, 12.0, fGreg) + rRound;
#endif
#ifdef PLACALC
  if (us.fPlacalcPla)
    jd = julday(mon, day, yea, 12.0, fGreg) + rRound;
#endif
  return (long)RFloor(jd);
#else
  return 0;        // Shouldn't ever be reached.
#endif // EPHEM
}


// Like above but return a fractional Julian time given the extra info.

real MdytszToJulian(int mon, int day, int yea, real tim, real dst, real zon)
{
  dst = DstReal(dst);
  return (real)MdyToJulian(mon, day, yea) + (tim + zon - dst) / 24.0;
}


// Take a Julian day value, and convert it back into the corresponding month,
// day, and year.

void JulianToMdy(real JD, int *mon, int *day, int *yea)
{
#ifdef EPHEM
  double tim;
#endif

#ifdef MATRIX
  if (!us.fEphemFiles) {
    MatrixJulianToMdy(JD, mon, day, yea);
    return;
  }
#endif
#ifdef SWISS
  if (!us.fPlacalcPla) {
    SwissRevJul(JD, JD >= 2299171.0 /* Oct 15, 1582 */, mon, day, yea, &tim);
    return;
  }
#endif
#ifdef PLACALC
  if (us.fPlacalcPla) {
    revjul(JD, JD >= 2299171.0 /* Oct 15, 1582 */, mon, day, yea, &tim);
    return;
  }
#endif
  *mon = mJan; *day = 1; *yea = 2026;
}


/*
******************************************************************************
** House Cusp Calculations.
******************************************************************************
*/

// Compute 3D houses for 3D Campanus or the default case where houses are 12
// equal sized wedges covering the celestial sphere. Basically the same as
// doing local horizon, giving coordinates relative to prime vertical.

real RHousePlaceIn3DCore(real rLon, real rLat)
{
  real lon, lat;

  lon = Tropical(rLon); lat = rLat;
  EclToEqu(&lon, &lat);
  lon = Mod(cp0.lonMC - lon + rDegQuad);
  if (us.nHouse3D == hmPrime) {
    if (!us.fRefract)
      EquToLocal(&lon, &lat, -Lat);
    else {
      EquToLocal(&lon, &lat, rDegQuad - Lat);
      lat = SwissRefract(lat);
      CoorXform(&lon, &lat, -rDegQuad);
    }
  } else if (us.nHouse3D == hmHorizon)
    EquToLocal(&lon, &lat, rDegQuad - Lat);
  lon = rDegMax - lon;
  return Mod(lon + rSmall);
}


// Compute 3D houses, or the house postion of a 3D location. Given a zodiac
// position and latitude, return the house position as a decimal number, which
// includes how far through the house the coordinates are.

real RHousePlaceIn3D(real rLon, real rLat)
{
  real deg, rRet;
  int i, di;

  // Campanus houses arranged along the prime vertical are equal sized in 3D,
  // as are a couple other combinations, and so are a simple case to handle.
  deg = RHousePlaceIn3DCore(rLon, rLat);
  if ((us.nHouseSystem == hsCampanus && us.nHouse3D == hmPrime) ||
    (us.nHouseSystem == hsHorizon && us.nHouse3D == hmHorizon) ||
    (us.nHouseSystem == hsMeridian && us.nHouse3D == hmEquator))
    return deg;

  // Determine which 3D house the prime vertical degree falls within.
  di = MinDifference(chouse3[1], chouse3[2]) >= 0.0 ? 1 : -1;
  i = 0;
  do {
    i++;
  } while (!(i >= cSign ||
    (deg >= chouse3[i] && deg < chouse3[Mod12(i + di)]) ||
    (chouse3[i] > chouse3[Mod12(i + di)] &&
    (deg >= chouse3[i] || deg < chouse3[Mod12(i + di)]))));
  if (di < 0)
    i = Mod12(i - 1);
  rRet = Mod(ZFromS(i) + MinDistance(chouse3[i], deg) /
    MinDistance(chouse3[i], chouse3[Mod12(i + 1)]) * 30.0);
  return rRet;
}


// This is a subprocedure of ComputeInHouses(). Given a zodiac position,
// return which of the twelve houses it falls in. Remember that a special
// check has to be done for the house that spans 0 degrees Aries.

int NHousePlaceIn(real rLon, real rLat)
{
  int i, di;

  // Special processing for 3D houses.
  if (us.fHouse3D && rLat != 0.0)
    return SFromZ(RHousePlaceIn3D(rLon, rLat));

  // This loop also works when house positions decrease through the zodiac.
  rLon = Mod(rLon + rSmall);
  di = MinDifference(chouse[1], chouse[2]) >= 0.0 ? 1 : -1;
  i = 0;
  do {
    i++;
  } while (!(i >= cSign ||
    (rLon >= chouse[i] && rLon < chouse[Mod12(i + di)]) ||
    (chouse[i] > chouse[Mod12(i + di)] &&
    (rLon >= chouse[i] || rLon < chouse[Mod12(i + di)]))));
  if (di < 0)
    i = Mod12(i - 1);
  return i;
}


// For each object in the chart, determine what house it belongs in.

void ComputeInHouses(void)
{
  int i;

  // First determine 3D house cusp offsets.
  if ((us.nHouseSystem == hsCampanus && us.nHouse3D == hmPrime) ||
    (us.nHouseSystem == hsHorizon && us.nHouse3D == hmHorizon) ||
    (us.nHouseSystem == hsMeridian && us.nHouse3D == hmEquator)) {
    // 3D Campanus cusps are always equal sized wedges when distributed
    // along the prime vertical, as are a couple of other combinations.
    for (i = 1; i <= cSign; i++)
      chouse3[i] = ZFromS(i);
  } else
    for (i = 1; i <= cSign; i++)
      chouse3[i] = RHousePlaceIn3DCore(chouse[i], 0.0);

  // Loop over each object and place it.
  for (i = 0; i <= is.nObj; i++)
    inhouse[i] = NHousePlaceIn(planet[i], planetalt[i]);

  // Avoid roundoff error by setting houses of objects known definitively.
  if (us.fHouse3D) {
    // 3D wedges that are equal sized should always be in corresponding house.
    if ((us.nHouseSystem == hsCampanus && us.nHouse3D == hmPrime) ||
      (us.nHouseSystem == hsHorizon && us.nHouse3D == hmHorizon) ||
      (us.nHouseSystem == hsMeridian && us.nHouse3D == hmEquator)) {
      for (i = cuspLo; i <= cuspHi; i++)
        if ((us.nHouse3D == hmPrime || (i != oAsc && i != oDes)) &&
          FNearR(chouse[i - cuspLo + 1], planet[i]))
          inhouse[i] = i - cuspLo + 1;
    // 3D angles for most systems should always be in the corresponding house.
    } else if (us.fHouseAngle) {
      if (us.nHouse3D == hmPrime) {
        for (i = cuspLo; i <= cuspHi; i += 3)
          if (FNearR(chouse[i - cuspLo + 1], planet[i]))
            inhouse[i] = i - cuspLo + 1;
      } else {
        for (i = cuspLo+4; i <= cuspHi; i += 6)
          if (FNearR(chouse[i - cuspLo + 1], planet[i]))
            inhouse[i] = i - cuspLo + 1;
      }
    }
    if (us.nHouse3D == hmHorizon && FNearR(chouse[7], planet[oVtx]))
      inhouse[oVtx] = 7;
    else if (us.nHouse3D == hmEquator && FNearR(chouse[1], planet[oEP]))
      inhouse[oEP] = 1;
  }
}


// Generic function to compute any of the various Equal house systems, in
// which all houses are an equal 30 degrees in size.

void HouseEqualGeneric(real rOffset)
{
  int i;

  for (i = 1; i <= cSign; i++)
    chouse[i] = Mod(ZFromS(i) + rOffset);
}


// Compute the cusp positions using the Porphyry house system.

void HousePorphyry(real Asc)
{
  int i;
  real rQuad, rSeg;

  rQuad = MinDistance(is.MC, Asc);
  rSeg = rQuad / 3.0;
  for (i = 0; i < 3; i++)
    chouse[sCap + i] = Mod(is.MC + rSeg*(real)i);
  rSeg = (rDegHalf - rQuad) / 3.0;
  for (i = 0; i < 3; i++)
    chouse[sLib + i] = Mod(Asc + rSeg*(real)i + rDegHalf);
  for (i = 1; i <= 6; i++)
    chouse[i] = Mod(chouse[6 + i] + rDegHalf);
}


// The Sripati house system is like the Porphyry system except each house
// starts in the middle of the previous house as defined by Porphyry.

void HouseSripati(void)
{
  int i;
  real rgr[cSign+1];

  HousePorphyry(is.Asc);
  for (i = 1; i <= cSign; i++)
    rgr[i] = chouse[i];
  for (i = 1; i <= cSign; i++)
    chouse[i] = Midpoint(rgr[i], rgr[Mod12(i-1)]);
}


// Compute the cusp positions using the Alcabitius house system.

void HouseAlcabitius(void)
{
  real rDecl, rSda, rSna, r, rLon;
  int i;

  rDecl = RAsin(RSinD(is.OB) * RSinD(is.Asc));
  r = -RTanD(AA) * RTan(rDecl);
  rSda = DFromR(RAcos(r));
  rSna = rDegHalf - rSda;
  chouse[sLib] = is.RA - rSna;
  chouse[sSco] = is.RA - rSna*2.0/3.0;
  chouse[sSag] = is.RA - rSna/3.0;
  chouse[sCap] = is.RA;
  chouse[sAqu] = is.RA + rSda/3.0;
  chouse[sPis] = is.RA + rSda*2.0/3.0;
  for (i = sLib; i <= sPis; i++) {
    r = RFromD(Mod(chouse[i]));
    // The transformation below is also done in CuspMidheaven().
    rLon = RAtn(RTan(r)/RCosD(is.OB));
    if (rLon < 0.0)
      rLon += rPi;
    if (r > rPi)
      rLon += rPi;
    chouse[i] = Mod(DFromR(rLon)+is.rSid);
  }
  for (i = sAri; i <= sVir; i++)
    chouse[i] = Mod(chouse[i+6]+rDegHalf);
}


// This is a newer house system similar in philosophy to Porphyry houses, and
// therefore (at least in the past) has also been called Neo-Porphyry. Instead
// of just trisecting the difference in each quadrant, do a smooth sinusoidal
// distribution of the difference around all the cusps. Note that middle
// houses become 0 sized if a quadrant is <= 30 degrees.

void HousePullenSinusoidalDelta(real Asc)
{
  real rQuad, rDelta;
  int iHouse;

  // Solve equations: x+n + x + x+n = q, x+3n + x+4n + x+3n = 180-q.
  rQuad = MinDistance(is.MC, Asc);
  rDelta = (rQuad - rDegQuad)/4.0;
  chouse[sLib] = Mod(Asc+rDegHalf); chouse[sCap] = is.MC;
  if (rQuad >= 30.0) {
    chouse[sAqu] = Mod(chouse[sCap] + 30.0 + rDelta);
    chouse[sPis] = Mod(chouse[sAqu] + 30.0 + rDelta*2.0);
  } else
    chouse[sAqu] = chouse[sPis] = Midpoint(chouse[sCap], Asc);
  if (rQuad <= 150.0) {
    chouse[sSag] = Mod(chouse[sCap] - 30.0 + rDelta);
    chouse[sSco] = Mod(chouse[sSag] - 30.0 + rDelta*2.0);
  } else
    chouse[sSag] = chouse[sSco] = Midpoint(chouse[sCap], chouse[sLib]);
  for (iHouse = sAri; iHouse < sLib; iHouse++)
    chouse[iHouse] = Mod(chouse[iHouse+6] + rDegHalf);
}


// This is a new house system very similar to Sinusoidal Delta. Instead of
// adding a sine wave offset, multiply a sine wave ratio.

void HousePullenSinusoidalRatio(real Asc)
{
  real qSmall, rRatio, rRatio3, xHouse, rLo, rHi;
  int iHouse, dir;

  // Start by determining the quadrant sizes.
  qSmall = MinDistance(is.MC, Asc);
  dir = qSmall <= rDegQuad ? 1 : -1;
  if (dir < 0)
    qSmall = rDegHalf - qSmall;

#if TRUE
  // Solve equations: rx + x + rx = q, xr^3 + xr^4 + xr^3 = 180-q. Solve
  // quartic for r, then compute x given 1st equation: x = q / (2r + 1).
  if (qSmall > 0.0) {
    rLo = (2.0*pow(qSmall*qSmall - 270.0*qSmall + 16200.0, 1.0/3.0)) /
      pow(qSmall, 2.0/3.0);
    rHi = RSqr(rLo + 1.0);
    rRatio = 0.5*rHi +
      0.5*RSqr(-6.0*(qSmall-120.0)/(qSmall*rHi) - rLo + 2.0) - 0.5;
  } else
    rRatio = 0.0;
  rRatio3 = rRatio * rRatio * rRatio;
  xHouse = qSmall / (2.0 * rRatio + 1.0);

#else
  // Can also solve equations empirically. Given candidate for r, compute x
  // given 1st equation: x = q / (2r + 1), then compare both against 2nd:
  // 2xr^3 + xr^4 = 180-q, to see whether current r is too large or small.
  // Before binary searching, first keep doubling rHi until too large.

  real qLarge = rDegHalf - qSmall, rRatio4;
  flag fBinarySearch = fFalse;

  rLo = rRatio = 1.0;
  loop {
    rRatio = fBinarySearch ? (rLo + rHi) / 2.0 : rRatio * 2.0;
    rRatio3 = rRatio * rRatio * rRatio; rRatio4 = rRatio3 * rRatio;
    xHouse = qSmall / (2.0 * rRatio + 1.0);
    if ((fBinarySearch && (rRatio <= rLo || rRatio >= rHi)) || xHouse <= 0.0)
      break;
    if (2.0 * xHouse * rRatio3 + xHouse * rRatio4 >= qLarge) {
      rHi = rRatio;
      fBinarySearch = fTrue;
    } else if (fBinarySearch)
      rLo = rRatio;
  }
#endif

  // xHouse and rRatio have been calculated. Fill in the house cusps.
  if (dir < 0)
    neg(xHouse);
  chouse[sAri] = Asc; chouse[sCap] = is.MC;
  chouse[sLib] = Mod(Asc + rDegHalf);
  chouse[sCap + dir]   = Mod(chouse[sCap]                + xHouse * rRatio);
  chouse[sCap + dir*2] = Mod(chouse[Mod12(sCap + dir*3)] - xHouse * rRatio);
  chouse[sCap - dir]   = Mod(chouse[sCap]                - xHouse * rRatio3);
  chouse[sCap - dir*2] = Mod(chouse[Mod12(sCap - dir*3)] + xHouse * rRatio3);
  for (iHouse = sTau; iHouse < sLib; iHouse++)
    chouse[iHouse] = Mod(chouse[iHouse+6] + rDegHalf);
}


// Compute the cusp positions using the Equal (Ascendant) house system.
#define HouseEqual() HouseEqualGeneric(is.Asc)

// This house system is just like the Equal system except that we start our 12
// equal segments from the Midheaven instead of the Ascendant.
#define HouseEqualMC() HouseEqualGeneric(is.MC + rDegQuad)

// The "Whole" house system is like the Equal system with 30 degree houses,
// where the 1st house starts at zero degrees of the sign of the Ascendant.
#define HouseWhole() HouseEqualGeneric((real)((SFromZ(is.Asc)-1)*30))

// Like "Whole" houses but the 10th house starts at the sign of the MC.
#define HouseWholeMC() \
  HouseEqualGeneric((real)((SFromZ(is.MC)-1)*30) + rDegQuad)

// The "Vedic" house system is like the Equal system except each house starts
// 15 degrees earlier. The Asc falls in the middle of the 1st house.
#define HouseVedic() HouseEqualGeneric(is.Asc - 15.0)

// Like "Vedic" houses bit the MC falls in the middle of the 10th house.
#define HouseVedicMC() HouseEqualGeneric(is.MC + rDegQuad - 15.0)

// Balanced Equal house systems split the difference between Asc and MC.
#define HouseEqualBalanced() HouseEqualGeneric(Midpoint(is.Asc, is.MC) + 45.0)
#define HouseWholeBalanced() HouseEqualGeneric((real)\
  ((SFromZ(Midpoint(is.Asc, is.MC) + 15.0)-1)*30 + 30.0))
#define HouseVedicBalanced() HouseEqualGeneric(Midpoint(is.Asc, is.MC) + 30.0)

// East Point Equal house systems are based around the East Point.
#define HouseEqualEP() HouseEqualGeneric(is.EP)
#define HouseWholeEP() HouseEqualGeneric((real)((SFromZ(is.EP)-1)*30))
#define HouseVedicEP() HouseEqualGeneric(is.EP - 15.0)

// Vertex Equal house systems are based around the Antivertex.
#define HouseEqualVertex() HouseEqualGeneric(is.Vtx + rDegHalf)
#define HouseWholeVertex() \
  HouseEqualGeneric((real)((SFromZ(is.Vtx + rDegHalf)-1)*30))
#define HouseVedicVertex() HouseEqualGeneric(is.Vtx + rDegHalf - 15.0)

// In "Null" houses, the cusps are fixed to start at their corresponding sign,
// i.e. the 1st house is always at 0 degrees Aries, etc.
#define HouseNull() HouseEqualGeneric(0.0)


// Calculate the house cusp positions, using the specified system. Note this
// is only called when Swiss Ephemeris is NOT computing the houses.

// A set of house cusps must go around the circle exactly once: the twelve
// gaps between consecutive cusps sum to 360 degrees. Five systems fail that
// toward the pole -- Topocentric, Campanus and Regiomontanus on both
// engines, APC and Savard-A on the Swiss one -- returning cusps whose gaps
// sum to 1080, 1800, 3240 or 3960, so the cusps circle three to eleven
// times and house assignment is meaningless rather than merely odd. The
// existing guard above covered Placidus and Koch only, and keyed on the
// polar circle rather than on the result (work log items 141, 157, 158).
//
// Checking the RESULT rather than listing systems and latitudes is what
// makes this one check serve both engines, and it will catch a system that
// starts failing without anyone adding it to a list.
//
// A zero-width house counts as degenerate too, with one family exempt.
// Pullen (S.Delta) collapses two cusps onto one degree ON PURPOSE when a
// quadrant is under 30 degrees wide -- see HousePullenSinusoidalDelta,
// "chouse[sAqu] = chouse[sPis] = Midpoint(...)" -- because three houses
// will not fit in it. That is Astrolog's own author writing a degenerate
// case, so the three systems built by that function are skipped.
//
// Sunshine is not in that category, and the Swiss Ephemeris source says
// so itself. swehouse.cpp falls back to Porphyry inside the polar circle
// for Placidus (line 1831), Koch (1251) and Regiomontanus (1628), and its
// Sunshine dispatch has the same fallback ready at line 1175 -- but the
// polar check that would trigger it is commented out in BOTH Sunshine
// solutions (2919 Makransky, 3055 Treindl):
//
//     // if (90 - fabs(lat) <= ecl) {
//     //   strcpy(hsp->serr, "Sunshine in polar circle not allowed");
//     //   return ERR;
//     // }
//
// Astrolog asks for 'I', the Treindl solution, so it gets cusps the
// library's own guard was written to refuse. Third-party code is out of
// scope here (REFACTORING.md non-goals); this check is the boundary our
// code presents to it, which is in scope, and it produces exactly the
// Porphyry fallback that file uses everywhere else (work log item 160).

flag FEnsureHousePartition(int housesystem)
{
  char sz[cchSzDef];
  real rSum = 0.0, rGap, rGapMin = rDegMax;
  flag fCollapseOk;
  int i;

  fCollapseOk = (housesystem == hsSineDelta ||
    housesystem == hsSineDeltaEP || housesystem == hsSineDeltaVtx);
  for (i = 1; i <= cSign; i++) {
    rGap = chouse[i == cSign ? 1 : i+1] - chouse[i];
    if (rGap < 0.0)
      rGap += rDegMax;
    if (rGap < rGapMin)
      rGapMin = rGap;
    rSum += rGap;
  }
  // 0.001 degrees is 3.6 arcseconds. A house narrower than that is
  // degenerate by any reading, and the threshold matches the one the
  // oracle's leg 4b sweep uses so the two cannot disagree about what
  // counts -- they did once, and the guard silently passed a Sunshine
  // chart whose narrowest house was a rounding error wide.
  if (RAbs(rSum - rDegMax) < 0.01 && (fCollapseOk || rGapMin >= 0.001))
    return fFalse;
  sprintf2(S(sz),
    "The %s system of houses is not defined at extreme latitudes.",
    szSystem[housesystem]);
  PrintWarning(sz);
  HousePorphyry(is.Asc);
  is.nHouseSystem = hsPorphyry;
  return fTrue;
}


void ComputeHouses(int housesystem)
{
  char sz[cchSzDef];
  real Vtx;

  // Don't allow polar latitudes if system not defined in polar zones.
  if ((housesystem == hsPlacidus || housesystem == hsKoch) &&
    RAbs(AA) >= rDegQuad - is.OB) {
    sprintf2(S(sz),
      "The %s system of houses is not defined at extreme latitudes.",
      szSystem[housesystem]);
    PrintWarning(sz);
    housesystem = hsPorphyry;
  }

  // Flip the Ascendant or MC if it falls in the wrong half of the zodiac.
  if (MinDifference(is.MC, is.Asc) < 0.0) {
    if (us.fPolarAsc)
      is.MC = Mod(is.MC + rDegHalf);
    else
      is.Asc = Mod(is.Asc + rDegHalf);
  }
  Vtx = Mod(is.Vtx + rDegHalf);

  switch (housesystem) {
#ifdef MATRIX
  case hsPlacidus:      HousePlacidus();       break;
  case hsKoch:          HouseKoch();           break;
  case hsCampanus:      HouseCampanus();       break;
  case hsMeridian:      HouseMeridian();       break;
  case hsRegiomontanus: HouseRegiomontanus();  break;
  case hsMorinus:       HouseMorinus();        break;
  case hsTopocentric:   HouseTopocentric();    break;
#endif
  case hsEqual:         HouseEqual();          break;
  case hsPorphyry:      HousePorphyry(is.Asc); break;
  case hsAlcabitius:    HouseAlcabitius();     break;
  case hsEqualMC:       HouseEqualMC();        break;
  case hsSineRatio:     HousePullenSinusoidalRatio(is.Asc); break;
  case hsSineDelta:     HousePullenSinusoidalDelta(is.Asc); break;
  case hsWhole:         HouseWhole();          break;
  case hsVedic:         HouseVedic();          break;
  case hsSripati:       HouseSripati();        break;

  // New experimental house systems follow:
  case hsWholeMC:       HouseWholeMC();        break;
  case hsVedicMC:       HouseVedicMC();        break;
  case hsEqualBalanced: HouseEqualBalanced();  break;
  case hsWholeBalanced: HouseWholeBalanced();  break;
  case hsVedicBalanced: HouseVedicBalanced();  break;
  case hsEqualEP:       HouseEqualEP();        break;
  case hsWholeEP:       HouseWholeEP();        break;
  case hsVedicEP:       HouseVedicEP();        break;
  case hsEqualVertex:   HouseEqualVertex();    break;
  case hsWholeVertex:   HouseWholeVertex();    break;
  case hsVedicVertex:   HouseVedicVertex();    break;
  case hsPorphyryEP:    HousePorphyry(is.EP);  break;
  case hsPorphyryVtx:   HousePorphyry(Vtx);    break;
  case hsSineRatioEP:   HousePullenSinusoidalRatio(is.EP); break;
  case hsSineRatioVtx:  HousePullenSinusoidalRatio(Vtx);   break;
  case hsSineDeltaEP:   HousePullenSinusoidalDelta(is.EP); break;
  case hsSineDeltaVtx:  HousePullenSinusoidalDelta(Vtx);   break;
  default:              HouseNull();
    housesystem = hsNull;
  }
  is.nHouseSystem = housesystem;
  FEnsureHousePartition(housesystem);
}


/*
******************************************************************************
** Star Position Calculations.
******************************************************************************
*/

// This is used by the chart calculation routine to calculate the positions
// of the fixed stars. Since stars don't move much in the sky over time,
// getting their positions is mostly just reading info from an array and
// converting it to the correct reference frame. However, have to add in
// the correct precession for the tropical zodiac.

void ComputeStars(real t, real Off)
{
#ifdef MATRIX
  int i;
  real x, y, z;
#endif

  // Read in star positions.

#ifdef SWISS
  if (FCmSwissStar())
    SwissComputeStars(t, fFalse);
  else
#endif
  {
#ifdef MATRIX
    for (i = 1; i <= cStar; i++) {
      x = rStarData[i*6-6]; y = rStarData[i*6-5]; z = rStarData[i*6-4];
      planet[oNorm+i] = x*rDegMax/24.0 + y*15.0/60.0 + z*0.25/60.0;
      x = rStarData[i*6-3]; y = rStarData[i*6-2]; z = rStarData[i*6-1];
      if (x < 0.0) {
        neg(y); neg(z);
      }
      planetalt[oNorm+i] = x + y/60.0 + z/60.0/60.0;
      // Convert to ecliptic zodiac coordinates.
      EquToEcl(&planet[oNorm+i], &planetalt[oNorm+i]);
      planet[oNorm+i] = Mod(planet[oNorm+i] + rEpoch2000 + Off);
      if (!us.fSidereal)
        ret[oNorm+i] = !us.fVelocity ? rDegMax/25765.0/rDayInYear : 1.0;
      SphToRec(cp0.dist[oNorm+i], planet[oNorm+i], planetalt[oNorm+i],
        &space[oNorm+i].x, &space[oNorm+i].y, &space[oNorm+i].z);
    }
#endif
  }
}


// Given the list of computed planet positions, sort and compose the final
// index list based on what order the planets are supposed to be printed in.

void SortPlanets()
{
  int i, j;
#ifdef EXPRESS
  real rgrSort[oNorm1];
  flag fCare1, fCare2;
#endif

  // By default, objects are displayed in object index order.
  for (i = 0; i <= cObj; i++)
    rgobjList[i] = rgobjList2[i] = i;

#ifdef EXPRESS
  // Adjust indexes used for display with AstroExpressions.
  if (FSzSet(us.szExpSort)) {
    for (i = 0; i <= oNorm; i++) {
      ExpSetN(iLetterZ, i);
      ParseExpression(us.szExpSort);
      rgrSort[i] = RExpGet(iLetterZ);
    }

    // Sort adjusted list to determine final display ordering.
    for (i = 1; i <= oNorm; i++) {
      j = i-1;
      loop {
        fCare1 = !ignore[rgobjList[j]] || !ignore2[rgobjList[j]];
        fCare2 = !ignore[rgobjList[j+1]] || !ignore2[rgobjList[j+1]];
        if (!(j >= 0 && ((!fCare1 && fCare2) || (fCare1 == fCare2 &&
          rgrSort[rgobjList[j]] > rgrSort[rgobjList[j+1]]))))
          break;
        SwapN(rgobjList[j], rgobjList[j+1]);
        j--;
      }
    }
  }
#endif

  // Now take the list of computed star positions, and sort and compose the
  // final index list based on what order the stars are supposed to be printed
  // in. Only sort if one of the special -U subswitches is in effect.

  if (us.nStarSort > 0)
  for (i = starLo+1; i <= starHi; i++) {
    j = i-1;

    // Compare star names for -Un switch.
    if (us.nStarSort == 'n') while (j >= starLo && NCompareSz(
      szObjDisp[rgobjList[j]], szObjDisp[rgobjList[j+1]]) > 0) {
      SwapN(rgobjList[j], rgobjList[j+1]);
      j--;

    // Compare star brightnesses for -Ub switch.
    } else if (us.nStarSort == 'b') while (j >= starLo &&
      rStarBright[rgobjList[j]-oNorm] > rStarBright[rgobjList[j+1]-oNorm]) {
      SwapN(rgobjList[j], rgobjList[j+1]);
      j--;

    // Compare star zodiac locations for -Uz switch.
    } else if (us.nStarSort == 'z') while (j >= starLo &&
      planet[rgobjList[j]] > planet[rgobjList[j+1]]) {
      SwapN(rgobjList[j], rgobjList[j+1]);
      j--;

    // Compare star latitudes for -Ul switch.
    } else if (us.nStarSort == 'l') while (j >= starLo &&
      planetalt[rgobjList[j]] < planetalt[rgobjList[j+1]]) {
      SwapN(rgobjList[j], rgobjList[j+1]);
      j--;

    // Compare star distances for -Ud switch.
    } else if (us.nStarSort == 'd') while (j >= starLo &&
      cp0.dist[rgobjList[j]] > cp0.dist[rgobjList[j+1]]) {
      SwapN(rgobjList[j], rgobjList[j+1]);
      j--;

    // Compare star velocities for -Uv switch.
    } else if (us.nStarSort == 'v') while (j >= starLo &&
      ret[rgobjList[j]] < ret[rgobjList[j+1]]) {
      SwapN(rgobjList[j], rgobjList[j+1]);
      j--;
    }
  }

  // Produce reverse lookup table mapping object index to its print order.
  for (i = 0; i <= cObj; i++)
    rgobjList2[rgobjList[i]] = i;
}


/*
******************************************************************************
** Chart Calculation.
******************************************************************************
*/

// Given a zodiac degree, transform it into its Decan sign, in which each
// sign is trisected into the three signs of its element. For example:
// 1 Aries -> 3 Aries, 10 Leo -> 0 Sagittarius, 25 Sagittarius -> 15 Leo.

real Decan(real deg)
{
  int sign;
  real unit;

  sign = SFromZ(deg);
  unit = deg - ZFromS(sign);
  sign = Mod12(sign + 4*((int)RFloor(unit/10.0)));
  unit = (unit - RFloor(unit/10.0)*10.0)*3.0;
  return ZFromS(sign)+unit;
}


// Given a zodiac degree, transform it into its Dwad sign, in which each
// sign is divided into twelfths, starting with its own sign. For example:
// 15 Aries -> 0 Libra, 10 Leo -> 0 Sagittarius, 20 Sagittarius -> 0 Leo.

real Dwad(real deg)
{
  int sign;
  real unit;

  sign = SFromZ(deg);
  unit = deg - ZFromS(sign);
  sign = Mod12(sign + ((int)RFloor(unit/2.5)));
  unit = (unit - RFloor(unit/2.5)*2.5)*12.0;
  return ZFromS(sign)+unit;
}


// Given a zodiac degree, transform it into its Navamsa position, in which
// each sign is divided into ninths, which determines the number of signs
// after a base element sign to use. Degrees within signs are unaffected.

real Navamsa(real deg)
{
  int sign, sign2;
  real unit;

  sign = SFromZ(deg);
  unit = deg - ZFromS(sign);
  sign2 = Mod12((((sign-1) & 3)^(2*FOdd(sign-1)))*3 + (int)(unit*0.3) + 1);
  return ZFromS(sign2) + unit;
}


CONST int rgnTermEgypt[cSign*2] = {
  0x66855, 0x64357, 0x86853, 0x43675, 0x66576, 0x36457, 0x76674, 0x54367,
  0x65766, 0x64735, 0x7a472, 0x34657, 0x68772, 0x73645, 0x74856, 0x54367,
  0xc5454, 0x64375, 0x77844, 0x36475, 0x76755, 0x34657, 0xc4392, 0x46357};
CONST int rgnTermPtolemy[cSign*2] = {
  0x68754, 0x64357, 0x87744, 0x43675, 0x77745, 0x36475, 0x67773, 0x56347,
  0x67665, 0x73465, 0x76566, 0x34675, 0x65856, 0x74635, 0x68763, 0x56437,
  0x86565, 0x64375, 0x66765, 0x43657, 0x66855, 0x73465, 0x86664, 0x46357};

// Return the planet associated with a degree of the zodiac. Can do decan
// rulerships, Chaldean decans, Egyptian terms, or Ptolemaic terms.

int ObjTerm(real pos, int nType)
{
  CONST TBLSIG *rgRules;
  CONST int *rgTerm;
  int nRet = 0, sig = SFromZ(pos) - 1, deg, i, d = 0, n;

  if (nType <= 0) {
    rgRules = RgRules();
    nRet = (*rgRules)[SIGT(SFromZ(Decan(pos)))];
  } else if (nType == 1) {
    n = ((int)pos)/10%7;
    nRet = (0x5143276 >> (6-n)*4) & 15;
  } else if (nType >= 2) {
    rgTerm = (nType == 2 ? rgnTermEgypt : rgnTermPtolemy);
    deg = (int)pos - sig*30;
    for (i = 0; i < 5; i++) {
      n = (rgTerm[sig*2] >> (4-i)*4) & 15;
      d += n;
      if (deg < d) {
        nRet = (rgTerm[sig*2+1] >> (4-i)*4) & 15;
        break;
      }
    }
    Assert(i < 5);
  }
#ifdef EXPRESS
  // Notify AstroExpression a term is about to be calculated.
  if (!us.fExpOff && FSzSet(us.szExpDecan2)) {
    ExpSetN(iLetterX, nType);
    ExpSetR(iLetterY, pos);
    ExpSetN(iLetterZ, nRet);
    ParseExpression(us.szExpDecan2);
    nRet = NExpGet(iLetterZ);
    if (!FBetween(nRet, 0, cObj))
      nRet = 0;
  }
#endif
  return nRet;
}


// Transform rectangular coordinates in x, y to polar coordinates.

void RecToPol(real x, real y, real *a, real *r)
{
  *r = RLength2(x, y);
  *a = RAngle(x, y);
}


// Transform spherical to rectangular coordinates in x, y, z.

void SphToRec(real r, real azi, real alt, real *rx, real *ry, real *rz)
{
  real rT;

  *rz = r *RSinD(alt);
  rT  = r *RCosD(alt);
  *rx = rT*RCosD(azi);
  *ry = rT*RSinD(azi);
}


// Convert 3D rectangular to spherical coordinates.

void RecToSph3(real rx, real ry, real rz, real *azi, real *alt)
{
  real ang, rad;

  RecToPol(rx, ry, &ang, &rad);
  *azi = DFromR(ang);
  ang = RAngleD(rad, rz);
  // Ensure latitude is from -90 to +90 degrees.
  if (ang > rDegHalf)
    ang -= rDegMax;
  *alt = ang;
}


// Do a coordinate transformation: Given a longitude and latitude value,
// return the new longitude and latitude values that the same location would
// have, were the equator tilted by a specified number of degrees. In other
// words, do a pole shift! This is used to convert among ecliptic, equatorial,
// and local coordinates, each of which have zero declination in different
// planes. In other words, take into account the Earth's axis.

void CoorXform(real *azi, real *alt, real tilt)
{
  real x, y, a1, l1;
  real sinalt, cosalt, sinazi, sintilt, costilt;

  *azi = RFromD(*azi); *alt = RFromD(*alt); tilt = RFromD(tilt);
  sinalt = RSin(*alt); cosalt = RCos(*alt); sinazi = RSin(*azi);
  sintilt = RSin(tilt); costilt = RCos(tilt);

  x = cosalt * sinazi * costilt - sinalt * sintilt;
  y = cosalt * RCos(*azi);
  l1 = RAngle(y, x);
  a1 = cosalt * sinazi * sintilt + sinalt * costilt;
  a1 = RAsin(a1);
  *azi = DFromR(l1); *alt = DFromR(a1);
}


// Fast version of CoorXform() in which the slow trigonometry values have
// already been computed. Useful when doing many transforms in a row.

void CoorXformFast(real *azi, real *alt, real sinazi, real cosazi,
  real sinalt, real cosalt, real sintilt, real costilt)
{
  real x, y, a1, l1;

  x = cosalt * sinazi * costilt - sinalt * sintilt;
  y = cosalt * cosazi;
  l1 = RAngle(y, x);
  a1 = cosalt * sinazi * sintilt + sinalt * costilt;
  a1 = RAsin(a1);
  *azi = DFromR(l1); *alt = DFromR(a1);
}


// Convert an ecliptic position to equatorial, or vice versa. Unlike
// CoorXform(), this also converts their velocities. There are options to
// actually edit the horizontal and/or vertical components of the position.

void EclToEquDir(real *azi, real *alt, real *azidir, real *altdir,
  flag fDoLon, flag fDoLat, flag fEclToEqu)
{
  real aziT = *azi, altT = *alt, azidirT, altdirT;

  azidirT = Mod(aziT + *azidir/12.0);
  altdirT =     altT + *altdir/12.0;
  if (fEclToEqu) {
    EclToEqu(&aziT, &altT);
    EclToEqu(&azidirT, &altdirT);
  } else {
    EquToEcl(&aziT, &altT);
    EquToEcl(&azidirT, &altdirT);
  }
  if (fDoLon) {
    *azi = aziT;
    *azidir = MinDifference(aziT, azidirT) * 12.0;
  }
  if (fDoLat) {
    *alt = altT;
    *altdir = (altdirT - altT) * 12.0;
  }
}


// Another subprocedure of the ComputeEphem() routine. Convert the final
// rectangular coordinates of a planet to zodiac position and latitude.

void ProcessPlanet(int ind, real aber)
{
  real ang, rad;

  RecToPol(space[ind].x, space[ind].y, &ang, &rad);
  planet[ind] = Mod(DFromR(ang) - aber + is.rSid);
  RecToPol(rad, space[ind].z, &ang, &rad);
  if (us.objCenter == oSun && ind == oSun)
    ang = 0.0;
  ang = DFromR(ang);
  // Ensure latitude is from -90 to +90 degrees.
  while (ang > rDegQuad)
    ang -= rDegHalf;
  while (ang < -rDegQuad)
    ang += rDegHalf;
  planetalt[ind] = ang;
}


#ifdef EPHEM
#ifdef JPLWEB
CONST int rgObjJPL[cThing+1] = {0/*399*/, 10, 301,
  199, 299, 499, 599, 699, 799, 899, 999, nMillion + 2060,
  nMillion + 1, nMillion + 2, nMillion + 3, nMillion + 4, 0, 0, 0};
#define FJPL(f) (f)
#else
#define FJPL(f) fFalse
#endif

// Return whether ComputeEphem() should skip computing object i entirely.
// Each if is one reason to skip, checked in the order the old inline
// condition chain OR'd them together.

flag FSkipEphem(int i, int objCentCalc, flag fSwiss, flag fJPLPla)
{
  // Restricted, and nothing else needs it: the Sun, Moon, and Earth are
  // always computed, as is a Node whose opposite Node is unrestricted,
  // and any object some forced midpoint is defined upon.
  if (ignore[i] && i > oMoo && (i != oNod || ignore[oSou]) &&
    !FObjMidSource(i))
    return fTrue;

  // Not a physical thing with an ephemeris position.
  if (!FThing(i))
    return fTrue;

  // The center of the computation has no position of its own, except
  // when JPL Horizons does its own centering, or the Swiss Ephemeris is
  // computing a barycentric Earth.
  if (i == objCentCalc && !fJPLPla &&
    !(fSwiss && objCentCalc == oEar && us.fBarycenter))
    return fTrue;

  // The Placalc backend computes nothing from Fortune onward, and the
  // -ba setting suppresses its four main asteroids.
  if (!fSwiss && (i >= oFor ||
    (us.fPlacalcAst && FBetween(i, oCer, oVes))))
    return fTrue;

  // JPL Horizons mode has no Earth entry in rgObjJPL[]: the Earth is
  // the center its queries are made from, not an object of its own.
  if (fJPLPla && i == oEar)
    return fTrue;

  return fFalse;
}


// Compute the positions of the planets at a certain time using the Swiss
// Ephemeris accurate formulas. This will supersede the Matrix routine values
// and is only called when the -b switch is in effect. Not all objects or
// modes are available using this, but some additional values such as Moon and
// Node velocities not available without -b are. (This is the main place in
// Astrolog which calls the Swiss Ephemeris functions.)

void ComputeEphem(real t)
{
  int objCentCalc, objOrbit, imax, i, j;
  real r1, r2, r3, r4, r5, r6, dist1, dist2, objPla, altPla, objEar, altEar,
    rT;
  flag fSwiss = !us.fPlacalcPla, fJPLPla, fJPL, fRet;
  PT3R ptPla, ptEar, vEar;
#ifdef JPLWEB
  flag fSav;
#endif

  // Can compute the positions of Sun through Pluto, Chiron, the four
  // asteroids, Lilith, North Node, and Uranians using ephemeris files.

  fJPLPla = fSwiss && us.nSwissEph == 3;
  objCentCalc = us.objCenter;
  if (objCentCalc > oNorm || FNodal(objCentCalc) ||
    (!fSwiss && objCentCalc != oEar) || (fJPLPla && us.objCenter > oSun) ||
    FJPL(FCust(objCentCalc) && rgTypSwiss[objCentCalc - custLo] == 4))
    objCentCalc = oSun;

  imax = Min(oNorm, is.nObj); imax = Max(imax, oSun);
  for (i = oEar; i <= imax; i++) {
    if (FSkipEphem(i, objCentCalc, fSwiss, fJPLPla))
      continue;

    // Calculate planet using Swiss Ephemeris, Placalc, or JPL Horizons
    fRet = fFalse;
    fJPL = FJPL((FCust(i) && rgTypSwiss[i - custLo] == 4) ||
      (fJPLPla && FBetween(i, 0, cThing) && rgObjJPL[i] > 0));
#ifdef JPLWEB
    if (fJPL) {
      fSav = us.fTruePos;
      if (us.objCenter != oEar)
        us.fTruePos = fTrue;
      j = FCust(i) ? rgObjSwiss[i - custLo] :
        (i == oSun && us.fBarycenter ? 0 : rgObjJPL[i]);
      fRet = GetJPLHorizons(j, &r1, &r2, &r3, &r4, &r5, &r6, NULL);
      us.fTruePos = fSav;
    } else
#endif
    {
#ifdef SWISS
      if (FCust(i) && rgTypSwiss[i - custLo] == 5)
        fRet = fTrue;
      else if (fSwiss) {
        objOrbit = us.fMoonMove ? ObjOrbit(i) : -1;
        if (objOrbit < 0 || objOrbit == oSun)
          objOrbit = objCentCalc;
        fRet = FSwissPlanet(i, JulianDayFromTime(t), objOrbit,
          &r1, &r2, &r3, &r4, &r5, &r6);
      }
#endif
#ifdef PLACALC
      if (!fSwiss)
        fRet = FPlacalcPlanet(i, JulianDayFromTime(t), objCentCalc != oEar,
          &r1, &r2, &r3, &r4, &r5, &r6);
#endif
    }
    if (!fRet)
      continue;

    // Store positions and velocities in object array.
    planet[i]    = Mod(r1 + is.rSid);
    planetalt[i] = r2;
    ret[i]       = r3;
    retalt[i]    = r5;
    retlen[i]    = r6;

    // Compute x,y,z coordinates from azimuth, altitude, and distance.
    SphToRec(r4, planet[i], planetalt[i],
      &space[i].x, &space[i].y, &space[i].z);

    // JPL Horizons always generated geocentric, so make heliocentric.
    if (fJPL && objCentCalc != oEar && !FNodal(i)) {
      if (i <= oSun) {
        // Heliocentric Earth is opposite geocentric Sun.
        PtNeg2(space[oEar], space[oSun]);
        ProcessPlanet(oEar, 0.0);
        continue;
      }
      PtAdd2(space[i], space[oEar]);
      ProcessPlanet(i, is.rSid);

      // Compute Earth's motion vector, to get Earth's true position.
      ptEar = space[oEar]; objEar = planet[oEar]; altEar = planetalt[oEar];
      SphToRec(PtLen(space[oEar]) + retlen[oEar], Mod(planet[oEar] +
        is.rSid + ret[oEar]), planetalt[oEar] + retalt[oEar],
        &space[oEar].x, &space[oEar].y, &space[oEar].z);
      ProcessPlanet(oEar, is.rSid);
      vEar = space[oEar]; PtSub2(vEar, ptEar);
      space[oEar] = ptEar; planet[oEar] = objEar; planetalt[oEar] = altEar;

      // Adjust true position of planet by true position of Earth.
      ptPla = space[i]; objPla = planet[i]; altPla = planetalt[i];
      SphToRec(r4 + r6, Mod(r1 + is.rSid + r3), r2 + r5,
        &space[i].x, &space[i].y, &space[i].z);
      PtAdd2(space[i], space[oEar]);
      PtAdd2(space[i], vEar);
      ProcessPlanet(i, is.rSid);
      ret[i] = (planet[i] - objPla);
      retalt[i] = (planetalt[i] - altPla);
      retlen[i] = (PtLen(space[i]) - PtLen(ptPla));
      space[i] = ptPla; planet[i] = objPla; planetalt[i] = altPla;

      if (!us.fTruePos) {
        // Convert AU to speed of light in days.
        rT = PtLen(ptPla) * rDayInYear / rLYToAU;
        SphToRec(PtLen(ptPla) - retlen[i]*rT, Mod(planet[i] - ret[i]*rT),
          planetalt[i] - retalt[i]*rT, &space[i].x, &space[i].y, &space[i].z);
        ProcessPlanet(i, 0.0);
      }
    }
  } // i

  // If Sun is solar system barycenter, offset it by Earth's position.
  if (us.fBarycenter && fSwiss && !fJPLPla && objCentCalc == oEar) {
    PtNeg2(space[oSun], space[oEar]);
    ProcessPlanet(oSun, 0.0);
  }

  // The central object should be opposite the Sun (or the Earth).
  if (fSwiss && !FNodal(us.objCenter)) {
    i = (objCentCalc != oSun ? oSun : oEar);
    PtNeg2(space[objCentCalc], space[i]);
  }
  if (fJPLPla && us.objCenter > oSun)
    PtZero(space[oSun]);

  // South Node object is geocentrically opposite the North Node.
  if (!ignore[oSou]) {
    PtNeg2(space[oSou], space[oNod]);
    planet[oSou] = Mod(planet[oNod] + rDegHalf);
    ret[oSou] = ret[oNod];
  }

  // Nodes and Lilith are always generated geocentric, so make heliocentric.
  if (objCentCalc != oEar)
    for (i = oNod; i <= oLil; i++) if (!ignore[i]) {
      PtAdd2(space[i], space[oEar]);
      if (fSwiss && us.objCenter > oSun && !FNodal(us.objCenter)) {
        PtSub2(space[i], space[oSun]);
      }
      ProcessPlanet(i, is.rSid);
      ret[i] = ret[oEar];
    }

  // If other planet centered, shift all positions by central planet.
  if (us.objCenter > oSun) {
    for (i = 0; i <= is.nObj; i++) {
      // Don't shift if object restricted, or if it's central object.
      if ((ignore[i] && i != oSun) || i == us.objCenter || !FThing(i))
        continue;
      fJPL = FJPL((FCust(i) && rgTypSwiss[i - custLo] == 4) || (fJPLPla &&
        (i == oEar || (FBetween(i, 0, cThing) && rgObjJPL[i] > 0))));
      // Don't shift if Swiss Ephemeris already shifted for us.
      if (fSwiss && !fJPL && !FNodal(i) && !FNodal(us.objCenter) &&
        us.objCenter <= oNorm && us.objCenter == objCentCalc)
        continue;
      if (fJPL && !fJPLPla && us.objCenter == objCentCalc)
        continue;
      if (us.fStarMagDist && FStar(i))
        dist1 = PtLen(space[i]);
      PtSub2(space[i], space[us.objCenter]);
      if (us.fStarMagDist && FStar(i)) {
        dist2 = us.fStarMagAbs ? 10.0 * rPCToAU : PtLen(space[i]);
        rStarBright[i-oNorm] = rStarBrightDef[i-oNorm] != rStarNot ?
          RStarBright(rStarBrightDef[i-oNorm], dist1, dist2) : rStarNot;
        kObjA[i] = KStarA(rStarBright[i-oNorm]);
      }
      ProcessPlanet(i, us.fSidereal ? is.rSid : 0.0);
    }
  }
  PtZero(space[us.objCenter]);

  // Potentially relocate objects so they orbit the central planet.
  if (us.fMoonMove)
    for (i = oMoo; i <= oNorm; i++) {
      if (ignore[i])
        continue;
      // Don't relocate if Swiss Ephemeris already relocated earlier.
      fJPL = FJPL((FCust(i) && rgTypSwiss[i - custLo] == 4) || (fJPLPla &&
        (i == oEar || (FBetween(i, 0, cThing) && rgObjJPL[i] > 0))));
      if (fSwiss && us.objCenter <= oNorm && !fJPL)
        continue;
      // Don't relocate if object already orbits Sun or central planet.
      j = ObjOrbit(i);
      if (j < 0 || j == us.objCenter || (j == oSun && us.objCenter <= oNorm))
        continue;
      PtSub2(space[i], space[j]);
      ProcessPlanet(i, us.objCenter > oSun && us.fSidereal ? is.rSid : 0.0);
    }

  // Potentially adjust velocities relative to average rate of body's motion.
  if (us.fVelocity) {
    objCentCalc = ObjOrbit(us.objCenter);
    for (i = 0; i <= oNorm; i++) {
      if (!FThing(i) || ignore[i])
        continue;
      j = i;
      objOrbit = ObjOrbit(j);
      // Planets closer to the Sun average orbit at the speed of the Sun.
      if (objOrbit == objCentCalc && FNorm(j) && FNorm(us.objCenter)) {
        if (rObjDist[j] < rObjDist[us.objCenter])
          j = objOrbit;
      // Planetary moons average orbit at the speed of their planet.
      } else if (objOrbit >= 0 && objOrbit != oSun &&
        objOrbit != us.objCenter)
        j = objOrbit;
      // Planets orbited by moons average orbit at the speed of the moon.
      if (objCentCalc >= 0 && objCentCalc != oSun && j == objCentCalc)
        j = us.objCenter;
      // The Sun moves at the speed of the central object orbiting the Sun.
      if (j == oSun)
        j = (us.objCenter == oSun ? oEar : us.objCenter);
      ret[i] /= (rDegMax / (rObjYear[j] * rDayInYear));
    }
  }
}
#endif


// This is probably the main routine in all of Astrolog. It generates a chart,
// calculating the positions of all the celestial bodies and house cusps,
// based on the current chart information, and saves them for use by any of
// the display routines.

real CastChart(int nContext)
{
  real housetemp[cSign+1], r, r2;
  int i, k, k2;

  is.nContext = nContext;
#ifdef EXPRESS
  // Notify AstroExpression a chart is about to be cast.
  if (!us.fExpOff && FSzSet(us.szExpCast1))
    ParseExpression(us.szExpCast1);
#endif

  // If month is negative, then chart was read in through a -o0 position file,
  // so planet positions are already in the arrays.

  if (FNoTimeOrSpace(ciCore)) {
    is.MC = planet[oMC]; is.Asc = planet[oAsc];
    ComputeInHouses();
    return 0.0;
  }

  // Time zone 24 means to have the time of day be in Local Mean Time (LMT).
  // This is done by making the time zone value reflect the logical offset
  // from UTC as indicated by the chart's longitude value.

  // From here to the end of the function, ciCore holds COOKED values, not
  // what the user typed: an LMT/LAT zone is resolved from the longitude,
  // an automatic Daylight setting from is.fDst, the zone is folded into
  // the time (making it UT), and the latitude is clamped off the exact
  // poles. Readers that depend on the cooked state: the is.T derivation
  // below, ComputeVariables()'s TT, the house math's AA, and any
  // AstroExpression hook that fires during the cast (funTim/funDst/
  // funZon/funLat read these fields). The Borrow restores the typed
  // values at every exit from the function; TestCastCookingQt() pins
  // both the cooked interpretation and the restore.
  Borrow bciCore(ciCore);
  is.JD = (real)MdyToJulian(MM, DD, YY);
  if (ZZ == zonLMT)
    ZZ = OO / 15.0;
  else if (ZZ == zonLAT)
    ZZ = OO / 15.0 - SwissLatLmt(is.JD);
  if (SS == dstAuto)
    SS = (real)is.fDst;
  TT = RSgn(TT)*RFloor(RAbs(TT))+RFract(RAbs(TT)) + (ZZ - SS);
  AA = Min(AA, rDegQuad-rSmall);     // Make sure chart isn't being cast on
  AA = Max(AA, -(rDegQuad-rSmall));  // precise North or South Pole.

  ClearB((pbyte)&cp0, sizeof(CP));     // On ecliptic unless say otherwise.
  ClearB((pbyte)space, sizeof(space));

  is.T = (is.JD + TT/24.0) + (us.rCuspAddition/24.0);
  if (us.fProgress) {

    // For ptCast, is.Tp is time that progressed chart cusps cast for.
    // For ptMixed, is.Tp is base chart time to solar arc cusps from.
    is.Tp = is.T;
    if (us.nProgress != ptMixed)
      is.Tp += (is.JDp - is.Tp) / ((us.nProgress != ptSolarArc ||
        us.objProgArc < 0) ? (us.rProgDay * us.rProgCusp) : rDayInYear);
    is.Tp = (is.Tp - 2415020.5) / 36525.0;

    // Determine actual time that a progressed chart is to be cast for.
    if (us.nProgress != ptSolarArc) {
      is.T += ((is.JDp - is.T) / us.rProgDay);
#ifdef EXPRESS
      // Adjust progression times with AstroExpressions.
      if (!us.fExpOff && FSzSet(us.szExpProg)) {
        ExpSetR(iLetterX, is.JDp);
        ExpSetR(iLetterY, is.Tp);
        ExpSetR(iLetterZ, is.T);
        ParseExpression(us.szExpProg);
        is.Tp = RExpGet(iLetterY);
        is.T  = RExpGet(iLetterZ);
      }
#endif
    }
  }
  is.T -= ((us.rCuspAddition - us.rObjAddition) / 24.0);
  is.T = (is.T - 2415020.5) / 36525.0;

  // Go calculate house cusp and angle positions.

#ifdef SWISS
  if (FCmSwissAny()) {
    SwissHouse(us.fProgress && us.nProgress != ptSolarArc ? is.Tp : is.T,
      OO, AA, us.nHouseSystem,
      &is.Asc, &is.MC, &is.RA, &is.Vtx, &is.EP, &is.OB, &is.rOff, &is.rNut);
  } else
#endif
  {
#ifdef MATRIX
    is.rOff = ProcessInput();
    ComputeVariables(&is.Vtx);
    if (us.fGeodetic)                // Check for -G geodetic chart.
      is.RA = Mod(-OO);
    is.MC  = CuspMidheaven();        // Calculate Ascendant & Midheaven.
    is.Asc = CuspAscendant();
    is.EP  = CuspEastPoint();
    ComputeHouses(us.nHouseSystem);  // Go calculate house cusps.
#endif
  }
  // This value (often same as is.RA) is frequently used, so compute once.
  cp0.lonMC = Tropical(is.MC); r = 0.0;
  EclToEqu(&cp0.lonMC, &r);

#ifdef MATRIX
  // Go calculate planet, Moon, and North Node positions.

  if (FCmMatrix() || (FCmPlacalc() && us.fUranian)) {
    ComputePlanets();
    if (!ignore[oMoo] || !ignore[oNod] || !ignore[oSou] || !ignore[oFor]) {
      ComputeLunar(&planet[oMoo], &planetalt[oMoo],
        &planet[oNod], &planetalt[oNod]);
      ret[oNod] = -1.0;
    }
  }
#endif

  // Go calculate star positions if -U switch in effect.

  if (us.fStar || FStar(us.objCenter))
    ComputeStars(is.T, (us.fSidereal ? us.rZodiacOffset : -is.rOff) +
      us.rZodiacOffsetAll);

#ifdef EPHEM
  // Compute more accurate ephemeris positions for certain objects.

  if (us.fEphemFiles)
    ComputeEphem(is.T);
#endif

  // Certain objects are positioned directly opposite to other objects.

  i = (us.objCenter != oSun ? oSun : oEar);
  planet[us.objCenter] = Mod(planet[i] + rDegHalf);
  planetalt[us.objCenter] = -planetalt[i];
  ret[us.objCenter] = ret[i];
  retalt[us.objCenter] = -retalt[i];
  if (!us.fEphemFiles) {
    planet[oSou] = Mod(planet[oNod] + rDegHalf);
    if (!us.fVelocity) {
      ret[oNod] = ret[oSou] = -0.053;
      ret[oMoo] = 12.2;
    } else
      ret[oNod] = ret[oSou] = ret[oMoo] = 1.0;
  }

  // Fill in "planet" positions corresponding to house cusps.

  planet[oVtx] = is.Vtx; planet[oEP] = is.EP;
  for (i = 1; i <= cSign; i++)
    planet[cuspLo-1 + i] = chouse[i];
  if (!us.fHouseAngle) {
    planet[oAsc] = is.Asc; planet[oMC] = is.MC;
    planet[oDes] = Mod(is.Asc + rDegHalf);
    planet[oNad] = Mod(is.MC + rDegHalf);
  }
  for (i = oVtx; i <= cuspHi; i++) {
    r = FCmSwissAny() ? ret[i] : (rDegMax + 1.0);
    if (us.fVelocity)
      r /= (rDegMax + 1.0);
    ret[i] = r;
  }

#ifdef ARABIC
  // Calculate position of Part of Fortune, and custom Arabic parts.

  if (!ignore[oFor]) {
    ComputeArabic(apFor, &planet[oFor], &planetalt[oFor], &ret[oFor],
      &r, &retalt[oFor], &retlen[oFor]);
    if (r != 0.0)
      SphToRec(r, planet[oFor], planetalt[oFor],
        &space[oFor].x, &space[oFor].y, &space[oFor].z);
  }
#ifdef SWISS
  for (i = custLo; i <= custHi; i++) {
    if (!ignore[i] && rgTypSwiss[i-custLo] == 5) {
      ComputeArabic(rgObjSwiss[i-custLo] - 1, &planet[i], &planetalt[i],
        &ret[i], &r, &retalt[i], &retlen[i]);
      if (r != 0.0)
        SphToRec(r, planet[i], planetalt[i],
          &space[i].x, &space[i].y, &space[i].z);
      else
        space[i].x = space[i].y = space[i].z = 0.0;
    }
  }
#endif
#endif // ARABIC

  // Transform ecliptic to equatorial coordinates if -sr in effect.

  if (us.fEquator || us.fEquator2)
    for (i = 0; i <= is.nObj; i++) if (!ignore[i]) {
      r = Tropical(planet[i]);
      EclToEquDir(&r, &planetalt[i], &ret[i], &retalt[i],
        us.fEquator, us.fEquator2, fTrue);
      planet[i] = Untropical(r);
    }

  // Now, may have to modify the base positions calculated above based on what
  // type of chart is being generated. To begin with: Solar arc progressions
  // apply an offset to planets and/or houses.

  if (us.fProgress && us.nProgress != ptCast) {
    // Compute true arc based on planet movement, or a fixed rate offset.
#ifdef SWISS
    if (us.objProgArc >= 0) {
      FSwissPlanet(us.objProgArc, JulianDayFromTime(is.Tp), us.objCenter,
        &r, &r2, &r2, &r2, &r2, &r2);
      r = Mod(r + is.rSid);
      r2 = (us.nProgress == ptSolarArc ? MinDifference(planet[us.objProgArc],
        r) : MinDifference(r, planet[us.objProgArc]));
      r = r2 / (us.rProgDay / rDayInYear);
    } else
#endif
    {
      r2 = JulianDayFromTime(us.nProgress == ptSolarArc ? is.T : is.Tp);
      r = (is.JDp - r2 - 0.5) / us.rProgDay;
    }
#ifdef EXPRESS
    // Adjust progression arc with AstroExpression.
    if (!us.fExpOff && FSzSet(us.szExpProg0)) {
      ExpSetR(iLetterX, is.JDp);
      ExpSetR(iLetterY, r2);
      ExpSetR(iLetterZ, r);
      ParseExpression(us.szExpProg0);
      r = RExpGet(iLetterZ);
    }
#endif
    // Full solar arc progressions apply offset to all planets.
    if (us.nProgress == ptSolarArc) {
      for (i = 0; i <= is.nObj; i++) {
        if (i == oFor)
          i = cuspHi+1;    // Skip over house cusp objects handled below.
        planet[i] = Mod(planet[i] + r);
      }
    }
    // Mixed solar arc progressions only apply offset to house cusps.
    r /= us.rProgCusp;
    for (i = oFor; i <= cuspHi; i++)
      planet[i] = Mod(planet[i] + r);
    for (i = 1; i <= cSign; i++)
      chouse[i] = Mod(chouse[i] + r);
#ifdef SWISS
    // May want to recalculate cusps based on the new position of the MC.
    if (us.fProgRAMC) {
      k = us.fGeodetic; us.fGeodetic = fTrue;
      r = Tropical(planet[oMC]); r2 = 0.0;
      EclToEqu(&r, &r2);
      r = Untropical(r);
      SwissHouse(us.nProgress == ptSolarArc ? is.T : is.Tp,
        rDegMax - r, AA, us.nHouseSystem, &r, &r, &r, &r, &r, &r, &r, &r);
      us.fGeodetic = k;
      for (i = 1; i <= cSign; i++)
        planet[cuspLo-1 + i] = chouse[i];
    }
#endif
  }

  // If -x harmonic chart in effect, then multiply all planet positions.

  if (us.rHarmonic != 1.0)
    for (i = 0; i <= is.nObj; i++)
      planet[i] = Mod(planet[i] * us.rHarmonic);

  // If -Y1 chart rotation in effect, then rotate the planets accordingly.

  if (us.objRot1 != us.objRot2 || us.fObjRotWhole) {
    r = planet[us.objRot2];
    if (us.fObjRotWhole)
      r = (real)((SFromZ(r)-1)*30);
    r -= planet[us.objRot1];
    k = Max(is.nObj, o12h);
    for (i = 0; i <= k; i++)
      planet[i] = Mod(planet[i] + r);
  }

  // Check to see if are -F forcing any objects to be particular values.

  for (i = 0; i <= is.nObj; i++)
    if (!FForceNone(force[i])) {
      if (FForcePos(force[i])) {
        // Force to a specific zodiac position.
        planet[i] = RForcePos(force[i]);
        planetalt[i] = ret[i] = retalt[i] = retlen[i] = 0.0;
      } else {
        // Force to a midpoint of two other positions.
        k = ObjForceMid1(force[i]); k2 = ObjForceMid2(force[i]);
        planet[i] = Midpoint(planet[k], planet[k2]);
        planetalt[i] = (planetalt[k] + planetalt[k2]) / 2.0;
        ret[i] = (ret[k] + ret[k2]) / 2.0;
        retalt[i] = (retalt[k] + retalt[k2]) / 2.0;
        retlen[i] = (retlen[k] + retlen[k2]) / 2.0;
      }
    }

  // If -1 or -2 solar chart in effect, then rotate the houses accordingly.

  if (us.objOnAsc) {
    r = planet[NAbs(us.objOnAsc)-1];
    if (us.fSolarWhole)
      r = ZFromS(SFromZ(r));
    r -= (us.objOnAsc > 0 ? is.Asc : is.MC);
    for (i = 1; i <= cSign; i++)
      chouse[i] = Mod(chouse[i] + r + rSmall);
  }

  // If -f domal chart switch in effect, switch planet and house positions.

  if (us.fFlip) {
    ComputeInHouses();
    for (i = 0; i <= is.nObj; i++) {
      k = inhouse[i];
      inhouse[i] = SFromZ(planet[i]);
      planet[i] = ZFromS(k)+MinDistance(chouse[k], planet[i]) /
        MinDistance(chouse[k], chouse[Mod12(k+1)])*30.0;
    }
    for (i = 1; i <= cSign; i++) {
      k = NHousePlaceIn2D(ZFromS(i));
      housetemp[i] = ZFromS(k)+MinDistance(chouse[k], ZFromS(i)) /
        MinDistance(chouse[k], chouse[Mod12(k+1)])*30.0;
    }
    for (i = 1; i <= cSign; i++)
      chouse[i] = housetemp[i];
  }

  // If -3 decan chart switch in effect, edit planet positions accordingly.

  if (us.fDecan)
    for (i = 0; i <= is.nObj; i++)
      planet[i] = Decan(planet[i]);

  // If -4 dwad chart switch in effect, edit planet positions accordingly.

  if (us.nDwad > 0)
    for (k = 0; k < us.nDwad; k++)
      for (i = 0; i <= is.nObj; i++)
        planet[i] = Dwad(planet[i]);

  // If -9 navamsa chart switch in effect, edit planet positions accordingly.

  if (us.fNavamsa)
    for (i = 0; i <= is.nObj; i++)
      planet[i] = Navamsa(planet[i]);

  // Sort planet and star positions now that all positions are finalized.

  for (i = 0; i <= is.nObj; i++)
    if (!ignore[i])
      cp0.dist[i] = PtLen(cp0.pt[i]);
  SortPlanets();

#ifdef EXPRESS
  // Adjust final planet and house positions with AstroExpressions.

  if (!us.fExpOff) {
    if (FSzSet(us.szExpObj)) {
      k = Max(is.nObj, o12h);
      for (i = 0; i <= k; i++) if (!ignore[i] || FCusp(i)) {
        ExpSetN(iLetterV, i);
        ExpSetR(iLetterW, planet[i]);
        ExpSetR(iLetterX, planetalt[i]);
        ExpSetR(iLetterY, ret[i]);
        ExpSetR(iLetterZ, retalt[i]);
        ParseExpression(us.szExpObj);
        planet[i]    = Mod(RExpGet(iLetterW));
        planetalt[i] = RExpGet(iLetterX);
        planetalt[i] = Min(planetalt[i], rDegQuad);
        planetalt[i] = Max(planetalt[i], -rDegQuad);
        ret[i]       = RExpGet(iLetterY);
        retalt[i]    = RExpGet(iLetterZ);
      }
    }
    if (FSzSet(us.szExpHou))
      for (i = 1; i <= cSign; i++) {
        ExpSetN(iLetterX, i);
        ExpSetR(iLetterY, chouse[i]);
        ExpSetR(iLetterZ, chouse3[i]);
        ParseExpression(us.szExpHou);
        chouse[i]  = Mod(RExpGet(iLetterY));
        chouse3[i] = Mod(RExpGet(iLetterZ));
      }
  }
#endif

  ComputeInHouses();    // Figure out what house everything falls in.
#ifdef EXPRESS
  // Notify AstroExpression a chart has just been cast.
  if (!us.fExpOff && FSzSet(us.szExpCast2))
    ParseExpression(us.szExpCast2);
#endif
  return is.T;
}


// Calculate the position of each planet with respect to the Gauquelin
// sectors. This is used by the sector charts. Fill out the planet position
// array where one degree means 1/10 the way across one of the 36 sectors.

void CastSectors()
{
  int source[MAXINDAY], type[MAXINDAY], occurcount, division, div,
    i, j, s1, s2, iSav, fSav;
  real time[MAXINDAY], rgalt1[objMax], rgalt2[objMax],
    azi1, azi2, alt1, alt2, mc1, mc2, d, k;
  CP cpA, cpB;

  // If the -l0 approximate sectors flag is set, we can quickly get rough
  // positions by having each position be the location of the planet as mapped
  // into Placidus houses. The -f flip houses flag does this for us.

  if (us.fSectorApprox) {
    iSav = us.nHouseSystem; us.nHouseSystem = hsPlacidus;
    inv(us.fFlip);
    CastChart(0);
    inv(us.fFlip);
    us.nHouseSystem = iSav;
    return;
  }

  // If not approximating sectors, then they need to be computed the formal
  // way: based on a planet's nearest rising and setting times. The code below
  // is similar to ChartHorizonRising() accessed by the -Zd switch.

  fSav = us.fSidereal; us.fSidereal = fFalse;
  division = us.nDivision * 4;
  occurcount = 0;

  // Start scanning from 18 hours before to 18 hours after the time of the
  // chart in question, to find the closest rising and setting times.

  ciCore = ciMain; ciCore.tim -= 18.0;
  if (ciCore.tim < 0.0) {
    ciCore.tim += 24.0;
    ciCore.day--;
  }
  CastChart(0);
  mc2 = planet[oMC]; k = planetalt[oMC];
  EclToEqu(&mc2, &k);
  cpB = cp0;
  for (i = 0; i <= is.nObj; i++)
    rgalt2[i] = planetalt[i];

  // Loop through 36 hours, dividing it into a certain number of segments.
  // For each segment we get the planet positions at its endpoints.

  for (div = 1; div <= division; div++) {
    ciCore = ciMain;
    ciCore.tim = ciCore.tim - 18.0 + 36.0*(real)div/(real)division;
    if (ciCore.tim < 0.0) {
      ciCore.tim += 24.0;
      ciCore.day--;
    } else if (ciCore.tim >= 24.0) {
      ciCore.tim -= 24.0;
      ciCore.day++;
    }
    CastChart(-1);
    mc1 = mc2;
    mc2 = planet[oMC]; k = planetalt[oMC];
    EclToEqu(&mc2, &k);
    cpA = cpB; cpB = cp0;
    for (i = 0; i <= is.nObj; i++) {
      rgalt1[i] = rgalt2[i]; rgalt2[i] = planetalt[i];
    }

    // During our segment, check to see if each planet rises or sets.

    for (i = 0; i <= is.nObj; i++) if (!FIgnore(i)) {
      EclToHoriz(&azi1, &alt1, cpA.obj[i], rgalt1[i], mc1, Lat);
      EclToHoriz(&azi2, &alt2, cpB.obj[i], rgalt2[i], mc2, Lat);
      j = 0;
      if ((alt1 > 0.0) != (alt2 > 0.0)) {
        d = RAbs(alt1)/(RAbs(alt1)+RAbs(alt2));
        k = Mod(azi1 + d*MinDifference(azi1, azi2));
        j = 1 + (MinDistance(k, rDegHalf) < rDegQuad);
      }
      if (j && occurcount < MAXINDAY) {
        source[occurcount] = i;
        type[occurcount] = j;
        time[occurcount] = 36.0*((real)(div-1)+d)/(real)division*60.0;
        occurcount++;
      }
    }
  }

  // Sort each event in order of time when it happens during the day.

  for (i = 1; i < occurcount; i++) {
    j = i-1;
    while (j >= 0 && time[j] > time[j+1]) {
      SwapN(source[j], source[j+1]);
      SwapN(type[j], type[j+1]);
      SwapR(&time[j], &time[j+1]);
      j--;
    }
  }

  // Now fill out the planet array with the appropriate sector location.

  for (i = 0; i <= is.nObj; i++) if (!ignore[i]) {
    planet[i] = 0.0;
    // Search for the first rising or setting event of our planet.
    for (s2 = 0; s2 < occurcount && source[s2] != i; s2++)
      ;
    if (s2 == occurcount) {
LFail:
      // If we failed to find a rising/setting bracket around our time,
      // automatically restrict that planet so it doesn't show up.
      ignore[i] = fTrue;
      continue;
    }
LRetry:
    // One rising or setting event was found. Now search for the next one.
    s1 = s2;
    for (s2 = s1 + 1; s2 < occurcount && source[s2] != i; s2++)
      ;
    if (s2 == occurcount)
      goto LFail;
    // Reject the two events if either (1) they're both the same, i.e. both
    // rising or both setting, or (2) they don't bracket the chart's time.
    if (type[s2] == type[s1] || time[s1] > 18.0*60.0 || time[s2] < 18.0*60.0)
      goto LRetry;
    // Cool, found the rising/setting bracket. The sector position is the
    // proportion the chart time is between the two event times.
    planet[i] = (18.0*60.0 - time[s1])/(time[s2] - time[s1])*rDegHalf;
    if (type[s1] == 2)
      planet[i] += rDegHalf;
    planet[i] = Mod(rDegMax - planet[i]);
  }

  // Restore original chart info since have overwritten it.

  ciCore = ciMain;
  us.fSidereal = fSav;
}


/*
******************************************************************************
** Aspect Calculations.
******************************************************************************
*/

// Set up the aspect/midpoint grid. Allocate memory for this array, if not
// already done. Allocation is only done once, first time this is called.

flag FEnsureGrid(void)
{
  if (grid != NULL)
    return fTrue;
  grid = (GridInfo *)PAllocate(sizeof(GridInfo), "grid");
  return grid != NULL;
}


// Indicate whether some aspect between two objects should be shown.

flag FAcceptAspect(int obj1, int asp, int obj2)
{
  int oCen;
  flag fSupp, fTrans;

  // Negative aspect means context is a transit to transit consideration.
  fTrans = (asp < 0);
  if (fTrans)
    neg(asp);

  // If the aspect restricted, reject immediately.
  if (FIgnoreA(asp))
    return fFalse;
  if (us.objRequire >= 0 && obj1 != us.objRequire && obj2 != us.objRequire)
    return fFalse;

  // Transits always need to prevent aspects involving continually opposite
  // objects, to prevent exact aspect events numerous times per day.
  oCen = us.objCenter == oSun ? oEar : us.objCenter;
  if (!us.fSmartCusp) {
    if (!fTrans)
      return fTrue;
    if (us.objCenter == oEar &&
      (obj1 == oNod || obj2 == oNod) && (obj1 == oSou || obj2 == oSou))
      return fFalse;
    if ((obj1 == oSun || obj2 == oSun) && (obj1 == oCen || obj2 == oCen))
      return fFalse;
    return fTrue;
  }

  // Allow only conjunctions to the minor house cusps.
  if ((FMinor(obj1) || FMinor(obj2)) && asp > aCon)
    return fFalse;

  // Is this a weaker aspect supplemental to a stronger one present?
  fSupp = (asp == aOpp && !FIgnoreA(aCon)) ||
    (asp == aSex && !FIgnoreA(aTri)) || (asp == aSSx && !FIgnoreA(aInc)) ||
    (asp == aSes && !FIgnoreA(aSSq)) || (asp == aSQn && !FIgnoreA(aBQn)) ||
    (asp == aDc3 && !FIgnoreA(aQui));

  // Prevent any simultaneous aspects to opposing Angle cusps, e.g. if
  // Conjunct one, don't be Opposite the other; if Trine one, don't Sextile
  // the other; don't Square both at once, etc.
  if (!ignore[oMC] && !ignore[oNad] &&
    (((obj1 == oMC || obj2 == oMC) && fSupp) ||
    ((obj1 == oNad || obj2 == oNad) && (fSupp || asp == aSqu))))
    return fFalse;
  if (!ignore[oAsc] && !ignore[oDes] &&
    (((obj1 == oAsc || obj2 == oAsc) && fSupp) ||
    ((obj1 == oDes || obj2 == oDes) && (fSupp || asp == aSqu))))
    return fFalse;

  // Prevent any simultaneous aspects to the North and South Node.
  if (us.objCenter == oEar && !ignore[oNod] && !ignore[oSou] &&
    (((obj1 == oNod || obj2 == oNod) && fSupp) ||
    ((obj1 == oSou || obj2 == oSou) && (fSupp || asp == aSqu))))
    return fFalse;

  // Prevent any simultaneous aspects to the Sun and central planet.
  if (!ignore[oCen] && !ignore[oSun] &&
    (((obj1 == oSun || obj2 == oSun) && fSupp) ||
    ((obj1 == oCen || obj2 == oCen) && (fSupp || asp == aSqu))))
    return fFalse;

  return fTrue;
}


// This is a subprocedure of FCreateGrid() and FCreateGridRelation(). Given
// two planets, determine what aspect, if any, is present between them, and
// determine the aspect's orb too.

int GetAspect(CONST GRDOBJR &planet1, CONST GRDOBJR &planet2,
  CONST GRDOBJR &planetalt1, CONST GRDOBJR &planetalt2,
  CONST GRDOBJR &ret1, CONST GRDOBJR &ret2, int i, int j, real *prOrb)
{
  int asp;
  real rAngle, rAngle3D, rDiff, rOrb, ret1a;

  // Compute the angle between the two planets.
  rAngle = MinDistance(planet1[i], planet2[j]);
  if (us.fAspect3D || us.fAspectLat)
    rAngle3D = SphDistance(planet1[i], planetalt1[i],
      planet2[j], planetalt2[j]);

  // Check each aspect angle to see if it applies.
  for (asp = 1; asp <= us.nAsp; asp++) {
    if (!FAcceptAspect(i, asp, j))
      continue;
    rDiff = (!us.fAspect3D ? rAngle : rAngle3D) - rAspAngle[ASPT(asp)];
    rOrb = GetOrb(i, j, asp);

    // If -ga switch in effect, then change the sign of the orb to correspond
    // to whether the aspect is applying or separating. To do this, check the
    // velocity vectors to see if the planets are moving toward, away, or are
    // overtaking each other.

    if (us.nAppSep == 1) {
      ret1a = us.nRel > rcTransit ? ret1[i] : 0.0;
      rDiff *= RSgn2(ret2[j]-ret1a) * RSgn2(MinDifference(planet1[i],
        planet2[j]));
    } else if (us.nAppSep == 2) {
      // The -gx switch means make aspect orb signs reflect waxing/waning.
      ret1a = us.nRel > rcTransit ? ret1[i] : 0.0;
      rDiff = RAbs(rDiff) * RSgn2(ret1a-ret2[j]) *
        RSgn2(MinDifference(planet1[i], planet2[j]));
    }

#ifdef EXPRESS
    // Adjust orb with AstroExpression, if one defined.
    if (!us.fExpOff && FSzSet(us.szExpAsp)) {
      ExpSetN(iLetterV, i);
      ExpSetN(iLetterW, asp);
      ExpSetN(iLetterX, j);
      ExpSetR(iLetterY, rDiff);
      ExpSetR(iLetterZ, rOrb);
      ParseExpression(us.szExpAsp);
      rDiff = RExpGet(iLetterY);
      rOrb  = RExpGet(iLetterZ);
    }
#endif

    // If aspect within orb, return it.
    if (RAbs(rDiff) < rOrb) {
      if (us.fAspectLat && !(RAbs((!us.fAspect3D ? rAngle3D : rAngle) -
        rAspAngle[ASPT(asp)]) < rOrb))
        continue;
      if (prOrb != NULL)
        *prOrb = rDiff;
      return asp;
    }
  }
  return 0;
}


// Very similar to GetAspect(), this determines if there is a parallel or
// contraparallel aspect between the given two planets. The settings and orbs
// for conjunction are used for parallel and those for opposition are used for
// contraparallel.

int GetParallel(CONST GRDOBJR &planet1, CONST GRDOBJR &planet2,
  CONST GRDOBJR &planetalt1, CONST GRDOBJR &planetalt2,
  CONST GRDOBJR &ret1, CONST GRDOBJR &ret2,
  CONST GRDOBJR &retalt1, CONST GRDOBJR &retalt2, int i, int j,
  real *prOrb)
{
  int asp;
  real rDiff, rOrb, azi1, azi2, alt1, alt2, dir1, dir2, altdir1, altdir2,
    retalt1a;

  // Compute the declination of the two planets.
  azi1 = planet1[i];    azi2 = planet2[j];
  alt1 = planetalt1[i]; alt2 = planetalt2[j];
  dir1 = ret1[i];       dir2 = ret2[j];
  altdir1 = retalt1[i]; altdir2 = retalt2[j];
  if (!us.fEquator2 && !us.fParallel2) {
    // If have ecliptic latitude and want equatorial declination, convert.
    EclToEquDir(&azi1, &alt1, &dir1, &altdir1, fFalse, fTrue, fTrue);
    EclToEquDir(&azi2, &alt2, &dir2, &altdir2, fFalse, fTrue, fTrue);
  } else if (us.fEquator2 && us.fParallel2) {
    // If have equatorial declination and want ecliptic latitude, convert.
    EclToEquDir(&azi1, &alt1, &dir1, &altdir1, fFalse, fTrue, fFalse);
    EclToEquDir(&azi2, &alt2, &dir2, &altdir2, fFalse, fTrue, fFalse);
  }

  // Check each vertical aspect type to see if it applies.
  for (asp = 1; asp <= (!us.fDistance ? aOpp : us.nAsp); asp++) {
    if (!FAcceptAspect(i, asp, j))
      continue;
    if (asp == aCon)
      rDiff = alt2 - alt1;
    else if (asp == aOpp)
      rDiff = alt1 + alt2;
    else {
      retalt1a = rAspAngle[ASPT(asp)] / rDegHalf;
      if (RAbs(alt1) > RAbs(alt2))
        alt2 /= retalt1a;
      else
        alt1 /= retalt1a;
      rDiff = ((alt1 >= 0.0) == (alt2 >= 0.0) ? alt1 - alt2 : alt1 + alt2);
    }
    rOrb = GetOrb(i, j, asp);
    if (us.nAppSep == 1) {
      if (FCmSwissAny()) {
        retalt1a = us.nRel > rcTransit ? altdir1 : 0.0;
        rDiff *= RSgn2(altdir2 - retalt1a);
      } else {
        // If no declination velocity, make aspect separating.
        rDiff = RAbs(rDiff);
      }
    } else if (us.nAppSep == 2) {
      // "Waxing" is if bodies on same side, "waning" if on different sides.
      // (This means Parallel is always waxing, Contraparallel always waning.)
      rDiff = RAbs(rDiff) * ((alt1 >= 0.0) == (alt2 >= 0.0) ? -1.0 : 1.0);
    }

#ifdef EXPRESS
    // Adjust parallel orb with AstroExpression, if one defined.
    if (!us.fExpOff && FSzSet(us.szExpAsp)) {
      ExpSetN(iLetterV, i);
      ExpSetN(iLetterW, asp + cAspect);
      ExpSetN(iLetterX, j);
      ExpSetR(iLetterY, rDiff);
      ExpSetR(iLetterZ, rOrb);
      ParseExpression(us.szExpAsp);
      rDiff = RExpGet(iLetterY);
      rOrb  = RExpGet(iLetterZ);
    }
#endif

    // If vertical aspect within orb, return it.
    if (RAbs(rDiff) < rOrb) {
      if (prOrb != NULL)
        *prOrb = rDiff;
      return asp;
    }
  }
  return 0;
}


// Similar to GetAspect() and GetParallel(), this determines if there is a
// distance type aspect between the given two planets.

int GetDistance(CONST PT3R *space1, CONST PT3R *space2,
  CONST GRDOBJR &retlen1, CONST GRDOBJR &retlen2, int i, int j,
  real *prOrb)
{
  int asp;
  real dist1, dist2, rPct, rDiff, rOrb, retlen1a;

  // Compute the distances of and between the two planets.
  dist1 = PtLen(space1[i]); dist2 = PtLen(space2[j]);
  if (us.fSmartCusp && (dist1 <= 0.0 || dist2 <= 0.0))
    return 0;
  if (dist1 <= 0.0 && dist2 <= 0.0)
    rPct = 100.0;
  else
    rPct = (dist1 <= dist2 ? dist1 / dist2 : dist2 / dist1) * 100.0;

  // Check each distance aspect proportion to see if it applies.
  for (asp = 1; asp <= us.nAsp; asp++) {
    if (asp == aOpp || !FAcceptAspect(i, asp, j))
      continue;
    rDiff = rPct -
      (rAspAngle[ASPT(asp == aCon ? aOpp : asp)] / rDegHalf * 100.0);
    rOrb = GetOrb(i, j, asp);

    // If -ga switch in effect, then change the sign of the orb to correspond
    // to whether the aspect is applying or separating. To do this, check the
    // distance velocity vectors to see if the planets are moving toward,
    // away, or are overtaking each other.

    if (us.nAppSep == 1) {
      if (FCmSwissAny()) {
        retlen1a = us.nRel > rcTransit ? retlen1[i] : 0.0;
        rDiff *= RSgn2(retlen2[j]-retlen1a);
      } else {
        // If no distance velocity, make aspect separating.
        rDiff = RAbs(rDiff);
      }
    } else if (us.nAppSep == 2) {
      // "Waxing" is if nearer body applying, "waning" if nearer separating.
      rDiff = RAbs(rDiff) *
        (dist1 <= dist2 ? RSgn2(retlen1[i]) : RSgn2(retlen2[j]));
    }

#ifdef EXPRESS
    // Adjust orb with AstroExpression, if one defined.
    if (!us.fExpOff && FSzSet(us.szExpAsp)) {
      ExpSetN(iLetterV, i);
      ExpSetN(iLetterW, -asp);
      ExpSetN(iLetterX, j);
      ExpSetR(iLetterY, rDiff);
      ExpSetR(iLetterZ, rOrb);
      ParseExpression(us.szExpAsp);
      rDiff = RExpGet(iLetterY);
      rOrb  = RExpGet(iLetterZ);
    }
#endif

    // If distance aspect within orb, return it.
    if (RAbs(rDiff) < rOrb) {
      if (prOrb != NULL)
        *prOrb = rDiff;
      return asp;
    }
  }
  return 0;
}


// Fill in the aspect grid based on the aspects taking place among the planets
// in the present chart. Also fill in the midpoint grid.

flag FCreateGrid(flag fFlip)
{
  int x, y, k, asp;
  real l, rOrb, rT;

  if (!FEnsureGrid())
    return fFalse;
  ClearB((pbyte)grid, sizeof(GridInfo));

  for (y = 0; y <= is.nObj; y++) if (!FIgnore(y))
    for (x = 0; x <= is.nObj; x++) if (!FIgnore(x)) {

      // The parameter 'flip' determines what half of the grid is filled in
      // with the aspects and what half is filled in with the midpoints.

      if (fFlip ? x > y : x < y) {
        if (us.fParallel)
          asp = GetParallel(planet, planet, planetalt, planetalt,
            ret, ret, retalt, retalt, x, y, &rOrb);
        else if (us.fDistance)
          asp = GetDistance(space, space, retlen, retlen, x, y, &rOrb);
        else
          asp = GetAspect(planet, planet, planetalt, planetalt,
            ret, ret, x, y, &rOrb);
        grid->n[x][y] = asp;
        grid->v[x][y] = asp > 0 ? rOrb : 0.0;
      } else if (fFlip ? x < y : x > y) {
        // Calculate midpoint in 2D or 3D.
        if (!us.fHouse3D)
          l = Midpoint2(planet[x], planet[y], us.rRatio);
        else
          SphRatio(planet[x], planetalt[x], planet[y], planetalt[y],
            us.rRatio, &l, &rT);
        k = SFromZ(l);
        grid->n[x][y] = k;
        grid->v[x][y] = l - (real)((k-1)*30);
      } else {
        l = planet[y];
        k = SFromZ(l);
        grid->n[x][y] = k;
        grid->v[x][y] = l - (real)((k-1)*30);
      }
    }
  return fTrue;
}


// This is similar to the previous function; however, this time fill in the
// grid based on the aspects (or midpoints if 'fMidpoint' set) taking place
// among the planets in two different charts, as in the -g -r0 combination.

flag FCreateGridRelation(flag fMidpoint)
{
  int x, y, k, asp;
  real l, rOrb, rT;

  if (!FEnsureGrid())
    return fFalse;
  ClearB((pbyte)grid, sizeof(GridInfo));

  for (y = 0; y <= is.nObj; y++) if (!FIgnore(y) || !FIgnore2(y))
    for (x = 0; x <= is.nObj; x++) if (!FIgnore(x) || !FIgnore2(x)) {
      if (!fMidpoint) {
        // Aspect grids are represented using x,y coordinates, however note
        // that relationship aspect grids have chart #1 down the Y axis.
        if (us.fParallel)
          asp = GetParallel(cp1.obj, cp2.obj, cp1.alt, cp2.alt,
            cp1.dir, cp2.dir, cp1.diralt, cp2.diralt, y, x, &rOrb);
        else if (us.fDistance)
          asp = GetDistance(cp1.pt, cp2.pt, cp1.dirlen, cp2.dirlen,
            y, x, &rOrb);
        else
          asp = GetAspect(cp1.obj, cp2.obj, cp1.alt, cp2.alt,
            cp1.dir, cp2.dir, y, x, &rOrb);
        grid->n[x][y] = asp;
        grid->v[x][y] = asp > 0 ? rOrb : 0.0;
      } else {
        // Calculate midpoint in 2D or 3D.
        if (!us.fHouse3D)
          l = Midpoint2(cp1.obj[x], cp2.obj[y], us.rRatio);
        else
          SphRatio(cp1.obj[x], cp1.alt[x], cp2.obj[y], cp2.alt[y],
            us.rRatio, &l, &rT);
        k = SFromZ(l);
        grid->n[x][y] = k;
        grid->v[x][y] = l - (real)((k-1)*30);
      }
    }
  return fTrue;
}


// Check whether a solar eclipse is taking place anywhere upon the Earth (as
// opposed to visible from a particular location) and if so by what percentage
// of distance overlap. Detects partial, annular, and total solar eclipses.

int NCheckEclipseSolar(int iEar, int iMoo, int iSun, real *prPct)
{
  PT3R pSun, pMoo, pEar, pNear, pUmb, vS2M, vS2E, vE2U, vS2Mu, vS2N;
  real radiS, radiE, radiM, radiU, radiP, lNear, lSM, lSU, lSE, lSN, lEU, dot;

  // Objects must be different, and within rObjDiam[]'s oNorm bound --
  // stars have no size on record (same guard as NCheckEclipseLunar).
  if (iEar == iSun || iEar == iMoo || iMoo == iSun ||
    !FNorm(iEar) || !FNorm(iMoo) || !FNorm(iSun))
    return etUndefined;

  // Calculate radius and coordinates of the objects in km.
  radiS = rObjDiam[iSun] / 2.0;
  radiE = rObjDiam[iEar] / 2.0;
  radiM = rObjDiam[iMoo] / 2.0;

  pSun = space[iSun]; pMoo = space[iMoo]; pEar = space[iEar];
  PtMul(pSun, rAUToKm);
  PtMul(pMoo, rAUToKm);
  PtMul(pEar, rAUToKm);

  // Determine point along Sun/Moon ray nearest the center of Earth.
  PtVec(vS2M, pSun, pMoo);
  PtVec(vS2E, pSun, pEar);
  dot = PtDot(vS2E, vS2M) / PtDot(vS2M, vS2M);
  vS2N = vS2M; PtMul(vS2N, dot);
  pNear = pSun; PtAdd2(pNear, vS2N);
  PtSub2(pNear, pEar); lNear = PtLen(pNear);

  // Determine point of maximum extent of Moon's umbra shadow.
  lSM = PtLen(vS2M);
  lSU = lSM * radiS / (radiS - radiM);
  vS2Mu = vS2M; PtDiv(vS2Mu, lSM);
  pUmb = vS2Mu; PtMul(pUmb, lSU); PtAdd2(pUmb, pSun);
  PtVec(vE2U, pEar, pUmb);
  lSE = PtLen(vS2E);
  lEU = PtLen(vE2U);
  lSN = PtLen(vS2N);
  radiU = radiS * (lSU - lSN) / lSU;
  if (radiU < 0.0)
    radiU = 0.0;

  // If Sun/Moon ray intersects Earth, must be an annular or total eclipse.
  if (lNear - radiU < radiE) {
    if (prPct != NULL)
      *prPct = 100.0;
    if (lSU < lSE && lEU > radiE)
      return etAnnular;
    return etTotal;
  }

  // Check if Earth intersects penumbra shadow, for a partial solar eclipse.
  radiP = (radiS + radiM) / lSM * lSN - radiS;
  if (lNear - radiE < radiP && radiM > 0.0) {
    if (prPct != NULL)
      *prPct = 100.0 - (lNear - radiE) / radiP * 100.0;
    return etPartial;
  }
  return etNone;
}


// Check whether one planet's disk is overlapping another, and if so by what
// percentage of distance overlap. Detects partial, annular, and total solar
// eclipses, along with transits and occulations of other bodies.

int NCheckEclipse(int obj1, int obj2, real *prPct)
{
  real radi0, radi1, radi2, len1, len2, angDiff, ang1, ang2, ang1T, ang2T;

  // Objects that aren't actual things in space can't eclipse or be eclipsed.
  if (!FThing(obj1) || !FThing(obj2))
    return etUndefined;

  // Calculate radius of the two objects in km.
  radi1 = RObjDiam(obj1) / 2.0;
  radi2 = RObjDiam(obj2) / 2.0;
  if (radi1 <= 0.0 && radi2 <= 0.0)
    return etNone;

  // Special check if solar eclipse allowed to happen anywhere on Earth.
  if (us.fEclipseAny && obj1 == oSun && (obj2 == oMoo || FMoons(obj2)) &&
    (us.fMoonMove || ObjOrbit(obj2) == us.objCenter))
    return NCheckEclipseSolar(us.objCenter, obj2, oSun, prPct);

  // Calculate angular distance between center points of the two objects.
  angDiff = SphDistance(planet[obj1], planetalt[obj1],
    planet[obj2], planetalt[obj2]);
  if (us.objCenter == oEar && angDiff > (!us.fEclipseAny ? 0.75 : 1.5))
    return etNone;

  // Calculate angular size in the sky spanned by the two objects.
  len1 = PtLen(space[obj1]) * rAUToKm;
  len2 = PtLen(space[obj2]) * rAUToKm;
  ang1 = RAtnD(radi1 / len1);
  ang2 = RAtnD(radi2 / len2);
  if (us.fEclipseAny) {
    // Shrink angular distance by best case topocentric offset.
    radi0 = RObjDiam(us.objCenter) / 2.0;
    ang1T = RAtnD(radi0 / len1);
    ang2T = RAtnD(radi0 / len2);
    angDiff -= RAbs(ang2T - ang1T);
    if (angDiff < 0.0)
      angDiff = 0.0;
  }
  if (ang1 + ang2 <= angDiff)
    return etNone;

  // Compare angular sizes to distance, to see how much overlap there is.
  if (prPct != NULL)
    *prPct = ang1 == ang2 ? 100.0 :
      100.0 - angDiff / RAbs(ang2 - ang1) * 100.0;
  if (ang1 >= ang2 + angDiff)
    return len1 - radi1 >= len2 + radi2 ? etAnnular : etTotal;
  else if (ang2 >= ang1 + angDiff)
    return len2 - radi2 >= len1 + radi1 ? etAnnular : etTotal;
  if (prPct != NULL)
    *prPct = 100.0 -
      (angDiff - RAbs(ang2 - ang1)) / (Min(ang1, ang2) * 2.0) * 100.0;
  return etPartial;
}


// Check whether a lunar eclipse is taking place as seen from the Earth (or
// other planet) and if so by what percentage of distance overlap. Detects
// penumbral, total penumbral, partial, and total lunar eclipses.

int NCheckEclipseLunar(int iEar, int iMoo, int iSun, real *prPct)
{
  real radiS, radiE, radiM, radiU, radiP, lenS, lenM,
    angDiff, angM, angU, angP, theta;

  // Objects must be different.
  if (iEar == iSun || iEar == iMoo || iMoo == iSun)
    return etUndefined;

  // Objects that aren't actual things in space can't eclipse or be eclipsed.
  if (!FThing(iEar) || !FThing(iMoo) || !FThing(iSun))
    return etUndefined;

  // Stars pass FThing() but sit above rObjDiam[]'s oNorm bound -- the
  // eclipse model only knows solar system bodies with sizes on record.
  if (!FNorm(iEar) || !FNorm(iMoo) || !FNorm(iSun))
    return etUndefined;

  // Calculate angular distance between the Moon and point opposite the Sun.
  angDiff = SphDistance(Mod(planet[iSun] + rDegHalf), -planetalt[iSun],
    planet[iMoo], planetalt[iMoo]);
  if (angDiff > (iEar == oEar ? 2.0 : 18.0))
    return etNone;

  // Calculate radius of the Sun, Earth, and Moon in km.
  radiS = rObjDiam[iSun] / 2.0;
  radiE = rObjDiam[iEar] / 2.0;
  radiM = rObjDiam[iMoo] / 2.0;
  lenS = PtLen(space[iSun]) * rAUToKm;
  lenM = PtLen(space[iMoo]) * rAUToKm;

  //radiU = (radiE - radiS) / lenS * (lenS + lenM) + radiS;
  //radiP = (radiS + radiE) / lenS * (lenS + lenM) - radiS;
  theta = RAsinD((radiS - radiE) / lenS);
  radiU = radiE - lenM * RTanD(theta);
  theta = RAsinD((radiE + radiS) / lenS);
  radiP = (lenS + lenM) * RTanD(theta) - radiS;

  // Calculate angular size in sky of Moon, and Earth's umbra and penumbra.
  angM = RAtnD(radiM / lenM);
  angU = RAtnD(radiU / lenM);
  angP = RAtnD(radiP / lenM);

  // Compare angular sizes to distance, to see how much overlap there is.
  if (angDiff - angM >= angP)
    return etNone;
  else if (angDiff + angM <= angU) {
    if (prPct != NULL)
      *prPct = 100.0 - angDiff / (angU - angM) * 100.0;
    return etTotal;
  } else if (angDiff - angM < angU) {
    if (prPct != NULL)
      *prPct = 100.0 - (angDiff - (angU - angM)) / (angM * 2.0) * 100.0;
    return etPartial;
  } else if (angDiff + angM <= angP) {
    if (prPct != NULL)
      *prPct = (angP - angDiff - angM) / (angP - (angU + angM * 2.0)) * 100.0;
    return etPenumbra2;
  }
  if (prPct != NULL)
    *prPct = 100.0 - (angDiff - (angP - angM)) / (angM * 2.0) * 100.0;
  return etPenumbra;
}


// Check whether an aspect between two objects is also an eclipse. Basically
// a wrapper around NCheckEclipse() and NCheckEclipseLunar().

int NCheckEclipseAny(int obj1, int asp, int obj2, real *prEclipse)
{
  int nEclipse = etUndefined;
  real rEclipse = 0.0;

  if (us.fEclipse && !us.fParallel) {
    if (asp == aCon)
      nEclipse = NCheckEclipse(obj1, obj2, &rEclipse);
    else if (asp == aOpp)
      nEclipse = NCheckEclipseLunar(us.objCenter, obj2, obj1, &rEclipse);
  }
  if (prEclipse != NULL)
    *prEclipse = rEclipse;
  return nEclipse;
}


#ifdef SWISS
// Check whether a solar eclipse is taking place at a particular location upon
// the Earth. Detects partial, annular, and total solar eclipses. Called from
// BmpDarkenKv() to darken parts on the globe that are under a solar eclipse.

int NCheckEclipseSolarLoc(real lon, real lat, real *prPct)
{
  CI ciSav;
  PT3R ptSav[oMoo+1], ptDiff;
  real obj[oMoo+1], alt[oMoo+1], r1, r2, r3, r4, r5, r6;
  int i, et = etUndefined;
  flag fSav1, fSav2;

  // Save existing chart settings to be restored later.
  ciSav = ciCore;
  fSav1 = us.fEclipseAny; us.fEclipseAny = fFalse;
  fSav2 = us.fTopoPos; us.fTopoPos = 2;
  for (i = oSun; i <= oMoo; i++) {
    ptSav[i] = space[i];
    obj[i] = planet[i]; alt[i] = planetalt[i];
  }

  // Compute the topocentric position of the Sun at a particular location.
  i = oSun;
  OO = lon; AA = lat;
  if (!FSwissPlanet(i, JulianDayFromTime(is.T), us.objCenter,
    &r1, &r2, &r3, &r4, &r5, &r6))
    goto LDone;
  planet[i] = Mod(r1 + is.rSid); planetalt[i] = r2;
  SphToRec(r4, planet[i], planetalt[i],
    &space[i].x, &space[i].y, &space[i].z);

  // For the Moon, for speed just apply the vector between the Sun positions.
  i = oMoo;
  ptDiff = space[oSun]; PtSub2(ptDiff, ptSav[oSun]);
  PtAdd2(space[i], ptDiff);
  RecToSph3(space[i].x, space[i].y, space[i].z, &planet[i], &planetalt[i]);

  et = NCheckEclipse(oSun, oMoo, prPct);

LDone:
  // Restore chart settings saved earlier.
  ciCore = ciSav;
  us.fEclipseAny = fSav1;
  us.fTopoPos = fSav2;
  for (i = oSun; i <= oMoo; i++) {
    planet[i] = obj[i]; planetalt[i] = alt[i];
    space[i] = ptSav[i];
  }
  return et;
}
#endif


/*
******************************************************************************
** Other Calculations.
******************************************************************************
*/

// Fill out tables based on the number of unrestricted planets in signs by
// element, signs by mode, as well as other values such as the number of
// objects in yang vs. yin signs, in various house hemispheres (north/south
// and east/west), and the number in first six signs vs. second six signs.
// This is used by the -v chart listing and the sidebar in graphics charts.

void CreateElemTable(ET *pet)
{
  int i, s;

  ClearB((pbyte)pet, sizeof(ET));
  for (i = 0; i <= is.nObj; i++) if (!FIgnore(i)) {
    pet->coSum++;
    s = SFromZ(planet[i]);
    pet->coSign[s-1]++;
    pet->coElemMode[(s-1)&3][(s-1)%3]++;
    pet->coElem[(s-1)&3]++; pet->coMode[(s-1)%3]++;
    pet->coYang += FOdd(s);
    pet->coLearn += (s < sLib);
    if (!FCusp(i)) {
      pet->coHemi++;
      s = inhouse[i];
      pet->coHouse[s-1]++;
      pet->coModeH[(s-1)%3]++;
      pet->coMC += (s >= sLib);
      pet->coAsc += (s < sCan || s >= sCap);
    }
  }
  pet->coYin   = pet->coSum  - pet->coYang;
  pet->coShare = pet->coSum  - pet->coLearn;
  pet->coDes   = pet->coHemi - pet->coAsc;
  pet->coIC    = pet->coHemi - pet->coMC;
}


/*
******************************************************************************
** Swiss Ephemeris Calculations.
******************************************************************************
*/

#ifdef SWISS
// On some systems "ret" is defined within system headers.
#undef ret
#include "swephexp.h"
#include "swephlib.h"
#define ret cp0.dir

// Set up path for Swiss Ephemeris to search in for ephemeris files.

void SwissEnsurePath()
{
  char szPath[AS_MAXCH], *pch;
#ifdef ENVIRON
  char szExe[cchSzMax], szT[cchSzMax], *env;
#endif
  int i;

  if (is.fSwissPathSet)
    return;

  // Get directory containing Astrolog executable.
#ifdef WIN
  GetModuleFileName(wi.hinst, szExe, cchSzMax);
#else
  sprintf2(S(szExe), "%s", is.szProgName != NULL ? is.szProgName : "");
#endif
  for (pch = szExe; *pch; pch++)
    ;
  while (pch > szExe && *pch != chDirSep)
    pch--;
  if (*pch == chDirSep)
    pch++;
  *pch = chNull;

  // First look for the file in the current directory, and that of executable.
  sprintf2(S(szPath), ".%s%.*s", PATH_SEPARATOR,
    (int)sizeof(szPath) - 4, szExe);
  // Next look in the directories indicated by the -Yi switch.
  for (i = 0; i < 10; i++) {
    pch = us.rgszPath[i];
    if (FSzSet(pch)) {
      if ((FCapCh(*pch) || FUncapCh(*pch) || FNumCh(*pch)) &&
        !(pch[1] == ':')) {
        // If dir is relative path, then prepend the path to executable.
        sprintf2(S(szT), "%s", szExe);
        for (pch = szT; *pch; pch++)
          ;
      } else
        pch = szT;
      sprintf2(SO(pch, szT), "%s", us.rgszPath[i]);
      sprintf2(SO(szPath + CchSz(szPath), szPath), "%s%s", PATH_SEPARATOR,
        szT);
    }
  }
#ifdef ENVIRON
  // Next look for the file in the directory indicated by the version
  // specific system environment variable.
  sprintf2(S(szT), "%s%s", ENVIRONVER, szVersionCore);
  for (pch = szT; *pch && *pch != '.'; pch++)
    ;
  while (*pch && (*pch = pch[1]) != chNull)
    pch++;
  env = getenv(szT);
  if (FSzSet(env))
    sprintf2(SO(szPath + CchSz(szPath), szPath), "%s%s", PATH_SEPARATOR, env);
  // Next look in the directory in the general environment variable.
  env = getenv(ENVIRONALL);
  if (FSzSet(env))
    sprintf2(SO(szPath + CchSz(szPath), szPath), "%s%s", PATH_SEPARATOR, env);
  // Next look in the directory in the version prefix environment variable.
  env = getenv(ENVIRONVER);
  if (FSzSet(env))
    sprintf2(SO(szPath + CchSz(szPath), szPath), "%s%s", PATH_SEPARATOR, env);
#endif
  // Finally look in a directory specified at compile time.
  pch = EPHE_DIR;
  if (FSzSet(pch))
    sprintf2(SO(szPath + CchSz(szPath), szPath), "%s%s", PATH_SEPARATOR,
      EPHE_DIR);
  if (CchSz(szPath) >= (int)sizeof(szPath)-1) {
    sprintf2(S(szT),
      "Swiss Ephemeris file path longer than %d characters, so truncated.",
      (int)sizeof(szPath)-1);
    PrintWarning(szT);
  }
  swe_set_ephe_path(szPath);
  is.fSwissPathSet = fTrue;
}


CONST int rgObjSwissDef[cCust] = {SE_VULCAN - SE_FICT_OFFSET_1,
  SE_CUPIDO   - SE_FICT_OFFSET_1, SE_HADES    - SE_FICT_OFFSET_1,
  SE_ZEUS     - SE_FICT_OFFSET_1, SE_KRONOS   - SE_FICT_OFFSET_1,
  SE_APOLLON  - SE_FICT_OFFSET_1, SE_ADMETOS  - SE_FICT_OFFSET_1,
  SE_VULKANUS - SE_FICT_OFFSET_1, SE_POSEIDON - SE_FICT_OFFSET_1,
  10/*Hygiea*/, oPho, 136199/*Eris*/,
  136108/*Haumea*/, 136472/*Makemake*/, 225088/*Gonggong*/,
  50000/*Quaoar*/, 90377/*Sedna*/, 90482/*Orcus*/,
  401, 402,
  503, 504, 501, 502,
  606, 605, 608, 604, 603, 602, 601, 607,
  703, 704, 702, 701, 705,
  801, 808, 802,
  901, 903, 902, 904, 905,
  599, 699, 799, 899, 999};
CONST int rgTypSwissDef[cCust] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0,
  1, 2, 1, 1, 1, 1, 1, 1, 1,
  3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3};
int rgObjSwiss[cCust];
int rgTypSwiss[cCust];
int rgPntSwiss[cCust];
int rgFlgSwiss[cCust];

// Bodies the Object Selections dialog offers for a slot. This is a
// convenience list and not a limit: that dialog's field is editable and
// takes the same definition text Object Customization does, so anything
// -Ye<x> accepts can still be typed. Type 0 is a set of orbital elements
// in seorbel.txt, type 1 an MPC minor planet number, type 2 an Astrolog
// object index -- the same encoding rgTypSwissDef[] above uses.
//
// A body listed here still needs its ephemeris file present to compute or
// to name itself; the dialog's Lookup Names button is what reports that,
// by leaving the name as "???" when Swiss Ephemeris can't find it.
CONST OBJSEL rgObjSel[] = {
  // The nine Uranians, as their default orbital element sets.
  {0, SE_VULCAN   - SE_FICT_OFFSET_1, "Vulcan"},
  {0, SE_CUPIDO   - SE_FICT_OFFSET_1, "Cupido"},
  {0, SE_HADES    - SE_FICT_OFFSET_1, "Hades"},
  {0, SE_ZEUS     - SE_FICT_OFFSET_1, "Zeus"},
  {0, SE_KRONOS   - SE_FICT_OFFSET_1, "Kronos"},
  {0, SE_APOLLON  - SE_FICT_OFFSET_1, "Apollon"},
  {0, SE_ADMETOS  - SE_FICT_OFFSET_1, "Admetos"},
  {0, SE_VULKANUS - SE_FICT_OFFSET_1, "Vulkanus"},
  {0, SE_POSEIDON - SE_FICT_OFFSET_1, "Poseidon"},

  // The dwarf planets and larger trans Neptunians, as the defaults have
  // them. Pholus is type 2 here because rgTypSwissDef[] makes it so, and
  // that definition needs no ephemeris file; 5145 can still be typed.
  {1,     10, "Hygiea"},
  {2,   oPho, "Pholus"},
  {1, 136199, "Eris"},
  {1, 136108, "Haumea"},
  {1, 136472, "Makemake"},
  {1, 225088, "Gonggong"},
  {1,  50000, "Quaoar"},
  {1,  90377, "Sedna"},
  {1,  90482, "Orcus"},
  {1, 120347, "Salacia"},

  // Centaurs.
  {1,   2060, "Chiron"},
  {1,   7066, "Nessus"},
  {1,   8405, "Asbolus"},
  {1,  10199, "Chariklo"},
  {1,  10370, "Hylonome"},

  // Other trans Neptunians.
  {1,  20000, "Varuna"},
  {1,  28978, "Ixion"},
  {1,  38628, "Huya"},
  {1,  42355, "Typhon"},

  // Main belt asteroids often wanted in a chart.
  {1,      5, "Astraea"},
  {1,      6, "Hebe"},
  {1,      7, "Iris"},
  {1,      8, "Flora"},
  {1,    433, "Eros"},
  {1,    944, "Hidalgo"},
  {1,   1181, "Lilith"},
  {1,   1566, "Icarus"},
  {1,   1862, "Apollo"},
  {1,   2062, "Aten"},
  {1,   3200, "Phaethon"}};
CONST int cObjSel = (int)(sizeof(rgObjSel) / sizeof(OBJSEL));

// Names this session has seen the ephemeris give for a body number.
//
// There is no name-to-number catalog anywhere: a .se1 file maps its own
// number to a name and nothing maps back, and there are on the order of
// 800,000 of those files, so enumerating them is not an option. What is
// cheap is remembering what has already been resolved. Once the program
// has shown someone that 52872 is Okyrhoe, typing "Okyrhoe" has to work --
// displaying a name and then refusing it is indefensible.
//
// A fixed table rather than an allocated one, deliberately: this is a
// cache, it should never grow without bound, and it must not show up in
// the unfreed-allocation count at exit.

#define cObjSelSeen 256

static struct {
  int nTyp;
  int nObj;
  char szName[cchSzDef];
} rgObjSelSeen[cObjSelSeen];
static int cObjSelSeenUsed = 0;

void ObjSelRemember(int nTyp, int nObj, CONST char *szName)
{
  int i;

  if (szName == NULL || !*szName || FEqSz(szName, szObjUnknown))
    return;
  for (i = 0; i < cObjSelSeenUsed; i++)
    if (rgObjSelSeen[i].nTyp == nTyp && rgObjSelSeen[i].nObj == nObj)
      return;
  if (cObjSelSeenUsed >= cObjSelSeen)
    return;
  rgObjSelSeen[cObjSelSeenUsed].nTyp = nTyp;
  rgObjSelSeen[cObjSelSeenUsed].nObj = nObj;
  sprintf(rgObjSelSeen[cObjSelSeenUsed].szName, "%s", szName);
  cObjSelSeenUsed++;
}


// The other direction: a name this session has been shown, back to the
// body it named. Returns false if it has never been seen.

flag FObjSelRecall(CONST char *szName, int *pnTyp, int *pnObj)
{
  int i;

  for (i = 0; i < cObjSelSeenUsed; i++)
    if (FEqSzI(szName, rgObjSelSeen[i].szName)) {
      *pnTyp = rgObjSelSeen[i].nTyp;
      *pnObj = rgObjSelSeen[i].nObj;
      return fTrue;
    }
  return fFalse;
}


// Resolve a body definition to the name it should display, the same way the
// -Ye switch handler does after setting one (astrolog.cpp). Returns
// szObjUnknown when the ephemeris can't identify it, which for an asteroid
// usually means its file isn't installed rather than that the number is
// wrong.

void SzObjSelName(char *sz, int cchMax, int nTyp, int nObj)
{
  if (nTyp <= 1)
    SwissGetObjName(sz, cchMax, nTyp <= 0 ? -nObj : nObj);
  else if (nTyp >= 5)
    sprintf2(sz, cchMax, "%s",
      FValidPart(nObj) ? ai[nObj-1].name : szObjUnknown);
  else if (nTyp == 4) {
    // A JPL Horizons body, named by asking JPL -- over the network,
    // synchronously, the same way Windows' DlgCustom always has. Before
    // this case existed, a "j" definition fell into the object-index
    // branch below and a Lookup Names on "j2" answered "Moon".
#ifdef JPLWEB
    real rT;
    if (!GetJPLHorizons(nObj, &rT, &rT, &rT, &rT, &rT, &rT, sz))
#endif
      sprintf2(sz, cchMax, "%s", szObjUnknown);
  } else {
    if (nTyp == 3)
      nObj = ObjMoons(nObj);
    sprintf2(sz, cchMax, "%s", FItem(nObj) ? szObjName[nObj] : szObjUnknown);
  }
  // Whatever came back, remember it, so the name just shown is a name the
  // program will accept back.
  ObjSelRemember(nTyp, nObj, sz);
}


// Format what a slot's body field should show: one of rgObjSel[]'s names
// when the slot holds exactly that body, otherwise the definition text
// Object Customization uses, including any point and flag suffix. The
// suffix case never takes the name shortcut, so that a slot carrying one
// round trips through the field unchanged rather than being silently
// stripped back to a plain body.

// Read a custom slot's definition out of the four parallel arrays that
// store it. The one place that knows a definition is spread over
// rgTypSwiss/rgObjSwiss/rgPntSwiss/rgFlgSwiss[obj - custLo]; everything
// else handles an OBJDEF.

void ObjDefGet(int obj, OBJDEF *pod)
{
  pod->nTyp = rgTypSwiss[obj - custLo];
  pod->nObj = rgObjSwiss[obj - custLo];
  pod->nPnt = rgPntSwiss[obj - custLo];
  pod->nFlg = rgFlgSwiss[obj - custLo];
}


// Put a definition into a custom slot: the four arrays, plus the one side
// effect every write path used to carry for itself -- a slot pointed at a
// different body, or at a different point of one, drops the old body's
// glyph and draws its name instead (a north node is not the planet, so
// nPnt is part of the identity; the calculation flags are not). The
// display name is deliberately NOT set here: -Ye always renames, the
// dialogs rename only a name the user left alone, so name policy stays
// with the caller. This replaces five separate store sites, three of
// which had already grown their own copies of the glyph rule.

void ObjDefSet(int obj, CONST OBJDEF *pod)
{
  if (pod->nTyp != rgTypSwiss[obj - custLo] ||
    pod->nObj != rgObjSwiss[obj - custLo] ||
    pod->nPnt != rgPntSwiss[obj - custLo])
    SetObjGlyphNoneCore(obj);
  rgTypSwiss[obj - custLo] = pod->nTyp;
  rgObjSwiss[obj - custLo] = pod->nObj;
  rgPntSwiss[obj - custLo] = pod->nPnt;
  rgFlgSwiss[obj - custLo] = pod->nFlg;
}


// Is this custom slot redefined to be the center of body of the given
// planet -- the "99" planetary-moon ephemeris entry -- and not
// restricted? Open coded five times before: four wheels in xcharts1.cpp
// with the moon number as a literal, and RObjDiam() with it computed.

flag FObjIsCOBOf(int obj, int objPla)
{
  return FCust(obj) && !ignore[obj] && rgTypSwiss[obj - custLo] == 3 &&
    rgObjSwiss[obj - custLo] == (objPla - oJup + 5)*100 + 99;
}


// Format a definition as the text Object Customization shows and every
// definition field accepts: "h120", "Mar", "2 n", "j2 nHS". The inverse
// of FObjDefParse(), and like the parse it used to exist in several
// places -- here, and open coded in both Custom Objects dialogs.

void SzObjDefFormat(char *sz, int cchMax, CONST OBJDEF *pod)
{
  char *pch;

  if (pod->nTyp != 2)
    sprintf2(sz, cchMax, "%s%d", pod->nTyp <= 0 ? "h" :
      (pod->nTyp == 1 ? "" : (pod->nTyp == 3 ? "m" :
      (pod->nTyp == 4 ? "j" : "A"))), pod->nObj);
  else
    sprintf2(sz, cchMax, pod->nObj < cobLo ? "%.3s" : "%.4s",
      szObjName[pod->nObj]);
  for (pch = sz; *pch; pch++)
    ;
  if (pod->nPnt > 0 || pod->nFlg > 0)
    *pch++ = ' ';
  if (pod->nPnt > 0)
    *pch++ = (pod->nPnt == 1 ? 'n' : (pod->nPnt == 2 ? 's' :
      (pod->nPnt == 3 ? 'p' : 'a')));
  if (pod->nFlg &  1) *pch++ = 'H';
  if (pod->nFlg &  2) *pch++ = 'S';
  if (pod->nFlg &  4) *pch++ = 'B';
  if (pod->nFlg &  8) *pch++ = 'N';
  if (pod->nFlg & 16) *pch++ = 'T';
  if (pod->nFlg & 32) *pch++ = 'V';
  *pch = chNull;
}


void SzObjSelDef(char *sz, int cchMax, int iobj)
{
  OBJDEF od;
  int i;

  ObjDefGet(iobj, &od);
  if (od.nPnt <= 0 && od.nFlg <= 0)
    for (i = 0; i < cObjSel; i++)
      if (rgObjSel[i].nTyp == od.nTyp && rgObjSel[i].nObj == od.nObj) {
        sprintf2(sz, cchMax, "%s", rgObjSel[i].szName);
        return;
      }
  SzObjDefFormat(sz, cchMax, &od);
}


// Parse a body field back. Accepts either a name from rgObjSel[] or the
// definition text Object Customization uses. Returns false only for an
// empty field; the caller still has to check FValidCustom() on the result,
// as the dialogs do.
//
// The definition half is Windows' own parse from DlgCustom, moved here so
// there is one copy rather than the three this program used to carry. Its
// "if (pch > sz)" guard matters: without it an all alphabetic definition
// reads its own letters as flags, so "Ven" would set the node flag off its
// own 'n'.

// Whether some object's forced midpoint reads this object's position.
//
// A midpoint set with -Fm is computed in CastChart() from planet[] of its
// two sources, and ComputeEphem() skips computing any restricted object
// above the Moon. So a midpoint of two restricted objects was quietly
// built from whatever those slots happened to hold -- stale values from a
// previous chart, or zero -- and produced a plausible looking position
// that was simply wrong. Nothing said so.
//
// Restricting an object hides it; it should not silently corrupt a
// midpoint that depends on it. Sources are therefore computed regardless,
// and stay hidden, because ignore[] still governs what is drawn.

flag FObjMidSource(int iObj)
{
  int i;

  for (i = 0; i <= cObj; i++)
    if (FForceMid(force[i]) &&
      (ObjForceMid1(force[i]) == iObj || ObjForceMid2(force[i]) == iObj))
      return fTrue;
  return fFalse;
}


// Resolve one midpoint operand to an object index, or -1 for none.
//
// -Fm stores object indexes, so that is what this has to produce -- but an
// index is not what a user has in front of them. Four things all name the
// same body and all now work:
//
//   Sun       a stock object name, which NParseSz() already knew
//   34        an object index, likewise
//   Nessus    the *display* name of a slot. NParseSz() matches szObjName[]
//             only, so a slot renamed by -YD was rejected even though the
//             dialog shows that very name in its own Name column.
//   7066      the ephemeris number of a body, resolved to whichever slot
//             holds it. There is no index for a body that occupies no slot,
//             which is a real limit of -Fm and not something a dialog can
//             paper over -- assign it to a slot first.
//
// A plain number is read as an object index before it is read as an
// ephemeris number, since that is what -Fm itself means by one. The two
// only overlap below 134, where the index reading is the useful one.

// Split a "Contains" field. A midpoint is written the way an astrologer
// writes one -- two bodies with a slash, "Sun/Moo", "Nessus/Orcus",
// "7066/90482" -- and each half goes through NObjSelMid(), so either side
// can be a name, an index, a slot's display name or an ephemeris number.
//
// Returns true and fills both indexes if the field is a midpoint; false if
// it holds no slash, in which case the caller reads it as a body.

flag FObjSelMidPair(CONST char *szIn, int *pn1, int *pn2)
{
  char sz[cchSzMax], *pch;

  sprintf2(S(sz), "%s", szIn);
  for (pch = sz; *pch && *pch != '/'; pch++)
    ;
  if (*pch != '/')
    return fFalse;
  *pch++ = chNull;
  *pn1 = NObjSelMid(sz);
  *pn2 = NObjSelMid(pch);
  return fTrue;
}


int NObjSelMid(CONST char *szIn)
{
  int i, n;

  while (*szIn && *szIn <= ' ')
    szIn++;
  if (!*szIn || FEqSz(szIn, szObjSelNone))
    return -1;

  n = NParseSz(szIn, pmObject);
  if (FItem(n))
    return n;

  for (i = 0; i <= cObj; i++)
    if (szObjDisp[i] != NULL && FEqSz(szIn, szObjDisp[i]))
      return i;

  n = NFromSz(szIn);
  if (n > 0)
    for (i = custLo; i <= custHi; i++)
      if (rgTypSwiss[i - custLo] == 1 && rgObjSwiss[i - custLo] == n)
        return i;
  return -1;
}


// Is this trailing run of letters a point/flag suffix, rather than a
// body's name? Only if every letter in it is one of the suffix letters.
//
// The body field may carry the name beside the number -- "10199
// Chariklo" -- which Lookup Names writes there so a looked up row stops
// reading as a bare catalogue number. Without this check that name is
// read as a run of flags: "Chariklo" alone would set the apsis marker
// off its own 'a'.

flag FObjSelFlagRun(CONST char *szRun)
{
  CONST char *szFlag = "nspaHSBNTV", *pch, *pchT;

  if (*szRun == chNull)
    return fFalse;
  for (pch = szRun; *pch; pch++) {
    for (pchT = szFlag; *pchT && *pchT != *pch; pchT++)
      ;
    if (*pchT == chNull)
      return fFalse;
  }
  return fTrue;
}


// Parse the definition half of a body field -- "h120", "Mar", "2 n",
// "j2 nHS" -- into an OBJDEF. No name lookup: FObjSelParse() does that
// first and calls here for the rest.
//
// This is Windows' own parse from DlgCustom, and this is the only copy.
// There were three: here, ParseCustomDefQt() in qtdialog.cpp, and open
// coded inside DlgCustom itself. Three copies of a parse that reads
// trailing letters as flags is three places for the same subtle fault,
// and they had already drifted -- only this one had the FObjSelFlagRun()
// guard that stops a body's name being read as a run of flags.

flag FObjDefParse(CONST char *szIn, OBJDEF *pod)
{
  char sz[cchSzMax], *pch, *pchEnd;
  int j, k, nPnt = 0, nFlg = 0;

  while (*szIn && *szIn <= ' ')
    szIn++;
  if (!*szIn)
    return fFalse;
  sprintf2(S(sz), "%s", szIn);
  for (pch = sz; *pch; pch++)
    ;
  pchEnd = pch;
  for (pch--; pch > sz && *pch >= 'A'; pch--)
    ;
  if (pch >= sz && *pch < '0')
    *pch = chNull;
  pch = sz;
  k = (*pch == 'h' ? 0 : (*pch == 'm' ? 3 : (*pch == 'j' ? 4 :
    (*pch == 'A' ? 5 : (FNumCh(*pch) ? 1 : 2)))));
  if (k == 0 || k >= 3)
    pch++;
  j = (k == 2 ? NParseSz(pch, pmObject) : NFromSz(pch));
  for (pch = pchEnd-1; pch > sz && *pch >= 'A'; pch--)
    ;
  if (pch > sz && FObjSelFlagRun(pch+1))
    for (pch = pchEnd-1; pch > sz && *pch >= 'A'; pch--) {
      if (*pch == 'n') nPnt = 1;
      if (*pch == 's') nPnt = 2;
      if (*pch == 'p') nPnt = 3;
      if (*pch == 'a') nPnt = 4;
      if (*pch == 'H') nFlg |= 1;
      if (*pch == 'S') nFlg |= 2;
      if (*pch == 'B') nFlg |= 4;
      if (*pch == 'N') nFlg |= 8;
      if (*pch == 'T') nFlg |= 16;
      if (*pch == 'V') nFlg |= 32;
    }
  pod->nTyp = k; pod->nObj = j; pod->nPnt = nPnt; pod->nFlg = nFlg;
  return fTrue;
}


// Parse a body field back. Accepts a name from rgObjSel[], any name the
// ephemeris has already been asked about this session, or the definition
// text Object Customization uses. Returns false only for an empty field;
// the caller still has to check FValidCustom() on the result, as the
// dialogs do.

flag FObjSelParse(CONST char *szIn, OBJDEF *pod)
{
  int i;

  while (*szIn && *szIn <= ' ')
    szIn++;
  if (!*szIn)
    return fFalse;

  // A name from the list wins, and never carries a suffix.
  for (i = 0; i < cObjSel; i++)
    if (FEqSzI(szIn, rgObjSel[i].szName)) {
      pod->nTyp = rgObjSel[i].nTyp; pod->nObj = rgObjSel[i].nObj;
      pod->nPnt = pod->nFlg = 0;
      return fTrue;
    }

  // Then anything the ephemeris has already been asked about this session.
  if (FObjSelRecall(szIn, &pod->nTyp, &pod->nObj)) {
    pod->nPnt = pod->nFlg = 0;
    return fTrue;
  }
  return FObjDefParse(szIn, pod);
}


// Given an object index and a Julian Day time, get ecliptic longitude and
// latitude of the object and its velocity and distance from the Earth or
// Sun. This basically just calls the Swiss Ephemeris calculation function to
// actually do it. Because this is the one of the only routines called from
// Astrolog, this routine has knowledge of and uses both Astrolog and Swiss
// Ephemeris definitions, and does things such as translation to indices and
// formats of Swiss Ephemeris.

// Map a standard body index to its Swiss Ephemeris body index, for a
// custom slot defined to point at another object (type 2). This list
// exists once on purpose: FSwissPlanet()'s direct and central-object
// translations used to carry line-identical copies of it.

static flag FSwissFromObj(int obj, int *piobj)
{
  if (obj <= oEar)
    *piobj = SE_EARTH;
  else if (obj <= oPlu)
    *piobj = obj-1;
  else if (obj == oChi)
    *piobj = SE_CHIRON;
  else if (FBetween(obj, oCer, oVes))
    *piobj = obj - oCer + SE_CERES;
  else if (obj == oVul)
    *piobj = SE_VULCAN;
  else if (FUranian(obj))
    *piobj = obj - uranLo + SE_FICT_OFFSET_1;
  else if (obj == oPho)
    *piobj = SE_PHOLUS;
  else
    return fFalse;
  return fTrue;
}


flag FSwissPlanet(int ind, real jd, int indCent,
  real *obj, real *objalt, real *dir, real *dist, real *diralt, real *dirlen)
{
  int iobj, iobjCent, iflag, nRet, nTyp, nPnt = 0, nFlg = 0, ix;
  OBJDEF od;
  double jde, xx[6], xnasc[6], xndsc[6], xperi[6], xaphe[6], *px;
  char serr[AS_MAXCH], szErr[AS_MAXCH + cchSzDef];
  static int nSwissEph = 0;
  flag fHelio = (indCent != oEar);

  // Reset Swiss Ephemeris if changing computation method.
  if (us.nSwissEph != nSwissEph)
    is.fSwissPathSet = fFalse;  // Ensure swe_set_ephe_path() gets called.
  nSwissEph = us.nSwissEph;
  SwissEnsurePath();

  // Convert Astrolog object index to Swiss Ephemeris index.
  if (ind == oEar)
    iobj = SE_EARTH;
  else if (ind <= oPlu)
    iobj = ind-1;
  else if (ind == oChi)
    iobj = SE_CHIRON;
  else if (FBetween(ind, oCer, oVes))
    iobj = ind - oCer + SE_CERES;
  else if (ind == oNod)
    iobj = us.fTrueNode ? SE_TRUE_NODE : SE_MEAN_NODE;
  else if (ind == oSou)
    return fFalse;
  else if (ind == oLil) {
    iobj = us.fNaturalNode ? SE_INTP_APOG :
      (us.fTrueNode ? SE_OSCU_APOG : SE_MEAN_APOG);
  } else if (FCust(ind)) {
    ObjDefGet(ind, &od);
    iobj = od.nObj;
    nTyp = od.nTyp;
    nPnt = od.nPnt;
    nFlg = od.nFlg;
    if (nTyp != 2)
      iobj += (nTyp <= 0 ? SE_FICT_OFFSET_1 : (nTyp == 1 ? SE_AST_OFFSET :
        SE_PLMOON_OFFSET));
    else if (!FSwissFromObj(iobj, &iobj))
      return fFalse;
    if (nFlg > 0) {
      if (nFlg & 1)  inv(fHelio);
      if (nFlg & 2)  inv(us.fSidereal);
      if (nFlg & 4)  inv(us.fBarycenter);
      if (nFlg & 8)  inv(us.fTrueNode);
      if (nFlg & 16) inv(us.fTruePos);
      if (nFlg & 32) inv(us.fTopoPos);
    }
  } else
    iobj = ind;

  // Convert Astrolog calculation settings to Swiss Ephemeris flags.
  iflag = SEFLG_SPEED;
  iflag |= (us.nSwissEph <= 0 ? SEFLG_SWIEPH :
    (us.nSwissEph == 1 ? SEFLG_MOSEPH : SEFLG_JPLEPH));
  if (us.fSidereal) {
    swe_set_sid_mode(!us.fSidereal2 ? SE_SIDM_FAGAN_BRADLEY :
      SE_SIDBIT_SSY_PLANE, 0.0, 0.0);
    iflag |= SEFLG_SIDEREAL;
  }
  if (fHelio && !FNodal(ind))
    iflag |= (us.fBarycenter ? SEFLG_BARYCTR : SEFLG_HELCTR);
  else if (!fHelio && ind <= oSun && us.fBarycenter)
    iflag |= SEFLG_BARYCTR;
  if (us.fNoNutation)
    iflag |= SEFLG_NONUT;
  if (us.fTruePos)
    iflag |= SEFLG_TRUEPOS;
  if (us.fTopoPos && !fHelio) {
    swe_set_topo(-OO, AA, us.elvDef);
    iflag |= SEFLG_TOPOCTR;
    if (us.fTopoPos > 1)      // Special value for faster lookup.
      iflag &= ~SEFLG_SPEED;
  }

  // Compute position of planet or node/helion.
  if (jd != is.jdDeltaT) {
    is.jdDeltaT = jd;
    is.rDeltaT = swe_deltat(jd);
  }
  jde = jd + (us.rDeltaT == rInvalid ? is.rDeltaT : us.rDeltaT/86400.0);
  if (nPnt == 0) {
    if (indCent <= oSun || indCent > oNorm || FNodal(ind) || FNodal(indCent)) {
      // Normal geocentric or heliocentric position.
      nRet = swe_calc(jde, iobj, iflag, xx, serr);
    } else {
      // Alternate position orbiting an unusual central object.
      if (indCent <= oPlu)
        iobjCent = indCent-1;
      else if (indCent == oChi)
        iobjCent = SE_CHIRON;
      else if (FBetween(indCent, oCer, oVes))
        iobjCent = indCent - oCer + SE_CERES;
      else if (FCust(indCent)) {
        ObjDefGet(indCent, &od);
        iobjCent = od.nObj;
        ix = od.nTyp;
        if (ix == 4)
          iobjCent = (fHelio ? SE_SUN : SE_EARTH);
        else if (ix != 2)
          iobjCent += (ix <= 0 ? SE_FICT_OFFSET_1 : (ix == 1 ? SE_AST_OFFSET :
            SE_PLMOON_OFFSET));
        else if (!FSwissFromObj(iobjCent, &iobjCent))
          return fFalse;
      } else
        return fFalse;
      // Can happen if object customized to be a COB.
      if (iobj == iobjCent)
        return fFalse;
      nRet = swe_calc_pctr(jde, iobj, iobjCent, iflag, xx, serr);
    }
  } else {
    if (us.fNaturalNode && iobj == SE_MOON && (nPnt == 3 || nPnt == 4)) {
      // Special case to get access to SE_INTP_APOG and SE_INTP_PERG.
      nRet = swe_calc(jde, nPnt == 3 ? SE_INTP_PERG : SE_INTP_APOG, iflag, xx,
        serr);
    } else {
      // Standard case to get node or apsis position.
      nRet = swe_nod_aps(jde, iobj, iflag, us.fTrueNode ? SE_NODBIT_OSCU :
        SE_NODBIT_MEAN, xnasc, xndsc, xperi, xaphe, serr);
      switch (nPnt) {
      case 1:  px = xnasc; break;  // North node
      case 2:  px = xndsc; break;  // South node
      case 3:  px = xperi; break;  // Perihelion point
      default: px = xaphe; break;  // Aphelion point
      }
      for (ix = 0; ix < 6; ix++)
        xx[ix] = px[ix];
    }
  }

  // Clean up and return position.
  if (nFlg > 0) {
    if (nFlg & 2)  inv(us.fSidereal);
    if (nFlg & 4)  inv(us.fBarycenter);
    if (nFlg & 8)  inv(us.fTrueNode);
    if (nFlg & 16) inv(us.fTruePos);
    if (nFlg & 32) inv(us.fTopoPos);
  }
  if (nRet < 0) {
    if (!is.fNoEphFile) {
      is.fNoEphFile = fTrue;
#ifdef WIN
      sprintf(szErr, "Swiss Ephemeris returned the following error:\n\n%s",
        serr);
#else
      sprintf(szErr, "%s", serr);
#endif
      PrintWarning(szErr);
    }
    return fFalse;
  }
  *obj    = xx[0] - is.rSid + (us.fSidereal ? us.rZodiacOffset : 0.0) +
    us.rZodiacOffsetAll;
  *objalt = xx[1];
  *dist   = xx[2];
  *dir    = xx[3];
  *diralt = xx[4];
  *dirlen = xx[5];
  return fTrue;
}


// Compute house cusps and related variables like the Ascendant. Given a
// Julian Day time, location, and house system, call Swiss Ephemeris to
// compute them. This is similar to FSwissPlanet() in that it knows about
// and translates between Astrolog and Swiss Ephemeris defintions.

void SwissHouse(real jd, real lon, real lat, int housesystem, real *asc,
  real *mc, real *ra, real *vtx, real *ep, real *ob, real *off, real *nut)
{
  double cusp[cSign+1], ascmc[11], cuspr[cSign+1], ascmcr[11], rSid;
  int i;
  char serr[AS_MAXCH], ch;

  // Translate Astrolog house index to Swiss Ephemeris house character.
  // Don't do hsWhole houses ('W') yet, until after is.rSid computed.
  switch (housesystem) {
  case hsPlacidus:      ch = 'P'; break;
  case hsKoch:          ch = 'K'; break;
  case hsEqual:         ch = 'E'; break;
  case hsCampanus:      ch = 'C'; break;
  case hsMeridian:      ch = 'X'; break;
  case hsRegiomontanus: ch = 'R'; break;
  case hsPorphyry:      ch = 'O'; break;
  case hsMorinus:       ch = 'M'; break;
  case hsTopocentric:   ch = 'T'; break;
  case hsAlcabitius:    ch = 'B'; break;
  case hsKrusinski:     ch = 'U'; break;
  case hsSineRatio:     ch = 'Q'; break;
  case hsSineDelta:     ch = 'L'; break;
  case hsVedic:         ch = 'V'; break;
  case hsSripati:       ch = 'S'; break;
  case hsHorizon:       ch = 'H'; break;
  case hsAPC:           ch = 'Y'; break;
  case hsCarter:        ch = 'F'; break;
  case hsSunshine:      ch = 'I'; break;
  case hsSavardA:       ch = 'J'; break;
  default:              ch = 'A'; break;
  }
  jd = JulianDayFromTime(jd);
  if (jd != is.jdDeltaT) {
    is.jdDeltaT = jd;
    is.rDeltaT = swe_deltat(jd);
  }
  lon = -lon;

  // The following is largely copied from swe_houses().
  double armc, eps, nutlo[2];
  double tjde = jd +
    (us.rDeltaT == rInvalid ? is.rDeltaT : us.rDeltaT/86400.0);

  eps = swi_epsiln(tjde, 0) * RADTODEG;
  swi_nutation(tjde, 0, nutlo);
  for (i = 0; i < 2; i++)
    nutlo[i] *= RADTODEG;
  armc = lon;
  if (!us.fGeodetic)
    armc += swe_degnorm(swe_sidtime0(jd + (us.rDeltaT == rInvalid ? 0.0 :
      us.rDeltaT/86400.0 - is.rDeltaT), eps + nutlo[1], nutlo[0]) * 15.0);
  if (ch == 'I') {
    // Need Sun declination for Sunshine houses.
    int flags = SEFLG_SPEED | SEFLG_EQUATORIAL;
    double xp[6];
    int result = swe_calc_ut(jd, SE_SUN, flags, xp, NULL);
    ascmc[9] = xp[1];
  }
  if (swe_houses_armc_ex2(armc, lat, eps + nutlo[1], ch, cusp, ascmc,
    cuspr, ascmcr, serr))
    housesystem = hsPorphyry;

  // Fill out return parameters for cusp array, Ascendant, etc.
  *off = -swe_get_ayanamsa(tjde);
  is.rSid = (us.fSidereal ? *off + us.rZodiacOffset : 0.0) +
    us.rZodiacOffsetAll;
  rSid = us.fSidereal ? is.rSid - nutlo[0] : is.rSid;
  *asc = Mod(ascmc[SE_ASC]    + rSid);
  *mc  = Mod(ascmc[SE_MC]     + rSid);
  *ra  = Mod(ascmc[SE_ARMC]   + rSid);
  *vtx = Mod(ascmc[SE_VERTEX] + rSid);
  *ep  = Mod(ascmc[SE_EQUASC] + rSid);
  *ob  = eps;
  *nut = nutlo[0];
  for (i = 1; i <= cSign; i++) {
    chouse[i] = Mod(cusp[i] + rSid);
    ret[cuspLo-1+i] = cuspr[i];
  }
  ret[oFor] = ascmcr[SE_ASC];
  ret[oVtx] = ascmcr[SE_VERTEX];
  ret[oEP]  = ascmcr[SE_EQUASC];
  if (!us.fHouseAngle) {
    ret[oAsc] = ret[oDes] = ascmcr[SE_ASC];
    ret[oMC]  = ret[oNad] = ascmcr[SE_MC];
  }

  // Want generic MC. Undo if house system flipped it 180 degrees.
  if ((housesystem == hsCampanus || housesystem == hsRegiomontanus ||
    housesystem == hsTopocentric || housesystem == hsAPC ||
    housesystem == hsSunshine || housesystem == hsSavardA) &&
    MinDifference(*mc, *asc) < 0.0)
    *mc = Mod(*mc + rDegHalf);

  // Have Astrolog compute the houses if Swiss Ephemeris didn't do so.
  if (ch == 'A')
    ComputeHouses(housesystem);
  else {
    is.nHouseSystem = housesystem;
    FEnsureHousePartition(housesystem);
  }
}


CONST char *szStarNameSwiss[cStar+1] = {"",
  "", "", "Rigil Kentaurus", "", "", "", "", "", "", "",
  "", "", "", "", "", "", "", "", "", ",beCru",
  "", "", "", "", "", "", "", "", "", "",
  "", "", "", "Kaus Australis", "", "", "", "", "", "",
  "", "", "", "", "", "", ",M31", ",ze-1Ret", ",SgrA*", ",GA"};

// Compute fixed star locations. Given a time, call Swiss Ephemeris to
// compute them. This is similar to FSwissPlanet() in that it knows about
// and translates between Astrolog and Swiss Ephemeris defintions.

void SwissComputeStars(real jd, flag fInitBright)
{
  char sz[cchSzDef], serr[AS_MAXCH];
  int i, iflag;
  double xx[6], mag;

  SwissEnsurePath();
  if (!fInitBright) {
    jd = JulianDayFromTime(jd);
    iflag = SEFLG_SPEED;
    iflag |= (us.nSwissEph <= 0 ? SEFLG_SWIEPH :
      (us.nSwissEph == 1 ? SEFLG_MOSEPH : SEFLG_JPLEPH));
    if (us.fSidereal) {
      swe_set_sid_mode(!us.fSidereal2 ? SE_SIDM_FAGAN_BRADLEY :
        SE_SIDBIT_SSY_PLANE, 0.0, 0.0);
      iflag |= SEFLG_SIDEREAL;
    }
    if (us.objCenter != oEar)
      iflag |= (us.fBarycenter ? SEFLG_BARYCTR : SEFLG_HELCTR);
    if (us.fTruePos)
      iflag |= SEFLG_TRUEPOS;
    if (us.fNoNutation)
      iflag |= SEFLG_NONUT;
  } else {
    jd = rJD2000;
    iflag = SEFLG_SPEED | SEFLG_SWIEPH | SEFLG_HELCTR;
  }
  for (i = 1; i <= cStar; i++) {
    if (!(fInitBright || !ignore[oNorm+i] || us.objCenter == oNorm+i))
      continue;

    // In most cases Astrolog's star name is the same as Swiss Ephemeris,
    // however for a few stars need to translate to a different string.
    if (!FSzSet(szStarCustom[i])) {
      if (*szStarNameSwiss[i])
        sprintf2(S(sz), "%s", szStarNameSwiss[i]);
      else
        sprintf2(S(sz), "%s", szObjName[oNorm+i]);
    } else
      sprintf2(S(sz), "%s", szStarCustom[i]);

    // Compute the star location or get the star's brightness.
    swe_fixstar2(sz, jd, iflag, xx, serr);
    if (!fInitBright) {
      planet[oNorm+i] = Mod(xx[0] + (us.fSidereal ? us.rZodiacOffset : 0.0) +
        us.rZodiacOffsetAll);
      planetalt[oNorm+i] = xx[1];
      ret[oNorm+i] = xx[3];
      if (!us.fSidereal && us.fVelocity)
        ret[oNorm+i] /= rDegMax/25765.0/rDayInYear;
      SphToRec(xx[2], planet[oNorm+i], planetalt[oNorm+i],
        &space[oNorm+i].x, &space[oNorm+i].y, &space[oNorm+i].z);
      if (us.fStarMagDist && rStarBrightDef[i] != rStarNot)
        rStarBright[i] = RStarBright(rStarBrightDef[i], rStarBrightDistDef[i],
          us.fStarMagAbs ? 10.0 * rPCToAU : xx[2]);
      else
        rStarBright[i] = rStarBrightDef[i];
      if (rgobjset[starLo].kolor >= cColor2)
        kObjA[oNorm+i] = KStarA(rStarBright[i]);
    } else {
      rStarBrightDistDef[i] = xx[2];
      swe_fixstar2_mag(sz, &mag, serr);
      rStarBrightDef[i] = rStarBright[i] = mag;
    }
  }
}


// Compute one fixed star location. Given a star index and time, call Swiss
// Ephemeris to compute it. This is similar to SwissComputeStars().

flag SwissComputeStar(real jd, ES *pes)
{
  char serr[AS_MAXCH], *pch, *pchT, chT;
  int iflag, isz = 0, i;
  double xx[6], dist1, dist2;
  static real lonPrev = 0.0, latPrev = 0.0;
  static int istar = 1;

  // Calling with empty parameters means initialize to first star.
  if (pes == NULL) {
    istar = 1;
#ifdef GRAPH
    if (gi.rges != NULL)
      ClearB((pbyte)gi.rges, sizeof(ES) * gi.cStarsLin);
#endif
    return fTrue;
  }

  // Determine Swiss Ephemeris flags.
  jd = JulianDayFromTime(jd);
  iflag = SEFLG_SPEED;
  iflag |= (us.nSwissEph <= 0 ? SEFLG_SWIEPH :
    (us.nSwissEph == 1 ? SEFLG_MOSEPH : SEFLG_JPLEPH));
  if (us.fSidereal) {
    swe_set_sid_mode(!us.fSidereal2 ? SE_SIDM_FAGAN_BRADLEY :
      SE_SIDBIT_SSY_PLANE, 0.0, 0.0);
    iflag |= SEFLG_SIDEREAL;
  }
  if (us.objCenter != oEar)
    iflag |= (us.fBarycenter ? SEFLG_BARYCTR : SEFLG_HELCTR);
  if (us.fTruePos)
    iflag |= SEFLG_TRUEPOS;
  if (us.fNoNutation)
    iflag |= SEFLG_NONUT;
LNext:
  sprintf2(S(pes->sz), "%d", istar);

  // Compute the star coordinates and get the star's brightness.
  if (us.fStarMagDist) {
    if (swe_fixstar2(pes->sz, rJD2000,
      SEFLG_SPEED | SEFLG_SWIEPH | SEFLG_HELCTR, xx, serr) < 0)
      return fFalse;
    dist1 = xx[2];
  }
  if (swe_fixstar2(pes->sz, jd, iflag, xx, serr) < 0)
    return fFalse;
  pes->lon = Mod(xx[0] + (us.fSidereal ? us.rZodiacOffset : 0.0) +
    us.rZodiacOffsetAll);
  pes->lat = xx[1];
  if (us.fStarMagDist)
    dist2 = xx[2];
  pes->dir = xx[3];

  // Store star data.
  SphToRec(xx[2], pes->lon, pes->lat, &pes->pt.x, &pes->pt.y, &pes->pt.z);
  if (us.objCenter > oSun) {
    pes->pt.x += space[oSun].x;
    pes->pt.y += space[oSun].y;
    pes->pt.z += space[oSun].z;
    RecToSph3(pes->pt.x, pes->pt.y, pes->pt.z, &pes->lon, &pes->lat);
    if (us.fStarMagDist)
      dist2 = PtLen(pes->pt);
  }
  if (swe_fixstar2_mag(pes->sz, &pes->mag, serr) < 0)
    return fFalse;
  if (pes->mag == 0.0)
    pes->mag = rStarNot;
  else if (us.fStarMagDist && pes->mag != rStarNot)
    pes->mag = RStarBright(pes->mag, dist1,
      us.fStarMagAbs ? 10.0 * rPCToAU : dist2);

  // Adjust star coordinates if needed.
  if (us.rHarmonic != 1.0)
    pes->lon = Mod(pes->lon * us.rHarmonic);
  if (us.fDecan)
    pes->lon = Decan(pes->lon);
  if (us.nDwad > 0)
    for (i = 0; i < us.nDwad; i++)
      pes->lon = Dwad(pes->lon);
  if (us.fNavamsa)
    pes->lon = Navamsa(pes->lon);

  // Skip over effectively duplicate stars, or non-stars.
  istar++;
  if ((pes->lon == lonPrev && pes->lat == latPrev) ||
    (pes->mag == rStarNot && !us.fGraphAll))
    goto LNext;
  lonPrev = pes->lon; latPrev = pes->lat;

#ifdef EXPRESS
  // Skip current star if AstroExpression says to do so.
  if (!us.fExpOff && FSzSet(us.szExpStar)) {
    ExpSetN(iLetterV, istar);
    ExpSetR(iLetterW, pes->lon);
    ExpSetR(iLetterX, pes->lat);
    ExpSetR(iLetterY, pes->dir);
    ExpSetR(iLetterZ, pes->mag);
    if (!NParseExpression(us.szExpStar))
      goto LNext;
    pes->lon = Mod(RExpGet(iLetterW));
    pes->lat = RExpGet(iLetterX);
    pes->dir = RExpGet(iLetterY);
    pes->mag = RExpGet(iLetterZ);
  }
#endif

  // Parse star name.
  pes->pchNam = pes->sz;
  for (pch = pes->sz; *pch && *pch != ','; pch++)
    ;
  if (*pch == ',') {
    *pch++ = chNull;
    pes->pchDes = pch;
    pes->pchBest = *pes->pchNam ? pes->pchNam : pes->pchDes;
  } else {
    pes->pchDes = "";
    pes->pchBest = pes->pchNam;
  }

  // Check for if should do anything special with this star?
  if (FSzSet(us.szStarsList) &&
    ((*pes->pchDes && SzInList(pes->pchDes, us.szStarsList, NULL) != NULL) ||
    (*pes->pchNam && SzInList(pes->pchNam, us.szStarsList, NULL) != NULL)) !=
    us.fStarsList)
    goto LNext;
  pes->ki = kDefault;
  if (FSzSet(us.szStarsColor)) {
    pch = (char *)SzInList(pes->pchBest, us.szStarsColor, NULL);
    if (FSzSet(pch)) {
      for (pchT = pch; *pchT && *pchT != chSep && *pchT != chSep2; pchT++)
        ;
      chT = *pchT; *pchT = chNull;
      pes->ki = NParseSz(pch, pmColor);
      *pchT = chT;
    }
  }
#ifdef GRAPH
  if (FSzSet(gs.szStarsLin)) {
    pch = (char *)SzInList(pes->pchBest, gs.szStarsLin, &isz);
    if (isz >= 0)
      gi.rges[isz] = *pes;
  }
#endif
  return fTrue;
}


// Like SwissComputeStar(), but potentially apply the star sorting method to
// the order stars are returned.

flag SwissComputeStarSort(real jd, ES *pes)
{
  static int ces = 0, istar = 0;
  int i, j;
  ES es, *pes2;

  // Simple cases when not sorting or when sorted list has been created.
  if (us.nStarSort <= 0)
    return SwissComputeStar(jd, pes);
  if (pes != NULL) {
    if (istar >= ces)
      return fFalse;
    *pes = is.rgesSort[istar];
    istar++;
    return fTrue;
  }

  // Allocate list and put all stars within it.
  ces = 1500;
  if (ces > is.cesSort) {
    if (is.rgesSort != NULL)
      DeallocateP(is.rgesSort);
    is.rgesSort = RgAllocate(ces, ES, "star sort");
    if (is.rgesSort == NULL)
      return fFalse;
    is.cesSort = ces;
  }
  SwissComputeStar(jd, pes);
  for (i = 0; i < 1500 && SwissComputeStar(jd, &is.rgesSort[i]); i++)
    ;
  ces = i;
  istar = 0;

  // Temporarily convert pointers to offsets, since sorting may move them.
  for (i = 0; i < ces; i++) {
    pes2 = &is.rgesSort[i];
    pes2->pchNam = (char *)(pes2->pchNam - pes2->sz);
    if (*pes2->pchDes)
      pes2->pchDes = (char *)(pes2->pchDes - pes2->sz);
    pes2->pchBest = (char *)(pes2->pchBest - pes2->sz);
  }
  for (i = 1; i < ces; i++) {
    j = i-1;

    // Compare star names for -Un switch.
    if (us.nStarSort == 'n') while (j >= 0 && NCompareSz(
      is.rgesSort[j].sz + (time_t)is.rgesSort[j].pchBest,
      is.rgesSort[j+1].sz + (time_t)is.rgesSort[j+1].pchBest) > 0) {
      SwapTemp(is.rgesSort[j], is.rgesSort[j+1], es);
      j--;

    // Compare star brightnesses for -Ub switch.
    } else if (us.nStarSort == 'b') while (j >= 0 &&
      is.rgesSort[j].mag > is.rgesSort[j+1].mag) {
      SwapTemp(is.rgesSort[j], is.rgesSort[j+1], es);
      j--;

    // Compare star zodiac locations for -Uz switch.
    } else if (us.nStarSort == 'z') while (j >= 0 &&
      is.rgesSort[j].lon > is.rgesSort[j+1].lon) {
      SwapTemp(is.rgesSort[j], is.rgesSort[j+1], es);
      j--;

    // Compare star latitudes for -Ul switch.
    } else if (us.nStarSort == 'l') while (j >= 0 &&
      is.rgesSort[j].lat > is.rgesSort[j+1].lat) {
      SwapTemp(is.rgesSort[j], is.rgesSort[j+1], es);
      j--;

    // Compare star velocities for -Uv switch.
    } else if (us.nStarSort == 'v') while (j >= 0 &&
      is.rgesSort[j].dir > is.rgesSort[j+1].dir) {
      SwapTemp(is.rgesSort[j], is.rgesSort[j+1], es);
      j--;
    }
  }
  for (i = 0; i < ces; i++) {
    pes2 = &is.rgesSort[i];
    pes2->pchNam = pes2->sz + (time_t)(pes2->pchNam);
    if (pes2->pchDes < (char *)cchSzDef)
      pes2->pchDes = pes2->sz + (time_t)(pes2->pchDes);
    pes2->pchBest = pes2->sz + (time_t)(pes2->pchBest);
  }
  return fTrue;
}


// Given a star definition string for a star from sefstars.txt, change that
// buffer to contain that star's human readable name instead.

flag SwissTestStar(char *sz)
{
  char serr[AS_MAXCH], *pch;
  double xx[6];

  if (swe_fixstar2(sz, rJD2000, SEFLG_SPEED, xx, serr) < 0)
    return fFalse;
  for (pch = sz; *pch && *pch != ','; pch++)
    ;
  if (*pch == ',') {
    // Choose part of string before a comma, else step over starting comma.
    if (pch > sz)
      *pch = chNull;
    else
      for (pch = sz; (*pch = pch[1]); pch++)
        ;
  }
  return fTrue;
}


#ifdef GRAPH
// Compute one asteroid location. Given an asteroid number and time, call
// Swiss Ephemeris to compute it. This is similar to SwissComputeStar().

flag SwissComputeAsteroid(real jd, ES *pes, flag fBack)
{
  int iflag, i;
  real r1, r2, r3, r4, r5, r6, rDiff;
  char sz[cchSzDef], *pch, *pchT, chT;
  static int iast = 1;

  // Determine Swiss Ephemeris flags.
  jd = JulianDayFromTime(jd);
  iflag = SEFLG_SPEED;
  iflag |= (us.nSwissEph <= 0 ? SEFLG_SWIEPH :
    (us.nSwissEph == 1 ? SEFLG_MOSEPH : SEFLG_JPLEPH));
  if (us.fSidereal) {
    swe_set_sid_mode(!us.fSidereal2 ? SE_SIDM_FAGAN_BRADLEY :
      SE_SIDBIT_SSY_PLANE, 0.0, 0.0);
    iflag |= SEFLG_SIDEREAL;
  }
  if (us.objCenter != oEar)
    iflag |= (us.fBarycenter ? SEFLG_BARYCTR : SEFLG_HELCTR);
  if (us.fTruePos)
    iflag |= SEFLG_TRUEPOS;
  if (us.fNoNutation)
    iflag |= SEFLG_NONUT;

  // Calling with empty parameters means initialize to first asteroid.
#ifdef EXPRESS
LNext:
#endif
  if (pes == NULL) {
    iast = fBack ? gs.nAstHi : gs.nAstLo;
    return fTrue;
  } else if (iast < Max(gs.nAstLo, 1) || iast > gs.nAstHi)
    return fFalse;

  // Compute the asteroid coordinates.
  if (!FSwissPlanet(iast + SE_AST_OFFSET, jd, us.objCenter,
    &r1, &r2, &r3, &r4, &r5, &r6))
    return fFalse;
  pes->lon = Mod(r1 + is.rSid);
  pes->lat = r2;
  pes->dir = r3;
  SphToRec(r4, pes->lon, pes->lat, &pes->pt.x, &pes->pt.y, &pes->pt.z);
  if (us.objCenter > oSun) {
    pes->pt.x += space[oSun].x;
    pes->pt.y += space[oSun].y;
    pes->pt.z += space[oSun].z;
    RecToSph3(pes->pt.x, pes->pt.y, pes->pt.z, &pes->lon, &pes->lat);
  }

  // Adjust asteroid coordinates if needed.
  if (us.rHarmonic != 1.0)
    pes->lon = Mod(pes->lon * us.rHarmonic);
  if (us.fDecan)
    pes->lon = Decan(pes->lon);
  if (us.nDwad > 0)
    for (i = 0; i < us.nDwad; i++)
      pes->lon = Dwad(pes->lon);
  if (us.fNavamsa)
    pes->lon = Navamsa(pes->lon);

#ifdef EXPRESS
  // Skip current asteroid if AstroExpression says to do so.
  if (!us.fExpOff && FSzSet(us.szExpAst)) {
    ExpSetN(iLetterW, iast);
    ExpSetR(iLetterX, pes->lon);
    ExpSetR(iLetterY, pes->lat);
    ExpSetR(iLetterZ, pes->dir);
    if (!NParseExpression(us.szExpAst)) {
      iast += (fBack ? -1 : 1);
      goto LNext;
    }
    pes->lon = Mod(RExpGet(iLetterX));
    pes->lat = RExpGet(iLetterY);
    pes->dir = RExpGet(iLetterZ);
  }
#endif

  // Determine asteroid display name.
  pch = pes->sz;
  *pch = chNull;
  if (FOdd(gs.nAstLabel))
    sprintf(pch, "%d", iast);
  if (gs.nAstLabel >= 3) {
    while (*pch)
      pch++;
    *pch++ = ' ';
    *pch = chNull;
  }
  if (gs.nAstLabel & 2) {
    swe_get_planet_name(iast + SE_AST_OFFSET, pch);
    // This check only needed for old style ephemeris files.
    if (*pch == '?' && pch[1] == ' ')
      while ((*pch = pch[2]))
        pch++;
  }
  pes->pchBest = pes->sz;

  // Determine asteroid coloring.
  rDiff = gs.nAstHi <= gs.nAstLo ? 1.0 : (real)(gs.nAstHi - gs.nAstLo);
  pes->mag = ((real)(iast - gs.nAstLo) / rDiff * rStarSpan) + rStarLite;
  pes->ki = kDefault;
  if (FSzSet(us.szAstColor)) {
    pch = (char *)SzInList(pes->pchBest, us.szAstColor, NULL);
    if (!FSzSet(pch)) {
      sprintf2(S(sz), "%d", iast);
      pch = (char *)SzInList(sz, us.szAstColor, NULL);
    }
    if (FSzSet(pch)) {
      for (pchT = pch; *pchT && *pchT != chSep && *pchT != chSep2; pchT++)
        ;
      chT = *pchT; *pchT = chNull;
      pes->ki = NParseSz(pch, pmColor);
      *pchT = chT;
    }
  }

  iast += (fBack ? -1 : 1);
  return fTrue;
}


// Like SwissComputeAsteroid(), but potentially apply a sorting method to the
// order asteroids are returned.

flag SwissComputeAsteroidSort(real jd, ES *pes)
{
  static int ces = 0, iast = 0;
  int i, j;
  ES es;

  // Simple cases when not sorting or when sorted list has been created.
  if (us.nStarSort <= 0)
    return SwissComputeAsteroid(jd, pes, fFalse);
  if (pes != NULL) {
    if (iast >= ces)
      return fFalse;
    *pes = is.rgesSort[iast];
    iast++;
    return fTrue;
  }

  // Allocate list and put all asteroids within it.
  ces = gs.nAstHi - gs.nAstLo + 1;
  if (ces > is.cesSort) {
    if (is.rgesSort != NULL)
      DeallocateP(is.rgesSort);
    is.rgesSort = RgAllocate(ces, ES, "asteroid sort");
    if (is.rgesSort == NULL)
      return fFalse;
    is.cesSort = ces;
  }
  SwissComputeAsteroid(jd, pes, fFalse);
  for (i = 0; SwissComputeAsteroid(jd, &is.rgesSort[i], fFalse); i++)
    ;
  ces = i;
  iast = 0;

  for (i = 1; i < ces; i++) {
    j = i-1;

    // Compare asteroid names for -Un switch.
    if (us.nStarSort == 'n') while (j >= 0 && NCompareSz(
      is.rgesSort[j].sz, is.rgesSort[j+1].sz) > 0) {
      SwapTemp(is.rgesSort[j], is.rgesSort[j+1], es);
      j--;

    // Compare asteroid zodiac locations for -Uz switch.
    } else if (us.nStarSort == 'z') while (j >= 0 &&
      is.rgesSort[j].lon > is.rgesSort[j+1].lon) {
      SwapTemp(is.rgesSort[j], is.rgesSort[j+1], es);
      j--;

    // Compare asteroid latitudes for -Ul switch.
    } else if (us.nStarSort == 'l') while (j >= 0 &&
      is.rgesSort[j].lat > is.rgesSort[j+1].lat) {
      SwapTemp(is.rgesSort[j], is.rgesSort[j+1], es);
      j--;

    // Compare asteroid velocities for -Uv switch.
    } else if (us.nStarSort == 'v') while (j >= 0 &&
      is.rgesSort[j].dir > is.rgesSort[j+1].dir) {
      SwapTemp(is.rgesSort[j], is.rgesSort[j+1], es);
      j--;
    }
  }
  return fTrue;
}
#endif


// Wrapper around Swiss Ephemeris planet name lookup routine.

void SwissGetObjName(char *sz, int cchMax, int iobj)
{
  char *pch;

  if (iobj < 0)
    iobj = -iobj + SE_FICT_OFFSET_1;
  else
    iobj += SE_AST_OFFSET;
  swe_get_planet_name(iobj, sz);

  // Check for object not found.
  for (pch = sz; *pch; pch++)
    if (FMatchSz(" not found", pch)) {
      sprintf2(sz, cchMax, "%s", szObjUnknown);
      break;
    }
}


// Similar to FSwissPlanet(), this instead computes a planet's phase, angular
// diameter, and magnitude. A wrapper around swe_pheno() which computes them.

flag FSwissPlanetData(real jd, int ind, real *rPhase, real *rDiam, real *rMag)
{
  int iobj, iflag;
  double attr[20];      // API requires at least 20 elements in output array.
  char serr[AS_MAXCH];

  if (ind == oEar)
    iobj = SE_EARTH;
  else if (ind <= oPlu)
    iobj = ind-1;
  else if (ind == oChi)
    iobj = SE_CHIRON;
  else if (FBetween(ind, oCer, oVes))
    iobj = ind - oCer + SE_CERES;
  else if (ind == oPho)
    iobj = SE_PHOLUS;
  else
    return fFalse;

  iflag = SEFLG_SPEED;
  iflag |= (us.nSwissEph <= 0 ? SEFLG_SWIEPH :
    (us.nSwissEph == 1 ? SEFLG_MOSEPH : SEFLG_JPLEPH));
  if (us.fSidereal) {
    swe_set_sid_mode(!us.fSidereal2 ? SE_SIDM_FAGAN_BRADLEY :
      SE_SIDBIT_SSY_PLANE, 0.0, 0.0);
    iflag |= SEFLG_SIDEREAL;
  }
  if (ind <= oSun && us.fBarycenter)
    iflag |= SEFLG_BARYCTR;
  if (us.fNoNutation)
    iflag |= SEFLG_NONUT;
  if (us.fTruePos)
    iflag |= SEFLG_TRUEPOS;
  if (us.fTopoPos) {
    swe_set_topo(-OO, AA, us.elvDef);
    iflag |= SEFLG_TOPOCTR;
  }

  swe_pheno_ut(JulianDayFromTime(jd), iobj, iflag, attr, serr);
  *rPhase = attr[1]; *rDiam = attr[3]; *rMag = attr[4];
  return fTrue;
}


// Wrapper around Swiss Ephemeris function to convert an altitude above the
// horizon to altitude when atmospheric refraction is taken into account.

real SwissRefract(real rAlt)
{
  real dret[20], rPress;

  rPress = 1013.25 * pow(1.0 - 0.0065 * us.elvDef / 288.0, 5.255);
  //alt[i] = swe_refrac(alt[i], rPress, us.tmpDef, SE_TRUE_TO_APP);
  swe_refrac_extended(rAlt, us.elvDef, rPress, us.tmpDef,
    0.0065/*SE_LAPSE_RATE*/, SE_TRUE_TO_APP, dret);
  return dret[1];
}


// Wrapper around Swiss Ephemeris function to get date range of ephem file.

void SwissGetFileData(real *jt1, real *jt2)
{
  int denum;

  swe_get_current_file_data(3, jt1, jt2, &denum);
}


// Return the equation of time or offset between LAT and LMT for a given date.

real SwissLatLmt(real jd)
{
  char serr[AS_MAXCH];
  real rDiff = 0.0;

  swe_time_equ(jd, &rDiff, serr);
  rDiff *= 24.0;
  return rDiff;
}


// Wrappers around Swiss Ephemeris Julian Day conversion routines.

real SwissJulDay(int month, int day, int year, real hour, int gregflag)
{
  return swe_julday(year, month, day, hour, gregflag);
}

void SwissRevJul(real jd, int gregflag,
  int *jmon, int *jday, int *jyear, real *jut)
{
  swe_revjul(jd, gregflag, jyear, jmon, jday, jut);
}
#endif /* SWISS */

/* calc.cpp */
