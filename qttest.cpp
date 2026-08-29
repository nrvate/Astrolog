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
#include <QtCore/QTemporaryDir>
#include <QtGui/QKeyEvent>
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
extern int NSchemeFromKdeTestQt(void);
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
  QByteArray baHome = qgetenv("HOME");
  qputenv("HOME", dir.path().toLocal8Bit());

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

  qputenv("HOME", baHome);
  printf("  the desktop's light/dark preference is read from each source\n");
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
    rgbASav[i] = ignorea[i];
  ClickInModalQt(ShowAspectDialogQt, "Toggle &Majors");
  cIn = cOut = 0;
  for (i = 1; i <= cAspect; i++)
    if (ignorea[i] != rgbASav[i]) {
      if (i <= 5) cIn++; else cOut++;
    }
  Check(cIn == 5, "Toggle Majors toggles the first five aspects (%d)", cIn);
  Check(cOut == 0, "and leaves the rest alone (%d)", cOut);
  for (i = 1; i <= cAspect; i++)
    ignorea[i] = rgbASav[i];

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
      flag fWas = pcb->isChecked();
      tap(rg[k].szCh);
      Check(pcb->isChecked() != fWas,
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
  sprintf(szDispSav, "%s", szObjDisp[iobj]);

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

  CopyRgb(ignore, rgbIgnoreSav, sizeof(ignore));
  CopyRgb(ignore2, rgbIgnore2Sav, sizeof(ignore2));
  // One snapshot where four parallel-array copies used to be -- the
  // struct being the point of the exercise.
  CopyRgb((pbyte)rgobjset, (pbyte)rgosSav, sizeof(rgobjset));

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

  sprintf(szPath, "%s/astrolog-qt-roundtrip.as", getenv("TMPDIR") != NULL ?
    getenv("TMPDIR") : "/tmp");
  us.fNoWrite = fFalse;
  us.nWriteFormat = 'd';
  is.szFileOut = szPath;
  Check(FOutputSettings(), "FOutputSettings() wrote a settings file");

  // Overwrite in memory, so anything the file failed to carry stays wrong.
  ignore[iMoon] = fTrue; ignore2[iMoon] = fFalse;
  rgobjset[iMoon].orb = rgobjset[iMoon].add = rgobjset[iMoon].inf = 99.0;
  rgobjset[iMoon].kolor = 1;
  rgobjset[iMoon].tinf = rgobjset[iCusp].tinf = 99.0;

  file = FileOpen(szPath, 3, NULL);
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

  CopyRgb(rgbIgnoreSav, ignore, sizeof(ignore));
  CopyRgb(rgbIgnore2Sav, ignore2, sizeof(ignore2));
  CopyRgb((pbyte)rgosSav, (pbyte)rgobjset, sizeof(rgobjset));
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
      SzObjDefFormat(szT, &rgodT[iT]);
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
static flag s_fWarnedQt = fFalse;

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
  sprintf(szDispSav, "%s", szObjDisp[iobj]);

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
  ClearB((pbyte)force, sizeof(force));
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
  SzObjSelDef(sz, uranLo);
  Check(FEqSz(sz, "Nessus"),
    "a slot holding 7066 showed as \"%s\", not the list name", sz);

  // With a suffix it must show the raw definition instead, or OK would
  // silently strip the suffix off the slot.
  rgPntSwiss[uranLo - custLo] = 1;
  SzObjSelDef(sz, uranLo);
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
  sprintf(szT, "=z %s", sz);
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
    sprintf(rgszNamT[i], "AstrologSuiteChart%d", i);
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
    CONST int *rgsign1, *rgsign2, *rgobj1, *rgobj2;
  } rgfam[] = {
    {"traditional",  rules,      rules2,     ruler1,    ruler2},
    {"esoteric",     rgSignEso1, rgSignEso2, rgObjEso1, rgObjEso2},
    {"hierarchical", rgSignHie1, rgSignHie2, rgObjHie1, rgObjHie2}};
  flag fEnc, fRul, fCorul;
  int ifam, i, k;

  Group("Rulership tables");
  for (ifam = 0; ifam < (int)(sizeof(rgfam)/sizeof(*rgfam)); ifam++) {
    fEnc = fRul = fCorul = fTrue;
    for (i = 1; i <= cSign; i++) {
      k = rgfam[ifam].rgsign1[i];
      fEnc &= FBetween(k, 0, oNorm);        // Every sign has a ruler.
      if (FBetween(k, 0, oNorm))
        fRul &= (rgfam[ifam].rgobj1[k] == i || rgfam[ifam].rgobj2[k] == i);
      k = rgfam[ifam].rgsign2[i];
      fEnc &= (k == -1 || FBetween(k, 0, oNorm));
      if (FBetween(k, 0, oNorm))
        fCorul &= (rgfam[ifam].rgobj1[k] == i || rgfam[ifam].rgobj2[k] == i);
    }
    for (i = 0; i <= oNorm; i++)
      fEnc &= FBetween(rgfam[ifam].rgobj1[i], 0, cSign) &&
        FBetween(rgfam[ifam].rgobj2[i], 0, cSign);
    Check(fEnc, "%s tables: none is -1 sign-keyed and 0 object-keyed",
      rgfam[ifam].szSystem);
    Check(fRul, "every sign's %s ruler rules it back", rgfam[ifam].szSystem);
    Check(fCorul, "every sign's %s co-ruler co-rules it back",
      rgfam[ifam].szSystem);
  }
  printf("  three systems' sign-keyed and object-keyed tables agree\n");
}


// A switch file can include another with -i, and switches like -YY read
// an in-band payload from the file being parsed through is.fileIn. The
// parsers used to clear that channel on exit instead of restoring it, so
// an include nested inside a file left the OUTER file's channel NULL:
// its next payload switch failed "Switch only allowed in file context"
// and everything after it was skipped -- including, from the command
// line, any switches after the -i itself. Regression: an outer file
// with an include, then an atlas payload, then one more switch, must
// load to the end with all three applied.
static void TestNestedIncludeQt()
{
  char szInner[cchSzMax], szOuter[cchSzMax];
  CONST char *szTmp;
  FILE *file;
  int nScrollSav = us.nScrollRow;
  flag fRet, fPopupSav = FNoPopupQt();

  Group("Nested include");
  SetNoPopupQt(fTrue);    // a failing load must fail, not open a box
  szTmp = getenv("TMPDIR") != NULL ? getenv("TMPDIR") : "/tmp";
  sprintf(szInner, "%s/astrolog-qt-nest-inner.as", szTmp);
  sprintf(szOuter, "%s/astrolog-qt-nest-outer.as", szTmp);
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
  Check(is.fileIn == NULL, "the payload channel is clear at top level");

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
  force[oFor] = ForceMid(1, 2);                   // -Fm 19 1 2
  force[uranLo] = ForcePos(ZD(1, 15.25));         // -F 34 Ari 15.25

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
  // Nothing in particular. Rewrite this body to ask the program a
  // question, build, run. Everything is in scope: poke us/gs/gi, call
  // SetChartModeQt() or a dialog function, save gi.qim and measure it.
  int i, cBad = 0;
  for (i = 0; i <= oNorm1; i++) {
    if (rgobjset[i].add != rgobjset[i].add || rgobjset[i].inf != rgobjset[i].inf ||
      rgobjset[i].tinf != rgobjset[i].tinf || rgobjset[i].kolor != rgobjset[i].kolor)
      { printf("row %d: add %g/%g inf %g/%g tinf %g/%g k %d/%d\n", i,
          rgobjset[i].add, rgobjset[i].add, rgobjset[i].inf, rgobjset[i].inf,
          rgobjset[i].tinf, rgobjset[i].tinf, rgobjset[i].kolor, rgobjset[i].kolor);
        cBad++; }
    if (i < 18 && rgobjset[i].orb != rgobjset[i].orb)
      { printf("row %d orb differs below the slip\n", i); cBad++; }
    if (i > 18 && i < 84 && rgobjset[i].orb != rgobjset[i-1].orb)
      { printf("row %d orb not the one-shifted flat value\n", i); cBad++; }
  }
  for (i = 1; i <= 5; i++)
    if (rgrBonusInf[i] != rgobjset[oNorm1+i].inf)
      { printf("bonus %d differs\n", i); cBad++; }
  printf("probe: %s; orb[18]=%g orb[84]=%g (want 2 2)\n",
    cBad ? "MISMATCH" : "struct == flat arrays everywhere expected",
    rgobjset[18].orb, rgobjset[84].orb);
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

typedef struct _qttestentry {
  CONST char *szName;
  void (*pfn)();
} QTTESTENTRY;

static CONST QTTESTENTRY rgqttestQt[] = {
  {"dialogs",              TestDialogsQt},
  {"context-menus",        TestContextMenusQt},
  {"hotkeys",              TestHotkeysQt},
  {"chart-render",         TestChartRenderQt},
  {"menu-actions",         TestAllMenuActionsQt},
  {"menu-parity",          TestMenuParityQt},
  {"bad-input",            TestBadInputQt},
  {"forced-positions",     TestForcedPositionsQt},
  {"shared-core",          TestSharedCoreFixesQt},
  {"rulership",            TestRulershipTablesQt},
  {"nested-include",       TestNestedIncludeQt},
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
  {"dialog-buttons",       TestDialogButtonWiringQt},
  {"shared-symbols",       TestSharedSymbolBoxesQt},
  {"animation",            TestAnimationStateQt},
  {"mnemonics",            TestDialogMnemonicsQt},
  {"arrow-keys",           TestDialogArrowKeysQt},
  {"midpoint-glyph",       TestMidpointGlyphQt},
  {"objsel-lookup",        TestObjSelLookupQt},
  {"custom-parse",         TestCustomDialogParseQt},
  {"objdef-set",           TestObjDefSetQt},
  {"objsel-glyph",         TestObjSelGlyphQt},
  {"settings-roundtrip",   TestSettingsRoundTripQt}};
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
  CONST char *szFilter = getenv("ASTROLOG_QT_TESTS");
  flag fTime = szFilter != NULL || getenv("ASTROLOG_QT_TIME") != NULL;
  QElapsedTimer timerTest;
  int i, cRun = 0;

  if (szFilter != NULL && FEqSzI(szFilter, "list")) {
    for (i = 0; i < cqttestQt; i++)
      printf("%s\n", rgqttestQt[i].szName);
    return fFalse;
  }
  printf("Astrolog Qt test suite\n");
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
