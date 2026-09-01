/*
** Astrolog (Version 8.00) File: xscreen.cpp
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


#ifdef GRAPH
/*
******************************************************************************
** Astrolog Icon.
******************************************************************************
*/

#ifdef X11
// This information used to define Astrolog's X Windows icon (ringed planet
// with moons) is similar to the output format used by the bitmap program.
// You could extract this section and run "xsetroot -bitmap" on it.

#define icon_width 48
#define icon_height 48
CONST uchar icon_bits[] = {
  0x0e,0x00,0x00,0x00,0x10,0x00,0x7f,0x00,0x00,0x00,0x00,0x00,0xff,0x01,0x00,
  0x02,0x00,0x00,0xff,0x43,0x00,0x00,0x00,0x00,0xfe,0x0f,0x00,0x00,0x02,0x00,
  0x9e,0x1f,0x00,0x00,0x00,0x10,0x1e,0x7c,0xc0,0x03,0x00,0x38,0x3c,0xf8,0x7e,
  0x7d,0x00,0x10,0x3c,0xe0,0xab,0xaa,0x00,0x00,0x38,0xc0,0x5f,0x05,0x03,0x00,
  0x70,0x60,0xb5,0x12,0x04,0x00,0xf0,0xf8,0x5f,0x85,0x18,0x0e,0xe0,0xf8,0xaa,
  0x2a,0x90,0x33,0xc0,0xfd,0x5f,0x05,0xe0,0x47,0xc0,0xbf,0xb5,0x52,0xc0,0x4b,
  0x80,0xff,0x5f,0x05,0xc0,0x85,0x00,0xff,0xaa,0x2a,0x80,0x83,0x80,0xff,0x5f,
  0x05,0x00,0x87,0x80,0x7f,0xb5,0x92,0x00,0x4b,0x80,0xff,0x5f,0x05,0x00,0x45,
  0x80,0xff,0xaa,0x2a,0x00,0x33,0x80,0xff,0x5f,0x05,0x00,0x0f,0xc0,0xff,0xb5,
  0x52,0x00,0x02,0xc0,0xff,0x5f,0x05,0x00,0x02,0xc1,0xff,0xab,0x2a,0x00,0x02,
  0xe0,0xff,0x5f,0x05,0x00,0x02,0x38,0x5f,0xbf,0x92,0x00,0x01,0x58,0xfe,0x5f,
  0x05,0x00,0x81,0xbc,0xfc,0xbe,0x2a,0x00,0x01,0x7c,0xfc,0x7f,0x05,0x00,0x01,
  0x3c,0xbc,0xf5,0x52,0x00,0x01,0x58,0xfe,0xff,0x05,0x80,0x00,0xb8,0xff,0xea,
  0x2b,0xc0,0x01,0xe0,0xfe,0xdf,0x0f,0xc0,0x23,0x00,0x5c,0xb5,0x9f,0xa0,0x03,
  0x00,0xf8,0x5f,0x3d,0x10,0x07,0x00,0xf8,0xaa,0xfa,0x18,0x0f,0x08,0xe0,0x5f,
  0xf5,0x05,0x0e,0x1c,0xc0,0xb5,0xea,0x03,0x1c,0x08,0x00,0x5f,0x85,0x07,0x3c,
  0x00,0x00,0xbe,0x7e,0x1f,0x3c,0x00,0x10,0xc0,0x03,0x3e,0x78,0x00,0x00,0x00,
  0x00,0xf8,0x79,0x00,0x00,0x00,0x00,0xf0,0x7f,0x00,0x00,0x20,0x00,0xc0,0xff,
  0x00,0x00,0x00,0x00,0x82,0xff,0x00,0x00,0x00,0x00,0x00,0xfe,0x40,0x00,0x00,
  0x00,0x00,0x70};
#endif


/*
******************************************************************************
** Interactive Screen Graphics Routines.
******************************************************************************
*/

// Set up the color palette of RGB values used by the program for its main
// color indexes. There's a default palette of bright colors used with black
// backgrounds, and an optional set of adjusted colors that looks better with
// white backgrounds. Also, if the user customizes a palette slot with the
// -YXK switch, then leave it alone and don't change it.

void InitColorPalette(int n)
{
  CONST KV *rgbbmpNew, *rgbbmpOld;
  int i;

  rgbbmpNew = (n < 1 || !gs.fAltPalette ? rgbbmpDef : rgbbmpDef2);
  rgbbmpOld = (n < 1 || !gs.fAltPalette ? rgbbmpDef2 : rgbbmpDef);
  for (i = 0; i < cColor2; i++)
    if (n < 0 || rgbbmp[i] == rgbbmpOld[i])
      rgbbmp[i] = rgbbmpNew[i];
}


// Set up all the colors used by the program, i.e. the foreground and
// background colors, and all the colors in the object arrays, based on
// whether or not are in monochrome and/or reverse video mode.

void InitColorsX()
{
  int i;
  flag fInverse = gs.fInverse;
  KI ki;
#ifdef X11
  char sz[cchSzDef];
  Colormap cmap;
  XColor xcol;
  KV kv;

  if (!gi.fFile) {
    cmap = XDefaultColormap(gi.disp, gi.screen);

    // Allocate colors from the present X11 colormap. Given RGB color strings,
    // allocate these colors and determine their values.

    for (i = 0; i < cColor2; i++) {
      kv = rgbbmp[i];
      sprintf2(S(sz), "#%02x%02x%02x", RgbR(kv), RgbG(kv), RgbB(kv));
      XParseColor(gi.disp, cmap, sz, &xcol);
      XAllocColor(gi.disp, cmap, &xcol);
      rgbind[i] = xcol.pixel;
    }
  }
  if (!gi.fFile) {
    XSetBackground(gi.disp, gi.gc,   rgbind[gi.kiOff]);
    XSetForeground(gi.disp, gi.pmgc, rgbind[gi.kiOff]);
  }
#endif

#ifdef WIN
  // Don't print on a black background unless user really wants that.
  if (wi.hdcPrint != NULL && us.fSmartSave)
    fInverse = fTrue;
#endif

  gi.kiOn   = kMainA[!fInverse];
  gi.kiOff  = kMainA[fInverse];
  gi.kiLite = gs.fColor ? kMainA[2+fInverse] : gi.kiOn;
  gi.kiGray = gs.fColor ? kMainA[3-fInverse] : gi.kiOn;
  for (i = 0; i <= 8; i++)
    kMainB[i]    = gs.fColor ? kMainA[i]    : gi.kiOn;
  for (i = 0; i <= cRainbow; i++)
    kRainbowB[i] = gs.fColor ? kRainbowA[i] : gi.kiOn;
  for (i = 0; i < cElem; i++)
    kElemB[i]    = gs.fColor ? kElemA[i]    : gi.kiOn;
  for (i = 0; i <= cAspect; i++)
    kAspB[ASPT(i)] = gs.fColor ? kAspA[ASPT(i)] : gi.kiOn;
  for (i = 0; i <= cObj; i++)
    kObjB[i]     = gs.fColor ? kObjA[i]     : gi.kiOn;
  for (i = 0; i <= cRay+1; i++)
    kRayB[RAYT(i)] = gs.fColor ? kRayA[RAYT(i)] : gi.kiOn;

  // Compute RGB colors to use for each of the Seven Rays.
  for (i = 1; i <= cRay+1; i++) {
    ki = kRayB[RAYT(i)];
    if (i == 2 && ki == kIndigo && rgbbmp[ki] == rgbbmpDef[ki])
      rgbbmpRay[RAYT(i)] = rgbbmpDef2[ki];
    else if (i == 4 && ki == kYellow && rgbbmp[ki] == rgbbmpDef2[ki])
      rgbbmpRay[RAYT(i)] = rgbbmpDef[ki];
    else
      rgbbmpRay[RAYT(i)] = rgbbmp[ki];
  }
}


#ifdef WCLI
// Window event processor for the Windows CLI version. Most event processing
// happens inside the InteractX() message loop.

LRESULT API WndProcWCLI(HWND hwnd, UINT wMsg, WPARAM wParam, LPARAM lParam)
{
  HDC hdc;
  HPEN hpen, hpenOld;
  int x, y, n;

  wi.hwnd = hwnd;
  switch (wMsg) {

  // The window has been resized. Change the chart size if need be.
  case WM_SIZE:
    wi.xClient = gs.xWin = LOWORD(lParam);
    wi.yClient = gs.yWin = HIWORD(lParam);
    if (!wi.fNotManual) {
      gi.xWinResize = gs.xWin; gi.yWinResize = gs.yWin;
    }
    wi.fDoResize = fTrue;
    break;

  // All or part of the window needs to be redrawn. Will do so later.
  case WM_PAINT:
    wi.fDoRedraw = fTrue;
    break;

  // The mouse has been left clicked or dragged over the window.
  case WM_LBUTTONDOWN:
  case WM_MOUSEMOVE:
    x = WLo(lParam);
    y = WHi(lParam);
    if (wMsg == WM_MOUSEMOVE) {

      // Dragging with right mouse down rotates and tilts globes.
      if ((wParam & MK_RBUTTON) != 0 && us.fGraphics && (fMap ||
        gi.nMode == gMidpoint || gi.nMode == gLocal || gi.nMode == gSphere ||
        gi.nMode == gGlobe || gi.nMode == gPolar || gi.nMode == gTelescope)) {
        gs.rRot += (real)(x-WLo(wi.lParamRC)) * rDegHalf / (real)gs.xWin *
          (gi.nMode == gLocal || gi.nMode == gTelescope ? -gi.zViewRatio :
          1.0);
        gs.rTilt += (real)(y-WHi(wi.lParamRC)) * rDegHalf / (real)gs.yWin *
          (gi.nMode == gLocal || gi.nMode == gTelescope ? gi.zViewRatio :
          (gi.nMode == gGlobe ? -1.0 : 1.0));
        while (gs.rRot >= rDegMax)
          gs.rRot -= rDegMax;
        while (gs.rRot < 0.0)
          gs.rRot += rDegMax;
        while (gs.rTilt > rDegQuad)
          gs.rTilt = rDegQuad;
        while (gs.rTilt < -rDegQuad)
          gs.rTilt = -rDegQuad;
        if (gi.nMode == gMidpoint || gi.nMode == gTelescope) {
          if (gi.nMode == gMidpoint && gs.objTrack >= 0)
            gs.rRot = planet[gs.objTrack];
          gs.objTrack = -1;
        }
        wi.lParamRC = lParam;
        wi.fDoRedraw = fTrue;
        break;
      }

      // Treat dragging with left mouse down as a Shift+left click.
      if ((wParam & MK_LBUTTON) == 0 ||
        (wParam & MK_SHIFT) || (wParam & MK_CONTROL))
        break;
      wParam = MK_SHIFT;
    }

    // Alt+click on a world map chart means relocate the chart there.
    if (wMsg == WM_LBUTTONDOWN && GetKeyState(VK_MENU) < 0) {
      if (fMap && !gs.fConstel && !gs.fMollweide) {
        Lon = rDegHalf -
          Mod((real)(x-gi.xOffset) / (real)gs.xWin*rDegMax - gs.rRot);
        if (Lon < -rDegHalf)
          Lon = -rDegHalf;
        else if (Lon > rDegHalf)
          Lon = rDegHalf;
        Lat = rDegQuad-(real)(y-gi.yOffset)/(real)gs.yWin*rDegHalf;
        if (Lat < -rDegQuad)
          Lat = -rDegQuad;
        else if (Lat > rDegQuad)
          Lat = rDegQuad;
        wi.xMouse = -1;
        ciCore = ciMain;
        wi.fDoCast = fTrue;
      }
      break;
    }
    hdc = GetDC(hwnd);
    n = (!gs.fThick ? 0 : 2) + gs.nThickAdjust;
    hpen = (HPEN)CreatePen(PS_SOLID, Max(n, 0), (COLORREF)KvFromKi(gi.kiPen));
    hpenOld = (HPEN)SelectObject(hdc, hpen);

    // Ctrl+click means draw a rectangle. Ctrl+Shift+click does ellipse.
    if (wParam & MK_CONTROL) {
      SelectObject(hdc, GetStockObject(NULL_BRUSH));
      if (wParam & MK_SHIFT)
        Ellipse(hdc, wi.xMouse, wi.yMouse, x, y);
      else
        Rectangle(hdc, wi.xMouse, wi.yMouse, x, y);

    // Shift+click means draw a line from the last to current position.
    } else if (wParam & MK_SHIFT) {
      if (wi.xMouse >= 0) {
        MoveTo(hdc, wi.xMouse, wi.yMouse);
        LineTo(hdc, x, y);
        if (wMsg == WM_MOUSEMOVE) {
          wi.xMouse = x; wi.yMouse = y;
        }
      }

    // A simple click means set a pixel and remember that location.
    } else {
      SetPixel(hdc, x, y, (COLORREF)KvFromKi(gi.kiPen));
      wi.xMouse = x; wi.yMouse = y;
    }
    SelectObject(hdc, hpenOld);
    DeleteObject(hpen);
    ReleaseDC(hwnd, hdc);
    break;

  // The mouse has been right clicked on the window.
  case WM_RBUTTONDOWN:
    if (us.fGraphics) {
      if (fMap || gi.nMode == gLocal || gi.nMode == gSphere ||
        gi.nMode == gGlobe || gi.nMode == gPolar || gi.nMode == gTelescope)
        wi.lParamRC = lParam;
    }
    break;

  default:
    return DefWindowProc(hwnd, wMsg, wParam, lParam);
  }
  return fFalse;
}
#endif


#ifdef ISG
// This routine opens up and initializes a window and prepares it to be drawn
// upon, and gets various information about the display, too.

void BeginX()
{
#ifdef X11
  gi.fBmp = fFalse;        // Astrolog can't draw 24 bit color bitmaps on X11.
  gi.disp = XOpenDisplay(gs.szDisplay);
  if (gi.disp == NULL) {
    PrintError("Can't open display.");
    Terminate(tcFatal);
  }
  gi.screen = DefaultScreen(gi.disp);
  bg = BlackPixel(gi.disp, gi.screen);
  fg = WhitePixel(gi.disp, gi.screen);
  hint.x = gi.xOffset; hint.y = gi.yOffset;
  hint.width = gs.xWin; hint.height = gs.yWin;
  hint.min_width = BITMAPX1; hint.min_height = BITMAPY1;
  hint.max_width = BITMAPX;  hint.max_height = BITMAPY;
  hint.flags = PPosition | PSize | PMaxSize | PMinSize;
  gi.depth = DefaultDepth(gi.disp, gi.screen);
  if (gi.depth < 5) {
    gi.fMono = fTrue;      // Is this a monochrome display?
    gs.fColor = fFalse;
  }
  gi.root = RootWindow(gi.disp, gi.screen);
  if (gs.fRoot)
    gi.wind = gi.root;     // If -XB in effect, then draw on the root window.
  else
    gi.wind = XCreateSimpleWindow(gi.disp, DefaultRootWindow(gi.disp),
      hint.x, hint.y, hint.width, hint.height, 5, fg, bg);
  gi.pmap = XCreatePixmap(gi.disp, gi.wind, gs.xWin, gs.yWin, gi.depth);
  gi.icon = XCreateBitmapFromData(gi.disp, DefaultRootWindow(gi.disp),
    (char *)icon_bits, icon_width, icon_height);
  if (!gs.fRoot)
    XSetStandardProperties(gi.disp, gi.wind, szAppName, szAppName, gi.icon,
      (char **)xkey, 0, &hint);

  // There are two graphics workareas. One is what the user currently sees in
  // the window, and the other is what is currently being drawn on. When done,
  // can quickly copy it to the viewport for a smooth look.

  gi.gc = XCreateGC(gi.disp, gi.wind, 0, 0);
  XSetGraphicsExposures(gi.disp, gi.gc, 0);
  gi.pmgc = XCreateGC(gi.disp, gi.wind, 0, 0);
  InitColorsX();                                  // Go set up colors.
  if (!gs.fRoot)
    XSelectInput(gi.disp, gi.wind, KeyPressMask | StructureNotifyMask |
      ExposureMask | ButtonPressMask | ButtonReleaseMask | ButtonMotionMask);
  XMapRaised(gi.disp, gi.wind);
  XSync(gi.disp, 0);
  XFillRectangle(gi.disp, gi.pmap, gi.pmgc, 0, 0, gs.xWin, gs.yWin);
#endif // X11

#ifdef QT
  BeginQt();       // Create the QApplication, main window, and chart buffer.
  InitColorsX();
#endif

#ifdef WIN
  if (wi.fChartWindow && (wi.xClient != gs.xWin ||
    wi.yClient != gs.yWin) && wi.hdcPrint == hdcNil)
    ResizeWindowToChart();
  if (!wi.fSmoothZoom || gi.fFile) {
    gi.xOffset = NMultDiv(wi.xClient - gs.xWin, wi.xScroll, nScrollDiv);
    gi.yOffset = NMultDiv(wi.yClient - gs.yWin, wi.yScroll, nScrollDiv);
    SetWindowOrg(wi.hdc, -gi.xOffset, -gi.yOffset);
    SetWindowExt(wi.hdc, wi.xClient, wi.yClient);
  } else {
    gi.xOffset = NMultDiv(wi.xClient*wi.nScaleWin - gs.xWin, wi.xScroll,
      nScrollDiv);
    gi.yOffset = NMultDiv(wi.yClient*wi.nScaleWin - gs.yWin, wi.yScroll,
      nScrollDiv);
    SetWindowOrg(wi.hdc, -gi.xOffset, -gi.yOffset);
    SetWindowExt(wi.hdc, wi.xClient*wi.nScaleWin, wi.yClient*wi.nScaleWin);
  }
  SetMapMode(wi.hdc, MM_ANISOTROPIC);
  SelectObject(wi.hdc, GetStockObject(NULL_PEN));
  SelectObject(wi.hdc, GetStockObject(NULL_BRUSH));
  if (!gs.fJetTrail || wi.nScaleWin > 1)
    PatBlt(wi.hdc, -gi.xOffset, -gi.yOffset, wi.xClient, wi.yClient,
      gs.fInverse ? WHITENESS : BLACKNESS);
  InitColorsX();
#endif // WIN

#ifdef WCLI
  WNDCLASS wndclass;

  if (!wi.fWndclass) {
    wi.fWndclass = fTrue;
    wi.hinst = GetModuleHandle(NULL);
    ClearB((pbyte)&wndclass, sizeof(WNDCLASS));
    wndclass.style = CS_HREDRAW | CS_VREDRAW | CS_BYTEALIGNWINDOW;
    wndclass.lpfnWndProc = WndProcWCLI;
    wndclass.hInstance = wi.hinst;
    wndclass.hCursor = LoadCursor((HINSTANCE)NULL, IDC_ARROW);
    wndclass.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wndclass.lpszClassName = szAppName;
    if (!RegisterClass(&wndclass)) {
      PrintError("The window class could not be registered.");
      Terminate(tcFatal);
    }
  }
  wi.hwndMain = CreateWindow(
    szAppName,
    szAppNameCore " " szVersionCore,
    WS_CAPTION |
    WS_SYSMENU |
    WS_MINIMIZEBOX |
    WS_MAXIMIZEBOX |
    WS_THICKFRAME |
    WS_VSCROLL |
    WS_HSCROLL |
    WS_CLIPCHILDREN |
    WS_OVERLAPPED,
    CW_USEDEFAULT, CW_USEDEFAULT,
    CW_USEDEFAULT, CW_USEDEFAULT,
    (HWND)NULL,
    (HMENU)NULL,
    wi.hinst,
    (LPSTR)NULL);
  if (wi.hwndMain == (HWND)NULL) {
    PrintError("The window could not be created.");
    Terminate(tcFatal);
  }
  wi.hwnd = wi.hwndMain;
  ResizeWindowToChart();
  ShowWindow(wi.hwndMain, SW_SHOW);
  ShowScrollBar(wi.hwnd, SB_BOTH, fFalse);
  gi.xOffset = gi.yOffset = 0;
  InitColorsX();
#endif // WCLI
}


// Animate the current chart based on the given values indicating how much
// to update by. Update and recast the current chart info appropriately.
// Note animation mode for comparison charts will update the second chart.

void Animate(int mode, int toadd)
{
  if (((gi.nMode == gAstroGraph || gi.nMode == gSphere) && gs.fAnimMap) ||
    ((gi.nMode == gWorldMap || gi.nMode == gGlobe || gi.nMode == gPolar) &&
    (gs.fAlt || gs.fAnimMap))) {
    gs.rRot += (real)toadd;
    if (gs.rRot >= rDegMax)     // For animating map displays, add in
      gs.rRot -= rDegMax;       // appropriate degree value.
    else if (gs.rRot < 0.0)
      gs.rRot += rDegMax;
    return;
  }

  mode = NAbs(mode);
  if (mode == iAnimNow) {
#ifdef TIMEFUNC
    // For the continuous chart update to present moment animation mode, go
    // get whatever time it is now.
    FInputData(szNowCore);
#else
    mode = 1;
    goto LNotNow;
#endif
  } else {    // Otherwise add on appropriate time vector to chart info.
#ifndef TIMEFUNC
LNotNow:
#endif
    if (us.nRel == rcDual || us.nRel <= rcTransit)
      ciCore = ciTwin;
    else if (us.fProgress || us.fTransit || us.fTransitInf || us.fTransitGra)
      ciCore = ciTran;
    else
      ciCore = ciMain;
    AddTime(&ciCore, mode, toadd);
  }
  if (us.nRel == rcDual || us.nRel <= rcTransit) {
    ciTwin = ciCore;
    ciCore = ciMain;
  } else if (us.fProgress || us.fTransit || us.fTransitInf || us.fTransitGra) {
    ciTran = ciCore;
    ciCore = ciMain;
    if (us.fProgress)
      is.JDp = MdytszToJulian(MonT, DayT, YeaT, TimT, ciDefa.dst, ciDefa.zon);
  } else
    ciMain = ciCore;
  if (us.nRel)
    CastRelation();
  else
    CastChart(0);
}


#ifndef WIN
// This routine exits graphics mode, prompts the user for a set of command
// switches, processes them, and returns to the previous graphics with the
// new settings in effect, allowing one to change most any setting without
// having to lose their graphics state or fall back to a -Q loop.

void CommandLineX()
{
  char szCommandLine[cchSzMax], *rgsz[MAXSWITCHES];
  int argc, fT, fPause = fFalse;

  ciCore = ciMain;
  fT = us.fLoop; us.fLoop = fTrue;
  argc = NPromptSwitches(szCommandLine, rgsz);
  is.cchRow = 0;
  is.fSzInteract = fTrue;
  if (!FProcessSwitches(argc, rgsz, NULL))
    fPause = fTrue;
  else {
    is.fMult = fFalse;
    FPrintTables();
    if (is.fMult) {
      ClearB((pbyte)&us.fCredit,
        (int)((pbyte)&us.fLoop - (pbyte)&us.fCredit));
      fPause = fTrue;
    }
  }

  is.fSzInteract = fFalse;
  us.fLoop = fT;
  ciMain = ciCore;
  InitColorsX();
}
#endif // WIN


// Given two chart size values, adjust them such that the chart will look
// "square". This rounds the higher value down and checks certain conditions.

void SquareX(int *x, int *y, flag fForce)
{
  // Unless really want to force a square, realize that some charts look
  // better rectangular.
  if ((!fForce && !fSquare) || gi.nMode == gGrid || fMap)
    return;
  if (*x > *y)
    *x = *y;
  else
    *y = *x;
  if (fSidebar)      // Take into account chart's sidebar, if any.
    *x += xSideT;
}


#ifdef WINANY
// Change the pixel size of the window so its internal drawable area is the
// dimensions of the current graphics chart. Both the upper left and lower
// right corners of the window may change depending on the scroll position.

void ResizeWindowToChart()
{
  HDC hdc;
  RECT rcOld, rcCli, rcNew;
  int xScr, yScr;

  if (!us.fGraphics)
    return;
  if (gs.xWin == 0)
    gs.xWin = DEFAULTX;
  if (gs.yWin == 0)
    gs.yWin = DEFAULTY;
  hdc = GetDC(wi.hwnd);
  xScr = GetDeviceCaps(hdc, HORZRES);
  yScr = GetDeviceCaps(hdc, VERTRES);
  ReleaseDC(wi.hwnd, hdc);
  GetWindowRect(wi.hwnd, &rcOld);
  GetClientRect(wi.hwnd, &rcCli);
  rcNew.left = rcOld.left + gi.xOffset;
  rcNew.top  = rcOld.top  + gi.yOffset;
  rcNew.right = rcNew.left + gs.xWin + (gi.nMode == 0 ? (SIDESIZE *
    gi.nScaleText) >> 1 : 0) + (rcOld.right - rcOld.left - rcCli.right);
  rcNew.bottom = rcNew.top + gs.yWin +
    (rcOld.bottom - rcOld.top - rcCli.bottom);
  if (rcNew.right > xScr)
    OffsetRect(&rcNew, xScr - rcNew.right, 0);
  if (rcNew.bottom > yScr)
    OffsetRect(&rcNew, 0, yScr - rcNew.bottom);
  if (rcNew.left < 0)
    OffsetRect(&rcNew, -rcNew.left, 0);
  if (rcNew.top < 0)
    OffsetRect(&rcNew, 0, -rcNew.top);
  wi.fNotManual = fTrue;
  MoveWindow(wi.hwnd, rcNew.left, rcNew.top,
    rcNew.right - rcNew.left, rcNew.bottom - rcNew.top, fTrue);
  wi.fNotManual = fFalse;
}
#endif


#if !defined(WIN) && !defined(QT)
// This routine gets called after graphics are brought up and displayed on
// the screen. It loops, processing key presses, mouse clicks, etc, that the
// window receives, until the user specifies they want to exit the program.
// (Qt builds use InteractQt() in qtdriver.cpp instead, since Qt drives its
// own event loop rather than polling XNextEvent() in a manual switch here.)

void InteractX()
{
#ifdef X11
  char sz[cchSzDef];
  XEvent xevent;
  KeySym keysym;
#endif
#ifdef WCLI
  HBITMAP hbmpOld;
  HDC hdcWin;
  PAINTSTRUCT ps;
  MSG msg;
  int nMsg;
#endif
  int fAutosize = fFalse, fResize = fFalse, fRedraw = fTrue, fNoChart = fFalse,
    fBreak = fFalse, fCast = fFalse, mousex = -1, mousey = -1,
    buttonx = -1, buttony = -1, length, key, i;

  neg(gs.nAnim);
  while (!fBreak) {
    gi.nScale = gs.nScale/100;
    gi.nScaleText = gs.nScaleText/50;
#ifdef WCLI
    if (wi.fDoResize) {
      wi.fDoResize = fFalse;
      fResize = fTrue;
    }
    if (wi.fDoRedraw) {
      wi.fDoRedraw = fFalse;
      fRedraw = fTrue;
    }
    if (wi.fDoCast) {
      wi.fDoCast = fFalse;
      fCast = fTrue;
    }
#endif

    // Some chart windows, like the world maps and aspect grids, should always
    // be a certain size, so correct if a resize was attempted.

    if (fMap) {
      length = nDegMax*gi.nScale;
      if (gs.xWin != length) {
        gs.xWin = length;
        fResize = fTrue;
      }
      length = nDegHalf*gi.nScale;
      if (gs.yWin != length) {
        gs.yWin = length;
        fResize = fTrue;
      }
    } else if (gi.nMode == gGrid) {
      length = (gi.nGridCell + (us.nRel <= rcDual))*CELLSIZE*gi.nScale + 1;
      if (gs.xWin != length) {
        gs.xWin = length;
        fResize = fTrue;
      }
      if (gs.yWin != length) {
        gs.yWin = length;
        fResize = fTrue;
      }

    // Make sure the window isn't too large or too small.

    } else {
      if (gs.fKeepSquare && fSquare) {
        if (fSidebar)
          gs.xWin -= (SIDESIZE * gi.nScaleText) >> 1;
        if (gs.xWin != gs.yWin) {
          i = Min(gs.xWin, gs.yWin);
          i = Max(i, BITMAPX1);
          gs.xWin = gs.yWin = i;
          fResize = fTrue;
        }
        if (fSidebar)
          gs.xWin += (SIDESIZE * gi.nScaleText) >> 1;
      }
      if (gs.xWin < BITMAPX1) {
        gs.xWin = BITMAPX1;
        fResize = fTrue;
      } else if (gs.xWin > BITMAPX) {
        gs.xWin = BITMAPX;
        fResize = fTrue;
      }
      if (gs.yWin < BITMAPY1) {
        gs.yWin = BITMAPY1;
        fResize = fTrue;
      } else if (gs.yWin > BITMAPY) {
        gs.yWin = BITMAPY;
        fResize = fTrue;
      }
    }

    // Negative animation jump rate means first time with this rate.

    if (gs.nAnim < 0)
      neg(gs.nAnim);

    // Physically resize window if we've changed the size parameters.

    if (fAutosize) {
      fAutosize = fFalse;
      gs.xWin = gi.xWinResize; gs.yWin = gi.yWinResize;
      fResize = fTrue;
    }
    if (fResize) {
      fResize = fFalse;
#ifdef X11
      XResizeWindow(gi.disp, gi.wind, gs.xWin, gs.yWin);
      XFreePixmap(gi.disp, gi.pmap);
      gi.pmap = XCreatePixmap(gi.disp, gi.wind, gs.xWin, gs.yWin, gi.depth);
#endif
#ifdef WCLI
      ResizeWindowToChart();
#endif
      fRedraw = fTrue;
    }

    // Recast chart if the chart information has changed any.

    if (fCast) {
      fCast = fFalse;
      ciCore = ciMain;
      if (us.nRel)
        CastRelation();
      else
        CastChart(0);
      fRedraw = fTrue;
    }
    if (gs.nAnim && !gi.fPause)
      fRedraw = fTrue;

    // Update the screen if anything has changed since last time around.

    if (fRedraw && (!gi.fPause || gs.nAnim)) {
      fRedraw = fFalse;

      // If we're in animation mode, change the chart info appropriately.
      if (gs.nAnim && !gi.fPause)
        Animate(gs.nAnim, gi.nDir);

      // Clear the screen and set up a buffer to draw in.
#ifdef X11
      if (!gs.fJetTrail)
        XFillRectangle(gi.disp, gi.pmap, gi.pmgc, 0, 0, gs.xWin, gs.yWin);
#endif
#ifdef WCLI
      InvalidateRect(wi.hwnd, NULL, fFalse);
      ClearB((pbyte)&ps, sizeof(PAINTSTRUCT));
      hdcWin = BeginPaint(wi.hwnd, &ps);
      wi.hdc = CreateCompatibleDC(hdcWin);
      wi.hbmp = CreateCompatibleBitmap(hdcWin, wi.xClient, wi.yClient);
      hbmpOld = (HBITMAP)SelectObject(wi.hdc, wi.hbmp);
      if (gs.fJetTrail)
        BitBlt(wi.hdc, 0, 0, wi.xClient, wi.yClient, hdcWin, 0, 0, SRCCOPY);
      SetWindowOrg(wi.hdc, 0, 0);
      SetWindowExt(wi.hdc, gs.xWin, gs.yWin);
      SetMapMode(wi.hdc, MM_ANISOTROPIC);
      SelectObject(wi.hdc, GetStockObject(NULL_PEN));
      SelectObject(wi.hdc, GetStockObject(NULL_BRUSH));
      if (!gs.fJetTrail)
        PatBlt(wi.hdc, 0, 0, gs.xWin, gs.yWin,
          gs.fInverse ? WHITENESS : BLACKNESS);
#endif
      if (fNoChart)
        fNoChart = fFalse;
      else
        DrawChartX();

      // Make the drawn chart visible in the current screen buffer.
#ifdef X11
      XSync(gi.disp, 0);
      XCopyArea(gi.disp, gi.pmap, gi.wind, gi.gc,
        0, 0, gs.xWin, gs.yWin, 0, 0);
#endif
#ifdef WCLI
      BitBlt(hdcWin, 0, 0, wi.xClient, wi.yClient,
        wi.hdc, 0, 0, SRCCOPY);
      SelectObject(wi.hdc, hbmpOld);
      DeleteObject(wi.hbmp);
      DeleteDC(wi.hdc);
      EndPaint(wi.hwnd, &ps);
#endif
#ifdef EXPRESS
      // Notify AstroExpression the screen has just been redrawn.
      if (!us.fExpOff && FSzSet(us.szExpDisp3))
        ParseExpression(us.szExpDisp3);
#endif
    } // if

    // Now process what's on the event queue, i.e. any keys pressed, etc.

#ifdef X11
    if (XEventsQueued(gi.disp, QueuedAfterFlush /*QueuedAfterReading*/) ||
      !gs.nAnim || gi.fPause) {
      XNextEvent(gi.disp, &xevent);

      // Restore what's on window if a part of it gets uncovered.
      if (xevent.type == Expose && xevent.xexpose.count == 0) {
        XSync(gi.disp, 0);
        XCopyArea(gi.disp, gi.pmap, gi.wind, gi.gc,
          0, 0, gs.xWin, gs.yWin, 0, 0);
      }
      switch (xevent.type) {

      // Check for a manual resize of window by user.
      case ConfigureNotify:
        gi.xWinResize = gs.xWin = xevent.xconfigure.width;
        gi.yWinResize = gs.yWin = xevent.xconfigure.height;
        XFreePixmap(gi.disp, gi.pmap);
        gi.pmap = XCreatePixmap(gi.disp, gi.wind, gs.xWin, gs.yWin, gi.depth);
        fRedraw = fTrue;
        break;
      case MappingNotify:
        XRefreshKeyboardMapping((XMappingEvent *)&xevent);
        break;

      // Process any mouse buttons the user pressed.
      case ButtonPress:
        mousex = xevent.xbutton.x; mousey = xevent.xbutton.y;
        if (xevent.xbutton.button == Button1) {
          DrawColor(gi.kiLite);
          DrawPoint(mousex, mousey);
          XSync(gi.disp, 0);
          XCopyArea(gi.disp, gi.pmap, gi.wind, gi.gc,
            0, 0, gs.xWin, gs.yWin, 0, 0);
        } else if (xevent.xbutton.button == Button2 && (gi.nMode ==
          gAstroGraph || gi.nMode == gWorldMap) && gs.rRot == 0.0) {
          Lon = rDegHalf -
            (real)(xevent.xbutton.x-1)/(real)(gs.xWin-2)*rDegMax;
          Lat = rDegQuad -
            (real)(xevent.xbutton.y-1)/(real)(gs.yWin-2)*181.0;
          sprintf2(S(sz), "Mouse is at %s.", SzLocation(Lon, Lat));
          PrintProgress(sz);
        } else if (xevent.xbutton.button == Button3)
          fBreak = fTrue;
        break;

      // Check for user dragging any of the mouse buttons across window.
      case MotionNotify:
        DrawColor(gi.kiPen);
        DrawLine(mousex, mousey, xevent.xbutton.x, xevent.xbutton.y);
        XSync(gi.disp, 0);
        XCopyArea(gi.disp, gi.pmap, gi.wind, gi.gc,
          0, 0, gs.xWin, gs.yWin, 0, 0);
        mousex = xevent.xbutton.x; mousey = xevent.xbutton.y;
        break;

      // Process any keys user pressed in window.
      case KeyPress:
        length = XLookupString((XKeyEvent *)&xevent, xkey, 10, &keysym, 0);
        if (length == 1) {
          key = xkey[0];
#endif // X11

#ifdef WCLI
      if (PeekMessage(&msg, (HWND)NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        nMsg = LOWORD(msg.message);
        if (nMsg != WM_CHAR && nMsg != WM_KEYDOWN) {
          switch (nMsg) {
          case WM_SIZE:
            wi.xClient = gs.xWin = LOWORD(msg.lParam);
            wi.yClient = gs.yWin = HIWORD(msg.lParam);
            fResize = fTrue;
            break;
          case WM_PAINT:
            fRedraw = fTrue;
            break;
          default:
            DispatchMessage(&msg);
          }
        } else {
          key = (int)msg.wParam;
          if (nMsg == WM_KEYDOWN)
            key = FBetween(key, VK_F1, VK_F12) ? key - VK_F1 + 201 : 0;
#endif // WCLI

#ifdef EXPRESS
          // May want to adjust current key if AstroExpression says to do so.
          if (!us.fExpOff && FSzSet(us.szExpKey)) {
            ExpSetN(iLetterZ, key);
            ParseExpression(us.szExpKey);
            key = NExpGet(iLetterZ);
          }
#endif
          switch (key) {
          case -1:                    // In case ~XQ returns -1
            break;
          case -2:
            fResize = fCast = fTrue;  // Special ~XQ return value
            break;
          case ' ':
            fRedraw = fTrue;
            break;
          case 'p':
            inv(gi.fPause);
            break;
          case 'r':
            neg(gi.nDir);
            break;
          case 'x':
            inv(gs.fInverse);
            InitColorPalette(gs.fInverse);
            InitColorsX();
            fRedraw = fTrue;
            break;
          case 'm':
            if (!gi.fMono) {
              inv(gs.fColor);
              InitColorsX();
              fRedraw = fTrue;
            }
            break;
#ifdef X11
          case 'B':
            XSetWindowBackgroundPixmap(gi.disp, gi.root, gi.pmap);
            XClearWindow(gi.disp, gi.root);
            break;
#endif
          case 't':
            inv(gs.fText);
            fRedraw = fTrue;
            break;
          case 'i':
            inv(gs.fAlt);
            fRedraw = fTrue;
            break;
          case 'b':
            inv(gs.fBorder);
            fRedraw = fTrue;
            break;
          case 'q':
            inv(gs.fThick);
            fRedraw = fTrue;
            break;
          case 'l':
            inv(gs.fLabel);
            fRedraw = fTrue;
            break;
          case 'k':
            inv(gs.fLabelAsp);
            fRedraw = fTrue;
            break;
          case 'j':
            inv(gs.fJetTrail);
            break;
          case '<':
            if (gs.nScale > 100) {
              gs.nScale -= 100;
              fResize = fTrue;
            }
            break;
          case '>':
            if (gs.nScale < MAXSCALE) {
              gs.nScale += 100;
              fResize = fTrue;
            }
            break;
          case '[':
            if (gs.rTilt > -rDegQuad) {
              gs.rTilt = gs.rTilt > -rDegQuad ?
                gs.rTilt-(real)NAbs(gi.nDir) : -rDegQuad;
              fRedraw = fTrue;
            }
            if (gi.nMode == gTelescope)
              gs.objTrack = -1;
            break;
          case ']':
            if (gs.rTilt < rDegQuad) {
              gs.rTilt = gs.rTilt < rDegQuad ?
                gs.rTilt+(real)NAbs(gi.nDir) : rDegQuad;
              fRedraw = fTrue;
            }
            if (gi.nMode == gTelescope)
              gs.objTrack = -1;
            break;
          case '{':
            if (gi.nMode == gMidpoint || gi.nMode == gTelescope) {
              if (gi.nMode == gMidpoint && gs.objTrack >= 0)
                gs.rRot = planet[gs.objTrack];
              gs.objTrack = -1;
            }
            gs.rRot += (real)NAbs(gi.nDir);
            if (gs.rRot >= rDegMax)
              gs.rRot -= rDegMax;
            fRedraw = fTrue;
            break;
          case '}':
            if (gi.nMode == gMidpoint || gi.nMode == gTelescope) {
              if (gi.nMode == gMidpoint && gs.objTrack >= 0)
                gs.rRot = planet[gs.objTrack];
              gs.objTrack = -1;
            }
            gs.rRot -= (real)NAbs(gi.nDir);
            if (gs.rRot < 0.0)
              gs.rRot += rDegMax;
            fRedraw = fTrue;
            break;
          case 'Q':
            SquareX(&gs.xWin, &gs.yWin, fTrue);
            fResize = fTrue;
            break;
          case 'R':
            for (i = oChi; i <= oVes; i++)
              inv(ignore[i]);
            for (i = oSou; i <= oEP; i++)
              inv(ignore[i]);
            AdjustRestrictions();
            fCast = fTrue;
            break;
          case 'C':
            inv(us.fCusp);
            for (i = cuspLo; i <= cuspHi; i++)
              ignore[i] = !us.fCusp || !ignore[i];
            AdjustRestrictions();
            fCast = fTrue;
            break;
          case 'u':
            inv(us.fUranian);
            for (i = uranLo; i <= uranHi; i++)
              ignore[i] = !us.fUranian || !ignore[i];
            AdjustRestrictions();
            fCast = fTrue;
            break;
          case 'y':
            inv(us.fDwarf);
            for (i = dwarfLo; i <= dwarfHi; i++)
              ignore[i] = !us.fDwarf || !ignore[i];
            AdjustRestrictions();
            fCast = fTrue;
            break;
          case '`':
            inv(us.fMoons);
            for (i = moonsLo; i <= moonsHi; i++)
              ignore[i] = !us.fMoons || !ignore[i];
            AdjustRestrictions();
            fCast = fTrue;
            break;
          case '~':
            inv(us.fCOB);
            for (i = cobLo; i <= cobHi; i++)
              ignore[i] = !us.fCOB || !ignore[i];
            AdjustRestrictions();
            fCast = fTrue;
            break;
          case 'U':
            inv(us.fStar);
            for (i = starLo; i <= starHi; i++)
              ignore[i] = !us.fStar || !ignore[i];
            AdjustRestrictions();
            fCast = fTrue;
            break;
          case 'c':
            us.nRel = us.nRel ? rcNone : rcDual;
            fCast = fTrue;
            break;
          case 's':
            inv(us.fSidereal);
            fCast = fTrue;
            break;
          case 'h':
            inv(us.objCenter);
            fCast = fTrue;
            break;
          case 'a':
            inv(us.fHouse3D);
            fCast = fTrue;
            break;
          case 'g':
            inv(us.fDecan);
            fCast = fTrue;
            break;
          case 'f':
            inv(us.fFlip);
            fCast = fTrue;
            break;
          case 'z':
            inv(us.fIndian);
            fRedraw = fTrue;
            break;
          case '+':
            Animate(gs.nAnim, NAbs(gi.nDir));
            fCast = fTrue;
            break;
          case '-':
            Animate(gs.nAnim, -NAbs(gi.nDir));
            fCast = fTrue;
            break;
          case 'o':
            ciSave = ciMain;
            break;
          case 'O':
            ciMain = ciSave;
            fCast = fTrue;
            break;
#ifdef TIMEFUNC
          case 'n':
            Animate(10, 0);
            ciMain = ciCore;
            fRedraw = fTrue;
            break;
#endif
          case 'N':                        // The continuous update animation.
            gs.nAnim = gs.nAnim ? 0 : -iAnimNow;
            break;

          // These are the nine different "add time to chart" animations.
          case '!': gs.nAnim = -1; break;
          case '@': gs.nAnim = -2; break;
          case '#': gs.nAnim = -3; break;
          case '$': gs.nAnim = -4; break;
          case '%': gs.nAnim = -5; break;
          case '^': gs.nAnim = -6; break;
          case '&': gs.nAnim = -7; break;
          case '*': gs.nAnim = -8; break;
          case '(': gs.nAnim = -9; break;

          // Should we go switch to a new chart type?
          case 'V': gi.nMode = gWheel;      fAutosize = fTrue; break;
          case 'A': gi.nMode = gGrid;       fRedraw   = fTrue; break;
          case 'Z': gi.nMode = gHorizon;    fAutosize = fTrue; break;
          case 'S': gi.nMode = gOrbit;      fAutosize = fTrue; break;
          case 'H': gi.nMode = gSector;     fAutosize = fTrue; break;
          case 'K': gi.nMode = gCalendar;   fAutosize = fTrue; break;
          case 'J': gi.nMode = gDisposit;   fAutosize = fTrue; break;
          case 'L': gi.nMode = gAstroGraph; fRedraw   = fTrue; break;
          case 'E': gi.nMode = gEphemeris;  fAutosize = fTrue; break;
          case 'I': gi.nMode = gRising;     fAutosize = fTrue; break;
          case 'M': gi.nMode = gMoons;      fAutosize = fTrue; break;
          case 'X': gi.nMode = gSphere;     fAutosize = fTrue; break;
          case 'W': gi.nMode = gWorldMap;   fRedraw   = fTrue; break;
          case 'G': gi.nMode = gGlobe;      fAutosize = fTrue; break;
          case 'P': gi.nMode = gPolar;      fAutosize = fTrue; break;
          case 'T': gi.nMode = gTelescope;  fAutosize = fTrue; break;
#ifdef BIORHYTHM
          case 'Y':                 // Should we switch to biorhythm chart?
            us.nRel = rcBiorhythm;
            gi.nMode = gBiorhythm;
            fCast = fTrue;
            break;
#endif
          case '=':
            inv(gs.fIndianWheel);
            fRedraw = fTrue;
            break;
#ifdef CONSTEL
          case 'F':
            if (gi.nMode != gHorizon && gi.nMode != gSphere && gi.nMode !=
              gGlobe && gi.nMode != gPolar && gi.nMode != gTelescope)
              gi.nMode = gWorldMap;
            inv(gs.fConstel);
            fRedraw = fTrue;
            break;
#endif
          case 'd':
            inv(gs.fHouseExtra);
            fRedraw = fTrue;
            break;
          case 'e':
            inv(gs.fEquator);
            fRedraw = fTrue;
            break;
#ifndef X11          // Astrolog can't draw 24 bit color bitmaps on X11.
          case 'w':
            inv(gi.fBmp);
            fRedraw = fTrue;
            break;
#endif
          case '0':
            inv(us.fPrimeVert);
            inv(us.fCalendarYear);
            inv(us.nEphemYears);
            inv(gs.fMollweide);
            gi.nMode = (gi.nMode == gWheel ? gHouse :
              (gi.nMode == gHouse ? gWheel : gi.nMode));
            fRedraw = fTrue;
            break;
          case 'v': case '?':
            length = us.nScrollRow;
            us.nScrollRow = 0;
            PrintL();
            if (key == 'v') {
              is.fMult = fFalse;
              PrintChart(us.fProgress);
            } else
              DisplayKeysX();
            us.nScrollRow = length;
            break;
          case chReturn:
            CommandLineX();
            fResize = fCast = fTrue;
            break;
          case chDelete:
            fRedraw = fNoChart = fTrue;
            break;
          case 'z'-'`': gi.kiPen = kBlack;   break;
          case 'e'-'`': gi.kiPen = kMaroon;  break;
          case 'f'-'`': gi.kiPen = kDkGreen; break;
          case 'o'-'`': gi.kiPen = kOrange;  break;
          case 'n'-'`': gi.kiPen = kDkBlue;  break;
          case 'u'-'`': gi.kiPen = kPurple;  break;
          case 'k'-'`': gi.kiPen = kDkCyan;  break;
          case 'l'-'`': gi.kiPen = kLtGray;  break;
          case 'd'-'`': gi.kiPen = kDkGray;  break;
          case 'r'-'`': gi.kiPen = kRed;     break;
          case 'g'-'`': gi.kiPen = kGreen;   break;
          case 'y'-'`': gi.kiPen = kYellow;  break;
          case 'b'-'`': gi.kiPen = kBlue;    break;
          case 'v'-'`': gi.kiPen = kMagenta; break;  // Ctrl+m is Enter
          case 'j'-'`': gi.kiPen = kCyan;    break;
          case 'a'-'`': gi.kiPen = kWhite;   break;
          case chEscape: case chBreak:
            fBreak = fTrue;
            break;
          default:
            if (FBetween(key, '1', '9')) {
              // Process numbers 1-9 signifying animation rate.
              gi.nDir = (gi.nDir > 0 ? 1 : -1)*(key-'0');
              break;
            } else if (FBetween(key, 201, 248)) {
              if (is.rgszMacro != NULL && is.rgszMacro[key-200]) {
                is.fSzInteract = fTrue;
                FProcessCommandLine(is.rgszMacro[key-200]);
                is.fSzInteract = fFalse;
                fResize = fCast = fTrue;
                break;
              }
            }
            putchar(chBell);    // Any key not bound will sound a beep.
          } // switch
        } // if
#ifdef X11
      default:
        ;
      } // switch
    } // if
#endif
#ifdef WCLI
    } // if
#endif
  } // while
}
#endif // !WIN && !QT


// This is called right before program termination to get rid of the window.

void EndX()
{
#ifdef X11
  XFreeGC(gi.disp, gi.gc);
  XFreeGC(gi.disp, gi.pmgc);
  XFreePixmap(gi.disp, gi.pmap);
  XDestroyWindow(gi.disp, gi.wind);
  XCloseDisplay(gi.disp);
#endif
#ifdef QT
  EndQt();
#endif
#ifdef WCLI
  UnregisterClass(szAppName, wi.hinst);
#endif
}
#endif // WIN


/*
******************************************************************************
** Main Graphics Processing.
******************************************************************************
*/

#ifdef SWISS
// Process an instance of the -YXU command switch.

flag FProcessYXU(CONST char *szLin, CONST char *szLnk, flag fAdd)
{
  char *pch;

  // Allocate or extend allocation of star name list.
  pch = (char *)PAllocate((fAdd ? CchSz(gs.szStarsLin) + 1 : 0) +
    CchSz(szLin) + 1, "star name list");
  if (pch == NULL)
    return fFalse;
  if (fAdd)
    sprintf(pch, "%s;%s", gs.szStarsLin, szLin);
  else
    sprintf(pch, "%s", szLin);
  DeallocatePIf(gs.szStarsLin);
  gs.szStarsLin = pch;

  // Allocate or extend allocation of star link list.
  pch = (char *)PAllocate((fAdd ? CchSz(gs.szStarsLnk) + 1 : 0) +
    CchSz(szLnk) + 2, "star link list");
  if (pch == NULL)
    return fFalse;
  if (fAdd)
    sprintf(pch, "%s;%s", gs.szStarsLnk, szLnk);
  else
    sprintf(pch, "%s", szLnk);
  DeallocatePIf(gs.szStarsLnk);
  gs.szStarsLnk = pch;

  // Count total number of star names present, and reserve that many slots.
  gi.cStarsLin = *gs.szStarsLin != chNull;
  for (pch = gs.szStarsLin; *pch; pch++)
    if (*pch == chSep || *pch == chSep2)
      gi.cStarsLin++;
  if (gi.rges != NULL) {
    DeallocateP(gi.rges);
    gi.rges = NULL;
  }
  gi.rges = RgAllocate(gi.cStarsLin, ES, "extra stars");
  if (gi.rges == NULL)
    gi.cStarsLin = 0;
  return fTrue;
}
#endif


// Process one command line switch passed to the program dealing with the
// graphics features. This is just like the processing of each switch in the
// main program, however here each switch has been prefixed with an 'X'.



// Every chart mode paired with the us.f* flag that selects it -- the one
// flag<->mode table. Three consumers share it: DetectGraphicsChartMode()
// below scans it, Windows' ProcessState() (wdriver.cpp) and the Qt port's
// SetChartModeQt() family (qtdriver.cpp) clear and set flags through it.
// Before this table each of the three kept its own copy of the mapping
// and they could silently drift apart.
//
// Row order is load-bearing: the first cchartmodeDetect rows are the
// modes DetectGraphicsChartMode() considers, in its priority order --
// first row whose flag is set wins. The rows after that line are modes
// detection has never covered (several are GUI-menu-only chart types);
// adding one to detection means moving it above the line, in priority
// position, not just adding it to the table.

CONST CHARTMODE rgchartmode[] = {
  // Detection rows, in priority order.
  {gHouse,      &us.fWheel},
  {gGrid,       &us.fGrid},     // us.fAspect also detects as gGrid, below
  {gMidpoint,   &us.fMidpoint},
  {gHorizon,    &us.fHorizon},
  {gOrbit,      &us.fOrbit},
  {gSector,     &us.fSector},
  {gDisposit,   &us.fInfluence},
  {gEsoteric,   &us.fEsoteric},
  {gAstroGraph, &us.fAstroGraph},
  {gCalendar,   &us.fCalendar},
  {gEphemeris,  &us.fEphemeris},
  {gRising,     &us.fHorizonSearch},
  {gLocal,      &us.fAtlasNear},
  {gMoons,      &us.fMoonChart},
  {gTraTraGra,  &us.fInDayGra},
  {gTraNatGra,  &us.fTransitGra},
  // Modes below this line are not consulted by detection.
  {gWheel,      &us.fListing},
  {gExo,        &us.fExoTransit},
#if defined(WIN) || defined(QT)
  {gAspect,     &us.fAspList},
  {gArabic,     &us.fArabic},
  {gTraTraTim,  &us.fInDay},
  {gTraTraInf,  &us.fInDayInf},
  {gTraNatTim,  &us.fTransit},
  {gTraNatInf,  &us.fTransitInf},
  {gSign,       &us.fSign},
  {gObject,     &us.fObject},
  {gHelpAsp,    &us.fAspect},
  {gConstel,    &us.fConstel},
  {gPlanet,     &us.fOrbitData},
  {gRay,        &us.fRay},
  {gMeaning,    &us.fMeaning},
  {gSwitch,     &us.fSwitch},
  {gObscure,    &us.fSwitchRare},
  {gKeystroke,  &us.fKeyGraph},
  {gCredit,     &us.fCredit},
#endif
};

CONST int cchartmode = (int)(sizeof(rgchartmode) / sizeof(CHARTMODE));
CONST int cchartmodeDetect = 16;


// Figure out what graphics mode a graphics chart should be generated in,
// based on various command switches in effect, e.g. -L combined with -X,
// -g combined with -X, and so on.

int DetectGraphicsChartMode()
{
  int i;

  for (i = 0; i < cchartmodeDetect; i++)
    if (*rgchartmode[i].pf ||
      // -HA has always detected as an aspect grid, at -g's priority slot.
      (rgchartmode[i].nMode == gGrid && us.fAspect))
      return rgchartmode[i].nMode;
  if (us.nRel == rcBiorhythm)  // A value test, not a flag, so not a row.
    return gBiorhythm;
  return gWheel;
}


// This is the main interface to all the graphics features. This routine is
// called from the main program if any of the -X switches were specified,
// and it sets up for and goes and generates the appropriate graphics chart.
// Return fTrue if successfull, or fFalse if some error occurred.

flag FActionX()
{
  int i, n, nScaleAdjust = 1, nScaleSav, yAdd;
  flag fAutoScaled = fFalse;
#ifdef WIN
  HDC hdcWin, hdcMem;
  HBITMAP hbmp, hbmpOld;
  int x, y, nScaleWin = 1;
#endif

  gi.fFile = (gs.ft != ftNone);
  if (gi.nMode == 0)
    gi.nMode = DetectGraphicsChartMode();

  gi.nScaleT = gs.ft == ftPS ? PSMUL : (gs.ft == ftWmf ? METAMUL :
    (gs.ft == ftSVG ? SVGMUL : (gs.ft == ftWire ? WIREMUL : 1)));
#ifdef WIN
  if (wi.hdcPrint != hdcNil)
    wi.nScaleWin = gi.nScaleT = nScaleAdjust = METAMUL;
  else if (wi.fSmoothZoom && (!gi.fFile ||
    ((wi.fAutoSave && gs.ft == (!wi.fAutoSaveWire ? ftBmp : ftWire))))) {
    wi.nScaleWin = gi.nScaleT = nScaleAdjust = nScaleWin = wi.nAntialias;
    gs.xWin   *= wi.nScaleWin;  // Increase chart sizes and scales behind the
    gs.yWin   *= wi.nScaleWin;  // scenes to make graphics look smoother.
    gs.nScale *= wi.nScaleWin;
  } else
    wi.nScaleWin = 1;
#endif
  gi.nScale = gs.nScale/100;
  AdjustTextScale();

  // Determine the pixel size the graphics chart is to have.
  // Also determine glyph scale to use, if -XQ0 in effect.

  if (gi.nMode == gWheel || gi.nMode == gHouse || gi.nMode == gSector) {
    if (gs.fAutoScale) {
      // For wheels, scale glyphs based on size of chart.
      n = gs.yWin / nScaleAdjust;
      i = (n < 350 ? 1 : (n < 750 ? 2 : (n < 950 ? 3 : 4)));
      i *= nScaleAdjust;
      if (i != gi.nScale) {
        fAutoScaled = fTrue;
        nScaleSav = gs.nScale;
        gi.nScale = i; gs.nScale = i*100;
      }
    }
  }
  if (gi.nMode == gGrid) {
    if (us.nRel <= rcDual && us.fMidpoint && !us.fAspList)
      us.fGridMidpoint = fTrue;
    if (gs.nGridCell > 0)
      gi.nGridCell = gs.nGridCell;
    else
      for (gi.nGridCell = i = 0; i <= is.nObj; i++)
        gi.nGridCell += FProper(i);
    yAdd = (yFont2*gi.nScaleText + 2)*gi.nScaleT;
#ifdef WIN
    if (gs.fAutoScale) {
      // For aspect grids, scale chart/glyphs based on size of window.
      n = nScaleAdjust;
      for (i = n; i < MAXSCALE*20/100*n; i += n) {
        x = y = (gi.nGridCell + (us.nRel <= rcDual))*CELLSIZE*(i+n) + 1;
        if (gs.fText)
          y += yAdd;
        if (!(x <= wi.xClient*nScaleWin && y <= wi.yClient*nScaleWin))
          break;
      }
      if (i != gi.nScale) {
        fAutoScaled = fTrue;
        nScaleSav = gs.nScale;
        gi.nScale = i; gs.nScale = i*100;
      }
    }
#endif
    gs.xWin = gs.yWin =
      (gi.nGridCell + (us.nRel <= rcDual))*CELLSIZE*gi.nScale + 1;
    if (gs.fText)
      gs.yWin += yAdd;
  } else if (gs.fKeepSquare && fSquare) {
#ifdef WIN
    if (wi.hdcPrint == hdcNil) {
      if (fSidebar)
        gs.xWin -= (SIDESIZE * gi.nScaleText * wi.nScaleWin) >> 1;
#endif
      n = Min(gs.xWin, gs.yWin);
      gs.xWin = gs.yWin = n;
#ifdef WIN
      if (fSidebar)
        gs.xWin += (SIDESIZE * gi.nScaleText * wi.nScaleWin) >> 1;
    }
#endif
  } else if (fMap) {
    yAdd = (yFont2*gi.nScaleText + 2)*gi.nScaleT;
#ifdef WIN
    if (gs.fAutoScale) {
      // For maps, scale chart/glyphs based on size of window.
      n = nScaleAdjust;
      for (i = n; i < MAXSCALE*20/100*n; i += n) {
        x = nDegMax*(i+n); y = nDegHalf*(i+n);
        if (gs.fText)
          y += yAdd;
        if (!(x <= wi.xClient*nScaleWin && y <= wi.yClient*nScaleWin))
          break;
      }
      if (i != gi.nScale) {
        fAutoScaled = fTrue;
        nScaleSav = gs.nScale;
        gi.nScale = i; gs.nScale = i*100;
      }
    }
#endif
    gs.xWin = nDegMax*gi.nScale;
    gs.yWin = nDegHalf*gi.nScale;
    if (gs.fText)
      gs.yWin += yAdd;
  }
#ifdef WIN
  if (fSidebar)
    gs.xWin -= (SIDESIZE * gi.nScaleText) >> 1;
#endif

  if (gi.fFile) {
    if (!BeginFileX())
      if (gs.ft == ftPS || gs.ft == ftSVG) {
        gs.ft = ftNone; gi.fFile = fFalse;
        return fFalse;
      }
    if (gs.xWin == 0)
      gs.xWin = DEFAULTX;
    if (gs.yWin == 0)
      gs.yWin = DEFAULTY;
    if (fSidebar)
      gs.xWin += (SIDESIZE * gi.nScaleText) >> 1;
    if (gs.xWin > BITMAPX)
      gs.xWin = BITMAPX;
    if (gs.yWin > BITMAPY)
      gs.yWin = BITMAPY;
    if (gs.ft == ftBmp) {
      if (!gi.fBmp) {
        gi.cbBmpRow = (gs.xWin + 1) >> 1;
        if ((gi.bm = PAllocate(gi.cbBmpRow * gs.yWin, "bitmap")) == NULL)
          return fFalse;
      } else {
        if (!FAllocateBmp(&gi.bmp, gs.xWin, gs.yWin))
          return fFalse;
      }
    }
#ifdef PSCRIPT
    else if (gs.ft == ftPS) {
      PsBegin();
      gs.nScale *= PSMUL;  // Increase chart sizes and scales behind the
      gs.xWin   *= PSMUL;  // scenes to make graphics look smoother.
      gs.yWin   *= PSMUL;
    }
#endif
#ifdef METAFILE
    else if (gs.ft == ftWmf) {
      for (gi.cbMeta = MAXMETA; gi.cbMeta > 0 &&
        (gi.bm = PAllocate(gi.cbMeta, "metafile")) == NULL;
        gi.cbMeta -= MAXMETA/8)
        PrintWarning("Attempting to get maximum memory for metafile.");
      if (gi.cbMeta <= 0)
        return fFalse;
      gs.xWin   *= METAMUL;  // Increase chart sizes and scales behind the
      gs.yWin   *= METAMUL;  // scenes to make graphics look smoother.
      gs.nScale *= METAMUL;
    }
#endif
#ifdef SVG
    else if (gs.ft == ftSVG) {
      gs.xWin   *= SVGMUL;  // Increase chart sizes and scales behind the
      gs.yWin   *= SVGMUL;  // scenes to make graphics look smoother.
      gs.nScale *= SVGMUL;
      fprintf(gi.file, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
      fprintf(gi.file, "<svg xmlns=\"http://www.w3.org/2000/svg\" "
        "viewBox=\"0 0 %d %d\">\n", gs.xWin, gs.yWin);
      fprintf(gi.file, "<!-- %s %s -->\n", szAppName, szVersionCore);
      n = SVGMUL*(1+gs.fThick) + gs.nThickAdjust;
      fprintf(gi.file, "<g stroke-width=\"%d\" stroke-linecap=\"round\" "
        "fill=\"none\" style=\"white-space:pre\">\n", Max(n, 1));
      gi.kiSvgAct = kMax;
    }
#endif
#ifdef WIRE
    else {
      gs.xWin   *= WIREMUL;  // Increase chart sizes and scales behind the
      gs.yWin   *= WIREMUL;  // scenes to make graphics look smoother.
      gs.nScale *= WIREMUL;
      for (gi.cbWire = MAXMETA; gi.cbWire > 0 &&
        (gi.bm = PAllocate(gi.cbWire, "wireframe")) == NULL;
        gi.cbWire -= MAXMETA/8)
        PrintWarning("Attempting to get maximum memory for wireframe.");
      if (gi.cbWire <= 0)
        return fFalse;
      gi.pwWireCur = (word *)gi.bm;
      gi.cWire = gi.zDefault = 0;
      gi.kiInFile = -1;
    }
#endif
    InitColorsX();
  }
#ifdef ISG
  else {
    if (gs.xWin == 0 || gs.yWin == 0) {
      if (gs.xWin == 0)
        gs.xWin = DEFAULTX;
      if (gs.yWin == 0)
        gs.yWin = DEFAULTY;
      SquareX(&gs.xWin, &gs.yWin, fFalse);
    } else if (fSidebar)
      gs.xWin += (SIDESIZE * gi.nScaleText) >> 1;
    BeginX();
  }
#endif

  if (gi.fFile || gs.fRoot) {    // Go draw the graphic chart.
#ifdef WIN
    if (wi.fBmpWindow && gs.ft == ftBmp && gi.fBmp) {
      // Create the bitmap by copying from the Windows screen.
      hdcWin = GetDC(wi.hwnd);
      hdcMem = CreateCompatibleDC(hdcWin);
      hbmp = CreateCompatibleBitmap(hdcWin, wi.xClient, wi.yClient);
      hbmpOld = (HBITMAP)SelectObject(hdcMem, hbmp);
      BitBlt(hdcMem, 0, 0, wi.xClient, wi.yClient, hdcWin, 0, 0, SRCCOPY);
      FBmpCopyFromWin(&gi.bmp, hdcMem, hbmp);
      SelectObject(hdcMem, hbmpOld);
      DeleteObject(hbmp);
      DeleteDC(hdcMem);
      ReleaseDC(wi.hwnd, hdcWin);
    } else
#endif
      DrawChartX();
  }
  if (gi.fFile) {    // Write bitmap to file if in that mode.
    EndFileX();
    if ((gs.ft == ftBmp && !gi.fBmp) || gs.ft == ftWmf || gs.ft == ftWire) {
      DeallocateP(gi.bm);
      gi.bm = NULL;
    }
  }
#ifdef ISG
  else {
#ifdef X11
    if (gs.fRoot) {                                           // Process -XB.
      XSetWindowBackgroundPixmap(gi.disp, gi.root, gi.pmap);
      XClearWindow(gi.disp, gi.root);

      // If -Xn in effect with -XB, then enter loop where continuously
      // calculate and animate chart, displaying on the root window.
      while (gs.nAnim) {
        Animate(gs.nAnim, 1);
        if (!gs.fJetTrail)
          XFillRectangle(gi.disp, gi.pmap, gi.pmgc, 0, 0, gs.xWin, gs.yWin);
        DrawChartX();
        XSetWindowBackgroundPixmap(gi.disp, gi.root, gi.pmap);
        XClearWindow(gi.disp, gi.root);
      }
    } else
#endif
#ifdef QT
      InteractQt();   // Window's up; hand control to Qt's own event loop.
    EndX();
#elif !defined(WIN)
      InteractX();    // Window's up; process commands given to window now.
    EndX();
#else
    DrawChartX();
#endif
  }
#endif // ISG
  if (gs.ft == ftPS) {
    gs.xWin /= PSMUL; gs.yWin /= PSMUL; gs.nScale /= PSMUL;
  } else if (gs.ft == ftWmf) {
    gs.xWin /= METAMUL; gs.yWin /= METAMUL; gs.nScale /= METAMUL;
  } else if (gs.ft == ftSVG) {
    gs.xWin /= SVGMUL; gs.yWin /= SVGMUL; gs.nScale /= SVGMUL;
  } else if (gs.ft == ftWire) {
    gs.xWin /= WIREMUL; gs.yWin /= WIREMUL; gs.nScale /= WIREMUL;
  }
  if (fAutoScaled) {
    gs.nScale = nScaleSav; gi.nScale = gs.nScale/100;
  }
  return fTrue;
}
#endif // GRAPH

/* xscreen.cpp */
