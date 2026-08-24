/*
** Astrolog (Version 8.00) File: qtdriver.h
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
** This file declares the shared entry points between qtdriver.cpp (the
** Qt main window, chart canvas, and menu bar) and qtdialog.cpp (the Qt
** dialogs), and is only compiled into the "QT" Linux GUI backend.
*/

#ifndef __QTDRIVER_H
#define __QTDRIVER_H

// Recompute the chart positions from the current chart info (ciCore), then
// redraw. Call this after a change that affects what gets cast, such as
// editing chart info, orbs, or object restrictions.
void RecastAndRedrawQt();

// Redraw the chart into the off screen buffer and repaint the canvas widget,
// without recasting first. Call this after a change that only affects how
// the chart is drawn, such as colors.
void RedrawQt();

// Dialog launchers, implemented in qtdialog.cpp, wired up to the menu bar
// built in qtdriver.cpp.
void ShowChartInfoDialogQt();
void ShowColorDialogQt();
void ShowObjectDialogQt();
void ShowRestrictDialogQt();
void ShowOpenChartDialogQt();
void ShowSaveChartDialogQt();

#endif // __QTDRIVER_H

/* qtdriver.h */
