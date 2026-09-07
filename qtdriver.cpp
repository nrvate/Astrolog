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
// qglobal.h first, so QT_VERSION and QT_VERSION_CHECK are defined for the
// guards below however this block is later reordered. Every other Qt
// header would define them too, but not provably.
#include <QtCore/qglobal.h>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QMenu>
#include <QtWidgets/QColorDialog>
// QAction and QActionGroup moved from QtWidgets to QtGui in Qt6. Together
// with QString::leftRef() further down, and the QStyleHints include below,
// that is the entire Qt6 incompatibility in this port -- measured by
// building it against Qt 6.8.3, not by reading release notes.
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QtGui/QAction>
#include <QtGui/QActionGroup>
#else
#include <QtWidgets/QAction>
#include <QtWidgets/QActionGroup>
#endif
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
#include <QtGui/QIcon>
#include <QtGui/QFontDatabase>
#include <QtCore/QDir>
#include <QtCore/QVector>
#include <QtCore/QPointer>
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
// Used by the Qt 6.5+ branch of NDarkPreferenceQt(). Present since Qt 5.0,
// so it needs no guard -- but nothing included it, and nothing had ever
// built this port on Qt6, so that branch had never once compiled.
#include <QtGui/QStyleHints>
#include <QtWidgets/QStyleFactory>
// For the checkbox and radio indicators AstroStyleQt redraws on a dark
// palette: the option type it has to copy, rather than slice.
#include <QtWidgets/QStyleOption>
#include <QtGui/QPalette>
#include <QtGui/QColor>

#include "astrolog.h"
#include "qtdriver.h"

#include <QtCore/QDir>
#include <QtCore/QTemporaryFile>
// For the one Windows call this file makes, resolved by name so no
// windows.h is needed: see ApplyTitleBarThemeQt().
#include <QtCore/QLibrary>

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

// The port's mutable window state, the analogue of Windows' Win32-only WI
// struct. The CONST tables -- menus, hotkeys, context menus -- stay
// beside the code that uses them.
//
// Rule: the chart-mode and relationship arrays are looked up BY VALUE,
// never by index. Menu build order is not a stable interface.
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

  // The interface settings, from -WF, -WG and -WI. They live here rather
  // than in a QSettings file so that Astrolog has one configuration file
  // on every platform -- the same astrolog.as the rest of these come
  // from -- instead of a second one per platform beside it.
  //
  // NULL family means the built-in default (Liberation Mono for the
  // chart, Liberation Sans for the interface); size 0 means follow -Xs
  // for the chart and the desktop for the interface.
  char *szFontCon = NULL;       // -WF: chart text face and size
  int nFontConSize = 0;
  flag fFontConAA = fTrue;      // =WFa
  char *szFontMen = NULL;       // -WG: interface face and size
  int nFontMenSize = 0;
  flag fFontMenAA = fTrue;      // =WGa
  int nThemePref = 0;           // -WI: 0 follow desktop, 1 light, 2 dark

  // The Animate menu's run/pause pair (see the note at BuildAnimateMenu).
  QAction *paAnimRun = NULL, *paAnimPause = NULL;
} QTUI;

static QTUI qi;

// Room left in one of qi's fixed menu tables. AssertRoomQt() is loud
// under QTTEST; AssertIndex is inert without it, so each call site also
// tests CRoomQt() and skips the write. The bound comes from the array
// itself, so resizing one cannot leave its guard behind.
#define CRoomQt(rg) ((int)(sizeof(rg)/sizeof((rg)[0])))
#define AssertRoomQt(c, rg) AssertIndex(c, CRoomQt(rg) - 1)


// The widget the chart is actually painted onto. Astrolog keeps rendering
// into an off screen buffer (gi.qim, the Qt analog of X11's Pixmap) via the
// Draw*() primitives in xgeneral.cpp; this widget's only job is to blit
// that buffer to the screen, and to tell Astrolog when its size changes.

static QAction *PaFindMenuActionQt(QWidget *pw, CONST QString &str);
static QAction *PaFindMenuActionLooseQt(QWidget *pw, CONST QString &str);
static void ConnectMenuQt(QAction *pa, QObject *pctx,
  std::function<void()> fn, flag fRegister = fTrue);
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


// Where a mouse event happened in screen coordinates. Qt6 deprecated
// globalPos() in favour of globalPosition(), which returns a QPointF and
// does not exist in Qt5; this is the one spelling both accept. Two call
// sites, and the Qt6 build named them on every compile -- nothing was
// reading that build's warnings, because tools/warning_audit.py covered
// the other four.

static QPoint PtGlobalQt(CONST QMouseEvent *pevent)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  return pevent->globalPosition().toPoint();
#else
  return pevent->globalPos();
#endif
}


// "-0q" forbids quitting: Windows refuses WM_CLOSE outright when
// us.fNoQuit is set. An event filter rather than a QMainWindow subclass,
// since gi.qwind is a plain one -- and it catches every route to a
// close, including the window manager's button, which guarding the two
// menu handlers would miss.

class NoQuitFilterQt : public QObject {
public:
  NoQuitFilterQt(QObject *pparent) : QObject(pparent) { }

protected:
  bool eventFilter(QObject *pobj, QEvent *pev) override
  {
    if (pev->type() == QEvent::Close && us.fNoQuit) {
      PrintWarningQt("Program exiting is not allowed now.", fFalse);
      pev->ignore();
      return true;
    }
    return QObject::eventFilter(pobj, pev);
  }
};


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
    // Applies in both modes: text charts draw into gi.qim too. Only
    // chase the widget's size when a resize is meant to change the
    // chart -- with that off the chart keeps its size and this widget is
    // sized to match it (ApplySizeModeQt), so redrawing to fit here
    // would fight that and repaint forever.
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
        ShowContextMenu(PtGlobalQt(pevent));
      return;
    }
    if (pevent->button() != Qt::LeftButton)
      return;

    // Alt+click on a world map relocates the chart to that spot. Windows
    // consumes the click either way, so Alt+click never also scribbles.
    if (pevent->modifiers() & Qt::AltModifier) {
      if (fMap && !gs.fConstel && !gs.fMollweide) {
        SetChartLocation(pevent->pos());
        fDrawAnchor = fFalse;
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
      ShowContextMenu(PtGlobalQt(pevent));
  }

private:
  // Where the last scribble left off, and whether there IS one. The flag
  // is not redundant with the point: QPoint::isNull() means BOTH
  // coordinates are zero, and (0,0) is the canvas's top left corner --
  // a perfectly good place to click. Using the point's own null state as
  // the sentinel meant that after clicking that one pixel, the next
  // Shift+click drew no line and the next Ctrl+click no rectangle,
  // because the anchor read as absent.
  QPoint ptDraw;
  flag fDrawAnchor = fFalse;
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
      if (!fDrawAnchor)
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
      if (!fDrawAnchor)
        return;
      QPainter p(gi.qim);
      p.setPen(QPen(col, Max((!gs.fThick ? 0 : 2) + gs.nThickAdjust, 0)));
      p.drawLine(ptDraw, pt);

      // Only a drag advances the anchor. A deliberate Shift+click leaves it
      // alone, so several lines can be fanned out from the same point.
      if (fDrag) {
        ptDraw = pt;
        fDrawAnchor = fTrue;
      }

    // A plain click sets a single pixel and remembers where it was. This
    // ignores pen thickness, exactly as Windows' SetPixel() does.
    } else {
      if (pt.x() >= 0 && pt.x() < gi.qim->width() &&
        pt.y() >= 0 && pt.y() < gi.qim->height())
        gi.qim->setPixel(pt, col.rgb());
      ptDraw = pt;
      fDrawAnchor = fTrue;
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


// Clear Screen. Fills the chart buffer with gi.kiOff, which is what
// Windows' DrawClearScreen() amounts to; that one cannot be used here
// because it draws through gi.qpaint, which exists only during a redraw.
// One path serves both modes, since text charts draw into this same
// buffer.
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

// The text cell comes from the font here, not the font from the cell.
// Windows uses Terminal, a bitmap face whose glyphs are exactly its 8x12
// cell; a TrueType face forced into that cell comes out thin and cramped.
// The chart is laid out in cells either way, so columns still line up,
// and the size still follows gs.nScale.
QString StrConsoleFontQt(void);
int NConsoleFontSizeQt(void);
flag FConsoleAntialiasQt(void);

static void SetTextMetricsQt()
{
  int nPix = Max(gs.nScale * 13 / 200, 9), nPref = NConsoleFontSizeQt();
  QString strFamily = StrConsoleFontQt();

  // The user's choice wins over both defaults, and each half is
  // independent: a family with no size still follows Character Scale.
  if (nPref > 0)
    nPix = nPref;
  qi.fontText = QFont(strFamily.isEmpty() ?
    QString("Liberation Mono") : strFamily);
  qi.fontText.setPixelSize(nPix);
  qi.fontText.setFixedPitch(fTrue);
  // Both halves of it. The style strategy is what the FONT engine does
  // when it rasterises a glyph; the render hint below, set on the
  // painter, is what the PAINTER does when it draws one. Qt has both on
  // by default, which is why this looked antialiased already -- saying
  // it out loud is what makes turning it OFF possible.
  qi.fontText.setStyleHint(QFont::Monospace, FConsoleAntialiasQt() ?
    QFont::PreferQuality : QFont::NoAntialias);
  qi.fontText.setStyleStrategy(FConsoleAntialiasQt() ?
    QFont::PreferAntialias : QFont::NoAntialias);
  QFontMetrics fm(qi.fontText);
  qi.xChar = fm.horizontalAdvance(QChar('M'));
  qi.yChar = fm.height();
  if (qi.xChar < 1) qi.xChar = 8;
  if (qi.yChar < 1) qi.yChar = 12;
}

// "Antialias Lines" (gs.fAntialias, Graphics / Chart Effects). A
// QPainter render hint rather than a port of Windows' 2x2 blend pass,
// which GDI needs and Qt does not.
//
// Safe for DrawFill(), which compares exact pixel colours to find its
// boundary: a softened edge pixel is still not the background colour, so
// a fill stops sooner rather than leaking through.

static void ApplyAntialiasQt(void)
{
  if (gi.qpaint != NULL)
    gi.qpaint->setRenderHint(QPainter::Antialiasing, gs.fAntialias);
}


// Called from AnsiColor() (general.cpp) for each colour change.
void TextColorQt(KI ki)
{
  qi.kvText = KvFromKi(ki);
}

// Called from PrintSz() (general.cpp) for each character, with the cell
// the text engine has reached and the character already decoded to a wide
// one. The decoding is that caller's job because a UTF-8 character spans
// two or three bytes and only the loop reading them can step over the
// rest; see the "#ifdef QT" branch there.
void TextCharQt(int xCell, int yCell, int wch)
{
  if (gi.qpaint == NULL)
    return;

  gi.qpaint->setPen(QColor(RgbR(qi.kvText), RgbG(qi.kvText),
    RgbB(qi.kvText)));
  gi.qpaint->drawText(xCell * qi.xChar + 4, (yCell + 1) * qi.yChar,
    QString(QChar(wch)));
}


// Capture a text chart as a string. Text mode renders through a separate
// path from graphics -- Action() calls PrintChart(), driven by
// is.S/is.szFileScreen rather than gi.qpaint -- so point is.szFileScreen
// at a temp file, run Action(), and read it back. HTML output is offered
// so colour arrives as <font color> tags rather than ANSI escapes.

// Print, equivalent to Windows' DlgPrint(). The chart is scaled up before
// rendering so it does not print at screen resolution, and
// us.fSmartSave forces a white background.
//
// Windows scales by METAMUL (12) into a printer DC. Here the chart is
// rendered into a real QImage first -- DrawFill() reads and writes gi.qim
// directly, so gi.qim and gi.qpaint must describe one surface -- and
// METAMUL would want ~250MB for a default window. This multiplier still
// prints well above screen resolution.

#define PRINTMUL 4

static QString CaptureTextChartQt(flag fHTML);   // defined below

// Render the current chart onto an already-chosen printer. Split out from
// PrintChartQt() so that something other than a person with a printer can
// exercise it: a QPrinter set to PdfFormat goes through every line below,
// which is how the print path is tested at all -- before this it was the
// one user-facing command with no coverage of any kind.

static void PrintChartToQt(QPrinter *pprinter)
{
  QPrinter &printer = *pprinter;

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
    // brace restores all seven on every exit.
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
      ApplyAntialiasQt();
      InitColors();
      // DrawChartX() switches on gi.nMode with no default case, and mode
      // 0 is not a chart (gWheel is 1), so an unset mode draws nothing.
      // RedoMenuQt() and FActionX() carry the same guard; this path goes
      // straight to DrawChartX() and needs its own. Reachable while the
      // screen still shows a chart, since "Show Graphics" zeroes the mode
      // and its redraw is a no-op under "Don't Automatically Redraw".
      if (gi.nMode == 0)
        gi.nMode = DetectGraphicsChartMode();
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


void PrintChartQt()
{
  QPrinter printer(QPrinter::HighResolution);
  QPrintDialog dlgPrint(&printer, gi.qwind);

  dlgPrint.setWindowTitle("Print Chart");
  if (dlgPrint.exec() != QDialog::Accepted)
    return;
  PrintChartToQt(&printer);
}

#ifdef QTTEST
// Print to a PDF, which is the only way a headless run can reach the
// code above.
void PrintChartToFileTestQt(CONST char *szFile)
{
  QPrinter printer(QPrinter::HighResolution);

  printer.setOutputFormat(QPrinter::PdfFormat);
  printer.setOutputFileName(QString::fromUtf8(szFile));
  PrintChartToQt(&printer);
}
#endif


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
  // QTemporaryFile rather than mkstemp into a hardcoded "/tmp": it puts
  // the file wherever the platform keeps temporaries, removes it when it
  // goes out of scope, and is the only one of the two that exists off
  // POSIX. These three sites were the entire non-portable surface of the
  // Qt backend -- everything else here is Qt or already has a "#ifdef PC"
  // path -- so this is what a Qt build on Windows would otherwise trip
  // over first.
  QTemporaryFile tmp(QDir::tempPath() + "/astrolog-qt-paste-XXXXXX");
  QByteArray baTemp;
  char *szTemp;
  flag fRet;

  if (pmime == NULL ||
    (!pmime->hasImage() && !pmime->hasText())) {
    QMessageBox::warning(gi.qwind, szAppName,
      "There is nothing on the clipboard to paste.");
    return;
  }
  if (!tmp.open())
    return;
  // Closed but not removed: the code below reopens it by name, which an
  // open handle would block on Windows. The destructor still deletes it.
  baTemp = tmp.fileName().toLocal8Bit();
  szTemp = baTemp.data();
  tmp.close();

  if (pmime->hasImage()) {
    QImage im = qvariant_cast<QImage>(pmime->imageData());
    if (im.isNull() || !im.save(szTemp, "BMP"))
      fRet = fFalse;
    else {
      // Two deliberate differences from Windows' FFilePaste(), both from
      // the clipboard being reached differently. Its fNoHeader argument is
      // fTrue there because CF_DIB hands over a device independent bitmap
      // with no 14-byte BITMAPFILEHEADER on it; QImage::save() writes a
      // real .bmp, header and all, so this one has to read it as a file.
      // And Windows leaves gi.fBmp alone here while setting it in
      // cmdOpenBackground, which loads a bitmap into the same slot -- the
      // pasted image is what a 24-bit surface is FOR, so this sets it in
      // both places rather than in one.
      fRet = FLoadBmp(szTemp, &gi.bmpBack, fFalse);
      if (fRet)
        gi.fBmp = fTrue;
    }
    if (!fRet)
      QMessageBox::warning(gi.qwind, szAppName,
        "Could not read the bitmap on the clipboard.");
  } else {
    QFile file(szTemp);
    QByteArray baText = pmime->text().toLocal8Bit();
    // Did the clipboard text reach the disk whole? The write and the
    // FLUSH both have to say so: close() returns void and flushes there,
    // so a failure that only appears when the buffer is written -- no
    // space, an I/O error -- is reported through error() and nowhere
    // else. Unchecked, the consequence here is not a lost file but a
    // misleading one: FInputData() parses whatever did get written and
    // reports a malformed chart, blaming the clipboard for a full disk.
    flag fWrote = file.open(QIODevice::WriteOnly);
    if (fWrote) {
      fWrote = (file.write(baText) == baText.size());
      file.close();
      fWrote = fWrote && file.error() == QFileDevice::NoError;
    }
    // FInputData() prints its own diagnostics on a malformed file, so
    // only the file trouble before it needs saying -- but it does need
    // saying. The image branch above reports what it cannot use; this one
    // reported nothing, so a paste that failed looked exactly like a menu
    // item that does nothing.
    if (!fWrote)
      QMessageBox::warning(gi.qwind, szAppName,
        QString("Could not write the clipboard text to a temporary file: "
          "%1").arg(file.errorString()));
    fRet = fWrote && FInputData(szTemp);
  }
  if (fRet)
    RecastAndRedrawQt();
}


// both need the plain-text or HTML chart listing PrintChart() would
// print, captured via a scratch file rather than shown/redirected for
// real. Doesn't touch us.fGraphics; callers decide how to reflect that.

// Render the current chart's text output to a file, touching nothing.
// Three things have to be put back, and only two are obvious:
//
//   us.fGraphics  Action() branches on it. Left true, it takes the
//                 graphics path, which nests a second Qt event loop.
//   us.fTextHTML  the caller chooses, rather than inheriting whatever
//                 File Settings last left.
//   is.S          the nested Action() opens it on the file and fcloses
//                 it, but never restores the caller's -- leaving is.S on
//                 a closed FILE and arming a double fclose at exit.
//
// And "Export Text and Print in Intuitive Manner" (us.fSmartSave, "-YO",
// on by DEFAULT), which is the whole of what Windows does around its own
// three text captures -- Save Text, Copy Text and printing a text chart,
// wdriver.cpp:2777 and 2852. Two shapes, chosen the way Windows chooses
// them, on whether the capture is HTML:
//
//   plain text: turn off Ansi colour and Ansi characters, so an exported
//               ".txt" is text rather than a file full of "ESC[1;31m"
//               and code page 437 box edges. Nothing did this here, and
//               "Colored Text" in the View menu turns BOTH of those on.
//   HTML:       force the white-background palette instead. Astrolog's
//               HTML page has a white body and SzColorHTML() reads
//               rgbbmp[], so without the swap a chart written for a black
//               background is printed onto a white page. Ansi characters
//               go off here too, as they do on Windows.
//
// InitColorPalette() is a no-op unless "Alternate Color Palette"
// (gs.fAltPalette) is on, which is what makes the second palette exist;
// calling it either way is what Windows does.

void CaptureTextToFileQt(CONST char *szFile, flag fHTML)
{
  flag fGraphicsSave = us.fGraphics, fTextHTMLSave = us.fTextHTML;
  flag fAnsiColorSave = us.fAnsiColor, fAnsiCharSave = us.fAnsiChar;
  flag fInverseSave = gs.fInverse, fSmart = us.fSmartSave;
  FILE *fileSave = is.S;

  us.fGraphics = fFalse;
  us.fTextHTML = fHTML;
  if (fSmart) {
    us.fAnsiChar = fFalse;
    if (!fHTML)
      us.fAnsiColor = fFalse;
    else {
      gs.fInverse = fTrue;
      InitColorPalette(1);
    }
  }
  FCloneSz(szFile, &is.szFileScreen);
  Action();
  FCloneSz(NULL, &is.szFileScreen);
  if (fSmart && fHTML)
    InitColorPalette(fInverseSave);
  gs.fInverse = fInverseSave;
  us.fAnsiChar = fAnsiCharSave;
  us.fAnsiColor = fAnsiColorSave;
  us.fTextHTML = fTextHTMLSave;
  us.fGraphics = fGraphicsSave;
  is.S = fileSave;
}


static QString CaptureTextChartQt(flag fHTML)
{
  QTemporaryFile tmp(QDir::tempPath() + "/astrolog-qt-text-XXXXXX");
  if (!tmp.open())
    return QString();
  QByteArray baTemp = tmp.fileName().toLocal8Bit();
  char *szTemp = baTemp.data();
  tmp.close();
  CaptureTextToFileQt(szTemp, fHTML);

  QString qs;
  QFile file(szTemp);
  if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QByteArray ba = file.readAll();
    file.close();
    // Decode by the codepage the file was written in. With us.fAnsiChar
    // on -- the View menu's "Colored Text" -- text wheels are drawn out
    // of IBM code page 437 line characters, which are raw high bytes,
    // not UTF-8. Same mapping TextCharQt() applies on the canvas.
    // Windows hands these to the clipboard as CF_OEMTEXT and lets the
    // paste target convert; a QString has to be converted here.
    if (us.nCharsetOut == ccUTF8)
      qs = QString::fromUtf8(ba);
    else if (us.nCharsetOut == ccLatin || us.nCharset == ccLatin)
      qs = QString::fromLatin1(ba);
    else {
      qs.reserve(ba.size());
      for (int i = 0; i < ba.size(); i++) {
        uchar b = (uchar)ba.at(i);
        qs += QChar(b >= 128 ? (wchar)WchFromChIBM(b) : (wchar)b);
      }
    }
  }
  // Astrolog writes a UTF-8 byte order mark at the head of the file when
  // us.nCharsetOut is ccUTF8 ("-Yao3"). That is right for a file and
  // wrong for a string: QString::fromUtf8() does not strip it, so it
  // survives as U+FEFF -- an invisible first character on the clipboard
  // for Copy Chart Text Output, and a stray one before "<html>" when
  // PrintChartQt() hands this to QTextDocument::setHtml(). The file
  // export path writes its own file and keeps its BOM, which is what a
  // file wants; this is the capture-into-memory path and does not.
  if (qs.startsWith(QChar(0xFEFF)))
    qs.remove(0, 1);
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


// "-~Q3", fired once the screen has been repainted. Windows does this
// after the whole paint and in BOTH modes -- wdriver.cpp:2929, outside the
// "if (!us.fGraphics)" block above it -- so it is called from the two
// places RedrawQt() finishes rather than only from the graphics one, which
// is where it used to sit. A hook set while a text chart was on screen
// never ran at all.

static void NotifyRedrawQt(void)
{
#ifdef EXPRESS
  if (!us.fExpOff && FSzSet(us.szExpDisp3))
    ParseExpression(us.szExpDisp3);
#endif
}


void RedrawQt()
{
  if (qi.fNoUpdate)
    return;
  // "-0X" forbids graphics, and Windows enforces it at the end of every
  // command (wdriver.cpp:2507) -- which is this point: after whatever the
  // user asked for, before the chart is drawn. This port never referenced
  // us.fNoGraphics at all, so the switch did nothing here and the View
  // menu could turn graphics straight back on. Windows corrects its own
  // menu check mark in the same breath, so this does too.
  if (us.fNoGraphics && us.fGraphics) {
    us.fGraphics = fFalse;
    if (qi.paGraphics != NULL)
      qi.paGraphics->setChecked(fFalse);
  }
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
  ApplyAntialiasQt();
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
    gi.qpaint->setRenderHint(QPainter::TextAntialiasing,
      FConsoleAntialiasQt());
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
    NotifyRedrawQt();
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
  NotifyRedrawQt();
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

// Every checkable menu item, paired with a predicate that re-reads the
// setting behind it. Windows' RedoMenu() does the same job with sixty
// hand written CheckMenu() calls, and a hand written list can fall behind
// the menus; this one is registered by the item itself, so it can't.
//
// QPointer rather than a raw QAction*: the menu bar is built once and
// these outlive nothing today, but a null is cheaper than the rule that
// they must.

typedef struct {
  QPointer<QAction> pa;
  std::function<bool()> pfn;
} MENUCHECK;

static QVector<MENUCHECK> rgmcheckQt;

static QAction *PaRegisterCheckQt(QAction *pa, std::function<bool()> pfn)
{
  MENUCHECK mc;

  if (pa == NULL)
    return pa;
  mc.pa = pa;
  mc.pfn = pfn;
  rgmcheckQt.append(mc);
  return pa;
}


// Re-derive every menu check mark from the setting behind it, the
// equivalent of Windows' RedoMenu(). Call it ONLY where Windows sets
// wi.fMenuAll: Enter Command Line, a macro, Graphics Settings, Redraw.
// Elsewhere see QT_GUI_PLAN.md item 9.
//
// Excludes the chart type radio, which has its own machinery
// (SnapChartModeQt/SyncChartModeFromFlagsQt). A pure read, unlike
// SyncRestrictMenuQt(), which writes the "Include <category>" flags back
// from ignore[]; those are calculation inputs, not just check marks.

void RedoMenuQt()
{
  int i;

  for (i = 0; i < rgmcheckQt.size(); i++)
    if (rgmcheckQt[i].pa != NULL)
      rgmcheckQt[i].pa->setChecked(rgmcheckQt[i].pfn());
}

static QAction *AddToggleAction(QMenu *pmenu, CONST char *szLabel,
  flag *pfield, flag fRecast)
{
  QAction *pa = pmenu->addAction(szLabel);
  pa->setCheckable(true);
  pa->setChecked(*pfield != 0);
  PaRegisterCheckQt(pa, [pfield]() { return *pfield != 0; });
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
  PaRegisterCheckQt(pa, [value, ptarget]() { return *ptarget == value; });
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
  AssertRoomQt(qi.cChartMode, qi.rgpaChartMode);
  if (qi.cChartMode < CRoomQt(qi.rgpaChartMode)) {
    qi.rgpaChartMode[qi.cChartMode] = pa;
    qi.rgnChartMode[qi.cChartMode] = mode;
    qi.cChartMode++;
  }
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
  AssertRoomQt(qi.cChartMode, qi.rgpaChartMode);
  if (qi.cChartMode < CRoomQt(qi.rgpaChartMode)) {
    qi.rgpaChartMode[qi.cChartMode] = pa;
    qi.rgnChartMode[qi.cChartMode] = mode;
    qi.cChartMode++;
  }
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
  // Two the table cannot reach. Windows clears a raw struct byte range
  // here, and it holds fAtlasLook ("-N") and fZoneChange ("-Nz"), which
  // have no rgchartmode[] row. Both are ADDITIVE listings -- charts1.cpp
  // appends each to whatever the chart already printed -- and this port
  // has no menu item for either, so left set they staple an atlas dump
  // onto every chart from then on. backend_parity_audit.py derives that
  // range from the struct and fails if a flag in it is neither in the
  // table nor named here.
  us.fAtlasLook = us.fZoneChange = fFalse;
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


// Command switches set the us.f* chart-type flags directly, leaving
// gi.nMode and the Chart menu on whatever was last picked. Snapshot the
// flags around the switches, and route a newly set one through
// SetChartModeQt() so flags, mode and menu agree. Windows has the same
// split and leaves its menu and chart disagreeing.
//
// Snapshot-and-compare rather than deriving the mode from the flags
// afterward: a switch sets its own flag and leaves the previous one
// standing, so after "-Z" from a wheel both fListing and fHorizon are
// true. Which is newly set is unambiguous; which has priority is not.

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

// Add one relationship chart type. fToggle marks the two items Windows
// runs through a shared toggle -- SetRel(us.nRel ? rcNone : rcDual) -- so
// either turns a relationship chart off, and turns comparison on when
// there is none. Both carry the same "c" accelerator in astrolog.rc, and
// that toggle is the only way back to a single chart from the keyboard.
// Every other mode is a plain set.

static QAction *AddRelAction(QMenu *pmenu, QActionGroup *pgroup,
  CONST char *szLabel, int rc, flag fToggle = fFalse)
{
  QAction *pa = pmenu->addAction(szLabel);
  pa->setCheckable(true);
  pa->setActionGroup(pgroup);
  pa->setChecked(us.nRel == rc);
  PaRegisterCheckQt(pa, [rc]() { return us.nRel == rc; });
  ConnectMenuQt(pa, pa, [rc, fToggle]() {
    SetRelQt(fToggle ? (us.nRel ? rcNone : rcDual) : rc);
  });
  AssertRoomQt(qi.cRel, qi.rgpaRel);
  if (qi.cRel < CRoomQt(qi.rgpaRel)) {
    qi.rgpaRel[qi.cRel] = pa;
    qi.rgnRel[qi.cRel] = rc;
    qi.cRel++;
  }
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


// Defined with the rest of the colour-scheme code near the bottom of this
// file; the View menu needs them several thousand lines earlier.
QString StrThemePrefQt(void);
void SetThemePrefQt(CONST char *sz);
void ApplyColorSchemeQt(void);
void ApplyTitleBarThemeQt(QWidget *pw);


// The interface theme submenu: System, Light, Dark, exclusive.
//
// A menu rather than a dialog control: the dialogs are generated from
// astrolog.rc and audited against it, so a hand-added control there would
// be a permanent divergence. Windows has no such setting, since it
// follows the OS.
//
// Exclusivity by hand rather than QActionGroup, which moved from
// QtWidgets to QtGui in Qt6 and this file builds against both.

static void BuildThemeMenuQt(QMenu *pmenuWin)
{
  struct { CONST char *szLabel, *szValue; } rgTheme[] = {
    {"&System",  "auto" },
    {"&Light",   "light"},
    {"&Dark",    "dark" }};
  QMenu *pmenuTheme = pmenuWin->addMenu("&Interface Theme");
  QString strNow = StrThemePrefQt();
  QAction *rgpa[3];
  int i;

  for (i = 0; i < 3; i++) {
    rgpa[i] = pmenuTheme->addAction(rgTheme[i].szLabel);
    rgpa[i]->setCheckable(true);
    rgpa[i]->setChecked(strNow == QString(rgTheme[i].szValue));
    CONST char *szValue = rgTheme[i].szValue;
    PaRegisterCheckQt(rgpa[i],
      [szValue]() { return StrThemePrefQt() == QString(szValue); });
  }
  for (i = 0; i < 3; i++) {
    CONST char *szValue = rgTheme[i].szValue;
    QAction *pa0 = rgpa[0], *pa1 = rgpa[1], *pa2 = rgpa[2];
    ConnectMenuQt(rgpa[i], pmenuWin,
      [szValue, pa0, pa1, pa2]() {
        SetThemePrefQt(szValue);
        pa0->setChecked(QString(szValue) == "auto");
        pa1->setChecked(QString(szValue) == "light");
        pa2->setChecked(QString(szValue) == "dark");
        // Qt propagates a palette change to every existing widget, so the
        // menus and dialogs restyle without a restart. The chart is drawn
        // by this port rather than by Qt, so it needs telling.
        ApplyColorSchemeQt();
        ApplyTitleBarThemeQt(gi.qwind);
        RedrawForceQt();
      });
  }
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
  PaRegisterCheckQt(qi.paGraphics, []() { return us.fGraphics != 0; });
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
    []() { RedoMenuQt(); RedrawForceQt(); });
  QAction *paClear = pmenuWin->addAction("&Clear Screen");
  ConnectMenuQt(paClear, pwind,
    []() { ClearScreenQt(); });
  BuildThemeMenuQt(pmenuWin);
  QAction *paHourglass = pmenuWin->addAction("&Hourglass on Redraw");
  paHourglass->setCheckable(true);
  paHourglass->setChecked(qi.fHourglass != fFalse);
  PaRegisterCheckQt(paHourglass, []() { return qi.fHourglass != fFalse; });
  ConnectMenuQt(paHourglass, pwind,
    [paHourglass]() {
      qi.fHourglass = !qi.fHourglass;
      paHourglass->setChecked(qi.fHourglass != fFalse);
    });
  pmenuWin->addSeparator();

  QAction *paChartWin = pmenuWin->addAction("Ch&art Resizes Window");
  paChartWin->setCheckable(true);
  paChartWin->setChecked(qi.fChartWindow != fFalse);
  PaRegisterCheckQt(paChartWin, []() { return qi.fChartWindow != fFalse; });
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
  PaRegisterCheckQt(paWinChart, []() { return qi.fWindowChart != fFalse; });
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
  PaRegisterCheckQt(paFull,
    []() { return gi.qwind != NULL && gi.qwind->isFullScreen(); });
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
  PaRegisterCheckQt(paColorText, []() { return us.fAnsiColor != 0; });
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
  PaRegisterCheckQt(paInterpret, []() { return us.fInterpret != 0; });
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
  PaRegisterCheckQt(qi.paApplying, []() { return us.nAppSep == 1; });
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
  if (pfield != NULL)
    PaRegisterCheckQt(pa, [pfield]() { return *pfield != 0; });
  else
    PaRegisterCheckQt(pa, [lo]() { return !ignore[lo]; });
  if (pfield != NULL && qi.ccatres < CRoomQt(qi.rgcatres)) {
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
  PaRegisterCheckQt(qi.paHelio, []() { return us.objCenter != oEar; });
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
  PaRegisterCheckQt(qi.paSolar, []() { return us.objOnAsc != 0; });
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
  PaRegisterCheckQt(qi.paProgress, []() { return us.fProgress != 0; });
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
  PaRegisterCheckQt(paReverse, []() { return gs.fInverse != 0; });
  ConnectMenuQt(paReverse, pwind, [paReverse]() {
    gs.fInverse = !gs.fInverse;
    paReverse->setChecked(gs.fInverse != 0);
    InitColorPalette(gs.fInverse);
    RedrawQt();
  });
  QAction *paMono = pmenu->addAction("&Monochrome");
  paMono->setCheckable(true);
  paMono->setChecked(!gs.fColor);
  PaRegisterCheckQt(paMono, []() { return !gs.fColor; });
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
  PaRegisterCheckQt(paSidebar, []() { return gs.fDoSidebar != 0; });
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
  PaRegisterCheckQt(paConstel, []() { return gs.fConstel != 0; });
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
  PaRegisterCheckQt(paStarLine, []() { return qi.fStarLine != 0; });
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


// Resolve a file the program is about to hand to the desktop's default
// application -- the documentation, the four data files, the two ".url"
// shortcuts, and F1's help. Windows routes every one of those through
// BootExternal(), which refuses outright when "-0i" has turned file
// reading off; nothing here did, so under that lockdown the Qt build
// still opened all of them. The wording is BootExternal()'s own
// ("reading"), not the file picker's ("input"), because these are the
// same sites.

static flag FBootPathQt(CONST char *szFile, char *szPath, int cchPath)
{
  char sz[cchSzMax];

  if (us.fNoRead) {
    PrintWarning("File reading is disabled.");
    return fFalse;
  }
  if (FileOpen(szFile, 2, szPath, cchPath) == NULL) {
    // PrintWarning() and not QMessageBox::warning(), which is what these
    // sites used to call: Windows says it through PrintWarning() too, so
    // this wording is its wording, and more to the point that is the one
    // route "Don't Show Popup Messages" (qi.fNoPopup) can suppress. A
    // direct box ignores the setting the user just ticked.
    sprintf2(S(sz), "File '%s' not found!", szFile);
    PrintWarning(sz);
    return fFalse;
  }
  return fTrue;
}


#ifdef QTTEST
flag FBootPathTestQt(CONST char *szFile, char *szPath, int cchPath)
{
  return FBootPathQt(szFile, szPath, cchPath);
}
#endif


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
    RedoMenuQt();
    RecastAndRedrawQt();
    return;
  }
  if (iMacro == 1) {
    if (FBootPathQt("astrolog.htm", S(szPath)))
      QDesktopServices::openUrl(QUrl::fromLocalFile(szPath));
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
#ifdef QTTEST
      // TestMenuExtraQt() walks this menu bar looking for items Windows
      // does not have, and cannot key on these labels: -WM makes every
      // one of them user data, and this machine's own nrvate.as renames
      // thirteen. Mark them, so that audit skips them for what they are
      // rather than for what they happen to be called.
      pa->setProperty("astrologMacro", iMacro);
#endif
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


// Defined with the rest of the interface settings, far below; declared
// here the way StrThemePrefQt() is, because the switches that set them
// are parsed long before that point in the file.
#define nFontSizeMinQt 6
#define nFontSizeMaxQt 48
#define nThemeAutoQt   0
#define nThemeLightQt  1
#define nThemeDarkQt   2
void SetConsoleFontQt(CONST char *szFamily, int nSize);
void SetMenuFontQt(CONST char *szFamily, int nSize);
void SetThemePrefNQt(int n);

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

  case 'F':
  case 'G':
    // The two interface fonts: -WF "<family>" <size> for the chart text,
    // -WG "<family>" <size> for the menus and dialogs, and =WFa / =WGa
    // for whether each is antialiased. An empty family or a size outside
    // the accessors' range means "no preference", which is also what an
    // absent switch means, so the two spell the same thing.
    {
      flag fCon = (pin->argv[0][pos] == 'F');
      if (ch1 == 'a') {
        // Not SwitchF() on a ternary: the macro expands to "f = ...", and
        // "a ? b : c = d" binds as "a ? b : (c = d)", so only one of the
        // two would ever be assigned.
        flag *pf = fCon ? &qi.fFontConAA : &qi.fFontMenAA;
        *pf = FSwitchF(*pf);
        break;
      }
      if (FErrorArgc(fCon ? "WF" : "WG", pin->argc, 2))
        return tcError;
      i = NFromSz(pin->argv[2]);
      // 0 is "no preference" and legal; anything else has to be a size a
      // window can actually be drawn in. Refused rather than clamped, the
      // way -Wx and every other ranged switch here refuses, so a typo in
      // a hand-edited astrolog.as is reported instead of silently
      // becoming something else.
      if (FErrorValN(fCon ? "WF" : "WG", i != 0 &&
        !FBetween(i, nFontSizeMinQt, nFontSizeMaxQt), i, 2))
        return tcError;
      if (fCon)
        SetConsoleFontQt(pin->argv[1], i);
      else
        SetMenuFontQt(pin->argv[1], i);
      darg += 2;
    }
    break;

  case 'I':
    // The interface theme, as the number the settings file carries.
    if (FErrorArgc("WI", pin->argc, 1))
      return tcError;
    i = NFromSz(pin->argv[1]);
    if (FErrorValN("WI", !FBetween(i, nThemeAutoQt, nThemeDarkQt), i, 0))
      return tcError;
    SetThemePrefNQt(i);
    darg++;
    break;

  // The same three flags Windows sets here, and this build has all three
  // -- qi.fNoUpdate, qi.fNoPopup and qi.fBmpWindow, each with a dialog
  // control editing it. They were accepted as no-ops, which meant a
  // setting the GUI offers could not be written down: FOutputSettings()
  // left them out too, on the honest grounds that writing a switch this
  // build ignored would claim a round trip that did not happen.
  case 'n':
    SwitchF(qi.fNoUpdate);
    break;

  case 't':
    SwitchF(qi.fNoPopup);
    break;

  case 'b':
    SwitchF(qi.fBmpWindow);
    break;

  // Screen saver mode, which has no counterpart here. Accepted so that a
  // shared astrolog.as keeps loading.
  case 'Z':
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


// One animation frame. Not reentrant: a cast can enter a nested event
// loop -- a JPL Horizons fetch, or any modal it puts up -- where this
// timer keeps firing, and a second tick would nest another cast inside
// the first without bound.
//
// A flag rather than stopping the timer, which would have to be
// restarted on every path out of the cast.

static flag s_fAnimTickQt = fFalse;

static void AnimTickQt(void)
{
  // Same guard Windows' WM_TIMER uses. Note gs.nAnim < 1 covers both
  // "off" (negative, remembering the rate) and "never set".
  if (gs.nAnim < 1 || gi.fPause)
    return;
  if (s_fAnimTickQt)
    return;
  s_fAnimTickQt = fTrue;
  // Redraw, NOT recast. Animate() casts the chart itself where it moved
  // the time (its last two lines are CastRelation()/CastChart(0)), and
  // returns early without casting at all where it only rotated a map or a
  // globe -- so a recast here was a second cast per frame in the first
  // case and an entirely gratuitous one in the second. Measured at 2 and 1
  // casts per tick; Windows does 1 and 0, because its WM_TIMER sets
  // wi.fRedraw and nothing else (wdriver.cpp:1031).
  //
  // ciMain is already right on every path out of Animate(): it assigns it
  // for a plain chart, and restores ciCore from it for the relationship
  // and transit ones, which is the whole of what RecastAndRedrawQt() would
  // have done before casting again.
  Animate(gs.nAnim, gi.nDir);
  RedrawQt();
  s_fAnimTickQt = fFalse;
}

#ifdef QTTEST
void AnimTickTestQt(void) { AnimTickQt(); }
flag FAnimTickBusyTestQt(void) { return s_fAnimTickQt; }
void SetAnimTickBusyTestQt(flag f) { s_fAnimTickQt = f; }
#endif

static void StartAnimTimerQt(QMainWindow *pwind)
{
  qi.ptimerAnim = new QTimer(pwind);
  QObject::connect(qi.ptimerAnim, &QTimer::timeout, pwind,
    []() { AnimTickQt(); });
  qi.ptimerAnim->start(qi.nTimerDelay);
}


// Animation state. gs.nAnim carries two things: its magnitude is the jump
// rate and its sign is whether animation is running, with gi.fPause a
// second stop on top. The encoding is fixed by -Xn and saved settings, so
// it is decoded here once and nowhere else in this file.
//
// One idea, not two: animation runs or it does not, and one control does
// both. Windows has a separate "arm it first" step; see "Known
// divergences" in QT_GUI_PLAN.md.

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
  PaRegisterCheckQt(pa, [rate]() { return NAbs(gs.nAnim) == rate; });
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
  PaRegisterCheckQt(pa, [factor]() { return NAbs(gi.nDir) == factor; });
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
  PaRegisterCheckQt(qi.paAnimRun, []() { return FAnimRunningQt() != 0; });
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
  PaRegisterCheckQt(paReverse, []() { return gi.nDir < 0; });
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
  PaRegisterCheckQt(qi.paAnimPause, []() { return !FAnimRunningQt(); });
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
      if (FBootPathQt(szFile, S(szPath)))
        QDesktopServices::openUrl(QUrl::fromLocalFile(szPath));
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
      if (!FBootPathQt(szFile, S(szPath)))
        return;
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


// Register the bundled astrology symbol fonts with Qt at startup, so
// Graphics Settings can select one on a machine that never installed
// them. The family names in font/ match rgszFontName[], which is what
// DrawSzFont() looks them up by.
//
// Not bundled: Wingdings, and the plain text families at the end of
// rgszFontName[], which come from the system if present.

// Apply the interface font. Called at startup and again when Display
// Settings closes, so a new face reaches the already built window.
//
// Liberation Sans by default: astrolog.rc lays dialogs out in units of
// MS Shell Dlg's average character width, and Liberation is metrically
// compatible enough to fit the same boxes. The user can pick any family
// and size instead.
//
// Size 0 means follow the desktop. The desktop's own size is remembered
// on the first call: after a change, QApplication::font() reports what
// this set, so re-reading it would mean "follow the last choice".
QString StrMenuFontQt(void);
int NMenuFontSizeQt(void);
flag FMenuAntialiasQt(void);

void ApplyUiFontQt(void)
{
  static real rPointDesktop = 0.0;
  QString strFamily = StrMenuFontQt();
  int nSize = NMenuFontSizeQt();

  if (rPointDesktop <= 0.0) {
    rPointDesktop = QApplication::font().pointSizeF();
    // A font defined in pixels has no point size; ask what it resolves to.
    if (rPointDesktop <= 0.0)
      rPointDesktop = QFontInfo(QApplication::font()).pointSizeF();
  }
  QFont font(strFamily.isEmpty() ? QString("Liberation Sans") : strFamily);
  if (strFamily.isEmpty() &&
    QFontInfo(font).family() != QString("Liberation Sans"))
    font = QApplication::font();  // Not there: keep the desktop's font.
  font.setPointSizeF(nSize > 0 ? (real)nSize : rPointDesktop);
  // Why this exists at all: on a Windows desktop with ClearType off, the
  // menus and dialogs came out unantialiased while the text charts were
  // crisp, because the console font asks for antialiasing by name and
  // this one did not. PreferOutline is in there too so the request can be
  // honoured -- a bitmap face has nothing to antialias.
  font.setStyleStrategy(FMenuAntialiasQt() ?
    (QFont::StyleStrategy)(QFont::PreferOutline | QFont::PreferAntialias) :
    QFont::NoAntialias);
  QApplication::setFont(font);

  // Qt5 pushes a new application font into the widgets that already
  // exist. Qt6 does not, and Qt6 is what the Windows build ships, so
  // there the menus kept the old face until the next start. Measured
  // both ways with a small test program rather than reasoned about:
  // sending each widget the change is right on both, and on Qt5 it is
  // the event it has already had.
  QEvent evt(QEvent::ApplicationFontChange);
  CONST QWidgetList rgpw = QApplication::allWidgets();
  for (int i = 0; i < rgpw.size(); i++)
    QApplication::sendEvent(rgpw[i], &evt);
}


// Ask for the interface font again once the event loop is turning.
//
// A platform theme is allowed to apply its own settings after startup:
// qt5ct posts applySettings() as a queued call from its constructor, and
// that calls QApplication::setFont() with the desktop's font, throwing
// away what ApplyUiFontQt() set a moment earlier. Measured with
// QT_QPA_PLATFORMTHEME=qt5ct -- "Fira Code Retina" 16 at the end of
// BeginQt(), "Ubuntu" 10 after one turn of the loop -- which is why the
// menus came up in the desktop font and only took the chosen one when
// Display Settings was closed, that being the first re-apply to happen
// after the theme had had its turn. The palette and the style survive
// that pass; the font is the only casualty.
//
// A zero timer, not a queued call: Qt runs zero timers after the posted
// events of the same iteration, so this lands after the theme's metacall
// however early that was posted.
void ScheduleUiFontReapplyQt(void)
{
  QTimer::singleShot(0, gi.qapp, []() { ApplyUiFontQt(); });
}


static void LoadBundledFontsQt()
{
  CONST char *rgszFontFile[] = { "Astro.ttf", "EnigmaAstrology.ttf",
    "HamburgSymbols.ttf", "Astronomicon.ttf", "StarFontSans.ttf",
    "StarFontSerif.ttf", "HanksNakshatra.ttf",
    // The interface font, bundled so the dialogs transcribed from the
    // Windows resource fit their boxes wherever this runs.
    "LiberationSans-Regular.ttf", "LiberationSans-Bold.ttf",
    // And the monospaced faces text charts are drawn in. Liberation Mono
    // is the default and what Windows' text window gets through Courier
    // New; the rest are bundled so the console font list is the same on
    // every platform rather than whatever that machine has installed.
    // Each ships its licence beside it in font/.
    "LiberationMono-Regular.ttf", "LiberationMono-Bold.ttf",
    "JetBrainsMono-Regular.ttf", "IBMPlexMono-Regular.ttf",
    "SourceCodePro-Regular.ttf", "Hack-Regular.ttf",
    "FiraCode-Regular.ttf" };
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


// Keyboard shortcuts, from the ACCELERATORS table in astrolog.rc.
// Generated, not transcribed. Each names a menu bar item by label and
// binds to that QAction, so nothing is reimplemented and Qt draws the
// shortcut beside the item for free.
//
// The 96 macro F-keys are BuildMacroMenus()' job and are excluded. A
// fixed set is deliberately unbound -- commands this port does not
// implement (Setup, Window Settings, Print Setup, wallpaper) plus the
// four text-scrolling ones, which the scroll area handles. The suite's
// "hotkeys" group asserts that exact set.

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

// Windows applies the "-~WQ" AstroExpression (us.szExpMenu) to a command
// id before dispatching it, so an expression can veto a command or swap
// it for another. This port binds each action to its own handler, so
// ConnectMenuQt() looks the id up from the action's label and records the
// handler against it, letting a substituted command reach the right
// place. Actions whose label is not in the resource -- renamed macros,
// website links -- get a plain connection and do not take part.
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
  std::function<void()> fn, flag fRegister)
{
  int cmd = NCmdFromLabelQt(pa->text());

  // fRegister must be fFalse for context menu entries. This list lets an
  // AstroExpression naming a different command id find that command's
  // handler, and the menu bar registered one for every command at
  // startup. A context menu is rebuilt on every right click, so
  // registering its entries would grow this list without bound.
  if (cmd > 0 && fRegister)
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

// Show the accelerator column the way Windows shows it: astrolog.rc
// writes the string verbatim after a "\t", capitalising a letter to mean
// Shift ("V", "Alt+O"), where Qt would spell every modifier out.
//
// Qt paints a tab in a QAction's text ahead of the shortcut, so appending
// the resource's own string replaces the rendering without touching what
// the shortcut does. The label stays the item's identity, so lookups
// compare only up to the tab.
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



// Right-click context menus, the equivalent of Windows' DoPopup()
// dispatch from WM_RBUTTONDOWN. One table per chart type, picked on
// gi.nMode, generated from the menu resources in astrolog.rc.
//
// Each entry names the menu bar item to act through, by label;
// PmenuBuildContextQt() builds a proxy that forwards to it and mirrors
// its check mark, so there is one implementation and one piece of state
// per command. The indirection is needed because Windows gives the same
// command different labels in different context menus -- cmdChartModify
// is "Draw Houses Same Size" on a Western wheel and "Toggle North
// Indian" on an Indian one.

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
      [paSrc]() { paSrc->trigger(); }, fFalse);
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

  // Fusion draws a checkbox or radio outline by darkening the window
  // colour, which is invisible on a dark one -- and its own fallback
  // fires only when the window colour is pure black. So on a dark
  // palette draw these two primitives with a lightened copy of it, which
  // is where Fusion reads the outline and box fill from. A light palette
  // or a non-Fusion style takes the ordinary path.
  void drawPrimitive(PrimitiveElement pe, CONST QStyleOption *popt,
    QPainter *ppaint, CONST QWidget *pw = NULL) const override
  {
    if ((pe == PE_IndicatorCheckBox || pe == PE_IndicatorRadioButton) &&
      popt != NULL && FDarkFusionQt(popt->palette)) {
      CONST QStyleOptionButton *pbtn =
        qstyleoption_cast<CONST QStyleOptionButton *>(popt);
      QStyleOptionButton optBtn;
      QStyleOption optAny;
      QStyleOption *poptDraw;

      // Copied as the type it really is: Fusion's checkbox branch starts
      // with a qstyleoption_cast, and a sliced option would draw nothing.
      if (pbtn != NULL) {
        optBtn = *pbtn;
        poptDraw = &optBtn;
      } else {
        optAny = *popt;
        poptDraw = &optAny;
      }
      poptDraw->palette.setColor(QPalette::Window,
        poptDraw->palette.color(QPalette::Window).lighter(250));
      poptDraw->palette.setColor(QPalette::Base,
        poptDraw->palette.color(QPalette::Base).lighter(150));
      QProxyStyle::drawPrimitive(pe, poptDraw, ppaint, pw);
      return;
    }
    QProxyStyle::drawPrimitive(pe, popt, ppaint, pw);
  }

private:
  // Both halves matter. A dark palette under a style that paints its own
  // colours -- Breeze under KDE, say -- needs none of this and would be
  // harmed by it, and Fusion under a light palette is already correct.
  flag FDarkFusionQt(CONST QPalette &pal) const
  {
    QStyle *pbase = baseStyle();

    return pal.color(QPalette::Window).lightness() < 128 && pbase != NULL &&
      pbase->objectName().compare(QString("fusion"), Qt::CaseInsensitive)
      == 0;
  }
};


// Is the desktop in dark mode? Qt5 has no API for it, and its gtk3
// platform theme supplies no palette, so whether a machine looked right
// came down to which style plugin happened to be installed.
//
// Read the XDG desktop portal directly -- which is what Qt 6.5's
// QStyleHints::colorScheme() does -- and fall back to each desktop's own
// setting where none runs. Stays inside the Qt 5.12 API.

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
  int n = str.mid(i + 6).trimmed().left(1).toInt();
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


// Read one key out of an INI-style file. By hand rather than QSettings,
// which cannot read a section name containing a colon -- kdeglobals'
// [Colors:Window] -- and which caches parsed files by timestamp and size,
// so one rewritten within a second to a same-length value reads stale.

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

#ifdef QTTEST
// Where the two config-file probes below look for a home directory. The
// suite has to point them at a scratch tree, and cannot do it with the
// environment: QDir::homePath() reads HOME only on Unix, and on Windows
// resolves through SHGetKnownFolderPath(FOLDERID_Profile), which no
// qputenv can redirect. Setting HOME and then USERPROFILE both failed
// there, six assertions at a time, before this was understood.
static QString s_strHomeQt;
void SetHomeTestQt(CONST char *sz) { s_strHomeQt = QString(sz); }
#define HomeDirQt() (s_strHomeQt.isEmpty() ? QDir::homePath() : s_strHomeQt)
#else
#define HomeDirQt() QDir::homePath()
#endif


static int NSchemeFromKdeQt(void)
{
  QStringList ls = SzIniValueQt(HomeDirQt() + "/.config/kdeglobals",
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
  QString strPath = HomeDirQt() + "/.config/gtk-3.0/settings.ini";
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

// The interface settings -- the theme and the two fonts -- live in
// astrolog.as with everything else, as -WI, -WF and -WG. One settings
// file on every platform; -WN, -Wx and -Ww are GUI-only switches that
// have always lived there too.
//
// The accessors below are the whole interface to these values; nothing
// outside this file touches qi's fields directly.

// The size limits both fonts share, declared with the switch that sets
// them. The switch refuses anything outside them; the accessors clamp as
// well, so a value that reached the field some other way still reads as
// "no preference" rather than as an unreadable window.

// The console font: which face text charts are drawn in, and at what
// size. Empty family is the built-in default (Liberation Mono); size 0
// means follow -Xs, the Character Scale, exactly as before this existed.

QString StrConsoleFontQt(void)
{
  return QString(qi.szFontCon != NULL ? qi.szFontCon : "").trimmed();
}

int NConsoleFontSizeQt(void)
{
  return FBetween(qi.nFontConSize, nFontSizeMinQt, nFontSizeMaxQt) ?
    qi.nFontConSize : 0;
}

void SetConsoleFontQt(CONST char *szFamily, int nSize)
{
  FCloneSz(szFamily, &qi.szFontCon);
  qi.nFontConSize = nSize;
}


// Antialiasing, on by default because Qt's own default is on and that is
// what every other application on the desktop does. Off is a real
// preference rather than a curiosity: at small sizes a hinted bitmap-ish
// face is sharper without it, and on a remote X display it is faster.

flag FConsoleAntialiasQt(void)
{
  return qi.fFontConAA;
}

void SetConsoleAntialiasQt(flag f)
{
  qi.fFontConAA = f;
}


// The same two families as plain strings, for FOutputSettings(), which is
// shared core and so cannot be handed a QString. Never NULL: an absent
// preference is the empty name, which is what the switch writes and what
// the parser reads back as "no preference".

CONST char *SzConsoleFontQt(void)
{
  return qi.szFontCon != NULL ? qi.szFontCon : "";
}

CONST char *SzMenuFontQt(void)
{
  return qi.szFontMen != NULL ? qi.szFontMen : "";
}


// The faces a picker offers: the bundled ones that are actually there, in
// a deliberate order, then every other family the system has -- fixed
// pitch only for the console, where the chart's columns depend on it, and
// everything for the interface, where they don't. Qt6 made the
// QFontDatabase methods static and deprecated constructing one, so both
// spellings are here.

static QStringList RgstrFontQt(CONST char **rgszBundled, int cBundled,
  flag fFixedOnly)
{
  QStringList rgstr, rgstrAll;
  int i;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  rgstrAll = QFontDatabase::families();
#else
  QFontDatabase fdb;
  rgstrAll = fdb.families();
#endif
  for (i = 0; i < cBundled; i++)
    if (rgstrAll.contains(QString(rgszBundled[i])))
      rgstr << QString(rgszBundled[i]);
  for (i = 0; i < rgstrAll.size(); i++) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    flag fFixed = QFontDatabase::isFixedPitch(rgstrAll[i]);
#else
    flag fFixed = fdb.isFixedPitch(rgstrAll[i]);
#endif
    if ((fFixed || !fFixedOnly) && !rgstr.contains(rgstrAll[i]))
      rgstr << rgstrAll[i];
  }
  return rgstr;
}

QStringList RgstrConsoleFontQt(void)
{
  CONST char *rgszBundled[] = {"Liberation Mono", "JetBrains Mono",
    "IBM Plex Mono", "Source Code Pro", "Hack", "Fira Code"};

  return RgstrFontQt(rgszBundled,
    (int)(sizeof(rgszBundled)/sizeof(char *)), fTrue);
}


// The interface font, which the menus, the dialogs and every label are
// drawn in. Same three settings as the console one and stored the same
// way, in astrolog.as under -WG.
//
// Empty family means Liberation Sans, which is what the dialogs are
// measured against (see ApplyUiFontQt); size 0 means follow the desktop.

QString StrMenuFontQt(void)
{
  return QString(qi.szFontMen != NULL ? qi.szFontMen : "").trimmed();
}

int NMenuFontSizeQt(void)
{
  return FBetween(qi.nFontMenSize, nFontSizeMinQt, nFontSizeMaxQt) ?
    qi.nFontMenSize : 0;
}

void SetMenuFontQt(CONST char *szFamily, int nSize)
{
  FCloneSz(szFamily, &qi.szFontMen);
  qi.nFontMenSize = nSize;
}

flag FMenuAntialiasQt(void)
{
  return qi.fFontMenAA;
}

void SetMenuAntialiasQt(flag f)
{
  qi.fFontMenAA = f;
}

// Not fixed pitch only: this one is proportional text, and Liberation
// Sans leads the list because it is what the dialog boxes are sized for.

QStringList RgstrMenuFontQt(void)
{
  CONST char *rgszBundled[] = {"Liberation Sans"};

  return RgstrFontQt(rgszBundled,
    (int)(sizeof(rgszBundled)/sizeof(char *)), fFalse);
}


// The theme the user chose. Held as the small number -WI writes, and
// spoken about as a name everywhere else, because a name is what the menu
// and the tests deal in. Anything unrecognized is "auto", so a
// hand-edited astrolog.as cannot leave the window in no theme at all.
// nTheme*Qt are declared with the switch that parses them, far above.

QString StrThemePrefQt(void)
{
  return qi.nThemePref == nThemeLightQt ? QString("light") :
    qi.nThemePref == nThemeDarkQt ? QString("dark") : QString("auto");
}

void SetThemePrefQt(CONST char *sz)
{
  QString str = QString(sz).trimmed().toLower();

  qi.nThemePref = str == "light" ? nThemeLightQt :
    str == "dark" ? nThemeDarkQt : nThemeAutoQt;
}

int NThemePrefQt(void) { return qi.nThemePref; }

void SetThemePrefNQt(int n)
{
  qi.nThemePref = FBetween(n, nThemeAutoQt, nThemeDarkQt) ? n : nThemeAutoQt;
}


// Did the user actually ASK for light, rather than light merely being
// what detection settled on? Only an explicit answer justifies overriding
// a palette the platform theme produced.

static flag FThemeLightForcedQt(void)
{
  CONST char *szEnv = getenv("ASTROLOG_QT_THEME");

  if (szEnv != NULL) {
    QString str = QString(szEnv).trimmed().toLower();
    if (str == "light")
      return fTrue;
    if (str == "dark")
      return fFalse;
  }
  return StrThemePrefQt() == "light";
}


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

  // The environment variable stays above this on purpose. It is how the
  // suite and QT_TESTING.md check a change under both schemes, and a
  // developer forcing one for a single run should not have to know, or
  // disturb, what the user picked in the menu.
  {
    QString strPref = StrThemePrefQt();
    if (strPref == "dark")
      return nSchemeDark;
    if (strPref == "light")
      return nSchemeLight;
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

int NDarkPreferenceTestQt(void) { return NDarkPreferenceQt(); }

// How many command handlers are registered. Building context menus must
// not change it; see ConnectMenuQt().
int CCmdFnTestQt(void) { return s_rgcmdfnQt.size(); }

flag FThemeNameDarkTestQt(CONST char *sz)
  { return FThemeNameDarkQt(QString(sz)) ? fTrue : fFalse; }
int NSchemeFromKdeTestQt(void) { return NSchemeFromKdeQt(); }
int NSchemeFromGtkFileTestQt(void) { return NSchemeFromGtkFileQt(); }
#endif


// Follow the desktop into dark mode, if it's in it and Qt hasn't already
// worked that out for itself.

// The dark scheme, in one place. Gunmetal with a blue cast for the
// chrome, a darkened emerald for what is selected, and a soft cool gray
// for text -- white on near-black is what made the dialogs read as
// glaring. Measured contrast of coText on coWind is 7.7:1, which clears
// WCAG AAA for body text with room to spare, so this is softer without
// being dim.

#define coWindDarkQt  QColor(0x2B, 0x31, 0x38)   // gunmetal, faintly blue
#define coBtnDarkQt   QColor(0x33, 0x3A, 0x42)   // a shade up: raised
#define coBaseDarkQt  QColor(0x21, 0x26, 0x2B)   // a field sits below
#define coAltDarkQt   QColor(0x30, 0x37, 0x3E)
#define coTextDarkQt  QColor(0xC6, 0xCE, 0xD4)   // soft gray, not white
#define coDimDarkQt   QColor(0x70, 0x79, 0x81)   // disabled
#define coHighDarkQt  QColor(0x1E, 0x7A, 0x5C)   // darkened emerald
#define coHiTxDarkQt  QColor(0xEA, 0xF3, 0xEE)
#define coLinkDarkQt  QColor(0x5C, 0xB8, 0x99)   // the same green, lifted
#define coTipDarkQt   QColor(0x3A, 0x42, 0x4A)
#define coShadDarkQt  QColor(0x14, 0x18, 0x1B)

void ApplyColorSchemeQt(void)
{
  QColor coWind = coWindDarkQt, coBase = coBaseDarkQt,
    coText = coTextDarkQt, coHigh = coHighDarkQt, coDim = coDimDarkQt,
    coBtn = coBtnDarkQt;
  QStyle *pstyle;
  QPalette pal;

  if (NDarkPreferenceQt() != nSchemeDark) {
    // Going the other way, which only a runtime choice can ask for.
    //
    // The condition is deliberately "the user SAID light", not "we did not
    // detect dark". Detection returns nSchemeNone on a desktop it does not
    // recognise, and on a dark one that would be indistinguishable from a
    // light answer -- so keying off the absence of dark would force a
    // light palette onto a dark desktop whose detection merely failed,
    // which is the one outcome worse than not following it at all.
    if (FThemeLightForcedQt() &&
      QApplication::palette().color(QPalette::Window).lightness() < 128) {
      pstyle = QStyleFactory::create("Fusion");
      if (pstyle != NULL) {
        // standardPalette(), not a default-constructed QPalette: the
        // latter is whatever is currently set, which at this point is the
        // dark one being undone.
        pal = pstyle->standardPalette();
        QApplication::setStyle(new AstroStyleQt(pstyle));
        QApplication::setPalette(pal);
      }
    }
    return;
  }

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
  pal.setColor(QPalette::AlternateBase, coAltDarkQt);
  pal.setColor(QPalette::ToolTipBase, coTipDarkQt);
  pal.setColor(QPalette::ToolTipText, coText);
  pal.setColor(QPalette::Text, coText);
  pal.setColor(QPalette::Button, coBtn);
  pal.setColor(QPalette::ButtonText, coText);
  pal.setColor(QPalette::BrightText, QColor(0xFF, 0x6B, 0x6B));
  pal.setColor(QPalette::Link, coLinkDarkQt);
  pal.setColor(QPalette::LinkVisited, coLinkDarkQt.darker(120));
  pal.setColor(QPalette::Highlight, coHigh);
  pal.setColor(QPalette::HighlightedText, coHiTxDarkQt);
  // The shades Qt derives for frames, group box lines and sunken panels.
  // A default-constructed palette carries the LIGHT ones, so leaving
  // these alone drew a dark dialog with light-theme edges -- part of why
  // the dark mode never looked all of a piece.
  pal.setColor(QPalette::Light, coBtn.lighter(140));
  pal.setColor(QPalette::Midlight, coBtn.lighter(115));
  pal.setColor(QPalette::Mid, coWind.darker(115));
  pal.setColor(QPalette::Dark, coWind.darker(140));
  pal.setColor(QPalette::Shadow, coShadDarkQt);
  pal.setColor(QPalette::Disabled, QPalette::Text, coDim);
  pal.setColor(QPalette::Disabled, QPalette::ButtonText, coDim);
  pal.setColor(QPalette::Disabled, QPalette::WindowText, coDim);
  pal.setColor(QPalette::Disabled, QPalette::HighlightedText, coDim);
  pal.setColor(QPalette::Disabled, QPalette::Highlight, coWind.lighter(130));
  pal.setColor(QPalette::Disabled, QPalette::Base, coWind);
  pal.setColor(QPalette::Disabled, QPalette::Button, coWind);
#if QT_VERSION >= QT_VERSION_CHECK(5, 12, 0)
  pal.setColor(QPalette::PlaceholderText, coDim);
#endif
  QApplication::setPalette(pal);
}


// Ask Windows for a dark title bar: DWMWA_USE_IMMERSIVE_DARK_MODE, 20
// since Windows 10 20H1 and 19 in 1809, refused before that. Resolved by
// name through QLibrary so the build needs no new library and this file
// needs no windows.h, whose macros collide with the core's.
//
// A no-op elsewhere: on Linux the title bar belongs to the window
// manager, which takes no instruction from a client.

// A COLORREF, which is 0x00BBGGRR -- the reverse byte order of every
// other colour in this file, and the sort of thing that is wrong until
// something checks it. Compiled everywhere, used only on Windows, so the
// suite can check it on the machine the work is done on.

unsigned long LRgbrefFromCoQt(CONST QColor &co)
{
  return ((unsigned long)co.blue() << 16) |
    ((unsigned long)co.green() << 8) | (unsigned long)co.red();
}

// Windows' own numbering, from dwmapi.h. The first is all Windows 10 has;
// the other three arrived with Windows 11 and are simply refused before
// it, which leaves the black bar that dark mode alone produces.
#define dwmaDarkQt     20   // DWMWA_USE_IMMERSIVE_DARK_MODE (19 in 1809)
#define dwmaDarkOldQt  19
#define dwmaBorderQt   34   // DWMWA_BORDER_COLOR
#define dwmaCaptionQt  35   // DWMWA_CAPTION_COLOR
#define dwmaTextQt     36   // DWMWA_TEXT_COLOR
#define lColorDefaultQt 0xFFFFFFFF   // DWMWA_COLOR_DEFAULT: hand it back

void ApplyTitleBarThemeQt(QWidget *pw)
{
#ifdef Q_OS_WIN
  typedef long (*PFNDWMSETQT)(void *, unsigned long, CONST void *,
    unsigned long);
  static PFNDWMSETQT pfn = NULL;
  static flag fResolved = fFalse;
  unsigned long lCaption, lText, lBorder;
  int nOn;

  if (pw == NULL)
    return;
  if (!fResolved) {
    fResolved = fTrue;
    pfn = (PFNDWMSETQT)QLibrary::resolve(QString("dwmapi"),
      "DwmSetWindowAttribute");
  }
  if (pfn == NULL)
    return;
  nOn = (NDarkPreferenceQt() == nSchemeDark);
  // winId() is what creates the native window, so ask for it before
  // handing the handle over.
  void *hwnd = (void *)pw->winId();
  if (pfn(hwnd, dwmaDarkQt, &nOn, sizeof(nOn)) != 0)
    pfn(hwnd, dwmaDarkOldQt, &nOn, sizeof(nOn));

  // Dark mode on its own gives the caption plain black, which is darker
  // than anything else in the scheme. Painted here in the window's own
  // gunmetal instead, with the same soft grey the menus use for text, so
  // the bar reads as part of the dialog rather than a hole above it. In
  // light mode each is handed back to Windows rather than set to a light
  // colour, so the desktop's own accent still shows.
  lCaption = nOn ? LRgbrefFromCoQt(coWindDarkQt) : lColorDefaultQt;
  lText = nOn ? LRgbrefFromCoQt(coTextDarkQt) : lColorDefaultQt;
  lBorder = nOn ? LRgbrefFromCoQt(QColor(coWindDarkQt).darker(130)) :
    lColorDefaultQt;
  pfn(hwnd, dwmaCaptionQt, &lCaption, sizeof(lCaption));
  pfn(hwnd, dwmaTextQt, &lText, sizeof(lText));
  pfn(hwnd, dwmaBorderQt, &lBorder, sizeof(lBorder));
#else
  (void)pw;
#endif
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

  // The two interface font names, cloned by SetConsoleFontQt() and
  // SetMenuFontQt() through FCloneSz(), which allocates through
  // PAllocate() -- so an unfreed one is counted by the exit-time check in
  // astrolog.cpp and reported as an unfreed allocation on every quit.
  DeallocatePIf(qi.szFontCon);
  qi.szFontCon = NULL;
  DeallocatePIf(qi.szFontMen);
  qi.szFontMen = NULL;
}


/*
******************************************************************************
** Fetching a URL.
******************************************************************************
*/

// Fetch a URL to a file. Astrolog needs exactly one: a body's position
// from JPL Horizons, for a "j<n>" custom object. Qt Network replaces
// upstream's shell-out to wget, which had no timeout and no TLS.
//
// Synchronous by contract -- ComputeEphem() cannot draw without the
// answer -- but implemented with a nested event loop, so the window keeps
// painting and can offer a Cancel button.
//
// qi.pnam is one manager for the session, not one per fetch: Qt pools
// connections per manager, so the TLS handshake is paid once per host.

#define cmsGetUrlQt 30000       // Give up on a fetch after this long.
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
      // close() FLUSHES, and it returns void -- a write that only fails
      // when the buffer reaches the disk (no space, an I/O error, a full
      // quota) reports it here and nowhere else. Checking write() alone
      // called those downloads successful and left a truncated file
      // behind for the parser to find, which is worse than no file: the
      // ephemeris and atlas downloads both land in files this program
      // then reads back.
      file.close();
      if (strErr.isEmpty() && file.error() != QFileDevice::NoError)
        strErr = QString("Couldn't finish writing %1: %2")
          .arg(QString::fromUtf8(szFile)).arg(file.errorString());
      // A half-written file is not a partial success. Take it away, so
      // the next run downloads again instead of parsing a truncated one.
      if (!strErr.isEmpty())
        file.remove();
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


// The application icon, from astrlog1.ico's artwork. Qt has no resource
// script, so it is loaded from disk: the three PNG sizes this fork
// extracts for its desktop entry, falling back to the .ico. Looked for
// beside the executable and then in the working directory, as
// PixAstrologIconQt() and the bundled fonts are.
//
// Not static because the suite checks it: an icon that fails to load
// looks exactly like one that was never asked for.

QIcon IconAstrologQt()
{
  CONST int rgnSize[3] = {16, 32, 48};
  QStringList rgstrDir;
  QIcon icon;
  QString str;
  int i, j;

  rgstrDir << QCoreApplication::applicationDirPath() << QDir::currentPath();
  for (i = 0; i < rgstrDir.size(); i++) {
    for (j = 0; j < 3; j++) {
      str = rgstrDir[i] + "/icons/astrolog" + QString::number(rgnSize[j]) +
        ".png";
      if (QFile::exists(str))
        icon.addFile(str);
    }
    if (!icon.isNull())
      break;
    str = rgstrDir[i] + "/astrlog1.ico";
    if (QFile::exists(str)) {
      icon.addFile(str);
      if (!icon.isNull())
        break;
    }
  }
  return icon;
}


void BeginQt()
{
  static int s_argc = 1;
  static char *s_argv[] = { (char *)"astrolog", NULL };

#ifdef QTTEST
  s_pfnMsgPrevQt = qInstallMessageHandler(MessageFilterQt);
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  // Qt6 defaults to PassThrough, so a fractional screen scale is applied
  // as-is; Qt5 and the Windows build round. Round here so all three
  // agree, and because the chart is laid out in whole character cells
  // (SetTextMetricsQt) that a fractional factor puts between pixels.
  // Must precede the QApplication constructor; ignored afterwards.
  QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
    Qt::HighDpiScaleFactorRoundingPolicy::Round);
#endif
  // Ties a running instance to astrolog.desktop, which "make install"
  // writes. Without it a panel has to guess the association from WM_CLASS,
  // and measured on a private display that read "astrolog-qt",
  // "Astrolog-qt" -- the executable's name, which is not the desktop
  // file's. With this it reads "astrolog", "astrolog" and the match is
  // exact. Must precede the QApplication constructor.
  QGuiApplication::setDesktopFileName(QStringLiteral("astrolog"));
  gi.qapp = new QApplication(s_argc, s_argv);
  QApplication::setStyle(new AstroStyleQt);
  ApplyColorSchemeQt();
  LoadBundledFontsQt();
  ApplyUiFontQt();
  // Set on the application, not the window, so every dialog inherits it
  // the way Windows' window class does.
  QApplication::setWindowIcon(IconAstrologQt());
  gi.qwind = new QMainWindow();
  gi.qwind->installEventFilter(new NoQuitFilterQt(gi.qwind));
  gi.qwind->setWindowTitle(szAppName);
  ApplyTitleBarThemeQt(gi.qwind);
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
  ScheduleUiFontReapplyQt();
}


// Destroy the window and then the application, before the process exits.
// Qt wants its widgets gone before the QApplication and the QApplication
// gone before the thread that made it; exit() ends the thread with both
// alive, which Qt6 reports as "QThreadStorage: entry destroyed before end
// of thread".
//
// Both exit paths come through here: the real binary from EndQt(), and
// the test binary, which exits from inside InteractQt().

void ShutdownQt()
{
  if (gi.qwind != NULL) {
    delete gi.qwind;      // Takes the canvas and the scroll area with it.
    gi.qwind = NULL;
    gi.qcanvas = NULL;
    qi.pscroll = NULL;
  }
  if (gi.qapp != NULL) {
    delete gi.qapp;
    gi.qapp = NULL;
  }
}


// Hand control to Qt once the window is up, the counterpart of X11's
// InteractX(). Blocks until the main window closes.

void InteractQt()
{
#ifdef QTTEST
  int nT;
#endif

  qi.fReady = true;
  RedrawQt();
#ifdef QTTEST
  // The test binary comes in through the same startup path as the real
  // one, so the suite runs against a fully built window: menus, hotkeys,
  // and a drawn chart. Run it here instead of handing over to the user.
  nT = NRunQtTestsQt();
  ShutdownQt();
  exit(nT);
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
  ShutdownQt();
}

#endif // QT

/* qtdriver.cpp */
