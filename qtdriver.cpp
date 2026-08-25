/*
** Astrolog (Version 8.00) File: qtdriver.cpp
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
** This file implements the Qt Linux GUI backend's main window, chart
** canvas, menu bar, and event loop entry points (BeginQt/InteractQt/
** EndQt), called from xscreen.cpp's BeginX()/FActionX()/EndX() the same
** way the X11 backend calls into its own XNextEvent() based routines.
** Unlike X11 (or WIN), Qt drives its own event loop, so this does not
** reuse InteractX()'s manual keystroke dispatch; the menu bar built here
** and the dialogs in qtdialog.cpp are the "proper" GUI configuration
** surface the X11 backend never had.
**
** Last code change made 8/24/2026.
*/

// All Qt headers this file needs must be included before astrolog.h, since
// astrolog.h defines several single word macros (META, PS, TIME, etc) that
// collide with identifiers used inside Qt's own headers.
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QMenu>
#include <QtWidgets/QAction>
#include <QtWidgets/QActionGroup>
#include <QtWidgets/QMessageBox>
#include <QtGui/QResizeEvent>
#include <QtGui/QPaintEvent>
#include <QtGui/QDesktopServices>
#include <QtCore/QUrl>

#include "astrolog.h"
#include "qtdriver.h"

#ifdef QT

// Is the chart window fully set up? Guards against a resize event arriving
// (e.g. during initial widget layout) before there is a chart to redraw.
static bool fQtReady = false;


// The widget the chart is actually painted onto. Astrolog keeps rendering
// into an off screen buffer (gi.qim, the Qt analog of X11's Pixmap) via the
// Draw*() primitives in xgeneral.cpp; this widget's only job is to blit
// that buffer to the screen, and to tell Astrolog when its size changes.

class ChartCanvas : public QWidget
{
public:
  ChartCanvas(QWidget *parent = NULL) : QWidget(parent) { }

protected:
  // Qt's resize/show calls made before the event loop is running (in
  // BeginQt()) don't synchronously resize this widget -- that only happens
  // once events start being processed, which for the very first frame is
  // partway through InteractQt()'s own initial RedrawQt() call. So rather
  // than trust resizeEvent() to always fire before the chart is redrawn,
  // paintEvent() double checks the buffer still matches this widget's
  // actual current size, and redraws first if not.
  void paintEvent(QPaintEvent *) override
  {
    if (fQtReady && width() >= 1 && height() >= 1 &&
      (gi.qim == NULL || gi.qim->width() != width() ||
      gi.qim->height() != height())) {
      gs.xWin = width();
      gs.yWin = height();
      RedrawQt();
    }
    QPainter p(this);
    if (gi.qim != NULL)
      p.drawImage(0, 0, *gi.qim);
  }

  void resizeEvent(QResizeEvent *pevent) override
  {
    QWidget::resizeEvent(pevent);
    update();
  }
};


// Redraw the chart into the off screen buffer at the chart's current size,
// and repaint the canvas widget with the result. Called after any change
// that affects only how the chart looks (e.g. colors), and after a resize.

void RedrawQt()
{
  if (gi.qim != NULL) {
    delete gi.qim;
    gi.qim = NULL;
  }
  if (gs.xWin < 1)
    gs.xWin = 1;
  if (gs.yWin < 1)
    gs.yWin = 1;
  gi.qim = new QImage(gs.xWin, gs.yWin, QImage::Format_RGB32);
  gi.qim->fill(Qt::black);
  gi.qpaint = new QPainter(gi.qim);
  DrawChartX();
  delete gi.qpaint;
  gi.qpaint = NULL;
  if (gi.qcanvas != NULL)
    gi.qcanvas->update();
}


// Recompute the chart positions from the current chart info, then redraw.
// Called after a change that affects what gets cast, such as editing chart
// info, orbs, or object restrictions.

void RecastAndRedrawQt()
{
  CastChart(0);
  RedrawQt();
}


// Add a checkbox style menu item bound directly to a flag field (us.fXxx or
// similar). Reflects *pfield's state when built, flips it and updates its
// own checked state on click, then applies the change. This is the pattern
// most of Windows' menu toggle commands follow (see e.g. cmdSidereal,
// wdriver.cpp:1611) -- one reusable helper here instead of hand written
// code per item.

static QAction *AddToggleAction(QMenu *pmenu, CONST char *szLabel,
  flag *pfield, flag fRecast)
{
  QAction *pa = pmenu->addAction(szLabel);
  pa->setCheckable(true);
  pa->setChecked(*pfield != 0);
  QObject::connect(pa, &QAction::triggered, pa, [pfield, pa, fRecast]() {
    *pfield = !*pfield;
    pa->setChecked(*pfield != 0);
    if (fRecast)
      RecastAndRedrawQt();
    else
      RedrawQt();
  });
  return pa;
}


// Add a radio style menu item: one of a mutually exclusive set of values
// that gets written into *ptarget. "pgroup" ties all the options for one
// field together into an exclusive group; the caller creates one QActionGroup
// per field and passes it to every option for that field.

static QAction *AddSelectAction(QMenu *pmenu, QActionGroup *pgroup,
  CONST char *szLabel, int value, int *ptarget, flag fRecast)
{
  QAction *pa = pmenu->addAction(szLabel);
  pa->setCheckable(true);
  pa->setChecked(*ptarget == value);
  pa->setActionGroup(pgroup);
  QObject::connect(pa, &QAction::triggered, pa, [value, ptarget, fRecast]() {
    *ptarget = value;
    if (fRecast)
      RecastAndRedrawQt();
    else
      RedrawQt();
  });
  return pa;
}


// The Chart menu's chart-type radio items, tracked separately from ordinary
// AddSelectAction groups because chart mode can also change from outside
// the menu (the Transits dialog) -- SetChartModeQt() looks up the action
// matching whatever mode was just applied and checks it, regardless of who
// called it. Sized generously; only 16 slots are used as of this writing.

static QAction *s_rgpaChartMode[64];
static int s_rgnChartMode[64];
static int s_cChartMode = 0;

// Shared across the Chart menu's 16 chart type items and the Graphics
// menu's 5 sphere/globe/map view items -- Windows treats chart type as one
// unified radio state (wi.cmdCur/rgcmdMode) no matter which menu changed
// it, so all 21 items here belong to the same exclusive group.
static QActionGroup *s_pgroupChartMode = NULL;

static QAction *AddChartModeAction(QMenu *pmenu, CONST char *szLabel,
  int mode)
{
  QAction *pa = pmenu->addAction(szLabel);
  pa->setCheckable(true);
  pa->setActionGroup(s_pgroupChartMode);
  QObject::connect(pa, &QAction::triggered, pa, [mode]() {
    SetChartModeQt(mode);
  });
  s_rgpaChartMode[s_cChartMode] = pa;
  s_rgnChartMode[s_cChartMode] = mode;
  s_cChartMode++;
  return pa;
}


// Switch chart type/mode, mirroring the chart-type switch in Windows'
// ProcessState() (wdriver.cpp:1143-1201): clear every chart-type flag, then
// set the one matching the new mode. See qtdriver.h for the full comment.

void SetChartModeQt(int mode)
{
  int i;

  us.fListing = us.fWheel = us.fGrid = us.fAspList = us.fMidpoint =
    us.fHorizon = us.fOrbit = us.fSector = us.fCalendar = us.fInfluence =
    us.fEsoteric = us.fAstroGraph = us.fEphemeris = us.fArabic =
    us.fHorizonSearch = us.fAtlasNear = us.fInDay = us.fInDayInf =
    us.fInDayGra = us.fTransit = us.fTransitInf = us.fTransitGra = fFalse;
  // DrawChartX() switches directly on gi.nMode with no fallback if it's 0,
  // and DetectGraphicsChartMode() (xscreen.cpp:2165, normally what
  // (re)derives gi.nMode from the us.f* flags before a redraw) doesn't
  // cover several of these flags (fListing, fAspList, fArabic among them)
  // -- so rather than zero gi.nMode and rely on that detection like
  // Windows' ProcessState() does, set it directly to what was actually
  // selected, since that's already known here.
  gi.nMode = mode;
  switch (mode) {
  case gWheel:      us.fListing       = fTrue; break;
  case gHouse:      us.fWheel         = fTrue; break;
  case gGrid:       us.fGrid          = fTrue; break;
  case gAspect:     us.fAspList       = fTrue; break;
  case gMidpoint:   us.fMidpoint      = fTrue; break;
  case gHorizon:    us.fHorizon       = fTrue; break;
  case gOrbit:      us.fOrbit         = fTrue; break;
  case gSector:     us.fSector        = fTrue; break;
  case gCalendar:   us.fCalendar      = fTrue; break;
  case gDisposit:   us.fInfluence     = fTrue; break;
  case gEsoteric:   us.fEsoteric      = fTrue; break;
  case gAstroGraph: us.fAstroGraph    = fTrue; break;
  case gEphemeris:  us.fEphemeris     = fTrue; break;
  case gArabic:     us.fArabic        = fTrue; break;
  case gRising:     us.fHorizonSearch = fTrue; break;
  case gLocal:      us.fAtlasNear     = fTrue; break;
  case gTraTraTim:  us.fInDay         = fTrue; break;
  case gTraTraInf:  us.fInDayInf      = fTrue; break;
  case gTraTraGra:  us.fInDayGra      = fTrue; break;
  case gTraNatTim:  us.fTransit       = fTrue; break;
  case gTraNatInf:  us.fTransitInf    = fTrue; break;
  case gTraNatGra:  us.fTransitGra    = fTrue; break;
  }
  for (i = 0; i < s_cChartMode; i++)
    if (s_rgnChartMode[i] == mode) {
      s_rgpaChartMode[i]->setChecked(true);
      break;
    }
  RedrawQt();
}


// Switch relationship chart type, mirroring Windows' SetRel()
// (wdriver.cpp:267-281).

static QAction *s_rgpaRel[16];
static int s_rgnRel[16];
static int s_cRel = 0;

void SetRelQt(int rc)
{
  CI ciT;
  int i;

  if (us.nRel == rcMidpoint) {  // Restore chart when leaving midpoint mode.
    ciT = ciMain;
    ciCore = ciMain = ciSave;
    ciSave = ciT;
  }
  if (rc == rcMidpoint)         // Remember chart so it can be restored.
    ciSave = ciMain;
  us.nRel = rc;
  for (i = 0; i < s_cRel; i++)
    if (s_rgnRel[i] == rc) {
      s_rgpaRel[i]->setChecked(true);
      break;
    }
  RecastAndRedrawQt();
}

static QAction *AddRelAction(QMenu *pmenu, QActionGroup *pgroup,
  CONST char *szLabel, int rc)
{
  QAction *pa = pmenu->addAction(szLabel);
  pa->setCheckable(true);
  pa->setActionGroup(pgroup);
  pa->setChecked(us.nRel == rc);
  QObject::connect(pa, &QAction::triggered, pa, [rc]() { SetRelQt(rc); });
  s_rgpaRel[s_cRel] = pa;
  s_rgnRel[s_cRel] = rc;
  s_cRel++;
  return pa;
}


static void BuildFileMenu(QMainWindow *pwind)
{
  QMenu *pmenu = pwind->menuBar()->addMenu("&File");
  QAction *paOpen = pmenu->addAction("&Open Chart...");
  QObject::connect(paOpen, &QAction::triggered, pwind,
    []() { ShowOpenChartDialogQt(); });
  QAction *paSave = pmenu->addAction("&Save Chart...");
  QObject::connect(paSave, &QAction::triggered, pwind,
    []() { ShowSaveChartDialogQt(); });
  pmenu->addSeparator();
  QAction *paQuit = pmenu->addAction("&Quit");
  QObject::connect(paQuit, &QAction::triggered, pwind,
    [pwind]() { pwind->close(); });
}


static void BuildViewMenu(QMainWindow *pwind)
{
  QMenu *pmenu = pwind->menuBar()->addMenu("&View");
  QAction *paGraphics = AddToggleAction(pmenu, "Show &Graphics",
    &us.fGraphics, fFalse);
  QAction *paColorText = pmenu->addAction("&Colored Text");
  paColorText->setCheckable(true);
  paColorText->setChecked(us.fAnsiColor != 0);
  QObject::connect(paColorText, &QAction::triggered, pwind,
    [paColorText, paGraphics]() {
      us.fAnsiColor = !us.fAnsiColor;
      us.fAnsiChar = !us.fAnsiChar;
      paColorText->setChecked(us.fAnsiColor != 0);
      us.fGraphics = fFalse;
      paGraphics->setChecked(fFalse);
      RedrawQt();
    });
  QAction *paRedraw = pmenu->addAction("&Redraw Screen");
  QObject::connect(paRedraw, &QAction::triggered, pwind,
    []() { RedrawQt(); });
  QAction *paColors = pmenu->addAction("Set &Colors...");
  QObject::connect(paColors, &QAction::triggered, pwind,
    []() { ShowColorDialogQt(); });
  pmenu->addSeparator();
  QAction *paInterpret = pmenu->addAction("Show &Interpretations");
  paInterpret->setCheckable(true);
  paInterpret->setChecked(us.fInterpret != 0);
  QObject::connect(paInterpret, &QAction::triggered, pwind,
    [paInterpret, paGraphics]() {
      us.fInterpret = !us.fInterpret;
      paInterpret->setChecked(us.fInterpret != 0);
      us.fGraphics = fFalse;
      paGraphics->setChecked(fFalse);
      RedrawQt();
    });
  AddToggleAction(pmenu, "Print Nearest &Second", &us.fSeconds, fFalse);
  AddToggleAction(pmenu, "&Parallel Aspects", &us.fParallel, fFalse);
  AddToggleAction(pmenu, "&Applying Aspects", &us.nAppSep, fFalse);
}


static void BuildInfoMenu(QMainWindow *pwind)
{
  QMenu *pmenu = pwind->menuBar()->addMenu("&Info");
  QAction *paInfo = pmenu->addAction("&Set Chart Info...");
  QObject::connect(paInfo, &QAction::triggered, pwind,
    []() { ShowChartInfoDialogQt(); });
  QAction *paNow = pmenu->addAction("Chart for &Now");
  QObject::connect(paNow, &QAction::triggered, pwind, []() {
    FInputData(szNowCore);
    RecastAndRedrawQt();
  });
  QAction *paDefault = pmenu->addAction("&Default Chart Info...");
  QObject::connect(paDefault, &QAction::triggered, pwind,
    []() { ShowDefaultInfoDialogQt(); });
  pmenu->addSeparator();

  QActionGroup *pgroup = new QActionGroup(pwind);
  AddRelAction(pmenu, pgroup, "&No Relationship Chart", rcNone);
  AddRelAction(pmenu, pgroup, "&Comparison Chart", rcDual);
  AddRelAction(pmenu, pgroup, "&Synastry Chart", rcSynastry);
  AddRelAction(pmenu, pgroup, "Co&mposite Chart", rcComposite);
  AddRelAction(pmenu, pgroup, "&Time Space Midpoint Chart", rcMidpoint);
  pmenu->addSeparator();
  AddRelAction(pmenu, pgroup, "&Date Difference Chart", rcDifference);
  AddRelAction(pmenu, pgroup, "&Biorhythm Chart", rcBiorhythm);
  AddRelAction(pmenu, pgroup, "&Transit and Natal", rcTransit);
  AddRelAction(pmenu, pgroup, "&Progressed and Natal", rcProgress);
}


// "Include <category>" toggles (Minors/Cusps/Uranians/Dwarfs/Fixed Stars/
// Moons/Body Centers), mirroring wdriver.cpp:1737-1802. Most of these flip
// a backing `us.f*` flag, then either hide every object in [lo, hi] (flag
// turned off) or flip each one individually back (flag turned on) -- since
// turning off always sets every ignore[i] to true, flipping them again on
// re-enable is equivalent to "restore all to shown", not a real memory of
// prior per-object state. Minors (pfield NULL) has no backing flag at all
// in Windows either -- it just flips each ignore[i] in range directly
// (skipping "except", used to exclude oNod from the Minors range).

static QAction *AddCategoryRestrictAction(QMenu *pmenu, CONST char *szLabel,
  flag *pfield, int lo, int hi, int except)
{
  QAction *pa = pmenu->addAction(szLabel);
  pa->setCheckable(true);
  pa->setChecked(pfield != NULL ? *pfield != 0 : !ignore[lo]);
  QObject::connect(pa, &QAction::triggered, pa,
    [pfield, pa, lo, hi, except]() {
      int i;
      if (pfield != NULL) {
        *pfield = !*pfield;
        pa->setChecked(*pfield != 0);
        for (i = lo; i <= hi; i++)
          ignore[i] = !*pfield || !ignore[i];
      } else {
        for (i = lo; i <= hi; i++)
          if (i != except)
            ignore[i] = !ignore[i];
        pa->setChecked(!ignore[lo]);
      }
      AdjustRestrictions();
      RecastAndRedrawQt();
    });
  return pa;
}


// Tracked at file scope so SyncHelioMenuQt() can refresh this checkmark
// after the Calculation Settings dialog changes the central planet too.
static QAction *s_paHelio = NULL;

void SyncHelioMenuQt()
{
  if (s_paHelio != NULL)
    s_paHelio->setChecked(us.objCenter != oEar);
}

static void BuildSettingMenu(QMainWindow *pwind)
{
  QMenu *pmenu = pwind->menuBar()->addMenu("&Setting");
  AddToggleAction(pmenu, "&Sidereal Zodiac", &us.fSidereal, fTrue);
  s_paHelio = pmenu->addAction("&Heliocentric");
  s_paHelio->setCheckable(true);
  s_paHelio->setChecked(us.objCenter != oEar);
  QObject::connect(s_paHelio, &QAction::triggered, pwind, []() {
    SetCentric(us.objCenter == oEar ? oSun : oEar);
    SyncHelioMenuQt();
    RecastAndRedrawQt();
  });

  QMenu *pmenuHouse = pmenu->addMenu("&House System");
  QActionGroup *pgroupHouse = new QActionGroup(pwind);
  AddSelectAction(pmenuHouse, pgroupHouse, "&Placidus", 0,
    &us.nHouseSystem, fTrue);
  AddSelectAction(pmenuHouse, pgroupHouse, "&Koch", 1,
    &us.nHouseSystem, fTrue);
  AddSelectAction(pmenuHouse, pgroupHouse, "C&ampanus", 3,
    &us.nHouseSystem, fTrue);
  AddSelectAction(pmenuHouse, pgroupHouse, "&Regiomontanus", 5,
    &us.nHouseSystem, fTrue);
  AddSelectAction(pmenuHouse, pgroupHouse, "&Topocentric", 8,
    &us.nHouseSystem, fTrue);
  AddSelectAction(pmenuHouse, pgroupHouse, "A&lcabitius", 9,
    &us.nHouseSystem, fTrue);
  AddSelectAction(pmenuHouse, pgroupHouse, "K&rusinski", 10,
    &us.nHouseSystem, fTrue);
  AddSelectAction(pmenuHouse, pgroupHouse, "A.P.&C.", 18,
    &us.nHouseSystem, fTrue);
  AddSelectAction(pmenuHouse, pgroupHouse, "&Savard-A", 21,
    &us.nHouseSystem, fTrue);
  pmenuHouse->addSeparator();
  AddSelectAction(pmenuHouse, pgroupHouse, "P&orphyry", 6,
    &us.nHouseSystem, fTrue);
  AddSelectAction(pmenuHouse, pgroupHouse, "Pullen (S.&Ratio)", 12,
    &us.nHouseSystem, fTrue);
  AddSelectAction(pmenuHouse, pgroupHouse, "Pullen (S.&Delta)", 13,
    &us.nHouseSystem, fTrue);
  pmenuHouse->addSeparator();
  AddSelectAction(pmenuHouse, pgroupHouse, "&Meridian", 4,
    &us.nHouseSystem, fTrue);
  AddSelectAction(pmenuHouse, pgroupHouse, "M&orinus", 7,
    &us.nHouseSystem, fTrue);
  AddSelectAction(pmenuHouse, pgroupHouse, "Hori&zon", 17,
    &us.nHouseSystem, fTrue);
  AddSelectAction(pmenuHouse, pgroupHouse, "&Carter P.Equat.", 19,
    &us.nHouseSystem, fTrue);
  AddSelectAction(pmenuHouse, pgroupHouse, "S&unshine", 20,
    &us.nHouseSystem, fTrue);
  AddSelectAction(pmenuHouse, pgroupHouse, "S&ripati", 16,
    &us.nHouseSystem, fTrue);
  pmenuHouse->addSeparator();
  AddSelectAction(pmenuHouse, pgroupHouse, "&Equal", 2,
    &us.nHouseSystem, fTrue);
  AddSelectAction(pmenuHouse, pgroupHouse, "Equal (&MC)", 11,
    &us.nHouseSystem, fTrue);
  AddSelectAction(pmenuHouse, pgroupHouse, "&Whole", 14,
    &us.nHouseSystem, fTrue);
  AddSelectAction(pmenuHouse, pgroupHouse, "&Vedic", 15,
    &us.nHouseSystem, fTrue);
  AddSelectAction(pmenuHouse, pgroupHouse, "N&ull", 22,
    &us.nHouseSystem, fTrue);

  QMenu *pmenuHouseSet = pmenu->addMenu("House S&ettings");
  QAction *paSolar = pmenuHouseSet->addAction("&Solar Chart");
  paSolar->setCheckable(true);
  paSolar->setChecked(us.objOnAsc != 0);
  QObject::connect(paSolar, &QAction::triggered, pwind, [paSolar]() {
    us.objOnAsc = us.objOnAsc ? 0 : oSun+1;
    paSolar->setChecked(us.objOnAsc != 0);
    RecastAndRedrawQt();
  });
  AddToggleAction(pmenuHouseSet, "&3D Houses", &us.fHouse3D, fTrue);
  pmenuHouseSet->addSeparator();
  AddToggleAction(pmenuHouseSet, "Show &Decans", &us.fDecan, fTrue);
  AddToggleAction(pmenuHouseSet, "Show D&wads", &us.nDwad, fTrue);
  AddToggleAction(pmenuHouseSet, "&Flip Signs with Houses", &us.fFlip, fTrue);
  AddToggleAction(pmenuHouseSet, "&Geodetic Houses", &us.fGeodetic, fTrue);
  pmenuHouseSet->addSeparator();
  AddToggleAction(pmenuHouseSet, "&Indian Wheel Order", &us.fIndian, fFalse);
  AddToggleAction(pmenuHouseSet, "Show &Navamsas", &us.fNavamsa, fTrue);

  QAction *paAspect = pmenu->addAction("&Aspect Settings...");
  QObject::connect(paAspect, &QAction::triggered, pwind,
    []() { ShowAspectDialogQt(); });
  QAction *paObject = pmenu->addAction("&Object Settings...");
  QObject::connect(paObject, &QAction::triggered, pwind,
    []() { ShowObjectDialogQt(); });
  QAction *paObject2 = pmenu->addAction("&More Object Settings...");
  QObject::connect(paObject2, &QAction::triggered, pwind,
    []() { ShowObject2DialogQt(); });
  pmenu->addSeparator();

  QAction *paRestrict = pmenu->addAction("&Restrictions...");
  QObject::connect(paRestrict, &QAction::triggered, pwind,
    []() { ShowRestrictDialogQt(); });
  QAction *paStarRestrict = pmenu->addAction("Star Restr&ictions...");
  QObject::connect(paStarRestrict, &QAction::triggered, pwind,
    []() { ShowStarRestrictDialogQt(); });
  QAction *paTransitRestrict = pmenu->addAction("&Transit Restrictions...");
  QObject::connect(paTransitRestrict, &QAction::triggered, pwind,
    []() { ShowTransitRestrictDialogQt(); });
  pmenu->addSeparator();

  AddCategoryRestrictAction(pmenu, "Include &Minors", NULL, oChi, oEP,
    oNod);
  AddCategoryRestrictAction(pmenu, "Include &Cusps", &us.fCusp, cuspLo,
    cuspHi, -1);
  AddCategoryRestrictAction(pmenu, "Include &Uranians", &us.fUranian,
    uranLo, uranHi, -1);
  AddCategoryRestrictAction(pmenu, "Include D&warfs", &us.fDwarf, dwarfLo,
    dwarfHi, -1);
  AddCategoryRestrictAction(pmenu, "Include &Fixed Stars", &us.fStar,
    starLo, starHi, -1);
  AddCategoryRestrictAction(pmenu, "Include &Moons", &us.fMoons, moonsLo,
    moonsHi, -1);
  AddCategoryRestrictAction(pmenu, "Include &Body Centers (COB)", &us.fCOB,
    cobLo, cobHi, -1);
  pmenu->addSeparator();

  QAction *paCalc = pmenu->addAction("&Calculation Settings...");
  QObject::connect(paCalc, &QAction::triggered, pwind,
    []() { ShowCalcDialogQt(); });
  QAction *paDisplay = pmenu->addAction("&Display Settings...");
  QObject::connect(paDisplay, &QAction::triggered, pwind,
    []() { ShowDisplayDialogQt(); });
}


static void BuildChartMenu(QMainWindow *pwind)
{
  QMenu *pmenu = pwind->menuBar()->addMenu("&Chart");
  s_pgroupChartMode = new QActionGroup(pwind);
  AddChartModeAction(pmenu, "Standard &Radix", gWheel);
  AddChartModeAction(pmenu, "&House Wheel", gHouse);
  AddChartModeAction(pmenu, "Aspect &Midpoint Grid", gGrid);
  AddChartModeAction(pmenu, "Aspect &List", gAspect);
  AddChartModeAction(pmenu, "M&idpoint List", gMidpoint);
  AddChartModeAction(pmenu, "Local &Horizon", gHorizon);
  AddChartModeAction(pmenu, "Solar System &Orbit", gOrbit);
  AddChartModeAction(pmenu, "&Gauquelin Sectors", gSector);
  AddChartModeAction(pmenu, "&Calendar", gCalendar);
  AddChartModeAction(pmenu, "&Influence", gDisposit);
  AddChartModeAction(pmenu, "&Esoteric", gEsoteric);
  AddChartModeAction(pmenu, "&Astrocartography", gAstroGraph);
  AddChartModeAction(pmenu, "&Ephemeris", gEphemeris);
  AddChartModeAction(pmenu, "Ara&bic Parts", gArabic);
  AddChartModeAction(pmenu, "R&ising and Setting", gRising);
  AddChartModeAction(pmenu, "&Nearest Cities", gLocal);
  // The chart starts on the standard radix; reflect that in the menu.
  s_rgpaChartMode[0]->setChecked(true);
  pmenu->addSeparator();

  QAction *paTransit = pmenu->addAction("&Transits...");
  QObject::connect(paTransit, &QAction::triggered, pwind,
    []() { ShowTransitDialogQt(); });
  QAction *paProgress = pmenu->addAction("&Progressions...");
  QObject::connect(paProgress, &QAction::triggered, pwind,
    []() { ShowProgressDialogQt(); });
  pmenu->addSeparator();
  QAction *paSettings = pmenu->addAction("Chart &Settings...");
  QObject::connect(paSettings, &QAction::triggered, pwind,
    []() { ShowChartSettingsDialogQt(); });
}


// A curated subset of Windows' Graphics menu (wdriver.cpp cmdGraphics*
// handlers): the 5 sphere/globe/map view types, the most broadly useful
// toggles, and the Scribble Color submenu. Skipped for this pass: Square
// Screen (ties to Windows' resizable-window model, no clean equivalent
// given the Qt canvas always auto-fits), Map Orientation (rotate/tilt/zoom
// -- mode-dependent math not worth replicating without the 3D globe modes
// fully fleshed out first), Indian Style Charts, Constellations/
// Constellation Lines/Detailed World Map (niche, and some depend on
// Windows-only state), Modify Display/Chart, Graphics Settings dialog.

static void BuildGraphicsMenu(QMainWindow *pwind)
{
  QMenu *pmenu = pwind->menuBar()->addMenu("&Graphics");
  AddChartModeAction(pmenu, "Draw Chart Sp&here", gSphere);
  AddChartModeAction(pmenu, "Draw &World Map", gWorldMap);
  AddChartModeAction(pmenu, "Draw &Globe", gGlobe);
  AddChartModeAction(pmenu, "Draw &Polar Globe", gPolar);
  AddChartModeAction(pmenu, "Draw &Telescope", gTelescope);
  pmenu->addSeparator();

  QAction *paReverse = pmenu->addAction("&Reverse Background");
  paReverse->setCheckable(true);
  paReverse->setChecked(gs.fInverse != 0);
  QObject::connect(paReverse, &QAction::triggered, pwind, [paReverse]() {
    gs.fInverse = !gs.fInverse;
    paReverse->setChecked(gs.fInverse != 0);
    InitColorPalette(gs.fInverse);
    RedrawQt();
  });
  QAction *paMono = pmenu->addAction("&Monochrome");
  paMono->setCheckable(true);
  paMono->setChecked(!gs.fColor);
  QObject::connect(paMono, &QAction::triggered, pwind, [paMono]() {
    gs.fColor = !gs.fColor;
    paMono->setChecked(!gs.fColor);
    us.fGraphics = fTrue;
    RedrawQt();
  });

  QMenu *pmenuScale = pmenu->addMenu("Character &Scale");
  QActionGroup *pgroupScale = new QActionGroup(pwind);
  AddSelectAction(pmenuScale, pgroupScale, "&Small", 100, &gs.nScale, fFalse);
  AddSelectAction(pmenuScale, pgroupScale, "&Medium", 200, &gs.nScale, fFalse);
  AddSelectAction(pmenuScale, pgroupScale, "&Large", 300, &gs.nScale, fFalse);
  AddSelectAction(pmenuScale, pgroupScale, "&Huge", 400, &gs.nScale, fFalse);
  pmenuScale->addSeparator();
  QAction *paScaleDn = pmenuScale->addAction("&Decrease");
  QObject::connect(paScaleDn, &QAction::triggered, pwind, []() {
    if (gs.nScale > 100) { gs.nScale -= 100; RedrawQt(); }
  });
  QAction *paScaleUp = pmenuScale->addAction("&Increase");
  QObject::connect(paScaleUp, &QAction::triggered, pwind, []() {
    if (gs.nScale < MAXSCALE) { gs.nScale += 100; RedrawQt(); }
  });
  pmenuScale->addSeparator();
  QAction *paTextDn = pmenuScale->addAction("Decrease &Text");
  QObject::connect(paTextDn, &QAction::triggered, pwind, []() {
    if (gs.nScaleText > 100) {
      gs.nScaleText -= 50; gs.fAutoScale = fFalse; RedrawQt();
    }
  });
  QAction *paTextUp = pmenuScale->addAction("&Increase Text");
  QObject::connect(paTextUp, &QAction::triggered, pwind, []() {
    if (gs.nScaleText < MAXSCALE) {
      gs.nScaleText += 50; gs.fAutoScale = fFalse; RedrawQt();
    }
  });

  QMenu *pmenuEffects = pmenu->addMenu("&Chart Effects");
  AddToggleAction(pmenuEffects, "Show &Border", &gs.fBorder, fFalse);
  AddToggleAction(pmenuEffects, "Show Chart &Info", &gs.fText, fFalse);
  QAction *paSidebar = pmenuEffects->addAction("Show Info &Sidebar");
  paSidebar->setCheckable(true);
  paSidebar->setChecked(gs.fDoSidebar != 0);
  QObject::connect(paSidebar, &QAction::triggered, pwind, [paSidebar]() {
    gs.fDoSidebar = !gs.fDoSidebar;
    paSidebar->setChecked(gs.fDoSidebar != 0);
    if (gs.fDoSidebar)
      gs.fText = fTrue;
    RedrawQt();
  });
  pmenuEffects->addSeparator();
  AddToggleAction(pmenuEffects, "&Thicker Lines", &gs.fThick, fFalse);
  AddToggleAction(pmenuEffects, "&Antialias Lines", &gs.fAntialias, fFalse);
  AddToggleAction(pmenuEffects, "Show Glyph &Labels", &gs.fLabel, fFalse);
  AddToggleAction(pmenuEffects, "Show &Glyphs on Aspect Lines",
    &gs.fLabelAsp, fFalse);

  QMenu *pmenuMap = pmenu->addMenu("Map &Effects");
  AddToggleAction(pmenuMap, "Show Full &Star List", &gs.fAllStar, fFalse);
  AddToggleAction(pmenuMap, "Show E&xoplanets", &gs.fAllExo, fFalse);
  pmenuMap->addSeparator();
  AddToggleAction(pmenuMap, "Show &House Details", &gs.fHouseExtra, fTrue);
  AddToggleAction(pmenuMap, "Show &Equator", &gs.fEquator, fFalse);
  AddToggleAction(pmenuMap, "Show C&ities", &gs.fLabelCity, fFalse);
  pmenuMap->addSeparator();
  AddToggleAction(pmenuMap, "Use Ecliptic &Axis", &gs.fEcliptic, fFalse);
  pmenu->addSeparator();

  QMenu *pmenuPen = pmenu->addMenu("Scribb&le Color");
  QActionGroup *pgroupPen = new QActionGroup(pwind);
  AddSelectAction(pmenuPen, pgroupPen, "Blac&k", 0, &gi.kiPen, fFalse);
  AddSelectAction(pmenuPen, pgroupPen, "&White", 15, &gi.kiPen, fFalse);
  AddSelectAction(pmenuPen, pgroupPen, "&Red", 9, &gi.kiPen, fFalse);
  AddSelectAction(pmenuPen, pgroupPen, "&Green", 10, &gi.kiPen, fFalse);
  AddSelectAction(pmenuPen, pgroupPen, "&Blue", 12, &gi.kiPen, fFalse);
  AddSelectAction(pmenuPen, pgroupPen, "&Yellow", 11, &gi.kiPen, fFalse);
  AddSelectAction(pmenuPen, pgroupPen, "&Magenta", 13, &gi.kiPen, fFalse);
  AddSelectAction(pmenuPen, pgroupPen, "&Cyan", 14, &gi.kiPen, fFalse);
  AddSelectAction(pmenuPen, pgroupPen, "Gr&ay", 8, &gi.kiPen, fFalse);
  AddSelectAction(pmenuPen, pgroupPen, "&Lt. Gray", 7, &gi.kiPen, fFalse);
  AddSelectAction(pmenuPen, pgroupPen, "Maroo&n", 1, &gi.kiPen, fFalse);
  AddSelectAction(pmenuPen, pgroupPen, "Dk. Gr&een", 2, &gi.kiPen, fFalse);
  AddSelectAction(pmenuPen, pgroupPen, "Dk. Bl&ue", 4, &gi.kiPen, fFalse);
  AddSelectAction(pmenuPen, pgroupPen, "Mai&ze", 3, &gi.kiPen, fFalse);
  AddSelectAction(pmenuPen, pgroupPen, "&Purple", 5, &gi.kiPen, fFalse);
  AddSelectAction(pmenuPen, pgroupPen, "&Dk. Cyan", 6, &gi.kiPen, fFalse);
}


// Edit menu: just the command line entry dialog, equivalent to Windows'
// DlgCommand / X11's CommandLineX() (xscreen.cpp) -- lets any switch
// Astrolog understands be applied without a dedicated menu item for it.
// Skipped: the 96 user-programmable macro slots (low value), copy/paste
// (would need clipboard integration this pass doesn't add).

static void BuildEditMenu(QMainWindow *pwind)
{
  QMenu *pmenu = pwind->menuBar()->addMenu("&Edit");
  QAction *paCommand = pmenu->addAction("Enter &Command Line...");
  QObject::connect(paCommand, &QAction::triggered, pwind,
    []() { ShowCommandLineDialogQt(); });
}


// Animate menu, equivalent to Windows' cmdAnimate*/cmdStep*/cmdStore/
// cmdRecall handlers (wdriver.cpp:2276-2354). gs.nAnim's sign doubles as
// the on/off state (negative = off, remembering the last active rate) --
// AddToggleAction/AddSelectAction don't fit that, so the rate/factor items
// use small dedicated helpers that preserve the sign the same way
// Windows' handlers do (e.g. "gs.nAnim = (gs.nAnim < 0 ? -1 : 1) * rate").

static QAction *AddAnimRateAction(QMenu *pmenu, QActionGroup *pgroup,
  CONST char *szLabel, int rate)
{
  QAction *pa = pmenu->addAction(szLabel);
  pa->setCheckable(true);
  pa->setActionGroup(pgroup);
  pa->setChecked(NAbs(gs.nAnim) == rate);
  QObject::connect(pa, &QAction::triggered, pa, [rate]() {
    gs.nAnim = (gs.nAnim < 0 ? -1 : 1) * rate;
  });
  return pa;
}

static QAction *AddAnimFactorAction(QMenu *pmenu, QActionGroup *pgroup,
  CONST char *szLabel, int factor)
{
  QAction *pa = pmenu->addAction(szLabel);
  pa->setCheckable(true);
  pa->setActionGroup(pgroup);
  pa->setChecked(NAbs(gi.nDir) == factor);
  QObject::connect(pa, &QAction::triggered, pa, [factor]() {
    gi.nDir = (gi.nDir > 0 ? 1 : -1) * factor;
  });
  return pa;
}

static void BuildAnimateMenu(QMainWindow *pwind)
{
  QMenu *pmenu = pwind->menuBar()->addMenu("&Animate");
  // gs.nAnim's sign is the on/off state and its magnitude is the rate, so
  // toggling it on/off (unlike AddToggleAction's plain-bool flip) has to
  // negate rather than overwrite, or the remembered rate would be lost.
  QAction *paAnim = pmenu->addAction("Do &Animation");
  paAnim->setCheckable(true);
  paAnim->setChecked(gs.nAnim > 0);
  QObject::connect(paAnim, &QAction::triggered, pwind, [paAnim]() {
    neg(gs.nAnim);
    paAnim->setChecked(gs.nAnim > 0);
  });

  QMenu *pmenuRate = pmenu->addMenu("&Jump Rate");
  QAction *paNow = pmenuRate->addAction("Update to &Now");
  QObject::connect(paNow, &QAction::triggered, pwind, []() {
    gs.nAnim = (gs.nAnim < 0 ? -1 : 1) * iAnimNow;
  });
  pmenuRate->addSeparator();
  QActionGroup *pgroupRate = new QActionGroup(pwind);
  AddAnimRateAction(pmenuRate, pgroupRate, "&Seconds", 1);
  AddAnimRateAction(pmenuRate, pgroupRate, "&Minutes", 2);
  AddAnimRateAction(pmenuRate, pgroupRate, "&Hours", 3);
  AddAnimRateAction(pmenuRate, pgroupRate, "&Days", iAnimDay);
  AddAnimRateAction(pmenuRate, pgroupRate, "M&onths", 5);
  AddAnimRateAction(pmenuRate, pgroupRate, "&Years", 6);
  AddAnimRateAction(pmenuRate, pgroupRate, "&Decades", 7);
  AddAnimRateAction(pmenuRate, pgroupRate, "&Centuries", 8);
  AddAnimRateAction(pmenuRate, pgroupRate, "Mi&llennia", 9);
  pmenuRate->addSeparator();
  AddAnimRateAction(pmenuRate, pgroupRate, "1/&10th Seconds", 11);
  AddAnimRateAction(pmenuRate, pgroupRate, "1/1&00th Seconds", 12);
  AddAnimRateAction(pmenuRate, pgroupRate, "1&/1000th Seconds", 13);

  QMenu *pmenuFactor = pmenu->addMenu("Jump &Factor");
  QActionGroup *pgroupFactor = new QActionGroup(pwind);
  AddAnimFactorAction(pmenuFactor, pgroupFactor, "&One Unit", 1);
  AddAnimFactorAction(pmenuFactor, pgroupFactor, "&Two Units", 2);
  AddAnimFactorAction(pmenuFactor, pgroupFactor, "T&hree Units", 3);
  AddAnimFactorAction(pmenuFactor, pgroupFactor, "&Four Units", 4);
  AddAnimFactorAction(pmenuFactor, pgroupFactor, "Fi&ve Units", 5);
  AddAnimFactorAction(pmenuFactor, pgroupFactor, "Si&x Units", 6);
  AddAnimFactorAction(pmenuFactor, pgroupFactor, "&Seven Units", 7);
  AddAnimFactorAction(pmenuFactor, pgroupFactor, "&Eight Units", 8);
  AddAnimFactorAction(pmenuFactor, pgroupFactor, "&Nine Units", 9);

  QAction *paReverse = pmenu->addAction("&Reverse Direction");
  paReverse->setCheckable(true);
  paReverse->setChecked(gi.nDir < 0);
  QObject::connect(paReverse, &QAction::triggered, pwind, [paReverse]() {
    neg(gi.nDir);
    paReverse->setChecked(gi.nDir < 0);
    if (gs.nAnim < 0)
      neg(gs.nAnim);
    RedrawQt();
  });
  AddToggleAction(pmenu, "&Pause Animation", &gi.fPause, fFalse);
  AddToggleAction(pmenu, "&Timed Exposure", &gs.fJetTrail, fFalse);
  pmenu->addSeparator();

  QAction *paForward = pmenu->addAction("Step &Forward");
  QObject::connect(paForward, &QAction::triggered, pwind, []() {
    Animate(NAbs(gs.nAnim) == iAnimNow ? iAnimDay : gs.nAnim, NAbs(gi.nDir));
    RecastAndRedrawQt();
  });
  QAction *paBackward = pmenu->addAction("Step &Backward");
  QObject::connect(paBackward, &QAction::triggered, pwind, []() {
    Animate(NAbs(gs.nAnim) == iAnimNow ? iAnimDay : gs.nAnim, -NAbs(gi.nDir));
    RecastAndRedrawQt();
  });
  pmenu->addSeparator();
  QAction *paStore = pmenu->addAction("&Store Chart Info");
  QObject::connect(paStore, &QAction::triggered, pwind,
    []() { ciSave = ciMain; });
  QAction *paRecall = pmenu->addAction("&Recall Chart Info");
  QObject::connect(paRecall, &QAction::triggered, pwind, []() {
    ciMain = ciCore = ciSave;
    RecastAndRedrawQt();
  });
}


// A curated subset of Windows' Help menu: About, and the doc/data file
// openers (via QDesktopServices, same file resolution FileOpen() already
// does). Skipped this pass: the 11 "List Signs/Objects/Aspects/..." text
// listing actions -- those print to a text stream (is.S) that would need a
// dedicated text output window to show usefully in a GUI, worth its own
// pass rather than a quick addition here.

static void BuildHelpMenu(QMainWindow *pwind)
{
  QMenu *pmenu = pwind->menuBar()->addMenu("&Help");
  CONST char *rgszDoc[6] = { "astrolog.htm", "changes.htm", "license.htm",
    "astrolog.as", "seorbel.txt", "sefstars.txt" };
  CONST char *rgszLabel[6] = { "Open &Documentation...", "Open &Changes",
    "Open &License", "Open Default &Settings", "Open Orbital &Elements",
    "Open &Star List" };
  int i;
  for (i = 0; i < 6; i++) {
    QAction *pa = pmenu->addAction(rgszLabel[i]);
    CONST char *szFile = rgszDoc[i];
    QObject::connect(pa, &QAction::triggered, pwind, [szFile]() {
      char szPath[cchSzMax];
      if (FileOpen(szFile, 2, szPath) != NULL)
        QDesktopServices::openUrl(QUrl::fromLocalFile(szPath));
      else
        QMessageBox::warning(gi.qwind, szAppName,
          QString("File '%1' not found.").arg(szFile));
    });
  }
  pmenu->addSeparator();
  QAction *paAbout = pmenu->addAction("&About Astrolog...");
  QObject::connect(paAbout, &QAction::triggered, pwind,
    []() { ShowAboutDialogQt(); });
}


// Build the main window's menu bar.

static void BuildAstrologMenus(QMainWindow *pwind)
{
  BuildFileMenu(pwind);
  BuildEditMenu(pwind);
  BuildViewMenu(pwind);
  BuildInfoMenu(pwind);
  BuildSettingMenu(pwind);
  BuildChartMenu(pwind);
  BuildGraphicsMenu(pwind);
  BuildAnimateMenu(pwind);
  BuildHelpMenu(pwind);
}


// This routine opens up and initializes the chart window, and is called
// from BeginX() the same way the X11 backend's window setup is, once per
// program invocation.

void BeginQt()
{
  static int s_argc = 1;
  static char *s_argv[] = { (char *)"astrolog", NULL };

  gi.qapp = new QApplication(s_argc, s_argv);
  gi.qwind = new QMainWindow();
  gi.qwind->setWindowTitle(szAppName);
  gi.qcanvas = new ChartCanvas();
  gi.qwind->setCentralWidget(gi.qcanvas);
  BuildAstrologMenus(gi.qwind);
  gi.qwind->resize(gs.xWin, gs.yWin);
  gi.qwind->show();
}


// Hand control over to Qt once the window is up, analogous to InteractX()'s
// XNextEvent() loop for X11, except here Qt itself drives all further
// keyboard, mouse, menu, and dialog interaction; this call blocks until the
// user quits (e.g. via File / Quit, which closes the main window).

void InteractQt()
{
  fQtReady = true;
  RedrawQt();
  gi.qapp->exec();
  fQtReady = false;
}


// This is called right before program termination to get rid of the window.

void EndQt()
{
  if (gi.qim != NULL) {
    delete gi.qim;
    gi.qim = NULL;
  }
}

#endif // QT

/* qtdriver.cpp */
