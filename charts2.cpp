/*
** Astrolog (Version 8.00) File: charts2.cpp
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
** Dual Chart Display Routines.
******************************************************************************
*/

// Print a listing of planets for two (or more) charts, as specified by the
// -v -r0 switch combination, along with the maximum delta between planets.

void ChartListingRelation(void)
{
  char sz[cchSzDef], szFormat[cchSzDef];
  int cChart, i, j, k, l, n;
  real r, rT;

  cChart = 2 - (FBetween(us.nRel, rcHexaWheel, rcTriWheel) ? us.nRel+1 : 0);

  // Print header rows.
  AnsiColor(kWhiteA);
  n = VSeconds(16, 23, 31);
  sprintf2(S(szFormat), " %%-%d.%ds", n, n);
  for (n = 0, i = 1; i <= cChart; i++)
    n += FSzSet(rgpci[i]->nam);
  if (n > 0) {
    PrintTab(' ', 5);
    for (i = 1; i <= cChart; i++) {
      AnsiColor(kMainA[FOdd(i) ? 1 : 3]);
      sprintf2(S(sz), szFormat, rgpci[i]->nam); PrintSz(sz);
    }
    PrintL();
  }
  PrintTab(' ', 6);
  for (i = 1; i <= cChart; i++) {
    AnsiColor(kMainA[FOdd(i) ? 1 : 3]);
    if (!FNoTimeOrSpace(*rgpci[i])) {
      sprintf2(S(sz), "%s %s", SzDate(rgpci[i]->mon, rgpci[i]->day,
        rgpci[i]->yea, us.fSeconds-1), SzTim(rgpci[i]->tim));
      PrintSz(sz);
      PrintTab(' ', VSeconds(17, 24, 32) - CchSz(sz));
    } else
      PrintSz(us.fSeconds ? "(No time or space)      " : "(No time/space)  ");
  }
  PrintL();
  for (n = 0, i = 1; i <= cChart; i++)
    n += FSzSet(rgpci[i]->loc);
  if (n > 0) {
    PrintTab(' ', 5);
    for (i = 1; i <= cChart; i++) {
      AnsiColor(kMainA[FOdd(i) ? 1 : 3]);
      sprintf2(S(sz), szFormat, rgpci[i]->loc); PrintSz(sz);
    }
    PrintL();
  }
  PrintTab(' ', 5);
  for (i = 1; i <= cChart; i++) {
    AnsiColor(kMainA[FOdd(i) ? 1 : 3]);
    sprintf2(S(sz), szFormat, SzLocation(rgpci[i]->lon, rgpci[i]->lat));
    PrintSz(sz);
  }
  AnsiColor(kDkGrayA);
  PrintSz("\nBody");
  for (i = 1; i <= cChart; i++) {
    AnsiColor(kMainA[FOdd(i) ? 1 : 3]);
    PrintSz("  Location");
    PrintTab(' ', VSeconds(1, 5, 9));
    if (us.fSeconds) {
      PrintSz(!us.fEquator2 ? " Latitude" : " Declin. ");
      if (us.fSecond1K)
        PrintSz("    ");
    } else
      PrintSz(!us.fEquator2 ? "Latit." : "Decl. ");
  }
  AnsiColor(kMainA[FOdd(i) ? 3 : 1]);
  PrintTab(' ', 3 + us.fSeconds);
  PrintSz("Delta\n");

  // Print object positions.
  for (l = 0; l <= is.nObj; l++) {
    i = rgobjList[l];
    if (FIgnore(i))
      continue;
    AnsiColor(kObjA[i]);
    sprintf2(S(sz), "%-4.4s:", szObjDisp[i]); PrintSz(sz);
    for (j = 1; j <= cChart; j++) {
      PrintCh(' ');
      PrintZodiac(rgpcp[j]->obj[i]);
      sprintf2(S(sz), "%c ", ChRet(rgpcp[j]->dir[i])); PrintSz(sz);
      PrintAltitude(rgpcp[j]->alt[i]);
    }

    // Compute maximum offset between any two instances of this planet.
    r = 0.0;
    for (j = 1; j <= cChart; j++)
      for (k = 1; k < j; k++) {
        if (!us.fAspect3D)
          rT = MinDistance(rgpcp[j]->obj[i], rgpcp[k]->obj[i]);
        else
          rT = SphDistance(rgpcp[j]->obj[i], rgpcp[j]->alt[i],
            rgpcp[k]->obj[i], rgpcp[k]->alt[i]);
        r = Max(r, rT);
      }
    AnsiColor(kDkGrayA);
    PrintCh(' ');
    PrintSz(SzDegree(r));
    PrintL();
  }
}


// Print out an aspect (or midpoint if -gm switch in effect) grid of a
// relationship chart. This is similar to the ChartGrid() routine, however
// here both axes are labeled with the planets for the two charts in question,
// instead of just a diagonal down the center for only one chart.

void ChartGridRelation(void)
{
  int i0, j0, i, j, k, temp;

#ifdef INTERPRET
  if (us.fInterpret && !us.fGridMidpoint) {
    InterpretGridRelation();
    return;
  }
#endif

  for (k = 2; k <= 4 + us.fSeconds; k++) {
    PrintSz(k <= 2 ? " 2>" : (k == 3 ? "1  " : (k == 4 ? "V  " : "   ")));
    for (i0 = 0; i0 <= is.nObj; i0++) {
      i = rgobjList[i0];
      if (ignore[i])
        continue;
      PrintCh2(chV);
      PrintGridCell(-3, i, 0, k);
      AnsiColor(kDefault);
    }
    PrintL();
  }
  for (j0 = 0; j0 <= is.nObj; j0++) {
    j = rgobjList[j0];
    if (ignore[j])
      continue;
    for (k = 1; k <= 4 + us.fSeconds; k++) {
      if (k < 2)
        PrintTab2(chH, 3);
      else
        PrintGridCell(-2, j, 0, k);
      if (k > 1)
        AnsiColor(kDefault);
      for (i0 = 0; i0 <= is.nObj; i0++) {
        i = rgobjList[i0];
        if (ignore[i])
          continue;
        PrintCh2(k < 2 ? chC : chV);
        temp = grid->n[i][j];
        if (k > 1 && i == j)
          AnsiColor(kReverse);
        if (k < 2)
          PrintTab2(chH, 3);
        else
          PrintGridCell(i, j, 1 + us.fGridMidpoint, k);
        AnsiColor(kDefault);
      }
      PrintL();
    }
  }
}


// Calculate any of the various kinds of relationship charts. This involves
// computing and storing the planet and house positions for the "core" and
// "second" charts, and then combining them in the main single chart in the
// proper manner, e.g. for synastry, composite, time space midpoint charts.

void CastRelation(void)
{
  byte ignoreSav[objMax];
  int i, j, cChart;
  real t1, t2, t, rSav, rRatio;
  flag fSav;
#ifdef SWISS
  real rT, rLon, rLat;
#endif

  // Cast up to six charts.

  fSav = us.fProgress;
  cChart = 2 - (FBetween(us.nRel, rcHexaWheel, rcTriWheel) ? us.nRel+1 : 0);

  for (i = 1; i <= cChart; i++) {
    ciCore = *rgpci[i];
    // A GUI recasts the same relationship chart over and over, so the
    // midpoint has to be taken from the chart as loaded every time. Left
    // out, each recast takes the midpoint of the *previous* midpoint and
    // the chart walks toward the twin a little more on every redraw.
    // SetRel()/SetRelQt() put the original in ciSave for exactly this.
#if defined(WIN) || defined(QT)
    if (i == 1 && us.nRel == rcMidpoint)
      ciCore = ciMain = ciSave;
#endif
    if (i == 2 && us.nRel <= rcTransit) {
      CopyRgb(ignore.rgn, ignoreSav, sizeof(ignore.rgn));
      for (j = 0; j <= is.nObj; j++)
        ignore[j] = ignore[j] && ignore2[j];
    }
    us.fProgress = (i == 1 && us.nRel == rcProgress ? fFalse :
      (i == 2 && us.nRel == rcProgress ? fTrue : rgfProg[i]));
    if (us.fProgress) {
      is.JDp = MdytszToJulian(MM, DD, YY, TT, SS, ZZ);
      ciCore = ciMain;
    }
    FProcessCommandLine(szWheel[i]);
    if (FNoTimeOrSpace(ciCore)) {
      cp0 = *rgpcp[i];
      t = 0.0;
    } else
      t = CastChart(i);
    if (i == 1) {
      t1 = t;
      rSav = is.MC;
    } else if (i == 2)
      t2 = t;
    *rgpcp[i] = cp0;
    if (i == 2 && us.nRel <= rcTransit)
      CopyRgb(ignoreSav, ignore.rgn, sizeof(ignore.rgn));
  }

  us.fProgress = fSav;
  ciCore = ciMain;
  FProcessCommandLine(szWheel[0]);
  is.MC = rSav;

  // Now combine the two charts based on what relation we are doing.
  // For the standard -r synastry chart, use the house cusps of chart1
  // and the planet positions of chart2.

  if (us.nRel <= rcSynastry) {
    for (i = 1; i <= cSign; i++)
      chouse[i] = cp1.cusp[i];

  // For the -rc composite chart, take the midpoints of the planets/houses.

  } else if (us.nRel == rcComposite) {
    rRatio = us.rRatio;
    if (!FBetween(rRatio, 0.0, 1.0))
      rRatio = 0.5;
    j = Max(is.nObj, cuspHi);
    for (i = 0; i <= j; i++) {
      if (!us.fHouse3D) {
        planet[i] = Midpoint2(cp1.obj[i], cp2.obj[i], rRatio);
        planetalt[i] = Ratio(cp1.alt[i], cp2.alt[i], rRatio);
      } else
        SphRatio(cp1.obj[i], cp1.alt[i], cp2.obj[i], cp2.alt[i], rRatio,
          &planet[i], &planetalt[i]);
      ret[i] = Ratio(cp1.dir[i], cp2.dir[i], rRatio);
      retalt[i] = Ratio(cp1.diralt[i], cp2.diralt[i], rRatio);
      retlen[i] = Ratio(cp1.dirlen[i], cp2.dirlen[i], rRatio);
    }
    for (i = 1; i <= cSign; i++) {
      chouse[i] = Ratio(cp1.cusp[i], cp2.cusp[i], rRatio);
      if (RAbs(cp2.cusp[i] - cp1.cusp[i]) > rDegHalf)
        chouse[i] = Mod(chouse[i] + rDegMax*rRatio);
    }

    // Make sure don't have any 180 degree errors in house cusp complement
    // pairs, which may happen if the cusps are far apart.

    j = us.fPolarAsc ? sAri : sCap;
    for (i = 1; i <= cSign; i++)
      if (MinDistance(chouse[j], Mod(chouse[i]-ZFromS(i+3))) > rDegQuad)
        chouse[i] = Mod(chouse[i]+rDegHalf);
    for (i = 1; i <= cSign; i++)
      if (RAbs(MinDistance(chouse[i], planet[oAsc - 1 + i])) > rDegQuad)
        planet[oAsc - 1 + i] = Mod(planet[oAsc - 1 + i]+rDegHalf);

#ifdef SWISS
    // May want to recalculate cusps based on the new position of the MC.
    if (us.fProgRAMC) {
      t = Ratio(t1, t2, us.rRatio);
      fSav = us.fGeodetic; us.fGeodetic = fTrue;
      rLon = Tropical(planet[oMC]); rLat = 0.0;
      EclToEqu(&rLon, &rLat);
      rLon = Untropical(rLon);
      rLat = Ratio(ciMain.lat, ciTwin.lat, rRatio);
      SwissHouse(t, rDegMax - rLon, rLat, us.nHouseSystem,
        &rT, &rT, &rT, &rT, &rT, &rT, &rT, &rT);
      us.fGeodetic = fSav;
      for (i = 1; i <= cSign; i++)
        planet[cuspLo-1 + i] = chouse[i];
    }
#endif

  // For the -rm time space midpoint chart, calculate the midpoint time and
  // place between the two charts and then recast for the new chart info.

  } else if (us.nRel == rcMidpoint) {
    is.T = Ratio(t1, t2, us.rRatio);
    t = (is.T*36525.0)+rRound; is.JD = RFloor(t)+2415020.0;
    TT = RFract(t)*24.0;
    ZZ = Ratio(GetOffsetCI(&ciMain), GetOffsetCI(&ciTwin), us.rRatio);
    TT -= ZZ;
    if (TT < 0.0) {
      TT += 24.0; is.JD -= 1.0;
    }
    JulianToMdy(is.JD, &MM, &DD, &YY);
    if (!us.fHouse3D) {
      // Take midpoint of longitude and latitude separately.
      OO = Ratio(Lon, ciTwin.lon, us.rRatio);
      if (RAbs(ciTwin.lon-Lon) > rDegHalf)
        OO = Mod(OO + rDegMax*us.rRatio);
      AA = Ratio(Lat, ciTwin.lat, us.rRatio);
    } else {
      // Take true midpoint along great circle between the two locations.
      SphRatio(Lon, Lat, ciTwin.lon, ciTwin.lat, us.rRatio, &OO, &AA);
    }
    ciMain = ciCore;
    CastChart(0);
    // The console builds cast once and exit, so they clear the mode here
    // rather than carry it. A GUI must keep it: the menu shows it as the
    // current mode, and SetRel()/SetRelQt() test it to know to put the
    // original chart back when the user leaves midpoint mode. Cleared
    // here, that restore never fires and the loaded chart is silently
    // replaced by the midpoint for the rest of the session.
#if !defined(WIN) && !defined(QT)
    us.nRel = rcNone;  // Turn off so don't move to midpoint again.
#endif

  // There are a couple of non-astrological charts, which only require the
  // number of days that have passed between the two charts to be done.

  } else {
    is.JD = RAbs(t2-t1)*36525.0;
    cp0 = cp1;
  }

  ComputeInHouses();
}


/*
******************************************************************************
** Other Chart Display Routines.
******************************************************************************
*/

// Given two objects and an aspect between them, or an object and a sign that
// it's entering, print if this is a "major" event, such as a season change or
// major lunar phase. This is called from the ChartInDay() searching and
// influence routines. Do an interpretation if need be too.

void PrintInDayEvent(int source, int aspect, int dest, int nVoid)
{
  char sz[cchSzDef];
  int nEclipse, nEclipse2;
  real rPct, rPct2;
  flag fSwap;

  // If the Sun changes sign, then print out if this is a season change.
  if (aspect == aSig) {
    if (source == oSun) {
      AnsiColor(kWhiteA);
      if (dest == sAri || dest == sLib) {
        if ((dest == sAri) == (AA >= 0.0))
          PrintSz(" (Spring Equinox)");
        else
          PrintSz(" (Autumn Equinox)");
      } else if (dest == sCan || dest == sCap) {
        if ((dest == sCan) == (AA >= 0.0))
          PrintSz(" (Summer Solstice)");
        else
          PrintSz(" (Winter Solstice)");
      }
    }

  } else if (aspect > 0 && !us.fParallel) {
    fSwap = (dest == oSun);
    if (fSwap)
      SwapN(source, dest);

    // Print if the present aspect is a New, Full, or Half Moon.
    if (source == oSun && (dest == oMoo || FMoons(dest)) &&
      (us.fMoonMove || ObjOrbit(dest) == us.objCenter)) {
      if (aspect <= aSqu)
        AnsiColor(kWhiteA);
      if (aspect == aCon)
        PrintSz(" (New Moon)");
      else if (aspect == aOpp) {
        PrintSz(" (Full Moon)");
        // Full Moons may be a lunar eclipse.
        if (us.fEclipse) {
          nEclipse = NCheckEclipseLunar(us.objCenter, dest, oSun, &rPct);
          if (nEclipse > etNone) {
            AnsiColor(kWhiteA);
            sprintf2(S(sz), " (%s Lunar Eclipse", szEclipse[nEclipse]);
            PrintSz(sz);
            if (us.fSeconds) {
              sprintf2(S(sz), " %.0f%%", rPct); PrintSz(sz);
            }
            PrintSz(")");
          }
        }
      } else if (aspect == aSqu)
        PrintSz(" (Half Moon)");
    } else if (us.fEclipse && aspect == aOpp) {
      // Check for generic opposition that's an eclipse.
      nEclipse = NCheckEclipseLunar(us.objCenter, dest, source, &rPct);
      if (nEclipse > etNone) {
        nEclipse2 = NCheckEclipseLunar(us.objCenter, source, dest, &rPct2);
        if (nEclipse2 > nEclipse) {
          nEclipse = nEclipse2;
          rPct = rPct2;
        }
        AnsiColor(kWhiteA);
        sprintf2(S(sz), " (%s Occultation", szEclipse[nEclipse]);
        PrintSz(sz);
        if (us.fSeconds) {
          sprintf2(S(sz), " %.0f%%", rPct); PrintSz(sz);
        }
        PrintSz(")");
      }
    }

    // Conjunctions may be a solar eclipse or other occultation.
    if (us.fEclipse && aspect == aCon) {
      nEclipse = NCheckEclipse(source, dest, &rPct);
      if (nEclipse > etNone) {
        AnsiColor(kWhiteA);
        sprintf2(S(sz), " (%s %s%s", szEclipse[nEclipse], source == oSun ?
          "Solar " : "", source == oSun && (dest == oMoo || FMoons(dest)) &&
          (us.fMoonMove || ObjOrbit(dest) == us.objCenter) ?
          "Eclipse" : "Occultation"); PrintSz(sz);
        if (us.fSeconds) {
          sprintf2(S(sz), " %.0f%%", rPct); PrintSz(sz);
        }
        PrintSz(")");
      }
    }
    if (fSwap)
      SwapN(source, dest);
  }

  // Print if the present aspect is the Moon going void of course.
  if (nVoid >= 0) {
    AnsiColor(kDefault);
    sprintf2(S(sz), " (v/c %d:%02d", nVoid / 3600, nVoid / 60 % 60); PrintSz(sz);
    if (us.fSeconds) {
      sprintf2(S(sz), ":%02d", nVoid % 60); PrintSz(sz);
    }
    PrintCh(')');
  }
  PrintL();

#ifdef INTERPRET
  if (us.fInterpret)
    InterpretInDay(source, aspect, dest);
#endif
  AnsiColor(kDefault);
}


// Given two objects and an aspect (or one object, and an event such as a sign
// or direction change) display the configuration in question. This is called
// by the many charts which list aspects among items, such as the -a aspect
// lists, -m midpoint lists, -d aspect in day search and -D influence charts,
// and -t transit search and -T influence charts.

void PrintAspect(int obj1, real pos1, real ret1, int asp,
  int obj2, real pos2, real ret2, char chart)
{
  char sz[cchSzDef];
  KI ki;
  real rDiff;
  flag fPar = (us.fParallel && asp >= aCon) || asp == aAlt, fDis, fLon,
    fSecond = us.fSeconds, fRound, fWax;

  fDis = us.fDistance && !us.fParallel && asp >= aCon &&
    !(chart == 'm' || chart == 'M' ||
    chart == 'd' || chart == 'e' || chart == 't' || chart == 'u');
  fLon = !fPar && !fDis;
  if (fDis) {
    // Express distance values as proportions of each other from 0-100%.
    if (pos1 > pos2) {
      pos2 = pos2/pos1*99.999999; pos1 = 99.999999;
    } else if (pos1 < pos2) {
      pos1 = pos1/pos2*99.999999; pos2 = 99.999999;
    } else
      pos1 = pos2 = 99.999999;
  }

  if (asp >= aCon && rgobjList2[obj1] > rgobjList2[obj2] &&
    !(chart == 'A' || chart == 'M' || chart == 't' || chart == 'T' ||
    chart == 'e' || chart == 'u' || chart == 'U' || chart == '8')) {
    SwapN(obj1, obj2); SwapR(&pos1, &pos2); SwapR(&ret1, &ret2);
  }
  AnsiColor(kObjA[obj1]);
  if (chart == 't' || chart == 'T')
    PrintSz("trans ");
  else if (chart == 'e' || chart == 'u' || chart == 'U')
    PrintSz("progr ");

  // Print name of first planet.
  sprintf(sz, "%7.7s", szObjDisp[obj1]); PrintSz(sz);
  ki = fLon ? kSignA(SFromZ(pos1)) : kDefault;
  AnsiColor(ki);
  sprintf(sz, " %c", ret1 > 0.0 ? '(' : (ret1 < 0.0 ? '[' : '<')); PrintSz(sz);
  if (asp == aSig && ret1 > 0.0)
    pos1 += 29.9999999;
  else if (asp == aDeg)
    pos1 = (real)obj2 * (rDegMax / (real)(cSign * us.nSignDiv));
  if (!us.fSeconds) {
    if (fLon) {
      if (us.nDegForm == df360)
        sprintf(sz, "%3d", (int)pos1);
      else if (us.nDegForm == dfHM)
        sprintf(sz, "%2dh", (int)pos1/15);
      else if (us.nDegForm == dfNak)
        sprintf(sz, "%.3s", szNakshatra[(int)(pos1 / (rDegMax/27.0)) + 1]);
      else
        sprintf(sz, "%.3s", szSignName[SFromZ(pos1)]);
    } else if (fPar)
      sprintf(sz, "%c%2d", pos1 < 0 ? '-' : '+', (int)RAbs(pos1));
    else
      sprintf(sz, "%2d%%", (int)pos1);
    PrintSz(sz);
  } else {
    if (!us.fSecond1K)
      us.fSeconds = fFalse;
    if (fLon) {
      if (asp == aSig) {
        fRound = us.fRound; us.fRound = fFalse;
      }
      PrintZodiac(pos1);
      if (asp == aSig)
        us.fRound = fRound;
    } else if (fPar) {
      PrintAltitude(pos1);
    } else {
      sprintf(sz, "%f", pos1);
      sprintf(sz + (!us.fSecond1K ? 6 : 9), "%s", "%"); PrintSz(sz);
    }
    us.fSeconds = fSecond;
    AnsiColor(ki);
  }
  sprintf(sz, "%c", ret1 > 0.0 ? ')' : (ret1 < 0.0 ? ']' : '>')); PrintSz(sz);
  PrintCh(' ');

  // Mark aspect with wax/wan for charts that don't already include it.
  if (us.nAppSep == 2 &&
    !(chart == 'a' || chart == 'A' || chart == 'm' || chart == 'M' ||
    chart == 'D' || chart == 'T' || chart == 'U' || chart == '8')) {
    if (asp > aOpp) {
      rDiff = MinDifference(pos2, pos1);
      if (chart == 't' || chart == 'T')
        fWax = (rDiff >= 0.0);
      else
        fWax = (RSgn2(ret1 - ret2) * rDiff >= 0.0);
      AnsiColor(fWax ? kWhiteA : kDkGrayA);
      PrintCh(rgchAppSep[5-fWax]);
    } else
      PrintCh(' ');
  }

  // Print name of aspect or other event.
  AnsiColor(asp > 0 ? kAspA[ASPT(asp)] : kWhiteA);
  if (asp == aSig || asp == aHou)
    sprintf(sz, ret1 >= 0.0 ? "-->" : "<--");  // Print a sign change.
  else if (asp == aDir)
    sprintf(sz, "S/%c", obj2 ? chRet : 'D');   // Print a direction change.
  else if (asp == aDeg)
    sprintf(sz, "At:");                        // Print a degree change.
  else if (asp == aAlt)
    sprintf(sz, "LA%c", obj2 ? '+' : '-');     // Print a latitude extreme.
  else if (asp == aLen)
    sprintf(sz, "%s", obj2 ? "Apo" : "Per");   // Print a distance extreme.
  else if (asp == aNod)
    sprintf(sz, "LA0");                        // Print at latitude zero.
  else if (asp == aDis)
    sprintf(sz, "EqD");                        // Print at equal distance.
  else if (asp == 0)
    sprintf(sz, chart == 'm' ? "&" : "with");
  else
    sprintf(sz, "%s", SzAspectAbbrev(asp));    // Print an aspect.
  PrintSz(sz);
  if (asp != aDir && asp != aAlt)
    PrintCh(' ');

  // Print name of second planet or event target.
  if (chart == 'A')
    PrintSz("with ");
  if (asp == aSig) {
    AnsiColor(kSignA(obj2));
    sprintf(sz, "%s", szSignName[obj2]); PrintSz(sz);
  } else if (asp == aDeg) {
    PrintZodiac((real)obj2 * (rDegMax / (real)(cSign * us.nSignDiv)));
  } else if (asp == aHou) {
    AnsiColor(kSignA(obj2));
    if (chart == 't' || chart == 'u' || chart == 'T' || chart == 'U')
      PrintSz("natal ");
    sprintf(sz, "%d%s 3D House", obj2, szSuffix[obj2]); PrintSz(sz);
  } else if (asp == aNod) {
    AnsiColor(kElemA[obj2*2+1]);
    sprintf(sz, "%s", rgszDir[obj2*2]); PrintSz(sz);
  } else if (asp >= 0 || asp == aDis) {
    ki = fLon ? kSignA(SFromZ(pos2)) : kDefault;
    AnsiColor(ki);
    if (chart == 't' || chart == 'u' || chart == 'T' || chart == 'U')
      PrintSz("natal ");
    sprintf(sz, "%c", ret2 > 0.0 ? '(' : (ret2 < 0.0 ? '[' : '<'));
    PrintSz(sz);
    if (!us.fSeconds) {
      if (fLon) {
        if (us.nDegForm == df360)
          sprintf(sz, "%3d", (int)pos2);
        else if (us.nDegForm == dfHM)
          sprintf(sz, "%2dh", (int)pos2/15);
        else if (us.nDegForm == dfNak)
          sprintf(sz, "%.3s", szNakshatra[(int)(pos2 / (rDegMax/27.0)) + 1]);
        else
          sprintf(sz, "%.3s", szSignName[SFromZ(pos2)]);
      } else if (fPar)
        sprintf(sz, "%c%2d", pos2 < 0 ? '-' : '+', (int)RAbs(pos2));
      else
        sprintf(sz, "%2d%%", (int)pos2);
      PrintSz(sz);
    } else {
      if (!us.fSecond1K)
        us.fSeconds = fFalse;
      if (fLon)
        PrintZodiac(pos2);
      else if (fPar)
        PrintAltitude(pos2);
      else {
        sprintf(sz, "%f", pos2);
        sprintf(sz + (!us.fSecond1K ? 6 : 9), "%s", "%"); PrintSz(sz);
      }
      us.fSeconds = fSecond;
      AnsiColor(ki);
    }
    sprintf(sz, "%c ", ret2 > 0.0 ? ')' : (ret2 < 0.0 ? ']' : '>'));
    PrintSz(sz);
    AnsiColor(kObjA[obj2]);
    sprintf(sz, "%.10s", szObjDisp[obj2]); PrintSz(sz);
  }

  if (chart == 'D' || chart == 'T' || chart == 'U' || chart == 'a' ||
    chart == 'A' || chart == 'm' || chart == 'M' || chart == '8')
    PrintTab(' ', 10-CchSz(szObjDisp[obj2]));
}


// Based on the given chart information, display all the aspects taking place
// in the chart, as specified with the -D switch. The aspects are printed in
// order of influence determined by treating them as happening outside among
// transiting planets, such that rare outer planet aspects are given more
// power than common ones among inner planets. (This is almost identical to
// the -a list, except the influences are different.)

void ChartInDayInfluence(void)
{
  int source[MAXINDAY], aspect[MAXINDAY], dest[MAXINDAY], ca[cAspect + 1],
    co[objMax], occurcount = 0, i, j, k, l, nSav;
  real power[MAXINDAY], rPowSum = 0.0, rT;
  char sz[cchSzDef], *pch;
  flag fDistance = us.fDistance && !us.fParallel, f;

  ClearB((pbyte)ca, sizeof(ca));
  ClearB((pbyte)co, sizeof(co));

  // Go compute the aspects in the chart.

  if (!FCreateGrid(fFalse))
    return;

  // Search through the grid and build up the list of aspects.

  for (j = 1; j <= is.nObj; j++) {
    if (FIgnore(j))
      continue;
    for (i = 0; i < j; i++) {
      if (FIgnore(i) || (k = grid->n[i][j]) == 0 || occurcount >= MAXINDAY)
        continue;
      source[occurcount] = i; aspect[occurcount] = k; dest[occurcount] = j;
      rT = grid->v[i][j];
      power[occurcount] = (RTransitInf(i)/4.0) * (RTransitInf(j)/4.0) *
        rAspInf[ASPT(k)]*(1.0-(real)RAbs(rT)/GetOrb(i, j, k));
      rPowSum += power[occurcount];
      ca[k]++;
      co[i]++; co[j]++;
      occurcount++;
    }
  }

  // Sort aspects by order of influence.

  for (i = 1; i < occurcount; i++) {
    j = i-1;
    while (j >= 0) {
      k = j+1;
      switch (us.nAspectSort) {
      default:  f = power[j] > power[k]; break;
      case aso: f = RAbs(grid->v[source[j]][dest[j]]) <
                    RAbs(grid->v[source[k]][dest[k]]); break;
      case asn: f = grid->v[source[j]][dest[j]] <
                    grid->v[source[k]][dest[k]]; break;
      case asO: f = rgobjList2[source[j]]*cObj + rgobjList2[dest[j]] <
                    rgobjList2[source[k]]*cObj + rgobjList2[dest[k]]; break;
      case asP: f = rgobjList2[dest[j]]*cObj + rgobjList2[source[j]] <
                    rgobjList2[dest[k]]*cObj + rgobjList2[source[k]]; break;
      case asA: f = aspect[j]*cObj*cObj + source[j]*cObj + dest[j] <
                    aspect[k]*cObj*cObj + source[k]*cObj + dest[k]; break;
      case asC: f = planet[source[j]] < planet[source[k]]; break;
      case asD: f = planet[dest[j]] < planet[dest[k]]; break;
      case asM: f = Midpoint(planet[dest[j]], planet[source[j]]) <
                    Midpoint(planet[dest[k]], planet[source[k]]); break;
      }
      if (f)
        break;
      SwapN(source[j], source[j+1]);
      SwapN(aspect[j], aspect[j+1]);
      SwapN(dest[j], dest[j+1]);
      SwapR(&power[j], &power[j+1]);
      j--;
    }
  }

  // Now display each aspect line.

  for (i = 0; i < occurcount; i++) {
    sprintf2(S(sz), "%3d: ", i+1); PrintSz(sz);
    j = source[i]; k = aspect[i]; l = dest[i];
    PrintAspect(j, planetval(j), planetdir(j), k,
      l, planetval(l), planetdir(l), 'D');
    rT = grid->v[j][l];
    AnsiColor(rT < 0.0 ? kWhiteA : kLtGrayA);
    if (fDistance) {
      nSav = us.nDegForm; us.nDegForm = df360;
    }
    sprintf2(S(sz), " - %s %s", szAppSep[us.nAppSep*2 + (rT >= 0.0)],
      SzDegree2(RAbs(rT)));
    if (fDistance) {
      us.nDegForm = nSav;
      for (pch = sz; *pch; pch++)
        ;
      pch[-1] = '%';
    }
    PrintSz(sz);
    AnsiColor(kDkGreenA);
    PrintSz(" - power:");
    sprintf2(S(sz), us.fSeconds ? "%8.4f" : "%6.2f", power[i]); PrintSz(sz);
    PrintInDayEvent(j, k, l, -1);
  }
  if (occurcount == 0)
    PrintSz("Empty transit aspect list.\n");
  PrintAspectSummary(ca, co, occurcount, rPowSum);
}


// Given an arbitrary day, determine what aspects are made between this
// transiting chart and the given natal chart, as specified with the -T
// switch, and display the transits in order sorted by influence.

void ChartTransitInfluence(flag fProg)
{
  int source[MAXINDAY], aspect[MAXINDAY], dest[MAXINDAY], ca[cAspect + 1],
    co[objMax], occurcount = 0, fProgress = us.fProgress, i, j, k, l, m, nSav;
  real power[MAXINDAY], rPowSum = 0.0, rT;
  char sz[cchSzDef], *pch;
  byte ignoreSav[objMax];
  flag fDistance = us.fDistance && !us.fParallel, f;

  ClearB((pbyte)ca, sizeof(ca));
  ClearB((pbyte)co, sizeof(co));

  if (!FNoTimeOrSpace(ciTran)) {
    PrintSz("Transits at: ");
    i = DayOfWeek(MonT, DayT, YeaT);
    sprintf2(S(sz), "%.3s %s %s (%cT Zone %s)\n", szDay[i],
      SzDate(MonT, DayT, YeaT, 3), SzTim(TimT), ChDst(DstT),
      SzZone(ZonT)); PrintSz(sz);
  }

  // Cast the natal and transiting charts as with a relationship chart.

  cp1 = cp0;
  CopyRgb(ignore.rgn, ignoreSav, sizeof(ignore.rgn));
  for (i = 0; i <= is.nObj; i++)
    ignore[i] = ignore2[i];
  ciCore = ciTran;
  if (us.fProgress = fProg) {
    is.JDp = MdytszToJulian(MM, DD, YY, TT, SS, ZZ);
    ciCore = ciMain;
  }
  CastChart(0);
  cp2 = cp0;
  CopyRgb(ignoreSav, ignore.rgn, sizeof(ignore.rgn));

  // Do a relationship aspect grid to get the transits. Have to make and
  // restore two changes to get it right for this chart: (1) Make the natal
  // planets have zero velocity so applying vs. separating is only a function
  // of the transiter. (2) Tweak the main restrictions to allow for transiting
  // objects not restricted.

  for (i = 0; i <= is.nObj; i++) {
    ret[i] = cp1.dir[i];
    cp1.dir[i] = 0.0;
    ignore[i] = ignore[i] && ignore2[i];
  }
  f = FCreateGridRelation(fFalse);
  CopyRgb(ignoreSav, ignore.rgn, sizeof(ignore.rgn));
  for (i = 0; i <= is.nObj; i++)
    cp1.dir[i] = ret[i];
  if (!f)
    return;

  // Loop through the grid, and build up a list of the valid transits.

  for (i = 0; i <= is.nObj; i++) {
    if (FIgnore2(i))
      continue;
    for (j = 0; j <= is.nObj; j++) {
      if (FIgnore(j) || (is.fReturn && i != j) || (k = grid->n[i][j]) == 0 ||
        occurcount >= MAXINDAY)
        continue;
      source[occurcount] = i; aspect[occurcount] = k; dest[occurcount] = j;
      rT = grid->v[i][j];
      power[occurcount] = RTransitInf(i) * (RObjInf(j)/4.0) *
        rAspInf[ASPT(k)] * (1.0-(real)RAbs(rT)/GetOrb(i, j, k));
      rPowSum += power[occurcount];
      ca[k]++;
      co[i]++; co[j]++;
      occurcount++;
    }
  }

  // After all transits located, sort them by their total power.

  for (i = 1; i < occurcount; i++) {
    j = i-1;
    while (j >= 0) {
      k = j+1;
      switch (us.nAspectSort) {
      default:  f = power[j] > power[k]; break;
      case aso: f = RAbs(grid->v[source[j]][dest[j]]) <
                    RAbs(grid->v[source[k]][dest[k]]); break;
      case asn: f = grid->v[source[j]][dest[j]] <
                    grid->v[source[k]][dest[k]]; break;
      case asO: f = rgobjList2[source[j]]*cObj + rgobjList2[dest[j]] <
                    rgobjList2[source[k]]*cObj + rgobjList2[dest[k]]; break;
      case asP: f = rgobjList2[dest[j]]*cObj + rgobjList2[source[j]] <
                    rgobjList2[dest[k]]*cObj + rgobjList2[source[k]]; break;
      case asA: f = aspect[j]*cObj*cObj + source[j]*cObj + dest[j] <
                    aspect[k]*cObj*cObj + source[k]*cObj + dest[k]; break;
      case asC: f = cp2.obj[source[j]] < cp2.obj[source[k]]; break;
      case asD: f = cp1.obj[dest[j]] < cp1.obj[dest[k]]; break;
      case asM: f = Midpoint(cp1.obj[dest[j]], cp2.obj[source[j]]) <
                    Midpoint(cp1.obj[dest[k]], cp2.obj[source[k]]); break;
      }
      if (f)
        break;
      SwapN(source[j], source[k]);
      SwapN(aspect[j], aspect[k]);
      SwapN(dest[j], dest[k]);
      SwapR(&power[j], &power[k]);
      j--;
    }
  }

  // Now loop through list and display each transit in effect at the time.

  for (i = 0; i < occurcount; i++) {
    k = aspect[i]; l = source[i]; m = dest[i];
    sprintf2(S(sz), "%3d: ", i+1); PrintSz(sz);
    PrintAspect(l, planetval2(l), planetdir2(l), k,
      dest[i], planetval1(m), planetdir1(m), fProg ? 'U' : 'T');
    rT = grid->v[l][m];
    AnsiColor(rT < 0.0 ? kWhiteA : kLtGrayA);
    if (fDistance) {
      nSav = us.nDegForm; us.nDegForm = df360;
    }
    sprintf2(S(sz), " - %s %s", szAppSep[us.nAppSep*2 + (rT >= 0.0)],
      SzDegree2(RAbs(rT)));
    if (fDistance) {
      us.nDegForm = nSav;
      for (pch = sz; *pch; pch++)
        ;
      pch[-1] = '%';
    }
    PrintSz(sz);
    AnsiColor(kDkGreenA);
    PrintSz(" - power: ");
    sprintf2(S(sz), us.fSeconds ? "%7.4f" : "%5.2f", power[i]); PrintSz(sz);
    if (k == aCon && l == dest[i]) {    // Print a "R" to mark returns.
      AnsiColor(kWhiteA);
      PrintSz(" R");
      if (us.fSeconds)
        PrintSz("eturn");
    }
    PrintL();
#ifdef INTERPRET
    if (us.fInterpret)
      InterpretTransit(l, k, dest[i]);
#endif
    AnsiColor(kDefault);
  }

  if (occurcount == 0)
    PrintSz("Empty transit list.\n");
  PrintAspectSummary(ca, co, occurcount, rPowSum);
  us.fProgress = fProgress;
  ciCore = ciMain;
  CastChart(1);
}


// Given the zodiac location of a planet in the sky and its declination, and
// a location on the Earth, compute the azimuth and altitude of where on the
// local horizon sky the planet would appear to one at the given location. A
// reference MC position at Greenwich is also needed for this.

void EclToHoriz(real *azi, real *alt, real obj, real objalt, real mc,
  real lat)
{
  real lonz, latz;

  lonz = obj; latz = objalt;
  EclToEqu(&lonz, &latz);
  lonz = Mod(mc - lonz + rDegQuad);
  EquToLocal(&lonz, &latz, rDegQuad - lat);
  if (us.fRefract)
    latz = SwissRefract(latz);
  *azi = rDegMax - lonz; *alt = latz;
}


// Display a calendar for the given month in the chart, as specified with the
// -K switch. When color is on, the title is white, weekends are highlighted
// in red, and the specific day in the chart is colored green.

void ChartCalendarMonth(void)
{
  char sz[cchSzDef];
  int mon = Mon, i, j, k;
  flag fMonday = us.fIndian;

  if (mon < mJan)
    mon = mJan;
  AnsiColor(kWhiteA);
  PrintTab(' ', (16-CchSz(szMonth[mon])) >> 1);
  sprintf2(S(sz), "%s%5d\n", szMonth[mon], Yea); PrintSz(sz);
  for (i = 0; i < cWeek; i++) {
    j = !fMonday ? i : (i + 1) % 7;
    sprintf2(S(sz), "%.2s%c", szDay[j], i < cWeek-1 ? ' ' : '\n');
    PrintSz(sz);
  }
  j = DayOfWeek(mon, 1, Yea);
  if (fMonday)
    j = (j + 6) % 7;
  AnsiColor(kDefault);
  for (i = 0; i < j; i++) {
    if (i == (!fMonday ? 0 : cWeek-2))
      AnsiColor(kRedA);
    PrintSz("-- ");
    if (i == 0)
      AnsiColor(kDefault);
  }
  k = DayInMonth(mon, Yea);
  for (i = 1; i <= k; i = AddDay(mon, i, Yea, 1)) {
    if (i == Day)
      AnsiColor(kGreenA);
    else if (!fMonday ? (j == 0 || j == cWeek-1) : (j >= cWeek-2))
      AnsiColor(kRedA);
    sprintf2(S(sz), "%2d", i); PrintSz(sz);
    if (j == 0 || j == cWeek-1 || i == Day)
      AnsiColor(kDefault);
    if (j < cWeek-1) {
      j++;
      PrintCh(' ');
    } else {
      j = 0;
      PrintL();
    }
  }
  while (j > 0 && j < cWeek) {
    if (j == (!fMonday ? cWeek-1 : cWeek-2))
      AnsiColor(kRedA);
    j++;
    sprintf2(S(sz), "--%c", j < cWeek ? ' ' : '\n'); PrintSz(sz);
  }
  AnsiColor(kDefault);
}


// Display a calendar for the entire year given in the chart, as specified
// with the -Ky switch. This is just like twelve of the individual month
// calendars above displayed together, with same color highlights and all.

void ChartCalendarYear(void)
{
  char sz[cchSzDef];
  int r, w, c, m, d, dy, p[3], l[3], n[3];
  flag fMonday = us.fIndian;

  dy = DayOfWeek(1, 1, Yea);
  if (fMonday)
    dy = (dy + 6) % 7;
  for (r = 0; r < 4; r++) {     // Loop over one set of three months.
    AnsiColor(kWhiteA);
    for (c = 0; c < 3; c++) {
      m = r*3+c+1;
      PrintTab(' ', (16-CchSz(szMonth[m])) >> 1);
      sprintf2(S(sz), "%s%5d", szMonth[m], Yea); PrintSz(sz);
      if (c < 2)
        PrintTab(' ', 20 + MONTHSPACE -
          ((16-CchSz(szMonth[m])) >> 1) - CchSz(szMonth[m]) - 5);
    }
    PrintL();
    for (c = 0; c < 3; c++) {
      for (d = 0; d < cWeek; d++) {
        m = !fMonday ? d : (d + 1) % 7;
        sprintf2(S(sz), "%.2s%c", szDay[m], d < cWeek-1 || c < 2 ? ' ' : '\n');
        PrintSz(sz);
      }
      if (c < 2)
        PrintTab(' ', MONTHSPACE-1);
      m = r*3+c+1;
      p[c] = dy % cWeek;
      l[c] = DayInMonth(m, Yea);
      n[c] = 0;
      dy += DaysInMonth(m, Yea);
    }
    for (w = 0; w < cWeek-1; w++) {    // Loop over one set of week rows.
      for (c = 0; c < 3; c++) {        // Loop over one week in a month.
        m = r*3+c+1;
        d = 0;
        if (w == 0)
          while (d < p[c]) {
            if (!fMonday ? (d == 0) : (d == cWeek-2))
              AnsiColor(kRedA);
            PrintSz("-- ");
            if (d == 0)
              AnsiColor(kDefault);
            d++;
          }
        AnsiColor(kDefault);
        while (d < cWeek && n[c] < l[c]) {
          n[c] = AddDay(m, n[c], Yea, 1);
          if (n[c] == Day && m == Mon)
            AnsiColor(kGreenA);
          else if (!fMonday ? (d == 0 || d == cWeek-1) : (d >= cWeek-2))
            AnsiColor(kRedA);
          sprintf2(S(sz), "%2d%c", n[c], d < cWeek-1 || c < 2 ? ' ' : '\n');
          PrintSz(sz);
          if (d == 0 || d == cWeek-1 || (n[c] == Day && m == Mon))
            AnsiColor(kDefault);
          d++;
        }
        while (d < cWeek) {
          if (!fMonday ? (d == 0 || d == cWeek-1) : (d >= cWeek-2))
            AnsiColor(kRedA);
          sprintf2(S(sz), "--%c", d < cWeek-1 || c < 2 ? ' ' : '\n'); PrintSz(sz);
          if (d == 0)
            AnsiColor(kDefault);
          d++;
        }
        if (c < 2)
          PrintTab(' ', MONTHSPACE-1);
      }
    }
    if (r < 3)
      PrintL();
  }
  AnsiColor(kDefault);
}


CONST char *rgszDateDiff[11] = {"", "Years  ", "Months ", "Weeks  ", "Days   ",
  "Hours  ", "Minutes", "Seconds", "Longitude", "Latitude ", "Distance "};

// Display either a biorhythm chart or the time difference in various units
// between two charts, i.e. two types of relationship "charts" that aren't
// related in any way to planetary positions, as specified by either the
// -rb or -rd switches, respectively.

void DisplayRelation(void)
{
  char sz[cchSzDef], szT[cchSzDef];
  int i;
  real k, l;
#ifdef BIORHYTHM
  int j;
#endif

  // If calculating the difference between two dates, then display the value
  // and return, as with the -rd switch.

  if (us.nRel == rcDifference) {
    PrintSz("Differences between the dates in the two charts:\n");
    k = RAbs(ciMain.lon - ciTwin.lon);
    l = RAbs(ciMain.lat - ciTwin.lat);
    for (i = 1; i <= 10; i++) {
      AnsiColor(i <= cRainbow ? kRainbowA[i] : kDefault);
      sprintf2(S(szT), "%s: %%.%dlf", rgszDateDiff[i],
        2 + us.fSeconds*(2 - (i == 7)));
      switch (i) {
      case 1: sprintf2(S(sz), szT, is.JD/rDayInYear);    break;
      case 2: sprintf2(S(sz), szT, is.JD/(rDayInYear/12.0)); break;
      case 3: sprintf2(S(sz), szT, is.JD/7.0);           break;
      case 4: sprintf2(S(sz), szT, is.JD);               break;
      case 5: sprintf2(S(sz), szT, is.JD*24.0);          break;
      case 6: sprintf2(S(sz), szT, is.JD*(24.0*60.0));   break;
      case 7: sprintf2(S(sz), szT, is.JD*(24.0*3600.0)); break;
      case 8: sprintf2(S(sz), szT, k);                 break;
      case 9: sprintf2(S(sz), szT, l);                 break;
      case 10:
        l = SphDistance(ciMain.lon, ciMain.lat, ciTwin.lon, ciTwin.lat);
        sprintf2(S(sz), szT, l); PrintSz(sz);
        sprintf2(S(szT), " (%%.%dlf %%s)", 2 + us.fSeconds*2);
        sprintf2(S(sz), szT, l / 360.0 * (us.fEuroDist ? 40075.0 : 24901.0),
          us.fEuroDist ? "km" : "miles");
        break;
      }
      PrintSz(sz);
      PrintL();
    }
    AnsiColor(kDefault);
    return;
  }

#ifdef BIORHYTHM
  // If doing a biorhythm (-rb switch), then calculate it for someone born on
  // the older date, at the time of the younger date. Loop through the days
  // preceeding and following the date in question.

  is.JD = RFloor(is.JD + rRound);
  for (is.JD -= (real)(us.nBioday/2), i = -us.nBioday/2; i <= us.nBioday/2;
    i++, is.JD += 1.0) {
    if (i == 0)
      AnsiColor(kWhiteA);
    else if (i == 1)
      AnsiColor(kDefault);
    j = NAbs(i);
    sprintf2(S(sz), "T%c%d%sDay%c:", i < 0 ? '-' : '+', j,
      j < 10 ? " " : "", j != 1 ? 's' : ' '); PrintSz(sz);
    for (j = 1; j <= 3; j++) {
      PrintCh(' ');
      AnsiColor(j <= 1 ? kRedA : (j == 2 ? kBlueA : kYellowA));
      switch (j) {
      case 1: k = brPhy; PrintSz("Physical");     break;
      case 2: k = brEmo; PrintSz("Emotional");    break;
      case 3: k = brInt; PrintSz("Intellectual"); break;
      }
      AnsiColor(i ? kDefault : kWhiteA);

      // The biorhythm calculation is below.

      l = RBiorhythm(is.JD, k);
      sprintf2(S(sz), " at %c%3.0f%%", l < 0.0 ? '-' : '+', RAbs(l)); PrintSz(sz);

      // Print smiley face, medium face, or sad face based on current cycle.

      AnsiColor(kDkGreenA);
      sprintf2(S(sz), " :%c", l > 50.0 ? ')' : (l < -50.0 ? '(' : '|'));
      PrintSz(sz);
      AnsiColor(i ? kDefault : kWhiteA);
      if (j < 3)
        PrintCh(',');
    }
    PrintL();
  }
#endif /* BIORHYTHM */
}

/* charts2.cpp */
