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
#include <QtGui/QResizeEvent>
#include <QtGui/QPaintEvent>

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


// Build the main window's menu bar. This is deliberately a small, curated
// subset of Astrolog's ~90 interactive keystroke commands (see
// DisplayKeysX() in charts0.cpp for the full list) -- just enough to create,
// load, save, and configure a chart without needing to know any of them.

static void BuildAstrologMenus(QMainWindow *pwind)
{
  QMenu *pmenuFile = pwind->menuBar()->addMenu("&File");
  QAction *paInfo = pmenuFile->addAction("&Chart Info...");
  QObject::connect(paInfo, &QAction::triggered, pwind,
    []() { ShowChartInfoDialogQt(); });
  QAction *paOpen = pmenuFile->addAction("&Open Chart...");
  QObject::connect(paOpen, &QAction::triggered, pwind,
    []() { ShowOpenChartDialogQt(); });
  QAction *paSave = pmenuFile->addAction("&Save Chart...");
  QObject::connect(paSave, &QAction::triggered, pwind,
    []() { ShowSaveChartDialogQt(); });
  pmenuFile->addSeparator();
  QAction *paQuit = pmenuFile->addAction("&Quit");
  QObject::connect(paQuit, &QAction::triggered, pwind,
    [pwind]() { pwind->close(); });

  QMenu *pmenuSettings = pwind->menuBar()->addMenu("&Settings");
  QAction *paColor = pmenuSettings->addAction("&Colors...");
  QObject::connect(paColor, &QAction::triggered, pwind,
    []() { ShowColorDialogQt(); });
  QAction *paObject = pmenuSettings->addAction("&Objects...");
  QObject::connect(paObject, &QAction::triggered, pwind,
    []() { ShowObjectDialogQt(); });
  QAction *paRestrict = pmenuSettings->addAction("&Restrictions...");
  QObject::connect(paRestrict, &QAction::triggered, pwind,
    []() { ShowRestrictDialogQt(); });
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
