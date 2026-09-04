/*
** Astrolog (Version 8.00) File: astrolog.cpp
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
** Program Dispatch Procedures.
******************************************************************************
*/

// Initialize the Ansi color arrays with the color to print each object in.

void InitColors(void)
{
  CONST TBLOBJ *ptblRuler = &ruler1;
  int i, k;

  // Figure out which rulership set to use for "Element" color.
  // Order: Standard rulership, esoteric, Hierarchical, exaltation, Ray.
  if (ignore7[rrStd]) {
    if      (!ignore7[rrEso]) ptblRuler = &rgObjEso1;
    else if (!ignore7[rrHie]) ptblRuler = &rgObjHie1;
    else if (!ignore7[rrExa]) ptblRuler = &exalt;
    else if (!ignore7[rrRay]) ptblRuler = NULL;
  }

  // Determine and assign the color of each planet.
  for (i = 0; i <= oNorm; i++) {
    k = rgobjset[i].kolor;
    if (k == kRay || ptblRuler == NULL)
      k = kRayA[RAYT(rgObjRay[OBJT(i)])];
    else if (k == kElement)
      k = (*ptblRuler)[OBJT(i)] > 0 ?
        kElemA[((*ptblRuler)[OBJT(i)]-1) & 3] : kLtGray;
    else if (k == kPlanet) {
      // Moon colors are dim versions of the color of the planet they orbit.
      k = kObjA[!FBetween(i, cobLo, cobHi) ? ObjOrbit(i) :
        oJup + (i - cobLo)];
      if (FBetween(k, 0, cColor-1))
        k ^= 8;
      else {
        k = KvFromKi(k);
        k = -(int)Rgb(RgbR(k) >> 1, RgbG(k) >> 1, RgbB(k) >> 1);
      }
    }
    kObjA[i] = k;
  }

  // Determine and assign the color of each star.
  EnsureStarBright();
  k = rgobjset[starLo].kolor;
  for (i = starLo; i <= starHi; i++)
    kObjA[i] = (k >= cColor2 ? KStarA(rStarBright[i-starLo+1]) : k);
}


// A planet's ruler has changed to a new sign. Adjust appropriately the
// reverse table of mapping a sign to which planet(s) rule it.

void AdjustRulership(TBLSIG &rgRules1, TBLSIG &rgRules2, int sig, int obj,
  flag fPrimary)
{
  int i;

  if (sig > 0) {
    // Set the primary or secondary ruler, sliding existing ruler if needed.
    if (rgRules1[SIGT(sig)] != obj) {
      if (fPrimary) {
        rgRules2[SIGT(sig)] = rgRules1[SIGT(sig)];
        rgRules1[SIGT(sig)] = obj;
      } else
        rgRules2[SIGT(sig)] = obj;
    }
  } else {
    // A planet is set to rule nothing. Make sure no sign is ruled by it.
    for (i = 1; i <= cSign; i++) {
      if (rgRules1[SIGT(i)] == obj && rgRules2[SIGT(i)] >= 0) {
        rgRules1[SIGT(i)] = rgRules2[SIGT(i)];
        rgRules2[SIGT(i)] = -1;
      } else if (rgRules2[SIGT(i)] == obj)
        rgRules2[SIGT(i)] = -1;
    }
  }
}


// This is the dispatch procedure for the entire program. After all the
// command switches have been processed, this routine is called to actually
// call the various routines to generate and display the charts.

void Action(void)
{
  char sz[cchSzMax];
  int cSequenceLine = us.cSequenceLine, iList, iList2, iLine, i;
  flag fDoList, fHTML, fHTMLClip = fFalse;

  // If the -os switch is in effect, open a file and set a global to
  // internally 'redirect' all screen output to.

  if (is.szFileScreen) {
    is.S = fopen(is.szFileScreen, "w");
    if (is.S == NULL) {
      sprintf2(S(sz), "File %s can not be created.", is.szFileScreen);
      PrintError(sz);
      is.S = stdout;
    }
  } else
    is.S = stdout;
  is.cchRow = is.cchCol = is.cchColMax = 0;

  // If the -kh switch is in effect, start outputting a new HTML file.

  fHTML = us.fTextHTML && !us.fGraphics && is.S != stdout;
  if (fHTML) {
    fHTMLClip = is.nHTML < 0;
    is.nHTML = 2;
    if (fHTMLClip)
      PrintSz("Version:0.9\n"
        "StartHTML:00000161\n"
        "EndHTML:00010000\n"
        "StartFragment:00000196\n"
        "EndFragment:00010000\n");
    sprintf2(S(sz), "<html>\n<head><meta charset=\"UTF-8\"><title>Astrolog %s"
      "</title></head>\n<body>", szVersionCore); PrintSz(sz);
    if (fHTMLClip)
      PrintSz("\n<!--StartFragment -->\n");
    PrintSz("<font face=\"Courier\">");
    is.nHTML = 3;
  } else
    is.nHTML = 0;

  if (us.nCharsetOut == ccUTF8 && is.S != stdout)
    fprintf(is.S, "%c%c%c", 0xef, 0xbb, 0xbf);

  // If the -5e switch is in effect, loop over all charts in chart list.

  fDoList = (us.nListAll > 0 && !us.fGraphics && is.cci > 0 &&
    !(us.nListAll == 3 && is.cci < 2));
  iList = (us.nListAll == 3); iList2 = 0;
LNextList:
  if (fDoList) {
    is.iciIndex1 = iList; is.iciIndex2 = iList2;
    if (us.nListAll == 1)
      ciCore = ciMain = is.rgci[iList];
    else if (us.nListAll == 2)
      ciTwin = is.rgci[iList];
    else {
      ciCore = ciMain = is.rgci[iList];
      ciTwin = is.rgci[iList2];
    }
  }
  iLine = 0;

LNextLine:
  if (iLine < cSequenceLine && is.rgszLine[iLine] != NULL)
    FProcessCommandLine(is.rgszLine[iLine]);
  is.fMult = fFalse;
  is.fNoEphFile = fFalse;
  InitColors();
  AnsiColor(kDefault);

  // First adjust the restriction status of the cusps, Uranians, stars, and so
  // on, based on whether -C, -u, and -U switches are in effect.

  if (!us.fCusp)
    for (i = cuspLo; i <= cuspHi; i++)
      ignore[i] = ignore2[i] = fTrue;
  if (!us.fUranian)
    for (i = uranLo; i <= uranHi; i++)
      ignore[i] = ignore2[i] = fTrue;
  if (!us.fDwarf)
    for (i = dwarfLo; i <= dwarfHi; i++)
      ignore[i] = ignore2[i] = fTrue;
  if (!us.fMoons)
    for (i = moonsLo; i <= moonsHi; i++)
      ignore[i] = ignore2[i] = fTrue;
  if (!us.fCOB)
    for (i = cobLo; i <= cobHi; i++)
      ignore[i] = ignore2[i] = fTrue;
  if (!us.fStar)
    for (i = starLo; i <= starHi; i++)
      ignore[i] = ignore2[i] = fTrue;

  if (FPrintTables())    // Print out any generic tables specified.
    goto LDone;          // If nothing else to do, then exit right away.
  if (is.fMult) {
    PrintL2();
    is.fMult = fFalse;
  }

  // Here either do a normal chart or some kind of relationship chart.

  if (!us.nRel) {
#ifndef WIN
    // If chart info not in memory yet, then prompt the user for it.
    if (!is.fHaveInfo && !FInputData(szTtyCore))
      return;
    ciMain = ciCore;
    CastChart(1);
#else
    ciMain = ciCore;
    if (wi.fCast || cSequenceLine > 0 || fDoList) {
      wi.fCast = fFalse;
      CastChart(1);
    }
#endif
  } else {
    ciMain = ciCore;
    CastRelation();
  }
#ifndef WIN
  ciSave = ciMain;
#endif

#ifdef GRAPH
  if (us.fGraphics) {
    // If in -X graphics mode, go make a graphics chart.
    FActionX();
    iLine = cSequenceLine;    // Once any graphics drawn, stop looping!
  } else
#endif
  {
    // If not in graphics mode, print a text only chart on screen.
#ifdef GRAPH
    if (gs.fInverse) {
      SwapN(kBlackA, kWhiteA);
      SwapN(kLtGrayA, kDkGrayA);
      AnsiColor(kDefault);
    }
#endif
#ifdef EXPRESS
  // Notify AstroExpression a chart is about to be drawn.
  if (!us.fExpOff && FSzSet(us.szExpDisp1))
    ParseExpression(us.szExpDisp1);
#endif
    PrintChart(is.fProgress);
#ifdef EXPRESS
  // Notify AstroExpression a chart has just been drawn.
  if (!us.fExpOff && FSzSet(us.szExpDisp2))
    ParseExpression(us.szExpDisp2);
#endif
#ifdef GRAPH
    if (gs.fInverse) {
      SwapN(kBlackA, kWhiteA);
      SwapN(kLtGrayA, kDkGrayA);
    }
#endif
  }

LDone:
  iLine++;
  if (iLine < cSequenceLine) {
    if (!us.fGraphics)
      PrintL2();
    goto LNextLine;
  }
  if (fDoList) {
    if (us.nListAll >= 3) {
      iList2++;
      if (iList2 < (us.nListAll >= 4 ? is.cci : iList)) {
        PrintL2();
        goto LNextList;
      }
      iList2 = 0;
    }
    iList++;
    if (iList < is.cci) {
      PrintL2();
      goto LNextList;
    }
    is.iciIndex1 = is.iciIndex2 = -1;
  }

  if (fHTML) {           // If -kh switch in effect, end the HTML file.
    is.nHTML = 2;
    PrintSz("</font>\n</font>");
    if (fHTMLClip)
      PrintSz("\n<!--EndFragment-->\n");
    PrintSz("</body>\n</html>\n");
    is.nHTML = 0;
  }

  if (us.fWriteFile)     // If -o switch in effect, then write the chart
    FOutputData();       // information to a file.

  if (is.S != stdout)    // If were internally directing chart display to a
    fclose(is.S);        // file as with the -os switch, close it here.
}


#ifndef WIN
// Reset a few variables to their default values they have upon startup of the
// program. We don't reset all variables, just the most volatile ones. This is
// called when in the -Q loop to reset things like which charts to display,
// but leave setups such as object restrictions and orbs alone.

void InitVariables(void)
{
  us.fInterpret = us.fProgress = is.fHaveInfo = is.fMult = fFalse;
  us.nRel = rcNone;
  FCloneSz(NULL, &is.szFileScreen);
  ClearB((pbyte)&us.fListing, (int)((pbyte)&us.fLoop - (pbyte)&us.fListing));
}
#endif


/*
******************************************************************************
** Command Switch Procedures.
******************************************************************************
*/

// Given a string representing a command line (e.g. a macro string), go parse
// it into its various switches and parameters, then go process them and
// change program settings. Basically a wrapper for other functions.

flag FProcessCommandLine(CONST char *szLine)
{
  char szCommandLine[cchSzLine], *rgsz[MAXSWITCHES];
  int argc, cb;
  flag fT = fFalse;
  FILE *fileT;

  if (szLine == NULL || *szLine == chNull)
    return fTrue;
  cb = CchSz(szLine)+1;

  // Check for filename on command line.
  if (!FChSwitch(szLine[0])) {
    fileT = fopen(szLine, "r");
    if (fileT != NULL) {
      fclose(fileT);
      sprintf2(S(szCommandLine), "-i \"%s\"", szLine);
      fT = fTrue;
    }
  }

  // Parse and process the command line.
  if (!fT)
    CopyRgb((byte *)szLine, (byte *)szCommandLine, cb);
  argc = NParseCommandLine(szCommandLine, rgsz);
  fT = FProcessSwitches(argc, rgsz, NULL);
  if (!fT) {
    sprintf2(S(szCommandLine), "Failed to parse command line: %s", szLine);
    PrintWarning(szCommandLine);
  }
  return fT;
}


// Given a string representing a command line, convert it to an "argv" format
// of an array of strings, one for each switch or parameter, i.e. exactly like
// the format of the command line as given when the program starts.

int NParseCommandLine(char *szLine, char **argv)
{
  int argc = 1, fSpace = fTrue;
  char *pch = szLine, chQuote = chNull;

  // Split the entered line up into its individual switch strings.
  while ((uchar)*pch >= ' ' || *pch == chTab) {
    if (*pch == ' ' || *pch == chTab) {
      if (fSpace)
        // Skip over the current run of spaces between strings.
        ;
      else {
        // First space after a string, end parameter here.
        if (chQuote == chNull) {
          *pch = chNull;
          fSpace = fTrue;
        }
      }
    } else {
      if (fSpace) {
        // First character after run of spaces, begin parameter here.
        if (argc >= MAXSWITCHES-1) {
          PrintWarning("Too many parameters! Rest of line ignored.");
          break;
        }
        chQuote = (*pch == '"' || *pch == '\'') ? *pch : chNull;
        argv[argc++] = pch + (chQuote != chNull);
        fSpace = fFalse;
      } else {
        // Skip over the current string.
        if (chQuote != chNull && *pch == chQuote && pch[1] < '@') {
          *pch = chNull;
          fSpace = fTrue;
        }
      }
    }
    pch++;
  }
  argv[0] = (char *)szAppNameCore;
  argv[argc] = NULL;               // Set last string in switch array to Null.
  return argc;
}


#ifndef WIN
// This routine is called by the main program to interactively prompt the user
// for command switches and parameters, entered in the same format as they
// would be on a command line. This needs to be called with certain systems
// which don't allow passing of a command line to the program, or when -Q is
// in effect. The result of this routine is returned to the main program which
// then processes it as done with a real command line.

int NPromptSwitches(char *line, int cchLine, char *argv[MAXSWITCHES])
{
  FILE *fileSav;
  char sz[cchSzDef];

  fileSav = is.S; is.S = stdout;
  is.cchRow = 0;
  AnsiColor(kWhiteA);
  sprintf2(S(sz), "** %s version %s ", szAppName, szVersionCore); PrintSz(sz);
  sprintf2(S(sz), "(See '%cHc' switch for copyrights and credits.) **\n",
    chSwitch); PrintSz(sz);
  AnsiColor(kDefault);
  PrintSz("Enter all parameter options below. ");
  sprintf2(S(sz), "(Enter '%cH' for help. Enter '.' to exit.)\n", chSwitch);
  PrintSz(sz);
  is.S = fileSav;
  InputString("Input command line", line, cchLine);
  PrintL();
  return NParseCommandLine(line, argv);
}
#endif


/*
******************************************************************************
** Main Program.
******************************************************************************
*/

// Store or recall the current state of restrictions, as done with the -YRo
// and -YRi switches.

void InitRestrictions(flag fStore)
{
  if (fStore) {
    CopyRgb(ignore.rgn,  ignoreMem,  sizeof(ignore.rgn));
    CopyRgb(ignore2.rgn, ignore2Mem, sizeof(ignore2.rgn));
    CopyRgb(ignorea.rgn, ignoreaMem.rgn, sizeof(ignorea.rgn));
    CopyRgb(ignorez, ignorezMem, sizeof(ignorez));
    CopyRgb(ignore7, ignore7Mem, sizeof(ignore7));
    ignorefMem[0] = us.fIgnoreSign;   ignorefMem[1] = us.fIgnoreDir;
    ignorefMem[2] = us.fIgnoreDiralt; ignorefMem[3] = us.fIgnoreDirlen;
    ignorefMem[4] = us.fIgnoreAlt0;   ignorefMem[5] = us.fIgnoreDisequ;
  } else {
    CopyRgb(ignoreMem,  ignore.rgn,  sizeof(ignore.rgn));
    CopyRgb(ignore2Mem, ignore2.rgn, sizeof(ignore2.rgn));
    CopyRgb(ignoreaMem.rgn, ignorea.rgn, sizeof(ignorea.rgn));
    CopyRgb(ignorezMem, ignorez, sizeof(ignorez));
    CopyRgb(ignore7Mem, ignore7, sizeof(ignore7));
    us.fIgnoreSign   = ignorefMem[0]; us.fIgnoreDir    = ignorefMem[1];
    us.fIgnoreDiralt = ignorefMem[2]; us.fIgnoreDirlen = ignorefMem[3];
    us.fIgnoreAlt0   = ignorefMem[4]; us.fIgnoreDisequ = ignorefMem[5];
  }
}


// Initialize program variables and tables that aren't done so at compile
// time. Called once when the program starts from main() or WinMain().

void InitProgram()
{
#ifdef WIN
  char sz[cchSzMax], *pch;
#endif
  int i;

  Assert(starHi == cObj && cObj == objMax-1);
  SetCI(ciDefa, MM, DD, YY, TT, 0, DEFAULT_ZONE, DEFAULT_LONG, DEFAULT_LAT);
  is.S = stdout;
  ClearB((pbyte)szStarCustom, sizeof(szStarCustom));
  InitRestrictions(fTrue);
  for (i = 0; i < objMax; i++) {
    szObjDisp[i] = szObjName[i];
    rgobjList[i] = i;
  }
  for (i = 1; i <= cAspect2; i++) {
    szAspectDisp[i]       = szAspectName[i];
    szAspectAbbrevDisp[i] = szAspectAbbrev[i];
    szAspectGlyphDisp[i]  = szAspectGlyph[i];
  }
#ifdef INTERPRET
  for (i = 0; i < objMax; i++)
    szMindPart[i] = szMindPartDef[i];
  for (i = 0; i <= cSign; i++) {
    szDesc[i]     = szDescDef[i];
    szDesire[i]   = szDesireDef[i];
    szLifeArea[i] = szLifeAreaDef[i];
  }
  for (i = 0; i <= cAspect; i++) {
    szInteract[i]  = szInteractDef[i];
    szTherefore[i] = szThereforeDef[i];
  }
#endif
#ifdef SWISS
  for (i = 0; i < cCust; i++) {
    rgObjSwiss[i] = rgObjSwissDef[i];
    rgTypSwiss[i] = rgTypSwissDef[i];
  }
  ClearB((pbyte)rgPntSwiss, sizeof(rgPntSwiss));
  ClearB((pbyte)rgFlgSwiss, sizeof(rgFlgSwiss));
#endif
#ifdef GRAPH
  InitColorPalette(-1);
  for (i = 0; i < objMaxG; i++) {
    szDrawObject[i]  = szDrawObjectDef[i];
    szDrawObject2[i] = szDrawObjectDef2[i];
  }
  for (i = 1; i <= cAspect3; i++) {
    szDrawAspect[i]  = szDrawAspectDef[i];
    szDrawAspect2[i] = szDrawAspectDef2[i];
  }
#endif
#ifdef WIN
  GetModuleFileName(wi.hinst, sz, cchSzMax);
  for (pch = sz; *pch; pch++)
    ;
  if (pch - sz > 4 && FEqSz(pch - 4, ".scr"))
    wi.fSaverExt = fTrue;
  // Ensure _graphicschart enum aligns with rgcmdMode array.
  Assert(rgcmdMode[gWheel]      == cmdChartList);
  Assert(rgcmdMode[gHouse]      == cmdChartWheel);
  Assert(rgcmdMode[gGrid]       == cmdChartGrid);
  Assert(rgcmdMode[gMidpoint]   == cmdChartMidpoint);
  Assert(rgcmdMode[gHorizon]    == cmdChartHorizon);
  Assert(rgcmdMode[gOrbit]      == cmdChartOrbit);
  Assert(rgcmdMode[gSector]     == cmdChartSector);
  Assert(rgcmdMode[gCalendar]   == cmdChartCalendar);
  Assert(rgcmdMode[gDisposit]   == cmdChartInfluence);
  Assert(rgcmdMode[gEsoteric]   == cmdChartEsoteric);
  Assert(rgcmdMode[gAstroGraph] == cmdChartAstroGraph);
  Assert(rgcmdMode[gEphemeris]  == cmdChartEphemeris);
  Assert(rgcmdMode[gRising]     == cmdChartRising);
  Assert(rgcmdMode[gLocal]      == cmdChartLocal);
  Assert(rgcmdMode[gTraTraGra]  == cmdTransit);
  Assert(rgcmdMode[gTraNatGra]  == cmdTransit);
  Assert(rgcmdMode[gMoons]      == cmdChartMoons);
  Assert(rgcmdMode[gExo]        == cmdChartExo);
  Assert(rgcmdMode[gSphere]     == cmdChartSphere);
  Assert(rgcmdMode[gWorldMap]   == cmdChartMap);
  Assert(rgcmdMode[gGlobe]      == cmdChartGlobe);
  Assert(rgcmdMode[gPolar]      == cmdChartPolar);
  Assert(rgcmdMode[gTelescope]  == cmdChartTelescope);
  Assert(rgcmdMode[gBiorhythm]  == 0/*cmdRelBiorhythm*/);
  Assert(rgcmdMode[gAspect]     == cmdChartAspect);
  Assert(rgcmdMode[gArabic]     == cmdChartArabic);
  Assert(rgcmdMode[gTraTraTim]  == cmdTransit);
  Assert(rgcmdMode[gTraTraInf]  == cmdTransit);
  Assert(rgcmdMode[gTraNatTim]  == cmdTransit);
  Assert(rgcmdMode[gTraNatInf]  == cmdTransit);
  Assert(rgcmdMode[gSign]       == cmdHelpSign);
  Assert(rgcmdMode[gObject]     == cmdHelpObject);
  Assert(rgcmdMode[gHelpAsp]    == cmdHelpAspect);
  Assert(rgcmdMode[gConstel]    == cmdHelpConstellation);
  Assert(rgcmdMode[gPlanet]     == cmdHelpPlanetInfo);
  Assert(rgcmdMode[gRay]        == cmdHelpRay);
  Assert(rgcmdMode[gMeaning]    == cmdHelpMeaning);
  Assert(rgcmdMode[gSwitch]     == cmdHelpSwitch);
  Assert(rgcmdMode[gObscure]    == cmdHelpObscure);
  Assert(rgcmdMode[gKeystroke]  == cmdHelpKeystroke);
  Assert(rgcmdMode[gCredit]     == cmdHelpCredit);
#endif
#ifdef DEBUG
  // Ensure planets and planet COB's have same data.
  for (i = 0; i < cCOB; i++) {
    Assert(rObjDist[i + oJup] == rObjDist[i + oJuC]);
    Assert(rObjYear[i + oJup] == rObjYear[i + oJuC]);
    Assert(rObjDiam[i + oJup] == rObjDiam[i + oJuC]);
    Assert(rObjDay[i + oJup] == rObjDay[i + oJuC]);
  }
#endif
}


// Program is about to exit, so free all memory that was allocated.

void FinalizeProgram(flag fSkip)
{
  char sz[cchSzDef];
  int i;

  DeallocatePIf(grid);
  for (i = 0; i < 10; i++)
    DeallocatePIf(us.rgszPath[i]);
  DeallocatePIf(us.szADB);
  DeallocatePIf(us.szStarsColor);
  DeallocatePIf(us.szAstColor);
  DeallocatePIf(us.szStarsList);
  DeallocatePIf(us.szExoList);
  DeallocatePIf(is.rgci);
  if (is.rgexod != NULL) {
    for (i = 0; i < is.cexod; i++)
      DeallocatePIf(is.rgexod[i].sz);
    DeallocateP(is.rgexod);
  }
  DeallocatePIf(is.szFileOut);
  DeallocatePIf(is.szFileScreen);
  for (i = 0; i < us.cSequenceLine; i++)
    DeallocatePIf(is.rgszLine[i]);
  if (is.rgszMacro != NULL) {
    for (i = 0; i < is.cszMacro; i++)
      DeallocatePIf(is.rgszMacro[i]);
    DeallocateP(is.rgszMacro);
  }
  if (is.rgszComment != NULL) {
    for (i = 0; i < is.cszComment; i++)
      DeallocatePIf(is.rgszComment[i]);
    DeallocateP(is.rgszComment);
  }
  for (i = 0; i < objMax; i++)
    if (FObjDispCustom(i))
      DeallocateP((char *)szObjDisp[i]);
  for (i = 1; i <= cAspect2; i++) {
    if (szAspectDisp[i] != szAspectName[i])
      DeallocateP((char *)szAspectDisp[i]);
    if (szAspectAbbrevDisp[i] != szAspectAbbrev[i])
      DeallocateP((char *)szAspectAbbrevDisp[i]);
    if (szAspectGlyphDisp[i] != szAspectGlyph[i])
      DeallocateP((char *)szAspectGlyphDisp[i]);
  }
  for (i = 1; i <= cStar; i++)
    DeallocatePIf(szStarCustom[i]);
  for (i = 0; i <= cRing; i++)
    DeallocatePIf(szWheel[i]);
#ifdef ATLAS
  DeallocatePIf(is.rgae);
  DeallocatePIf(is.rgzc);
  DeallocatePIf(is.rgrun);
  DeallocatePIf(is.rgrue);
  DeallocatePIf(is.rgzonCol);
#endif
#ifdef INTERPRET
  for (i = 0; i < objMax; i++)
    if (szMindPart[i] != szMindPartDef[i])
      DeallocateP((char *)szMindPart[i]);
  for (i = 0; i <= cSign; i++) {
    if (szDesc[i] != szDescDef[i])
      DeallocateP((char *)szDesc[i]);
    if (szDesire[i] != szDesireDef[i])
      DeallocateP((char *)szDesire[i]);
    if (szLifeArea[i] != szLifeAreaDef[i])
      DeallocateP((char *)szLifeArea[i]);
  }
  for (i = 0; i <= cAspect; i++) {
    if (szInteract[i] != szInteractDef[i])
      DeallocateP((char *)szInteract[i]);
    if (szTherefore[i] != szThereforeDef[i])
      DeallocateP((char *)szTherefore[i]);
  }
#endif
#ifdef EXPRESS
  ExpFinalize();
  DeallocatePIf(us.szExpConfig);
  DeallocatePIf(us.szExpAspList);
  DeallocatePIf(us.szExpAspSumm);
  DeallocatePIf(us.szExpMid);
  DeallocatePIf(us.szExpMidAsp);
  DeallocatePIf(us.szExpInf);
  DeallocatePIf(us.szExpInf0);
  DeallocatePIf(us.szExpEso);
  DeallocatePIf(us.szExpCross);
  DeallocatePIf(us.szExpEph);
  DeallocatePIf(us.szExpRis);
  DeallocatePIf(us.szExpDay);
  DeallocatePIf(us.szExpVoid);
  DeallocatePIf(us.szExpTra);
  DeallocatePIf(us.szExpPart);
  DeallocatePIf(us.szExpObj);
  DeallocatePIf(us.szExpHou);
  DeallocatePIf(us.szExpAsp);
  DeallocatePIf(us.szExpProg);
  DeallocatePIf(us.szExpProg0);
  DeallocatePIf(us.szExpColObj);
  DeallocatePIf(us.szExpColAsp);
  DeallocatePIf(us.szExpColFill);
  DeallocatePIf(us.szExpFontSig);
  DeallocatePIf(us.szExpFontHou);
  DeallocatePIf(us.szExpFontObj);
  DeallocatePIf(us.szExpFontAsp);
  DeallocatePIf(us.szExpFontNak);
  DeallocatePIf(us.szExpFontTxt);
  DeallocatePIf(us.szExpSort);
  DeallocatePIf(us.szExpDecan);
  DeallocatePIf(us.szExpDecan2);
  DeallocatePIf(us.szExpDegree);
  DeallocatePIf(us.szExpStar);
  DeallocatePIf(us.szExpAst);
  DeallocatePIf(us.szExpExo);
  DeallocatePIf(us.szExpIntV);
  DeallocatePIf(us.szExpIntV2);
  DeallocatePIf(us.szExpIntA);
  DeallocatePIf(us.szExpIntA2);
  DeallocatePIf(us.szExpCorner);
  DeallocatePIf(us.szExpCity);
  DeallocatePIf(us.szExpSidebar);
  DeallocatePIf(us.szExpKey);
  DeallocatePIf(us.szExpMenu);
  DeallocatePIf(us.szExpCast1);
  DeallocatePIf(us.szExpCast2);
  DeallocatePIf(us.szExpDisp1);
  DeallocatePIf(us.szExpDisp2);
  DeallocatePIf(us.szExpDisp3);
  DeallocatePIf(us.szExpListS);
  DeallocatePIf(us.szExpListF);
  DeallocatePIf(us.szExpListY);
  DeallocatePIf(us.szExpADB);
#endif
#ifdef GRAPH
  DeallocatePIf(gi.bm);
  DeallocatePIf(gi.bmp.rgb);
  DeallocatePIf(gi.bmpBack.rgb);
  DeallocatePIf(gi.bmpBack2.rgb);
  DeallocatePIf(gi.bmpWorld.rgb);
  DeallocatePIf(gi.bmpRising.rgb);
  DeallocatePIf(gi.rgspace);
  DeallocatePIf(gi.rgConstel);
  DeallocatePIf(gi.szFileOut);
  DeallocatePIf(gs.szSidebar);
  for (i = 0; i <= cRing; i++)
    DeallocatePIf(szWheelX[i]);
  for (i = 0; i < objMax; i++) {
    if (szDrawObject[i] != szDrawObjectDef[i])
      DeallocateP((char *)szDrawObject[i]);
    if (szDrawObject2[i] != szDrawObjectDef2[i])
      DeallocateP((char *)szDrawObject2[i]);
  }
  for (i = 1; i <= cAspect2; i++) {
    if (szDrawAspect[i] != szDrawAspectDef[i])
      DeallocateP((char *)szDrawAspect[i]);
    if (szDrawAspect2[i] != szDrawAspectDef2[i])
      DeallocateP((char *)szDrawAspect2[i]);
  }
#ifdef SWISS
  DeallocatePIf(gi.rges);
  DeallocatePIf(gs.szStarsLin);
  DeallocatePIf(gs.szStarsLnk);
  DeallocatePIf(is.rgesSort);
#endif
#endif // GRAPH
#ifdef X11
  DeallocatePIf(gs.szDisplay);
#endif
#ifdef QT
  FinalizeQt();
#endif
#ifdef WINANY
  DeallocatePIf(wi.bmpWin.rgb);
#endif
#ifdef WIN
  DeallocatePIf(wi.bmpSmooth.rgb);
  if (ofn.lpstrFile != szFileName)
    DeallocatePIf(ofn.lpstrFile);
#endif
  if (fSkip)
    return;
  if (is.cAlloc != 0) {
    sprintf2(S(sz), "Number of memory allocations not freed before exiting: %d",
      is.cAlloc);
    PrintWarning(sz);
  }
#ifdef DEBUG
  else if (is.cbAllocSize != 0) {
    sprintf2(S(sz), "Number of memory bytes not freed before exiting: %d",
      is.cbAllocSize);
    PrintWarning(sz);
  }
#endif
}


#ifndef WIN
// The main program, the starting point for Astrolog, follows. This routine
// basically consists of a loop, inside which we read a command line, and go
// process it, before actually calling a routine to display astrology.

#ifdef SWITCHES
int main(int argc, char **argv)
{
#else
int main()
{
  int argc;
  char **argv;
#endif
  char szCommandLine[cchSzMax], *rgsz[MAXSWITCHES];
#ifdef BETA
  char szBeta[cchSzMax];
#endif

  // Read in info from the astrolog.as file.
  InitProgram();
#ifdef SWITCHES
  is.szProgName = argv[0];
#endif
  FProcessSwitchFile(DEFAULT_INFOFILE, NULL);
#ifdef QT
  // The Qt build is a GUI application first, like Windows: bring up the
  // chart window by default instead of requiring the astrolog.as default
  // settings file to have graphics mode active (it doesn't, by default),
  // showing today's chart rather than prompting at a text mode stdin
  // prompt if no chart file or switches were given on the command line.
  // This runs after astrolog.as is processed, so an explicit command line
  // switch (processed below) can still override it either way.
  us.fGraphics = fTrue;
  FInputData(szNowCore);
#endif
  ciTran = ciHexa = ciFive = ciFour = ciThre = ciTwin = ciMain = ciCore;
#ifdef BETA
  sprintf2(S(szBeta), "This is a beta version of %s %s! "
    "That means changes are still being made and testing is not complete. "
    "If this is being run after %s %d, %d, "
    "it should be replaced with the finished release.\n\n",
    szAppName, szVersionCore, szMonth[ciSave.mon], ciSave.day, ciSave.yea);
  FieldWord(szBeta);
#endif

LBegin:
  if (is.fNoSwitches) {                             // Go prompt for switches
    argc = NPromptSwitches(S(szCommandLine), rgsz); // if don't have them.
    argv = rgsz;
  }
  is.szProgName = argv[0];
  if (FProcessSwitches(argc, argv, NULL)) {
    if (!is.fNoSwitches && us.fLoopInit) {
      is.fNoSwitches = fTrue;
      goto LBegin;
    }
    Action();
  }
  if (us.fLoop || us.fNoQuit) {  // If -Q in effect loop back and get switch
    PrintL2();                   // info for another chart to display.
    InitVariables();
    us.fLoop = is.fNoSwitches = fTrue;
    goto LBegin;
  }
  Terminate(tcOK);    // The only standard place to exit Astrolog is here.
  return 0;
}
#endif // WIN

/* astrolog.cpp */
