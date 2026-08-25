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
    {ShowChartInfoDialogQt,        "Chart Info"},
    {ShowChartsAllDialogQt,        "Charts #3 through #6"},
    {ShowChartListDialogQt,        "Chart List"},
    {ShowColorDialogQt,            "Colors"},
    {ShowObjectDialogQt,           "Objects"},
    {ShowObject2DialogQt,          "More Object Settings"},
    {ShowMoonObjectDialogQt,       "Moon Object Settings"},
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
  printf("  %d chart types rendered\n", cmode);
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
    // Getting here at all is most of the test: a crash takes the process
    // with it and the run reports nothing further.
    Check(gi.qim != NULL, "after \"%s\": no image",
      str.toLocal8Bit().constData());
    if (gi.qim == NULL)
      continue;
    Check(gi.qim->width() > 0 && gi.qim->height() > 0,
      "after \"%s\": image is %dx%d", str.toLocal8Bit().constData(),
      gi.qim->width(), gi.qim->height());
    cpix = 0;
    for (y = 0; y < gi.qim->height(); y += 8)
      for (x = 0; x < gi.qim->width(); x += 8)
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
** Entry point.
******************************************************************************
*/

int NRunQtTestsQt()
{
  printf("Astrolog Qt test suite\n");
  TestDialogsQt();
  TestContextMenusQt();
  TestHotkeysQt();
  TestChartRenderQt();
  TestAllMenuActionsQt();
  printf("\n%s: %d passed, %d failed\n",
    s_cFail == 0 ? "PASS" : "FAIL", s_cPass, s_cFail);
  return s_cFail > 0;
}

#endif // QTTEST

/* qttest.cpp */
