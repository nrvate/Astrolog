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
// QAction moved to QtGui in Qt6; see the same guard in qtdriver.cpp.
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QtGui/QAction>
#else
#include <QtWidgets/QAction>
#endif
#include <QtWidgets/QDialog>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QPushButton>
#include <QtCore/QTimer>
#include <QtCore/QStringList>
#include <QtCore/QSet>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QElapsedTimer>
#include <QtGui/QImage>
#include <QtGui/QIcon>
#include <QtCore/QTemporaryDir>
#include <QtGui/QKeyEvent>
#include <stdarg.h>
#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include "astrolog.h"
#include "extern.h"
#include "qtdriver.h"

#ifdef SWISS
// The oracle calls the ephemeris library directly, so this file needs the
// Swiss headers -- and the same "ret" dance calc.cpp does, since astrolog.h
// makes "ret" a macro for cp0.dir and the Swiss headers use that name.
#undef ret
#include "swephexp.h"
#define ret cp0.dir
#endif

#ifdef QTTEST

// Hooks into qtdriver.cpp, where the menu tables are file static.
extern void FormatSz P((CONST char *, char *, int));
extern int CCtxTestQt();
extern QMenu *PmenuCtxTestQt(int, CONST char **);
extern int CHotkeyTestQt();
extern void HotkeyTestQt(int, CONST char **, CONST char **);
extern QAction *PaFindActionTestQt(CONST char *);
extern void AllActionsTestQt(QList<QAction *> *);
extern QAction *PaFindLooseTestQt(CONST char *, CONST char **);
typedef struct _RcAccel { CONST char *szLabel, *szAccel; } RCACCEL;
extern CONST RCACCEL *PaccelTestQt();
extern int CaccelTestQt();
#define rgaccelQt PaccelTestQt()
#define caccelQt CaccelTestQt()

// Which ephemeris this run is expected to have, from ASTROLOG_QT_EPHEM:
//
//   full      (the default) the whole Swiss set, which is what
//             "-i nrvate.as" reaches through /swe
//   minimal   the "ephem/" directory this repository ships, which is what
//             "-Yi1 ephem" reaches and the only thing CI can ever have
//
// Declared, not detected, and that is the whole design. The mode says
// which ephemeris the run is supposed to have and the suite then checks
// reality against that claim; a suite that surveys what is present and
// adjusts to it can never fail, which is the vacuous-harness failure this
// project has already paid for three times. Default "full" for the same
// reason: the maintainer's own run must be unchanged, and a flag forgotten
// in CI has to fail loudly rather than quietly test half as much.
//
// 19 is measured, not guessed -- "-Yi1 ephem" resolves 19 of the 39 rows
// in rgObjSel[], the other 20 needing files only /swe has. See
// QT_CI_PLAN.md item 2.0.
#define cObjSelEphemMinimal 19

static flag FEphemMinimalQt()
{
  CONST char *szEphem = getenv("ASTROLOG_QT_EPHEM");
  return szEphem != NULL && FEqSzI(szEphem, "minimal");
}

static int s_cPass = 0, s_cFail = 0;
static CONST char *s_szGroup = "";
static QString s_strModal;

// Every timer here is racing a dialog that has to appear before it can be
// closed, and under AddressSanitizer the whole program runs roughly an
// order of magnitude slower -- so a delay that comfortably wins the race
// in a normal build loses it there, the dialog goes unclosed, and the run
// blocks in exec() with nothing driving the event loop. That looks exactly
// like a hang and wasted twenty minutes once. GCC and Clang both define
// this when -fsanitize=address is on, so it needs no flag of its own.
#ifdef __SANITIZE_ADDRESS__
#define nScaleTest 10
#else
#define nScaleTest 1
#endif

// Report the count from the group just finished before naming the next.
// Two builds of this suite disagreed by 20 assertions and there was no way
// to see where from the output, because only the total was ever printed.
static int s_cPassGroup = 0;

static void GroupEnd(void)
{
  if (s_szGroup[0] != chNull && getenv("ASTROLOG_QT_TEST_VERBOSE") != NULL)
    printf("  [%s: %d assertions]\n", s_szGroup, s_cPass - s_cPassGroup);
  s_cPassGroup = s_cPass;
}

static void Group(CONST char *sz)
{
  GroupEnd();
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
  vsnprintf(sz, sizeof(sz), szFmt, ap);   // A failing assertion can
  va_end(ap);              // carry an arbitrarily wide value; this is
                           // the same unbounded-format class the rest
                           // of this project keeps finding, and the
                           // check harness is no place for it.
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
  // Stoppable timers rather than singleShot, so nothing outlives this
  // call. A queued close that is still pending when the dialog has
  // already been dealt with goes on to close whatever modal window the
  // *next* test opens -- which is exactly what happened: 25 dialogs here
  // left 25 nets armed, and the first dialog any later test opened was
  // shut before it could be looked at, reporting itself as absent.
  QTimer tOpen, tNet;
  tOpen.setSingleShot(fTrue);
  tNet.setSingleShot(fTrue);
  QObject::connect(&tOpen, &QTimer::timeout, []() {
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
  QObject::connect(&tNet, &QTimer::timeout, []() {
    QWidget *pw = QApplication::activeModalWidget();
    if (pw != NULL)
      pw->close();
  });
  tOpen.start(50 * nScaleTest);
  tNet.start(1500 * nScaleTest);
  pfn();
  tOpen.stop();
  tNet.stop();
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
    {ShowObjectSelDialogQt,        "Object Selections"},
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
  int i, k, cfired = 0, cmodal = 0, ctext = 0, x, y;
  long cpix;

  Group("Firing every menu item");
  // One repeating closer for the whole group, rather than a pair of
  // queued shots per item. Per-item shots cannot be cancelled, so at the
  // end of the group hundreds were still pending, and they went on to
  // close the first modal window the *next* test opened -- which reported
  // that dialog as never having appeared. A timer that lives exactly as
  // long as this loop cannot do that.
  QTimer tClose;
  QObject::connect(&tClose, &QTimer::timeout, []() {
    QWidget *pw = QApplication::activeModalWidget();
    if (pw != NULL) {
      if (s_strModal.isEmpty())
        s_strModal = pw->windowTitle();
      pw->close();
    }
  });
  tClose.start(60 * nScaleTest);
  AllActionsTestQt(&rgpa);

  // Firing the macro items below runs the user's own macros, and a macro
  // may do anything a settings file can -- on this machine several load
  // /data/med files full of -Yeb redefinitions. Whatever they do to the
  // custom slots' identity (definition, glyph, display name) is restored
  // after the sweep: three later groups assert untouched-slot
  // preconditions, and whether a macro's -i target even exists depends
  // on files outside the repository, which the suite's result must not.
  // (Found when those files reappeared on this machine and the three
  // groups went red with no code change at all.)
  int rgnTypSav[cCust], rgnObjSav[cCust], rgnPntSav[cCust], rgnFlgSav[cCust];
  char rgszGlyphSav[cCust][cchSzMax], rgszGlyph2Sav[cCust][cchSzMax];
  char rgszDispSav[cCust][cchSzMax];
  flag rgfGlyphDef[cCust], rgfGlyph2Def[cCust];
  for (i = 0; i < cCust; i++) {
    rgnTypSav[i] = rgTypSwiss[i]; rgnObjSav[i] = rgObjSwiss[i];
    rgnPntSav[i] = rgPntSwiss[i]; rgnFlgSav[i] = rgFlgSwiss[i];
    rgfGlyphDef[i] = (szDrawObject[custLo+i] == szDrawObjectDef[custLo+i]);
    rgfGlyph2Def[i] =
      (szDrawObject2[custLo+i] == szDrawObjectDef2[custLo+i]);
    sprintf2(S(rgszGlyphSav[i]), "%s", szDrawObject[custLo+i]);
    sprintf2(S(rgszGlyph2Sav[i]), "%s", szDrawObject2[custLo+i]);
    sprintf2(S(rgszDispSav[i]), "%s", szObjDisp[custLo+i]);
  }

  for (i = 0; i < rgpa.size(); i++) {
    // The label is the item's identity; the accelerator column after the
    // tab is display only, and carrying it here would break every
    // comparison below against a label from astrolog.rc.
    QString str = rgpa[i]->text().section(QChar('\t'), 0, 0);
    // Quit would end the run. The doc and website items hand a file or a
    // URL to the desktop, which isn't this suite's business to trigger.
    if (str.contains("Quit") || str.contains("Exit") ||
      str.startsWith("Open ") || str.contains("Website"))
      continue;
#ifdef __APPLE__
    // Print is the same class of thing on macOS and only there.
    // QPrintDialog is the native NSPrintPanel, which runs its own Cocoa
    // modal loop rather than Qt's -- so the repeating closer above, which
    // dismisses every other modal in this group, cannot see it and the
    // run hangs. Measured: the suite stopped here for the full 420 s
    // watchdog, and the last line before it was "firing: P&rint...".
    // On X11 and Windows QPrintDialog is a QDialog and closes normally,
    // so this skip is deliberately not portable. It costs exactly six
    // assertions: a macOS run reports 3786 where Linux reports 3792 on
    // the same ephemeris, and forcing this branch on in a Linux build
    // reproduces 3786 exactly. That is the whole difference between the
    // platforms -- worth knowing, because a lower count is otherwise
    // indistinguishable from a group that silently did not run.
    if (str.contains("rint"))
      continue;
#endif
    // Anything that puts up a modal dialog blocks here forever, since
    // nothing is driving the event loop to dismiss it. Rather than guess
    // which items those are from their labels -- the first attempt did
    // guess, guessed wrong, and hung the run -- queue a shot that closes
    // whatever modal window appears and count it.
    s_strModal = QString();
    // Name each item before firing it, flushed, so that when one takes
    // the process down the log says which. Set ASTROLOG_QT_TEST_VERBOSE
    // to see it; a clean run doesn't need the noise.
    if (getenv("ASTROLOG_QT_TEST_VERBOSE") != NULL) {
      printf("    firing: %s\n", str.toLocal8Bit().constData());
      fflush(stdout);
    }
    rgpa[i]->trigger();
    if (!s_strModal.isEmpty()) {
      cmodal++;
      if (getenv("ASTROLOG_QT_TEST_VERBOSE") != NULL)
        printf("      modal: %s -> %s\n", str.toLocal8Bit().constData(),
          s_strModal.toLocal8Bit().constData());
    }
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
  // No dialog count is reported, deliberately. Most open asynchronously,
  // appearing well after the trigger that caused them returns, so they
  // cannot be attributed to an item here and the total is not even stable
  // run to run -- 112 to 115 in one build, 126 under a sanitizer, which
  // shifts every delay. The earlier per item figure was worse than
  // unstable, it was wrong: it counted text mode items that open no
  // dialog at all, because a close armed by one item fired during a
  // later one. What matters is asserted rather than counted -- every item
  // fires, nothing hangs, and the chart still draws afterwards.
  // Let any dialog still opening settle and be closed while the closer is
  // still running, then stop it so nothing outlives this group.
  for (k = 0; k < 40; k++)
    QApplication::processEvents(QEventLoop::AllEvents, 5 * nScaleTest);
  tClose.stop();

  // Put the custom slots back (see the snapshot above the sweep). The
  // glyph pointers can't be saved and replanted directly: a macro's
  // redefinition frees the clone a saved pointer would point at, so the
  // text is what was saved, and the restore frees whatever clone the
  // sweep left before cloning the text back -- the same discipline
  // TestObjSelGlyphQt() documents for szObjDisp.
  for (i = 0; i < cCust; i++) {
    rgTypSwiss[i] = rgnTypSav[i]; rgObjSwiss[i] = rgnObjSav[i];
    rgPntSwiss[i] = rgnPntSav[i]; rgFlgSwiss[i] = rgnFlgSav[i];
    if (szDrawObject[custLo+i] != szDrawObjectDef[custLo+i]) {
      DeallocateP((char *)szDrawObject[custLo+i]);
      szDrawObject[custLo+i] = szDrawObjectDef[custLo+i];
    }
    if (!rgfGlyphDef[i])
      FCloneSzCore(rgszGlyphSav[i], (char **)&szDrawObject[custLo+i],
        fTrue);
    if (szDrawObject2[custLo+i] != szDrawObjectDef2[custLo+i]) {
      DeallocateP((char *)szDrawObject2[custLo+i]);
      szDrawObject2[custLo+i] = szDrawObjectDef2[custLo+i];
    }
    if (!rgfGlyph2Def[i])
      FCloneSzCore(rgszGlyph2Sav[i], (char **)&szDrawObject2[custLo+i],
        fTrue);
    SetObjDisp(custLo+i, rgszDispSav[i]);
  }

  printf("  %d menu items fired, %d switched to text\n", cfired, ctext);
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
  {"Setting",     "Object Selectio&ns...",                       fFalse},
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

  Check(FileOpen("no-such-file-here.as", 0, NULL, 0) == NULL,
    "FileOpen() found a file that isn't there");
  Check(fTrue, "FileOpen() on a missing file returned");

  // The macro path proper: a command line naming a file that isn't here.
  sprintf2(S(sz), "-i no-such-file-here.as");
  FProcessCommandLine(sz);
  Check(fTrue, "FProcessCommandLine() returned after a missing -i file");

  // And an outright bad switch, the other way a stale macro goes wrong.
  sprintf2(S(sz), "-ZZzzz");
  FProcessCommandLine(sz);
  Check(fTrue, "FProcessCommandLine() returned after an unknown switch");

  PrintError("Test error; the suite expects to keep running past this.");
  Check(fTrue, "PrintError() returned instead of terminating");

  // A 400-digit switch parameter, which crashed twice over before
  // REFACTORING.md B1's net pinned it (work log item 117): NParseSz()
  // and RParseSz() copied their argument into a cchSzMax local
  // unbounded, and FErrorValR() then formatted the astronomical
  // out-of-range value through two buffers too small for any big
  // double rendered in %f style.
  {
    CI ciSav = ciCore;
    char szLong[cchSzLine];
    int i;

    sprintf2(S(szLong), "-q 3 4 2020 5:0");
    for (i = CchSz(szLong); i < 420; i++)
      szLong[i] = '6';
    szLong[i] = chNull;
    FProcessCommandLine(szLong);
    Check(fTrue, "FProcessCommandLine() returned after a 400-digit time");
    ciCore = ciSav;
  }

  SetNoPopupQt(fSav);
  printf("  survived missing files, a bad switch and PrintError()\n");
}


// The body list the Object Selections dialog offers is a table of
// {definition type, definition index, name} triples, and its whole value is
// that the numbers are right -- that {1, 7066} really is Nessus. A digit
// wrong there silently puts a different body in the chart, which no other
// check would notice. So resolve each entry the way the -Ye handler does
// (astrolog.cpp, the SwissGetObjName call) and compare against the name the
// table claims.
//
// Only entries whose ephemeris data is actually present can be checked;
// this checkout ships the orbital elements and eight asteroid files, so the
// rest come back as szObjUnknown. Those are skipped rather than failed --
// a missing file is not a wrong number -- and the count of what was really
// verified is printed, so the check can't quietly degrade to nothing.
//
// Know what this does and does not catch. A number changed to another real
// body IS caught, and that is the dangerous case: the dialog would offer
// "Sedna" and quietly put Eris in the chart. A number changed to one that
// resolves to nothing is NOT caught, because it is indistinguishable here
// from a body whose ephemeris file simply isn't installed -- it only drops
// the printed count. That case is self announcing anyway: the user picks it
// and the name comes up "???" straight away.
// Drive a modal dialog: wait for it to appear, run "fnOn" against it, and
// make sure it is gone before returning.
//
// Everything here is a stoppable QTimer rather than QTimer::singleShot,
// for the reason the menu group and StrOpenDialogQt already are: a queued
// close cannot be cancelled, so one armed by a test that has finished goes
// on to close the first modal window a *later* test opens. That is not
// theoretical -- adding a two-assertion diagnostic before the Object
// Selections group made three of its assertions fail, because the
// diagnostic's own closers were still pending. A test whose result depends
// on what ran before it is reporting on the suite, not the program.
static void DriveModalQt(void (*pfnOpen)(), std::function<void(QWidget *)> fnOn)
{
  QTimer tPoll, tNet;
  flag fDone = fFalse;

  QObject::connect(&tPoll, &QTimer::timeout, [&]() {
    QWidget *pw;
    if (fDone)
      return;
    pw = QApplication::activeModalWidget();
    if (pw == NULL)
      return;
    fDone = fTrue;
    fnOn(pw);
  });
  // If the dialog never appeared, or fnOn left it open, do not hang the
  // run on it.
  QObject::connect(&tNet, &QTimer::timeout, []() {
    QWidget *pw = QApplication::activeModalWidget();
    if (pw != NULL)
      pw->close();
  });
  tPoll.start(80 * nScaleTest);
  tNet.start(3000 * nScaleTest);
  pfnOpen();
  tPoll.stop();
  tNet.stop();
}


extern flag FThemeNameDarkTestQt(CONST char *);   // qtdriver.cpp
extern QIcon IconAstrologQt();                   // qtdriver.cpp
extern void SetHomeTestQt(CONST char *);          // qtdriver.cpp
extern int NSchemeFromKdeTestQt(void);
extern void SetThemeConfigDirTestQt(CONST char *);
extern int NDarkPreferenceTestQt(void);
extern QString StrThemePrefQt(void);
extern void SetThemePrefQt(CONST char *);
extern void ApplyColorSchemeQt(void);
extern int NSchemeFromGtkFileTestQt(void);

// Write sz to the named file, creating its directory.
static flag FWriteScratchQt(CONST QString &strPath, CONST char *sz)
{
  QDir().mkpath(QFileInfo(strPath).path());
  QFile file(strPath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return fFalse;
  file.write(sz);
  return fTrue;
}

// Does the port follow the desktop into dark mode? Qt5 has no API for
// this -- QStyleHints::colorScheme() is Qt 6.5 -- and Qt5's own gtk3
// platform theme loads without supplying any palette, so the port detects
// it. The routes that need no helper program are the ones testable in
// process; the portal and gsettings routes depend on the desktop the
// developer is sitting at and are deliberately not asserted here.
// The application icon. Windows takes it from the "icon" resource in
// astrolog.rc; this port had none at all until 2026-09-01, which is
// invisible by inspection because a window with a failed icon load looks
// exactly like a window that never asked for one. So the check is that it
// resolves, and at the sizes a panel or task switcher actually requests.

static void TestAppIconQt()
{
  QList<QSize> rgsize;
  QIcon icon;
  QPixmap pix;
  int rgnSize[3] = {16, 32, 48}, i, n;

  Group("Application icon");

  icon = IconAstrologQt();
  Check(!icon.isNull(), "the application icon resolves");
  rgsize = icon.availableSizes();
  Check(rgsize.size() >= 3, "it offers at least three sizes, has %d",
    rgsize.size());
  for (i = 0; i < 3; i++) {
    n = rgnSize[i];
    Check(rgsize.contains(QSize(n, n)), "%dx%d is one of them", n, n);
  }

  // What a desktop asks for is a pixmap at a size, and QIcon will happily
  // return a scaled-up blur rather than nothing. Require the real one.
  for (i = 0; i < 3; i++) {
    n = rgnSize[i];
    pix = icon.pixmap(QSize(n, n));
    Check(!pix.isNull() && pix.width() == n && pix.height() == n,
      "pixmap(%d) comes back %dx%d", n, pix.width(), pix.height());
  }
}


static void TestColorSchemeQt()
{
  Group("Desktop colour scheme");

  // Theme names, as the desktops actually spell them. gsettings quotes
  // what it prints, so a quoted answer has to classify the same way.
  Check(FThemeNameDarkTestQt("Mint-L-Dark"), "Mint-L-Dark is dark");
  Check(FThemeNameDarkTestQt("'Mint-L-Dark'"), "a quoted answer is dark");
  Check(FThemeNameDarkTestQt("Adwaita-dark"), "Adwaita-dark is dark");
  Check(FThemeNameDarkTestQt("Breeze Dark"), "Breeze Dark is dark");
  Check(FThemeNameDarkTestQt("Yaru-blue-dark"), "Yaru-blue-dark is dark");
  Check(!FThemeNameDarkTestQt("Mint-L"), "Mint-L is light");
  Check(!FThemeNameDarkTestQt("Adwaita"), "Adwaita is light");
  Check(!FThemeNameDarkTestQt("Yaru"), "Yaru is light");
  Check(!FThemeNameDarkTestQt("Breeze"), "Breeze is light");

  QTemporaryDir dir;
  Check(dir.isValid(), "a scratch directory to write config files into");
  if (!dir.isValid())
    return;
  // Not the environment. QDir::homePath() reads HOME only on Unix; on
  // Windows it resolves through SHGetKnownFolderPath(FOLDERID_Profile),
  // which no qputenv can redirect -- setting HOME, and then HOME and
  // USERPROFILE together, both left these six assertions reading the
  // real home directory and failing on Windows. SetHomeTestQt() is a
  // seam in the QTTEST build instead, so what is being tested is the INI
  // parsing rather than the platform's idea of where a user lives.
  SetHomeTestQt(dir.path().toLocal8Bit().constData());

  // kdeglobals. This is a regression test with a specific target: the
  // section is [Colors:Window], and QSettings cannot read a key out of a
  // section whose name contains a colon -- allKeys() lists it and value()
  // then returns an empty variant for that very string. Reading it with
  // QSettings compiles, runs, and silently reports every KDE desktop as
  // light.
  QString strKde = dir.path() + "/.config/kdeglobals";
  Check(FWriteScratchQt(strKde,
    "[General]\nwidgetStyle=Breeze\n"
    "[Colors:Window]\nForegroundNormal=252,252,252\n"
    "BackgroundNormal=27,30,32\n"), "wrote a dark kdeglobals");
  Check(NSchemeFromKdeTestQt() == 1, "a dark kdeglobals reads as dark");
  Check(FWriteScratchQt(strKde,
    "[Colors:Window]\nBackgroundNormal=239,240,241\n"),
    "wrote a light kdeglobals");
  Check(NSchemeFromKdeTestQt() == 0, "a light kdeglobals reads as light");
  Check(FWriteScratchQt(strKde, "[Colors:Window]\nBackgroundNormal=x\n"),
    "wrote a malformed kdeglobals");
  Check(NSchemeFromKdeTestQt() == -1, "a malformed kdeglobals says nothing");
  QFile::remove(strKde);
  Check(NSchemeFromKdeTestQt() == -1, "no kdeglobals at all says nothing");

  // The GTK config file, which a plain GTK setup writes with no settings
  // daemon running at all.
  QString strGtk = dir.path() + "/.config/gtk-3.0/settings.ini";
  Check(FWriteScratchQt(strGtk,
    "[Settings]\ngtk-application-prefer-dark-theme=1\n"),
    "wrote gtk settings.ini asking for dark");
  Check(NSchemeFromGtkFileTestQt() == 1, "prefer-dark-theme=1 reads as dark");
  Check(FWriteScratchQt(strGtk,
    "[Settings]\ngtk-application-prefer-dark-theme=0\n"),
    "wrote gtk settings.ini asking for light");
  Check(NSchemeFromGtkFileTestQt() == 0, "prefer-dark-theme=0 reads as light");
  Check(FWriteScratchQt(strGtk, "[Settings]\ngtk-theme-name=Adwaita-dark\n"),
    "wrote gtk settings.ini naming a dark theme");
  Check(NSchemeFromGtkFileTestQt() == 1, "a dark gtk-theme-name reads as dark");
  Check(FWriteScratchQt(strGtk, "[Settings]\ngtk-theme-name=Adwaita\n"),
    "wrote gtk settings.ini naming a light theme");
  Check(NSchemeFromGtkFileTestQt() == 0, "a light gtk-theme-name reads as light");
  QFile::remove(strGtk);
  Check(NSchemeFromGtkFileTestQt() == -1, "no settings.ini at all says nothing");

  SetHomeTestQt("");
  printf("  the desktop's light/dark preference is read from each source\n");

  // The saved interface theme: View / Window Settings / Interface Theme.
  // Redirected into a scratch directory first -- these write a real
  // preference file, and writing into the config of whoever is running
  // the suite would be a side effect, not a test.
  QTemporaryDir dirCfg;
  Check(dirCfg.isValid(), "a scratch directory for the theme preference");
  if (dirCfg.isValid()) {
    SetThemeConfigDirTestQt(dirCfg.path().toUtf8().constData());

    Check(StrThemePrefQt() == "auto", "with nothing saved, the theme is auto");

    // The environment variable outranks the saved preference, because it
    // is how a developer checks one run under the other scheme without
    // disturbing what the user chose. Both directions, so this cannot
    // pass by the two happening to agree.
    // The env var is unset around each "is the SAVED theme honoured"
    // assertion. The first draft left it set to "light" across the saved-
    // light check, which therefore passed on the env var and would have
    // passed with the saved preference ignored entirely. Sabotage found
    // it. The two saved-theme checks also have to be a PAIR: detection
    // returns one fixed value on any given machine, so a build that
    // ignored the preference can satisfy at most one of them, whichever
    // desktop the suite runs on.
    qunsetenv("ASTROLOG_QT_THEME");
    SetThemePrefQt("dark");
    Check(StrThemePrefQt() == "dark", "a saved theme survives a round trip");
    Check(NDarkPreferenceTestQt() == 1, "a saved dark theme is honoured");
    SetThemePrefQt("light");
    Check(NDarkPreferenceTestQt() == 0, "a saved light theme is honoured");

    SetThemePrefQt("dark");
    qputenv("ASTROLOG_QT_THEME", "light");
    Check(NDarkPreferenceTestQt() == 0, "the env var outranks a saved dark");
    SetThemePrefQt("light");
    qputenv("ASTROLOG_QT_THEME", "dark");
    Check(NDarkPreferenceTestQt() == 1, "the env var outranks a saved light");
    qunsetenv("ASTROLOG_QT_THEME");

    // "auto" must fall through to detection rather than pinning a value.
    // Asserting the detected answer would be asserting whatever desktop
    // the test happens to run on, so assert that it is A detection: the
    // same thing the sources above returned.
    SetThemePrefQt("auto");
    Check(StrThemePrefQt() == "auto", "auto round-trips as auto");
    SetThemePrefQt("");
    Check(StrThemePrefQt() != "dark" && StrThemePrefQt() != "light",
      "an empty preference is not read as a choice");

    // And that choosing one actually REPAINTS. Detection returning the
    // right number is worth nothing if the palette never moves, which is
    // exactly the half a "the setting is saved" test would miss.
    QPalette palWas = QApplication::palette();
    SetThemePrefQt("dark");
    ApplyColorSchemeQt();
    Check(QApplication::palette().color(QPalette::Window).lightness() < 128,
      "choosing Dark actually darkens the palette");
    SetThemePrefQt("light");
    ApplyColorSchemeQt();
    Check(QApplication::palette().color(QPalette::Window).lightness() >= 128,
      "and choosing Light brings it back");
    SetThemePrefQt("auto");
    QApplication::setPalette(palWas);
  }
  printf("  the saved interface theme is honoured, and the env var beats it\n");
}


// Click a button by its label in a modal, then OK.
static void ClickInModalQt(void (*pfnOpen)(), CONST char *szButton)
{
  DriveModalQt(pfnOpen, [szButton](QWidget *pw) {
    QPushButton *ppbHit = NULL, *ppbOK = NULL;
    for (QPushButton *ppb : pw->findChildren<QPushButton *>()) {
      if (ppb->text() == QString(szButton))
        ppbHit = ppb;
      if (ppb->text() == "OK")
        ppbOK = ppb;
    }
    if (ppbHit != NULL)
      ppbHit->click();
    if (ppbOK != NULL)
      ppbOK->click();
  });
}

// Tick one checkbox by its label in a modal, then OK.
static void TickInModalQt(void (*pfnOpen)(), CONST char *szLabel)
{
  DriveModalQt(pfnOpen, [szLabel](QWidget *pw) {
    QCheckBox *pcb = NULL;
    QPushButton *ppbOK = NULL;
    for (QCheckBox *p : pw->findChildren<QCheckBox *>())
      if (p->text() == QString(szLabel))
        pcb = p;
    for (QPushButton *p : pw->findChildren<QPushButton *>())
      if (p->text() == "OK")
        ppbOK = p;
    if (pcb != NULL)
      pcb->setChecked(fTrue);
    if (ppbOK != NULL)
      ppbOK->click();
  });
}


// The quick buttons on the restriction dialogs, and the one control lookup
// they all go through.
//
// rc2qt.py splits a trailing digit run off a resource symbol into nIdx, so
// dbRe_R0/dbRe_R1/dbRe_R all arrive as szId "dbRe_R" with nIdx 0/1/-1.
// PwRcFindQt() matched szId alone and returned whichever the generated
// table listed first, which is the nIdx=0 one -- so "Toggle Minors" was
// wired to nothing at all while "Restrict All" silently ran the minors
// toggle as well, leaving every minor object unrestricted immediately
// after a user asked for everything to be restricted.
static void TestDialogButtonWiringQt()
{
  int i, cIn, cOut;

  Group("Restriction dialog buttons");

  // Toggle Minors covers oMain+1..oCore, and nothing else (wdialog.cpp
  // dbRe_R). Run it twice: a toggle must come back to where it started.
  byte rgbSav[oNorm+1];
  for (i = 0; i <= dwarfHi && i <= oNorm; i++)
    rgbSav[i] = ignore[i];

  ClickInModalQt(ShowRestrictDialogQt, "Toggle Minors");
  cIn = cOut = 0;
  for (i = 0; i <= dwarfHi && i <= oNorm; i++)
    if (ignore[i] != rgbSav[i]) {
      if (FBetween(i, oMain+1, oCore)) cIn++; else cOut++;
    }
  Check(cIn == oCore - oMain,
    "Toggle Minors toggles every minor object (%d of %d)", cIn,
    oCore - oMain);
  Check(cOut == 0, "and touches nothing outside them (%d)", cOut);
  ClickInModalQt(ShowRestrictDialogQt, "Toggle Minors");
  cIn = 0;
  for (i = 0; i <= dwarfHi && i <= oNorm; i++)
    if (ignore[i] != rgbSav[i]) cIn++;
  Check(cIn == 0, "and toggling twice returns to the start (%d changed)",
    cIn);

  // Restrict All must restrict the minors too. With the lookup bug the
  // toggle rode along on this button and turned them all back off.
  ClickInModalQt(ShowRestrictDialogQt, "&Restrict All");
  cIn = cOut = 0;
  for (i = oMain+1; i <= oCore; i++)
    (ignore[i] ? cIn : cOut)++;
  Check(cOut == 0,
    "Restrict All leaves every minor restricted (%d still not)", cOut);
  ClickInModalQt(ShowRestrictDialogQt, "&Unrestrict All");
  cIn = 0;
  for (i = oMain+1; i <= oCore; i++)
    if (ignore[i]) cIn++;
  Check(cIn == 0, "Unrestrict All clears them all (%d left)", cIn);

  for (i = 0; i <= dwarfHi && i <= oNorm; i++)
    ignore[i] = rgbSav[i];
  AdjustRestrictions();

  // Same shape in Aspect Settings: dbAs_RA0/dbAs_RA1/dbAs_RA, where the
  // toggle covers the first five aspects (wdialog.cpp:1380).
  byte rgbASav[cAspect+1];
  for (i = 1; i <= cAspect; i++)
    rgbASav[i] = ignorea[ASPT(i)];
  ClickInModalQt(ShowAspectDialogQt, "Toggle &Majors");
  cIn = cOut = 0;
  for (i = 1; i <= cAspect; i++)
    if (ignorea[ASPT(i)] != rgbASav[i]) {
      if (i <= 5) cIn++; else cOut++;
    }
  Check(cIn == 5, "Toggle Majors toggles the first five aspects (%d)", cIn);
  Check(cOut == 0, "and leaves the rest alone (%d)", cOut);
  for (i = 1; i <= cAspect; i++)
    ignorea[ASPT(i)] = rgbASav[i];

  // And the same collision on a pair of checkboxes rather than buttons:
  // dxSe_sr is "&Equatorial Latitudes" (nIdx 0) and "E&quatorial
  // Longitudes" (nIdx -1), and the bare lookup bound us.fEquator to the
  // latitudes box, so the longitudes box drove nothing.
  flag fEqSav = us.fEquator, fEq2Sav = us.fEquator2;
  us.fEquator = fFalse; us.fEquator2 = fFalse;
  DriveModalQt(ShowCalcDialogQt, [](QWidget *pw) {
    QCheckBox *pcb = NULL;
    QPushButton *ppbOK = NULL;
    for (QCheckBox *p : pw->findChildren<QCheckBox *>())
      if (p->text() == "E&quatorial Longitudes")
        pcb = p;
    for (QPushButton *p : pw->findChildren<QPushButton *>())
      if (p->text() == "OK")
        ppbOK = p;
    if (pcb != NULL)
      pcb->setChecked(fTrue);
    if (ppbOK != NULL)
      ppbOK->click();
  });
  Check(us.fEquator, "the Equatorial Longitudes box drives us.fEquator");
  Check(!us.fEquator2, "and not us.fEquator2, its neighbour");
  us.fEquator = fEqSav; us.fEquator2 = fEq2Sav;

  printf("  the restriction quick buttons drive their own ranges\n");
}


// The other symbols where a bare name sits beside an indexed control in
// the same dialog. rc_lookup_audit.py lists all seven; three were wired to
// the wrong control and are covered above, and these four were right only
// because the generated table happened to list the bare entry first.
// Matching nIdx makes them right by construction instead -- these pin that
// down, since nothing else here opens these four dialogs and checks which
// box drove which setting.
static void TestSharedSymbolBoxesQt()
{
  Group("Shared control symbols");

  // dxSe_Yn in Calculation Settings: bare is fTrueNode, index 0 is
  // fNoNutation.
  flag fSav1 = us.fTrueNode, fSav2 = us.fNoNutation;
  us.fTrueNode = fFalse; us.fNoNutation = fFalse;
  TickInModalQt(ShowCalcDialogQt, "Compute True Instead of Mean N&odes and Lilith");
  Check(us.fTrueNode, "the true-nodes box drives us.fTrueNode");
  Check(!us.fNoNutation, "and not us.fNoNutation beside it");
  us.fTrueNode = fFalse; us.fNoNutation = fFalse;
  TickInModalQt(ShowCalcDialogQt, "Tropical &Zodiac No Nutation");
  Check(us.fNoNutation, "the no-nutation box drives us.fNoNutation");
  Check(!us.fTrueNode, "and not us.fTrueNode beside it");
  us.fTrueNode = fSav1; us.fNoNutation = fSav2;

  // dxGr_XQ in Graphics Settings: bare is fKeepSquare, index 0 is
  // fAutoScale.
  fSav1 = gs.fKeepSquare; fSav2 = gs.fAutoScale;
  gs.fKeepSquare = fFalse; gs.fAutoScale = fFalse;
  TickInModalQt(ShowGraphicsSettingsDialogQt, "Ensure S&quare Charts Remain Square");
  Check(gs.fKeepSquare, "the keep-square box drives gs.fKeepSquare");
  Check(!gs.fAutoScale, "and not gs.fAutoScale beside it");
  gs.fKeepSquare = fFalse; gs.fAutoScale = fFalse;
  TickInModalQt(ShowGraphicsSettingsDialogQt, "Character Autoscale to &Fit Window");
  Check(gs.fAutoScale, "the autoscale box drives gs.fAutoScale");
  Check(!gs.fKeepSquare, "and not gs.fKeepSquare beside it");
  gs.fKeepSquare = fSav1; gs.fAutoScale = fSav2;

  // dxDi_Yu in Display Settings: bare is fEclipse. Its neighbour is
  // stored inverted, so it is only checked for not moving.
  fSav1 = us.fEclipse;
  us.fEclipse = fFalse;
  TickInModalQt(ShowDisplayDialogQt, "Sho&w Eclipse Information");
  Check(us.fEclipse, "the eclipse-information box drives us.fEclipse");
  us.fEclipse = fSav1;

  // deCh_L in Chart Settings is a pair of edits rather than checkboxes:
  // bare is the astro-graph step, index 2 the distance.
  int nSav1 = us.nAstroGraphStep, nSav2 = us.nAstroGraphDist;
  us.nAstroGraphStep = 7; us.nAstroGraphDist = 15;
  DriveModalQt(ShowChartSettingsDialogQt, [](QWidget *pw) {
    QLineEdit *peStep = NULL, *peDist = NULL;
    QPushButton *ppbOK = NULL;
    for (QLineEdit *p : pw->findChildren<QLineEdit *>()) {
      if (p->text() == "7") peStep = p;
      if (p->text() == "15") peDist = p;
    }
    for (QPushButton *p : pw->findChildren<QPushButton *>())
      if (p->text() == "OK")
        ppbOK = p;
    if (peStep != NULL) peStep->setText("9");
    if (peDist != NULL) peDist->setText("21");
    if (ppbOK != NULL) ppbOK->click();
  });
  Check(us.nAstroGraphStep == 9,
    "the two deCh_L edits are distinct: step is %d (want 9)",
    us.nAstroGraphStep);
  Check(us.nAstroGraphDist == 21,
    "and distance is %d (want 21)", us.nAstroGraphDist);
  us.nAstroGraphStep = nSav1; us.nAstroGraphDist = nSav2;

  printf("  a bare symbol and its indexed neighbour drive separate settings\n");
}


int s_nAnimStartQt = 0;   // gs.nAnim as the program started, before any test

// Animation: one switch, and only the switch moves it.
//
// Upstream stores the jump rate and the running state in the sign and
// magnitude of one int, with gi.fPause a second independent stop on top.
// That is why every control here used to be able to start the chart
// moving by accident -- picking a rate did, reversing direction did, and
// the control actually named Pause did nothing at all from a standing
// start. This port now has one running/not-running state behind
// FAnimRunningQt()/SetAnimRunningQt(), and these pin down that only the
// two controls meant to touch it do.
//
// Deliberately not Windows' behaviour; see "Known divergences".
// Clear Screen, in both modes. Text charts draw into the same buffer the
// graphics ones do, but ClearScreenQt() used to branch on us.fGraphics and
// send text mode to a ClearTextWindowQt() that cleared the separate text
// window the port stopped creating -- so the command silently did nothing
// there. Reverting the fix makes the text half of this fail.

static long CpixDifferQt()
{
  long cpix = 0;
  int x, y;

  if (gi.qim == NULL)
    return -1;
  for (y = 0; y < gi.qim->height(); y += 4)
    for (x = 0; x < gi.qim->width(); x += 4)
      if (gi.qim->pixel(x, y) != gi.qim->pixel(0, 0))
        cpix++;
  return cpix;
}

static void TestClearScreenQt()
{
  flag fGraphicsSav = us.fGraphics;
  int nModeSav = gi.nMode;
  int iMode;

  Group("Clear Screen");

  // iMode 0 is text, 1 is graphics. Both draw into gi.qim.
  //
  // The dirt is painted into the existing buffer rather than drawn, and
  // no RedrawQt() happens here at all. Two reasons, both learned the hard
  // way in one sitting: a rendered chart inherits whatever restrictions
  // and chart flags earlier groups left set, so "did it draw anything" is
  // not a stable precondition (the trap this file's header warns about,
  // and item 141's diagnosis); and RedrawQt() in TEXT mode runs Action()
  // with is.S pointed at stdout, which left the long-strings group
  // failing intermittently two runs in three. What is under test is
  // ClearScreenQt(), not the renderer.
  for (iMode = 0; iMode <= 1; iMode++) {
    CONST char *szMode = iMode ? "graphics" : "text";
    us.fGraphics = iMode;
    Check(gi.qim != NULL, "%s mode: a buffer exists", szMode);
    if (gi.qim == NULL)
      continue;
    KV kvDirt = KvFromKi(gi.kiOff) ^ 0xffffff;
    int x, y;
    for (y = 0; y < gi.qim->height(); y++)
      for (x = 0; x < gi.qim->width(); x++)
        gi.qim->setPixel(x, y,
          qRgb(RgbR(kvDirt), RgbG(kvDirt), RgbB(kvDirt)));
    long cpixDrawn = CpixDifferQt();
    Check(cpixDrawn == 0 && gi.qim->pixel(0, 0) !=
      qRgb(RgbR(KvFromKi(gi.kiOff)), RgbG(KvFromKi(gi.kiOff)),
        RgbB(KvFromKi(gi.kiOff))),
      "%s mode: buffer dirtied to a colour that is not the background",
      szMode);
    ClearScreenQt();
    long cpixAfter = CpixDifferQt();
    Check(cpixAfter == 0,
      "%s mode: Clear Screen left a uniform buffer (%ld pixels still "
      "differ)", szMode, cpixAfter);
    KV kv = KvFromKi(gi.kiOff);
    Check(gi.qim != NULL &&
      gi.qim->pixel(0, 0) == qRgb(RgbR(kv), RgbG(kv), RgbB(kv)),
      "%s mode: cleared to the background colour", szMode);
  }

  us.fGraphics = fGraphicsSav;
  gi.nMode = nModeSav;
}

// The one text-capture dance, and the global it is easy to forget.
// Action() opens is.S on the export file and fclose()s it on the way out
// without putting the caller's back, so a capture that does not restore
// it leaves the stream on a closed FILE -- and the outer Action() the
// whole GUI runs inside fcloses the same handle again on exit, which
// glibc aborts on. Export Chart Text Output had its own copy of the
// dance and was missing exactly that line (work log item 154).
//
// is.S is put back by hand after the check so a regression here fails
// this group instead of taking the rest of the suite down with it.

// The Rising chart's altitude gradient, which the Qt screen path did not
// draw until work log item 156. XChartRising() packs either one bit per
// object -- an index into an eight-entry palette -- or one byte per
// object, which across up to three objects is a packed RGB. An
// "#ifndef WINANY || !gi.fFile" clause forced every non-Windows SCREEN
// render down the one-bit path, so Qt drew eight flat colours where
// Windows drew a gradient and where every build's -Xb file render
// already drew one.
//
// Counting distinct colours separates them cheaply: the flat version has
// under a dozen, the gradient tens of thousands. The bound sits far below
// what was measured (80,595) so ordinary drift cannot trip it, and far
// above the flat case.

static void TestRisingGradientQt()
{
  int nModeSav = gi.nMode;
  flag fGraphicsSav = us.fGraphics;
  flag rgfIgnoreSav[objMax];
  QSet<QRgb> setColor;
  int x, y, i, ckv = 0;

  Group("Rising chart gradient");
  // The gradient is only reachable when the chart is in colour and has
  // objects to plot -- XChartRising() takes the first three unrestricted
  // ones, falling back to the Sun alone. TestAllMenuActionsQt() leaves
  // both of those wherever 338 menu items put them, which is why this
  // passed alone and drew three colours in the full run the first time.
  // gi.fBmp is the 24 bit target the gradient needs; without it eight
  // palette colours IS the right answer, and -Xbb turns it off.
  Borrow bColor(gs.fColor, fTrue), bInv(gs.fInverse, fFalse);
  Borrow bSec(us.fSeconds, fFalse), bBmp(gi.fBmp, fTrue);
  for (i = 0; i < objMax; i++)
    rgfIgnoreSav[i] = ignore[i];
  for (i = 0; i <= cObj; i++)
    ignore[i] = (i != oSun && i != oMoo && i != oMer);
  us.fGraphics = fTrue;
  SetChartModeQt(gRising);
  Check(gi.qim != NULL, "the rising chart rendered");
  if (gi.qim != NULL) {
    for (y = 0; y < gi.qim->height(); y += 2)
      for (x = 0; x < gi.qim->width(); x += 2)
        setColor.insert(gi.qim->pixel(x, y));
    ckv = setColor.size();
  }
  Check(ckv > 1000,
    "and drew the altitude gradient, not eight flat palette colours "
    "(%d distinct colours)", ckv);
  for (i = 0; i < objMax; i++)
    ignore[i] = rgfIgnoreSav[i];
  us.fGraphics = fGraphicsSav;
  SetChartModeQt(nModeSav);
}


static void TestTextExportQt()
{
  char szFile[cchSzMax];
  FILE *fileSav = is.S;
  flag fGraphicsSav = us.fGraphics, fHTMLSav = us.fTextHTML;
  QByteArray baDir = QDir::tempPath().toLocal8Bit();
  CONST char *szDir = baDir.constData();
  FILE *fileT;
  long cb = -1;

  Group("Text export");
  sprintf2(S(szFile), "%s/astrolog-qt-textexport-%d.tmp", szDir,
    (int)QCoreApplication::applicationPid());
  CaptureTextToFileQt(szFile, fFalse);

  Check(is.S == fileSav,
    "the text capture puts is.S back (Action() leaves it on a closed FILE)");
  is.S = fileSav;
  Check(us.fGraphics == fGraphicsSav, "and restores us.fGraphics");
  Check(us.fTextHTML == fHTMLSav, "and restores us.fTextHTML");

  fileT = fopen(szFile, "r");
  if (fileT != NULL) {
    fseek(fileT, 0, SEEK_END);
    cb = ftell(fileT);
    fclose(fileT);
  }
  Check(cb > 100, "and actually wrote the chart (%ld bytes)", cb);
  QFile::remove(QString(szFile));
}


static void TestAnimationStateQt()
{
  int nAnimSav = gs.nAnim, nDirSav = gi.nDir;
  flag fPauseSav = gi.fPause;
  CI ciSav = ciCore;

  Group("Animation state");

  Check(s_nAnimStartQt < 0,
    "animation is stopped at startup, with a rate remembered (was %d)",
    s_nAnimStartQt);

  QAction *paRun = PaFindActionTestQt("Do &Animation");
  QAction *paPause = PaFindActionTestQt("&Pause Animation");
  QAction *paHours = PaFindActionTestQt("&Hours");
  QAction *paSecs = PaFindActionTestQt("&Seconds");
  QAction *paRev = PaFindActionTestQt("&Reverse Direction");
  Check(paRun != NULL && paPause != NULL && paHours != NULL &&
    paSecs != NULL && paRev != NULL, "the animation items are on the menu");
  if (paRun == NULL || paPause == NULL || paHours == NULL ||
    paSecs == NULL || paRev == NULL)
    return;

  // The guard the timer itself uses.
#define FRunningQt() (gs.nAnim >= 1 && !gi.fPause)
  gs.nAnim = -10; gi.fPause = fFalse;
  Check(!FRunningQt(), "nothing is moving at rest");

  // 'p' is the whole interface: it starts, and it stops.
  paPause->trigger();
  Check(FRunningQt(), "'p' starts it from a standing start (nAnim %d)",
    gs.nAnim);
  paPause->trigger();
  Check(!FRunningQt(), "'p' again stops it (nAnim %d)", gs.nAnim);
  paPause->trigger();
  Check(FRunningQt(), "and 'p' starts it once more");

  // Stopped is one canonical state, so the two upstream stops can never
  // disagree and leave the menu contradicting the chart.
  paPause->trigger();
  Check(gs.nAnim < 0 && !gi.fPause,
    "stopped is always negative rate with pause clear (%d, %d)",
    gs.nAnim, gi.fPause);

  // Picking a rate never starts or stops anything, in either state.
  paHours->trigger();
  Check(!FRunningQt() && gs.nAnim == -3,
    "picking a rate while stopped stays stopped (%d)", gs.nAnim);
  paSecs->trigger();
  Check(!FRunningQt() && gs.nAnim == -1,
    "and so does picking another (%d)", gs.nAnim);
  paPause->trigger();
  Check(FRunningQt() && gs.nAnim == 1,
    "starting uses the rate that was chosen (%d)", gs.nAnim);
  paHours->trigger();
  Check(FRunningQt() && gs.nAnim == 3,
    "picking a rate while running keeps it running (%d)", gs.nAnim);

  // Reversing reverses, and does nothing else.
  int nDirWas = gi.nDir;
  paRev->trigger();
  Check(gi.nDir == -nDirWas, "reverse flips the direction (%d)", gi.nDir);
  Check(FRunningQt(), "and leaves it running");
  paPause->trigger();
  nDirWas = gi.nDir;
  paRev->trigger();
  Check(gi.nDir == -nDirWas, "reverse flips it while stopped too (%d)",
    gi.nDir);
  Check(!FRunningQt(), "and does not start it, unlike Windows");

  // The rate survives a stop, which is the whole reason for the sign.
  Check(NAbs(gs.nAnim) == 3, "the rate is remembered while stopped (%d)",
    NAbs(gs.nAnim));

  // Do Animation is the same one switch under its Windows name.
  paRun->trigger();
  Check(FRunningQt(), "Do Animation starts it too");
  Check(paPause->isChecked() == fFalse && paRun->isChecked(),
    "and both menu items agree it is running");
  paRun->trigger();
  Check(!FRunningQt(), "and stops it");
  Check(paPause->isChecked() && !paRun->isChecked(),
    "and both agree it is stopped");
#undef FRunningQt

  gs.nAnim = nAnimSav; gi.nDir = nDirSav; gi.fPause = fPauseSav;
  ciCore = ciSav;
  CastChart(1);
  printf("  one switch starts and stops it; nothing else moves the chart\n");
}

// Windows dialogs act on a mnemonic letter pressed on its own -- "s"
// ticks "&Sun" -- while Qt wants Alt held. Both builds read the same "&"
// out of astrolog.rc, so only the routing differs, and on the restriction
// grid of 52 checkboxes it decides whether the dialog can be used from
// the keyboard at all.
static void TestDialogMnemonicsQt()
{
  Group("Dialog mnemonic keys");

  DriveModalQt(ShowRestrictDialogQt, [](QWidget *pw) {
    auto tap = [pw](CONST char *szCh) {
      QKeyEvent ev(QEvent::KeyPress, (int)Qt::Key_A, Qt::NoModifier,
        QString(szCh));
      QApplication::sendEvent(pw, &ev);
    };
    auto find = [pw](CONST char *sz) -> QCheckBox * {
      for (QCheckBox *p : pw->findChildren<QCheckBox *>())
        if (p->text() == QString(sz))
          return p;
      return NULL;
    };
    struct { CONST char *szLabel, *szCh; } rg[] = {
      {"&Sun", "s"}, {"Mercur&y", "y"}, {"&Venus", "v"}, {"&Earth", "e"},
      {"M&oon", "o"}, {"&Jupiter", "j"}, {"&Pluto", "p"},
      {"&Neptune", "n"}, {"&Chiron", "c"} };
    for (int k = 0; k < 9; k++) {
      QCheckBox *pcb = find(rg[k].szLabel);
      Check(pcb != NULL, "%s is on the dialog", rg[k].szLabel);
      if (pcb == NULL)
        continue;
      // fTrue/fFalse rather than the bool straight from Qt: "flag" is an
      // int, and MSVC warns C4805 on mixing bool with it in a comparison.
      // Benign here -- both sides are 0 or 1 -- but it is the exact shape
      // of a real mistake, and the codebase's own type is flag.
      flag fWas = pcb->isChecked() ? fTrue : fFalse;
      tap(rg[k].szCh);
      Check((pcb->isChecked() ? fTrue : fFalse) != fWas,
        "bare '%s' toggles %s", rg[k].szCh, rg[k].szLabel);
      tap(rg[k].szCh);
    }

    // A letter no control claims changes nothing.
    int cBefore = 0;
    for (QCheckBox *p : pw->findChildren<QCheckBox *>())
      cBefore += p->isChecked() ? 1 : 0;
    tap("q");
    int cAfter = 0;
    for (QCheckBox *p : pw->findChildren<QCheckBox *>())
      cAfter += p->isChecked() ? 1 : 0;
    Check(cBefore == cAfter, "a letter with no mnemonic changes nothing");
    pw->close();
  });

  // The keystroke has to reach a text field when one has focus, or typing
  // a chart name would tick boxes across the dialog.
  DriveModalQt(ShowChartInfoDialogQt, [](QWidget *pw) {
    QLineEdit *pe = NULL;
    for (QLineEdit *p : pw->findChildren<QLineEdit *>())
      if (p->isEnabled() && p->isVisibleTo(pw)) {
        pe = p;
        break;
      }
    Check(pe != NULL, "the chart info dialog has a text field");
    if (pe != NULL) {
      int cBefore = 0;
      for (QCheckBox *p : pw->findChildren<QCheckBox *>())
        cBefore += p->isChecked() ? 1 : 0;
      pe->setFocus();
      pe->setText("");
      for (CONST char *pch = "sunny"; *pch != chNull; pch++) {
        QKeyEvent ev(QEvent::KeyPress, (int)Qt::Key_A, Qt::NoModifier,
          QString(QChar(*pch)));
        QApplication::sendEvent(pe, &ev);
      }
      int cAfter = 0;
      for (QCheckBox *p : pw->findChildren<QCheckBox *>())
        cAfter += p->isChecked() ? 1 : 0;
      Check(pe->text() == "sunny",
        "a text field still takes typed letters (\"%s\")",
        pe->text().toLocal8Bit().constData());
      Check(cBefore == cAfter, "and typing ticks nothing");
    }
    pw->close();
  });

  printf("  bare mnemonic letters work, and text fields still take typing\n");
}


// Arrow keys move to the control that is actually in that direction.
//
// Qt moves focus on an arrow key by walking the tab chain. Object
// Restrictions lists OK and Cancel before all 52 checkboxes, and focus
// starts on OK, so Up wrapped to the end of the chain and landed on
// "Recall" at the opposite corner -- with Cancel sitting directly above.
// Windows walks its tab order too, so following the layout instead is a
// divergence, and a deliberate one: the dialog is a grid of columns.
static void TestDialogArrowKeysQt()
{
  Group("Dialog arrow keys");

  DriveModalQt(ShowRestrictDialogQt, [](QWidget *pw) {
    auto go = [pw](CONST char *szFrom, int key) -> QString {
      for (QAbstractButton *p : pw->findChildren<QAbstractButton *>())
        if (p->text() == QString(szFrom)) {
          p->setFocus();
          break;
        }
      QKeyEvent ev(QEvent::KeyPress, key, Qt::NoModifier);
      QApplication::sendEvent(pw, &ev);
      QWidget *pwNow = QApplication::focusWidget();
      QAbstractButton *pb = qobject_cast<QAbstractButton *>(pwNow);
      return pb != NULL ? pb->text() : QString("(not a button)");
    };
    struct { CONST char *szFrom; int key; CONST char *szKey, *szWant; } rg[] = {
      // The reported case: Cancel is directly above OK.
      {"OK",              Qt::Key_Up,    "Up",    "Cancel"},
      {"Cancel",          Qt::Key_Down,  "Down",  "OK"},
      // Down and up the Planets column.
      {"&Earth",          Qt::Key_Down,  "Down",  "&Sun"},
      {"&Sun",            Qt::Key_Down,  "Down",  "M&oon"},
      {"&Sun",            Qt::Key_Up,    "Up",    "&Earth"},
      // Across to the next column and back.
      {"&Earth",          Qt::Key_Right, "Right", "&Chiron"},
      {"&Chiron",         Qt::Key_Left,  "Left",  "&Earth"},
      {"&Chiron",         Qt::Key_Right, "Right", "Ascendant"},
      // The quick buttons stacked down the right edge.
      {"&Restrict All",   Qt::Key_Down,  "Down",  "&Unrestrict All"},
      {"&Unrestrict All", Qt::Key_Down,  "Down",  "Toggle Minors"},
      {"Toggle Minors",   Qt::Key_Up,    "Up",    "&Unrestrict All"} };
    for (int k = 0; k < 11; k++) {
      QString str = go(rg[k].szFrom, rg[k].key);
      Check(str == QString(rg[k].szWant), "%s from %s reaches %s (got %s)",
        rg[k].szKey, rg[k].szFrom, rg[k].szWant,
        str.toLocal8Bit().constData());
    }
    pw->close();
  });

  printf("  arrow keys follow the layout, not the order controls were built\n");
}


// A slot forced to a midpoint draws its name, not its old glyph.
//
// -Fm overrides where a slot sits but not what it is, so the slot keeps
// the body it started as -- and so its glyph. The position list, the
// sidebar and the Object Selections dialog all show the name the midpoint
// was given, which left the wheel the one place still claiming the point
// was Chiron.
//
// Checked by rendering rather than by reading the code: change only the
// name and see whether the picture moves. The sidebar has to be off for
// that to mean anything, because it prints object names into the same
// image and would make any rename change it.
static void TestMidpointGlyphQt()
{
  unsigned long lPlain1, lPlain2, lMid1, lMid2;
  int obj = oChi;
  flag fIgnoreSav = ignore[obj];
  real forceSav = force[obj];
  CONST char *szDispSav = szObjDisp[obj];

  Group("Midpoint glyph");

  // Take a copy and put the whole of it back at the end, then set a known
  // baseline on top. TestAllMenuActionsQt() fires all 338 menu items and
  // leaves the program wherever that lands: measured here as heliocentric,
  // sidereal, equatorial, 3D houses, an Indian wheel, house system 22,
  // monochrome and double scale. A rendering test that inherits any of
  // that is testing the leftovers, not the change -- this one failed twice
  // that way, on gs.fLabel and then on us.nRel, before the state was
  // pinned rather than guessed at one field per attempt.
  US usSav = us;
  GS gsSav = gs;
  CI ciSav = ciCore;

  // Pin the chart's moment too, to CONSTANTS. The first pin here was
  // "ciCore = ciTwin", which only worked while ciTwin still held its
  // compile-time 1991 data -- the relationship groups load charts of
  // "now" into it, so the flake this pin exists to kill (item 4's
  // pin-the-time lesson: the render drifted with the clock at the
  // double scale TestAllMenuActionsQt() leaves behind) came back the
  // same evening, wearing a different global.
  ciCore.mon = 9; ciCore.day = 11; ciCore.yea = 1991;
  ciCore.tim = 0.0; ciCore.dst = 0.0; ciCore.zon = 8.0;
  ciCore.lon = DEFAULT_LONG; ciCore.lat = DEFAULT_LAT;

  ignore[obj] = fFalse;
  AdjustRestrictions();
  us.nRel = rcNone;
  us.fIndian = fFalse;
  us.fHouse3D = fFalse;
  us.objCenter = oEar;
  us.fSidereal = fFalse;
  gs.fEquator = fFalse;
  gs.fThick = fFalse;
  gs.fColor = fTrue;
  // Set what the render depends on rather than inheriting it.
  // TestAllMenuActionsQt() fires all 338 menu items, "Show Glyph Labels"
  // among them, so gs.fLabel arrives here as whatever that left. With it
  // off DrawObject() returns before drawing anything and every hash below
  // matches every other one -- the test then passes or fails on nothing.
  gs.fText = fFalse;
  gs.fLabel = fTrue;
  // And a single chart. TestAllMenuActionsQt() fires every relationship
  // mode on its way through all 338 menu items, and this arrived in
  // rcProgress, where the wheel draws two charts and the forced slot is
  // not the one being labelled -- so the name never reached the picture
  // and the assertion failed on the chart type rather than on the fix.
  us.nRel = rcNone;

  // Render the wheel and hash it.
  auto hash = []() -> unsigned long {
    unsigned long l = 5381;
    int x, y;
    SetChartModeQt(gWheel);
    RedrawQt();
    if (gi.qim == NULL)
      return 0;
    for (y = 0; y < gi.qim->height(); y++) {
      CONST uchar *pb = gi.qim->constScanLine(y);
      for (x = 0; x < gi.qim->bytesPerLine(); x++)
        l = l*33 + pb[x];
    }
    return l;
  };

  // Control: a plain slot draws its glyph, so its name cannot show.
  force[obj] = 0.0;
  szObjDisp[obj] = "AAA"; CastChart(1); lPlain1 = hash();
  szObjDisp[obj] = "ZZZ"; CastChart(1); lPlain2 = hash();
  Check(lPlain1 == lPlain2,
    "renaming a plain slot leaves the wheel alone -- it draws a glyph");

  // The reported case: forced to the Sun/Moon midpoint.
  force[obj] = ForceMid(oSun, oMoo);
  szObjDisp[obj] = "AAA"; CastChart(1); lMid1 = hash();
  szObjDisp[obj] = "ZZZ"; CastChart(1); lMid2 = hash();
  Check(lMid1 != lMid2,
    "a forced midpoint draws the name it was given, not the old glyph");
  Check(lMid1 != lPlain1,
    "and a midpoint slot no longer renders like the body it replaced");

  ignore[obj] = fIgnoreSav;
  force[obj] = forceSav;
  szObjDisp[obj] = szDispSav;
  us = usSav;
  gs = gsSav;
  ciCore = ciSav;
  AdjustRestrictions();
  CastChart(1);
  printf("  a slot forced to a midpoint is labelled, not glyphed\n");
}


// Pointing a slot at a different body drops the old body's glyph.
//
// -Ye does this (astrolog.cpp, now via SetObjGlyphNoneCore) and the two
// Object Selections dialogs did not -- szDrawObject was referenced zero
// times in either of them. So Chiron assigned to a slot from the command
// line drew its name, while the same assignment made through the dialog
// kept the old body's glyph, on a point the position list, the sidebar
// and the dialog itself all called Chiron. The same fault as the midpoint
// glyph above, in the path nobody had looked at.
//
// Row 1 (Cupido) on purpose: nrvate.as redefines row 0 already, so that
// slot's glyph is the sentinel before the test starts and there would be
// nothing to observe. This one still holds its own glyph.
static void TestObjSelGlyphQt()
{
  int iobj = uranLo + 1;
  int nTypSav = rgTypSwiss[iobj - custLo];
  int nObjSav = rgObjSwiss[iobj - custLo];
  flag fIgnoreSav = ignore[iobj];
  // Save the display name's *text*, not its pointer. The dialog writes it
  // through FCloneSzCore(), which frees the old string, so a saved pointer
  // is dangling by the time this returns -- restoring it plants a freed
  // address that FinalizeProgram() frees a second time. That is a heap
  // corruption, and it showed up as an intermittent SIGSEGV inside Qt's
  // accessibility cache while a later dialog was being torn down, which
  // points nowhere near the cause.
  char szDispSav[cchSzMax];
  flag fDispWasOwn = (szObjDisp[iobj] == szObjName[iobj]);
  sprintf2(S(szDispSav), "%s", szObjDisp[iobj]);

  Group("Object selection glyph");

  Check(szDrawObject[iobj] == szDrawObjectDef[iobj],
    "the slot starts out holding its own body's glyph");

  DriveModalQt(ShowObjectSelDialogQt, [](QWidget *pw) {
    QComboBox *pcb = NULL;
    QPushButton *ppbOK = NULL;
    QList<QComboBox *> rgcb = pw->findChildren<QComboBox *>();
    if (rgcb.size() > 1)
      pcb = rgcb[1];
    for (QPushButton *p : pw->findChildren<QPushButton *>())
      if (p->text() == "OK")
        ppbOK = p;
    if (pcb != NULL)
      pcb->setEditText("10199");
    if (ppbOK != NULL)
      ppbOK->click();
    else
      pw->close();
  });

  Check(rgObjSwiss[iobj - custLo] == 10199,
    "the dialog put the new body in the slot (%d)",
    rgObjSwiss[iobj - custLo]);
  Check(szDrawObject[iobj] != szDrawObjectDef[iobj],
    "and the slot stopped carrying its old body's glyph");
  Check(szDrawObject[iobj][0] == 't' || szDrawObject[iobj][0] == 'T',
    "leaving the draw-the-name sentinel instead (\"%s\")",
    szDrawObject[iobj]);

  // Put the slot back, freeing whatever the dialog cloned rather than
  // cloning over it -- the exit-time "allocations not freed" count is a
  // release-build feature and a leak here would show up in it.
  if (szDrawObject[iobj] != szDrawObjectDef[iobj]) {
    DeallocateP((char *)szDrawObject[iobj]);
    szDrawObject[iobj] = szDrawObjectDef[iobj];
  }
  if (szDrawObject2[iobj] != szDrawObjectDef2[iobj]) {
    DeallocateP((char *)szDrawObject2[iobj]);
    szDrawObject2[iobj] = szDrawObjectDef2[iobj];
  }
  if (fDispWasOwn) {
    // It was the stock name, a constant: free any clone and point back.
    if (szObjDisp[iobj] != szObjName[iobj])
      DeallocateP((char *)szObjDisp[iobj]);
    szObjDisp[iobj] = szObjName[iobj];
  } else
    FCloneSzCore(szDispSav, (char **)&szObjDisp[iobj],
      szObjDisp[iobj] == szObjName[iobj]);
  rgTypSwiss[iobj - custLo] = nTypSav;
  rgObjSwiss[iobj - custLo] = nObjSav;
  ignore[iobj] = fIgnoreSav;
  AdjustRestrictions();
  CastChart(1);
  printf("  a slot given a new body stops drawing the old one\n");
}


// Lookup Names writes the number and the name into the body field, and
// the parse reads that pair back.
//
// A looked-up row used to keep a bare catalogue number in Contains while
// the Name box beside it filled in, so the two columns disagreed about
// what the row was. The field now reads "10199 Chariklo".
//
// The parse had to learn that first: a trailing run of letters after a
// space was always read as point/flag letters, so "Chariklo" would set
// the apsis marker off its own 'a'. A run counts as flags only when every
// letter in it is one.
static void TestObjSelLookupQt()
{
  OBJDEF od;
  int j, k, nPnt, nFlg;

  Group("Object selection lookup");

  struct { CONST char *sz; int nTyp, nObj, nPnt, nFlg; } rg[] = {
    {"10199",             1,  10199, 0, 0},
    {"10199 Chariklo",    1,  10199, 0, 0},   // the pair Lookup Names writes
    {"52872 Okyrhoe",     1,  52872, 0, 0},
    {"10199 nH",          1,  10199, 1, 1},   // a real suffix still reads
    {"10199 Chariklo nH", 1,  10199, 1, 1},   // and still reads beside a name
    {"Chariklo",          1,  10199, 0, 0},
    {"h0",                0,      0, 0, 0} };
  for (int i = 0; i < 7; i++) {
    flag f = FObjSelParse(rg[i].sz, &od);
    j = od.nTyp; k = od.nObj; nPnt = od.nPnt; nFlg = od.nFlg;
    Check(f && j == rg[i].nTyp && k == rg[i].nObj && nPnt == rg[i].nPnt &&
      nFlg == rg[i].nFlg,
      "\"%s\" parses as typ %d obj %d pnt %d flg %d (got %d %d %d %d)",
      rg[i].sz, rg[i].nTyp, rg[i].nObj, rg[i].nPnt, rg[i].nFlg,
      j, k, nPnt, nFlg);
  }
  Check(FObjSelFlagRun("nH") && FObjSelFlagRun("a"),
    "a run of suffix letters is recognised as one");
  Check(!FObjSelFlagRun("Chariklo") && !FObjSelFlagRun("Okyrhoe"),
    "and a body's name is not");

  // Through the real dialog: put a bare number in the first row's body
  // field, press Lookup Names, and read both boxes back.
  DriveModalQt(ShowObjectSelDialogQt, [](QWidget *pw) {
    QComboBox *pcb = NULL;
    QLineEdit *pe = NULL;
    QPushButton *ppb = NULL, *ppbCancel = NULL;
    QList<QComboBox *> rgcb = pw->findChildren<QComboBox *>();
    QList<QLineEdit *> rgpe;

    // A combo box owns a QLineEdit of its own; the Name column's are the
    // ones that do not belong to one.
    for (QLineEdit *p : pw->findChildren<QLineEdit *>())
      if (qobject_cast<QComboBox *>(p->parentWidget()) == NULL)
        rgpe.append(p);
    if (!rgcb.isEmpty())
      pcb = rgcb[0];
    if (!rgpe.isEmpty())
      pe = rgpe[0];
    for (QPushButton *p : pw->findChildren<QPushButton *>()) {
      if (p->text() == "&Lookup Names")
        ppb = p;
      if (p->text() == "Cancel")
        ppbCancel = p;
    }
    Check(pcb != NULL && pe != NULL && ppb != NULL,
      "the first row's two boxes and the Lookup Names button are there");
    if (pcb == NULL || pe == NULL || ppb == NULL) {
      pw->close();
      return;
    }
    pcb->setEditText("10199");
    pe->setText("");
    ppb->click();
    Check(pe->text() == "Chariklo",
      "Lookup Names fills the Name box (\"%s\")",
      pe->text().toLocal8Bit().constData());
    Check(pcb->currentText() == "10199 Chariklo",
      "and writes the number and name into Contains (\"%s\")",
      pcb->currentText().toLocal8Bit().constData());

    // What it wrote has to parse back to what was typed.
    QByteArray ba = pcb->currentText().toLocal8Bit();
    OBJDEF od2;
    Check(FObjSelParse(ba.constData(), &od2) &&
      od2.nTyp == 1 && od2.nObj == 10199 && od2.nPnt == 0 && od2.nFlg == 0,
      "and that pair parses back to the number it started from");

    // Cancel, so none of this reaches the settings.
    if (ppbCancel != NULL)
      ppbCancel->click();
    else
      pw->close();
  });

  printf("  Lookup Names fills both boxes, and the pair parses back\n");
}


// Does a line start with this switch, and end there or at a space? A bare
// prefix test would let "-YRT" answer to "-YR", which is the very pair
// this is here to tell apart.
static flag FEqSzPrefixQt(CONST char *szLine, CONST char *szSwitch)
{
  int i;

  for (i = 0; szSwitch[i]; i++)
    if (szLine[i] != szSwitch[i])
      return fFalse;
  return szLine[i] <= ' ';
}


// Does a settings file bring back what it was written from?
//
// Save Program Settings is the only way a user keeps anything, and until
// now nothing asserted that what it writes reloads. Four ranges did not,
// in both builds, and all four are invisible unless you look for them --
// the file is written, it parses, and it quietly holds different values
// than the program did.
//
// Each field is set to a distinctive value, written out, overwritten in
// memory with a sentinel, and only the lines for the switch under test are
// replayed. Replaying the whole file would apply several hundred settings
// to the running suite, which is why TestForcedPositionsQt() reads rather
// than replays; filtering by switch keeps the round trip real and the
// blast radius nil.
static void TestSettingsRoundTripQt()
{
  byte rgbIgnoreSav[objMax], rgbIgnore2Sav[objMax];
  OBJSET rgosSav[oNorm1+1];
  char *szFileOutSav = is.szFileOut;
  int nWriteFormatSav = us.nWriteFormat, i;
  flag fNoWriteSav = us.fNoWrite;
  char szPath[cchSzMax], szLine[cchSzMax];
  FILE *file;

  Group("Settings file round trip");

  // The defaults themselves, before anything else: the flat rObjOrb[]
  // initializer was one entry short from Lilith on, so Lilith wore
  // Fortune's 360-degree orb, every later slot shifted onto its
  // neighbor's, and the star row read zero. rgobjset[]'s named rows
  // carry the corrected values; these are the sentinel points of that
  // repair. (Live data, but nothing before this group rewrites row 18's
  // orb, and nrvate.as sets it to the same 2.0 the default now is.)
  Check(rgobjset[18].orb == 2.0,
    "Lilith has her own orb back (%g)", rgobjset[18].orb);
  Check(rgobjset[84].orb == 2.0,
    "and the fixed-star row is no longer zero (%g)", rgobjset[84].orb);
  Check(rgrBonusInf[1] == 20.0 && rgrBonusInf[5] == 10.0,
    "the rulership bonuses moved out whole (%g, %g)",
    rgrBonusInf[1], rgrBonusInf[5]);

  CopyRgb(ignore.rgn, rgbIgnoreSav, sizeof(ignore.rgn));
  CopyRgb(ignore2.rgn, rgbIgnore2Sav, sizeof(ignore2.rgn));
  // One snapshot where four parallel-array copies used to be -- the
  // struct being the point of the exercise.
  CopyRgb((pbyte)rgobjset.rgn, (pbyte)rgosSav, sizeof(rgobjset.rgn));

  // Index 60 is a planetary moon, inside the range the Moon Object
  // Settings dialog edits and outside every range the writer covered.
  // Index 25 is a house cusp. Both are ordinary things to customise.
  int iMoon = 60, iCusp = 25;
  ignore[iMoon] = fFalse; ignore2[iMoon] = fTrue;
  rgobjset[iMoon].orb = 7.5;
  rgobjset[iMoon].add = 1.25;
  rgobjset[iMoon].kolor = 13;
  // Influence is written "%2.0f", so it is a whole number by format --
  // not a gap, a precision limit, and worth pinning as one.
  rgobjset[iMoon].inf = 3.0;
  rgobjset[iMoon].tinf = 4.0;
  rgobjset[iCusp].tinf = 6.0;

  sprintf2(S(szPath), "%s/astrolog-qt-roundtrip-%d.as",
    QDir::tempPath().toLocal8Bit().constData(),
    (int)QCoreApplication::applicationPid());
  us.fNoWrite = fFalse;
  us.nWriteFormat = 'd';
  is.szFileOut = szPath;
  Check(FOutputSettings(), "FOutputSettings() wrote a settings file");

  // Overwrite in memory, so anything the file failed to carry stays wrong.
  ignore[iMoon] = fTrue; ignore2[iMoon] = fFalse;
  rgobjset[iMoon].orb = rgobjset[iMoon].add = rgobjset[iMoon].inf = 99.0;
  rgobjset[iMoon].kolor = 1;
  rgobjset[iMoon].tinf = rgobjset[iCusp].tinf = 99.0;

  file = FileOpen(szPath, 3, NULL, 0);
  Check(file != NULL, "and it can be read back");
  if (file != NULL) {
    while (fgets(szLine, cchSzMax, file) != NULL) {
      // Only the switches under test, so replaying cannot disturb the
      // several hundred other settings the file carries.
      if (!FEqSzPrefixQt(szLine, "-YR") && !FEqSzPrefixQt(szLine, "-YRT") &&
        !FEqSzPrefixQt(szLine, "-YAm") && !FEqSzPrefixQt(szLine, "-YAd") &&
        !FEqSzPrefixQt(szLine, "-Yj") && !FEqSzPrefixQt(szLine, "-YjT") &&
        !FEqSzPrefixQt(szLine, "-YkO"))
        continue;
      for (i = 0; szLine[i]; i++)
        ;
      while (i > 0 && szLine[i-1] < ' ')
        szLine[--i] = chNull;
      FProcessCommandLine(szLine);
    }
    fclose(file);
  }

  Check(ignore[iMoon] == fFalse,
    "a natal restriction on a moon survives (ignore[%d] is %d, want 0)",
    iMoon, ignore[iMoon]);
  Check(ignore2[iMoon] == fTrue,
    "and so does the transit one (ignore2[%d] is %d, want 1)",
    iMoon, ignore2[iMoon]);
  Check(rgobjset[iMoon].kolor == 13,
    "a moon's color survives (%d, want 13)", rgobjset[iMoon].kolor);
  Check(rgobjset[iMoon].orb == 7.5,
    "a moon's max orb survives (%.2f, want 7.50)", rgobjset[iMoon].orb);
  Check(rgobjset[iMoon].add == 1.25,
    "a moon's orb addition survives (%.2f, want 1.25)", rgobjset[iMoon].add);
  Check(rgobjset[iMoon].inf == 3.0,
    "a moon's influence survives (%.2f, want 3.00)", rgobjset[iMoon].inf);
  Check(rgobjset[iMoon].tinf == 4.0,
    "a moon's transit influence survives (%.2f, want 4.00)",
    rgobjset[iMoon].tinf);
  Check(rgobjset[iCusp].tinf == 6.0,
    "a cusp's transit influence survives (%.2f, want 6.00)",
    rgobjset[iCusp].tinf);

  CopyRgb(rgbIgnoreSav, ignore.rgn, sizeof(ignore.rgn));
  CopyRgb(rgbIgnore2Sav, ignore2.rgn, sizeof(ignore2.rgn));
  CopyRgb((pbyte)rgosSav, (pbyte)rgobjset.rgn, sizeof(rgobjset.rgn));
  is.szFileOut = szFileOutSav;
  us.nWriteFormat = nWriteFormatSav;
  us.fNoWrite = fNoWriteSav;
  AdjustRestrictions();
  printf("  what Save Program Settings writes is what it reads back\n");
}


// The Custom Objects dialog's parse is the shared FObjDefParse() now.
//
// The copy it replaced (ParseCustomDefQt, and the same open coded twice
// in Windows' DlgCustom) lacked the FObjSelFlagRun() guard, so a
// definition carrying a name beside its number -- "10199 Chariklo", the
// very pair Lookup Names writes into the Object Selections field -- read
// the name's 'a' as the apsis marker and stored nPnt=4. This drives the
// real dialog and asserts what reached the arrays.
static void TestCustomDialogParseQt()
{
  int nTypSav = rgTypSwiss[0], nObjSav = rgObjSwiss[0];
  int nPntSav = rgPntSwiss[0], nFlgSav = rgFlgSwiss[0];
  // Row 1 too, for the glyph: row 0's slot is already redefined by
  // nrvate.as, so its glyph is the sentinel before the test starts and
  // proves nothing. Row 1 still holds its own body and its own glyph.
  int iobj1 = custLo + 1;
  int nTyp1Sav = rgTypSwiss[1], nObj1Sav = rgObjSwiss[1];
  int nPnt1Sav = rgPntSwiss[1], nFlg1Sav = rgFlgSwiss[1];

  Group("Custom objects parse");
  Check(szDrawObject[iobj1] == szDrawObjectDef[iobj1],
    "row 1's slot starts out holding its own body's glyph");

  DriveModalQt(ShowCustomDialogQt, [](QWidget *pw) {
    QLineEdit *peDef = NULL;
    QList<int> rgx;

    // The dialog lays its 50 rows out in two banks, so the edits sit in
    // four columns: name and definition of the left bank, then of the
    // right (x = 40, 85, 170, 215 in dialog units). ded01 -- row zero's
    // definition -- is the top of the second column. Found by geometry
    // because the first attempt took "rightmost column" and edited row
    // 25 of the other bank instead, which the assertion caught.
    for (QLineEdit *pe : pw->findChildren<QLineEdit *>())
      if (!rgx.contains(pe->x()))
        rgx.append(pe->x());
    std::sort(rgx.begin(), rgx.end());
    if (rgx.size() < 2)
      return;
    QLineEdit *peDef1 = NULL;
    for (QLineEdit *pe : pw->findChildren<QLineEdit *>())
      if (pe->x() == rgx[1] && (peDef == NULL || pe->y() < peDef->y()))
        peDef = pe;
    for (QLineEdit *pe : pw->findChildren<QLineEdit *>())
      if (pe->x() == rgx[1] && pe != peDef &&
        (peDef1 == NULL || pe->y() < peDef1->y()))
        peDef1 = pe;
    if (peDef != NULL)
      peDef->setText("10199 Chariklo");
    if (peDef1 != NULL)
      peDef1->setText("52872");
    for (QPushButton *ppb : pw->findChildren<QPushButton *>())
      if (ppb->text() == "OK") {
        ppb->click();
        return;
      }
    pw->close();
  });

  Check(rgTypSwiss[0] == 1 && rgObjSwiss[0] == 10199,
    "the dialog stored the body (typ %d obj %d)",
    rgTypSwiss[0], rgObjSwiss[0]);
  Check(rgPntSwiss[0] == 0,
    "and a name beside the number is not a run of point flags (nPnt %d)",
    rgPntSwiss[0]);
  Check(rgFlgSwiss[0] == 0,
    "nor of calculation flags (nFlg %d)", rgFlgSwiss[0]);

  // The glyph half: pointing row 1 at a new body dropped the old body's
  // glyph, the way -Ye and Object Selections do. This dialog was the
  // last path that kept it.
  Check(rgObjSwiss[1] == 52872,
    "row 1 took its new body (obj %d)", rgObjSwiss[1]);
  Check(szDrawObject[iobj1] != szDrawObjectDef[iobj1] &&
    ChCap(szDrawObject[iobj1][0]) == 'T',
    "and dropped the old body's glyph for the sentinel (\"%s\")",
    szDrawObject[iobj1]);

  // Put row 1 back, freeing what the dialog cloned.
  if (szDrawObject[iobj1] != szDrawObjectDef[iobj1]) {
    DeallocateP((char *)szDrawObject[iobj1]);
    szDrawObject[iobj1] = szDrawObjectDef[iobj1];
  }
  if (szDrawObject2[iobj1] != szDrawObjectDef2[iobj1]) {
    DeallocateP((char *)szDrawObject2[iobj1]);
    szDrawObject2[iobj1] = szDrawObjectDef2[iobj1];
  }
  rgTypSwiss[1] = nTyp1Sav; rgObjSwiss[1] = nObj1Sav;
  rgPntSwiss[1] = nPnt1Sav; rgFlgSwiss[1] = nFlg1Sav;
  rgTypSwiss[0] = nTypSav; rgObjSwiss[0] = nObjSav;
  rgPntSwiss[0] = nPntSav; rgFlgSwiss[0] = nFlgSav;
  CastChart(1);

  // The formatter and the parse are inverses, now that each exists once:
  // whatever SzObjDefFormat() writes, FObjDefParse() reads back as the
  // same four values. The definition types by turn, with and without a
  // point and flag suffix.
  {
    CONST OBJDEF rgodT[] = {
      {0, 120, 0, 0}, {1, 10199, 0, 0}, {2, oMar, 0, 0}, {3, 401, 0, 0},
      {4, 599, 0, 0}, {5, 1, 0, 0}, {1, 10199, 4, 0}, {1, 7066, 1, 33},
      {2, oVen, 2, 2}, {0, 120, 0, 63}};
    OBJDEF odT;
    char szT[cchSzMax];
    int iT;

    for (iT = 0; iT < (int)(sizeof(rgodT)/sizeof(OBJDEF)); iT++) {
      SzObjDefFormat(S(szT), &rgodT[iT]);
      Check(FObjDefParse(szT, &odT) && odT.nTyp == rgodT[iT].nTyp &&
        odT.nObj == rgodT[iT].nObj && odT.nPnt == rgodT[iT].nPnt &&
        odT.nFlg == rgodT[iT].nFlg,
        "\"%s\" round trips (%d %d %d %d -> %d %d %d %d)", szT,
        rgodT[iT].nTyp, rgodT[iT].nObj, rgodT[iT].nPnt, rgodT[iT].nFlg,
        odT.nTyp, odT.nObj, odT.nPnt, odT.nFlg);
    }
  }
  printf("  the one shared parse is what the Custom Objects dialog uses\n");
}


// ObjDefSet() is now the one way a definition reaches a slot, and the
// glyph rule rides inside it: identity is type, body and point -- a
// north node is not the planet -- while the calculation flags are not.
// Two edges the dialog tests cannot reach: re-asserting the definition a
// slot already has keeps its glyph (a deliberate change from -Ye's old
// unconditional drop), and changing only the point drops it.
static void TestObjDefSetQt()
{
  int iobj = custLo + 2;   // Hades: pristine under nrvate.as
  OBJDEF od, odSav;

  Group("Object definition store");

  ObjDefGet(iobj, &odSav);
  Check(szDrawObject[iobj] == szDrawObjectDef[iobj],
    "the slot starts out holding its own body's glyph");

  od = odSav;
  ObjDefSet(iobj, &od);
  Check(szDrawObject[iobj] == szDrawObjectDef[iobj],
    "re-asserting the same definition keeps the glyph");

  od.nFlg ^= 1;
  ObjDefSet(iobj, &od);
  Check(szDrawObject[iobj] == szDrawObjectDef[iobj],
    "and a calculation flag alone is not an identity change");

  od.nPnt = 1;
  ObjDefSet(iobj, &od);
  Check(rgPntSwiss[iobj - custLo] == 1, "the point was stored (%d)",
    rgPntSwiss[iobj - custLo]);
  Check(szDrawObject[iobj] != szDrawObjectDef[iobj] &&
    ChCap(szDrawObject[iobj][0]) == 'T',
    "but a point is not the body, so the glyph dropped (\"%s\")",
    szDrawObject[iobj]);

  // Put the slot back, freeing what the drop cloned.
  if (szDrawObject[iobj] != szDrawObjectDef[iobj]) {
    DeallocateP((char *)szDrawObject[iobj]);
    szDrawObject[iobj] = szDrawObjectDef[iobj];
  }
  if (szDrawObject2[iobj] != szDrawObjectDef2[iobj]) {
    DeallocateP((char *)szDrawObject2[iobj]);
    szDrawObject2[iobj] = szDrawObjectDef2[iobj];
  }
  ObjDefSet(iobj, &odSav);

  // And the display-name convention, through its own setter: renaming a
  // slot customises it, renaming it back to the stock text repoints at
  // the szObjName[] constant -- before SetObjDisp() that left a clone of
  // the stock name behind, which read as a rename forever after and
  // earned the slot a -YD line in every saved settings file.
  Check(!FObjDispCustom(iobj), "the slot starts un-renamed");
  SetObjDisp(iobj, "AstrologSuiteHades");
  Check(FObjDispCustom(iobj) && FEqSz(szObjDisp[iobj], "AstrologSuiteHades"),
    "renaming a slot customises it (\"%s\")", szObjDisp[iobj]);
  SetObjDisp(iobj, szObjName[iobj]);
  Check(!FObjDispCustom(iobj),
    "and renaming it back to the stock text un-customises it");

  CastChart(1);
  printf("  one function stores a definition, and owns the glyph rule\n");
}


static int s_cTickQt = 0;

// Does a queued timer fire while a modal dialog is up, and while a second
// modal is up inside the first? Three of the tests here depend on it.
static void TestTimerSanityQt()
{
  Group("Harness: queued timers");

  // A shot armed before a modal has to fire during its exec(), and one
  // armed inside that has to fire during a second modal opened from it.
  // Every dialog test here depends on both, and neither is obvious.
  s_cTickQt = 0;
  DriveModalQt(ShowCalcDialogQt, [](QWidget *pw) {
    QTimer t;
    t.setSingleShot(fTrue);
    QObject::connect(&t, &QTimer::timeout, []() { s_cTickQt++; });
    t.start(50 * nScaleTest);
    QMessageBox box(QMessageBox::Warning, "T", "nested", QMessageBox::Ok);
    QTimer tClose;
    tClose.setSingleShot(fTrue);
    QObject::connect(&tClose, &QTimer::timeout, [&box]() { box.close(); });
    tClose.start(300 * nScaleTest);
    box.exec();
    t.stop();
    tClose.stop();
    pw->close();
  });
  Check(s_cTickQt == 1,
    "a queued shot fires during a modal nested inside a modal (%d)",
    s_cTickQt);
  printf("  queued timers fire at both nesting levels\n");
}


static QString s_strLookupQt;

// Open Object Selections, do one thing to the first row, press OK.
static void DriveObjSelQt(int nWhat)
{
  s_strLookupQt = QString();
  DriveModalQt(ShowObjectSelDialogQt, [nWhat](QWidget *pw) {
    QList<QComboBox *> rgcb = pw->findChildren<QComboBox *>();
    QList<QLineEdit *> rgle, rgall = pw->findChildren<QLineEdit *>();
    QList<QPushButton *> rgb = pw->findChildren<QPushButton *>();
    int i, b;

    for (i = 0; i < rgall.size(); i++)
      if (qobject_cast<QComboBox *>(rgall[i]->parentWidget()) == NULL)
        rgle.append(rgall[i]);
    if (rgcb.isEmpty() || rgle.isEmpty()) {
      pw->close();
      return;
    }
    switch (nWhat) {
    case 0:                                   // pick a body from the list
      i = rgcb[0]->findText("Chiron");
      if (i >= 0)
        rgcb[0]->setCurrentIndex(i);
      break;
    case 1:                                   // a number, then Lookup Names
      rgcb[0]->setEditText("52872");
      for (b = 0; b < rgb.size(); b++)
        if (rgb[b]->text().contains("Lookup")) {
          rgb[b]->click();
          break;
        }
      s_strLookupQt = rgle[0]->text();
      break;
    case 2:                                   // a name the user typed
      rgcb[0]->setEditText("Chiron");
      rgle[0]->setText("AstrologSuiteName");
      break;
    case 3: {                                 // a later row, and un-shown
      QList<QCheckBox *> rgx = pw->findChildren<QCheckBox *>();
      if (rgcb.size() > 5)
        rgcb[5]->setEditText("2060");
      if (rgx.size() > 5)
        rgx[5]->setChecked(false);
      break;
    }
    case 4:                                   // a midpoint
      rgcb[0]->setEditText("Sun/Moo");
      break;
    case 5:                                   // changed, then cancelled
      rgcb[0]->setEditText("2060");
      for (b = 0; b < rgb.size(); b++)
        if (rgb[b]->text().contains("Cancel")) {
          rgb[b]->click();
          return;
        }
      break;
    case 6:                                   // nonsense
      rgcb[0]->setEditText("zznotabody");
      break;
    }
    for (b = 0; b < rgb.size(); b++)
      if (rgb[b]->text() == "OK") {
        rgb[b]->click();
        // OK refuses an unparseable row and leaves the dialog open, with
        // its warning already dismissed by the net in DriveModalQt.
        if (pw->isVisible())
          pw->close();
        return;
      }
    pw->close();
  });
}


// Choosing a body really has to change what the slot *says*, not only
// what it computes. See plan item 48.
static void TestObjSelDialogQt()
{
  int iobj = uranLo, nTypSav = rgTypSwiss[iobj - custLo];
  int nObjSav = rgObjSwiss[iobj - custLo];
  // Save the display name's text, not its pointer: the dialog's apply
  // frees the old clone through FCloneSzCore(), so a saved pointer is
  // dangling by case 1 -- and restoring it left the global aimed at freed
  // memory for the rest of the suite, read by every later redraw that
  // names this slot and freed a second time at exit. ASan pinned it; it
  // had been the suite's intermittent exit crash for some time, and the
  // glyph test below once had an independent copy of the same bug.
  char szDispSav[cchSzMax];
  flag fDispWasOwn = (szObjDisp[iobj] == szObjName[iobj]);
  sprintf2(S(szDispSav), "%s", szObjDisp[iobj]);

  Group("Object Selections dialog");

  DriveObjSelQt(0);
  Check(rgObjSwiss[iobj - custLo] == 2060,
    "picking a body from the list sets it (obj %d)",
    rgObjSwiss[iobj - custLo]);
  Check(FEqSz(szObjDisp[iobj], "Chiron"),
    "and the slot is named after it, not the body it used to be (%s)",
    szObjDisp[iobj]);

  DriveObjSelQt(1);
  Check(rgObjSwiss[iobj - custLo] == 52872,
    "a raw ephemeris number sets the body (obj %d)",
    rgObjSwiss[iobj - custLo]);
  Check(s_strLookupQt == QString("Okyrhoe"),
    "Lookup Names turns that number into a name (\"%s\")",
    s_strLookupQt.toLocal8Bit().constData());
  Check(FEqSz(szObjDisp[iobj], "Okyrhoe"), "which is what gets saved (%s)",
    szObjDisp[iobj]);

  DriveObjSelQt(2);
  Check(FEqSz(szObjDisp[iobj], "AstrologSuiteName"),
    "a name the user typed is kept, not overwritten (%s)", szObjDisp[iobj]);

  // Row 5, not row 0: an off-by-one in the row mapping would set the
  // wrong slot and nothing above would notice.
  int nRow5Sav = rgObjSwiss[uranLo + 5 - custLo], nRow4, nRow6;
  nRow4 = rgObjSwiss[uranLo + 4 - custLo];
  nRow6 = rgObjSwiss[uranLo + 6 - custLo];
  DriveObjSelQt(3);
  Check(rgObjSwiss[uranLo + 5 - custLo] == 2060,
    "a row other than the first sets that row (obj %d)",
    rgObjSwiss[uranLo + 5 - custLo]);
  Check(rgObjSwiss[uranLo + 4 - custLo] == nRow4 &&
    rgObjSwiss[uranLo + 6 - custLo] == nRow6,
    "and leaves its neighbours alone");
  Check(ignore[uranLo + 5] != 0, "the Show box drives the restriction");
  rgObjSwiss[uranLo + 5 - custLo] = nRow5Sav;

  // A midpoint has to rename the slot too, or it sits at the midpoint
  // under the name of the body it used to be.
  ClearB((pbyte)force.rgn, sizeof(force));
  DriveObjSelQt(4);
  Check(force[iobj] == ForceMid(oSun, oMoo),
    "a midpoint typed into the box is stored (%.1f)", force[iobj]);
  Check(FEqSz(szObjDisp[iobj], "Sun/Moo"),
    "and names the slot after its two halves (%s)", szObjDisp[iobj]);

  // Cancel discards everything.
  int nBeforeCancel = rgObjSwiss[iobj - custLo];
  DriveObjSelQt(5);
  Check(rgObjSwiss[iobj - custLo] == nBeforeCancel,
    "Cancel discards what was typed (%d)", rgObjSwiss[iobj - custLo]);

  // Nonsense is refused, and nothing is applied.
  // Only the behaviour is asserted, not that a warning widget appeared.
  // Not because queued checks are unreliable -- TestTimerSanityQt proves
  // they are not, at both nesting levels -- but because the warning is a
  // second modal opened from inside the first, and DriveModalQt's own net
  // closes it. Catching it would mean teaching the driver to distinguish
  // the two, for an assertion that adds nothing: that the settings
  // survive an unparseable entry is the part that matters, and it fails
  // if the guard is removed.
  DriveObjSelQt(6);
  Check(rgObjSwiss[iobj - custLo] == nBeforeCancel,
    "an unparseable definition applies nothing (%d)",
    rgObjSwiss[iobj - custLo]);

  rgTypSwiss[iobj - custLo] = nTypSav;
  rgObjSwiss[iobj - custLo] = nObjSav;
  if (fDispWasOwn) {
    if (szObjDisp[iobj] != szObjName[iobj])
      DeallocateP((char *)szObjDisp[iobj]);
    szObjDisp[iobj] = szObjName[iobj];
  } else
    FCloneSzCore(szDispSav, (char **)&szObjDisp[iobj],
      szObjDisp[iobj] == szObjName[iobj]);
  AdjustRestrictions();
  printf("  the dialog sets the body and names it\n");
}


static void TestObjSelTableQt()
{
  char szName[cchSzDef];
  int i, j, k, cCheck = 0, cWant;

  Group("Object selection table");
  Check(cObjSel > 0, "the body list is empty");
  for (i = 0; i < cObjSel; i++) {
    if (rgObjSel[i].nTyp <= 1)
      SwissGetObjName(S(szName),
        rgObjSel[i].nTyp <= 0 ? -rgObjSel[i].nObj : rgObjSel[i].nObj);
    else
      sprintf2(S(szName), "%s", FItem(rgObjSel[i].nObj) ?
        szObjName[rgObjSel[i].nObj] : szObjUnknown);
    if (FEqSz(szName, szObjUnknown))
      continue;                   // No ephemeris for it here; can't judge.
    cCheck++;
    // Accept a spelling difference only where Astrolog itself treats the
    // two as the same object. seorbel.txt writes the seventh Uranian
    // "Vulcanus" while szObjName[] writes it "Vulkanus", and data.cpp's
    // own name table carries both -- so ask that table rather than
    // hardcoding the pair, and a genuinely wrong number still fails
    // because the two names then resolve to different objects, or to none.
    j = NParseSz(rgObjSel[i].szName, pmObject);
    k = NParseSz(szName, pmObject);
    Check(FMatchSz(rgObjSel[i].szName, szName) ||
      FMatchSz(szName, rgObjSel[i].szName) ||
      (FItem(j) && FItem(k) && j == k),
      "list says \"%s\" for type %d index %d, ephemeris says \"%s\"",
      rgObjSel[i].szName, rgObjSel[i].nTyp, rgObjSel[i].nObj, szName);
  }
  // Exactly, not a floor. Two reasons, both measured 2026-09-02. A floor
  // tests the guess: 11 of these bodies resolve with no ephemeris files at
  // all -- "-Yi1" pointed at a directory that does not exist still answers
  // for 11 of them from the Moshier formulas -- so "cCheck > 0" passes on a
  // run that found nothing. And every body that fails to resolve skips its
  // own assertion above, silently, so this number is also the count of
  // assertions the loop actually ran: 83 passed on /swe, 63 on ephem/, 53
  // on nothing, with no failure to show for the difference. Asserting it is
  // the only thing standing between a thinner ephemeris and a green run
  // that tested less.
  cWant = FEphemMinimalQt() ? cObjSelEphemMinimal : cObjSel;
  Check(cCheck == cWant,
    "%d of %d bodies resolved; ASTROLOG_QT_EPHEM=%s expects exactly %d. "
    "Fewer means the ephemeris is thinner than the mode claims, and this "
    "group ran %d assertions where it should have run %d; more means the "
    "mode is stale",
    cCheck, cObjSel, FEphemMinimalQt() ? "minimal" : "full", cWant,
    cCheck, cWant);
  printf("  %d of %d bodies resolved and matched their listed name\n",
    cCheck, cObjSel);
}


// The Object Selections dialog's fields go through three shared helpers in
// calc.cpp, which both this build and the Windows one call. The dialog
// itself is modal and can only be driven by clicking, so what is worth
// pinning down in a suite is those helpers: a field accepts either a name
// from the offered list or the definition text Object Customization uses,
// and comes back as the same body either way.
// The four values back as separate ints, which is what these assertions
// read most clearly. FObjSelParse() itself takes one OBJDEF now, so that
// four ints in a row cannot be handed over in the wrong order.
static flag FObjSelParseTestQt(CONST char *sz, int *pnTyp, int *pnObj,
  int *pnPnt, int *pnFlg)
{
  OBJDEF od;
  flag f = FObjSelParse(sz, &od);

  *pnTyp = od.nTyp; *pnObj = od.nObj;
  *pnPnt = od.nPnt; *pnFlg = od.nFlg;
  return f;
}


static void TestObjSelParseQt()
{
  char sz[cchSzMax];
  int nTyp, nObj, nPnt, nFlg, nTypSav, nObjSav, nPntSav, nFlgSav;

  Group("Object selection fields");

  // A name from the list, and the bare number, are the same body.
  Check(FObjSelParseTestQt("Nessus", &nTyp, &nObj, &nPnt, &nFlg) &&
    nTyp == 1 && nObj == 7066 && nPnt == 0 && nFlg == 0,
    "\"Nessus\" did not read as asteroid 7066");
  Check(FObjSelParseTestQt("7066", &nTyp, &nObj, &nPnt, &nFlg) &&
    nTyp == 1 && nObj == 7066,
    "\"7066\" did not read as asteroid 7066");
  Check(FObjSelParseTestQt("nessus", &nTyp, &nObj, &nPnt, &nFlg) && nObj == 7066,
    "the list match is case sensitive when it shouldn't be");

  // Definition forms other than a plain asteroid number.
  Check(FObjSelParseTestQt("h5", &nTyp, &nObj, &nPnt, &nFlg) &&
    nTyp == 0 && nObj == 5, "\"h5\" did not read as element set 5");
  Check(FObjSelParseTestQt("Ven", &nTyp, &nObj, &nPnt, &nFlg) &&
    nTyp == 2 && nObj == oVen, "\"Ven\" did not read as Venus");

  // The guard this parse exists to keep. Without it the trailing letters
  // of an all alphabetic definition are read as point and flag suffixes,
  // so "Ven" sets the north node off its own 'n' and the chart quietly
  // shows Venus's node instead of Venus.
  Check(nPnt == 0 && nFlg == 0,
    "\"Ven\" read its own letters as a point/flag suffix (pnt %d flg %d)",
    nPnt, nFlg);
  Check(FObjSelParseTestQt("Mar", &nTyp, &nObj, &nPnt, &nFlg) && nPnt == 0,
    "\"Mar\" read its own letters as a suffix");

  // A real suffix still parses.
  Check(FObjSelParseTestQt("7066 nH", &nTyp, &nObj, &nPnt, &nFlg) &&
    nTyp == 1 && nObj == 7066 && nPnt == 1 && (nFlg & 1),
    "\"7066 nH\" lost its point or flag suffix");

  Check(!FObjSelParseTestQt("", &nTyp, &nObj, &nPnt, &nFlg),
    "an empty field was accepted");

  // And the formatting side round trips: set a slot, read it back.
  nTypSav = rgTypSwiss[uranLo - custLo];
  nObjSav = rgObjSwiss[uranLo - custLo];
  nPntSav = rgPntSwiss[uranLo - custLo];
  nFlgSav = rgFlgSwiss[uranLo - custLo];

  rgTypSwiss[uranLo - custLo] = 1; rgObjSwiss[uranLo - custLo] = 7066;
  rgPntSwiss[uranLo - custLo] = rgFlgSwiss[uranLo - custLo] = 0;
  SzObjSelDef(S(sz), uranLo);
  Check(FEqSz(sz, "Nessus"),
    "a slot holding 7066 showed as \"%s\", not the list name", sz);

  // With a suffix it must show the raw definition instead, or OK would
  // silently strip the suffix off the slot.
  rgPntSwiss[uranLo - custLo] = 1;
  SzObjSelDef(S(sz), uranLo);
  Check(FEqSz(sz, "7066 n"),
    "a slot with a point suffix showed as \"%s\"", sz);
  Check(FObjSelParseTestQt(sz, &nTyp, &nObj, &nPnt, &nFlg) &&
    nTyp == 1 && nObj == 7066 && nPnt == 1,
    "the suffixed form did not read back to the same slot");

  rgTypSwiss[uranLo - custLo] = nTypSav;
  rgObjSwiss[uranLo - custLo] = nObjSav;
  rgPntSwiss[uranLo - custLo] = nPntSav;
  rgFlgSwiss[uranLo - custLo] = nFlgSav;
  printf("  body fields read as names, as definitions, and with suffixes\n");
}


// Forced object positions have to survive being written to a settings file
// and read back. FOutputSettings() had no "-F"/"-Fm" section at all, so
// File / Save Program Settings silently dropped every forced position --
// including ones set by hand in a user's own astrolog.as, which is how it
// was found. The write loop covers every object rather than any narrower
// range on purpose: a forced position can sit on anything from 0 to cObj,
// and this asserts an out-of-range one is not lost, since that is the
// failure that would destroy someone's configuration rather than annoy
// them. Remove the io.cpp block and the first two checks here fail.
// Two bugs in shared upstream code, neither of them Qt specific -- both
// files have no "ifdef QT" in them at all -- and both of the kind that
// produce a plausible number rather than an obvious failure.
// A GUI casts the same relationship chart repeatedly; the console builds
// cast once and exit. charts2.cpp was written for the latter and only
// excepted WIN, so this build took the console path while behaving like a
// GUI -- see plan item 39.
static QString s_strCombo;
static QString s_strComboWin;

// Capture the ephemeris dropdown's contents from the Calculation Settings
// dialog, then close it. The dialog blocks in exec(), so as everywhere
// else here the inspection has to be queued before it opens.
static QString StrEphemListQt()
{
  s_strCombo = QString();
  s_strComboWin = QString();
  DriveModalQt(ShowCalcDialogQt, [](QWidget *pw) {
    s_strComboWin = pw->windowTitle();
    QList<QComboBox *> rg = pw->findChildren<QComboBox *>();
    for (int i = 0; i < rg.size(); i++) {
      QStringList items;
      for (int j = 0; j < rg[i]->count(); j++)
        items << rg[i]->itemText(j);
      if (items.join(",").contains("Swiss")) {
        s_strCombo = items.join(" | ");
        break;
      }
    }
    pw->close();
  });
  return s_strCombo;
}


// Windows leaves an ephemeris out of this list when the user has switched
// it off; see plan item 41. The maintainer's own settings file sets both
// restrictions, so this is the list they actually get.
static int s_cRowList;
static QString s_strRow0;

// Open the chart list, press Filter, and report what the list holds.
static void FilterChartListQt()
{
  s_cRowList = -1;
  s_strRow0 = QString();
  DriveModalQt(ShowChartListDialogQt, [](QWidget *pw) {
    QList<QPushButton *> rgb = pw->findChildren<QPushButton *>();
    for (int b = 0; b < rgb.size(); b++)
      if (rgb[b]->text().contains("Filter") &&
        !rgb[b]->text().contains("Remove")) {
        rgb[b]->click();
        break;
      }
    QList<QListWidget *> rgl = pw->findChildren<QListWidget *>();
    if (rgl.size() > 0) {
      s_cRowList = rgl[0]->count();
      if (s_cRowList > 0)
        s_strRow0 = rgl[0]->item(0)->text();
    }
    pw->close();
  });
}


// Windows' DlgList narrows the chart list by AstroExpression as well as by
// name and location; this one did not. See plan item 42.
// Windows fires the redraw notification hook at the end of its redraw;
// the X11 path fires it from a block that excludes both GUI builds, so
// this one never did. See plan item 43.
// The accelerator column is drawn from astrolog.rc's own text, not from
// Qt's rendering of the key sequence; see plan item 44. Every label in the
// generated table has to still name a real menu item, or the column
// silently goes missing for it.
// Evaluate an AstroExpression and return what it left in @z.
static int NExpEvalQt(CONST char *sz)
{
  char szT[cchSzMax];

  ExpSetN(iLetterZ, -999);
  sprintf2(S(szT), "=z %s", sz);
  ParseExpression(szT);
  return NExpGet(iLetterZ);
}


// The twelve expression functions that were "#ifdef WIN" only, plus a
// "QT" to sit beside "WIN" and "X11"; see plan item 46.
static void TestExpressionFunctionsQt()
{
  int nDelaySav = NAnimDelayQt();
  flag fPopupSav = FNoPopupQt();

  Group("AstroExpression functions");
  Check(NExpEvalQt("QT") == 1, "an expression can tell it is the Qt build");
  Check(NExpEvalQt("WIN") == 0, "and that it is not Windows");
  Check(NExpEvalQt("X11") == 0, "and not X11 either");

  // Read live settings back, not constants: set one and see it change.
  SetAnimDelayQt(137);
  Check(NExpEvalQt("_WN") == 137, "_WN reads the animation delay (%d)",
    NExpEvalQt("_WN"));
  SetAnimDelayQt(42);
  Check(NExpEvalQt("_WN") == 42, "and follows it when it changes (%d)",
    NExpEvalQt("_WN"));
  SetAnimDelayQt(nDelaySav);

  SetNoPopupQt(fTrue);
  Check(NExpEvalQt("_Wt") == 1, "_Wt reads the no-popup setting");
  SetNoPopupQt(fFalse);
  Check(NExpEvalQt("_Wt") == 0, "and follows it when it changes");
  SetNoPopupQt(fPopupSav);

  // Autosave and the screen saver have no counterpart in this build, so
  // they report off rather than pretending to a setting that isn't there.
  Check(NExpEvalQt("_Wo") == 0 && NExpEvalQt("_Wo0") == 0 &&
    NExpEvalQt("_Wo3") == 0, "autosave reports off, having no counterpart");
  Check(NExpEvalQt("_WZ") == 0, "and so does the screen saver");
  printf("  the Windows-only expression functions answer here too\n");
}


static void TestAccelTextQt()
{
  int i, j, cFound = 0, cShown = 0, cWant = 0;
  QSet<QAction *> rgpaClaimed;
  QAction *pa;

  Group("Accelerator column");
  for (i = 0; i < caccelQt; i++) {
    // Loose, the way the parity test looks items up: this port puts the
    // mnemonic on a different letter than the resource in a few places,
    // which is not a gap. Items the port deliberately doesn't implement
    // are flagged in rgparityQt[] and expected to be absent here too.
    CONST char *szTop;
    flag fSkip = fFalse;
    for (j = 0; j < cparityQt; j++)
      if (rgparityQt[j].fSkip &&
        FEqSz(rgparityQt[j].szLabel, rgaccelQt[i].szLabel)) {
        fSkip = fTrue;
        break;
      }
    if (fSkip)
      continue;
    cWant++;
    pa = PaFindLooseTestQt(rgaccelQt[i].szLabel, &szTop);
    if (pa == NULL)
      continue;
    cFound++;
    // The resource names one command from two menus with different labels
    // and different accelerators -- "Open &Documentation\tCtrl+h" in Help
    // and "&Open Documentation\t)" in its More Documentation submenu --
    // and this port has the first. The apply pass leaves an item alone
    // once it carries a column, so mirror that here rather than counting
    // the second entry as a miss.
    if (rgpaClaimed.contains(pa))
      continue;
    rgpaClaimed.insert(pa);
    if (pa->text().section(QChar('\t'), 1, 1) == QString(rgaccelQt[i].szAccel))
      cShown++;
  }
  Check(cFound == cWant,
    "every label in the accelerator table names a real menu item (%d of %d)",
    cFound, cWant);
  Check(cShown == (int)rgpaClaimed.size(),
    "and each shows the resource's own text (%d of %d)",
    cShown, (int)rgpaClaimed.size());
  // The notation is the whole point: Windows writes "V" for Shift+V and
  // "Alt+O" for Alt+Shift+O, capitalising a letter to mean Shift, where
  // Qt would spell both modifiers out.
  pa = PaFindActionTestQt("Standard Radi&x");
  Check(pa != NULL && pa->text().endsWith(QChar('\t') + QString("V")),
    "Standard Radix shows \"V\", not \"Shift+V\"");
  Check(pa != NULL && !pa->shortcuts().isEmpty(),
    "and still has its shortcut bound");
  printf("  %d menu items show Windows' accelerator text\n", cShown);
}


static void TestExpressionHooksQt()
{
  char *szSav = us.szExpDisp3;

  Group("AstroExpression hooks");
  us.szExpDisp3 = SzClone("=z 4242");
  ExpSetN(iLetterZ, 0);
  RedrawQt();
  Check(NExpGet(iLetterZ) == 4242,
    "the redraw notification hook fires (@z is %d)", NExpGet(iLetterZ));

  // And is not fired when expressions are switched off wholesale.
  us.fExpOff = fTrue;
  ExpSetN(iLetterZ, 0);
  RedrawQt();
  Check(NExpGet(iLetterZ) == 0, "and not when -~0 has turned them off");
  us.fExpOff = fFalse;

  us.szExpDisp3 = szSav;

  // "-~WQ": Windows applies this to the command id before dispatching, so
  // an expression can veto a menu command or swap it for another. This
  // port had no command id at dispatch until ConnectMenuQt(); see item 45.
  char *szMenuSav = us.szExpMenu;
  QAction *paGrid = PaFindActionTestQt("Aspect Midpoint &Grid");
  int nModeSav = gi.nMode;

  Check(paGrid != NULL, "the Aspect Midpoint Grid item is there to fire");
  if (paGrid != NULL) {
    SetChartModeQt(gWheel);
    us.szExpMenu = SzClone("=z 0");        // veto whatever was chosen
    paGrid->trigger();
    Check(gi.nMode == gWheel,
      "a menu command an expression returns 0 for does not run (mode %d)",
      gi.nMode);

    us.szExpMenu = SzClone("=z 40057");    // cmdChartWheel
    paGrid->trigger();
    Check(gi.nMode == gHouse,
      "and one swapped for another command runs that one (mode %d)",
      gi.nMode);

    us.szExpMenu = NULL;
    paGrid->trigger();
    Check(gi.nMode == gGrid,
      "with no expression the command runs as itself (mode %d)", gi.nMode);
  }
  us.szExpMenu = szMenuSav;
  SetChartModeQt(nModeSav);
  printf("  the redraw and menu command hooks both fire\n");
}


static void TestChartListFilterQt()
{
  int cciSav = is.cci, i;
  char *szSav = us.szExpListF;

  Group("Chart list filter");
  // CI.nam is a pointer, not a buffer: after "ciCore = ciMain" it aims at
  // the name string cloned from the settings file, which for nrvate.as is
  // a one-byte "". The sprintf that used to be here wrote twenty bytes
  // through it -- a heap smash ASan pinned after it had spent the evening
  // crashing the suite intermittently at whatever unlucky spot the
  // corrupted neighbour was freed. Static buffers, because
  // FAppendCIList() copies the struct shallowly and the list keeps the
  // pointers.
  static char rgszNamT[3][cchSzDef];
  for (i = 0; i < 3; i++) {
    ciCore = ciMain; ciCore.yea = 1990 + i;
    sprintf2(S(rgszNamT[i]), "AstrologSuiteChart%d", i);
    ciCore.nam = rgszNamT[i];
    FAppendCIList(&ciCore);
  }
  Check(is.cci >= cciSav + 3, "three charts went into the list");

  us.szExpListF = SzClone("1");     // keep everything
  FilterChartListQt();
  Check(s_cRowList == 3, "an expression that keeps everything keeps 3 (got %d)",
    s_cRowList);

  us.szExpListF = SzClone("0");     // keep nothing
  FilterChartListQt();
  Check(s_cRowList == 1 && s_strRow0.contains("No charts"),
    "an expression that keeps nothing empties the list (got %d rows, \"%s\")",
    s_cRowList, s_strRow0.toLocal8Bit().constData());

  us.szExpListF = szSav;
  is.cci = cciSav;
  printf("  the chart list honours its AstroExpression filter\n");
}


static void TestEphemerisListQt()
{
  flag fNetSav = us.fNoNetwork, fPlaSav = us.fNoPlacalc;
  QString str;

  Group("Ephemeris list");

  us.fNoNetwork = us.fNoPlacalc = fTrue;
  str = StrEphemListQt();
  Check(!str.isEmpty(), "the ephemeris list was found at all (modal seen: \"%s\")",
    s_strComboWin.toLocal8Bit().constData());
  Check(!str.contains("Web"),
    "no web query offered when web queries are off: %s",
    str.toLocal8Bit().constData());
  Check(!str.contains("Placalc") && !str.contains("Matrix"),
    "no Placalc or Matrix offered when those are off: %s",
    str.toLocal8Bit().constData());
  Check(str.contains("Swiss"), "Swiss Ephemeris is still offered");

  us.fNoNetwork = us.fNoPlacalc = fFalse;
  str = StrEphemListQt();
  Check(str.contains("Web"), "the web query is offered when allowed");
  Check(str.contains("Placalc") && str.contains("Matrix"),
    "Placalc and Matrix are offered when allowed");

  us.fNoNetwork = fNetSav; us.fNoPlacalc = fPlaSav;
  printf("  the ephemeris list omits what the user switched off\n");
}


static void TestRelationshipModeQt()
{
  CI ciMainSav = ciMain, ciTwinSav = ciTwin, ciSaveSav = ciSave, ciOrig;
  int nRelSav = us.nRel, k;

  Group("Relationship chart modes");

  ciTwin = ciMain;
  ciTwin.yea = ciMain.yea - 10;
  ciOrig = ciMain;

  SetRelQt(rcMidpoint);
  Check(us.nRel == rcMidpoint,
    "midpoint mode survives its own cast (us.nRel is %d)", us.nRel);
  Check(ciMain.yea != ciOrig.yea, "and the chart actually moved to the midpoint");

  // Every recast must take the midpoint of the chart as *loaded*. Taking
  // it of the previous midpoint instead walks the chart toward the twin a
  // little further on each redraw -- 2021, 2019, 2017, 2016 -- which a
  // user sees as the chart changing every time the window is resized.
  CI ciMid = ciMain;
  for (k = 0; k < 3; k++)
    RecastAndRedrawQt();
  Check(ciMain.yea == ciMid.yea && ciMain.mon == ciMid.mon &&
    ciMain.day == ciMid.day,
    "a midpoint chart does not drift when redrawn (%d/%d/%d vs %d/%d/%d)",
    ciMain.mon, ciMain.day, ciMain.yea, ciMid.mon, ciMid.day, ciMid.yea);

  // Leaving midpoint mode puts the loaded chart back, which SetRelQt()
  // can only do while us.nRel still says it is in midpoint mode.
  SetRelQt(rcNone);
  Check(ciMain.yea == ciOrig.yea && ciMain.mon == ciOrig.mon &&
    ciMain.day == ciOrig.day,
    "leaving midpoint mode restores the loaded chart (%d/%d/%d)",
    ciMain.mon, ciMain.day, ciMain.yea);

  // "No Relationship Chart" and "Comparison Chart" are one shared toggle
  // on Windows -- SetRel(us.nRel ? rcNone : rcDual), wdriver.cpp:1571 --
  // which is why astrolog.rc gives both the same "c" accelerator, and why
  // 'c' is the only way out of transit mode from the keyboard. Wired as
  // two fixed modes instead, 'c' set comparison forever and a user who
  // pressed Alt+Shift+N could never get back to a single chart.
  // Confirmed against the Windows build under Wine: from transit mode 'c'
  // renders pixel-identical to the single chart it started from.
  QAction *paComp = PaFindActionTestQt("Com&parison Chart");
  QAction *paNo = PaFindActionTestQt("No &Relationship Chart");
  Check(paComp != NULL && paNo != NULL,
    "both halves of the comparison toggle are on the menu");
  if (paComp != NULL && paNo != NULL) {
    SetRelQt(rcTransit);
    Check(us.nRel == rcTransit, "Alt+Shift+N reaches transit mode (%d)",
      us.nRel);
    paComp->trigger();
    Check(us.nRel == rcNone,
      "'c' leaves transit mode for a single chart (%d)", us.nRel);
    paComp->trigger();
    Check(us.nRel == rcDual, "'c' again turns comparison on (%d)", us.nRel);
    paComp->trigger();
    Check(us.nRel == rcNone, "and 'c' again turns it off (%d)", us.nRel);

    // Either item drives the same toggle, so this one leaves every mode
    // too -- including the ones with no key of their own.
    SetRelQt(rcProgress);
    paNo->trigger();
    Check(us.nRel == rcNone,
      "\"No Relationship Chart\" leaves progressed mode (%d)", us.nRel);
    SetRelQt(rcSynastry);
    paComp->trigger();
    Check(us.nRel == rcNone, "'c' leaves synastry mode (%d)", us.nRel);

    // A mode that is not part of the toggle still sets outright.
    paNo->trigger();
    Check(us.nRel == rcDual, "from a single chart the toggle turns on (%d)",
      us.nRel);
    SetRelQt(rcSynastry);
    Check(us.nRel == rcSynastry, "synastry still sets outright (%d)",
      us.nRel);
  }

  // Windows' CmdFromRc() bullets Comparison for every multi-wheel mode,
  // not just rcDual (wdriver.cpp:251), and the Charts #3 Through #6
  // dialog reaches all four. Matching rc exactly finds no item for those
  // and leaves the bullet wherever it was.
  if (paComp != NULL) {
    int rgrc[] = {rcTriWheel, rcQuadWheel, rcQuinWheel, rcHexaWheel};
    for (k = 0; k < 4; k++) {
      SetRelQt(rcSynastry);
      SetRelQt(rgrc[k]);
      Check(paComp->isChecked(),
        "multi-wheel mode %d bullets Comparison", rgrc[k]);
    }
  }

  us.nRel = nRelSav;
  ciMain = ciMainSav; ciTwin = ciTwinSav; ciSave = ciSaveSav;
  ciCore = ciMain;
  CastChart(1);
  printf("  relationship modes persist, restore, and do not drift\n");
  printf("  the comparison toggle leaves any mode, as Windows does\n");
}


static void TestSharedCoreFixesQt()
{
  real rgforceSav[objMax], rMid;
  flag rgignoreSav[objMax];
  int xWinSav = gs.xWin, yWinSav = gs.yWin, nModeSav = gi.nMode, i;
  int nDwadSav, objOnAscSav;
  real rHarmonicSav;
  flag fTextSav = gs.fText, fDoSidebarSav = gs.fDoSidebar;
  flag fFlipSav, fDecanSav, fNavamsaSav, fExpOffSav;
  char szLine[cchSzMax];

  Group("Shared core fixes");
  for (i = 0; i < objMax; i++) {
    rgforceSav[i] = force[i];
    rgignoreSav[i] = ignore[i];
  }

  // Work log item 114: PrintHeader() and PrintWheelCenter() format the
  // chart's name and location -- arbitrary user strings, and the atlas
  // itself produces location names near 60 characters -- through what
  // were 80-byte buffers. A saved chart of a long-named city crashed
  // every text chart under fortify (found by exporting a real eclipse
  // chart of "Washington, Washington West, District of Columbia Co.,
  // DC" with seconds on). Drive both functions with strings past every
  // old bound; surviving IS the assertion, the way TestBadInputQt()
  // treats a crash.
  {
    static char szLongNam[121], szLongLoc[121];
    char szOut[cchSzMax];
    CI ciMainSav = ciMain, ciCoreSav = ciCore;
    flag fSecSav = us.fSeconds, fWheelSav = us.fWheel;
    flag fListSav = us.fListing, fGraphSav = us.fGraphics;
    FILE *file;
    long cb = 0;

    for (i = 0; i < 120; i++) {
      szLongNam[i] = 'N';
      szLongLoc[i] = 'L';
    }
    ciMain.nam = szLongNam; ciMain.loc = szLongLoc;
    ciCore = ciMain;
    us.fSeconds = fTrue;
    sprintf2(S(szOut), "%s/astrolog-qt-longloc-%d.txt",
      QDir::tempPath().toLocal8Bit().constData(),
      (int)QCoreApplication::applicationPid());
    FCloneSz(szOut, &is.szFileScreen);
    us.fGraphics = fFalse;
    us.fListing = fTrue; us.fWheel = fFalse;
    Action();                       // PrintHeader() path (-v listing).
    us.fListing = fFalse; us.fWheel = fTrue;
    Action();                       // PrintWheelCenter() path (-w wheel).
    us.fWheel = fWheelSav; us.fListing = fListSav;
    us.fGraphics = fGraphSav;
    FCloneSz(NULL, &is.szFileScreen);
    us.fSeconds = fSecSav;
    ciMain = ciMainSav; ciCore = ciCoreSav;
    file = fopen(szOut, "rb");
    if (file != NULL) {
      fseek(file, 0, SEEK_END);
      cb = ftell(file);
      fclose(file);
      remove(szOut);
    }
    Check(file != NULL && cb > 500,
      "a 120-character name and location survive the text charts "
      "(%ld bytes)", cb);
    CastChart(1);                   // Leave real positions for the rest.
  }

  // Work log item 145: the three loops that expand "\\A"-style escapes
  // walked their destination with no end check at all, so a format string
  // longer than the buffer smashed the stack -- reachable, and fatal in
  // the release build, from two documented switches:
  //   astrolog -YYt <3000 chars>                 (PrintSzFormat)
  //   astrolog -YXt <3000 chars> -Xv 6 -Xo x.bmp (the sidebar)
  // FormatSz() is the same walk with an inspectable destination, so it
  // carries the length assertion; the other two assert by surviving, the
  // way TestBadInputQt() does. Reintroduce any of the three bounds and
  // this group aborts rather than printing FAIL -- that is the test
  // working, not the test broken.
  {
    static char szHuge[3001];
    char szSmall[100];
    char *szSideSav = gs.szSidebar;
    int nModeSav = gi.nMode, nFillSav = gs.nDecaFill;

    for (i = 0; i < 3000; i++)
      szHuge[i] = 'Y';
    szHuge[3000] = chNull;

    FormatSz(szHuge, S(szSmall));
    Check(CchSz(szSmall) < (int)sizeof(szSmall),
      "a 3000-character format stays inside a 100-byte destination (%d)",
      CchSz(szSmall));

    // PrintSzFormat() is deliberately NOT called here. It ends in
    // PrintSz(), which writes to is.S -- a FILE* that only Action() opens
    // and closes -- so calling it from inside the suite puts characters
    // into a stream that is not open, and glibc frees a backup buffer it
    // never allocated. That is a heap corruption in the TEST, not in the
    // code under test, and it cost an hour of bisecting a "regression"
    // that was the regression test. Its switch, "-YYt", is checked as a
    // separate process in run-qt-tests.sh's startup diagnostics instead,
    // which is where a crash reachable from the command line belongs.

    gs.szSidebar = szHuge;
    gs.nDecaFill = 6;
    SetChartModeQt(gWheel);
    Check(gi.qim != NULL,
      "a 3000-character sidebar renders instead of smashing the stack");
    gs.szSidebar = szSideSav;
    gs.nDecaFill = nFillSav;
    SetChartModeQt(nModeSav);
  }

  // Work log item 93: a slot forced to a midpoint draws its NAME in
  // place of a glyph only when it was also renamed. A forced slot that
  // keeps its name is that body computed by another formula -- the
  // maintainer's own config does -Fm on Fortune to redefine the Part
  // of Fortune as the Sun/Moon midpoint, and the glyph must stay.
  {
    real forceFor = force[oFor];
    CONST char *dispSav = szObjDisp[oFor];
    static char szRenamedT[] = "Sun/Moo";
    force[oFor] = ForceMid(oSun, oMoo);
    szObjDisp[oFor] = szObjName[oFor];
    Check(!FDrawObjectAsName(oFor),
      "forced but un-renamed slot keeps its glyph");
    szObjDisp[oFor] = szRenamedT;
    Check(FDrawObjectAsName(oFor),
      "forced and renamed slot draws its name");
    force[oFor] = 0.0;
    Check(!FDrawObjectAsName(oFor),
      "renamed but unforced slot keeps its glyph");
    force[oFor] = forceFor;
    szObjDisp[oFor] = dispSav;
  }

  // gs.xWin includes the sidebar; FOutputSettings() writes ":Xw" without
  // it and says so in the comment beside the value. Reading it back has
  // to add it on again, and did not -- so save, reload, save, reload
  // walked the window down 240 pixels a cycle. Mirror exactly what
  // io.cpp writes, rather than writing a file, and require the value to
  // come back where it started.
  gi.nMode = gWheel; gs.fText = fTrue; gs.fDoSidebar = fTrue;
  gs.yWin = 1260;
  gs.xWin = 1260 + ((SIDESIZE * gi.nScaleText) >> 1);
  i = gs.xWin; if (fSidebar) i -= (SIDESIZE * gi.nScaleText) >> 1;
  sprintf2(S(szLine), ":Xw %d %d", i, gs.yWin);
  FProcessCommandLine(szLine);
  Check(gs.xWin == 1260 + ((SIDESIZE * gi.nScaleText) >> 1),
    "a saved window width reloads to the width it was saved at");

  // A forced midpoint is computed in CastChart() from planet[] of its two
  // sources, and ComputeEphem() skips any restricted object above the
  // Moon -- so a midpoint of two restricted objects was built from
  // whatever those slots last held. Zero them first: without the fix they
  // stay zero and the midpoint is confidently wrong, with it they are
  // computed and hidden. Sun and Moon would prove nothing here, being
  // below the point ComputeEphem() starts skipping at.
  //
  // Half a dozen switches rewrite planet[] *after* the forces are applied
  // -- domal, decan, dwad, navamsa, solar rotation, object expressions --
  // and any of them leaves the midpoint relation untrue of the final
  // positions. They are off in a default chart, but this suite shares
  // live us/gs state with everything that ran before it, so say so rather
  // than inherit it: with -4 left on by an earlier test, the forced slot
  // came back exactly 30 degrees from the midpoint and the failure looked
  // like the fix not working.
  fFlipSav = us.fFlip; fDecanSav = us.fDecan; nDwadSav = us.nDwad;
  fNavamsaSav = us.fNavamsa; objOnAscSav = us.objOnAsc;
  rHarmonicSav = us.rHarmonic; fExpOffSav = us.fExpOff;
  us.fFlip = us.fDecan = us.fNavamsa = fFalse;
  us.nDwad = 0; us.objOnAsc = 0; us.rHarmonic = 1.0; us.fExpOff = fTrue;

  ClearB((pbyte)force.rgn, sizeof(force));
  ignore[oJup] = ignore[oSat] = fTrue;
  force[oFor] = ForceMid(oJup, oSat);             // -Fm 19 6 7
  planet[oJup] = planet[oSat] = 0.0;
  AdjustRestrictions();
  CastChart(1);
  rMid = Midpoint(planet[oJup], planet[oSat]);
  Check(planet[oJup] != 0.0 && planet[oSat] != 0.0,
    "a restricted object a forced midpoint reads is still computed");
  Check(planet[oFor] == rMid && rMid != 0.0,
    "a midpoint of two restricted objects is their real midpoint");
  Check(ignore[oJup] && ignore[oSat],
    "and computing them did not un-restrict them");

  us.fFlip = fFlipSav; us.fDecan = fDecanSav; us.nDwad = nDwadSav;
  us.fNavamsa = fNavamsaSav; us.objOnAsc = objOnAscSav;
  us.rHarmonic = rHarmonicSav; us.fExpOff = fExpOffSav;

  for (i = 0; i < objMax; i++) {
    force[i] = rgforceSav[i];
    ignore[i] = rgignoreSav[i];
  }
  gs.xWin = xWinSav; gs.yWin = yWinSav; gi.nMode = nModeSav;
  gs.fText = fTextSav; gs.fDoSidebar = fDoSidebarSav;
  AdjustRestrictions();
  CastChart(1);
  printf("  window size round trips, forced midpoints read real positions\n");
}


// The rulership tables come in mirrored pairs for each of the three
// systems: sign-keyed (a sign's ruler and co-ruler) and object-keyed (an
// object's ruled and co-ruled sign). The two directions spell "none"
// differently -- -1 sign-keyed, where 0 is a real object, the Earth; 0
// object-keyed -- and work log item 38 is what one forgotten difference
// cost: an esoteric block tested a sign-keyed table the object-keyed way
// and used -1 as an array index. Pin the encodings, and pin the shipped
// defaults agreeing with themselves: every ruler a sign names must name
// that sign back. Only that direction -- ruler1[] gives minor objects
// sign affinities the sign tables never record, and AdjustRulership()
// is deliberately lossy when -YJ moves a planet off a sign that has no
// co-ruler to promote -- so this is a claim about the defaults, made
// where nothing has customized them.
static void TestRulershipTablesQt()
{
  static CONST struct {
    CONST char *szSystem;
    CONST TBLSIG *rgsign1, *rgsign2;
    CONST TBLOBJ *rgobj1, *rgobj2;
  } rgfam[] = {
    {"traditional",  &rules,      &rules2,     &ruler1,    &ruler2},
    {"esoteric",     &rgSignEso1, &rgSignEso2, &rgObjEso1, &rgObjEso2},
    {"hierarchical", &rgSignHie1, &rgSignHie2, &rgObjHie1, &rgObjHie2}};
  flag fEnc, fRul, fCorul;
  int ifam, i, k;

  Group("Rulership tables");
  for (ifam = 0; ifam < (int)(sizeof(rgfam)/sizeof(*rgfam)); ifam++) {
    fEnc = fRul = fCorul = fTrue;
    for (i = 1; i <= cSign; i++) {
      k = (*rgfam[ifam].rgsign1)[SIGT(i)];
      fEnc &= FBetween(k, 0, oNorm);        // Every sign has a ruler.
      if (FBetween(k, 0, oNorm))
        fRul &= ((*rgfam[ifam].rgobj1)[OBJT(k)] == i ||
          (*rgfam[ifam].rgobj2)[OBJT(k)] == i);
      k = (*rgfam[ifam].rgsign2)[SIGT(i)];
      fEnc &= (k == -1 || FBetween(k, 0, oNorm));
      if (FBetween(k, 0, oNorm))
        fCorul &= ((*rgfam[ifam].rgobj1)[OBJT(k)] == i ||
          (*rgfam[ifam].rgobj2)[OBJT(k)] == i);
    }
    for (i = 0; i <= oNorm; i++)
      fEnc &= FBetween((*rgfam[ifam].rgobj1)[OBJT(i)], 0, cSign) &&
        FBetween((*rgfam[ifam].rgobj2)[OBJT(i)], 0, cSign);
    Check(fEnc, "%s tables: none is -1 sign-keyed and 0 object-keyed",
      rgfam[ifam].szSystem);
    Check(fRul, "every sign's %s ruler rules it back", rgfam[ifam].szSystem);
    Check(fCorul, "every sign's %s co-ruler co-rules it back",
      rgfam[ifam].szSystem);
  }
  printf("  three systems' sign-keyed and object-keyed tables agree\n");
}


// T2 step (1) continued (REFACTORING.md): the same machine-checked-
// encoding treatment for the esoteric tables the rulership group does
// not cover. exalt[] is object-keyed, so none is 0 and every value is a
// sign; rgObjRay[] maps objects to a single ray or 0; rgSignRay[] is a
// decimal digit-string of rays (456 = rays 4, 5 and 6), from which
// EnsureRay() derives rgSignRay2[] rows whose nonzero entries are the
// per-ray proportions -- every row must total 420, the base the ray
// charts divide by. The last case is the regression for a real crasher
// this group found on its first survey: -Y7C range-checks the composed
// number rather than its digits, so a list with no valid digit reached
// EnsureRay() as c=0 and "-Y7C 1 1 8 8 -7" died on 420/0.
static void TestEsotericTablesQt()
{
  flag fOk;
  int i, j, c, n, nSav;

  Group("Esoteric tables");

  fOk = fTrue;
  for (i = 0; i <= oNorm; i++)
    fOk &= FBetween(exalt[OBJT(i)], 0, cSign);
  Check(fOk, "every exaltation is a sign, with none spelled 0");

  fOk = fTrue;
  for (i = 0; i <= oNorm; i++)
    fOk &= FBetween(rgObjRay[OBJT(i)], 0, cRay);
  Check(fOk, "every object's ray is 1..%d, with none spelled 0", cRay);

  fOk = fTrue;
  for (i = 1; i <= cSign; i++) {
    c = 0;
    for (n = rgSignRay[SIGT(i)]; n; n /= 10) {
      fOk &= FBetween(n % 10, 1, cRay);
      c++;
    }
    fOk &= (c >= 1);
  }
  Check(fOk, "every sign's ray list has only valid digits, at least one");

  EnsureRay();
  fOk = fTrue;
  for (i = 1; i <= cSign; i++) {
    c = 0;
    for (j = 1; j <= cRay; j++)
      c += rgSignRay2[SIGT(i)][j];
    fOk &= (c == 420);
  }
  Check(fOk, "every sign's derived ray proportions total 420");

  // The crasher: an all-invalid ray list must derive to an all-zero row,
  // not divide by zero.
  nSav = rgSignRay[SIGT(1)];
  rgSignRay[SIGT(1)] = 8;
  EnsureRay();
  c = 0;
  for (j = 1; j <= cRay; j++)
    c += rgSignRay2[SIGT(1)][j];
  Check(c == 0, "a ray list with no valid digits derives to no rays (%d)",
    c);
  rgSignRay[SIGT(1)] = nSav;
  EnsureRay();

  printf("  exaltations and ray tables carry their encodings\n");
}


// A switch file can include another with -i, and switches like -YY read
// an in-band payload from the file being parsed -- through the global
// is.fileIn once (whose clear-on-exit bug this test caught), and now
// through the PARSECTX each file parser passes down its own stack.
// Regression either way: an outer file with an include, then an atlas
// payload, then one more switch, must load to the end with all three
// applied, and load again the same way.
static void TestNestedIncludeQt()
{
  char szInner[cchSzMax], szOuter[cchSzMax];
  // The QByteArray has to outlive the pointer into it. toLocal8Bit()
  // returns a temporary, so "szTmp = QDir::tempPath().toLocal8Bit()
  // .constData()" leaves szTmp dangling at the semicolon -- which Linux
  // survived, reading bytes nothing had reused yet, and macOS did not:
  // SIGSEGV in this group, in the first run that got far enough to
  // reach it. Every other site passes the expression straight to
  // sprintf2(), where the temporary lives to the end of the call.
  QByteArray baTmp = QDir::tempPath().toLocal8Bit();
  CONST char *szTmp = baTmp.constData();
  FILE *file;
  int nScrollSav = us.nScrollRow;
  flag fRet, fPopupSav = FNoPopupQt();

  Group("Nested include");
  SetNoPopupQt(fTrue);    // a failing load must fail, not open a box
  sprintf2(S(szInner), "%s/astrolog-qt-nest-inner-%d.as", szTmp,
    (int)QCoreApplication::applicationPid());
  sprintf2(S(szOuter), "%s/astrolog-qt-nest-outer-%d.as", szTmp,
    (int)QCoreApplication::applicationPid());
  file = fopen(szInner, "w");
  fprintf(file, "@AD800  ; inner\n-YQ 41\n");
  fclose(file);
  file = fopen(szOuter, "w");
  fprintf(file, "@AD800  ; outer\n-i \"%s\"\n-YY 1\n"
    "0.0\t0.0\tUS\tNowhere\tAfrica/Abidjan\n-YQ 47\n", szInner);
  fclose(file);

  us.nScrollRow = 24;
  fRet = FProcessSwitchFile(szOuter, NULL);
  Check(fRet, "a switch file with a nested include loads to its end");
  Check(is.cae == 1 && is.rgae != NULL,
    "the payload switch after the include read its payload");
  Check(us.nScrollRow == 47,
    "a switch after the payload still applied (got %d)", us.nScrollRow);
  Check(FProcessSwitchFile(szOuter, NULL),
    "and the same file loads a second time cleanly");

  // The payload replaced the atlas with one synthetic city; hand the
  // real one back to FEnsureAtlas()'s lazy load.
  if (is.rgae != NULL) {
    DeallocateP(is.rgae);
    is.rgae = NULL;
  }
  is.cae = 0;
  us.nScrollRow = nScrollSav;
  SetNoPopupQt(fPopupSav);
  remove(szInner); remove(szOuter);
  printf("  an include inside a settings file hands the channel back\n");
}


// The switch registry's structural invariants. Every switch spelling
// in the program resolves through three tables scanned in order, with
// prefix rows matching any spelling they begin. Two mistakes are easy
// to make when adding rows and were each made once during the M1-M10
// migration before review caught them: a duplicate spelling (the first
// row silently wins), and a prefix row placed where it shadows an
// exact spelling scanned later (-XE registered exact once made "-XE1"
// unknown; the reverse ordering would silently reroute it). These
// checks make both structural.
static void TestRegistryQt()
{
  CONST char *rgsz[400], *pch1, *pch2;
  int rggrf[400], rgtab[400], csw = 0, cPrefix = 0, i, j;
  flag fOk;

  Group("Switch registry");
  while (csw < 400 && FSwitchRegistryRow(csw, &rgsz[csw], &rggrf[csw],
    &rgtab[csw]))
    csw++;
  Check(csw >= 240 && csw < 400,
    "the registry enumerates a plausible row count (%d)", csw);

  // Spellings are unique across all three tables.
  fOk = fTrue;
  for (i = 0; i < csw; i++)
    for (j = i+1; j < csw; j++)
      if (FEqSz(rgsz[i], rgsz[j])) {
        fOk = fFalse;
        printf("  duplicate spelling \"%s\" (rows %d and %d)\n",
          rgsz[i], i, j);
      }
  Check(fOk, "every spelling appears exactly once");

  // A prefix row (handler table only) must scan after any row whose
  // exact spelling it would otherwise swallow, and must not begin any
  // later prefix row's spelling either.
  fOk = fTrue;
  for (i = 0; i < csw; i++) {
    if (!(rgtab[i] == 2 && (rggrf[i] & 1)))    // grfSwPrefix
      continue;
    cPrefix++;
    for (j = i+1; j < csw; j++) {
      for (pch1 = rgsz[i], pch2 = rgsz[j]; *pch1 && *pch1 == *pch2;
        pch1++, pch2++)
        ;
      if (*pch1 == chNull && rgsz[i][0] != chNull) {
        fOk = fFalse;
        printf("  prefix row \"%s\" shadows later row \"%s\"\n",
          rgsz[i], rgsz[j]);
      }
    }
  }
  Check(fOk, "no prefix row shadows a row scanned after it");
  Check(cPrefix >= 30, "the prefix rows enumerated (%d)", cPrefix);

  // Exactly one empty spelling: the day-arithmetic row for a lone
  // prefix character.
  for (i = j = 0; i < csw; i++)
    if (rgsz[i][0] == chNull)
      j++;
  Check(j == 1, "exactly one empty spelling, the day-arithmetic row");

  // Every spelling the running binary's own -H text documents must also
  // resolve. That check is not here: it lives in
  // tools/registry_audit.py, which parses the help source directly.
  printf("  %d rows, %d of them prefix rows, all invariants hold\n",
    csw, cPrefix);
}


static void TestForcedPositionsQt()
{
  real rgforceSav[objMax];
  char *szFileOutSav = is.szFileOut;
  int nWriteFormatSav = us.nWriteFormat, i;
  flag fNoWriteSav = us.fNoWrite, fFoundMacro = fFalse;
  char szPath[cchSzMax], szLine[cchSzMax], szMid[cchSzMax], szPos[cchSzMax];
  FILE *file;

  Group("Forced object positions");
  for (i = 0; i < objMax; i++)
    rgforceSav[i] = force[i];

  // oFor (19) stands in for a forced position outside any one dialog's
  // range; uranLo (34) for one inside it. Both must come back.
  ClearB((pbyte)force.rgn, sizeof(force));
  force[oFor] = ForceMid(1, 2);                   // -Fm 19 1 2
  force[uranLo] = ForcePos(ZD(1, 15.25));         // -F 34 Ari 15.25

  // A renamed macro menu entry goes in the same file. The Qt build stores
  // -WM through NProcessSwitchesQt() but the block that writes it back was
  // #ifdef WIN, so a settings file saved here silently lost every macro
  // name -- 13 of them in the config this was found with -- while the
  // Windows build kept them. Save what this build can load.
  sprintf2(S(szLine), "-WM 1 \"AstrologQtSuiteMacro\"");
  FProcessCommandLine(szLine);

  sprintf2(S(szPath), "%s/astrolog-qt-force-test-%d.as", QDir::tempPath().toLocal8Bit().constData(), (int)QCoreApplication::applicationPid());
  us.fNoWrite = fFalse;
  us.nWriteFormat = 'd';
  is.szFileOut = szPath;
  Check(FOutputSettings(), "FOutputSettings() wrote a settings file");

  szMid[0] = szPos[0] = chNull;
  file = FileOpen(szPath, 3, NULL, 0);
  if (file != NULL) {
    while (fgets(szLine, cchSzMax, file) != NULL) {
      for (i = 0; szLine[i]; i++)          // Keep the line, minus its \n.
        ;
      while (i > 0 && szLine[i-1] < ' ')
        szLine[--i] = chNull;
      // All three comparisons happen AFTER that strip. The macro one used
      // to run before it and carry a literal "\n" in the pattern, which
      // matches on a platform whose line terminator is one byte and never
      // matches on Windows, where fgets hands back "...\"\r\n". It cost
      // one of the 15 failures in the first Windows run of this suite.
      if (FEqSz(szLine, "-WM 1 \"AstrologQtSuiteMacro\""))
        fFoundMacro = fTrue;
      if (FEqSz(szLine, "-Fm 19 1 2"))
        CopyRgb((pbyte)szLine, (pbyte)szMid, i+1);
      else if (FEqSz(szLine, "-F 34 Ari 15.25"))
        CopyRgb((pbyte)szLine, (pbyte)szPos, i+1);
    }
    fclose(file);
  }
  Check(szMid[0] != chNull, "an out-of-range forced midpoint survived the save");
  Check(fFoundMacro, "a renamed macro menu entry survived the save");
  Check(szPos[0] != chNull,
    "a forced zodiac position survived the save, to the digit");

  // Now feed those exact lines back through the switch parser. This is the
  // half that proves the written *form* reads: the sign is abbreviated and
  // the degrees go through FormatR(), so either could be written in a shape
  // that looks right and parses wrong. Only the two lines are replayed, not
  // the whole settings file -- the suite shares live us/gs state, and
  // reading a full settings file back would apply several hundred settings
  // to the running program.
  ClearB((pbyte)force.rgn, sizeof(force));
  if (szMid[0] != chNull)
    FProcessCommandLine(szMid);
  if (szPos[0] != chNull)
    FProcessCommandLine(szPos);
  Check(force[oFor] == ForceMid(1, 2),
    "the saved midpoint parsed back to the same encoding");
  Check(force[uranLo] == ForcePos(ZD(1, 15.25)),
    "the saved zodiac position parsed back to the same value");

  // The encoding's edges, through the helpers that now own it: the
  // largest legal pair is where an objMax off-by-one would show, and
  // 0 Aries is the value that collides with "no force" if the rDegMax
  // bias is ever lost.
  force[oFor] = ForceMid(cObj, cObj);
  Check(FForceMid(force[oFor]) && ObjForceMid1(force[oFor]) == cObj &&
    ObjForceMid2(force[oFor]) == cObj,
    "the largest legal midpoint pair unpacks to itself (%d/%d)",
    ObjForceMid1(force[oFor]), ObjForceMid2(force[oFor]));
  force[oFor] = ForcePos(0.0);
  Check(FForcePos(force[oFor]) && RForcePos(force[oFor]) == 0.0,
    "0 Aries is a forced position, not \"no force\"");

  remove(szPath);
  for (i = 0; i < objMax; i++)
    force[i] = rgforceSav[i];
  is.szFileOut = szFileOutSav;
  us.nWriteFormat = nWriteFormatSav;
  us.fNoWrite = fNoWriteSav;
  printf("  forced positions round trip through a settings file\n");
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

// Graphics charts to PNG with no display, the counterpart of
// TextChartCaptureQt() below. SetChartModeQt() already renders each mode
// into gi.qim -- that is what TestChartRenderQt() checks -- so this is
// that loop plus a save.
//
//   QTGRAPHDIR=out/qtg ./run-qt-tests.sh
static void GraphicsChartCaptureQt(CONST char *szDir)
{
  CONST int rgnMode[] = { gWheel, gHouse, gGrid, gMidpoint,
    gHorizon, gOrbit, gSector, gCalendar, gDisposit, gEsoteric,
    gAstroGraph, gEphemeris, gRising, gLocal, gMoons, gExo,
    gTraTraGra, gTraNatGra, gSphere, gWorldMap, gGlobe, gPolar,
    gTelescope, gBiorhythm };
  CONST char *rgszFile[] = { "wheel", "house", "grid", "midpoint",
    "horizon", "orbit", "sector", "calendar", "influence", "esoteric",
    "astrograph", "ephemeris", "rising", "local", "moons", "exo",
    "tratragra", "tranatgra", "sphere", "worldmap", "globe", "polar",
    "telescope", "biorhythm" };
  int i, cmode = (int)(sizeof(rgnMode) / sizeof(int)), nSav = gi.nMode;
  flag fSav = us.fGraphics, fPopupSav;
  QElapsedTimer tim;
  qint64 msDraw, msSave;

  // gAspect and gArabic are absent on purpose: DrawChartX() has no case
  // for either, which is why Windows forces text mode for exactly those
  // two (item 24).
  if (!QDir().mkpath(QString(szDir))) {
    printf("cannot create %s\n", szDir);
    return;
  }
  printf("capturing %d graphics charts to %s\n", cmode, szDir);
  // Any PrintWarning() here becomes a modal QMessageBox and blocks for a
  // click that is never coming -- gMoons wants moon ephemeris files that
  // aren't installed, and hangs the whole capture on the warning. Same
  // hazard TestBadInputQt() guards against, same escape.
  fPopupSav = FNoPopupQt();
  SetNoPopupQt(fTrue);
  us.fGraphics = fTrue;
  for (i = 0; i < cmode; i++) {
    tim.start();
    SetChartModeQt(rgnMode[i]);
    msDraw = tim.elapsed();
    if (gi.qim == NULL) {
      printf("  %-12s nothing rendered\n", rgszFile[i]);
      continue;
    }
    tim.start();
    QString str = QString("%1/%2.png").arg(szDir).arg(rgszFile[i]);
    flag fOk = gi.qim->save(str);
    msSave = tim.elapsed();
    printf("  %-12s draw %5lldms  save %4lldms  %s\n", rgszFile[i],
      (long long)msDraw, (long long)msSave, fOk ? "" : "WRITE FAILED");
    fflush(stdout);
  }
  us.fGraphics = fSav;
  SetNoPopupQt(fPopupSav);
  SetChartModeQt(nSav);
}


static void TextChartCaptureQt(CONST char *szDir)
{
  CONST char *rgszAct[] = { "Standard Radi&x", "House &Wheel",
    "Aspect Midpoint &Grid", "&Calendar", "Inf&luence", "&Ephemeris",
    "&Aspect List", "&Midpoint List" };
  CONST char *rgszFile[] = { "radix", "wheel", "grid", "calendar",
    "influence", "ephemeris", "aspectlist", "midpointlist" };
  int i, cchart = (int)(sizeof(rgszAct) / sizeof(char *));

  // The chart tools/text-chart-capture.sh leaves the Windows build on:
  // Nov 19 1971 11:01am, ST Zone 8W, no name or location string (a name
  // makes the header wrap to a second line, charts1.cpp:91).
  //
  // The location has to be the *exact* one astrolog.as carries, seconds
  // and all -- "-zl 122W19'59 47N36'35". Rounding it to whole minutes,
  // which is all the header displays, leaves the planets looking right
  // while every house cusp sits one to two arcminutes off, which then
  // reads as a calculation divergence between the two builds.
  ciCore.mon = 11; ciCore.day = 19; ciCore.yea = 1971;
  ciCore.tim = 11.0 + 1.0/60.0; ciCore.dst = 0.0; ciCore.zon = 8.0;
  ciCore.lon = 122.0 + 19.0/60.0 + 59.0/3600.0;
  ciCore.lat = 47.0 + 36.0/60.0 + 35.0/3600.0;
  // Point at empty strings rather than writing NULs through the shared
  // clones ciMain also holds -- same pointer-not-buffer trap as the chart
  // list filter test, in its harmless-looking form.
  static char szNamT[1], szLocT[1];
  ciCore.nam = szNamT; ciCore.loc = szLocT;
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

// Scratch probe. This is the fast way to answer a question about what the
// program does, and it is deliberately disposable: rewrite the body, build,
// run, read the answer, rewrite it again. Seconds per iteration, because
// there is no window, no display, no input simulation and no waiting.
//
//   ASTROLOG_QT_PROBE=1 QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= \
//     ./astrolog-qt-test
//
// Everything the program has is in scope: poke us/gs/gi directly, call
// SetChartModeQt() or any dialog function, then save gi.qim and measure it
// with PIL rather than looking at it. See QT_TESTING.md.
//
// Nothing here is a test. Do not add assertions; put those in the suite.
static void ProbeQt()
{
  printf("gi.nMode=%d (gWheel=%d gHouse=%d)\n", gi.nMode, gWheel, gHouse);
  printf("us.nHouseSystem=%d (%s)  fEphemFiles=%d\n",
    us.nHouseSystem, szSystem[us.nHouseSystem], us.fEphemFiles);
}



// ---- The numeric oracle ----
//
// Every other net in this project is differential. tools/switch-matrix.sh
// byte-diffs the tree against an older build of ITSELF; tools/win-tests.sh
// and the text-chart diff compare two builds that share this same core;
// the sanitizer sweeps prove no bad memory access, not a right answer. All
// of those can prove "unchanged". None of them can prove "correct", and a
// differential actively locks a wrong answer in -- fixing a defect that
// shipped in 1993 reads as a regression. Before this group the entire
// suite contained exactly two assertions about a computed number, both
// house cusps on the Matrix path (see TestCastCookingQt).
//
// So: ask the ephemeris library the same question Astrolog asks it, and
// require the same answer. The Astrolog-object -> Swiss-body mapping below
// is written out here on purpose rather than read from calc.cpp, so this
// is an independent transcription and a drift in that mapping fails.
//
// What that actually tests is Astrolog's glue, not Swiss's arithmetic:
// object numbering, the flag construction in FSwissPlanet(), the delta-T
// shift, the sidereal offset, and ProcessPlanet()'s rectangular-to-zodiac
// conversion. That glue is where every calculation bug this project has
// found actually lived -- the star-numbered rulership tables, the missing
// FNorm guards, the raw-rObjDiam eclipse checkers.
//
// Measured 2026-08-31, and the numbers are why the tolerances look the way
// they do:
//   * Swiss agreement is EXACT -- 0.000000 arcsec, 15 bodies, 7 epochs
//     1900-2080, tropical and sidereal alike. rEpsSwiss is slack against
//     future compiler reassociation, not a fudge factor.
//   * Matrix-vs-Swiss worst case over the same epochs: Sun-Mars 0.010 deg,
//     Jupiter-Neptune 0.255, Pluto 0.867, Chiron/Ceres/Pallas 2.33, Juno
//     8.17, Vesta 11.01. The per-body tolerances carry about 2x headroom.
//     This leg is catastrophe detection: it is what would have caught the
//     all-zero chart that -bm produced for years (work log item 139).
//   * All 40 house systems partition the circle exactly once at mid
//     latitude: 12 positive gaps summing to 360 to within 1e-9.

typedef struct _OracleBody {
  int obj;         // Astrolog object index
  int se;          // Swiss Ephemeris body number
  real rTolMat;    // Matrix-engine tolerance in degrees, measured x2
} ORACLEBODY;

static CONST ORACLEBODY rgoracle[] = {
  {oSun, SE_SUN,      0.05}, {oMoo, SE_MOON,    0.15},
  {oMer, SE_MERCURY,  0.05}, {oVen, SE_VENUS,   0.05},
  {oMar, SE_MARS,     0.05}, {oJup, SE_JUPITER, 0.50},
  {oSat, SE_SATURN,   0.50}, {oUra, SE_URANUS,  0.50},
  {oNep, SE_NEPTUNE,  0.50}, {oPlu, SE_PLUTO,   1.75},
  {oChi, SE_CHIRON,   5.00}, {oCer, SE_CERES,   5.00},
  {oPal, SE_PALLAS,   5.00}, {oJun, SE_JUNO,   17.00},
  {oVes, SE_VESTA,   22.00}};
#define coracle (int)(sizeof(rgoracle) / sizeof(ORACLEBODY))

// Slack against compiler reassociation, 3.6e-6 arcsec. The measurement it
// stands in for was exact equality.
#define rEpsSwiss 1.0e-9

// Pin the chart the way TestCastCookingQt does, so the suite's own clock
// never enters, and borrow every cast-relevant knob: TestAllMenuActionsQt
// fires all 338 menu items and leaves a pile of them dirty.
static void OraclePinChartQt(int yea)
{
  ciCore = ciMain;
  ciCore.mon = 3; ciCore.day = 15; ciCore.yea = yea;
  ciCore.tim = 10.5; ciCore.dst = 0.0; ciCore.zon = 6.0;
  ciCore.lon = 87.65; ciCore.lat = 41.85;
  ciCore.nam = ciCore.loc = NULL;
}

// The same, at an exact UT moment rather than a pinned local date: the
// eclipse leg gets its times from the library in UT and has to hand them
// back unshifted, so zone and DST are zero and the location is the one
// thing that cannot matter to a global eclipse.

static void OraclePinUtQt(int yea, int mon, int day, real tim)
{
  ciCore = ciMain;
  ciCore.mon = mon; ciCore.day = day; ciCore.yea = yea;
  ciCore.tim = tim; ciCore.dst = 0.0; ciCore.zon = 0.0;
  ciCore.lon = 0.0; ciCore.lat = 0.0;
  ciCore.nam = ciCore.loc = NULL;
}

static void TestNumericOracleQt()
{
  static CONST int rgyea[] = {1900, 1940, 1980, 2000, 2020, 2050, 2080};
  flag fPopupSav = FNoPopupQt();
  CI ciCoreSav = ciCore, ciMainSav = ciMain;
  flag rgfIgnoreSav[objMax];
  real rgrSwiss[coracle], rD;
  double xx[6];
  char serr[AS_MAXCH];
  real jd;
  int iy, i, cGood;

  Group("Numeric oracle");
  SetNoPopupQt(fTrue);
#ifndef SWISS
  Check(fFalse, "built without SWISS: the oracle cannot run");
#else
  for (i = 0; i < objMax; i++)
    rgfIgnoreSav[i] = ignore[i];
  {
    // The same borrow list TestCastCookingQt's pinned-cusp check uses,
    // plus the backend, since this group is about which engine answers.
    Borrow bEphem(us.fEphemFiles, fTrue), bSid(us.fSidereal, fFalse);
    Borrow bPla(us.fPlacalcPla, fFalse), bMat(us.fMatrixPla, fFalse);
    Borrow bSwiss(us.nSwissEph, 0), bNoPla(us.fNoPlacalc, fFalse);
    Borrow b3D(us.fHouse3D, fFalse), bProg(us.fProgress, fFalse);
    Borrow bEqu(us.fEquator, fFalse), bEqu2(us.fEquator2, fFalse);
    Borrow bFlip(us.fFlip, fFalse), bGeo(us.fGeodetic, fFalse);
    Borrow bRotW(us.fObjRotWhole, fFalse), bExp(us.fExpOff, fTrue);
    Borrow bCtr(us.objCenter, (int)oEar), bRel(us.nRel, (int)rcNone);
    Borrow bZoff(us.rZodiacOffset, 0.0), bZall(us.rZodiacOffsetAll, 0.0);
    Borrow bCusp(us.rCuspAddition, 0.0);
    Borrow bTopo(us.fTopoPos, fFalse), bTrue(us.fTruePos, fFalse);
    Borrow bNut(us.fNoNutation, fFalse);
    Borrow bBary(us.fBarycenter, fFalse), bHel(us.fHouseAngle, fFalse);
    Borrow bAsc(us.objOnAsc, 0), bRot1(us.objRot1, 0), bRot2(us.objRot2, 0);
    // CastChart() rewrites every position AGAIN after ComputeEphem():
    // harmonic, decan, dwad and navamsa each map planet[] through a
    // function of itself, so a stale one silently rescrambles the sky.
    // TestAllMenuActionsQt() fires all 338 menu items and leaves all
    // four set -- which is how this group passed alone and failed 222
    // assertions in the full run. TestSharedCoreFixesQt() clears the
    // same four by hand for the same reason.
    Borrow bHarm(us.rHarmonic, 1.0);
    Borrow bDec(us.fDecan, fFalse), bNav(us.fNavamsa, fFalse);
    Borrow bDwad(us.nDwad, 0);

    for (i = 0; i < coracle; i++)
      ignore[rgoracle[i].obj] = fFalse;

    // ---- Leg 1: Astrolog's Swiss path IS the library's own answer ----
    for (iy = 0; iy < 7; iy++) {
      OraclePinChartQt(rgyea[iy]);
      CastChart(1);
      jd = JulianDayFromTime(is.T);
      for (i = 0; i < coracle; i++) {
        if (swe_calc_ut(jd, rgoracle[i].se, SEFLG_SWIEPH | SEFLG_SPEED,
          xx, serr) < 0) {
          Check(fFalse, "%d %s: Swiss Ephemeris refused (%s)", rgyea[iy],
            szObjName[rgoracle[i].obj], serr);
          rgrSwiss[i] = rLarge;
          continue;
        }
        rgrSwiss[i] = xx[0];
        rD = RAbs(planet[rgoracle[i].obj] - rgrSwiss[i]);
        if (rD > rDegHalf)
          rD = rDegMax - rD;
        Check(rD < rEpsSwiss, "%d %s matches swe_calc_ut (%.6f\")",
          rgyea[iy], szObjName[rgoracle[i].obj], rD * 3600.0);
      }
    }

    // ---- Leg 2: the sidereal offset is applied once, not twice ----
    // is.rSid is added in ProcessPlanet() and SEFLG_SIDEREAL subtracts the
    // ayanamsa inside the library, which reads like a double application
    // and measured as exact agreement instead. Pin that.
    {
      Borrow bSid2(us.fSidereal, fTrue);
      OraclePinChartQt(2020);
      CastChart(1);
      jd = JulianDayFromTime(is.T);
      swe_set_sid_mode(SE_SIDM_FAGAN_BRADLEY, 0.0, 0.0);
      Check(is.rSid != 0.0, "a sidereal cast has a nonzero offset (%.6f)",
        is.rSid);
      for (i = 0; i < coracle; i++) {
        if (swe_calc_ut(jd, rgoracle[i].se,
          SEFLG_SWIEPH | SEFLG_SPEED | SEFLG_SIDEREAL, xx, serr) < 0)
          continue;
        rD = RAbs(planet[rgoracle[i].obj] - xx[0]);
        if (rD > rDegHalf)
          rD = rDegMax - rD;
        Check(rD < rEpsSwiss, "sidereal %s matches the library (%.6f\")",
          szObjName[rgoracle[i].obj], rD * 3600.0);
      }
    }

    // ---- Leg 3: the Matrix engine computes the same sky ----
    // Two independent implementations of the solar system. This is the leg
    // that fails loudly if a backend stops computing: an all-zero chart
    // puts every body up to 180 degrees from the truth.
    {
      Borrow bEph2(us.fEphemFiles, fFalse), bMat2(us.fMatrixPla, fTrue);
      for (iy = 0; iy < 7; iy++) {
        OraclePinChartQt(rgyea[iy]);
        {
          Borrow bEph3(us.fEphemFiles, fTrue), bMat3(us.fMatrixPla, fFalse);
          CastChart(1);
          jd = JulianDayFromTime(is.T);
          for (i = 0; i < coracle; i++)
            rgrSwiss[i] = swe_calc_ut(jd, rgoracle[i].se,
              SEFLG_SWIEPH | SEFLG_SPEED, xx, serr) < 0 ? rLarge : xx[0];
        }
        CastChart(1);
        for (i = 0; i < coracle; i++) {
          if (rgrSwiss[i] == rLarge)
            continue;
          rD = RAbs(planet[rgoracle[i].obj] - rgrSwiss[i]);
          if (rD > rDegHalf)
            rD = rDegMax - rD;
          Check(rD < rgoracle[i].rTolMat,
            "%d Matrix %s within %.2f deg of Swiss (%.4f)", rgyea[iy],
            szObjName[rgoracle[i].obj], rgoracle[i].rTolMat, rD);
        }
      }
    }

    // ---- Leg 5: coincident points are zero degrees apart, not NaN ----
    // SphDistance() feeds acos an expression that is sin^2+cos^2 when the
    // two points coincide -- exactly 1.0 in arithmetic, and above it for
    // 3.75% of latitudes in double precision, where acos returns NaN. Two
    // objects sharing a position are ordinary (a tight conjunction, or two
    // slots both left at 0.0), and the NaN reached ChartMidpoint()'s span
    // total and then SzDegree(), where (int)NaN is INT_MIN and "%3d" wrote
    // past a 15-byte buffer. That is the intermittent abort of work log
    // items 133 and 142, and it is why this leg sweeps rather than spot
    // checks: the failing latitudes are scattered a few ULP apart.
    {
      int cNan = 0;
      real rLat, rD, rMax = 0.0;

      for (rLat = -89.9; rLat <= 89.9; rLat += 0.0007) {
        rD = SphDistance(123.456, rLat, 123.456, rLat);
        if (rD != rD)                     // The only portable NaN test.
          cNan++;
        else if (rD > rMax)
          rMax = rD;
      }
      Check(cNan == 0,
        "coincident points never give NaN over 256858 latitudes (%d did)",
        cNan);
      // Not exactly zero, and that is arithmetic rather than a defect: the
      // spherical law of cosines resolves small distances no finer than
      // acos(1-eps) ~ sqrt(2*eps), about 1.2e-6 degrees here. Measured
      // worst case 2026-08-31 was 1.7e-06; the bound is an order above it.
      // Switching to haversine would fix the precision and change every
      // distance the program prints, so it is not on the table.
      Check(rMax < 1.0e-5,
        "coincident points are zero degrees apart to arithmetic (%.3g)",
        rMax);
      rD = SphDistance(0.0, 0.0, 180.0, 0.0);
      Check(RAbs(rD - rDegHalf) < 1.0e-9,
        "antipodal points are 180 degrees apart (%.9f)", rD);
      rD = SphDistance(0.0, -90.0, 0.0, 90.0);
      Check(RAbs(rD - rDegHalf) < 1.0e-9,
        "pole to pole is 180 degrees (%.9f)", rD);
    }

    // ---- Leg 4: every house system partitions the circle exactly once ----
    // SwissHouse() says "largely copied from swe_houses()" and 40 systems
    // read the result. Whatever a system's construction, its twelve cusps
    // must go around once: every gap positive, the gaps summing to 360.
    for (i = 0; i < cSystem; i++) {
      Borrow bHouse(us.nHouseSystem, i);
      real rSum = 0.0, rGap;
      int iCusp, cBad = 0;

      OraclePinChartQt(2020);
      CastChart(1);
      for (iCusp = 1; iCusp <= cSign; iCusp++) {
        rGap = chouse[iCusp == cSign ? 1 : iCusp+1] - chouse[iCusp];
        if (rGap < 0.0)
          rGap += rDegMax;
        if (rGap <= 0.0)
          cBad++;
        rSum += rGap;
      }
      Check(cBad == 0, "%s houses all have positive width", szSystem[i]);
      Check(RAbs(rSum - rDegMax) < 1.0e-9,
        "%s houses close the circle once (%.9f)", szSystem[i], rSum);
    }

    // ---- Leg 4b: the same invariant by latitude and by ENGINE ----
    // Leg 4 runs at one mid latitude on whichever engine is configured,
    // which is why seven systems degenerating toward the pole went
    // unnoticed. There are two engines -- SwissHouse() when
    // us.fEphemFiles && !us.fPlacalcPla, Astrolog's own ComputeHouses()
    // otherwise -- and ComputeHouses() guards exactly two systems
    // (calc.cpp:508, Placidus and Koch fall back to Porphyry).
    //
    // This asserts the partition for every combination EXCEPT the ones
    // measured as degenerate on 2026-09-01, and separately asserts that
    // set is exactly what it was: a system that starts failing shows up,
    // and a system that gets fixed shows up too. The table is the record
    // of a real defect in shared core, not an excuse for it -- see work
    // log item 157 for the measurement and the maintainer decision it
    // is waiting on.
    {
      // The seven systems measured as degenerating toward the pole. No
      // latitude threshold is stored with them on purpose: the latitude
      // at which each first fails moves with the date and longitude (a
      // first attempt pinned thresholds from one sample and they were
      // wrong within the hour), so what is pinned is the SET.
      // Two, not the seven item 157 measured. The five that WRAPPED --
      // Topocentric, Campanus, Regiomontanus, APC, Savard-A -- fall back
      // to Porphyry now (item 158). These two remain because their
      // failure is a zero-width house whose gaps still sum to 360, which
      // Pullen (S.Delta) collapses on purpose when a quadrant is under
      // 30 degrees wide; that is its author's degenerate case, not a
      // broken partition, so the guard leaves it alone.
      static CONST int rgDegen[] = { hsSineDelta };
      static CONST int rgLatH[] = {45, 66, 70, 75, 82};
      static CONST int rgMonH[] = {6, 12};
      flag rgfSeenBad[cSystem];
      int iEngine, iLatH, iMonH, iDeg, cUnexpected = 0, cExpectedBad = 0;
      int cMissingBad = 0, cCase = 0;

      for (i = 0; i < cSystem; i++)
        rgfSeenBad[i] = fFalse;
      real rSumH, rGapH, rGapMinH;
      int iCuspH;
      flag fExpectBad, fIsBad;

      for (iEngine = 0; iEngine <= 1; iEngine++) {
        Borrow bEngine(us.fEphemFiles, iEngine ? fTrue : fFalse);
        for (i = 0; i < cSystem; i++) {
          Borrow bHouseH(us.nHouseSystem, i);
          for (iLatH = 0; iLatH < 5; iLatH++)
            for (iMonH = 0; iMonH < 2; iMonH++) {
              OraclePinChartQt(1976);
              ciCore.mon = rgMonH[iMonH]; ciCore.day = 20;
              ciCore.tim = 12.0; ciCore.zon = 0.0;
              ciCore.lon = -15.63; ciCore.lat = (real)rgLatH[iLatH];
              CastChart(1);
              rSumH = 0.0; rGapMinH = rDegMax;
              for (iCuspH = 1; iCuspH <= cSign; iCuspH++) {
                rGapH = chouse[iCuspH == cSign ? 1 : iCuspH+1] -
                  chouse[iCuspH];
                if (rGapH < 0.0)
                  rGapH += rDegMax;
                if (rGapH < rGapMinH)
                  rGapMinH = rGapH;
                rSumH += rGapH;
              }
              fIsBad = (RAbs(rSumH - rDegMax) > 0.01 || rGapMinH < 0.001);
              fExpectBad = fFalse;
              for (iDeg = 0; iDeg < 1; iDeg++)
                if (rgDegen[iDeg] == i)
                  fExpectBad = fTrue;
              cCase++;
              if (fIsBad && !fExpectBad) {
                cUnexpected++;
                printf("    NEW polar degeneracy: %s at %dN month %d, "
                  "%s engine (sum %.3f minGap %.4f)\n", szSystem[i],
                  rgLatH[iLatH], rgMonH[iMonH],
                  iEngine ? "Swiss" : "Matrix", rSumH, rGapMinH);
              } else if (fIsBad) {
                cExpectedBad++;
                rgfSeenBad[i] = fTrue;
              }
            }
        }
      }
      Check(cCase == cSystem * 5 * 2 * 2,
        "the house sweep covered every system, latitude and engine (%d)",
        cCase);
      Check(cUnexpected == 0,
        "no house system degenerates toward the pole beyond Pullen "
        "(S.Delta), whose collapse is deliberate (%d new)", cUnexpected);
      for (iDeg = 0; iDeg < 1; iDeg++)
        if (!rgfSeenBad[rgDegen[iDeg]])
          cMissingBad++;
      Check(cMissingBad == 0,
        "and Pullen (S.Delta) still collapses, as its author wrote "
        "(%d appear changed "
        "fixed -- if that is deliberate, drop them from rgDegen)",
        cMissingBad);
      Check(cExpectedBad > 0,
        "the polar sweep reaches the degenerate region at all (%d cases)",
        cExpectedBad);
    }

    // ---- Leg 6: an aspect is the same aspect from either side ----
    // GetAspect(i, j) and GetAspect(j, i) ask about one pair of points,
    // and FCreateGrid() depends on their agreeing: it fills half the grid
    // from (x,y) and reads the other half as (y,x). Nothing had ever
    // checked it. This needs no reference outside the program -- it is
    // the kind of invariant T9 argues for, true whatever the numbers are.
    {
      Borrow bApp(us.nAppSep, 0), bA3D(us.fAspect3D, fFalse);
      Borrow bALat(us.fAspectLat, fFalse);
      int j, aspA, aspB, cPair = 0, cBadAsp = 0, cBadOrb = 0;
      real rOrbA, rOrbB;

      OraclePinChartQt(2020);
      CastChart(1);
      for (i = 0; i <= is.nObj && i <= oNorm; i++) {
        if (ignore[i])
          continue;
        for (j = i+1; j <= is.nObj && j <= oNorm; j++) {
          if (ignore[j])
            continue;
          aspA = GetAspect(planet, planet, planetalt, planetalt,
            ret, ret, i, j, &rOrbA);
          aspB = GetAspect(planet, planet, planetalt, planetalt,
            ret, ret, j, i, &rOrbB);
          cPair++;
          if (aspA != aspB)
            cBadAsp++;
          else if (aspA > 0 && RAbs(RAbs(rOrbA) - RAbs(rOrbB)) > 1.0e-9)
            cBadOrb++;
        }
      }
      Check(cPair > 100, "aspect symmetry has pairs to check (%d)", cPair);
      Check(cBadAsp == 0,
        "an aspect is the same aspect from either side (%d of %d differ)",
        cBadAsp, cPair);
      Check(cBadOrb == 0,
        "and the same orb from either side (%d of %d differ)",
        cBadOrb, cPair);
    }

    // ---- Leg 7: a midpoint lies halfway between its two sources ----
    // Midpoint2() picks between two candidate points 180 apart, which is
    // exactly where a sign error hides. The distances to each source must
    // be equal and must add up to the distance between them.
    {
      real rA, rB, rMid, dAM, dMB, dAB;
      int cPt = 0, cBadMid = 0;

      for (rA = 0.0; rA < rDegMax; rA += 7.0)
        for (rB = 0.0; rB < rDegMax; rB += 11.0) {
          rMid = Midpoint2(rA, rB, 0.5);
          dAM = MinDistance(rA, rMid);
          dMB = MinDistance(rMid, rB);
          dAB = MinDistance(rA, rB);
          cPt++;
          if (RAbs(dAM + dMB - dAB) > 1.0e-9 || RAbs(dAM - dMB) > 1.0e-9)
            cBadMid++;
        }
      Check(cPt > 1000, "midpoint sweep has points (%d)", cPt);
      Check(cBadMid == 0,
        "a midpoint is equidistant from both sources and between them "
        "(%d of %d fail)", cBadMid, cPt);
    }

    // ---- Leg 8: a progressed chart at zero elapsed time is the natal ----
    // CastChart() adds (JDp - T) / rProgDay to the chart time, so a
    // progression whose target date IS the natal date must land back on
    // the natal sky. Nothing else here exercises the progression path at
    // all, and this costs one extra cast.
    {
      real rgrNatal[objMax];
      int cDiffProg = 0, cObjProg = 0;

      OraclePinChartQt(2020);
      CastChart(1);
      for (i = 0; i <= is.nObj; i++)
        rgrNatal[i] = planet[i];
      {
        Borrow bProgOn(us.fProgress, fTrue);
        Borrow bJDp(is.JDp);
        is.JDp = MdytszToJulian(MM, DD, YY, TT, SS, ZZ);
        CastChart(1);
        for (i = 0; i <= is.nObj; i++) {
          if (ignore[i])
            continue;
          cObjProg++;
          if (MinDistance(rgrNatal[i], planet[i]) > 1.0e-6)
            cDiffProg++;
        }
      }
      Check(cObjProg > 10, "progression check has objects (%d)", cObjProg);
      Check(cDiffProg == 0,
        "a progressed chart at zero elapsed time is the natal chart "
        "(%d of %d moved)", cDiffProg, cObjProg);
    }

    // ---- Leg 9: a return really returns ----
    // Astrolog has no "cast the return chart" command; -tr searches a
    // month for the moments a transiting object conjoins its own natal
    // position, and with -5 (us.fListAuto) each hit is appended to the
    // chart list. So the invariant is checkable end to end: take every
    // moment the search reports as a solar return, cast a chart for it,
    // and the Sun must be back where it started. Nothing else in the
    // suite exercises ChartTransitSearch() at all.
    {
      Borrow bList(us.fListAuto, fTrue), bRet(is.fReturn, fTrue);
      Borrow bMonth(us.fInDayMonth, fTrue), bYear(us.fInDayYear, fFalse);
      Borrow bDivision(us.nDivision, 48);
      CI rgciSav[8], ciTranSav = ciTran;
      real rNatalSun;
      int cciSav = is.cci, cRet = 0, cBadRet = 0, j;

      for (j = 0; j < 8 && j < cciSav; j++)
        rgciSav[j] = is.rgci[j];
      OraclePinChartQt(2020);
      CastChart(1);
      rNatalSun = planet[oSun];
      // Only the Sun, in both charts, so the search reports solar
      // returns and nothing else -- one hit per year, so one line of
      // output rather than a month of transits.
      for (i = 0; i <= cObj; i++)
        ignore[i] = ignore2[i] = (i != oSun);
      ciTran = ciCore;
      ciTran.yea++;
      is.cci = 0;
      // ChartTransitSearch() prints its results through PrintSz(), which
      // writes to is.S -- and is.S is only ever opened by Action(). The
      // GUI runs inside one, so is.S is that Action()'s stream, and
      // putc()ing into it from here made glibc free a backup area it
      // never allocated: "free(): invalid pointer", about one full-suite
      // run in six, with a backtrace of
      // _IO_free_backup_area <- _IO_putc <- PrintSz <- ChartTransitSearch.
      //
      // This is the incident CLAUDE.md records verbatim -- "a new test
      // called a print routine outside Action(), so it wrote to a FILE *
      // nothing had opened" -- reintroduced by leg 9 on the day that
      // lesson was quoted twice. Give the search a stream of its own.
      {
        char szTmpRet[cchSzMax];
        FILE *fileRetSav = is.S, *fileRet;

        sprintf2(S(szTmpRet), "%s/astrolog-qt-return-%d.txt",
          QDir::tempPath().toLocal8Bit().constData(),
          (int)QCoreApplication::applicationPid());
        fileRet = fopen(szTmpRet, "w");
        if (fileRet != NULL)
          is.S = fileRet;
        ChartTransitSearch(fFalse);
        is.S = fileRetSav;
        if (fileRet != NULL) {
          fclose(fileRet);
          remove(szTmpRet);
        }
      }
      for (j = 0; j < is.cci; j++) {
        ciCore = is.rgci[j];
        CastChart(1);
        cRet++;
        if (MinDistance(planet[oSun], rNatalSun) > 0.01)
          cBadRet++;
      }
      Check(cRet > 0,
        "the return search found a solar return to cast (%d)", cRet);
      Check(cBadRet == 0,
        "and the Sun is at its natal longitude in every one (%d off by "
        "more than 0.01 degrees)", cBadRet);
      is.cci = cciSav;
      for (j = 0; j < 8 && j < cciSav; j++)
        is.rgci[j] = rgciSav[j];
      ciTran = ciTranSav;
    }

    // ---- Leg 10: eclipses, against the library that finds them ----
    // Astrolog decides whether an eclipse is happening from its own 3D
    // geometry -- NCheckEclipseSolar() and NCheckEclipseLunar()
    // (calc.cpp) walk space[] with real body diameters and never ask the
    // Swiss library, which has its own eclipse finder. So this is a
    // genuine outside answer rather than the same code consulted twice,
    // and it is the only surface in this file where two independent
    // implementations of the same astronomy can be set against each
    // other.
    //
    // Three questions per eclipse, and the third is the one a
    // detector-only check would miss: does Astrolog see an eclipse at
    // the moment the library names, does it call it the same kind, and
    // does it see nothing halfway between two consecutive ones.
    //
    // Measured over five epochs, 1900 through 2060: 60 solar eclipses,
    // 59 midpoints, 40 lunar. Every one agrees except a single case,
    // named below with its number rather than folded into a threshold.
    {
      static CONST int rgyeaEcl[] = {1900, 1940, 1980, 2020, 2060};
      double tret[10], jdE, jdPrev;
      char serrE[AS_MAXCH];
      int32 typ, yeaE, monE, dayE;
      double hourE;
      real rPct;
      int et, etWant, cSol = 0, cLun = 0, cGap = 0, i, j;

      for (j = 0; j < 5; j++) {
        jdE = 2415020.5 + (real)(rgyeaEcl[j] - 1900) * 365.25;
        jdPrev = 0.0;
        for (i = 0; i < 12; i++) {
          typ = swe_sol_eclipse_when_glob(jdE, SEFLG_SWIEPH, 0, tret, 0,
            serrE);
          if (typ < 0) {
            Check(fFalse, "Swiss found a solar eclipse after %d (%s)",
              rgyeaEcl[j], serrE);
            break;
          }

          // Halfway between two eclipses there is no eclipse. Without
          // this the detector could answer "yes" always and still pass.
          if (jdPrev > 0.0) {
            swe_revjul((jdPrev + tret[0]) / 2.0, SE_GREG_CAL, &yeaE, &monE,
              &dayE, &hourE);
            OraclePinUtQt(yeaE, monE, dayE, hourE);
            CastChart(1);
            cGap++;
            Check(NCheckEclipseSolar(oEar, oMoo, oSun, NULL) <= etNone,
              "no solar eclipse midway between two, %d-%02d-%02d",
              yeaE, monE, dayE);
          }
          jdPrev = tret[0];

          swe_revjul(tret[0], SE_GREG_CAL, &yeaE, &monE, &dayE, &hourE);
          OraclePinUtQt(yeaE, monE, dayE, hourE);
          CastChart(1);
          et = NCheckEclipseSolar(oEar, oMoo, oSun, &rPct);
          cSol++;
          Check(et > etNone, "solar eclipse seen on %d-%02d-%02d",
            yeaE, monE, dayE);
          etWant = (typ & SE_ECL_TOTAL) ? etTotal :
            ((typ & SE_ECL_ANNULAR) ? etAnnular :
            ((typ & SE_ECL_PARTIAL) ? etPartial : -1));
          Check(etWant < 0 || et == etWant,
            "%d-%02d-%02d solar is %s to both", yeaE, monE, dayE,
            szEclipse[etWant < 0 ? 0 : etWant]);
          jdE = tret[0] + 20.0;
        }
      }

      for (j = 0; j < 5; j++) {
        jdE = 2415020.5 + (real)(rgyeaEcl[j] - 1900) * 365.25;
        for (i = 0; i < 8; i++) {
          typ = swe_lun_eclipse_when(jdE, SEFLG_SWIEPH, 0, tret, 0, serrE);
          if (typ < 0) {
            Check(fFalse, "Swiss found a lunar eclipse after %d (%s)",
              rgyeaEcl[j], serrE);
            break;
          }
          swe_revjul(tret[0], SE_GREG_CAL, &yeaE, &monE, &dayE, &hourE);
          OraclePinUtQt(yeaE, monE, dayE, hourE);
          CastChart(1);
          et = NCheckEclipseLunar(oEar, oMoo, oSun, &rPct);
          cLun++;
          etWant = (typ & SE_ECL_TOTAL) ? etTotal :
            ((typ & SE_ECL_PARTIAL) ? etPartial : etPenumbra);

          // The one disagreement in 159 checks, and it is the boundary
          // rather than a wrong answer: the total lunar eclipse of
          // 2021-05-26 was total for about fifteen minutes, magnitude
          // 1.009. Astrolog measures 98.9% umbral overlap and calls it
          // partial, one step down. Named by date on purpose -- a
          // threshold here would hide the next real one.
          if (yeaE == 2021 && monE == 5 && dayE == 26) {
            Check(et == etPartial && rPct > 98.0,
              "2021-05-26 is the known boundary: total by 15 minutes, "
              "Astrolog says %s at %.1f%%", szEclipse[et], rPct);
            jdE = tret[0] + 20.0;
            continue;
          }
          Check(et == etWant || (etWant == etPenumbra && et == etPenumbra2),
            "%d-%02d-%02d lunar is %s to both", yeaE, monE, dayE,
            szEclipse[etWant]);
          jdE = tret[0] + 20.0;
        }
      }
      Check(cSol == 60 && cGap == 55 && cLun == 40,
        "the eclipse leg ran its whole span (%d solar, %d gaps, %d lunar)",
        cSol, cGap, cLun);
    }

    // ---- Leg 11: the atlas names the nearest city, not a nearby one ----
    // The atlas has no reference outside this repo -- it *is* the data --
    // so the check is an invariant with a brute-force answer instead: for
    // a set of probe coordinates, DisplayAtlasNearby() must name the same
    // city a linear scan of is.rgae[] does. That tests the selection and
    // the insertion sort feeding it, not the metric, since both sides use
    // SphDistance; the selection is where an indexing bug would live and
    // the metric is not something this file can second-guess.
    //
    // Plus the data itself: every entry inside the coordinate ranges its
    // own struct implies, and no empty name. A truncated atlas is a real
    // failure mode here -- work log item 149 found one re-parsing a line
    // 33,219 times -- and nothing else in the suite looks at the table's
    // contents at all.
    if (FEnsureAtlas()) {
      // Astrolog's longitude is positive WEST, which is the opposite of
      // the geographic convention and is exactly the mistake this leg
      // caught in its own first draft: the probes read as eight world
      // cities and were resolving to their mirror images -- Chicago's
      // coordinates found Korla, in Xinjiang. Both sides of the check
      // agreed, because both used the same wrong number.
      static CONST real rgrProbe[8][2] = {
        {87.65, 41.85}, {-2.35, 48.86}, {-139.69, 35.69}, {-151.21, -33.87},
        {58.38, -34.60}, {-18.42, -33.92}, {149.90, 61.22}, {-77.21, 28.61}};
      static CONST char *rgszProbe[8] = {"Chicago", "Paris", "Tokyo",
        "Sydney", "Buenos Aires", "Cape Town", "Anchorage", "Delhi"};
      AtlasEntry *pae;
      // The search's own unit: miles by default, kilometres under -Yu,
      // truncated to a whole one before anything is compared.
      real rCirc = us.fEuroDist ? 40075.0 : 24901.0;
      char szTmpAtl[cchSzMax];
      FILE *fileAtlSav, *fileAtl;
      real rBest, rD2;
      int iaeGot, iaeWant, cBad = 0, cRange = 0, i, j;

      // DisplayAtlasNearby() prints a whole city list through is.S on
      // its way to returning the index -- the "just return the index"
      // early exit is in the fDialog branch, and that branch does not
      // fill *piae the way this needs. So it gets a stream of its own,
      // which is work log item 165's hazard: this leg shipped without
      // one, into a FILE nothing had opened, and crashed 3 runs in 10.
      //
      // The last line before the segfault named the WIREFRAME writer,
      // because PrintProgress goes to unbuffered stderr while this went
      // to a buffered stream. CLAUDE.md says not to reason from that
      // ordering. An hour went into WriteWire() before a backtrace said
      // qttest.cpp:4596.
      fileAtlSav = is.S;
      sprintf2(S(szTmpAtl), "%s/astrolog-qt-atlas-%d.txt",
        QDir::tempPath().toLocal8Bit().constData(),
        (int)QCoreApplication::applicationPid());
      fileAtl = fopen(szTmpAtl, "w");
      Check(fileAtl != NULL, "the atlas leg got a stream of its own");
      if (fileAtl != NULL)
        is.S = fileAtl;
      for (i = 0; i < 8; i++) {
        iaeGot = -1;
        if (!DisplayAtlasNearby(rgrProbe[i][0], rgrProbe[i][1], fFalse,
          &iaeGot, fFalse) || iaeGot < 0 || iaeGot >= is.cae) {
          Check(fFalse, "the atlas answered for probe %d", i);
          continue;
        }
        iaeWant = -1; rBest = rLarge;
        for (j = 0; j < is.cae; j++) {
          rD2 = SphDistance(rgrProbe[i][0], rgrProbe[i][1],
            is.rgae[j].lon, is.rgae[j].lat);
          if (rD2 < rBest) {
            rBest = rD2;
            iaeWant = j;
          }
        }
        // Ties are real -- two cities can share a coordinate to the
        // atlas's precision -- so accept any city at the same distance
        // rather than the same index.
        rD2 = SphDistance(rgrProbe[i][0], rgrProbe[i][1],
          is.rgae[iaeGot].lon, is.rgae[iaeGot].lat);
        // The search truncates each distance to a whole unit before
        // comparing (atlas.cpp: "nDist = (int)rDist"), so cities in the
        // same neighbourhood tie and table order breaks the tie --
        // Chicago's coordinates legitimately return Bridgeport, and
        // Sydney's return Surry Hills. The invariant that survives that
        // is the one worth asserting: no city the search skipped is a
        // whole unit closer than the one it chose.
        Check((int)(rD2 / 360.0 * rCirc) <= (int)(rBest / 360.0 * rCirc),
          "%s: the atlas chose %s, and %s is not a kilometre closer",
          rgszProbe[i], is.rgae[iaeGot].szNam, is.rgae[iaeWant].szNam);

        // And that the answer is in the right hemisphere at all. This is
        // the assertion the leg's own first draft would have failed:
        // Astrolog's longitude is positive WEST, and geographic-sign
        // probes resolved to mirror-image cities half a world away while
        // both sides of the comparison agreed with each other.
        Check(rD2 < 1.0, "%s's coordinates land within a degree of %s, "
          "not %.0f degrees away", rgszProbe[i], is.rgae[iaeGot].szNam,
          rD2);
      }

      is.S = fileAtlSav;
      if (fileAtl != NULL) {
        fclose(fileAtl);
        remove(szTmpAtl);
      }

      for (j = 0; j < is.cae; j++) {
        pae = &is.rgae[j];
        if (pae->lat < -90.0 || pae->lat > 90.0 ||
          pae->lon < -180.0 || pae->lon > 180.0)
          cRange++;
        if (pae->szNam[0] == chNull)
          cBad++;
      }
      Check(is.cae > 1000, "the atlas actually loaded (%d cities)", is.cae);
      Check(cRange == 0, "every city is on the globe (%d outside)", cRange);
      Check(cBad == 0, "every city has a name (%d empty)", cBad);
    }

    // ---- Leg 12: the interpretation tables have no holes ----
    // The interpretation text has no reference outside this repo either,
    // and unlike the atlas it has no invariant worth the name: prose is
    // prose. What it does have is a shape -- one row per object, per
    // sign, per aspect -- and the failure that shape permits is the one
    // defaults_audit found in ruler2[]: a table one entry short, which
    // reads as a blank sentence in a chart nobody ran.
    //
    // So: every row of every interpretation table, present and non-empty,
    // and the text actually produced for a sampled set of aspects, with
    // is.S pointed at a stream of its own. That last part is why this
    // leg exists at all rather than being an audit -- InterpretAspect()
    // indexes szInteract[] and szTherefore[] by aspect and szMindPart[]
    // by object, and only running it proves those indexes line up.
    {
      int cNull = 0, cShort = 0, cText = 0, x, asp;

      // NOT "every row has text": these tables are sparse on purpose --
      // Astrolog interprets ten aspects and four angles and leaves the
      // rest blank. The invariant is weaker and sharper. **No row may be
      // NULL**, because the code tests row[0] before deciding whether a
      // row is blank, and a NULL row is a dereference rather than a
      // blank sentence. That is not hypothetical: szThereforeDef[] had
      // 19 initializers for a cAspect+1 array, so aspects 19-24 were
      // NULL, and
      //
      //     astrolog -A 24 -YIA 19 "is %sopposed to" -I
      //
      // dumped core in both builds. Work log item 171; -YIA is the
      // documented switch for setting exactly those strings.
      for (i = 0; i < objMax; i++)
        if (szMindPart[i] == NULL)
          cNull++;
      for (i = 0; i <= cSign; i++)
        if (szDesc[i] == NULL || szDesire[i] == NULL ||
          szLifeArea[i] == NULL)
          cNull++;
      for (i = 0; i <= cAspect; i++)
        if (szInteract[i] == NULL || szTherefore[i] == NULL)
          cNull++;
      Check(cNull == 0,
        "no interpretation row is NULL, at any index the switches reach "
        "(%d were)", cNull);

      // And the shape that IS populated, so deleting a row is a failure
      // rather than a silently shorter chart. Measured, not chosen:
      // aspects 1-11 carry interaction text and 12-24 do not.
      for (i = 1; i <= cAspect; i++)
        if (FSzSet(szInteract[i]))
          cText++;
      Check(cText == 11,
        "the eleven interpreted aspects still have their text (%d)",
        cText);
      for (i = 1; i <= cSign; i++)
        if (!FSzSet(szDesc[i]) || !FSzSet(szDesire[i]) ||
          !FSzSet(szLifeArea[i]))
          cShort++;
      Check(cShort == 0,
        "and all twelve signs have all three of theirs (%d short)",
        cShort);
      Check(!FSzSet(szDesc[0]) && !FSzSet(szInteract[0]),
        "with the none-slots at index 0 left empty");
      cShort = 0;

      {
        char szTmpInt[cchSzMax];
        FILE *fileIntSav = is.S, *fileInt;
        long lcb;

        sprintf2(S(szTmpInt), "%s/astrolog-qt-interp-%d.txt",
          QDir::tempPath().toLocal8Bit().constData(),
          (int)QCoreApplication::applicationPid());
        fileInt = fopen(szTmpInt, "w");
        if (fileInt != NULL) {
          is.S = fileInt;
          for (x = oSun; x <= oSat; x++)
            for (asp = aCon; asp <= aOpp; asp++)
              InterpretAspectCore(x, asp, x == oSun ? oMoo : oSun, 0);
          is.S = fileIntSav;
          lcb = ftell(fileInt);
          fclose(fileInt);
          remove(szTmpInt);
          Check(lcb > 1000,
            "and running them produces text (%ld bytes for %d aspects)",
            lcb, (oSat - oSun + 1) * (aOpp - aCon + 1));
        } else {
          is.S = fileIntSav;
          cShort++;
        }
        Check(cShort == 0, "the interpretation leg got its own stream");
      }
    }

    // ---- Leg 13: the in-day search finds real conjunctions ----
    // The search functions have no reference outside this repo, and
    // unlike positions they have no library to ask. What they do have is
    // the same invariant leg 9 uses for returns: **a hit, re-cast, must
    // satisfy the condition it was searching for.** Nothing else in the
    // suite exercises ChartInDaySearch() at all.
    //
    // Restricted to the Sun and Moon with conjunction as the only
    // aspect, and with sign and direction changes and the
    // void-of-course pass turned off, every hit is a new moon. Without
    // those restrictions the search reports six other event kinds and
    // half the hits are not aspects -- measured, and the first draft of
    // this leg asserted otherwise.
    //
    // Two things are checked, and the second is not an internal
    // invariant at all: the separation at each hit, and the interval
    // between consecutive hits, which must be the synodic month. That
    // number belongs to the solar system rather than to this program.
    {
      Borrow bList(us.fListAuto, fTrue), bRet(is.fReturn, fFalse);
      Borrow bMonth(us.fInDayMonth, fTrue), bYear(us.fInDayYear, fFalse);
      Borrow bDiv(us.nDivision, 48), bAsp(us.nAsp, 1);
      Borrow bSign(us.fIgnoreSign, fTrue), bDir(us.fIgnoreDir, fTrue);
      Borrow bDalt(us.fIgnoreDiralt, fTrue), bDlen(us.fIgnoreDirlen, fTrue);
      CI rgciSav[8], ciMainSav2 = ciMain;
      real rgjdNew[8], rSep, rGap;
      char szTmpDay[cchSzMax];
      FILE *fileDaySav, *fileDay;
      int cciSav = is.cci, cNew = 0, cBadSep = 0, cGap = 0, cBadGap = 0,
        iMon, j;

      for (j = 0; j < 8 && j < cciSav; j++)
        rgciSav[j] = is.rgci[j];
      sprintf2(S(szTmpDay), "%s/astrolog-qt-inday-%d.txt",
        QDir::tempPath().toLocal8Bit().constData(),
        (int)QCoreApplication::applicationPid());

      for (iMon = 1; iMon <= 6; iMon++) {
        OraclePinUtQt(2020, iMon, 1, 0.0);
        ciMain = ciCore;
        CastChart(1);
        for (j = 0; j <= cObj; j++)
          ignore[j] = ignore2[j] = !(j == oSun || j == oMoo);
        is.cci = 0;
        // The search prints; give it a stream of its own (item 165).
        fileDaySav = is.S;
        fileDay = fopen(szTmpDay, "w");
        if (fileDay != NULL)
          is.S = fileDay;
        ChartInDaySearch(fFalse);
        is.S = fileDaySav;
        if (fileDay != NULL) {
          fclose(fileDay);
          remove(szTmpDay);
        }
        for (j = 0; j < is.cci; j++) {
          ciCore = is.rgci[j];
          CastChart(1);
          rSep = MinDistance(planet[oSun], planet[oMoo]);
          if (rSep > 0.01)
            cBadSep++;
          if (cNew < 8)
            rgjdNew[cNew] = JulianDayFromTime(is.T);
          cNew++;
        }
      }
      is.cci = cciSav;
      for (j = 0; j < 8 && j < cciSav; j++)
        is.rgci[j] = rgciSav[j];
      ciMain = ciMainSav2;

      Check(cNew >= 6, "the in-day search found a new moon in each of six "
        "months (%d)", cNew);
      Check(cBadSep == 0, "and the Sun and Moon are conjunct at every one "
        "(%d off by more than 0.01 degrees)", cBadSep);

      // 29.530588 days is the synodic month. Individual lunations vary
      // by several hours either way, so the tolerance is half a day.
      for (j = 1; j < cNew && j < 8; j++) {
        rGap = rgjdNew[j] - rgjdNew[j-1];
        cGap++;
        if (RAbs(rGap - 29.530588) > 0.5)
          cBadGap++;
      }
      Check(cGap >= 5 && cBadGap == 0,
        "and consecutive ones are a synodic month apart (%d gaps, %d "
        "outside 29.53 +/- 0.5 days)", cGap, cBadGap);
    }

    // ---- Leg 14: a transit really transits ----
    // The same invariant as leg 13, on the other search. Leg 9 already
    // uses ChartTransitSearch() in RETURN mode, where the transiting
    // object comes back to its own natal place; this is the ordinary
    // mode, where it reaches a different object's.
    //
    // Transiting Moon to natal Sun, conjunction only: that recurs every
    // synodic month, so one month of search is enough to have hits, and
    // the invariant is exact -- at the reported moment the transiting
    // Moon's longitude is the NATAL Sun's, not its own.
    {
      Borrow bList(us.fListAuto, fTrue), bRet(is.fReturn, fFalse);
      Borrow bMonth(us.fInDayMonth, fTrue), bYear(us.fInDayYear, fFalse);
      Borrow bDiv(us.nDivision, 48), bAsp(us.nAsp, 1);
      Borrow bSign(us.fIgnoreSign, fTrue), bDir(us.fIgnoreDir, fTrue);
      Borrow bDalt(us.fIgnoreDiralt, fTrue), bDlen(us.fIgnoreDirlen, fTrue);
      CI rgciSav[8], ciTranSav2 = ciTran, ciMainSav3 = ciMain;
      real rNatalSun2, rSep2;
      char szTmpTra[cchSzMax];
      FILE *fileTraSav, *fileTra;
      int cciSav = is.cci, cTra = 0, cBadTra = 0, j;

      for (j = 0; j < 8 && j < cciSav; j++)
        rgciSav[j] = is.rgci[j];

      OraclePinUtQt(2020, 3, 1, 0.0);
      ciMain = ciCore;
      CastChart(1);
      rNatalSun2 = planet[oSun];
      // Transiting Moon only, natal Sun only. The sense of the two
      // tables is the opposite of the obvious one: ChartTransitSearch()
      // swaps them around its CastChart(-1), so ignore2[] selects the
      // TRANSITING objects and ignore[] the natal ones. Written the
      // other way round first, and the search found nothing at all --
      // which is what a leg like this is for.
      for (j = 0; j <= cObj; j++) {
        ignore[j] = (j != oSun);
        ignore2[j] = (j != oMoo);
      }
      ciTran = ciCore;
      is.cci = 0;
      sprintf2(S(szTmpTra), "%s/astrolog-qt-transit-%d.txt",
        QDir::tempPath().toLocal8Bit().constData(),
        (int)QCoreApplication::applicationPid());
      fileTraSav = is.S;
      fileTra = fopen(szTmpTra, "w");
      if (fileTra != NULL)
        is.S = fileTra;
      ChartTransitSearch(fFalse);
      is.S = fileTraSav;
      if (fileTra != NULL) {
        fclose(fileTra);
        remove(szTmpTra);
      }
      for (j = 0; j < is.cci; j++) {
        ciCore = is.rgci[j];
        CastChart(1);
        cTra++;
        rSep2 = MinDistance(planet[oMoo], rNatalSun2);
        if (rSep2 > 0.01)
          cBadTra++;
      }
      is.cci = cciSav;
      for (j = 0; j < 8 && j < cciSav; j++)
        is.rgci[j] = rgciSav[j];
      ciTran = ciTranSav2; ciMain = ciMainSav3;

      Check(cTra > 0, "the transit search found the Moon reaching the "
        "natal Sun (%d)", cTra);
      Check(cBadTra == 0, "and it is there at every reported moment "
        "(%d off by more than 0.01 degrees)", cBadTra);
    }

    // ---- Leg 15: a rising is on the horizon and a zenith is on the
    // meridian ----
    // The third search, and the one with a geometric invariant rather
    // than an aspect one. ChartHorizonRising() reports four events a day
    // per object; re-cast each and convert the object to horizon
    // coordinates, and the event's own name says what must be true:
    // "rises" and "sets" put it on the horizon, "zeniths" and "nadirs"
    // put it on the meridian. Measured at 41.85N on 2020-03-20 the
    // altitudes come back -0.001 and 0.000 and the azimuths 270.005 and
    // 90.004, so the tolerances below are twenty times the observed
    // error rather than a guess.
    //
    // Note the azimuth convention: Astrolog's meridian is 270 and 90,
    // not the compass 180 and 0. Taken from the measurement, not from
    // an assumption about which way the numbers run.
    {
      // The month and year flags are borrowed OFF, not left alone. This
      // leg wants one day; run inside the full suite it found 123 events
      // instead of 4, because an earlier group leaves us.fInDayMonth set
      // and the search then swept the month. Running the oracle group
      // alone hid it -- which is exactly the inter-test interaction
      // QT_TESTING.md says to state preconditions for rather than
      // inherit.
      Borrow bList(us.fListAuto, fTrue);
      Borrow bHMon(us.fInDayMonth, fFalse), bHYea(us.fInDayYear, fFalse);
      CI rgciSav[8], ciMainSav4 = ciMain;
      real azi, alt, mc, kT, rAltZen = -rLarge, rAltNad = rLarge;
      char szTmpHor[cchSzMax], *pchNam;
      FILE *fileHorSav, *fileHor;
      int cciSav = is.cci, cHor = 0, cBadHor = 0, cKind = 0, j;

      for (j = 0; j < 8 && j < cciSav; j++)
        rgciSav[j] = is.rgci[j];

      OraclePinUtQt(2020, 3, 20, 0.0);
      ciCore.lon = 87.65; ciCore.lat = 41.85;
      ciMain = ciCore;
      CastChart(1);
      for (j = 0; j <= cObj; j++)
        ignore[j] = (j != oSun);
      is.cci = 0;
      sprintf2(S(szTmpHor), "%s/astrolog-qt-horizon-%d.txt",
        QDir::tempPath().toLocal8Bit().constData(),
        (int)QCoreApplication::applicationPid());
      fileHorSav = is.S;
      fileHor = fopen(szTmpHor, "w");
      if (fileHor != NULL)
        is.S = fileHor;
      ChartHorizonRising();
      is.S = fileHorSav;
      if (fileHor != NULL) {
        fclose(fileHor);
        remove(szTmpHor);
      }

      for (j = 0; j < is.cci; j++) {
        ciCore = is.rgci[j];
        pchNam = (char *)is.rgci[j].nam;
        CastChart(1);
        mc = planet[oMC]; kT = planetalt[oMC];
        EclToEqu(&mc, &kT);
        EclToHoriz(&azi, &alt, planet[oSun], planetalt[oSun], mc, Lat);
        cHor++;
        if (pchNam == NULL) {
          cBadHor++;
          continue;
        }
        if (strstr(pchNam, "rises") != NULL || strstr(pchNam, "sets") != NULL) {
          cKind++;
          if (RAbs(alt) > 0.02)
            cBadHor++;
        } else if (strstr(pchNam, "zeniths") != NULL) {
          cKind++;
          rAltZen = alt;
          if (RAbs(MinDifference(azi, 270.0)) > 0.02)
            cBadHor++;
        } else if (strstr(pchNam, "nadirs") != NULL) {
          cKind++;
          rAltNad = alt;
          if (RAbs(MinDifference(azi, 90.0)) > 0.02)
            cBadHor++;
        }
      }
      is.cci = cciSav;
      for (j = 0; j < 8 && j < cciSav; j++)
        is.rgci[j] = rgciSav[j];
      ciMain = ciMainSav4;

      Check(cHor == 4, "the horizon search reports four Sun events in a "
        "day (%d)", cHor);
      Check(cKind == 4, "each naming which one it is (%d recognized)",
        cKind);
      Check(cBadHor == 0, "and each is where its name says: horizon for "
        "rise and set, meridian for zenith and nadir (%d off)", cBadHor);
      Check(rAltZen > rAltNad,
        "with the Sun higher at its zenith than its nadir (%.1f vs %.1f)",
        rAltZen, rAltNad);
    }
    cGood = 1;
  }

  for (i = 0; i < objMax; i++)
    ignore[i] = rgfIgnoreSav[i];
  Check(cGood == 1, "the oracle restored every borrowed setting");
#endif
  ciCore = ciCoreSav; ciMain = ciMainSav;
  CastChart(1);                // Leave real positions for the rest.
  SetNoPopupQt(fPopupSav);
}


// One entry per test group, so one group can be run by itself:
//
//   ASTROLOG_QT_TESTS=animation ./run-qt-tests.sh        one group, ~2s
//   ASTROLOG_QT_TESTS=objsel,glyph ./run-qt-tests.sh     several
//   ASTROLOG_QT_TESTS=list ./run-qt-tests.sh             print the names
//
// Matching is a case-insensitive substring over the names below, with
// commas separating alternatives. The full suite is ~40 seconds, and
// chasing one intermittent failure through full runs is how a debugging
// session turns into minutes of dead air per attempt -- an exit-time
// heap corruption took eight full runs to localise the night before this
// existed, when three two-second runs of its own group would have done.
//
// A group that passes alone and fails in the full run is inheriting
// state: TestAllMenuActionsQt() fires all 338 menu items and leaves
// every setting wherever that lands, and anything after it must set what
// it depends on. Dump the globals in a solo run and a full run and diff
// them (work log item 57) rather than guessing one variable per rebuild.
//
// When a filter is active (or ASTROLOG_QT_TIME is set), each group also
// reports its wall time, which is how to find where the 40 seconds go.

// The atlas lookups deliver dialog rows through the pfnAtlasRow sink on
// every port. The Qt Time Changes list was empty from the port's first
// day to work log item 108: the delivery calls sat nested inside
// #ifdef WIN, the exact dead-branch shape of items 39 and 54, so only
// the city lists ever reached the dialog. Assert all three lookups
// deliver rows when the dialog flag is up, and none when it is down.

static int s_cAtlasRow = 0;

static void CountAtlasRowQt(CONST char *sz, int n)
{
  s_cAtlasRow++;
}

static void TestAtlasSinkQt()
{
  void (*pfnSav)(CONST char *, int) = pfnAtlasRow;
  CI ci = ciMain;
  int i = 12, cLook, cTz, cNear;

  Group("Atlas row sink");
  pfnAtlasRow = CountAtlasRowQt;
  s_cAtlasRow = 0;
  Check(DisplayAtlasLookup("Seattle, WA, USA", fTrue, &i),
    "dialog city lookup succeeds");
  cLook = s_cAtlasRow;
  Check(cLook == 1, "Seattle delivers exactly one row, got %d", cLook);

  pfnAtlasRow = NULL;
  i = 12;
  Check(DisplayAtlasLookup("Seattle, WA, USA", fFalse, &i),
    "console city lookup succeeds");
  pfnAtlasRow = CountAtlasRowQt;
  s_cAtlasRow = 0;
  Check(DisplayTimezoneChanges(is.rgae[i].izn, fTrue, &ci),
    "dialog time changes succeed");
  cTz = s_cAtlasRow;
  Check(cTz > 1, "time changes deliver header and rows, got %d", cTz);

  s_cAtlasRow = 0;
  i = 12;
  Check(DisplayAtlasNearby(is.rgae[0].lon, is.rgae[0].lat, fTrue, &i,
    fFalse), "dialog nearby lookup succeeds");
  cNear = s_cAtlasRow;
  Check(cNear >= 1, "nearby delivers rows, got %d", cNear);
  pfnAtlasRow = pfnSav;
}


typedef struct _qttestentry {
  CONST char *szName;
  void (*pfn)();
} QTTESTENTRY;

// Work log item 111: the flag<->mode mapping exists once, in rgchartmode[]
// (xscreen.cpp), and DetectGraphicsChartMode() scans its first
// cchartmodeDetect rows in priority order -- first set flag wins. The
// expected table here is a deliberate second copy of that mapping: row
// order carries detection priority, so the pin has to hold the order
// itself, not read it back from the table under test (a check that asks
// the table what to expect passes under any reordering -- the first
// draft of this test did exactly that, and swapping two rows proved it).
static void TestChartModeTableQt()
{
  static CONST CHARTMODE rgExpected[] = {
    {gHouse,      &us.fWheel},         {gGrid,       &us.fGrid},
    {gMidpoint,   &us.fMidpoint},      {gHorizon,    &us.fHorizon},
    {gOrbit,      &us.fOrbit},         {gSector,     &us.fSector},
    {gDisposit,   &us.fInfluence},     {gEsoteric,   &us.fEsoteric},
    {gAstroGraph, &us.fAstroGraph},    {gCalendar,   &us.fCalendar},
    {gEphemeris,  &us.fEphemeris},     {gRising,     &us.fHorizonSearch},
    {gLocal,      &us.fAtlasNear},     {gMoons,      &us.fMoonChart},
    {gTraTraGra,  &us.fInDayGra},      {gTraNatGra,  &us.fTransitGra},
    {gWheel,      &us.fListing},       {gExo,        &us.fExoTransit},
    {gAspect,     &us.fAspList},       {gArabic,     &us.fArabic},
    {gTraTraTim,  &us.fInDay},         {gTraTraInf,  &us.fInDayInf},
    {gTraNatTim,  &us.fTransit},       {gTraNatInf,  &us.fTransitInf},
    {gSign,       &us.fSign},          {gObject,     &us.fObject},
    {gHelpAsp,    &us.fAspect},        {gConstel,    &us.fConstel},
    {gPlanet,     &us.fOrbitData},     {gRay,        &us.fRay},
    {gMeaning,    &us.fMeaning},       {gSwitch,     &us.fSwitch},
    {gObscure,    &us.fSwitchRare},    {gKeystroke,  &us.fKeyGraph},
    {gCredit,     &us.fCredit}};
  CONST int cExpected = (int)(sizeof(rgExpected) / sizeof(CHARTMODE));
  flag rgfSav[48];
  int i, j, nRelSav = us.nRel;

  Group("Chart mode table");
  Check(cchartmode == cExpected && cchartmode <= 48,
    "the table carries all %d chart modes (%d)", cExpected, cchartmode);
  Check(cchartmodeDetect == 16,
    "the first 16 rows are the detection rows (%d)", cchartmodeDetect);
  for (i = 0; i < Min(cchartmode, cExpected); i++)
    Check(rgchartmode[i].nMode == rgExpected[i].nMode &&
      rgchartmode[i].pf == rgExpected[i].pf,
      "row %d is {mode %d} where %d expected -- order carries priority",
      i, rgchartmode[i].nMode, rgExpected[i].nMode);

  for (i = 0; i < cchartmode; i++) {
    rgfSav[i] = *rgchartmode[i].pf;
    *rgchartmode[i].pf = fFalse;
  }
  us.nRel = rcNone;

  // Each detection flag alone selects its mode, named independently of
  // the table under test.
  for (i = 0; i < cchartmodeDetect; i++) {
    *rgExpected[i].pf = fTrue;
    Check(DetectGraphicsChartMode() == rgExpected[i].nMode,
      "detection flag %d alone detects mode %d", i, rgExpected[i].nMode);
    *rgExpected[i].pf = fFalse;
  }

  // Priority spot checks, each driving two named flags: the old else-if
  // chain's ordering must survive the table.
  us.fWheel = us.fGrid = fTrue;
  Check(DetectGraphicsChartMode() == gHouse, "-w outranks -g");
  us.fWheel = us.fGrid = fFalse;
  us.fMidpoint = us.fHorizon = fTrue;
  Check(DetectGraphicsChartMode() == gMidpoint, "-m outranks -Z");
  us.fMidpoint = us.fHorizon = fFalse;
  us.fInDayGra = us.fTransitGra = fTrue;
  Check(DetectGraphicsChartMode() == gTraTraGra, "-B outranks -V");
  us.fInDayGra = us.fTransitGra = fFalse;
  us.fListing = us.fWheel = fTrue;
  Check(DetectGraphicsChartMode() == gHouse, "-w outranks -v's leftover flag");
  us.fListing = us.fWheel = fFalse;

  // -HA has always detected as an aspect grid, at -g's priority slot:
  // above -m, below -w.
  us.fAspect = fTrue;
  Check(DetectGraphicsChartMode() == gGrid, "-HA alone detects as gGrid");
  us.fMidpoint = fTrue;
  Check(DetectGraphicsChartMode() == gGrid, "-HA outranks -m");
  us.fWheel = fTrue;
  Check(DetectGraphicsChartMode() == gHouse, "-w outranks -HA");
  us.fAspect = us.fMidpoint = us.fWheel = fFalse;

  // The rows below the boundary never detect; they fall through to the
  // gWheel default (except gHelpAsp, whose flag is us.fAspect, above).
  for (i = cchartmodeDetect; i < cExpected; i++) {
    *rgExpected[i].pf = fTrue;
    j = (rgExpected[i].nMode == gHelpAsp) ? gGrid : gWheel;
    Check(DetectGraphicsChartMode() == j,
      "mode %d's flag is not a detection flag", rgExpected[i].nMode);
    *rgExpected[i].pf = fFalse;
  }

  // The value tests: biorhythm rides on us.nRel, and nothing at all is a
  // wheel chart.
  us.nRel = rcBiorhythm;
  Check(DetectGraphicsChartMode() == gBiorhythm,
    "rcBiorhythm detects with no flag set");
  us.nRel = rcNone;
  Check(DetectGraphicsChartMode() == gWheel, "nothing set falls back to gWheel");

  for (i = 0; i < cchartmode; i++)
    *rgchartmode[i].pf = rgfSav[i];
  us.nRel = nRelSav;
}


// C2's net (REFACTORING.md): CastChart() cooks the typed chart info in
// place inside ciCore -- an LMT or LAT zone resolved from the longitude,
// auto-DST resolved from is.fDst, the zone folded into the time, the
// latitude clamped off the exact poles -- and restores the typed values
// from a stack copy 350 lines later (calc.cpp:1260 -> 1616). Two
// contracts, pinned here before any restructure touches that window:
// each cooked form casts the same chart as its explicitly-typed
// equivalent, and after the cast ciCore reads exactly as typed.
static void TestCastCookingQt()
{
  CI ciCoreSav = ciCore, ciMainSav = ciMain, ciT;
  flag fDstSav = is.fDst, fPopupSav = FNoPopupQt();
  real rSun1, rCusp1, rSun2, rCusp2, zonEquiv;

  Group("Cast input cooking");
  // A cast at the pole can warn depending on the house system, and a
  // warning here is a modal box nothing will ever click (the
  // TestBadInputQt() hazard).
  SetNoPopupQt(fTrue);

  // The typed chart everything below varies from: fixed date and place,
  // so the suite's own clock never enters (work log items 109/111).
  ciT = ciMain;
  ciT.mon = 3; ciT.day = 15; ciT.yea = 2020;
  ciT.tim = 10.5; ciT.dst = 0.0; ciT.zon = 6.0;
  ciT.lon = 87.65; ciT.lat = 41.85;

  // LMT: zone 24 means the time is Local Mean Time, i.e. offset lon/15.
  ciCore = ciT; ciCore.zon = zonLMT;
  CastChart(1);
  rSun1 = planet[oSun]; rCusp1 = chouse[1];
  Check(ciCore.zon == zonLMT && ciCore.tim == 10.5,
    "an LMT chart reads as typed after the cast");
  ciCore = ciT; ciCore.zon = ciT.lon / 15.0;
  CastChart(1);
  Check(planet[oSun] == rSun1 && chouse[1] == rCusp1,
    "LMT casts the same chart as the explicit lon/15 zone");

  // LAT: zone 23 is Local Apparent Time -- LMT further corrected by the
  // equation of time, which SwissLatLmt() supplies for the chart's day.
  ciCore = ciT; ciCore.zon = zonLAT;
  CastChart(1);
  rSun1 = planet[oSun]; rCusp1 = chouse[1];
  Check(ciCore.zon == zonLAT, "a LAT chart reads as typed after the cast");
  zonEquiv = ciT.lon / 15.0 -
    SwissLatLmt((real)MdyToJulian(ciT.mon, ciT.day, ciT.yea));
  ciCore = ciT; ciCore.zon = zonEquiv;
  CastChart(1);
  Check(planet[oSun] == rSun1 && chouse[1] == rCusp1,
    "LAT casts the same chart as its equation-of-time zone");
  Check(zonEquiv != ciT.lon / 15.0,
    "the equation of time is nonzero mid-March, so LAT proved itself");

  // Auto-DST: dst 24 defers to is.fDst, whichever way it points.
  is.fDst = fTrue;
  ciCore = ciT; ciCore.dst = dstAuto;
  CastChart(1);
  rSun1 = planet[oSun]; rCusp1 = chouse[1];
  Check(ciCore.dst == dstAuto,
    "an auto-DST chart reads as typed after the cast");
  ciCore = ciT; ciCore.dst = 1.0;
  CastChart(1);
  Check(planet[oSun] == rSun1 && chouse[1] == rCusp1,
    "auto-DST with is.fDst set casts as dst 1");
  is.fDst = fFalse;
  ciCore = ciT; ciCore.dst = dstAuto;
  CastChart(1);
  rSun2 = planet[oSun]; rCusp2 = chouse[1];
  ciCore = ciT;
  CastChart(1);
  Check(planet[oSun] == rSun2 && chouse[1] == rCusp2,
    "auto-DST with is.fDst clear casts as dst 0");
  Check(rSun1 != rSun2, "the two DST states cast different charts");

  // The exact pole is clamped just off it, and only cooked -- the typed
  // 90 survives.
  ciCore = ciT; ciCore.lat = rDegQuad;
  CastChart(1);
  rSun1 = planet[oSun]; rCusp1 = chouse[1];
  Check(ciCore.lat == rDegQuad, "a pole chart reads as typed after the cast");
  ciCore = ciT; ciCore.lat = rDegQuad - rSmall;
  CastChart(1);
  Check(planet[oSun] == rSun1 && chouse[1] == rCusp1,
    "latitude 90 casts as the clamped rDegQuad - rSmall");

  // The Matrix house backend's topocentric system, which no other group
  // reaches (the suite runs with Swiss ephemeris on): pin two cusps to
  // literal constants, matrix.cpp's only standing net. The constants
  // assume this exact environment, so every cast-relevant knob a
  // previous group may have left set is borrowed to its default --
  // TestAllMenuActionsQt fires all 338 menu items and leaves five of
  // these dirty (found the hard way: the pin held alone and failed in
  // the full suite).
  {
    Borrow bEphem(us.fEphemFiles, fFalse), bSid(us.fSidereal, fFalse);
    Borrow b3D(us.fHouse3D, fFalse), bProg(us.fProgress, fFalse);
    Borrow bEqu(us.fEquator, fFalse), bEqu2(us.fEquator2, fFalse);
    Borrow bFlip(us.fFlip, fFalse), bGeo(us.fGeodetic, fFalse);
    Borrow bRotW(us.fObjRotWhole, fFalse), bExp(us.fExpOff, fTrue);
    Borrow bHouse(us.nHouseSystem, (int)hsTopocentric);
    Borrow bCtr(us.objCenter, (int)oEar), bRel(us.nRel, (int)rcNone);
    Borrow bZoff(us.rZodiacOffset, 0.0), bZall(us.rZodiacOffsetAll, 0.0);
    Borrow bCusp(us.rCuspAddition, 0.0);
    Borrow bAsc(us.objOnAsc, 0), bRot1(us.objRot1, 0), bRot2(us.objRot2, 0);
    ciCore = ciT;
    CastChart(1);
    Check(RAbs(chouse[1] - 86.684187020121769) < 1e-9 &&
      RAbs(chouse[5] - 184.489550958797622) < 1e-9,
      "matrix topocentric cusps sit at their pinned positions (%f %f)",
      chouse[1], chouse[5]);
  }

  is.fDst = fDstSav;
  ciCore = ciCoreSav; ciMain = ciMainSav;
  CastChart(1);            // Put the shared chart state back for the rest.
  SetNoPopupQt(fPopupSav);
}


// REFACTORING.md B1: the file-format importers read lines through what
// used to be hand-rolled readers with drifting truncation points, now
// the two FReadSzLine* helpers in io.cpp -- a line fits cchSzLine-1
// (1019 chars) for AAF, Astrodatabank, Solar Fire and calendar, and
// cchSzMax-1 (254) for Quick*Chart's fixed 100-column lines, while
// switch files realloc-grow. Solar Fire and calendar originally read
// only cchSzMax into their cchSzLine buffers; the buffer's declared
// intent won (work log item 120), and the fixtures here pin both the
// new whole-buffer reads and what each importer still does past its
// real limit, so nothing can move a truncation point by accident. The
// overflow cases double as regression tests for two real crashers this
// group's first probe run found: FProcessAAFFile sprintf'd unbounded
// name and location fields into a cchSzMax buffer, and FProcessADBFile
// concatenated two cchSzDef strings into one.
static void WriteParserFileQt(CONST char *szFile, CONST char *sz)
{
  FILE *file = fopen(szFile, "wb");

  if (file != NULL) {
    fwrite(sz, 1, CchSz(sz), file);
    fclose(file);
  }
}

// Load one fixture through the real entry point, from cleared chart info
// so a field the parser never wrote can't inherit the previous case's.
static flag FLoadParserFileQt(CONST char *szFile)
{
  ciCore.nam = ciCore.loc = "";
  MM = DD = YY = 0;
  TT = SS = ZZ = OO = AA = 0.0;
  return FInputData(szFile);
}

static void TestFileParsersQt()
{
  char szFile[cchSzMax], sz[8192], szPad[2048];
  CI ciCoreSav = ciCore;
  int cciSav = is.cci, i;
  flag fHaveSav = is.fHaveInfo, fPopupSav = FNoPopupQt(), fRet;

  Group("File import long lines");
  SetNoPopupQt(fTrue);
  for (i = 0; i < 2047; i++)
    szPad[i] = 'P';
  szPad[2047] = chNull;
  sprintf2(S(szFile), "%s/astrolog-qt-parserfixture-%d.tmp",
    QDir::tempPath().toLocal8Bit().constData(), (int)QCoreApplication::applicationPid());

  // iCalendar, fgets through the whole cchSzLine buffer: a 400-char
  // SUMMARY arrives intact (it used to truncate at 246, the old
  // cchSzMax read this fixture caught -- work log item 120). Past the
  // real limit the old behavior still holds: the first 1011 characters
  // (1019 minus "SUMMARY:") become the name, and the tail reads as an
  // unknown keyword line.
  WriteParserFileQt(szFile,
    "BEGIN:VCALENDAR\nBEGIN:VEVENT\nSUMMARY:Cal Control\n"
    "LOCATION:Seattle\nDTSTART:20200304T050607\nEND:VEVENT\n"
    "END:VCALENDAR\n");
  fRet = FLoadParserFileQt(szFile);
  Check(fRet && FEqSz(ciCore.nam, "Cal Control") &&
    FEqSz(ciCore.loc, "Seattle") && MM == 3 && DD == 4 && YY == 2020,
    "calendar control loads (nam '%s' loc '%s' %d/%d/%d)",
    ciCore.nam, ciCore.loc, MM, DD, YY);
  sprintf2(S(sz), "BEGIN:VCALENDAR\nBEGIN:VEVENT\nSUMMARY:%.400s\n"
    "LOCATION:Seattle\nDTSTART:20200304T050607\nEND:VEVENT\n"
    "END:VCALENDAR\n", szPad);
  WriteParserFileQt(szFile, sz);
  fRet = FLoadParserFileQt(szFile);
  Check(fRet && CchSz(ciCore.nam) == 400 &&
    ciCore.nam[0] == 'P' && MM == 3,
    "calendar 400-char summary arrives whole (%d)", CchSz(ciCore.nam));
  sprintf2(S(sz), "BEGIN:VCALENDAR\nBEGIN:VEVENT\nSUMMARY:%.1100s\n"
    "LOCATION:Seattle\nDTSTART:20200304T050607\nEND:VEVENT\n"
    "END:VCALENDAR\n", szPad);
  WriteParserFileQt(szFile, sz);
  fRet = FLoadParserFileQt(szFile);
  Check(fRet && CchSz(ciCore.nam) == cchSzLine-1-8 &&
    ciCore.nam[0] == 'P' && MM == 3,
    "calendar 1100-char summary keeps its first %d characters (%d)",
    cchSzLine-1-8, CchSz(ciCore.nam));

  // Solar Fire text, fgets through the whole cchSzLine buffer: a
  // 300-char name line loads now (it used to poison the whole file at
  // 255 -- work log item 120). A name line past the real limit still
  // does: its tail is consumed as the date line, the real date line
  // reads as the location line, and range validation rejects
  // everything -- no chart is appended at all.
  WriteParserFileQt(szFile,
    "\nCreated by Esoteric Technologies\n\n"
    "SF Control - Natal Chart\n"
    "Mar 4 2020, 5:06 am, +5:00\n"
    "Seattle WA, 47N36 00, 122W19 00\n\n");
  fRet = FLoadParserFileQt(szFile);
  Check(fRet && FEqSz(ciCore.nam, "SF Control") &&
    FEqSz(ciCore.loc, "Seattle WA") && MM == 3 && DD == 4 && YY == 2020,
    "Solar Fire control loads (nam '%s' loc '%s' %d/%d/%d)",
    ciCore.nam, ciCore.loc, MM, DD, YY);
  sprintf2(S(sz), "\nCreated by Esoteric Technologies\n\n"
    "%.300s\n"
    "Mar 4 2020, 5:06 am, +5:00\n"
    "Seattle WA, 47N36 00, 122W19 00\n\n", szPad);
  WriteParserFileQt(szFile, sz);
  fRet = FLoadParserFileQt(szFile);
  Check(fRet && CchSz(ciCore.nam) == 300 && MM == 3 && DD == 4,
    "Solar Fire 300-char name line loads whole (ret=%d nam %d)",
    fRet, CchSz(ciCore.nam));
  i = is.cci;
  sprintf2(S(sz), "\nCreated by Esoteric Technologies\n\n"
    "%.1100s\n"
    "Mar 4 2020, 5:06 am, +5:00\n"
    "Seattle WA, 47N36 00, 122W19 00\n\n", szPad);
  WriteParserFileQt(szFile, sz);
  fRet = FLoadParserFileQt(szFile);
  Check(!fRet && is.cci == i && CchSz(ciCore.nam) == cchSzLine-1,
    "Solar Fire 1100-char name line rejects the file (ret=%d nam %d)",
    fRet, CchSz(ciCore.nam));

  // AAF, getc loop through cchSzLine. The name and location fields are
  // assembled from two slices of the input line each, and the buffer they
  // are assembled in used to be cchSzMax -- narrower than one field -- so
  // a long name was cut at 254 and a long city dropped its country
  // outright. It is cchSzLine*2 since 2026-09-01 (work log item 152), so
  // a field pair that fits on one input line survives whole. This is the
  // group that first found the crasher here: both assembly sprintfs were
  // unbounded before work log item 118.
  WriteParserFileQt(szFile,
    "#A93:*,First Last,*,4.3.2020,5:06,Seattle,WA (USA)\n"
    "#B93:2458912.5,47N36,122W19,+5:00,0\n");
  fRet = FLoadParserFileQt(szFile);
  Check(fRet && FEqSz(ciCore.nam, "First Last") &&
    FEqSz(ciCore.loc, "Seattle, WA, USA") && MM == 3 && DD == 4,
    "AAF control loads (nam '%s' loc '%s' %d/%d/%d)",
    ciCore.nam, ciCore.loc, MM, DD, YY);
  sprintf2(S(sz), "#A93:*,First %.400s,*,4.3.2020,5:06,Sea%.400sttle,WA (USA)\n"
    "#B93:2458912.5,47N36,122W19,+5:00,0\n", szPad, szPad);
  WriteParserFileQt(szFile, sz);
  fRet = FLoadParserFileQt(szFile);
  // "First " + 400 pad = 406; "Sea" + 400 pad + "ttle" + ", " + "WA, USA"
  // = 416. Both are what the input actually carried, not a buffer limit.
  Check(fRet && CchSz(ciCore.nam) == 406 &&
    CchSz(ciCore.loc) == 416 && MM == 3 && DD == 4,
    "AAF 400-char name and location survive whole (%d, %d)",
    CchSz(ciCore.nam), CchSz(ciCore.loc));
  // And a line past cchSzLine splits: the tail reads as its own line,
  // which can't start with '#', so the file is rejected.
  i = is.cci;
  sprintf2(S(sz), "#: %.1200s\n"
    "#A93:*,First Last,*,4.3.2020,5:06,Seattle,WA (USA)\n"
    "#B93:2458912.5,47N36,122W19,+5:00,0\n", szPad);
  WriteParserFileQt(szFile, sz);
  fRet = FLoadParserFileQt(szFile);
  Check(!fRet && is.cci == i,
    "AAF 1200-char comment line splits and rejects the file (ret=%d)",
    fRet);

  // Astrodatabank, the same getc loop. City and country each fit
  // cchSzDef, but their joined form is truncated to it -- the second
  // crasher: the join was an unbounded sprintf of two full buffers.
  WriteParserFileQt(szFile,
    "<adb_entry>\n"
    "<x imonth=\"3\" iday=\"4\" iyear=\"2020\" sbtime_ampm=\"5:06 AM\"\n"
    "ctimetype=\"h\" stmerid=\"5E\">\n"
    "<sflname>ADB Control</sflname>\n"
    "<place slong=\"122w19\" slati=\"47n36\">Seattle</place>\n"
    "<country>USA</country>\n"
    "</adb_entry>\n");
  fRet = FLoadParserFileQt(szFile);
  Check(fRet && FEqSz(ciCore.nam, "ADB Control") &&
    FEqSz(ciCore.loc, "Seattle, USA") && MM == 3 && DD == 4 && YY == 2020,
    "ADB control loads (nam '%s' loc '%s' %d/%d/%d)",
    ciCore.nam, ciCore.loc, MM, DD, YY);
  sprintf2(S(sz),
    "<adb_entry>\n"
    "<x imonth=\"3\" iday=\"4\" iyear=\"2020\" sbtime_ampm=\"5:06 AM\"\n"
    "ctimetype=\"h\" stmerid=\"5E\">\n"
    "<sflname>ADB Control</sflname>\n"
    "<place slong=\"122w19\" slati=\"47n36\">%.79s</place>\n"
    "<country>%.79s</country>\n"
    "</adb_entry>\n", szPad, szPad);
  WriteParserFileQt(szFile, sz);
  fRet = FLoadParserFileQt(szFile);
  // 79 + ", " + 79 = 160. The join buffer used to be cchSzDef, the width
  // of ONE of the two fields, so a long city name discarded the country
  // entirely (work log item 152).
  Check(fRet && CchSz(ciCore.loc) == 160 && MM == 3,
    "ADB 79-char city and country join keeps both (%d)",
    CchSz(ciCore.loc));

  // Quick*Chart, fgets whose buffer matches its limit; the fixed
  // 100-column layout can't reach it. A control only.
  sprintf2(S(sz), "%-23s%-3s%-4s%-5s%-12s%-3s%-6s%-10s%-9s%-25s\n",
    "Quick Control", "Mar", "4", "2020", "5:06am", "EST", "+5:00",
    "122W19", "47N36", "Seattle WA");
  WriteParserFileQt(szFile, sz);
  fRet = FLoadParserFileQt(szFile);
  Check(fRet && FEqSz(ciCore.nam, "Quick Control") &&
    FEqSz(ciCore.loc, "Seattle WA") && MM == 3 && DD == 4 && YY == 2020,
    "Quick*Chart control loads (nam '%s' loc '%s' %d/%d/%d)",
    ciCore.nam, ciCore.loc, MM, DD, YY);

  // Switch files are the one reader with a growth policy: the buffer
  // realloc-doubles, so a 2000-character line arrives whole.
  sprintf2(S(sz), "@0308  ; Astrolog chart info.\n-zi \"Switch Control\" "
    "\"%.2000s\"\n-qb 3 4 2020 5:06 0 5:00 122:19W 47:36N\n", szPad);
  WriteParserFileQt(szFile, sz);
  fRet = FLoadParserFileQt(szFile);
  Check(fRet && FEqSz(ciCore.nam, "Switch Control") &&
    CchSz(ciCore.loc) == 2000 && MM == 3 && DD == 4 && YY == 2020,
    "switch file 2000-char line arrives whole (loc %d)",
    CchSz(ciCore.loc));

  remove(szFile);
  is.cci = cciSav;
  is.fHaveInfo = fHaveSav;
  ciCore = ciCoreSav;
  SetNoPopupQt(fPopupSav);
}


// Work log item 115: item 114's crasher class -- a user-supplied string
// formatted through a fixed-size line buffer -- pinned across the whole
// text chart surface rather than just the two functions caught crashing.
// The IBM line drawing divergence, in its own group and next to
// TestLongStringsQt() because it shares that test's hazard: it calls
// Action() to render a text chart to a file, which needs the chart
// state put back afterwards or later groups inherit a half-redirected
// output stream. Run inside the divergences group it aborted the suite
// with "invalid stdio handle" several groups later.
//
// charts1.cpp and general.cpp clear us.fAnsiChar and swap the degree
// glyph whenever gs.nFontTxt > 0, under #ifdef WIN, because the Windows
// text window can be set to a font with no box drawing in it. This port
// draws text charts in a fixed Liberation Mono, which has every one of
// those characters, so copying the guard would strip the rules out of
// grids that render correctly. Widening that ifdef to QT fails this.

static void TestLineDrawingQt()
{
  CI ciMainSav = ciMain, ciCoreSav = ciCore;
  int nFontSav = gs.nFontTxt, j;
  flag rgfSav[48], fPopupSav = FNoPopupQt();
  byte rgbIgnSav[oNorm+1];
  static char szFont0[65536];
  long cbFont0 = 0;
  char szOut[cchSzMax];
  FILE *file;
  long cb = 0, cRule = 0;

  Group("IBM line drawing");
  SetNoPopupQt(fTrue);
  {
    Borrow bGraph(us.fGraphics, fFalse), bProg(us.fProgress, fFalse);
    Borrow bRel(us.nRel, (int)rcNone);
    // us.fAnsiChar is the precondition, not the subject: the claim is
    // that gs.nFontTxt does not *clear* it. TestAllMenuActionsQt() fires
    // 338 items and leaves it wherever they land, so set it here or the
    // test reads "no rules" for the wrong reason (it did, in the full
    // run, while passing alone).
    Borrow bAnsi(us.fAnsiChar, (int)fTrue);
    // Same reason, one level further: the grid's size is the number of
    // unrestricted objects, and with almost everything restricted it
    // renders a stub with no rules in it at all. Give the test its own
    // object set rather than inherit 338 menu items' worth.
    for (j = 0; j <= oNorm; j++)
      rgbIgnSav[j] = ignore[j];
    for (j = 0; j <= oNorm; j++)
      ignore[j] = (j > oCore);
    AdjustRestrictions();

    for (j = 0; j < cchartmode && j < 48; j++) {
      rgfSav[j] = *rgchartmode[j].pf;
      *rgchartmode[j].pf = fFalse;
    }
    us.fGrid = fTrue;
    sprintf2(S(szOut), "%s/astrolog-qt-linedraw-%d.txt",
      QDir::tempPath().toLocal8Bit().constData(), (int)QCoreApplication::applicationPid());
    FCloneSz(szOut, &is.szFileScreen);

    // The claim is that gs.nFontTxt changes nothing about a *text*
    // chart here, so render the same grid with it off and on and demand
    // the bytes match. Comparing two renders rather than hunting for
    // particular characters keeps this independent of the charset (-Ya
    // encodes the same rules as one byte or three) and of whichever
    // objects an earlier group left unrestricted.
    for (j = 0; j <= 1; j++) {
      gs.nFontTxt = j;
      remove(szOut);
      Action();
      file = fopen(szOut, "rb");
      cb = 0;
      if (file != NULL) {
        int ch;
        while ((ch = fgetc(file)) != EOF) {
          if (j == 0) {
            if (cb < (long)sizeof(szFont0))
              szFont0[cb] = (char)ch;
          } else if (cb < cbFont0 && cb < (long)sizeof(szFont0) &&
            szFont0[cb] != (char)ch)
            cRule++;
          cb++;
        }
        fclose(file);
      }
      if (j == 0)
        cbFont0 = cb;
    }
    remove(szOut);
    FCloneSz(NULL, &is.szFileScreen);
    Check(cbFont0 > 100, "the grid chart wrote %ld bytes, so this proves "
      "nothing", cbFont0);
    Check(cb == cbFont0 && cRule == 0,
      "a text font changed the text chart: %ld bytes vs %ld, %ld bytes "
      "differing -- that is the #ifdef WIN line-drawing behaviour, which "
      "this port must not copy", cb, cbFont0, cRule);
    gs.nFontTxt = nFontSav;
    for (j = 0; j < cchartmode && j < 48; j++)
      *rgchartmode[j].pf = rgfSav[j];
    for (j = 0; j <= oNorm; j++)
      ignore[j] = rgbIgnSav[j];
    AdjustRestrictions();
  }
  ciMain = ciMainSav; ciCore = ciCoreSav;
  CastChart(1);              // Put the shared chart state back for the rest.
  SetNoPopupQt(fPopupSav);
}


// Every mode in rgchartmode[] is rendered to a file with a 120-character
// chart name and location in place; each one surviving with output is
// the assertion, the way TestBadInputQt() treats a crash. This is the
// battery that would have caught PrintHeader() before a user's saved
// eclipse chart did.
static void TestLongStringsQt()
{
  static char szLongNam[121], szLongLoc[121];
  char szOut[cchSzMax];
  FILE *fileSav;
  CI ciMainSav = ciMain, ciCoreSav = ciCore;
  flag rgfSav[48], fPopupSav = FNoPopupQt();
  long cb;
  int i, j;
  FILE *file;

  Group("Long strings through every text chart");
  Check(cchartmode <= 48, "the flag snapshot holds the table (%d)", cchartmode);
  // A text search mode can warn (missing ephemeris range, say), and a
  // warning is a modal box nothing will click.
  SetNoPopupQt(fTrue);
  {
    Borrow bSec(us.fSeconds, fTrue), bGraph(us.fGraphics, fFalse);
    Borrow bProg(us.fProgress, fFalse);
    Borrow bRel(us.nRel, (int)rcNone);

    for (i = 0; i < 120; i++) {
      szLongNam[i] = 'N';
      szLongLoc[i] = 'L';
    }
    szLongNam[120] = szLongLoc[120] = chNull;
    ciMain.nam = szLongNam; ciMain.loc = szLongLoc;
    ciCore = ciMain;
    for (i = 0; i < cchartmode; i++)
      rgfSav[i] = *rgchartmode[i].pf;
    sprintf2(S(szOut), "%s/astrolog-qt-longstrings-%d.txt",
      QDir::tempPath().toLocal8Bit().constData(), (int)QCoreApplication::applicationPid());
    FCloneSz(szOut, &is.szFileScreen);

    // Action() opens is.S on is.szFileScreen and fclose()s it on the way
    // out WITHOUT putting the caller's back (astrolog.cpp:151 and :338),
    // and the whole GUI runs inside an Action() already, so every call
    // below is a nested one. Restoring is.S is hygiene the rest of this
    // file observes -- CaptureTextToFileQt() exists for it and carries
    // the long reasoning.
    //
    // It is NOT the cause of this group's intermittent "(0 bytes)"
    // failures, and saying so here is the point: adding this restore
    // moved the rate from 2 of 5 to 4 of 6, which is to say not at all.
    // Work log item 164 has what the hunt did establish, including the
    // reproduction the fix was tested against.
    fileSav = is.S;
    for (i = 0; i < cchartmode; i++) {
      for (j = 0; j < cchartmode; j++)
        *rgchartmode[j].pf = (j == i);
      remove(szOut);
      Action();
      is.S = fileSav;
      cb = 0;
      file = fopen(szOut, "rb");
      if (file != NULL) {
        fseek(file, 0, SEEK_END);
        cb = ftell(file);
        fclose(file);
      }
      Check(file != NULL && cb > 0,
        "mode %d survives 120-char name and location (%ld bytes)",
        rgchartmode[i].nMode, cb);
    }

    remove(szOut);
    FCloneSz(NULL, &is.szFileScreen);
    for (i = 0; i < cchartmode; i++)
      *rgchartmode[i].pf = rgfSav[i];
  }
  ciMain = ciMainSav; ciCore = ciCoreSav;
  CastChart(1);              // Put the shared chart state back for the rest.
  SetNoPopupQt(fPopupSav);
}


// A sign's Ray list packs its Rays as decimal digits (-Y7C), and the
// switch range checks the composed number rather than each digit -- so a
// digit naming no Ray reaches DrawFillWheel(), which used to index the
// nine-slot Ray colour table with it. "-Y7C 1 1 999 -Xv 6" read
// rgbbmpRay[9] and drew whatever followed the array (work log items
// 129-130; ASan called it a global-buffer-overflow at xcharts0.cpp:695).
//
// The rule this pins is EnsureRay()'s: a digit outside 1..cRay names no
// Ray, so it reads as absent. The three all-invalid lists below must
// therefore render exactly like an empty list, and the valid one must
// not -- without that last leg the test would pass on a build that had
// stopped filling by Ray at all.
//
// A regression here shows up as SIGABRT, not as a FAIL line: the checked
// table's range assert (E2) fires inside operator[] before the image
// comparison is ever reached. That is the test working, not the test
// broken -- proven by reintroducing the bug, which aborts exactly here.

static void TestRayDigitFillQt()
{
  int nSav = gi.nMode, nRaySav, nFillSav;
  real rBackSav;
  QImage imEmpty, imValid, imTwo;
  int i;
  CONST int rgnBad[] = {999, 888, 909};
  CONST char *rgszBad[] = {"999", "888", "909"};

  Group("Ray digit wheel fill");
  nRaySav = rgSignRay[SIGT(sAri)];
  nFillSav = gs.nDecaFill;
  rBackSav = gs.rBackPct;
  gs.nDecaFill = 6;          // Ray sign fill.
  gs.rBackPct = 100.0;       // DrawFillWheel() returns early below this.

  rgSignRay[SIGT(sAri)] = 0;
  SetChartModeQt(gWheel);
  if (gi.qim != NULL)
    imEmpty = gi.qim->copy();
  Check(!imEmpty.isNull(), "no image rendered for an empty Ray list");

  rgSignRay[SIGT(sAri)] = 123;
  SetChartModeQt(gWheel);
  if (gi.qim != NULL)
    imValid = gi.qim->copy();
  Check(!imValid.isNull() && imValid != imEmpty,
    "Rays 1/2/3 fill the same as no Rays at all -- this test cannot see "
    "the Ray fill, so its other legs prove nothing");

  for (i = 0; i < 3; i++) {
    rgSignRay[SIGT(sAri)] = rgnBad[i];
    SetChartModeQt(gWheel);
    Check(gi.qim != NULL && *gi.qim == imEmpty,
      "Ray list \"%s\" (no digit names a Ray) did not render as an empty "
      "list", rgszBad[i]);
  }

  // The largest value -Y7C accepts is the one that made the old code
  // read furthest out: its third "digit" is 1234567/100 == 12345. Its
  // low two digits are Rays 7 and 6 though, so it is not an empty list
  // -- it must render as exactly those two, with the third absent.
  rgSignRay[SIGT(sAri)] = 67;
  SetChartModeQt(gWheel);
  if (gi.qim != NULL)
    imTwo = gi.qim->copy();
  Check(!imTwo.isNull() && imTwo != imEmpty, "Rays 7/6 fill as no Rays");
  rgSignRay[SIGT(sAri)] = 1234567;
  SetChartModeQt(gWheel);
  Check(gi.qim != NULL && *gi.qim == imTwo,
    "Ray list \"1234567\" did not render as Rays 7 and 6 with the "
    "third position absent");

  rgSignRay[SIGT(sAri)] = nRaySav;
  gs.nDecaFill = nFillSav;
  gs.rBackPct = rBackSav;
  SetChartModeQt(nSav);
}


// Display Settings' aspect count must move in both directions. Raising
// it un-restricts what it now includes, which needs a loop reading the
// *old* us.nAsp -- and that loop was deleted in a transcription pass
// (commit bf92b9e), leaving only the loop that restricts. AdjustAspectCount()
// then recomputed the count straight back down from the restrictions, so
// the field could be lowered and never raised: a one-way ratchet that
// silently did nothing, and that no assertion or audit could see. Found
// by driving a real config through the live GUI (item 7's practice).
//
// Windows has the same symptom by a different route -- its loop is there
// but assigns us.nAsp before running, so it iterates zero times. Not
// reproducing that is a deliberate divergence (QT_GUI_PLAN.md 8.12).
//
// The field carries no accessible name, so it is found the way the
// deCh_L pair is: seed a distinctive value and match on it.

static void TestAspectCountQt()
{
  int nAspSav = us.nAsp, i;
  byte rgbSav[cAspect+1];

  Group("Display Settings aspect count");
  for (i = 1; i <= cAspect; i++)
    rgbSav[i] = ignorea[ASPT(i)];

  // A count of 11 with everything above it restricted, so the field
  // reads "11" and nothing else in the dialog does.
  for (i = 1; i <= cAspect; i++)
    ignorea[ASPT(i)] = (i > 11);
  us.nAsp = 11;

  DriveModalQt(ShowDisplayDialogQt, [](QWidget *pw) {
    QLineEdit *pe = NULL;
    QPushButton *ppbOK = NULL;
    for (QLineEdit *p : pw->findChildren<QLineEdit *>())
      if (p->text() == "11") pe = p;
    for (QPushButton *p : pw->findChildren<QPushButton *>())
      if (p->text() == "OK") ppbOK = p;
    if (pe != NULL) pe->setText("20");
    if (ppbOK != NULL) ppbOK->click();
  });
  Check(us.nAsp == 20, "raising the aspect count 11 -> 20 left it at %d",
    us.nAsp);
  Check(!ignorea[ASPT(20)],
    "aspect 20 is still restricted after the count was raised to include "
    "it -- the count reads high but the aspect cannot appear");
  Check(!ignorea[ASPT(12)], "aspect 12 was not un-restricted either");

  // And down again: the restricting half must still work.
  DriveModalQt(ShowDisplayDialogQt, [](QWidget *pw) {
    QLineEdit *pe = NULL;
    QPushButton *ppbOK = NULL;
    for (QLineEdit *p : pw->findChildren<QLineEdit *>())
      if (p->text() == "20") pe = p;
    for (QPushButton *p : pw->findChildren<QPushButton *>())
      if (p->text() == "OK") ppbOK = p;
    if (pe != NULL) pe->setText("3");
    if (ppbOK != NULL) ppbOK->click();
  });
  Check(us.nAsp == 3, "lowering the aspect count 20 -> 3 left it at %d",
    us.nAsp);
  Check(ignorea[ASPT(4)], "aspect 4 is unrestricted below a count of 3");

  for (i = 1; i <= cAspect; i++)
    ignorea[ASPT(i)] = rgbSav[i];
  us.nAsp = nAspSav;
}


// The guard for "Known divergences from Windows".
//
// A divergence is a claim about behaviour that no audit can check: the
// four rc_*_audit.py scripts all compare this port *against*
// astrolog.rc, so anywhere it intends to differ is outside what they
// can see by construction. Prose was the only record, and prose does
// not fail -- which is how Display Settings' aspect count reverted to
// the Windows bug and stayed there for months (work log item 131).
// Every divergence that is a testable behavioural claim gets one here.
//
// Already covered elsewhere, deliberately not repeated: the dialog
// arrow keys (arrow-keys), animation's single running state
// (animation), and the aspect count itself (aspect-count).

static void TestDivergencesQt()
{
  Group("Windows divergences");

  // Windows resolves dstAuto through DstReal() before showing it, so a
  // chart typed as "work it out for me" comes back as a concrete 0 or 1
  // on the next OK. This port shows Autodetect and keeps it.
  real dstSav = ciCore.dst;
  ciCore.dst = dstAuto;
  DriveModalQt(ShowChartInfoDialogQt, [](QWidget *pw) {
    for (QPushButton *p : pw->findChildren<QPushButton *>())
      if (p->text() == "OK") p->click();
  });
  Check(ciCore.dst == dstAuto,
    "an Autodetect daylight setting did not survive the chart info "
    "dialog (dst is now %.2f) -- Windows resolves it away, this port "
    "must not", ciCore.dst);
  ciCore.dst = dstSav;

  // Atlas City Coloring drives gs.fLabelAsp, not gs.fLabelCity, and
  // that is correct however odd it reads: -XL plots the cities and -XA
  // gates whether they are coloured, which the -H text states outright
  // ("-XL[1-5]: ... set how to color cities (when -XA is on)").
  // xcharts0.cpp:2088 and xcharts1.cpp:174 are the two readers. The
  // plan used to call this an upstream typo and propose writing
  // fLabelCity instead, which would have toggled whether cities appear
  // at all -- a different control. Pinned here so the claim stays
  // checked rather than re-argued.
  flag fAspSav = gs.fLabelAsp, fCitySav = gs.fLabelCity;
  int nCitySav = gs.nLabelCity;
  gs.fLabelAsp = fFalse; gs.nLabelCity = 1;
  DriveModalQt(ShowGraphicsSettingsDialogQt, [](QWidget *pw) {
    for (QComboBox *p : pw->findChildren<QComboBox *>())
      if (p->currentText() == "None")
        p->setEditText("Rainbow");
    for (QPushButton *p : pw->findChildren<QPushButton *>())
      if (p->text() == "OK") p->click();
  });
  Check(gs.fLabelAsp,
    "picking a city colouring left gs.fLabelAsp clear, so the cities "
    "will draw flat orange");
  Check(gs.nLabelCity == 5,
    "city colouring scheme is %d, wanted 5 (Rainbow)", gs.nLabelCity);
  Check(gs.fLabelCity == fCitySav,
    "picking a colouring also changed whether cities are plotted at all");
  gs.fLabelAsp = fAspSav; gs.fLabelCity = fCitySav;
  gs.nLabelCity = nCitySav;

}


static CONST QTTESTENTRY rgqttestQt[] = {
  {"dialogs",              TestDialogsQt},
  {"context-menus",        TestContextMenusQt},
  {"hotkeys",              TestHotkeysQt},
  {"chart-render",         TestChartRenderQt},
  {"ray-digit-fill",       TestRayDigitFillQt},
  {"aspect-count",         TestAspectCountQt},
  {"divergences",          TestDivergencesQt},
  {"menu-actions",         TestAllMenuActionsQt},
  {"menu-parity",          TestMenuParityQt},
  {"bad-input",            TestBadInputQt},
  {"forced-positions",     TestForcedPositionsQt},
  {"shared-core",          TestSharedCoreFixesQt},
  {"rulership",            TestRulershipTablesQt},
  {"esoteric-tables",      TestEsotericTablesQt},
  {"nested-include",       TestNestedIncludeQt},
  {"registry",             TestRegistryQt},
  {"relationship",         TestRelationshipModeQt},
  {"ephemeris-list",       TestEphemerisListQt},
  {"chart-list",           TestChartListFilterQt},
  {"expression-hooks",     TestExpressionHooksQt},
  {"accel-text",           TestAccelTextQt},
  {"expression-functions", TestExpressionFunctionsQt},
  {"objsel-table",         TestObjSelTableQt},
  {"timers",               TestTimerSanityQt},
  {"objsel-dialog",        TestObjSelDialogQt},
  {"objsel-parse",         TestObjSelParseQt},
  {"color-scheme",         TestColorSchemeQt},
  {"app-icon",             TestAppIconQt},
  {"dialog-buttons",       TestDialogButtonWiringQt},
  {"shared-symbols",       TestSharedSymbolBoxesQt},
  {"clear-screen",         TestClearScreenQt},
  {"text-export",          TestTextExportQt},
  {"rising-gradient",      TestRisingGradientQt},
  {"animation",            TestAnimationStateQt},
  {"mnemonics",            TestDialogMnemonicsQt},
  {"arrow-keys",           TestDialogArrowKeysQt},
  {"midpoint-glyph",       TestMidpointGlyphQt},
  {"objsel-lookup",        TestObjSelLookupQt},
  {"custom-parse",         TestCustomDialogParseQt},
  {"objdef-set",           TestObjDefSetQt},
  {"objsel-glyph",         TestObjSelGlyphQt},
  {"settings-roundtrip",   TestSettingsRoundTripQt},
  {"atlas-sink",           TestAtlasSinkQt},
  {"chartmode-table",      TestChartModeTableQt},
  {"cast-cooking",         TestCastCookingQt},
  {"line-drawing",         TestLineDrawingQt},
  {"long-strings",         TestLongStringsQt},
  {"file-parsers",         TestFileParsersQt},
  {"oracle",               TestNumericOracleQt}};
#define cqttestQt (int)(sizeof(rgqttestQt) / sizeof(QTTESTENTRY))

// Does any comma-separated token of the filter appear in the name?
static flag FTestWantedQt(CONST char *szFilter, CONST char *szName)
{
  char szLow[cchSzMax], szTok[cchSzMax];
  int i, j;

  if (szFilter == NULL)
    return fTrue;
  for (i = 0; szName[i] != chNull && i < cchSzMax-1; i++)
    szLow[i] = ChUncap(szName[i]);
  szLow[i] = chNull;
  i = 0;
  while (szFilter[i] != chNull) {
    for (j = 0; szFilter[i] != chNull && szFilter[i] != ','; i++)
      if (j < cchSzMax-1)
        szTok[j++] = ChUncap(szFilter[i]);
    szTok[j] = chNull;
    if (j > 0 && strstr(szLow, szTok) != NULL)
      return fTrue;
    if (szFilter[i] == ',')
      i++;
  }
  return fFalse;
}

static int NRunQtTestTableQt()
{
  CONST char *szFilter = getenv("ASTROLOG_QT_TESTS"), *szEphem;
  flag fTime = szFilter != NULL || getenv("ASTROLOG_QT_TIME") != NULL;
  QElapsedTimer timerTest;
  int i, cRun = 0;

  if (szFilter != NULL && FEqSzI(szFilter, "list")) {
    for (i = 0; i < cqttestQt; i++)
      printf("%s\n", rgqttestQt[i].szName);
    return fFalse;
  }
  // A misspelled mode must not mean "full" by accident. ASTROLOG_QT_EPHEM
  // exists to make the run's coverage a stated claim, and a typo that
  // silently falls back to the default would make it a stated wrong one.
  szEphem = getenv("ASTROLOG_QT_EPHEM");
  if (szEphem != NULL && !FEqSzI(szEphem, "full") &&
    !FEqSzI(szEphem, "minimal")) {
    printf("\nFAIL: ASTROLOG_QT_EPHEM=\"%s\" is neither \"full\" nor "
      "\"minimal\"\n", szEphem);
    return fTrue;
  }
  // No message boxes, for the whole run. This is not tidiness: a modal
  // box in an unattended run is a hang, and it is a hang that only
  // appears where /swe is absent -- which is every machine except the
  // maintainer's, CI included. Found 2026-09-02 by running the suite the
  // way CI has to run it, "-Yi1 ephem": the Chart rendering group blocked
  // past a ten-minute timeout on the Moons chart, sitting in do_poll() at
  // 1.9% CPU, while the same chart from the console build drew in 0.01 s.
  // Nothing was slow; PrintWarningQt() had put up a box about a missing
  // ephemeris file and there was nobody to dismiss it.
  //
  // It is set here rather than per group because 40 of the 49 groups did
  // not set it, and every one of them was one missing ephemeris file away
  // from the same hang. The flag gates exactly two things -- warning boxes
  // (qtdriver.cpp:649) and the network progress dialog (:4574) -- and an
  // automated run wants neither. A group that specifically tests popup
  // behaviour still turns it off for itself and restores it, which is what
  // TestExpressionFunctionsQt does.
  SetNoPopupQt(fTrue);
  printf("Astrolog Qt test suite\n");
  if (szEphem != NULL)
    printf("  ephemeris mode: %s\n", szEphem);
  for (i = 0; i < cqttestQt; i++) {
    if (!FTestWantedQt(szFilter, rgqttestQt[i].szName))
      continue;
    cRun++;
    timerTest.start();
    rgqttestQt[i].pfn();
    if (fTime)
      printf("  [%s: %d ms]\n", rgqttestQt[i].szName,
        (int)timerTest.elapsed());
  }
  // A filter that matches nothing must fail loudly, or a typo in the
  // group name reads as a suite that passed.
  if (cRun < 1) {
    printf("\nFAIL: no test group matches \"%s\" -- "
      "ASTROLOG_QT_TESTS=list names them\n", szFilter);
    return fTrue;
  }
  if (szFilter != NULL)
    printf("\n%d of %d groups matched \"%s\"\n", cRun, cqttestQt,
      szFilter);
  printf("\n%s: %d passed, %d failed\n",
    s_cFail == 0 ? "PASS" : "FAIL", s_cPass, s_cFail);
  return s_cFail > 0;
}


int NRunQtTestsQt()
{
  if (getenv("ASTROLOG_QT_PROBE") != NULL) {
    ProbeQt();
    return fFalse;
  }
  if (getenv("QTGRAPHDIR") != NULL) {
    GraphicsChartCaptureQt(getenv("QTGRAPHDIR"));
    return fFalse;
  }
  if (getenv("QTTEXTDIR") != NULL) {
    TextChartCaptureQt(getenv("QTTEXTDIR"));
    return 0;
  }
  s_nAnimStartQt = gs.nAnim;
  return NRunQtTestTableQt();
}

#endif // QTTEST

/* qttest.cpp */
