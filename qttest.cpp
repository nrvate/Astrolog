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
#include <QtWidgets/QComboBox>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QPushButton>
#include <QtCore/QTimer>
#include <QtCore/QStringList>
#include <QtCore/QSet>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QElapsedTimer>
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
typedef struct _RcAccel { CONST char *szLabel, *szAccel; } RCACCEL;
extern CONST RCACCEL *PaccelTestQt();
extern int CaccelTestQt();
#define rgaccelQt PaccelTestQt()
#define caccelQt CaccelTestQt()

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
static void TestObjSelTableQt()
{
  char szName[cchSzDef];
  int i, j, k, cCheck = 0;

  Group("Object selection table");
  Check(cObjSel > 0, "the body list is empty");
  for (i = 0; i < cObjSel; i++) {
    if (rgObjSel[i].nTyp <= 1)
      SwissGetObjName(szName,
        rgObjSel[i].nTyp <= 0 ? -rgObjSel[i].nObj : rgObjSel[i].nObj);
    else
      sprintf(szName, "%s", FItem(rgObjSel[i].nObj) ?
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
  Check(cCheck > 0, "no entry could be resolved at all, so nothing was checked");
  printf("  %d of %d bodies resolved and matched their listed name\n",
    cCheck, cObjSel);
}


// The Object Selections dialog's fields go through three shared helpers in
// calc.cpp, which both this build and the Windows one call. The dialog
// itself is modal and can only be driven by clicking, so what is worth
// pinning down in a suite is those helpers: a field accepts either a name
// from the offered list or the definition text Object Customization uses,
// and comes back as the same body either way.
static void TestObjSelParseQt()
{
  char sz[cchSzMax];
  int nTyp, nObj, nPnt, nFlg, nTypSav, nObjSav, nPntSav, nFlgSav;

  Group("Object selection fields");

  // A name from the list, and the bare number, are the same body.
  Check(FObjSelParse("Nessus", &nTyp, &nObj, &nPnt, &nFlg) &&
    nTyp == 1 && nObj == 7066 && nPnt == 0 && nFlg == 0,
    "\"Nessus\" did not read as asteroid 7066");
  Check(FObjSelParse("7066", &nTyp, &nObj, &nPnt, &nFlg) &&
    nTyp == 1 && nObj == 7066,
    "\"7066\" did not read as asteroid 7066");
  Check(FObjSelParse("nessus", &nTyp, &nObj, &nPnt, &nFlg) && nObj == 7066,
    "the list match is case sensitive when it shouldn't be");

  // Definition forms other than a plain asteroid number.
  Check(FObjSelParse("h5", &nTyp, &nObj, &nPnt, &nFlg) &&
    nTyp == 0 && nObj == 5, "\"h5\" did not read as element set 5");
  Check(FObjSelParse("Ven", &nTyp, &nObj, &nPnt, &nFlg) &&
    nTyp == 2 && nObj == oVen, "\"Ven\" did not read as Venus");

  // The guard this parse exists to keep. Without it the trailing letters
  // of an all alphabetic definition are read as point and flag suffixes,
  // so "Ven" sets the north node off its own 'n' and the chart quietly
  // shows Venus's node instead of Venus.
  Check(nPnt == 0 && nFlg == 0,
    "\"Ven\" read its own letters as a point/flag suffix (pnt %d flg %d)",
    nPnt, nFlg);
  Check(FObjSelParse("Mar", &nTyp, &nObj, &nPnt, &nFlg) && nPnt == 0,
    "\"Mar\" read its own letters as a suffix");

  // A real suffix still parses.
  Check(FObjSelParse("7066 nH", &nTyp, &nObj, &nPnt, &nFlg) &&
    nTyp == 1 && nObj == 7066 && nPnt == 1 && (nFlg & 1),
    "\"7066 nH\" lost its point or flag suffix");

  Check(!FObjSelParse("", &nTyp, &nObj, &nPnt, &nFlg),
    "an empty field was accepted");

  // And the formatting side round trips: set a slot, read it back.
  nTypSav = rgTypSwiss[uranLo - custLo];
  nObjSav = rgObjSwiss[uranLo - custLo];
  nPntSav = rgPntSwiss[uranLo - custLo];
  nFlgSav = rgFlgSwiss[uranLo - custLo];

  rgTypSwiss[uranLo - custLo] = 1; rgObjSwiss[uranLo - custLo] = 7066;
  rgPntSwiss[uranLo - custLo] = rgFlgSwiss[uranLo - custLo] = 0;
  SzObjSelDef(sz, uranLo);
  Check(FEqSz(sz, "Nessus"),
    "a slot holding 7066 showed as \"%s\", not the list name", sz);

  // With a suffix it must show the raw definition instead, or OK would
  // silently strip the suffix off the slot.
  rgPntSwiss[uranLo - custLo] = 1;
  SzObjSelDef(sz, uranLo);
  Check(FEqSz(sz, "7066 n"),
    "a slot with a point suffix showed as \"%s\"", sz);
  Check(FObjSelParse(sz, &nTyp, &nObj, &nPnt, &nFlg) &&
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
  // Retried rather than fired once. A single shot has to guess when the
  // dialog exists *and* its combo is populated, and guessing 50ms came
  // back empty -- which quietly satisfied every "does not contain" check.
  // Hence also the "found at all" assertion: an empty string passes all
  // of them.
  for (int t = 1; t <= 8; t++)
    QTimer::singleShot(120 * t * nScaleTest, []() {
      if (!s_strCombo.isEmpty())
        return;
      QWidget *pw = QApplication::activeModalWidget();
      if (pw == NULL)
        return;
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
      if (!s_strCombo.isEmpty())
        pw->close();
    });
  QTimer::singleShot(2500 * nScaleTest, []() {
    QWidget *pw = QApplication::activeModalWidget();
    if (pw != NULL)
      pw->close();
  });
  ShowCalcDialogQt();
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
  for (int t = 1; t <= 8; t++)
    QTimer::singleShot(120 * t * nScaleTest, []() {
      if (s_cRowList >= 0)
        return;
      QWidget *pw = QApplication::activeModalWidget();
      if (pw == NULL)
        return;
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
  QTimer::singleShot(2500 * nScaleTest, []() {
    QWidget *pw = QApplication::activeModalWidget();
    if (pw != NULL)
      pw->close();
  });
  ShowChartListDialogQt();
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
  printf("  the redraw notification hook fires\n");
}


static void TestChartListFilterQt()
{
  int cciSav = is.cci, i;
  char *szSav = us.szExpListF;

  Group("Chart list filter");
  for (i = 0; i < 3; i++) {
    ciCore = ciMain; ciCore.yea = 1990 + i;
    sprintf(ciCore.nam, "AstrologSuiteChart%d", i);
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

  us.nRel = nRelSav;
  ciMain = ciMainSav; ciTwin = ciTwinSav; ciSave = ciSaveSav;
  ciCore = ciMain;
  CastChart(1);
  printf("  relationship modes persist, restore, and do not drift\n");
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
  sprintf(szLine, ":Xw %d %d", i, gs.yWin);
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

  ClearB((pbyte)force, sizeof(force));
  ignore[oJup] = ignore[oSat] = fTrue;
  force[oFor] = (real)-(oJup*objMax + oSat + 1);      // -Fm 19 6 7
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
  ClearB((pbyte)force, sizeof(force));
  force[oFor] = (real)-(1*objMax + 2 + 1);        // -Fm 19 1 2
  force[uranLo] = ZD(1, 15.25) + rDegMax;         // -F 34 Ari 15.25

  // A renamed macro menu entry goes in the same file. The Qt build stores
  // -WM through NProcessSwitchesQt() but the block that writes it back was
  // #ifdef WIN, so a settings file saved here silently lost every macro
  // name -- 13 of them in the config this was found with -- while the
  // Windows build kept them. Save what this build can load.
  sprintf(szLine, "-WM 1 \"AstrologQtSuiteMacro\"");
  FProcessCommandLine(szLine);

  sprintf(szPath, "%s/astrolog-qt-force-test.as", getenv("TMPDIR") != NULL ?
    getenv("TMPDIR") : "/tmp");
  us.fNoWrite = fFalse;
  us.nWriteFormat = 'd';
  is.szFileOut = szPath;
  Check(FOutputSettings(), "FOutputSettings() wrote a settings file");

  szMid[0] = szPos[0] = chNull;
  file = FileOpen(szPath, 3, NULL);
  if (file != NULL) {
    while (fgets(szLine, cchSzMax, file) != NULL) {
      if (FEqSz(szLine, "-WM 1 \"AstrologQtSuiteMacro\"\n"))
        fFoundMacro = fTrue;
      for (i = 0; szLine[i]; i++)          // Keep the line, minus its \n.
        ;
      while (i > 0 && szLine[i-1] < ' ')
        szLine[--i] = chNull;
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
  ClearB((pbyte)force, sizeof(force));
  if (szMid[0] != chNull)
    FProcessCommandLine(szMid);
  if (szPos[0] != chNull)
    FProcessCommandLine(szPos);
  Check(force[oFor] == (real)-(1*objMax + 2 + 1),
    "the saved midpoint parsed back to the same encoding");
  Check(force[uranLo] == ZD(1, 15.25) + rDegMax,
    "the saved zodiac position parsed back to the same value");

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
  // Nothing in particular. Rewrite this body to ask the program a
  // question, build, run. Everything is in scope: poke us/gs/gi, call
  // SetChartModeQt() or a dialog function, save gi.qim and measure it.
  SetChartModeQt(gWheel);
  RedrawQt();
  if (gi.qim != NULL) {
    gi.qim->save("/tmp/wedge-qt.png");
    printf("saved %dx%d\n", gi.qim->width(), gi.qim->height());
  }
  SetChartModeQt(gWheel);
  RedrawQt();
  printf("probe: mode=%d image=%dx%d\n", gi.nMode,
    gi.qim != NULL ? gi.qim->width() : 0,
    gi.qim != NULL ? gi.qim->height() : 0);
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
  printf("Astrolog Qt test suite\n");
  TestDialogsQt();
  TestContextMenusQt();
  TestHotkeysQt();
  TestChartRenderQt();
  TestAllMenuActionsQt();
  TestMenuParityQt();
  TestBadInputQt();
  TestForcedPositionsQt();
  TestSharedCoreFixesQt();
  TestRelationshipModeQt();
  TestEphemerisListQt();
  TestChartListFilterQt();
  TestExpressionHooksQt();
  TestAccelTextQt();
  TestObjSelTableQt();
  TestObjSelParseQt();
  printf("\n%s: %d passed, %d failed\n",
    s_cFail == 0 ? "PASS" : "FAIL", s_cPass, s_cFail);
  return s_cFail > 0;
}

#endif // QTTEST

/* qttest.cpp */
