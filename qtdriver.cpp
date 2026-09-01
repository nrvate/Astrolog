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

// The Qt headers this file needs. They may come before or after astrolog.h
// now that its feature macros are prefixed words (METAFILE, PSCRIPT,
// TIMEFUNC) that collide with nothing in Qt's own headers.
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QMenu>
#include <QtWidgets/QColorDialog>
#include <QtWidgets/QAction>
#include <QtWidgets/QActionGroup>
#include <QtGui/QKeySequence>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QTextBrowser>
#include <QtGui/QResizeEvent>
#include <QtGui/QContextMenuEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QProxyStyle>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QScrollBar>
#include <QtGui/QPaintEvent>
#include <QtGui/QDesktopServices>
#include <QtGui/QClipboard>
#include <QtGui/QImage>
#include <QtGui/QFontDatabase>
#include <QtCore/QDir>
#include <QtCore/QVector>
#include <QtCore/QUrl>
#include <QtCore/QTimer>
#include <functional>
#include <QtCore/QEventLoop>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QtWidgets/QProgressDialog>
#include <QtGui/QTextDocument>
#include <QtPrintSupport/QPrinter>
#include <QtPrintSupport/QPrintDialog>
#include <QtCore/QMimeData>
#include <QtCore/QFile>
#include <QtCore/QSettings>
#include <QtCore/QProcess>
#include <QtCore/QStandardPaths>
#include <QtWidgets/QStyleFactory>
#include <QtGui/QPalette>
#include <QtGui/QColor>

#include "astrolog.h"
#include "qtdriver.h"

#include <unistd.h>

#ifdef QT

// A Setting-menu restriction entry: "Include Cusps"/"Include Uranians"/
// etc each mirror a us.f* flag the restriction dialogs can also change, so
// they're tracked for SyncRestrictMenuQt() to refresh -- the same job
// Windows does with the WiCheckMenu() calls at the end of DlgRestrict,
// DlgStar, and DlgMoons. Only entries backed by a real flag are tracked;
// "Include Minors" has none and is derived from ignore[] alone.
typedef struct {
  QAction *pa;
  flag *pfield;
  int lo, hi;
  flag fTransit;   // also count the transit set as making this included
} CATRES;

// The port's mutable window state in one place -- the analogue of Windows'
// WI struct (astrolog.h), which is Win32-only. What used to be ~40
// file-scope statics lives here so ownership is visible; the CONST tables
// (menus, hotkeys, context menus) stay beside the code that uses them.
//
// Rule, paid for once as gotcha 7: the chart-mode and relationship
// tracking arrays are looked up BY VALUE, never by index. Menu build
// order is not a stable interface.
typedef struct _qtuserinterface {
  // Is the chart window fully set up? Guards against a resize event
  // arriving (during initial widget layout) before there is a chart.
  bool fReady = false;

  // Windows' wi.fChartWindow and wi.fWindowChart (astrolog.h:2353), which
  // can't be used here because the whole WI struct is Win32 only. Same
  // defaults Windows starts with (xdata.cpp:130): a window resize changes
  // the chart to match, but a chart size change leaves the window alone.
  // When neither is on the chart keeps whatever size it was given and the
  // scroll area provides scrollbars to pan around it.
  flag fChartWindow = fFalse;   // Chart resize resizes the window?
  flag fWindowChart = fTrue;    // Window resize resizes the chart?

  // Wraps the chart canvas, so a chart bigger than the window can be
  // scrolled. Qt scrolls the viewport itself, which is why none of
  // Windows' wi.xScroll/gi.xOffset panning arithmetic is ported.
  QScrollArea *pscroll = NULL;

  // Should a redraw put up a wait cursor? Windows' wi.fHourglass, same
  // default (xdata.cpp:130). Applied by RedrawQt().
  flag fHourglass = fTrue;

  // Windows' wi.fNoUpdate: suppress automatic redraws, so a run of
  // setting changes doesn't repaint after every one. Redraw Screen still
  // works, and goes through RedrawForceQt() to say so explicitly.
  flag fNoUpdate = fFalse;

  // Windows' wi.fNoPopup and wi.fBmpWindow, which File Settings edits.
  // The first suppresses warning message boxes; the second says a chart
  // bitmap should be grabbed from the window rather than redrawn, which
  // is what CopyChartBitmapQt() already does, so it is kept for the
  // setting's sake.
  flag fNoPopup = fFalse;
  flag fBmpWindow = fTrue;

  // The session's one network manager; see FGetUrlQt() for why it is not
  // created per fetch. Torn down in FinalizeQt().
  QNetworkAccessManager *pnam = NULL;

  // The text-chart character cell, color, and font (see the text-mode
  // rendering above SetTextMetricsQt()).
  int xChar = 8, yChar = 12;
  KV kvText = 0;
  QFont fontText;

  // Menu items that a dialog can also change, so they need re-syncing
  // when it closes -- the job Windows does with the WiCheckMenu() calls
  // sprinkled through DlgCalc and DlgDisplay.
  QAction *paSeconds = NULL, *paApplying = NULL;
  QAction *paSolar = NULL, *paHouse3D = NULL, *paDwad = NULL;
  QAction *paProgress = NULL;
  QAction *paGraphics = NULL;
  QAction *paHelio = NULL;

  // The Chart menu's chart-type radio items, tracked separately from
  // ordinary AddSelectAction groups because chart mode can also change
  // from outside the menu (the Transits dialog) -- SetChartModeQt() looks
  // up the action matching whatever mode was just applied and checks it,
  // regardless of who called it. Sized generously; only 16 slots are
  // used as of this writing. Looked up by value (rule above).
  QAction *rgpaChartMode[64];
  int rgnChartMode[64];
  int cChartMode = 0;

  // Shared across the Chart menu's 16 chart type items and the Graphics
  // menu's 5 sphere/globe/map view items -- Windows treats chart type as
  // one unified radio state (wi.cmdCur/rgcmdMode) no matter which menu
  // changed it, so all 21 items belong to the same exclusive group.
  QActionGroup *pgroupChartMode = NULL;

  // The relationship chart type radio items (SetRelQt). By value, ditto.
  QAction *rgpaRel[16];
  int rgnRel[16];
  int cRel = 0;

  // The Setting menu's restriction-category entries (CATRES above).
  CATRES rgcatres[8];
  int ccatres = 0;

  // "Show Constellation Lines" tracks its own flag here instead of
  // Windows' wi.fStarLine, which lives in the Win32-only WI struct.
  flag fStarLine = fFalse;

  // Custom labels set by -WM (a macro slot) and -WM0 (a submenu), which
  // is how a Windows user names their macros in astrolog.as. Windows
  // applies these immediately with ModifyMenu on its Win32-only wi.hmenu;
  // here the switches are processed long before the menu bar exists, so
  // the names are held until BuildMacroMenus() runs. NULL means "keep
  // the default label".
  char *rgszMacro[cMacro];
  char *rgszMSub[cMSub];

  // Windows keeps one Win32 timer running for the entire session and
  // works out inside its WM_TIMER handler whether animation is actually
  // on -- gs.nAnim's sign is the on/off switch and gi.fPause suspends
  // it. A QTimer does the same job here.
  QTimer *ptimerAnim = NULL;
  int nTimerDelay = 100;        // Windows' wi.nTimerDelay default
  int nAntialias = 6;           // Windows' wi.nAntialias default (-Wx)
  int xWind = 0, yWind = 0;     // Window position from -Ww
  flag fWindPos = fFalse;

  // The Animate menu's run/pause pair (see the note at BuildAnimateMenu).
  QAction *paAnimRun = NULL, *paAnimPause = NULL;
} QTUI;

static QTUI qi;


// The widget the chart is actually painted onto. Astrolog keeps rendering
// into an off screen buffer (gi.qim, the Qt analog of X11's Pixmap) via the
// Draw*() primitives in xgeneral.cpp; this widget's only job is to blit
// that buffer to the screen, and to tell Astrolog when its size changes.

static QAction *PaFindMenuActionQt(QWidget *pw, CONST QString &str);
static QAction *PaFindMenuActionLooseQt(QWidget *pw, CONST QString &str);
static void ConnectMenuQt(QAction *pa, QObject *pctx,
  std::function<void()> fn);
static QMenu *PmenuContextForChartQt();   // defined below
static QMenu *PmenuContextForTextQt();    // defined below

// Does a right button drag rotate and tilt the current chart? Same set of
// chart types as wdriver.cpp:842. These are exactly the types that have to
// hold their context menu until the button is released, since otherwise
// every rotate would end by popping up a menu.
static flag FRotatableQt()
{
  return us.fGraphics && (fMap || gi.nMode == gMidpoint ||
    gi.nMode == gLocal || gi.nMode == gSphere || gi.nMode == gGlobe ||
    gi.nMode == gPolar || gi.nMode == gTelescope);
}


class ChartCanvas : public QWidget
{
public:
  ChartCanvas(QWidget *parent = NULL) : QWidget(parent), fRotated(fFalse)
  {
    // Handle the right button in the mouse events below rather than letting
    // Qt synthesize a ContextMenu event: Windows pops the menu on button
    // down for most charts but on button up for the ones a drag rotates,
    // and Qt's automatic event can't express that split (it fires on press
    // under X11 but on release under Windows, so it isn't even consistent
    // between the two platforms this program targets).
    setContextMenuPolicy(Qt::PreventContextMenu);
  }

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
    // Text charts now draw into gi.qim too, so this size check applies in
    // both modes -- it used to skip text mode, back when text lived in a
    // window of its own and this buffer went stale.
    // Only chase the widget's size when a window resize is supposed to
    // change the chart. With that off the chart keeps its own size and
    // this widget is sized to match it instead (see ApplySizeModeQt), so
    // redrawing to fit here would fight that and repaint forever.
    if (qi.fReady && qi.fWindowChart &&
      width() >= 1 && height() >= 1 &&
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

  // Windows' chart window does three things with the mouse that aren't
  // menu commands at all, in NWndProc()'s WM_LBUTTONDOWN / WM_MOUSEMOVE /
  // WM_RBUTTONDOWN / WM_RBUTTONUP cases (wdriver.cpp:820-960): drag with
  // the right button to rotate globes and maps, Alt+click a world map to
  // move the chart to that spot, and scribble freehand over the chart with
  // the left button. All three are ported below.

  void mousePressEvent(QMouseEvent *pevent) override
  {
    if (pevent->button() == Qt::RightButton) {
      ptRot = pevent->pos();
      fRotated = fFalse;

      // Charts a drag can rotate hold their menu until the button comes
      // back up, so that dragging one doesn't end in a popup.
      if (!FRotatableQt())
        ShowContextMenu(pevent->globalPos());
      return;
    }
    if (pevent->button() != Qt::LeftButton)
      return;

    // Alt+click on a world map relocates the chart to that spot. Windows
    // consumes the click either way, so Alt+click never also scribbles.
    if (pevent->modifiers() & Qt::AltModifier) {
      if (fMap && !gs.fConstel && !gs.fMollweide) {
        SetChartLocation(pevent->pos());
        ptDraw = QPoint();
      }
      return;
    }
    Scribble(pevent->pos(), pevent->modifiers(), fFalse);
  }

  void mouseMoveEvent(QMouseEvent *pevent) override
  {
    if (pevent->buttons() & Qt::RightButton) {
      if (FRotatableQt())
        RotateByDrag(pevent->pos());
      return;
    }

    // Windows treats a plain left drag as a series of Shift+clicks, which
    // is what makes dragging draw a continuous line instead of a dotted
    // trail of single pixels. Holding Shift or Ctrl during a drag draws
    // nothing until the button goes down again, so that a Shift+click can
    // fan several lines out from one anchor point, and a Ctrl+click can
    // place a rectangle, without the drag itself scribbling over them.
    if ((pevent->buttons() & Qt::LeftButton) &&
      !(pevent->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)))
      Scribble(pevent->pos(), Qt::ShiftModifier, fTrue);
  }

  void mouseReleaseEvent(QMouseEvent *pevent) override
  {
    // The rotatable charts' menu appears now instead of on button down,
    // and only if this drag didn't actually rotate anything.
    if (pevent->button() == Qt::RightButton && FRotatableQt() && !fRotated)
      ShowContextMenu(pevent->globalPos());
  }

private:
  QPoint ptDraw;    // Where the last scribble left off, or null for none.
  QPoint ptRot;     // Where the right button drag in progress last was.
  flag fRotated;    // Has that drag rotated the chart? (Windows' wi.fMoved.)

  // Right-click brings up the context menu for the current chart type, as
  // it does on Windows. Chart types that have no menu there get none here
  // either, rather than a stub.
  void ShowContextMenu(CONST QPoint &ptGlobal)
  {
    // The canvas shows text charts too now, and those have their own set
    // of context menus, keyed off the text chart type rather than gi.nMode.
    QMenu *pmenu = us.fGraphics ? PmenuContextForChartQt() :
      PmenuContextForTextQt();

    if (pmenu == NULL)
      return;
    pmenu->exec(ptGlobal);
    delete pmenu;
  }

  // Port of the rotate and tilt math at wdriver.cpp:844-870.
  void RotateByDrag(CONST QPoint &pt)
  {
    if (gs.xWin < 1 || gs.yWin < 1)
      return;
    gs.rRot += (real)(pt.x() - ptRot.x()) * rDegHalf / (real)gs.xWin *
      (gi.nMode == gLocal || gi.nMode == gTelescope ? -gi.zViewRatio : 1.0);
    gs.rTilt += (real)(pt.y() - ptRot.y()) * rDegHalf / (real)gs.yWin *
      (gi.nMode == gLocal || gi.nMode == gTelescope ? gi.zViewRatio :
      (gi.nMode == gGlobe ? -1.0 : 1.0));
    while (gs.rRot >= rDegMax)
      gs.rRot -= rDegMax;
    while (gs.rRot < 0.0)
      gs.rRot += rDegMax;
    if (gs.rTilt > rDegQuad)
      gs.rTilt = rDegQuad;
    else if (gs.rTilt < -rDegQuad)
      gs.rTilt = -rDegQuad;

    // Dragging a telescope chart, or a midpoint chart that's tracking an
    // object, means the view is no longer tied to what it was tracking.
    if (gi.nMode == gMidpoint || gi.nMode == gTelescope) {
      if (gi.nMode == gMidpoint && gs.objTrack >= 0)
        gs.rRot = planet[gs.objTrack];
      gs.objTrack = -1;
    }
    ptRot = pt;
    fRotated = fTrue;
    RedrawQt();
  }

  // Port of the Alt+click relocation at wdriver.cpp:876-897. Note this
  // sets ciMain (what Lon and Lat resolve to) and then copies it over the
  // working chart, rather than the other way around.
  void SetChartLocation(CONST QPoint &pt)
  {
    if (gs.xWin < 1 || gs.yWin < 1)
      return;
    Lon = rDegHalf -
      Mod((real)(pt.x() - gi.xOffset) / (real)gs.xWin * rDegMax - gs.rRot);
    if (Lon < -rDegHalf)
      Lon = -rDegHalf;
    else if (Lon > rDegHalf)
      Lon = rDegHalf;
    Lat = rDegQuad - (real)(pt.y() - gi.yOffset) / (real)gs.yWin * rDegHalf;
    if (Lat < -rDegQuad)
      Lat = -rDegQuad;
    else if (Lat > rDegQuad)
      Lat = rDegQuad;
    ciCore = ciMain;
    RecastAndRedrawQt();
  }

  // Port of the freehand drawing at wdriver.cpp:899-930. Windows scribbles
  // straight onto the window's device context, so its marks last until the
  // next redraw paints over them; drawing into gi.qim here gives them the
  // same lifetime, since a redraw replaces that buffer wholesale.
  void Scribble(CONST QPoint &pt, Qt::KeyboardModifiers mods, flag fDrag)
  {
    if (gi.qim == NULL || !us.fGraphics)
      return;
    KV kv = KvFromKi(gi.kiPen);
    QColor col(RgbR(kv), RgbG(kv), RgbB(kv));

    // Ctrl+click draws a rectangle, Ctrl+Shift+click an ellipse, in both
    // cases from the last remembered point to this one.
    if (mods & Qt::ControlModifier) {
      if (ptDraw.isNull())
        return;
      QPainter p(gi.qim);
      p.setPen(QPen(col, Max((!gs.fThick ? 0 : 2) + gs.nThickAdjust, 0)));
      p.setBrush(Qt::NoBrush);
      if (mods & Qt::ShiftModifier)
        p.drawEllipse(QRect(ptDraw, pt).normalized());
      else
        p.drawRect(QRect(ptDraw, pt).normalized());

    // Shift+click draws a line from the last point to this one.
    } else if (mods & Qt::ShiftModifier) {
      if (ptDraw.isNull())
        return;
      QPainter p(gi.qim);
      p.setPen(QPen(col, Max((!gs.fThick ? 0 : 2) + gs.nThickAdjust, 0)));
      p.drawLine(ptDraw, pt);

      // Only a drag advances the anchor. A deliberate Shift+click leaves it
      // alone, so several lines can be fanned out from the same point.
      if (fDrag)
        ptDraw = pt;

    // A plain click sets a single pixel and remembers where it was. This
    // ignores pen thickness, exactly as Windows' SetPixel() does.
    } else {
      if (pt.x() >= 0 && pt.x() < gi.qim->width() &&
        pt.y() >= 0 && pt.y() < gi.qim->height())
        gi.qim->setPixel(pt, col.rgb());
      ptDraw = pt;
    }
    update();
  }
};


/*
******************************************************************************
** Window sizing, the View / Window Settings commands.
******************************************************************************
*/

flag FNoUpdateQt() { return qi.fNoUpdate; }
void SetNoUpdateQt(flag f) { qi.fNoUpdate = f; }

flag FNoPopupQt() { return qi.fNoPopup; }
void SetNoPopupQt(flag f) { qi.fNoPopup = f; }
flag FBmpWindowQt() { return qi.fBmpWindow; }
void SetBmpWindowQt(flag f) { qi.fBmpWindow = f; }



// Put the canvas into whichever of the two sizing modes is currently set.
// With "window resizes chart" on, the canvas tracks the scroll area's
// viewport and paintEvent() picks the chart size up from it. With it off
// the canvas is sized to the chart instead, and the scroll area grows
// scrollbars whenever that doesn't fit in the window.
void ApplySizeModeQt()
{
  if (qi.pscroll == NULL || gi.qcanvas == NULL)
    return;
  qi.pscroll->setWidgetResizable(qi.fWindowChart != fFalse);
  if (!qi.fWindowChart && gs.xWin >= 1 && gs.yWin >= 1)
    gi.qcanvas->resize(gs.xWin, gs.yWin);
}


// Grow or shrink the window so the chart fits it exactly, the Qt version
// of Windows' ResizeWindowToChart() (xscreen.cpp:582). Rather than compute
// frame and menu bar thickness, measure how much of the window currently
// isn't chart viewport and keep that much.
void ResizeWindowToChartQt()
{
  if (gi.qwind == NULL || qi.pscroll == NULL || !us.fGraphics)
    return;
  if (gs.xWin < 1)
    gs.xWin = DEFAULTX;
  if (gs.yWin < 1)
    gs.yWin = DEFAULTY;
  QSize sizeExtra = gi.qwind->size() - qi.pscroll->viewport()->size();
  gi.qwind->resize(QSize(gs.xWin, gs.yWin) + sizeExtra);
}


// Size Chart to Window: adopt the viewport's size as the chart's.
void SizeChartToWindowQt()
{
  if (qi.pscroll == NULL)
    return;
  QSize size = qi.pscroll->viewport()->size();

  if (size.width() < 1 || size.height() < 1)
    return;
  gs.xWin = size.width();
  gs.yWin = size.height();
  us.fGraphics = fTrue;
  ApplySizeModeQt();
  RedrawQt();
}


// Size Window Full Screen. Windows saves and restores the window rectangle
// by hand and can fail outright on it; Qt has this built in.
void ToggleFullScreenQt()
{
  if (gi.qwind == NULL)
    return;
  if (gi.qwind->isFullScreen())
    gi.qwind->showNormal();
  else
    gi.qwind->showFullScreen();
}


// Clear Screen. Windows calls DrawClearScreen(), which can't be used here
// because it draws through gi.qpaint, and that only exists for the length
// of a redraw. Filling the buffer with the background color is what that
// would have done anyway (its DrawColor(gi.kiOff) + DrawBlock pair).
//
// One path serves both modes, because text charts draw into this same
// buffer (see RedrawQt()). It used to branch on us.fGraphics and send text
// mode to a ClearTextWindowQt() that cleared the separate text window --
// and that window stopped being created when text moved onto the canvas,
// so Clear Screen silently did nothing in text mode from then until
// 2026-09-01. gi.kiOff is kMainA[fInverse] (xscreen.cpp:162), which is the
// colour Windows' TextClearScreen() passes to WinClearScreen().
void ClearScreenQt()
{
  if (gi.qim == NULL)
    return;
  KV kv = KvFromKi(gi.kiOff);
  gi.qim->fill(QColor(RgbR(kv), RgbG(kv), RgbB(kv)));
  if (gi.qcanvas != NULL)
    gi.qcanvas->update();
}


// The four scrolling commands. Windows posts scrollbar messages to itself
// and repaints at a new offset; here the scroll area already owns real
// scrollbars, so these just drive them.
void ScrollChartQt(int nDir)
{
  if (qi.pscroll == NULL)
    return;
  QScrollBar *psb = qi.pscroll->verticalScrollBar();
  QScrollBar *psbH = qi.pscroll->horizontalScrollBar();

  switch (nDir) {
  case -1: psb->setValue(psb->value() - psb->pageStep()); break;
  case  1: psb->setValue(psb->value() + psb->pageStep()); break;
  case  0:
    psb->setValue(psb->minimum());
    psbH->setValue(psbH->minimum());
    break;
  case  2:
    psb->setValue(psb->maximum());
    psbH->setValue(psbH->maximum());
    break;
  }
}


// Warnings and errors reach the user in a message box, as they do on
// Windows, rather than going to stderr. That matters more than it looks:
// PrintError()'s non-Windows path ends in Terminate(), so a chart that
// referenced a missing file -- a macro pointing at a path that doesn't
// exist here, say -- took the whole program down rather than complaining
// about it. Windows shows a box and carries on, and so does this.
void PrintWarningQt(CONST char *sz, flag fError)
{
  if (FNoPopupQt())
    return;
  // Before the window exists, say it on stderr instead. main() parses
  // astrolog.as and then the command line (astrolog.cpp, the
  // FProcessSwitchFile and FProcessSwitches calls) well before Action()
  // reaches InteractQt() and BeginQt() constructs the QApplication, so a
  // warning raised from either -- a chart file that isn't there, a switch
  // given too few parameters -- would build a QWidget with no application
  // alive, and Qt answers that with qFatal() and a core dump. Windows has
  // no equivalent problem: MessageBox(NULL, ...) needs neither a window
  // nor an application object. The two formats are the ones the console
  // builds use in PrintWarning()/PrintError() (general.cpp).
  if (QApplication::instance() == NULL) {
    if (fError)
      fprintf(stderr, "%s: %s\n", szAppName, sz);
    else
      fprintf(stderr, "%s\n", sz);
    return;
  }
  // Stop an animation first, or the same box comes back every frame.
  // Declared below; stopping is the one thing this needs from it.
  if (gs.nAnim > 0)
    neg(gs.nAnim);
  QMessageBox::warning(gi.qwind, QString("%1 %2").arg(szAppName)
    .arg(fError ? "Error" : "Warning"), QString::fromLatin1(sz));
}


/*
******************************************************************************
** Text charts drawn into the chart window.
******************************************************************************
*/

// Windows draws text charts into the same window the graphics ones use,
// one character at a time on a fixed grid (the TextOut() in PrintSz(),
// general.cpp:1175, with the cell size set in wdriver.cpp:2841). This does
// the same into gi.qim, so pressing V switches what the window shows
// rather than opening a second window beside it.

// Windows draws text charts in "Terminal", a bitmap font whose glyphs are
// exactly the 8x12 cell it lays them out on, which is why they come out
// crisp. Forcing a TrueType face into that same cell doesn't: at 12 pixels
// its advance is nearer 7, so the glyphs end up thin and cramped with a
// ragged gap at the end of every cell.
//
// So the cell comes from the font here rather than the font from the cell.
// The chart is laid out in character cells either way, so the columns still
// line up; the text is simply rendered at a size the face was drawn for.
// The size still follows gs.nScale, so Character Scale keeps working.
static void SetTextMetricsQt()
{
  int nPix = Max(gs.nScale * 13 / 200, 9);

  qi.fontText = QFont("Liberation Mono");
  qi.fontText.setPixelSize(nPix);
  qi.fontText.setFixedPitch(fTrue);
  qi.fontText.setStyleHint(QFont::Monospace, QFont::PreferQuality);
  QFontMetrics fm(qi.fontText);
  qi.xChar = fm.horizontalAdvance(QChar('M'));
  qi.yChar = fm.height();
  if (qi.xChar < 1) qi.xChar = 8;
  if (qi.yChar < 1) qi.yChar = 12;
}

// Called from AnsiColor() (general.cpp) for each colour change.
void TextColorQt(KI ki)
{
  qi.kvText = KvFromKi(ki);
}

// Called from PrintSz() (general.cpp) for each character, with the cell
// the text engine has reached.
void TextCharQt(int xCell, int yCell, int ch)
{
  if (gi.qpaint == NULL)
    return;
  wchar wch = (uchar)ch;

  // Astrolog's text charts draw their boxes with IBM code page glyphs, so
  // map the high bytes to what they mean rather than showing Latin-1.
  if ((uchar)ch >= 128 && us.nCharset != ccLatin)
    wch = WchFromChIBM((uchar)ch);
  gi.qpaint->setPen(QColor(RgbR(qi.kvText), RgbG(qi.kvText),
    RgbB(qi.kvText)));
  gi.qpaint->drawText(xCell * qi.xChar + 4, (yCell + 1) * qi.yChar,
    QString(QChar(wch)));
}


// Capturing a text chart as text. Text mode renders through a wholly
// separate path from the graphics one -- Action() (astrolog.cpp) calls
// PrintChart() instead of FActionX()/DrawChartX(), driven by
// is.S/is.szFileScreen rather than gi.qpaint. So: point is.szFileScreen at
// a temp file, optionally ask for HTML output (so colour comes from real
// <font color> tags instead of needing an ANSI escape parser), run
// Action(), and read the file back. Same trick ShowExportTextDialogQt()
// in qtdialog.cpp uses.
//
// This used to end in a persistent QTextBrowser window. It does not any
// more: RedrawQt() draws text charts into the canvas buffer, the way
// Windows draws them into its client area, so the only callers left want
// the string itself.

// Shared by the Edit menu's Copy Chart Text Output --
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

  flag fNoMemory = fFalse;
  {
    // Borrow the render geometry and both canvas pointers; the closing
    // brace restores all seven on every exit, which is what the two
    // hand-written restore blocks here used to do (gotcha 3's site).
    Borrow bx(gs.xWin), by(gs.yWin);
    Borrow bs(gs.nScale), bst(gs.nScaleText);
    Borrow bi(gs.fInverse);
    Borrow bqim(gi.qim);
    Borrow bqpaint(gi.qpaint);

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
      fNoMemory = fTrue;
    } else {
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
    }
  }
  if (fNoMemory) {
    // After the restore on purpose: the warning is modal, and a repaint
    // behind it must not happen at the print scale.
    QMessageBox::warning(gi.qwind, szAppName,
      "Not enough memory to render the chart for printing.");
    return;
  }
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

// Render the current chart's text output to a file, touching nothing.
// One place, because there are three things to put back and only two of
// them are obvious -- the third cost a reproduction to find (work log
// item 154), and Export Chart Text Output was getting it wrong.
//
// us.fGraphics: Action() branches on it ("if (us.fGraphics) FActionX();
// else PrintChart();"). If it is still true here, Action() takes the
// *graphics* path, which for QT calls InteractQt() again -- a second,
// nested Qt event loop.
//
// us.fTextHTML: the caller says which it wants rather than inheriting
// whatever the File Settings dialog last left.
//
// is.S: the whole GUI runs inside an Action() call already (main ->
// Action -> FActionX -> InteractQt), so the one below is nested. It
// opens is.S on the file and fclose()s it on the way out, but never puts
// the caller's back -- so is.S is left pointing at a closed FILE.
// Everything printing through it afterwards writes to a dead handle, and
// the outer Action() eventually fclose()s the same FILE a second time,
// which glibc catches as "invalid stdio handle" and aborts on.
// astrolog.cpp:464 saves and restores it around its own nested call for
// exactly this reason.

void CaptureTextToFileQt(CONST char *szFile, flag fHTML)
{
  flag fGraphicsSave = us.fGraphics, fTextHTMLSave = us.fTextHTML;
  FILE *fileSave = is.S;

  us.fGraphics = fFalse;
  us.fTextHTML = fHTML;
  FCloneSz(szFile, &is.szFileScreen);
  Action();
  FCloneSz(NULL, &is.szFileScreen);
  us.fTextHTML = fTextHTMLSave;
  us.fGraphics = fGraphicsSave;
  is.S = fileSave;
}


static QString CaptureTextChartQt(flag fHTML)
{
  char szTemp[] = "/tmp/astrolog-qt-text-XXXXXX";
  int fd = mkstemp(szTemp);
  if (fd < 0)
    return QString();
  close(fd);
  CaptureTextToFileQt(szTemp, fHTML);

  QString qs;
  QFile file(szTemp);
  if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    qs = QString::fromUtf8(file.readAll());
    file.close();
  }
  unlink(szTemp);
  return qs;
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

void RedrawForceQt()
{
  flag fSav = qi.fNoUpdate;

  qi.fNoUpdate = fFalse;
  RedrawQt();
  qi.fNoUpdate = fSav;
}


void RedrawQt()
{
  if (qi.fNoUpdate)
    return;
  // Astrolog's own Action() calls this before every chart it renders, and
  // the drawing code depends on it: InitColors() is what turns the
  // element and ray colors (kElemA/kRayA, which the Colors dialog edits)
  // and the rulership restrictions into the per object kObjA[] table that
  // kSignA() and the glyph drawing actually read. Without it those
  // settings can be changed and saved but never visibly take effect.
  // Note this is a different function from InitColorsX() in xscreen.cpp,
  // which sets up the backend palette instead.
  InitColors();
  if (gi.qim != NULL) {
    delete gi.qim;
    gi.qim = NULL;
  }
  if (gs.xWin < 1)
    gs.xWin = 1;
  if (gs.yWin < 1)
    gs.yWin = 1;
  // Keep the buffer the size of the widget, then draw into a square part
  // of it if the chart wants that. Windows does the squaring in FActionX
  // (xscreen.cpp:2275) when "Ensure Square Charts Remain Square" is on and
  // the chart type is one that looks right square, which is why a
  // maximized window there keeps a round wheel with space beside it rather
  // than stretching it into an oval. The screen path here goes straight to
  // DrawChartX() and never passes through FActionX, so it does the same
  // thing itself.
  int dxWin = gs.xWin, dyWin = gs.yWin;
  gi.qim = new QImage(gs.xWin, gs.yWin, QImage::Format_RGB32);
  gi.qim->fill(Qt::black);
  gi.qpaint = new QPainter(gi.qim);
  // With no mode set, work one out from the chart flags the way FActionX
  // does (xscreen.cpp:2208). DetectGraphicsChartMode() falls through to
  // gWheel, which is how switching back to graphics from a text only chart
  // type lands on the main chart instead of on nothing.
  if (us.fGraphics && gi.nMode == 0)
    gi.nMode = DetectGraphicsChartMode();

  // Text mode draws characters into this same buffer rather than the
  // chart, which is what Windows does and is why the window shows the
  // text chart instead of going black while a second window holds it.
  if (!us.fGraphics) {
    SetTextMetricsQt();
    gi.qpaint->setFont(qi.fontText);
    qi.kvText = KvFromKi(kLtGrayA);
    is.cchRow = is.cchCol = is.cchColMax = 0;
    FILE *fileSav = is.S;
    is.S = stdout;
    Action();
    is.S = fileSav;
    delete gi.qpaint;
    gi.qpaint = NULL;
    gs.xWin = dxWin; gs.yWin = dyWin;
    if (gi.qcanvas != NULL)
      gi.qcanvas->update();
    return;
  }

  if (gs.fKeepSquare && fSquare) {
    // The sidebar isn't part of the square, so take it off before
    // squaring and put it back after, as Windows does.
    int dxSide = fSidebar ? (SIDESIZE * gi.nScaleText) >> 1 : 0, n;
    gs.xWin -= dxSide;
    n = Min(gs.xWin, gs.yWin);
    gs.xWin = gs.yWin = n;
    gs.xWin += dxSide;
  }
  // DrawChartX() derives gi.nScale from gs.nScale itself, but not the
  // text scale -- FActionX() is what normally calls AdjustTextScale(),
  // and the screen path here goes straight to DrawChartX(). Without this
  // gs.nScaleText (Graphics Settings' "Text Scale") never takes effect.
  // gi.nScaleT is FActionX()'s per-output-format multiplier, 1 for
  // anything drawn rather than written to a vector file.
  gi.nScaleT = 1;
  AdjustTextScale();
  if (qi.fHourglass)
    QApplication::setOverrideCursor(Qt::WaitCursor);
  DrawChartX();
  if (qi.fHourglass)
    QApplication::restoreOverrideCursor();
  delete gi.qpaint;
  gi.qpaint = NULL;
  // Put back what the squaring above changed, so the size the user set is
  // still the size Graphics Settings reports.
  gs.xWin = dxWin; gs.yWin = dyWin;
  if (gi.qcanvas != NULL) {
    // With the chart keeping its own size, the canvas has to be resized to
    // match whenever the chart changes size, or the scroll area would keep
    // scrolling over the old extent.
    if (!qi.fWindowChart &&
      (gi.qcanvas->width() != gs.xWin || gi.qcanvas->height() != gs.yWin))
      gi.qcanvas->resize(gs.xWin, gs.yWin);
    gi.qcanvas->update();
  }
  // Chart Resizes Window: fit the window around whatever was just drawn.
  if (qi.fChartWindow)
    ResizeWindowToChartQt();
#ifdef EXPRESS
  // Notify AstroExpression the screen has just been redrawn, as Windows
  // does at the end of its own redraw. The X11 path fires this too, but
  // from a block xscreen.cpp excludes both GUI builds from, so this one
  // never reached it.
  if (!us.fExpOff && FSzSet(us.szExpDisp3))
    ParseExpression(us.szExpDisp3);
#endif
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
  // Which cast to run depends on whether a relationship chart is selected,
  // exactly as Action() chooses (astrolog.cpp:242). A relationship chart
  // needs both charts computed, and CastChart() only does the one, so
  // calling it unconditionally left every second ring full of zeroes --
  // every object in the outer wheel sitting at 0 Aries. That affected all
  // eight relationship types, not just transits.
  //
  // The context argument matches Action()'s too: 1 rather than 0, which is
  // what tells the AstroExpression hooks this is the main chart.
  if (!us.nRel)
    CastChart(1);
  else
    CastRelation();
  RedrawQt();
}


// Add a checkbox style menu item bound directly to a flag field (us.fXxx or
// similar). Reflects *pfield's state when built, flips it and updates its
// own checked state on click, then applies the change. This is the pattern
// most of Windows' menu toggle commands follow (see e.g. cmdSidereal,
// wdriver.cpp:1611) -- one reusable helper here instead of hand written
// code per item.

void SyncProgressMenuQt()
{
  if (qi.paProgress != NULL)
    qi.paProgress->setChecked(us.fProgress != 0);
}

void SyncDisplayMenuQt()
{
  if (qi.paSeconds != NULL)
    qi.paSeconds->setChecked(us.fSeconds != 0);
  if (qi.paApplying != NULL)
    qi.paApplying->setChecked(us.nAppSep == 1);
}

void SyncHouseSetMenuQt()
{
  if (qi.paSolar != NULL)
    qi.paSolar->setChecked(us.objOnAsc != 0);
  if (qi.paHouse3D != NULL)
    qi.paHouse3D->setChecked(us.fHouse3D != 0);
  if (qi.paDwad != NULL)
    qi.paDwad->setChecked(us.nDwad > 0);
}

static QAction *AddToggleAction(QMenu *pmenu, CONST char *szLabel,
  flag *pfield, flag fRecast)
{
  QAction *pa = pmenu->addAction(szLabel);
  pa->setCheckable(true);
  pa->setChecked(*pfield != 0);
  ConnectMenuQt(pa, pa, [pfield, pa, fRecast]() {
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
  ConnectMenuQt(pa, pa, [value, ptarget, fRecast]() {
    *ptarget = value;
    if (fRecast)
      RecastAndRedrawQt();
    else
      RedrawQt();
  });
  return pa;
}


static QAction *AddChartModeAction(QMenu *pmenu, CONST char *szLabel,
  int mode)
{
  QAction *pa = pmenu->addAction(szLabel);
  pa->setCheckable(true);
  pa->setActionGroup(qi.pgroupChartMode);
  ConnectMenuQt(pa, pa, [mode]() {
    SetChartModeQt(mode);
  });
  qi.rgpaChartMode[qi.cChartMode] = pa;
  qi.rgnChartMode[qi.cChartMode] = mode;
  qi.cChartMode++;
  return pa;
}


// Same as AddChartModeAction(), but for the handful of chart modes that
// are actually text listings (Exoplanets Chart, and the Help menu's 11
// List Signs/Objects/etc actions) -- these need us.fGraphics forced false
// *before* SetChartModeQt() redraws, not after, or the first redraw would
// still take the graphics path. Also keeps the View menu's "Show Graphics"
// checkbox in sync, the same way Colored Text/Show Interpretations already
// do when they force text mode -- set once BuildViewMenu() runs.

// Keep the View menu's Show Graphics tick honest when something other than
// that menu item changes the mode -- the Transits dialog does, since its
// list chart types are text only.
void SyncGraphicsMenuQt()
{
  if (qi.paGraphics != NULL)
    qi.paGraphics->setChecked(us.fGraphics != 0);
}

static QAction *AddChartModeTextAction(QMenu *pmenu, CONST char *szLabel,
  int mode)
{
  QAction *pa = pmenu->addAction(szLabel);
  pa->setCheckable(true);
  pa->setActionGroup(qi.pgroupChartMode);
  ConnectMenuQt(pa, pa, [mode]() {
    us.fGraphics = fFalse;
    if (qi.paGraphics != NULL)
      qi.paGraphics->setChecked(fFalse);
    SetChartModeQt(mode);
  });
  qi.rgpaChartMode[qi.cChartMode] = pa;
  qi.rgnChartMode[qi.cChartMode] = mode;
  qi.cChartMode++;
  return pa;
}


// Switch chart type/mode, the same operation as Windows' ProcessState()
// (wdriver.cpp): clear every chart-type flag, then set the one matching the
// new mode. See qtdriver.h for the full comment. Both go through
// rgchartmode[] (xscreen.cpp), the one flag<->mode table -- this port kept
// its own copy of that mapping until the table was promoted to the core.
// SetChartModeQt() writes the flags through it; SnapChartModeQt()/
// SyncChartModeFromFlagsQt() read them.

// Move the Chart menu's radio to "mode", if it has an entry for it.
static void CheckChartModeMenuQt(int mode)
{
  int i;

  for (i = 0; i < qi.cChartMode; i++)
    if (qi.rgnChartMode[i] == mode) {
      qi.rgpaChartMode[i]->setChecked(true);
      break;
    }
}

void SetChartModeQt(int mode)
{
  int i;

  for (i = 0; i < cchartmode; i++)
    *rgchartmode[i].pf = fFalse;
  // DrawChartX() switches directly on gi.nMode with no fallback if it's 0,
  // and DetectGraphicsChartMode() (xscreen.cpp, normally what
  // (re)derives gi.nMode from the us.f* flags before a redraw) doesn't
  // cover several of these flags (fListing, fAspList, fArabic among them)
  // -- so rather than zero gi.nMode and rely on that detection like
  // Windows' ProcessState() does, set it directly to what was actually
  // selected, since that's already known here.
  gi.nMode = mode;
  for (i = 0; i < cchartmode; i++)
    if (rgchartmode[i].nMode == mode) {
      *rgchartmode[i].pf = fTrue;
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

  for (i = 0; i < cchartmode; i++)
    rgf[i] = *rgchartmode[i].pf;
}

void SyncChartModeFromFlagsQt(CONST flag *rgf)
{
  int i;

  for (i = 0; i < cchartmode; i++)
    if (*rgchartmode[i].pf && !rgf[i]) {
      SetChartModeQt(rgchartmode[i].nMode);
      return;
    }
}

int CChartModeQt()
{
  return cchartmode;
}


// Switch relationship chart type, mirroring Windows' SetRel()
// (wdriver.cpp:267-281).

void SetRelQt(int rc)
{
  CI ciT;
  int i, rcMenu;

  if (us.nRel == rcMidpoint) {  // Restore chart when leaving midpoint mode.
    ciT = ciMain;
    ciCore = ciMain = ciSave;
    ciSave = ciT;
  }
  if (rc == rcMidpoint)         // Remember chart so it can be restored.
    ciSave = ciMain;
  us.nRel = rc;
  // Windows' CmdFromRc() (wdriver.cpp:251) bullets Comparison for every
  // multi-wheel mode, not only rcDual, and the "Charts #3 Through #6"
  // dialog reaches all of them -- qtdialog.cpp calls here with rcTriWheel
  // through rcHexaWheel. Matching rc exactly would find no menu item for
  // those and leave the bullet wherever it happened to be.
  rcMenu = FBetween(rc, rcHexaWheel, rcDual) ? rcDual : rc;
  for (i = 0; i < qi.cRel; i++)
    if (qi.rgnRel[i] == rcMenu) {
      qi.rgpaRel[i]->setChecked(true);
      break;
    }
  RecastAndRedrawQt();
}

// Windows runs cmdRelNo and cmdRelComparison through one shared toggle --
// SetRel(us.nRel ? rcNone : rcDual), wdriver.cpp:1571 -- so either item
// turns off a relationship chart of any kind, and turns comparison on when
// there isn't one. That is why astrolog.rc gives both menu items the same
// "c" accelerator (lines 309-310), and it is the only way back to a single
// chart from the keyboard.
//
// Wiring each item to its own fixed mode looks right and is not: 'c' then
// set rcDual unconditionally, so from Alt+Shift+N a user reached the
// transit chart and could never leave it -- pressing 'c' again just set
// comparison again. Confirmed against the real Windows build under Wine:
// from transit mode, 'c' renders pixel-identical to the single chart it
// started from, and a second 'c' gives comparison.
//
// fToggle marks the two items that share that toggle. Every other mode is
// a plain set, as Windows has it.

static QAction *AddRelAction(QMenu *pmenu, QActionGroup *pgroup,
  CONST char *szLabel, int rc, flag fToggle = fFalse)
{
  QAction *pa = pmenu->addAction(szLabel);
  pa->setCheckable(true);
  pa->setActionGroup(pgroup);
  pa->setChecked(us.nRel == rc);
  ConnectMenuQt(pa, pa, [rc, fToggle]() {
    SetRelQt(fToggle ? (us.nRel ? rcNone : rcDual) : rc);
  });
  qi.rgpaRel[qi.cRel] = pa;
  qi.rgnRel[qi.cRel] = rc;
  qi.cRel++;
  return pa;
}


static void BuildFileMenu(QMainWindow *pwind)
{
  QMenu *pmenu = pwind->menuBar()->addMenu("&File");
  QAction *paOpen = pmenu->addAction("&Open Chart...");
  ConnectMenuQt(paOpen, pwind,
    []() { ShowOpenChartDialogQt(); });
  QAction *paOpen2 = pmenu->addAction("Open Chart #&2...");
  ConnectMenuQt(paOpen2, pwind,
    []() { ShowOpenChart2DialogQt(); });
  QAction *paSave = pmenu->addAction("&Save Chart Info...");
  ConnectMenuQt(paSave, pwind,
    []() { ShowSaveChartDialogQt(); });
  QAction *paSavePos = pmenu->addAction("Save Chart &Positions...");
  ConnectMenuQt(paSavePos, pwind,
    []() { ShowSaveChartPositionsDialogQt(); });
  pmenu->addSeparator();

  QAction *paSaveSettings = pmenu->addAction("Save Program Settin&gs...");
  ConnectMenuQt(paSaveSettings, pwind,
    []() { ShowSaveSettingsDialogQt(); });
  QMenu *pmenuOtherFormats = pmenu->addMenu("Ot&her Formats");
  QAction *paOpenDir = pmenuOtherFormats->addAction(
    "Open Charts in &Folder...");
  ConnectMenuQt(paOpenDir, pwind,
    []() { ShowOpenChartDirDialogQt(); });
  QAction *paSaveList = pmenuOtherFormats->addAction("Save Chart &List...");
  ConnectMenuQt(paSaveList, pwind,
    []() { ShowSaveChartListDialogQt(); });
  pmenuOtherFormats->addSeparator();
  QAction *paSaveAAF = pmenuOtherFormats->addAction(
    "Save Chart &Exchange...");
  ConnectMenuQt(paSaveAAF, pwind,
    []() { ShowSaveAAFDialogQt(); });
  QAction *paSaveQuick = pmenuOtherFormats->addAction(
    "Save Chart &Quick*Chart...");
  ConnectMenuQt(paSaveQuick, pwind,
    []() { ShowSaveQuickDialogQt(); });
  QAction *paSaveCalendar = pmenuOtherFormats->addAction(
    "Save Chart i&Calendar...");
  ConnectMenuQt(paSaveCalendar, pwind,
    []() { ShowSaveCalendarDialogQt(); });
  pmenu->addSeparator();

  QAction *paExportText = pmenu->addAction("Export Chart &Text Output...");
  ConnectMenuQt(paExportText, pwind,
    []() { ShowExportTextDialogQt(); });
  QAction *paExportBmp = pmenu->addAction("Export Chart &Bitmap...");
  ConnectMenuQt(paExportBmp, pwind,
    []() { ShowExportBitmapDialogQt(); });
  QMenu *pmenuVector = pmenu->addMenu("Export &Vector Format");
  QAction *paExportMeta = pmenuVector->addAction("Export Chart &Metafile...");
  ConnectMenuQt(paExportMeta, pwind,
    []() { ShowExportMetafileDialogQt(); });
  QAction *paExportPS = pmenuVector->addAction("Export Chart &PostScript...");
  ConnectMenuQt(paExportPS, pwind,
    []() { ShowExportPSDialogQt(); });
  QAction *paExportSVG = pmenuVector->addAction("Export Chart &SVG...");
  ConnectMenuQt(paExportSVG, pwind,
    []() { ShowExportSVGDialogQt(); });
  QAction *paExportWire = pmenuVector->addAction("Export Chart &Wireframe...");
  ConnectMenuQt(paExportWire, pwind,
    []() { ShowExportWireDialogQt(); });
  pmenu->addSeparator();

  QMenu *pmenuOpenBmp = pmenu->addMenu("Open Bit&map");
  QAction *paOpenBack = pmenuOpenBmp->addAction("Open Chart &Background...");
  ConnectMenuQt(paOpenBack, pwind,
    []() { ShowOpenBackgroundDialogQt(); });
  QAction *paOpenWorld = pmenuOpenBmp->addAction("Open &World Map...");
  ConnectMenuQt(paOpenWorld, pwind,
    []() { ShowOpenWorldDialogQt(); });
  QAction *paFileSettings = pmenu->addAction("&File Settings...");
  ConnectMenuQt(paFileSettings, pwind,
    []() { ShowFileSettingsDialogQt(); });
  pmenu->addSeparator();

  // Windows also has Print Setup here, which is its native printer
  // configuration dialog; Qt's print dialog covers that itself.
  QAction *paPrint = pmenu->addAction("P&rint...");
  ConnectMenuQt(paPrint, pwind,
    []() { PrintChartQt(); });
  pmenu->addSeparator();

  QAction *paQuit = pmenu->addAction("E&xit");
  ConnectMenuQt(paQuit, pwind,
    [pwind]() { pwind->close(); });
}


static void BuildViewMenu(QMainWindow *pwind)
{
  QMenu *pmenu = pwind->menuBar()->addMenu("&View");
  // Not AddToggleAction: turning graphics back on has to re-derive the
  // chart mode. Several chart types are text only -- the aspect list, the
  // transit lists, the help listings -- and DrawChartX() has nothing to
  // draw for them, so toggling straight back left the window blank.
  // Zeroing gi.nMode makes RedrawQt() work one out from the chart flags,
  // which is what Windows does by way of ProcessState() and FActionX().
  qi.paGraphics = pmenu->addAction("Show &Graphics");
  qi.paGraphics->setCheckable(fTrue);
  qi.paGraphics->setChecked(us.fGraphics != 0);
  ConnectMenuQt(qi.paGraphics, pwind, []() {
    us.fGraphics = !us.fGraphics;
    qi.paGraphics->setChecked(us.fGraphics != 0);
    if (us.fGraphics)
      gi.nMode = 0;
    RedrawQt();
  });

  // Window Settings. Windows' "Buffer Redraws" is deliberately absent:
  // it toggles whether Win32 draws through an off screen bitmap, and Qt
  // composites every widget off screen regardless, so there is nothing
  // for it to switch. A toggle that silently does nothing would be worse
  // than not offering it.
  QMenu *pmenuWin = pmenu->addMenu("&Window Settings");
  QAction *paRedraw = pmenuWin->addAction("&Redraw Screen");
  ConnectMenuQt(paRedraw, pwind,
    []() { RedrawForceQt(); });
  QAction *paClear = pmenuWin->addAction("&Clear Screen");
  ConnectMenuQt(paClear, pwind,
    []() { ClearScreenQt(); });
  QAction *paHourglass = pmenuWin->addAction("&Hourglass on Redraw");
  paHourglass->setCheckable(true);
  paHourglass->setChecked(qi.fHourglass != fFalse);
  ConnectMenuQt(paHourglass, pwind,
    [paHourglass]() {
      qi.fHourglass = !qi.fHourglass;
      paHourglass->setChecked(qi.fHourglass != fFalse);
    });
  pmenuWin->addSeparator();

  QAction *paChartWin = pmenuWin->addAction("Ch&art Resizes Window");
  paChartWin->setCheckable(true);
  paChartWin->setChecked(qi.fChartWindow != fFalse);
  ConnectMenuQt(paChartWin, pwind,
    [paChartWin]() {
      qi.fChartWindow = !qi.fChartWindow;
      paChartWin->setChecked(qi.fChartWindow != fFalse);
      if (qi.fChartWindow)
        ResizeWindowToChartQt();
    });
  QAction *paWinChart = pmenuWin->addAction("&Window Resizes Chart");
  paWinChart->setCheckable(true);
  paWinChart->setChecked(qi.fWindowChart != fFalse);
  ConnectMenuQt(paWinChart, pwind,
    [paWinChart]() {
      qi.fWindowChart = !qi.fWindowChart;
      paWinChart->setChecked(qi.fWindowChart != fFalse);
      ApplySizeModeQt();
    });
  QAction *paSizeChart = pmenuWin->addAction("Si&ze Chart to Window");
  ConnectMenuQt(paSizeChart, pwind,
    []() { SizeChartToWindowQt(); });
  QAction *paSizeWin = pmenuWin->addAction("&Size Window to Chart");
  ConnectMenuQt(paSizeWin, pwind,
    []() { ResizeWindowToChartQt(); });
  QAction *paFull = pmenuWin->addAction("Size Window &Full Screen");
  paFull->setCheckable(true);
  ConnectMenuQt(paFull, pwind,
    [paFull]() {
      ToggleFullScreenQt();
      paFull->setChecked(gi.qwind != NULL && gi.qwind->isFullScreen());
    });
  pmenuWin->addSeparator();

  QAction *paScrollUp = pmenuWin->addAction("Scroll Page &Up");
  ConnectMenuQt(paScrollUp, pwind,
    []() { ScrollChartQt(-1); });
  QAction *paScrollDown = pmenuWin->addAction("Scroll Page &Down");
  ConnectMenuQt(paScrollDown, pwind,
    []() { ScrollChartQt(1); });
  QAction *paScrollHome = pmenuWin->addAction("Scroll &to Beginning");
  ConnectMenuQt(paScrollHome, pwind,
    []() { ScrollChartQt(0); });
  QAction *paScrollEnd = pmenuWin->addAction("Scroll to &End");
  ConnectMenuQt(paScrollEnd, pwind,
    []() { ScrollChartQt(2); });

  QAction *paColorText = pmenu->addAction("&Colored Text");
  paColorText->setCheckable(true);
  paColorText->setChecked(us.fAnsiColor != 0);
  ConnectMenuQt(paColorText, pwind,
    [paColorText]() {
      us.fAnsiColor = !us.fAnsiColor;
      us.fAnsiChar = !us.fAnsiChar;
      paColorText->setChecked(us.fAnsiColor != 0);
      us.fGraphics = fFalse;
      qi.paGraphics->setChecked(fFalse);
      RedrawQt();
    });
  QAction *paColors = pmenu->addAction("&Set Colors...");
  ConnectMenuQt(paColors, pwind,
    []() { ShowColorDialogQt(); });
  pmenu->addSeparator();
  QAction *paInterpret = pmenu->addAction("Show &Interpretations");
  paInterpret->setCheckable(true);
  paInterpret->setChecked(us.fInterpret != 0);
  ConnectMenuQt(paInterpret, pwind,
    [paInterpret]() {
      us.fInterpret = !us.fInterpret;
      paInterpret->setChecked(us.fInterpret != 0);
      us.fGraphics = fFalse;
      qi.paGraphics->setChecked(fFalse);
      RedrawQt();
    });
  qi.paSeconds = AddToggleAction(pmenu, "Print &Nearest Second", &us.fSeconds,
    fFalse);
  AddToggleAction(pmenu, "&Parallel Aspects", &us.fParallel, fFalse);
  // Not AddToggleAction: nAppSep has three values, and the checkmark means
  // specifically "Applying/Separating", not "non-zero" -- Windows checks
  // "us.nAppSep == 1" everywhere (cmdApplying in wdriver.cpp), so Waxing/
  // Waning (2) shows unchecked. The toggle itself is still inv(), which is
  // what Windows does too, oddly enough.
  qi.paApplying = pmenu->addAction("&Applying Aspects");
  qi.paApplying->setCheckable(true);
  qi.paApplying->setChecked(us.nAppSep == 1);
  ConnectMenuQt(qi.paApplying, pwind, []() {
    us.nAppSep = !us.nAppSep;
    qi.paApplying->setChecked(us.nAppSep == 1);
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
  ConnectMenuQt(pa, pa, [nDir]() {
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
  QAction *paInfo = pmenu->addAction("Set Chart &Info...");
  ConnectMenuQt(paInfo, pwind,
    []() { ShowChartInfoDialogQt(); });
  QAction *paNow = pmenu->addAction("Chart for &Now");
  ConnectMenuQt(paNow, pwind, []() {
    FInputData(szNowCore);
    RecastAndRedrawQt();
  });
  QAction *paDefault = pmenu->addAction("D&efault Chart Info...");
  ConnectMenuQt(paDefault, pwind,
    []() { ShowDefaultInfoDialogQt(); });
  pmenu->addSeparator();

  QAction *paInfo2 = pmenu->addAction("Set Chart #&2 Info...");
  ConnectMenuQt(paInfo2, pwind,
    []() { ShowChartInfo2DialogQt(); });
  QAction *paInfoAll = pmenu->addAction("Charts #&3 Through #6...");
  ConnectMenuQt(paInfoAll, pwind,
    []() { ShowChartsAllDialogQt(); });
  QMenu *pmenuList = pmenu->addMenu("Chart &List");
  QAction *paList = pmenuList->addAction("&Chart List...");
  ConnectMenuQt(paList, pwind,
    []() { ShowChartListDialogQt(); });
  pmenuList->addSeparator();
  AddChartListNavAction(pmenuList, "&Previous Chart", -1);
  AddChartListNavAction(pmenuList, "&Next Chart", 1);
  pmenuList->addSeparator();
  AddChartListNavAction(pmenuList, "&First Chart", -2);
  AddChartListNavAction(pmenuList, "&Last Chart", 2);
  pmenuList->addSeparator();
  QAction *paSwap = pmenuList->addAction("Swap Chart #&1 and #2");
  ConnectMenuQt(paSwap, pwind, []() {
    CI ciT;
    SwapTemp(ciCore, ciTwin, ciT);
    RecastAndRedrawQt();
  });
  pmenu->addSeparator();

  QActionGroup *pgroup = new QActionGroup(pwind);
  AddRelAction(pmenu, pgroup, "No &Relationship Chart", rcNone, fTrue);
  AddRelAction(pmenu, pgroup, "Com&parison Chart", rcDual, fTrue);
  AddRelAction(pmenu, pgroup, "&Synastry Chart", rcSynastry);
  AddRelAction(pmenu, pgroup, "&Composite Chart", rcComposite);
  AddRelAction(pmenu, pgroup, "Time Space &Midpoint Chart", rcMidpoint);
  pmenu->addSeparator();
  AddRelAction(pmenu, pgroup, "Date &Difference Chart", rcDifference);
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
void SyncRestrictMenuQt()
{
  int i, j;

  for (i = 0; i < qi.ccatres; i++) {
    CATRES *pcat = &qi.rgcatres[i];
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
  if (pfield != NULL && qi.ccatres < (int)(sizeof(qi.rgcatres)/sizeof(CATRES))) {
    CATRES *pcat = &qi.rgcatres[qi.ccatres++];
    pcat->pa = pa; pcat->pfield = pfield; pcat->lo = lo; pcat->hi = hi;
    pcat->fTransit = fTransit;
  }
  ConnectMenuQt(pa, pa,
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


void SyncHelioMenuQt()
{
  if (qi.paHelio != NULL)
    qi.paHelio->setChecked(us.objCenter != oEar);
}

static void BuildSettingMenu(QMainWindow *pwind)
{
  QMenu *pmenu = pwind->menuBar()->addMenu("&Setting");
  AddToggleAction(pmenu, "&Sidereal Zodiac", &us.fSidereal, fTrue);
  qi.paHelio = pmenu->addAction("He&liocentric");
  qi.paHelio->setCheckable(true);
  qi.paHelio->setChecked(us.objCenter != oEar);
  ConnectMenuQt(qi.paHelio, pwind, []() {
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
  AddSelectAction(pmenuHouse, pgroupHouse, "&Campanus", 3,
    &us.nHouseSystem, fTrue);
  AddSelectAction(pmenuHouse, pgroupHouse, "&Regiomontanus", 5,
    &us.nHouseSystem, fTrue);
  AddSelectAction(pmenuHouse, pgroupHouse, "&Topocentric", 8,
    &us.nHouseSystem, fTrue);
  AddSelectAction(pmenuHouse, pgroupHouse, "Alca&bitius", 9,
    &us.nHouseSystem, fTrue);
  AddSelectAction(pmenuHouse, pgroupHouse, "Kr&usinski", 10,
    &us.nHouseSystem, fTrue);
  AddSelectAction(pmenuHouse, pgroupHouse, "A&.P.C.", 18,
    &us.nHouseSystem, fTrue);
  AddSelectAction(pmenuHouse, pgroupHouse, "Savard-&A", 21,
    &us.nHouseSystem, fTrue);
  pmenuHouse->addSeparator();
  AddSelectAction(pmenuHouse, pgroupHouse, "Porph&yry", 6,
    &us.nHouseSystem, fTrue);
  AddSelectAction(pmenuHouse, pgroupHouse, "Pullen (S.Rati&o)", 12,
    &us.nHouseSystem, fTrue);
  AddSelectAction(pmenuHouse, pgroupHouse, "Pullen (S.&Delta)", 13,
    &us.nHouseSystem, fTrue);
  pmenuHouse->addSeparator();
  AddSelectAction(pmenuHouse, pgroupHouse, "&Meridian", 4,
    &us.nHouseSystem, fTrue);
  AddSelectAction(pmenuHouse, pgroupHouse, "Morinu&s", 7,
    &us.nHouseSystem, fTrue);
  AddSelectAction(pmenuHouse, pgroupHouse, "Hori&zon", 17,
    &us.nHouseSystem, fTrue);
  AddSelectAction(pmenuHouse, pgroupHouse, "Carter& P.Equat.", 19,
    &us.nHouseSystem, fTrue);
  AddSelectAction(pmenuHouse, pgroupHouse, "Suns&hine", 20,
    &us.nHouseSystem, fTrue);
  AddSelectAction(pmenuHouse, pgroupHouse, "Sr&ipati", 16,
    &us.nHouseSystem, fTrue);
  pmenuHouse->addSeparator();
  AddSelectAction(pmenuHouse, pgroupHouse, "&Equal", 2,
    &us.nHouseSystem, fTrue);
  AddSelectAction(pmenuHouse, pgroupHouse, "E&qual (MC)", 11,
    &us.nHouseSystem, fTrue);
  AddSelectAction(pmenuHouse, pgroupHouse, "&Whole", 14,
    &us.nHouseSystem, fTrue);
  AddSelectAction(pmenuHouse, pgroupHouse, "&Vedic", 15,
    &us.nHouseSystem, fTrue);
  AddSelectAction(pmenuHouse, pgroupHouse, "&Null", 22,
    &us.nHouseSystem, fTrue);

  QMenu *pmenuHouseSet = pmenu->addMenu("House S&ettings");
  qi.paSolar = pmenuHouseSet->addAction("&Solar Chart");
  qi.paSolar->setCheckable(true);
  qi.paSolar->setChecked(us.objOnAsc != 0);
  ConnectMenuQt(qi.paSolar, pwind, []() {
    us.objOnAsc = us.objOnAsc ? 0 : oSun+1;
    qi.paSolar->setChecked(us.objOnAsc != 0);
    RecastAndRedrawQt();
  });
  qi.paHouse3D = AddToggleAction(pmenuHouseSet, "&3D Houses", &us.fHouse3D,
    fTrue);
  pmenuHouseSet->addSeparator();
  AddToggleAction(pmenuHouseSet, "Show &Decans", &us.fDecan, fTrue);
  qi.paDwad = AddToggleAction(pmenuHouseSet, "Show D&wads", &us.nDwad, fTrue);
  AddToggleAction(pmenuHouseSet, "&Flip Signs with Houses", &us.fFlip, fTrue);
  AddToggleAction(pmenuHouseSet, "&Geodetic Houses", &us.fGeodetic, fTrue);
  pmenuHouseSet->addSeparator();
  AddToggleAction(pmenuHouseSet, "&Indian Wheel Order", &us.fIndian, fFalse);
  AddToggleAction(pmenuHouseSet, "Show &Navamsas", &us.fNavamsa, fTrue);

  QAction *paAspect = pmenu->addAction("&Aspect Settings...");
  ConnectMenuQt(paAspect, pwind,
    []() { ShowAspectDialogQt(); });
  QAction *paObject = pmenu->addAction("&Object Settings...");
  ConnectMenuQt(paObject, pwind,
    []() { ShowObjectDialogQt(); });
  QAction *paObject2 = pmenu->addAction("More Ob&ject Settings...");
  ConnectMenuQt(paObject2, pwind,
    []() { ShowObject2DialogQt(); });
  QAction *paObjectSel = pmenu->addAction("Object Selectio&ns...");
  ConnectMenuQt(paObjectSel, pwind,
    []() { ShowObjectSelDialogQt(); });
  pmenu->addSeparator();

  QAction *paRestrict = pmenu->addAction("&Restrictions...");
  ConnectMenuQt(paRestrict, pwind,
    []() { ShowRestrictDialogQt(); });
  QAction *paStarRestrict = pmenu->addAction("Star Restr&ictions...");
  ConnectMenuQt(paStarRestrict, pwind,
    []() { ShowStarRestrictDialogQt(); });
  QAction *paTransitRestrict = pmenu->addAction("&Transit Restrictions...");
  ConnectMenuQt(paTransitRestrict, pwind,
    []() { ShowTransitRestrictDialogQt(); });

  QMenu *pmenuMoons = pmenu->addMenu("&Planetary Moons");
  AddChartModeAction(pmenuMoons, "&Moons Chart", gMoons);
  // Windows also forces text mode when switching to this chart type
  // (cmdChartExo, wdriver.cpp).
  AddChartModeTextAction(pmenuMoons, "&Exoplanets Chart", gExo);
  pmenuMoons->addSeparator();
  QAction *paMoonRestrict = pmenuMoons->addAction("Moon &Restrictions...");
  ConnectMenuQt(paMoonRestrict, pwind,
    []() { ShowMoonRestrictDialogQt(); });
  QAction *paMoonObject = pmenuMoons->addAction("Moon &Object Settings...");
  ConnectMenuQt(paMoonObject, pwind,
    []() { ShowMoonObjectDialogQt(); });
  pmenuMoons->addSeparator();
  QAction *paCustom = pmenuMoons->addAction("Object &Customization...");
  ConnectMenuQt(paCustom, pwind,
    []() { ShowCustomDialogQt(); });
  QAction *paCustomS = pmenuMoons->addAction("&Star Customization...");
  ConnectMenuQt(paCustomS, pwind,
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
  AddCategoryRestrictAction(pmenu, "&Include Moons", &us.fMoons, moonsLo,
    moonsHi, -1, fTrue);
  AddCategoryRestrictAction(pmenu, "Include &Body Centers (COB)", &us.fCOB,
    cobLo, cobHi, -1, fTrue);
  pmenu->addSeparator();

  QAction *paCalc = pmenu->addAction("Calculation Settin&gs...");
  ConnectMenuQt(paCalc, pwind,
    []() { ShowCalcDialogQt(); });
  QAction *paDisplay = pmenu->addAction("&Display Settings...");
  ConnectMenuQt(paDisplay, pwind,
    []() { ShowDisplayDialogQt(); });
}


static void BuildChartMenu(QMainWindow *pwind)
{
  QMenu *pmenu = pwind->menuBar()->addMenu("&Chart");
  int i;
  AddChartModeAction(pmenu, "Standard Radi&x", gWheel);
  AddChartModeAction(pmenu, "House &Wheel", gHouse);
  AddChartModeAction(pmenu, "Aspect Midpoint &Grid", gGrid);
  // Aspect List and Arabic Parts have no case in DrawChartX(), so drawing
  // either with graphics on yields an empty window. Windows sets
  // us.fGraphics = fFalse for exactly these two chart types (wdriver.cpp
  // cmdChartAspect and cmdChartArabic) and no others; match that.
  AddChartModeTextAction(pmenu, "&Aspect List", gAspect);
  AddChartModeAction(pmenu, "&Midpoint List", gMidpoint);
  AddChartModeAction(pmenu, "Local Hori&zon", gHorizon);
  AddChartModeAction(pmenu, "Solar System &Orbit", gOrbit);
  AddChartModeAction(pmenu, "Ga&uquelin Sectors", gSector);
  AddChartModeAction(pmenu, "&Calendar", gCalendar);
  AddChartModeAction(pmenu, "Inf&luence", gDisposit);
  AddChartModeAction(pmenu, "Esoter&ic", gEsoteric);
  AddChartModeAction(pmenu, "Astrocartograp&hy", gAstroGraph);
  AddChartModeAction(pmenu, "&Ephemeris", gEphemeris);
  AddChartModeTextAction(pmenu, "Ara&bic Parts", gArabic);
  AddChartModeAction(pmenu, "Risi&ng and Setting", gRising);
  AddChartModeAction(pmenu, "Nea&rest Cities", gLocal);
  // The chart starts on the standard radix; reflect that in the menu. Not
  // qi.rgpaChartMode[0] -- since BuildSettingMenu()'s Planetary Moons items
  // share this same group and are added before this menu is built, index 0
  // is no longer reliably "Standard Radix".
  for (i = 0; i < qi.cChartMode; i++)
    if (qi.rgnChartMode[i] == gWheel) {
      qi.rgpaChartMode[i]->setChecked(true);
      break;
    }
  pmenu->addSeparator();

  QAction *paTransit = pmenu->addAction("&Transits...");
  ConnectMenuQt(paTransit, pwind,
    []() { ShowTransitDialogQt(); });
  // Windows ticks this while progressions are on (WiCheckMenu with
  // cmdProgress in DlgProgress), so it does here too.
  qi.paProgress = pmenu->addAction("&Progressions...");
  qi.paProgress->setCheckable(fTrue);
  qi.paProgress->setChecked(us.fProgress != 0);
  ConnectMenuQt(qi.paProgress, pwind,
    []() { ShowProgressDialogQt(); });
  pmenu->addSeparator();
  QAction *paSettings = pmenu->addAction("Chart &Settings...");
  ConnectMenuQt(paSettings, pwind,
    []() { ShowChartSettingsDialogQt(); });
}


// Windows' Graphics menu (wdriver.cpp cmdGraphics* handlers), in full.

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
  ConnectMenuQt(paReverse, pwind, [paReverse]() {
    gs.fInverse = !gs.fInverse;
    paReverse->setChecked(gs.fInverse != 0);
    InitColorPalette(gs.fInverse);
    RedrawQt();
  });
  QAction *paMono = pmenu->addAction("&Monochrome");
  paMono->setCheckable(true);
  paMono->setChecked(!gs.fColor);
  ConnectMenuQt(paMono, pwind, [paMono]() {
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
  ConnectMenuQt(paSquare, pwind, []() {
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
  ConnectMenuQt(paScaleDn, pwind, []() {
    if (gs.nScale > 100) { gs.nScale -= 100; RedrawQt(); }
  });
  QAction *paScaleUp = pmenuScale->addAction("&Increase");
  ConnectMenuQt(paScaleUp, pwind, []() {
    if (gs.nScale < MAXSCALE) { gs.nScale += 100; RedrawQt(); }
  });
  pmenuScale->addSeparator();
  QAction *paTextDn = pmenuScale->addAction("D&ecrease Text");
  ConnectMenuQt(paTextDn, pwind, []() {
    if (gs.nScaleText > 100) {
      gs.nScaleText -= 50; gs.fAutoScale = fFalse; RedrawQt();
    }
  });
  QAction *paTextUp = pmenuScale->addAction("I&ncrease Text");
  ConnectMenuQt(paTextUp, pwind, []() {
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
  ConnectMenuQt(paSidebar, pwind, [paSidebar]() {
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
  ConnectMenuQt(paConstel, pwind, [paConstel]() {
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
  paStarLine->setChecked(qi.fStarLine != 0);
  ConnectMenuQt(paStarLine, pwind, [paStarLine]() {
    CONST char **ppch;
    qi.fStarLine = !qi.fStarLine;
    paStarLine->setChecked(qi.fStarLine != 0);
    if (qi.fStarLine) {
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
  ConnectMenuQt(paRotWest, pwind, []() {
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
  ConnectMenuQt(paRotEast, pwind, []() {
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
  ConnectMenuQt(paTiltNorth, pwind, []() {
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
  ConnectMenuQt(paTiltSouth, pwind, []() {
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
  ConnectMenuQt(paTiltZero, pwind, []() {
    gs.rTilt = 0.0;
    us.fGraphics = fTrue;
    if (gi.nMode != gTelescope && gi.nMode != gSphere && gi.nMode != gGlobe)
      SetChartModeQt(gGlobe);
    else
      RedrawQt();
  });
  pmenuOrient->addSeparator();
  QAction *paZoomOut = pmenuOrient->addAction("Zoom &Out");
  ConnectMenuQt(paZoomOut, pwind, []() {
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
  ConnectMenuQt(paZoomIn, pwind, []() {
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
  ConnectMenuQt(paIndianS, pwind, []() {
    gs.fIndianWheel = fTrue;
    gs.fHouseExtra = fFalse;
    SetChartModeQt(gWheel);
  });
  QAction *paIndianN = pmenuIndian->addAction("Draw &North Indian");
  ConnectMenuQt(paIndianN, pwind, []() {
    gs.fIndianWheel = fTrue;
    SetChartModeQt(gHouse);
  });
  QAction *paIndianE = pmenuIndian->addAction("Draw &East Indian");
  ConnectMenuQt(paIndianE, pwind, []() {
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
  ConnectMenuQt(paChartModify, pwind, []() {
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
  ConnectMenuQt(paGraphicsSettings, pwind,
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
    QVector<flag> rgfMode(cchartmode);
    SnapChartModeQt(rgfMode.data());
    FProcessCommandLine(is.rgszMacro[iMacro]);
    SyncChartModeFromFlagsQt(rgfMode.constData());
    RecastAndRedrawQt();
    return;
  }
  if (iMacro == 1) {
    if (FileOpen("astrolog.htm", 2, S(szPath)) != NULL)
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
    QMenu *pmenuSet = pmenu->addMenu(iSet < cMSub && qi.rgszMSub[iSet] != NULL ?
      QString(qi.rgszMSub[iSet]) : QString(rgszMacroSetQt[iSet]));
    for (iKey = 0; iKey < 12; iKey++) {
      int iMacro = iSet*12 + iKey + 1;
      QAction *pa = pmenuSet->addAction(qi.rgszMacro[iMacro-1] != NULL ?
        QString(qi.rgszMacro[iMacro-1]) : QString("Macro %1").arg(iMacro));
      // The shortcut makes the key work without opening the menu, which
      // is how Windows' accelerator table has it.
      pa->setShortcut(QKeySequence(SzMacroKeyQt(iSet, iKey)));
      ConnectMenuQt(pa, pwind,
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
// only runs them, exactly as Windows' does. A macro's menu entry can be
// renamed with -WM, and a whole submenu with -WM0, the same as on Windows;
// see NProcessSwitchesQt().

static void BuildEditMenu(QMainWindow *pwind)
{
  QMenu *pmenu = pwind->menuBar()->addMenu("&Edit");
  QAction *paCommand = pmenu->addAction("Enter Command &Line...");
  ConnectMenuQt(paCommand, pwind,
    []() { ShowCommandLineDialogQt(); });
  pmenu->addSeparator();
  BuildMacroMenus(pmenu, pwind);
  pmenu->addSeparator();
  QAction *paPaste = pmenu->addAction("&Paste");
  ConnectMenuQt(paPaste, pwind,
    []() { PasteChartQt(); });
  pmenu->addSeparator();
  pmenu->addSeparator();

  QAction *paCopyText = pmenu->addAction("Copy Chart &Text Output");
  ConnectMenuQt(paCopyText, pwind,
    []() { CopyChartTextQt(); });
  QAction *paCopyBmp = pmenu->addAction("Copy Chart &Bitmap");
  ConnectMenuQt(paCopyBmp, pwind,
    []() { CopyChartBitmapQt(); });
  QMenu *pmenuCopyVector = pmenu->addMenu("Copy &Vector Format");
  QAction *paCopyMeta = pmenuCopyVector->addAction("Copy Chart &Metafile");
  ConnectMenuQt(paCopyMeta, pwind,
    []() { CopyChartMetafileQt(); });
  QAction *paCopyPS = pmenuCopyVector->addAction("Copy Chart &PostScript");
  ConnectMenuQt(paCopyPS, pwind,
    []() { CopyChartPSQt(); });
  QAction *paCopySVG = pmenuCopyVector->addAction("Copy Chart &SVG");
  ConnectMenuQt(paCopySVG, pwind,
    []() { CopyChartSVGQt(); });
  QAction *paCopyWire = pmenuCopyVector->addAction("Copy Chart &Wireframe");
  ConnectMenuQt(paCopyWire, pwind,
    []() { CopyChartWireQt(); });
}


int NAntialiasQt() { return qi.nAntialias; }
void SetAntialiasQt(int n) { qi.nAntialias = n; }
// The animation interval, in milliseconds. Windows keeps this in
// wi.nTimerDelay, which is Win32-only, so the Qt build owns its own copy;
// Graphics Settings edits it through these.
// Two readbacks the AstroExpression functions need, so express.cpp can
// answer "Dlg" and "Mouse" here as it does on Windows. The "-W" settings
// they sit beside already have accessors; autosave and the screen saver
// have no counterpart in this build and report off. See item 46.

// Windows' KvDialog(): put up the colour picker and return what was
// chosen, or the current foreground colour if the user cancelled.

KV KvDialogQt()
{
  KV kv = KvFromKi(gi.kiOn);
  QColor col = QColorDialog::getColor(
    QColor(RgbR(kv), RgbG(kv), RgbB(kv)), gi.qwind, "Choose Color");

  if (!col.isValid())
    return kv;
  return Rgb(col.red(), col.green(), col.blue());
}


// Windows maps the cursor to client coordinates; the equivalent here is
// the canvas the chart is painted on.

void MousePosQt(int *px, int *py)
{
  QPoint pt = QCursor::pos();

  if (gi.qcanvas != NULL)
    pt = gi.qcanvas->mapFromGlobal(pt);
  *px = pt.x(); *py = pt.y();
}


int NAnimDelayQt()
{
  return qi.nTimerDelay;
}

void SetAnimDelayQt(int nDelay)
{
  qi.nTimerDelay = nDelay;
  if (qi.ptimerAnim != NULL)
    qi.ptimerAnim->setInterval(nDelay);
}

/*
******************************************************************************
** The -W command switches.
******************************************************************************
*/

// Windows parses the -W switch family in NProcessSwitchesW() (wdriver.cpp),
// inside #ifdef WIN. That means a settings file written by the Windows
// build -- which is what a Windows user coming to this port arrives with --
// hits "Unknown switch '-WM'" here and Astrolog stops reading the file at
// that line, taking every setting after it with it. Since astrolog.as is
// meant to be portable between the two builds, this handles the same
// switches: the ones with a Qt equivalent do their job, and the ones that
// are meaningful only to Win32 are accepted and ignored rather than being
// allowed to abort the file.

// Read back what -WM/-WM0/-Wh set, so FOutputSettings() can write them
// out again. The arrays are file static here, and io.cpp is shared code
// that must not see them directly.
CONST char *SzMacroNameQt(int i)
{
  return FBetween(i, 0, cMacro-1) ? qi.rgszMacro[i] : NULL;
}

CONST char *SzMacroSubNameQt(int i)
{
  return FBetween(i, 0, cMSub-1) ? qi.rgszMSub[i] : NULL;
}

flag FHourglassQt() { return qi.fHourglass; }


int NProcessSwitchesQt(int pos, PARSEIN *pin)
{
  int darg = 0, i, j;
  char ch1;

  ch1 = pin->argv[0][pos+1];
  switch (pin->argv[0][pos]) {
  case chNull:
    // -W <n> invokes a menu command by its Windows command ID. Those IDs
    // don't exist here, so consume the argument and move on.
    if (FErrorArgc("W", pin->argc, 1))
      return tcError;
    darg++;
    break;

  case 'N':
    if (FErrorArgc("WN", pin->argc, 1))
      return tcError;
    i = NFromSz(pin->argv[1]);
    if (FErrorValN("WN", !FValidTimer(i), i, 0))
      return tcError;
    SetAnimDelayQt(i);
    darg++;
    break;

  case 'M':
    if (FErrorArgc("WM", pin->argc, 2))
      return tcError;
    i = NFromSz(pin->argv[1]);
    if (ch1 != '0') {
      if (FErrorValN("WM", !FValidMacro2(i), i, 1))
        return tcError;
      i--;
      FCloneSz(pin->argv[2], &qi.rgszMacro[i]);
    } else {
      if (FErrorValN("WM0", !FBetween(i, 0, cMSub-1), i, 1))
        return tcError;
      FCloneSz(pin->argv[2], &qi.rgszMSub[i]);
    }
    darg += 2;
    break;

  case 'h':
    SwitchF(qi.fHourglass);
    break;

  case 'T':
    if (FErrorArgc("WT", pin->argc, 1))
      return tcError;
    if (gi.qwind != NULL)
      gi.qwind->setWindowTitle(pin->argv[1]);
    darg++;
    break;

  case 'w':
    // Window position. Only meaningful once there's a window; when this
    // comes from astrolog.as there isn't one yet, so remember it.
    if (FErrorArgc("Ww", pin->argc, 2))
      return tcError;
    i = NFromSz(pin->argv[1]); j = NFromSz(pin->argv[2]);
    if (gi.qwind != NULL)
      gi.qwind->move(i, j);
    else {
      qi.xWind = i; qi.yWind = j; qi.fWindPos = fTrue;
    }
    darg += 2;
    break;

  case 'B':
    if (FErrorArgc("WB", pin->argc, 2))
      return tcError;
    darg += 2;
    break;

  case 'x':
    // Antialiasing zoom scale. Windows renders the chart at this multiple
    // and shrinks it down; nothing here does that yet, so just validate
    // and remember the value so it survives a settings round trip.
    if (FErrorArgc("Wx", pin->argc, 1))
      return tcError;
    i = NFromSz(pin->argv[1]);
    qi.nAntialias = i;
    darg++;
    break;

  // Win32-only behaviour with no Qt counterpart. Accepted so that a shared
  // astrolog.as keeps loading; each is a no-op here.
  case 'n': case 't': case 'b': case 'Z':
    break;

  case 'o':
    if (ch1 == 'n' || ch1 == 'w')
      break;
    break;

  case 'S':
    // Windows installer actions: program group, desktop icon, file
    // associations. None apply to a Linux build.
    break;

  default:
    ErrorSwitch(pin->argv[0]);
    return tcError;
  }
  return darg;
}


static void StartAnimTimerQt(QMainWindow *pwind)
{
  qi.ptimerAnim = new QTimer(pwind);
  QObject::connect(qi.ptimerAnim, &QTimer::timeout, pwind, []() {
    // Same guard Windows' WM_TIMER uses. Note gs.nAnim < 1 covers both
    // "off" (negative, remembering the rate) and "never set".
    if (gs.nAnim < 1 || gi.fPause)
      return;
    Animate(gs.nAnim, gi.nDir);
    RecastAndRedrawQt();
  });
  qi.ptimerAnim->start(qi.nTimerDelay);
}


// Animation state.
//
// Upstream stores two things in one int: the magnitude of gs.nAnim is the
// jump rate, and its sign is whether animation is running. gi.fPause is a
// second, independent stop on top of that. The encoding has to stay -- the
// -Xn switch and saved settings both depend on it (xscreen.cpp:1814) --
// but it is written down here once, and nothing else in this file reasons
// about it. Six call sites used to open-code "(gs.nAnim < 0 ? -1 : 1) * x"
// and "neg(gs.nAnim)", and a sign wrong in any of them is invisible until
// something moves that shouldn't. Three of this port's animation bugs were
// exactly that.
//
// Above this line there is one idea, not two: animation is running or it
// isn't, and one control starts and stops it. There is no separate "arm
// it first" step, which is a divergence from Windows and a deliberate one
// -- see "Known divergences" in QT_GUI_PLAN.md.

static flag FAnimRunningQt(void) { return gs.nAnim >= 1 && !gi.fPause; }

// The jump rate, never zero: a stopped state still remembers one, and a
// settings file that never set -Xn leaves nothing useful behind.
static int NAnimRateQt(void)
{
  int n = NAbs(gs.nAnim);
  return n >= 1 ? n : iAnimNow;
}

static void SyncAnimMenuQt(void)
{
  if (qi.paAnimRun != NULL)
    qi.paAnimRun->setChecked(FAnimRunningQt());
  if (qi.paAnimPause != NULL)
    qi.paAnimPause->setChecked(!FAnimRunningQt());
}

// Start or stop, keeping the rate. Stopped is always the one canonical
// state -- rate negated, pause clear -- so the two ways upstream can stop
// can't disagree and leave the menu contradicting the chart.
static void SetAnimRunningQt(flag fRun)
{
  gs.nAnim = fRun ? NAnimRateQt() : -NAnimRateQt();
  gi.fPause = fFalse;
  SyncAnimMenuQt();
}

// Choose the jump rate. Never starts or stops anything.
static void SetAnimRateQt(int rate)
{
  gs.nAnim = FAnimRunningQt() ? rate : -rate;
  SyncAnimMenuQt();
}

static QAction *AddAnimRateAction(QMenu *pmenu, QActionGroup *pgroup,
  CONST char *szLabel, int rate)
{
  QAction *pa = pmenu->addAction(szLabel);
  pa->setCheckable(true);
  pa->setActionGroup(pgroup);
  pa->setChecked(NAbs(gs.nAnim) == rate);
  ConnectMenuQt(pa, pa, [rate]() { SetAnimRateQt(rate); });
  return pa;
}

static QAction *AddAnimFactorAction(QMenu *pmenu, QActionGroup *pgroup,
  CONST char *szLabel, int factor)
{
  QAction *pa = pmenu->addAction(szLabel);
  pa->setCheckable(true);
  pa->setActionGroup(pgroup);
  pa->setChecked(NAbs(gi.nDir) == factor);
  ConnectMenuQt(pa, pa, [factor]() {
    gi.nDir = (gi.nDir > 0 ? 1 : -1) * factor;
  });
  return pa;
}

static void BuildAnimateMenu(QMainWindow *pwind)
{
  QMenu *pmenu = pwind->menuBar()->addMenu("&Animate");
  // Both this and Pause Animation below drive the one running/not-running
  // state, so either starts and either stops. Two names for one switch is
  // upstream's menu, kept for parity; the behaviour behind them is not.
  qi.paAnimRun = pmenu->addAction("Do &Animation");
  qi.paAnimRun->setCheckable(true);
  ConnectMenuQt(qi.paAnimRun, pwind,
    []() { SetAnimRunningQt(!FAnimRunningQt()); });

  QMenu *pmenuRate = pmenu->addMenu("&Jump Rate");
  QAction *paNow = pmenuRate->addAction("Update to &Now");
  ConnectMenuQt(paNow, pwind, []() { SetAnimRateQt(iAnimNow); });
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
  // Reverse only reverses. Windows also starts animation here when it was
  // stopped, which means a direction control silently doubles as a start
  // button -- another divergence, and deliberate.
  ConnectMenuQt(paReverse, pwind, [paReverse]() {
    neg(gi.nDir);
    paReverse->setChecked(gi.nDir < 0);
    RedrawQt();
  });

  // Play/pause. The same one switch as Do Animation above: pressing it
  // when nothing is moving starts it, pressing it again stops it.
  qi.paAnimPause = pmenu->addAction("&Pause Animation");
  qi.paAnimPause->setCheckable(true);
  ConnectMenuQt(qi.paAnimPause, pwind,
    []() { SetAnimRunningQt(!FAnimRunningQt()); });
  AddToggleAction(pmenu, "&Timed Exposure", &gs.fJetTrail, fFalse);
  pmenu->addSeparator();

  QAction *paForward = pmenu->addAction("Step &Forward");
  ConnectMenuQt(paForward, pwind, []() {
    Animate(NAbs(gs.nAnim) == iAnimNow ? iAnimDay : gs.nAnim, NAbs(gi.nDir));
    RecastAndRedrawQt();
  });
  QAction *paBackward = pmenu->addAction("Step &Backward");
  ConnectMenuQt(paBackward, pwind, []() {
    Animate(NAbs(gs.nAnim) == iAnimNow ? iAnimDay : gs.nAnim, -NAbs(gi.nDir));
    RecastAndRedrawQt();
  });
  pmenu->addSeparator();
  QAction *paStore = pmenu->addAction("&Store Chart Info");
  ConnectMenuQt(paStore, pwind,
    []() { ciSave = ciMain; });
  QAction *paRecall = pmenu->addAction("Re&call Chart Info");
  ConnectMenuQt(paRecall, pwind, []() {
    ciMain = ciCore = ciSave;
    RecastAndRedrawQt();
  });

  // The two items above both show the one state; set them from it now
  // that they exist, rather than from gs.nAnim by hand at each site.
  SyncAnimMenuQt();
}


// Windows' Help menu: About, the doc/data file openers (via
// QDesktopServices, same file resolution FileOpen() already does), and
// the 11 "List Signs/Objects/Aspects/..." text listing actions. Those
// last ones print to a text stream rather than drawing a chart, which is
// why they go through AddChartModeTextAction() -- it forces text mode, so
// RedrawQt() draws them into the canvas instead of a chart.

static void BuildHelpMenu(QMainWindow *pwind)
{
  QMenu *pmenu = pwind->menuBar()->addMenu("&Help");
  CONST char *rgszDoc[9] = { "astrolog.htm", "changes.htm", "license.htm",
    DEFAULT_INFOFILE, "seorbel.txt", "sefstars.txt", DEFAULT_ATLASFILE,
    DEFAULT_TIMECHANGE, szFileExoCore };
  CONST char *rgszLabel[9] = { "Open &Documentation", "Open &Changes",
    "Open &License", "Open &Default Settings", "Open &Orbital Elements",
    "Open &Star List", "Open &Atlas", "Open &Time Zone Changes",
    "Open &Exoplanet List" };
  int i;
  for (i = 0; i < 9; i++) {
    QAction *pa = pmenu->addAction(rgszLabel[i]);
    CONST char *szFile = rgszDoc[i];
    ConnectMenuQt(pa, pwind, [szFile]() {
      char szPath[cchSzMax];
      if (FileOpen(szFile, 2, S(szPath)) != NULL)
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
    ConnectMenuQt(pa, pwind, [szFile]() {
      char szPath[cchSzMax];
      if (FileOpen(szFile, 2, S(szPath)) == NULL) {
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
  // AddChartModeTextAction(). Field mapping verified
  // against ProcessState()'s chart-mode switch, wdriver.cpp:1180-1190.
  AddChartModeTextAction(pmenu, "List Si&gns", gSign);
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
  ConnectMenuQt(paAbout, pwind,
    []() { ShowAboutDialogQt(); });
}


// Build the main window's menu bar.

static void BuildAstrologMenus(QMainWindow *pwind)
{
  // Allocated before any menu is built, since both BuildSettingMenu()
  // (Planetary Moons chart types) and BuildChartMenu()/BuildGraphicsMenu()
  // add actions to this same shared group.
  qi.pgroupChartMode = new QActionGroup(pwind);

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

// Astrolog's Windows dialogs are laid out in units of the dialog font's
// average character width, against MS Shell Dlg. A font whose strings run
// wider than that per unit of average width doesn't fit the boxes the
// resource gives them: measured across all 630 pieces of text in the
// dialogs, this desktop's default overflows 168 of them where Liberation
// Sans overflows 8. Liberation is metrically compatible with Arial and
// close enough to MS Shell Dlg to lay out the same way, so the interface
// uses it and the bundled copy means that holds on any machine.
//
// The point size stays whatever the desktop asked for, so its scaling
// still applies -- only the family changes.
static void SetUiFontQt()
{
  QFont font("Liberation Sans");

  if (QFontInfo(font).family() != QString("Liberation Sans"))
    return;                       // Not there: keep the desktop's font.
  font.setPointSizeF(QApplication::font().pointSizeF());
  QApplication::setFont(font);
}


static void LoadBundledFontsQt()
{
  CONST char *rgszFontFile[] = { "Astro.ttf", "EnigmaAstrology.ttf",
    "HamburgSymbols.ttf", "Astronomicon.ttf", "StarFontSans.ttf",
    "StarFontSerif.ttf", "HanksNakshatra.ttf",
    // The interface font, bundled so the dialogs transcribed from the
    // Windows resource fit their boxes wherever this runs.
    "LiberationSans-Regular.ttf", "LiberationSans-Bold.ttf",
    // And the monospaced face text charts are drawn in.
    "LiberationMono-Regular.ttf", "LiberationMono-Bold.ttf" };
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


// Keyboard shortcuts, the Qt equivalent of Windows' "accelerator
// ACCELERATORS" table in astrolog.rc (~line 3061). Astrolog's whole
// single-keystroke interface lives there -- "v" to swap between graphics
// and text, Alt+Shift+N for a transit chart, and so on -- and none of it
// existed here until now.
//
// Same approach as the context menus: every accelerator names a cmd* the
// menu bar already implements, so these bind to the existing QAction by
// its label rather than duplicating anything. A side benefit is that Qt
// then renders the shortcut alongside the item in the menu, which
// Windows does and this port previously didn't.
//
// Generated from the resource; the 96 macro F-keys are handled by
// BuildMacroMenus() instead and excluded here. Twenty-three accelerators
// are deliberately not bound because their commands are ones this port
// doesn't implement on purpose -- the Setup submenu, the Window Settings
// submenu, Print Setup, the wallpaper modes -- plus the four text
// scrolling ones, which the text window's own scrollbar handles.

typedef struct {
  CONST char *szKey;      // Qt key sequence text.
  CONST char *szAction;   // Menu bar item to fire, by its label.
} HOTKEY;

static CONST HOTKEY rghotkeyQt[] = {
  {"!",                 "&Seconds"},
  {"#",                 "&Hours"},
  {"$",                 "&Days"},
  {"%",                 "M&onths"},
  {"&",                 "&Decades"},
  {"(",                 "Mi&llennia"},
  {")",                 "Open &Documentation"},
  {"*",                 "&Centuries"},
  {"+",                 "Step &Forward"},
  {"-",                 "Step &Backward"},
  {"0",                 "Modif&y Chart"},
  {"Ctrl+0",            "List Cr&edits"},
  {"Alt+0",             "&About Astrolog..."},
  {"Space",             "&Redraw Screen"},
  {"Backspace",         "&Clear Screen"},
  {"Alt+U",             "&Hourglass on Redraw"},
  {"Alt+Shift+Q",       "Ch&art Resizes Window"},
  {"Ctrl+Alt+Q",        "&Window Resizes Chart"},
  {"Shift+B",           "Si&ze Chart to Window"},
  {"Alt+Shift+U",       "&Size Window to Chart"},
  {"Shift+Tab",         "Size Window &Full Screen"},
  {"PgUp",              "Scroll Page &Up"},
  {"PgDown",            "Scroll Page &Down"},
  {"Home",              "Scroll &to Beginning"},
  {"End",               "Scroll to &End"},
  {"1",                 "&One Unit"},
  {"Ctrl+1",            "&Small"},
  {"Alt+1",             "&Solar Chart"},
  {"Ctrl+Shift+1",      "Open &Changes"},
  {"Alt+Shift+1",       "1/&10th Seconds"},
  {"2",                 "&Two Units"},
  {"Ctrl+2",            "&Medium"},
  {"Alt+2",             "List Si&gns"},
  {"Ctrl+Shift+2",      "Open &License"},
  {"Alt+Shift+2",       "1/1&00th Seconds"},
  {"3",                 "T&hree Units"},
  {"Ctrl+3",            "&Large"},
  {"Alt+3",             "List &Objects"},
  {"Ctrl+Shift+3",      "Open &Website"},
  {"Alt+Shift+3",       "1&/1000th Seconds"},
  {"4",                 "&Four Units"},
  {"Ctrl+4",            "&Huge"},
  {"Alt+4",             "List Aspec&ts"},
  {"Ctrl+Shift+4",      "Open Website &Mirror"},
  {"5",                 "Fi&ve Units"},
  {"Ctrl+5",            "Export Chart &Text Output..."},
  {"Alt+5",             "List &Constellations"},
  {"Ctrl+Shift+5",      "Copy Chart &Text Output"},
  {"6",                 "Si&x Units"},
  {"Ctrl+6",            "Export Chart &Bitmap..."},
  {"Alt+6",             "List &Planet Info"},
  {"Ctrl+Shift+6",      "Copy Chart &Bitmap"},
  {"7",                 "&Seven Units"},
  {"Ctrl+7",            "Export Chart &Metafile..."},
  {"Alt+7",             "Esoter&ic"},
  {"Ctrl+Shift+7",      "Copy Chart &Metafile"},
  {"Alt+Shift+7",       "List &Rays"},
  {"8",                 "&Eight Units"},
  {"Ctrl+8",            "Export Chart &PostScript..."},
  {"Alt+8",             "List S&witches"},
  {"Ctrl+Shift+8",      "Copy Chart &PostScript"},
  {"9",                 "&Nine Units"},
  {"Ctrl+9",            "Save Program Settin&gs..."},
  {"Alt+9",             "List O&bscure Switches"},
  {"Ctrl+Shift+9",      "Open &Default Settings"},
  {"Alt+Shift+9",       "Show &Navamsas"},
  {"<",                 "&Decrease"},
  {">",                 "&Increase"},
  {"?",                 "List &Keystrokes"},
  {"@",                 "&Minutes"},
  {"^",                 "&Years"},
  {"A",                 "&3D Houses"},
  {"Ctrl+A",            "&White"},
  {"Shift+A",           "Aspect Midpoint &Grid"},
  {"Ctrl+Shift+A",      "Alca&bitius"},
  {"Alt+Shift+A",       "&Aspect Settings..."},
  {"B",                 "Show &Border"},
  {"Ctrl+B",            "&Blue"},
  {"Alt+B",             "Print &Nearest Second"},
  {"Ctrl+Alt+B",        "Open &World Map..."},
  {"Ctrl+Shift+B",      "Open Chart &Background..."},
  {"Alt+Shift+B",       "&Display Settings..."},
  {"C",                 "Com&parison Chart"},
  {"Ctrl+C",            "Show C&ities"},
  {"Shift+C",           "Include &Cusps"},
  {"Ctrl+Shift+C",      "&Campanus"},
  {"Alt+Shift+C",       "Chart &Settings..."},
  {"D",                 "Show &House Details"},
  {"Ctrl+D",            "Gr&ay"},
  {"Alt+D",             "D&efault Chart Info..."},
  {"Shift+D",           "Date &Difference Chart"},
  {"Ctrl+Shift+D",      "Pullen (S.&Delta)"},
  {"Alt+Shift+D",       "&Progressed and Natal"},
  {"E",                 "Show &Equator"},
  {"Ctrl+E",            "Maroo&n"},
  {"Ctrl+Alt+E",        "Open &Orbital Elements"},
  {"Shift+E",           "&Ephemeris"},
  {"Ctrl+Shift+E",      "&Equal"},
  {"Alt+Shift+E",       "&File Settings..."},
  {"F",                 "&Flip Signs with Houses"},
  {"Ctrl+F",            "Dk. Gr&een"},
  {"Ctrl+Alt+F",        "&Star Customization..."},
  {"Shift+F",           "Show &Constellations"},
  {"Ctrl+Shift+F",      "Savard-&A"},
  {"Alt+Shift+F",       "Star Restr&ictions..."},
  {"G",                 "Show &Decans"},
  {"Ctrl+G",            "&Green"},
  {"Shift+G",           "Draw &Globe"},
  {"Ctrl+Shift+G",      "Carter& P.Equat."},
  {"Alt+Shift+G",       "&Graphics Settings..."},
  {"H",                 "He&liocentric"},
  {"Ctrl+H",            "Open &Documentation"},
  {"Shift+H",           "Ga&uquelin Sectors"},
  {"Ctrl+Shift+H",      "Hori&zon"},
  {"Alt+Shift+H",       "&Geodetic Houses"},
  {"I",                 "Modify &Display"},
  {"Ctrl+I",            "List &General Meanings"},
  {"Shift+I",           "Risi&ng and Setting"},
  {"Ctrl+Shift+I",      "Sr&ipati"},
  {"Alt+Shift+I",       "Show &Interpretations"},
  {"J",                 "&Timed Exposure"},
  {"Ctrl+J",            "&Cyan"},
  {"Alt+J",             "&Object Settings..."},
  {"Ctrl+Alt+J",        "Open &Exoplanet List"},
  {"Shift+J",           "Inf&luence"},
  {"Ctrl+Shift+J",      "Suns&hine"},
  {"Alt+Shift+J",       "More Ob&ject Settings..."},
  {"Ctrl+T",            "Object Selectio&ns..."},
  {"K",                 "Show &Glyphs on Aspect Lines"},
  {"Ctrl+K",            "&Dk. Cyan"},
  {"Alt+K",             "&Colored Text"},
  {"Ctrl+Alt+K",        "Save Chart &Quick*Chart..."},
  {"Shift+K",           "&Calendar"},
  {"Ctrl+Shift+K",      "&Koch"},
  {"Alt+Shift+K",       "&Set Colors..."},
  {"L",                 "Show Glyph &Labels"},
  {"Ctrl+L",            "&Lt. Gray"},
  {"Alt+L",             "&Aspect List"},
  {"Shift+L",           "Astrocartograp&hy"},
  {"Ctrl+Shift+L",      "A&.P.C."},
  {"Alt+Shift+L",       "Nea&rest Cities"},
  {"M",                 "&Monochrome"},
  {"Ctrl+M",            "&Magenta"},
  {"Alt+M",             "&Midpoint List"},
  {"Ctrl+Alt+M",        "Open &Atlas"},
  {"Shift+M",           "&Moons Chart"},
  {"Ctrl+Shift+M",      "&Meridian"},
  {"Alt+Shift+M",       "Time Space &Midpoint Chart"},
  {"N",                 "Chart for &Now"},
  {"Ctrl+N",            "Dk. Bl&ue"},
  {"Alt+N",             "Update to &Now"},
  {"Ctrl+Alt+N",        "Set Tilt to &Zero"},
  {"Shift+N",           "Do &Animation"},
  {"Ctrl+Shift+N",      "&Null"},
  {"Alt+Shift+N",       "&Transit and Natal"},
  {"O",                 "&Store Chart Info"},
  {"Ctrl+O",            "Mai&ze"},
  {"Alt+O",             "&Open Chart..."},
  {"Ctrl+Alt+O",        "Open Charts in &Folder..."},
  {"Shift+O",           "Re&call Chart Info"},
  {"Ctrl+Shift+O",      "Pullen (S.Rati&o)"},
  {"Alt+Shift+O",       "Open Chart #&2..."},
  {"P",                 "&Pause Animation"},
  {"Ctrl+P",            "P&rint..."},
  {"Alt+P",             "Ara&bic Parts"},
  {"Ctrl+Alt+P",        "Save Chart &Exchange..."},
  {"Shift+P",           "Draw &Polar Globe"},
  {"Ctrl+Shift+P",      "&Placidus"},
  {"Alt+Shift+P",       "&Progressions..."},
  {"Q",                 "&Thicker Lines"},
  {"Alt+Q",             "&Antialias Lines"},
  {"Shift+Q",           "S&quare Screen"},
  {"Ctrl+Shift+Q",      "E&qual (MC)"},
  {"R",                 "&Reverse Direction"},
  {"Ctrl+R",            "&Red"},
  {"Alt+R",             "&Restrictions..."},
  {"Ctrl+Alt+R",        "Open &Star List"},
  {"Shift+R",           "Include &Minors"},
  {"Ctrl+Shift+R",      "&Regiomontanus"},
  {"Alt+Shift+R",       "&Transit Restrictions..."},
  {"S",                 "&Sidereal Zodiac"},
  {"Ctrl+S",            "Show Full &Star List"},
  {"Ctrl+Alt+S",        "Show Constellation &Lines"},
  {"Shift+S",           "Solar System &Orbit"},
  {"Ctrl+Shift+S",      "Kr&usinski"},
  {"Alt+Shift+S",       "Calculation Settin&gs..."},
  {"T",                 "Show Chart &Info"},
  {"Alt+T",             "Show Info &Sidebar"},
  {"Shift+T",           "Draw &Telescope"},
  {"Ctrl+Shift+T",      "&Topocentric"},
  {"Alt+Shift+T",       "&Transits..."},
  {"U",                 "Include &Uranians"},
  {"Ctrl+U",            "&Purple"},
  {"Shift+U",           "Include &Fixed Stars"},
  {"Ctrl+Shift+U",      "Morinu&s"},
  {"V",                 "Show &Graphics"},
  {"Ctrl+V",            "&Paste"},
  {"Ctrl+Alt+V",        "Save Chart i&Calendar..."},
  {"Shift+V",           "Standard Radi&x"},
  {"Ctrl+Shift+V",      "&Vedic"},
  {"Alt+Shift+V",       "House &Wheel"},
  {"Down",              "Tilt &South"},
  {"Ctrl+Down",         "&Last Chart"},
  {"Shift+Down",        "&Next Chart"},
  {"Esc",               "E&xit"},
  {"Left",              "Rotate &West"},
  {"Shift+Left",        "Zoom &Out"},
  {"`",                 "&Include Moons"},
  {"Ctrl+`",            "Show E&xoplanets"},
  {"Alt+`",             "&Exoplanets Chart"},
  {"Shift+`",           "Include &Body Centers (COB)"},
  {"[",                 "Tilt &North"},
  {"Ctrl+[",            "Zoom &Out"},
  {"Shift+[",           "Rotate &West"},
  {"Ctrl+\\\\",         "Export Chart &SVG..."},
  {"Ctrl+Shift+\\\\",   "Copy Chart &SVG"},
  {"]",                 "Tilt &South"},
  {"Ctrl+]",            "Zoom &In"},
  {"Shift+]",           "Rotate &East"},
  {"Ctrl+,",            "D&ecrease Text"},
  {"Ctrl+-",            "Export Chart &Wireframe..."},
  {"Ctrl+Shift+-",      "Copy Chart &Wireframe"},
  {"Ctrl+.",            "I&ncrease Text"},
  {"=",                 "Show &Indian Wheels"},
  {"Ctrl+=",            "Draw &South Indian"},
  {"Alt+=",             "Draw &North Indian"},
  {"Ctrl+Alt+=",        "Draw &East Indian"},
  {"Ctrl+Shift+=",      "Save Chart &List..."},
  {"Pause",             "&Pause Animation"},
  {"Return",            "Enter Command &Line..."},
  {"Right",             "Rotate &East"},
  {"Shift+Right",       "Zoom &In"},
  {"Ctrl+Tab",          "Moon &Restrictions..."},
  {"Ctrl+Shift+Tab",    "Moon &Object Settings..."},
  {"Up",                "Tilt &North"},
  {"Ctrl+Up",           "&First Chart"},
  {"Shift+Up",          "&Previous Chart"},
  {"W",                 "Use Detailed World &Map"},
  {"Ctrl+W",            "Show D&wads"},
  {"Alt+W",             "&Save Chart Info..."},
  {"Ctrl+Alt+W",        "Object &Customization..."},
  {"Shift+W",           "Draw &World Map"},
  {"Ctrl+Shift+W",      "&Whole"},
  {"Alt+Shift+W",       "Save Chart &Positions..."},
  {"X",                 "&Reverse Background"},
  {"Ctrl+X",            "Use Ecliptic &Axis"},
  {"Alt+X",             "&Parallel Aspects"},
  {"Shift+X",           "Draw Chart Sp&here"},
  {"Ctrl+Shift+X",      "Swap Chart #&1 and #2"},
  {"Alt+Shift+X",       "&Applying Aspects"},
  {"Y",                 "Include D&warfs"},
  {"Ctrl+Y",            "&Yellow"},
  {"Alt+Y",             "&Synastry Chart"},
  {"Ctrl+Alt+Y",        "Open &Time Zone Changes"},
  {"Shift+Y",           "&Biorhythm Chart"},
  {"Ctrl+Shift+Y",      "Porph&yry"},
  {"Alt+Shift+Y",       "&Composite Chart"},
  {"Z",                 "&Indian Wheel Order"},
  {"Ctrl+Z",            "Blac&k"},
  {"Alt+Z",             "Set Chart &Info..."},
  {"Ctrl+Alt+Z",        "&Chart List..."},
  {"Shift+Z",           "Local Hori&zon"},
  {"Ctrl+Shift+Z",      "Charts #&3 Through #6..."},
  {"Alt+Shift+Z",       "Set Chart #&2 Info..."} };

#define chotkeyQt (int)(sizeof(rghotkeyQt) / sizeof(HOTKEY))

#include "qtrcaccel.h"
#include "qtrccmd.h"

// Windows hands every menu choice to one WM_COMMAND switch, and applies
// the "-~WQ" AstroExpression (us.szExpMenu) to the command id before the
// switch runs -- so an expression can veto a command or swap it for
// another. This port binds each action to its own handler and had no id
// to hand over, which is why the hook was unimplemented.
//
// ConnectMenuQt() supplies the missing half without giving every call
// site a command constant: it looks the id up from the action's own
// label, and records the handler against that id so a substituted
// command can be dispatched to the right place. Actions whose label is
// not in the resource (the runtime-renamed macros, the website links)
// get a plain connection and simply do not take part.
static QVector<QPair<int, std::function<void()> > > s_rgcmdfnQt;

static int NCmdFromLabelQt(CONST QString &str)
{
  QString strT = str.section(QChar('\t'), 0, 0);
  int i;

  for (i = 0; i < ccmdQt; i++)
    if (strT == QString(rgcmdQt[i].szLabel))
      return rgcmdQt[i].cmd;
  return 0;
}

static void ConnectMenuQt(QAction *pa, QObject *pctx,
  std::function<void()> fn)
{
  int cmd = NCmdFromLabelQt(pa->text());

  if (cmd > 0)
    s_rgcmdfnQt.append(qMakePair(cmd, fn));
  QObject::connect(pa, &QAction::triggered, pctx, [cmd, fn]() {
    int n = cmd;
#ifdef EXPRESS
    if (!us.fExpOff && FSzSet(us.szExpMenu) && cmd > 0) {
      ExpSetN(iLetterZ, cmd);
      ParseExpression(us.szExpMenu);
      n = NExpGet(iLetterZ);
    }
#endif
    if (n == cmd) {
      fn();
      return;
    }
    if (n <= 0)                      // Expression vetoed the command.
      return;
    for (int i = 0; i < s_rgcmdfnQt.size(); i++)
      if (s_rgcmdfnQt[i].first == n) {
        s_rgcmdfnQt[i].second();
        return;
      }
  });
}

#ifdef QTTEST
// The suite checks every label here still names a menu item.
CONST RCACCEL *PaccelTestQt() { return rgaccelQt; }
int CaccelTestQt() { return caccelQt; }
#endif

// Show the accelerator column the way Windows shows it. Qt renders that
// column from the QKeySequence, spelling every modifier out -- "Shift+V",
// "Alt+Shift+O" -- while astrolog.rc writes the string Windows draws
// verbatim after a "\t", capitalising a letter to mean Shift: "V",
// "Alt+O". Same keys either way, but the notation is on every menu, every
// time, and a Windows user reads it on every item.
//
// A tab in a QAction's text is what Qt checks *first* when painting a
// menu row, ahead of the shortcut, so appending the resource's own string
// replaces the rendering without touching what the shortcut does. The
// label stays the item's identity: everything here finds an action by its
// label, so the lookups compare only up to the tab.
static void ApplyAccelTextQt(QMainWindow *pwind)
{
  int i;

  for (i = 0; i < caccelQt; i++) {
    QAction *pa = PaFindMenuActionQt(pwind->menuBar(),
      QString(rgaccelQt[i].szLabel));
    if (pa == NULL) {
      // Fall back to matching without the mnemonic. This port puts "&" on
      // a different letter than the resource in a few dozen places, on
      // purpose, and those items still want Windows' accelerator text.
      pa = PaFindMenuActionLooseQt(pwind->menuBar(),
        QString(rgaccelQt[i].szLabel).remove('&'));
    }
    if (pa == NULL || pa->text().contains(QChar('\t')))
      continue;
    pa->setText(pa->text() + QChar('\t') + QString(rgaccelQt[i].szAccel));
  }
}

// Bind them onto the menu bar's own actions. Called once, after the menus
// are built. An entry naming an item that no longer exists is skipped
// rather than crashing; see the note above PmenuBuildContextQt() about
// why label mismatches are worth surfacing.
static void ApplyHotkeysQt(QMainWindow *pwind)
{
  int i;

  for (i = 0; i < chotkeyQt; i++) {
    QAction *pa = PaFindMenuActionQt(pwind->menuBar(),
      QString(rghotkeyQt[i].szAction));
    if (pa == NULL)
      continue;
    QList<QKeySequence> rgks = pa->shortcuts();
    rgks.append(QKeySequence(QString(rghotkeyQt[i].szKey)));
    pa->setShortcuts(rgks);
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
  {"Toggle &Comparison Wheel",              "Com&parison Chart"},
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
  {"Show Info &Sidebar",                    "Show Info &Sidebar"} };

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
  {"Transits are Transit to &Natal",            "Com&parison Chart"},
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
  {"Year Plots Every &Day",                     "Print &Nearest Second"},
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
  {"Year Plots Every &Day",                     "Print &Nearest Second"},
  {"Plot Vertical &Latitudes",                  "&Parallel Aspects"},
  {NULL, NULL},
  {"Don't Show &Moon",                          "Modify &Display"},
  {"Highlight &Current Date",                   "Show Glyph &Labels"},
  {"Show &Horizontal Lines",                    "Show &Equator"} };

// Windows' menuZd, the Rising chart.
static CONST CTXITEM rgctxRisingQt[] = {
  {"&View Rising/Setting Times",                "Show &Graphics"},
  {NULL, NULL},
  {"Year Plots Every &Day",                     "Print &Nearest Second"},
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
  {"Show Full &Star List",                      "Show Full &Star List"},
  {"Show E&xoplanets",                          "Show E&xoplanets"},
  {"Show Degree &Grid",                         "Show C&ities"},
  {NULL, NULL},
  {"Use Ecliptic &Axis",                        "Use Ecliptic &Axis"} };

// Windows' menu_V, the Standard listing text chart.
static CONST CTXITEM rgctxTxtListQt[] = {
  {"&View Graphics Mode Wheel",                 "Show &Graphics"},
  {NULL, NULL},
  {"Toggle &Comparison Chart",                  "Com&parison Chart"},
  {"Print &Nearest Second",                     "Print &Nearest Second"},
  {"House Placements Based on &3D Houses",      "&3D Houses"} };

// Windows' menu_W, the House wheel text chart.
static CONST CTXITEM rgctxTxtWheelQt[] = {
  {"&View Graphic House Wheel",                 "Show &Graphics"},
  {NULL, NULL},
  {"&Indian Sign Arrangement",                  "&Indian Wheel Order"},
  {"Print &Nearest Second",                     "Print &Nearest Second"},
  {"House Placements Based on &3D Houses",      "&3D Houses"} };

// Windows' menu_G, the Grid text chart.
static CONST CTXITEM rgctxTxtGridQt[] = {
  {"&View Graphic Grid",                        "Show &Graphics"},
  {NULL, NULL},
  {"Toggle &Comparison Chart",                  "Com&parison Chart"},
  {"Print &Nearest Second",                     "Print &Nearest Second"},
  {"&Parallel Aspects",                         "&Parallel Aspects"},
  {"&Applying Aspects",                         "&Applying Aspects"} };

// Windows' menu_A, the Aspect list text chart.
static CONST CTXITEM rgctxTxtAspectQt[] = {
  {"Toggle &Comparison Chart",                  "Com&parison Chart"},
  {"Print &Nearest Second",                     "Print &Nearest Second"},
  {"&Parallel Aspects",                         "&Parallel Aspects"},
  {"&Applying Aspects",                         "&Applying Aspects"} };

// Windows' menu_M, the Midpoint list text chart.
static CONST CTXITEM rgctxTxtMidpointQt[] = {
  {"&View Graphic Dial Chart",                  "Show &Graphics"},
  {NULL, NULL},
  {"Toggle &Comparison Chart",                  "Com&parison Chart"},
  {"Print &Nearest Second",                     "Print &Nearest Second"},
  {"Show &Latitude Midpoints Too",              "&Parallel Aspects"},
  {"Midpoints are &3D",                         "&3D Houses"} };

// Windows' menu_Z, the Horizon text chart.
static CONST CTXITEM rgctxTxtHorizonQt[] = {
  {"&View Graphic Horizon Chart",               "Show &Graphics"},
  {NULL, NULL},
  {"Print &Nearest Second",                     "Print &Nearest Second"},
  {"Show &3D House Placements",                 "&3D Houses"} };

// Windows' menu_S, the Orbit text chart.
static CONST CTXITEM rgctxTxtOrbitQt[] = {
  {"&View Graphic Orbit Chart",                 "Show &Graphics"},
  {NULL, NULL},
  {"Print &Nearest Second",                     "Print &Nearest Second"} };

// Windows' menu_H, the Sector text chart.
static CONST CTXITEM rgctxTxtSectorQt[] = {
  {"&View Graphic Sector Wheel",                "Show &Graphics"},
  {NULL, NULL},
  {"Print &Nearest Second",                     "Print &Nearest Second"} };

// Windows' menu_K, the Calendar text chart.
static CONST CTXITEM rgctxTxtCalendarQt[] = {
  {"&View Graphic Calendar",                    "Show &Graphics"},
  {NULL, NULL},
  {"Weeks Start on &Monday",                    "&Indian Wheel Order"} };

// Windows' menu_J, the Influence text chart.
static CONST CTXITEM rgctxTxtInfluenceQt[] = {
  {"&View Graphic Dispositor Chart",            "Show &Graphics"},
  {NULL, NULL},
  {"&Combine Signs and Houses",                 "&Indian Wheel Order"},
  {"Print &Detailed Percentages",               "Print &Nearest Second"} };

// Windows' menu_7, the Esoteric text chart.
static CONST CTXITEM rgctxTxtEsotericQt[] = {
  {"&View Graphic Ray Ephemeris",               "Show &Graphics"} };

// Windows' menu_L, the Astro-graph text chart.
static CONST CTXITEM rgctxTxtAstroGraphQt[] = {
  {"&View Graphic Astro-Graph Chart",           "Show &Graphics"},
  {NULL, NULL},
  {"Ignore Planet &Latitudes",                  "&3D Houses"},
  {"Print &Nearest Second",                     "Print &Nearest Second"} };

// Windows' menu_E, the Ephemeris text chart.
static CONST CTXITEM rgctxTxtEphemerisQt[] = {
  {"&View Graphic Ephemeris",                   "Show &Graphics"},
  {NULL, NULL},
  {"Ephemeris Shows &Latitudes",                "&Parallel Aspects"},
  {"Print &Nearest Second",                     "Print &Nearest Second"} };

// Windows' menu_P, the Arabic parts text chart.
static CONST CTXITEM rgctxTxtArabicQt[] = {
  {"Print &Nearest Second",                     "Print &Nearest Second"} };

// Windows' menu_I, the Rising text chart.
static CONST CTXITEM rgctxTxtRisingQt[] = {
  {"&View Graphic Rising Chart",                "Show &Graphics"},
  {NULL, NULL},
  {"Print &Nearest Second",                     "Print &Nearest Second"} };

// Windows' menu_N, the Nearest cities text chart.
static CONST CTXITEM rgctxTxtLocalQt[] = {
  {"&View Graphic Local Space Chart",           "Show &Graphics"},
  {NULL, NULL},
  {"Print &Nearest Second",                     "Print &Nearest Second"} };

// Windows' menu_8, the Moons text chart.
static CONST CTXITEM rgctxTxtMoonsQt[] = {
  {"&View Graphic Moons Chart",                 "Show &Graphics"},
  {NULL, NULL},
  {"&Parallel Aspects",                         "&Parallel Aspects"},
  {"Print &Nearest Second",                     "Print &Nearest Second"} };

// Windows' menu_Ux, the Exoplanets text chart.
static CONST CTXITEM rgctxTxtExoQt[] = {
  {"Transits at &Chart Time",                   "&Parallel Aspects"},
  {"&Exact Transits Only",                      "&3D Houses"} };

// Windows' menu_D, the Transit times text chart.
static CONST CTXITEM rgctxTxtInDayQt[] = {
  {"&Parallel Aspects",                         "&Parallel Aspects"},
  {"Print &Nearest Second",                     "Print &Nearest Second"} };

// Windows' menu_T, the Transit influence text chart.
static CONST CTXITEM rgctxTxtTransInfQt[] = {
  {"&Parallel Aspects",                         "&Parallel Aspects"},
  {"&Applying Aspects",                         "&Applying Aspects"},
  {"Print &Nearest Second",                     "Print &Nearest Second"} };

// Windows' menu_B, the Transit graph text chart.
static CONST CTXITEM rgctxTxtTransGraQt[] = {
  {"&View Graphic Transit Graph",               "Show &Graphics"},
  {NULL, NULL},
  {"&Parallel Aspects",                         "&Parallel Aspects"},
  {"Show Only &Exact Aspects",                  "&Indian Wheel Order"} };

// Windows' menu_Y, the Biorhythm text chart.
static CONST CTXITEM rgctxTxtBiorhythmQt[] = {
  {"&View Graphic Biorhythm",                   "Show &Graphics"} };

#define CctxQt(rg) (int)(sizeof(rg) / sizeof(CTXITEM))

// Find a menu bar item by its exact label, searching submenus too.
// As PaFindMenuActionQt, but comparing with the mnemonic removed.
static QAction *PaFindMenuActionLooseQt(QWidget *pw, CONST QString &str)
{
  QList<QAction *> rgpa = pw->actions();
  QAction *pa, *paT;
  int i;

  for (i = 0; i < rgpa.size(); i++) {
    pa = rgpa[i];
    if (pa->menu() != NULL) {
      paT = PaFindMenuActionLooseQt(pa->menu(), str);
      if (paT != NULL)
        return paT;
    } else if (pa->text().section(QChar('\t'), 0, 0).remove('&') == str)
      return pa;
  }
  return NULL;
}

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
    } else if (pa->text().section(QChar('\t'), 0, 0) == str)
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
    ConnectMenuQt(pa, pa,
      [paSrc]() { paSrc->trigger(); });
  }
  return pmenu;
}

// The text chart menus. Windows picks these from the us.f* chart-type
// flags rather than gi.nMode, in an else-if chain whose order matters --
// several of these flags can be set at once, and the first match wins.
// Kept in Windows' order for that reason.

static QMenu *PmenuContextForTextQt()
{
#define CtxIf(cond, rg) \
  if (cond) return PmenuBuildContextQt(rg, CctxQt(rg));

  CtxIf(us.nRel == rcBiorhythm, rgctxTxtBiorhythmQt)
  CtxIf(us.fListing,       rgctxTxtListQt)
  CtxIf(us.fWheel,         rgctxTxtWheelQt)
  CtxIf(us.fGrid,          rgctxTxtGridQt)
  CtxIf(us.fAspList,       rgctxTxtAspectQt)
  CtxIf(us.fMidpoint,      rgctxTxtMidpointQt)
  CtxIf(us.fHorizon,       rgctxTxtHorizonQt)
  CtxIf(us.fOrbit,         rgctxTxtOrbitQt)
  CtxIf(us.fSector,        rgctxTxtSectorQt)
  CtxIf(us.fCalendar,      rgctxTxtCalendarQt)
  CtxIf(us.fInfluence,     rgctxTxtInfluenceQt)
  CtxIf(us.fEsoteric,      rgctxTxtEsotericQt)
  CtxIf(us.fAstroGraph,    rgctxTxtAstroGraphQt)
  CtxIf(us.fEphemeris,     rgctxTxtEphemerisQt)
  CtxIf(us.fArabic,        rgctxTxtArabicQt)
  CtxIf(us.fHorizonSearch, rgctxTxtRisingQt)
  CtxIf(us.fAtlasNear,     rgctxTxtLocalQt)
  CtxIf(us.fMoonChart,     rgctxTxtMoonsQt)
  CtxIf(us.fExoTransit,    rgctxTxtExoQt)
  CtxIf(us.fInDay    || us.fTransit,    rgctxTxtInDayQt)
  CtxIf(us.fInDayInf || us.fTransitInf, rgctxTxtTransInfQt)
  CtxIf(us.fInDayGra || us.fTransitGra, rgctxTxtTransGraQt)
  return NULL;
#undef CtxIf
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


#ifdef QTTEST
extern int NRunQtTestsQt();   // qttest.cpp

// Hooks for the test binary (see qttest.cpp and Makefile.qt.test). Only
// compiled when QTTEST is defined, so the shipped astrolog-qt carries
// none of this. They exist because the menu tables and the lookup helper
// above are file static, and the tests need to walk them.

typedef struct {
  CONST char *szName;
  CONST CTXITEM *rgitem;
  int citem;
} CTXTEST;

static CONST CTXTEST rgctxtestQt[] = {
  {"rgctxWheelQt", rgctxWheelQt, CctxQt(rgctxWheelQt)},
  {"rgctxIndianQt", rgctxIndianQt, CctxQt(rgctxIndianQt)},
  {"rgctxGridQt", rgctxGridQt, CctxQt(rgctxGridQt)},
  {"rgctxMidpointQt", rgctxMidpointQt, CctxQt(rgctxMidpointQt)},
  {"rgctxHorizonQt", rgctxHorizonQt, CctxQt(rgctxHorizonQt)},
  {"rgctxOrbitQt", rgctxOrbitQt, CctxQt(rgctxOrbitQt)},
  {"rgctxSectorQt", rgctxSectorQt, CctxQt(rgctxSectorQt)},
  {"rgctxCalendarQt", rgctxCalendarQt, CctxQt(rgctxCalendarQt)},
  {"rgctxInfluenceQt", rgctxInfluenceQt, CctxQt(rgctxInfluenceQt)},
  {"rgctxEsotericQt", rgctxEsotericQt, CctxQt(rgctxEsotericQt)},
  {"rgctxAstroGraphQt", rgctxAstroGraphQt, CctxQt(rgctxAstroGraphQt)},
  {"rgctxEphemerisQt", rgctxEphemerisQt, CctxQt(rgctxEphemerisQt)},
  {"rgctxRisingQt", rgctxRisingQt, CctxQt(rgctxRisingQt)},
  {"rgctxLocalQt", rgctxLocalQt, CctxQt(rgctxLocalQt)},
  {"rgctxMoonsQt", rgctxMoonsQt, CctxQt(rgctxMoonsQt)},
  {"rgctxTransitQt", rgctxTransitQt, CctxQt(rgctxTransitQt)},
  {"rgctxBiorhythmQt", rgctxBiorhythmQt, CctxQt(rgctxBiorhythmQt)},
  {"rgctxSphereQt", rgctxSphereQt, CctxQt(rgctxSphereQt)},
  {"rgctxGlobeQt", rgctxGlobeQt, CctxQt(rgctxGlobeQt)},
  {"rgctxTelescopeQt", rgctxTelescopeQt, CctxQt(rgctxTelescopeQt)},
  {"rgctxTxtListQt", rgctxTxtListQt, CctxQt(rgctxTxtListQt)},
  {"rgctxTxtWheelQt", rgctxTxtWheelQt, CctxQt(rgctxTxtWheelQt)},
  {"rgctxTxtGridQt", rgctxTxtGridQt, CctxQt(rgctxTxtGridQt)},
  {"rgctxTxtAspectQt", rgctxTxtAspectQt, CctxQt(rgctxTxtAspectQt)},
  {"rgctxTxtMidpointQt", rgctxTxtMidpointQt, CctxQt(rgctxTxtMidpointQt)},
  {"rgctxTxtHorizonQt", rgctxTxtHorizonQt, CctxQt(rgctxTxtHorizonQt)},
  {"rgctxTxtOrbitQt", rgctxTxtOrbitQt, CctxQt(rgctxTxtOrbitQt)},
  {"rgctxTxtSectorQt", rgctxTxtSectorQt, CctxQt(rgctxTxtSectorQt)},
  {"rgctxTxtCalendarQt", rgctxTxtCalendarQt, CctxQt(rgctxTxtCalendarQt)},
  {"rgctxTxtInfluenceQt", rgctxTxtInfluenceQt, CctxQt(rgctxTxtInfluenceQt)},
  {"rgctxTxtEsotericQt", rgctxTxtEsotericQt, CctxQt(rgctxTxtEsotericQt)},
  {"rgctxTxtAstroGraphQt", rgctxTxtAstroGraphQt, CctxQt(rgctxTxtAstroGraphQt)},
  {"rgctxTxtEphemerisQt", rgctxTxtEphemerisQt, CctxQt(rgctxTxtEphemerisQt)},
  {"rgctxTxtArabicQt", rgctxTxtArabicQt, CctxQt(rgctxTxtArabicQt)},
  {"rgctxTxtRisingQt", rgctxTxtRisingQt, CctxQt(rgctxTxtRisingQt)},
  {"rgctxTxtLocalQt", rgctxTxtLocalQt, CctxQt(rgctxTxtLocalQt)},
  {"rgctxTxtMoonsQt", rgctxTxtMoonsQt, CctxQt(rgctxTxtMoonsQt)},
  {"rgctxTxtExoQt", rgctxTxtExoQt, CctxQt(rgctxTxtExoQt)},
  {"rgctxTxtInDayQt", rgctxTxtInDayQt, CctxQt(rgctxTxtInDayQt)},
  {"rgctxTxtTransInfQt", rgctxTxtTransInfQt, CctxQt(rgctxTxtTransInfQt)},
  {"rgctxTxtTransGraQt", rgctxTxtTransGraQt, CctxQt(rgctxTxtTransGraQt)},
  {"rgctxTxtBiorhythmQt", rgctxTxtBiorhythmQt, CctxQt(rgctxTxtBiorhythmQt)} };

int CCtxTestQt()
{
  return (int)(sizeof(rgctxtestQt) / sizeof(CTXTEST));
}

// Build context menu "i" and report its name. The caller owns the menu.
QMenu *PmenuCtxTestQt(int i, CONST char **pszName)
{
  *pszName = rgctxtestQt[i].szName;
  return PmenuBuildContextQt(rgctxtestQt[i].rgitem, rgctxtestQt[i].citem);
}

int CHotkeyTestQt()
{
  return chotkeyQt;
}

void HotkeyTestQt(int i, CONST char **pszKey, CONST char **pszAction)
{
  *pszKey = rghotkeyQt[i].szKey;
  *pszAction = rghotkeyQt[i].szAction;
}

// Collect every action in the menu bar, so a test can fire them all.
static void CollectActionsTestQt(QWidget *pw, QList<QAction *> *prg)
{
  QList<QAction *> rgpa = pw->actions();
  int i;

  for (i = 0; i < rgpa.size(); i++) {
    if (rgpa[i]->menu() != NULL)
      CollectActionsTestQt(rgpa[i]->menu(), prg);
    else if (!rgpa[i]->isSeparator())
      prg->append(rgpa[i]);
  }
}

void AllActionsTestQt(QList<QAction *> *prg)
{
  CollectActionsTestQt(gi.qwind->menuBar(), prg);
}

// Find a menu bar item by label, ignoring "&" placement, and report
// which top-level menu it turned up under. Used by the parity test:
// Windows and this port do not always put the mnemonic on the same
// letter, and that is not a parity gap worth failing over -- an item
// living under the wrong menu is.
QAction *PaFindLooseTestQt(CONST char *sz, CONST char **pszTop)
{
  QString str = QString(sz).remove('&');
  int i;

  QList<QAction *> rgtop = gi.qwind->menuBar()->actions();
  for (i = 0; i < rgtop.size(); i++) {
    if (rgtop[i]->menu() == NULL)
      continue;
    static QByteArray baTop;
    baTop = rgtop[i]->text().remove('&').toLocal8Bit();
    QList<QAction *> rgpa;
    CollectActionsTestQt(rgtop[i]->menu(), &rgpa);
    for (int j = 0; j < rgpa.size(); j++)
      if (rgpa[j]->text().section(QChar('\t'), 0, 0).remove('&') == str) {
        *pszTop = baTop.constData();
        return rgpa[j];
      }
  }
  *pszTop = NULL;
  return NULL;
}

QAction *PaFindActionTestQt(CONST char *sz)
{
  return PaFindMenuActionQt(gi.qwind->menuBar(), QString(sz));
}
#endif // QTTEST


// This routine opens up and initializes the chart window, and is called
// from BeginX() the same way the X11 backend's window setup is, once per
// program invocation.

// Windows' dialogs put Cancel to the left of OK -- see the button X
// positions in astrolog.rc, where Cancel sits at 320 and OK at 375 in
// dlgAspect, and the same the whole way through. Qt orders the buttons in
// a QDialogButtonBox by platform convention instead, which under the
// Fusion style is the other way round. Rather than hand-build the button
// row in each of the twenty-odd dialogs, override the one style hint that
// decides it: GnomeLayout is Qt's name for Cancel-then-OK.

class AstroStyleQt : public QProxyStyle
{
public:
  // A NULL base means "whatever style the application already has", which
  // is how this is constructed at startup. ApplyColorSchemeQt() passes a
  // real one when it needs to pin the style to Fusion.
  explicit AstroStyleQt(QStyle *pbase = NULL) : QProxyStyle(pbase) { }

  int styleHint(StyleHint hint, CONST QStyleOption *popt = NULL,
    CONST QWidget *pw = NULL, QStyleHintReturn *pret = NULL) const override
  {
    if (hint == SH_DialogButtonLayout)
      return QDialogButtonBox::GnomeLayout;
    return QProxyStyle::styleHint(hint, popt, pw, pret);
  }
};


// Linux has no single place to ask "is the desktop in dark mode?", and Qt5
// has no API for it at all: QStyleHints::colorScheme() only arrived in Qt
// 6.5, and what it does there is read the XDG desktop portal -- the
// cross-desktop standard every current desktop publishes. So read the
// portal directly, and fall back to each desktop's own setting for the
// ones that don't run one. Everything here stays inside the Qt 5.12 API,
// which is the oldest of the Ubuntu LTS releases this fork targets.
//
// This is needed because Qt5's gtk3 platform theme plugin loads and then
// supplies no palette. Verified on Mint/Cinnamon with QT_DEBUG_PLUGINS:
// libqgtk3.so loads, and the palette stays Qt's default light #efefef
// while the desktop sits on Mint-L-Dark. The gtk2 plugin does supply one
// (#383838 there), so whether a machine looks right comes down to whether
// qt5-style-plugins happens to be installed. Detecting it ourselves ends
// that lottery.

#define nSchemeNone  (-1)
#define nSchemeLight 0
#define nSchemeDark  1

// Run a helper and return its trimmed output, or a null string if it isn't
// installed, fails, or takes longer than nMsec. None of these answers is
// worth delaying startup for, and a desktop that hangs its own settings
// daemon shouldn't be able to hang Astrolog.

static QString SzRunToolQt(CONST char *szProg, CONST QStringList &lsArg,
  int nMsec = 400)
{
  QString strProg = QStandardPaths::findExecutable(QString(szProg));
  if (strProg.isEmpty())
    return QString();
  QProcess proc;
  proc.setStandardErrorFile(QProcess::nullDevice());
  proc.start(strProg, lsArg, QIODevice::ReadOnly);
  if (!proc.waitForFinished(nMsec)) {
    proc.kill();
    proc.waitForFinished(100);
    return QString();
  }
  if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
    return QString();
  return QString::fromLocal8Bit(proc.readAllStandardOutput()).trimmed();
}


// True if a theme name like "Mint-L-Dark" or "Adwaita-dark" names a dark
// variant. Deliberately narrow -- the variant is always a separate word or
// suffix, so matching a bare "dark" anywhere would catch theme names that
// merely contain it.

static bool FThemeNameDarkQt(CONST QString &str)
{
  QString strLow = str.toLower();
  strLow.remove(QChar('\''));          // gsettings quotes what it prints
  return strLow.endsWith("dark") || strLow.contains("-dark") ||
    strLow.contains("_dark") || strLow.contains(" dark");
}


// org.freedesktop.appearance/color-scheme: 0 no preference, 1 dark, 2
// light. This is the one route that is not desktop specific, and it is
// what Qt 6.5+ reads too.

static int NSchemeFromPortalQt(void)
{
  QStringList lsArg;
  lsArg << "call" << "--session"
    << "--dest" << "org.freedesktop.portal.Desktop"
    << "--object-path" << "/org/freedesktop/portal/desktop"
    << "--method" << "org.freedesktop.portal.Settings.Read"
    << "org.freedesktop.appearance" << "color-scheme";
  QString str = SzRunToolQt("gdbus", lsArg);
  // Prints as: (<<uint32 1>>,)
  int i = str.indexOf("uint32");
  if (i < 0)
    return nSchemeNone;
  int n = str.mid(i + 6).trimmed().leftRef(1).toInt();
  return n == 1 ? nSchemeDark : (n == 2 ? nSchemeLight : nSchemeNone);
}


// GNOME 42+ publishes the preference outright. Cinnamon and MATE don't,
// and only name a theme, so fall back to reading the variant off that.

static int NSchemeFromGSettingsQt(void)
{
  CONST char *rgszSchema[] = {"org.cinnamon.desktop.interface",
    "org.gnome.desktop.interface", "org.mate.interface", NULL};
  QString str;
  int i;

  str = SzRunToolQt("gsettings", QStringList()
    << "get" << "org.gnome.desktop.interface" << "color-scheme");
  if (str.contains("prefer-dark"))
    return nSchemeDark;
  if (str.contains("prefer-light"))
    return nSchemeLight;
  // "default" means the theme name is the only evidence there is.
  for (i = 0; rgszSchema[i] != NULL; i++) {
    str = SzRunToolQt("gsettings", QStringList()
      << "get" << rgszSchema[i] << "gtk-theme");
    if (!str.isEmpty())
      return FThemeNameDarkQt(str) ? nSchemeDark : nSchemeLight;
  }
  return nSchemeNone;
}


static int NSchemeFromXfceQt(void)
{
  QString str = SzRunToolQt("xfconf-query", QStringList()
    << "-c" << "xsettings" << "-p" << "/Net/ThemeName");
  if (str.isEmpty())
    return nSchemeNone;
  return FThemeNameDarkQt(str) ? nSchemeDark : nSchemeLight;
}


// Read one key out of an INI-style file.
//
// Written by hand rather than with QSettings, for two reasons, both found
// the hard way on Qt 5.15.13. QSettings cannot read a key whose section
// name contains a colon -- which is exactly kdeglobals' [Colors:Window]:
// allKeys() lists "Colors:Window/BackgroundNormal" and passing that very
// string back to value() returns an empty variant, as do beginGroup() and
// a percent-encoded key. And QSettings caches parsed files by timestamp
// and size, so a file rewritten inside the same second to a value of the
// same length reads back stale. The first bug silently reports every KDE
// desktop as light; the second only shows up under test, but both are
// invisible at the call site.

static QString SzIniValueQt(CONST QString &strPath, CONST char *szSect,
  CONST char *szKey)
{
  QFile file(strPath);
  QString strSect, str;
  int i;

  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    return QString();
  while (!file.atEnd()) {
    str = QString::fromLocal8Bit(file.readLine()).trimmed();
    if (str.startsWith(QChar('['))) {
      i = str.indexOf(QChar(']'));
      strSect = i > 1 ? str.mid(1, i - 1) : QString();
      continue;
    }
    if (strSect != QString(szSect))
      continue;
    i = str.indexOf(QChar('='));
    if (i < 0 || str.left(i).trimmed() != QString(szKey))
      continue;
    return str.mid(i + 1).trimmed();
  }
  return QString();
}


// KDE writes the active scheme's window background into kdeglobals as
// "r,g,b". Judging the colour is better than matching scheme names, of
// which there are many. Needs no helper program, so this still works on a
// machine with no glib tools installed.

static int NSchemeFromKdeQt(void)
{
  QStringList ls = SzIniValueQt(QDir::homePath() + "/.config/kdeglobals",
    "Colors:Window", "BackgroundNormal").split(QChar(','));

  if (ls.size() < 3)
    return nSchemeNone;
  return QColor(ls[0].trimmed().toInt(), ls[1].trimmed().toInt(),
    ls[2].trimmed().toInt()).lightness() < 128 ? nSchemeDark : nSchemeLight;
}


// The GTK config file, which a plain GTK setup writes even with no
// settings daemon running. Also needs no helper program.

static int NSchemeFromGtkFileQt(void)
{
  QString strPath = QDir::homePath() + "/.config/gtk-3.0/settings.ini";
  QString str;

  str = SzIniValueQt(strPath, "Settings",
    "gtk-application-prefer-dark-theme");
  if (!str.isEmpty()) {
    str = str.toLower();
    return (str == "1" || str == "true") ? nSchemeDark : nSchemeLight;
  }
  str = SzIniValueQt(strPath, "Settings", "gtk-theme-name");
  if (!str.isEmpty())
    return FThemeNameDarkQt(str) ? nSchemeDark : nSchemeLight;
  return nSchemeNone;
}


// Cheapest and most explicit first, then the standard, then per desktop,
// then the files that need no helper program at all.

static int NDarkPreferenceQt(void)
{
  CONST char *szEnv;
  int n;

  szEnv = getenv("ASTROLOG_QT_THEME");
  if (szEnv != NULL) {
    QString str = QString(szEnv).trimmed().toLower();
    if (str == "dark")
      return nSchemeDark;
    if (str == "light")
      return nSchemeLight;
    // "auto", "system", or anything else: detect as normal.
  }
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
  // Qt answers this itself from 6.5 on, and none of the rest need run.
  // Nothing below Qt6 compiles this, so it costs the Qt5 build nothing.
  Qt::ColorScheme cs = QGuiApplication::styleHints()->colorScheme();
  if (cs == Qt::ColorScheme::Dark)
    return nSchemeDark;
  if (cs == Qt::ColorScheme::Light)
    return nSchemeLight;
#endif
  if ((n = NSchemeFromPortalQt())    != nSchemeNone) return n;
  if ((n = NSchemeFromGSettingsQt()) != nSchemeNone) return n;
  if ((n = NSchemeFromXfceQt())      != nSchemeNone) return n;
  if ((n = NSchemeFromKdeQt())       != nSchemeNone) return n;
  if ((n = NSchemeFromGtkFileQt())   != nSchemeNone) return n;
  szEnv = getenv("GTK_THEME");
  if (szEnv != NULL)
    return FThemeNameDarkQt(QString(szEnv)) ? nSchemeDark : nSchemeLight;
  return nSchemeNone;
}


#ifdef QTTEST
// The suite exercises the detection above, which outside the tests only
// ever runs once, at startup, against whatever desktop the developer
// happens to be sitting at.

flag FThemeNameDarkTestQt(CONST char *sz)
  { return FThemeNameDarkQt(QString(sz)) ? fTrue : fFalse; }
int NSchemeFromKdeTestQt(void) { return NSchemeFromKdeQt(); }
int NSchemeFromGtkFileTestQt(void) { return NSchemeFromGtkFileQt(); }
#endif


// Follow the desktop into dark mode, if it's in it and Qt hasn't already
// worked that out for itself.

void ApplyColorSchemeQt(void)
{
  QColor coWind(0x35, 0x35, 0x35), coBase(0x2A, 0x2A, 0x2A),
    coText(0xFF, 0xFF, 0xFF), coHigh(0x2A, 0x82, 0xDA),
    coDim(0x7F, 0x7F, 0x7F);
  QStyle *pstyle;
  QPalette pal;

  if (NDarkPreferenceQt() != nSchemeDark)
    return;

  // A platform theme that already produced a dark palette knows the real
  // desktop colours, which are better than anything invented here. This is
  // the KDE case, and the gtk2 plugin's case.
  if (QApplication::palette().color(QPalette::Window).lightness() < 128)
    return;

  // Fusion is the one bundled style that draws entirely from the palette.
  // The GTK styles paint their own colours and would ignore all of this.
  pstyle = QStyleFactory::create("Fusion");
  if (pstyle != NULL)
    QApplication::setStyle(new AstroStyleQt(pstyle));

  pal.setColor(QPalette::Window, coWind);
  pal.setColor(QPalette::WindowText, coText);
  pal.setColor(QPalette::Base, coBase);
  pal.setColor(QPalette::AlternateBase, coWind);
  pal.setColor(QPalette::ToolTipBase, coWind);
  pal.setColor(QPalette::ToolTipText, coText);
  pal.setColor(QPalette::Text, coText);
  pal.setColor(QPalette::Button, coWind);
  pal.setColor(QPalette::ButtonText, coText);
  pal.setColor(QPalette::BrightText, QColor(0xFF, 0x40, 0x40));
  pal.setColor(QPalette::Link, coHigh);
  pal.setColor(QPalette::Highlight, coHigh);
  pal.setColor(QPalette::HighlightedText, QColor(0x00, 0x00, 0x00));
  pal.setColor(QPalette::Disabled, QPalette::Text, coDim);
  pal.setColor(QPalette::Disabled, QPalette::ButtonText, coDim);
  pal.setColor(QPalette::Disabled, QPalette::WindowText, coDim);
  pal.setColor(QPalette::Disabled, QPalette::HighlightedText, coDim);
  pal.setColor(QPalette::Disabled, QPalette::Highlight, QColor(0x50,0x50,0x50));
#if QT_VERSION >= QT_VERSION_CHECK(5, 12, 0)
  pal.setColor(QPalette::PlaceholderText, coDim);
#endif
  QApplication::setPalette(pal);
}


// Free what this backend allocated through Astrolog's own allocator. The
// counterpart of the #ifdef X11 and #ifdef WINANY blocks beside the call
// site in FinalizeProgram() (astrolog.cpp), and called from the same
// place. Only the -WM menu names come through PAllocate() here; the rest
// of what the Qt driver owns is QObjects, which go with the QApplication.
//
// Without this a settings file carrying -WM lines ends every session with
// upstream's own leak check firing -- "Number of memory allocations not
// freed before exiting: 13" for thirteen renamed macros -- which reads
// like a fault in the chart engine rather than an unfreed menu label.
void FinalizeQt(void)
{
  int i;

  if (qi.pnam != NULL) {
    delete qi.pnam;
    qi.pnam = NULL;
  }

  for (i = 0; i < cMacro; i++) {
    DeallocatePIf(qi.rgszMacro[i]);
    qi.rgszMacro[i] = NULL;
  }
  for (i = 0; i < cMSub; i++) {
    DeallocatePIf(qi.rgszMSub[i]);
    qi.rgszMSub[i] = NULL;
  }
}


/*
******************************************************************************
** Fetching a URL.
******************************************************************************
*/

// Astrolog needs to fetch exactly one thing: a body's positions from JPL
// Horizons, for the "j<n>" custom object definition. Upstream does that by
// handing a sprintf'd command line to system() and shelling out to wget --
// which means an undeclared dependency on wget being installed, a shell
// string built from a URL, no timeout, and a completely frozen window for
// however long the network takes.
//
// None of that is necessary here. Qt Network is part of the same Qt this
// build already requires, under the same licence as the modules already
// linked, and it speaks TLS -- which matters, because the Horizons URL is
// https and so a plain-HTTP client would not do.
//
// The call has to *look* synchronous, because its shared callers are: one
// of them is inside ComputeEphem() and a chart genuinely cannot be drawn
// without the position. So this runs a nested event loop rather than
// blocking, which is the difference between a window that keeps painting
// and offers a Cancel button, and one the desktop greys out and offers to
// kill.

#define cmsGetUrlQt 30000       // Give up on a fetch after this long.

// One manager for the whole session, not one per fetch.
//
// Qt pools connections and keeps them alive per manager, so a persistent
// one costs a TCP connection and a TLS handshake once and reuses them for
// every later request to the same host. A local manager -- which is what
// this was at first -- throws that away and pays a fresh handshake for
// every object looked up, which is exactly the waste the cache below it
// exists to avoid.
//
// The server does not offer HTTP/2 (its ALPN advertises http/1.1 only),
// so there is no multiplexing to be had; the attribute is set anyway so
// this picks it up for free if that ever changes. Pipelining would not
// help either way: each fetch runs to completion before the next starts,
// so there is never more than one request in flight to pipeline.
flag FGetUrlQt(CONST char *szUrl, CONST char *szFile)
{
  QEventLoop evloop;
  QTimer timer;
  QString strErr;
  flag fCancel = fFalse;

  if (qi.pnam == NULL)
    qi.pnam = new QNetworkAccessManager();

  QNetworkRequest req((QUrl(QString::fromUtf8(szUrl))));
  if (!req.url().isValid()) {
    PrintWarningQt("The address to download from is not a valid URL.",
      fTrue);
    return fFalse;
  }
  // Follow redirects, but never a downgrade from https to http.
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
    QNetworkRequest::NoLessSafeRedirectPolicy);
  req.setHeader(QNetworkRequest::UserAgentHeader,
    QString("%1/%2").arg(szAppName).arg(szVersionCore));
  req.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);

  QNetworkReply *prep = qi.pnam->get(req);
  QObject::connect(prep, &QNetworkReply::finished, &evloop, &QEventLoop::quit);

  // Where the time actually goes. Set ASTROLOG_QT_NETLOG to print it.
  //
  // The encrypted() signal fires only when a TLS handshake really happens,
  // so whether it fires at all is the categorical answer to "was the
  // connection reused" -- far better evidence than comparing wall times,
  // which on this service are dominated by however long Horizons takes to
  // compute an ephemeris and vary more between runs than the handshake
  // costs in the first place.
  flag fNetLog = (getenv("ASTROLOG_QT_NETLOG") != NULL);
  QElapsedTimer timNet;
  qint64 msTls = -1, msHead = -1, msFirst = -1;
  timNet.start();
  if (fNetLog) {
    QObject::connect(prep, &QNetworkReply::encrypted, prep,
      [&msTls, &timNet]() { msTls = timNet.elapsed(); });
    QObject::connect(prep, &QNetworkReply::metaDataChanged, prep,
      [&msHead, &timNet]() { msHead = timNet.elapsed(); });
    QObject::connect(prep, &QNetworkReply::readyRead, prep,
      [&msFirst, &timNet]() {
        if (msFirst < 0)
          msFirst = timNet.elapsed();
      });
  }

  // A fetch with no timeout is a hang waiting to happen.
  timer.setSingleShot(fTrue);
  QObject::connect(&timer, &QTimer::timeout, prep, [prep]() {
    prep->abort();
  });
  timer.start(cmsGetUrlQt);

  // Keep the window alive and give the user a way out. Suppressed when
  // popups are off, which is how an unattended run avoids putting up a
  // dialog nobody is there to dismiss.
  QProgressDialog *pdlg = NULL;
  if (!FNoPopupQt()) {
    pdlg = new QProgressDialog(
      QString("Downloading from %1...").arg(req.url().host()),
      "Cancel", 0, 0, gi.qwind);
    pdlg->setWindowTitle(szAppName);
    pdlg->setWindowModality(Qt::WindowModal);
    pdlg->setMinimumDuration(400);      // no flash for a fast reply
    QObject::connect(pdlg, &QProgressDialog::canceled, prep,
      [prep, &fCancel]() { fCancel = fTrue; prep->abort(); });
  }

  evloop.exec();
  timer.stop();
  if (fNetLog)
    printf("  net: %s  connect+TLS %s  headers %lldms  first byte %lldms  "
      "done %lldms\n",
      req.url().host().toLocal8Bit().constData(),
      msTls < 0 ? "reused" : QString("%1ms").arg(msTls).toLocal8Bit()
        .constData(),
      (long long)msHead, (long long)msFirst,
      (long long)timNet.elapsed());
  if (pdlg != NULL) {
    pdlg->close();
    delete pdlg;
  }

  // Say which of the three different things went wrong, rather than the
  // one "Failed to download" upstream prints for all of them.
  if (fCancel)
    strErr = "Download cancelled.";
  else if (prep->error() == QNetworkReply::OperationCanceledError)
    strErr = QString("Timed out after %1 seconds contacting %2.")
      .arg(cmsGetUrlQt / 1000).arg(req.url().host());
  else if (prep->error() != QNetworkReply::NoError)
    strErr = QString("Couldn't reach %1: %2")
      .arg(req.url().host()).arg(prep->errorString());

  if (strErr.isEmpty()) {
    QByteArray ba = prep->readAll();
    QFile file(QString::fromUtf8(szFile));
    if (!file.open(QIODevice::WriteOnly))
      strErr = QString("Couldn't write %1").arg(QString::fromUtf8(szFile));
    else {
      if (file.write(ba) != ba.size())
        strErr = QString("Couldn't finish writing %1")
          .arg(QString::fromUtf8(szFile));
      file.close();
    }
  }
  prep->deleteLater();

  if (!strErr.isEmpty()) {
    QByteArray baErr = strErr.toLocal8Bit();
    PrintWarningQt(baErr.constData(), fTrue);
    return fFalse;
  }
  return fTrue;
}


#ifdef QTTEST
// The offscreen QPA plugin -- which every headless run of this suite and
// both capture modes use -- emits "This plugin does not support
// propagateSizeHints()" on each window that sets size hints, several times
// per run. It carries no logging category, so QT_LOGGING_RULES cannot
// filter it. Drop that one string and pass everything else through, so a
// real Qt warning still reaches the log.
static QtMessageHandler s_pfnMsgPrevQt = NULL;

static void MessageFilterQt(QtMsgType typ, CONST QMessageLogContext &ctx,
  CONST QString &str)
{
  if (str.contains(QStringLiteral("does not support propagateSizeHints")))
    return;
  if (s_pfnMsgPrevQt != NULL)
    s_pfnMsgPrevQt(typ, ctx, str);
}
#endif


void BeginQt()
{
  static int s_argc = 1;
  static char *s_argv[] = { (char *)"astrolog", NULL };

#ifdef QTTEST
  s_pfnMsgPrevQt = qInstallMessageHandler(MessageFilterQt);
#endif
  gi.qapp = new QApplication(s_argc, s_argv);
  QApplication::setStyle(new AstroStyleQt);
  ApplyColorSchemeQt();
  LoadBundledFontsQt();
  SetUiFontQt();
  gi.qwind = new QMainWindow();
  gi.qwind->setWindowTitle(szAppName);
  gi.qcanvas = new ChartCanvas();
  qi.pscroll = new QScrollArea();
  qi.pscroll->setWidget(gi.qcanvas);
  qi.pscroll->setFrameShape(QFrame::NoFrame);
  // Center a chart smaller than the window rather than pinning it to the
  // top left corner, which is what Windows does with the leftover space.
  qi.pscroll->setAlignment(Qt::AlignCenter);
  gi.qwind->setCentralWidget(qi.pscroll);
  ApplySizeModeQt();
  BuildAstrologMenus(gi.qwind);
  ApplyHotkeysQt(gi.qwind);
  ApplyAccelTextQt(gi.qwind);
  StartAnimTimerQt(gi.qwind);
  gi.qwind->resize(gs.xWin, gs.yWin);
  // A -Ww in astrolog.as was parsed before this window existed.
  if (qi.fWindPos)
    gi.qwind->move(qi.xWind, qi.yWind);
  gi.qwind->show();
}


// Hand control over to Qt once the window is up, analogous to InteractX()'s
// XNextEvent() loop for X11, except here Qt itself drives all further
// keyboard, mouse, menu, and dialog interaction; this call blocks until the
// user quits (e.g. via File / Quit, which closes the main window).

void InteractQt()
{
  qi.fReady = true;
  RedrawQt();
#ifdef QTTEST
  // The test binary comes in through the same startup path as the real
  // one, so the suite runs against a fully built window: menus, hotkeys,
  // and a drawn chart. Run it here instead of handing over to the user.
  exit(NRunQtTestsQt());
#endif
  gi.qapp->exec();
  qi.fReady = false;
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
