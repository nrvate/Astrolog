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

// Switch the chart type/mode, mirroring the chart-type switch in Windows'
// ProcessState() (wdriver.cpp) -- clears all the chart-type flags, then
// sets the one flag matching the new mode, syncs the Chart menu's radio
// checkmark if "mode" has one (this is also how the Transits dialog keeps
// that menu in sync, since it can set chart mode too), then redraws (chart
// type alone doesn't need a recast; the underlying positions haven't
// changed -- callers that also changed what's being cast, like the
// Transits dialog, recast separately afterward). "mode" is one of the gXxx
// chart-mode constants in astrolog.h (gWheel, gHouse, gGrid, etc), the same
// set Windows' wi.nMode holds.
void SetChartModeQt(int mode);

// Switch the relationship chart type, mirroring Windows' SetRel().
// "rc" is one of the rcXxx constants in astrolog.h (rcNo, rcDual, etc).
void SetRelQt(int rc);

// Refresh the Setting menu's Heliocentric checkmark from current state.
// Needed because the Calculation Settings dialog can also change the
// central planet, not just the Heliocentric menu item itself.
void SyncHelioMenuQt();

// Dialog launchers, implemented in qtdialog.cpp, wired up to the menu bar
// built in qtdriver.cpp.
void ShowChartInfoDialogQt();
void ShowChartInfo2DialogQt();
void ShowChartsAllDialogQt();
void ShowOpenChart2DialogQt();
void ShowChartListDialogQt();
void ShowOpenChartDirDialogQt();
void ShowSaveChartListDialogQt();
void ShowColorDialogQt();
void ShowObjectDialogQt();
void ShowRestrictDialogQt();
void ShowOpenChartDialogQt();
void ShowSaveChartDialogQt();
void ShowSaveChartPositionsDialogQt();
void ShowSaveSettingsDialogQt();
void ShowSaveAAFDialogQt();
void ShowSaveQuickDialogQt();
void ShowSaveCalendarDialogQt();
void ShowOpenBackgroundDialogQt();
void ShowOpenWorldDialogQt();
void ShowFileSettingsDialogQt();
void ShowGraphicsSettingsDialogQt();
void ShowDefaultInfoDialogQt();
void ShowTransitDialogQt();
void ShowProgressDialogQt();
void ShowChartSettingsDialogQt();
void ShowCommandLineDialogQt();
void ShowAboutDialogQt();
void ShowAspectDialogQt();
void ShowStarRestrictDialogQt();
void ShowTransitRestrictDialogQt();
void ShowObject2DialogQt();
void ShowCalcDialogQt();
void ShowDisplayDialogQt();
void ShowMoonRestrictDialogQt();
void ShowMoonObjectDialogQt();
void ShowCustomDialogQt();
void ShowCustomStarDialogQt();
void ShowExportBitmapDialogQt();
void ShowExportMetafileDialogQt();
void ShowExportPSDialogQt();
void ShowExportSVGDialogQt();
void ShowExportWireDialogQt();
void ShowExportTextDialogQt();
void CopyChartMetafileQt();
void CopyChartPSQt();
void CopyChartSVGQt();
void CopyChartWireQt();

#endif // __QTDRIVER_H

/* qtdriver.h */
