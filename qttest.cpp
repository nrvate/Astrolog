/*
** Astrolog (Version 8.00) File: qttest.cpp
**
** IMPORTANT NOTICE: Astrolog and all chart display routines and anything
** not enumerated below used in this program are Copyright (C) 1991-2026 by
** Walter D. Pullen (Astara@msn.com, http://www.astrolog.org/astrolog.htm).
** Permission is granted to freely use, modify, and distribute these
** routines provided these credits and notices remain unmodified with any
** altered or distributed versions of the program.
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
** This file is the automated test suite for the Qt GUI backend. It is
** compiled only into the separate "astrolog-qt-test" binary, built with
** Makefile.qt.test, which defines QTTEST; the shipped astrolog-qt does
** not contain any of it.
**
** Run it headless, with no X display needed:
**
**   make -f Makefile.qt.test
**   ./run-qt-tests.sh
**
** It enters through the normal startup path and is called from
** InteractQt() once the window, menus, hotkeys and first chart are all
** up, so everything it inspects is the real thing rather than a fixture.
** That also means it shares the program's global state (us/gs/gi), so
** tests that change a setting put it back.
*/

#include <QtWidgets/QApplication>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QAction>
#include <QtWidgets/QDialog>
#include <QtCore/QTimer>
#include <QtCore/QStringList>
#include <QtCore/QDir>
#include <QtGui/QImage>
#include <stdarg.h>
#include "astrolog.h"
#include "extern.h"
#include "qtdriver.h"

#ifdef QTTEST

// Hooks into qtdriver.cpp, where the menu tables are file static.
extern int CCtxTestQt();
extern QMenu *PmenuCtxTestQt(int, CONST char **);
extern int CHotkeyTestQt();
extern void HotkeyTestQt(int, CONST char **, CONST char **);
extern QAction *PaFindActionTestQt(CONST char *);
extern void AllActionsTestQt(QList<QAction *> *);
extern QAction *PaFindLooseTestQt(CONST char *, CONST char **);

static int s_cPass = 0, s_cFail = 0;
static CONST char *s_szGroup = "";
static QString s_strModal;

static void Group(CONST char *sz)
{
  s_szGroup = sz;
  printf("\n== %s ==\n", sz);
}

// Report one assertion. Passes are counted but only failures are printed,
// so a clean run stays short enough to actually read.
static void Check(flag fOk, CONST char *szFmt, ...)
{
  char sz[cchSzMax];
  va_list ap;

  va_start(ap, szFmt);
  vsprintf(sz, szFmt, ap);
  va_end(ap);
  if (fOk)
    s_cPass++;
  else {
    s_cFail++;
    printf("  FAIL  %s\n", sz);
    fflush(stdout);
  }
}


/*
******************************************************************************
** Dialogs.
******************************************************************************
*/

// Every dialog blocks in exec(), so the only way to inspect one is to
// queue the inspection before opening it. This runs "pfn", waits for
// whatever modal window it puts up, records the title, and closes it.

static QString StrOpenDialogQt(void (*pfn)())
{
  static QString strTitle;

  strTitle = QString();
  QTimer::singleShot(50, []() {
    QWidget *pw = QApplication::activeModalWidget();
    if (pw == NULL)
      pw = QApplication::activePopupWidget();
    if (pw != NULL) {
      strTitle = pw->windowTitle();
      pw->close();
    }
  });
  // Safety net: if nothing showed up, or close() didn't take, don't hang
  // the whole suite on one dialog.
  QTimer::singleShot(1500, []() {
    QWidget *pw = QApplication::activeModalWidget();
    if (pw != NULL)
      pw->close();
  });
  pfn();
  return strTitle;
}

typedef struct {
  void (*pfn)();
  CONST char *szTitle;   // Expected title, or NULL to only require one.
} DLGTEST;

static void TestDialogsQt()
{
  CONST DLGTEST rgdlg[] = {
    {ShowFileSettingsDialogQt,     "File Settings"},
    {ShowGraphicsSettingsDialogQt, "Graphics Settings"},
    {ShowChartInfoDialogQt,        "Set Chart Info"},
    {ShowChartsAllDialogQt,        "Charts #3 through #6"},
    {ShowChartListDialogQt,        "Chart List"},
    {ShowColorDialogQt,            "Set Colors"},
    {ShowObjectDialogQt,           "Object Settings"},
    {ShowObject2DialogQt,          "More Object Settings"},
    {ShowMoonObjectDialogQt,       "Planetary Moon Object Settings"},
    {ShowAspectDialogQt,           "Aspect Settings"},
    {ShowRestrictDialogQt,         "Object Restrictions"},
    {ShowStarRestrictDialogQt,     "Fixed Star Restrictions"},
    {ShowTransitRestrictDialogQt,  "Transit Object Restrictions"},
    {ShowMoonRestrictDialogQt,     "Planetary Moon Restrictions"},
    {ShowCustomDialogQt,           "Object Customization"},
    {ShowCustomStarDialogQt,       "Fixed Star Customization"},
    {ShowDefaultInfoDialogQt,      "Default Chart Info"},
    {ShowTransitDialogQt,          "Transits"},
    {ShowProgressDialogQt,         "Progressions"},
    {ShowChartSettingsDialogQt,    "Chart Settings"},
    {ShowCalcDialogQt,             "Calculation Settings"},
    {ShowDisplayDialogQt,          "Display Settings"},
    {ShowCommandLineDialogQt,      "Enter Command Line"},
    {ShowAboutDialogQt,            "About Astrolog"} };
  int i, cdlg = (int)(sizeof(rgdlg) / sizeof(DLGTEST));

  Group("Dialogs");
  for (i = 0; i < cdlg; i++) {
    QString str = StrOpenDialogQt(rgdlg[i].pfn);
    Check(!str.isEmpty(), "%s: no dialog appeared", rgdlg[i].szTitle);
    if (!str.isEmpty())
      Check(str == rgdlg[i].szTitle, "expected title \"%s\", got \"%s\"",
        rgdlg[i].szTitle, str.toLocal8Bit().constData());
  }
  printf("  %d dialogs opened and closed\n", cdlg);
}


/*
******************************************************************************
** Context menus.
******************************************************************************
*/

// Each entry names the menu bar item it acts through, by label. When that
// lookup fails the entry is built disabled on purpose, so it shows up in
// the UI rather than silently vanishing -- which makes "no disabled
// entries" exactly the assertion that catches a drifted label.
//
// Deliberately says nothing about *what* is in these menus: not the
// labels, not the order, not how many entries. Only that whatever is
// there resolves. Add, remove or reorder entries freely and this keeps
// passing; it fails only when an entry points at a menu bar item that
// isn't there, which is the mistake worth catching. Same reasoning for
// the hotkey test below.

static void TestContextMenusQt()
{
  CONST char *szName;
  int i, j, cmenu = CCtxTestQt(), citem = 0;

  Group("Context menus");
  for (i = 0; i < cmenu; i++) {
    QMenu *pmenu = PmenuCtxTestQt(i, &szName);
    QList<QAction *> rgpa = pmenu->actions();
    Check(rgpa.size() > 0, "%s: built empty", szName);
    for (j = 0; j < rgpa.size(); j++) {
      if (rgpa[j]->isSeparator())
        continue;
      citem++;
      Check(rgpa[j]->isEnabled(),
        "%s: entry \"%s\" did not resolve to a menu bar item",
        szName, rgpa[j]->text().toLocal8Bit().constData());
    }
    delete pmenu;
  }
  printf("  %d menus, %d entries, all resolved\n", cmenu, citem);
}


/*
******************************************************************************
** Keyboard shortcuts.
******************************************************************************
*/

static void TestHotkeysQt()
{
  CONST char *szKey, *szAction;
  QStringList rgstrSeen;
  int i, chotkey = CHotkeyTestQt();

  Group("Hotkeys");
  for (i = 0; i < chotkey; i++) {
    HotkeyTestQt(i, &szKey, &szAction);
    QAction *pa = PaFindActionTestQt(szAction);
    Check(pa != NULL, "%s: target \"%s\" not found in the menu bar",
      szKey, szAction);
    if (pa == NULL)
      continue;
    // Two commands on one key means Qt calls it ambiguous and fires
    // neither, so a duplicate is worse than a missing binding.
    Check(!rgstrSeen.contains(szKey), "%s: bound more than once", szKey);
    rgstrSeen.append(szKey);
    QKeySequence ks = QKeySequence(QString(szKey));
    Check(!ks.isEmpty(), "%s: not a key sequence Qt understands", szKey);
    Check(pa->shortcuts().contains(ks),
      "%s: never made it onto \"%s\"", szKey, szAction);
  }
  printf("  %d shortcuts, all bound and unique\n", chotkey);
}


/*
******************************************************************************
** Chart rendering.
******************************************************************************
*/

// Draw each chart type and confirm something actually came out. Cheap,
// but it is the check that would have caught the blank chart bug the
// gi.nMode handling once had, and it needs no screenshotting: the chart
// is already a QImage in memory.

static void TestChartRenderQt()
{
  CONST int rgnMode[] = { gWheel, gHouse, gGrid, gAspect, gMidpoint,
    gHorizon, gOrbit, gSector, gCalendar, gDisposit, gEsoteric,
    gAstroGraph, gEphemeris, gArabic, gRising, gLocal, gMoons, gExo,
    gTraTraGra, gTraNatGra, gSphere, gWorldMap, gGlobe, gPolar,
    gTelescope, gBiorhythm };
  CONST char *rgszMode[] = { "Wheel", "House", "Grid", "Aspect", "Midpoint",
    "Horizon", "Orbit", "Sector", "Calendar", "Influence", "Esoteric",
    "AstroGraph", "Ephemeris", "Arabic", "Rising", "Local", "Moons", "Exo",
    "TraTraGra", "TraNatGra", "Sphere", "WorldMap", "Globe", "Polar",
    "Telescope", "Biorhythm" };
  int i, x, y, cmode = (int)(sizeof(rgnMode) / sizeof(int)), nSav = gi.nMode;
  // Named before drawing, flushed, so a crash says which one.
  long cpix;

  Group("Chart rendering");
  for (i = 0; i < cmode; i++) {
    if (getenv("ASTROLOG_QT_TEST_VERBOSE") != NULL) {
      printf("    rendering: %s\n", rgszMode[i]); fflush(stdout);
    }
    SetChartModeQt(rgnMode[i]);
    Check(gi.nMode == rgnMode[i], "%s: gi.nMode did not take", rgszMode[i]);
    Check(gi.qim != NULL, "%s: no image was rendered", rgszMode[i]);
    if (gi.qim == NULL)
      continue;
    Check(gi.qim->width() == gs.xWin && gi.qim->height() == gs.yWin,
      "%s: image is %dx%d, chart size is %dx%d", rgszMode[i],
      gi.qim->width(), gi.qim->height(), gs.xWin, gs.yWin);
    // A chart that drew nothing leaves the fill colour everywhere.
    cpix = 0;
    for (y = 0; y < gi.qim->height(); y += 4)
      for (x = 0; x < gi.qim->width(); x += 4)
        if (gi.qim->pixel(x, y) != gi.qim->pixel(0, 0))
          cpix++;
    Check(cpix > 100, "%s: rendered blank (%ld pixels differ from the "
      "background)", rgszMode[i], cpix);
  }
  SetChartModeQt(nSav);

  // The loop above drives SetChartModeQt() directly, which leaves
  // us.fGraphics alone -- so it only ever exercised whichever mode the
  // suite happened to be in. A user picks these off the Chart menu, and
  // the two chart types with no case in DrawChartX() (Aspect List and
  // Arabic Parts) render an empty window unless the menu action turns
  // graphics off first, the way Windows does. Fire the actions themselves,
  // with graphics deliberately on, so that path is covered.
  CONST char *rgszChart[] = { "Standard Radi&x", "House &Wheel",
    "Aspect Midpoint &Grid", "&Aspect List", "&Midpoint List",
    "Local Hori&zon", "Solar System &Orbit", "Ga&uquelin Sectors",
    "&Calendar", "Inf&luence", "Esoter&ic", "Astrocartograp&hy",
    "&Ephemeris", "Ara&bic Parts", "Risi&ng and Setting",
    "Nea&rest Cities" };
  int cchart = (int)(sizeof(rgszChart) / sizeof(char *));
  flag fSav = us.fGraphics;

  for (i = 0; i < cchart; i++) {
    QAction *pa = PaFindActionTestQt(rgszChart[i]);
    Check(pa != NULL, "%s: not on the Chart menu", rgszChart[i]);
    if (pa == NULL)
      continue;
    us.fGraphics = fTrue;
    pa->trigger();
    // Windows drops to text for exactly these two and leaves every other
    // chart type in whatever mode it was already in (wdriver.cpp
    // cmdChartAspect and cmdChartArabic are the only chart-type cases that
    // assign us.fGraphics). Anything else here is a divergence.
    flag fWantText = (NCompareSz(rgszChart[i], "&Aspect List") == 0 ||
      NCompareSz(rgszChart[i], "Ara&bic Parts") == 0);
    Check(fWantText ? !us.fGraphics : us.fGraphics != 0,
      "%s: left us.fGraphics %s; Windows leaves it %s", rgszChart[i],
      us.fGraphics ? "on" : "off", fWantText ? "off" : "on");
  }
  us.fGraphics = fSav;
  SetChartModeQt(nSav);
  printf("  %d chart types rendered, %d fired from the Chart menu\n",
    cmode, cchart);
}


/*
******************************************************************************
** Firing every menu item.
******************************************************************************
*/

// Trigger every menu item that doesn't open a dialog and check the app
// survives and still draws. Deliberately does not reset state between
// items, so this walks through a long chain of odd setting combinations
// -- which is the point, since that's where the crashes have been.
//
// Skipped: anything whose label ends in "..." (those open a dialog and
// would block; the dialog test covers them), and Quit.

static void TestAllMenuActionsQt()
{
  QList<QAction *> rgpa;
  int i, cfired = 0, cmodal = 0, ctext = 0, x, y;
  long cpix;

  Group("Firing every menu item");
  AllActionsTestQt(&rgpa);
  for (i = 0; i < rgpa.size(); i++) {
    QString str = rgpa[i]->text();
    // Quit would end the run. The doc and website items hand a file or a
    // URL to the desktop, which isn't this suite's business to trigger.
    if (str.contains("Quit") || str.contains("Exit") ||
      str.startsWith("Open ") || str.contains("Website"))
      continue;
    // Anything that puts up a modal dialog blocks here forever, since
    // nothing is driving the event loop to dismiss it. Rather than guess
    // which items those are from their labels -- the first attempt did
    // guess, guessed wrong, and hung the run -- queue a shot that closes
    // whatever modal window appears and count it.
    s_strModal = QString();
    QTimer::singleShot(120, []() {
      QWidget *pw = QApplication::activeModalWidget();
      if (pw != NULL) {
        s_strModal = pw->windowTitle();
        pw->close();
      }
    });
    // Name each item before firing it, flushed, so that when one takes
    // the process down the log says which. Set ASTROLOG_QT_TEST_VERBOSE
    // to see it; a clean run doesn't need the noise.
    if (getenv("ASTROLOG_QT_TEST_VERBOSE") != NULL) {
      printf("    firing: %s\n", str.toLocal8Bit().constData());
      fflush(stdout);
    }
    rgpa[i]->trigger();
    if (!s_strModal.isEmpty())
      cmodal++;
    cfired++;
    // Some items switch to text mode, where gi.qim isn't redrawn at all,
    // so the image checks below would be reading a stale buffer. Put
    // graphics back before carrying on -- and not only for correctness:
    // left off, every later item takes the much slower text redraw path
    // and the whole sweep stops finishing in reasonable time.
    if (!us.fGraphics) {
      ctext++;
      // Back to graphics, and to a chart mode that has a graphical form:
      // the items that switch to text mode also select a text-only mode
      // (the Help menu listings, gCredit and friends), and asking
      // DrawChartX() to draw one of those as graphics quite reasonably
      // produces nothing. Restoring only the flag left the next item
      // looking at a blank buffer and being blamed for it.
      us.fGraphics = fTrue;
      SetChartModeQt(gWheel);
      continue;
    }
    // Clear Screen blanks the chart on purpose, so it can't be held to the
    // "still drew something" check below, and leaving the buffer blank
    // would get the next item blamed for it. Draw the chart back.
    if (str == "&Clear Screen") {
      RedrawQt();
      continue;
    }
    // Getting here at all is most of the test: a crash takes the process
    // with it and the run reports nothing further.
    Check(gi.qim != NULL, "after \"%s\": no image",
      str.toLocal8Bit().constData());
    if (gi.qim == NULL)
      continue;
    Check(gi.qim->width() > 0 && gi.qim->height() > 0,
      "after \"%s\": image is %dx%d", str.toLocal8Bit().constData(),
      gi.qim->width(), gi.qim->height());
    // Sampled every four pixels rather than every eight. A chart can be
    // legitimately sparse without being blank -- a telescope chart zoomed
    // out to two degrees is a border, its axis labels and a single star --
    // and on the coarser grid that came to exactly 20 differing samples,
    // failing a "more than 20" check for drawing exactly what it should.
    cpix = 0;
    for (y = 0; y < gi.qim->height(); y += 4)
      for (x = 0; x < gi.qim->width(); x += 4)
        if (gi.qim->pixel(x, y) != gi.qim->pixel(0, 0))
          cpix++;
    Check(cpix > 20, "after \"%s\": chart went blank",
      str.toLocal8Bit().constData());
  }
  printf("  %d menu items fired (%d opened a dialog, %d switched to text)\n",
    cfired, cmodal, ctext);
}


/*
******************************************************************************
** Parity with the Windows menu bar.
******************************************************************************
*/

// Every item in Windows' main menu resource (astrolog.rc, the "menu MENU"
// block), generated from that file rather than typed, checked against the
// live Qt menu bar. This is the measurement behind any claim that the two
// builds have the same menus -- previously that claim rested on grepping
// the source for label strings, which counts text in comments and misses
// anything built at runtime.
//
// Matching ignores "&" placement: the two builds do not always put the
// mnemonic on the same letter and that is not worth failing over. What it
// does check is that the item exists and sits under the same top-level
// menu, since putting something in the wrong menu is a real parity bug
// and has happened in this port before.
//
// The 96 macro slots are excluded: their labels are generated at runtime,
// and TestHotkeysQt already covers that they exist and are bound.

typedef struct {
  CONST char *szTop;     // Top level menu it lives under on Windows.
  CONST char *szLabel;   // The item's Windows label.
  flag fSkip;            // Deliberately not ported; see the plan.
} PARITYITEM;

static CONST PARITYITEM rgparityQt[] = {
  {"File",        "&Open Chart...",                              fFalse},
  {"File",        "Open Chart #&2...",                           fFalse},
  {"File",        "&Save Chart Info...",                         fFalse},
  {"File",        "Save Chart &Positions...",                    fFalse},
  {"File",        "Save Program Settin&gs...",                   fFalse},
  {"File",        "Open Charts in &Folder...",                   fFalse},
  {"File",        "Save Chart &List...",                         fFalse},
  {"File",        "Save Chart &Exchange...",                     fFalse},
  {"File",        "Save Chart &Quick*Chart...",                  fFalse},
  {"File",        "Save Chart i&Calendar...",                    fFalse},
  {"File",        "Export Chart &Text Output...",                fFalse},
  {"File",        "Export Chart &Bitmap...",                     fFalse},
  {"File",        "Export Chart &Metafile...",                   fFalse},
  {"File",        "Export Chart &PostScript...",                 fFalse},
  {"File",        "Export Chart &SVG...",                        fFalse},
  {"File",        "Export Chart &Wireframe...",                  fFalse},
  {"File",        "&Tile Bitmap",                                fTrue},
  {"File",        "&Center Bitmap",                              fTrue},
  {"File",        "&Stretch Bitmap",                             fTrue},
  {"File",        "&Fit Bitmap",                                 fTrue},
  {"File",        "Fi&ll Bitmap",                                fTrue},
  {"File",        "Open Chart &Background...",                   fFalse},
  {"File",        "Open &World Map...",                          fFalse},
  {"File",        "&File Settings...",                           fFalse},
  {"File",        "P&rint...",                                   fFalse},
  {"File",        "Pr&int Setup...",                             fTrue},
  {"File",        "E&xit",                                       fFalse},
  {"Edit",        "Enter Command &Line...",                      fFalse},
  {"Edit",        "Copy Chart &Text Output",                     fFalse},
  {"Edit",        "Copy Chart &Bitmap",                          fFalse},
  {"Edit",        "Copy Chart &Metafile",                        fFalse},
  {"Edit",        "Copy Chart &PostScript",                      fFalse},
  {"Edit",        "Copy Chart &SVG",                             fFalse},
  {"Edit",        "Copy Chart &Wireframe",                       fFalse},
  {"Edit",        "&Paste",                                      fFalse},
  {"View",        "Show &Graphics",                              fFalse},
  {"View",        "&Buffer Redraws",                             fTrue},
  {"View",        "&Redraw Screen",                              fFalse},
  {"View",        "&Clear Screen",                               fFalse},
  {"View",        "&Hourglass on Redraw",                        fFalse},
  {"View",        "Ch&art Resizes Window",                       fFalse},
  {"View",        "&Window Resizes Chart",                       fFalse},
  {"View",        "Si&ze Chart to Window",                       fFalse},
  {"View",        "&Size Window to Chart",                       fFalse},
  {"View",        "Size Window &Full Screen",                    fFalse},
  {"View",        "Scroll Page &Up",                             fFalse},
  {"View",        "Scroll Page &Down",                           fFalse},
  {"View",        "Scroll &to Beginning",                        fFalse},
  {"View",        "Scroll to &End",                              fFalse},
  {"View",        "&Colored Text",                               fFalse},
  {"View",        "&Set Colors...",                              fFalse},
  {"View",        "Show &Interpretations",                       fFalse},
  {"View",        "Print &Nearest Second",                       fFalse},
  {"View",        "&Parallel Aspects",                           fFalse},
  {"View",        "&Applying Aspects",                           fFalse},
  {"Info",        "Set Chart &Info...",                          fFalse},
  {"Info",        "Chart for &Now",                              fFalse},
  {"Info",        "D&efault Chart Info...",                      fFalse},
  {"Info",        "Set Chart #&2 Info...",                       fFalse},
  {"Info",        "Charts #&3 Through #6...",                    fFalse},
  {"Info",        "&Chart List...",                              fFalse},
  {"Info",        "&Previous Chart",                             fFalse},
  {"Info",        "&Next Chart",                                 fFalse},
  {"Info",        "&First Chart",                                fFalse},
  {"Info",        "&Last Chart",                                 fFalse},
  {"Info",        "Swap Chart #&1 and #2",                       fFalse},
  {"Info",        "No &Relationship Chart",                      fFalse},
  {"Info",        "Com&parison Chart",                           fFalse},
  {"Info",        "&Synastry Chart",                             fFalse},
  {"Info",        "&Composite Chart",                            fFalse},
  {"Info",        "Time Space &Midpoint Chart",                  fFalse},
  {"Info",        "Date &Difference Chart",                      fFalse},
  {"Info",        "&Biorhythm Chart",                            fFalse},
  {"Info",        "&Transit and Natal",                          fFalse},
  {"Info",        "&Progressed and Natal",                       fFalse},
  {"Setting",     "&Sidereal Zodiac",                            fFalse},
  {"Setting",     "He&liocentric",                               fFalse},
  {"Setting",     "&Placidus",                                   fFalse},
  {"Setting",     "&Koch",                                       fFalse},
  {"Setting",     "&Campanus",                                   fFalse},
  {"Setting",     "&Regiomontanus",                              fFalse},
  {"Setting",     "&Topocentric",                                fFalse},
  {"Setting",     "Alca&bitius",                                 fFalse},
  {"Setting",     "Kr&usinski",                                  fFalse},
  {"Setting",     "A&.P.C.",                                     fFalse},
  {"Setting",     "Savard-&A",                                   fFalse},
  {"Setting",     "Porph&yry",                                   fFalse},
  {"Setting",     "Pullen (S.Rati&o)",                           fFalse},
  {"Setting",     "Pullen (S.&Delta)",                           fFalse},
  {"Setting",     "&Meridian",                                   fFalse},
  {"Setting",     "Morinu&s",                                    fFalse},
  {"Setting",     "Hori&zon",                                    fFalse},
  {"Setting",     "Carter& P.Equat.",                            fFalse},
  {"Setting",     "Suns&hine",                                   fFalse},
  {"Setting",     "Sr&ipati",                                    fFalse},
  {"Setting",     "&Equal",                                      fFalse},
  {"Setting",     "E&qual (MC)",                                 fFalse},
  {"Setting",     "&Whole",                                      fFalse},
  {"Setting",     "&Vedic",                                      fFalse},
  {"Setting",     "&Null",                                       fFalse},
  {"Setting",     "&Solar Chart",                                fFalse},
  {"Setting",     "&3D Houses",                                  fFalse},
  {"Setting",     "Show &Decans",                                fFalse},
  {"Setting",     "Show D&wads",                                 fFalse},
  {"Setting",     "&Flip Signs with Houses",                     fFalse},
  {"Setting",     "&Geodetic Houses",                            fFalse},
  {"Setting",     "&Indian Wheel Order",                         fFalse},
  {"Setting",     "Show &Navamsas",                              fFalse},
  {"Setting",     "&Aspect Settings...",                         fFalse},
  {"Setting",     "&Object Settings...",                         fFalse},
  {"Setting",     "More Ob&ject Settings...",                    fFalse},
  {"Setting",     "&Restrictions...",                            fFalse},
  {"Setting",     "Star Restr&ictions...",                       fFalse},
  {"Setting",     "&Transit Restrictions...",                    fFalse},
  {"Setting",     "&Moons Chart",                                fFalse},
  {"Setting",     "&Exoplanets Chart",                           fFalse},
  {"Setting",     "Moon &Restrictions...",                       fFalse},
  {"Setting",     "Moon &Object Settings...",                    fFalse},
  {"Setting",     "&Include Moons",                              fFalse},
  {"Setting",     "Include &Body Centers (COB)",                 fFalse},
  {"Setting",     "Object &Customization...",                    fFalse},
  {"Setting",     "&Star Customization...",                      fFalse},
  {"Setting",     "Include &Minors",                             fFalse},
  {"Setting",     "Include &Cusps",                              fFalse},
  {"Setting",     "Include &Uranians",                           fFalse},
  {"Setting",     "Include D&warfs",                             fFalse},
  {"Setting",     "Include &Fixed Stars",                        fFalse},
  {"Setting",     "Calculation Settin&gs...",                    fFalse},
  {"Setting",     "&Display Settings...",                        fFalse},
  {"Chart",       "Standard Radi&x",                             fFalse},
  {"Chart",       "House &Wheel",                                fFalse},
  {"Chart",       "Aspect Midpoint &Grid",                       fFalse},
  {"Chart",       "&Aspect List",                                fFalse},
  {"Chart",       "&Midpoint List",                              fFalse},
  {"Chart",       "Local Hori&zon",                              fFalse},
  {"Chart",       "Solar System &Orbit",                         fFalse},
  {"Chart",       "Ga&uquelin Sectors",                          fFalse},
  {"Chart",       "&Calendar",                                   fFalse},
  {"Chart",       "Inf&luence",                                  fFalse},
  {"Chart",       "Esoter&ic",                                   fFalse},
  {"Chart",       "Astrocartograp&hy",                           fFalse},
  {"Chart",       "&Ephemeris",                                  fFalse},
  {"Chart",       "Ara&bic Parts",                               fFalse},
  {"Chart",       "Risi&ng and Setting",                         fFalse},
  {"Chart",       "Nea&rest Cities",                             fFalse},
  {"Chart",       "&Transits...",                                fFalse},
  {"Chart",       "&Progressions...",                            fFalse},
  {"Chart",       "Chart &Settings...",                          fFalse},
  {"Graphics",    "Draw Chart Sp&here",                          fFalse},
  {"Graphics",    "Draw &World Map",                             fFalse},
  {"Graphics",    "Draw &Globe",                                 fFalse},
  {"Graphics",    "Draw &Polar Globe",                           fFalse},
  {"Graphics",    "Draw &Telescope",                             fFalse},
  {"Graphics",    "&Reverse Background",                         fFalse},
  {"Graphics",    "&Monochrome",                                 fFalse},
  {"Graphics",    "S&quare Screen",                              fFalse},
  {"Graphics",    "&Small",                                      fFalse},
  {"Graphics",    "&Medium",                                     fFalse},
  {"Graphics",    "&Large",                                      fFalse},
  {"Graphics",    "&Huge",                                       fFalse},
  {"Graphics",    "&Decrease",                                   fFalse},
  {"Graphics",    "&Increase",                                   fFalse},
  {"Graphics",    "D&ecrease Text",                              fFalse},
  {"Graphics",    "I&ncrease Text",                              fFalse},
  {"Graphics",    "Show &Border",                                fFalse},
  {"Graphics",    "Show Chart &Info",                            fFalse},
  {"Graphics",    "Show Info &Sidebar",                          fFalse},
  {"Graphics",    "&Thicker Lines",                              fFalse},
  {"Graphics",    "&Antialias Lines",                            fFalse},
  {"Graphics",    "Show Glyph &Labels",                          fFalse},
  {"Graphics",    "Show &Glyphs on Aspect Lines",                fFalse},
  {"Graphics",    "Show &Constellations",                        fFalse},
  {"Graphics",    "Show Full &Star List",                        fFalse},
  {"Graphics",    "Show E&xoplanets",                            fFalse},
  {"Graphics",    "Show Constellation &Lines",                   fFalse},
  {"Graphics",    "Show &House Details",                         fFalse},
  {"Graphics",    "Show &Equator",                               fFalse},
  {"Graphics",    "Show C&ities",                                fFalse},
  {"Graphics",    "Use Detailed World &Map",                     fFalse},
  {"Graphics",    "Use Ecliptic &Axis",                          fFalse},
  {"Graphics",    "Rotate &West",                                fFalse},
  {"Graphics",    "Rotate &East",                                fFalse},
  {"Graphics",    "Tilt &North",                                 fFalse},
  {"Graphics",    "Tilt &South",                                 fFalse},
  {"Graphics",    "Set Tilt to &Zero",                           fFalse},
  {"Graphics",    "Zoom &Out",                                   fFalse},
  {"Graphics",    "Zoom &In",                                    fFalse},
  {"Graphics",    "Show &Indian Wheels",                         fFalse},
  {"Graphics",    "Draw &South Indian",                          fFalse},
  {"Graphics",    "Draw &North Indian",                          fFalse},
  {"Graphics",    "Draw &East Indian",                           fFalse},
  {"Graphics",    "Modify &Display",                             fFalse},
  {"Graphics",    "Modif&y Chart",                               fFalse},
  {"Graphics",    "Blac&k",                                      fFalse},
  {"Graphics",    "&White",                                      fFalse},
  {"Graphics",    "&Red",                                        fFalse},
  {"Graphics",    "&Green",                                      fFalse},
  {"Graphics",    "&Blue",                                       fFalse},
  {"Graphics",    "&Yellow",                                     fFalse},
  {"Graphics",    "&Magenta",                                    fFalse},
  {"Graphics",    "&Cyan",                                       fFalse},
  {"Graphics",    "Gr&ay",                                       fFalse},
  {"Graphics",    "&Lt. Gray",                                   fFalse},
  {"Graphics",    "Maroo&n",                                     fFalse},
  {"Graphics",    "Dk. Gr&een",                                  fFalse},
  {"Graphics",    "Dk. Bl&ue",                                   fFalse},
  {"Graphics",    "Mai&ze",                                      fFalse},
  {"Graphics",    "&Purple",                                     fFalse},
  {"Graphics",    "&Dk. Cyan",                                   fFalse},
  {"Graphics",    "&Graphics Settings...",                       fFalse},
  {"Animate",     "Do &Animation",                               fFalse},
  {"Animate",     "Update to &Now",                              fFalse},
  {"Animate",     "&Seconds",                                    fFalse},
  {"Animate",     "&Minutes",                                    fFalse},
  {"Animate",     "&Hours",                                      fFalse},
  {"Animate",     "&Days",                                       fFalse},
  {"Animate",     "M&onths",                                     fFalse},
  {"Animate",     "&Years",                                      fFalse},
  {"Animate",     "&Decades",                                    fFalse},
  {"Animate",     "&Centuries",                                  fFalse},
  {"Animate",     "Mi&llennia",                                  fFalse},
  {"Animate",     "1/&10th Seconds",                             fFalse},
  {"Animate",     "1/1&00th Seconds",                            fFalse},
  {"Animate",     "1&/1000th Seconds",                           fFalse},
  {"Animate",     "&One Unit",                                   fFalse},
  {"Animate",     "&Two Units",                                  fFalse},
  {"Animate",     "T&hree Units",                                fFalse},
  {"Animate",     "&Four Units",                                 fFalse},
  {"Animate",     "Fi&ve Units",                                 fFalse},
  {"Animate",     "Si&x Units",                                  fFalse},
  {"Animate",     "&Seven Units",                                fFalse},
  {"Animate",     "&Eight Units",                                fFalse},
  {"Animate",     "&Nine Units",                                 fFalse},
  {"Animate",     "&Reverse Direction",                          fFalse},
  {"Animate",     "&Pause Animation",                            fFalse},
  {"Animate",     "&Timed Exposure",                             fFalse},
  {"Animate",     "Step &Forward",                               fFalse},
  {"Animate",     "Step &Backward",                              fFalse},
  {"Animate",     "&Store Chart Info",                           fFalse},
  {"Animate",     "Re&call Chart Info",                          fFalse},
  {"Help",        "Open &Documentation",                         fFalse},
  {"Help",        "&Open Documentation",                         fFalse},
  {"Help",        "Open &Changes",                               fFalse},
  {"Help",        "Open &License",                               fFalse},
  {"Help",        "Open &Website",                               fFalse},
  {"Help",        "Open Website &Mirror",                        fFalse},
  {"Help",        "Open &Default Settings",                      fFalse},
  {"Help",        "Open &Atlas",                                 fFalse},
  {"Help",        "Open &Time Zone Changes",                     fFalse},
  {"Help",        "Open &Star List",                             fFalse},
  {"Help",        "Open &Orbital Elements",                      fFalse},
  {"Help",        "Open &Exoplanet List",                        fFalse},
  {"Help",        "List Si&gns",                                 fFalse},
  {"Help",        "List &Objects",                               fFalse},
  {"Help",        "List Aspec&ts",                               fFalse},
  {"Help",        "List &Constellations",                        fFalse},
  {"Help",        "List &Planet Info",                           fFalse},
  {"Help",        "List &Rays",                                  fFalse},
  {"Help",        "List &General Meanings",                      fFalse},
  {"Help",        "List S&witches",                              fFalse},
  {"Help",        "List O&bscure Switches",                      fFalse},
  {"Help",        "List &Keystrokes",                            fFalse},
  {"Help",        "List Cr&edits",                               fFalse},
  {"Help",        "Create Program Group (&User)",                fTrue},
  {"Help",        "Create Program Group (&All)",                 fTrue},
  {"Help",        "Create &Desktop Icon",                        fTrue},
  {"Help",        "Install File &Extensions",                    fTrue},
  {"Help",        "Uninstall File E&xtensions",                  fTrue},
  {"Help",        "&About Astrolog...",                          fFalse} };

#define cparityQt (int)(sizeof(rgparityQt) / sizeof(PARITYITEM))

static void TestMenuParityQt()
{
  CONST char *szTop;
  int i, cfound = 0, cskip = 0, cwrong = 0;

  Group("Menu parity with Windows");
  for (i = 0; i < cparityQt; i++) {
    QAction *pa = PaFindLooseTestQt(rgparityQt[i].szLabel, &szTop);
    if (rgparityQt[i].fSkip) {
      cskip++;
      continue;
    }
    Check(pa != NULL, "%s > \"%s\" is missing from the Qt menu bar",
      rgparityQt[i].szTop, rgparityQt[i].szLabel);
    if (pa == NULL)
      continue;
    cfound++;
    if (szTop != NULL && strcmp(szTop, rgparityQt[i].szTop) != 0) {
      cwrong++;
      Check(fFalse, "\"%s\" is under %s here but under %s on Windows",
        rgparityQt[i].szLabel, szTop, rgparityQt[i].szTop);
    }
  }
  printf("  %d of %d Windows menu items present (%d skipped on purpose)\n",
    cfound, cparityQt - cskip, cskip);
}


/*
******************************************************************************
** Bad input.
******************************************************************************
*/

// A macro is just a stored command line, and one written on a different
// machine will name files that aren't here. On every non-Windows build
// PrintError() ended in Terminate(), so a macro pointing at a missing file
// took the whole program down instead of complaining -- which is what
// hitting F1 with someone else's macro set did. Windows shows a message
// box and carries on; the Qt build now does too.
//
// If that regresses, these calls never return and the suite dies partway
// through with no summary line, which is loud enough to spot.

static void TestBadInputQt()
{
  flag fSav = FNoPopupQt();
  char sz[cchSzMax];

  Group("Bad input");
  SetNoPopupQt(fTrue);          // no message boxes during an automated run

  Check(FileOpen("no-such-file-here.as", 0, NULL) == NULL,
    "FileOpen() found a file that isn't there");
  Check(fTrue, "FileOpen() on a missing file returned");

  // The macro path proper: a command line naming a file that isn't here.
  sprintf(sz, "-i no-such-file-here.as");
  FProcessCommandLine(sz);
  Check(fTrue, "FProcessCommandLine() returned after a missing -i file");

  // And an outright bad switch, the other way a stale macro goes wrong.
  sprintf(sz, "-ZZzzz");
  FProcessCommandLine(sz);
  Check(fTrue, "FProcessCommandLine() returned after an unknown switch");

  PrintError("Test error; the suite expects to keep running past this.");
  Check(fTrue, "PrintError() returned instead of terminating");

  SetNoPopupQt(fSav);
  printf("  survived missing files, a bad switch and PrintError()\n");
}


/*
******************************************************************************
** Text chart capture, for comparing against the Windows build.
******************************************************************************
*/

// Render the text charts to PNGs so they can be put beside the same
// charts captured from the real Windows build under Wine -- see
// QT_COMPARING_WITH_WINDOWS.md. This side is done in code rather than by
// driving the UI because "v" is a *toggle*: pressing it leaves the two
// builds in whatever state they started in, which is not necessarily the
// same one, and a graphics chart then gets compared against a text chart.
// Setting us.fGraphics directly is deterministic, needs no display, no
// window manager and no keystrokes, and cannot beep.
//
//   make -f Makefile.qt.test
//   QTTEXTDIR=out/qt ./run-qt-tests.sh
//
// Chart data is pinned here so both sides show the same chart; change it
// in both places or the comparison is only about layout, not values.

static void TextChartCaptureQt(CONST char *szDir)
{
  CONST char *rgszAct[] = { "Standard Radi&x", "House &Wheel",
    "Aspect Midpoint &Grid", "&Calendar", "Inf&luence", "&Ephemeris",
    "&Aspect List", "&Midpoint List" };
  CONST char *rgszFile[] = { "radix", "wheel", "grid", "calendar",
    "influence", "ephemeris", "aspectlist", "midpointlist" };
  int i, cchart = (int)(sizeof(rgszAct) / sizeof(char *));

  // The chart tools/text-chart-capture.sh leaves the Windows build on:
  // Nov 19 1971 11:01am, ST Zone 8W, 122:19W 47:36N, no name or location
  // string (a name makes the header wrap to a second line, charts1.cpp:91).
  ciCore.mon = 11; ciCore.day = 19; ciCore.yea = 1971;
  ciCore.tim = 11.0 + 1.0/60.0; ciCore.dst = 0.0; ciCore.zon = 8.0;
  ciCore.lon = 122.0 + 19.0/60.0; ciCore.lat = 47.0 + 36.0/60.0;
  ciCore.nam[0] = chNull; ciCore.loc[0] = chNull;
  ciMain = ciCore;
  CastChart(1);

  gs.xWin = 1000; gs.yWin = 620;
  if (gi.qcanvas != NULL)
    gi.qcanvas->resize(gs.xWin, gs.yWin);

  // QImage::save() just returns false into a directory that isn't there,
  // so without this the run reports every chart as captured and writes
  // nothing at all.
  if (!QDir().mkpath(QString(szDir))) {
    printf("cannot create %s\n", szDir);
    return;
  }
  printf("capturing %d text charts to %s\n", cchart, szDir);
  for (i = 0; i < cchart; i++) {
    QAction *pa = PaFindActionTestQt(rgszAct[i]);
    if (pa == NULL) {
      printf("  %-24s NOT FOUND\n", rgszAct[i]);
      continue;
    }
    pa->trigger();
    us.fGraphics = fFalse;      // deterministic, unlike toggling "v"
    RedrawQt();
    if (gi.qim == NULL) {
      printf("  %-24s nothing rendered\n", rgszAct[i]);
      continue;
    }
    QString str = QString("%1/%2.png").arg(szDir).arg(rgszFile[i]);
    printf("  %s%s\n", rgszFile[i],
      gi.qim->save(str) ? "" : "   FAILED TO WRITE");
    fflush(stdout);
  }
}


/*
******************************************************************************
** Entry point.
******************************************************************************
*/

int NRunQtTestsQt()
{
  if (getenv("QTTEXTDIR") != NULL) {
    TextChartCaptureQt(getenv("QTTEXTDIR"));
    return 0;
  }
  printf("Astrolog Qt test suite\n");
  TestDialogsQt();
  TestContextMenusQt();
  TestHotkeysQt();
  TestChartRenderQt();
  TestAllMenuActionsQt();
  TestMenuParityQt();
  TestBadInputQt();
  printf("\n%s: %d passed, %d failed\n",
    s_cFail == 0 ? "PASS" : "FAIL", s_cPass, s_cFail);
  return s_cFail > 0;
}

#endif // QTTEST

/* qttest.cpp */
