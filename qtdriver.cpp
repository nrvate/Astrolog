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
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QLabel>
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
#include <QtNetwork/QSslError>
#include <QtNetwork/QSslSocket>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QtWebSockets/QWebSocket>
#include <QtCore/QRandomGenerator>
// The Ephemeris Server's wire protocol, shared with the server end.
// Compiled into the client on purpose (EPHEMERIS_CLIENT_PLAN.md lesson 7):
// a protocol mismatch is a compile error, not a runtime mystery. Resolved
// by the "-I ephsrv" the Qt makefiles carry.
#include "ephproto.h"
#include <QtWidgets/QProgressDialog>
#include <QtCore/QMap>
#include <QtCore/QBitArray>
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
#include <QtGui/QScreen>

#include "astrolog.h"
#include "qtdriver.h"
#include "ephreq.h"
// The Swiss Ephemeris constants the Ephemeris Server backend speaks in
// (SEFLG_*, SE_SIDM_*) and swe_deltat(), which the local path also calls
// to make the instant it sends -- the vendored library, the one every
// build links.
#include "swephexp.h"
// Appendix B in code, and the half of it this end needs: the Swiss call
// FSwissPlanetSpec() decided on, as the version 4 object and profile that
// make the server make the same call (ephswiss.h ObjectFromSwiss,
// ProfileFromSwiss). Compiled into both ends, so the two cannot drift.
#include "ephswiss.h"

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

// One row of the text aspect list, as delivered by the pfnAspectRow()
// sink while the chart prints: the displayed object pair, the aspect, and
// the orb and power the row's numbers show. This is what the sortable
// aspect list view sorts by -- real values from the chart, not text
// parsed back out of the console.
typedef struct {
  int o1, ahi, o2;
  real rOrb, rPow;
} ASPROWQT;

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

  // What Generate Animation holds still while it runs (grfHold* in
  // qtdriver.h). Its dialog is open over a chart whose date it read, and
  // its progress box processes events between frames while gs.ft and
  // gi.fFile still describe the frame being written -- so an animation
  // tick, or a paint after a resize, reached DrawBlock() with a file's
  // state and a screen's buffer, and crashed.
  int grfHold = 0;

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

  // What the text console drew, retained one character per cell.
  // TextCharQt() records into this as the chart renders, so that the
  // mouse can hit-test words in it afterwards -- the render itself is
  // fire and forget, and once done the text exists only as pixels in
  // gi.qim. Row major; a zero cell was never drawn to. The two word
  // highlights read it, one pinned by a click and one soft under the
  // cursor, each holding the word it lights and where that word sits.
  wchar *rgwchGrid = NULL;
  int cchGrid = 0, crowGrid = 0;
  // And the palette index each cell was drawn in, stamped alongside the
  // character, so the aspect list view can re-render the console from the
  // grid alone. (TextColorQt() keeps the current one in kiText.)
  byte *rgkiGrid = NULL;
  int kiText = kLtGrayA;
  QString strTextHi;
  QVector<QRect> rgrcTextHi;
  QString strTextHover;
  QVector<QRect> rgrcTextHover;

  // The drag selection: where the left button went down (a cell), the
  // cell the drag has reached, the row spans and text that covers, and
  // the button state between them. Rows and cells are VIEW coordinates
  // -- what the console shows -- so any re-render clears it.
  flag fSelPress = fFalse;      // Left button down over the console.
  flag fSelShift = fFalse;      // What that press was holding, for the
                                // plain click the release may yet become.
  int xSel1 = 0, ySel1 = 0;     // The anchor cell.
  flag fSel = fFalse;           // A drag grew out of that press.
  int xSel2 = 0, ySel2 = 0;     // Where the drag has reached.
  QVector<QRect> rgrcTextSel;   // Its row spans, canvas pixels.
  QString strTextSel;           // The text it covers.

  // The aspect list view: the console's own cells re-rendered with a
  // header row above the data, whose labels the mouse clicks to sort.
  // The pristine grid above is what the chart printed and never changes
  // after a render; this view is rebuilt from it (rows in sort order,
  // renumbered, header re-labelled) on every sort click. Empty view
  // pointers mean "show the pristine grid as it printed", which is every
  // text chart that isn't an aspect list.
  wchar *rgwchView = NULL;
  byte *rgkiView = NULL;
  int cchView = 0, crowView = 0;
  flag fHdr = fFalse;           // Is the header row part of the view?
  int yHdr = -1;                // Its row in the view.
  QRect rgrcHdr[5];             // The five header labels' hit zones.
  int rnSortCol[4];             // Active sort keys, primary first.
  flag rgfSortDesc[4];
  int cSortKeys = 0;
  flag fSortObjAlpha = fFalse;  // -WA: object columns sort by name.
  QVector<ASPROWQT> rgasprow;   // Rows the sink delivered, print order.

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

  // Windows' wi.yScroll, as a 0..nScrollDiv fraction. It is a scrollbar
  // position there, and shared core reads it in exactly ONE place that
  // is not panning: xcharts2.cpp:1404, where the transit graph uses it to
  // choose WHICH aspect rows to draw when there are more than fit. That
  // one had "#else cRow = 0", so this build always drew the first
  // screenful and the rest were unreachable. The scroll area cannot help:
  // the extra rows are never drawn at all, so there is nothing for it to
  // scroll to.
  int nScrollChart = 0;

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
    // Plain mouse moves arrive only with tracking on, and the text
    // console's hover highlight is a plain mouse move. Graphics mode
    // ignores buttonless moves either way.
    setMouseTracking(true);
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
    //
    // Graphics only. A text chart's canvas is the size of the TEXT, which
    // can be larger than the window on purpose so the scroll area can
    // reach the rest of it; chasing that here would write the text's width
    // into gs.xWin and make "Save Program Settings" record it as the chart
    // size. Nothing is lost by not chasing: a text chart's layout does not
    // depend on gs.xWin at all -- its width comes from what it prints and
    // from us.fClip80/us.nScreenWidth -- so there is nothing to re-lay-out
    // when the window changes.
    if (qi.fReady && qi.fWindowChart && us.fGraphics &&
      !(qi.grfHold & grfHoldRedraw) &&
      width() >= 1 && height() >= 1 &&
      (gi.qim == NULL || gi.qim->width() != width() ||
      gi.qim->height() != height())) {
      gs.xWin = width();
      gs.yWin = height();
      RedrawQt();
    }
    QPainter p(this);
    // The letterbox when the canvas is bigger than the image -- a text
    // chart in a window larger than it -- is the chart's own background
    // colour, not the viewport's gray. In graphics the canvas is the
    // image exactly, so the fill never shows.
    KV kvBack = KvFromKi(gi.kiOff);
    p.fillRect(rect(), QColor(RgbR(kvBack), RgbG(kvBack), RgbB(kvBack)));
    if (gi.qim != NULL)
      p.drawImage(0, 0, *gi.qim);
    // The text console's word highlights, over the ink: two washes of the
    // ink's own colour, the hover lighter than the pinned word, so
    // hovering a pinned word just brightens it. Neutral on purpose -- a
    // tinted wash fights the many colours a listing already prints --
    // and taken from gi.kiOn rather than hard white so that Reverse
    // Background (white paper, black ink) washes dark instead. Drawn here
    // rather than baked into gi.qim, so that changing either costs a
    // repaint and not a re-render, and so the pixel-exact nets
    // (chart-render, the export matrices) reading gi.qim see none of it.
    if (!us.fGraphics) {
      KV kv = KvFromKi(gi.kiOn);
      QColor col(RgbR(kv), RgbG(kv), RgbB(kv));
      int i;
      // The drag selection wash, under the word layers: whole cells
      // rather than glyph-hugging bands, and stronger than the pin, so
      // the three read as one under the other.
      for (i = 0; i < qi.rgrcTextSel.size(); i++) {
        col.setAlpha(128);
        p.fillRect(qi.rgrcTextSel[i], col);
      }
      for (i = 0; i < qi.rgrcTextHover.size(); i++) {
        col.setAlpha(48);
        p.fillRect(qi.rgrcTextHover[i], col);
      }
      for (i = 0; i < qi.rgrcTextHi.size(); i++) {
        col.setAlpha(96);
        p.fillRect(qi.rgrcTextHi[i], col);
      }
    }
  }

  void resizeEvent(QResizeEvent *pevent) override
  {
    QWidget::resizeEvent(pevent);
    update();
  }

  void leaveEvent(QEvent *pevent) override
  {
    // Leaving the canvas drops the hover half; a pinned word stays.
    if (!qi.strTextHover.isEmpty()) {
      ClearTextHoverQt();
      update();
    }
    QWidget::leaveEvent(pevent);
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

    // The text console's left press is its own thing -- Scribble() is
    // graphics only: the press starts a drag selection, which the moves
    // grow and the release resolves; a release that never dragged is
    // the plain click that pins a word or sorts a header. The work
    // happens in TextPressAtPtQt() and friends, qtdriver.cpp.
    if (!us.fGraphics) {
      TextPressAtPtQt(pevent->pos().x(), pevent->pos().y(),
        (pevent->modifiers() & Qt::ShiftModifier) != 0);
      return;
    }

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
    // A buttonless move is the text console's hover. Nothing in graphics
    // mode does anything with one, so it returns there as before.
    if (!(pevent->buttons() & (Qt::LeftButton | Qt::RightButton))) {
      TextHoverAtPtQt(pevent->pos().x(), pevent->pos().y());
      return;
    }

    if (pevent->buttons() & Qt::RightButton) {
      if (FRotatableQt())
        RotateByDrag(pevent->pos());
      return;
    }

    // The text console's left drag is its selection growing, not
    // Scribble(), which is graphics only and returns at once there.
    if ((pevent->buttons() & Qt::LeftButton) && !us.fGraphics) {
      TextDragAtPtQt(pevent->pos().x(), pevent->pos().y());
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
    // The text console's left release resolves the press: a drag keeps
    // its selection, a plain click does the click work.
    else if (pevent->button() == Qt::LeftButton && !us.fGraphics)
      TextReleaseAtPtQt(pevent->pos().x(), pevent->pos().y());
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
flag FWindowChartQt() { return qi.fWindowChart; }
void SetWindowChartQt(flag f)
{
  qi.fWindowChart = f;
  ApplySizeModeQt();
}

flag FChartWindowQt() { return qi.fChartWindow; }

void SetChartWindowQt(flag f)
{
  qi.fChartWindow = f;
  if (f)
    ResizeWindowToChartQt();
}

#ifdef QTTEST
// The chart viewport's size, which is what "the chart is 640 by 480"
// actually means to a user: the window is bigger than this by the menu
// bar and the frame.
QSize SizeChartViewportTestQt()
{
  return qi.pscroll != NULL ? qi.pscroll->viewport()->size() : QSize();
}
#endif
void SetNoPopupQt(flag f) { qi.fNoPopup = f; }
flag FBmpWindowQt() { return qi.fBmpWindow; }
void SetBmpWindowQt(flag f) { qi.fBmpWindow = f; }



// Put the canvas into whichever of the two sizing modes is currently set.
// With "window resizes chart" on, the canvas tracks the scroll area's
// viewport and paintEvent() picks the chart size up from it. With it off
// the canvas is sized to the chart instead, and the scroll area grows
// scrollbars whenever that doesn't fit in the window.
void ApplySizeModeQt();

// A text chart anchors its image at the top left corner -- where a
// listing starts -- and the canvas covers the viewport around it, so the
// window's leftover space is the canvas's own background fill rather
// than the scroll area's gray with the text floating centered in it.
// The canvas's minimum is the image, which is what makes the scroll area
// resizable here safe: a listing longer or wider than the window keeps
// its scrollbars, because the canvas can never be shrunk below it.
void ApplyTextSizeModeQt()
{
  int dx = gi.qim != NULL ? gi.qim->width() : 0;
  int dy = gi.qim != NULL ? gi.qim->height() : 0;

  qi.pscroll->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  qi.pscroll->setWidgetResizable(fTrue);
  gi.qcanvas->setMinimumSize(Max(BITMAPX1, dx), Max(BITMAPY1, dy));
}

void ApplySizeModeQt()
{
  if (qi.pscroll == NULL || gi.qcanvas == NULL)
    return;
  if (!us.fGraphics) {
    ApplyTextSizeModeQt();
    return;
  }
  // Graphics mode's own arrangement: the chart centered when smaller
  // than the window, and the canvas free to shrink back to the chart --
  // a text render raises its minimum to the image, which has to come
  // off again here or a window-resized chart could never get smaller
  // than the last listing was.
  qi.pscroll->setAlignment(Qt::AlignCenter);
  gi.qcanvas->setMinimumSize(BITMAPX1, BITMAPY1);
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
  // The chart-content fraction moves too, not just the viewport. A chart
  // that pages its own rows draws none of what is past the bottom, so
  // the scroll area has nothing to scroll to and these four menu items
  // could never reach it -- see qi.nScrollChart.
  //
  // A page is nScrollPage, which is what Windows' WM_VSCROLL moves
  // wi.yScroll by (wdriver.cpp, SB_PAGEUP/SB_PAGEDOWN). This used to be
  // "nScrollDiv / 8", which is 3 against Windows' 6, so the transit graph
  // paged half as far here -- and xcharts2.cpp reads this fraction exactly
  // as it reads wi.yScroll there. Nothing documented the 8.
  switch (nDir) {
  case -1: qi.nScrollChart -= nScrollPage; break;
  case  1: qi.nScrollChart += nScrollPage; break;
  case  0: qi.nScrollChart = 0; break;
  case  2: qi.nScrollChart = nScrollDiv; break;
  }
  qi.nScrollChart = Min(Max(qi.nScrollChart, 0), nScrollDiv);

  if (qi.pscroll == NULL) {
    RedrawQt();
    return;
  }
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
  RedrawQt();
}


// The chart-content scroll position, 0 to nScrollDiv. Read by shared core
// (xcharts2.cpp) the way Windows reads wi.yScroll.

int NScrollChartQt(void)
{
  return qi.nScrollChart;
}


// Warnings and errors reach the user in a message box, as they do on
// Windows, rather than going to stderr. That matters more than it looks:
// PrintError()'s non-Windows path ends in Terminate(), so a chart that
// referenced a missing file -- a macro pointing at a path that doesn't
// exist here, say -- took the whole program down rather than complaining
// about it. Windows shows a box and carries on, and so does this.
// What suppressed popups would have shown since the suite last cleared it,
// joined with " | ", so a test can assert on what a refusal SAYS -- a
// refused switch is followed by "Failed to parse command line", so the
// last message alone is never the one that matters.
static char s_szPopupSuppressedQt[1024];
CONST char *SzPopupSuppressedTestQt() { return s_szPopupSuppressedQt; }
void ClearPopupSuppressedTestQt() { s_szPopupSuppressedQt[0] = chNull; }

void PrintWarningQt(CONST char *sz, flag fError)
{
  if (FNoPopupQt()) {
    size_t cch = strlen(s_szPopupSuppressedQt);
    snprintf(s_szPopupSuppressedQt + cch, sizeof(s_szPopupSuppressedQt) - cch,
      "%s%s", cch ? " | " : "", sz);
    return;
  }
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


// The third kind. PrintWarning() and PrintError() have routed here since
// the port began; PrintNotice() did not, so it fell through to the plain
// non-Windows path and wrote to STDERR -- invisible in a window. Windows
// shows an information box (MessageBox with MB_ICONINFORMATION,
// general.cpp:1470), and two things reach it here: the "-YYT" switch, and
// an AstroExpression asking to show a value (express.cpp:2753).
//
// Same shape as PrintWarningQt() above, deliberately: the popup
// suppression, the no-QApplication-yet fallback and the title format are
// all the ones that function already settled.

void PrintNoticeQt(CONST char *sz)
{
  if (FNoPopupQt())
    return;
  if (QApplication::instance() == NULL) {
    fprintf(stderr, "%s\n", sz);
    return;
  }
  QMessageBox::information(gi.qwind,
    QString("%1 Notice").arg(szAppName), QString::fromLatin1(sz));
}


// The "-YB" switch, which exists to be put in a macro so a chart can ring
// the bell. Windows calls MessageBeep() (switch.cpp, NSwYB); every other
// build writes chBell to stdout, which in a GUI goes to the terminal it
// was launched from, or nowhere at all from a desktop launcher. Qt has
// the same system sound MessageBeep() asks for.

void BeepQt(void)
{
  if (QApplication::instance() == NULL) {
    putchar(chBell);
    return;
  }
  QApplication::beep();
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
  qi.kiText = ki;
}

// The sink behind pfnAspectRow() while the text aspect list prints: each
// row's structured keys, in print order, for the sortable view.
static void AspectRowSinkQt(int o1, int ahi, int o2, real rOrb, real rPow)
{
  ASPROWQT ar;

  ar.o1 = o1; ar.ahi = ahi; ar.o2 = o2;
  ar.rOrb = rOrb; ar.rPow = rPow;
  qi.rgasprow.append(ar);
}

// The retained text grid the character draws below also record, one cell
// per character, so the mouse can hit-test words in the console after the
// render. Growth is on demand and never shrinks: re-rendering into the
// same allocation just zeroes it, and a chart that renders smaller than an
// earlier one costs nothing.

// Record one character, growing the grid around the cell if needed. Cell
// positions can't reach the caps here -- the canvas itself is bounded by
// BITMAPX x BITMAPY and a cell is qi.xChar x qi.yChar pixels -- but they
// are checked rather than trusted, because a cell past the caps would
// write outside the grid.
static void RecordTextGridQt(int xCell, int yCell, int wch)
{
  int cchNew, crowNew, y;
  wchar *rgwchNew;
  byte *rgkiNew;

  if (xCell < 0 || yCell < 0 || xCell >= BITMAPX || yCell >= BITMAPY)
    return;
  if (xCell >= qi.cchGrid || yCell >= qi.crowGrid) {
    cchNew = Min(Max(Max(qi.cchGrid * 2, xCell + 1), 128), BITMAPX);
    crowNew = Min(Max(Max(qi.crowGrid * 2, yCell + 1), 64), BITMAPY);
    rgwchNew = new wchar[cchNew * crowNew];
    memset(rgwchNew, 0, (size_t)cchNew * crowNew * sizeof(wchar));
    rgkiNew = new byte[cchNew * crowNew];
    memset(rgkiNew, 0, (size_t)cchNew * crowNew * sizeof(byte));
    for (y = 0; y < qi.crowGrid; y++) {
      memcpy(rgwchNew + y * cchNew, qi.rgwchGrid + y * qi.cchGrid,
        qi.cchGrid * sizeof(wchar));
      memcpy(rgkiNew + y * cchNew, qi.rgkiGrid + y * qi.cchGrid,
        qi.cchGrid * sizeof(byte));
    }
    delete[] qi.rgwchGrid;
    delete[] qi.rgkiGrid;
    qi.rgwchGrid = rgwchNew;
    qi.rgkiGrid = rgkiNew;
    qi.cchGrid = cchNew;
    qi.crowGrid = crowNew;
  }
  qi.rgwchGrid[yCell * qi.cchGrid + xCell] = (wchar)wch;
  qi.rgkiGrid[yCell * qi.cchGrid + xCell] = (byte)qi.kiText;
}

// Empty the grid ahead of a text render. The allocation stays: charts
// redraw often, and the biggest chart seen is a fine thing to keep room
// for.
static void TextGridResetQt(void)
{
  if (qi.rgwchGrid != NULL) {
    memset(qi.rgwchGrid, 0,
      (size_t)qi.cchGrid * qi.crowGrid * sizeof(wchar));
    memset(qi.rgkiGrid, 0,
      (size_t)qi.cchGrid * qi.crowGrid * sizeof(byte));
  }
  // A fresh listing sorts the way the chart printed it until clicked.
  qi.cSortKeys = 0;
}

// And give them back, when the window that shows them is going away.
static void TextGridFreeQt(void)
{
  delete[] qi.rgwchGrid;
  qi.rgwchGrid = NULL;
  qi.rgkiGrid = NULL;
  qi.cchGrid = qi.crowGrid = 0;
  delete[] qi.rgwchView;
  qi.rgwchView = NULL;
  qi.rgkiView = NULL;
  qi.cchView = qi.crowView = 0;
}

// One character of the retained grid the VIEW shows -- the pristine
// recording when no aspect-list view is up, the headered reordered view
// when one is -- or 0 for a cell nothing was drawn in, including any cell
// outside it entirely. Everything the mouse does reads this, so the
// hit-tests always describe what is on screen.
int WchTextGridQt(int xCell, int yCell)
{
  if (qi.rgwchView != NULL) {
    if (xCell < 0 || yCell < 0 ||
      xCell >= qi.cchView || yCell >= qi.crowView)
      return 0;
    return qi.rgwchView[yCell * qi.cchView + xCell];
  }
  if (qi.rgwchGrid == NULL || xCell < 0 || yCell < 0 ||
    xCell >= qi.cchGrid || yCell >= qi.crowGrid)
    return 0;
  return qi.rgwchGrid[yCell * qi.cchGrid + xCell];
}

// The dimensions WchTextGridQt() actually indexes: the view's when one
// is built, the pristine grid's when it is not -- which is every chart
// but the aspect list. The selection's cell coordinates clamp into
// these, not into the view's alone.
static int CchTextAtQt(void)
{
  return qi.rgwchView != NULL ? qi.cchView : qi.cchGrid;
}

static int CrowTextAtQt(void)
{
  return qi.rgwchView != NULL ? qi.crowView : qi.crowGrid;
}

// A word character in the retained grid: a letter or a digit -- ASCII, or
// anything above 127, so accented names and the box-drawing characters a
// colored text wheel is drawn with stay word characters too. Everything
// else is punctuation the listings glue to their text -- "(New Moon)",
// "SSx:" in the aspect summary, "- orb:", quotes and dashes -- and it
// separates words instead of joining them. That is what makes a summary
// line's "SSx:" hover as "SSx" and light the SSx rows above it, and what
// keeps a stray ';', '-', '"' or '.' from riding along with the word
// next to it.
static flag FIsWordChQt(int wch)
{
  if ((wch >= '0' && wch <= '9') || (wch >= 'A' && wch <= 'Z') ||
    (wch >= 'a' && wch <= 'z'))
    return fTrue;
  return wch >= 0x80;
}

// Is this cell a digit? Part of the value-globbing rules below.
static flag FIsDigitChQt(int wch)
{
  return wch >= '0' && wch <= '9';
}

// Does the punctuation cell chGlue sit INSIDE a word? It does when the
// cells on both sides of it are word characters -- the point in "10.28",
// the colons in "22:07:28", the slash in "1/10th" -- or when it is one of
// the arc-minute and arc-second marks a degree value ends in, "08'" and
// "37'50\"", which hang off a digit with nothing owed after them.
static flag FGluesQt(int chPrev, int chGlue, int chNext)
{

  // The glue cell has to be punctuation itself. This check is what keeps
  // a SPACE from reading as glue between two words: without it, "North
  // Node" is one word and neither "North" nor "Node" can match alone.
  if (chGlue <= ' ' || chGlue >= 0x80 || FIsWordChQt(chGlue))
    return fFalse;
  if (FIsWordChQt(chPrev) && FIsWordChQt(chNext))
    return fTrue;
  return (chGlue == '\'' || chGlue == '"' || chGlue == '%') &&
    FIsDigitChQt(chPrev);
}

// Does the cell ch START a word? A word character does; a sign does when
// a digit follows, so "+7:26'" and "-0.070" are whole values, sign and
// all, while the dash in "- orb:" separates.
static flag FStartsWordQt(int ch, int chNext)
{
  return FIsWordChQt(ch) ||
    ((ch == '+' || ch == '-') && FIsDigitChQt(chNext));
}

// The span of the word containing a cell: it starts at a word character
// or a sign before a digit, and extends over word characters and over
// punctuation that glues. fFalse when the cell holds no word.
static flag FWordSpanAtCellQt(int xCell, int yCell, int *px1, int *px2)
{
  int x;

  if (!FStartsWordQt(WchTextGridQt(xCell, yCell),
    WchTextGridQt(xCell + 1, yCell)))
    return fFalse;
  *px1 = *px2 = xCell;
  // Right: word characters, and punctuation that glues to what the run
  // already holds.
  for (x = *px2 + 1; ; x++) {
    int ch = WchTextGridQt(x, yCell);
    if (FIsWordChQt(ch)) {
      *px2 = x;
      continue;
    }
    if (FGluesQt(WchTextGridQt(*px2, yCell), ch,
      WchTextGridQt(x + 1, yCell))) {
      *px2 = x;
      continue;
    }
    break;
  }
  // Left: word characters, a sign hanging before a digit, and punctuation
  // gluing the run's first cell to the word before it.
  for (x = *px1 - 1; x >= 0; x--) {
    int ch = WchTextGridQt(x, yCell);
    int chAfter = WchTextGridQt(x + 1, yCell);
    if (FIsWordChQt(ch)) {
      *px1 = x;
      continue;
    }
    if (FStartsWordQt(ch, chAfter)) {
      *px1 = x;
      continue;
    }
    if (FGluesQt(WchTextGridQt(x - 1, yCell), ch, chAfter)) {
      *px1 = x;
      continue;
    }
    break;
  }
  return fTrue;
}

// The grid's characters from one row, first column through last inclusive.
static QString StrWordSpanQt(int yCell, int x1, int x2)
{
  QString str;
  int x;

  for (x = x1; x <= x2; x++)
    str += QChar(WchTextGridQt(x, yCell));
  return str;
}

// The word under a canvas point, as one string. The pixel-to-cell math is
// the draw below read backwards -- a cell's glyph starts xCell * qi.xChar
// + 4 pixels in, and its row spans yCell * qi.yChar to (yCell + 1) *
// qi.yChar -- so a point inside a drawn glyph finds the cell it belongs
// to. Null string between words and past the end of what was drawn.
QString StrTextWordAtPtQt(int xPix, int yPix)
{
  int xCell = (xPix - 4) / qi.xChar, yCell = yPix / qi.yChar;
  int x1, x2;

  if (!FWordSpanAtCellQt(xCell, yCell, &x1, &x2))
    return QString();
  return StrWordSpanQt(yCell, x1, x2);
}

// Is this text one of the display names objects are known by? szObjDisp[]
// is the table the listings actually print -- PrintAspect()'s "%7.7s"
// reads it -- so a name customised by -Yo is matched as the custom text,
// and a long name truncated by the field (PrintAspect caps at 7
// characters, "North Node" down to "North N") is simply not here, which
// leaves such an instance on the word fallback below.
static flag FObjNamePhraseQt(CONST QString &str)
{
  int i;

  if (str.isEmpty())
    return fFalse;
  for (i = 0; i < objMax; i++)
    if (str == szObjDisp[i])
      return fTrue;
  return fFalse;
}

// The word or NAME under a canvas point. Names that contain a space --
// "North Node", "East Point", a customised object name -- are one thing
// on screen, so they are one thing here: the hovered word extends across
// a gap of exactly one space cell, leftward and rightward, and the longest
// extension that equals a display name wins. The wider gaps that separate
// columns never bridge, and a join has to BE a name, so "Sun (Gem) Con"
// (single spaces throughout the aspect list) does not falsely fuse --
// only real names can. Null string when the point is on no word at all.
QString StrTextPhraseAtPtQt(int xPix, int yPix)
{
  // Three words each way around the hovered one, which is slot 3; every
  // object name fits in that window with room to spare.
  int xCell = (xPix - 4) / qi.xChar, yCell = yPix / qi.yChar;
  int rgx1[7], rgx2[7], i, j, x;
  QString rgsz[7], strT, strBest;
  const int ih = 3, cSlot = 7;

  if (!FWordSpanAtCellQt(xCell, yCell, &rgx1[ih], &rgx2[ih]))
    return strBest;
  rgsz[ih] = StrWordSpanQt(yCell, rgx1[ih], rgx2[ih]);
  for (i = ih - 1; i >= 0; i--) {
    if (WchTextGridQt(rgx1[i + 1] - 1, yCell) != ' ')
      break;
    if (!FWordSpanAtCellQt(rgx1[i + 1] - 2, yCell, &rgx1[i], &rgx2[i]))
      break;
    rgsz[i] = StrWordSpanQt(yCell, rgx1[i], rgx2[i]);
  }
  for (i = ih + 1; i < cSlot; i++) {
    if (WchTextGridQt(rgx2[i - 1] + 1, yCell) != ' ')
      break;
    if (!FWordSpanAtCellQt(rgx2[i - 1] + 2, yCell, &rgx1[i], &rgx2[i]))
      break;
    rgsz[i] = StrWordSpanQt(yCell, rgx1[i], rgx2[i]);
  }

  // Which runs of those words containing the hovered one name an object?
  // The longest such run is the unit: "North" alone names nothing while
  // "North Node" does, and hovering the "Node" half reaches the same
  // phrase leftward. Without a name, the hovered word is the unit, as it
  // always was.
  for (i = 0; i <= ih; i++)
    for (j = ih; j < cSlot; j++) {
      if (rgsz[i].isEmpty() || rgsz[j].isEmpty())
        continue;
      strT = rgsz[i];
      for (x = i + 1; x <= j; x++) {
        strT += QChar(' ');
        strT += rgsz[x];
      }
      if (!FObjNamePhraseQt(strT))
        continue;
      if (strBest.isEmpty() || strT.size() > strBest.size())
        strBest = strT;
    }
  return strBest.isEmpty() ? rgsz[ih] : strBest;
}

// Every place the word appears in the retained grid, as canvas pixel
// rectangles, one per occurrence. A match is a whole word: the cells on
// both sides of it in its row must not be more word, so "Jup" does not
// light part of "Jupiter" -- and glued punctuation, a bracket, a colon
// or a dash, is not more word, so "Moon" lights inside "(New Moon)"
// without the bracket. A phrase with inner spaces -- "North Node" --
// matches the same way, each inner space being one space cell exactly.
// The rectangle hugs the glyphs rather than the cell: the draw puts the
// BASELINE at the bottom of the cell band, so a full-cell rectangle puts
// the font's leading and the cap-height slack above the ink and none
// below, and the wash reads as a band floating over the top half of the
// text. Anchoring the rect at baseline - ascent and giving it
// ascent + descent centres the wash on the ink the way a selection does.
QVector<QRect> RgrcTextWordQt(CONST QString &strWord)
{
  QVector<QRect> rgrc;
  QFontMetrics fm(qi.fontText);
  int cwch = strWord.size(), cch, crow, x, y, i;

  // Scan the grid the view shows, whatever it is: the dimensions below
  // are the view's when an aspect-list view is up (one row taller than
  // the recording), else the recording's own.
  if (qi.rgwchView != NULL) {
    cch = qi.cchView;
    crow = qi.crowView;
  } else {
    cch = qi.cchGrid;
    crow = qi.crowGrid;
  }
  if (cwch < 1 || (qi.rgwchView == NULL && qi.rgwchGrid == NULL))
    return rgrc;
  for (y = 0; y < crow; y++)
    for (x = 0; x <= cch - cwch; x++) {
      int chBefore = WchTextGridQt(x - 1, y);
      int chAfter = WchTextGridQt(x + cwch, y);
      if (FIsWordChQt(chBefore) || FIsWordChQt(chAfter))
        continue;
      for (i = 0; i < cwch && WchTextGridQt(x + i, y) ==
        strWord[i].unicode(); i++)
        ;
      if (i < cwch)
        continue;
      // The boundary cells are punctuation, but punctuation glues: a
      // sign before the value's first digit starts the value, a mark
      // after the value's last digit ends it, and a point or colon
      // between word characters sits inside one. A match has to be the
      // whole value -- "0:08" inside "+0:08'" is a fragment, and not
      // what the mouse asked for.
      if (FStartsWordQt(chBefore, strWord[0].unicode()) ||
        FGluesQt(WchTextGridQt(x - 2, y), chBefore,
          strWord[0].unicode()) ||
        FGluesQt(strWord[cwch - 1].unicode(), chAfter,
          WchTextGridQt(x + cwch + 1, y)))
        continue;
      rgrc << QRect(x * qi.xChar + 4, (y + 1) * qi.yChar - fm.ascent(),
        cwch * qi.xChar, fm.ascent() + fm.descent());
    }
  return rgrc;
}

// The aspect list view: the console's own cells shown with a header row
// above the data -- "Obj1 Asp Obj2 Orb Power" -- whose labels the mouse
// clicks to sort. The sort keys chain with Shift+click, primary first.

#define ctcolAspect  5
#define ccolSortMax  4
enum { tcolObj1, tcolAsp, tcolObj2, tcolOrb, tcolPower };

// Does row a sort before row b under the active keys? Each key in turn,
// ties falling through to the next, and finally to the order the chart
// printed -- which is what the stable insertion sort below preserves.
// The object keys sort by object number, the ephemeris's own order,
// unless -WA puts them by name.
static flag FRowLessQt(int a, int b)
{
  const ASPROWQT &ra = qi.rgasprow[a], &rb = qi.rgasprow[b];
  int k, z;

  for (k = 0; k < qi.cSortKeys; k++) {
    switch (qi.rnSortCol[k]) {
    case tcolObj1:
      z = qi.fSortObjAlpha ? QString(szObjDisp[ra.o1]).compare(
        QString(szObjDisp[rb.o1]), Qt::CaseInsensitive) : ra.o1 - rb.o1;
      break;
    case tcolAsp:
      z = ra.ahi - rb.ahi;
      break;
    case tcolObj2:
      z = qi.fSortObjAlpha ? QString(szObjDisp[ra.o2]).compare(
        QString(szObjDisp[rb.o2]), Qt::CaseInsensitive) : ra.o2 - rb.o2;
      break;
    case tcolOrb:
      z = RAbs(ra.rOrb) < RAbs(rb.rOrb) ? -1 :
        (RAbs(ra.rOrb) > RAbs(rb.rOrb) ? 1 : 0);
      break;
    default:
      z = ra.rPow < rb.rPow ? -1 : (ra.rPow > rb.rPow ? 1 : 0);
      break;
    }
    if (z != 0)
      return qi.rgfSortDesc[k] ? z > 0 : z < 0;
  }
  return fFalse;
}

// One cell of the PRISTINE grid, the recording TextCharQt() made. The
// view builder reads this; everything on the mouse side reads the view
// through WchTextGridQt() instead.
static int WchPrisQt(int xCell, int yCell)
{
  if (qi.rgwchGrid == NULL || xCell < 0 || yCell < 0 ||
    xCell >= qi.cchGrid || yCell >= qi.crowGrid)
    return 0;
  return qi.rgwchGrid[yCell * qi.cchGrid + xCell];
}

// The grid's characters from one PRISTINE row, first column through last
// inclusive -- the view-building twin of StrWordSpanQt(), which reads
// whatever the view shows and is wrong here: mid-rebuild the view is the
// previous one, whose rows are shifted and whose first data row is the
// old header.
static QString StrPrisSpanQt(int yCell, int x1, int x2)
{
  QString str;
  int x;

  for (x = x1; x <= x2; x++)
    str += QChar(WchPrisQt(x, yCell));
  return str;
}

// Re-render the console image from the view grid, character by character,
// in each cell's own colour. This is the same draw TextCharQt() did,
// which is why the re-render is indistinguishable from the print except
// for what the view itself changes: the header row, the row order, and
// the renumbered index column.
static void ClearTextSelectionQt(void);

static void TextViewRenderQt(void)
{
  KV kvBack = KvFromKi(gi.kiOff);
  int dx = gi.qim != NULL ? gi.qim->width() :
    qi.cchView * qi.xChar + 8;
  int x, y;

  // The view is about to change row-for-row, which is what a selection's
  // cells name; a re-sort rebuilds the view without a full redraw, so
  // this is the second of the two places a selection dies.
  ClearTextSelectionQt();

  delete gi.qim;
  gi.qim = new QImage(dx, qi.crowView * qi.yChar, QImage::Format_RGB32);
  gi.qim->fill(QColor(RgbR(kvBack), RgbG(kvBack), RgbB(kvBack)));
  gi.qpaint = new QPainter(gi.qim);
  gi.qpaint->setRenderHint(QPainter::TextAntialiasing,
    FConsoleAntialiasQt());
  gi.qpaint->setFont(qi.fontText);
  for (y = 0; y < qi.crowView; y++)
    for (x = 0; x < qi.cchView; x++)
      if (qi.rgwchView[y * qi.cchView + x] != 0) {
        KV kv = KvFromKi(qi.rgkiView[y * qi.cchView + x]);
        gi.qpaint->setPen(QColor(RgbR(kv), RgbG(kv), RgbB(kv)));
        gi.qpaint->drawText(x * qi.xChar + 4, (y + 1) * qi.yChar,
          QString(QChar(qi.rgwchView[y * qi.cchView + x])));
      }
  delete gi.qpaint;
  gi.qpaint = NULL;
  if (gi.qcanvas != NULL) {
    if (qi.pscroll != NULL)
      ApplyTextSizeModeQt();
    gi.qcanvas->update();
  }
}

// Build the aspect list view from the pristine grid: a header row above
// the data, the data rows in sort-key order and renumbered, everything
// else in place. The header's labels anchor to the first data row's own
// text and center over their fields, so they sit over the columns
// whatever the degree and distance formats are doing. fFalse (and no
// view at all, the plain print) when this chart's rows and the sink's
// don't agree -- an interpret-mode listing has prose rows and gets no
// header.
static flag FBuildAspectViewQt(void)
{
  static CONST char *rgszLabel[ctcolAspect] =
    {"Obj1", "Asp", "Obj2", "Orb", "Power"};
  int rgtcolX[ctcolAspect], rgspan[ctcolAspect];
  QVector<int> rgyData, rgis;
  int cch = qi.cchGrid, crow = qi.crowGrid, x, y, c, i, k;
  int yLast = -1;
  wchar *rgwchNew;
  byte *rgkiNew;

  // The data rows: an index field (" %3d" then ':') at the left edge.
  for (y = 0; y < crow; y++) {
    c = 0;
    while (c < 6 && WchPrisQt(c, y) == ' ')
      c++;
    if (c > 5)
      continue;
    while (c < 6 && WchPrisQt(c, y) >= '0' && WchPrisQt(c, y) <= '9')
      c++;
    if (c > 0 && c < 6 && WchPrisQt(c, y) == ':')
      rgyData.append(y);
  }
  if (rgyData.isEmpty() || rgyData.size() != qi.rgasprow.size())
    return fFalse;

  // The label columns anchor to the first data row's own text. The row
  // prints the sink's own objects and aspect, so search for those exact
  // strings in print order -- names truncated to the seven cells the
  // row's format gives them -- then the "orb:"/"power:" labels the core
  // prints for the last two columns. A name like "North N" spans two
  // word runs where a short one spans padding plus a single run, so
  // counting runs or tokens would slide every label a field left;
  // searching the strings themselves can't.
  QString strRow = StrPrisSpanQt(rgyData[0], 0, cch - 1);
  QString strObj1 = QString(szObjDisp[qi.rgasprow[0].o1]).left(7);
  QString strObj2 = QString(szObjDisp[qi.rgasprow[0].o2]).left(7);
  QString strAsp = QString(SzAspectAbbrev(qi.rgasprow[0].ahi));
  int xObj1 = strRow.indexOf(strObj1);
  int xAsp = strRow.indexOf(strAsp, xObj1 + strObj1.length());
  int xObj2 = strRow.indexOf(strObj2, xAsp + strAsp.length());
  int xOrb = strRow.indexOf(QString("orb:"), xObj2 + strObj2.length());
  int xPow = strRow.indexOf(QString("power:"), xOrb + 4);
  if (xObj1 < 0 || xAsp < 0 || xObj2 < 0 || xOrb < 0 || xPow < 0)
    return fFalse;
  // Each label centers over the field it names: the first name's
  // seven-cell %7.7s box (the anchor above sits on the name's first
  // letter, which right-alignment moves around inside the box), the
  // abbrev's own width, the second name's ten cells, and the core's
  // "orb:"/"power:" texts. Every span is text the row itself prints,
  // so a centered label can't run past cells the row doesn't have.
  rgtcolX[tcolObj1] = xObj1 - (7 - strObj1.length());
  rgspan[tcolObj1] = 7;
  rgtcolX[tcolAsp] = xAsp;
  rgspan[tcolAsp] = strAsp.length();
  rgtcolX[tcolObj2] = xObj2;
  rgspan[tcolObj2] = 10;
  rgtcolX[tcolOrb] = xOrb;
  rgspan[tcolOrb] = CchSz("orb:");
  rgtcolX[tcolPower] = xPow;
  rgspan[tcolPower] = CchSz("power:");

  // The sort order over the sink's structured keys: each active key in
  // turn, ties falling through to the next, and finally to the order the
  // chart printed (a stable sort).
  rgis.reserve(rgyData.size());
  for (i = 0; i < rgyData.size(); i++)
    rgis.append(i);
  if (qi.cSortKeys > 0)
    // A stable insertion sort: a listing is at most a few hundred rows,
    // and std::stable_sort on ints drags a libstdc++ temporary-buffer
    // path in that clang flags as deprecated.
    for (i = 1; i < rgis.size(); i++) {
      int nT = rgis[i], j;
      for (j = i - 1; j >= 0 && FRowLessQt(nT, rgis[j]); j--)
        rgis[j + 1] = rgis[j];
      rgis[j + 1] = nT;
    }

  // The view: every pristine row, plus one for the header.
  rgwchNew = new wchar[cch * (crow + 1)];
  rgkiNew = new byte[cch * (crow + 1)];
  memset(rgwchNew, 0, (size_t)cch * (crow + 1) * sizeof(wchar));
  memset(rgkiNew, 0, (size_t)cch * (crow + 1) * sizeof(byte));
  y = rgyData[0];
  for (i = 0; i < y; i++) {
    memcpy(rgwchNew + i * cch, qi.rgwchGrid + i * cch, cch * sizeof(wchar));
    memcpy(rgkiNew + i * cch, qi.rgkiGrid + i * cch, cch * sizeof(byte));
  }
  // The header row itself: each label centered in its column's field,
  // and after each label that's an active sort key, the direction it's
  // sorting in.
  for (k = 0; k < ctcolAspect; k++) {
    CONST char *pch = rgszLabel[k];
    int xLab = rgtcolX[k] + Max(0, (rgspan[k] - CchSz(pch)) >> 1);

    x = xLab;
    while (*pch) {
      rgwchNew[y * cch + x] = (wchar)*pch;
      rgkiNew[y * cch + x] = (byte)kWhiteA;
      pch++;
      x++;
    }
    for (i = 0; i < qi.cSortKeys; i++)
      if (qi.rnSortCol[i] == k) {
        rgwchNew[y * cch + x] = qi.rgfSortDesc[i] ? 'v' : '^';
        rgkiNew[y * cch + x] = (byte)kWhiteA;
        x++;
        break;
      }
    qi.rgrcHdr[k] = QRect(xLab * qi.xChar + 4, y * qi.yChar,
      (x - xLab) * qi.xChar, qi.yChar);
  }
  // The data rows, in the sorted order, renumbered from one: the index
  // field is always three cells of "%3d", so the ':' and everything
  // after it stay put.
  for (i = 0; i < rgyData.size(); i++) {
    int ys = rgyData[rgis[i]], yd = y + 1 + i;
    char szNum[16];

    memcpy(rgwchNew + yd * cch, qi.rgwchGrid + ys * cch, cch * sizeof(wchar));
    memcpy(rgkiNew + yd * cch, qi.rgkiGrid + ys * cch, cch * sizeof(byte));
    sprintf2(S(szNum), "%3d", i + 1);
    for (x = 0; x < 3; x++) {
      rgwchNew[yd * cch + x] = (wchar)szNum[x];
      rgkiNew[yd * cch + x] = qi.rgkiGrid[ys * cch + x];
    }
    yLast = yd;
  }
  // And whatever followed the data -- the summary block -- shifted down
  // one to make room.
  for (i = yLast + 1; i < crow + 1; i++) {
    memcpy(rgwchNew + i * cch, qi.rgwchGrid + (i - 1) * cch,
      cch * sizeof(wchar));
    memcpy(rgkiNew + i * cch, qi.rgkiGrid + (i - 1) * cch,
      cch * sizeof(byte));
  }
  delete[] qi.rgwchView;
  delete[] qi.rgkiView;
  qi.rgwchView = rgwchNew;
  qi.rgkiView = rgkiNew;
  qi.cchView = cch;
  qi.crowView = crow + 1;
  qi.fHdr = fTrue;
  qi.yHdr = y;
  TextViewRenderQt();
  return fTrue;
}

// After every text render: decide whether what printed is a sortable
// aspect list, and if so show it as the headered view. Without one, the
// view is simply the pristine grid, which is to say the console as it
// printed -- every other text chart behaves exactly as before.
static void TextAspectViewQt(void)
{
  delete[] qi.rgwchView;
  qi.rgwchView = NULL;
  delete[] qi.rgkiView;
  qi.rgkiView = NULL;
  qi.cchView = qi.crowView = 0;
  qi.fHdr = fFalse;
  qi.yHdr = -1;
  if (us.fAspList && qi.rgasprow.size() > 0)
    FBuildAspectViewQt();
}

// A header click: click sets the column as the sole sort key (or, on the
// already-primary column, reverses it); Shift+click adds it to the chain
// (or reverses it where it already sits). Power defaults to descending,
// strongest first; the rest to ascending.
static void SortClickQt(int nCol, flag fShift)
{
  int k;

  if (!fShift) {
    if (qi.cSortKeys >= 1 && qi.rnSortCol[0] == nCol)
      qi.rgfSortDesc[0] = !qi.rgfSortDesc[0];
    else {
      qi.rnSortCol[0] = nCol;
      qi.rgfSortDesc[0] = nCol == tcolPower;
      qi.cSortKeys = 1;
    }
  } else {
    for (k = 0; k < qi.cSortKeys; k++)
      if (qi.rnSortCol[k] == nCol) {
        qi.rgfSortDesc[k] = !qi.rgfSortDesc[k];
        break;
      }
    if (k >= qi.cSortKeys && qi.cSortKeys < ccolSortMax) {
      qi.rnSortCol[qi.cSortKeys] = nCol;
      qi.rgfSortDesc[qi.cSortKeys] = nCol == tcolPower;
      qi.cSortKeys++;
    }
  }
  if (FBuildAspectViewQt())
    gi.qcanvas->update();
}

// The two highlight layers, each a word plus where it sits. The pinned one
// is what a click set (or cleared); the other is whatever is under the
// cursor. Refresh re-reads the pinned word after every text render, so a
// pinned word survives switching charts and chart types -- the same word
// lights in the new listing, which is the point of pinning one.
void SetTextHighlightQt(CONST QString &strWord)
{
  qi.strTextHi = strWord;
  qi.rgrcTextHi = RgrcTextWordQt(strWord);
}

void SetTextHoverQt(CONST QString &strWord)
{
  qi.strTextHover = strWord;
  qi.rgrcTextHover = RgrcTextWordQt(strWord);
}

static void RefreshTextHighlightQt(void)
{
  if (!qi.strTextHi.isEmpty())
    qi.rgrcTextHi = RgrcTextWordQt(qi.strTextHi);
}

void ClearTextHoverQt(void)
{
  qi.strTextHover = QString();
  qi.rgrcTextHover.clear();
}

// The mouse, forwarded by the canvas's mousePressEvent and mouseMoveEvent
// below: the text console's click and hover. Keeping them here puts every
// piece of the text grid logic in this file, and gives the suite the same
// entry points a mouse press has. Nothing to do in graphics mode, where a
// left button scribbles and Alt+click relocates instead.
void TextClickAtPtQt(int xPix, int yPix, flag fShift)
{
  QString str;
  int k;

  if (us.fGraphics)
    return;
  // A click on a header label sorts the view; a Shift+click adds the
  // column to the sort chain. Only then is it an ordinary click, pinning
  // (or unpinning) the word it's on.
  if (qi.fHdr) {
    for (k = 0; k < ctcolAspect; k++)
      if (qi.rgrcHdr[k].contains(xPix, yPix)) {
        SortClickQt(k, fShift);
        return;
      }
  }
  str = StrTextPhraseAtPtQt(xPix, yPix);
  SetTextHighlightQt(str == qi.strTextHi ? QString() : str);
  if (gi.qcanvas != NULL)
    gi.qcanvas->update();
}

void TextHoverAtPtQt(int xPix, int yPix)
{
  QString str;

  if (us.fGraphics)
    return;
  str = StrTextPhraseAtPtQt(xPix, yPix);
  if (str == qi.strTextHover)
    return;
  SetTextHoverQt(str);
  if (gi.qcanvas != NULL)
    gi.qcanvas->update();
}

// The cell a canvas point lands in, clamped into the view -- the draw in
// TextCharQt() above read backwards, which is the same math the word
// lookup uses.
static void CellAtPtQt(int xPix, int yPix, int *px, int *py)
{
  *px = (xPix - 4) / qi.xChar;
  *py = yPix / qi.yChar;
  if (*px < 0)
    *px = 0;
  if (*py < 0)
    *py = 0;
  if (*px > CchTextAtQt() - 1)
    *px = CchTextAtQt() - 1;
  if (*py > CrowTextAtQt() - 1)
    *py = CrowTextAtQt() - 1;
}

// A selection goes away on its own terms: any re-render invalidates the
// view cells it names. The press state is the drag in progress, which
// outlives this -- a re-render mid-drag just means the drag restarts
// from wherever the render left the view.
static void ClearTextSelectionQt(void)
{
  qi.fSel = fFalse;
  qi.rgrcTextSel.clear();
  qi.strTextSel = QString();
}

// The drag selection's row spans and text, rebuilt from the anchor cell
// to the cell the drag has reached. Rows in between go the full width,
// and each row's trailing blanks come off -- the shape a terminal's
// select-and-copy has, which is the gesture this is.
static void RebuildTextSelQt(void)
{
  int cch = CchTextAtQt(), crow = CrowTextAtQt(), y, x, xA, xB;
  int xTop, yTop, xBot, yBot;

  qi.rgrcTextSel.clear();
  qi.strTextSel = QString();
  if (!qi.fSel || cch < 1 || crow < 1)
    return;
  if (qi.ySel1 <= qi.ySel2) {
    xTop = qi.xSel1; yTop = qi.ySel1;
    xBot = qi.xSel2; yBot = qi.ySel2;
  } else {
    xTop = qi.xSel2; yTop = qi.ySel2;
    xBot = qi.xSel1; yBot = qi.ySel1;
  }
  for (y = yTop; y <= yBot; y++) {
    QString strRow;

    xA = y == yTop ? xTop : 0;
    xB = y == yBot ? xBot : cch - 1;
    if (xA > xB) {
      x = xA; xA = xB; xB = x;
    }
    for (x = xA; x <= xB; x++) {
      int wch = WchTextGridQt(x, y);

      strRow += QChar(wch ? wch : ' ');
    }
    while (strRow.endsWith(' '))
      strRow.chop(1);
    qi.rgrcTextSel << QRect(xA * qi.xChar + 4, y * qi.yChar,
      (xB - xA + 1) * qi.xChar, qi.yChar);
    if (y > yTop)
      qi.strTextSel += '\n';
    qi.strTextSel += strRow;
  }
}

// The three halves of a drag selection, forwarded by the canvas's mouse
// handlers: the press records where the left button landed and lets go
// of whatever a previous drag left, a move to a new cell grows the
// selection, and the release either keeps it or -- for a press that
// never became a drag -- is the plain click, which pins a word or sorts
// a header exactly as it always did. The click moving from press to
// release is what lets one gesture mean two things without a word
// pinning mid-drag.

void TextPressAtPtQt(int xPix, int yPix, flag fShift)
{
  int x, y;

  if (us.fGraphics || gi.qim == NULL || CchTextAtQt() < 1 ||
    CrowTextAtQt() < 1)
    return;
  CellAtPtQt(xPix, yPix, &x, &y);
  qi.xSel1 = qi.xSel2 = x;
  qi.ySel1 = qi.ySel2 = y;
  qi.fSelPress = fTrue;
  qi.fSelShift = fShift;
  ClearTextSelectionQt();
}

void TextDragAtPtQt(int xPix, int yPix)
{
  int x, y;

  if (us.fGraphics || !qi.fSelPress)
    return;
  CellAtPtQt(xPix, yPix, &x, &y);
  if (x == qi.xSel2 && y == qi.ySel2)
    return;
  qi.xSel2 = x;
  qi.ySel2 = y;
  // Sticky: a drag that wanders back to its anchor is still a drag.
  qi.fSel = qi.fSel || x != qi.xSel1 || y != qi.ySel1;
  RebuildTextSelQt();
  if (gi.qcanvas != NULL)
    gi.qcanvas->update();
}

void TextReleaseAtPtQt(int xPix, int yPix)
{
  if (us.fGraphics || !qi.fSelPress)
    return;
  qi.fSelPress = fFalse;
  if (qi.fSel) {
    if (gi.qcanvas != NULL)
      gi.qcanvas->update();
    return;
  }
  TextClickAtPtQt(xPix, yPix, qi.fSelShift);
}

// What the selection covers, for the copy path and the suite.
QString StrTextSelectionQt(void)
{
  return qi.strTextSel;
}

// The pixel rectangle of one view cell -- the wash a selection lays over
// it, and how the suite aims a press or a drag at a known cell without
// knowing the font metrics.
QRect RgrcTextCellQt(int xCell, int yCell)
{
  return QRect(xCell * qi.xChar + 4, yCell * qi.yChar, qi.xChar, qi.yChar);
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

  // Only text charts come through here -- graphics text is DrawSz() in
  // xcharts0.cpp, which never calls this -- so what the grid retains is
  // always the whole console, characters and their colours both.
  RecordTextGridQt(xCell, yCell, wch);
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
  // A drag selection narrows this to just what it covers, as a
  // terminal's Edit > Copy does. An empty one (blank rows only) copies
  // nothing rather than clearing the clipboard.
  QString qs = qi.fSel ? qi.strTextSel : CaptureTextChartQt(fFalse);

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


// The Ephemeris Server's bounded wait (SrvPrefetchQt) runs an event loop
// in the middle of a cast. User input is held back for its length, but
// paint events and timers still run, and a paint that finds the canvas the
// wrong size redraws -- which casts, inside the cast, through a second
// painter on the image the first one still holds (EPHEMERIS_REVIEW.md C1).
// So nothing redraws while a cast waits: the redraw is owed, and made the
// moment the wait ends.
static flag s_fSrvWaitingQt = fFalse;
static flag s_fSrvRedrawOwedQt = fFalse;

void RedrawQt()
{
  if (qi.fNoUpdate || (qi.grfHold & grfHoldRedraw))
    return;
  if (s_fSrvWaitingQt) {
    s_fSrvRedrawOwedQt = fTrue;
    return;
  }
  s_fSrvRedrawOwedQt = fFalse;   // This redraw pays whatever was owed.
  // A redraw is about to replace what the console held, so the hover is
  // gone, and with it any drag selection: both name view cells, and the
  // view is about to change. The pinned word is not: the text branch
  // re-records the grid below and looks the word up in it again, so it
  // re-lights in whatever listing is now up.
  ClearTextHoverQt();
  ClearTextSelectionQt();
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
  if (gs.xWin < 1)
    gs.xWin = 1;
  if (gs.yWin < 1)
    gs.yWin = 1;
  // "Timed Exposure" (gs.fJetTrail, "-Xj", Graphics / Chart Effects):
  // KEEP the buffer and do not clear it, so each chart draws over the last
  // and an animation leaves trails. Windows does it inside
  // DrawClearScreen() (xgeneral.cpp:639), which returns without erasing;
  // this path allocated a fresh QImage and filled it on every redraw, so
  // that early return had nothing left to protect and the menu item did
  // NOTHING on screen. Measured: two renders of a globe at different
  // rotations left 151,650 pixels of ink with the option on and 151,650
  // with it off, to the pixel.
  //
  // Graphics only, and only when the buffer is still the right size --
  // a text chart's buffer is the size of the TEXT and is rebuilt each
  // time, and a resized window has nothing to carry over anyway. "Clear
  // Screen" still clears, as it does on Windows, which forces the flag off
  // around its own erase (wdriver.cpp:1393).
  flag fTrail = gs.fJetTrail && us.fGraphics && gi.qim != NULL &&
    gi.qim->width() == gs.xWin && gi.qim->height() == gs.yWin;
  if (!fTrail && gi.qim != NULL) {
    delete gi.qim;
    gi.qim = NULL;
  }
  // Keep the buffer the size of the widget, then draw into a square part
  // of it if the chart wants that. Windows does the squaring in FActionX
  // (xscreen.cpp:2275) when "Ensure Square Charts Remain Square" is on and
  // the chart type is one that looks right square, which is why a
  // maximized window there keeps a round wheel with space beside it rather
  // than stretching it into an oval. The screen path here goes straight to
  // DrawChartX() and never passes through FActionX, so it does the same
  // thing itself.
  int dxWin = gs.xWin, dyWin = gs.yWin;
  if (gi.qim == NULL)
    gi.qim = new QImage(gs.xWin, gs.yWin, QImage::Format_RGB32);
  // InitColorsX() is what turns "Reverse Background" (gs.fInverse) and
  // "Monochrome" (gs.fColor) into the colours the drawing code actually
  // reads: gi.kiOn, kiOff, kiLite and kiGray, and the whole *B family --
  // kMainB, kRainbowB, kElemB, kAspB, kObjB, kRayB (xscreen.cpp:124).
  // FActionX() calls it before every render to a FILE (xscreen.cpp:1672).
  // This screen path never did, so both menu items worked when exporting
  // a chart and did NOTHING on screen.
  //
  // Measured before the fix rather than reasoned: with reverse on, the
  // commonest pixel of a wheel stayed black and gi.kiOn/kiOff stayed
  // 15/0; with monochrome on, the render still had 14 distinct colours.
  //
  // After InitColors() above, not before: that one derives kObjA[] from
  // the per-object settings, and this derives kObjB[] from kObjA[].
  InitColorsX();
  // And the background is gi.kiOff, not black. Same colour ClearScreenQt()
  // uses, and what Windows' TextClearScreen() resolves to.
  KV kvBack = KvFromKi(gi.kiOff);
  if (!fTrail)
    gi.qim->fill(QColor(RgbR(kvBack), RgbG(kvBack), RgbB(kvBack)));
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
    // What the console is about to draw is what the mouse will hit-test
    // against, so the retained grid starts empty here, ahead of Action().
    // Two passes can happen below; both draw the same characters, so the
    // recording is idempotent.
    TextGridResetQt();
    gi.qpaint->setRenderHint(QPainter::TextAntialiasing,
      FConsoleAntialiasQt());
    gi.qpaint->setFont(qi.fontText);
    qi.kvText = KvFromKi(kLtGrayA);
    qi.kiText = kLtGrayA;
    is.cchRow = is.cchCol = is.cchColMax = 0;
    FILE *fileSav = is.S;
    flag fMultSav;
    is.S = stdout;
    // Collect the aspect list's structured rows while the chart prints,
    // for the sortable view; a fresh pass restarts the collection.
    qi.rgasprow.clear();
    pfnAspectRow = AspectRowSinkQt;
    Action();

    // A text chart is as big as it prints, and that has nothing to do with
    // the window: is.cchColMax and is.cchRow come from the chart and from
    // us.fClip80/us.nScreenWidth. Drawn into a buffer the size of the
    // window, anything past the edge was simply GONE -- measured at the
    // compiled default 600x600 window, the aspect grid prints 56 rows and
    // 40 of them fit, and the ephemeris listing prints 99 columns where 75
    // fit. Windows loses nothing there because its window scrolls over a
    // virtual area (the wi.xScroll/wi.yScroll offsets in PrintSz); this
    // port's answer is the scroll area it already has, which needs the
    // canvas to be the size of the text.
    //
    // Two passes only when the first did not fit, and the second is
    // PrintChart() rather than Action(): the chart is already cast, and
    // repeating Action() would cast it again and fire the "-~Q1"/"-~Q2"
    // display hooks a second time.
    int dxText = is.cchColMax * qi.xChar + 8;
    int dyText = (is.cchRow + 1) * qi.yChar;
    if (dxText > gi.qim->width() || dyText > gi.qim->height()) {
      delete gi.qpaint;
      delete gi.qim;
      // Bounded by what the canvas can be, which is Astrolog's own chart
      // size limit: the canvas carries setMaximumSize(BITMAPX, BITMAPY),
      // and a buffer larger than the canvas would put the excess back out
      // of reach, which is the bug this is fixing.
      gi.qim = new QImage(Min(Max(dxText, dxWin), BITMAPX),
        Min(Max(dyText, dyWin), BITMAPY), QImage::Format_RGB32);
      gi.qim->fill(QColor(RgbR(kvBack), RgbG(kvBack), RgbB(kvBack)));
      gi.qpaint = new QPainter(gi.qim);
      gi.qpaint->setRenderHint(QPainter::TextAntialiasing,
        FConsoleAntialiasQt());
      gi.qpaint->setFont(qi.fontText);
      qi.kvText = KvFromKi(kLtGrayA);
      qi.kiText = kLtGrayA;
      is.cchRow = is.cchCol = is.cchColMax = 0;
      qi.rgasprow.clear();
      // Action()'s own text half, minus the cast and minus the "-~Q1" /
      // "-~Q2" hooks, both of which the first pass already did. Calling
      // PrintChart() alone was tried and is wrong: the eleven Help menu
      // chart types -- the credits, the sign and object lists, the
      // keystroke list -- are printed by FPrintTables(), not by
      // PrintChart(), so a second pass without it redrew the ordinary
      // chart in their place. Only the "credit-colors" group under
      // "-i nrvate.as" caught that, because only there was the box big
      // enough to need a second pass at all.
      fMultSav = is.fMult;
      is.fMult = fFalse;
      if (!FPrintTables()) {
        if (is.fMult) {
          PrintL2();
          is.fMult = fFalse;
        }
        PrintChart(is.fProgress);
      }
      is.fMult = fMultSav;
    }
    is.S = fileSav;
    pfnAspectRow = NULL;
    delete gi.qpaint;
    gi.qpaint = NULL;
    // The grid is complete: an aspect list shows as the headered sortable
    // view, every other text chart as it printed. Whatever word is pinned
    // re-lights in the view.
    TextAspectViewQt();
    RefreshTextHighlightQt();
    gs.xWin = dxWin; gs.yWin = dyWin;
    if (gi.qcanvas != NULL && qi.pscroll != NULL) {
      ApplyTextSizeModeQt();
      gi.qcanvas->update();
    }
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
    // scrolling over the old extent. ApplySizeModeQt() does that, and also
    // puts setWidgetResizable() back to what "Window Resizes Chart" says
    // -- which the text branch above turns off while a text chart is up.
    ApplySizeModeQt();
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


// Like AddSelectAction(), but selecting a value does not redraw the chart,
// exactly as Windows' own commands for this setting don't (the pen color
// cases in NWmCommand set wi.fRedraw nowhere). Used by the scribble pen
// color menu: a redraw replaces the chart buffer wholesale, which would
// erase every scribble already on it, making it impossible to draw marks
// in more than one color per chart. The check marks still update, and the
// color takes effect on the next stroke.
static QAction *AddSelectActionNoRedraw(QMenu *pmenu, QActionGroup *pgroup,
  CONST char *szLabel, int value, int *ptarget)
{
  QAction *pa = pmenu->addAction(szLabel);
  pa->setCheckable(true);
  pa->setChecked(*ptarget == value);
  pa->setActionGroup(pgroup);
  PaRegisterCheckQt(pa, [value, ptarget]() { return *ptarget == value; });
  ConnectMenuQt(pa, pa, [value, ptarget]() { *ptarget = value; });
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
  // Windows' cmdNow is Animate(iAnimNow, 0) and a redraw, and the whole
  // of the difference is WHICH CHART gets set to now. Animate() puts it
  // in the twin slot for a comparison chart, in the transit slot (and
  // is.JDp with it) for a transit or progression, and only otherwise in
  // the main chart. This called FInputData() and RecastAndRedrawQt(),
  // which assigns ciMain unconditionally -- so "Chart for Now" on a
  // transit chart REPLACED THE NATAL CHART with the present moment
  // instead of moving the transits to it.
  //
  // RedrawQt() and not RecastAndRedrawQt(), for the reason work log item
  // 212 records: Animate() casts the chart itself, and Windows' cmdNow
  // sets wi.fRedraw without wi.fCast, so nothing casts a second time
  // there either.
  QAction *paNow = pmenu->addAction("Chart for &Now");
  ConnectMenuQt(paNow, pwind, []() {
    Animate(iAnimNow, 0);
    RedrawQt();
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
  QAction *paEphem = pmenu->addAction("E&phemeris Settings...");
  ConnectMenuQt(paEphem, pwind,
    []() { ShowEphemDialogQt(); });
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
    // Around the chart, not to it: the latter left the menu bar's height
    // out of a chart that was meant to be square (wdriver.cpp calls
    // ResizeWindowToChart() here).
    ResizeWindowToChartQt();
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
  // Both clear gs.fAutoScale, which is what Windows does at
  // wdriver.cpp:1972 and 1981 and what the two "Text" items below already
  // did. Without it "Autoscale Glyphs" (-XQ0) recomputes gs.nScale from
  // the window size inside FActionX() (xscreen.cpp:1495) and these two
  // menu items have NO VISIBLE EFFECT at all. (Windows re-bullets
  // Small/Medium/Large/Huge here too; ConnectMenuQt() does that for every
  // item.)
  QAction *paScaleDn = pmenuScale->addAction("&Decrease");
  ConnectMenuQt(paScaleDn, pwind, []() {
    if (gs.nScale > 100) {
      gs.nScale -= 100; gs.fAutoScale = fFalse; RedrawQt();
    }
  });
  QAction *paScaleUp = pmenuScale->addAction("&Increase");
  ConnectMenuQt(paScaleUp, pwind, []() {
    if (gs.nScale < MAXSCALE) {
      gs.nScale += 100; gs.fAutoScale = fFalse; RedrawQt();
    }
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
    // Turning the sidebar on turns "Show Chart Info" on with it, which is
    // an item of its own; Windows corrects that item here by hand
    // (WiCheckMenu(cmdGraphicsText, fTrue), wdriver.cpp:2020) and
    // ConnectMenuQt() re-derives it for this port.
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
      // Turns on "Show Full Star List", an item of its own two lines up
      // the menu. Windows corrects that item's check mark here by hand
      // (WiCheckMenu(cmdGraphicsAllStar, fTrue), wdriver.cpp:2087); this
      // port re-derives every check mark after any menu action instead,
      // in ConnectMenuQt().
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
  // Selecting a pen color must not redraw: that erases scribbles already
  // drawn, so only the newest color could ever show (AddSelectActionNoRedraw
  // above). Windows' pen menu commands don't redraw either.
  AddSelectActionNoRedraw(pmenuPen, pgroupPen, "Blac&k", 0, &gi.kiPen);
  AddSelectActionNoRedraw(pmenuPen, pgroupPen, "&White", 15, &gi.kiPen);
  AddSelectActionNoRedraw(pmenuPen, pgroupPen, "&Red", 9, &gi.kiPen);
  AddSelectActionNoRedraw(pmenuPen, pgroupPen, "&Green", 10, &gi.kiPen);
  AddSelectActionNoRedraw(pmenuPen, pgroupPen, "&Blue", 12, &gi.kiPen);
  AddSelectActionNoRedraw(pmenuPen, pgroupPen, "&Yellow", 11, &gi.kiPen);
  AddSelectActionNoRedraw(pmenuPen, pgroupPen, "&Magenta", 13, &gi.kiPen);
  AddSelectActionNoRedraw(pmenuPen, pgroupPen, "&Cyan", 14, &gi.kiPen);
  AddSelectActionNoRedraw(pmenuPen, pgroupPen, "Gr&ay", 8, &gi.kiPen);
  AddSelectActionNoRedraw(pmenuPen, pgroupPen, "&Lt. Gray", 7, &gi.kiPen);
  AddSelectActionNoRedraw(pmenuPen, pgroupPen, "Maroo&n", 1, &gi.kiPen);
  AddSelectActionNoRedraw(pmenuPen, pgroupPen, "Dk. Gr&een", 2, &gi.kiPen);
  AddSelectActionNoRedraw(pmenuPen, pgroupPen, "Dk. Bl&ue", 4, &gi.kiPen);
  AddSelectActionNoRedraw(pmenuPen, pgroupPen, "Mai&ze", 3, &gi.kiPen);
  AddSelectActionNoRedraw(pmenuPen, pgroupPen, "&Purple", 5, &gi.kiPen);
  AddSelectActionNoRedraw(pmenuPen, pgroupPen, "&Dk. Cyan", 6, &gi.kiPen);

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
  // The standard shortcut, so a drag selection copies the way it does
  // in everything else on the desktop.
  paCopyText->setShortcut(QKeySequence::Copy);
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

int GrfHoldQt()
{
  return qi.grfHold;
}

void SetHoldQt(int grf)
{
  qi.grfHold = grf;
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
    //
    // The validation is not decoration even though nothing here reads the
    // number -- Qt's antialiasing is a render hint, not a supersample
    // factor. The comment above always claimed to validate and did not,
    // and the number is written straight back into astrolog.as, so what
    // was accepted here got saved. Two reasons it should not be:
    //
    // This build's own File Settings dialog refuses anything outside
    // 1-12 (qtdialog.cpp, FValidAntialias), so the switch was the only
    // way to get a value the dialog would then display back at you. And
    // NProcessSwitchesW() refuses the same range (wdriver.cpp:168), so a
    // settings file written here could carry a value the Win32 build
    // rejects -- one astrolog.as is meant to load everywhere. That second
    // one is read from wdriver.cpp rather than measured: "astrolog-wcli"
    // routes "-W" through NProcessSwitchesNullW(), which only consumes,
    // so the console Windows build cannot demonstrate it.
    if (FErrorArgc("Wx", pin->argc, 1))
      return tcError;
    i = NFromSz(pin->argv[1]);
    if (FErrorValN("Wx", !FValidAntialias(i), i, 0))
      return tcError;
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

  case 'A':
    // The aspect list's object columns: =WA sorts them by name, _WA by
    // object number.
    SwitchF(qi.fSortObjAlpha);
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

// Set for the length of the cast an animation tick makes, and nothing else:
// the Ephemeris Server backend serves those casts from wide f32 windows on
// the animation's own grid (EPHEMERIS_CLIENT_PLAN.md section 6), where
// every other cast is asked exactly.
static flag s_fAnimFrameQt = fFalse;
static flag FSrvApproxTakeQt(void);

static void AnimTickQt(void)
{
  // Same guard Windows' WM_TIMER uses. Note gs.nAnim < 1 covers both
  // "off" (negative, remembering the rate) and "never set".
  if (gs.nAnim < 1 || gi.fPause || (qi.grfHold & grfHoldAnim)) {
    // Stopped, whichever way it stopped -- the menu, -Xn from a macro, a
    // warning box negating gs.nAnim: if the chart on screen was served
    // from animation rows, cast it again exactly, once. The timer keeps
    // ticking while stopped, so this is the one place every stop passes.
    if (!s_fAnimTickQt && !s_fSrvWaitingQt && FSrvApproxTakeQt())
      RecastAndRedrawQt();
    return;
  }
  if (s_fAnimTickQt || s_fSrvWaitingQt)
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
  s_fAnimFrameQt = fTrue;
  Animate(gs.nAnim, gi.nDir);
  s_fAnimFrameQt = fFalse;
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
  // A frame the Ephemeris Server served from an animation window is
  // animation-grade, not the exact answer; the chart animation stops on
  // is cast again the exact way, so what stays on screen -- and what any
  // text chart or save made from it says -- is the bit-exact chart.
  if (!fRun && FSrvApproxTakeQt())
    RecastAndRedrawQt();
}
#ifdef QTTEST
void SetAnimRunningTestQt(flag fRun) { SetAnimRunningQt(fRun); }
#endif

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
  QActionGroup *pgroupRate = new QActionGroup(pwind);
  // One of the rates, checked like them: Windows' cmdAnimateNow sits inside
  // the RadioMenu(cmdAnimateNo1, cmdAnimateNo_13) range (wdriver.cpp:2295).
  AddAnimRateAction(pmenuRate, pgroupRate, "Update to &Now", iAnimNow);
  pmenuRate->addSeparator();
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
  // Not a Windows command; see ShowGenerateGifDialogQt(). Greyed out while
  // the chart is text, or a map whose animation spins it rather than
  // moving time -- re-tested each time the menu opens.
  QAction *paGif = pmenu->addAction("&Generate Animation...");
  ConnectMenuQt(paGif, pwind, []() { ShowGenerateGifDialogQt(); });
  QObject::connect(pmenu, &QMenu::aboutToShow, pwind,
    [paGif]() { paGif->setEnabled(FCanGenerateGifQt()); });
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

  // The menu bar's height follows the font, and the window's does not, so
  // the chart viewport took up the difference -- and with "Window Resizes
  // Chart" on, the canvas writes the viewport back into gs.xWin/gs.yWin.
  // A font changed and changed back left the chart a few pixels off, and
  // Save Program Settings recorded that (review finding N-J). Windows has
  // no interface font, so the chart size is what has to stay put: lay the
  // window out now rather than on the next turn of the loop, while
  // gs.xWin/gs.yWin still hold the chart's size, and fit the window
  // around it. Not with the option off: then the chart's size is not
  // chased, and the window is the user's to size.
  if (gi.qwind != NULL && qi.fWindowChart) {
    if (gi.qwind->layout() != NULL)
      gi.qwind->layout()->activate();
    ResizeWindowToChartQt();
  }
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
        // addApplicationFont() returns -1 and says nothing when a font
        // file cannot be parsed; a user missing StarFont Sans would have
        // had no signal at all. A qWarning reaches the console (and the
        // CI log) without bothering anyone in the window itself.
        if (QFontDatabase::addApplicationFont(str) < 0)
          qWarning() << "Astrolog: could not load font" << str;
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
    // Every check mark re-derived from its own setting after ANY menu
    // action, which is the one place that can close a whole class of bug:
    // a handler that changes a flag some OTHER item displays and leaves
    // that item claiming the opposite. Windows corrects each one by hand
    // with WiCheckMenu(cmdOther, ...) and there are ten such sites in
    // wdriver.cpp; this port had matched some and missed others -- "Show
    // Constellation Lines" turning on "Show Full Star List", "Show Info
    // Sidebar" turning on "Show Chart Info", and all three "Draw <x>
    // Indian" items turning on "Show Indian Wheels" and "Show House
    // Details".
    //
    // Safe by construction rather than by review: every entry in
    // rgmcheckQt was registered with a predicate that READS the setting
    // behind it (PaRegisterCheckQt), so re-deriving can only ever agree
    // with the program's own state. RedoMenuQt() is a pure read and
    // deliberately excludes the chart-type radio, which has machinery of
    // its own.
    if (n == cmd) {
      fn();
      RedoMenuQt();
      return;
    }
    if (n <= 0)                      // Expression vetoed the command.
      return;
    for (int i = 0; i < s_rgcmdfnQt.size(); i++)
      if (s_rgcmdfnQt[i].first == n) {
        s_rgcmdfnQt[i].second();
        RedoMenuQt();
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
  {"Show &Full Star List",                      "Show Full &Star List"},
  {"Show E&xoplanets",                          "Show E&xoplanets"},
  {"Show Degree &Grid",                         "Show C&ities"},
  {NULL, NULL},
  {"Use &Ecliptic Axis",                        "Use Ecliptic &Axis"} };

// Windows' menu_V, the Standard listing text chart.
static CONST CTXITEM rgctxTxtListQt[] = {
  {"&View Graphics Mode Wheel",                 "Show &Graphics"},
  {NULL, NULL},
  {"Toggle &Comparison Chart",                  "Com&parison Chart"},
  {"Print Nearest &Second",                     "Print &Nearest Second"},
  {"House Placements Based on &3D Houses",      "&3D Houses"} };

// Windows' menu_W, the House wheel text chart.
static CONST CTXITEM rgctxTxtWheelQt[] = {
  {"&View Graphic House Wheel",                 "Show &Graphics"},
  {NULL, NULL},
  {"&Indian Sign Arrangement",                  "&Indian Wheel Order"},
  {"Print Nearest &Second",                     "Print &Nearest Second"},
  {"House Placements Based on &3D Houses",      "&3D Houses"} };

// Windows' menu_G, the Grid text chart.
static CONST CTXITEM rgctxTxtGridQt[] = {
  {"&View Graphic Grid",                        "Show &Graphics"},
  {NULL, NULL},
  {"Toggle &Comparison Chart",                  "Com&parison Chart"},
  {"Print Nearest &Second",                     "Print &Nearest Second"},
  {"&Parallel Aspects",                         "&Parallel Aspects"},
  {"&Applying Aspects",                         "&Applying Aspects"} };

// Windows' menu_A, the Aspect list text chart.
static CONST CTXITEM rgctxTxtAspectQt[] = {
  {"Toggle &Comparison Chart",                  "Com&parison Chart"},
  {"Print Nearest &Second",                     "Print &Nearest Second"},
  {"&Parallel Aspects",                         "&Parallel Aspects"},
  {"&Applying Aspects",                         "&Applying Aspects"} };

// Windows' menu_M, the Midpoint list text chart.
static CONST CTXITEM rgctxTxtMidpointQt[] = {
  {"&View Graphic Dial Chart",                  "Show &Graphics"},
  {NULL, NULL},
  {"Toggle &Comparison Chart",                  "Com&parison Chart"},
  {"Print Nearest &Second",                     "Print &Nearest Second"},
  {"Show &Latitude Midpoints Too",              "&Parallel Aspects"},
  {"Midpoints are &3D",                         "&3D Houses"} };

// Windows' menu_Z, the Horizon text chart.
static CONST CTXITEM rgctxTxtHorizonQt[] = {
  {"&View Graphic Horizon Chart",               "Show &Graphics"},
  {NULL, NULL},
  {"Print Nearest &Second",                     "Print &Nearest Second"},
  {"Show &3D House Placements",                 "&3D Houses"} };

// Windows' menu_S, the Orbit text chart.
static CONST CTXITEM rgctxTxtOrbitQt[] = {
  {"&View Graphic Orbit Chart",                 "Show &Graphics"},
  {NULL, NULL},
  {"Print Nearest &Second",                     "Print &Nearest Second"} };

// Windows' menu_H, the Sector text chart.
static CONST CTXITEM rgctxTxtSectorQt[] = {
  {"&View Graphic Sector Wheel",                "Show &Graphics"},
  {NULL, NULL},
  {"Print Nearest &Second",                     "Print &Nearest Second"} };

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
  {"Print Nearest &Second",                     "Print &Nearest Second"} };

// Windows' menu_E, the Ephemeris text chart.
static CONST CTXITEM rgctxTxtEphemerisQt[] = {
  {"&View Graphic Ephemeris",                   "Show &Graphics"},
  {NULL, NULL},
  {"Ephemeris Shows &Latitudes",                "&Parallel Aspects"},
  {"Print Nearest &Second",                     "Print &Nearest Second"} };

// Windows' menu_P, the Arabic parts text chart.
static CONST CTXITEM rgctxTxtArabicQt[] = {
  {"Print Nearest &Second",                     "Print &Nearest Second"} };

// Windows' menu_I, the Rising text chart.
static CONST CTXITEM rgctxTxtRisingQt[] = {
  {"&View Graphic Rising Chart",                "Show &Graphics"},
  {NULL, NULL},
  {"Print Nearest &Second",                     "Print &Nearest Second"} };

// Windows' menu_N, the Nearest cities text chart.
static CONST CTXITEM rgctxTxtLocalQt[] = {
  {"&View Graphic Local Space Chart",           "Show &Graphics"},
  {NULL, NULL},
  {"Print Nearest &Second",                     "Print &Nearest Second"} };

// Windows' menu_8, the Moons text chart.
static CONST CTXITEM rgctxTxtMoonsQt[] = {
  {"&View Graphic Moons Chart",                 "Show &Graphics"},
  {NULL, NULL},
  {"&Parallel Aspects",                         "&Parallel Aspects"},
  {"Print Nearest &Second",                     "Print &Nearest Second"} };

// Windows' menu_Ux, the Exoplanets text chart.
static CONST CTXITEM rgctxTxtExoQt[] = {
  {"Transits at &Chart Time",                   "&Parallel Aspects"},
  {"&Exact Transits Only",                      "&3D Houses"} };

// Windows' menu_D, the Transit times text chart.
static CONST CTXITEM rgctxTxtInDayQt[] = {
  {"&Parallel Aspects",                         "&Parallel Aspects"},
  {"Print Nearest &Second",                     "Print &Nearest Second"} };

// Windows' menu_T, the Transit influence text chart.
static CONST CTXITEM rgctxTxtTransInfQt[] = {
  {"&Parallel Aspects",                         "&Parallel Aspects"},
  {"&Applying Aspects",                         "&Applying Aspects"},
  {"Print Nearest &Second",                     "Print &Nearest Second"} };

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


// What "-WF"/"-WG" accept, so the Display Settings dialog can accept the
// same thing. Zero means "no preference": character scale for the console
// font, the desktop's own size for the menu one.

flag FValidFontSizeQt(int n)
{
  return n == 0 || FBetween(n, nFontSizeMinQt, nFontSizeMaxQt);
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

// The aspect list's object columns sort by object number, the
// ephemeris's own order; =WA flips them to the names' alphabetical
// order, which is behind the switch because the number order is the
// one the rest of the program's object lists use.

flag FSortObjAlphaQt(void)
{
  return qi.fSortObjAlpha;
}

void SetSortObjAlphaQt(flag f)
{
  qi.fSortObjAlpha = f;
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
// Declared here, defined below with the rest of the Ephemeris Server
// connection, which FinalizeQt() tears down.
void EphSrvFinalizeQt();

void FinalizeQt(void)
{
  int i;

  if (qi.pnam != NULL) {
    delete qi.pnam;
    qi.pnam = NULL;
  }

  // The Ephemeris Server's socket and timers go too.
  EphSrvFinalizeQt();

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


/*
******************************************************************************
** Ephemeris server connection.
******************************************************************************
*/

// The Ephemeris Server backend's one held connection: a WebSocket to the
// ephd service (EPHEMERIS_CLIENT_PLAN.md §4). State machine Disconnected →
// Connecting → Welcomed, entirely in the background -- no dialogs, no
// status noise -- while a local ephemeris path is configured, because the
// backend is a convenience there and nobody is waiting on it. The
// required-server mode, whose dialog and exit ladder land in
// EphSrvStartupQt() at increment 4, is the only blocking UI the plan
// allows at all.
//
// The whole protocol rides ephproto.h (the envelope, HELLO/WELCOME, the
// limits WELCOME carries); nothing below duplicates it. Qt answers
// WebSocket protocol-level pings by itself, so there is no heartbeat of
// our own here (lesson 4); the app-level PING/PONG message types are
// answered for servers of other clients' sake only.

// Connection states.
#define esDisconnected 0        // No socket, or the last one is gone.
#define esConnecting   1        // Socket open, HELLO sent, WELCOME awaited.
#define esWelcomed     2        // WELCOME parsed; limits/caps govern.

// The reconnect ladder. Before the first WELCOME -- or when the server
// keeps refusing -- the background path retries on the slow tick. After a
// session has been had and dropped, the ladder doubles from 1 second,
// capped at a minute, jittered, so a restarted server is back within a
// couple of seconds and a dead one is not hammered.
#define msEphSrvRetry     60000 // Slow background tick before a session.
#define msEphSrvBackoff   1000  // First rung of the drop ladder.
#define msEphSrvBackoffMax 60000
#define msEphSrvHello     15000 // A HELLO with no WELCOME back is a dead
                                // server; give up on the attempt.
static int s_msEphSrvHelloQt = msEphSrvHello;   // Settable by the suite.

// A request in flight: its exact wire bytes, and how many sessions it has
// been re-sent into. A request that keeps losing its connection -- one
// that crashes the server, say -- is given up after three, rather than
// re-sent every second forever (EPHEMERIS_REVIEW.md C11).
typedef struct _EphReq {
  QByteArray ba;
  int cResend;
} EPHREQ;
#define cEphSrvResendMax 3

static struct {
  int est;                    // The es* state.
  QWebSocket *pws;            // The socket, Connecting or Welcomed; NULL
                              // in Disconnected.
  QTimer *ptim;               // The one-shot retry timer, armed whenever
                              // Disconnected and the backend is selected.
  QTimer *ptimWelc;           // The HELLO-timeout timer, Connecting only.
  int msBack;                 // Next rung of the drop ladder.
  int msRetry;                // The delay actually armed (jittered), for
                              // the state tests to pin.
  flag fHad;                  // A session has been had; drops take the
                              // ladder, refusals the slow tick.
  flag fWelc;                 // welc holds the current session's WELCOME.
  eph::Welcome welc;          // What WELCOME said: limits and caps bits.
  eph::Capabilities caps;     // Its capability TLVs, decoded (A.3): what
                              // this session may ask for at all.
  QByteArray baDataset;       // WELCOME's datasetId: the answers' identity,
                              // and part of every window cache key (3.7).
  uint32_t dwReqNext;         // The next request id to hand out.
  QMap<uint32_t, EPHREQ> mpReq;
                              // Every request awaiting an answer, by id,
                              // as its exact wire bytes: re-sent verbatim
                              // after a reconnect -- requests are pure
                              // functions (lesson 5), so there is nothing
                              // else to resume. A cast can have several
                              // in flight at once, one per group of
                              // objects that share a flag set (below).
  int cReqSent;               // Requests ever sent, for the suite.
  QString strUrlWelc;         // The address the last WELCOME came from:
                              // a different server's windows are not
                              // this one's answers.
  QString strErr;             // The last refusal or drop's error text.
  QString strSslErr;          // Why the certificate was refused, from
                              // sslErrors; the drop that follows says it.
  byte bProto;                // The session's protocol version, from
                              // WELCOME; what requests are written in.
  flag fTerminal;             // Refused for good (ERROR 7 or 8): no ladder
                              // until the backend is started again.
                              // A version mismatch's server version is
                              // kept here, for the required-server
                              // dialog to show (increment 4).
} esrv;

// Whether the backend is selected and allowed to run. (The old -0n
// network lock is inert now; a selected source is used if it is
// reachable, and a failure is reported when it happens -- section 2.)

static flag FEphSrvOn()
{
  return FSrcChainHead("server");
}


// Turn the setting into the URL to open. The address is the server
// source's url parameter (-bP server.url, or -bW, the same setting): a
// ws:// URL or host:port; an empty setting is localhost on the
// protocol's default port, read from ephproto.h, so the two ends agree
// with zero configuration. The address is read per connect attempt, so
// changing it takes effect on the next (re)connect.

static flag FUrlEphSrv(CONST char *szAddr, QUrl *purl, QString *pstrErr)
{
  QString str = QString::fromUtf8(SzSet(szAddr)).trimmed();
  QUrl url;

  if (str.isEmpty())
    str = "localhost";   // The two ends agree with zero configuration.

  if (str.startsWith("ws://", Qt::CaseInsensitive) ||
      str.startsWith("wss://", Qt::CaseInsensitive))
    url = QUrl(str);
  else
    // A bare host[:port]. The scheme prefix makes QUrl parse the host,
    // the port and IPv6 brackets alike, and means the port default below
    // applies to it exactly as to a ws:// spelling without one.
    url = QUrl("ws://" + str);
  if (!url.isValid() || url.host().isEmpty()) {
    *pstrErr = QString("Not a valid Ephemeris Server address: \"%1\"")
      .arg(str);
    return fFalse;
  }
  // No port: the protocol's own for ws:// and a bare host, HTTPS's for
  // wss://, where a public server sits on 443 so it gets through the
  // firewalls and proxies that only pass web traffic. It used to be the
  // protocol's port for both, which no public wss:// server would use
  // (EPHEMERIS_SERVER_PRODUCTION_PLAN.md G12).
  if (url.port() < 0)
    url.setPort(url.scheme().compare("wss", Qt::CaseInsensitive) == 0 ? 443 :
      eph::kDefaultPort);
  *purl = url;
  return fTrue;
}


// One rung of the ladder, jittered a bit either side so a room full of
// clients that all lost the server at the same moment do not all knock on
// the same second.

static int MsEphSrvRetry(int ms);

static void EphSrvConnect();
static flag FSrvWaitQt(CONST std::function<flag()> &fDone, int msMax);
void SwissEnsurePath();
void ShutdownQt();

static int MsEphSrvRetry(int ms)
{
  int j = ms / 8;

  if (j > 0)
    ms += QRandomGenerator::global()->bounded(2*j) - j;
  return ms;
}


// Tear the socket down and arm the next attempt. Kept in one place
// because every way out of Connecting and Welcomed comes through it:
// refusal, version mismatch, a bad frame, a dropped connection.

static void SrvWaitWakeQt();

// Let a socket go: its signals disconnected first, so nothing it still
// emits on its way out reaches handlers that now mean another socket
// (EPHEMERIS_REVIEW.md C12).
static void EphSrvDiscardSocket(QWebSocket *pws)
{
  if (pws == NULL)
    return;
  QObject::disconnect(pws, NULL, NULL, NULL);
  pws->abort();
  pws->deleteLater();
}

static void EphSrvDropped(CONST QString &strErr)
{
  int ms;

  if (esrv.pws != NULL) {
    EphSrvDiscardSocket(esrv.pws);
    esrv.pws = NULL;
  }
  SrvWaitWakeQt();   // A cast waiting on this connection stops waiting.
  if (esrv.ptimWelc != NULL)
    esrv.ptimWelc->stop();
  esrv.est = esDisconnected;
  esrv.fWelc = fFalse;     // The limits were the session's; a new one
                           // re-reads them from its own WELCOME.
  if (!strErr.isEmpty() && !esrv.fTerminal)
    esrv.strErr = strErr;
  if (esrv.fTerminal)
    return;   // Refused for good; the text above says why. No retry.
  // mpReq is deliberately kept: every request in flight is re-sent
  // verbatim when the next WELCOME arrives.
  ms = esrv.fHad ? esrv.msBack : msEphSrvRetry;
  if (esrv.fHad)
    esrv.msBack = Min(msEphSrvBackoffMax, esrv.msBack * 2);
  esrv.msRetry = MsEphSrvRetry(ms);
  if (esrv.ptim == NULL) {
    esrv.ptim = new QTimer();
    esrv.ptim->setSingleShot(fTrue);
    QObject::connect(esrv.ptim, &QTimer::timeout, esrv.ptim, []() {
      // Not connected while deselected, and never twice at once. The
      // address is re-read by EphSrvConnect(), so a setting changed
      // since the last attempt takes effect here.
      if (FEphSrvOn() && esrv.est == esDisconnected && esrv.pws == NULL)
        EphSrvConnect();
    });
  }
  esrv.ptim->start(esrv.msRetry);
}


// The window cache's two entry points the message handler needs (defined
// with the cache below): a DATA chunk lands in the window that asked, an
// ERROR settles it as failed.
static flag FWindowChunkQt(uint32_t dwReq, CONST byte *rgb, uint32_t cb);
static void ResetWindowChunksQt(uint32_t dwReq);
static void WindowFailedQt(uint32_t dwReq, CONST char *szErr);
static void ClearWindowsSrvQt();
static void ForgetMissedCastSrvQt();
static flag FSrvObjQuietQt(int obj);
extern flag s_fSrvStarQt;
static void ClearSettledWindowsSrvQt();
static void SrvWelcomedQt();

// One message off the wire. What is not understood is answered by closing
// the connection: a peer that cannot frame the protocol is of no use, and
// the ladder brings the connection back to a server that can.

static void EphSrvMessage(CONST QByteArray &ba)
{
  CONST byte *rgb = (CONST byte *)ba.constData();
  size_t cb = (size_t)ba.size();
  eph::Envelope env;
  std::string strWhy;
  QString strErr;

  if (eph::ParseEnvelope(rgb, cb, &env, &strWhy) != eph::kOk)
    goto LBad;
  if (env.version < eph::kProtoMin) {
    // A server too old to speak version 4 answers in its own layout (3.3),
    // which is the only thing this client can read from it. Not terminal:
    // a server can be upgraded under a running client, so the ladder goes
    // on, with the refusal's text kept for the status line.
    eph::LegacyError le;
    esrv.strErr = eph::ParseLegacyError(rgb + eph::kEnvelopeSize, env.payloadLen, &le, &strWhy)
      == eph::kOk ?
      QString("The Ephemeris Server speaks protocol %1, older than this client's %2: %3")
        .arg(env.version).arg(eph::kProtoVersion)
        .arg(QString::fromUtf8(le.text.c_str())) :
      QString("The Ephemeris Server answered in protocol %1, which this client "
        "cannot read.").arg(env.version);
    esrv.fWelc = fFalse;
    if (esrv.pws != NULL)
      esrv.pws->close();
    return;
  }
  if (eph::CheckRequestId(env, &strWhy) != eph::kOk)
    goto LBad;
  {
  CONST byte *pl = rgb + eph::kEnvelopeSize;
  size_t cbPl = env.payloadLen;
  switch (env.type) {
  case eph::kMsgWelcome:
    // One WELCOME per connection, in Connecting only.
    if (esrv.est != esConnecting ||
      eph::ParseWelcome(pl, cbPl, &esrv.welc, &strWhy) != eph::kOk ||
      eph::ParseCapabilities(esrv.welc.caps_, &esrv.caps, &strWhy) != eph::kOk)
      goto LBad;
    if (esrv.welc.protoSession != eph::kProtoVersion) {
      // A server outside the versions this client speaks: behave as
      // connection refused, the retry cycle continues -- a server can be
      // upgraded under a running client -- and the server's version text
      // is retained so the mismatch is diagnosable. (The other way round,
      // a client too old for the server, is ERROR 8 and final; below.)
      esrv.strErr = QString("The Ephemeris Server reports version \"%1\", "
        "which speaks protocol %2; this client speaks protocol %3.")
        .arg(esrv.welc.serverName.c_str())
        .arg(esrv.welc.protoSession).arg(eph::kProtoVersion);
      esrv.fWelc = fFalse;
      esrv.pws->close();
      return;
    }
    esrv.est = esWelcomed;
    esrv.bProto = (byte)esrv.welc.protoSession;
    esrv.fWelc = fTrue;
    esrv.fHad = fTrue;
    esrv.msBack = msEphSrvBackoff;  // A session resets the ladder.
    if (esrv.ptimWelc != NULL)
      esrv.ptimWelc->stop();
    {
      // A different server -- the address changed -- has its own files, and
      // so has the same server with a new dataset (3.7: datasetId changes
      // whenever an answer could), so what was answered before is not this
      // session's answer.
      QString strUrl = esrv.pws != NULL ? esrv.pws->requestUrl().toString() :
        QString();
      QByteArray baDataset(esrv.welc.datasetId.data(),
        (int)esrv.welc.datasetId.size());
      if ((!esrv.strUrlWelc.isEmpty() && strUrl != esrv.strUrlWelc) ||
        (!esrv.baDataset.isEmpty() && baDataset != esrv.baDataset)) {
        // Only the settled windows: a request still in flight is a pure
        // function, and this server's answer to it is as good as the last
        // one's would have been.
        ClearSettledWindowsSrvQt();
      }
      esrv.strUrlWelc = strUrl;
      esrv.baDataset = baDataset;
    }
    // Every in-flight request is a pure function; re-send each verbatim,
    // unless it has already been re-sent into cEphSrvResendMax sessions
    // that all ended before it was answered.
    {
      QList<uint32_t> rgdwGiveUp;
      for (QMap<uint32_t, EPHREQ>::iterator it = esrv.mpReq.begin();
        it != esrv.mpReq.end(); ++it) {
        if (++it.value().cResend > cEphSrvResendMax) {
          rgdwGiveUp.append(it.key());
        } else if (esrv.pws != NULL) {
          // Its answer starts again from chunk 0 (3.4: a request's chunks
          // ascend, and this is a new answer to the same question), so the
          // window expects chunk 0 again. The rows it already has are kept
          // and de-duplicated as they arrive (EPHEMERIS_REVIEW.md C5).
          ResetWindowChunksQt(it.key());
          esrv.pws->sendBinaryMessage(it.value().ba);
        }
      }
      for (uint32_t dw : rgdwGiveUp) {
        esrv.mpReq.remove(dw);
        WindowFailedQt(dw, "the connection to the Ephemeris Server was lost "
          "every time this request was sent");
      }
    }
    SrvWaitWakeQt();   // A cast waiting to be connected can send now.
    SrvWelcomedQt();
    break;
  case eph::kMsgData:
    // The request id says which window the chunk fills. A chunk for no
    // window -- one evicted or cancelled while its answer was in flight,
    // or a stray -- is dropped; a chunk the window cannot read is a peer we
    // cannot read, and settles the window as failed on the way out.
    if (!FWindowChunkQt(env.requestId, pl, (uint32_t)cbPl))
      goto LBad;
    break;
  case eph::kMsgError: {
    // A whole-request error settles the window it answers as failed, with
    // the text, which is also kept for the same reason a version
    // mismatch's is.
    eph::Error err;
    if (eph::ParseError(pl, cbPl, &err, &strWhy) != eph::kOk)
      goto LBad;
    esrv.mpReq.remove(env.requestId);
    esrv.strErr = QString::fromUtf8(err.text.c_str());
    // This client is too old for the server (8), or the server wants a
    // token this client did not give (7). Neither gets better by asking
    // again, so no ladder: the text says what to do, and starting the
    // backend again (a new address, a restart) clears it.
    if (err.code == eph::kErrVersion || err.code == eph::kErrToken) {
      esrv.fTerminal = fTrue;
      esrv.strErr = QString("The Ephemeris Server refused this Astrolog: %1")
        .arg(QString::fromUtf8(err.text.c_str()));
      if (esrv.pws != NULL)
        esrv.pws->close();
      break;
    }
    // ERROR 10 answers a CANCEL this client sent when it dropped a window
    // it no longer needs: nothing is waiting for it.
    if (err.code == eph::kErrCancelled)
      break;
    WindowFailedQt(env.requestId, err.text.c_str());
    break;
  }
  case eph::kMsgPing:
    // App-level ping (lesson 4: the protocol-level ones never reach here,
    // QWebSocket answers them itself). Empty payload, requestId 0 (3.2).
    if (esrv.pws != NULL) {
      std::vector<uint8_t> msg;
      eph::WriteEnvelope(&msg, eph::kMsgPong, 0, 0, esrv.bProto != 0 ?
        esrv.bProto : eph::kProtoVersion);
      esrv.pws->sendBinaryMessage(QByteArray((CONST char *)msg.data(),
        (int)msg.size()));
    }
    break;
  case eph::kMsgPong:
    break;
  default:
    goto LBad;
  }
  }
  return;

LBad:
  strErr = QString("The Ephemeris Server sent a message this client could not "
    "read (%1); closing the connection.").arg(QString::fromStdString(strWhy));
  if (esrv.pws != NULL)
    esrv.pws->close();
  esrv.strErr = strErr;
}


// Open a connection, synchronously in the sense that everything up to the
// socket is done here; WELCOME arrives by signal.

static void EphSrvConnect()
{
  QUrl url;
  QString strErr;

  if (esrv.pws != NULL)
    return;      // Already Connecting or Welcomed.
  if (!FUrlEphSrv(us.rgszEphParam[epServerUrl], &url, &strErr)) {
    // A bad setting is a refusal like any other: keep its text and let
    // the ladder retry, so fixing the setting is enough.
    EphSrvDropped(strErr);
    return;
  }
  esrv.est = esConnecting;
  // The HELLO timeout: a peer that takes the upgrade and never WELCOMEs --
  // a hung server, or some other WebSocket service on the port -- is a
  // refusal. The timer was declared, guarded at every use and never
  // created, so such a peer held the client in Connecting until restart
  // (EPHEMERIS_REVIEW.md C4).
  if (esrv.ptimWelc == NULL) {
    esrv.ptimWelc = new QTimer();
    esrv.ptimWelc->setSingleShot(fTrue);
    QObject::connect(esrv.ptimWelc, &QTimer::timeout, esrv.ptimWelc, []() {
      if (esrv.est == esConnecting)
        EphSrvDropped(QString("The Ephemeris Server at %1 accepted the "
          "connection and did not answer its greeting in %2 seconds.")
          .arg(esrv.pws != NULL ? esrv.pws->requestUrl().toString() :
          QString()).arg(s_msEphSrvHelloQt / 1000.0));
    });
  }
  // A wss:// address with no TLS in this Qt says so, rather than failing a
  // handshake with a message about sockets. On Linux Qt 5 loads OpenSSL at
  // run time, so a build can lack it where the machine does.
  if (url.scheme().compare("wss", Qt::CaseInsensitive) == 0 &&
    !QSslSocket::supportsSsl()) {
    EphSrvDropped(QString("Can't open %1: this Astrolog has no TLS support "
      "(Qt found no SSL library; on Linux, install OpenSSL).")
      .arg(url.toString()));
    return;
  }
  esrv.strSslErr.clear();
  // Every handler below is about the socket that connected it, and does
  // nothing once that socket has been replaced.
  QWebSocket *pws = new QWebSocket(QString("%1/%2").arg(szAppName)
    .arg(szVersionCore), QWebSocketProtocol::VersionLatest, NULL);
  esrv.pws = pws;
  QObject::connect(pws, &QWebSocket::connected, pws, [pws]() {
    eph::Hello hello;
    std::vector<uint8_t> pay, msg;

    if (pws != esrv.pws)
      return;
    // Say hello first; WELCOME comes back by signal. The versions this
    // client speaks, the capabilities it can use, and the -bT token when
    // one is set (a server that requires one and gets none, or an unknown
    // one, refuses with ERROR 7, final above).
    hello.protoMax = hello.protoMin = eph::kProtoVersion;
    hello.caps = eph::kCapF32 | eph::kCapCancel | eph::kCapInstantLists |
      eph::kCapPriority;
    hello.clientName = std::string(szAppName) + " " + szVersionCore;
    hello.token = SzSet(us.rgszEphParam[epServerToken]);
    eph::EncodeHello(&pay, hello);
    eph::WriteEnvelope(&msg, eph::kMsgHello, 0, pay.size());
    msg.insert(msg.end(), pay.begin(), pay.end());
    pws->sendBinaryMessage(QByteArray((CONST char *)msg.data(),
      (int)msg.size()));
  });
  QObject::connect(pws, &QWebSocket::disconnected, pws, [pws]() {
    // Every way out of Connecting and Welcomed lands here. Say which
    // thing went wrong when the socket itself has an opinion; the
    // message handler's refusals have already said theirs.
    QString strErr;
    if (pws != esrv.pws)
      return;
    if (!esrv.strSslErr.isEmpty())
      strErr = esrv.strSslErr;
    else if (pws->error() != QAbstractSocket::UnknownSocketError)
      strErr = QString("Couldn't reach the Ephemeris Server at %1: %2.")
        .arg(pws->requestUrl().toString())
        .arg(pws->errorString());
    EphSrvDropped(strErr);
  });
  // A certificate the client will not trust: each reason in Qt's words,
  // kept for the drop that follows. The errors are never ignored -- a
  // client that connected anyway would send birth times and places to
  // whoever answered.
  QObject::connect(pws, &QWebSocket::sslErrors, pws,
    [pws](CONST QList<QSslError> &rgerr) {
      QStringList rgstr;
      if (pws != esrv.pws)
        return;
      for (CONST QSslError &err : rgerr)
        rgstr << err.errorString();
      esrv.strSslErr = QString("The Ephemeris Server at %1 presented a "
        "certificate this computer does not trust: %2.")
        .arg(pws->requestUrl().toString()).arg(rgstr.join("; "));
    });
  QObject::connect(pws, &QWebSocket::binaryMessageReceived, pws,
    [pws](CONST QByteArray &ba) {
      if (pws == esrv.pws)
        EphSrvMessage(ba);
    });
  QObject::connect(pws, &QWebSocket::textMessageReceived, pws,
    [pws](CONST QString &str) {
      if (pws != esrv.pws)
        return;
      // The protocol is binary; a peer speaking text is a peer we cannot
      // read, and the same close-and-retry answers it.
      esrv.strErr = "The Ephemeris Server sent a text message where the "
        "protocol speaks binary; closing the connection.";
      pws->close();
    });
  // Timed from the attempt, not from the upgrade: a server that never
  // completes the handshake is as dead as one that never WELCOMEs.
  esrv.ptimWelc->start(s_msEphSrvHelloQt);
  pws->open(url);
}


// Issue one REQUEST. Requires a Welcomed connection; clamps the request
// to what WELCOME promised first (lesson 5: the caps govern the session).
// Returns fFalse when there is nothing to send it on -- callers fail soft
// (plan §8); increment 2's facade waits bounded and cancelable on the
// answer instead.

void ClampEphSrvReqQt(eph::Request *preq)
{
  eph::Welcome w;   // The protocol's own limits, before any WELCOME.
  uint32_t dwObjs = esrv.fWelc ? esrv.welc.maxObjs : w.maxObjs;
  uint32_t dwRows = esrv.fWelc ? esrv.welc.maxRows : w.maxRows;
  uint32_t dwChunk = esrv.fWelc ? esrv.welc.maxChunkRows : w.maxChunkRows;
  uint32_t dwCells = esrv.fWelc ? esrv.welc.maxCells : w.maxCells;
  uint32_t dwCaps = esrv.fWelc ? esrv.welc.caps : 0;

  // Ask only for correction terms this observer LISTS. 3.5a makes an
  // unlisted mask ERROR 11 -- the whole request, not one object -- and
  // Astrolog sends the full mask for every cast that is not true-position,
  // including heliocentric ones. That worked against astrolog-ephd only
  // because it happened to be lenient about what it accepted; against
  // Prometheia's server the same cast was refused outright, which the
  // cross-test found the day astrolog-ephd stopped advertising what it
  // could not deliver. Narrowing here is right whatever a server accepts:
  // the terms being dropped are ones that observer would not have applied.
  if (esrv.fWelc)
    for (size_t iP = 0; iP < preq->profiles.size(); iP++) {
      eph::Profile &pf = preq->profiles[iP];
      if (!esrv.caps.CorrectionMask(pf.observer, pf.corrections))
        pf.corrections =
          NBestCorrMaskEph(esrv.caps, pf.observer, pf.corrections);
    }
  if (preq->objs.size() > (size_t)dwObjs)
    preq->objs.resize(dwObjs);  // The server would refuse the whole
                                // request; SrvPrefetchQt splits a cast into
                                // requests this size, so this is the floor
                                // under a hand-built one.
  preq->nTime = Min(preq->nTime, dwRows);
  // The work bound WELCOME advertises (3.5): a request past objects x rows
  // is refused whole, so the rows are cut before it is sent
  // (EPHEMERIS_REVIEW.md S4). One row is always kept: the server's own
  // nTime == 0 is a refusal, and an over-cap object count on a tiny bound
  // is the server's ERROR 2 to fail soft on.
  if ((uint64_t)preq->objs.size() * preq->nTime > dwCells) {
    uint32_t c = dwCells / (uint32_t)preq->objs.size();
    preq->nTime = c < 1 ? 1 : c;
  }
  if (preq->nTime < 2)
    preq->stepNs = 0;           // 3.5: one row carries no step.
  preq->chunkRows = Min(preq->chunkRows, dwChunk);
  // Float32 is chosen for animation windows only if the caps bit says the
  // server supports it; f64 is always legal.
  if (preq->precision == eph::kPrecF32 && (dwCaps & eph::kCapF32) == 0)
    preq->precision = eph::kPrecF64;
  if (preq->priority != 0 && (dwCaps & eph::kCapPriority) == 0)
    preq->priority = 0;         // Not advertised: never sent (3.4).
}

// The bytes of one message: the envelope of the session's version, then the
// payload.
static QByteArray BaMsgEphSrvQt(uint16_t wType, uint32_t dwReq,
  CONST std::vector<uint8_t> &pay)
{
  std::vector<uint8_t> msg;

  eph::WriteEnvelope(&msg, wType, dwReq, pay.size(),
    esrv.bProto != 0 ? esrv.bProto : eph::kProtoVersion);
  msg.insert(msg.end(), pay.begin(), pay.end());
  return QByteArray((CONST char *)msg.data(), (int)msg.size());
}

static uint32_t DwSendEphSrvQt(eph::Request *preq)
{
  std::vector<uint8_t> pay;
  uint32_t dwReq;

  if (esrv.est != esWelcomed || esrv.pws == NULL)
    return 0;
  ClampEphSrvReqQt(preq);
  eph::EncodeRequest(&pay, *preq);
  dwReq = ++esrv.dwReqNext;
  if (dwReq == 0)   // Wrapped; 0 means "none" everywhere, and 3.2 reserves
    dwReq = ++esrv.dwReqNext;   // it for connection-level messages.
  esrv.mpReq[dwReq].ba = BaMsgEphSrvQt(eph::kMsgRequest, dwReq, pay);
  esrv.mpReq[dwReq].cResend = 0;
  esrv.cReqSent++;
  esrv.pws->sendBinaryMessage(esrv.mpReq[dwReq].ba);
  return dwReq;
}

// A window this client no longer needs, while its answer is still coming:
// CANCEL (3.4) tells the server to drop the chunks it has not sent, rather
// than spend the connection on rows nobody will read. The answer is
// ERROR 10, which reaches no window -- this one is already gone.
static void CancelEphSrvQt(uint32_t dwReq)
{
  if (dwReq == 0 || esrv.est != esWelcomed || esrv.pws == NULL ||
    !esrv.fWelc || (esrv.welc.caps & eph::kCapCancel) == 0)
    return;
  esrv.pws->sendBinaryMessage(BaMsgEphSrvQt(eph::kMsgCancel, dwReq,
    std::vector<uint8_t>()));
}

flag FSendEphSrvQt(eph::Request *preq)
{
  return DwSendEphSrvQt(preq) != 0;
}



// The startup path's hook, called from BeginQt() -- and, mid-session,
// from a cast that found the connection down. It never blocks and never
// shows anything.
//
// It used to. With no local ephemeris and the server selected, the server
// was the only source there was, so startup put up a modal "Connecting to
// cloud ephemeris" dialog and climbed a retry ladder for an hour, and a
// give-up printed to stderr and exited EXIT_NO_EPHEMERIS. All of that was
// for a world where a selection was a single backend and an unreachable
// one left nothing to cast from.
//
// The fallback chain ended that world (section 4.1). A selected source
// that cannot answer is a source that did not answer: the walk offers
// each object to the next source in the chain, and a failure is reported
// WHERE IT HAPPENS -- once per cast, with the engine's own words -- which
// is both more accurate than a startup modal and available to a run that
// never had a window. A chart with no source at all still says so,
// through the same path a missing ephemeris file has always used.

void EphSrvStartupQt()
{
  if (!FEphSrvOn() || esrv.est != esDisconnected || esrv.pws != NULL ||
    (esrv.ptim != NULL && esrv.ptim->isActive()))
    return;
  EphSrvConnect();
}


// One quiet status line for the About dialog (plan §7): the address the
// connector reads, the state, and the server's version once a WELCOME
// has said it. Empty when the backend is not the selected one; a dialog
// is the only place it shows, because background mode has no status
// noise (plan §4).

void SzEphSrvStatusQt(char *sz, int cch)
{
  char szAddr[cchSzMax];

  sz[0] = chNull;
  if (!FSrcChainHead("server"))
    return;
  // FSzSet, not SzSet: SzSet() hands back "" for a null, and "" is a
  // perfectly true pointer -- so this test was always taken, the default
  // below was unreachable, and the raw null went to "%s". The status line
  // read "Ephemeris Server (null)" for every user who had not set an
  // address, which is exactly the user the default exists for.
  if (FSzSet(us.rgszEphParam[epServerUrl]))
    sprintf2(S(szAddr), "%s", us.rgszEphParam[epServerUrl]);
  else
    sprintf2(S(szAddr), "localhost:%d", eph::kDefaultPort);
  if (esrv.est == esWelcomed && esrv.fWelc)
    snprintf(sz, cch, "Ephemeris Server %s: online, %s", szAddr,
      esrv.welc.serverName.c_str());
  else if (esrv.est == esConnecting)
    snprintf(sz, cch, "Ephemeris Server %s: connecting", szAddr);
  else if (esrv.ptim != NULL && esrv.ptim->isActive())
    snprintf(sz, cch, "Ephemeris Server %s: retry in %ds", szAddr,
      (esrv.ptim->remainingTime() + 999) / 1000);
  else
    snprintf(sz, cch, "Ephemeris Server %s: not connected", szAddr);
}


// Program exit: the socket and the timers go before the application does,
// the way every other Qt object here is torn down in FinalizeQt().

// ---------------------------------------------------------------------
// The Qt transport (section 4.2, phase 6c).
//
// The registry's server source has no idea any of the above exists: it
// asks whatever transport registered itself (ephem.h, EPHTRANS). This is
// that table for the Qt build, and it is thin on purpose -- every one of
// these delegates to the adapter the Ephemeris Server client already is,
// so the chain reaches the SAME connection, window cache, retry ladder
// and prefetch that the pre-registry path reached, rather than a second
// client that would drift from it.

static flag FAvailTransQt(char *szWhy, int cch)
{
  QUrl url;
  QString strErr;

  if (!FUrlEphSrv(SzSet(us.rgszEphParam[epServerUrl]), &url, &strErr)) {
    if (szWhy != NULL)
      sprintf2(szWhy, cch, "%s",
        strErr.isEmpty() ? "the server address is not usable" :
        strErr.toLocal8Bit().constData());
    return fFalse;
  }
  return fTrue;
}


static int NStateTransQt(char *sz, int cch)
{
  if (sz != NULL)
    SzEphSrvStatusQt(sz, cch);
  return esrv.est == esWelcomed ? esReady :
    esrv.est == esConnecting ? esConnecting : esFailed;
}


static void StartTransQt()
{
  EphSrvStartupQt();
}


static void StopTransQt()
{
  EphSrvFinalizeQt();
}


static flag FSubmitTransQt(CONST EPHQUERY *pq)
{
  int i;

  if (pq == NULL || pq->cobj <= 0)
    return fFalse;
  // This adapter addresses its plan by Astrolog object index, so it can
  // only answer objects that HAVE one. A side call does not always: an
  // asteroid is SE_AST_OFFSET + n (over 10000) and a star is its
  // catalogue number. Refusing the whole query -- rather than serving
  // part of it -- is what the chain wants: false means "attempted
  // nothing", every object stays open, and the Swiss files behind this
  // source answer them, which is where those side calls were always
  // served from.
  // Only the objects still OPEN. ephem.h states the contract -- "FSubmit
  // skips marked objects, so no source is asked twice" -- and ignoring it
  // meant a chain of "swiss,server" translated, sent and waited on
  // objects swiss had already answered, and let ONE already-answered
  // out-of-range object refuse the whole query (review M2).
  for (i = 0; i < pq->cobj; i++)
    if (pq->rgisrc[i] == ephSrcNone &&
      !FBetween(pq->rgobj[i], 0, objMax-1))
      return fFalse;
  // One submit for the whole query, which is what a remote source needs:
  // a per-object fetch is a round trip per body (EPHEMERIS_CLIENT_PLAN.md
  // lesson 1). The plan it leaves is read back per object below.
  // The wait below scans oEar..imax for settled windows, so imax must
  // cover the objects THIS QUERY names, not the main cast's range. A
  // star sits at oNorm + n, above oNorm, so passing oNorm left every
  // star request unwaited: the scan found nothing outstanding, returned
  // at once, and each star then failed with "the Ephemeris Server did
  // not answer in time" -- a client bug reported as a server timeout,
  // one modal and one wasted REQUEST per cast (phase 8 review, D4).
  {
    int iMax = oNorm, iQ;

    for (iQ = 0; iQ < pq->cobj; iQ++)
      if (pq->rgobj[iQ] > iMax)
        iMax = pq->rgobj[iQ];
    SrvPrefetchQt((pq->rJD - 2415020.0) / 36525.0, oEar, iMax, pq);
  }
  return fTrue;
}


static flag FReadTransQt(CONST EPHQUERY *pq, int iObj, EPHROW *prow)
{
  real r1, r2, r3, r4, r5, r6;
  int obj;
  flag fRet;

  if (pq == NULL || iObj < 0 || iObj >= pq->cobj)
    return fFalse;
  obj = pq->rgobj[iObj];
  // An object the REQUEST never carried is not a failure to report: the
  // translation has no version 4 form for it (ephreq.h skips those), so
  // this source simply cannot do it and the chain walk offers it to the
  // next one. Saying so quietly is what every other source does for an
  // object it cannot compute.
  //
  // Without this the adapter's own "the Ephemeris Server has no answer"
  // warning fired for each of them -- once per cast, as a modal. It did
  // not show up under "-Yi1 ephem", where few exotic bodies resolve, and
  // did under "-i nrvate.as", which is the configuration CLAUDE.md's
  // hard rule names and which resolves far more of them from /swe.
  if (FSrvObjQuietQt(obj))
    return fFalse;
  // D2 made the server compute the star with the chart's own settings;
  // this is the consumer half of the same contract, which that fix left
  // behind. Without it a sidereal chart put every fixed star about 24.7
  // degrees from where the local files put it, with nErr clear, so the
  // chain claimed the row and never fell back (phase 8, third review).
  s_fSrvStarQt = (pq->rgszName[iObj] != NULL);
  fRet = FSrvPlanetQt(pq->rgobj[iObj], pq->rJD, &r1, &r2, &r3, &r4, &r5, &r6);
  s_fSrvStarQt = fFalse;
  if (!fRet)
    return fFalse;
  // FSrvPlanetQt() answers in FSwissPlanet()'s argument order; EPHROW
  // carries the protocol's. The one transposition is ephem.h's, and this
  // is the same swap ephswiss.cpp makes for the local sources.
  prow->rg[0] = r1;   // longitude
  prow->rg[1] = r2;   // latitude
  prow->rg[2] = r4;   // distance
  prow->rg[3] = r3;   // longitude rate
  prow->rg[4] = r5;   // latitude rate
  prow->rg[5] = r6;   // distance rate
  prow->nErr = ephErrNone;
  prow->nNativeRes = ephNativeNone;
  prow->fApprox = fFalse;
  prow->szSrc = "server";
  return fTrue;
}


static CONST EPHTRANS ephtransQt = {
  "Qt WebSocket", FAvailTransQt, NStateTransQt, StartTransQt, StopTransQt,
  FSubmitTransQt, FReadTransQt
};


void EphSrvTransportBindQt()
{
  EphSrvTransportSet(&ephtransQt);
}


void EphSrvFinalizeQt()
{
  ClearWindowsSrvQt();
  ForgetMissedCastSrvQt();
  esrv.fTerminal = fFalse;   // Starting again may meet a different server.
  esrv.bProto = 0;
  if (esrv.ptim != NULL) {
    esrv.ptim->stop();
    delete esrv.ptim;
    esrv.ptim = NULL;
  }
  if (esrv.ptimWelc != NULL) {
    esrv.ptimWelc->stop();
    delete esrv.ptimWelc;
    esrv.ptimWelc = NULL;
  }
  if (esrv.pws != NULL) {
    EphSrvDiscardSocket(esrv.pws);
    esrv.pws = NULL;
  }
  esrv.est = esDisconnected;
  esrv.strUrlWelc = QString();
  esrv.fWelc = esrv.fHad = fFalse;
  esrv.msBack = msEphSrvBackoff;
  esrv.dwReqNext = 0;
  esrv.mpReq.clear();
  esrv.strErr = QString();
}


/*
******************************************************************************
** Ephemeris server state hooks, for the test suite in qttest.cpp.
******************************************************************************
*/

int NEphSrvStateTestQt() { return esrv.est; }
flag FTerminalEphSrvTestQt() { return esrv.fTerminal; }
int NBackoffEphSrvTestQt() { return esrv.msBack; }
void SetBackoffEphSrvTestQt(int ms) { esrv.msBack = ms; }
int NRetryEphSrvTestQt() { return esrv.ptim != NULL && esrv.ptim->isActive() ?
  esrv.msRetry : -1; }
flag FWelcEphSrvTestQt() { return esrv.fWelc; }
CONST eph::Welcome *PwelcEphSrvTestQt() { return &esrv.welc; }
uint32_t DwReqEphSrvTestQt() { return esrv.mpReq.isEmpty() ? 0 :
  esrv.mpReq.lastKey(); }
int CReqSentEphSrvTestQt() { return esrv.cReqSent; }
flag FErrEphSrvTestQt(char *sz, int cchMax) {
  QByteArray ba = esrv.strErr.toLocal8Bit();
  CopyRgchToSz(ba.constData(), CchSz(ba.constData())+1, sz, cchMax);
  return !esrv.strErr.isEmpty();
}
flag FUrlEphSrvTestQt(CONST char *sz, char *szOut, int cchMax) {
  QUrl url;
  QString strErr;
  if (!FUrlEphSrv(sz, &url, &strErr))
    return fFalse;
  QByteArray ba = url.toString().toLocal8Bit();
  CopyRgchToSz(ba.constData(), CchSz(ba.constData())+1, szOut, cchMax);
  return fTrue;
}

/*
******************************************************************************
** Ephemeris server: the window cache, the cast plan, and the facade.
******************************************************************************
*/

// A window is one REQUEST's answer: nObj objects over nTime rows from a
// TT instant, the columns swe_calc() fills, held here so a cast reads per
// object from memory (EPHEMERIS_CLIENT_PLAN.md section 6). A chart cast is a
// one-row f64 window at its own instant, read exactly; an animation frame
// reads a wide f32 window on the animation's grid, nearest row corrected by
// the speed columns (see SrvPrefetchQt()). Filled
// asynchronously by the message handler as the DATA chunks arrive; a cast
// waits on it, bounded, in SrvPrefetchQt().

typedef struct _EphWindow {
  QByteArray key;         // The canonical request plus its precision.
  QByteArray keyShape;    // The same with the rows left out (the grid's
                          // start, step count and delta T): what an
                          // animation frame looks a covering window up by.
  flag fAnim;             // An animation window: read at the nearest row,
                          // speed-corrected. Never matched by a chart cast.
  eph::Request req;       // What was asked, for the row grid and the
                          // object list the columns are laid out by.
  uint32_t dwReq;         // In flight under this id; 0 once settled.
  flag fDone;             // Every row landed.
  flag fFailed;           // A whole-request ERROR, or a chunk unreadable.
  QByteArray baErr;       // Its text.
  uint32_t cRowsGot;       // Distinct rows landed: a chunk that arrives
                           // twice -- re-sent after a reconnect -- counts
                           // once (EPHEMERIS_REVIEW.md C5).
  QBitArray rgfRow;        // Which rows have landed.
  QVector<double> rgcol;  // Object-major nObj*nTime*6, f64 whatever the
                          // wire carried.
  std::vector<eph::Meta> rgmeta;  // Per object: rowsOk, the error and its
                          // text, the flags -- chunk 0's metadata (3.4).
  uint32_t chunkNext;     // The chunk index expected next: they are
                          // contiguous and ascending (3.4).
  flag fMeta;             // rgmeta taken from chunk 0.
} EPHWINDOW;

// The columns a window holds: the six a cast reads (3.5's base six).
#define cColWindowSrvQt 6

#define cWindowSrvQt 32         // Windows kept, most recently used first,
#define cbWindowSrvMax (64 << 20)   // and at most this many bytes of them.
                                // Eight windows thrashed once a chart
                                // had five groups, or a relationship
                                // chart three: a frame needs its window
                                // and the next one per group, for each
                                // chart (EPHEMERIS_REVIEW.md A5). A
                                // 30-body 1000-row window is 1.4 MB.
#define msEphSrvWait 10000      // A cast waits this long for its windows.

// Rows in an animation window. Sized against the server bench
// (EPHEMERIS_SERVER_PLAN.md section 9): a cold 30-body 1000-row window is
// ~0.65 s of server compute, which is the one stall an animation starting
// on cold data sees, and the lead the next window has is half a window --
// 1000 frames at the default 100 ms delay is 100 s, so 50 s of lead against
// 0.65 s of work. The plan's first sketch said 2000; that doubles the stall
// for lead nobody needs.
#define cRowsAnimSrvQt 1000

// How far past half a row an animation frame may sit and still be read
// from that row, in seconds. The frames of a uniform rate land on the grid
// in UT, and the grid is in TT, so they drift off it by however much
// delta-t changes across a window -- about two seconds over a 1000-day
// window today, more in antiquity. The speed correction is first order,
// and a minute of it costs the Moon about 2e-7 degrees.
#define rSlackAnimSrvQt 60.0

static QList<EPHWINDOW *> s_lwinSrvQt;
static int s_cWindowCapQt = cWindowSrvQt;   // Settable by the suite.
static int s_cRowsAnimQt = cRowsAnimSrvQt;  // Settable by the suite.
static int s_msSrvWaitQt = msEphSrvWait;    // Settable by the suite.
static int s_cChunkRowsQt = (int)eph::Welcome().maxChunkRows;   // Settable
                                            // by the suite, to make a
                                            // window arrive in several
                                            // chunks.
static int s_cSrvRecastQt = 0;              // Recasts on WELCOME, counted.
static flag s_fSrvApproxQt = fFalse;        // A cast read an animation row
                                            // since animation last stopped.
static flag FWindowInPlanQt(CONST EPHWINDOW *pwin);

// The window a request id is filling, or NULL.
static EPHWINDOW *PwinByReqQt(uint32_t dwReq)
{
  if (dwReq == 0)
    return NULL;
  for (EPHWINDOW *pwin : s_lwinSrvQt)
    if (pwin->dwReq == dwReq)
      return pwin;
  return NULL;
}

// A request re-sent into a new session is answered from chunk 0 again.
static void ResetWindowChunksQt(uint32_t dwReq)
{
  EPHWINDOW *pwin = PwinByReqQt(dwReq);

  if (pwin != NULL)
    pwin->chunkNext = 0;
}

// The window answering a key, moved to the front (most recently used).
static EPHWINDOW *PwinByKeyQt(CONST QByteArray &key)
{
  for (int i = 0; i < s_lwinSrvQt.size(); i++)
    if (s_lwinSrvQt[i]->key == key) {
      EPHWINDOW *pwin = s_lwinSrvQt.takeAt(i);
      s_lwinSrvQt.prepend(pwin);
      return pwin;
    }
  return NULL;
}

// The cache key: the server's own (3.7) -- this session's datasetId and the
// REQUEST's question block, byte for byte -- plus the precision, which the
// server leaves out because it converts at send and this leaves in because
// a chart must never read an f32 window. Keyed on the datasetId, a window
// cannot outlive the files it was computed from.
static QByteArray KeyWindowQt(CONST eph::Request &req)
{
  std::vector<uint8_t> pay;
  QByteArray ba;

  eph::EncodeRequest(&pay, req);
  ba = esrv.baDataset;
  ba.append((char)1);
  ba.append((CONST char *)pay.data() + req.questionOffset,
    (int)(pay.size() - req.questionOffset));
  ba.append((char)req.precision);
  return ba;
}

// The same with the rows left out (the grid's start and count): what an
// animation frame looks a covering window up by.
static QByteArray KeyShapeWindowQt(CONST eph::Request &req)
{
  eph::Request reqShape = req;

  reqShape.start = eph::Time();
  reqShape.nTime = 1;
  reqShape.stepNs = 0;
  reqShape.deltaTSec = eph::CanonicalNaN();
  return KeyWindowQt(reqShape);
}

// Make a window for a request and send the request, evicting the least
// recently used window past the cap -- an in-flight one included, whose
// answer then lands nowhere -- but never one the cast being planned points
// at: a cast with more groups than the cap holds all of its windows until
// the next cast, rather than reading one freed under it. NULL when not
// connected: the caller's cast fails soft and the connector is prodded.
static EPHWINDOW *PwinOpenQt(eph::Request *preq, flag fAnim)
{
  EPHWINDOW *pwin;
  uint32_t dwReq;
  size_t cObj;
  int i;

  dwReq = DwSendEphSrvQt(preq);   // Clamps preq to WELCOME first.
  if (dwReq == 0)
    return NULL;
  pwin = new EPHWINDOW;
  pwin->key = KeyWindowQt(*preq);
  pwin->keyShape = KeyShapeWindowQt(*preq);
  pwin->fAnim = fAnim;
  pwin->req = *preq;
  pwin->dwReq = dwReq;
  pwin->fDone = pwin->fFailed = pwin->fMeta = fFalse;
  pwin->cRowsGot = 0;
  pwin->chunkNext = 0;
  pwin->rgfRow.fill(false, (int)preq->nTime);
  cObj = preq->objs.size();
  pwin->rgcol.fill(0.0, (int)(cObj * preq->nTime * cColWindowSrvQt));
  pwin->rgmeta.assign(cObj, eph::Meta());
  s_lwinSrvQt.prepend(pwin);
  size_t cbHeld = 0;
  for (EPHWINDOW *pwinT : s_lwinSrvQt)
    cbHeld += (size_t)pwinT->rgcol.size() * sizeof(double);
  for (i = s_lwinSrvQt.size() - 1; i > 0 && (s_lwinSrvQt.size() >
    s_cWindowCapQt || cbHeld > (size_t)cbWindowSrvMax); i--) {
    if (FWindowInPlanQt(s_lwinSrvQt[i]))
      continue;
    cbHeld -= (size_t)s_lwinSrvQt[i]->rgcol.size() * sizeof(double);
    EPHWINDOW *pwinOld = s_lwinSrvQt.takeAt(i);
    if (pwinOld->dwReq != 0) {
      esrv.mpReq.remove(pwinOld->dwReq);
      // Its answer would land nowhere: tell the server to stop sending it.
      CancelEphSrvQt(pwinOld->dwReq);
    }
    delete pwinOld;
  }
  return pwin;
}

// One DATA chunk into its window. The layout is 4.5's: header, nObj
// metadata records, then the values object-major for this chunk's rows,
// f64 or f32 as the header says. Returns fFalse only for a chunk that
// cannot be read, which the caller treats as a peer that cannot be read.
static flag FWindowChunkQt(uint32_t dwReq, CONST byte *rgb, uint32_t cb)
{
  EPHWINDOW *pwin = PwinByReqQt(dwReq);
  eph::DataChunk d;
  std::string strWhy;
  uint32_t r;
  int o, c;

  if (pwin == NULL)
    return fTrue;   // Nobody is waiting for it.
  if (eph::ParseData(rgb, cb, &d, &strWhy) != eph::kOk)
    goto LBad;
  // What the header says must be what was asked for. The precision above
  // all: an f32 chunk for an f64 chart window would quietly not be the bits
  // the local path computes. The chunk index too -- chunks are contiguous
  // and ascending (3.4), and metadata rides on chunk 0, so a chunk out of
  // order is a peer this client cannot follow.
  if (d.nObj != (uint32_t)pwin->req.objs.size() || d.totalRows != pwin->req.nTime ||
    (uint64_t)d.iTime + d.nRows > pwin->req.nTime || d.nRows == 0 ||
    d.precision != pwin->req.precision || d.chunkIndex != pwin->chunkNext ||
    (d.chunkIndex == 0) != ((d.flags & eph::kChunkMeta) != 0))
    goto LBad;
  pwin->chunkNext++;
  if (d.flags & eph::kChunkMeta) {
    pwin->rgmeta = d.meta;
    pwin->fMeta = fTrue;
  }
  {
    // The base six columns are what a cast reads; any extra column the
    // answer carries (this client asks for none) sits past them.
    int cCol = d.Cols();
    for (o = 0; o < (int)d.nObj; o++)
      for (r = 0; r < d.nRows; r++) {
        double *pr = pwin->rgcol.data() +
          ((size_t)o * pwin->req.nTime + d.iTime + r) * cColWindowSrvQt;
        CONST double *px = d.values.data() +
          ((size_t)o * d.nRows + r) * (size_t)cCol;
        for (c = 0; c < (int)cColWindowSrvQt; c++)
          pr[c] = px[c];
      }
  }
  for (r = 0; r < d.nRows; r++)
    if (!pwin->rgfRow.testBit((int)(d.iTime + r))) {
      pwin->rgfRow.setBit((int)(d.iTime + r));
      pwin->cRowsGot++;
    }
  if (pwin->cRowsGot >= pwin->req.nTime) {
    pwin->fDone = fTrue;
    esrv.mpReq.remove(pwin->dwReq);
    pwin->dwReq = 0;
    SrvWaitWakeQt();
  }
  return fTrue;

LBad:
  pwin->fFailed = fTrue;
  pwin->baErr = "the server sent a DATA chunk this client could not read";
  esrv.mpReq.remove(pwin->dwReq);
  pwin->dwReq = 0;
  SrvWaitWakeQt();
  return fFalse;
}

static void WindowFailedQt(uint32_t dwReq, CONST char *szErr)
{
  EPHWINDOW *pwin = PwinByReqQt(dwReq);

  if (pwin == NULL)
    return;
  pwin->fFailed = fTrue;
  pwin->baErr = szErr;
  pwin->dwReq = 0;
  SrvWaitWakeQt();
}

// The cast plan: for the chart being cast, where each object's answer is
// -- which window, which column -- or why there is none. Built by
// SrvPrefetchQt() at the head of ComputeEphem()'s loop, read by
// FSrvPlanetQt() per object inside it.

typedef struct _EphPlanEntry {
  EPHWINDOW *pwin;        // NULL: no window (not connected, or the object
                          // is one the local path cannot compute either).
  int iObj;               // Its column in the window.
  flag fUnsupported;      // FSwissPlanetSpec() said no: silent, like
                          // FSwissPlanet() is for the same objects.
} EPHPLANENTRY;

static struct {
  real jd;                              // The cast this plan is for,
  real jde;                             // and its TT instant, as the
                                        // prefetch made it: the reads use
                                        // this, not a delta-t recomputed
                                        // after anything in between may
                                        // have moved is.rDeltaT
                                        // (EPHEMERIS_REVIEW.md C9).
  EPHPLANENTRY rgent[objMax];
  flag fPrefetched;                     // The prefetch ran for jd.
  QByteArray baErr;                     // Why the cast has no windows, if
                                        // it has none.
} s_plan;

static flag FWindowInPlanQt(CONST EPHWINDOW *pwin)
{
  int i;

  for (i = 0; i < objMax; i++)
    if (s_plan.rgent[i].pwin == pwin)
      return fTrue;
  return fFalse;
}

// The row's instant: the expression the server evaluates for row r (3.5),
// computed by the codec both ends compile.
static real JdWindowRowQt(CONST EPHWINDOW *pwin, uint32_t r)
{
  return pwin->req.RowTime(r).Sum();
}

// The window's grid step in seconds; 0 for a one-row window.
static real RStepWindowSrvQt(CONST EPHWINDOW *pwin)
{
  return (real)pwin->req.stepNs / 1000000000.0;
}

// The row a window answers a TT instant from, and the days from that row's
// instant to it. A chart window answers only the instant it was asked at,
// on the bytes. An animation window answers any instant within half a row
// of a row (plus the delta-t slack), from the nearest row.
static flag FRowWindowQt(CONST EPHWINDOW *pwin, real jde, uint32_t *pr,
  real *pdt)
{
  uint32_t r = 0;
  real rRow;

  real rStep = RStepWindowSrvQt(pwin);

  if (pwin->req.nTime > 1 && rStep > 0.0) {
    rRow = (jde - pwin->req.start.Sum()) * 86400.0 / rStep;
    if (rRow < -0.5 - rSlackAnimSrvQt / rStep ||
      rRow > (real)pwin->req.nTime - 0.5 + rSlackAnimSrvQt / rStep)
      return fFalse;
    rRow = RFloor(rRow + 0.5);
    r = rRow <= 0.0 ? 0 : rRow >= (real)(pwin->req.nTime - 1) ?
      pwin->req.nTime - 1 : (uint32_t)rRow;
  }
  *pr = r;
  *pdt = jde - JdWindowRowQt(pwin, r);
  if (!pwin->fAnim)
    return *pdt == 0.0;
  // Only a frame ON the grid, give or take the delta-t drift. A frame half
  // a row off was read corrected to first order across that half row --
  // arc-minutes for the Moon on a days grid -- and progressed, midpoint,
  // auto-DST and hand-edited charts put frames there routinely
  // (EPHEMERIS_REVIEW.md A4). Off the grid, the prefetch asks exactly.
  return RAbs(*pdt) * 86400.0 <= rSlackAnimSrvQt;
}

// The server row step, in seconds, of the grid the running animation's
// frames land on, and the direction they move along it; 0 when this cast is
// not an animation frame, or its rate is a calendar one -- months and up
// are not a uniform number of seconds, and are cast exactly, one row each.
// A sub-second rate reads a one-second grid, correcting up to half a
// second by the speeds; "now" moves forward a second at a time.
static int NGridAnimSrvQt(int *pnDir)
{
  int n = NAbs(gi.nDir);

  *pnDir = gi.nDir < 0 ? -1 : 1;
  // Only when the chart animation moves is the chart itself. A tick of a
  // relationship, transit or progressed chart casts charts that do not
  // move, or move on instants other than the animation's steps, and they
  // were read from f32 rows too (EPHEMERIS_REVIEW.md A6); those casts are
  // asked exactly, as before animation windows existed. (Comparing ciCore
  // with the animated chart cannot tell them apart: CastChart() cooks
  // ciCore -- the zone folded into the time -- for the length of the cast.)
  if (!s_fAnimFrameQt || PciAnimate() != &ciMain)
    return 0;
  if (n < 1)
    n = 1;
  switch (NAbs(gs.nAnim)) {
  case 1:  return n;           // Seconds.
  case 2:  return 60 * n;      // Minutes.
  case 3:  return 3600 * n;    // Hours.
  case 4:  return 86400 * n;   // Days.
  case iAnimNow: *pnDir = 1; return 1;
  case 11: case 12: case 13: return 1;   // Tenths, hundredths, milliseconds.
  }
  return 0;
}

// The animation window of this shape answering a TT instant, moved to the
// front, or NULL. A failed one is dropped on the way: asked again, as the
// chart path does.
static EPHWINDOW *PwinCoverQt(CONST QByteArray &keyShape, real jde,
  flag fFront)
{
  uint32_t r;
  real dt;
  int i;

  for (i = 0; i < s_lwinSrvQt.size(); i++) {
    EPHWINDOW *pwin = s_lwinSrvQt[i];
    if (!pwin->fAnim || pwin->keyShape != keyShape ||
      !FRowWindowQt(pwin, jde, &r, &dt))
      continue;
    if (pwin->fFailed) {
      if (FWindowInPlanQt(pwin))
        continue;
      s_lwinSrvQt.removeAt(i--);
      delete pwin;
      continue;
    }
    if (fFront) {
      s_lwinSrvQt.removeAt(i);
      s_lwinSrvQt.prepend(pwin);
    }
    return pwin;
  }
  return NULL;
}

// The animation window of this shape that STARTS at jd, or NULL: what the
// background prefetch asks before sending the next window. Asking "does a
// window cover the next window's first row" was wrong, because the
// current window's own slack covers it -- for steps of two minutes or
// less the next window always looked present and was never sent
// (EPHEMERIS_REVIEW.md A3).
static EPHWINDOW *PwinStartQt(CONST QByteArray &keyShape, real jd)
{
  for (EPHWINDOW *pwin : s_lwinSrvQt)
    if (pwin->fAnim && !pwin->fFailed && pwin->keyShape == keyShape &&
      RAbs(pwin->req.start.Sum() - jd) * 86400.0 < 0.001)
      return pwin;
  return NULL;
}

// Whether an animation window of this shape spans jd, on its grid or not.
// A frame inside one but off its grid is asked exactly rather than given
// a window of its own: re-anchoring would open a window every frame for a
// chart whose instants do not step with the animation (a progressed one).
static flag FInsideAnimWindowQt(CONST QByteArray &keyShape, real jd)
{
  for (EPHWINDOW *pwin : s_lwinSrvQt) {
    real dHalf = RStepWindowSrvQt(pwin) / 172800.0;
    if (pwin->fAnim && !pwin->fFailed && pwin->keyShape == keyShape &&
      jd >= pwin->req.start.Sum() - dHalf &&
      jd <= JdWindowRowQt(pwin, pwin->req.nTime - 1) + dHalf)
      return fTrue;
  }
  return fFalse;
}

// Whether a row of the window's columns is NaN: protocol 2 marks a failed
// row in all six columns with it, and the rows that computed are real.
static flag FColNanSrvQt(real r)
{
  return r != r;   // The one IEEE property NaN is guaranteed.
}

// Whether this window fails the frame at the TT instant jde: an object
// with no row computed at all (retFlag < 0), or one whose row here is
// NaN. Protocol 2 fails a row one at a time, so a window reaching past an
// ephemeris file's range still serves every frame before the edge -- the
// frames past it are asked exactly (EPHEMERIS_REVIEW.md A2, S9). A
// window still filling answers nothing false: fFalse, and the wait
// settles before this is asked again.
static flag FWindowObjFailedQt(CONST EPHWINDOW *pwin, real jde)
{
  uint32_t r;
  real dt;
  int i;

  if (pwin->fFailed)
    return fTrue;
  if (!pwin->fDone)
    return fFalse;
  for (i = 0; i < (int)pwin->rgmeta.size(); i++) {
    if (pwin->rgmeta[i].rowsOk <= 0)
      return fTrue;
    if (!FRowWindowQt(pwin, jde, &r, &dt))
      return fTrue;
    if (FColNanSrvQt(pwin->rgcol[((size_t)i * pwin->req.nTime + r) *
      cColWindowSrvQt]))
      return fTrue;
  }
  return fFalse;
}

// The background prefetch: a frame past the middle of its window, in the
// direction the animation moves, sends the next window if nothing covers it
// yet, and does not wait for it. Half a window of frames is the lead.
static void SrvPrefetchNextQt(EPHWINDOW *pwin, real jde, int nDir)
{
  uint32_t r, n = pwin->req.nTime;
  real dt, dStep = RStepWindowSrvQt(pwin) / 86400.0, jdNext;
  eph::Request req;

  if (!FRowWindowQt(pwin, jde, &r, &dt))
    return;
  if (nDir > 0 ? r * 2 < n - 1 : r * 2 > n - 1)
    return;
  jdNext = nDir > 0 ? pwin->req.start.Sum() + (real)n * dStep :
    pwin->req.start.Sum() - (real)n * dStep;
  if (PwinStartQt(pwin->keyShape, jdNext) != NULL)
    return;
  req = pwin->req;
  req.start = {jdNext, 0.0};
  // A window nobody is waiting for: prefetch, which the server answers
  // after the interactive work in front of the user (3.9).
  req.priority = 1;
  PwinOpenQt(&req, fTrue);
}

// The prefetch hook (plan section 5): one REQUEST per group of objects that
// share a flag set, all sent before any is waited on, then one bounded wait
// for the lot. The grouping is forced by FSwissPlanet()'s own arithmetic --
// a heliocentric chart's nodes stay geocentric, a custom object's definition
// flags can flip any setting for that object alone -- so a cast is several
// questions to Swiss, not one. Objects are skipped here exactly as
// ComputeEphem()'s loop skips them, and as it routes them elsewhere (the
// ephemeris-less custom type, the JPL Horizons type).
//
// The instant sent is TT, made exactly as FSwissPlanet() makes it -- the
// same delta-t call, the same user override -- under kIflagTimeTT, so the
// number Swiss sees on the server is the number it would have seen here.
// That, and the spec being the same function, is what makes a server cast
// bit-identical to a local one; nothing is "close".

// One REQUEST of a cast: the question, and which Astrolog objects its
// columns are. A cast is ONE request (3.9: the batch is the unit of work);
// it becomes several only when it has more objects than WELCOME's maxObjs,
// or more profiles than its maxProfiles.
typedef struct _EphPart {
  eph::Request req;
  QVector<int> rgobj;      // Astrolog object indices, in column order.
} EPHPART;

// The bounded wait. An event loop that sleeps until something it waits on
// settles -- a window lands or fails, the connection is welcomed or drops
// -- or the time is up. The first form spun processEvents() with a
// maxtime, which silently drops WaitForMoreEvents: a full core for up to
// ten seconds on every miss (EPHEMERIS_REVIEW.md C2). User input is held
// back for its length; redraws are owed rather than made (RedrawQt), and
// the animation timer does not tick (AnimTickQt).
static QEventLoop *s_ploopSrvWaitQt = NULL;

static void SrvWaitWakeQt()
{
  if (s_ploopSrvWaitQt != NULL)
    s_ploopSrvWaitQt->quit();
}

static flag FSrvWaitQt(CONST std::function<flag()> &fDone, int msMax)
{
  QEventLoop evloop;   // Not "loop": astrolog.h defines that.
  QTimer tim;
  QEventLoop *ploopSav = s_ploopSrvWaitQt;
  flag fWaitingSav = s_fSrvWaitingQt;

  if (fDone() || QCoreApplication::instance() == NULL || msMax <= 0)
    return fDone();
  tim.setSingleShot(fTrue);
  QObject::connect(&tim, &QTimer::timeout, &evloop, &QEventLoop::quit);
  s_ploopSrvWaitQt = &evloop;
  s_fSrvWaitingQt = fTrue;
  tim.start(msMax);
  while (!fDone() && tim.isActive())
    evloop.exec(QEventLoop::ExcludeUserInputEvents);
  s_ploopSrvWaitQt = ploopSav;
  s_fSrvWaitingQt = fWaitingSav;
  // An owed redraw is made only if nothing redraws first: the caller that
  // cast usually redraws right after (an animation tick always does), and
  // paying it regardless cast the chart again outside the tick -- an
  // exact one-row request every few frames, measured.
  if (!s_fSrvWaitingQt && s_fSrvRedrawOwedQt)
    QTimer::singleShot(0, []() {
      if (s_fSrvRedrawOwedQt && !s_fSrvWaitingQt)
        RedrawQt();
    });
  return fDone();
}

// A cast that failed for want of a connection is cast again once one is
// welcomed, so a chart cast at startup -- before the application, let
// alone the socket, exists -- or while the server was down does not sit
// at 0 Aries until the user happens to do something (EPHEMERIS_REVIEW.md
// C3).
static flag s_fSrvCastMissedQt = fFalse;

// Whether the object FSrvPlanetQt() is about to answer is a FIXED STAR.
// A star row is the entry point's own six and carries no is.rSid: the
// star callers apply the zodiac themselves, so subtracting it here hands
// them a second ayanamsa. ephswiss.cpp copies FSwissStar()'s raw six for
// the same reason and ephprom.cpp branches on the object's kind; this
// adapter shares one exit with bodies, so it is told instead.
flag s_fSrvStarQt = fFalse;


// Should a missing answer for this object be passed over in SILENCE?
//
// Only when the cast itself was fine and this one object simply was not
// in the request -- the translation has no version 4 form for it, so
// this source cannot do it and the chain offers it to the next one,
// which is not news. A cast-level failure (the server gone, the request
// refused) sets s_plan.baErr, and that still warns, because then the
// source COULD have done the object and did not.
static flag FSrvObjQuietQt(int obj)
{
  if (!FBetween(obj, 0, objMax-1))
    return fTrue;               // no plan slot: never asked
  return s_plan.rgent[obj].pwin == NULL && s_plan.baErr.isEmpty();
}

// A cast that could not reach the server sets the flag above, and the
// next WELCOME recasts it. Finalizing has to drop that intent with
// everything else it drops: the connector that comes back may be a
// DIFFERENT server, and replaying the old session's cast at it is both
// a request nobody asked for and -- measured in the suite -- a request
// the next conversation reads as its own, because it arrives first and
// carries the earlier id.
static void ForgetMissedCastSrvQt()
{
  s_fSrvCastMissedQt = fFalse;
}

static void SrvRecastMissedQt()
{
  if (s_fSrvWaitingQt || s_fAnimTickQt) {
    QTimer::singleShot(200, []() { SrvRecastMissedQt(); });
    return;
  }
  if (FSrcChainHead("server") && gi.qwind != NULL) {
    s_cSrvRecastQt++;
    RecastAndRedrawQt();
  }
}

static void SrvWelcomedQt()
{
  if (!s_fSrvCastMissedQt || QCoreApplication::instance() == NULL)
    return;
  s_fSrvCastMissedQt = fFalse;
  QTimer::singleShot(0, []() { SrvRecastMissedQt(); });
}

// Every cast's generation: the once-per-cast warning is once per this.
static int s_nSrvCastGenQt = 0;

// The most objects one REQUEST may carry: what this session's WELCOME
// promised, or the protocol's own default before one has arrived. A cast
// with more objects than this becomes several requests -- they used to be
// clamped instead, while the plan still mapped objects past the clamp to
// columns that did not exist (EPHEMERIS_REVIEW.md C7).
static int CObjReqSrvQt()
{
  uint32_t dw = eph::Welcome().maxObjs;

  if (esrv.fWelc && esrv.welc.maxObjs < dw)
    dw = esrv.welc.maxObjs;
  return dw < 1 ? 1 : (int)dw;
}

void SrvPrefetchQt(real t, int objCentCalc, int imax, CONST EPHQUERY *pqSrv)
{
  // The QUERY's own instant when there is one, not a reconstruction of
  // it. The transport had to hand this function Astrolog's T, so the JD
  // went out as (jd - 2415020) / 36525 and came back as t * 36525 +
  // 2415020 -- and that is not the identity in IEEE double once the date
  // is far from 1900. FSrvPlanetQt() compares s_plan.jd against the
  // caller's jd EXACTLY, so a mismatch meant the request was built,
  // sent, waited for and cached, and then every object was rejected with
  // "no request was made of the Ephemeris Server for this cast": the
  // whole network cost plus an alarming and untrue modal.
  //
  // Most callers never saw it because their rJD came from
  // JulianDayFromTime() itself, which makes the round trip self-inverse.
  // RProgArc()'s does not -- it is jd + rDays -- and that missed 5% of
  // the time at year 0 and 55% at -3000 (phase 8, third review, P2).
  real jd = pqSrv != NULL ? pqSrv->rJD : JulianDayFromTime(t), jde;
  std::vector<eph::Profile> rgprof;    // the cast's profiles, deduplicated
  QVector<QByteArray> rgbaProf;        // each profile's bytes, to compare by
  std::vector<eph::Object> rgobjCast;  // every object, in cast order
  QVector<int> rgiobjCast;             // its Astrolog index
  QVector<EPHPART> rgpart;
  QVector<EPHWINDOW *> rgpwin;
  SWISSSPEC ss;
  int i, ig, objOrbit, nGrid, nDir, cObjMax;
  QElapsedTimer tim;

  // A cast made while another waits -- a timer, a paint -- leaves the
  // waiting cast's plan alone. The first form wiped it on entry, so the
  // waiting cast woke to an empty plan and failed every object
  // (EPHEMERIS_REVIEW.md C1); this cast's own reads fail soft instead
  // (FSrvPlanetQt).
  if (s_fSrvWaitingQt)
    return;
  s_nSrvCastGenQt++;
  for (i = 0; i < objMax; i++) {
    s_plan.rgent[i].pwin = NULL;
    s_plan.rgent[i].iObj = 0;
    s_plan.rgent[i].fUnsupported = fFalse;
  }
  s_plan.jd = jd;
  s_plan.jde = rInvalid;
  s_plan.fPrefetched = fTrue;
  s_plan.baErr = QByteArray();
  if (QCoreApplication::instance() == NULL) {
    // The startup chart, cast before the window exists: no socket can be
    // made yet (Qt needs its application first). BeginQt() connects, and
    // the WELCOME casts this chart again.
    s_plan.baErr = "the Ephemeris Server is not connected yet";
    s_fSrvCastMissedQt = fTrue;
    return;
  }
  if (esrv.est != esWelcomed) {
    // Casting on the backend with the connector down begins the background
    // connect, so selecting the server mid-session brings it up the way
    // startup would have; and a connection already on its way is waited
    // for, like the answer would be, rather than failing the cast at once.
    // One that is not on its way -- the server is down and the ladder is
    // between rungs -- fails the cast now, and the WELCOME recasts it.
    EphSrvStartupQt();
    if (esrv.est == esConnecting)
      FSrvWaitQt([]() -> flag { return esrv.est != esConnecting; },
        s_msSrvWaitQt);
    if (esrv.est != esWelcomed) {
      s_plan.baErr = "the Ephemeris Server is not connected";
      s_fSrvCastMissedQt = fTrue;
      return;
    }
  }

  // The instant, as FSwissPlanet() makes it.
  if (jd != is.jdDeltaT) {
    is.jdDeltaT = jd;
    is.rDeltaT = swe_deltat(jd);
  }
  jde = jd + (us.rDeltaT == rInvalid ? is.rDeltaT : us.rDeltaT/86400.0);
  s_plan.jde = jde;

  // The cast, as one version 4 question: every object with the profile its
  // settings make (ephswiss.h turns FSwissPlanetSpec()'s Swiss call into
  // the object and profile that make the server make that same call), then
  // split into as few REQUESTs as WELCOME's limits allow.
  cObjMax = CObjReqSrvQt();
  if (pqSrv != NULL) {
    // Driven by the host's query (phase 6): the objects are the ones the
    // registry asked for, and ephreq.h makes the same translation this
    // loop makes -- one implementation, so a remote answer through the
    // chain and one through the legacy path cannot ask different
    // questions. The profiles come back deduplicated already.
    eph::Request reqQ;
    EPHREQMAP rgmapQ[objMax];
    int cQ = CEphRequestFromQuery(pqSrv, (jde - jd) * 86400.0, &reqQ,
      rgmapQ), iQ;

    for (iQ = 0; iQ < (int)reqQ.profiles.size(); iQ++)
      rgprof.push_back(reqQ.profiles[iQ]);
    for (iQ = 0; iQ < cQ; iQ++) {
      rgobjCast.push_back(reqQ.objs[rgmapQ[iQ].iObjReq]);
      rgiobjCast.append(pqSrv->rgobj[rgmapQ[iQ].iObjQuery]);
    }
  } else
  for (i = oEar; i <= imax; i++) {
    if (FSkipEphem(i, objCentCalc, fFalse))
      continue;
    if (FCust(i) && rgTypSwiss[i - custLo] == 5)
      continue;   // Ephemeris-less: ComputeEphem() leaves it alone.
#ifdef JPLWEB
    if (FCust(i) && rgTypSwiss[i - custLo] == 4)
      continue;   // A JPL Horizons object: ComputeEphem() routes it there.
#endif
    objOrbit = us.fMoonMove ? ObjOrbit(i) : -1;
    if (objOrbit < 0 || objOrbit == oSun)
      objOrbit = objCentCalc;
    if (!FSwissPlanetSpec(i, objOrbit, &ss)) {
      s_plan.rgent[i].fUnsupported = fTrue;
      continue;
    }
    // GetSwissFlags() spells the backend number into the ephemeris bits;
    // which ephemeris a server reads is the server's business, and its
    // datasetId says which it was.
    ss.iflag = (ss.iflag & ~(SEFLG_JPLEPH | SEFLG_MOSEPH)) | SEFLG_SWIEPH;
    {
      eph::Profile pf;
      eph::Object obj;
      double topo[3] = {ss.topoLon, ss.topoLat, ss.topoElv};
      std::vector<uint8_t> rgbProf;
      QByteArray baProf;
      int ip;

      if (!eph::swiss::ProfileFromSwiss(ss.iflag, ss.iobjCent, ss.nSidMode, topo, &pf) ||
        !eph::swiss::ObjectFromSwiss(ss.iobj, ss.nPnt > 0 ? ss.nPnt - 1 : -1,
        ss.nNodMethod, &obj)) {
        // No version 4 form for this call. Not silent: the read below says
        // so, once per cast, like every other object with no answer.
        continue;
      }
      {
        eph::Writer w(&rgbProf);
        eph::WriteProfile(w, pf);
      }
      baProf = QByteArray((CONST char *)rgbProf.data(), (int)rgbProf.size());
      for (ip = 0; ip < rgbaProf.size(); ip++)
        if (rgbaProf[ip] == baProf)
          break;
      if (ip >= rgbaProf.size()) {
        rgbaProf.append(baProf);
        rgprof.push_back(pf);
      }
      obj.profile = (uint8_t)ip;
      rgobjCast.push_back(obj);
      rgiobjCast.append(i);
    }
  }
  if (rgobjCast.empty())
    return;   // Nothing this backend computes: no request to make.

  // Into requests: WELCOME's maxObjs objects and maxProfiles profiles at
  // most, each request carrying only the profiles its own objects use --
  // an object names its profile by index INTO ITS OWN REQUEST.
  {
    int cProfMax = esrv.fWelc ? (int)esrv.welc.maxProfiles : 16;
    EPHPART part;
    QVector<int> rgipPart;   // this part's profiles, as indices into rgprof
    for (i = 0; i < (int)rgobjCast.size(); i++) {
      int ipCast = rgobjCast[i].profile, ipPart;
      for (ipPart = 0; ipPart < rgipPart.size(); ipPart++)
        if (rgipPart[ipPart] == ipCast)
          break;
      if ((int)part.req.objs.size() >= cObjMax ||
        (ipPart >= rgipPart.size() && rgipPart.size() >= cProfMax)) {
        rgpart.append(part);
        part = EPHPART();
        rgipPart.clear();
        ipPart = 0;
      }
      if (ipPart >= rgipPart.size()) {
        ipPart = rgipPart.size();
        rgipPart.append(ipCast);
        part.req.profiles.push_back(rgprof[ipCast]);
      }
      eph::Object obj = rgobjCast[i];
      obj.profile = (uint8_t)ipPart;
      part.req.objs.push_back(obj);
      part.rgobj.append(rgiobjCast[i]);
    }
    if (!part.req.objs.empty())
      rgpart.append(part);
  }

  // One request per part, from the window cache when it has been asked
  // before. A frame of the running animation, at a uniform rate, reads the
  // wide f32 window whose grid it is on, or opens one anchored at itself
  // when no window of its shape spans it; a frame inside such a window but
  // off its grid, or one whose window could not compute every object, is
  // asked exactly, one row, like any other cast.
  nGrid = NGridAnimSrvQt(&nDir);
  rgpwin.fill(NULL, rgpart.size());
  for (ig = 0; ig < rgpart.size(); ig++) {
    eph::Request req = rgpart[ig].req;
    EPHWINDOW *pwin = NULL;
    QByteArray key;
    flag fCover = fFalse;

    // The instant is TT, made exactly as FSwissPlanet() makes it, and the
    // delta T Astrolog used goes with it: Astrolog owns delta T (3.5), and
    // the server needs none of its own for a TT instant.
    req.timeScale = eph::kTimeTT;
    req.timeMode = eph::kTimeGrid;
    req.start = eph::Time{jde, 0.0};
    req.stepNs = 0;
    req.nTime = 1;
    req.deltaTSec = (jde - jd) * 86400.0;
    req.precision = eph::kPrecF64;
    req.chunkRows = (uint32_t)s_cChunkRowsQt;
    if (nGrid > 0) {
      eph::Request reqAnim = req;
      reqAnim.stepNs = (int64_t)nGrid * 1000000000LL;
      reqAnim.nTime = (uint32_t)s_cRowsAnimQt;
      reqAnim.precision = eph::kPrecF32;
      // A window of many instants has no one delta T: the server's model
      // would answer for it, and for a TT instant nothing reads it.
      reqAnim.deltaTSec = eph::CanonicalNaN();
      ClampEphSrvReqQt(&reqAnim);
      if (reqAnim.nTime > 1) {
        key = KeyShapeWindowQt(reqAnim);
        pwin = PwinCoverQt(key, jde, fTrue);
        if (pwin != NULL && FWindowObjFailedQt(pwin, jde))
          pwin = NULL;
        else if (pwin != NULL)
          fCover = fTrue;
        else if (!FInsideAnimWindowQt(key, jde)) {
          if (nDir < 0)
            reqAnim.start = eph::Time{jde - (real)(reqAnim.nTime - 1) *
              (real)nGrid / 86400.0, 0.0};
          pwin = PwinOpenQt(&reqAnim, fTrue);
          if (pwin == NULL) {
            s_plan.baErr = "the Ephemeris Server connection dropped";
            break;
          }
        }
      }
    }
    if (pwin == NULL) {
      ClampEphSrvReqQt(&req);
      key = KeyWindowQt(req);
      pwin = PwinByKeyQt(key);
      if (pwin != NULL && pwin->fFailed && !FWindowInPlanQt(pwin)) {
        // Ask again: a failure is a fact about that attempt, not the sky.
        s_lwinSrvQt.removeOne(pwin);
        delete pwin;
        pwin = NULL;
      }
      if (pwin == NULL)
        pwin = PwinOpenQt(&req, fFalse);
      if (pwin == NULL) {
        s_plan.baErr = "the Ephemeris Server connection dropped";
        break;
      }
    }
    // Into the plan BEFORE anything else is opened: an open evicts, and
    // only what the plan points at is safe from it. The background
    // prefetch below used to run first, and could evict the very window
    // this part was about to read (EPHEMERIS_REVIEW.md A1, C10).
    rgpwin[ig] = pwin;
    for (i = 0; i < rgpart[ig].rgobj.size(); i++) {
      // The plan is addressed BY ASTROLOG OBJECT INDEX and is objMax
      // long, so an object outside that cannot be recorded in it. The
      // read side has always checked (FSrvPlanetQt); the write side had
      // not, and did not need to while the only producer was the cast
      // loop below, which runs oEar..imax. A QUERY can carry more: a
      // side call names an asteroid as SE_AST_OFFSET + n, over 10000.
      // FSubmitTransQt() refuses such a query outright, so this is the
      // second line rather than the first.
      if (!FBetween(rgpart[ig].rgobj[i], 0, objMax-1))
        continue;
      s_plan.rgent[rgpart[ig].rgobj[i]].pwin = pwin;
      s_plan.rgent[rgpart[ig].rgobj[i]].iObj = i;
    }
    if (fCover && pwin->fDone)
      SrvPrefetchNextQt(pwin, jde, nDir);
  }

  // The bounded wait, for the lot. The windows stay, filling in the
  // background, whatever happens here; the next cast at this instant is a
  // hit.
  auto fSettled = [imax]() -> flag {
    for (int j = oEar; j <= imax; j++) {
      CONST EPHWINDOW *pwinT = s_plan.rgent[j].pwin;
      if (pwinT != NULL && !pwinT->fDone && !pwinT->fFailed)
        return fFalse;
    }
    return fTrue;
  };
  tim.start();
  FSrvWaitQt(fSettled, s_msSrvWaitQt);

  // A frame whose new animation window could not compute every object is
  // asked again exactly, and waited on for what time is left.
  if (nGrid > 0) {
    flag fAgain = fFalse;
    for (ig = 0; ig < rgpart.size(); ig++) {
      EPHWINDOW *pwin = rgpwin[ig];
      eph::Request req;

      if (pwin == NULL || !pwin->fAnim || !FWindowObjFailedQt(pwin, jde))
        continue;
      req = pwin->req;
      req.start = eph::Time{jde, 0.0};
      req.stepNs = 0;
      req.nTime = 1;
      req.precision = eph::kPrecF64;
      req.priority = 0;
      req.deltaTSec = (jde - jd) * 86400.0;
      ClampEphSrvReqQt(&req);
      pwin = PwinByKeyQt(KeyWindowQt(req));
      if (pwin == NULL)
        pwin = PwinOpenQt(&req, fFalse);
      if (pwin == NULL)
        continue;
      for (i = 0; i < rgpart[ig].rgobj.size(); i++) {
        // The same bound as the first write site, for the same reason.
        if (!FBetween(rgpart[ig].rgobj[i], 0, objMax-1))
          continue;
        s_plan.rgent[rgpart[ig].rgobj[i]].pwin = pwin;
      }
      fAgain = fTrue;
    }
    if (fAgain)
      FSrvWaitQt(fSettled, s_msSrvWaitQt - (int)tim.elapsed());
  }
  if (!fSettled())
    s_plan.baErr = "the Ephemeris Server did not answer in time";
}

// The synchronous facade's per-object read (plan section 5), ComputeEphem()'s
// server analogue of the GetJPLHorizons() call site: the same six reals
// FSwissPlanet() and GetJPLHorizons() fill, from the window the prefetch
// left for this object, post-processed exactly as FSwissPlanet()
// post-processes xx[]. Fails soft, once per cast in words, and never sets
// is.fNoEphFile -- lesson 3, that latch is for one-shot web queries, and
// here one dropped cast must not disable a held connection until restart.

// The once-per-cast warning's counter and last text, for the suite to pin
// "once" and to say why when a cast that should have succeeded did not.
static int s_cSrvWarnQt = 0;
static char s_szSrvWarnQt[cchSzMax];

#define msEphSrvWarnGap 30000   // At most one warning box this often.

static void SrvWarnOnceQt(real jd, CONST char *szWhy)
{
  static real rJd = rInvalid;    // The cast the once-per-cast warning was
  static int nGen = -1;          // raised for: its generation and instant.
  static QElapsedTimer timShown;

  // Once per cast, and a cast is a generation of the prefetch, not an
  // instant: keyed on the instant alone, a chart cast again at the same
  // moment was silent at 0 Aries (EPHEMERIS_REVIEW.md C8).
  if (rJd == jd && nGen == s_nSrvCastGenQt)
    return;
  rJd = jd; nGen = s_nSrvCastGenQt;
  s_cSrvWarnQt++;
  sprintf2(S(s_szSrvWarnQt), "%s; objects served by the Ephemeris Server "
    "fail this cast.", szWhy);
  s_szSrvWarnQt[0] = ChCap(s_szSrvWarnQt[0]);
  // The box is a modal and every chart that casts many instants -- an
  // ephemeris listing, a transit search -- is many casts: one box per
  // stretch of time, the rest counted and kept for the status line. No
  // latch (lesson 3): the next cast after the gap says it again.
  if (timShown.isValid() && timShown.elapsed() < msEphSrvWarnGap)
    return;
  timShown.start();
  PrintWarning(s_szSrvWarnQt);
}

// Which body a per-object message is about. The plan is indexed by
// Astrolog object, and for a planet szObjName[] is that object's name --
// but a STAR's plan slot is its SWISS CATALOGUE NUMBER, because
// SwissComputeStar() passes istar where an object index goes, so
// szObjName[] there names an unrelated planet: "could not compute Moon"
// for star #1. Positions are unaffected -- the same index writes and
// reads -- and only the sentence was wrong. The request's own object
// carries the name that was asked for, which is right for either kind.
static CONST char *SzSrvObjNameQt(CONST EPHWINDOW *pwin, int iObj, int obj)
{
  if (pwin != NULL && iObj >= 0 && iObj < (int)pwin->req.objs.size() &&
    pwin->req.objs[iObj].kind == eph::kObjStar &&
    !pwin->req.objs[iObj].name.empty())
    return pwin->req.objs[iObj].name.c_str();
  return FBetween(obj, 0, objMax-1) ? szObjName[obj] : "an object";
}


flag FSrvPlanetQt(int obj, real jd, real *objPos, real *objAlt, real *dir,
  real *dist, real *diralt, real *dirlen)
{
  CONST EPHPLANENTRY *pent;
  CONST EPHWINDOW *pwin;
  CONST double *xx;
  char sz[cchSzMax];

  if (s_fSrvWaitingQt) {
    // Cast from inside another cast's wait; its plan is not this cast's.
    SrvWarnOnceQt(jd, "a chart was cast while another cast was waiting on "
      "the Ephemeris Server");
    return fFalse;
  }
  if (!s_plan.fPrefetched || s_plan.jd != jd || !FBetween(obj, 0, objMax-1)) {
    // No prefetch ran for this cast: ComputeEphem() was not the caller.
    SrvWarnOnceQt(jd, "no request was made of the Ephemeris Server for "
      "this cast");
    return fFalse;
  }
  pent = &s_plan.rgent[obj];
  if (pent->fUnsupported)
    return fFalse;   // As FSwissPlanet() is for the same object: silent.
  pwin = pent->pwin;
  if (pwin == NULL) {
    SrvWarnOnceQt(jd, s_plan.baErr.isEmpty() ?
      "the Ephemeris Server has no answer" : s_plan.baErr.constData());
    return fFalse;
  }
  if (pwin->fFailed) {
    sprintf2(S(sz), "the Ephemeris Server refused the request (%.160s)",
      pwin->baErr.constData());
    SrvWarnOnceQt(jd, sz);
    return fFalse;
  }
  if (!pwin->fDone) {
    SrvWarnOnceQt(jd, s_plan.baErr.isEmpty() ?
      "the Ephemeris Server did not answer in time" : s_plan.baErr.constData());
    return fFalse;
  }
  if (pwin->rgmeta[pent->iObj].rowsOk <= 0) {
    // This object failed on the server: its A.17 code and the server's own
    // text, where FSwissPlanet() would have printed Swiss's.
    sprintf2(S(sz), "the Ephemeris Server could not compute %s (error %d: %.120s)",
      SzSrvObjNameQt(pwin, pent->iObj, obj),
      (int)pwin->rgmeta[pent->iObj].errCode,
      pwin->rgmeta[pent->iObj].errText.c_str());
    SrvWarnOnceQt(jd, sz);
    return fFalse;
  }
  // The row for this instant. A chart cast's window is one row at the
  // cast's own TT, read on the bytes. An animation frame's is the nearest
  // row of its grid, moved to the frame's instant along the speeds the
  // row carries -- first order, over at most half a row plus the delta-t
  // drift (see rSlackAnimSrvQt), which at f32 is animation-grade and is
  // why stopping the animation casts the chart again exactly.
  {
    uint32_t r;
    real dt;
    real jde = s_plan.jde;
    if (!FRowWindowQt(pwin, jde, &r, &dt)) {
      SrvWarnOnceQt(jd, "the Ephemeris Server window has no row at this "
        "instant");
      return fFalse;
    }
    xx = pwin->rgcol.constData() +
      ((size_t)pent->iObj * pwin->req.nTime + r) * cColWindowSrvQt;
    // 3.5: an object whose other rows computed can still fail this one --
    // its row is NaN in every column, and its metadata carries the first
    // failure's code and text (EPHEMERIS_REVIEW.md S9).
    if (FColNanSrvQt(xx[0])) {
      sprintf2(S(sz), "the Ephemeris Server could not compute %s (error %d: %.120s)",
        SzSrvObjNameQt(pwin, pent->iObj, obj),
        (int)pwin->rgmeta[pent->iObj].errCode,
        pwin->rgmeta[pent->iObj].errText.c_str());
      SrvWarnOnceQt(jd, sz);
      return fFalse;
    }
    if (pwin->fAnim) {
      s_fSrvApproxQt = fTrue;
      *objPos = s_fSrvStarQt ? Mod(xx[0] + xx[3] * dt) :
        Mod(xx[0] + xx[3] * dt) - is.rSid +
        (us.fSidereal ? us.rZodiacOffset : 0.0) + us.rZodiacOffsetAll;
      *objAlt = xx[1] + xx[4] * dt;
      *dist   = xx[2] + xx[5] * dt;
      *dir    = xx[3];
      *diralt = xx[4];
      *dirlen = xx[5];
      return fTrue;
    }
  }
  *objPos = s_fSrvStarQt ? xx[0] :
    xx[0] - is.rSid + (us.fSidereal ? us.rZodiacOffset : 0.0) +
    us.rZodiacOffsetAll;
  *objAlt = xx[1];
  *dist   = xx[2];
  *dir    = xx[3];
  *diralt = xx[4];
  *dirlen = xx[5];
  return fTrue;
}

static void ClearWindowsSrvQt()
{
  int i;

  while (!s_lwinSrvQt.isEmpty())
    delete s_lwinSrvQt.takeLast();
  for (i = 0; i < objMax; i++)
    s_plan.rgent[i].pwin = NULL;
  s_plan.fPrefetched = fFalse;
  s_fSrvApproxQt = fFalse;
}

static void ClearSettledWindowsSrvQt()
{
  for (int i = s_lwinSrvQt.size() - 1; i >= 0; i--)
    if (s_lwinSrvQt[i]->dwReq == 0 && !FWindowInPlanQt(s_lwinSrvQt[i]))
      delete s_lwinSrvQt.takeAt(i);
}

// Whether a cast has read an animation row since this was last asked, and
// forget it: the question stopping the animation asks.
static flag FSrvApproxTakeQt(void)
{
  flag f = s_fSrvApproxQt;

  s_fSrvApproxQt = fFalse;
  return f;
}

int CWinSrvTestQt() { return s_lwinSrvQt.size(); }
void SetWaitSrvTestQt(int ms) { s_msSrvWaitQt = ms; }
void SetHelloSrvTestQt(int ms) { s_msEphSrvHelloQt = ms; }
void SetChunkRowsSrvTestQt(int c) { s_cChunkRowsQt = c; }
int CRecastSrvTestQt() { return s_cSrvRecastQt; }
flag FWaitingSrvTestQt() { return s_fSrvWaitingQt; }
void SetWelcMaxObjsSrvTestQt(uint32_t dw) { esrv.welc.maxObjs = dw; }
void SetWelcMaxCellsSrvTestQt(uint32_t dw) { esrv.welc.maxCells = dw; }
// Which name a per-object failure message would use, for one object of a
// hand-built window. iCase 0 is a STAR in the plan slot of Astrolog object
// 1, which is the Moon -- the shape SwissComputeStar() makes, and the one
// that printed "could not compute Moon" for star #1. iCase 1 is an
// ordinary body, which must still be named from szObjName[].
CONST char *SzObjNameProbeSrvTestQt(int iCase)
{
  static EPHWINDOW winT;
  eph::Object o;

  winT.req.objs.clear();
  if (iCase == 0) {
    o.kind = eph::kObjStar;
    o.name = "Aldebaran";
  } else {
    o.kind = eph::kObjBody;
    o.naif = 10;
  }
  winT.req.objs.push_back(o);
  return SzSrvObjNameQt(&winT, 0, 1);
}


// Feed hand-built DATA chunks to a window of rows 2, one object, f64, held
// under a request id nothing else uses, and say what became of it:
// 1 done, 0 still waiting, -1 failed. The cases are the chunk defects the
// review named: a chunk delivered twice, a row index that wraps 32 bits,
// and a chunk in the wrong precision.
int NChunkProbeSrvTestQt(int iCase)
{
  CONST uint32_t dwReq = 0xFEEDF00D;
  EPHWINDOW *pwin = new EPHWINDOW;
  eph::Object o;
  eph::DataChunk d;
  std::vector<uint8_t> pay;
  int n;

  o.kind = eph::kObjBody; o.naif = 10;
  pwin->req.objs.push_back(o);
  pwin->req.profiles.push_back(eph::Profile());
  pwin->req.nTime = 2;
  pwin->req.stepNs = 60LL * 1000000000LL;
  pwin->req.precision = eph::kPrecF64;
  pwin->dwReq = dwReq;
  pwin->fDone = pwin->fFailed = pwin->fMeta = pwin->fAnim = fFalse;
  pwin->cRowsGot = 0;
  pwin->chunkNext = 0;
  pwin->rgfRow.fill(false, 2);
  pwin->rgcol.fill(0.0, 12);
  pwin->rgmeta.assign(1, eph::Meta());
  s_lwinSrvQt.append(pwin);
  d.chunkIndex = 0; d.iTime = 0; d.nRows = 1; d.totalRows = 2;
  d.precision = eph::kPrecF64;
  d.flags = eph::kChunkMeta;
  d.nObj = 1;
  d.sources.push_back("test");
  d.meta.assign(1, eph::Meta());
  d.meta[0].rowsOk = 2;
  d.values.assign(6, 1.0);
  switch (iCase) {
  case 0:   // Row 0 twice: chunk 0 is refused the second time (3.4 says
            // chunks ascend), and the window is not complete either way.
    eph::EncodeData(&pay, d);
    FWindowChunkQt(dwReq, pay.data(), (uint32_t)pay.size());
    FWindowChunkQt(dwReq, pay.data(), (uint32_t)pay.size());
    break;
  case 1:   // Row 0xFFFFFFFF, two rows: the 32-bit sum wraps to 1.
    d.iTime = 0xFFFFFFFFu;
    d.nRows = 2;
    d.values.assign(12, 1.0);
    eph::EncodeData(&pay, d);
    FWindowChunkQt(dwReq, pay.data(), (uint32_t)pay.size());
    break;
  default:  // Both rows, but f32 for an f64 window.
    d.nRows = 2;
    d.precision = eph::kPrecF32;
    d.values.assign(12, 1.0);
    eph::EncodeData(&pay, d);
    FWindowChunkQt(dwReq, pay.data(), (uint32_t)pay.size());
    break;
  }
  n = pwin->fFailed ? -1 : pwin->fDone ? 1 : 0;
  s_lwinSrvQt.removeOne(pwin);
  delete pwin;
  return n;
}
void SetRowsAnimSrvTestQt(int c) { s_cRowsAnimQt = c; }
void SetWindowCapSrvTestQt(int c) { s_cWindowCapQt = c; }
flag FApproxSrvTestQt() { return s_fSrvApproxQt; }
// Whether the last cast's plan routed an object to the server.
flag FPlanSrvTestQt(int obj)
  { return FBetween(obj, 0, objMax-1) && s_plan.rgent[obj].pwin != NULL; }
void SetAnimFrameSrvTestQt(flag f) { s_fAnimFrameQt = f; }
// The i'th window, most recently used first: its grid, precision, whether
// it is an animation window and whether it has landed.
flag FWinInfoSrvTestQt(int i, double *pjdStart, int *pnStep, int *pnTime,
  int *pnPrec, flag *pfAnim, flag *pfDone)
{
  if (i < 0 || i >= s_lwinSrvQt.size())
    return fFalse;
  CONST EPHWINDOW *pwin = s_lwinSrvQt[i];
  *pjdStart = pwin->req.start.Sum();
  *pnStep = (int)(pwin->req.stepNs / 1000000000LL);
  *pnTime = (int)pwin->req.nTime;
  *pnPrec = (int)pwin->req.precision;
  *pfAnim = pwin->fAnim;
  *pfDone = pwin->fDone;
  return fTrue;
}
CONST char *SzWarnSrvTestQt() { return s_szSrvWarnQt; }
void ClearWinSrvTestQt() { ClearWindowsSrvQt(); }

int NCastWarnSrvTestQt() { return s_cSrvWarnQt; }


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

  // Before anything casts: the registry's server source asks whichever
  // transport registered itself, and in this build that is the adapter
  // below. Bound here rather than by a static initializer so the order is
  // visible and so a build that never starts a GUI never binds one.
  EphSrvTransportBindQt();

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
  // The chart never smaller or larger than Astrolog's own limits, which
  // Windows enforces on the WINDOW with WM_GETMINMAXINFO (wdriver.cpp,
  // ptMinTrackSize/ptMaxTrackSize). Enforced on the CANVAS here rather
  // than on the window, so a window dragged smaller scrolls over a 180
  // pixel chart instead of refusing to move: the scroll area is already
  // there for exactly that.
  //
  // Not cosmetic. With "Window Resizes Chart" on -- the default -- the
  // canvas size IS gs.xWin/gs.yWin, "Save Program Settings" writes them
  // as ":Xw <x> <y>", and NSwXw() REFUSES a value outside 180 to 4096.
  // A refused line in a settings file does not merely lose that line:
  // FProcessSwitchFile() stops reading there, so everything after it goes
  // too -- from ":Xw" that is every graphics default, every object and
  // aspect setting, every colour and every macro. Measured: a window at
  // 220x200 left gs.yWin at 169, and the file that produced stopped at
  // that line.
  gi.qcanvas->setMinimumSize(BITMAPX1, BITMAPY1);
  gi.qcanvas->setMaximumSize(BITMAPX, BITMAPY);
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
  // And then sized so the CHART is gs.xWin by gs.yWin, as Windows' startup
  // does with ResizeWindowToChart(). The resize above sizes the window to
  // it, which loses the menu bar's height from the chart; the canvas wrote
  // the smaller viewport back into gs.yWin, and every launch followed by
  // "Save Program Settings" saved a chart 25 pixels shorter. Measured
  // after show() and not before: a hidden scroll area has had no resize
  // event, so its viewport is not laid out and the chrome measures 122 by
  // 25 rather than 0 by 25. Nothing adopts the viewport before
  // InteractQt() sets qi.fReady, so the first size cannot leak into
  // gs.yWin. A text chart keeps the plain resize; that function leaves
  // it alone, as Windows' does.
  ResizeWindowToChartQt();
  ScheduleUiFontReapplyQt();
  // The Ephemeris Server backend connects in the background when it is
  // the selected backend (plan §4); the required-server branch that
  // dialogs here at increment 4 lands inside EphSrvStartupQt().
  EphSrvStartupQt();
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
  TextGridFreeQt();
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
