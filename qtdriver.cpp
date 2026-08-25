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
#include <QtGui/QKeySequence>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QTextBrowser>
#include <QtGui/QResizeEvent>
#include <QtGui/QContextMenuEvent>
#include <QtGui/QPaintEvent>
#include <QtGui/QDesktopServices>
#include <QtGui/QClipboard>
#include <QtGui/QImage>
#include <QtGui/QFontDatabase>
#include <QtCore/QDir>
#include <QtCore/QVector>
#include <QtCore/QUrl>
#include <QtCore/QTimer>
#include <QtGui/QTextDocument>
#include <QtPrintSupport/QPrinter>
#include <QtPrintSupport/QPrintDialog>
#include <QtCore/QMimeData>
#include <QtCore/QFile>
#include <QtCore/QSettings>

#include "astrolog.h"
#include "qtdriver.h"

#include <unistd.h>

#ifdef QT

// Is the chart window fully set up? Guards against a resize event arriving
// (e.g. during initial widget layout) before there is a chart to redraw.
static bool fQtReady = false;


// The widget the chart is actually painted onto. Astrolog keeps rendering
// into an off screen buffer (gi.qim, the Qt analog of X11's Pixmap) via the
// Draw*() primitives in xgeneral.cpp; this widget's only job is to blit
// that buffer to the screen, and to tell Astrolog when its size changes.

static QMenu *PmenuContextForChartQt();   // defined below

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
    // Only auto-correct a buffer/widget size mismatch in graphics mode --
    // in text mode (us.fGraphics false) there's no buffer to paint here at
    // all (see RedrawTextQt()), and gi.qim never gets (re)created, so this
    // check would otherwise fire on every single repaint of this widget.
    if (us.fGraphics && fQtReady && width() >= 1 && height() >= 1 &&
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

  // Right-click brings up the context menu for the current chart type,
  // as it does on Windows. Chart types that have no menu there get none
  // here either, rather than a stub.
  void contextMenuEvent(QContextMenuEvent *pevent) override
  {
    QMenu *pmenu = PmenuContextForChartQt();

    if (pmenu == NULL) {
      pevent->ignore();
      return;
    }
    pmenu->exec(pevent->globalPos());
    delete pmenu;
  }
};


// Text mode (us.fGraphics false, e.g. Colored Text / Show Interpretations)
// renders through a wholly separate path from the graphics one -- Action()
// (astrolog.cpp) calls PrintChart() instead of FActionX()/DrawChartX(),
// driven by is.S/is.szFileScreen rather than gi.qpaint. There's no Win32
// window to draw text characters into here, so instead: point
// is.szFileScreen at a temp file, ask for HTML output (so color comes from
// real <font color> tags instead of needing an ANSI escape parser), run
// Action(), then load the result into a persistent read-only text window.
// Same trick already proven by ShowExportTextDialogQt() in qtdialog.cpp,
// just re-shown in a window instead of left on disk.

static QDialog *s_pdlgText = NULL;
static QTextBrowser *s_ptextBrowser = NULL;

// Shared by RedrawTextQt() and the Edit menu's Copy Chart Text Output --
// Print, equivalent to Windows' DlgPrint(). Two things are copied from
// there: the chart is scaled up before rendering so it doesn't print at
// screen resolution, and "Export Text and Print in Intuitive Manner"
// (us.fSmartSave) forces a white background, since printing a black one
// wastes a cartridge.
//
// Windows scales by METAMUL (12) because it draws into a metafile-style
// printer DC, where that costs nothing. Here the chart has to be rendered
// into a real QImage first -- DrawFill() (xgeneral.cpp) reads and writes
// gi.qim pixels directly, so gi.qim and gi.qpaint must describe the same
// surface, exactly as they do on screen. At METAMUL a default window
// would need a 9120x6900 image, around 250MB, so this uses a smaller
// multiplier that still prints well above screen resolution.

#define PRINTMUL 4

static QString CaptureTextChartQt(flag fHTML);   // defined below

void PrintChartQt()
{
  QPrinter printer(QPrinter::HighResolution);
  QPrintDialog dlgPrint(&printer, gi.qwind);

  dlgPrint.setWindowTitle("Print Chart");
  if (dlgPrint.exec() != QDialog::Accepted)
    return;

  // Text charts aren't drawn at all, they're printed. Hand Qt the same
  // HTML listing the text window already displays and let it paginate.
  if (!us.fGraphics) {
    QTextDocument doc;
    doc.setHtml(CaptureTextChartQt(fTrue));
    doc.print(&printer);
    return;
  }

  int xSav = gs.xWin, ySav = gs.yWin;
  int nScaleSav = gs.nScale, nScaleTextSav = gs.nScaleText;
  flag fInverseSav = gs.fInverse;
  QImage *pqimSav = gi.qim;
  QPainter *pqpaintSav = gi.qpaint;

  // Scale the text along with everything else. Windows only scales
  // gs.nScale, because it draws text through GDI at the printer's own
  // resolution; rendering into an image here means the sidebar has to
  // grow with the canvas or it comes out as an unreadable sliver.
  gs.xWin *= PRINTMUL; gs.yWin *= PRINTMUL;
  gs.nScale *= PRINTMUL; gs.nScaleText *= PRINTMUL;
  if (us.fSmartSave)
    gs.fInverse = fTrue;

  gi.qim = new QImage(gs.xWin, gs.yWin, QImage::Format_RGB32);
  if (gi.qim->isNull()) {
    delete gi.qim;
    gi.qim = pqimSav;
    gs.xWin = xSav; gs.yWin = ySav;
    gs.nScale = nScaleSav; gs.nScaleText = nScaleTextSav;
    gs.fInverse = fInverseSav;
    QMessageBox::warning(gi.qwind, szAppName,
      "Not enough memory to render the chart for printing.");
    return;
  }
  gi.qim->fill(gs.fInverse ? Qt::white : Qt::black);
  gi.qpaint = new QPainter(gi.qim);
  InitColors();
  gi.nScaleT = 1;
  AdjustTextScale();
  DrawChartX();
  delete gi.qpaint;

  // Fit the rendered chart to the page, keeping its aspect ratio.
  QPainter painter(&printer);
  QRect rect = painter.viewport();
  QSize size = gi.qim->size();
  size.scale(rect.size(), Qt::KeepAspectRatio);
  painter.setViewport(rect.x(), rect.y(), size.width(), size.height());
  painter.setWindow(gi.qim->rect());
  painter.drawImage(0, 0, *gi.qim);
  painter.end();

  delete gi.qim;
  gi.qim = pqimSav;
  gi.qpaint = pqpaintSav;
  gs.xWin = xSav; gs.yWin = ySav;
  gs.nScale = nScaleSav; gs.nScaleText = nScaleTextSav;
  gs.fInverse = fInverseSav;
  RedrawQt();
}


// Paste, equivalent to Windows' FFilePaste(): take whatever is on the
// clipboard and, if it's something Astrolog understands, load it. Windows
// checks for a bitmap first and text second, and does the work by dumping
// the clipboard to a temp file and handing that to the same loaders the
// File menu uses -- do exactly that here.
//
// The one difference is the bitmap handoff. Windows writes a raw CF_DIB,
// which has no BITMAPFILEHEADER, hence its FLoadBmp(..., fTrue). QImage
// writes a complete .bmp, so this passes fFalse and takes the same path
// File / Open Bitmap already uses.

void PasteChartQt()
{
  CONST QMimeData *pmime = QApplication::clipboard()->mimeData();
  char szTemp[] = "/tmp/astrolog-qt-paste-XXXXXX";
  flag fRet;
  int fd;

  if (pmime == NULL ||
    (!pmime->hasImage() && !pmime->hasText())) {
    QMessageBox::warning(gi.qwind, szAppName,
      "There is nothing on the clipboard to paste.");
    return;
  }
  fd = mkstemp(szTemp);
  if (fd < 0)
    return;
  close(fd);

  if (pmime->hasImage()) {
    QImage im = qvariant_cast<QImage>(pmime->imageData());
    if (im.isNull() || !im.save(szTemp, "BMP"))
      fRet = fFalse;
    else {
      fRet = FLoadBmp(szTemp, &gi.bmpBack, fFalse);
      if (fRet)
        gi.fBmp = fTrue;
    }
    if (!fRet)
      QMessageBox::warning(gi.qwind, szAppName,
        "Could not read the bitmap on the clipboard.");
  } else {
    QFile file(szTemp);
    fRet = file.open(QIODevice::WriteOnly);
    if (fRet) {
      file.write(pmime->text().toLocal8Bit());
      file.close();
      // FInputData() prints its own diagnostics on a malformed file.
      fRet = FInputData(szTemp);
    }
  }
  unlink(szTemp);
  if (fRet)
    RecastAndRedrawQt();
}


// both need the plain-text or HTML chart listing PrintChart() would
// print, captured via a scratch file rather than shown/redirected for
// real. Doesn't touch us.fGraphics; callers decide how to reflect that.

static QString CaptureTextChartQt(flag fHTML)
{
  char szTemp[] = "/tmp/astrolog-qt-text-XXXXXX";
  int fd = mkstemp(szTemp);
  if (fd < 0)
    return QString();
  close(fd);

  // Action() branches on us.fGraphics: "if (us.fGraphics) FActionX(); else
  // PrintChart();" -- if it's still true here (e.g. a caller invoked this
  // directly, without going through RedrawQt()'s own text-mode branch
  // first), Action() takes the *graphics* path instead, which for QT ends
  // up calling InteractQt() again -- a second, nested Qt event loop. Force
  // it false for the duration so this helper is safe regardless of what
  // the caller already did.
  flag fGraphicsSave = us.fGraphics;
  flag fTextHTMLSave = us.fTextHTML;
  us.fGraphics = fFalse;
  FCloneSz(szTemp, &is.szFileScreen);
  us.fTextHTML = fHTML;
  Action();
  FCloneSz(NULL, &is.szFileScreen);
  us.fTextHTML = fTextHTMLSave;
  us.fGraphics = fGraphicsSave;

  QString qs;
  QFile file(szTemp);
  if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    qs = QString::fromUtf8(file.readAll());
    file.close();
  }
  unlink(szTemp);
  return qs;
}

static void RedrawTextQt()
{
  QString qsHtml = CaptureTextChartQt(fTrue);

  if (s_pdlgText == NULL) {
    s_pdlgText = new QDialog(gi.qwind, Qt::Window);
    s_pdlgText->setWindowTitle("Text Chart");
    s_pdlgText->resize(700, 550);
    QVBoxLayout *playout = new QVBoxLayout(s_pdlgText);
    s_ptextBrowser = new QTextBrowser();
    s_ptextBrowser->setStyleSheet("background-color: black;");
    playout->addWidget(s_ptextBrowser);
  }
  s_ptextBrowser->setHtml(qsHtml);
  s_pdlgText->show();
  s_pdlgText->raise();
  s_pdlgText->activateWindow();
}


// Copy Chart Text Output, equivalent to Windows' cmdCopyText: put the
// same plain text listing Export Chart Text Output would write onto the
// system clipboard instead of a file. Plain text only (not HTML) --
// unlike the persistent Text Chart window, most paste targets for a
// clipboard copy (chat, email, a plain text file) want plain text, and
// it avoids a second capture pass just for an HTML fallback few of them
// would use anyway.

static void CopyChartTextQt()
{
  QString qs = CaptureTextChartQt(fFalse);
  if (!qs.isEmpty())
    QApplication::clipboard()->setText(qs);
}


// Copy Chart Bitmap, equivalent to Windows' cmdCopyBitmap: put the
// currently rendered graphics chart on the clipboard as an image.
// gi.qim already holds exactly that (it's what the canvas blits from),
// so unlike the text/vector copies there's no capture step needed here.

static void CopyChartBitmapQt()
{
  if (gi.qim != NULL)
    QApplication::clipboard()->setImage(*gi.qim);
}


// Redraw the chart into the off screen buffer at the chart's current size,
// and repaint the canvas widget with the result. Called after any change
// that affects only how the chart looks (e.g. colors), and after a resize.

void RedrawQt()
{
  // Astrolog's own Action() calls this before every chart it renders, and
  // the drawing code depends on it: InitColors() is what turns the
  // element and ray colors (kElemA/kRayA, which the Colors dialog edits)
  // and the rulership restrictions into the per object kObjA[] table that
  // kSignA() and the glyph drawing actually read. Without it those
  // settings can be changed and saved but never visibly take effect.
  // Note this is a different function from InitColorsX() in xscreen.cpp,
  // which sets up the backend palette instead.
  InitColors();
  if (!us.fGraphics) {
    RedrawTextQt();
    return;
  }
  if (s_pdlgText != NULL)
    s_pdlgText->hide();
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
  // DrawChartX() derives gi.nScale from gs.nScale itself, but not the
  // text scale -- FActionX() is what normally calls AdjustTextScale(),
  // and the screen path here goes straight to DrawChartX(). Without this
  // gs.nScaleText (Graphics Settings' "Text Scale") never takes effect.
  // gi.nScaleT is FActionX()'s per-output-format multiplier, 1 for
  // anything drawn rather than written to a vector file.
  gi.nScaleT = 1;
  AdjustTextScale();
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
  // ciCore is the chart being edited; ciMain is the one the drawing code
  // reads (DrawInfo() in xcharts0.cpp builds the info sidebar from it).
  // Astrolog's own Action() assigns one to the other immediately before
  // casting, and every caller here that changes ciCore relies on that
  // happening -- without it the chart recalculates but the sidebar keeps
  // describing the previous chart.
  ciMain = ciCore;
  CastChart(0);
  RedrawQt();
}


// Add a checkbox style menu item bound directly to a flag field (us.fXxx or
// similar). Reflects *pfield's state when built, flips it and updates its
// own checked state on click, then applies the change. This is the pattern
// most of Windows' menu toggle commands follow (see e.g. cmdSidereal,
// wdriver.cpp:1611) -- one reusable helper here instead of hand written
// code per item.

// Menu items that a dialog can also change, so they need re-syncing when
// it closes -- the job Windows does with the WiCheckMenu() calls sprinkled
// through DlgCalc and DlgDisplay.
static QAction *s_paSeconds = NULL, *s_paApplying = NULL;
static QAction *s_paSolar = NULL, *s_paHouse3D = NULL, *s_paDwad = NULL;

void SyncDisplayMenuQt()
{
  if (s_paSeconds != NULL)
    s_paSeconds->setChecked(us.fSeconds != 0);
  if (s_paApplying != NULL)
    s_paApplying->setChecked(us.nAppSep == 1);
}

void SyncHouseSetMenuQt()
{
  if (s_paSolar != NULL)
    s_paSolar->setChecked(us.objOnAsc != 0);
  if (s_paHouse3D != NULL)
    s_paHouse3D->setChecked(us.fHouse3D != 0);
  if (s_paDwad != NULL)
    s_paDwad->setChecked(us.nDwad > 0);
}

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


// Same as AddChartModeAction(), but for the handful of chart modes that
// are actually text listings (Exoplanets Chart, and the Help menu's 11
// List Signs/Objects/etc actions) -- these need us.fGraphics forced false
// *before* SetChartModeQt() redraws, not after, or the first redraw would
// still take the graphics path. Also keeps the View menu's "Show Graphics"
// checkbox in sync, the same way Colored Text/Show Interpretations already
// do when they force text mode -- set once BuildViewMenu() runs.

static QAction *s_paGraphics = NULL;

static QAction *AddChartModeTextAction(QMenu *pmenu, CONST char *szLabel,
  int mode)
{
  QAction *pa = pmenu->addAction(szLabel);
  pa->setCheckable(true);
  pa->setActionGroup(s_pgroupChartMode);
  QObject::connect(pa, &QAction::triggered, pa, [mode]() {
    us.fGraphics = fFalse;
    if (s_paGraphics != NULL)
      s_paGraphics->setChecked(fFalse);
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

// Every chart mode and the us.f* flag that selects it. Kept as one table
// rather than the clear-list-plus-switch this used to be, because that
// needed two edits per new mode and they could silently drift apart.
// SetChartModeQt() writes it; SyncChartModeFromFlagsQt() reads it.

typedef struct {
  int nMode;
  flag *pf;
} CHARTMODEQT;

static CONST CHARTMODEQT rgchartmodeQt[] = {
  {gWheel,      &us.fListing},       {gHouse,      &us.fWheel},
  {gGrid,       &us.fGrid},          {gAspect,     &us.fAspList},
  {gMidpoint,   &us.fMidpoint},      {gHorizon,    &us.fHorizon},
  {gOrbit,      &us.fOrbit},         {gSector,     &us.fSector},
  {gCalendar,   &us.fCalendar},      {gDisposit,   &us.fInfluence},
  {gEsoteric,   &us.fEsoteric},      {gAstroGraph, &us.fAstroGraph},
  {gEphemeris,  &us.fEphemeris},     {gArabic,     &us.fArabic},
  {gRising,     &us.fHorizonSearch}, {gLocal,      &us.fAtlasNear},
  {gTraTraTim,  &us.fInDay},         {gTraTraInf,  &us.fInDayInf},
  {gTraTraGra,  &us.fInDayGra},      {gTraNatTim,  &us.fTransit},
  {gTraNatInf,  &us.fTransitInf},    {gTraNatGra,  &us.fTransitGra},
  {gMoons,      &us.fMoonChart},     {gExo,        &us.fExoTransit},
  {gSign,       &us.fSign},          {gObject,     &us.fObject},
  {gHelpAsp,    &us.fAspect},        {gConstel,    &us.fConstel},
  {gPlanet,     &us.fOrbitData},     {gRay,        &us.fRay},
  {gMeaning,    &us.fMeaning},       {gSwitch,     &us.fSwitch},
  {gObscure,    &us.fSwitchRare},    {gKeystroke,  &us.fKeyGraph},
  {gCredit,     &us.fCredit} };

#define cchartmodeQt (int)(sizeof(rgchartmodeQt) / sizeof(CHARTMODEQT))

// Move the Chart menu's radio to "mode", if it has an entry for it.
static void CheckChartModeMenuQt(int mode)
{
  int i;

  for (i = 0; i < s_cChartMode; i++)
    if (s_rgnChartMode[i] == mode) {
      s_rgpaChartMode[i]->setChecked(true);
      break;
    }
}

void SetChartModeQt(int mode)
{
  int i;

  for (i = 0; i < cchartmodeQt; i++)
    *rgchartmodeQt[i].pf = fFalse;
  // DrawChartX() switches directly on gi.nMode with no fallback if it's 0,
  // and DetectGraphicsChartMode() (xscreen.cpp:2165, normally what
  // (re)derives gi.nMode from the us.f* flags before a redraw) doesn't
  // cover several of these flags (fListing, fAspList, fArabic among them)
  // -- so rather than zero gi.nMode and rely on that detection like
  // Windows' ProcessState() does, set it directly to what was actually
  // selected, since that's already known here.
  gi.nMode = mode;
  for (i = 0; i < cchartmodeQt; i++)
    if (rgchartmodeQt[i].nMode == mode) {
      *rgchartmodeQt[i].pf = fTrue;
      break;
    }
  CheckChartModeMenuQt(mode);
  RedrawQt();
}


// Command switches set the us.f* chart-type flags directly, without going
// through SetChartModeQt(), so nothing updates gi.nMode or the Chart menu
// and the chart keeps drawing as whatever was last picked from a menu.
// Windows has the same split and doesn't resolve it: after "-Z" its
// RedoMenu() re-derives the menu radio but gi.nMode still isn't touched,
// so its menu and its chart actively disagree. Rather than reproduce
// that, snapshot the flags around the switches and, if they turned one
// on, route it through SetChartModeQt() so the flags, gi.nMode and the
// menu all end up agreeing.
//
// Snapshot-and-compare rather than deriving the mode from the flags
// afterward, because a switch only sets its own flag and leaves the
// previous mode's flag standing -- after "-Z" from a wheel chart both
// fListing and fHorizon are true, and picking between them by priority
// is guesswork. Which one is newly set is not.

void SnapChartModeQt(flag *rgf)
{
  int i;

  for (i = 0; i < cchartmodeQt; i++)
    rgf[i] = *rgchartmodeQt[i].pf;
}

void SyncChartModeFromFlagsQt(CONST flag *rgf)
{
  int i;

  for (i = 0; i < cchartmodeQt; i++)
    if (*rgchartmodeQt[i].pf && !rgf[i]) {
      SetChartModeQt(rgchartmodeQt[i].nMode);
      return;
    }
}

int CChartModeQt()
{
  return cchartmodeQt;
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
  QAction *paOpen2 = pmenu->addAction("Open Chart #&2...");
  QObject::connect(paOpen2, &QAction::triggered, pwind,
    []() { ShowOpenChart2DialogQt(); });
  QAction *paSave = pmenu->addAction("&Save Chart...");
  QObject::connect(paSave, &QAction::triggered, pwind,
    []() { ShowSaveChartDialogQt(); });
  QAction *paSavePos = pmenu->addAction("Save Chart &Positions...");
  QObject::connect(paSavePos, &QAction::triggered, pwind,
    []() { ShowSaveChartPositionsDialogQt(); });
  pmenu->addSeparator();

  QAction *paSaveSettings = pmenu->addAction("Save Program &Settings...");
  QObject::connect(paSaveSettings, &QAction::triggered, pwind,
    []() { ShowSaveSettingsDialogQt(); });
  QMenu *pmenuOtherFormats = pmenu->addMenu("&Other Formats");
  QAction *paOpenDir = pmenuOtherFormats->addAction(
    "Open Charts in &Folder...");
  QObject::connect(paOpenDir, &QAction::triggered, pwind,
    []() { ShowOpenChartDirDialogQt(); });
  QAction *paSaveList = pmenuOtherFormats->addAction("Save Chart &List...");
  QObject::connect(paSaveList, &QAction::triggered, pwind,
    []() { ShowSaveChartListDialogQt(); });
  pmenuOtherFormats->addSeparator();
  QAction *paSaveAAF = pmenuOtherFormats->addAction(
    "Save Chart E&xchange...");
  QObject::connect(paSaveAAF, &QAction::triggered, pwind,
    []() { ShowSaveAAFDialogQt(); });
  QAction *paSaveQuick = pmenuOtherFormats->addAction(
    "Save Chart &Quick*Chart...");
  QObject::connect(paSaveQuick, &QAction::triggered, pwind,
    []() { ShowSaveQuickDialogQt(); });
  QAction *paSaveCalendar = pmenuOtherFormats->addAction(
    "Save Chart i&Calendar...");
  QObject::connect(paSaveCalendar, &QAction::triggered, pwind,
    []() { ShowSaveCalendarDialogQt(); });
  pmenu->addSeparator();

  QAction *paExportText = pmenu->addAction("Export Chart &Text Output...");
  QObject::connect(paExportText, &QAction::triggered, pwind,
    []() { ShowExportTextDialogQt(); });
  QAction *paExportBmp = pmenu->addAction("Export Chart &Bitmap...");
  QObject::connect(paExportBmp, &QAction::triggered, pwind,
    []() { ShowExportBitmapDialogQt(); });
  QMenu *pmenuVector = pmenu->addMenu("Export &Vector Format");
  QAction *paExportMeta = pmenuVector->addAction("Export Chart &Metafile...");
  QObject::connect(paExportMeta, &QAction::triggered, pwind,
    []() { ShowExportMetafileDialogQt(); });
  QAction *paExportPS = pmenuVector->addAction("Export Chart &PostScript...");
  QObject::connect(paExportPS, &QAction::triggered, pwind,
    []() { ShowExportPSDialogQt(); });
  QAction *paExportSVG = pmenuVector->addAction("Export Chart &SVG...");
  QObject::connect(paExportSVG, &QAction::triggered, pwind,
    []() { ShowExportSVGDialogQt(); });
  QAction *paExportWire = pmenuVector->addAction("Export Chart &Wireframe...");
  QObject::connect(paExportWire, &QAction::triggered, pwind,
    []() { ShowExportWireDialogQt(); });
  pmenu->addSeparator();

  QMenu *pmenuOpenBmp = pmenu->addMenu("Open &Bitmap");
  QAction *paOpenBack = pmenuOpenBmp->addAction("Open Chart &Background...");
  QObject::connect(paOpenBack, &QAction::triggered, pwind,
    []() { ShowOpenBackgroundDialogQt(); });
  QAction *paOpenWorld = pmenuOpenBmp->addAction("Open &World Map...");
  QObject::connect(paOpenWorld, &QAction::triggered, pwind,
    []() { ShowOpenWorldDialogQt(); });
  QAction *paFileSettings = pmenu->addAction("File &Settings...");
  QObject::connect(paFileSettings, &QAction::triggered, pwind,
    []() { ShowFileSettingsDialogQt(); });
  pmenu->addSeparator();

  // Windows also has Print Setup here, which is its native printer
  // configuration dialog; Qt's print dialog covers that itself.
  QAction *paPrint = pmenu->addAction("&Print...");
  QObject::connect(paPrint, &QAction::triggered, pwind,
    []() { PrintChartQt(); });
  pmenu->addSeparator();

  QAction *paQuit = pmenu->addAction("&Quit");
  QObject::connect(paQuit, &QAction::triggered, pwind,
    [pwind]() { pwind->close(); });
}


static void BuildViewMenu(QMainWindow *pwind)
{
  QMenu *pmenu = pwind->menuBar()->addMenu("&View");
  s_paGraphics = AddToggleAction(pmenu, "Show &Graphics", &us.fGraphics,
    fFalse);
  QAction *paColorText = pmenu->addAction("&Colored Text");
  paColorText->setCheckable(true);
  paColorText->setChecked(us.fAnsiColor != 0);
  QObject::connect(paColorText, &QAction::triggered, pwind,
    [paColorText]() {
      us.fAnsiColor = !us.fAnsiColor;
      us.fAnsiChar = !us.fAnsiChar;
      paColorText->setChecked(us.fAnsiColor != 0);
      us.fGraphics = fFalse;
      s_paGraphics->setChecked(fFalse);
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
    [paInterpret]() {
      us.fInterpret = !us.fInterpret;
      paInterpret->setChecked(us.fInterpret != 0);
      us.fGraphics = fFalse;
      s_paGraphics->setChecked(fFalse);
      RedrawQt();
    });
  s_paSeconds = AddToggleAction(pmenu, "Print Nearest &Second", &us.fSeconds,
    fFalse);
  AddToggleAction(pmenu, "&Parallel Aspects", &us.fParallel, fFalse);
  // Not AddToggleAction: nAppSep has three values, and the checkmark means
  // specifically "Applying/Separating", not "non-zero" -- Windows checks
  // "us.nAppSep == 1" everywhere (cmdApplying in wdriver.cpp), so Waxing/
  // Waning (2) shows unchecked. The toggle itself is still inv(), which is
  // what Windows does too, oddly enough.
  s_paApplying = pmenu->addAction("&Applying Aspects");
  s_paApplying->setCheckable(true);
  s_paApplying->setChecked(us.nAppSep == 1);
  QObject::connect(s_paApplying, &QAction::triggered, pwind, []() {
    us.nAppSep = !us.nAppSep;
    s_paApplying->setChecked(us.nAppSep == 1);
    RedrawQt();
  });
}


// Chart list navigation, mirroring wdriver.cpp's cmdListPrev/Next/First/
// Last handlers. "nDir" is -1/+1 to step, or -2/+2 to jump to the first
// or last chart in the list.

static QAction *AddChartListNavAction(QMenu *pmenu, CONST char *szLabel,
  int nDir)
{
  QAction *pa = pmenu->addAction(szLabel);
  QObject::connect(pa, &QAction::triggered, pa, [nDir]() {
    if (is.cci <= 0) {
      QMessageBox::warning(gi.qwind, szAppName,
        "There is no chart list in memory.");
      return;
    }
    int i = nDir == -2 ? 0 : (nDir == 2 ? is.cci-1 : is.iciCur + nDir);
    if (i < 0)
      i = 0;
    else if (i >= is.cci)
      i = is.cci-1;
    if (i != is.iciCur) {
      is.iciCur = i;
      ciCore = is.rgci[i];
      RecastAndRedrawQt();
    }
  });
  return pa;
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

  QAction *paInfo2 = pmenu->addAction("Set Chart #&2 Info...");
  QObject::connect(paInfo2, &QAction::triggered, pwind,
    []() { ShowChartInfo2DialogQt(); });
  QAction *paInfoAll = pmenu->addAction("Charts #&3 Through #6...");
  QObject::connect(paInfoAll, &QAction::triggered, pwind,
    []() { ShowChartsAllDialogQt(); });
  QMenu *pmenuList = pmenu->addMenu("Chart &List");
  QAction *paList = pmenuList->addAction("Chart &List...");
  QObject::connect(paList, &QAction::triggered, pwind,
    []() { ShowChartListDialogQt(); });
  pmenuList->addSeparator();
  AddChartListNavAction(pmenuList, "&Previous Chart", -1);
  AddChartListNavAction(pmenuList, "&Next Chart", 1);
  pmenuList->addSeparator();
  AddChartListNavAction(pmenuList, "&First Chart", -2);
  AddChartListNavAction(pmenuList, "&Last Chart", 2);
  pmenuList->addSeparator();
  QAction *paSwap = pmenuList->addAction("&Swap Chart #1 and #2");
  QObject::connect(paSwap, &QAction::triggered, pwind, []() {
    CI ciT;
    SwapTemp(ciCore, ciTwin, ciT);
    RecastAndRedrawQt();
  });
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

// The Setting menu's "Include Cusps"/"Include Uranians"/etc entries each
// mirror a us.f* flag that the restriction dialogs can also change, so
// they're tracked here for SyncRestrictMenuQt() to refresh -- the same job
// Windows does with the WiCheckMenu() calls at the end of DlgRestrict,
// DlgStar, and DlgMoons. Only the entries backed by a real flag are
// tracked; "Include Minors" has none and is derived from ignore[] alone.
typedef struct {
  QAction *pa;
  flag *pfield;
  int lo, hi;
  flag fTransit;   // also count the transit set as making this included
} CATRES;
static CATRES s_rgcatres[8];
static int s_ccatres = 0;

void SyncRestrictMenuQt()
{
  int i, j;

  for (i = 0; i < s_ccatres; i++) {
    CATRES *pcat = &s_rgcatres[i];
    flag f = fFalse;
    // A category counts as included when anything in its range is
    // unrestricted -- in either the standard or the transit set for most
    // of them, but the standard set alone for fixed stars, which is the
    // one place Windows differs (DlgStar tests ignore[] only, where
    // DlgRestrict and DlgMoons test both).
    for (j = pcat->lo; j <= pcat->hi; j++)
      if (!ignore[j] || (pcat->fTransit && !ignore2[j])) {
        f = fTrue;
        break;
      }
    *pcat->pfield = f;
    pcat->pa->setChecked(f != 0);
  }
}

static QAction *AddCategoryRestrictAction(QMenu *pmenu, CONST char *szLabel,
  flag *pfield, int lo, int hi, int except, flag fTransit)
{
  QAction *pa = pmenu->addAction(szLabel);
  pa->setCheckable(true);
  pa->setChecked(pfield != NULL ? *pfield != 0 : !ignore[lo]);
  if (pfield != NULL && s_ccatres < (int)(sizeof(s_rgcatres)/sizeof(CATRES))) {
    CATRES *pcat = &s_rgcatres[s_ccatres++];
    pcat->pa = pa; pcat->pfield = pfield; pcat->lo = lo; pcat->hi = hi;
    pcat->fTransit = fTransit;
  }
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
  s_paSolar = pmenuHouseSet->addAction("&Solar Chart");
  s_paSolar->setCheckable(true);
  s_paSolar->setChecked(us.objOnAsc != 0);
  QObject::connect(s_paSolar, &QAction::triggered, pwind, []() {
    us.objOnAsc = us.objOnAsc ? 0 : oSun+1;
    s_paSolar->setChecked(us.objOnAsc != 0);
    RecastAndRedrawQt();
  });
  s_paHouse3D = AddToggleAction(pmenuHouseSet, "&3D Houses", &us.fHouse3D,
    fTrue);
  pmenuHouseSet->addSeparator();
  AddToggleAction(pmenuHouseSet, "Show &Decans", &us.fDecan, fTrue);
  s_paDwad = AddToggleAction(pmenuHouseSet, "Show D&wads", &us.nDwad, fTrue);
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

  QMenu *pmenuMoons = pmenu->addMenu("Planetary &Moons");
  AddChartModeAction(pmenuMoons, "Moons Chart", gMoons);
  // Windows also forces text mode when switching to this chart type
  // (cmdChartExo, wdriver.cpp).
  AddChartModeTextAction(pmenuMoons, "Exoplanets Chart", gExo);
  pmenuMoons->addSeparator();
  QAction *paMoonRestrict = pmenuMoons->addAction("Moon &Restrictions...");
  QObject::connect(paMoonRestrict, &QAction::triggered, pwind,
    []() { ShowMoonRestrictDialogQt(); });
  QAction *paMoonObject = pmenuMoons->addAction("Moon &Object Settings...");
  QObject::connect(paMoonObject, &QAction::triggered, pwind,
    []() { ShowMoonObjectDialogQt(); });
  pmenuMoons->addSeparator();
  QAction *paCustom = pmenuMoons->addAction("Object &Customization...");
  QObject::connect(paCustom, &QAction::triggered, pwind,
    []() { ShowCustomDialogQt(); });
  QAction *paCustomS = pmenuMoons->addAction("&Star Customization...");
  QObject::connect(paCustomS, &QAction::triggered, pwind,
    []() { ShowCustomStarDialogQt(); });
  pmenu->addSeparator();

  AddCategoryRestrictAction(pmenu, "Include &Minors", NULL, oChi, oEP,
    oNod, fTrue);
  AddCategoryRestrictAction(pmenu, "Include &Cusps", &us.fCusp, cuspLo,
    cuspHi, -1, fTrue);
  AddCategoryRestrictAction(pmenu, "Include &Uranians", &us.fUranian,
    uranLo, uranHi, -1, fTrue);
  AddCategoryRestrictAction(pmenu, "Include D&warfs", &us.fDwarf, dwarfLo,
    dwarfHi, -1, fTrue);
  AddCategoryRestrictAction(pmenu, "Include &Fixed Stars", &us.fStar,
    starLo, starHi, -1, fFalse);
  AddCategoryRestrictAction(pmenu, "Include &Moons", &us.fMoons, moonsLo,
    moonsHi, -1, fTrue);
  AddCategoryRestrictAction(pmenu, "Include &Body Centers (COB)", &us.fCOB,
    cobLo, cobHi, -1, fTrue);
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
  int i;
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
  // The chart starts on the standard radix; reflect that in the menu. Not
  // s_rgpaChartMode[0] -- since BuildSettingMenu()'s Planetary Moons items
  // share this same group and are added before this menu is built, index 0
  // is no longer reliably "Standard Radix".
  for (i = 0; i < s_cChartMode; i++)
    if (s_rgnChartMode[i] == gWheel) {
      s_rgpaChartMode[i]->setChecked(true);
      break;
    }
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


// Windows' Graphics menu (wdriver.cpp cmdGraphics* handlers), in full.
// "Show Constellation Lines" tracks its own flag here (s_fStarLine) instead
// of Windows' wi.fStarLine, which lives in the Win32-only WI struct.

static flag s_fStarLine = fFalse;

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
  // Not a persistent toggle in Windows either (cmdGraphicsSquare) -- just a
  // one-shot resize. There, resizing the OS window is what changes the
  // chart's drawable area; here the chart's drawable area is however big
  // the canvas widget is, so resize the window that owns it instead.
  QAction *paSquare = pmenu->addAction("S&quare Screen");
  QObject::connect(paSquare, &QAction::triggered, pwind, []() {
    SquareX(&gs.xWin, &gs.yWin, fTrue);
    gi.qwind->resize(gs.xWin, gs.yWin);
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
  // Custom instead of AddToggleAction: also forces the chart into a
  // constellation-capable mode, same as Windows' cmdConstellation.
  QAction *paConstel = pmenuMap->addAction("Show &Constellations");
  paConstel->setCheckable(true);
  paConstel->setChecked(gs.fConstel != 0);
  QObject::connect(paConstel, &QAction::triggered, pwind, [paConstel]() {
    gs.fConstel = !gs.fConstel;
    paConstel->setChecked(gs.fConstel != 0);
    us.fGraphics = fTrue;
    if (gi.nMode != gHorizon && gi.nMode != gSphere && gi.nMode != gGlobe &&
      gi.nMode != gPolar && gi.nMode != gTelescope)
      SetChartModeQt(gWorldMap);
    else
      RedrawQt();
  });
  AddToggleAction(pmenuMap, "Show Full &Star List", &gs.fAllStar, fFalse);
  AddToggleAction(pmenuMap, "Show E&xoplanets", &gs.fAllExo, fFalse);
  // Custom instead of AddToggleAction: Windows stores this toggle in its
  // Win32-only wi struct (not reachable from Qt), and applying it also
  // calls into the portable star-line list builder -- so track the flag
  // locally here instead and reuse just that logic.
  QAction *paStarLine = pmenuMap->addAction("Show Constellation &Lines");
  paStarLine->setCheckable(true);
  paStarLine->setChecked(s_fStarLine != 0);
  QObject::connect(paStarLine, &QAction::triggered, pwind, [paStarLine]() {
    CONST char **ppch;
    s_fStarLine = !s_fStarLine;
    paStarLine->setChecked(s_fStarLine != 0);
    if (s_fStarLine) {
      for (ppch = szDrawConstelLine; *ppch != NULL; ppch += 2)
        if (!FProcessYXU(ppch[0], ppch[1], ppch != szDrawConstelLine))
          break;
      gs.fAllStar = fTrue;
    } else
      FProcessYXU("", "", fFalse);
    us.fGraphics = fTrue;
    RedrawQt();
  });
  pmenuMap->addSeparator();
  AddToggleAction(pmenuMap, "Show &House Details", &gs.fHouseExtra, fTrue);
  AddToggleAction(pmenuMap, "Show &Equator", &gs.fEquator, fFalse);
  AddToggleAction(pmenuMap, "Show C&ities", &gs.fLabelCity, fFalse);
  pmenuMap->addSeparator();
  AddToggleAction(pmenuMap, "Use Detailed World &Map", &gi.fBmp, fFalse);
  AddToggleAction(pmenuMap, "Use Ecliptic &Axis", &gs.fEcliptic, fFalse);

  QMenu *pmenuOrient = pmenu->addMenu("Map &Orientation");
  QAction *paRotWest = pmenuOrient->addAction("Rotate &West");
  QObject::connect(paRotWest, &QAction::triggered, pwind, []() {
    real r = (real)NAbs(gi.nDir) *
      (gi.nMode == gTelescope || gi.nMode == gLocal ? gi.zViewRatio : 1.0);
    if (gi.nMode == gMidpoint || gi.nMode == gTelescope) {
      if (gi.nMode == gMidpoint && gs.objTrack >= 0)
        gs.rRot = planet[gs.objTrack];
      gs.objTrack = -1;
    }
    gs.rRot += r;
    if (gs.rRot >= rDegMax)
      gs.rRot -= rDegMax;
    us.fGraphics = fTrue;
    RedrawQt();
  });
  QAction *paRotEast = pmenuOrient->addAction("Rotate &East");
  QObject::connect(paRotEast, &QAction::triggered, pwind, []() {
    real r = (real)NAbs(gi.nDir) *
      (gi.nMode == gTelescope || gi.nMode == gLocal ? gi.zViewRatio : 1.0);
    if (gi.nMode == gMidpoint || gi.nMode == gTelescope) {
      if (gi.nMode == gMidpoint && gs.objTrack >= 0)
        gs.rRot = planet[gs.objTrack];
      gs.objTrack = -1;
    }
    gs.rRot -= r;
    if (gs.rRot < 0)
      gs.rRot += rDegMax;
    us.fGraphics = fTrue;
    RedrawQt();
  });
  pmenuOrient->addSeparator();
  QAction *paTiltNorth = pmenuOrient->addAction("Tilt &North");
  QObject::connect(paTiltNorth, &QAction::triggered, pwind, []() {
    real r = (real)NAbs(gi.nDir) *
      (gi.nMode == gTelescope || gi.nMode == gLocal ? gi.zViewRatio : 1.0);
    if (gs.rTilt > -rDegQuad) {
      gs.rTilt -= r;
      if (gs.rTilt < -rDegQuad)
        gs.rTilt = -rDegQuad;
    }
    if (gi.nMode == gTelescope)
      gs.objTrack = -1;
    us.fGraphics = fTrue;
    RedrawQt();
  });
  QAction *paTiltSouth = pmenuOrient->addAction("Tilt &South");
  QObject::connect(paTiltSouth, &QAction::triggered, pwind, []() {
    real r = (real)NAbs(gi.nDir) *
      (gi.nMode == gTelescope || gi.nMode == gLocal ? gi.zViewRatio : 1.0);
    if (gs.rTilt < rDegQuad) {
      gs.rTilt += r;
      if (gs.rTilt > rDegQuad)
        gs.rTilt = rDegQuad;
    }
    if (gi.nMode == gTelescope)
      gs.objTrack = -1;
    us.fGraphics = fTrue;
    RedrawQt();
  });
  pmenuOrient->addSeparator();
  QAction *paTiltZero = pmenuOrient->addAction("Set Tilt to &Zero");
  QObject::connect(paTiltZero, &QAction::triggered, pwind, []() {
    gs.rTilt = 0.0;
    us.fGraphics = fTrue;
    if (gi.nMode != gTelescope && gi.nMode != gSphere && gi.nMode != gGlobe)
      SetChartModeQt(gGlobe);
    else
      RedrawQt();
  });
  pmenuOrient->addSeparator();
  QAction *paZoomOut = pmenuOrient->addAction("Zoom &Out");
  QObject::connect(paZoomOut, &QAction::triggered, pwind, []() {
    real r = gs.rspace;
    if (r < rSmall)
      r = (real)(1 << (4 - gi.nScale/gi.nScaleT));
    r *= 2.0;
    if (FValidZoom(r)) {
      gs.rspace = r;
      us.fGraphics = fTrue;
      RedrawQt();
    }
  });
  QAction *paZoomIn = pmenuOrient->addAction("Zoom &In");
  QObject::connect(paZoomIn, &QAction::triggered, pwind, []() {
    real r = gs.rspace;
    if (r < rSmall)
      r = (real)(1 << (4 - gi.nScale/gi.nScaleT));
    r /= 2.0;
    if (FValidZoom(r)) {
      gs.rspace = r;
      us.fGraphics = fTrue;
      RedrawQt();
    }
  });
  pmenu->addSeparator();

  QMenu *pmenuIndian = pmenu->addMenu("&Indian Style Charts");
  AddToggleAction(pmenuIndian, "Show &Indian Wheels", &gs.fIndianWheel,
    fFalse);
  pmenuIndian->addSeparator();
  QAction *paIndianS = pmenuIndian->addAction("Draw &South Indian");
  QObject::connect(paIndianS, &QAction::triggered, pwind, []() {
    gs.fIndianWheel = fTrue;
    gs.fHouseExtra = fFalse;
    SetChartModeQt(gWheel);
  });
  QAction *paIndianN = pmenuIndian->addAction("Draw &North Indian");
  QObject::connect(paIndianN, &QAction::triggered, pwind, []() {
    gs.fIndianWheel = fTrue;
    SetChartModeQt(gHouse);
  });
  QAction *paIndianE = pmenuIndian->addAction("Draw &East Indian");
  QObject::connect(paIndianE, &QAction::triggered, pwind, []() {
    gs.fIndianWheel = fTrue;
    gs.fHouseExtra = fTrue;
    SetChartModeQt(gWheel);
  });

  AddToggleAction(pmenu, "Modify &Display", &gs.fAlt, fFalse);
  // Windows' cmdChartModify: a compound "alternate form" flip for whichever
  // chart type is current, including a direct gWheel/gHouse mode swap that
  // bypasses the usual us.f* chart-type flags entirely -- ported as is,
  // not cleaned up, since that's exactly what Windows itself does here.
  QAction *paChartModify = pmenu->addAction("Modif&y Chart");
  QObject::connect(paChartModify, &QAction::triggered, pwind, []() {
    inv(us.fGridMidpoint);
    inv(us.fPrimeVert);
    inv(us.fCalendarYear);
    inv(us.fLatitudeCross);
    inv(us.nEphemYears);
    inv(us.fGraphAll);
    inv(gs.fSouth);
    inv(gs.fMollweide);
    gi.nMode = (gi.nMode == gWheel ? gHouse :
      (gi.nMode == gHouse ? gWheel : gi.nMode));
    us.fGraphics = fTrue;
    RedrawQt();
  });

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

  QAction *paGraphicsSettings = pmenu->addAction("&Graphics Settings...");
  QObject::connect(paGraphicsSettings, &QAction::triggered, pwind,
    []() { ShowGraphicsSettingsDialogQt(); });
}


// The Edit menu's 96 macro slots, equivalent to Windows' cmdMacro01
// through cmdMacro96 (wdriver.cpp). Eight sets of twelve, each set bound
// to F1-F12 under a different modifier combination. The set names and
// their order are Windows'; so is the modifier assignment, which is
// spelled out in its -WM label handler rather than anywhere obvious:
// Ctrl for sets 2, 4, 6, 7 and 8; Alt for 3, 5, 6, 7 and 8; Shift for
// 1, 4, 5 and 7 (zero-based there, one-based in the menu names below).

static CONST char *rgszMacroSetQt[8] = {
  "Run Macro (&Normal Set)",     "Run Macro (&Shift Set)",
  "Run Macro (&Control Set)",    "Run Macro (&Alt Set)",
  "Run Macro (Ctrl+S&hift Set)", "Run Macro (Alt+Sh&ift Set)",
  "Run Macro (Ct&rl+Alt Set)",   "Run Macro (Ctrl+Alt+Shi&ft)" };

static QString SzMacroKeyQt(int iSet, int iKey)
{
  QString str;

  if (iSet == 2 || iSet == 4 || iSet >= 6)
    str += "Ctrl+";
  if (iSet == 3 || iSet >= 5)
    str += "Alt+";
  if (iSet == 1 || iSet == 4 || iSet == 5 || iSet == 7)
    str += "Shift+";
  return str + QString("F%1").arg(iKey + 1);
}

// Run macro "iMacro" (1 based, as Windows numbers them). Undefined slots
// mostly just say so, but Windows gives two of them a default meaning,
// kept here: F1 opens the documentation, and Alt+F4 quits.

static void RunMacroQt(int iMacro)
{
  char szPath[cchSzMax];

  if (is.rgszMacro != NULL && FSzSet(is.rgszMacro[iMacro])) {
    // Same chart-type handling the Command Line dialog needs; a macro is
    // just a stored command line.
    QVector<flag> rgfMode(cchartmodeQt);
    SnapChartModeQt(rgfMode.data());
    FProcessCommandLine(is.rgszMacro[iMacro]);
    SyncChartModeFromFlagsQt(rgfMode.constData());
    RecastAndRedrawQt();
    return;
  }
  if (iMacro == 1) {
    if (FileOpen("astrolog.htm", 2, szPath) != NULL)
      QDesktopServices::openUrl(QUrl::fromLocalFile(szPath));
    else
      QMessageBox::warning(gi.qwind, szAppName,
        "File 'astrolog.htm' not found.");
    return;
  }
  if (iMacro == 40) {
    gi.qwind->close();
    return;
  }
  QMessageBox::warning(gi.qwind, szAppName,
    QString("Macro number %1 is not defined.").arg(iMacro));
}

static void BuildMacroMenus(QMenu *pmenu, QMainWindow *pwind)
{
  int iSet, iKey;

  for (iSet = 0; iSet < 8; iSet++) {
    QMenu *pmenuSet = pmenu->addMenu(rgszMacroSetQt[iSet]);
    for (iKey = 0; iKey < 12; iKey++) {
      int iMacro = iSet*12 + iKey + 1;
      QAction *pa = pmenuSet->addAction(QString("Macro %1").arg(iMacro));
      // The shortcut makes the key work without opening the menu, which
      // is how Windows' accelerator table has it.
      pa->setShortcut(QKeySequence(SzMacroKeyQt(iSet, iKey)));
      QObject::connect(pa, &QAction::triggered, pwind,
        [iMacro]() { RunMacroQt(iMacro); });
    }
    // Windows breaks its eight submenus into two groups of four.
    if (iSet == 3)
      pmenu->addSeparator();
  }
}


// Edit menu, equivalent to Windows' cmdCommand/cmdCopy*/cmdMacro*/cmdPaste
// handlers. Macros are defined the same way they are on Windows -- with
// the -M command switch, or in astrolog.as -- not from the GUI; this menu
// only runs them, exactly as Windows' does. Windows can additionally
// rename a macro's menu entry with -WM, which it does through ModifyMenu
// on its Win32-only wi.hmenu, so that part isn't ported and the entries
// keep their default "Macro N" labels.

static void BuildEditMenu(QMainWindow *pwind)
{
  QMenu *pmenu = pwind->menuBar()->addMenu("&Edit");
  QAction *paCommand = pmenu->addAction("Enter &Command Line...");
  QObject::connect(paCommand, &QAction::triggered, pwind,
    []() { ShowCommandLineDialogQt(); });
  pmenu->addSeparator();
  BuildMacroMenus(pmenu, pwind);
  pmenu->addSeparator();
  QAction *paPaste = pmenu->addAction("&Paste");
  QObject::connect(paPaste, &QAction::triggered, pwind,
    []() { PasteChartQt(); });
  pmenu->addSeparator();
  pmenu->addSeparator();

  QAction *paCopyText = pmenu->addAction("&Copy Chart Text Output");
  QObject::connect(paCopyText, &QAction::triggered, pwind,
    []() { CopyChartTextQt(); });
  QAction *paCopyBmp = pmenu->addAction("Copy Chart &Bitmap");
  QObject::connect(paCopyBmp, &QAction::triggered, pwind,
    []() { CopyChartBitmapQt(); });
  QMenu *pmenuCopyVector = pmenu->addMenu("Copy &Vector Format");
  QAction *paCopyMeta = pmenuCopyVector->addAction("Copy Chart &Metafile");
  QObject::connect(paCopyMeta, &QAction::triggered, pwind,
    []() { CopyChartMetafileQt(); });
  QAction *paCopyPS = pmenuCopyVector->addAction("Copy Chart &PostScript");
  QObject::connect(paCopyPS, &QAction::triggered, pwind,
    []() { CopyChartPSQt(); });
  QAction *paCopySVG = pmenuCopyVector->addAction("Copy Chart &SVG");
  QObject::connect(paCopySVG, &QAction::triggered, pwind,
    []() { CopyChartSVGQt(); });
  QAction *paCopyWire = pmenuCopyVector->addAction("Copy Chart &Wireframe");
  QObject::connect(paCopyWire, &QAction::triggered, pwind,
    []() { CopyChartWireQt(); });
}


// Windows keeps one Win32 timer running for the entire session and works
// out inside its WM_TIMER handler whether animation is actually on
// (wdriver.cpp) -- gs.nAnim's sign is the on/off switch and gi.fPause
// suspends it. A QTimer does the same job here. Without this the whole
// Animate menu below Step Forward/Backward sets state that nothing ever
// consumes, which is exactly how it behaved before this was added.
static QTimer *s_ptimerAnim = NULL;
static int s_nTimerDelay = 100;   // Windows' wi.nTimerDelay default

// The animation interval, in milliseconds. Windows keeps this in
// wi.nTimerDelay, which is Win32-only, so the Qt build owns its own copy;
// Graphics Settings edits it through these.
int NAnimDelayQt()
{
  return s_nTimerDelay;
}

void SetAnimDelayQt(int nDelay)
{
  s_nTimerDelay = nDelay;
  if (s_ptimerAnim != NULL)
    s_ptimerAnim->setInterval(nDelay);
}

static void StartAnimTimerQt(QMainWindow *pwind)
{
  s_ptimerAnim = new QTimer(pwind);
  QObject::connect(s_ptimerAnim, &QTimer::timeout, pwind, []() {
    // Same guard Windows' WM_TIMER uses. Note gs.nAnim < 1 covers both
    // "off" (negative, remembering the rate) and "never set".
    if (gs.nAnim < 1 || gi.fPause)
      return;
    Animate(gs.nAnim, gi.nDir);
    RecastAndRedrawQt();
  });
  s_ptimerAnim->start(s_nTimerDelay);
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


// Windows' Help menu: About, the doc/data file openers (via
// QDesktopServices, same file resolution FileOpen() already does), and
// the 11 "List Signs/Objects/Aspects/..." text listing actions. Those
// last ones print to a text stream rather than drawing a chart, which is
// why they go through AddChartModeTextAction() -- it forces text mode, so
// RedrawQt() takes the RedrawTextQt() path and they land in the shared
// text window instead of needing one of their own.

static void BuildHelpMenu(QMainWindow *pwind)
{
  QMenu *pmenu = pwind->menuBar()->addMenu("&Help");
  CONST char *rgszDoc[9] = { "astrolog.htm", "changes.htm", "license.htm",
    DEFAULT_INFOFILE, "seorbel.txt", "sefstars.txt", DEFAULT_ATLASFILE,
    DEFAULT_TIMECHANGE, szFileExoCore };
  CONST char *rgszLabel[9] = { "Open &Documentation...", "Open &Changes",
    "Open &License", "Open Default &Settings", "Open Orbital &Elements",
    "Open &Star List", "Open &Atlas", "Open &Time Zone Changes",
    "Open E&xoplanet List" };
  int i;
  for (i = 0; i < 9; i++) {
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

  // Unlike the doc/data files above, these are Windows .url shortcut
  // files (simple INI format, readable via QSettings) whose *content* is
  // the actual URL to open, not something to display directly.
  CONST char *rgszWebsite[2] = { "astrolog.url", "astrlog2.url" };
  CONST char *rgszWebsiteLabel[2] =
    { "Open &Website", "Open Website &Mirror" };
  for (i = 0; i < 2; i++) {
    QAction *pa = pmenu->addAction(rgszWebsiteLabel[i]);
    CONST char *szFile = rgszWebsite[i];
    QObject::connect(pa, &QAction::triggered, pwind, [szFile]() {
      char szPath[cchSzMax];
      if (FileOpen(szFile, 2, szPath) == NULL) {
        QMessageBox::warning(gi.qwind, szAppName,
          QString("File '%1' not found.").arg(szFile));
        return;
      }
      QSettings settings(szPath, QSettings::IniFormat);
      QString qsUrl = settings.value("InternetShortcut/URL").toString();
      if (qsUrl.isEmpty()) {
        QMessageBox::warning(gi.qwind, szAppName,
          QString("Could not read a URL from '%1'.").arg(szFile));
        return;
      }
      QDesktopServices::openUrl(QUrl(qsUrl));
    });
  }
  pmenu->addSeparator();

  // Each of these is a chart "mode" that's actually a plain text listing
  // (us.fGraphics forced false) rather than a picture -- see
  // AddChartModeTextAction() and RedrawTextQt(). Field mapping verified
  // against ProcessState()'s chart-mode switch, wdriver.cpp:1180-1190.
  AddChartModeTextAction(pmenu, "List &Signs", gSign);
  AddChartModeTextAction(pmenu, "List &Objects", gObject);
  AddChartModeTextAction(pmenu, "List Aspec&ts", gHelpAsp);
  AddChartModeTextAction(pmenu, "List &Constellations", gConstel);
  AddChartModeTextAction(pmenu, "List &Planet Info", gPlanet);
  AddChartModeTextAction(pmenu, "List &Rays", gRay);
  AddChartModeTextAction(pmenu, "List &General Meanings", gMeaning);
  AddChartModeTextAction(pmenu, "List S&witches", gSwitch);
  AddChartModeTextAction(pmenu, "List O&bscure Switches", gObscure);
  AddChartModeTextAction(pmenu, "List &Keystrokes", gKeystroke);
  AddChartModeTextAction(pmenu, "List Cr&edits", gCredit);
  pmenu->addSeparator();

  QAction *paAbout = pmenu->addAction("&About Astrolog...");
  QObject::connect(paAbout, &QAction::triggered, pwind,
    []() { ShowAboutDialogQt(); });
}


// Build the main window's menu bar.

static void BuildAstrologMenus(QMainWindow *pwind)
{
  // Allocated before any menu is built, since both BuildSettingMenu()
  // (Planetary Moons chart types) and BuildChartMenu()/BuildGraphicsMenu()
  // add actions to this same shared group.
  s_pgroupChartMode = new QActionGroup(pwind);

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


// Astrolog ships the astrology symbol fonts it draws with in font/, and
// on Windows they are expected to be installed system wide. Register them
// with Qt at startup instead, so selecting one in Graphics Settings works
// out of the box on a machine that has never installed them. The family
// names inside the files match rgszFontName[] exactly, which is what
// DrawSzFont() looks them up by.
//
// Not bundled: Wingdings (proprietary, Windows only) and the plain text
// families near the end of rgszFontName[] -- Arial, Courier New and so on
// -- which come from the system if present. Qt substitutes something
// readable when they aren't, which is the same thing Windows does.

static void LoadBundledFontsQt()
{
  CONST char *rgszFontFile[] = { "Astro.ttf", "EnigmaAstrology.ttf",
    "HamburgSymbols.ttf", "Astronomicon.ttf", "StarFontSans.ttf",
    "StarFontSerif.ttf", "HanksNakshatra.ttf" };
  QStringList rgstrDir;
  int i, j;

  // Look next to the binary first, then in the working directory, so both
  // a run from the source tree and an installed copy work.
  rgstrDir << QCoreApplication::applicationDirPath() + "/font"
           << QDir::currentPath() + "/font";
  for (i = 0; i < (int)(sizeof(rgszFontFile)/sizeof(char *)); i++)
    for (j = 0; j < rgstrDir.size(); j++) {
      QString str = rgstrDir[j] + "/" + rgszFontFile[i];
      if (QFile::exists(str)) {
        QFontDatabase::addApplicationFont(str);
        break;
      }
    }
}


// Right-click context menus, the Qt equivalent of Windows' DoPopup()
// dispatch from WM_RBUTTONDOWN (wdriver.cpp). Windows keeps one menu
// resource per chart type in astrolog.rc (menuV, menuG, menuZ and the
// rest, from ~line 607) and picks between them on gi.nMode.
//
// Every entry in those resources is an ordinary cmd* command that the
// menu bar already implements, so rather than duplicate any behaviour
// these tables name the *menu bar item* each entry should act through,
// by its label. PmenuBuildContextQt() then looks that item up and builds
// a proxy action that forwards to it and mirrors its checkmark. That
// keeps one implementation and one piece of state per command, which
// matters because most of these are toggles.
//
// The indirection is needed because Windows gives the same command a
// different label depending on which context menu it appears in --
// cmdChartModify is "Draw Houses Same Size" on a Western wheel and
// "Toggle North Indian" on an Indian one -- so the context label can't
// simply be the menu bar item's own text.

typedef struct {
  CONST char *szLabel;    // What this context menu calls the command.
  CONST char *szAction;   // Menu bar item to act through, by its label.
} CTXITEM;                // Both NULL means a separator.

// Wheel charts (gWheel/gHouse), Windows' menuV.
static CONST CTXITEM rgctxWheelQt[] = {
  {"Toggle &Comparison Wheel",              "&Comparison Chart"},
  {NULL, NULL},
  {"Draw &Houses Same Size",                "Modif&y Chart"},
  {"Position Planets Based on &3D Houses",  "&3D Houses"},
  {"&Indian Sign Arrangement",              "&Indian Wheel Order"},
  {"Show Indian Style &Wheel",              "Show &Indian Wheels"},
  {NULL, NULL},
  {"Show &Aspect Lines",                    "Show &Equator"},
  {"Aspect Lines Show &Glyphs",             "Show &Glyphs on Aspect Lines"},
  {"Aspect Lines Dotted Based on Max &Orb", "Modify &Display"},
  {"Show Big &Planet Dots",                 "Show &House Details"},
  {"Show &Degrees on Wheel",                "Show C&ities"},
  {"Show Info &Sidebar",                    "Show Info &Sidebar"} };

// Indian style wheel, Windows' menuV2. Same chart mode as above; which
// of the two applies depends on gs.fIndianWheel.
static CONST CTXITEM rgctxIndianQt[] = {
  {"Draw &South Indian",                    "Draw &South Indian"},
  {"Draw &North Indian",                    "Draw &North Indian"},
  {"Draw &East Indian",                     "Draw &East Indian"},
  {"Show &Western Style Wheel",             "Show &Indian Wheels"},
  {NULL, NULL},
  {"&Toggle North Indian",                  "Modif&y Chart"},
  {"To&ggle South/East Indian",             "Show &House Details"},
  {"&1st House on Left Edge",               "&Indian Wheel Order"},
  {NULL, NULL},
  {"&Aspect Grid in South/East Indian",     "Show &Equator"},
  {"Aspect Grid &Highlights Main Axis",     "Show &Glyphs on Aspect Lines"},
  {"Two &Letter Object Labels",             "Show Glyph &Labels"},
  {"Show &Degrees on Wheel",                "Show C&ities"},
  {"Show &Info Sidebar",                    "Show Info &Sidebar"} };

// Windows' menuG, the Grid chart.
static CONST CTXITEM rgctxGridQt[] = {
  {"&View Text Mode Grid",                      "Show &Graphics"},
  {NULL, NULL},
  {"&Increase Aspect Size",                     "&Increase"},
  {"&Decrease Aspect Size",                     "&Decrease"},
  {NULL, NULL},
  {"&Parallel Aspects",                         "&Parallel Aspects"},
  {"&Applying Aspects",                         "&Applying Aspects"},
  {NULL, NULL},
  {"&Transpose Grid",                           "Modify &Display"},
  {"&Highlight Main Axis",                      "Show &Glyphs on Aspect Lines"} };

// Windows' menuM, the Midpoint chart.
static CONST CTXITEM rgctxMidpointQt[] = {
  {"&View Text List of Midpoints",              "Show &Graphics"},
  {"Wheel is &Flipped Over",                    "&Indian Wheel Order"},
  {NULL, NULL},
  {"Show &Aspect Lines",                        "Show &Equator"},
  {"&Label Aspect/Midpoint Lines",              "Show &Glyphs on Aspect Lines"},
  {"Label &Midpoint Lines with Orbs",           "Show C&ities"},
  {"Lines Dotted Based on Max &Orb",            "Modify &Display"},
  {NULL, NULL},
  {"Show Big Planet &Dots",                     "Show &House Details"},
  {"Show Info &Sidebar",                        "Show Info &Sidebar"} };

// Windows' menuZ, the Horizon chart.
static CONST CTXITEM rgctxHorizonQt[] = {
  {"&View Text Horizon List",                   "Show &Graphics"},
  {"Use &Polar Projection",                     "Modif&y Chart"},
  {"Use Ecliptic &Axis",                        "Use Ecliptic &Axis"},
  {NULL, NULL},
  {"Show &Constellations",                      "Show &Constellations"},
  {"Show Sign &Boundaries",                     "&Indian Wheel Order"},
  {"Show &House Boundaries",                    "Show &House Details"},
  {"House Boundaries Are &3D",                  "&3D Houses"},
  {"Show Earth's &Equator",                     "Show &Equator"},
  {NULL, NULL},
  {"Show Full &Star List",                      "Show Full &Star List"},
  {"Show E&xoplanets",                          "Show E&xoplanets"},
  {"Show Planet &Glyphs",                       "Show Glyph &Labels"},
  {"Show Big Planet &Dots",                     "Modify &Display"},
  {"Show Aspect &Lines",                        "Show C&ities"},
  {"Aspect Lines Show Gl&yphs",                 "Show &Glyphs on Aspect Lines"} };

// Windows' menuS, the Orbit chart.
static CONST CTXITEM rgctxOrbitQt[] = {
  {"&View Text Orbit Positions",                "Show &Graphics"},
  {NULL, NULL},
  {"Zoom &Out",                                 "Zoom &Out"},
  {"Zoom &In",                                  "Zoom &In"},
  {NULL, NULL},
  {"&Logarithmic Distances",                    "&3D Houses"},
  {"Show Full &Star List",                      "Show Full &Star List"},
  {NULL, NULL},
  {"Show Sign &Boundaries",                     "Show &House Details"},
  {"Show &Aspect Lines",                        "Show &Equator"},
  {"Aspect Lines Show Gl&yphs",                 "Show &Glyphs on Aspect Lines"},
  {"Show Solid Orbit &Trails",                  "Show C&ities"},
  {"Show Planet &Glyphs",                       "Show Glyph &Labels"},
  {"Show Big Planet &Dots",                     "Modify &Display"} };

// Windows' menuH, the Sector chart.
static CONST CTXITEM rgctxSectorQt[] = {
  {"&View Text Sector List",                    "Show &Graphics"},
  {NULL, NULL},
  {"Show &Aspect Lines",                        "Show &Equator"},
  {"Aspect Lines Show &Glyphs",                 "Show &Glyphs on Aspect Lines"},
  {"Aspect Lines Dotted Based on Max &Orb",     "Modify &Display"},
  {NULL, NULL},
  {"Wheel is &Flipped Over",                    "&Indian Wheel Order"},
  {"Show Big Planet &Dots",                     "Show &House Details"},
  {"Show Info &Sidebar",                        "Show Info &Sidebar"} };

// Windows' menuK, the Calendar chart.
static CONST CTXITEM rgctxCalendarQt[] = {
  {"&View Text Mode Calendar",                  "Show &Graphics"},
  {NULL, NULL},
  {"Show Entire &Year",                         "Modif&y Chart"},
  {"Show &Transits Within Days",                "Show &Glyphs on Aspect Lines"},
  {"Transits are Transit to &Natal",            "&Comparison Chart"},
  {NULL, NULL},
  {"Weeks Start on &Monday",                    "&Indian Wheel Order"},
  {"&Center Date Numbers",                      "Modify &Display"},
  {"&Justify Date Numbers",                     "Show Chart &Info"},
  {"Highlight Current &Date",                   "Show Glyph &Labels"} };

// Windows' menuJ, the Influence chart.
static CONST CTXITEM rgctxInfluenceQt[] = {
  {"&View Text Influences",                     "Show &Graphics"},
  {NULL, NULL},
  {"&Include House Cusp Objects",               "Modify &Display"},
  {"&Circle Final Dispositors",                 "Show &Glyphs on Aspect Lines"},
  {"&Planets Arranged Clockwise",               "&Indian Wheel Order"},
  {"Sun at &Top of Wheels",                     "Show &House Details"} };

// Windows' menu7, the Esoteric chart.
static CONST CTXITEM rgctxEsotericQt[] = {
  {"&View Text Esoteric Chart",                 "Show &Graphics"},
  {NULL, NULL},
  {"Show Entire &Year",                         "Modif&y Chart"},
  {"Year Plots Every &Day",                     "Print Nearest &Second"},
  {NULL, NULL},
  {"Ray Powers Are &Slice Not Count",           "Modify &Display"},
  {"Highlight &Current Date",                   "Show Glyph &Labels"},
  {"Show &Horizontal Lines",                    "Show &Equator"} };

// Windows' menuL, the Astro-Graph chart.
static CONST CTXITEM rgctxAstroGraphQt[] = {
  {"&View Text Astro-Graph Table",              "Show &Graphics"},
  {NULL, NULL},
  {"&Increase Map Size",                        "&Increase"},
  {"&Decrease Map Size",                        "&Decrease"},
  {NULL, NULL},
  {"Ignore Planet &Latitudes",                  "&3D Houses"},
  {"Show Detailed &World Map",                  "Use Detailed World &Map"},
  {"Show Latitude &Crossing",                   "Modif&y Chart"},
  {"Only Show &Midheaven Lines",                "Modify &Display"},
  {NULL, NULL},
  {"Show Ci&ties from Atlas",                   "Show C&ities"},
  {"Cities Colored By &Region",                 "Show &Glyphs on Aspect Lines"} };

// Windows' menuE, the Ephemeris chart.
static CONST CTXITEM rgctxEphemerisQt[] = {
  {"&View Text Mode Ephemeris",                 "Show &Graphics"},
  {NULL, NULL},
  {"Show Entire &Year",                         "Modif&y Chart"},
  {"Year Plots Every &Day",                     "Print Nearest &Second"},
  {"Plot Vertical &Latitudes",                  "&Parallel Aspects"},
  {NULL, NULL},
  {"Don't Show &Moon",                          "Modify &Display"},
  {"Highlight &Current Date",                   "Show Glyph &Labels"},
  {"Show &Horizontal Lines",                    "Show &Equator"} };

// Windows' menuZd, the Rising chart.
static CONST CTXITEM rgctxRisingQt[] = {
  {"&View Rising/Setting Times",                "Show &Graphics"},
  {NULL, NULL},
  {"Year Plots Every &Day",                     "Print Nearest &Second"},
  {"Show Detailed &Color",                      "Use Detailed World &Map"},
  {"Show &Object Key",                          "Show Glyph &Labels"},
  {NULL, NULL},
  {"Show &Grid",                                "Show C&ities"},
  {"Show Current &Time",                        "Modify &Display"} };

// Windows' menuN, the Local chart.
static CONST CTXITEM rgctxLocalQt[] = {
  {"&View Nearest City List",                   "Show &Graphics"},
  {NULL, NULL},
  {"Zoom &Out",                                 "Zoom &Out"},
  {"Zoom &In",                                  "Zoom &In"},
  {NULL, NULL},
  {"Show Lines to &Planets",                    "&Indian Wheel Order"},
  {"Lines are &Astrocartography",               "Use Ecliptic &Axis"},
  {"Ignore Planet La&titudes",                  "&3D Houses"},
  {"Show &Equator",                             "Show &Equator"},
  {"Show &Degree Grid",                         "Show &House Details"},
  {NULL, NULL},
  {"&Label Cities",                             "Show C&ities"},
  {"&Color Cities",                             "Show &Glyphs on Aspect Lines"},
  {"Show &Big City Dots",                       "Modify &Display"} };

// Windows' menu8, the Moons chart.
static CONST CTXITEM rgctxMoonsQt[] = {
  {"&View Text Mode Moons Chart",               "Show &Graphics"},
  {NULL, NULL},
  {"Show Prominence &Zones",                    "Show &House Details"},
  {"Show Outer &Boundary",                      "Show &Equator"},
  {NULL, NULL},
  {"Show Planet &Glyphs",                       "Show Glyph &Labels"},
  {"Show Big Planet &Dots",                     "Modify &Display"},
  {NULL, NULL},
  {"Show Aspect &Lines",                        "Show C&ities"},
  {"Aspect Lines Show Gl&yphs",                 "Show &Glyphs on Aspect Lines"} };

// Windows' menuB, the Transits chart.
static CONST CTXITEM rgctxTransitQt[] = {
  {"&View Text Mode Transit Graph",             "Show &Graphics"},
  {NULL, NULL},
  {"&Parallel Aspects",                         "&Parallel Aspects"},
  {"Show &All Aspects",                         "Modif&y Chart"},
  {"Show Only &Exact Aspects",                  "&Indian Wheel Order"},
  {"Highlight Chart &Time",                     "Modify &Display"} };

// Windows' menuY, the Biorhythm chart.
static CONST CTXITEM rgctxBiorhythmQt[] = {
  {"&View Text Mode Biorhythm",                 "Show &Graphics"},
  {"Show &Average Line",                        "Modify &Display"} };

// Windows' menuXX, the Sphere chart.
static CONST CTXITEM rgctxSphereQt[] = {
  {"Show &Other Half of Sphere",                "Modif&y Chart"},
  {"Show Just Half of S&phere",                 "Modify &Display"},
  {"Show Info &Sidebar",                        "Show Info &Sidebar"},
  {NULL, NULL},
  {"Show Sign &Boundaries",                     "&Indian Wheel Order"},
  {"Show &House Boundaries",                    "Show &House Details"},
  {"House Boundaries Are &3D",                  "&3D Houses"},
  {NULL, NULL},
  {"Aspect Lines Show &Glyphs",                 "Show &Glyphs on Aspect Lines"},
  {"Show Earth's &Equator",                     "Show &Equator"},
  {"Show &Constellations",                      "Show &Constellations"},
  {"Show Full &Star List",                      "Show Full &Star List"},
  {"Show E&xoplanets",                          "Show E&xoplanets"},
  {"Use Ecliptic &Axis",                        "Use Ecliptic &Axis"} };

// Windows' menuXG, the Globe chart.
static CONST CTXITEM rgctxGlobeQt[] = {
  {"Show Astro-Graph &Lines",                   "Modify &Display"},
  {"Show &Constellations",                      "Show &Constellations"},
  {NULL, NULL},
  {"&Ignore Planet Latitudes",                  "&3D Houses"},
  {"Show Detailed &World Map",                  "Use Detailed World &Map"},
  {"Show Sign &Boundaries",                     "Show &House Details"},
  {"Show &Equator",                             "Show &Equator"},
  {"Show Full &Star List",                      "Show Full &Star List"},
  {"Show E&xoplanets",                          "Show E&xoplanets"},
  {"Use Ecliptic &Axis",                        "Use Ecliptic &Axis"},
  {NULL, NULL},
  {"Show C&ities from Atlas",                   "Show C&ities"},
  {"Cities Colored By &Region",                 "Show &Glyphs on Aspect Lines"} };

// Windows' menuXZ, the Telescope chart.
static CONST CTXITEM rgctxTelescopeQt[] = {
  {"Zoom &Out",                                 "Zoom &Out"},
  {"Zoom &In",                                  "Zoom &In"},
  {NULL, NULL},
  {"Show &Constellations",                      "Show &Constellations"},
  {"Show &Sign Boundaries",                     "&Indian Wheel Order"},
  {"Show &House Boundaries",                    "Show &House Details"},
  {"House Boundaries Are &3D",                  "&3D Houses"},
  {"Show Hori&zon Line",                        "Show &Equator"},
  {NULL, NULL},
  {"Show Planet &Details",                      "Show &Glyphs on Aspect Lines"},
  {"O&utline Occulted Planets",                 "Modif&y Chart"},
  {"&Label Planets",                            "Show Glyph &Labels"},
  {"Show &Big Planet Dots",                     "Modify &Display"},
  {"Show &Full Star List",                      "Show Full &Star List"},
  {"Show E&xoplanets",                          "Show E&xoplanets"},
  {"Show Degree &Grid",                         "Show C&ities"},
  {NULL, NULL},
  {"Use &Ecliptic Axis",                        "Use Ecliptic &Axis"} };

#define CctxQt(rg) (int)(sizeof(rg) / sizeof(CTXITEM))

// Find a menu bar item by its exact label, searching submenus too.
static QAction *PaFindMenuActionQt(QWidget *pw, CONST QString &str)
{
  QAction *pa, *paT;
  int i;

  QList<QAction *> rgpa = pw->actions();
  for (i = 0; i < rgpa.size(); i++) {
    pa = rgpa[i];
    if (pa->menu() != NULL) {
      paT = PaFindMenuActionQt(pa->menu(), str);
      if (paT != NULL)
        return paT;
    } else if (pa->text() == str)
      return pa;
  }
  return NULL;
}

static QMenu *PmenuBuildContextQt(CONST CTXITEM *rgitem, int citem)
{
  QMenu *pmenu = new QMenu(gi.qwind);
  int i;

  for (i = 0; i < citem; i++) {
    if (rgitem[i].szLabel == NULL) {
      pmenu->addSeparator();
      continue;
    }
    QAction *paSrc = PaFindMenuActionQt(gi.qwind->menuBar(),
      QString(rgitem[i].szAction));
    if (paSrc == NULL) {
      // The menu bar item was renamed or removed. Show the entry
      // disabled rather than silently dropping it, so the mismatch is
      // visible instead of looking like the menu is simply shorter.
      pmenu->addAction(rgitem[i].szLabel)->setEnabled(false);
      continue;
    }
    QAction *pa = pmenu->addAction(rgitem[i].szLabel);
    pa->setCheckable(paSrc->isCheckable());
    pa->setChecked(paSrc->isChecked());
    pa->setEnabled(paSrc->isEnabled());
    QObject::connect(pa, &QAction::triggered, pa,
      [paSrc]() { paSrc->trigger(); });
  }
  return pmenu;
}

// Pick the menu for the chart currently on screen, or NULL if this chart
// type has none. Windows switches on gi.nMode the same way.
static QMenu *PmenuContextForChartQt()
{
#define CtxCase(mode, rg) \
  case mode: return PmenuBuildContextQt(rg, CctxQt(rg));

  switch (gi.nMode) {
  case gWheel:
  case gHouse:
    return !gs.fIndianWheel ?
      PmenuBuildContextQt(rgctxWheelQt, CctxQt(rgctxWheelQt)) :
      PmenuBuildContextQt(rgctxIndianQt, CctxQt(rgctxIndianQt));
  CtxCase(gGrid,       rgctxGridQt)
  CtxCase(gMidpoint,   rgctxMidpointQt)
  CtxCase(gHorizon,    rgctxHorizonQt)
  CtxCase(gOrbit,      rgctxOrbitQt)
  CtxCase(gSector,     rgctxSectorQt)
  CtxCase(gCalendar,   rgctxCalendarQt)
  CtxCase(gDisposit,   rgctxInfluenceQt)
  CtxCase(gEsoteric,   rgctxEsotericQt)
  CtxCase(gAstroGraph, rgctxAstroGraphQt)
  CtxCase(gEphemeris,  rgctxEphemerisQt)
  CtxCase(gRising,     rgctxRisingQt)
  CtxCase(gLocal,      rgctxLocalQt)
  CtxCase(gMoons,      rgctxMoonsQt)
  CtxCase(gBiorhythm,  rgctxBiorhythmQt)
  CtxCase(gSphere,     rgctxSphereQt)
  CtxCase(gTelescope,  rgctxTelescopeQt)
  // Windows shares one menu between the three flat/round world maps, and
  // one between the two transit graph types.
  case gWorldMap:
  case gGlobe:
  case gPolar:
    return PmenuBuildContextQt(rgctxGlobeQt, CctxQt(rgctxGlobeQt));
  case gTraTraGra:
  case gTraNatGra:
    return PmenuBuildContextQt(rgctxTransitQt, CctxQt(rgctxTransitQt));
  }
  return NULL;
#undef CtxCase
}


// This routine opens up and initializes the chart window, and is called
// from BeginX() the same way the X11 backend's window setup is, once per
// program invocation.

void BeginQt()
{
  static int s_argc = 1;
  static char *s_argv[] = { (char *)"astrolog", NULL };

  gi.qapp = new QApplication(s_argc, s_argv);
  LoadBundledFontsQt();
  gi.qwind = new QMainWindow();
  gi.qwind->setWindowTitle(szAppName);
  gi.qcanvas = new ChartCanvas();
  gi.qwind->setCentralWidget(gi.qcanvas);
  BuildAstrologMenus(gi.qwind);
  StartAnimTimerQt(gi.qwind);
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
