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
#include <QtCore/QSet>
#include <QtGui/QClipboard>
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
#include <QtWidgets/QRadioButton>
#include <QtCore/QMap>
#include <QtWidgets/QLabel>
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
// For the dark scheme assertions: an indicator is drawn into an image and
// measured against what it sits on.
#include <QtGui/QPainter>
#include <QtWidgets/QStyle>
#include <QtWidgets/QStyleOption>
#include <QtGui/QIcon>
#include <QtCore/QTemporaryDir>
#include <QtGui/QKeyEvent>
#include <stdarg.h>
#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QRegularExpression>
#include "astrolog.h"
#include "extern.h"
#include "qtdriver.h"
// Every scalar member of US and GS by name, generated from astrolog.h, so
// the settings round trip can ask about all of them rather than one at a
// time. See tools/gen_settings_fields.py.
#include <stddef.h>
#include "settingsfields.h"

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
extern flag FExportChartToFileTestQt(CONST char *, int);
extern void AllActionsTestQt(QList<QAction *> *);
extern QAction *PaFindLooseTestQt(CONST char *, CONST char **);
typedef struct _RcAccel { CONST char *szLabel, *szAccel; } RCACCEL;
extern CONST RCACCEL *PaccelTestQt();
extern int CaccelTestQt();
#define rgaccelQt PaccelTestQt()
#define caccelQt CaccelTestQt()

// The bundled ephem/ and the body list are one set: every row of
// rgObjSel[] has its file here, and every asteroid file here is a row.
// So the suite asserts one number -- all of them resolve -- and there is
// nothing for a run to declare.
//
// The bundled ephem/ and the Object Selections list are one set by
// construction, so a run against it and a run against /swe assert the
// same number and there is no mode to declare.


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

// What a grab of the dialog showed, recorded beside its title because the
// widget is only in hand for the instant before it is closed.
//
// A title check cannot see a dialog that opens as a blank rectangle, or
// one whose layout collapsed to nothing. Those are real failures with a
// correct title, and they are what a person notices immediately in a
// screenshot and no assertion here noticed at all.
static QSize s_sizeDlgQt;
static flag s_fDlgFlatQt;
static QString s_strDlgClipQt;   // first control found outside the dialog

// Does any visible control extend past the dialog that owns it?
//
// This is the failure everyone means by "a control sits off the edge",
// and it is the one thing a screenshot shows instantly and no assertion
// here could see. Qt knows the answer without an image: every child has a
// geometry and the dialog has a rect.
//
// Only direct, visible children with a real size are considered. A hidden
// page of a stack legitimately sits anywhere, a zero-sized widget has no
// position worth checking, and a nested child is bounded by its own
// parent rather than by the window. One pixel of tolerance, because a
// frame that lands exactly on the boundary is not a defect.
static QString StrClippedChildQt(QWidget *pw)
{
  QRect rcOwn = pw->rect().adjusted(-1, -1, 1, 1);

  for (QObject *pobj : pw->children()) {
    QWidget *pch = qobject_cast<QWidget *>(pobj);
    if (pch == NULL || !pch->isVisible() || pch->isWindow())
      continue;
    if (pch->width() <= 0 || pch->height() <= 0)
      continue;
    if (!rcOwn.contains(pch->geometry()))
      return QString("%1 %2 at (%3,%4 %5x%6) outside %7x%8")
        .arg(QString(pch->metaObject()->className()))
        .arg(pch->objectName().isEmpty() ? QString("(unnamed)")
                                         : pch->objectName())
        .arg(pch->x()).arg(pch->y()).arg(pch->width()).arg(pch->height())
        .arg(pw->width()).arg(pw->height());
  }
  return QString();
}

// Is the image a single flat colour?
//
// By SMOOTH-SCALING to 16x16 and asking whether the result is uniform,
// not by sampling a grid of the original. The first version did sample a
// grid -- 8x8 points at regular intervals -- and reported Object
// Selections as blank on its first run. It is not: 860x790 with 548
// distinct colours. Every one of those 64 points had landed on the
// dialog background, which is 44% of the image, because a regular grid on
// a regular layout hits the gaps between the controls.
//
// Scaling cannot miss content that way: every source pixel contributes to
// some destination pixel, so anything drawn anywhere moves a value.
static flag FPixmapFlatQt(CONST QPixmap &pix)
{
  QImage img, imgT;
  QRgb rgbFirst;
  int x, y;

  img = pix.toImage();
  if (img.isNull() || img.width() < 2 || img.height() < 2)
    return fTrue;
  imgT = img.scaled(16, 16, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  if (imgT.isNull())
    return fTrue;
  rgbFirst = imgT.pixel(0, 0);
  for (y = 0; y < imgT.height(); y++)
    for (x = 0; x < imgT.width(); x++)
      if (imgT.pixel(x, y) != rgbFirst)
        return fFalse;
  return fTrue;
}


static QString StrOpenDialogQt(void (*pfn)())
{
  static QString strTitle;

  strTitle = QString();
  s_sizeDlgQt = QSize();
  s_fDlgFlatQt = fTrue;
  s_strDlgClipQt = QString();
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
      QPixmap pix = pw->grab();
      strTitle = pw->windowTitle();
      s_sizeDlgQt = pix.size();
      s_fDlgFlatQt = FPixmapFlatQt(pix);
      s_strDlgClipQt = StrClippedChildQt(pw);
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

// One table, used by TestDialogsQt() below and by DialogShotCaptureQt().
// At file scope on purpose: a screenshot set that drifts from the dialogs
// actually tested is a baseline of the wrong thing, and two hand-kept
// lists is how that happens.
static CONST DLGTEST rgdlgQt[] = {
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
#define cdlgQt ((int)(sizeof(rgdlgQt) / sizeof(DLGTEST)))


static void TestDialogsQt()
{
  CONST DLGTEST *rgdlg = rgdlgQt;
  int i, cdlg = cdlgQt;

  Group("Dialogs");
  for (i = 0; i < cdlg; i++) {
    QString str = StrOpenDialogQt(rgdlg[i].pfn);
    Check(!str.isEmpty(), "%s: no dialog appeared", rgdlg[i].szTitle);
    if (!str.isEmpty()) {
      Check(str == rgdlg[i].szTitle, "expected title \"%s\", got \"%s\"",
        rgdlg[i].szTitle, str.toLocal8Bit().constData());
      // And that it DREW something. A dialog can carry the right title and
      // still render as an empty rectangle -- a layout that collapsed, a
      // paint that never ran. The bounds are deliberately loose: this is
      // meant to catch nothing-at-all, not to pin a layout that a font
      // change may legitimately move.
      Check(s_sizeDlgQt.width() >= 100 && s_sizeDlgQt.height() >= 50,
        "%s: rendered %dx%d, too small to be a dialog", rgdlg[i].szTitle,
        s_sizeDlgQt.width(), s_sizeDlgQt.height());
      Check(!s_fDlgFlatQt, "%s: rendered as one flat colour -- it opened "
        "with the right title and drew nothing", rgdlg[i].szTitle);
      Check(s_strDlgClipQt.isEmpty(), "%s: a control is outside the dialog: %s",
        rgdlg[i].szTitle, s_strDlgClipQt.toLocal8Bit().constData());
    }
  }
  printf("  %d dialogs opened, drew something, and closed\n", cdlg);
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

extern int CCmdFnTestQt(void);   // qtdriver.cpp

static void TestContextMenusQt()
{
  CONST char *szName;
  int i, j, cmenu = CCtxTestQt(), citem = 0;
  // A context menu is rebuilt on every right click and thrown away when
  // it closes, so building one must not leave anything behind. It used
  // to: ConnectMenuQt() registered a command handler for any entry whose
  // label resolved to a command id, and 14 of the 411 context labels do,
  // so the list grew for the life of the session. Counted around the
  // loop below, which builds all of them.
  int ccmdfn = CCmdFnTestQt();

  Group("Context menus");
  for (i = 0; i < cmenu; i++) {
    QMenu *pmenu = PmenuCtxTestQt(i, &szName);
    QList<QAction *> rgpa = pmenu->actions();
    QSize sizeMenu = pmenu->sizeHint();
    Check(rgpa.size() > 0, "%s: built empty", szName);
    // A menu that resolves every entry and asks for no room is a menu
    // nobody can click. sizeHint() answers without showing the popup,
    // which matters because these run with no display.
    Check(sizeMenu.width() >= 20 && sizeMenu.height() >= 10,
      "%s: wants %dx%d, too small to be clickable", szName,
      sizeMenu.width(), sizeMenu.height());
    for (j = 0; j < rgpa.size(); j++) {
      if (rgpa[j]->isSeparator())
        continue;
      citem++;
      Check(rgpa[j]->isEnabled(),
        "%s: entry \"%s\" did not resolve to a menu bar item",
        szName, rgpa[j]->text().toLocal8Bit().constData());
      // And that it has a LABEL. An entry with empty text resolves,
      // enables, and fires -- and is invisible to whoever is looking for
      // it. Every check above would pass on a menu of blank rows.
      Check(!rgpa[j]->text().trimmed().isEmpty(),
        "%s: entry %d resolves and enables but has no label", szName, j);
    }
    delete pmenu;
  }
  Check(CCmdFnTestQt() == ccmdfn,
    "building %d context menus registered nothing new (%d, was %d)",
    cmenu, CCmdFnTestQt(), ccmdfn);
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

  // The loop above calls SetChartModeQt() directly, leaving us.fGraphics
  // alone. A user picks these off the Chart menu, and the two types with
  // no case in DrawChartX() -- Aspect List and Arabic Parts -- render
  // empty unless the menu action turns graphics off first. Fire the
  // actions themselves, with graphics on, to cover that path.
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
// items, so this walks a long chain of odd setting combinations, which
// is where the crashes have been.
//
// Skipped: anything whose label ends in "..." (those open a dialog and
// would block; the dialog test covers them), and Quit.
//
// WHAT IT LEAVES BEHIND, measured, since every group after it inherits
// this:
//
//   us.nRel      -7    a relationship chart -- RecastAndRedrawQt() then
//                      goes down CastRelation(), which rewrites ciMain
//   us.objCenter oSun  heliocentric; NCheckEclipseLunar() returns
//                      etUndefined outright when the centre IS the Sun
//   us.fEquator  1     ecliptic converted to equatorial -- and the loop
//                      skips restricted objects, so a restricted body
//                      keeps ECLIPTIC coordinates while the rest do not
//   ignore[oSun] 1     the Sun restricted, which is what makes the line
//                      above visible
//   us.fSidereal 1, us.nDwad 1, us.fNavamsa 1, us.objOnAsc 2,
//   us.fFlip 1, us.fGeodetic 1, us.fDecan 1, us.fHouse3D 1,
//   us.fIndian 1, us.nHouseSystem 22, gs.fColor 0, gs.nScale 200
//
//   And the two window-sizing flags, which are checkable menu items like
//   the rest and so come out INVERTED from their defaults:
//
//   qi.fChartWindow  1   "Chart Resizes Window" -- RedrawQt() then fits
//                        the window around the chart after every redraw
//   qi.fWindowChart  0   "Window Resizes Chart" -- the canvas stops
//                        writing its own size back into gs.xWin/gs.yWin
//
//   Both were missing from this list until 2026-09-07, and both silenced
//   an assertion that measured window geometry: one made the group skip
//   itself, the other corrected the very bug it was checking for. See
//   TestGraphicsSizeQt(), which pins them.
//
// A group asserting on positions, dates or rendering pins what it needs
// from that list and restores it -- field by field, never by assigning a
// saved US or GS back, since both carry char * fields other code frees.

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
    // Every item has a label. The menu-parity group checks that Windows'
    // 258 items are PRESENT, by label; it says nothing about the other
    // eighty this build offers, and nothing anywhere requires a menu bar
    // entry to have any text at all. A blank row fires, enables, and
    // cannot be found by whoever wants it.
    if (!rgpa[i]->isSeparator())
      Check(!str.trimmed().isEmpty(),
        "menu bar item %d has no label", i);
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
    //
    // Measured against the BACKGROUND the renderer actually used, not
    // against pixel(0,0). That corner holds the border when one is drawn,
    // and in monochrome the border is the same colour as everything else
    // -- so a telescope chart whose disc fills the frame came to twenty
    // odd differing samples and read as blank, in 23 of these items at
    // once. It was the check that was blind, not the chart. gi.kiOff is
    // what RedrawQt() fills with, so it is what "did anything get drawn"
    // has to compare against.
    KV kvBackT = KvFromKi(gi.kiOff);
    QRgb rgbBackT = qRgb(RgbR(kvBackT), RgbG(kvBackT), RgbB(kvBackT));
    cpix = 0;
    for (y = 0; y < gi.qim->height(); y += 4)
      for (x = 0; x < gi.qim->width(); x += 4)
        if (gi.qim->pixel(x, y) != rgbBackT)
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
** Menu items Windows does not have.
******************************************************************************
*/

// The group above walks Windows' 258 items and checks each is present
// here. That direction is blind to an item this port ADDS -- a knowing
// divergence, a debug leftover, or one command wired in twice under two
// names -- and the sweep that fires every item says so itself: "it says
// nothing about the other eighty this build offers". Nothing did.
//
// So this walks the other way. Every leaf action in the menu bar must be
// a Windows item, a macro slot, or listed here WITH ITS REASON. A new
// Qt-only item fails until someone writes down why it exists, which is
// the same bargain rgparityQt's twelve fSkip rows make in the other
// direction.

static CONST struct { CONST char *szLabel, *szWhy; } rgqtonlyQt[] = {
  {"System", "View > Window Settings > Interface Theme. Qt5 has no API "
             "for the desktop's colour scheme and its gtk3 platform "
             "theme supplies no palette, so the port detects dark mode "
             "itself (NDarkPreferenceQt) and offers the override that "
             "detection makes necessary. Windows themes the frame for "
             "its app and needs no such menu."},
  {"Light",  "ditto -- forces light whatever the desktop says"},
  {"Dark",   "ditto -- forces dark"} };

#define cqtonlyQt (int)(sizeof(rgqtonlyQt) / sizeof(rgqtonlyQt[0]))

static void TestMenuExtraQt()
{
  QList<QAction *> rgpa;
  int i, j, cExtra = 0, cSlot = 0, cUnknown = 0;

  Group("Menu items beyond Windows' set");
  AllActionsTestQt(&rgpa);
  for (i = 0; i < rgpa.size(); i++) {
    // The 96 macro slots are excluded by what they ARE. Their labels are
    // user data: -WM renames any of them, and this machine's own
    // nrvate.as renames thirteen -- so a list of labels would pass here
    // and fail on someone else's settings file.
    if (rgpa[i]->property("astrologMacro").isValid()) {
      cSlot++;
      continue;
    }
    QString str = rgpa[i]->text().section(QChar('\t'), 0, 0).remove('&');
    if (str.trimmed().isEmpty())
      continue;                 // TestAllMenuActionsQt already fails this
    for (j = 0; j < cparityQt; j++)
      if (QString(rgparityQt[j].szLabel).remove('&') == str)
        break;
    if (j < cparityQt)
      continue;
    cExtra++;
    for (j = 0; j < cqtonlyQt; j++)
      if (QString(rgqtonlyQt[j].szLabel) == str)
        break;
    if (j >= cqtonlyQt)
      cUnknown++;
    Check(j < cqtonlyQt,
      "\"%s\" is in this port's menus, is not one of Windows' items, "
      "and is not in rgqtonlyQt with a reason",
      str.toLocal8Bit().constData());
  }
  // Not a tautology: the property is set in BuildMacroMenus() and read
  // here, so a build that stopped generating the slots -- or stopped
  // marking them -- lands on this line rather than dumping 96 unexplained
  // labels into the check above and burying whatever else moved.
  // Against cMacro, astrolog.h's own constant, rather than a literal 96.
  Check(cSlot == cMacro, "%d macro slots expected, found %d",
    cMacro, cSlot);
  // "all accounted for" was printed unconditionally at first, which made
  // the summary line contradict the FAIL directly above it.
  printf("  %d menu items beyond Windows' set, %s; %d macro slots\n",
    cExtra, cUnknown == 0 ? "all accounted for" : "SOME UNEXPLAINED",
    cSlot);
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
  // REFACTORING.md B1's net pinned it: NParseSz()
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


// The Object Selections list is a table of {type, index, name} triples,
// and its whole value is that {1, 7066} really is Nessus -- a digit wrong
// there silently puts a different body in the chart. Resolve each entry
// the way the -Ye handler does and compare against the name it claims.
//
// Only entries whose ephemeris data is present can be checked; the rest
// come back szObjUnknown and are skipped, with the verified count printed
// so this cannot degrade to nothing.
//
// Catches a number changed to ANOTHER REAL BODY, which is the dangerous
// case -- the dialog offering "Sedna" and charting Eris. Does not catch
// one that resolves to nothing, which is indistinguishable from a missing
// file here and announces itself as "???" when the user picks it.
// Drive a modal dialog: wait for it to appear, run "fnOn" against it, and
// make sure it is gone before returning.
//
// Stoppable QTimers rather than QTimer::singleShot: a queued close cannot
// be cancelled, so one armed by a finished test goes on to close the
// first modal a LATER test opens.
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


extern QString StrDefaultSuffixTestQt(CONST QString &, CONST char *);
extern int COpenChartDirTestQt(CONST char *);      // qtdialog.cpp
extern void PrintChartToFileTestQt(CONST char *);  // qtdriver.cpp
extern void AnimTickTestQt(void);                 // qtdriver.cpp
extern flag FAnimTickBusyTestQt(void);
extern void SetAnimTickBusyTestQt(flag);
extern flag FThemeNameDarkTestQt(CONST char *);   // qtdriver.cpp
extern flag FBootPathTestQt(CONST char *, char *, int);  // qtdriver.cpp
extern flag FRunCommandLineTestQt(CONST char *);  // qtdialog.cpp
extern QSize SizeChartViewportTestQt();          // qtdriver.cpp
extern QIcon IconAstrologQt();                   // qtdriver.cpp
extern void SetHomeTestQt(CONST char *);          // qtdriver.cpp
extern int NSchemeFromKdeTestQt(void);
extern int NDarkPreferenceTestQt(void);
extern QString StrConsoleFontQt(void);
extern int NConsoleFontSizeQt(void);
extern void SetConsoleFontQt(CONST char *szFamily, int nSize);
extern QStringList RgstrConsoleFontQt(void);
extern flag FConsoleAntialiasQt(void);
extern void SetConsoleAntialiasQt(flag f);
extern QString StrMenuFontQt(void);
extern int NMenuFontSizeQt(void);
extern void SetMenuFontQt(CONST char *szFamily, int nSize);
extern QStringList RgstrMenuFontQt(void);
extern flag FMenuAntialiasQt(void);
extern void SetMenuAntialiasQt(flag f);
extern void ApplyUiFontQt(void);
extern void ScheduleUiFontReapplyQt(void);
extern QString StrThemePrefQt(void);
extern void SetThemePrefQt(CONST char *);
extern void ApplyColorSchemeQt(void);
extern unsigned long LRgbrefFromCoQt(CONST QColor &co);
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
// The application icon. A window whose icon failed to load looks exactly
// like one that never asked for an icon, so the check is that it
// resolves, at the sizes a panel or task switcher requests.

// Save every dialog as a PNG, for a visual baseline. QTSHOTDIR=<dir>.
//
// The suite already asserts each dialog OPENS and carries the right
// title, which is what an assertion can do. What it cannot say is whether
// the thing looks right -- a control off the edge, a label truncated, a
// layout that collapses under a platform's default font. Those are the
// failures a screenshot catches and no title check ever will, and macOS
// is where they would appear first, since nobody working on this has a
// Mac to look at one on.
//
// QWidget::grab() renders the widget through Qt's own paint path rather
// than asking the window system for pixels, so it works under the
// offscreen platform with no display at all -- which is the only way this
// can run in CI.
static void DialogShotCaptureQt(CONST char *szDir)
{
  QDir().mkpath(QString(szDir));
  for (int i = 0; i < cdlgQt; i++) {
    QString strName = QString(rgdlgQt[i].szTitle).toLower()
      .replace(QRegularExpression("[^a-z0-9]+"), "-");
    QString strPath = QString("%1/%2.png").arg(szDir).arg(strName);
    DriveModalQt(rgdlgQt[i].pfn, [&strPath](QWidget *pw) {
      QPixmap pix = pw->grab();
      if (!pix.isNull())
        pix.save(strPath, "PNG");
      pw->close();
    });
    printf("  %s%s\n", QFile::exists(strPath) ? "" : "MISSING ",
      strPath.toUtf8().constData());
  }
}



// Does every control actually FIT inside the dialog that holds it? The
// dialogs are setFixedSize(), so a control past the edge is not scrolled
// to, it is gone.
//
// Cheap to assert because RcBuildDialogQt() makes every control a direct
// child of the dialog in absolute coordinates -- no scroll areas, no
// nesting -- so a child's geometry() is already in the dialog's own
// coordinates and anything outside its rect is off the edge.
//
// It is worth having because the layout is computed rather than fixed:
// one scale factor is derived from the widest string that must not wrap
// and applied to the whole dialog (RRcTextRatioQt), so a font, a
// platform, or a translated string can push a control out.

// A command line longer than the buffer it is copied into.
//
// FProcessCommandLine() declares char szCommandLine[cchSzLine] and then
// CopyRgb()s CchSz(szLine)+1 bytes into it with nothing in between, so a
// longer string smashed the stack: measured as *** stack smashing
// detected ***, SIGABRT and a core file, from
//   astrolog -M0 1 "<1500 characters>" -M 1
// A macro is the reachable path, and an AstroExpression string is
// another. Neither GUI can reach it -- Windows caps its Enter Command
// Line box at cchSzLine and the Qt one at cchSzMax -- so it is the
// programmatic callers that hand this an unmeasured string.
//
// The popup is suppressed because the refusal raises a warning, and in
// this build with a QApplication alive that is a MODAL MESSAGE BOX. The
// suite would stop dead on it rather than fail.

static void TestLongCommandLineQt()
{
  char szLong[cchSzLine*2], szFits[cchSzLine];
  flag fPopupSav = FNoPopupQt();
  int i;

  Group("Over-long command line");

  // Both strings are VALID command lines -- "-n " repeated, which casts a
  // chart for now and does nothing else -- so the only thing that differs
  // between them is the length. A string of filler would be refused for
  // being nonsense and the two assertions would agree for the wrong
  // reason.
  for (i = 0; i + 3 < (int)sizeof(szFits); i += 3)
    CopyRgb((pbyte)"-n ", (pbyte)&szFits[i], 3);
  szFits[i] = chNull;
  for (i = 0; i + 3 < (int)sizeof(szLong); i += 3)
    CopyRgb((pbyte)"-n ", (pbyte)&szLong[i], 3);
  szLong[i] = chNull;
  Check(CchSz(szFits) < cchSzLine && CchSz(szLong) > cchSzLine,
    "one line fits the buffer and one does not (%d, %d, limit %d)",
    CchSz(szFits), CchSz(szLong), cchSzLine);

  SetNoPopupQt(fTrue);
  // Refused rather than truncated: half a command line is a different
  // command line. Reaching the next assertion at all is most of the
  // point -- before the fix this call did not return, it aborted the
  // process with *** stack smashing detected ***.
  Check(!FProcessCommandLine(szLong),
    "a command line over the buffer is refused, not copied into it");
  Check(FProcessCommandLine(szFits),
    "and one that fits is still processed (%d chars)", CchSz(szFits));

  // The Enter Command Line box has a buffer of its OWN, and it was the
  // wrong size: cchSzMax, so everything past 254 characters was silently
  // cut, while Windows reads its box with cchSzLine and comments the
  // size (wdialog.cpp:1073). Silent is the part that matters -- the
  // refusal above exists because half a command line is a different
  // command line, and truncating one layer up defeated it.
  //
  // A line long enough to have been cut, with an observable switch at the
  // very END of it, which is the only place the difference shows.
  {
    CI ciSav = ciCore, ciMainSav = ciMain;
    int nScrollSav = us.nScrollRow;
    char szCmd[cchSzLine];
    int cch = 0;

    // Padded with the same switch it ends with, and not with "-n " as the
    // draft above it: NParseCommandLine() stops at MAXSWITCHES (100)
    // parameters and says so, and 300 characters of "-n " is 100 of them,
    // so the sentinel was dropped for a reason that had nothing to do
    // with the buffer. Two tokens per six characters keeps it well under.
    while (cch < cchSzMax + 20)
      cch += sprintf2(SO(&szCmd[cch], szCmd), "-YQ 0 ");
    sprintf2(SO(&szCmd[cch], szCmd), "-YQ 137");
    Check(CchSz(szCmd) > cchSzMax && CchSz(szCmd) < cchSzLine,
      "a line longer than the old buffer and shorter than the new "
      "(%d chars, %d, %d)", CchSz(szCmd), cchSzMax, cchSzLine);
    us.nScrollRow = 0;
    FRunCommandLineTestQt(szCmd);
    Check(us.nScrollRow == 137,
      "and the switch at the end of it still runs (nScrollRow %d)",
      us.nScrollRow);
    us.nScrollRow = nScrollSav;
    ciCore = ciSav; ciMain = ciMainSav;
  }
  // The same class, one buffer further in: DisplayAtlasLookup() copied
  // its argument into a cchSzMax stack buffer one character at a time
  // with nothing stopping it, so "-zN <260 characters>" aborted -- and
  // the city field of the chart info dialog reaches it in both builds, so
  // a long paste was enough.
  //
  // The ordinary lookup is asserted FIRST and deliberately: it proves the
  // atlas actually loaded, without which DisplayAtlasLookup() returns
  // before it reaches the copy and the long-name case would prove nothing.
  {
    int iae = 0;

    Check(DisplayAtlasLookup("Seattle, WA, USA", 0, &iae),
      "an ordinary city resolves, so the atlas is loaded");
    szLong[cchSzLine] = chNull;    // still far past cchSzMax
    // Not asserted on its own return -- a miss is the correct answer to a
    // 1020-character city. What is asserted is that the atlas still works
    // AFTERWARDS, which fails if the over-long name walked over anything,
    // and which cannot run at all if it aborts.
    DisplayAtlasLookup(szLong, 0, &iae);
    Check(DisplayAtlasLookup("Seattle, WA, USA", 0, &iae),
      "and an over-long name leaves the atlas intact behind it");
  }
  SetNoPopupQt(fPopupSav);
  printf("  over-long input is truncated or refused, never copied blind\n");
}


// Every "Save Chart As" format, for a chart carrying NO name and NO
// location.
//
// FOutputAAFFile() dereferenced a NULL pch in exactly that case and
// segfaulted -- "astrolog -q 1 1 2000 0 -oa file.aaf", which is casting a
// chart and saving it. It hid behind the default settings file, which
// fills both fields with -zj, so only a chart that replaces them reaches
// it. Nothing here wrote any of these formats, so nothing could have
// caught it.
//
// Written with the fields EMPTY on purpose: that is the case the writers
// have to survive, and the case a chart cast from bare coordinates
// actually produces.

// Settings files that include each other with -i. FProcessSwitchFile()
// recurses, and each level carries a cchSzLine buffer and a MAXSWITCHES
// argv on the frame, so an unbounded chain exhausts the stack.
//
// Two CHAINS either side of the limit, rather than breaking the guard:
// one shorter than cFileDepthMax must load, one longer must be refused,
// and moving the limit flips one of them. The deepest file sets "=b0",
// so "did it load" is a setting that arrived -- not a return value, which
// says nothing: a refused inner file does not propagate failure outward.
//
// The popup is suppressed because refusing raises a warning, and in this
// build that is a modal message box the suite would stop dead on.

static void TestFileRecursionQt()
{
  flag fPopupSav = FNoPopupQt(), fSecondsSav = us.fSeconds;
  QTemporaryDir dir;
  char szHead[cchSzMax], szPath[cchSzMax], szSelf[cchSzMax];
  int rgcDepth[2], i, j;

  Group("Settings file recursion");

  Check(dir.isValid(), "a scratch directory for the files");
  if (!dir.isValid())
    return;
  rgcDepth[0] = cFileDepthMax - 5;    // comfortably inside the limit
  rgcDepth[1] = cFileDepthMax + 5;    // comfortably past it
  SetNoPopupQt(fTrue);

  for (i = 0; i < 2; i++) {
    for (j = 0; j < rgcDepth[i]; j++) {
      QString str;
      sprintf2(S(szPath), "%s/chain%d-%d.as",
        dir.path().toLocal8Bit().constData(), i, j);
      if (j + 1 < rgcDepth[i])
        str = QString("@AD800\n-i %1/chain%2-%3.as\n")
          .arg(dir.path()).arg(i).arg(j + 1);
      else
        str = QString("@AD800\n=b0\n");
      QFile file(QString::fromLocal8Bit(szPath));
      if (file.open(QIODevice::WriteOnly)) {
        file.write(str.toLocal8Bit());
        file.close();
      }
      if (j == 0)
        sprintf2(S(szHead), "%s", szPath);
    }
    us.fSeconds = fFalse;
    FProcessSwitchFile(szHead, NULL);
    if (i == 0)
      Check(us.fSeconds, "a chain %d deep loads, and its last file is read",
        rgcDepth[i]);
    else
      Check(!us.fSeconds, "a chain %d deep is refused before the stack goes",
        rgcDepth[i]);
  }

  // And the shape that actually reaches a user: one file naming itself.
  // Reaching this assertion at all is most of it -- before the guard the
  // process died here and Check() could not run.
  sprintf2(S(szSelf), "%s/self.as", dir.path().toLocal8Bit().constData());
  {
    QFile file(QString::fromLocal8Bit(szSelf));
    if (file.open(QIODevice::WriteOnly)) {
      file.write(QString("@AD800\n-i %1\n=b0\n").arg(
        QString::fromLocal8Bit(szSelf)).toLocal8Bit());
      file.close();
    }
  }
  us.fSeconds = fFalse;
  FProcessSwitchFile(szSelf, NULL);
  Check(!us.fSeconds, "a file that includes itself is stopped, not recursed");

  us.fSeconds = fSecondsSav;
  SetNoPopupQt(fPopupSav);
  printf("  settings files cannot include each other forever\n");
}


// The Graphics Settings dialog refuses a numeric field the switches would
// refuse, instead of storing it.
//
// Every one of those fields went straight into the settings with no check
// -- while -WN, -Yg and the rest have always validated the same fields --
// and QString::toInt() answers 0 for an empty or non-numeric box, so the
// bad value was one keystroke away. The delay is the one that bites:
// SetAnimDelayQt(0) is QTimer::setInterval(0), a timer that fires as fast
// as the event loop allows for as long as the program runs.

static void TestGraphicsFieldsQt()
{
  int nDelaySav = NAnimDelayQt(), nGridSav = gs.nGridCell;

  Group("Graphics Settings field validation");

  DriveModalQt(ShowGraphicsSettingsDialogQt, [](QWidget *pw) {
    QLineEdit *pe = pw->findChild<QLineEdit *>("deGr_WN");
    if (pe != NULL)
      pe->setText("0");
    for (QPushButton *ppb : pw->findChildren<QPushButton *>())
      if (ppb->text() == "OK")
        ppb->click();
  });
  Check(NAnimDelayQt() == nDelaySav,
    "an animation delay of 0 is refused, not stored (%d, want %d)",
    NAnimDelayQt(), nDelaySav);

  // And the dialog still applies a GOOD value, or "refuses everything"
  // would pass the assertion above just as well.
  DriveModalQt(ShowGraphicsSettingsDialogQt, [](QWidget *pw) {
    QLineEdit *pe = pw->findChild<QLineEdit *>("deGr_WN");
    if (pe != NULL)
      pe->setText("250");
    for (QPushButton *ppb : pw->findChildren<QPushButton *>())
      if (ppb->text() == "OK")
        ppb->click();
  });
  Check(NAnimDelayQt() == 250,
    "while a delay inside the range is applied (%d, want 250)",
    NAnimDelayQt());

  SetAnimDelayQt(nDelaySav);
  gs.nGridCell = nGridSav;

  // The other five fields Windows checks here, which this dialog stored
  // unchecked until 2026-09-07. Two of them are memory safety: gs.objTrack
  // and gs.objLeft INDEX planet[] (xcharts1.cpp:1680, 2321, 3113) and both
  // come from NParseSz(), which hands back whatever number was typed --
  // "9999" in the telescope planet box was an out-of-range read.
  //
  // Each one both ways. "Refuses everything" would pass the bad half on
  // its own, and it is a real failure mode here: the dialog returns at the
  // first bad field, so a check that only ever fed it rubbish could not
  // tell a working validation from a broken store.
  {
    // Each row starts from a known GOOD value rather than from whatever
    // the last group left, and names what the good input must produce.
    // Both matter: with the starting value taken on trust, the refusal
    // half is vacuous whenever it already equals the bad input, and
    // "the value changed" is vacuous whenever it already equals the good
    // one -- which is exactly how the first draft failed in the full
    // suite (text scale was already 150) while passing alone.
    static CONST struct {
      CONST char *szId;    // control's object name
      flag fCombo;         // a QComboBox rather than a QLineEdit
      int iDup;            // which control of that name, in table order
      int nStart;          // put the setting here first
      CONST char *szBad, *szGood;
      int nGood;           // what the good input must store
      int *pn;             // the setting
      CONST char *szWhat;
    } rgt[] = {
      {"dcGr_Xs",  fTrue,  0, 200, "0",    "300",  300, &gs.nScale,
       "character scale"},
      {"dcGr_XSS", fTrue,  0, 100, "0",    "150",  150, &gs.nScaleText,
       "text scale"},
      {"deGr_XZ",  fFalse, 0, oMoo, "9999", "None", -1, &gs.objTrack,
       "telescope planet"}};
    int iT;

    for (iT = 0; iT < (int)(sizeof(rgt)/sizeof(*rgt)); iT++) {
      CONST char *szId = rgt[iT].szId, *szBad = rgt[iT].szBad;
      CONST char *szGood = rgt[iT].szGood;
      flag fCombo = rgt[iT].fCombo;
      int iDup = rgt[iT].iDup;
      int nSav = *rgt[iT].pn, nAfterBad, nAfterGood;

      *rgt[iT].pn = rgt[iT].nStart;

      auto fnSet = [szId, fCombo, iDup](QWidget *pw, CONST char *szText) {
        if (fCombo) {
          QList<QComboBox *> rg = pw->findChildren<QComboBox *>(szId);
          if (iDup < rg.size())
            rg[iDup]->setEditText(szText);
        } else {
          QList<QLineEdit *> rg = pw->findChildren<QLineEdit *>(szId);
          if (iDup < rg.size())
            rg[iDup]->setText(szText);
        }
        for (QPushButton *ppb : pw->findChildren<QPushButton *>())
          if (ppb->text() == "OK") {
            ppb->click();
            return;
          }
        pw->close();
      };

      DriveModalQt(ShowGraphicsSettingsDialogQt,
        [&fnSet, szBad](QWidget *pw) { fnSet(pw, szBad); });
      nAfterBad = *rgt[iT].pn;
      DriveModalQt(ShowGraphicsSettingsDialogQt,
        [&fnSet, szGood](QWidget *pw) { fnSet(pw, szGood); });
      nAfterGood = *rgt[iT].pn;

      Check(nAfterBad == rgt[iT].nStart,
        "%s refuses \"%s\" and stores nothing (%d, was %d)",
        rgt[iT].szWhat, szBad, nAfterBad, rgt[iT].nStart);
      Check(nAfterGood == rgt[iT].nGood,
        "%s accepts \"%s\" (%d, want %d)", rgt[iT].szWhat, szGood,
        nAfterGood, rgt[iT].nGood);
      *rgt[iT].pn = nSav;
    }
  }

  // The rotation planet needs its own block: gs.objLeft is the field it
  // feeds, and that is "0" whenever the three-way radio beside it says
  // "none" -- so accepting a good value is only visible with the radio
  // moved as well. The first draft left the radio alone and read 0 for
  // both halves.
  //
  // And index 0, not 1: PwRcFindIdxQt() looks the control up by the
  // RESOURCE index (the symbol is "deGr_X1"), but rc2qt.py splits that
  // digit off into nIdx and the object name is plain "deGr_X" -- of which
  // this dialog has exactly one. Asking findChildren() for [1] touched
  // nothing, so the refusal half passed because nothing had changed,
  // which is the same as no assertion. Measured: one line edit named
  // "deGr_X", holding "Sun", and three radios named "dr".
  {
    int nSav = gs.objLeft, nBad, nGood;

    gs.objLeft = 0;
    DriveModalQt(ShowGraphicsSettingsDialogQt, [](QWidget *pw) {
      QList<QLineEdit *> rg = pw->findChildren<QLineEdit *>("deGr_X");
      if (!rg.isEmpty())
        rg[0]->setText("9999");
      for (QPushButton *ppb : pw->findChildren<QPushButton *>())
        if (ppb->text() == "OK") { ppb->click(); return; }
      pw->close();
    });
    nBad = gs.objLeft;
    DriveModalQt(ShowGraphicsSettingsDialogQt, [](QWidget *pw) {
      QList<QLineEdit *> rg = pw->findChildren<QLineEdit *>("deGr_X");
      QList<QRadioButton *> rgrb = pw->findChildren<QRadioButton *>("dr");
      if (!rg.isEmpty())
        rg[0]->setText("Moon");
      if (rgrb.size() > 1)
        rgrb[1]->setChecked(fTrue);
      for (QPushButton *ppb : pw->findChildren<QPushButton *>())
        if (ppb->text() == "OK") { ppb->click(); return; }
      pw->close();
    });
    nGood = gs.objLeft;
    Check(nBad == 0,
      "rotation planet refuses \"9999\" and stores nothing (%d)", nBad);
    Check(nGood == oMoo + 1,
      "and accepts the Moon with the radio on (%d, want %d)",
      nGood, oMoo + 1);
    gs.objLeft = nSav;
  }

  // The zoom is a real, so it needs its own pair rather than the table.
  {
    real rSav = gs.rspace, rBad, rGood;

    gs.rspace = 1.0;
    DriveModalQt(ShowGraphicsSettingsDialogQt, [](QWidget *pw) {
      QLineEdit *pe = pw->findChild<QLineEdit *>("deGr_YXS");
      if (pe != NULL)
        pe->setText("-1");
      for (QPushButton *ppb : pw->findChildren<QPushButton *>())
        if (ppb->text() == "OK") { ppb->click(); return; }
      pw->close();
    });
    rBad = gs.rspace;
    DriveModalQt(ShowGraphicsSettingsDialogQt, [](QWidget *pw) {
      QLineEdit *pe = pw->findChild<QLineEdit *>("deGr_YXS");
      if (pe != NULL)
        pe->setText("2.5");
      for (QPushButton *ppb : pw->findChildren<QPushButton *>())
        if (ppb->text() == "OK") { ppb->click(); return; }
      pw->close();
    });
    rGood = gs.rspace;
    Check(rBad == 1.0, "telescope zoom refuses -1 (%.4f)", (double)rBad);
    Check(rGood == 2.5, "and accepts 2.5 (%.4f)", (double)rGood);
    gs.rspace = rSav;
  }
  printf("  the dialog validates what the switches validate\n");
}


static void TestChartExportQt()
{
  CONST char rgchFmt[] = {'d', 'l', 'a', 'q', 'c'};
  CONST char *rgszFmt[] = {"settings", "chart list", "AAF", "Quick*Chart",
    "iCalendar"};
  char szPath[cchSzMax], *szFileOutSav = is.szFileOut, *szNamSav, *szLocSav;
  int nWriteFormatSav = us.nWriteFormat, i;
  flag fNoWriteSav = us.fNoWrite;

  Group("Chart export formats");

  // Export Chart Bitmap writes what File Settings' "Export Bitmaps in PNG
  // Format" asks for. It used to force gs.chBmpMode to 'B' first, so it
  // could never write a PNG and turned that box off on the way past.
  // Windows sets that field in one place only, cmdCopyBitmap, because a
  // bitmap going onto the Windows clipboard has to be a real one.
  //
  // The magic bytes, not the extension: the extension is chosen by the
  // file dialog and the content by the writer, and it is the content that
  // was wrong.
  {
    Borrow bMode(gs.chBmpMode, 'B');
    Borrow bNoWrite(us.fNoWrite, fFalse);
    struct { char ch; CONST char *szMagic; CONST char *szWhat; } rgbmp[] = {
      {'B', "BM", "a Windows bitmap"}, {'P', "\x89PNG", "a PNG"} };
    char szBmp[cchSzMax];
    int ib;

    for (ib = 0; ib < 2; ib++) {
      QFile file;
      QByteArray ba;

      sprintf2(S(szBmp), "%s/astrolog-qt-bmpmode-%d-%c",
        QDir::tempPath().toLocal8Bit().constData(),
        (int)QCoreApplication::applicationPid(), rgbmp[ib].ch);
      remove(szBmp);
      gs.chBmpMode = rgbmp[ib].ch;
      Check(FExportChartToFileTestQt(szBmp, ftBmp),
        "Export Chart Bitmap writes %s", rgbmp[ib].szWhat);
      Check(gs.chBmpMode == rgbmp[ib].ch,
        "and leaves the PNG setting where it found it (%c)", gs.chBmpMode);
      file.setFileName(QString::fromLocal8Bit(szBmp));
      if (file.open(QIODevice::ReadOnly)) {
        ba = file.read(4);
        file.close();
      }
      Check(ba.startsWith(QByteArray(rgbmp[ib].szMagic,
        rgbmp[ib].ch == 'B' ? 2 : 4)),
        "and what it wrote really is %s", rgbmp[ib].szWhat);
      remove(szBmp);
    }
  }

  szNamSav = ciMain.nam; szLocSav = ciMain.loc;
  ciMain.nam = ciMain.loc = NULL;
  us.fNoWrite = fFalse;

  for (i = 0; i < (int)sizeof(rgchFmt); i++) {
    sprintf2(S(szPath), "%s/astrolog-qt-export-%d-%c",
      QDir::tempPath().toLocal8Bit().constData(),
      (int)QCoreApplication::applicationPid(), rgchFmt[i]);
    us.nWriteFormat = rgchFmt[i];
    is.szFileOut = szPath;
    // Reaching the assertion is most of the point: the AAF writer did not
    // return at all for this chart, it took the process down.
    Check(FOutputData(),
      "%s writes a chart with no name and no location", rgszFmt[i]);
    Check(QFileInfo(QString::fromLocal8Bit(szPath)).size() > 0,
      "and the %s file it wrote is not empty", rgszFmt[i]);
    // Not empty is not the same as not garbage. ciMain.nam and .loc are
    // NULL above, and a writer that hands NULL to a "%s" has undefined
    // behaviour -- where it does not crash, glibc writes the literal
    // text "(null)" into a file another program is meant to read. The
    // Quick*Chart writer did exactly that, in both fields, while every
    // other writer here guarded with FSzSet(). Cheap to ask of all five.
    QFile fileT(QString::fromLocal8Bit(szPath));
    QByteArray ba;
    if (fileT.open(QIODevice::ReadOnly))
      ba = fileT.readAll();
    fileT.close();
    Check(!ba.contains("(null)"),
      "and no unset field reached the %s file as \"(null)\"", rgszFmt[i]);
    QFile::remove(QString::fromLocal8Bit(szPath));
  }

  ciMain.nam = szNamSav; ciMain.loc = szLocSav;
  is.szFileOut = szFileOutSav;
  us.nWriteFormat = nWriteFormatSav;
  us.fNoWrite = fNoWriteSav;
  printf("  every export format survives a chart with empty fields\n");
}


static void TestDialogFitQt()
{
  Group("Dialog controls fit their dialog");

  for (int i = 0; i < cdlgQt; i++) {
    int dxWorst = 0, dyWorst = 0;
    char szWorst[cchSzMax];

    szWorst[0] = chNull;
    DriveModalQt(rgdlgQt[i].pfn, [&dxWorst, &dyWorst, &szWorst](QWidget *pw) {
      QRect rcDlg = pw->rect();
      CONST QObjectList rgobj = pw->children();
      for (int j = 0; j < rgobj.size(); j++) {
        QWidget *pwc = qobject_cast<QWidget *>(rgobj[j]);
        // Windows of their own (a combo's popup view is one) are not laid
        // out in the dialog's coordinates at all.
        if (pwc == NULL || pwc->isWindow() || pwc->isHidden())
          continue;
        QRect rc = pwc->geometry();
        if (rc.isEmpty())
          continue;
        int dx = rc.right() - rcDlg.right(), dy = rc.bottom() - rcDlg.bottom();
        dx = Max(dx, -rc.left()); dy = Max(dy, -rc.top());
        if (dx > dxWorst || dy > dyWorst) {
          if (dx > dxWorst) dxWorst = dx;
          if (dy > dyWorst) dyWorst = dy;
          sprintf2(S(szWorst), "%s \"%s\"",
            pwc->metaObject()->className(),
            pwc->property("text").toString().left(24).toUtf8().constData());
        }
      }
      pw->close();
    });
    Check(dxWorst <= 0 && dyWorst <= 0,
      "%s: every control is inside it (worst %+d,%+d %s)",
      rgdlgQt[i].szTitle, dxWorst, dyWorst, szWorst);
  }
  printf("  no control sits off the edge of its dialog\n");

  // And the other half of the same question: a label that WRAPPED still
  // has to fit its box vertically. This is the specific risk the layout
  // rewrite created -- it stopped shrinking a label's font to make the
  // text fit and started wrapping instead, but the boxes are the
  // resource's and their heights did not change, so a label that goes to
  // two lines in a one-line box loses the second line. Nothing shows
  // that: the dialog opens, the title is right, and the text is simply
  // cut off.
  int cWrap = 0;
  for (int i = 0; i < cdlgQt; i++) {
    int dyWorst = 0;
    char szWorst[cchSzMax];

    szWorst[0] = chNull;
    DriveModalQt(rgdlgQt[i].pfn, [&dyWorst, &szWorst, &cWrap](QWidget *pw) {
      CONST QObjectList rgobj = pw->children();
      for (int j = 0; j < rgobj.size(); j++) {
        QLabel *pl = qobject_cast<QLabel *>(rgobj[j]);
        if (pl == NULL || !pl->wordWrap() || pl->isHidden() ||
          pl->text().isEmpty())
          continue;
        // What the wrapped text needs at the width it was given, against
        // the height the resource gave it.
        cWrap++;
        int dyWant = pl->heightForWidth(pl->width());
        if (dyWant - pl->height() > dyWorst) {
          dyWorst = dyWant - pl->height();
          sprintf2(S(szWorst), "\"%s\" wants %d has %d",
            pl->text().left(28).toUtf8().constData(), dyWant, pl->height());
        }
      }
      pw->close();
    });
    Check(dyWorst <= 0, "%s: a wrapped label still fits its box (%s)",
      rgdlgQt[i].szTitle, szWorst[0] != chNull ? szWorst : "none wrapped");
  }
  // A check that examined nothing would pass too. The layout only turns
  // wrapping on for a label whose text overflows its box, so if no label
  // anywhere wrapped, the loop above asserted nothing at all and the fact
  // that it "passed" would be meaningless.
  // A check that examined nothing would pass too, so require that some
  // label really does wrap: 37 have wrapping enabled and three take two
  // lines. The margin scales with the font, so this passes rather than
  // sitting tight against the limit.
  Check(cWrap > 0, "and some label actually wraps, or the above proves "
    "nothing (%d)", cWrap);
  printf("  and no wrapped label is cut off by the box it wraps inside\n");
}


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


// The console font: that the bundled faces actually SHIP and load, and
// that the preference round trips. The faces are the point -- the picker
// is only as good as the files beside the binary, and a font/ that lost
// them would leave a list of one on a machine with nothing else
// installed, which is exactly what nobody would notice.

static void TestConsoleFontQt()
{
  CONST char *rgszWant[] = {"Liberation Mono", "JetBrains Mono",
    "IBM Plex Mono", "Source Code Pro", "Hack", "Fira Code"};
  QStringList rgstr = RgstrConsoleFontQt();
  char sz[cchSzMax];
  int i;

  Group("Console font");
  for (i = 0; i < (int)(sizeof(rgszWant)/sizeof(char *)); i++) {
    sprintf2(S(sz), "%s is bundled and loaded", rgszWant[i]);
    Check(rgstr.contains(QString(rgszWant[i])), sz);
  }
  Check(rgstr.size() >= 6, "the picker offers at least the bundled faces");
  // The bundled ones come first, in order, so the list does not depend on
  // what the machine happens to have.
  Check(rgstr.size() > 0 && rgstr[0] == QString("Liberation Mono"),
    "the default face heads the list");

  // These settings live in astrolog.as now, which means they are ordinary
  // program state rather than a file on disk: nothing to redirect, and
  // nothing of the developer's to read or overwrite. There WAS something,
  // and it mattered -- the first version of this test moved HOME and
  // still read the running developer's config, because QSettings resolves
  // its path once at startup; the version after that captured the
  // application font before establishing a baseline, and so failed on a
  // machine whose saved font differed. Both failure modes are gone with
  // the file. What remains is to put back whatever the run started with,
  // since the rest of the suite draws with these.
  QString strConSave = StrConsoleFontQt(), strMenSave = StrMenuFontQt();
  int nConSave = NConsoleFontSizeQt(), nMenSave = NMenuFontSizeQt();
  flag fConSave = FConsoleAntialiasQt(), fMenSave = FMenuAntialiasQt();

  SetConsoleFontQt("JetBrains Mono", 18);
  Check(StrConsoleFontQt() == QString("JetBrains Mono"),
    "the chosen face round trips");
  Check(NConsoleFontSizeQt() == 18, "the chosen size round trips");
  SetConsoleFontQt("", 0);
  Check(StrConsoleFontQt().isEmpty(), "an empty face means the default");
  Check(NConsoleFontSizeQt() == 0, "size 0 means follow Character Scale");
  // Out of range is refused rather than stored, so a hand edited config
  // cannot ask for a 2 pixel or a 2000 pixel chart.
  SetConsoleFontQt("Hack", 900);
  Check(NConsoleFontSizeQt() == 0, "an absurd size reads as automatic");

  // Antialiasing: on unless it has been turned off, because Qt's own
  // default is on and that is what the rest of the desktop does.
  Check(FConsoleAntialiasQt(), "smoothing defaults to on");
  SetConsoleAntialiasQt(fFalse);
  Check(!FConsoleAntialiasQt(), "turning smoothing off round trips");
  SetConsoleAntialiasQt(fTrue);
  Check(FConsoleAntialiasQt(), "and back on again");

  // The interface font: the same three settings again, for the face the
  // menus and dialogs are drawn in rather than the text charts.
  QStringList rgstrMenu = RgstrMenuFontQt();
  Check(rgstrMenu.size() > 0 && rgstrMenu[0] == QString("Liberation Sans"),
    "the dialogs' own face heads the interface list");
  // Proportional faces are in this one and not in the console one, which
  // is the whole difference between the two lists. Counted rather than
  // compared by length: with the interface list wrongly filtered to fixed
  // pitch it is still LONGER than the console one, by the bundled face at
  // its head, so a length comparison passes on the bug.
  int cProp = 0;
  for (i = 0; i < rgstrMenu.size(); i++)
    if (!QFontInfo(QFont(rgstrMenu[i])).fixedPitch())
      cProp++;
  Check(cProp > 1, "the interface list offers proportional faces too");

  // Establish the no-preference baseline before capturing what "default"
  // looks like. A settings file can carry a menu font, and then "back to
  // how startup left it" is not the default at all -- the restore
  // assertions below would compare against that person's taste.
  SetMenuFontQt("", 0);
  SetMenuAntialiasQt(fTrue);
  ApplyUiFontQt();

  // A widget built BEFORE the change, because applying it to the window
  // already on screen is the point: QApplication::setFont() has to reach
  // what is already built, or the menus would keep the old face until the
  // next start.
  QWidget wOpen;
  // And a menu bar specifically, because the menus are what this is for
  // and they are not an ordinary widget: a platform theme registers a
  // font of its own for QMenu and QMenuBar, which is why they can look
  // nothing like the rest of the interface. Setting the application font
  // with no class name is what discards those.
  QMenuBar *pmb = new QMenuBar(&wOpen);
  QFont fontStart = QApplication::font();
  SetMenuFontQt("JetBrains Mono", 13);
  Check(StrMenuFontQt() == QString("JetBrains Mono"),
    "the chosen interface face round trips");
  Check(NMenuFontSizeQt() == 13, "the chosen interface size round trips");
  SetMenuFontQt("JetBrains Mono", 900);
  Check(NMenuFontSizeQt() == 0, "an absurd interface size reads as automatic");
  SetMenuFontQt("JetBrains Mono", 13);
  ApplyUiFontQt();
  Check(QApplication::font().family() == QString("JetBrains Mono"),
    "applying it changes the interface font");
  Check((int)(QApplication::font().pointSizeF() + rRound) == 13,
    "and its size");
  Check(wOpen.font().family() == QString("JetBrains Mono"),
    "a widget already built follows it");
  Check(pmb->font().family() == QString("JetBrains Mono"),
    "and so does a menu bar, which the platform themes claim");

  // A platform theme is allowed to apply ITS font after the event loop
  // starts -- qt5ct posts applySettings() as a queued call from its
  // constructor -- and that throws away what BeginQt() set. Measured with
  // QT_QPA_PLATFORMTHEME=qt5ct: the chosen face at the end of startup,
  // the desktop's one turn of the loop later, which is exactly why the
  // menus came up wrong and closing Display Settings put them right.
  // ScheduleUiFontReapplyQt() is the answer, so play the theme's part
  // here and require the schedule to win.
  QApplication::setFont(QFont("Liberation Serif", 9));
  Check(QApplication::font().family() != QString("JetBrains Mono"),
    "a theme applying its own font does take effect");
  ScheduleUiFontReapplyQt();
  for (i = 0; i < 20; i++)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
  Check(QApplication::font().family() == QString("JetBrains Mono"),
    "and the deferred re-apply takes the interface font back");

  // And a menu bar built AFTER the change, which is the startup order:
  // the settings file is read, ApplyUiFontQt() runs, and only then does
  // BuildAstrologMenus() create the menus. A platform theme registers its
  // QMenuBar font as a CLASS default, and a class default outranks the
  // plain application font for a widget created later -- so a face that
  // reached the menus on screen need not reach the ones built next.
  QMenuBar *pmbLate = new QMenuBar(&wOpen);
  Check(pmbLate->font().family() == QString("JetBrains Mono"),
    "and a menu bar built after the change starts out with it");

  // Smoothing, which is why this exists: the console font asks for
  // antialiasing by name and the interface one did not, so on a desktop
  // with it off the menus came out jagged beside a crisp text chart.
  Check(FMenuAntialiasQt(), "interface smoothing defaults to on");
  Check((QApplication::font().styleStrategy() & QFont::PreferAntialias)
    != 0, "smoothing is asked for by name");
  SetMenuAntialiasQt(fFalse);
  ApplyUiFontQt();
  Check(!FMenuAntialiasQt(), "turning interface smoothing off round trips");
  Check(QApplication::font().styleStrategy() == QFont::NoAntialias,
    "and reaches the font");

  // Back to no preference, which is the baseline captured above.
  SetMenuFontQt("", 0);
  SetMenuAntialiasQt(fTrue);
  ApplyUiFontQt();
  Check(QApplication::font().family() == fontStart.family(),
    "an empty interface face means the default again");
  Check(QApplication::font().pointSizeF() == fontStart.pointSizeF(),
    "and size 0 means the desktop's own size again");

  // And then back to whatever the settings file actually asked for, since
  // every group after this one measures dialogs in that font. Nothing is
  // written anywhere: these live in memory until the user saves settings.
  SetConsoleFontQt(strConSave.toUtf8().constData(), nConSave);
  SetConsoleAntialiasQt(fConSave);
  SetMenuFontQt(strMenSave.toUtf8().constData(), nMenSave);
  SetMenuAntialiasQt(fMenSave);
  ApplyUiFontQt();
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

  // The chosen interface theme: View / Window Settings / Interface Theme.
  // It lives in astrolog.as with everything else, so this writes nothing
  // to disk and there is no config of the running developer's to redirect
  // away from -- only the value the run started with to put back.
  {
    QString strThemeSave = StrThemePrefQt();

    SetThemePrefQt("auto");
    Check(StrThemePrefQt() == "auto", "with nothing chosen, the theme is auto");

    // The environment variable outranks the saved preference, because it
    // is how a developer checks one run under the other scheme without
    // disturbing what the user chose. Both directions, so this cannot
    // pass by the two happening to agree.
    // The env var is unset around each "is the SAVED theme honoured"
    // assertion: left set, that check passes on the env var whether or
    // not the saved preference is read at all. The two saved-theme
    // checks also have to be a PAIR: detection
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
    // What the dark palette is actually made of. Each of these was a
    // complaint before it was an assertion.
    QPalette palDark = QApplication::palette();
    QColor coBg = palDark.color(QPalette::Window);
    QColor coFg = palDark.color(QPalette::WindowText);
    Check(coFg.lightness() < 240,
      "dialog text is a gray rather than white (%d)", coFg.lightness());
    Check(coFg.lightness() - coBg.lightness() >= 120,
      "and still well clear of the surface it is on (%d)",
      coFg.lightness() - coBg.lightness());
    // Qt derives frames, group box lines and sunken panels from these,
    // and a default-constructed palette carries the LIGHT ones -- so a
    // dark dialog was drawn with light-theme edges.
    CONST QPalette::ColorRole rgrole[] = {QPalette::Light,
      QPalette::Midlight, QPalette::Mid, QPalette::Dark, QPalette::Shadow};
    for (int iRole = 0;
      iRole < (int)(sizeof(rgrole)/sizeof(QPalette::ColorRole)); iRole++)
      Check(palDark.color(rgrole[iRole]).lightness() < 128,
        "the shade Qt draws edges from is dark too (%d: %d)", iRole,
        palDark.color(rgrole[iRole]).lightness());

    // And the toggles beside menu items, which is where this started:
    // Fusion derives their outline by DARKENING the window colour, so on
    // a dark palette they came out black on black. Drawn here exactly as
    // a menu draws one, and measured against what it sits on.
    QImage im(24, 24, QImage::Format_RGB32);
    im.fill(coBg);
    {
      QPainter paint(&im);
      QStyleOptionButton optBox;
      optBox.rect = QRect(4, 4, 16, 16);
      optBox.state = QStyle::State_Enabled;
      optBox.palette = palDark;
      QApplication::style()->drawPrimitive(QStyle::PE_IndicatorCheckBox,
        &optBox, &paint, NULL);
    }
    int dMax = 0, x, y;
    for (y = 0; y < im.height(); y++)
      for (x = 0; x < im.width(); x++) {
        int d = QColor(im.pixel(x, y)).lightness() - coBg.lightness();
        if (d > dMax)
          dMax = d;
      }
    Check(dMax >= 25,
      "an unchecked toggle is visible against the surface (%d)", dMax);

    // The one piece of arithmetic behind the Windows title bar. A
    // COLORREF is 0x00BBGGRR, so a colour handed to it straight comes out
    // with red and blue swapped -- a wrong dark bar rather than none, and
    // nothing on this platform would ever show it.
    Check(LRgbrefFromCoQt(QColor(0x2B, 0x31, 0x38)) == 0x38312B,
      "a caption colour is packed as Windows reads it (0x%06lX)",
      LRgbrefFromCoQt(QColor(0x2B, 0x31, 0x38)));
    Check(LRgbrefFromCoQt(QColor(0xFF, 0x00, 0x00)) == 0x0000FF,
      "and red does not come out blue (0x%06lX)",
      LRgbrefFromCoQt(QColor(0xFF, 0x00, 0x00)));

    SetThemePrefQt("light");
    ApplyColorSchemeQt();
    Check(QApplication::palette().color(QPalette::Window).lightness() >= 128,
      "and choosing Light brings it back");
    SetThemePrefQt(strThemeSave.toUtf8().constData());
    ApplyColorSchemeQt();
    QApplication::setPalette(palWas);
  }
  printf("  the chosen interface theme is honoured, and the env var beats it\n");
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
// magnitude of one int, with gi.fPause a second stop on top, so any
// control could start the chart moving by accident. This port has one
// running state behind FAnimRunningQt()/SetAnimRunningQt(); these pin
// down that only the two controls meant to touch it do.
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
// glibc aborts on.
//
// is.S is put back by hand after the check so a regression here fails
// this group instead of taking the rest of the suite down with it.

// The Rising chart's altitude gradient. XChartRising() packs either one
// bit per
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

  // The HTML half of "Export Text and Print in Intuitive Manner"
  // (us.fSmartSave, "-YO"), which is the shape Windows uses for printing
  // a text chart and for Save/Copy Text with HTML output on. Astrolog's
  // HTML page has a white body and SzColorHTML() reads rgbbmp[], so
  // without the swap a chart coloured for a black background is printed
  // onto a white one. Windows does InitColorPalette(1) for the length of
  // the capture and puts it back (wdriver.cpp:2864 and 2902); nothing
  // here did.
  //
  // Only observable with "Alternate Color Palette" on, since that is what
  // makes the second palette exist -- InitColorPalette() is a no-op
  // otherwise, on both builds.
  {
    flag fAltSav = gs.fAltPalette, fColorSav = us.fAnsiColor;
    flag fSmartSav = us.fSmartSave, fInvSav = gs.fInverse;
    flag fCharSav = us.fAnsiChar;
    KV rgbbmpSav[cColor2];
    QByteArray baOff, baOn;
    QFile fileHtml;

    CopyRgb((pbyte)rgbbmp, (pbyte)rgbbmpSav, sizeof(rgbbmp));
    gs.fAltPalette = fTrue;
    us.fAnsiColor = fTrue;
    // Ansi CHARACTERS off in both captures on purpose. Smart Save turns
    // them off as well, and with them left on the two files differ over
    // the box edges alone -- which is a true difference and the wrong
    // one, and it made this pass with the palette swap sabotaged.
    us.fAnsiChar = fFalse;

    us.fSmartSave = fFalse;
    QFile::remove(QString(szFile));
    CaptureTextToFileQt(szFile, fTrue);
    fileHtml.setFileName(QString(szFile));
    if (fileHtml.open(QIODevice::ReadOnly))
      baOff = fileHtml.readAll();
    fileHtml.close();

    us.fSmartSave = fTrue;
    QFile::remove(QString(szFile));
    CaptureTextToFileQt(szFile, fTrue);
    fileHtml.setFileName(QString(szFile));
    if (fileHtml.open(QIODevice::ReadOnly))
      baOn = fileHtml.readAll();
    fileHtml.close();
    QFile::remove(QString(szFile));

    Check(baOff.size() > 100 && baOn.size() > 100,
      "both HTML captures wrote a chart (%d and %d bytes)",
      (int)baOff.size(), (int)baOn.size());
    Check(baOff != baOn,
      "and Smart Save colours the HTML one for a white page instead");
    Check(memcmp(rgbbmp, rgbbmpSav, sizeof(rgbbmp)) == 0,
      "and puts the palette back when it is done");
    Check(gs.fInverse == fInvSav, "and gs.fInverse with it");

    // The assertion above cannot isolate the palette, and that matters:
    // gs.fInverse alone moves the file, because the chart header's colour
    // follows it (charts0.cpp:109). The first draft of this passed with
    // the palette swap deliberately broken for exactly that reason. So
    // ask about the palette directly -- bright green, which the two
    // tables disagree about (0x00ff00 against 0x009f00) and which every
    // text chart uses.
    {
      QByteArray baAlt, baStd;

      gs.fAltPalette = fTrue;
      InitColorPalette(1);
      baAlt = SzColorHTML(kGreen);
      InitColorPalette(0);
      baStd = SzColorHTML(kGreen);
      Check(!baAlt.isEmpty() && baAlt != baStd,
        "the two palettes disagree about bright green (\"%s\" against "
        "\"%s\")", baAlt.constData(), baStd.constData());
      Check(baOn.contains(baAlt),
        "and the Smart Save capture wrote the white-page one");
      Check(!baOff.contains(baAlt),
        "where the capture without it did not");
    }

    us.fSmartSave = fSmartSav;
    us.fAnsiColor = fColorSav;
    us.fAnsiChar = fCharSav;
    gs.fAltPalette = fAltSav;
    CopyRgb((pbyte)rgbbmpSav, (pbyte)rgbbmp, sizeof(rgbbmp));
  }
}


// The Chart Info dialog's "Apply Info" button, which is the whole point of
// the atlas: pick a city, and the location, zone and Daylight fields fill
// themselves in.
//
// Two of those were wrong answers here. The zone came from
// ZondefFromIzn(), the zone area's LATEST offset, and Daylight Saving was
// never written at all -- so a summer date in a daylight zone was cast an
// hour out by the button whose job is to prevent exactly that. Windows
// asks DisplayTimezoneChanges(izn, fFalse, &ci) for the chart's own date
// and fills in both (wdialog.cpp:324).
//
// Both directions, and that is the assertion rather than a nicety: a fix
// that simply wrote "Yes" would pass a July check on its own. Measured
// through the probe before it was written -- Seattle, izn 135, reads
// dst 1.00 in July 1990 and 0.00 in January, zone 8 either way.
//
// Driven through the real dialog rather than against the helper, because
// the bug was in the WIRING: the computation it now calls was always
// there and always right.

#ifdef ATLAS
static QString StrApplyInfoQt(int mon, int day, int yea)
{
  QString strDst;

  DriveModalQt(ShowChartInfoDialogQt, [&](QWidget *pw) {
    QLineEdit *peLoc = pw->findChild<QLineEdit *>("deInLoc");
    QComboBox *pcbMon = pw->findChild<QComboBox *>("dcInMon");
    QComboBox *pcbDay = pw->findChild<QComboBox *>("dcInDay");
    QComboBox *pcbYea = pw->findChild<QComboBox *>("dcInYea");
    QComboBox *pcbDst = pw->findChild<QComboBox *>("dcInDst");
    QListWidget *plist = pw->findChild<QListWidget *>("dlIn");
    QPushButton *ppbCity = pw->findChild<QPushButton *>("dbInCity");
    QPushButton *ppbAppl = pw->findChild<QPushButton *>("dbInAppl");

    if (peLoc == NULL || pcbMon == NULL || pcbDst == NULL ||
      plist == NULL || ppbCity == NULL || ppbAppl == NULL) {
      strDst = "controls missing";
    } else {
      pcbMon->setEditText(QString(szMonth[mon]).left(3));
      if (pcbDay != NULL) pcbDay->setEditText(QString::number(day));
      if (pcbYea != NULL) pcbYea->setEditText(QString::number(yea));
      // A sentinel the button has to overwrite. Without it "No" in
      // January proves nothing -- it is also what the field already said.
      pcbDst->setEditText("sentinel");
      peLoc->setText("Seattle");
      ppbCity->click();
      if (plist->count() > 0)
        plist->setCurrentRow(0);
      ppbAppl->click();
      strDst = pcbDst->currentText();
    }
    for (QPushButton *ppb : pw->findChildren<QPushButton *>())
      if (ppb->text() == "Cancel")
        ppb->click();
    pw->close();
  });
  return strDst;
}
#endif


// "Chart for Now", which is not the same command in a relationship chart.
//
// Windows' cmdNow is Animate(iAnimNow, 0), and Animate() chooses which
// chart the present moment lands in: the twin slot for a comparison or a
// transit chart, the transit slot (and is.JDp with it) for a progression,
// the main chart only otherwise. This port called FInputData() and
// RecastAndRedrawQt(), which assigns ciMain unconditionally -- so on a
// transit chart the menu item REPLACED THE NATAL CHART with today
// instead of moving the transits to it, and the chart the user had been
// looking at was gone.
//
// Both shapes, because the plain one always worked and a fix that broke
// it would be worse than the bug.

// The "Now" button in the Transit and Progression dialogs, which filled
// in four of the six fields it is supposed to.
//
// Windows sets the date, the time, AND the Daylight and Zone boxes, in
// one SetEditSZOA() call from ciDefa (wdialog.cpp:2585 and 2718). This
// port set only the four date and time boxes -- and that is a wrong
// answer, not a missing convenience: GetTimeNow() is asked for the
// present moment expressed in ciDefa's zone and daylight setting, while
// OK reads the zone back out of the two boxes. Left holding the previous
// chart's zone, "Now" produced a transit chart that was not now, out by
// the difference between the two zones.
//
// Both dialogs, because the two are copies of each other and the fix had
// to be made twice.

static void TestNowButtonQt(void (*pfnOpen)(), CONST char *szDst,
  CONST char *szZon, CONST char *szWhich)
{
  QString strDst, strZon;

  DriveModalQt(pfnOpen, [&](QWidget *pw) {
    QComboBox *pcbDst = pw->findChild<QComboBox *>(szDst);
    QComboBox *pcbZon = pw->findChild<QComboBox *>(szZon);
    QPushButton *ppbNow = NULL;

    for (QPushButton *ppb : pw->findChildren<QPushButton *>())
      if (ppb->text().contains("Now"))
        ppbNow = ppb;
    if (pcbDst != NULL && pcbZon != NULL && ppbNow != NULL) {
      // Sentinels the button has to overwrite. Without them a field that
      // happened to agree with ciDefa would pass on its own.
      pcbDst->setEditText("sentinelD");
      pcbZon->setEditText("sentinelZ");
      ppbNow->click();
      strDst = pcbDst->currentText();
      strZon = pcbZon->currentText();
    }
    for (QPushButton *ppb : pw->findChildren<QPushButton *>())
      if (ppb->text() == "Cancel")
        ppb->click();
  });

  // Compared against ciDefa put through the same formatting the dialog
  // uses when it loads, rather than against a literal: what "5 hours
  // west" reads as in that box is SzZone()'s business, and the claim
  // here is that the button writes ciDefa's zone, not that it writes any
  // particular spelling.
  {
    char szWant[cchSzDef];
    CONST char *pchWant;

    sprintf2(S(szWant), "%s", SzZone(ciDefa.zon));
    pchWant = szWant[0] == '+' ? &szWant[1] : szWant;
    Check(strDst == "Yes",
      "%s: \"Now\" sets Daylight from the default chart info (\"%s\")",
      szWhich, strDst.toLocal8Bit().constData());
    Check(strZon == QString(pchWant),
      "%s: and the time zone with it (\"%s\", wanted \"%s\")",
      szWhich, strZon.toLocal8Bit().constData(), pchWant);
  }
}


// The orbital-trail buffer, gi.rgspace, and the Graphics Settings field
// that sizes it ("Orbit trail steps", -YXj, gs.cspace).
//
// It is allocated ONCE, as oNorm1*gs.cspace entries, and every allocation
// site guards on the pointer being NULL (xdevice.cpp:2608,
// xcharts1.cpp:2810). So whoever changes the count has to free it. The
// "-YXj" switch handler does (switch.cpp:1277) and so does Windows'
// Graphics Settings dialog (wdialog.cpp:3014); this port's assigned the
// field and nothing else.
//
// That is a heap overflow rather than a wrong picture:
// xcharts1.cpp:2693 writes at gi.ispace*oNorm1 with gi.ispace cycling
// modulo the NEW gs.cspace, so raising the count from 4 to 16 in the
// dialog and drawing an orbit chart writes four times past the end of
// the buffer.
//
// Both directions. Freeing unconditionally would throw away the trail
// the user is watching every time they press OK on an unrelated setting.

static void DriveSpaceCountQt(int cspace)
{
  DriveModalQt(ShowGraphicsSettingsDialogQt, [cspace](QWidget *pw) {
    QLineEdit *peSpace = pw->findChild<QLineEdit *>("deGr_YXj");

    if (peSpace != NULL)
      peSpace->setText(QString::number(cspace));
    for (QPushButton *ppb : pw->findChildren<QPushButton *>())
      if (ppb->text() == "OK") {
        ppb->click();
        return;
      }
    pw->close();
  });
}


// gs.nFontAll, the packed form of the six graphics font settings, and
// gi.nFontPrev, the copy File Settings restores from.
//
// The Graphics Settings dialog stored the six fields and recomputed
// neither. That is not a cache going stale in private: "Save Program
// Settings" writes gs.nFontAll as ":YXf #%06x" (io.cpp:2572), so a font
// chosen in the dialog was SAVED WRONG; the metafile writer sizes its
// object table from it (xdevice.cpp:1808 and 1866); and File Settings'
// "Use Astrolog Font" box reads it and, when re-ticked, multiplies
// gi.nFontPrev -- which nothing here ever wrote, so that restored
// whatever astrolog.as had rather than what the user picked.
//
// Windows does both, in this order, at wdialog.cpp:3067, and so does the
// "-YXf" switch handler.

// The chart size typed into Graphics Settings, which came back smaller
// than it was typed.
//
// The dialog resized the WINDOW to the chart's size. A window is bigger
// than its chart viewport by the menu bar and the frame, and with "Window
// Resizes Chart" on -- the default -- the viewport's size is written
// straight back into gs.xWin/gs.yWin by the canvas. So 640 by 480 became
// 640 by rather less than 480, silently, and reopening the dialog showed
// the reduced number. ResizeWindowToChartQt() measures the chrome and
// adds it; that is what it exists for, and Windows calls its equivalent
// here (wdialog.cpp:3006).

// Menu items that change a setting ANOTHER item displays.
//
// Windows corrects the other item's check mark by hand in the same
// breath -- WiCheckMenu(cmdOther, ...), ten such sites in wdriver.cpp --
// and this port had matched some and missed others, so an item went on
// claiming the opposite of what the program was doing. ConnectMenuQt()
// re-derives every registered check mark after any menu action now,
// which closes the class rather than the four instances.
//
// Also here because it is the same handlers: Character Scale's Decrease
// and Increase left gs.fAutoScale alone, and with "Autoscale Glyphs"
// (-XQ0) on, FActionX() recomputes gs.nScale from the window size for
// the duration of the draw (xscreen.cpp:1495) -- so those two menu items
// had NO VISIBLE EFFECT at all. Windows clears it, and the two "Text"
// items beside them already did.

static void TestMenuSideEffectsQt()
{
  QAction *paSide = PaFindActionTestQt("Show Info &Sidebar");
  QAction *paText = PaFindActionTestQt("Show Chart &Info");
  QAction *paLine = PaFindActionTestQt("Show Constellation &Lines");
  QAction *paStar = PaFindActionTestQt("Show Full &Star List");
  QAction *paIndS = PaFindActionTestQt("Draw &South Indian");
  QAction *paInd = PaFindActionTestQt("Show &Indian Wheels");
  QAction *paDown = PaFindActionTestQt("&Decrease");
  flag fSideSav = gs.fDoSidebar, fTextSav = gs.fText;
  flag fStarSav = gs.fAllStar, fIndSav = gs.fIndianWheel;
  flag fAutoSav = gs.fAutoScale, fHouseSav = gs.fHouseExtra;
  int nScaleSav = gs.nScale, nModeSav = gi.nMode;

  Group("Menu side effects");
  Check(paSide != NULL && paText != NULL && paLine != NULL &&
    paStar != NULL && paIndS != NULL && paInd != NULL && paDown != NULL,
    "the seven menu items this is about are all present");
  if (paSide == NULL || paText == NULL || paLine == NULL ||
    paStar == NULL || paIndS == NULL || paInd == NULL || paDown == NULL)
    return;

  // Sidebar on forces "Show Chart Info" on.
  gs.fDoSidebar = fFalse; gs.fText = fFalse;
  RedoMenuQt();
  paSide->trigger();
  Check(gs.fText && paText->isChecked(),
    "the sidebar turns chart info on, and its item says so (flag %d, "
    "mark %d)", gs.fText, (int)paText->isChecked());

  // Constellation lines force the full star list on. This one is a
  // TOGGLE, over qi.fStarLine, which has no accessor out here -- and
  // "menu-actions" leaves it on, so triggering it once turned the lines
  // OFF and this failed in the full suite while passing alone. Read the
  // state off the item's own check mark, which RedoMenuQt() keeps
  // honest, and put it where this needs it.
  RedoMenuQt();
  if (paLine->isChecked())
    paLine->trigger();
  Check(!paLine->isChecked(),
    "constellation lines start off, whatever an earlier group left");
  gs.fAllStar = fFalse;
  RedoMenuQt();
  paLine->trigger();
  Check(gs.fAllStar && paStar->isChecked(),
    "constellation lines turn the full star list on, and its item says "
    "so (flag %d, mark %d)", gs.fAllStar, (int)paStar->isChecked());
  paLine->trigger();               // back off, it edits the star list

  // "Draw South Indian" forces "Show Indian Wheels" on.
  gs.fIndianWheel = fFalse;
  RedoMenuQt();
  paIndS->trigger();
  Check(gs.fIndianWheel && paInd->isChecked(),
    "South Indian turns Indian wheels on, and its item says so (flag %d, "
    "mark %d)", gs.fIndianWheel, (int)paInd->isChecked());

  // Character Scale / Decrease clears autoscaling, or it does nothing at
  // all on the chart types that autoscale.
  gs.fAutoScale = fTrue;
  gs.nScale = 300;
  paDown->trigger();
  Check(gs.nScale == 200, "Decrease steps the character scale down (%d)",
    gs.nScale);
  Check(!gs.fAutoScale,
    "and turns autoscaling off, or the render would overrule it");

  gs.fDoSidebar = fSideSav; gs.fText = fTextSav;
  gs.fAllStar = fStarSav; gs.fIndianWheel = fIndSav;
  gs.fAutoScale = fAutoSav; gs.fHouseExtra = fHouseSav;
  gs.nScale = nScaleSav;
  SetChartModeQt(nModeSav);
  RedoMenuQt();
}


static void TestGraphicsSizeQt()
{
  int xWinSav = gs.xWin, yWinSav = gs.yWin;
  flag fWinChartSav = FWindowChartQt(), fChartWinSav = FChartWindowQt();
  int xWant = 640, yWant = 480;

  Group("Chart size from Graphics Settings");

  // "Chart Resizes Window" OFF, which is the default and the state the
  // bug appears in. With it on, RedrawQt() fits the window around the
  // chart at the end of the dialog and quietly corrects the mistake --
  // and an earlier group leaves it on, so the assertion below passed in
  // the full suite with the bug deliberately present while failing when
  // the group ran alone. Measured, not guessed: window 640x505 against
  // 640x480 for the same sabotaged build.
  SetChartWindowQt(fFalse);

  // Turned ON rather than skipped when it is off. The first draft
  // returned early with a message, and in the full suite that is exactly
  // what happened -- an earlier group had left it off, so the group
  // reported "0 assertions" and proved nothing while passing. A test
  // that quietly does not run is worse than one that fails.
  SetWindowChartQt(fTrue);
  // Somewhere else to start from, so "took what was typed" and "left it
  // alone" are different answers.
  gs.xWin = 500; gs.yWin = 400;
  DriveModalQt(ShowGraphicsSettingsDialogQt, [xWant, yWant](QWidget *pw) {
    QLineEdit *peX = pw->findChild<QLineEdit *>("deGr_Xw_x");
    QLineEdit *peY = pw->findChild<QLineEdit *>("deGr_Xw_y");

    if (peX != NULL) peX->setText(QString::number(xWant));
    if (peY != NULL) peY->setText(QString::number(yWant));
    for (QPushButton *ppb : pw->findChildren<QPushButton *>())
      if (ppb->text() == "OK") {
        ppb->click();
        return;
      }
    pw->close();
  });
  QApplication::processEvents(QEventLoop::AllEvents, 200 * nScaleTest);

  // The VIEWPORT, not gs.yWin. Both are true statements, but only one of
  // them is measured deterministically here: gs.yWin is written back by
  // the canvas's paint handler, and whether that has run by now depends
  // on what the previous group left the window doing. The first draft
  // asserted gs.yWin, bit when the group ran alone, and passed in the
  // full suite with the bug deliberately present -- which is the same as
  // no assertion.
  QSize sizeView = SizeChartViewportTestQt();
  Check(sizeView.width() == xWant && sizeView.height() == yWant,
    "the chart viewport is the size that was typed (%d by %d, wanted "
    "%d by %d; window %d by %d)", sizeView.width(), sizeView.height(),
    xWant, yWant, gi.qwind != NULL ? gi.qwind->width() : -1,
    gi.qwind != NULL ? gi.qwind->height() : -1);

  gs.xWin = xWinSav; gs.yWin = yWinSav;
  SetWindowChartQt(fWinChartSav);
  SetChartWindowQt(fChartWinSav);
  ResizeWindowToChartQt();
  RedrawQt();
}


// The three plain combo boxes in Graphics Settings whose store loop kept
// the LAST prefix match instead of the first exact one.
//
// FMatchSz() matches a prefix of three characters or more, and two of
// these lists contain entries that are prefixes of other entries: "Region"
// is a prefix of "Region+State", and "Rays 1" of both "Rays 1,2" and
// "Rays 12345". Keeping the last match therefore stored a DIFFERENT row
// than the one clicked -- from the dropdown, not from typing, which is
// what makes it worth an assertion rather than a note. Windows uses
// FEqSzI() and breaks on the first (wdialog.cpp:3038, 3044, 3080).

// Numeric dialog fields, read through Astrolog's own parsers.
//
// Windows reads every one of them with GetEditN()/GetEditR(), which are
// NFromSz() and RFromSz() -- and those accept more than a plain decimal:
// "#1f" is hexadecimal, "##1010" binary, and either kind may be an
// ASTROEXPRESSION when it starts with "~". QString::toInt() and
// toDouble() accept none of that and answer 0, so all three spellings
// worked on Windows and silently became zero here, in about forty
// fields.
//
// Driven through the Graphics Settings dialog because it has both an
// integer field and a real one, and because a value of 0 in either is
// refused by the validation added earlier -- so "became zero" is a
// visible failure rather than a quiet one.

// The four orb-and-colour grids, which stored whatever was typed.
//
// Windows validates every row of these dialogs BEFORE storing any of
// them -- its "for (j = 0; j <= 1; j++)" two-pass loop -- and refuses on
// a bad orb, a bad orb addition or a bad colour. All four of this port's
// stored straight through.
//
// The colour is memory safety rather than a strange picture: KvFromKi()
// is "ki >= 0 ? rgbbmp[ki] : -ki", and rgbbmp[] has cColor2 entries, so a
// number typed into a colour box indexes it unbounded on every redraw.
// Same shape as the telescope planet field.
//
// The Aspect Settings dialog stands for all four here: the four store
// loops are copies of one another and the colour reader is shared, so a
// grid apiece would be four tests of one function. Both directions, and
// the refusal is measured by the setting NOT moving.

// "Reverse Background" and "Monochrome", which did nothing on screen.
//
// InitColorsX() (xscreen.cpp:124) is what turns gs.fInverse and gs.fColor
// into the colours the drawing code reads: gi.kiOn, kiOff, kiLite, kiGray
// and the whole *B family. FActionX() calls it before every render to a
// FILE; RedrawQt() never did, and filled its buffer with a hardcoded
// black. So both View menu items worked when exporting a chart and were
// inert on screen.
//
// Measured before the fix: with reverse on, the commonest pixel of a
// wheel stayed black and kiOn/kiOff stayed 15/0; with monochrome on, the
// render still had 14 distinct colours.
//
// Asserted on the RENDER rather than on the flags, because the flags were
// always being set -- that is exactly why backend_parity_audit.py, which
// works per field, could not see this.

// PrintNotice(), the third kind of message, which had no Qt branch.
//
// PrintWarning() and PrintError() have routed to PrintWarningQt() since
// the port began. PrintNotice() fell through to the plain non-Windows
// path and wrote to STDERR, where a window has nobody reading it --
// Windows shows an information box. Two things reach it: the "-YYT"
// switch and an AstroExpression asking to show a value.
//
// The popup is suppressed for the whole run (NRunQtTestTableQt sets
// qi.fNoPopup), so what this can assert is that PrintNotice() takes the
// Qt path and returns without writing anything: is.S must be untouched
// and nothing may reach the text stream. Before the fix it printed to
// stderr, which is invisible here -- so the real subject is that "-YYT"
// no longer goes through PrintSz() at all, which IS observable: it used
// to land in the captured text.

// The transit graph's own scrolling, which did not exist here.
//
// That chart draws aspect rows until it runs out of window and then
// stops. Windows picks the STARTING row from its scrollbar
// (xcharts2.cpp:1404, "cRow * wi.yScroll / nScrollDiv"); every other
// build had "#else cRow = 0", so the first screenful was all there ever
// was. The scroll area could not help: the rows past the bottom are not
// drawn at all, so there is nothing there to scroll to.
//
// Asserted on the render, because that is the whole of the claim: with
// more rows than fit, scrolling to the end has to show something
// different from the top.

// The two key and mouse help lines this build implements and did not
// document. Same class as the "-W" switch help: "-H documents what a
// build IMPLEMENTS, not what it accepts", and nothing checked it.
//
// DisplayKeysX() described the right button's drag and context menu, the
// left button's scribble, and Shift+B ("Press 'B'" -- an accelerator is
// case sensitive here) to Windows only. This port does all three: the
// mouse handling is a line-for-line port of wdriver.cpp's, and Shift+B is
// in the hotkey table.
//
// Asserted through the menu item rather than the help text, because the
// help is a run of PrintS() calls with no state to read back: what can be
// checked here is that the command the line describes really exists.

static void TestKeyHelpQt()
{
  Group("Key help lines");

  Check(PaFindActionTestQt("Si&ze Chart to Window") != NULL,
    "\"Size Chart to Window\" is a real menu item, so the 'B' line "
    "applies to this build");
  {
    // rghotkeyQt, not the accelerator TEXT table: the resource writes the
    // column as "\tB", because a capital letter is Astrolog's own
    // spelling of Shift, and the text table carries that verbatim. What
    // actually binds the key is the hotkey table, and there it is spelt
    // out.
    CONST char *szKey, *szAction, *szFound = NULL;
    int i, chotkey = CHotkeyTestQt();

    for (i = 0; i < chotkey; i++) {
      HotkeyTestQt(i, &szKey, &szAction);
      if (FEqSz(szAction, "Si&ze Chart to Window"))
        szFound = szKey;
    }
    Check(szFound != NULL && FEqSz(szFound, "Shift+B"),
      "on Shift+B, which is what \"Press 'B'\" means (\"%s\")",
      SzSet(szFound));
  }
  Check(PaFindActionTestQt("&Redraw Screen") != NULL,
    "the chart window is real, so the mouse lines apply too");
}


static void TestChartScrollQt()
{
  int nModeSav = gi.nMode, xSav = gs.xWin, ySav = gs.yWin;
  flag fGraphicsSav = us.fGraphics;
  int nRelSav = us.nRel;
  flag rgfIgnoreSav[objMax], rgfIgnore2Sav[objMax];
  int i;
  QImage imTop, imEnd;
  QAction *paEnd = PaFindActionTestQt("Scroll to &End");
  QAction *paHome = PaFindActionTestQt("Scroll &to Beginning");

  Group("Transit graph scrolling");
  Check(paEnd != NULL && paHome != NULL,
    "the two scroll-to-limit menu items are there");
  if (paEnd == NULL || paHome == NULL)
    return;

  // Its own object set, both sides of the transit, for the reason
  // TestLineDrawingQt() gives: the subject is which ROWS get drawn, and
  // inheriting whatever the last group left would decide that instead.
  for (i = 0; i <= oNorm; i++) {
    rgfIgnoreSav[i] = ignore[i];
    rgfIgnore2Sav[i] = ignore2[i];
    ignore[i] = ignore2[i] = (i > oSat);
  }
  AdjustRestrictions();


  // A short window, so the rows certainly overflow it.
  us.fGraphics = fTrue;
  SetRelQt(rcTransit);
  SetChartModeQt(gTraNatGra);
  gs.xWin = 700; gs.yWin = 220;
  paHome->trigger();
  if (gi.qim != NULL)
    imTop = gi.qim->copy();
  paEnd->trigger();
  if (gi.qim != NULL)
    imEnd = gi.qim->copy();

  Check(!imTop.isNull() && !imEnd.isNull(), "both renders happened");
  Check(imTop != imEnd,
    "scrolling to the end of a transit graph shows different rows");

  paHome->trigger();
  for (i = 0; i <= oNorm; i++) {
    ignore[i] = rgfIgnoreSav[i];
    ignore2[i] = rgfIgnore2Sav[i];
  }
  AdjustRestrictions();
  gs.xWin = xSav; gs.yWin = ySav;
  SetRelQt(nRelSav);
  us.fGraphics = fGraphicsSav;
  SetChartModeQt(nModeSav);
  RedrawQt();
}


static void TestNoticeQt()
{
  char szFile[cchSzMax];
  QByteArray baDir = QDir::tempPath().toLocal8Bit();
  flag fPopupSav = FNoPopupQt(), fGraphicsSav = us.fGraphics;
  int iT;

  Group("Notice messages");
  SetNoPopupQt(fTrue);
  // "-YYt" is a no-op in graphics mode ("if (!us.fGraphics)",
  // switch.cpp:729) and the suite leaves that on, so without this the
  // plain half wrote nothing and proved nothing. "-YYT" has no such
  // guard, which is the asymmetry this group is about.
  us.fGraphics = fFalse;

  sprintf2(S(szFile), "%s/astrolog-qt-notice-%d.txt", baDir.constData(),
    (int)QCoreApplication::applicationPid());

  // The output stream is redirected around the switch itself, because
  // that is when the text is printed -- a capture taken afterwards would
  // see nothing either way, which is how the first draft of this passed
  // with the fix deliberately removed.
  //
  // Opening a FILE and putting is.S back is the same discipline
  // CaptureTextToFileQt() uses. See the guard in NRunQtTestTableQt() for
  // what happens when a group forgets the second half.
  for (iT = 0; iT <= 1; iT++) {
    CONST char *szSwitch = iT == 0 ?
      "-YYt \"ProbeNoticeText\"" : "-YYT \"ProbeNoticePopup\"";
    FILE *fileSav = is.S, *fileT;
    QByteArray ba;
    QFile fileRead;

    QFile::remove(QString(szFile));
    fileT = fopen(szFile, "w");
    Check(fileT != NULL, "a scratch stream opens");
    if (fileT == NULL)
      continue;
    is.S = fileT;
    FProcessCommandLine((char *)szSwitch);
    is.S = fileSav;
    fclose(fileT);

    fileRead.setFileName(QString(szFile));
    if (fileRead.open(QIODevice::ReadOnly))
      ba = fileRead.readAll();
    fileRead.close();
    QFile::remove(QString(szFile));

    if (iT == 0)
      Check(ba.contains("ProbeNoticeText"),
        "\"-YYt\" prints its string as ordinary text (%d bytes)",
        (int)ba.size());
    else
      Check(!ba.contains("ProbeNoticePopup"),
        "and \"-YYT\" does not -- it asks for a popup, which this build "
        "can now show (%d bytes)", (int)ba.size());
  }

  us.fGraphics = fGraphicsSav;
  SetNoPopupQt(fPopupSav);
}


static void TestScreenColorsQt()
{
  flag fInvSav = gs.fInverse, fColorSav = gs.fColor;
  int nModeSav = gi.nMode;
  flag fGraphicsSav = us.fGraphics;
  int cDistinct[3] = {0, 0, 0};
  int rgcGrey[3] = {0, 0, 0}, rgcAll[3] = {1, 1, 1};
  QRgb rgbBack[3] = {0, 0, 0};
  int pass;

  Group("Reverse and monochrome on screen");

  us.fGraphics = fTrue;
  SetChartModeQt(gWheel);
  for (pass = 0; pass < 3; pass++) {
    QMap<QRgb, int> cnt;
    int x, y;

    gs.fInverse = (pass == 1);
    gs.fColor = (pass != 2);
    RedrawQt();
    if (gi.qim == NULL)
      continue;
    for (y = 0; y < gi.qim->height(); y += 3)
      for (x = 0; x < gi.qim->width(); x += 3)
        cnt[gi.qim->pixel(x, y)]++;
    cDistinct[pass] = (int)cnt.size();
    {
      int cGrey = 0, cAll = 0;
      for (QRgb k : cnt.keys()) {
        cAll += cnt[k];
        if (qRed(k) == qGreen(k) && qGreen(k) == qBlue(k))
          cGrey += cnt[k];
      }
      rgcGrey[pass] = cGrey; rgcAll[pass] = cAll;
    }
    // The commonest sample is the background, on every chart that does
    // not fill its frame.
    for (QRgb k : cnt.keys())
      if (cnt[k] > cnt.value(rgbBack[pass], -1))
        rgbBack[pass] = k;
  }

  Check(rgbBack[0] == qRgb(0, 0, 0),
    "a plain wheel is drawn on black (%08x)", (unsigned)rgbBack[0]);
  Check(rgbBack[1] == qRgb(255, 255, 255),
    "and \"Reverse Background\" puts it on white (%08x)",
    (unsigned)rgbBack[1]);
  // Counted as GREY pixels rather than as distinct colours. With
  // antialiasing on -- which an earlier group leaves on -- a monochrome
  // wheel has 57 shades between its background and its ink, all of them
  // grey; a distinct-colour threshold fails on that and says nothing
  // about hue, which is the whole subject. This passes alone and in the
  // full suite for the same reason rather than by a wider bound.
  Check(rgcGrey[0] * 100 / rgcAll[0] < 90,
    "a colour wheel is mostly not grey (%d%% grey)",
    rgcGrey[0] * 100 / rgcAll[0]);
  Check(rgcGrey[2] * 100 / rgcAll[2] >= 99,
    "and \"Monochrome\" leaves only the background, the ink and the "
    "shades between them (%d%% grey, %d colours)",
    rgcGrey[2] * 100 / rgcAll[2], cDistinct[2]);

  gs.fInverse = fInvSav; gs.fColor = fColorSav;
  us.fGraphics = fGraphicsSav;
  SetChartModeQt(nModeSav);
  RedrawQt();
}


static void TestOrbGridQt()
{
  real rOrbSav = rAspOrb[ASPT(1)], rAngSav = rAspAngle[ASPT(1)];
  int kSav = kAspA[ASPT(1)];
  flag fPopupSav = FNoPopupQt();

  Group("Orb grid validation");
  SetNoPopupQt(fTrue);            // the refusal is a message box

  {
    // Each row names a field, a value that must be refused, and a good
    // value that must LAND -- a different one from the starting value, or
    // "the dialog applied nothing" would pass the second half as easily
    // as the first. That was this group's first draft.
    static CONST struct {
      CONST char *szId;     // control family: orb, angle or colour
      int iDup;             // which row -- aspect 1, the conjunction
      CONST char *szBad, *szGood;
      int iField;           // 0 orb, 1 angle, 2 colour
      real rWant;           // what the good value must produce
      CONST char *szWhat;
    } rgt[] = {
      {"deo", 0, "400",  "9",     0,  9.0, "an orb past 360 degrees"},
      {"dea", 0, "-400", "45",    1, 45.0, "an angle past -360"},
      {"dck", 0, "999",  "Red",   2,  0.0, "a colour index past the palette"}};
    int iT;

    for (iT = 0; iT < (int)(sizeof(rgt)/sizeof(*rgt)); iT++) {
      CONST char *szId = rgt[iT].szId, *szBad = rgt[iT].szBad;
      CONST char *szGood = rgt[iT].szGood;
      int iDup = rgt[iT].iDup;

      rAspOrb[ASPT(1)] = 7.0; rAspAngle[ASPT(1)] = 0.0; kAspA[ASPT(1)] = 15;
      DriveModalQt(ShowAspectDialogQt, [szId, iDup, szBad](QWidget *pw) {
        QList<QWidget *> rg = pw->findChildren<QWidget *>(szId);
        if (iDup < rg.size()) {
          QLineEdit *pe = qobject_cast<QLineEdit *>(rg[iDup]);
          QComboBox *pcb = qobject_cast<QComboBox *>(rg[iDup]);
          if (pe != NULL)
            pe->setText(szBad);
          else if (pcb != NULL)
            pcb->setEditText(szBad);
        }
        for (QPushButton *ppb : pw->findChildren<QPushButton *>())
          if (ppb->text() == "OK") { ppb->click(); return; }
        pw->close();
      });
      Check(rAspOrb[ASPT(1)] == 7.0 && rAspAngle[ASPT(1)] == 0.0 && kAspA[ASPT(1)] == 15,
        "%s is refused and nothing is stored (%.1f %.1f %d)",
        rgt[iT].szWhat, (double)rAspOrb[ASPT(1)], (double)rAspAngle[ASPT(1)], kAspA[ASPT(1)]);

      // And the same field with a good value still applies, or "refuses
      // everything" would pass the line above just as well.
      DriveModalQt(ShowAspectDialogQt, [szId, iDup, szGood](QWidget *pw) {
        QList<QWidget *> rg = pw->findChildren<QWidget *>(szId);
        if (iDup < rg.size()) {
          QLineEdit *pe = qobject_cast<QLineEdit *>(rg[iDup]);
          QComboBox *pcb = qobject_cast<QComboBox *>(rg[iDup]);
          if (pe != NULL)
            pe->setText(szGood);
          else if (pcb != NULL)
            pcb->setEditText(szGood);
        }
        for (QPushButton *ppb : pw->findChildren<QPushButton *>())
          if (ppb->text() == "OK") { ppb->click(); return; }
        pw->close();
      });
      {
        real rGot = rgt[iT].iField == 0 ? rAspOrb[ASPT(1)] :
          (rgt[iT].iField == 1 ? rAspAngle[ASPT(1)] :
          (real)kAspA[ASPT(1)]);
        real rWant = rgt[iT].iField == 2 ?
          (real)NParseSz(rgt[iT].szGood, pmColor) : rgt[iT].rWant;

        Check(rGot == rWant && rWant != 15.0,
          "and \"%s\" in the same field is applied (%.1f, want %.1f)",
          rgt[iT].szGood, (double)rGot, (double)rWant);
      }
    }
  }

  rAspOrb[ASPT(1)] = rOrbSav; rAspAngle[ASPT(1)] = rAngSav; kAspA[ASPT(1)] = kSav;
  SetNoPopupQt(fPopupSav);
}


static void TestFieldParseQt()
{
  int nGridSav = gs.nGridCell;
  real rRotSav = gs.rRot;
  flag fExpSav = us.fExpOff;

  Group("Numeric field parsing");

  {
    static CONST struct {
      CONST char *szId, *szType;
      int nWant;
      CONST char *szWhat;
    } rgt[] = {
      {"deGr_YXg", "24",       24, "a plain decimal"},
      {"deGr_YXg", "#18",      24, "hexadecimal, which Windows accepts"},
      {"deGr_YXg", "##11000",  24, "and binary"},
      {"deGr_YXg", "~Add 20 4", 24, "and an AstroExpression"}};
    int iT;

    us.fExpOff = fFalse;
    for (iT = 0; iT < (int)(sizeof(rgt)/sizeof(*rgt)); iT++) {
      CONST char *szId = rgt[iT].szId, *szType = rgt[iT].szType;

      gs.nGridCell = 1;
      DriveModalQt(ShowGraphicsSettingsDialogQt, [szId, szType](QWidget *pw) {
        QLineEdit *pe = pw->findChild<QLineEdit *>(szId);

        if (pe != NULL)
          pe->setText(szType);
        for (QPushButton *ppb : pw->findChildren<QPushButton *>())
          if (ppb->text() == "OK") { ppb->click(); return; }
        pw->close();
      });
      Check(gs.nGridCell == rgt[iT].nWant, "\"%s\" reads as %d -- %s (%d)",
        szType, rgt[iT].nWant, rgt[iT].szWhat, gs.nGridCell);
    }
  }

  // And a real, which takes the expression form but not the two bases.
  gs.rRot = 0.0;
  DriveModalQt(ShowGraphicsSettingsDialogQt, [](QWidget *pw) {
    QLineEdit *pe = pw->findChild<QLineEdit *>("deGr_XW");

    if (pe != NULL)
      pe->setText("~Add 30 15");
    for (QPushButton *ppb : pw->findChildren<QPushButton *>())
      if (ppb->text() == "OK") { ppb->click(); return; }
    pw->close();
  });
  Check(gs.rRot == 45.0,
    "and a real field takes an AstroExpression too (%.2f)",
    (double)gs.rRot);

  us.fExpOff = fExpSav;
  gs.nGridCell = nGridSav;
  gs.rRot = rRotSav;
}


static void TestComboPickQt()
{
  int nDecaTypeSav = gs.nDecaType, nLabelCitySav = gs.nLabelCity;
  flag fLabelAspSav = gs.fLabelAsp;
  int nDecaFillSav = gs.nDecaFill;

  Group("Graphics Settings combo picks");

  {
    static CONST struct {
      CONST char *szId, *szPick;
      int nWant;
      int *pn;
      CONST char *szWhat;
    } rgt[] = {
      {"dcGr_YXv", "Rays 1",     3, &gs.nDecaType,
       "\"Rays 1\" is Rays 1, not Rays 12345"},
      {"dcGr_YXv", "Rays 12345", 5, &gs.nDecaType,
       "and the longer one is still itself"},
      {"dcGr_XL",  "Region",     1, &gs.nLabelCity,
       "\"Region\" is Region, not Region+State"},
      {"dcGr_XL",  "Region+State", 2, &gs.nLabelCity,
       "and Region+State is still itself"},
      {"dcGr_Xv",  "Rainbow RYB", 3, &gs.nDecaFill,
       "and a list with no prefix pairs is unaffected"}};
    int iT;

    for (iT = 0; iT < (int)(sizeof(rgt)/sizeof(*rgt)); iT++) {
      CONST char *szId = rgt[iT].szId, *szPick = rgt[iT].szPick;

      *rgt[iT].pn = -1;
      DriveModalQt(ShowGraphicsSettingsDialogQt, [szId, szPick](QWidget *pw) {
        QComboBox *pcb = pw->findChild<QComboBox *>(szId);

        if (pcb != NULL)
          pcb->setEditText(szPick);
        for (QPushButton *ppb : pw->findChildren<QPushButton *>())
          if (ppb->text() == "OK") { ppb->click(); return; }
        pw->close();
      });
      Check(*rgt[iT].pn == rgt[iT].nWant, "%s (%d, want %d)",
        rgt[iT].szWhat, *rgt[iT].pn, rgt[iT].nWant);
    }
  }

  gs.nDecaType = nDecaTypeSav; gs.nDecaFill = nDecaFillSav;
  gs.nLabelCity = nLabelCitySav; gs.fLabelAsp = fLabelAspSav;
}


static void TestFontPackQt()
{
  int nFontAllSav = gs.nFontAll, nFontPrevSav = gi.nFontPrev;
  int nTxtSav = gs.nFontTxt, nSigSav = gs.nFontSig, nHouSav = gs.nFontHou;
  int nObjSav = gs.nFontObj, nAspSav = gs.nFontAsp, nNakSav = gs.nFontNak;
  int nWant;

  Group("Graphics font packing");

  // Start from "no fonts at all", so the packed value the dialog must
  // produce cannot be the one it already held.
  gs.nFontTxt = gs.nFontSig = gs.nFontHou = 0;
  gs.nFontObj = gs.nFontAsp = gs.nFontNak = 0;
  gs.nFontAll = 0;
  gi.nFontPrev = 0;

  // All six font combos, not one: rc2qt.py splits the trailing digit off
  // a resource symbol into an index, so the six share the object name
  // "dcGr_Xf" and findChild() cannot tell them apart. Setting them all
  // asks the same question and needs no such guess.
  //
  // "Consolas" and not "Wingdings", which the first draft used: each slot
  // takes only the fonts rgszFontAllow[] says can draw its glyphs, and
  // Wingdings is refused by three of the six. Index 11 is one of the
  // three allowed in all of them.
  int cFound = 0;
  DriveModalQt(ShowGraphicsSettingsDialogQt, [&cFound](QWidget *pw) {
    for (QComboBox *pcb : pw->findChildren<QComboBox *>("dcGr_Xf")) {
      pcb->setEditText("Consolas");
      cFound++;
    }
    for (QPushButton *ppb : pw->findChildren<QPushButton *>())
      if (ppb->text() == "OK") {
        ppb->click();
        return;
      }
    pw->close();
  });

  Check(cFound == 6, "the dialog has six font combo boxes (found %d)",
    cFound);
  Check(gs.nFontSig == 11 && gs.nFontNak == 11,
    "and it stored what was picked in them (sig %d, nak %d)",
    gs.nFontSig, gs.nFontNak);
  nWant = gs.nFontTxt*0x100000 + gs.nFontSig*0x10000 +
    gs.nFontHou*0x1000 + gs.nFontObj*0x100 + gs.nFontAsp*0x10 +
    gs.nFontNak;
  Check(gs.nFontAll == nWant,
    "and gs.nFontAll is the six of them packed, which is what the "
    "settings writer and the metafile writer read (#%06x, wanted #%06x)",
    gs.nFontAll, nWant);
  Check(gi.nFontPrev == gs.nFontAll,
    "and gi.nFontPrev followed it, so File Settings restores this set "
    "rather than the last one (#%06x)", gi.nFontPrev);

  // Which fonts each slot OFFERS, and what it does with one typed in.
  //
  // rgszFontAllow[] says which of the 14 fonts can draw each kind of
  // glyph, and Windows filters every combo by it (wdialog.cpp:2956) and
  // filters again when reading the box back (3051). This offered all 14
  // everywhere and took whatever matched -- so a slot could be given a
  // font with no glyphs for it, which then drew wrong and vanished on the
  // next save and reload, since "-YXf" zeroes exactly that.
  //
  // The text slot allows 5 of the 14 ("0---------ABCD"), so a count is
  // the whole assertion for the list half.
  {
    int cItem = -1;

    gs.nFontTxt = 0;
    DriveModalQt(ShowGraphicsSettingsDialogQt, [&cItem](QWidget *pw) {
      QList<QComboBox *> rg = pw->findChildren<QComboBox *>("dcGr_Xf");
      if (!rg.isEmpty())
        cItem = rg[0]->count();
      for (QPushButton *ppb : pw->findChildren<QPushButton *>())
        if (ppb->text() == "Cancel") { ppb->click(); return; }
      pw->close();
    });
    Check(cItem == 5,
      "the chart text font offers the five fonts that can draw it (%d)",
      cItem);
  }

  // Typing, which an editable combo still allows. Three cases, all of
  // them Windows' behaviour:
  //
  //  "Astro"     -> the font of that name, not Astronomicon. FMatchSz()
  //                 takes a prefix of three or more, so "Astrolog",
  //                 "Astro" and "Astronomicon" all match it; Windows
  //                 checks index 2 FIRST for exactly this reason and
  //                 this took the LAST match.
  //  "Wingdings" -> refused in the text slot, which does not allow it,
  //                 and falls back to Astrolog's own font.
  //  nonsense    -> the same fallback, rather than leaving what was
  //                 there, so a typo is visible.
  {
    static CONST struct {
      int iSlot;
      CONST char *szType;
      int nWant;
      CONST char *szWhat;
    } rgt[] = {
      {1, "Astro",      2, "\"Astro\" in the signs slot is the Astro font"},
      {0, "Wingdings",  0, "a font the text slot forbids falls back"},
      {0, "Nonesuch",   0, "and so does a name that matches nothing"}};
    int iT;
    int *rgpn[6] = {&gs.nFontTxt, &gs.nFontSig, &gs.nFontHou,
      &gs.nFontObj, &gs.nFontAsp, &gs.nFontNak};

    for (iT = 0; iT < (int)(sizeof(rgt)/sizeof(*rgt)); iT++) {
      int iSlot = rgt[iT].iSlot;
      CONST char *szType = rgt[iT].szType;

      *rgpn[iSlot] = 11;              // Consolas: allowed everywhere
      DriveModalQt(ShowGraphicsSettingsDialogQt,
        [iSlot, szType](QWidget *pw) {
        QList<QComboBox *> rg = pw->findChildren<QComboBox *>("dcGr_Xf");
        if (iSlot < rg.size())
          rg[iSlot]->setEditText(szType);
        for (QPushButton *ppb : pw->findChildren<QPushButton *>())
          if (ppb->text() == "OK") { ppb->click(); return; }
        pw->close();
      });
      Check(*rgpn[iSlot] == rgt[iT].nWant, "%s (%d, want %d)",
        rgt[iT].szWhat, *rgpn[iSlot], rgt[iT].nWant);
    }
  }

  gs.nFontAll = nFontAllSav; gi.nFontPrev = nFontPrevSav;
  gs.nFontTxt = nTxtSav; gs.nFontSig = nSigSav; gs.nFontHou = nHouSav;
  gs.nFontObj = nObjSav; gs.nFontAsp = nAspSav; gs.nFontNak = nNakSav;
}


static void TestOrbitBufferQt()
{
  int cspaceSav = gs.cspace;

  Group("Orbit trail buffer");

  // Start from nothing allocated: the pointer is reallocated on demand,
  // so dropping whatever is there costs a redraw and no correctness.
  if (gi.rgspace != NULL) {
    DeallocateP(gi.rgspace);
    gi.rgspace = NULL;
  }
  gs.cspace = 4;
  gi.rgspace = RgAllocate(oNorm1*gs.cspace, PT3R, "orbits");
  gi.cspace = gi.ispace = 0;
  Check(gi.rgspace != NULL, "a buffer sized for four steps is allocated");

  DriveSpaceCountQt(4);
  Check(gs.cspace == 4, "OK with the count unchanged leaves it at 4 (%d)",
    gs.cspace);
  Check(gi.rgspace != NULL,
    "and keeps the trail the user is watching");

  DriveSpaceCountQt(16);
  Check(gs.cspace == 16, "the dialog stored the new count (%d)", gs.cspace);
  Check(gi.rgspace == NULL,
    "and dropped the buffer sized for the old one, which the next render "
    "would have written past the end of");

  if (gi.rgspace != NULL) {
    DeallocateP(gi.rgspace);
    gi.rgspace = NULL;
  }
  gs.cspace = cspaceSav;
  gi.cspace = gi.ispace = 0;
}


static void TestNowButtonsQt()
{
  CI ciDefaSav = ciDefa, ciTranSav = ciTran;
  flag fPopupSav = FNoPopupQt();

  Group("Transit and progression Now");
  SetNoPopupQt(fTrue);

  // A default chart info whose zone and daylight are not the transit
  // chart's, so "took ciDefa's" and "left the old value" are different
  // answers.
  ciDefa.dst = 1.0; ciDefa.zon = 5.0;
  ciTran.dst = 0.0; ciTran.zon = 8.0;

  TestNowButtonQt(ShowTransitDialogQt, "dcTrDst", "dcTrZon", "transit");
  TestNowButtonQt(ShowProgressDialogQt, "dcPrDst", "dcPrZon", "progression");

  // The Chart Info dialog's "Now", which has the opposite problem: it
  // wrote two fields it should have left alone. Windows' dbInNow assigns
  // date, time, zone, daylight and coordinates into its working copy and
  // NOTHING else (wdialog.cpp:1206), so the name and location survive the
  // button. This took them from ciDefa, which for most users is empty --
  // so typing a name and pressing "Now" erased it.
  //
  // The pointers are swapped rather than cloned over: FCloneSz() frees
  // what is there, and ciCore's strings are shared with ciMain. The
  // dialog is cancelled, so nothing writes them back.
  {
    static char szNamT[] = "ProbeNowName";
    char *pszNamSav = ciCore.nam;
    QString strNam;

    ciCore.nam = szNamT;
    DriveModalQt(ShowChartInfoDialogQt, [&](QWidget *pw) {
      QLineEdit *peNam = pw->findChild<QLineEdit *>("deInNam");
      QPushButton *ppbNow = NULL;

      for (QPushButton *ppb : pw->findChildren<QPushButton *>())
        if (ppb->text().contains("Now"))
          ppbNow = ppb;
      if (peNam != NULL && ppbNow != NULL) {
        ppbNow->click();
        strNam = peNam->text();
      }
      for (QPushButton *ppb : pw->findChildren<QPushButton *>())
        if (ppb->text() == "Cancel")
          ppb->click();
    });
    ciCore.nam = pszNamSav;
    Check(strNam == QString(szNamT),
      "chart info: \"Now\" keeps the chart's name (\"%s\")",
      strNam.toLocal8Bit().constData());
  }

  ciDefa = ciDefaSav; ciTran = ciTranSav;
  SetNoPopupQt(fPopupSav);
}


static void TestChartNowQt()
{
  CI ciMainSav = ciMain, ciTwinSav = ciTwin, ciCoreSav = ciCore;
  int nRelSav = us.nRel, nModeSav = gi.nMode;
  QAction *pa = PaFindActionTestQt("Chart for &Now");

  Group("Chart for Now");
  Check(pa != NULL, "\"Chart for Now\" is on the Info menu");
  if (pa == NULL)
    return;

  // A wheel, not a map: Animate() returns after rotating and casts
  // nothing at all when the chart on screen is an astro-graph or a globe
  // with map animation on, so this would ask its question of a code path
  // that never runs.
  SetChartModeQt(gWheel);

  SetRelQt(rcNone);
  ciMain.yea = 1899; ciMain.mon = 3; ciMain.day = 4;
  ciCore = ciMain;
  pa->trigger();
  Check(ciMain.yea != 1899,
    "on a single chart it sets the main chart to now (yea %d)", ciMain.yea);

  SetRelQt(rcTransit);
  ciMain.yea = 1899; ciMain.mon = 3; ciMain.day = 4;
  ciTwin.yea = 1898; ciTwin.mon = 5; ciTwin.day = 6;
  ciCore = ciMain;
  pa->trigger();
  Check(ciMain.yea == 1899,
    "on a transit chart the natal chart is left where it was (yea %d)",
    ciMain.yea);
  Check(ciTwin.yea != 1898,
    "and the transiting chart is the one moved to now (yea %d)",
    ciTwin.yea);

  SetRelQt(nRelSav);
  SetChartModeQt(nModeSav);
  ciMain = ciMainSav; ciTwin = ciTwinSav; ciCore = ciCoreSav;
}


static void TestAtlasApplyQt()
{
  Group("Atlas Apply Info");
#ifndef ATLAS
  printf("  built without ATLAS\n");
#else
  CI ciSav = ciCore, ciMainSav = ciMain;
  flag fPopupSav = FNoPopupQt();
  QString strJul, strJan;

  // Apply Info warns through PrintWarning() when the Location field
  // holds no city it can find -- which, in this build, is a MODAL box
  // raised from inside a modal dialog. It should not fire here, and if
  // the atlas ever goes missing it must not stop the run either.
  SetNoPopupQt(fTrue);
  strJul = StrApplyInfoQt(7, 1, 1990);
  strJan = StrApplyInfoQt(1, 1, 1990);
  SetNoPopupQt(fPopupSav);
  ciCore = ciSav; ciMain = ciMainSav;

  Check(strJul == "Yes",
    "Apply Info sets Daylight Saving for Seattle in July 1990 (\"%s\")",
    strJul.toLocal8Bit().constData());
  Check(strJan == "No",
    "and clears it for the same city in January (\"%s\")",
    strJan.toLocal8Bit().constData());
#endif
}


static void TestAnimationStateQt()
{
  int nAnimSav = gs.nAnim, nDirSav = gi.nDir;
  flag fPauseSav = gi.fPause;
  CI ciSav = ciCore;

  Group("Animation state");

  // NOT asserted here, and the reason is worth carrying: that a frame
  // casts ONCE rather than twice. "-~q1" fires from inside CastChart(),
  // so counting it counts casts -- but not one per call. A sector chart
  // casts once per division and fired it 228 times for a single cast, a
  // relationship chart casts one per ring, and several chart types cast
  // again while DRAWING. Pinning all three still left the count reading
  // the same with the bug present and without it, so the check bit when
  // the group ran alone and was blind inside the suite. A net that only
  // works sometimes is worse than none; the fix is evidenced by the
  // measurement in qtdriver.cpp's own comment (2 casts a frame and 1 for
  // a turning map, against Windows' 1 and 0) and by AnimTickQt() now
  // being what wdriver.cpp:1031 is.

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

  // A tick must not run inside another tick: a cast can enter a nested
  // event loop where this timer keeps firing, and an unguarded second
  // tick nests another cast inside the first without bound.
  //
  // Testable without a network or a nested loop: while a tick is in
  // progress another does nothing. Both halves are asserted, since "does
  // nothing" passes just as well on a tick that never worked.
  // Pin what a frame depends on rather than inheriting it: a direction
  // of zero advances nothing, and a relationship chart sends
  // RecastAndRedrawQt() down CastRelation(), which rewrites ciMain from
  // the two charts being compared and puts back the very date the tick
  // just moved. Both were inherited from TestAllMenuActionsQt() in the
  // full run, where this assertion failed while passing on its own.
  int nRelSav2 = us.nRel;
  us.nRel = rcNone;
  gi.nDir = 1;
  gs.nAnim = iAnimDay; gi.fPause = fFalse;   // running, a day a frame
  ciCore.mon = 6; ciCore.day = 15; ciCore.yea = 1990; ciCore.tim = 12.0;
  ciMain = ciCore;
  CI ciBefore = ciMain;
  AnimTickTestQt();
  Check(!FEqCI(ciMain, ciBefore), "a tick advances the chart (%d/%d/%d)",
    ciMain.mon, ciMain.day, ciMain.yea);
  Check(!FAnimTickBusyTestQt(), "and clears its own guard on the way out");

  ciBefore = ciMain;
  SetAnimTickBusyTestQt(fTrue);          // as if a tick were in progress
  AnimTickTestQt();
  SetAnimTickBusyTestQt(fFalse);
  Check(FEqCI(ciMain, ciBefore),
    "a tick fired inside another one does nothing (%d/%d/%d)",
    ciMain.mon, ciMain.day, ciMain.yea);

  us.nRel = nRelSav2;
  gs.nAnim = nAnimSav; gi.nDir = nDirSav; gi.fPause = fPauseSav;
  // ciMain as well as ciCore: the ticks above moved both, and a later
  // group that inherits a chart three days from where it thinks it is
  // fails on the leftovers rather than on its own subject.
  ciCore = ciMain = ciSav;
  CastChart(1);
  printf("  one switch starts and stops it; nothing else moves the chart\n");
}

// Windows dialogs act on a mnemonic letter pressed on its own -- "s"
// ticks "&Sun" -- while Qt wants Alt held. Both builds read the same "&"
// out of astrolog.rc, so only the routing differs, and on the restriction
// grid of 52 checkboxes it decides whether the dialog can be used from
// the keyboard at all.
/*
******************************************************************************
** Menu check marks after a setting changes behind the menu's back.
******************************************************************************
*/

// Windows redetermines every menu check mark (RedoMenu(), driven by
// wi.fMenuAll) after the four routes that can move a setting without
// going through the menu item that owns it: the Enter Command Line
// dialog, running a macro, Graphics Settings, and Redraw Screen.
// RedoMenuQt() is this port's equivalent. Before it existed, typing
// "-Xr" into the command line dialog inverted the chart and left
// "Reverse Background" unchecked for the rest of the session.
//
// Each case flips the setting directly, the way FProcessCommandLine
// does, then requires the check mark to be stale until RedoMenuQt() runs
// and correct immediately after. The stale leg is what makes the other
// one mean anything: without it a case whose predicate reads the wrong
// field still passes whenever the two happen to agree.
//
// The six between them cover every way an item registers -- a plain flag
// (AddToggleAction), a value out of a set (AddSelectAction), a derived
// condition, an inverted one, and two one-offs.

static void TestMenuResyncQt()
{
  flag fSidSav = us.fSidereal, fColorSav = gs.fColor;
  int nScaleSav = gs.nScale, nAppSav = us.nAppSep;
  int objCenSav = us.objCenter, nDirSav = gi.nDir;

  Group("Menu resync");

  QAction *paSid = PaFindActionTestQt("&Sidereal Zodiac");
  QAction *paMed = PaFindActionTestQt("&Medium");
  QAction *paApp = PaFindActionTestQt("&Applying Aspects");
  QAction *paHel = PaFindActionTestQt("He&liocentric");
  QAction *paMon = PaFindActionTestQt("&Monochrome");
  QAction *paRev = PaFindActionTestQt("&Reverse Direction");
  Check(paSid != NULL && paMed != NULL && paApp != NULL && paHel != NULL &&
    paMon != NULL && paRev != NULL, "all six items are on the menu bar");
  if (paSid == NULL || paMed == NULL || paApp == NULL || paHel == NULL ||
    paMon == NULL || paRev == NULL)
    return;

  // A plain flag, registered by AddToggleAction().
  us.fSidereal = fFalse;
  RedoMenuQt();
  Check(!paSid->isChecked(), "Sidereal Zodiac starts unchecked");
  us.fSidereal = fTrue;
  Check(!paSid->isChecked(), "and nothing else puts it right");
  RedoMenuQt();
  Check(paSid->isChecked(), "RedoMenuQt() does");

  // One value out of a set, registered by AddSelectAction(). A scale
  // typed into Graphics Settings that matches none of the four presets
  // has to leave all four unchecked, which is what Windows shows.
  gs.nScale = 200;
  RedoMenuQt();
  Check(paMed->isChecked(), "Character Scale / Medium follows gs.nScale");
  gs.nScale = 250;
  RedoMenuQt();
  Check(!paMed->isChecked(),
    "a scale matching no preset leaves the preset unchecked");

  // Derived, and not simply "non-zero": Windows checks nAppSep == 1
  // everywhere, so Waxing/Waning (2) shows unchecked.
  us.nAppSep = 1;
  RedoMenuQt();
  Check(paApp->isChecked(), "Applying Aspects follows nAppSep == 1");
  us.nAppSep = 2;
  Check(paApp->isChecked(), "still stale at nAppSep == 2");
  RedoMenuQt();
  Check(!paApp->isChecked(),
    "and Waxing/Waning reads as unchecked, as on Windows");

  // Derived from a value that isn't a flag at all.
  us.objCenter = oEar;
  RedoMenuQt();
  Check(!paHel->isChecked(), "Heliocentric is off with Earth at the center");
  us.objCenter = oSun;
  Check(!paHel->isChecked(), "nothing else notices the center moved");
  RedoMenuQt();
  Check(paHel->isChecked(), "RedoMenuQt() does");

  // Inverted: the item is on when the flag is off. A predicate that
  // dropped the negation would pass every case above and fail here.
  gs.fColor = fTrue;
  RedoMenuQt();
  Check(!paMon->isChecked(), "Monochrome is off while color is on");
  gs.fColor = fFalse;
  RedoMenuQt();
  Check(paMon->isChecked(), "and on when color goes off");

  // A sign test rather than an equality.
  gi.nDir = 1;
  RedoMenuQt();
  Check(!paRev->isChecked(), "Reverse Direction is off going forward");
  gi.nDir = -3;
  Check(!paRev->isChecked(), "still stale after the direction flips");
  RedoMenuQt();
  Check(paRev->isChecked(), "and on once it resyncs, at any factor");

  // The "Include <category>" items are the one place where resyncing the
  // menu could change what gets computed. SyncRestrictMenuQt() re-derives
  // each flag from ignore[] and writes it back, which is right after a
  // restriction dialog; RedoMenuQt() must only read it, because Windows'
  // RedoMenu() only reads it (CheckMenu with us.fUranian). Otherwise a
  // command line that restricted the Uranians by hand would find the
  // us.fUranian it also set switched back off by the menus catching up,
  // and that flag reaches matrix.cpp.
  QAction *paUra = PaFindActionTestQt("Include &Uranians");
  Check(paUra != NULL, "Include Uranians is on the menu bar");
  if (paUra != NULL) {
    flag fUraSav = us.fUranian;
    flag rgfIgnoreSav[uranHi - uranLo + 1];
    int i;

    for (i = uranLo; i <= uranHi; i++) {
      rgfIgnoreSav[i - uranLo] = ignore[i];
      ignore[i] = fTrue;
    }
    us.fUranian = fTrue;
    RedoMenuQt();
    Check(us.fUranian, "RedoMenuQt() leaves a category flag alone");
    Check(paUra->isChecked(), "and shows the flag, not the restrictions");
    SyncRestrictMenuQt();
    Check(!us.fUranian,
      "where SyncRestrictMenuQt() does re-derive it, and clears it");
    for (i = uranLo; i <= uranHi; i++)
      ignore[i] = rgfIgnoreSav[i - uranLo];
    us.fUranian = fUraSav;
  }

  us.fSidereal = fSidSav; gs.fColor = fColorSav; gs.nScale = nScaleSav;
  us.nAppSep = nAppSav; us.objCenter = objCenSav; gi.nDir = nDirSav;
  RedoMenuQt();
}


/*
******************************************************************************
** Pressing OK twice is the same as pressing it once.
******************************************************************************
*/

// Pressing OK twice must equal pressing it once. The FIRST OK
// legitimately moves settings -- a dialog normalises what it shows, so
// with seconds off 5:47:55pm displays as "5:47pm" and parses back as
// 5:47:00, exactly as Windows does from the same SzTim()/RParseSz() pair.
// The second must not: a value losing precision each round trip, a list
// growing an entry, a flag that toggles where it should set.
//
// Compared as a SAVED SETTINGS FILE rather than a memcmp of us and gs,
// which carry char * fields FCloneSz() reallocates on every OK -- their
// bytes differ while the settings do not. That covers ignore[] and
// rgobjset[] for free; chart info is not in the file, so ciMain's eight
// numeric fields are compared beside it.
//
// Settings are put back by saving and reloading rather than by a struct
// copy: a copy taken before an OK holds pointers the OK has since freed.

static flag FSaveSettingsToQt(CONST char *szPath)
{
  char *szSav = is.szFileOut;
  flag f;

  is.szFileOut = (char *)szPath;
  f = FOutputSettings();
  is.szFileOut = szSav;
  return f;
}

static QByteArray BaReadFileQt(CONST QString &strPath)
{
  QFile file(strPath);

  if (!file.open(QIODevice::ReadOnly))
    return QByteArray();
  return file.readAll();
}

static void ClickOkInModalQt(void (*pfnOpen)())
{
  DriveModalQt(pfnOpen, [](QWidget *pw) {
    for (QPushButton *ppb : pw->findChildren<QPushButton *>())
      if (ppb->text() == "OK") {
        ppb->click();
        return;
      }
    pw->close();
  });
}

// Run "fnApply" twice, saving the settings after each, and say whether
// the second run left them where the first did. On a difference,
// *pstrWhere gets the first settings line that moved, so the field is
// findable without re-running anything by hand. Returns -1 if a save
// failed, which is a different answer from "it drifted".

static int NSettlesQt(CONST QString &strDir, std::function<void()> fnApply,
  QString *pstrWhere)
{
  QString strA = strDir + "/a.as", strB = strDir + "/b.as";
  QByteArray baA = strA.toLocal8Bit(), baB = strB.toLocal8Bit();

  fnApply();
  if (!FSaveSettingsToQt(baA.constData()))
    return -1;
  fnApply();
  if (!FSaveSettingsToQt(baB.constData()))
    return -1;

  QByteArray ba1 = BaReadFileQt(strA), ba2 = BaReadFileQt(strB);
  QFile::remove(strA);
  QFile::remove(strB);
  if (ba1.isEmpty() || ba2.isEmpty())
    return -1;
  if (ba1 == ba2)
    return fTrue;
  QList<QByteArray> rg1 = ba1.split('\n'), rg2 = ba2.split('\n');
  for (int j = 0; j < rg1.size() && j < rg2.size(); j++)
    if (rg1[j] != rg2[j]) {
      *pstrWhere = QString::fromLatin1(rg1[j].trimmed()) + " -> " +
        QString::fromLatin1(rg2[j].trimmed());
      break;
    }
  return fFalse;
}

static void TestOkSettlesQt()
{
  QString strDir = QDir::tempPath() + QString("/astrolog-qt-ok-%1")
    .arg((int)QCoreApplication::applicationPid());
  int nWriteFormatSav = us.nWriteFormat, cDrift = 0, i, n;
  flag fNoWriteSav = us.fNoWrite;
  CI ciSav = ciMain, ciCoreSav = ciCore;
  QString strWhere;

  Group("OK settles");
  QDir().mkpath(strDir);
  us.fNoWrite = fFalse;
  us.nWriteFormat = 'd';

  // Everything this group is about to normalise, written down first.
  QString strWas = strDir + "/was.as";
  QByteArray baWas = strWas.toLocal8Bit();
  Check(FSaveSettingsToQt(baWas.constData()),
    "the settings this group is about to disturb are saved first");

  // The detector first, against something that definitely drifts. Every
  // assertion below is "no difference", and a comparison that cannot
  // find one passes all 25 while proving nothing -- so make it find one.
  int nScaleSav = gs.nScale;
  n = NSettlesQt(strDir, []() {
    gs.nScale = gs.nScale >= 400 ? 100 : gs.nScale + 100;
  }, &strWhere);
  Check(n == fFalse, "a setting that moves on every pass is caught (%d)", n);
  // ":Xs", not "-Xs": FOutputSettings() writes the values-only prefix.
  Check(strWhere.contains("Xs "),
    "and the line that moved is named (\"%s\")",
    strWhere.toLocal8Bit().constData());
  gs.nScale = nScaleSav;

  for (i = 0; i < cdlgQt; i++) {
    void (*pfn)() = rgdlgQt[i].pfn;
    CI ci1;

    ClickOkInModalQt(pfn);                   // the normalising pass
    ci1 = ciMain;
    strWhere.clear();
    n = NSettlesQt(strDir, [pfn]() { ClickOkInModalQt(pfn); }, &strWhere);
    if (n < 0) {
      Check(fFalse, "%s: could not save the settings to compare",
        rgdlgQt[i].szTitle);
      continue;
    }
    if (n == fFalse)
      cDrift++;
    Check(n != fFalse, "%s: a second OK moved the settings again (%s)",
      rgdlgQt[i].szTitle, strWhere.toLocal8Bit().constData());
    Check(ciMain.mon == ci1.mon && ciMain.day == ci1.day &&
      ciMain.yea == ci1.yea && ciMain.tim == ci1.tim &&
      ciMain.dst == ci1.dst && ciMain.zon == ci1.zon &&
      ciMain.lon == ci1.lon && ciMain.lat == ci1.lat,
      "%s: a second OK left the chart info alone", rgdlgQt[i].szTitle);
  }

  if (!FProcessSwitchFile(baWas.constData(), NULL)) {
    Check(fFalse, "and put back through the program's own parser (kept: %s)",
      baWas.constData());
  } else
    QFile::remove(strWas);
  QDir().rmdir(strDir);
  ciMain = ciSav; ciCore = ciCoreSav;
  us.nWriteFormat = nWriteFormatSav;
  us.fNoWrite = fNoWriteSav;
  AdjustRestrictions();
  printf("  %d dialogs settle on the first OK, %d still drifting\n",
    cdlgQt - cDrift, cDrift);
}


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
// Force one custom-object slot to its COMPILED default: no user
// definition, and both glyph pointers back at the shared constant rather
// than a clone. Three groups need that state and none of them can assume
// it.
//
// They used to assume it, and went about it by picking a row the
// maintainer had not customised yet -- "row 1 still holds its own body",
// "Hades: pristine under nrvate.as". That is a race with a file the suite
// is REQUIRED to load: run-qt-tests.sh defaults to "-i nrvate.as", which
// CLAUDE.md's hard rule says to test with. The race was lost when that
// file grew "-Yeb 35 10199", which is row 1, and the documented
// pre-commit command failed three assertions that "make check" (which
// passes "-Yi1 ephem") could not see. Establish the state instead of
// hoping for it.
//
// rgTypSwissDef[]/rgObjSwissDef[] are what the writer already compares
// against to decide which slots to save, so they are the right meaning of
// "default" here too.

static void ResetCustomSlotQt(int iobj)
{
  int i = iobj - custLo;

  if (szDrawObject[iobj] != szDrawObjectDef[iobj]) {
    DeallocateP((char *)szDrawObject[iobj]);
    szDrawObject[iobj] = szDrawObjectDef[iobj];
  }
  if (szDrawObject2[iobj] != szDrawObjectDef2[iobj]) {
    DeallocateP((char *)szDrawObject2[iobj]);
    szDrawObject2[iobj] = szDrawObjectDef2[iobj];
  }
  rgTypSwiss[i] = rgTypSwissDef[i];
  rgObjSwiss[i] = rgObjSwissDef[i];
  rgPntSwiss[i] = rgFlgSwiss[i] = 0;
}


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

  ResetCustomSlotQt(iobj);
  Check(szDrawObject[iobj] == szDrawObjectDef[iobj] &&
    rgObjSwiss[iobj - custLo] != 10199,
    "the slot holds its own body's glyph, and not the body about to be "
    "put in it");

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


// Replay the switch lines a test cares about out of a saved settings
// file, which is what "did this setting survive being saved?" means in
// practice. Four tests wanted this loop and had four copies of it.
//
// It returns the number of lines replayed, and that is not a convenience.
// The failure mode of a hand-written filter is matching NOTHING: the
// replay then restores nothing, the setting stays at whatever the test
// clobbered it to, and the assertion reports that the PROGRAM lost the
// setting. That happened here -- FEqSzPrefixQt() above requires the
// switch to END at the prefix, and the object writer emits "-Yeb 34
// 2060", so a "-Ye" filter never fired. Callers assert the count.

static int CReplaySettingsQt(CONST char *szPath,
  flag (*pfnWant)(CONST char *))
{
  char szLine[cchSzMax];
  FILE *file;
  int i, cLine = 0;

  file = FileOpen(szPath, 3, NULL, 0);
  if (file == NULL)
    return -1;
  while (fgets(szLine, cchSzMax, file) != NULL) {
    if (!pfnWant(szLine))
      continue;
    for (i = 0; szLine[i]; i++)          // Keep the line, minus its \n.
      ;
    while (i > 0 && szLine[i-1] < ' ')
      szLine[--i] = chNull;
    FProcessCommandLine(szLine);
    cLine++;
  }
  fclose(file);
  return cLine;
}


// The four filters, one per caller. Only the switches under test, so a
// replay cannot disturb the several hundred other settings the file
// carries.

static flag FWantObjSetQt(CONST char *sz)
{
  return FEqSzPrefixQt(sz, "-YR") || FEqSzPrefixQt(sz, "-YRT") ||
    FEqSzPrefixQt(sz, "-YAm") || FEqSzPrefixQt(sz, "-YAd") ||
    FEqSzPrefixQt(sz, "-Yj") || FEqSzPrefixQt(sz, "-YjT") ||
    FEqSzPrefixQt(sz, "-YkO");
}

static flag FWantInterfaceQt(CONST char *sz)
{
  return FEqSzPrefixQt(sz, "-WF") || FEqSzPrefixQt(sz, "-WG") ||
    FEqSzPrefixQt(sz, "-WI") || FEqSzPrefixQt(sz, "_WFa") ||
    FEqSzPrefixQt(sz, "=WFa") || FEqSzPrefixQt(sz, "_WGa") ||
    FEqSzPrefixQt(sz, "=WGa");
}

// The three File Settings flags -Wn/-Wt/-Wb, which the switches ignored
// and the writer skipped.
static flag FWantWinFlagQt(CONST char *sz)
{
  return FEqSzPrefixQt(sz, "_Wn") || FEqSzPrefixQt(sz, "=Wn") ||
    FEqSzPrefixQt(sz, "_Wt") || FEqSzPrefixQt(sz, "=Wt") ||
    FEqSzPrefixQt(sz, "_Wb") || FEqSzPrefixQt(sz, "=Wb");
}

static flag FWantInterfaceValueQt(CONST char *sz)
{
  return FEqSzPrefixQt(sz, "-WF") || FEqSzPrefixQt(sz, "-WG") ||
    FEqSzPrefixQt(sz, "-WI");
}

// Not FEqSzPrefixQt(): the object writer puts the body type in the switch
// itself -- "-Yeb 34 2060" -- so there is nothing for that helper's
// end-of-switch check to land on.
static flag FWantObjDefQt(CONST char *sz)
{
  return sz[0] == '-' && sz[1] == 'Y' && (sz[2] == 'e' || sz[2] == 'D');
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
  char szPath[cchSzMax];

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

  i = CReplaySettingsQt(szPath, FWantObjSetQt);
  Check(i > 0, "and it can be read back (%d lines replayed)", i);

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


// The interface settings -- the theme and the two fonts -- through the
// same path: out to a settings file and back in through the switch
// parser. This is the whole claim of moving them out of a QSettings file
// of their own, so it is asserted rather than assumed.
//
// The face names deliberately contain spaces. That is the part that can
// actually break: the writer quotes them and the parser has to put them
// back together, and a name arriving as just "Bitstream" would still look
// like a font to every other check here.

static void TestInterfaceSettingsQt()
{
  char szPath[cchSzMax], *szFileOutSav;
  QString strConSav, strMenSav, strThemeSav;
  int nConSav, nMenSav, nWriteFormatSav;
  flag fConSav, fMenSav, fNoWriteSav;
  int i;

  Group("Interface settings in the settings file");

  strConSav = StrConsoleFontQt(); nConSav = NConsoleFontSizeQt();
  strMenSav = StrMenuFontQt();    nMenSav = NMenuFontSizeQt();
  fConSav = FConsoleAntialiasQt(); fMenSav = FMenuAntialiasQt();
  strThemeSav = StrThemePrefQt();
  szFileOutSav = is.szFileOut;
  nWriteFormatSav = us.nWriteFormat;
  fNoWriteSav = us.fNoWrite;

  SetConsoleFontQt("Bitstream Vera Sans Mono", 18);
  SetConsoleAntialiasQt(fFalse);
  SetMenuFontQt("Bitstream Vera Serif", 14);
  SetMenuAntialiasQt(fTrue);
  SetThemePrefQt("dark");

  sprintf2(S(szPath), "%s/astrolog-qt-interface-%d.as",
    QDir::tempPath().toLocal8Bit().constData(),
    (int)QCoreApplication::applicationPid());
  us.fNoWrite = fFalse;
  us.nWriteFormat = 'd';
  is.szFileOut = szPath;
  Check(FOutputSettings(), "Save Program Settings wrote the file");

  // Clobber every one of them, so anything the file failed to carry stays
  // wrong rather than merely looking right.
  SetConsoleFontQt("Courier", 9);
  SetConsoleAntialiasQt(fTrue);
  SetMenuFontQt("Courier", 9);
  SetMenuAntialiasQt(fFalse);
  SetThemePrefQt("light");

  i = CReplaySettingsQt(szPath, FWantInterfaceQt);
  Check(i == 5, "and all five lines read back (%d)", i);

  Check(StrConsoleFontQt() == QString("Bitstream Vera Sans Mono"),
    "a chart font whose name has spaces survives whole (\"%s\")",
    SzConsoleFontQt());
  Check(NConsoleFontSizeQt() == 18,
    "and its size (%d, want 18)", NConsoleFontSizeQt());
  Check(!FConsoleAntialiasQt(), "chart font smoothing off survives");
  Check(StrMenuFontQt() == QString("Bitstream Vera Serif"),
    "the interface font survives whole (\"%s\")", SzMenuFontQt());
  Check(NMenuFontSizeQt() == 14,
    "and its size (%d, want 14)", NMenuFontSizeQt());
  Check(FMenuAntialiasQt(), "interface font smoothing on survives");
  Check(StrThemePrefQt() == QString("dark"),
    "and the chosen theme (\"%s\")",
    StrThemePrefQt().toUtf8().constData());

  // An empty face and size 0 are what "no preference" looks like, and
  // they have to survive the same trip: an empty name is where a writer
  // that forgot its quotes turns one switch into the next one's argument.
  SetConsoleFontQt("", 0);
  SetMenuFontQt("", 0);
  SetThemePrefQt("auto");
  Check(FOutputSettings(), "no preference writes a file too");
  SetConsoleFontQt("Courier", 9);
  SetMenuFontQt("Courier", 9);
  SetThemePrefQt("dark");
  i = CReplaySettingsQt(szPath, FWantInterfaceValueQt);
  Check(i == 3, "the three value lines read back (%d)", i);
  Check(StrConsoleFontQt().isEmpty() && NConsoleFontSizeQt() == 0,
    "an empty chart font comes back empty (\"%s\" %d)",
    SzConsoleFontQt(), NConsoleFontSizeQt());
  Check(StrMenuFontQt().isEmpty() && NMenuFontSizeQt() == 0,
    "and so does an empty interface font (\"%s\" %d)",
    SzMenuFontQt(), NMenuFontSizeQt());
  Check(StrThemePrefQt() == QString("auto"), "and auto comes back auto");

  // The three File Settings flags. Editable in the GUI since the port
  // began and impossible to keep: NProcessSwitchesQt() accepted -Wn, -Wt
  // and -Wb as no-ops, so FOutputSettings() rightly refused to write a
  // round trip that did not exist. Both halves work now, so this asserts
  // the whole loop rather than either end of it.
  {
    flag fUpdSav = FNoUpdateQt(), fPopSav = FNoPopupQt(),
      fBmpSav = FBmpWindowQt();
    int cLine;

    SetNoUpdateQt(fTrue); SetNoPopupQt(fTrue); SetBmpWindowQt(fFalse);
    Check(FOutputSettings(), "the window flags write to a settings file");
    SetNoUpdateQt(fFalse); SetNoPopupQt(fFalse); SetBmpWindowQt(fTrue);
    cLine = CReplaySettingsQt(szPath, FWantWinFlagQt);
    Check(cLine == 3, "all three come back (%d lines)", cLine);
    Check(FNoUpdateQt(), "\"don't update\" survives");
    Check(FNoPopupQt(), "\"no popups\" survives");
    Check(!FBmpWindowQt(), "and a flag saved OFF comes back off");
    SetNoUpdateQt(fUpdSav); SetNoPopupQt(fPopSav); SetBmpWindowQt(fBmpSav);
  }

  // The rest of the Qt side's own state: the animation delay, the
  // antialiasing level, and the names the Macro menu and its submenus have
  // been given. Those three live in qi rather than in us or gs, so neither
  // settings sweep can see them, and the macro names are the only user
  // text the port stores of its own.
  {
    int nDelaySav = NAnimDelayQt(), nAaSav = NAntialiasQt();
    QByteArray baMacSav(SzSet(SzMacroNameQt(0)));
    QByteArray baSubSav(SzSet(SzMacroSubNameQt(0)));
    char szLine[cchSzLine];

    SetAnimDelayQt(137);
    SetAntialiasQt(9);
    FProcessCommandLine((char *)"-WM 1 \"ProbeMacroName\"");
    FProcessCommandLine((char *)"-WM0 0 \"ProbeSubName\"");
    Check(FOutputSettings(), "the Qt interface state writes to a file");

    SetAnimDelayQt(1);
    SetAntialiasQt(1);
    FProcessCommandLine((char *)"-WM 1 \"\"");
    FProcessCommandLine((char *)"-WM0 0 \"\"");
    Check(FProcessSwitchFile(szPath, NULL), "and the file loads back");
    Check(NAnimDelayQt() == 137, "the animation delay survives (%d)",
      NAnimDelayQt());
    Check(NAntialiasQt() == 9, "the antialiasing level survives (%d)",
      NAntialiasQt());
    Check(FEqSz(SzSet(SzMacroNameQt(0)), "ProbeMacroName"),
      "a renamed macro survives (\"%s\")", SzSet(SzMacroNameQt(0)));
    Check(FEqSz(SzSet(SzMacroSubNameQt(0)), "ProbeSubName"),
      "and a renamed macro submenu (\"%s\")", SzSet(SzMacroSubNameQt(0)));

    // And the antialias level's RANGE, which only the switch could get
    // wrong: this build's own File Settings dialog has checked it since
    // it was written, and the switch accepted anything. Nothing here
    // reads the number -- Qt's antialiasing is a render hint, not a
    // supersample factor -- so the harm is in what gets written down: a
    // value the dialog can never produce, saved into astrolog.as and
    // shown back in that dialog, and refused by the Win32 build's own
    // handler (wdriver.cpp:168).
    SetAntialiasQt(6);
    SetNoPopupQt(fTrue);            // the refusal raises an error box
    Check(!FProcessCommandLine((char *)"-Wx 0"),
      "\"-Wx 0\" is refused, not stored");
    Check(NAntialiasQt() == 6, "and the level is left alone (%d)",
      NAntialiasQt());
    Check(!FProcessCommandLine((char *)"-Wx 13"),
      "\"-Wx 13\" is refused too, the range being 1 to 12");
    Check(NAntialiasQt() == 6, "and that leaves it alone as well (%d)",
      NAntialiasQt());
    Check(FProcessCommandLine((char *)"-Wx 12"),
      "while the top of the range is accepted");
    Check(NAntialiasQt() == 12, "and stored (%d)", NAntialiasQt());

    SetAnimDelayQt(nDelaySav);
    SetAntialiasQt(nAaSav);
    sprintf2(S(szLine), "-WM 1 \"%s\"", baMacSav.constData());
    FProcessCommandLine(szLine);
    sprintf2(S(szLine), "-WM0 0 \"%s\"", baSubSav.constData());
    FProcessCommandLine(szLine);
  }

  // ARITY, ON ONE COMMAND LINE. Everything above replays a settings file,
  // and a settings file cannot see an arity bug: each line is parsed on
  // its own, so a switch that consumes one argument too many just runs
  // off the end of its own line and the next line starts clean. Sharing
  // one argv is the case that bites -- a miscount there eats the switch
  // that FOLLOWS. Found the hard way: the Win32 build was checked with a
  // settings file ending in "-c Camp" and that proved less than claimed.
  //
  // -WI is last and is the sentinel: it only takes effect if every switch
  // before it consumed exactly its own arguments.
  // EVERY switch on the line is checked, not just the last one. The first
  // draft asserted only the trailing -WI and passed with the arity
  // deliberately broken: consuming one argument too many ate the "_WFa"
  // between the two fonts, and -WI -- two tokens further on -- was still
  // reached. The flag immediately after a value switch is what a miscount
  // actually destroys, so that is what has to be asserted.
  SetThemePrefQt("auto");
  SetConsoleFontQt("Courier", 9);
  SetMenuFontQt("Courier", 9);
  SetConsoleAntialiasQt(fTrue);
  SetMenuAntialiasQt(fFalse);
  FProcessCommandLine((char *)
    "-WF Monospace 18 _WFa -WG Monospace 14 =WGa -WI 2");
  Check(StrConsoleFontQt() == QString("Monospace") &&
    NConsoleFontSizeQt() == 18,
    "on one command line the chart font takes (\"%s\" %d)",
    SzConsoleFontQt(), NConsoleFontSizeQt());
  Check(!FConsoleAntialiasQt(),
    "and the flag right after it is not eaten by it");
  Check(StrMenuFontQt() == QString("Monospace") && NMenuFontSizeQt() == 14,
    "and the interface font after that (\"%s\" %d)",
    SzMenuFontQt(), NMenuFontSizeQt());
  Check(FMenuAntialiasQt(), "and the flag after THAT one either");
  Check(StrThemePrefQt() == QString("dark"),
    "and the switch that ends the line is still reached (\"%s\")",
    StrThemePrefQt().toUtf8().constData());

  QFile::remove(QString::fromLocal8Bit(szPath));   // Not unlink(): no <unistd.h> on the Windows build.

  SetConsoleFontQt(strConSav.toUtf8().constData(), nConSav);
  SetConsoleAntialiasQt(fConSav);
  SetMenuFontQt(strMenSav.toUtf8().constData(), nMenSav);
  SetMenuAntialiasQt(fMenSav);
  SetThemePrefQt(strThemeSav.toUtf8().constData());
  is.szFileOut = szFileOutSav;
  us.nWriteFormat = nWriteFormatSav;
  us.fNoWrite = fNoWriteSav;
  printf("  the interface settings live in astrolog.as with everything else\n");
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
  // Row 1 too, for the glyph, and normalized rather than assumed: which
  // rows carry their own glyph depends on the settings file the suite
  // loaded, and this used to pick a row on that basis and lose the race.
  // See ResetCustomSlotQt().
  int iobj1 = custLo + 1;
  int nTyp1Sav = rgTypSwiss[1], nObj1Sav = rgObjSwiss[1];
  int nPnt1Sav = rgPntSwiss[1], nFlg1Sav = rgFlgSwiss[1];

  Group("Custom objects parse");
  ResetCustomSlotQt(custLo);
  ResetCustomSlotQt(iobj1);
  Check(szDrawObject[iobj1] == szDrawObjectDef[iobj1] &&
    rgObjSwiss[1] != 52872,
    "row 1's slot holds its own body's glyph, and not the body about to "
    "be put in it");

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
  int iobj = custLo + 2;
  OBJDEF od, odSav;

  Group("Object definition store");

  // Normalized, not chosen for being untouched: this row used to be
  // picked as "Hades: pristine under nrvate.as", which is a race with the
  // maintainer's own settings. See ResetCustomSlotQt().
  ResetCustomSlotQt(iobj);
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

  // What the dialog chose has to reach the settings file, or the user
  // picks their bodies again every launch. The cases above assert that
  // the dialog SET something; that it SURVIVES is a different question,
  // and the one a user actually has. A writer that omits a setting is
  // invisible both to registry_audit.py, which only checks that what IS
  // written resolves, and to a round trip whose fixture never set it.
  //
  // Here, not after the six cases below: case 4 leaves a midpoint in
  // force[iobj] and renames the slot, so a re-run of case 0 down there
  // does not produce a clean "Chiron" and this would be measuring the
  // leftovers. Slot state on exit is what case 0 already left.
  {
    char szPath[cchSzMax], *szFileOutSav = is.szFileOut;
    int nWriteFormatSav = us.nWriteFormat, i;
    flag fNoWriteSav = us.fNoWrite;

    sprintf2(S(szPath), "%s/astrolog-qt-objsel-%d.as",
      QDir::tempPath().toLocal8Bit().constData(),
      (int)QCoreApplication::applicationPid());
    us.fNoWrite = fFalse;
    us.nWriteFormat = 'd';
    is.szFileOut = szPath;
    Check(FOutputSettings(), "Save Program Settings wrote the file");

    // Aim the slot somewhere else entirely, so a file that carries
    // nothing leaves it wrong rather than accidentally right.
    rgObjSwiss[iobj - custLo] = 433;
    FCloneSzCore("NotChiron", (char **)&szObjDisp[iobj],
      szObjDisp[iobj] == szObjName[iobj]);

    i = CReplaySettingsQt(szPath, FWantObjDefQt);
    Check(i > 0, "and the object lines read back (%d)", i);
    Check(rgObjSwiss[iobj - custLo] == 2060,
      "the body the dialog chose survives a save and load (%d, want 2060)",
      rgObjSwiss[iobj - custLo]);
    Check(FEqSz(szObjDisp[iobj], "Chiron"),
      "and so does the name it gave the slot (%s)", szObjDisp[iobj]);

    QFile::remove(QString::fromLocal8Bit(szPath));
    is.szFileOut = szFileOutSav;
    us.nWriteFormat = nWriteFormatSav;
    us.fNoWrite = fNoWriteSav;
  }

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
  printf("  the dialog sets the body, names it, and it reaches the file\n");
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
  // Exactly, not a floor. Two reasons. A floor
  // tests the guess: 11 of these bodies resolve with no ephemeris files at
  // all -- "-Yi1" pointed at a directory that does not exist still answers
  // for 11 of them from the Moshier formulas -- so "cCheck > 0" passes on a
  // run that found nothing. And every body that fails to resolve skips its
  // own assertion above, silently, so this number is also the count of
  // assertions the loop actually ran. When the list
  // was 39 rows: 83 passed on /swe, 63 on ephem/, 53 on nothing, with no
  // failure to show for the difference. The list is 78 rows now and both
  // ephemerides answer for all of them, which is the point of keeping
  // them one set. Asserting it is
  // the only thing standing between a thinner ephemeris and a green run
  // that tested less.
  cWant = cObjSel;
  Check(cCheck == cWant,
    "%d of %d bodies resolved, expected all of them. Fewer means the "
    "bundled ephemeris no longer answers for everything rgObjSel[] "
    "offers, and this group ran %d assertions where it should have run "
    "%d -- see tools/check-ephem.sh for the same set from the other side",
    cCheck, cObjSel, cCheck, cWant);
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


// The time as the Set Chart Info dialog puts it in its own field, which
// is a different question from what SzTim() returns: the field is what
// the user reads.
static QString s_strTimField;

static QString StrChartInfoTimeQt()
{
  s_strTimField = QString();
  DriveModalQt(ShowChartInfoDialogQt, [](QWidget *pw) {
    QList<QComboBox *> rg = pw->findChildren<QComboBox *>();
    for (int i = 0; i < rg.size(); i++) {
      QStringList items;
      for (int j = 0; j < rg[i]->count(); j++)
        items << rg[i]->itemText(j);
      // The time field is the one offering Midnight and Noon, found the
      // same way the ephemeris list above is: these controls are built
      // from the resource and carry no object name.
      if (items.contains("Midnight")) {
        s_strTimField = rg[i]->currentText();
        break;
      }
    }
    pw->close();
  });
  return s_strTimField;
}


static void TestChartInfoTimeQt()
{
  real timSav = ciCore.tim;
  QString str;

  Group("Chart info time field");

  // 8:15 in the morning: one digit of hour, which is where the padding
  // shows. SzTim() writes " 8:15am" so a text chart's rows line up under
  // each other; in an edit box that is just an indent, and the field sat
  // beside a dropdown whose own entries are unpadded.
  ciCore.tim = 8.25;
  str = StrChartInfoTimeQt();
  Check(!str.isEmpty(), "the time field was found at all");
  Check(!str.startsWith(' '), "a one digit hour is not indented: \"%s\"",
    str.toLocal8Bit().constData());
  Check(str.startsWith("8:15"), "and it is still the right time: \"%s\"",
    str.toLocal8Bit().constData());

  // Two digits, where there was never any padding to trim: the same field
  // has to be untouched.
  ciCore.tim = 14.5;
  str = StrChartInfoTimeQt();
  Check(str.startsWith(us.fEuroTime ? "14:30" : "2:30"),
    "a two digit hour is unchanged: \"%s\"", str.toLocal8Bit().constData());

  ciCore.tim = timSav;
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

  // And in TEXT mode as well, which is the half this missed: Windows fires
  // it after the whole paint, in both modes (wdriver.cpp:2929, outside the
  // "if (!us.fGraphics)" block above it), and RedrawQt()'s text branch
  // returned before ever reaching it. A "-~Q3" hook simply never ran while
  // a text chart was on screen.
  {
    Borrow bGraph(us.fGraphics, fFalse);
    ExpSetN(iLetterZ, 0);
    RedrawQt();
    Check(NExpGet(iLetterZ) == 4242,
      "and fires for a text chart too (@z is %d)", NExpGet(iLetterZ));
  }

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

  // And OK with nothing selected makes a showing filter PERMANENT, which
  // is what FilterCIList() does and what Windows says in as many words:
  // "Only permanently filter on OK if no chart is selected."
  // The display half of this dialog's filter was ported in plan item 42;
  // this half was not, so pressing Filter and then OK narrowed the list
  // on Windows and threw the narrowing away here.
  //
  // Both directions asserted. Without the second the first passes on a
  // dialog that drops the whole list, and without the first it passes on
  // one that filters nothing.
  int cciBefore = is.cci;
  DriveModalQt(ShowChartListDialogQt, [](QWidget *pw) {
    QLineEdit *pe = pw->findChild<QLineEdit *>("deLi_n");
    if (pe != NULL)
      pe->setText("SuiteChart1");
    for (QPushButton *ppb : pw->findChildren<QPushButton *>())
      if (ppb->text().contains("Filter") && !ppb->text().contains("Remove"))
        ppb->click();
    for (QPushButton *ppb : pw->findChildren<QPushButton *>())
      if (ppb->text() == "OK") { ppb->click(); return; }
    pw->close();
  });
  Check(is.cci == 1,
    "Filter then OK narrows the list for good (%d charts, was %d)",
    is.cci, cciBefore);
  Check(is.cci > 0 && FEqSz(is.rgci[0].nam, "AstrologSuiteChart1"),
    "and the one left is the one that matched (\"%s\")",
    is.cci > 0 ? SzSet(is.rgci[0].nam) : "");

  // The list keeps its selection across the actions that leave it on
  // screen. Windows remembers the row, refills, clamps and re-selects
  // (wdialog.cpp:1019); this port refilled and lost it, so deleting three
  // charts meant selecting three times. Delete is the one to drive: it is
  // the action a person repeats.
  {
    Borrow bCci(is.cci, cciSav);
    int cciWas;

    is.cci = 0;
    for (i = 0; i < 3; i++) {
      ciCore = ciMain; ciCore.yea = 1990 + i;
      ciCore.nam = rgszNamT[i];
      FAppendCIList(&ciCore);
    }
    cciWas = is.cci;
    DriveModalQt(ShowChartListDialogQt, [](QWidget *pw) {
      QListWidget *pl = pw->findChild<QListWidget *>("dlLi");
      if (pl != NULL)
        pl->setCurrentRow(1);
      for (QPushButton *ppb : pw->findChildren<QPushButton *>())
        if (ppb->text().contains("Delete") && !ppb->text().contains("All")) {
          ppb->click();
          break;
        }
      // Read the selection back out before the dialog goes away.
      s_cRowList = pl != NULL ? pl->currentRow() : -99;
      for (QPushButton *ppb : pw->findChildren<QPushButton *>())
        if (ppb->text() == "Cancel") { ppb->click(); return; }
      pw->close();
    });
    Check(is.cci == cciWas - 1, "Delete Chart removed one (%d of %d)",
      is.cci, cciWas);
    Check(s_cRowList == 1,
      "and the highlight stayed on row 1 rather than being lost (%d)",
      s_cRowList);
  }

  // A chart with NO name at all, which the three list filters used to
  // walk straight off. ciDefa.nam is NULL until astrolog.as sets it with
  // "-zj", so a run started where that file is not found has a NULL name
  // on every chart "Copy From slot" appends -- and filtering one dumped
  // core. Reproduced by running the test binary from a directory with no
  // astrolog.as in it, then reduced to this.
  //
  // Both filters, because they are two separate loops and the location
  // one was as unguarded as the name one.
  {
    CI ciNull = ciMain;

    ciNull.nam = NULL; ciNull.loc = NULL;
    is.cci = 0;
    FAppendCIList(&ciNull);
    Check(is.cci == 1, "a chart with a NULL name goes into the list");
    FilterCIList("AstrologNoSuchName", "");
    Check(is.cci == 0, "filtering it by name does not walk off the NULL");
    is.cci = 0;
    FAppendCIList(&ciNull);
    FilterCIList("", "AstrologNoSuchPlace");
    Check(is.cci == 0, "and neither does filtering it by location");
  }

  is.cci = cciSav;
  printf("  the chart list honours its AstroExpression filter\n");
}


static void TestEphemerisListQt()
{
  flag fNetSav = us.fNoNetwork, fOldSav = us.fNoOldCalc;
  QString str;

  Group("Ephemeris list");

  us.fNoNetwork = us.fNoOldCalc = fTrue;
  str = StrEphemListQt();
  Check(!str.isEmpty(), "the ephemeris list was found at all (modal seen: \"%s\")",
    s_strComboWin.toLocal8Bit().constData());
  Check(!str.contains("Web"),
    "no web query offered when web queries are off: %s",
    str.toLocal8Bit().constData());
  Check(!str.contains("Matrix"),
    "no Matrix offered when it is off: %s",
    str.toLocal8Bit().constData());
  Check(str.contains("Swiss"), "Swiss Ephemeris is still offered");

  us.fNoNetwork = us.fNoOldCalc = fFalse;
  str = StrEphemListQt();
  Check(str.contains("Web"), "the web query is offered when allowed");
  Check(str.contains("Matrix"), "Matrix is offered when allowed");
  Check(!str.contains("Placalc"),
    "Placalc is never offered: the backend was removed on 2026-09-04");

  us.fNoNetwork = fNetSav; us.fNoOldCalc = fOldSav;
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
    FILE *fileSSav = is.S;
    FCloneSz(szOut, &is.szFileScreen);
    us.fGraphics = fFalse;
    us.fListing = fTrue; us.fWheel = fFalse;
    Action();                       // PrintHeader() path (-v listing).
    is.S = fileSSav;
    us.fListing = fFalse; us.fWheel = fTrue;
    Action();                       // PrintWheelCenter() path (-w wheel).
    // Action() opens is.S on is.szFileScreen and fclose()s it on the way
    // out without putting the caller's back, so leaving it moved arms an
    // abort in whatever prints next -- see the guard in
    // NRunQtTestTableQt(), which is where this one was finally caught.
    is.S = fileSSav;
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
    // never allocated -- a heap corruption in the TEST rather than in
    // the code under test. Its switch, "-YYt", is checked as a
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

  // The window size is written and read verbatim. It used to be written
  // with the sidebar width subtracted and read with it added back, which
  // agreed only when fSidebar read the same at both ends -- and it cannot,
  // because that macro tests gi.nMode and the chart mode is not a saved
  // setting. So save, reload, save, reload walked the window down 240
  // pixels a cycle. Asserted from both sides of fSidebar, since agreeing
  // with itself in one state is exactly what the old pair did.
  {
    int xWinSav = gs.xWin, yWinSav = gs.yWin, nModeSav = gi.nMode;
    flag fTextSav = gs.fText, fSideSav = gs.fDoSidebar;

    gi.nMode = gWheel; gs.fText = fTrue;
    for (i = 0; i <= 1; i++) {
      gs.fDoSidebar = (i > 0);
      gs.xWin = 1500; gs.yWin = 1260;
      sprintf2(S(szLine), ":Xw %d %d", gs.xWin, gs.yWin);
      FProcessCommandLine(szLine);
      Check(gs.xWin == 1500 && gs.yWin == 1260,
        "a saved window size reloads unchanged, sidebar %s (%d x %d)",
        i > 0 ? "on" : "off", gs.xWin, gs.yWin);
    }
    gs.xWin = xWinSav; gs.yWin = yWinSav; gi.nMode = nModeSav;
    gs.fText = fTextSav; gs.fDoSidebar = fSideSav;
  }

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
// object-keyed -- and one forgotten difference is what
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


// ---- Every settings field, asked at once ----
//
// FOutputSettings() is a hand-maintained list of sprintf2() lines and the
// reader is the switch registry. Nothing in the program knows which fields
// the pair is supposed to carry between them, so every check written before
// this one asked about the settings somebody remembered to ask about, and
// the ones nobody remembered were exactly the ones that got lost.
//
// This asks about all of them. settingsfields.h names every scalar member
// of US and GS, generated from astrolog.h; the test saves the current
// settings, poisons every field, replays the file, and names what did not
// come back. It found 62 lost settings on its first run, of which the
// AstroExpression hooks were 46.
//
// The ledger below is what is EXPECTED not to come back, each entry with a
// reason. A field that starts surviving is reported too, so an entry cannot
// quietly go stale.

typedef struct _setfieldskip {
  CONST char *szName;   // A field name, or a whole section with "*"
  CONST char *szWhy;
} SETFIELDSKIP;

static CONST SETFIELDSKIP rgsetskip[] = {

  // Two whole sections. Which chart to draw has never been a saved
  // setting: astrolog.as would then dictate the chart on every launch,
  // and the ":" prefix discipline in FOutputSettings() exists precisely
  // to write a value without disturbing one of these.
  {"Chart types*",       "chart type, not a saved setting"},
  {"Table chart types*", "chart type, not a saved setting"},

  {"us.fGraphics",    "-X, view state; and in a GUI build a settings "
                      "file deliberately cannot change it at all"},
  {"gs.szDisplay",    "-Xd, the X11 display this run opened"},
  {"us.nRel",         "-r family, which chart is being compared, not a "
                      "preference"},
  {"us.fProgress",    "-p, a progressed chart is a chart"},
  {"us.nProgress",    "-p0/-p1, chosen with the progression it belongs to"},
  {"us.fInDayMonth",  "-dm, the span of one transit search"},
  {"us.fInDayYear",   "-dy, the same"},
  {"us.nEphemYears",  "\"-EY 0\" is refused (FErrorValN, i < 1) and 0 is "
                      "what the Chart Settings box unticked leaves, while "
                      "\":Ey\" reads us.fEphemeris and is not deterministic"},
  {"us.rRatio",       "\"-r0 <file1> <file2> [<ratio>]\" wants two chart "
                      "files to name a ratio, and a settings file has none"},

  // The sixteen chart sub-option flags that used to sit here -- every one
  // spelt as a sub-letter of a chart type switch, and so unwritable on its
  // own -- are carried by "-Y2" as one packed field now (rgpfSubopt[] in
  // switch.cpp). They are asked about like any other field.
  {"gs.rRot",          "every switch that sets it (-XX/-XW/-XG/-XP) ends "
                       "in \"gi.nMode = FSwitchF2(gi.nMode == <mode>) * "
                       "<mode>\", zeroing the chart mode"},
  {"gs.rTilt",         "same as gs.rRot -- -XX and -XG set both"},
  {"gs.objTrack",      "same as gs.rRot -- -XZ zeroes gi.nMode too"},
  {"gs.fBackDraw",     "says a bitmap loaded with \"-XI <file>\" is "
                       "showing, and the file name is not saved either"},
  // What this run was asked to do, rather than what it remembers.
  {"us.fWriteFile",   "-o, write this chart to a file"},
  {"us.nWriteFormat", "-o, and in which format"},
  {"us.fNoDisplay",   "-Y0, print nothing this run"},
  {"us.cSequenceLine","-Yq, how many charts this run draws"},
  {"us.nListAll",     "-5e, draw every chart in the list, an action"},
  {"gs.fRoot",        "-XB, draw on the X11 root window"},
  {"gs.nAnim",        "-Xn, start up in animation mode"},

  // A preference the switch language cannot separate from a chart.
  {"us.fGraphAll",    "NSwB/NSwV: \"-B0\"/\"-V0\" toggle it and the "
                      "chart type"},
  {"gs.fConstel",     "NSwXF: \"-XF\" also sets gi.nMode to the world "
                      "map unless it already holds one of five modes"},
  {"gs.fPrintMap",    "NSwXP: \"-XPv\" zeroes gi.nMode and gs.rRot with "
                      "it"},
  {"us.nProgress",    "no spelling sets the progression method without "
                      "also demanding a date: \"-p0\" falls through "
                      "NSwp() to the three-argument branch"},

  // One-way by design, like the rest of the -0 lockdown family: NSw0 sets
  // fTrue and nothing sets it back, so a settings file could disable
  // AstroExpressions but never re-enable them. Not written, for the same
  // reason "-0o", "-0X" and "-0q" are not.
  {"us.fNoExp",       "-0~ only ever turns expressions off"} };

// Fields this test cannot poison, and so cannot ask about. Poisoning them
// changes what the reader will ACCEPT or what the run itself does, rather
// than only what it remembers: with "-0X" set the file's own graphics
// lines become an error and the load stops at the first one.
static CONST SETFIELDSKIP rgsetnopoison[] = {
  {"us.fNoRead",     "the -0 lockdown family gates the reader itself"},
  {"us.fNoWrite",    "same"},
  {"us.fNoGraphics", "same"},
  {"us.fNoQuit",     "same"},
  {"us.fLoop",       "-Q, how this run was invoked, not what it remembers"},
  {"us.fLoopInit",   "-Q0, the same"},
  {"us.fNoSwitches", "whether a command line was given at all"},
  {"us.fSzPersist",  "an allocation discipline, not a setting"},
  {"gs.ft",          "which file a render would be written to, chosen per "
                     "invocation; and every \"-Xb\"/\":Xp\" line in the "
                     "file writes it, so a poisoned value is overwritten "
                     "rather than remembered"} };

// gs.szStarsLin and gs.szStarsLnk are not two independent strings: the
// count of names in the first sizes gi.rges, which FProcessYXU() allocates
// in the same breath. Writing either directly leaves that array smaller
// than the list it is indexed by, and the heap goes with it a few groups
// later. So this pair is set through the one call that owns them.
static flag FSetFieldPairQt(CONST SETTINGFIELD *psf)
{
  return FEqSz(psf->szName, "gs.szStarsLin") ||
    FEqSz(psf->szName, "gs.szStarsLnk");
}


static flag FSetFieldAskQt(CONST SETTINGFIELD *psf)
{
  int i;

  for (i = 0; i < (int)(sizeof(rgsetnopoison)/sizeof(SETFIELDSKIP)); i++)
    if (FEqSz(psf->szName, rgsetnopoison[i].szName))
      return fFalse;
  return fTrue;
}


// The marker one string field is set to. Deliberately over cchSzMax, so a
// writer that formats it through a fixed buffer is caught by the truncation
// rather than by luck.
static void SzSetFieldMarkQt(int i, char *sz, int cchMax)
{
  int cch;

  sprintf2(sz, cchMax, "AstrologFieldProbe%d-", i);
  for (cch = CchSz(sz); cch < 300 && cch < cchMax-1; cch++)
    sz[cch] = 'x';
  sz[cch] = chNull;
}


static byte *PbSetFieldQt(CONST SETTINGFIELD *psf)
{
  return (byte *)(psf->fGs ? (void *)&gs : (void *)&us) + psf->off;
}


// The reason a field is not expected to survive, or NULL if it is.
static CONST char *SzSetFieldSkipQt(CONST SETTINGFIELD *psf)
{
  int i, cch;

  for (i = 0; i < (int)(sizeof(rgsetskip)/sizeof(SETFIELDSKIP)); i++) {
    cch = CchSz(rgsetskip[i].szName);
    if (rgsetskip[i].szName[cch-1] == '*') {
      // A whole section, named as it appears in astrolog.h.
      if (CchSz(psf->szSect) == cch-1 &&
        !memcmp(psf->szSect, rgsetskip[i].szName, cch-1))
        return rgsetskip[i].szWhy;
    } else if (FEqSz(psf->szName, rgsetskip[i].szName))
      return rgsetskip[i].szWhy;
  }
  return NULL;
}


static void TestSettingsFieldsQt()
{
  CONST char *szWhy;
  char szPath[cchSzMax], szMark[cchSzLine];
  QVector<real> rgrSav(csetfield);
  QVector<QByteArray> rgbaSav(csetfield);
  QVector<bool> rgfNull(csetfield);
  char *szFileOutSav = is.szFileOut;
  int nWriteFormatSav = us.nWriteFormat;
  flag fNoWriteSav = us.fNoWrite, fPopupSav = FNoPopupQt();
  int i, cLost = 0, cStale = 0, cAsked = 0;

  Group("Every settings field");
  SetNoPopupQt(fTrue);

  // Snapshot the PRISTINE state, which is what gets put back at the end.
  // Every group after this one runs against whatever this leaves behind,
  // and this one rewrites nearly every setting there is. The strings are copied, not pointed at: FCloneSzCore()
  // writes into the destination buffer in place when it is big enough,
  // so a saved pointer would follow the field rather than remember it.
  for (i = 0; i < csetfield; i++) {
    CONST SETTINGFIELD *psf = &rgsetfield[i];
    byte *pb = PbSetFieldQt(psf);
    switch (psf->ch) {
    case 'f': case 'i': rgrSav[i] = (real)*(int *)pb;  break;
    case 'l':           rgrSav[i] = (real)*(long *)pb; break;
    case 'r':           rgrSav[i] = *(real *)pb;       break;
    case 'c':           rgrSav[i] = (real)*(char *)pb; break;
    case 's':
      rgfNull[i] = (*(char **)pb == NULL);
      if (!rgfNull[i])
        rgbaSav[i] = QByteArray(*(char **)pb);
      break;
    }
  }

  // Then give every string field a value, so the question asked of it is
  // "does a set value survive" rather than "is an unset one still unset".
  // Most are empty in any ordinary run, and an empty field round trips
  // through a writer that skips it. The marker is what the comparison
  // below expects back; rgbaSav[] keeps the pristine value for the restore.
  //
  // And the marker is LONGER THAN cchSzMax, which is 255. A writer that
  // formats one of these through sprintf2() into sz truncates it, and a
  // truncated one does not merely lose its tail: it loses the closing
  // quote, and the next word on the line is read as a switch. That is what
  // happened to the -YXU star list, whose constellation set alone runs to
  // thousands of characters.
  for (i = 0; i < csetfield; i++)
    if (rgsetfield[i].ch == 's' && FSetFieldAskQt(&rgsetfield[i]) &&
      !FSetFieldPairQt(&rgsetfield[i])) {
      SzSetFieldMarkQt(i, S(szMark));
      FCloneSz(szMark, (char **)PbSetFieldQt(&rgsetfield[i]));
    }
  {
    char szLin[cchSzLine], szLnk[cchSzLine];
    for (i = 0; i < csetfield; i++)
      if (FEqSz(rgsetfield[i].szName, "gs.szStarsLin"))
        SzSetFieldMarkQt(i, S(szLin));
      else if (FEqSz(rgsetfield[i].szName, "gs.szStarsLnk"))
        SzSetFieldMarkQt(i, S(szLnk));
    FProcessYXU(szLin, szLnk, fFalse);
  }

  sprintf2(S(szPath), "%s/astrolog-qt-fields-%d.as",
    QDir::tempPath().toLocal8Bit().constData(),
    (int)QCoreApplication::applicationPid());
  us.fNoWrite = fFalse;
  us.nWriteFormat = 'd';
  is.szFileOut = szPath;
  Check(FOutputSettings(), "the settings writer wrote a file to ask about");
  is.szFileOut = szFileOutSav;
  us.nWriteFormat = nWriteFormatSav;

  // Poison everything the ledger does not excuse, so a field the file
  // fails to carry stays visibly wrong. "^1" rather than a constant, so
  // the new value stays near the old one and inside whatever range the
  // switch that reads it back will accept.
  for (i = 0; i < csetfield; i++) {
    CONST SETTINGFIELD *psf = &rgsetfield[i];
    byte *pb = PbSetFieldQt(psf);
    if (!FSetFieldAskQt(psf))
      continue;
    cAsked++;
    switch (psf->ch) {
    case 'f': case 'i': *(int *)pb ^= 1;            break;
    case 'l':           *(long *)pb ^= 1L;          break;
    case 'r':           *(real *)pb += 1.0;         break;
    case 'c':           *(char *)pb ^= 1;           break;
    // Strings are poisoned to empty rather than to a marker, so that a
    // field the writer skips because it is unset does not read as lost.
    case 's':
      if (FSetFieldPairQt(psf))
        FProcessYXU("", "", fFalse);
      else
        FCloneSz("", (char **)pb);
      break;
    }
  }

  SetNoPopupQt(fPopupSav);
  Check(FProcessSwitchFile(szPath, NULL),
    "and the file it wrote loads back with every field poisoned");
  SetNoPopupQt(fTrue);

  // Compare, and report by name. One assertion per lost field, because a
  // count says nothing about which one and the names are the whole value.
  for (i = 0; i < csetfield; i++) {
    CONST SETTINGFIELD *psf = &rgsetfield[i];
    byte *pb = PbSetFieldQt(psf);
    flag fBack;

    if (!FSetFieldAskQt(psf))
      continue;
    if (psf->ch == 's') {
      // NULL and "" are one state: FSzSet() is what every reader of these
      // uses, and a writer that emits "" for an unset field is right.
      char *sz = *(char **)pb;
      SzSetFieldMarkQt(i, S(szMark));
      fBack = (sz != NULL && FEqSz(sz, szMark));
    } else {
      real r = psf->ch == 'r' ? *(real *)pb :
        (psf->ch == 'l' ? (real)*(long *)pb :
        (psf->ch == 'c' ? (real)*(char *)pb : (real)*(int *)pb));
      fBack = (r == rgrSav[i]);
    }
    szWhy = SzSetFieldSkipQt(psf);
    if (szWhy == NULL) {
      if (!fBack) {
        Check(fFalse, "%s (%s) did not survive a save and reload",
          psf->szName, psf->szSwitch[0] ? psf->szSwitch : "no switch");
        cLost++;
      }
    } else if (fBack) {
      // It was poisoned and came back anyway, so the ledger is out of
      // date -- the writer grew a line for it.
      Check(fFalse, "%s survives now; drop its entry (\"%s\")",
        psf->szName, szWhy);
      cStale++;
    }
  }

  // Put everything back, scalars from the snapshot and strings through
  // the same call that owns them, before any other group runs.
  for (i = 0; i < csetfield; i++) {
    CONST SETTINGFIELD *psf = &rgsetfield[i];
    byte *pb = PbSetFieldQt(psf);
    switch (psf->ch) {
    case 'f': case 'i': *(int *)pb = (int)rgrSav[i];   break;
    case 'l':           *(long *)pb = (long)rgrSav[i]; break;
    case 'r':           *(real *)pb = rgrSav[i];       break;
    case 'c':           *(char *)pb = (char)rgrSav[i]; break;
    case 's':
      if (FSetFieldPairQt(psf))
        break;    // both halves restored together, just below
      FCloneSz(rgfNull[i] ? NULL : rgbaSav[i].constData(), (char **)pb);
      break;
    }
  }
  {
    QByteArray baLin, baLnk;
    for (i = 0; i < csetfield; i++)
      if (FEqSz(rgsetfield[i].szName, "gs.szStarsLin"))
        baLin = rgfNull[i] ? QByteArray() : rgbaSav[i];
      else if (FEqSz(rgsetfield[i].szName, "gs.szStarsLnk"))
        baLnk = rgfNull[i] ? QByteArray() : rgbaSav[i];
    FProcessYXU(baLin.constData(), baLnk.constData(), fFalse);
  }
  us.fNoWrite = fNoWriteSav;
  SetNoPopupQt(fPopupSav);
  remove(szPath);
  // Two caches are computed from fields this group rewrote and are not
  // recomputed by putting the fields back: the Swiss ephemeris search
  // path, which "-Yi" invalidates by hand, and the colour palette, which
  // "-YXK0" rebuilds. Leaving the first stale pointed every later group at
  // a marker directory, and 63 of 78 bodies stopped resolving.
  is.fSwissPathSet = fFalse;
  InitColorPalette(gs.fInverse);
  AdjustRestrictions();
  AdjustAspectCount();
  printf("  %d of %d settings fields asked, %d lost, %d stale excuses\n",
    cAsked, csetfield, cLost, cStale);
}


// ---- The settings that are not fields ----
//
// The sweep above reads settingsfields.h and so sees every scalar member of
// US and GS. It cannot see the rest of the configuration, which lives in
// global arrays: the restrictions, the per-object settings, the aspect and
// house influence tables, the rulerships, the palette. Those are what the
// Restrictions, Object Settings, Aspect Settings and Set Colors dialogs
// edit, and until this group the only assertions on them were at a handful
// of sample indices somebody had picked.
//
// Same method: save, poison every byte, replay, compare. Reported by array
// and by the first index that differs, since "rgobjset moved" is not a
// finding and "rgobjset[60].orb" is.

typedef struct _setarray {
  CONST char *szName;
  void *pv;             // The array's first element
  int cb;               // Its size in bytes
  char ch;              // b byte, i int, r real, o OBJSET
  int iLo, iHi;         // The elements that are settings; iHi < 0 is "all"
  CONST char *szWhy;    // NULL when it has to survive
} SETARRAY;

static void TestSettingsArraysQt()
{
  SETARRAY rgsetarray[] = {
    // iLo is 1 wherever element 0 is unused padding -- the aspect, sign and
    // rulership tables are all indexed from 1 -- and iHi names the last
    // element the writer's own loop reaches. Both are transcribed from
    // FOutputSettings(), and a bound set too wide fails loudly here rather
    // than quietly, which is the direction to be wrong in.
    {"ignore",      ignore.rgn,      sizeof(ignore.rgn),      'b',
      0, cObj, NULL},
    {"ignore2",     ignore2.rgn,     sizeof(ignore2.rgn),     'b',
      0, cObj, NULL},
    {"ignorez",     ignorez,         sizeof(ignorez),         'b',
      0, arAnt, NULL},
    {"ignore7",     ignore7,         sizeof(ignore7),         'b',
      0, rrMax-1, NULL},
    {"ignorea",     ignorea.rgn,     sizeof(ignorea.rgn),     'b',
      1, cAspect, NULL},
    {"rgobjset",    rgobjset.rgn,    sizeof(rgobjset.rgn),    'o',
      0, oNorm1, NULL},
    {"force",       force.rgn,       sizeof(force.rgn),       'r',
      0, cObj, NULL},
    {"rgrBonusInf", rgrBonusInf,     sizeof(rgrBonusInf),     'r',
      1, 5, NULL},
    {"rHouseInf",   rHouseInf,       sizeof(rHouseInf),       'r',
      1, cSign+5, NULL},
    {"rAspInf",     rAspInf.rgn,     sizeof(rAspInf.rgn),     'r',
      1, 18, NULL},
    {"rAspAngle",   rAspAngle.rgn,   sizeof(rAspAngle.rgn),   'r',
      1, cAspect, NULL},
    {"rAspOrb",     rAspOrb.rgn,     sizeof(rAspOrb.rgn),     'r',
      1, cAspect, NULL},
    {"ruler1",      ruler1.rgn,      sizeof(ruler1.rgn),      'i',
      1, 10, NULL},
    {"ruler2",      ruler2.rgn,      sizeof(ruler2.rgn),      'i',
      1, 10, NULL},
    {"exalt",       exalt.rgn,       sizeof(exalt.rgn),       'i',
      1, 10, NULL},
    {"kAspA",       kAspA.rgn,       sizeof(kAspA.rgn),       'i',
      1, 18, NULL},
    {"kMainA",      kMainA,          sizeof(kMainA),          'i',
      0, 8, NULL},
    {"kRainbowA",   kRainbowA,       sizeof(kRainbowA),       'i',
      1, cRainbow, NULL},
    {"kElemA",      kElemA,          sizeof(kElemA),          'i',
      0, cElem-1, NULL},
    // Derived, and the deriving is what puts them right.
    {"rules",       rules.rgn,       sizeof(rules.rgn),       'i', 1, cSign,
     "sign-keyed view of ruler1[], rebuilt by the \"-YJ\" reader"},
    {"rules2",      rules2.rgn,      sizeof(rules2.rgn),      'i', 1, cSign,
     "sign-keyed view of ruler2[], the same"},
    {"kObjA",       kObjA.rgn,       sizeof(kObjA.rgn),       'i', 0, cObj,
     "computed from rgobjset[].kolor and the rulership colors, not stored"},
    {"starname",    starname,        sizeof(starname),        'i', 1, cStar,
     "the star sort order for this run, rebuilt whenever stars are cast"},
    {"pluszone",    pluszone,        sizeof(pluszone),        'b',
      1, cSector,
     "the sector plus zones, which are compiled in and have no switch"} };
  int csetarray = (int)(sizeof(rgsetarray)/sizeof(SETARRAY));
  char szPath[cchSzMax];
  QVector<QByteArray> rgbaSav(csetarray), rgbaWant(csetarray);
  char *szFileOutSav = is.szFileOut;
  int nWriteFormatSav = us.nWriteFormat;
  flag fNoWriteSav = us.fNoWrite, fPopupSav = FNoPopupQt();
  int i, j, cb, cLost = 0, cStale = 0;

  Group("Settings arrays");
  SetNoPopupQt(fTrue);

  // The PRISTINE state, which is what gets put back at the end. Taken
  // before the fill below, or the fill would be what every later group
  // inherits -- every object forced to a position, and every aspect at a
  // made-up angle.
  for (i = 0; i < csetarray; i++)
    rgbaSav[i] = QByteArray((CONST char *)rgsetarray[i].pv,
      rgsetarray[i].cb);

  // Two arrays are written only where they differ from a default, so an
  // element left at its default round trips through a writer that skips
  // it. Give them all a non-default value first, the way the field sweep
  // gives every string one, so the question asked is the useful one.
  for (i = 0; i <= cObj; i++)
    force[i] = ForcePos((real)((i * 7) % 360));
  // Whole degrees and a half, not "the default plus one": the writer emits
  // six decimals, and a default like 360/7 has more than that, so the fill
  // would come back a few bits out and read as a loss it is not.
  for (i = 1; i <= cAspect; i++)
    rAspAngle[ASPT(i)] = (real)((i * 7) % 180) + 0.5;
  // And the aspect restrictions, whose line carries a LIST: with none
  // restricted the list is empty, and an empty list comes back from a
  // writer that emits nothing just as well as from one that emits the
  // line. Restricting every third aspect is what makes the check bite.
  for (i = 1; i <= cAspect; i++)
    ignorea[ASPT(i)] = (i % 3) == 0;
  AdjustAspectCount();

  // And the state the file has to bring back, which is the filled one.
  for (i = 0; i < csetarray; i++)
    rgbaWant[i] = QByteArray((CONST char *)rgsetarray[i].pv,
      rgsetarray[i].cb);

  sprintf2(S(szPath), "%s/astrolog-qt-arrays-%d.as",
    QDir::tempPath().toLocal8Bit().constData(),
    (int)QCoreApplication::applicationPid());
  us.fNoWrite = fFalse;
  us.nWriteFormat = 'd';
  is.szFileOut = szPath;
  Check(FOutputSettings(), "the settings writer wrote a file to ask about");
  is.szFileOut = szFileOutSav;
  us.nWriteFormat = nWriteFormatSav;

  // Poison by type, so the new value stays in the neighbourhood of the old
  // and nothing the reader touches on the way past goes out of range.
  for (i = 0; i < csetarray; i++) {
    SETARRAY *psa = &rgsetarray[i];
    if (psa->ch == 'b') {
      for (j = 0; j < psa->cb; j++)
        ((byte *)psa->pv)[j] ^= 1;
    } else if (psa->ch == 'i') {
      for (j = 0; j < psa->cb / (int)sizeof(int); j++)
        ((int *)psa->pv)[j] ^= 1;
    } else if (psa->ch == 'r') {
      for (j = 0; j < psa->cb / (int)sizeof(real); j++)
        ((real *)psa->pv)[j] += 1.0;
    } else {
      for (j = 0; j <= oNorm1; j++) {
        rgobjset.rgn[j].orb += 1.0;  rgobjset.rgn[j].add += 1.0;
        rgobjset.rgn[j].inf += 1.0;  rgobjset.rgn[j].tinf += 1.0;
        rgobjset.rgn[j].kolor ^= 1;
      }
    }
  }

  Check(FProcessSwitchFile(szPath, NULL),
    "and the file it wrote loads back with every array poisoned");

  for (i = 0; i < csetarray; i++) {
    SETARRAY *psa = &rgsetarray[i];
    CONST byte *pbWas = (CONST byte *)rgbaWant[i].constData();
    CONST byte *pbIs = (CONST byte *)psa->pv;
    int ib = -1;

    {
      int cbElem = psa->ch == 'b' ? 1 : (psa->ch == 'i' ?
        (int)sizeof(int) : (psa->ch == 'r' ? (int)sizeof(real) :
        (int)sizeof(OBJSET)));
      for (cb = psa->iLo * cbElem; cb < (psa->iHi + 1) * cbElem &&
        cb < psa->cb; cb++)
        if (pbWas[cb] != pbIs[cb]) {
          ib = cb;
          break;
        }
    }
    if (psa->szWhy == NULL) {
      if (ib >= 0) {
        int cbElem = psa->ch == 'b' ? 1 : (psa->ch == 'i' ?
          (int)sizeof(int) : (psa->ch == 'r' ? (int)sizeof(real) :
          (int)sizeof(OBJSET)));
        int iElem = ib / cbElem;
        char szWas[cchSzDef], szIs[cchSzDef];

        if (psa->ch == 'b')
          sprintf2(S(szWas), "%d", (int)pbWas[iElem]),
          sprintf2(S(szIs), "%d", (int)pbIs[iElem]);
        else if (psa->ch == 'i')
          sprintf2(S(szWas), "%d", ((CONST int *)pbWas)[iElem]),
          sprintf2(S(szIs), "%d", ((CONST int *)pbIs)[iElem]);
        else if (psa->ch == 'r')
          sprintf2(S(szWas), "%.4f", ((CONST real *)pbWas)[iElem]),
          sprintf2(S(szIs), "%.4f", ((CONST real *)pbIs)[iElem]);
        else
          sprintf2(S(szWas), "(objset)"), sprintf2(S(szIs), "(objset)");
        Check(fFalse, "%s[%d] did not survive a save and reload "
          "(was %s, back as %s)", psa->szName, iElem, szWas, szIs);
        cLost++;
      }
    } else if (ib < 0) {
      Check(fFalse, "%s survives now; drop its entry (\"%s\")",
        psa->szName, psa->szWhy);
      cStale++;
    }
  }

  for (i = 0; i < csetarray; i++)
    CopyRgb((pbyte)rgbaSav[i].constData(), (pbyte)rgsetarray[i].pv,
      rgsetarray[i].cb);
  us.fNoWrite = fNoWriteSav;
  SetNoPopupQt(fPopupSav);
  remove(szPath);
  AdjustRestrictions();
  AdjustAspectCount();
  printf("  %d settings arrays asked, %d lost, %d stale excuses\n",
    csetarray, cLost, cStale);
}


// ---- And the settings that are strings in arrays ----
//
// The two sweeps above cover the scalar fields and the numeric arrays. The
// last of the configuration is user text held in arrays: renamed objects,
// custom star names, and the 96 macros. Each has its own ownership rule --
// szObjDisp[] is the object's own name when it is not custom, and
// SetObjDisp() is the only thing that knows it -- so they are set and put
// back through those rather than by assignment.
//
// Same shape as the field sweep's strings: mark every element, save,
// poison to empty, replay, compare. The marker is over cchSzMax again,
// because "-YD" and "-YU" formatted their value through sz until this
// group was written.

static void TestSettingsStringsQt()
{
  char szPath[cchSzMax], szMark[cchSzLine];
  QVector<QByteArray> rgbaObj(cObj+1), rgbaStar(cStar+1), rgbaMac(cMacro);
  QVector<bool> rgfObjCustom(cObj+1);
  char *szFileOutSav = is.szFileOut;
  int nWriteFormatSav = us.nWriteFormat;
  flag fNoWriteSav = us.fNoWrite, fPopupSav = FNoPopupQt();
  int i, cLost = 0, cAsked = 0;

  Group("Settings strings");
  SetNoPopupQt(fTrue);

  // Pristine, for the restore.
  for (i = 0; i <= cObj; i++) {
    rgfObjCustom[i] = FObjDispCustom(i);
    rgbaObj[i] = QByteArray(szObjDisp[i]);
  }
  for (i = 1; i <= cStar; i++)
    rgbaStar[i] = QByteArray(SzSet(szStarCustom[i]));
  for (i = 0; i < cMacro; i++)
    rgbaMac[i] = QByteArray(i < is.cszMacro ? SzSet(is.rgszMacro[i]) : "");

  // Marked, which is what has to come back. Object names go through
  // NParseSz() when a file names one, so the marker keeps the object's own
  // name at its head and every one stays distinct.
  for (i = 0; i <= cObj; i++) {
    sprintf2(S(szMark), "%.3sProbe%d", szObjName[i], i);
    SetObjDisp(i, szMark);
    cAsked++;
  }
  for (i = 1; i <= cStar; i++) {
    sprintf2(S(szMark), "StarProbe%d", i);
    FCloneSz(szMark, &szStarCustom[i]);
    cAsked++;
  }
  if (FEnsureMacro(cMacro))
    for (i = 0; i < cMacro; i++) {
      SzSetFieldMarkQt(i, S(szMark));
      FCloneSz(szMark, &is.rgszMacro[i]);
      cAsked++;
    }

  sprintf2(S(szPath), "%s/astrolog-qt-strings-%d.as",
    QDir::tempPath().toLocal8Bit().constData(),
    (int)QCoreApplication::applicationPid());
  us.fNoWrite = fFalse;
  us.nWriteFormat = 'd';
  is.szFileOut = szPath;
  Check(FOutputSettings(), "the settings writer wrote a file to ask about");
  is.szFileOut = szFileOutSav;
  us.nWriteFormat = nWriteFormatSav;

  for (i = 0; i <= cObj; i++)
    SetObjDisp(i, szObjName[i]);
  for (i = 1; i <= cStar; i++)
    FCloneSz("", &szStarCustom[i]);
  for (i = 0; i < is.cszMacro; i++)
    FCloneSz("", &is.rgszMacro[i]);

  Check(FProcessSwitchFile(szPath, NULL),
    "and the file it wrote loads back with every string emptied");

  for (i = 0; i <= cObj; i++) {
    sprintf2(S(szMark), "%.3sProbe%d", szObjName[i], i);
    if (!FEqSz(szObjDisp[i], szMark)) {
      Check(fFalse, "szObjDisp[%d] (-YD) did not survive a save and reload",
        i);
      cLost++;
      break;
    }
  }
  for (i = 1; i <= cStar; i++) {
    sprintf2(S(szMark), "StarProbe%d", i);
    if (!FEqSz(SzSet(szStarCustom[i]), szMark)) {
      Check(fFalse,
        "szStarCustom[%d] (-YU) did not survive a save and reload", i);
      cLost++;
      break;
    }
  }
  for (i = 0; i < cMacro; i++) {
    SzSetFieldMarkQt(i, S(szMark));
    if (i >= is.cszMacro || !FEqSz(SzSet(is.rgszMacro[i]), szMark)) {
      Check(fFalse,
        "is.rgszMacro[%d] (-M0) did not survive a save and reload", i);
      cLost++;
      break;
    }
  }

  for (i = 0; i <= cObj; i++)
    SetObjDisp(i, rgfObjCustom[i] ? rgbaObj[i].constData() : szObjName[i]);
  for (i = 1; i <= cStar; i++)
    FCloneSz(rgbaStar[i].isEmpty() ? NULL : rgbaStar[i].constData(),
      &szStarCustom[i]);
  for (i = 0; i < is.cszMacro && i < cMacro; i++)
    FCloneSz(rgbaMac[i].isEmpty() ? NULL : rgbaMac[i].constData(),
      &is.rgszMacro[i]);
  us.fNoWrite = fNoWriteSav;
  SetNoPopupQt(fPopupSav);
  remove(szPath);
  printf("  %d settings strings asked, %d lost\n", cAsked, cLost);
}


// Graphics mode is view state, not a setting, and a settings file must not
// be able to turn the GUI off. "Save Program Settings" with a text chart on
// screen writes "_X"; the window is created inside FActionX(), which
// Action() only reaches when us.fGraphics is set, so before the fix
// "astrolog-qt -i <that file>" printed nothing and exited. Two-sided: the
// same "_X" straight from a command line still has to work, because that is
// how run-qt-tests.sh runs this build to completion with no window.
static void TestGraphicsModeSourceQt()
{
  char szFile[cchSzMax];
  CONST char *rgsz[3];
  FILE *file;
  flag fSav = us.fGraphics, fPopupSav = FNoPopupQt();

  Group("Graphics mode source");
  SetNoPopupQt(fTrue);
  sprintf2(S(szFile), "%s/astrolog-qt-gfx-%d.as",
    QDir::tempPath().toLocal8Bit().constData(),
    (int)QCoreApplication::applicationPid());
  file = fopen(szFile, "w");
  fprintf(file, "@AD800  ; graphics mode\n"
    "_X               ; Graphics chart display [\"_X\" is text]\n");
  fclose(file);

  us.fGraphics = fTrue;
  Check(FProcessSwitchFile(szFile, NULL), "a file holding \"_X\" loads");
  Check(us.fGraphics, "and it did not turn the GUI off");

  rgsz[0] = szAppNameCore; rgsz[1] = "_X"; rgsz[2] = NULL;
  Check(FProcessSwitches(2, (char **)rgsz, NULL),
    "\"_X\" on a command line is accepted");
  Check(!us.fGraphics, "and there it still selects text mode");

  us.fGraphics = fSav;
  SetNoPopupQt(fPopupSav);
  remove(szFile);
  printf("  a settings file cannot turn graphics mode off; a switch can\n");
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
// Every other net in this project is differential and can prove only
// "unchanged". This one asks the ephemeris library the same question
// Astrolog asks it and requires the same answer.
//
// The Astrolog-object -> Swiss-body mapping below is transcribed here on
// purpose rather than read from calc.cpp, so a drift in that mapping
// fails. What it tests is Astrolog's glue -- object numbering, the flags
// in FSwissPlanet(), delta-T, the sidereal offset, ProcessPlanet()'s
// rectangular-to-zodiac conversion -- which is where the calculation bugs
// have been.
//
// Why the tolerances are what they are:
//   * Swiss agreement is EXACT: 0.000000 arcsec over 15 bodies and 7
//     epochs, tropical and sidereal. rEpsSwiss is slack against compiler
//     reassociation, not a fudge factor.
//   * Matrix vs Swiss is catastrophe detection, not precision. Worst case
//     runs from 0.010 deg (Sun-Mars) to 11.01 (Vesta); the per-body
//     tolerances carry about 2x headroom.
//   * All 40 house systems partition the circle once: 12 positive gaps
//     summing to 360 within 1e-9.

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
    Borrow bMat(us.fMatrixPla, fFalse);
    Borrow bSwiss(us.nSwissEph, 0), bNoOld(us.fNoOldCalc, fFalse);
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
    // past a 15-byte buffer. That is the intermittent abort of
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
      // worst case measured 1.7e-06; the bound is an order above it.
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
    // us.fEphemFiles, Astrolog's own ComputeHouses()
    // otherwise -- and ComputeHouses() guards exactly two systems
    // (calc.cpp:508, Placidus and Koch fall back to Porphyry).
    //
    // This asserts the partition for every combination EXCEPT the ones
    // measured as degenerate, and separately asserts that
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
    // failure mode here -- one such found a probe re-parsing a line
    // 33,219 times -- and nothing else in the suite looks at the table's
    // contents at all.
    if (FEnsureAtlas()) {
      // Astrolog's longitude is positive WEST, which is the opposite of
      // the geographic convention and is exactly the mistake this leg
      // caught while writing it: the probes read as eight world
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
      // fill *piae the way this needs. So it gets a stream of its own:
      // without one it prints into a FILE nothing has opened.
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
        // the assertion a weaker version would have missed:
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
    // unlike positions they have no library to ask. The invariant is the
    // one leg 9 uses for returns: a hit, re-cast, must satisfy the
    // condition it was searching for.
    //
    // Restricted to Sun and Moon with conjunction the only aspect, and
    // sign changes, direction changes and the void-of-course pass off,
    // every hit is a new moon. Without those the search reports six
    // other event kinds and half the hits are not aspects.
    //
    // Two checks, and the second is not an internal invariant at all:
    // the separation at each hit, and the interval between consecutive
    // hits, which must be the synodic month.
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
    // put it on the meridian. At 41.85N on 2020-03-20 the
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


/*
******************************************************************************
** The atlas and the time zone engine, against the world.
******************************************************************************
*/

// The atlas and time zone engine, against facts from outside this
// repository: where six cities are, and when two countries moved their
// clocks. A regression shows up here as a wrong answer rather than as a
// diff somebody has to judge.
//
// The DST dates are the ones a rule change moved, since the ordinary
// cases come out right by accident:
//
//   15 Mar 2006 standard / 15 Mar 2007 daylight  the 2007 US rule change
//   15 Jan 1974 DAYLIGHT                          year-round DST that year
//   7 Mar 2020 standard / 8 Mar 2020 daylight     the transition day
//   Sydney: daylight in January                   southern hemisphere
//   Mumbai, Kathmandu, Adelaide                   half and quarter hours
//
// The table falsifies itself: it holds both answers for one city on
// different dates and four different offsets, so no constant reply
// passes -- not "always standard", not "always a whole hour".

// Degrees WEST and degrees NORTH, from an atlas that is not this one.
// Astrolog stores longitude positive west, which SzLocation() renders as
// the "W"/"E" suffix -- so a sign flip in either field fails here rather
// than quietly relocating every chart to the opposite hemisphere.
//
// A tenth of a degree is about 11 km: comfortably inside a metropolitan
// area, and nowhere near another city. Comparing formatted strings was
// tried first and is worse -- SzLocation() pads longitude to three
// columns, so two of these read as failures over a leading space.

typedef struct {
  CONST char *szCity;
  real degW, degN;
} ATLASCITY;

static CONST ATLASCITY rgatlascityQt[] = {
  {"Seattle, WA, USA",    122.33,  47.61},
  {"Tokyo, Japan",       -139.69,  35.69},
  {"Sydney, Australia",  -151.21, -33.87},
  {"Reykjavik, Iceland",   21.94,  64.15},
  {"Quito, Ecuador",       78.52,  -0.22},
  {"Mumbai, India",       -72.88,  19.08} };
#define catlascityQt ((int)(sizeof(rgatlascityQt) / sizeof(ATLASCITY)))
#define rAtlasSlopQt 0.1

typedef struct {
  CONST char *szCity;
  int mon, day, yea;
  real zon;                 // Hours west of UTC.
  flag fDst;                // On daylight time that day?
  CONST char *szWhy;
} ATLASZONE;

static CONST ATLASZONE rgatlaszoneQt[] = {
  {"Seattle, WA, USA",     1, 15, 1990,  8.0,   fFalse, "winter"},
  {"Seattle, WA, USA",     6, 15, 1990,  8.0,   fTrue,  "summer"},
  {"Seattle, WA, USA",     3, 15, 2006,  8.0,   fFalse, "the old April rule"},
  {"Seattle, WA, USA",     3, 15, 2007,  8.0,   fTrue,  "the 2007 rule change"},
  {"Seattle, WA, USA",     1, 15, 1974,  8.0,   fTrue,  "year-round DST, 1974"},
  {"Seattle, WA, USA",     3,  7, 2020,  8.0,   fFalse, "the day before"},
  {"Seattle, WA, USA",     3,  8, 2020,  8.0,   fTrue,  "the transition day"},
  {"Seattle, WA, USA",    11,  1, 2020,  8.0,   fFalse, "back to standard"},
  {"Sydney, Australia",    1, 15, 2020, -10.0,  fTrue,  "southern summer"},
  {"Sydney, Australia",    7, 15, 2020, -10.0,  fFalse, "southern winter"},
  {"Mumbai, India",        6, 15, 2020,  -5.5,  fFalse, "a half hour zone"},
  {"Kathmandu, Nepal",     6, 15, 2020,  -5.75, fFalse, "a quarter hour zone"},
  {"Adelaide, Australia",  1, 15, 2020,  -9.5,  fTrue,  "half hour plus DST"} };
#define catlaszoneQt ((int)(sizeof(rgatlaszoneQt) / sizeof(ATLASZONE)))

// Swallow the rows the console path prints, so driving the atlas here
// does not scribble a city listing through the suite's output.
static void SinkAtlasRowQt(CONST char *, int) { }

static void TestAtlasZoneQt()
{
  void (*pfnSav)(CONST char *, int) = pfnAtlasRow;
  CI ciCoreSav = ciCore, ciMainSav = ciMain;
  int i, iae;

  Group("Atlas and time zones");
  pfnAtlasRow = SinkAtlasRowQt;

  iae = 1;
  if (!DisplayAtlasLookup(rgatlascityQt[0].szCity, fFalse, &iae)) {
    pfnAtlasRow = pfnSav;
    printf("  skipped: no atlas data, so there is nothing to check\n");
    return;
  }

  for (i = 0; i < catlascityQt; i++) {
    iae = 1;
    flag fFound = DisplayAtlasLookup(rgatlascityQt[i].szCity, fFalse, &iae);
    Check(fFound, "%s is in the atlas", rgatlascityQt[i].szCity);
    if (!fFound)
      continue;
    Check(RAbs(ciCore.lon - rgatlascityQt[i].degW) < rAtlasSlopQt &&
      RAbs(ciCore.lat - rgatlascityQt[i].degN) < rAtlasSlopQt,
      "%s is within %g degrees of %.2fW %.2fN (got %s)",
      rgatlascityQt[i].szCity, rAtlasSlopQt, rgatlascityQt[i].degW,
      rgatlascityQt[i].degN, SzLocation(ciCore.lon, ciCore.lat));
  }

  for (i = 0; i < catlaszoneQt; i++) {
    CONST ATLASZONE *paz = &rgatlaszoneQt[i];

    iae = 1;
    flag fFound = DisplayAtlasLookup(paz->szCity, fFalse, &iae);
    Check(fFound, "%s is in the atlas", paz->szCity);
    if (!fFound)
      continue;
    // The date has to be set before asking, because which rule applies
    // is the whole question.
    ciCore.mon = paz->mon; ciCore.day = paz->day; ciCore.yea = paz->yea;
    ciCore.tim = 12.0;
    flag fZone = DisplayTimezoneChanges(is.rgae[iae].izn, fFalse, &ciCore);
    Check(fZone, "%s has time zone data", paz->szCity);
    if (!fZone)
      continue;
    Check(ciCore.zon == paz->zon,
      "%s %d/%d/%d is zone %g (got %g)", paz->szCity, paz->mon, paz->day,
      paz->yea, paz->zon, ciCore.zon);
    Check((ciCore.dst != 0.0) == (paz->fDst != fFalse),
      "%s %d/%d/%d is %s -- %s (got dst %g)", paz->szCity, paz->mon,
      paz->day, paz->yea, paz->fDst ? "daylight time" : "standard time",
      paz->szWhy, ciCore.dst);
  }

  pfnAtlasRow = pfnSav;
  ciCore = ciCoreSav; ciMain = ciMainSav;
  printf("  %d cities placed and %d zone rules held\n",
    catlascityQt, catlaszoneQt);
}


/*
******************************************************************************
** Eclipses, against the published canon.
******************************************************************************
*/

// The same argument as the atlas group above, on a harder surface: which
// eclipses happened, and what kind each was, are facts recorded outside
// this repository. Astrolog decides the type itself, from the angular
// sizes and separation of two discs, so agreement with NASA's canon is a
// real check on that arithmetic rather than a check that it has not
// changed.
//
// One subtlety worth stating, because it looks like a bug and is not.
// The type of a solar eclipse depends on WHERE you are, and Astrolog has
// two modes. With us.fEclipseAny set -- which is what astrolog.as ships,
// and what "=Yu0" means -- it asks whether the eclipse is total or
// annular anywhere on Earth, and answers exactly as the canon does. With
// it clear ("=Yu" on its own, which deliberately turns it off, work log
// item 66) it asks geocentrically, and at the moment of ecliptic
// conjunction the Moon's disc almost never covers the Sun's completely
// from the Earth's centre -- so every solar eclipse reads "Partial".
// That is the honest geocentric answer, not a defect, and this group
// pins both modes so the distinction cannot be quietly lost.
//
// Times are UT at greatest eclipse, rounded to the minute.

typedef struct {
  int mon, day, yea;
  real tim;                 // UT hours at greatest eclipse.
  flag fSolar;
  int et;                   // What the canon says.
  CONST char *szWhat;
} ECLIPSEFACT;

static CONST ECLIPSEFACT rgeclipseQt[] = {
  {  8, 21, 2017, 18.43, fTrue,  etTotal,   "the 2017 American totality"},
  {  2, 26, 2017, 14.90, fTrue,  etAnnular, "annular over South America"},
  {  7,  2, 2019, 19.38, fTrue,  etTotal,   "total over Chile"},
  { 12, 26, 2019,  5.30, fTrue,  etAnnular, "annular over Indonesia"},
  {  6, 21, 2020,  6.68, fTrue,  etAnnular, "annular over Africa and Asia"},
  { 12, 14, 2020, 16.23, fTrue,  etTotal,   "total over Patagonia"},
  {  6, 10, 2021, 10.72, fTrue,  etAnnular, "annular over the Arctic"},
  { 12,  4, 2021,  7.55, fTrue,  etTotal,   "total over Antarctica"},
  {  4,  8, 2024, 18.30, fTrue,  etTotal,   "the 2024 American totality"},
  { 10,  2, 2024, 18.75, fTrue,  etAnnular, "annular over the South Pacific"},
  {  1, 31, 2018, 13.50, fFalse, etTotal,   "total lunar"},
  {  7, 27, 2018, 20.37, fFalse, etTotal,   "the century's longest totality"},
  {  1, 21, 2019,  5.20, fFalse, etTotal,   "total lunar"},
  {  5, 16, 2022,  4.20, fFalse, etTotal,   "total lunar"},
  {  9, 18, 2024,  2.72, fFalse, etPartial, "a shallow partial lunar"} };
#define ceclipseQt ((int)(sizeof(rgeclipseQt) / sizeof(ECLIPSEFACT)))

static void TestEclipseQt()
{
  CI ciCoreSav = ciCore, ciMainSav = ciMain;
  flag fEclSav = us.fEclipse, fAnySav = us.fEclipseAny;
  flag fTopoSav = us.fTopoPos;
  int objCenSav = us.objCenter, cSolar = 0, i, et;
  real rPct;

  Group("Eclipses");
  us.fEclipse = fTrue;
  us.fEclipseAny = fTrue;         // What astrolog.as ships: "=Yu0".
  // Pin the geometry rather than inheriting it: every field below moves
  // a planet's longitude or the frame it is measured in, and
  // TestAllMenuActionsQt() leaves all of them set -- see the list above
  // that function. Heliocentric is the loudest, making
  // NCheckEclipseLunar() return etUndefined outright. Topocentric
  // positions move the Moon by more than the annular/total margin.
  //
  // us.fEquator is worth knowing about on its own: CastChart() converts
  // ecliptic to equatorial under "-sr", but the loop skips restricted
  // objects -- so a RESTRICTED body keeps ecliptic coordinates while
  // everything else does not, and a separation between the two is
  // measured across two coordinate systems. Astrolog does not appear to
  // read a restricted object's position, so this is an observation rather
  // than a reported defect.
  //
  // Restored field by field below rather than by assigning the struct
  // back: us carries char * fields other code frees, so putting a stale
  // copy back is a use-after-free.
  US usSav = us;
  us.objCenter = oEar;
  us.fTopoPos = fFalse;
  us.nRel = rcNone;
  us.fProgress = fFalse;
  us.rHarmonic = 1.0;
  us.nDwad = 0;
  us.fNavamsa = fFalse;
  us.objOnAsc = 0;
  us.fFlip = us.fGeodetic = us.fSidereal = us.fParallel = fFalse;
  us.rZodiacOffset = 0.0;
  us.fDecan = us.fHouse3D = us.fIndian = fFalse;
  us.fEquator = us.fEquator2 = fFalse;
  us.nHouseSystem = 0;

  for (i = 0; i < ceclipseQt; i++) {
    CONST ECLIPSEFACT *pef = &rgeclipseQt[i];

    ciCore.mon = pef->mon; ciCore.day = pef->day; ciCore.yea = pef->yea;
    ciCore.tim = pef->tim;
    ciCore.dst = 0.0; ciCore.zon = 0.0;      // The times above are UT.
    ciCore.lon = 0.0; ciCore.lat = 51.5;
    CastChart(1);
    et = pef->fSolar ? NCheckEclipse(oSun, oMoo, &rPct) :
      NCheckEclipseLunar(us.objCenter, oMoo, oSun, &rPct);
    Check(et == pef->et,
      "%d/%d/%d is a %s %s eclipse -- %s (got %d)", pef->mon, pef->day,
      pef->yea, pef->et == etTotal ? "total" :
      (pef->et == etAnnular ? "annular" : "partial"),
      pef->fSolar ? "solar" : "lunar", pef->szWhat, et);
    cSolar += pef->fSolar;
  }

  // And the other mode. Geocentrically, from the Earth's centre at the
  // moment of conjunction, the 2017 and 2024 totalities are partial --
  // which is right, and is what a reader of "=Yu" without the "0" sees.
  us.fEclipseAny = fFalse;
  ciCore.mon = 8; ciCore.day = 21; ciCore.yea = 2017; ciCore.tim = 18.43;
  ciCore.dst = 0.0; ciCore.zon = 0.0; ciCore.lon = 0.0; ciCore.lat = 51.5;
  CastChart(1);
  et = NCheckEclipse(oSun, oMoo, &rPct);
  Check(et == etPartial,
    "and geocentrically the same eclipse is partial, not total (got %d)",
    et);

  us.fEclipse = fEclSav; us.fEclipseAny = fAnySav;
  us.objCenter = objCenSav; us.fTopoPos = fTopoSav;
  us.nRel = usSav.nRel; us.fProgress = usSav.fProgress;
  us.rHarmonic = usSav.rHarmonic; us.nDwad = usSav.nDwad;
  us.fNavamsa = usSav.fNavamsa; us.objOnAsc = usSav.objOnAsc;
  us.fFlip = usSav.fFlip; us.fGeodetic = usSav.fGeodetic;
  us.fSidereal = usSav.fSidereal; us.fParallel = usSav.fParallel;
  us.rZodiacOffset = usSav.rZodiacOffset; us.fDecan = usSav.fDecan;
  us.fHouse3D = usSav.fHouse3D; us.fIndian = usSav.fIndian;
  us.fEquator = usSav.fEquator; us.fEquator2 = usSav.fEquator2;
  us.nHouseSystem = usSav.nHouseSystem;
  ciCore = ciCoreSav; ciMain = ciMainSav;
  CastChart(1);
  printf("  %d solar and %d lunar eclipses match the canon\n",
    cSolar, ceclipseQt - cSolar);
}


/*
******************************************************************************
** The "-0" lockdown family, in the build that never looked at it.
******************************************************************************
*/

// "-0" is one-way by design: "_0 does nothing", so a settings file or a
// command line can lock the program down and nothing can unlock it. That
// is the whole point of the family, and it means a build that fails to
// enforce one of them is not merely inconsistent, it has a switch that
// silently does nothing at all.
//
// Two of them were exactly that here. Windows refuses WM_CLOSE under
// us.fNoQuit and forces us.fGraphics off under us.fNoGraphics; neither
// flag was referenced anywhere in qtdriver.cpp or qtdialog.cpp. Found by
// counting each flag's uses in the two GUI backends side by side, which
// is the sweep CLAUDE.md prescribes for exactly this shape -- a WIN-only
// branch with no QT in it.
//
// The others were already enforced, and deeper than the GUI: BeginFileX()
// refuses to open an output file under us.fNoWrite, so "-0o" blocks both
// "-os" and "-Xb" without either backend saying anything. Measured, both
// ways, rather than assumed.

static void TestLockdownQt()
{
  flag fNoQuitSav = us.fNoQuit, fNoGraphicsSav = us.fNoGraphics;
  flag fGraphicsSav = us.fGraphics, fPopupSav = FNoPopupQt();

  Group("Lockdown switches");
  SetNoPopupQt(fTrue);           // the refusal warns; not in a test run

  // -0X: graphics forced off before the chart is drawn, and the View
  // menu's check mark corrected with it, which is what Windows does in
  // the same breath.
  us.fNoGraphics = fFalse;
  us.fGraphics = fTrue;
  RedrawQt();
  Check(us.fGraphics, "without -0X a graphics chart stays graphics");
  us.fNoGraphics = fTrue;
  us.fGraphics = fTrue;
  RedrawQt();
  Check(!us.fGraphics, "with -0X it is turned back off before drawing");
  QAction *paGr = PaFindActionTestQt("Show &Graphics");
  Check(paGr != NULL && !paGr->isChecked(),
    "and the View menu item stops claiming graphics are on");
  us.fNoGraphics = fFalse;

  // -0q: a close is refused. Sent as an event rather than by calling
  // close(), so the half of this that is SUPPOSED to succeed does not
  // end the test run.
  if (gi.qwind != NULL) {
    QCloseEvent evClose;
    us.fNoQuit = fFalse;
    QApplication::sendEvent(gi.qwind, &evClose);
    Check(evClose.isAccepted(), "without -0q a close is accepted");
    QCloseEvent evClose2;
    us.fNoQuit = fTrue;
    QApplication::sendEvent(gi.qwind, &evClose2);
    Check(!evClose2.isAccepted(), "with -0q it is refused");
    us.fNoQuit = fFalse;
  }

  // -0o: the writers themselves refuse, which is where this one has
  // always been enforced and is why both GUI backends can leave most of
  // it alone. Both halves, because "never writes anything" would pass
  // the first on its own.
  //
  // Deliberately NOT asserted here: that CaptureTextToFileQt() refuses.
  // It does not, and neither does Windows -- cmdCopyText sits above the
  // "if (us.fNoWrite) break;" that gates cmdCopyBitmap and the four
  // vector copies, so Copy Chart Text writes its temp file under -0o in
  // both builds, and so does printing a text chart. Asserting otherwise
  // was this test's first draft, and it was the test that was wrong.
  QString strPath = QDir::tempPath() +
    QString("/astrolog-qt-lockdown-%1.as")
    .arg((int)QCoreApplication::applicationPid());
  QByteArray baPath = strPath.toLocal8Bit();
  flag fNoWriteSav = us.fNoWrite;
  int nWriteFormatSav = us.nWriteFormat;
  char *szFileOutSav = is.szFileOut;

  QFile::remove(strPath);
  us.nWriteFormat = 'd';
  is.szFileOut = (char *)baPath.constData();
  us.fNoWrite = fTrue;
  Check(!FOutputSettings() && !QFile::exists(strPath),
    "with -0o the settings writer refuses and writes nothing");
  us.fNoWrite = fFalse;
  Check(FOutputSettings() && QFileInfo(strPath).size() > 0,
    "and without it the same call writes the file");

  // And a saved file has to LOAD under the same lockdown, which it did
  // not: NSwXb() refused the ":Xb*" line the writer emits, and one
  // refusal stops the whole load, so "-0o" made a user's own settings
  // unreadable. The lockdown gates SELECTING an output file, which ":"
  // provably cannot do -- FSwitchF2() leaves gs.ft where it was -- while
  // "-Xb", which does select one, still has to be refused.
  {
    CONST char *rgsz[3];
    int ftSav = gs.ft;

    us.fNoWrite = fTrue;
    Check(FProcessSwitchFile(baPath.constData(), NULL),
      "with -0o a file the writer just saved still loads");
    rgsz[0] = szAppNameCore; rgsz[1] = "-Xb"; rgsz[2] = NULL;
    Check(!FProcessSwitches(2, (char **)rgsz, NULL) && gs.ft != ftBmp,
      "and \"-Xb\", which would select one, is still refused");
    us.fNoWrite = fFalse;
    gs.ft = ftSav;
  }
  QFile::remove(strPath);
  us.fNoWrite = fNoWriteSav;
  us.nWriteFormat = nWriteFormatSav;
  is.szFileOut = szFileOutSav;

  // -0i: the Help menu's eleven file openers and F1, which hand a file to
  // the desktop's default application. Windows routes all of them through
  // BootExternal(), which refuses under this flag; the Qt build called
  // FileOpen() inline at every site and so opened the documentation, the
  // four data files and the two ".url" shortcuts with lockdown on. Both
  // halves, because a helper that never resolved anything would pass the
  // refusal on its own.
  {
    char szPath[cchSzMax];
    flag fNoReadSav = us.fNoRead;

    us.fNoRead = fFalse;
    Check(FBootPathTestQt(DEFAULT_INFOFILE, S(szPath)),
      "without -0i the Help menu resolves a data file to open");
    us.fNoRead = fTrue;
    Check(!FBootPathTestQt(DEFAULT_INFOFILE, S(szPath)),
      "with -0i the same file is refused before it is handed to the desktop");
    us.fNoRead = fNoReadSav;
  }

  us.fNoQuit = fNoQuitSav; us.fNoGraphics = fNoGraphicsSav;
  us.fGraphics = fGraphicsSav;
  SetNoPopupQt(fPopupSav);
  RedrawQt();
  printf("  four lockdown switches enforced, both ways each\n");
}


// Copy Chart Text Output, with the output codepage set to UTF-8.
//
// Astrolog writes a byte order mark at the head of a UTF-8 file, which is
// right for a file and wrong for a capture into memory:
// QString::fromUtf8() does not strip it, so it reached the clipboard as
// an invisible U+FEFF first character, and reached
// QTextDocument::setHtml() as a stray one before "<html>" when printing a
// text chart. Both halves asserted -- the text still arrives, and it
// starts where it should -- because "no BOM" passes just as well on an
// empty clipboard.

static void TestCopyTextBomQt()
{
  int nCharsetOutSav = us.nCharsetOut;
  flag fGraphicsSav = us.fGraphics;

  Group("Copy text codepage");
  QAction *pa = PaFindActionTestQt("Copy Chart &Text Output");
  Check(pa != NULL, "Copy Chart Text Output is on the menu bar");
  if (pa == NULL)
    return;
  // Pin the chart being captured, not just "text mode". Whatever chart
  // type an earlier group left selected decides what the listing says,
  // and a relationship chart prints a different header -- so asserting
  // on the word "Astrolog" without pinning this failed in the full run
  // while passing on its own. The measured leftovers are listed above
  // TestAllMenuActionsQt().
  QVector<flag> rgfSav(cchartmode);
  int i, nRelSav = us.nRel;
  for (i = 0; i < cchartmode; i++)
    rgfSav[i] = *rgchartmode[i].pf;
  us.nRel = rcNone;
  us.fGraphics = fFalse;
  SetChartModeQt(gWheel);
  us.fGraphics = fFalse;

  for (int i = 0; i < 2; i++) {
    CONST char *szWhat = (i == 0 ? "the default codepage" : "UTF-8");
    us.nCharsetOut = (i == 0 ? ccNone : ccUTF8);
    QApplication::clipboard()->setText(QString("sentinel"));
    pa->trigger();
    QString str = QApplication::clipboard()->text();
    Check(str.contains("Astrolog"), "%s: a text chart reaches the clipboard",
      szWhat);
    Check(!str.startsWith(QChar(0xFEFF)),
      "%s: and does not start with a byte order mark", szWhat);
  }

  // The box-drawing half. Astrolog draws its text wheels out of IBM code
  // page 437 line characters when us.fAnsiChar is on, which the View
  // menu's "Colored Text" turns on -- and those are raw high bytes, not
  // UTF-8. fromUtf8() turned every box edge into U+FFFD on the clipboard
  // while the canvas showed them correctly, because TextCharQt() has
  // mapped each high byte through WchFromChIBM() all along.
  //
  // Windows has no such gap and that is why this was invisible from its
  // side: cmdCopyText hands the bytes over as CF_OEMTEXT and lets the
  // paste target convert.
  {
    flag fAnsiCharSav = us.fAnsiChar, fAnsiColorSav = us.fAnsiColor;
    flag fSmartSaveSav = us.fSmartSave;
    us.fAnsiChar = fTrue; us.fAnsiColor = fFalse;
    // Smart Save off, or there are no IBM line characters to ask about:
    // it is what strips them, on Windows and now here. This block is
    // about the ENCODING of what does get copied, so it has to reach the
    // path where something is. The pair below is the other half.
    us.fSmartSave = fFalse;
    us.nCharsetOut = ccNone;
    // gHouse, not gWheel. rgchartmode[] maps gHouse to us.fWheel and
    // gWheel to us.fListing (xscreen.cpp:1392 and 1409), so the obvious
    // reading of the two names is the wrong way round -- and a listing
    // has degree signs but no box edges, so this asked about a chart
    // that was not being drawn and failed on it. The two Copy triggers
    // above each run Action() and leave the chart on the listing, so the
    // pin has to be repeated here rather than done once at the top.
    SetChartModeQt(gHouse);
    us.fGraphics = fFalse;
    QApplication::clipboard()->setText(QString("sentinel"));
    pa->trigger();
    QString str = QApplication::clipboard()->text();
    Check(!str.contains(QChar(0xFFFD)),
      "IBM line characters survive the trip to the clipboard");
    Check(str.contains(QChar(0x2502)) || str.contains(QChar(0x2500)),
      "and arrive as real box drawing, not as something else");

    // "Export Text and Print in Intuitive Manner" (us.fSmartSave, "-YO"),
    // which is on by DEFAULT and which this build ignored: Windows turns
    // Ansi characters and Ansi colour off around Save Text and Copy Text
    // (wdriver.cpp:2777), so an exported ".txt" is text. Here it was a
    // file of "ESC[1;31m" and code page 437 box edges whenever "Colored
    // Text" was on. Both halves, since the assertions above are the
    // "off" half only for the characters.
    us.fAnsiChar = fTrue; us.fAnsiColor = fTrue;
    us.fSmartSave = fTrue;
    QApplication::clipboard()->setText(QString("sentinel"));
    pa->trigger();
    str = QApplication::clipboard()->text();
    Check(!str.isEmpty() && str != QString("sentinel"),
      "with Smart Save on the copy still happens");
    Check(!str.contains(QChar(0x2502)) && !str.contains(QChar(0x2500)),
      "and it strips the box drawing, as Windows does");
    Check(!str.contains(QChar(0x1B)),
      "and the Ansi colour escapes with it");
    us.fSmartSave = fSmartSaveSav;
    us.fAnsiChar = fAnsiCharSav; us.fAnsiColor = fAnsiColorSav;
  }

  // And the mark really is there to be stripped. Without this the pair
  // above passes on a build that never writes one, which is the same
  // assertion as no assertion.
  QString strPath = QDir::tempPath() + QString("/astrolog-qt-bom-%1.txt")
    .arg((int)QCoreApplication::applicationPid());
  QByteArray baPath = strPath.toLocal8Bit();
  char *szScreenSav = is.szFileScreen;
  us.nCharsetOut = ccUTF8;
  QFile::remove(strPath);
  CaptureTextToFileQt(baPath.constData(), fFalse);
  QFile fileT(strPath);
  QByteArray baFile;
  if (fileT.open(QIODevice::ReadOnly))
    baFile = fileT.read(8);
  fileT.close();
  Check(baFile.startsWith("\xEF\xBB\xBF"),
    "the file this captures from does begin with a UTF-8 BOM");
  QFile::remove(strPath);
  is.szFileScreen = szScreenSav;

  us.nCharsetOut = nCharsetOutSav;
  us.fGraphics = fGraphicsSav;
  us.nRel = nRelSav;
  for (i = 0; i < cchartmode; i++)
    *rgchartmode[i].pf = rgfSav[i];
  printf("  the clipboard gets the text and nothing in front of it\n");
}


/*
******************************************************************************
** What the Object Restrictions dialog's "Recall" button hands back.
******************************************************************************
*/

// The Object Restrictions dialog's "Recall" button (dbRe_YRi) restores
// from ignoreMem[], which InitRestrictions(fTrue) last stored. Windows
// stores that after reading astrolog.as and the command line;
// InitProgram() stores it before either, so a build that does not repeat
// the call hands back the COMPILED defaults instead.
//
// Startup is what is asserted, so the runner snapshots both arrays before
// any group can touch restrictions -- which also makes this independent
// of where the group sits in the table.
//
// In a plain run the two are equal either way, since astrolog.as
// restricts nothing the defaults do not. run-qt-tests.sh runs this group
// a second time with "-R Sun" on the command line, where they can only
// be equal if the store really happened after the switches.

static byte s_rgbIgnoreStartQt[objMax], s_rgbIgnoreMemStartQt[objMax];

static void TestRestrictRecallQt()
{
  int i, cDiff = 0;

  Group("Restriction recall");
  for (i = 0; i < objMax; i++)
    cDiff += (s_rgbIgnoreStartQt[i] != s_rgbIgnoreMemStartQt[i]);
  Check(cDiff == 0,
    "Recall remembers the restrictions startup ended with, not the "
    "compiled defaults (%d object(s) differ)", cDiff);
  if (cDiff > 0)
    for (i = 0; i < objMax; i++)
      if (s_rgbIgnoreStartQt[i] != s_rgbIgnoreMemStartQt[i]) {
        printf("    first at object %d (%s): startup %d, remembered %d\n",
          i, szObjName[i], s_rgbIgnoreStartQt[i], s_rgbIgnoreMemStartQt[i]);
        break;
      }
  // And when the command line restricted something, say so out loud.
  // The compiled default leaves the Sun unrestricted, and InitProgram()
  // stores the remembered set before a single switch has been read -- so
  // "startup has the Sun restricted and the remembered set does too" is
  // only reachable if the store was repeated AFTER the switches. That is
  // the whole claim, and it is what run-qt-tests.sh's "-YR 0 0 1" probe
  // exists to reach.
  if (getenv("ASTROLOG_QT_RECALL_PROBE") != NULL) {
    // The switch has to have taken, or the assertion below is about
    // nothing -- the same trap CReplaySettingsQt() documents, where a
    // filter that matches no lines reports the program lost a setting.
    Check(s_rgbIgnoreStartQt[oSun],
      "the probe's \"-R Sun\" restricted the Sun at startup");
    Check(s_rgbIgnoreMemStartQt[oSun],
      "and that reached the remembered set, which the compiled default "
      "-- Sun unrestricted, stored before any switch -- cannot explain");
  }
  printf("  the remembered set is the one startup left\n");
}


/*
******************************************************************************
** Printing.
******************************************************************************
*/

// Print was the one user-facing command with no coverage of any kind,
// because PrintChartQt() opens a QPrintDialog and a headless run has
// nobody to answer it. The render is split out now, so a QPrinter set to
// PdfFormat goes through every line of it.
//
// What is asserted is not "a file appeared" -- a blank page is a file.
// A chart is rendered into an image and that image is embedded, so a page
// with a chart on it is much larger than a page with a uniform background,
// which compresses to almost nothing. The middle case is the point:
// printing with gi.nMode UNSET has to produce the same chart, because
// DrawChartX() switches on that field and has no default case, and the
// View menu can leave it at zero while the screen still shows a chart.

static qint64 CbPrintPdfQt(CONST char *szWhy)
{
  QString strPath = QDir::tempPath() + QString("/astrolog-qt-print-%1.pdf")
    .arg((int)QCoreApplication::applicationPid());
  QByteArray ba = strPath.toLocal8Bit();

  QFile::remove(strPath);
  PrintChartToFileTestQt(ba.constData());
  qint64 cb = QFileInfo(strPath).size();
  QFile::remove(strPath);
  if (cb <= 0)
    printf("    (%s produced no PDF at all)\n", szWhy);
  return cb;
}

static void TestPrintQt()
{
  flag fGraphicsSav = us.fGraphics;
  int nModeSav = gi.nMode, nRelSav = us.nRel;
  qint64 cbChart, cbUnset, cbText;

  Group("Printing");
  QVector<flag> rgfSav(cchartmode);
  flag fColorSav = gs.fColor;
  int i;
  for (i = 0; i < cchartmode; i++)
    rgfSav[i] = *rgchartmode[i].pf;
  us.nRel = rcNone;
  us.fGraphics = fTrue;
  // Colour on: the colour-count assertion below separates a drawn chart
  // from a blank page, and a monochrome wheel draws in four colours --
  // close enough to blank to make the threshold meaningless.
  // TestAllMenuActionsQt() leaves gs.fColor clear.
  gs.fColor = fTrue;
  SetChartModeQt(gHouse);
  cbChart = CbPrintPdfQt("a wheel");
  Check(cbChart > 20000, "a graphics chart prints a page with a chart on it "
    "(%lld bytes)", (long long)cbChart);

  // The reason the guard exists. Nothing else in this suite reaches it:
  // every other route into DrawChartX() sets the mode on the way past.
  us.fGraphics = fTrue;
  gi.nMode = 0;
  cbUnset = CbPrintPdfQt("a wheel with the mode unset");
  Check(cbUnset > cbChart / 2,
    "and so does one printed with gi.nMode unset (%lld vs %lld bytes)",
    (long long)cbUnset, (long long)cbChart);

  us.fGraphics = fFalse;
  cbText = CbPrintPdfQt("a text chart");
  Check(cbText > 2000, "a text chart prints too (%lld bytes)",
    (long long)cbText);
  printf("  sizes: wheel %lld, wheel with no mode %lld, text %lld bytes\n",
    (long long)cbChart, (long long)cbUnset, (long long)cbText);

  // The premise the guard rests on, asserted rather than asserted-about:
  // DrawChartX() with gi.nMode at zero draws NOTHING. Without this the
  // assertion above passes on a build where mode 0 happens to be fine and
  // the guard is doing nothing, and nobody would know which.
  {
    QImage *pqimSav = gi.qim;
    QPainter *pqpaintSav = gi.qpaint;
    us.fGraphics = fTrue;
    gi.qim = new QImage(400, 400, QImage::Format_RGB32);
    gi.qim->fill(Qt::black);
    gi.qpaint = new QPainter(gi.qim);
    InitColors();
    gi.nScaleT = 1;
    AdjustTextScale();
    gi.nMode = 0;
    DrawChartX();
    delete gi.qpaint;
    QImage imBlank = *gi.qim;
    gi.qim->fill(Qt::black);
    gi.qpaint = new QPainter(gi.qim);
    gi.nMode = gHouse;
    DrawChartX();
    delete gi.qpaint;
    QImage imDrawn = *gi.qim;
    delete gi.qim;
    gi.qim = pqimSav; gi.qpaint = pqpaintSav;

    // Two measures, neither of which depends on colour depth or scale.
    // A count of distinct colours does: a monochrome wheel has four.
    // And pixel(0,0) is not the background here, since DrawChartX()
    // repaints it and the corner can be an outlier -- so the second
    // measure compares the two renders against each other.
    auto cColour = [](CONST QImage &im) {
      QSet<QRgb> set;
      for (int y = 0; y < im.height(); y += 4)
        for (int x = 0; x < im.width(); x += 4)
          set.insert(im.pixel(x, y));
      return set.size();
    };
    int cDiff = 0;
    for (int y = 0; y < imBlank.height(); y += 2)
      for (int x = 0; x < imBlank.width(); x += 2)
        cDiff += (imBlank.pixel(x, y) != imDrawn.pixel(x, y));
    Check(cColour(imBlank) <= 2,
      "DrawChartX() with gi.nMode unset draws nothing (%d colours)",
      cColour(imBlank));
    Check(cDiff > 100,
      "and with a real mode it draws a chart the unset one did not "
      "(%d pixels differ)", cDiff);
  }

  us.fGraphics = fGraphicsSav; gi.nMode = nModeSav; us.nRel = nRelSav;
  gs.fColor = fColorSav;
  for (i = 0; i < cchartmode; i++)
    *rgchartmode[i].pf = rgfSav[i];
  RedrawQt();
  printf("  graphics and text both print, with the mode set or not\n");
}


/*
******************************************************************************
** Open Charts in Folder.
******************************************************************************
*/

// Which files that command picks up, which it skips, and what the "Save
// Chart Info Files in Old Style Format" box does to both.
//
// With the box clear the folder is read as "*.as", case insensitively so
// that ".AS" files copied from a Windows install are seen, and Astrolog's
// own three data files are skipped by name. With it ticked the filter
// widens to every file and those three exclusions stop applying -- which
// is what OpenDir() does in shared core and what Windows' own dialog
// title advertises, "Astrolog *.as" against "Astrolog *.*". An old style
// chart file has no extension of its own, so narrowing to "*.as" while
// that box is ticked makes the folder read as empty.
//
// This does not go through the dialog, which would want a directory
// picker answered; the walk is split out for that, the same way the
// print render is.

static flag FWriteChartFileQt(CONST QString &strPath, CONST char *szName)
{
  QFile file(strPath);

  if (!file.open(QIODevice::WriteOnly))
    return fFalse;
  QByteArray ba = QString(
    "@AI800  ; Astrolog chart info.\n"
    "-qb Jun 15 1990 12:34pm ST 8W 122:19W 47:36N\n"
    "-zi \"%1\" \"Nowhere\"\n").arg(QString::fromUtf8(szName)).toLocal8Bit();
  flag f = (file.write(ba) == ba.size());
  file.close();
  return f && file.error() == QFileDevice::NoError;
}

static void TestOpenDirQt()
{
  int cciSav = is.cci, cLoaded;
  flag fOldSav = us.fWriteOld, fPopupSav = FNoPopupQt();
  CI ciCoreSav = ciCore;

  Group("Open charts in folder");
  SetNoPopupQt(fTrue);
  QString strDir = QDir::tempPath() + QString("/astrolog-qt-dir-%1")
    .arg((int)QCoreApplication::applicationPid());
  QDir().mkpath(strDir);

  // Four files, every one of them a loadable chart -- including the two
  // that are meant to be skipped, so that a skip is a decision rather
  // than a parse failure.
  flag fWrote =
    FWriteChartFileQt(strDir + "/one.as", "One") &&
    FWriteChartFileQt(strDir + "/two.AS", "Two") &&
    FWriteChartFileQt(strDir + "/" + DEFAULT_INFOFILE, "Default") &&
    FWriteChartFileQt(strDir + "/three.txt", "Three");
  Check(fWrote, "four chart files written to a scratch folder");

  QByteArray baDir = strDir.toLocal8Bit();
  is.cci = 0;
  us.fWriteOld = fFalse;
  cLoaded = COpenChartDirTestQt(baDir.constData());
  Check(cLoaded == 2,
    "only the two .as files load, case regardless (%d)", cLoaded);
  Check(is.cci == 2, "and both reached the chart list (%d)", is.cci);

  is.cci = 0;
  us.fWriteOld = fTrue;
  cLoaded = COpenChartDirTestQt(baDir.constData());
  Check(cLoaded == 4,
    "old style format widens it to every file, exclusions and all (%d)",
    cLoaded);

  QFile::remove(strDir + "/one.as");
  QFile::remove(strDir + "/two.AS");
  QFile::remove(strDir + "/" + DEFAULT_INFOFILE);
  QFile::remove(strDir + "/three.txt");
  QDir().rmdir(strDir);

  is.cci = cciSav;
  us.fWriteOld = fOldSav;
  ciCore = ciCoreSav;
  SetNoPopupQt(fPopupSav);
  printf("  the folder walk filters and skips the way the oracle does\n");
}


/*
******************************************************************************
** The extension a save dialog adds when the user types none.
******************************************************************************
*/

// Windows' OPENFILENAME appends lpstrDefExt, and DlgSaveChart sets one
// for every format it writes. Qt's getSaveFileName appends nothing --
// defaultSuffix is empty and the static convenience function does not
// expose it -- so a name typed as "mychart" was saved as a file called
// exactly that. Not merely untidy: "Open Charts in Folder" filters on
// ".as", so a chart saved without one is invisible to the command whose
// job is to find it.
//
// The cases below are the ones that decide whether the rule is right
// rather than merely present. A name that already has an extension keeps
// it, whatever it is -- a user who typed "chart.dat" meant that. And
// "has an extension" is a dot in the LAST path component: a folder named
// "charts.old" holding a file typed as "june" still gets its suffix, the
// case a naive lastIndexOf('.') gets wrong.

static void TestSaveSuffixQt()
{
  static CONST struct {
    CONST char *szIn, *szExt, *szWant, *szWhy;
  } rgt[] = {
    {"mychart",            "as",  "mychart.as",     "no extension"},
    {"mychart.as",         "as",  "mychart.as",     "already right"},
    {"mychart.dat",        "as",  "mychart.dat",    "already something"},
    {"/tmp/june",          "as",  "/tmp/june.as",   "a path"},
    {"/tmp/charts.old/june", "as", "/tmp/charts.old/june.as",
                                                    "a dotted FOLDER"},
    {"/tmp/charts.old/june.as", "as", "/tmp/charts.old/june.as",
                                                    "and one with a name"},
    {"chart",              "png", "chart.png",      "another format"},
    {"chart.",             "as",  "chart.",         "a trailing dot counts"},
    {"",                   "as",  "",               "nothing stays nothing"} };
  int i;

  Group("Save file extension");
  for (i = 0; i < (int)(sizeof(rgt)/sizeof(rgt[0])); i++) {
    QString str = StrDefaultSuffixTestQt(QString::fromUtf8(rgt[i].szIn),
      rgt[i].szExt);
    Check(str == QString::fromUtf8(rgt[i].szWant),
      "\"%s\" + %s is \"%s\" -- %s (got \"%s\")", rgt[i].szIn, rgt[i].szExt,
      rgt[i].szWant, rgt[i].szWhy, str.toLocal8Bit().constData());
  }
  printf("  a typed name gets the format's extension, and keeps its own\n");
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

  // Picking a chart type clears the two additive listings that have no
  // row in the table above. Windows does it by zeroing a raw struct
  // range that happens to contain them (wdriver.cpp:1164);
  // SetChartModeQt() walks the table, which cannot reach them, so it
  // names them. Left set they are not merely stale -- charts1.cpp
  // APPENDS each to whatever the chart printed, so an atlas dump follows
  // every chart the user picks from then on, and this port has no menu
  // item for either to turn it back off.
  flag fAtlasSav = us.fAtlasLook, fZoneSav = us.fZoneChange;
  us.fAtlasLook = us.fZoneChange = fTrue;
  SetChartModeQt(gWheel);
  Check(!us.fAtlasLook,
    "picking a chart type clears the atlas listing (-N)");
  Check(!us.fZoneChange,
    "and the time zone change listing (-Nz)");
  us.fAtlasLook = fAtlasSav; us.fZoneChange = fZoneSav;

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
  ciCore.nam = ciCore.loc = (char *)"";
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

  // One character, not its bytes. PrintSz()'s Qt branch handed the driver
  // each raw byte, so with "-Ya3" -- UTF-8, which Display Settings offers
  // as a radio button -- a chart name drew one IBM glyph per byte and
  // every column after it was off by the number of continuation bytes.
  // is.cchCol is the measurement: it counts what the text engine thinks it
  // has drawn, and the Windows branch has always stepped over the rest of
  // a multi-byte character the same way.
  //
  // is.S has to be stdout for the duration, and not because the test wants
  // output: that is the condition the branch under test is guarded by, and
  // with is.S left wherever the previous group put it PrintSz() falls
  // through to putc() on a FILE the suite no longer owns. That segfaulted
  // four groups later, which is this project's own recorded trap -- a
  // regression test that is the regression.
  {
    Borrow bChar(us.nCharset, (int)ccNone);
    Borrow bNoDisp(us.fNoDisplay, fFalse);
    Borrow bHTML(is.nHTML, 0);
    Borrow bClip(us.fClip80, fFalse);
    FILE *pfileSav = is.S;
    int nCharsetT;

    is.S = stdout;
    for (nCharsetT = ccNone; nCharsetT <= ccUTF8; nCharsetT++) {
      int cchWant = nCharsetT >= ccUTF8 ? 3 : 4;

      us.nCharset = nCharsetT;
      is.cchCol = is.cchRow = is.cchColMax = 0;
      // "aez" with an e-acute in the middle: four bytes, three characters.
      PrintSz("a\xC3\xA9z");
      Check(is.cchCol == cchWant,
        "-Ya%d draws %d cells for \"a<e-acute>z\" (got %d)",
        nCharsetT, cchWant, is.cchCol);
    }
    is.S = pfileSav;
    is.cchCol = is.cchRow = is.cchColMax = 0;
  }
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
    FILE *fileSSav = is.S;
    for (j = 0; j <= 1; j++) {
      gs.nFontTxt = j;
      remove(szOut);
      Action();
      // Action() fcloses the stream it opened on is.szFileScreen without
      // putting the caller's back; leaving it moved arms an abort in
      // whatever prints next. See the guard in NRunQtTestTableQt().
      is.S = fileSSav;
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
  // BEFORE "menu-actions", and that is a timing decision as much as a
  // tidiness one: a transit graph in the state that group leaves behind
  // takes five seconds a render rather than a tenth, and this draws
  // three. Measured at 42 s after it and 0.5 s before.
  {"chart-scroll",         TestChartScrollQt},
  {"menu-actions",         TestAllMenuActionsQt},
  {"menu-parity",          TestMenuParityQt},
  {"menu-extra",           TestMenuExtraQt},
  {"bad-input",            TestBadInputQt},
  {"forced-positions",     TestForcedPositionsQt},
  {"shared-core",          TestSharedCoreFixesQt},
  {"rulership",            TestRulershipTablesQt},
  {"esoteric-tables",      TestEsotericTablesQt},
  {"nested-include",       TestNestedIncludeQt},
  {"graphics-mode",        TestGraphicsModeSourceQt},
  {"settings-fields",      TestSettingsFieldsQt},
  {"settings-arrays",      TestSettingsArraysQt},
  {"settings-strings",     TestSettingsStringsQt},
  {"registry",             TestRegistryQt},
  {"relationship",         TestRelationshipModeQt},
  {"ephemeris-list",       TestEphemerisListQt},
  {"chart-list",           TestChartListFilterQt},
  {"info-time",            TestChartInfoTimeQt},
  {"expression-hooks",     TestExpressionHooksQt},
  {"accel-text",           TestAccelTextQt},
  {"expression-functions", TestExpressionFunctionsQt},
  {"objsel-table",         TestObjSelTableQt},
  {"timers",               TestTimerSanityQt},
  {"objsel-dialog",        TestObjSelDialogQt},
  {"objsel-parse",         TestObjSelParseQt},
  {"color-scheme",         TestColorSchemeQt},
  {"console-font",         TestConsoleFontQt},
  {"app-icon",             TestAppIconQt},
  {"dialog-buttons",       TestDialogButtonWiringQt},
  {"shared-symbols",       TestSharedSymbolBoxesQt},
  {"clear-screen",         TestClearScreenQt},
  {"text-export",          TestTextExportQt},
  {"rising-gradient",      TestRisingGradientQt},
  {"animation",            TestAnimationStateQt},
  {"menu-resync",          TestMenuResyncQt},
  {"mnemonics",            TestDialogMnemonicsQt},
  {"arrow-keys",           TestDialogArrowKeysQt},
  {"midpoint-glyph",       TestMidpointGlyphQt},
  {"objsel-lookup",        TestObjSelLookupQt},
  {"custom-parse",         TestCustomDialogParseQt},
  {"objdef-set",           TestObjDefSetQt},
  {"objsel-glyph",         TestObjSelGlyphQt},
  {"settings-roundtrip",   TestSettingsRoundTripQt},
  {"interface-settings",   TestInterfaceSettingsQt},
  {"graphics-fields",      TestGraphicsFieldsQt},
  {"chart-export",         TestChartExportQt},
  {"file-recursion",       TestFileRecursionQt},
  {"dialog-fit",           TestDialogFitQt},
  {"long-command-line",    TestLongCommandLineQt},
  {"atlas-sink",           TestAtlasSinkQt},
  {"atlas-zone",           TestAtlasZoneQt},
  {"eclipses",             TestEclipseQt},
  {"lockdown",             TestLockdownQt},
  {"chart-now",            TestChartNowQt},
  {"now-buttons",          TestNowButtonsQt},
  {"menu-side-effects",    TestMenuSideEffectsQt},
  {"graphics-size",        TestGraphicsSizeQt},
  {"font-pack",            TestFontPackQt},
  {"combo-pick",           TestComboPickQt},
  {"screen-colors",        TestScreenColorsQt},
  {"key-help",             TestKeyHelpQt},
  {"notice",               TestNoticeQt},
  {"orb-grid",             TestOrbGridQt},
  {"field-parse",          TestFieldParseQt},
  {"orbit-buffer",         TestOrbitBufferQt},
  {"atlas-apply",          TestAtlasApplyQt},
  {"copy-text-bom",        TestCopyTextBomQt},
  {"restrict-recall",      TestRestrictRecallQt},
  {"printing",             TestPrintQt},
  {"open-dir",             TestOpenDirQt},
  {"save-suffix",          TestSaveSuffixQt},
  {"chartmode-table",      TestChartModeTableQt},
  {"cast-cooking",         TestCastCookingQt},
  {"line-drawing",         TestLineDrawingQt},
  {"long-strings",         TestLongStringsQt},
  {"file-parsers",         TestFileParsersQt},
  {"oracle",               TestNumericOracleQt},
  // LAST ON PURPOSE, and the runner asserts it stays last. This group
  // opens all 25 dialogs and OKs each of them twice, which is the point
  // of it; what a dialog's OK legitimately re-applies does not all live
  // in a settings file, so reloading one puts most of the state back but
  // not the drawing tables. Run before "midpoint-glyph" it left Chiron's
  // slot carrying a glyph again, and that group -- which pins ten globals
  // by hand and inherits the eleventh -- failed on the leftovers rather
  // than on its own subject.
  {"ok-settles",           TestOkSettlesQt}};
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
  // Taken before any group runs, because what it records is a fact about
  // STARTUP and every group after this is free to change restrictions.
  // See TestRestrictRecallQt().
  CopyRgb(ignore.rgn, s_rgbIgnoreStartQt, sizeof(ignore.rgn));
  CopyRgb(ignoreMem, s_rgbIgnoreMemStartQt, sizeof(ignoreMem));
  // See the note on that entry: it disturbs state no settings file
  // carries, so anything after it inherits the disturbance and fails on
  // the leftovers. Cheaper to assert than to rediscover.
  Check(FMatchSz(rgqttestQt[cqttestQt-1].szName, "ok-settles"),
    "\"ok-settles\" is still the last group in the table (found \"%s\")",
    rgqttestQt[cqttestQt-1].szName);
  // is.S is opened by Action() and by nothing else, and Action() fcloses
  // it on the way out without putting the caller's back. A group that
  // points is.szFileScreen at a file and runs Action() therefore leaves
  // is.S on a CLOSED FILE, and the next thing that prints -- which is
  // usually a PrintProgress() several groups later, through AnsiColor()
  // and PrintSz() -- aborts the process with "glibc detected an invalid
  // stdio handle".
  //
  // This has now cost three hunts. CLAUDE.md records the first ("A
  // regression test can be the regression"), work log item 165 the
  // second, and item 164 spent its effort on the wireframe writer because
  // the last line before the abort was the last UNBUFFERED line rather
  // than the last thing that happened. It is intermittent in a normal
  // build because a freed FILE often still looks usable; under
  // AddressSanitizer it is every run, which is how the third one was
  // caught, in one gdb backtrace.
  //
  // So stop diagnosing it and detect it: remember what is.S was before
  // the table, and after every group say WHICH GROUP moved it. Repaired
  // as well as reported, because one leak should not take the rest of the
  // run down with it.
  FILE *fileSStart = is.S;
  for (i = 0; i < cqttestQt; i++) {
    if (!FTestWantedQt(szFilter, rgqttestQt[i].szName))
      continue;
    cRun++;
    timerTest.start();
    rgqttestQt[i].pfn();
    if (is.S != fileSStart) {
      Check(fFalse, "group \"%s\" left is.S on another stream; "
        "Action() has closed it and the next print would abort",
        rgqttestQt[i].szName);
      is.S = fileSStart;
    }
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
  if (getenv("QTSHOTDIR") != NULL) {
    DialogShotCaptureQt(getenv("QTSHOTDIR"));
    return fFalse;
  }
  s_nAnimStartQt = gs.nAnim;
  return NRunQtTestTableQt();
}

#endif // QTTEST

/* qttest.cpp */
