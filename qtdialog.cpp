/*
** Astrolog (Version 8.00) File: qtdialog.cpp
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
** This file implements the Qt Linux GUI backend's configuration dialogs,
** the initial curated subset of Windows' larger dialog set (wdialog.cpp):
** chart info entry, open/save chart, colors, object display, and object
** restriction. Each one edits the same global chart state the Windows
** dialogs of the same purpose do, and reuses the same platform independent
** validation/parsing/file I/O routines they call into (FValid*() in
** astrolog.h, NParseSz()/RParseSz() and FInputData()/FOutputData() in
** io.cpp), rather than reimplementing any of that logic here.
**
** Last code change made 8/24/2026.
*/

// All Qt headers this file needs must be included before astrolog.h, since
// astrolog.h defines several single word macros (META, PS, TIME, etc) that
// collide with identifiers used inside Qt's own headers.
#include <QtWidgets/QDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QRadioButton>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QListWidget>
#include <QtCore/QDir>
#include <QtCore/QVector>
#include <QtCore/QMimeData>
#include <QtGui/QClipboard>

#include "astrolog.h"
#include "qtdriver.h"

#include <unistd.h>

#ifdef QT

// Load a chart file chosen via a standard file picker, exactly as Windows'
// DlgOpenChart does via the stock Windows file dialog -- no custom dialog
// is needed here either, just FInputData() doing the actual work.

void ShowOpenChartDialogQt()
{
  QString qs = QFileDialog::getOpenFileName(gi.qwind, "Open Chart", QString(),
    "Astrolog Chart Files (*.as);;All Files (*)");
  if (qs.isEmpty())
    return;
  QByteArray ba = qs.toLocal8Bit();
  if (!FInputData(ba.constData())) {
    QMessageBox::warning(gi.qwind, szAppName, "Could not read that chart file.");
    return;
  }
  RecastAndRedrawQt();
}


// Save the current chart to a file chosen via a standard file picker, the
// same way Windows' DlgSaveChart does for its "Save Chart" command.

void ShowSaveChartDialogQt()
{
  QString qs = QFileDialog::getSaveFileName(gi.qwind, "Save Chart", QString(),
    "Astrolog Chart Files (*.as);;All Files (*)");
  if (qs.isEmpty())
    return;
  QByteArray ba = qs.toLocal8Bit();
  is.szFileOut = SzClone((char *)ba.constData());
  us.nWriteFormat = 0;
  if (!FOutputData())
    QMessageBox::warning(gi.qwind, szAppName, "Could not write that chart file.");
}


// Save Chart Positions, equivalent to Windows' "Save Chart Positions"
// (part of DlgSaveChart, cmdSavePositions): same mechanism as Save Chart
// above, but us.nWriteFormat is set to the *character* '0' rather than
// left at its default (integer) 0 -- FOutputData() branches on this to
// write calculated positions instead of full chart info.

void ShowSaveChartPositionsDialogQt()
{
  QString qs = QFileDialog::getSaveFileName(gi.qwind, "Save Chart Positions",
    QString(), "Astrolog Chart Files (*.as);;All Files (*)");
  if (qs.isEmpty())
    return;
  QByteArray ba = qs.toLocal8Bit();
  FCloneSz(ba.constData(), &is.szFileOut);
  us.nWriteFormat = '0';
  if (!FOutputData())
    QMessageBox::warning(gi.qwind, szAppName, "Could not write that chart file.");
}


// Save Program Settings, equivalent to Windows' "Save Program Settings"
// (cmdSaveSettings): writes the current configuration as an Astrolog
// switch file, the same format loaded back via -i0 or by placing it at
// DEFAULT_INFOFILE.

void ShowSaveSettingsDialogQt()
{
  QString qs = QFileDialog::getSaveFileName(gi.qwind, "Save Program Settings",
    DEFAULT_INFOFILE, "Astrolog Chart Files (*.as);;All Files (*)");
  if (qs.isEmpty())
    return;
  QByteArray ba = qs.toLocal8Bit();
  FCloneSz(ba.constData(), &is.szFileOut);
  if (!FOutputSettings())
    QMessageBox::warning(gi.qwind, szAppName,
      "Could not write that settings file.");
}


// Save Chart Exchange (AAF) / Quick*Chart / iCalendar formats, equivalent
// to Windows' cmdSaveAAF/cmdSaveQuick/cmdSaveCalendar (part of
// DlgSaveChart) -- each just a file picker into one already-portable
// FOutputXxxFile() function.

void ShowSaveAAFDialogQt()
{
  QString qs = QFileDialog::getSaveFileName(gi.qwind,
    "Save Chart Exchange Format", QString(),
    "Astrological Exchange Files (*.aaf);;All Files (*)");
  if (qs.isEmpty())
    return;
  QByteArray ba = qs.toLocal8Bit();
  FCloneSz(ba.constData(), &is.szFileOut);
  if (!FOutputAAFFile())
    QMessageBox::warning(gi.qwind, szAppName, "Could not write that AAF file.");
}

void ShowSaveQuickDialogQt()
{
  QString qs = QFileDialog::getSaveFileName(gi.qwind,
    "Save Chart Quick*Chart Format", QString(),
    "Quick*Chart Files (*.qck);;All Files (*)");
  if (qs.isEmpty())
    return;
  QByteArray ba = qs.toLocal8Bit();
  FCloneSz(ba.constData(), &is.szFileOut);
  if (!FOutputQuickFile())
    QMessageBox::warning(gi.qwind, szAppName,
      "Could not write that Quick*Chart file.");
}

void ShowSaveCalendarDialogQt()
{
  QString qs = QFileDialog::getSaveFileName(gi.qwind,
    "Save Chart iCalendar Format", QString(),
    "iCalendar Files (*.ics);;All Files (*)");
  if (qs.isEmpty())
    return;
  QByteArray ba = qs.toLocal8Bit();
  FCloneSz(ba.constData(), &is.szFileOut);
  if (!FOutputCalendarFile())
    QMessageBox::warning(gi.qwind, szAppName,
      "Could not write that iCalendar file.");
}


// Open Chart Background / Open World Map, equivalent to Windows'
// DlgOpenChart when wi.nDlgChart <= 0: load a bitmap into gi.bmpBack or
// gi.bmpWorld instead of loading a chart. Open Chart Background also
// turns on gi.fBmp (Use Detailed World Map's underlying flag) the same
// way Windows' cmdOpenBackground handler does; Open World Map doesn't.

void ShowOpenBackgroundDialogQt()
{
  QString qs = QFileDialog::getOpenFileName(gi.qwind, "Open Background",
    QString(), "Windows Bitmaps (*.bmp);;All Files (*)");
  if (qs.isEmpty())
    return;
  QByteArray ba = qs.toLocal8Bit();
  if (!FLoadBmp(ba.constData(), &gi.bmpBack, fFalse)) {
    QMessageBox::warning(gi.qwind, szAppName, "Could not read that bitmap file.");
    return;
  }
  gi.fBmp = fTrue;
  RedrawQt();
}

void ShowOpenWorldDialogQt()
{
  QString qs = QFileDialog::getOpenFileName(gi.qwind, "Open World Map",
    QString(), "Windows Bitmaps (*.bmp);;All Files (*)");
  if (qs.isEmpty())
    return;
  QByteArray ba = qs.toLocal8Bit();
  if (!FLoadBmp(ba.constData(), &gi.bmpWorld, fFalse)) {
    QMessageBox::warning(gi.qwind, szAppName, "Could not read that bitmap file.");
    return;
  }
  RedrawQt();
}


// Export the current chart as a graphics file, equivalent to what Windows'
// DlgSaveChart does for its Export Chart Bitmap/Metafile/PostScript/SVG/
// Wireframe commands. All five formats fall out of one shared mechanism:
// FActionX() (xscreen.cpp) -- the same function invoked for -Xb/-Xp/etc
// command line switches -- derives gi.fFile from whether gs.ft is set, and
// take its "write to gi.szFileOut instead of drawing to screen" path when
// it is, so setting gs.ft and calling it is really all that's needed.
// FActionX() only restores gs.xWin/yWin/nScale for some formats (not
// bitmap) since normally it's only ever called once per process, right
// before that process exits -- here it can be called many times in one
// running session, so save/restore all three unconditionally regardless,
// and re-render the on-screen chart afterward.

static flag FExportChartQt(CONST char *szFile, int ft)
{
  int xWinSave = gs.xWin, yWinSave = gs.yWin, nScaleSave = gs.nScale;
  flag fGraphicsSave = us.fGraphics;
  flag f;

  FCloneSz(szFile, &is.szFileOut);
  FCloneSz(szFile, &gi.szFileOut);
  gs.ft = ft;
  us.fGraphics = fTrue;
  f = FActionX();
  gs.ft = ftNone;
  gi.fFile = fFalse;
  gs.xWin = xWinSave; gs.yWin = yWinSave; gs.nScale = nScaleSave;
  us.fGraphics = fGraphicsSave;
  RedrawQt();
  return f;
}

static void ShowExportGraphicsDialogQt(CONST char *szTitle,
  CONST char *szFilter, int ft)
{
  QString qs = QFileDialog::getSaveFileName(gi.qwind, szTitle, QString(),
    QString(szFilter) + ";;All Files (*)");
  if (qs.isEmpty())
    return;
  QByteArray ba = qs.toLocal8Bit();
  if (!FExportChartQt(ba.constData(), ft))
    QMessageBox::warning(gi.qwind, szAppName, "Could not write that file.");
}

void ShowExportBitmapDialogQt()
{
  gs.chBmpMode = 'B';
  ShowExportGraphicsDialogQt("Export Chart Bitmap",
    "Windows Bitmaps (*.bmp)", ftBmp);
}

void ShowExportMetafileDialogQt()
{
  ShowExportGraphicsDialogQt("Export Chart Metafile",
    "Windows Metafiles (*.wmf)", ftWmf);
}

void ShowExportPSDialogQt()
{
  ShowExportGraphicsDialogQt("Export Chart PostScript",
    "PostScript Files (*.eps *.ps)", ftPS);
}

void ShowExportSVGDialogQt()
{
  ShowExportGraphicsDialogQt("Export Chart SVG",
    "Scalable Vector Graphics (*.svg)", ftSVG);
}

void ShowExportWireDialogQt()
{
  ShowExportGraphicsDialogQt("Export Chart Wireframe",
    "Daedalus Wireframes (*.dw)", ftWire);
}


// Copy Vector Format (Metafile/PostScript/SVG/Wireframe), equivalent to
// Windows' cmdCopyPicture/cmdCopyPS/cmdCopySVG/cmdCopyWire: the same
// FExportChartQt() mechanism as the Export Vector Format menu above, to a
// scratch file that gets put on the clipboard and deleted instead of one
// the user chose to keep. PostScript/SVG/Wireframe are text formats, so
// plain text on the clipboard is directly useful (e.g. pasting the SVG
// source into a text editor); SVG also gets image/svg+xml set alongside,
// for apps that can paste it as an actual image. Metafile (a binary
// Windows format) has no broadly supported Linux clipboard equivalent, so
// it only gets the image/x-wmf MIME type, useful to the few apps that
// recognize it and otherwise harmless.

static void CopyChartVectorQt(int ft, CONST char *szMime)
{
  char szTemp[] = "/tmp/astrolog-qt-copy-XXXXXX";
  int fd = mkstemp(szTemp);
  if (fd < 0)
    return;
  close(fd);

  if (FExportChartQt(szTemp, ft)) {
    QFile file(szTemp);
    if (file.open(QIODevice::ReadOnly)) {
      QByteArray ba = file.readAll();
      file.close();
      QMimeData *pmime = new QMimeData();
      if (ft != ftWmf)
        pmime->setText(QString::fromUtf8(ba));
      if (szMime != NULL)
        pmime->setData(szMime, ba);
      QApplication::clipboard()->setMimeData(pmime);
    }
  }
  unlink(szTemp);
}

void CopyChartMetafileQt() { CopyChartVectorQt(ftWmf, "image/x-wmf"); }
void CopyChartPSQt()       { CopyChartVectorQt(ftPS, NULL); }
void CopyChartSVGQt()      { CopyChartVectorQt(ftSVG, "image/svg+xml"); }
void CopyChartWireQt()     { CopyChartVectorQt(ftWire, NULL); }


// Export the chart's text output (the same plain-text listing the -o0
// command line switch writes) to a file, equivalent to Windows' Export
// Chart Text Output. Text-mode chart output is a wholly separate rendering
// path from the graphics one (Action(), astrolog.cpp: "if (us.fGraphics)
// FActionX(); else PrintChart();"), driven by is.S/is.szFileScreen instead
// of gi.qpaint -- so unlike the graphics exports above, this doesn't (and
// can't) go through RedrawQt()/DrawChartX(). Calling Action() directly
// reuses that path exactly as the -o0/-os command line switches do; since
// the destination is a file, not the screen, this doesn't need the
// still-undesigned on-screen text display QT is missing (see the Help
// menu's list actions).

void ShowExportTextDialogQt()
{
  QString qs = QFileDialog::getSaveFileName(gi.qwind, "Export Chart Text",
    QString(), "Text Files (*.txt);;All Files (*)");
  if (qs.isEmpty())
    return;
  QByteArray ba = qs.toLocal8Bit();
  flag fGraphicsSave = us.fGraphics;
  FCloneSz(ba.constData(), &is.szFileScreen);
  us.fGraphics = fFalse;
  Action();
  us.fGraphics = fGraphicsSave;
  FCloneSz(NULL, &is.szFileScreen);
}


// File settings, equivalent to Windows' DlgFile. A curated subset of
// Windows' full field list -- skipped: "Export Bitmaps from Window
// Content" (wi.fBmpWindow), the antialias detail level (wi.nAntialias --
// note the antialias toggle itself, gs.fAntialias, is already exposed via
// Graphics > Chart Effects > Antialias Lines, just not this intensity
// knob), and "Don't Show Popup Messages" (wi.fNoPopup) -- all three live
// in the Win32-only WI struct. Also skipped: "Use Real System Fonts"
// (gs.nFontAll/gi.nFontPrev), since that depends on the same Windows GDI
// font enumeration system Graphics Settings' font pickers would need --
// no direct Linux/Qt equivalent, not worth a partial port for one
// checkbox with nothing underneath it.

void ShowFileSettingsDialogQt()
{
  QDialog dlg(gi.qwind);
  dlg.setWindowTitle("File Settings");
  QVBoxLayout *pouter = new QVBoxLayout(&dlg);
  int i;

  QCheckBox *pcbSmartSave = new QCheckBox(
    "Export Text and Print in Intuitive Manner");
  QCheckBox *pcbTextHTML = new QCheckBox("Export Text Files in HTML Format");
  QCheckBox *pcbBmpPNG = new QCheckBox("Export Bitmaps in PNG Format");
  QCheckBox *pcbPSComplete = new QCheckBox(
    "Export Encapsulated PostScript Files");
  QCheckBox *pcbWriteOld = new QCheckBox(
    "Save Chart Info Files in Old Style Format");
  QCheckBox *pcbNoBackDraw = new QCheckBox("Don't Show Background Bitmap");
  pcbSmartSave->setChecked(us.fSmartSave != 0);
  pcbTextHTML->setChecked(us.fTextHTML != 0);
  pcbBmpPNG->setChecked(gs.chBmpMode == 'P');
  pcbPSComplete->setChecked(!gs.fPSComplete);
  pcbWriteOld->setChecked(us.fWriteOld != 0);
  pcbNoBackDraw->setChecked(!gs.fBackDraw);
  for (QCheckBox *pcb : { pcbSmartSave, pcbTextHTML, pcbBmpPNG,
    pcbPSComplete, pcbWriteOld, pcbNoBackDraw })
    pouter->addWidget(pcb);

  QFormLayout *pform = new QFormLayout();
  QLineEdit *peThickAdjust = new QLineEdit(QString::number(gs.nThickAdjust));
  QLineEdit *peBackPct = new QLineEdit(QString::number(gs.rBackPct));
  QLineEdit *peADB = new QLineEdit(FSzSet(us.szADB) ? us.szADB : "");
  QLineEdit *pePaperX = new QLineEdit(SzLength(gs.xInch));
  QLineEdit *pePaperY = new QLineEdit(SzLength(gs.yInch));
  pform->addRow("Line Thickness Adjustment:", peThickAdjust);
  pform->addRow("Background Transparency Percent:", peBackPct);
  pform->addRow("Astrodatabank File Load Filter:", peADB);
  pform->addRow("Horizontal PostScript Paper Size:", pePaperX);
  pform->addRow("Vertical PostScript Paper Size:", pePaperY);
  pouter->addLayout(pform);

  QGroupBox *pgroupOrient = new QGroupBox("PostScript Paper Orientation");
  QVBoxLayout *pgrouplayout = new QVBoxLayout(pgroupOrient);
  QButtonGroup *pgroup = new QButtonGroup(&dlg);
  CONST char *rgszOrient[3] =
    { "Portrait", "Landscape", "Based on Chart Dimensions" };
  int nOrientCur = gs.nOrient == 0 ? 2 : (gs.nOrient > 0 ? 0 : 1);
  for (i = 0; i < 3; i++) {
    QRadioButton *prb = new QRadioButton(rgszOrient[i]);
    prb->setChecked(i == nOrientCur);
    pgroup->addButton(prb, i);
    pgrouplayout->addWidget(prb);
  }
  pouter->addWidget(pgroupOrient);

  QDialogButtonBox *pbuttons =
    new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  pouter->addWidget(pbuttons);
  QObject::connect(pbuttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(pbuttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  if (dlg.exec() != QDialog::Accepted)
    return;

  int nThickAdjust = peThickAdjust->text().toInt();
  real rBackPct = peBackPct->text().toDouble();
  if (!FValidBackPct(rBackPct)) {
    QMessageBox::warning(gi.qwind, szAppName,
      "The background transparency percent is invalid.");
    return;
  }

  us.fSmartSave = pcbSmartSave->isChecked();
  us.fTextHTML = pcbTextHTML->isChecked();
  if (pcbBmpPNG->isChecked())
    gs.chBmpMode = 'P';
  else if (gs.chBmpMode == 'P')
    gs.chBmpMode = 'B';
  gs.fPSComplete = !pcbPSComplete->isChecked();
  us.fWriteOld = pcbWriteOld->isChecked();
  gs.nThickAdjust = nThickAdjust;
  gs.fBackDraw = !pcbNoBackDraw->isChecked();
  gs.rBackPct = rBackPct;
  QByteArray baADB = peADB->text().toLocal8Bit();
  FCloneSz(baADB.constData(), &us.szADB);
  QByteArray baPaperX = pePaperX->text().toLocal8Bit();
  gs.xInch = RParseSz(baPaperX.constData(), pmLength);
  QByteArray baPaperY = pePaperY->text().toLocal8Bit();
  gs.yInch = RParseSz(baPaperY.constData(), pmLength);
  int nOrientSel = pgroup->checkedId();
  gs.nOrient = nOrientSel == 2 ? 0 : (nOrientSel == 0 ? 1 : -1);
  RedrawQt();
}


// Label lists for the Graphics Settings dialog below. Windows has these in
// wdialog.cpp, which isn't compiled into the QT build, so they're
// duplicated here rather than shared -- keep in sync with the originals if
// upstream ever adds a wheel decoration / fill / city coloring mode.

static CONST char *rgszCityColorQt[6] = {"None", "Region", "Region+State",
  "Generic Zone", "Current Zone", "Rainbow"};
static CONST char *rgszWheelCornerQt[7] = {"None", "Spider Web",
  "Moire Pattern", "Rays 1", "Rays 1,2", "Rays 12345", "Hearts"};
// Windows lists wheel corner types in this order rather than array order,
// so the combo shows the same sequence a Windows user would expect. The
// value stored is still the index into rgszWheelCornerQt[].
static CONST int rgiWheelCornerOrderQt[7] = {0, 1, 2, 6, 3, 4, 5};
static CONST char *rgszDecaFillQt[8] = {"None", "Standard", "Rainbow RGB",
  "Rainbow RYB", "Ruler Sign", "Ruler House", "7 Rays Sign", "7 Rays House"};


// Graphics settings, equivalent to Windows' DlgGraphics. Note several of
// these overlap menu items already built (Character Scale submenu, Map
// Orientation submenu, Modify Chart) -- that's deliberate and matches
// Windows: the menu items step values coarsely, while this dialog is where
// an exact value gets typed in. As elsewhere, no attempt is made to resync
// those menus' checkmarks afterward if a value set here doesn't line up
// with one of their preset choices (Windows' own DlgGraphics leans on a
// full RedoMenu() for that, which this port deliberately doesn't have).
//
// Skipped, as Win32-only (they live in the WI struct): the animation
// update delay (wi.nTimerDelay, a Win32 SetTimer interval) and "Don't
// Automatically Redraw Screen" (wi.fNoUpdate). Also skipped: the six font
// selection combos (gs.nFontTxt/Sig/Hou/Obj/Asp/Nak), which pick from a
// hardcoded list of Windows GDI font names -- porting them properly means
// building a real Qt font picker, not translating a list.

void ShowGraphicsSettingsDialogQt()
{
  QDialog dlg(gi.qwind);
  dlg.setWindowTitle("Graphics Settings");
  dlg.resize(460, 640);
  QVBoxLayout *pouter = new QVBoxLayout(&dlg);
  QScrollArea *pscroll = new QScrollArea(&dlg);
  QWidget *pinner = new QWidget();
  QVBoxLayout *pin = new QVBoxLayout(pinner);
  int i;

  QFormLayout *pformSize = new QFormLayout();
  QLineEdit *peWinX = new QLineEdit(QString::number(gs.xWin));
  QLineEdit *peWinY = new QLineEdit(QString::number(gs.yWin));
  pformSize->addRow("Horizontal Chart Size:", peWinX);
  pformSize->addRow("Vertical Chart Size:", peWinY);
  pin->addLayout(pformSize);
  QCheckBox *pcbKeepSquare =
    new QCheckBox("Ensure Square Charts Remain Square");
  pcbKeepSquare->setChecked(gs.fKeepSquare != 0);
  pin->addWidget(pcbKeepSquare);

  QFormLayout *pformScale = new QFormLayout();
  QLineEdit *peScale = new QLineEdit(QString::number(gs.nScale));
  QLineEdit *peScaleText = new QLineEdit(QString::number(gs.nScaleText));
  pformScale->addRow("Character Scale:", peScale);
  pformScale->addRow("Text Scale:", peScaleText);
  pin->addLayout(pformScale);
  QCheckBox *pcbAutoScale =
    new QCheckBox("Character Autoscale to Fit Window");
  pcbAutoScale->setChecked(gs.fAutoScale != 0);
  pin->addWidget(pcbAutoScale);

  QFormLayout *pformMisc = new QFormLayout();
  QLineEdit *peGridCell = new QLineEdit(QString::number(gs.nGridCell));
  QLineEdit *peSpace = new QLineEdit(QString::number(gs.cspace));
  QLineEdit *peTrack = new QLineEdit(gs.objTrack >= 0 ?
    szObjName[gs.objTrack] : "None");
  QLineEdit *peZoom = new QLineEdit(QString::number(gs.rspace));
  pformMisc->addRow("Number of Cells in Graphics Aspect Grid:", peGridCell);
  pformMisc->addRow("Solar System Orbit Trail Length:", peSpace);
  pformMisc->addRow("Telescope Focuses on This Object:", peTrack);
  pformMisc->addRow("Telescope and Orbit Zoom Scale:", peZoom);
  pin->addLayout(pformMisc);

  QGroupBox *pgbMap = new QGroupBox("Map and Globe");
  QVBoxLayout *pvMap = new QVBoxLayout(pgbMap);
  QFormLayout *pformMap = new QFormLayout();
  QLineEdit *peRot = new QLineEdit(QString::number(gs.rRot));
  QLineEdit *peTilt = new QLineEdit(QString::number(gs.rTilt));
  pformMap->addRow("Horizontal Map Degree Rotation:", peRot);
  pformMap->addRow("Vertical Globe Degree Tilt:", peTilt);
  pvMap->addLayout(pformMap);
  QCheckBox *pcbSouth =
    new QCheckBox("Globe Halves Focus on South Hemisphere");
  QCheckBox *pcbMollweide =
    new QCheckBox("World Map in Mollweide Projection");
  pcbSouth->setChecked(gs.fSouth != 0);
  pcbMollweide->setChecked(gs.fMollweide != 0);
  pvMap->addWidget(pcbSouth);
  pvMap->addWidget(pcbMollweide);
  pin->addWidget(pgbMap);

  QCheckBox *pcbAnimMap = new QCheckBox("Animate Map Instead of Time");
  pcbAnimMap->setChecked(gs.fAnimMap != 0);
  pin->addWidget(pcbAnimMap);

  QGroupBox *pgbStar = new QGroupBox("Full Star List");
  QVBoxLayout *pvStar = new QVBoxLayout(pgbStar);
  QCheckBox *pcbBigDots = new QCheckBox("Show Big Dots");
  QCheckBox *pcbStarName = new QCheckBox("Label with Name");
  pcbBigDots->setChecked(FOdd(gs.nAllStar));
  pcbStarName->setChecked((gs.nAllStar & 2) > 0);
  pvStar->addWidget(pcbBigDots);
  pvStar->addWidget(pcbStarName);
  pin->addWidget(pgbStar);

  QGroupBox *pgbRot = new QGroupBox("Wheel Chart Rotation");
  QVBoxLayout *pvRot = new QVBoxLayout(pgbRot);
  QButtonGroup *pgroupRot = new QButtonGroup(&dlg);
  CONST char *rgszRot[3] =
    { "None", "Object at Left Edge", "Object at Top Edge" };
  int nRotCur = gs.objLeft == 0 ? 0 : (gs.objLeft > 0 ? 1 : 2);
  for (i = 0; i < 3; i++) {
    QRadioButton *prb = new QRadioButton(rgszRot[i]);
    prb->setChecked(i == nRotCur);
    pgroupRot->addButton(prb, i);
    pvRot->addWidget(prb);
  }
  QFormLayout *pformRot = new QFormLayout();
  QLineEdit *peObjLeft = new QLineEdit(
    szObjName[gs.objLeft == 0 ? oSun : NAbs(gs.objLeft)-1]);
  pformRot->addRow("Use This Planet:", peObjLeft);
  pvRot->addLayout(pformRot);
  pin->addWidget(pgbRot);

  QGroupBox *pgbCorner = new QGroupBox("Wheel Corners");
  QFormLayout *pformCorner = new QFormLayout(pgbCorner);
  QComboBox *pcbCorner = new QComboBox();
  for (i = 0; i < 7; i++)
    pcbCorner->addItem(rgszWheelCornerQt[rgiWheelCornerOrderQt[i]]);
  for (i = 0; i < 7; i++)
    if (rgiWheelCornerOrderQt[i] == gs.nDecaType) {
      pcbCorner->setCurrentIndex(i);
      break;
    }
  QLineEdit *peDecaSize = new QLineEdit(QString::number(gs.nDecaSize));
  pformCorner->addRow("Type:", pcbCorner);
  pformCorner->addRow("Coverage:", peDecaSize);
  pin->addWidget(pgbCorner);

  QFormLayout *pformFill = new QFormLayout();
  QComboBox *pcbFill = new QComboBox();
  for (i = 0; i < 8; i++)
    pcbFill->addItem(rgszDecaFillQt[i]);
  pcbFill->setCurrentIndex(gs.nDecaFill);
  pformFill->addRow("Wheel Fill:", pcbFill);
  QComboBox *pcbCity = new QComboBox();
  for (i = 0; i < 6; i++)
    pcbCity->addItem(rgszCityColorQt[i]);
  pcbCity->setCurrentIndex(gs.fLabelCity ? gs.nLabelCity : 0);
  pformFill->addRow("Atlas City Coloring:", pcbCity);
  pin->addLayout(pformFill);

  // Six glyph-variant radio groups, all the same shape.
  CONST char *rgszGlyphTitle[6] = { "Capricorn Glyph", "Uranus Glyph",
    "Pluto Glyph", "Lilith Glyph", "Vertex Glyph", "Eris Glyph" };
  CONST int rgcGlyph[6] = { 2, 2, 3, 2, 2, 2 };
  CONST char *rgszGlyphOpt[6][3] = {
    { "American", "European", NULL },
    { "Herschel's", "Astronomy", NULL },
    { "Astrology", "Astronomy", "Esoteric" },
    { "Classic", "Standard", NULL },
    { "Classic", "Standard", NULL },
    { "Form One", "Form Two", NULL } };
  int *rgpnGlyph[6] = { &gs.nGlyphCap, &gs.nGlyphUra, &gs.nGlyphPlu,
    &gs.nGlyphLil, &gs.nGlyphVer, &gs.nGlyphEri };
  QButtonGroup *rgpgroupGlyph[6];
  int j;
  for (i = 0; i < 6; i++) {
    QGroupBox *pgb = new QGroupBox(rgszGlyphTitle[i]);
    QVBoxLayout *pv = new QVBoxLayout(pgb);
    rgpgroupGlyph[i] = new QButtonGroup(&dlg);
    for (j = 0; j < rgcGlyph[i]; j++) {
      QRadioButton *prb = new QRadioButton(rgszGlyphOpt[i][j]);
      prb->setChecked(j == *rgpnGlyph[i] - 1);
      rgpgroupGlyph[i]->addButton(prb, j);
      pv->addWidget(prb);
    }
    pin->addWidget(pgb);
  }

  pscroll->setWidget(pinner);
  pscroll->setWidgetResizable(true);
  pouter->addWidget(pscroll);

  QDialogButtonBox *pbuttons =
    new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  pouter->addWidget(pbuttons);
  QObject::connect(pbuttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(pbuttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  if (dlg.exec() != QDialog::Accepted)
    return;

  int nWinX = peWinX->text().toInt();
  int nWinY = peWinY->text().toInt();
  int nScale = peScale->text().toInt();
  int nScaleText = peScaleText->text().toInt();
  int nGridCell = peGridCell->text().toInt();
  int cspace = peSpace->text().toInt();
  int nDecaSize = peDecaSize->text().toInt();
  real rRot = peRot->text().toDouble();
  real rTilt = peTilt->text().toDouble();
  real rZoom = peZoom->text().toDouble();
  QByteArray baTrack = peTrack->text().toLocal8Bit();
  int objTrack = NParseSz(baTrack.constData(), pmObject);
  QByteArray baObjLeft = peObjLeft->text().toLocal8Bit();
  int objLeft = NParseSz(baObjLeft.constData(), pmObject);
  if (!FValidGraphX(nWinX) || !FValidGraphY(nWinY) ||
    !FValidScale(nScale) || !FValidScaleText(nScaleText) ||
    !FValidGrid(nGridCell) || !FValidDecaSize(nDecaSize) ||
    !FValidRotation(rRot) || !FValidTilt(rTilt) || !FValidZoom(rZoom) ||
    !FValidTelescope(objTrack) || !FItem(objLeft)) {
    QMessageBox::warning(gi.qwind, szAppName,
      "One or more graphics settings fields are invalid.");
    return;
  }

  flag fResize = (gs.xWin != nWinX || gs.yWin != nWinY);
  gs.xWin = nWinX; gs.yWin = nWinY;
  gs.fKeepSquare = pcbKeepSquare->isChecked();
  gs.nScale = nScale; gs.nScaleText = nScaleText;
  gs.fAutoScale = pcbAutoScale->isChecked();
  gs.nGridCell = nGridCell;
  // Changing the orbit trail length invalidates any trail already
  // accumulated at the old length, same as Windows does here.
  if (gs.cspace != cspace) {
    gs.cspace = cspace;
    if (gi.rgspace != NULL) {
      DeallocateP(gi.rgspace);
      gi.rgspace = NULL;
    }
  }
  gs.objTrack = objTrack;
  gs.rspace = rZoom;
  gs.rRot = rRot; gs.rTilt = rTilt;
  gs.fSouth = pcbSouth->isChecked();
  gs.fMollweide = pcbMollweide->isChecked();
  gs.fAnimMap = pcbAnimMap->isChecked();
  gs.nAllStar = (pcbStarName->isChecked() << 1) | pcbBigDots->isChecked();
  int nRotSel = pgroupRot->checkedId();
  gs.objLeft = nRotSel == 0 ? 0 :
    (nRotSel == 1 ? objLeft+1 : -objLeft-1);
  gs.nDecaType = rgiWheelCornerOrderQt[pcbCorner->currentIndex()];
  gs.nDecaSize = nDecaSize;
  gs.nDecaFill = pcbFill->currentIndex();
  // Windows' DlgGraphics writes gs.fLabelAsp here, but that field is -XA
  // ("draw aspect glyphs on lines", the Graphics / Chart Effects / Show
  // Glyphs on Aspect Lines toggle) and has nothing to do with city
  // coloring -- the -XL switch that owns gs.nLabelCity toggles
  // gs.fLabelCity (see xscreen.cpp). Treating that as an upstream typo
  // and using gs.fLabelCity, so this combo doesn't silently turn aspect
  // glyphs on and off.
  i = pcbCity->currentIndex();
  if (i <= 0)
    gs.fLabelCity = fFalse;
  else {
    gs.fLabelCity = fTrue;
    gs.nLabelCity = i;
  }
  for (i = 0; i < 6; i++)
    *rgpnGlyph[i] = rgpgroupGlyph[i]->checkedId() + 1;

  us.fGraphics = fTrue;
  if (fResize && gs.xWin > 0 && gs.yWin > 0)
    gi.qwind->resize(gs.xWin, gs.yWin);
  RedrawQt();
}


// Chart info entry, equivalent to Windows' DlgInfo. This is the dialog that
// lets someone actually create a chart interactively instead of only ever
// loading one from disk.

static void ShowChartInfoForQt(CI *pci, CONST char *szTitle)
{
  QDialog dlg(gi.qwind);
  dlg.setWindowTitle(szTitle);
  QFormLayout *playout = new QFormLayout(&dlg);

  // Every field is shown in the same human readable form Windows' DlgInfo
  // uses (SetEditMDYT/SetEditSZOA), not as a raw number: month as a name,
  // time as "9:54pm"/"21:54", zone as "8W", and longitude/latitude as
  // "122W19"/"47N36". Astrolog's own parsers accept exactly these forms
  // back (that's what the NParseSz/RParseSz calls below do), so this is
  // purely a display change -- no extra conversion needed either way.
  char sz[cchSzMax];
  QLineEdit *peName = new QLineEdit(FSzSet(pci->nam) ? pci->nam : "");
  QLineEdit *peLoc  = new QLineEdit(FSzSet(pci->loc) ? pci->loc : "");
  sprintf(sz, "%.3s", szMonth[FValidMon(pci->mon) ? pci->mon : 1]);
  QLineEdit *peMon  = new QLineEdit(sz);
  QLineEdit *peDay  = new QLineEdit(QString::number(pci->day));
  QLineEdit *peYea  = new QLineEdit(QString::number(pci->yea));
  QLineEdit *peTim  = new QLineEdit(SzTim(pci->tim));
  // Daylight saving has sentinel values (see astrolog.h's dstAuto) rather
  // than being a plain offset. Windows resolves dstAuto to its concrete
  // Yes/No via DstReal() before display, which silently discards the
  // user's "work it out for me" choice on the next OK -- show it as
  // "Autodetect" instead so it survives a round trip.
  QLineEdit *peDst  = new QLineEdit(pci->dst == 0.0 ? "No" :
    (pci->dst == 1.0 ? "Yes" :
    (pci->dst == dstAuto ? "Autodetect" : SzZone(pci->dst))));
  sprintf(sz, "%s", SzZone(pci->zon));
  QLineEdit *peZon  = new QLineEdit(sz[0] == '+' ? &sz[1] : sz);
  // SzLocation() returns longitude and latitude in one string split at
  // is.ichLocSplit. Force plain ASCII while formatting: otherwise it uses
  // a Latin-1/IBM degree byte that isn't valid UTF-8 (see the Charts
  // dialog for where that matters), and these fields want the compact
  // "122W19" form anyway.
  int nSavChar = us.fAnsiChar; us.fAnsiChar = fFalse;
  sprintf(sz, "%s", SzLocation(pci->lon, pci->lat));
  us.fAnsiChar = nSavChar;
  sz[is.ichLocSplit] = chNull;
  QLineEdit *peLon  = new QLineEdit(&sz[0]);
  QLineEdit *peLat  = new QLineEdit(&sz[is.ichLocSplit+1]);
  // Long names/locations otherwise show their tail end, not their start.
  peName->setCursorPosition(0);
  peLoc->setCursorPosition(0);

  playout->addRow("Month:", peMon);
  playout->addRow("Day:", peDay);
  playout->addRow("Year:", peYea);
  playout->addRow("Time:", peTim);
  playout->addRow("Daylight Saving:", peDst);
  playout->addRow("Time Zone:", peZon);
  playout->addRow("Longitude:", peLon);
  playout->addRow("Latitude:", peLat);
  playout->addRow("Name:", peName);
  playout->addRow("Location:", peLoc);

  QDialogButtonBox *pbuttons =
    new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  playout->addRow(pbuttons);
  QObject::connect(pbuttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(pbuttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  if (dlg.exec() != QDialog::Accepted)
    return;

  CI ci = *pci;
  QByteArray ba;
  ba = peMon->text().toLocal8Bit(); ci.mon = NParseSz(ba.constData(), pmMon);
  ba = peDay->text().toLocal8Bit(); ci.day = NParseSz(ba.constData(), pmDay);
  ba = peYea->text().toLocal8Bit(); ci.yea = NParseSz(ba.constData(), pmYea);
  ba = peTim->text().toLocal8Bit(); ci.tim = RParseSz(ba.constData(), pmTim);
  ba = peDst->text().toLocal8Bit(); ci.dst = RParseSz(ba.constData(), pmDst);
  ba = peZon->text().toLocal8Bit(); ci.zon = RParseSz(ba.constData(), pmZon);
  ba = peLon->text().toLocal8Bit(); ci.lon = RParseSz(ba.constData(), pmLon);
  ba = peLat->text().toLocal8Bit(); ci.lat = RParseSz(ba.constData(), pmLat);

  if (!FValidMon(ci.mon) || !FValidYea(ci.yea) ||
    !FValidDay(ci.day, ci.mon, ci.yea) || !FValidTim(ci.tim) ||
    !FValidDst(ci.dst) || !FValidZon(ci.zon) ||
    !FValidLon(ci.lon) || !FValidLat(ci.lat)) {
    QMessageBox::warning(gi.qwind, szAppName,
      "One or more chart info fields are invalid.");
    return;
  }
  ba = peName->text().toLocal8Bit(); ci.nam = SzClone((char *)ba.constData());
  ba = peLoc->text().toLocal8Bit();  ci.loc = SzClone((char *)ba.constData());

  *pci = ci;
  RecastAndRedrawQt();
}

void ShowChartInfoDialogQt()
{
  ShowChartInfoForQt(&ciCore, "Chart Info");
}

void ShowChartInfo2DialogQt()
{
  ShowChartInfoForQt(&ciTwin, "Chart #2 Info");
}


// Load a chart file into one of the six chart slots (rgpci/rgpcp, where
// slot 1 is the main chart and 2-6 are the extra rings a bi/tri/.../hexa
// wheel draws), equivalent to Windows' DlgOpenChart when wi.nDlgChart > 1.
// FInputData() always loads into ciCore, so for the extra slots the
// current chart is saved, the file is read, the result copied into the
// target slot, and ciCore put back -- same dance Windows does.

static flag FOpenChartIntoQt(int iChart, CONST char *szFile)
{
  CI ciT = ciCore;

  if (!FInputData(szFile)) {
    ciCore = ciT;
    return fFalse;
  }
  if (iChart <= 1)
    cp1 = cp0;
  else {
    *rgpci[iChart] = ciCore;
    *rgpcp[iChart] = cp0;
    ciCore = ciT;
  }
  return fTrue;
}

static void ShowOpenChartIntoDialogQt(int iChart)
{
  QString qsTitle = iChart <= 1 ? QString("Open Chart") :
    QString("Open Chart #%1").arg(iChart);
  QString qs = QFileDialog::getOpenFileName(gi.qwind, qsTitle, QString(),
    "Astrolog Chart Files (*.as);;All Files (*)");
  if (qs.isEmpty())
    return;
  QByteArray ba = qs.toLocal8Bit();
  if (!FOpenChartIntoQt(iChart, ba.constData())) {
    QMessageBox::warning(gi.qwind, szAppName, "Could not read that chart file.");
    return;
  }
  RecastAndRedrawQt();
}

void ShowOpenChart2DialogQt()
{
  ShowOpenChartIntoDialogQt(2);
}


// One chart's date/zone/location summary, as Windows formats it for the
// Charts and Chart List dialogs. Those force us.fAnsiChar/us.fGraphics so
// SzDate() and friends emit a real degree sign rather than the ASCII ':'
// fallback; do the same, and additionally pin us.nCharset to Latin-1 so
// the byte that comes back is predictable (ChDeg() would otherwise pick
// the IBM codepage degree at 0xF8 depending on the user's -Ya setting).
// That byte isn't valid UTF-8 either way, hence decoding as Latin-1.

static QString SzChartDateLineQt(CONST CI *pci)
{
  char sz[cchSzMax];
  int nSavChar = us.fAnsiChar, nSavSet = us.nCharset;
  flag fSav = us.fGraphics;

  us.fAnsiChar = 2; us.nCharset = ccLatin; us.fGraphics = fTrue;
  int nDay = DayOfWeek(pci->mon, pci->day, pci->yea);
  sprintf(sz, "%.3s %s %s (%cT Zone %s) %s", szDay[nDay],
    SzDate(pci->mon, pci->day, pci->yea, 3), SzTim(pci->tim),
    ChDst(pci->dst), SzZone(pci->zon), SzLocation(pci->lon, pci->lat));
  us.fAnsiChar = nSavChar; us.nCharset = nSavSet; us.fGraphics = fSav;
  return QString::fromLatin1(sz);
}


// The same chart's "name; location". Kept separate from the line above
// because this is user entered text that really may be UTF-8, so it must
// not be swept up in that function's Latin-1 decode.

static QString SzChartNameLineQt(CONST CI *pci)
{
  char sz[cchSzMax];

  sprintf(sz, "%s%s%s", FSzSet(pci->nam) ? pci->nam : "",
    FSzSet(pci->nam) && FSzSet(pci->loc) ? "; " : "",
    FSzSet(pci->loc) ? pci->loc : "");
  return QString::fromLocal8Bit(sz);
}


// The multi-chart manager, equivalent to Windows' DlgInfoAll ("Charts #3
// Through #6" on the Info menu, though it covers all six): a summary line
// per chart slot with buttons to load a file into it or edit its info,
// plus how many of those slots the wheel actually draws and which of them
// are progressed.

void ShowChartsAllDialogQt()
{
  QDialog dlg(gi.qwind);
  dlg.setWindowTitle("Charts");
  dlg.resize(640, 400);
  QVBoxLayout *pouter = new QVBoxLayout(&dlg);
  QGridLayout *pgrid = new QGridLayout();
  QLabel *rgplabel[cRing+1];
  int i;

  auto RefreshRow = [&rgplabel](int iChart) {
    CI *pci = rgpci[iChart];
    QString qs = SzChartDateLineQt(pci);
    QString qsName = SzChartNameLineQt(pci);
    if (!qsName.isEmpty())
      qs += "\n" + qsName;
    rgplabel[iChart]->setText(qs);
  };

  for (i = 1; i <= cRing; i++) {
    QPushButton *pbOpen = new QPushButton(i <= 1 ?
      QString("Open Chart...") : QString("Open Chart #%1...").arg(i));
    QPushButton *pbInfo = new QPushButton(i <= 1 ?
      QString("Set Chart Info...") : QString("Set Chart #%1 Info...").arg(i));
    rgplabel[i] = new QLabel();
    rgplabel[i]->setTextInteractionFlags(Qt::TextSelectableByMouse);
    pgrid->addWidget(pbOpen, i-1, 0);
    pgrid->addWidget(pbInfo, i-1, 1);
    pgrid->addWidget(rgplabel[i], i-1, 2);
    pgrid->setColumnStretch(2, 1);
    RefreshRow(i);
    int iChart = i;
    QObject::connect(pbOpen, &QPushButton::clicked, &dlg,
      [iChart, RefreshRow]() {
        ShowOpenChartIntoDialogQt(iChart);
        RefreshRow(iChart);
      });
    QObject::connect(pbInfo, &QPushButton::clicked, &dlg,
      [iChart, RefreshRow]() {
        QByteArray baTitle = (iChart <= 1 ? QString("Chart Info") :
          QString("Chart #%1 Info").arg(iChart)).toLocal8Bit();
        ShowChartInfoForQt(rgpci[iChart], baTitle.constData());
        RefreshRow(iChart);
      });
  }
  pouter->addLayout(pgrid);

  QHBoxLayout *phbox = new QHBoxLayout();
  QGroupBox *pgbWheel = new QGroupBox("Wheel Chart Is");
  QVBoxLayout *pvWheel = new QVBoxLayout(pgbWheel);
  QButtonGroup *pgroupWheel = new QButtonGroup(&dlg);
  CONST char *rgszWheel[6] = { "1: Single Wheel", "2: Dual Wheel",
    "3: Tri-Wheel", "4: Quad-Wheel", "5: Quin-Wheel", "6: Hexa-Wheel" };
  // us.nRel is 0 for a single wheel and counts down (rcDual is -1, through
  // rcHexaWheel at -5) for multi-wheels; the other rcXxx values are
  // unrelated relationship chart types, which show here as a single wheel.
  int nWheelCur = (us.nRel <= rcNone && us.nRel >= rcHexaWheel) ?
    -us.nRel : 0;
  for (i = 0; i < 6; i++) {
    QRadioButton *prb = new QRadioButton(rgszWheel[i]);
    prb->setChecked(i == nWheelCur);
    pgroupWheel->addButton(prb, i);
    pvWheel->addWidget(prb);
  }
  phbox->addWidget(pgbWheel);

  QGroupBox *pgbProg = new QGroupBox("Progress");
  QVBoxLayout *pvProg = new QVBoxLayout(pgbProg);
  QCheckBox *rgpcbProg[cRing+1];
  for (i = 2; i <= 5; i++) {
    rgpcbProg[i] = new QCheckBox(QString::number(i));
    rgpcbProg[i]->setChecked(rgfProg[i] != 0);
    pvProg->addWidget(rgpcbProg[i]);
  }
  phbox->addWidget(pgbProg);
  phbox->addStretch(1);
  pouter->addLayout(phbox);

  QDialogButtonBox *pbuttons =
    new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  pouter->addWidget(pbuttons);
  QObject::connect(pbuttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(pbuttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  if (dlg.exec() != QDialog::Accepted)
    return;

  for (i = 2; i <= 5; i++)
    rgfProg[i] = rgpcbProg[i]->isChecked();
  // SetRelQt() recasts and redraws, and syncs the Info menu's relationship
  // radios for the values that have one. The multi-wheel counts past
  // rcDual don't appear in that menu, so picking one leaves whichever
  // relationship item was checked there showing stale -- same
  // menu-vs-dialog staleness accepted elsewhere in this port.
  SetRelQt(-pgroupWheel->checkedId());
}


// Chart list, equivalent to Windows' DlgList: the list of charts held in
// memory (is.rgci / is.cci), which FInputData() populates automatically
// when it reads a multi chart file (AAF, Quick*Chart, Astrodatabank, or
// Solar Fire text -- see the FAppendCIList() calls in io.cpp), and which
// Open Charts in Folder also fills. Charts can be sorted, filtered,
// edited, deleted, loaded into any of the six chart slots, or copied back
// out of one.

// Case insensitive substring test matching Windows' filter loop, guarding
// the NULL name/location that Windows' version would dereference.
static flag FChartFieldMatchQt(CONST char *szField, CONST char *szFind)
{
  int j;

  if (!FSzSet(szFind))
    return fTrue;
  if (!FSzSet(szField))
    return fFalse;
  for (j = 0; szField[j]; j++)
    if (FEqSzSubI(szFind, &szField[j]))
      return fTrue;
  return fFalse;
}

void ShowChartListDialogQt()
{
  QDialog dlg(gi.qwind);
  dlg.setWindowTitle("Chart List");
  dlg.resize(900, 560);
  QVBoxLayout *pouter = new QVBoxLayout(&dlg);
  QHBoxLayout *phMain = new QHBoxLayout();
  QListWidget *plist = new QListWidget();
  QLabel *plabelSize = new QLabel();
  int i;

  phMain->addWidget(plist, 1);
  QVBoxLayout *pvSide = new QVBoxLayout();
  pvSide->addWidget(plabelSize);

  QGroupBox *pgbSort = new QGroupBox("Sort By");
  QVBoxLayout *pvSort = new QVBoxLayout(pgbSort);
  QButtonGroup *pgroupSort = new QButtonGroup(&dlg);
  CONST char *rgszSort[5] =
    { "Date", "Longitude", "Latitude", "Name", "Location" };
  for (i = 0; i < 5; i++) {
    QRadioButton *prb = new QRadioButton(rgszSort[i]);
    prb->setChecked(i == 0);
    pgroupSort->addButton(prb, i);
    pvSort->addWidget(prb);
  }
  pvSide->addWidget(pgbSort);
  QPushButton *pbSort = new QPushButton("Sort List");
  pvSide->addWidget(pbSort);

  QGroupBox *pgbSlot = new QGroupBox("Chart Slot");
  QVBoxLayout *pvSlot = new QVBoxLayout(pgbSlot);
  QButtonGroup *pgroupSlot = new QButtonGroup(&dlg);
  CONST char *rgszSlot[cRing] =
    { "1st", "2nd", "3rd", "4th", "5th", "6th" };
  for (i = 0; i < cRing; i++) {
    QRadioButton *prb = new QRadioButton(rgszSlot[i]);
    prb->setChecked(i == 0);
    pgroupSlot->addButton(prb, i);
    pvSlot->addWidget(prb);
  }
  pvSide->addWidget(pgbSlot);

  QPushButton *pbSet = new QPushButton("Set To Slot");
  QPushButton *pbCopy = new QPushButton("Copy From");
  QPushButton *pbEdit = new QPushButton("Edit Chart...");
  QPushButton *pbDel = new QPushButton("Delete Chart");
  QPushButton *pbDelAll = new QPushButton("Delete All");
  for (QPushButton *pb : { pbSet, pbCopy, pbEdit, pbDel, pbDelAll })
    pvSide->addWidget(pb);
  pvSide->addStretch(1);
  phMain->addLayout(pvSide);
  pouter->addLayout(phMain);

  QHBoxLayout *phFilter = new QHBoxLayout();
  QLineEdit *peName = new QLineEdit();
  QLineEdit *peLoc = new QLineEdit();
  QPushButton *pbFilter = new QPushButton("Filter");
  QPushButton *pbUnfilter = new QPushButton("Remove Filter");
  phFilter->addWidget(new QLabel("Name:"));
  phFilter->addWidget(peName, 1);
  phFilter->addWidget(new QLabel("Location:"));
  phFilter->addWidget(peLoc, 1);
  phFilter->addWidget(pbFilter);
  phFilter->addWidget(pbUnfilter);
  pouter->addLayout(phFilter);

  // Whether the list is currently showing a filtered view. Windows only
  // applies a filter permanently (FilterCIList, which actually discards
  // the non matching charts) on OK with nothing selected; until then the
  // filter is just a view over the full list.
  flag fFilter = fFalse;

  auto RefreshList = [&](flag fApplyFilter) {
    QByteArray baName = peName->text().toLocal8Bit();
    QByteArray baLoc = peLoc->text().toLocal8Bit();
    int iSel = plist->currentRow(), cShown = 0, j;
    plist->clear();
    for (j = 0; j < is.cci; j++) {
      CI *pci = &is.rgci[j];
      if (fApplyFilter &&
        (!FChartFieldMatchQt(pci->nam, baName.constData()) ||
        !FChartFieldMatchQt(pci->loc, baLoc.constData())))
        continue;
      QString qs = SzChartDateLineQt(pci);
      QString qsName = SzChartNameLineQt(pci);
      if (!qsName.isEmpty())
        qs += " " + qsName;
      QListWidgetItem *pitem = new QListWidgetItem(qs, plist);
      pitem->setData(Qt::UserRole, j);
      cShown++;
    }
    if (cShown <= 0) {
      QListWidgetItem *pitem =
        new QListWidgetItem("(No charts in list)", plist);
      pitem->setData(Qt::UserRole, -1);
    }
    plabelSize->setText(QString("List size: %1").arg(cShown));
    if (iSel >= 0 && iSel < plist->count())
      plist->setCurrentRow(iSel);
  };

  // Index into is.rgci of the selected row, or -1 for none/placeholder.
  auto ISelected = [&plist]() -> int {
    QListWidgetItem *pitem = plist->currentItem();
    return pitem == NULL ? -1 : pitem->data(Qt::UserRole).toInt();
  };

  auto LoadIntoSlot = [&](int iList) {
    CI ciT = is.rgci[iList];
    int iSlot = pgroupSlot->checkedId() + 1;
    is.iciCur = iList;
    *rgpci[iSlot] = ciT;
    if (iSlot == 1)
      ciCore = ciT;
  };

  RefreshList(fFalse);

  QObject::connect(pbSort, &QPushButton::clicked, &dlg, [&]() {
    FSortCIList(pgroupSort->checkedId());
    RefreshList(fFilter);
  });
  QObject::connect(pbDelAll, &QPushButton::clicked, &dlg, [&]() {
    is.cci = 0;
    RefreshList(fFilter);
  });
  QObject::connect(pbFilter, &QPushButton::clicked, &dlg, [&]() {
    fFilter = fTrue;
    RefreshList(fTrue);
  });
  QObject::connect(pbUnfilter, &QPushButton::clicked, &dlg, [&]() {
    fFilter = fFalse;
    peName->clear(); peLoc->clear();
    RefreshList(fFalse);
  });
  QObject::connect(pbSet, &QPushButton::clicked, &dlg, [&]() {
    int iList = ISelected();
    if (iList < 0) {
      QMessageBox::warning(gi.qwind, szAppName,
        "Can't do operation because no chart in list is selected.");
      return;
    }
    LoadIntoSlot(iList);
    RecastAndRedrawQt();
  });
  QObject::connect(pbCopy, &QPushButton::clicked, &dlg, [&]() {
    FAppendCIList(rgpci[pgroupSlot->checkedId() + 1]);
    RefreshList(fFilter);
    plist->setCurrentRow(plist->count() - 1);
  });
  QObject::connect(pbEdit, &QPushButton::clicked, &dlg, [&]() {
    int iList = ISelected();
    if (iList < 0) {
      QMessageBox::warning(gi.qwind, szAppName,
        "Can't do operation because no chart in list is selected.");
      return;
    }
    QByteArray baTitle =
      QString("Chart List #%1 Info").arg(iList + 1).toLocal8Bit();
    ShowChartInfoForQt(&is.rgci[iList], baTitle.constData());
    RefreshList(fFilter);
  });
  QObject::connect(pbDel, &QPushButton::clicked, &dlg, [&]() {
    int iList = ISelected();
    if (iList < 0) {
      QMessageBox::warning(gi.qwind, szAppName,
        "Can't do operation because no chart in list is selected.");
      return;
    }
    CopyRgb((pbyte)&is.rgci[iList+1], (pbyte)&is.rgci[iList],
      (is.cci-1-iList)*sizeof(CI));
    is.cci--;
    RefreshList(fFilter);
  });

  QDialogButtonBox *pbuttons =
    new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  pouter->addWidget(pbuttons);
  QObject::connect(pbuttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(pbuttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  if (dlg.exec() != QDialog::Accepted)
    return;

  // Read the selection after exec() returns rather than from a second
  // "accepted" handler: the dialog is still on the stack here, so its
  // widgets are alive, and this doesn't depend on which order two slots
  // connected to the same signal happen to run in.
  int iOk = ISelected();
  if (iOk >= 0) {
    LoadIntoSlot(iOk);
    RecastAndRedrawQt();
  } else if (fFilter) {
    // Windows only commits the filter (which actually drops the charts
    // that don't match) when OK is pressed with nothing selected.
    QByteArray baName = peName->text().toLocal8Bit();
    QByteArray baLoc = peLoc->text().toLocal8Bit();
    FilterCIList(baName.constData(), baLoc.constData());
  }
}


// Load every chart file in a folder into the chart list, equivalent to
// Windows' DlgOpenDir. Windows builds this on FindFirstFile inside a
// WSETUP-only block; QDir does the same job portably. Astrolog's own
// default data files are skipped, same as there, since a folder of charts
// often sits alongside them.

void ShowOpenChartDirDialogQt()
{
  QString qsDir = QFileDialog::getExistingDirectory(gi.qwind,
    "Open Charts in Folder");
  if (qsDir.isEmpty())
    return;

  QDir dir(qsDir);
  // Both cases: QDir's name filters are case sensitive on Linux, and
  // chart files copied from a Windows install are often ".AS".
  QStringList qslFiles = dir.entryList(QStringList() << "*.as" << "*.AS",
    QDir::Files, QDir::Name);
  CI ciT = ciCore;
  int cAdded = 0;

  for (CONST QString &qsFile : qslFiles) {
    if (qsFile.compare(DEFAULT_INFOFILE, Qt::CaseInsensitive) == 0 ||
      qsFile.compare(DEFAULT_ATLASFILE, Qt::CaseInsensitive) == 0 ||
      qsFile.compare(DEFAULT_TIMECHANGE, Qt::CaseInsensitive) == 0)
      continue;
    QByteArray ba = dir.filePath(qsFile).toLocal8Bit();
    if (!FInputData(ba.constData()))
      break;
    if (!FAppendCIList(&ciCore))
      break;
    cAdded++;
  }
  ciCore = ciT;

  if (cAdded <= 0) {
    QMessageBox::warning(gi.qwind, szAppName,
      "No chart files were loaded from that folder.");
    return;
  }
  RecastAndRedrawQt();
}


// Save the chart list, equivalent to Windows' cmdSaveList: the same
// FOutputData() path as the other save formats, with the chart list
// write format selected.

void ShowSaveChartListDialogQt()
{
  if (is.cci <= 0) {
    QMessageBox::warning(gi.qwind, szAppName,
      "There is no chart list in memory.");
    return;
  }
  QString qs = QFileDialog::getSaveFileName(gi.qwind, "Save Chart List",
    QString(), "Astrolog Chart Files (*.as);;All Files (*)");
  if (qs.isEmpty())
    return;
  QByteArray ba = qs.toLocal8Bit();
  FCloneSz(ba.constData(), &is.szFileOut);
  us.nWriteFormat = 'l';
  if (!FOutputData())
    QMessageBox::warning(gi.qwind, szAppName,
      "Could not write that chart list file.");
}


// Colors, equivalent to Windows' DlgColor: the 16 slot standard palette,
// the four element colors, the seven ray colors, and the scribble pen and
// wheel corner colors. Each is set by color name the same way SzColor()
// and NParseSz() format and parse them elsewhere in Astrolog.

// Adds one color field: an editable combo listing the color names, since
// that is what Windows offers here. "fExtra" widens the list to include
// the handful of symbolic entries past the plain colors (Element, Ray,
// Star, Planet, Auto), which only the wheel corner color accepts.
static QComboBox *AddColorComboQt(QFormLayout *pform, CONST char *szLabel,
  KI ki, flag fExtra)
{
  QComboBox *pcb = new QComboBox();
  int i, iMax = fExtra ? cColor2 + 5 : cColor2;

  pcb->setEditable(true);
  // addItem() before setEditText(): see the Progressions dialog.
  for (i = 0; i < iMax; i++)
    pcb->addItem(szColor[i]);
  pcb->setEditText(SzColor(ki));
  pform->addRow(QString(szLabel) + ":", pcb);
  return pcb;
}

static int NColorFromComboQt(QComboBox *pcb)
{
  QByteArray ba = pcb->currentText().toLocal8Bit();
  return NParseSz(ba.constData(), pmColor);
}

void ShowColorDialogQt()
{
  // The palette slots are named for the color they nominally represent,
  // but each one maps through ikPalette[] to an entry in either kMainA[]
  // or kRainbowA[] -- the dialog remaps which actual color each slot
  // renders as, so the label and the current value need not agree.
  static CONST char *rgszPalette[cColor] = {
    "Black", "White", "Red", "Green", "Blue", "Yellow", "Magenta", "Cyan",
    "Gray", "Lt. Gray", "Maroon", "Dk. Green", "Dk. Blue", "Maize",
    "Purple", "Dk. Cyan" };
  static CONST char *rgszElem[cElem] = { "Fire", "Earth", "Air", "Water" };
  static CONST char *rgszRay[cRay] =
    { "1st", "2nd", "3rd", "4th", "5th", "6th", "7th" };
  QDialog dlg(gi.qwind);
  dlg.setWindowTitle("Colors");
  dlg.resize(720, 640);
  QVBoxLayout *pouter = new QVBoxLayout(&dlg);
  QScrollArea *pscroll = new QScrollArea(&dlg);
  QWidget *pinner = new QWidget();
  QHBoxLayout *phInner = new QHBoxLayout(pinner);
  QVBoxLayout *pvLeft = new QVBoxLayout();
  QVBoxLayout *pvRight = new QVBoxLayout();
  QComboBox *rgpcbPalette[cColor];
  QComboBox *rgpcbElem[cElem];
  QComboBox *rgpcbRay[cRay+1];
  int i, j;

  QGroupBox *pgbPalette = new QGroupBox("Standard Color Palette");
  QFormLayout *pformPalette = new QFormLayout(pgbPalette);
  for (i = 0; i < cColor; i++) {
    j = ikPalette[i];
    rgpcbPalette[i] = AddColorComboQt(pformPalette, rgszPalette[i],
      j <= 0 ? kMainA[-j] : kRainbowA[j], fFalse);
  }
  pvLeft->addWidget(pgbPalette);

  QGroupBox *pgbExtra = new QGroupBox("Other");
  QFormLayout *pformExtra = new QFormLayout(pgbExtra);
  QComboBox *pcbPen = AddColorComboQt(pformExtra, "Scribble", gi.kiPen,
    fFalse);
  QComboBox *pcbDeca = AddColorComboQt(pformExtra, "Corners", gs.kiDeca,
    fTrue);
  pvLeft->addWidget(pgbExtra);
  pvLeft->addStretch(1);

  QGroupBox *pgbElem = new QGroupBox("Elements");
  QFormLayout *pformElem = new QFormLayout(pgbElem);
  for (i = 0; i < cElem; i++)
    rgpcbElem[i] = AddColorComboQt(pformElem, rgszElem[i], kElemA[i],
      fFalse);
  pvRight->addWidget(pgbElem);

  QGroupBox *pgbRay = new QGroupBox("Seven Rays");
  QFormLayout *pformRay = new QFormLayout(pgbRay);
  for (i = 1; i <= cRay; i++)
    rgpcbRay[i] = AddColorComboQt(pformRay, rgszRay[i-1], kRayA[i], fFalse);
  pvRight->addWidget(pgbRay);
  pvRight->addStretch(1);

  phInner->addLayout(pvLeft);
  phInner->addLayout(pvRight);
  pscroll->setWidget(pinner);
  pscroll->setWidgetResizable(true);
  pouter->addWidget(pscroll);

  QDialogButtonBox *pbuttons =
    new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  pouter->addWidget(pbuttons);
  QObject::connect(pbuttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(pbuttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  if (dlg.exec() != QDialog::Accepted)
    return;

  // Validate everything before writing anything, so a single bad entry
  // can't leave the palette half updated.
  int rgkPalette[cColor], rgkElem[cElem], rgkRay[cRay+1], kPen, kDeca;
  flag fOK = fTrue;
  for (i = 0; i < cColor; i++) {
    rgkPalette[i] = NColorFromComboQt(rgpcbPalette[i]);
    fOK &= FValidColorA(rgkPalette[i]);
  }
  for (i = 0; i < cElem; i++) {
    rgkElem[i] = NColorFromComboQt(rgpcbElem[i]);
    fOK &= FValidColorA(rgkElem[i]);
  }
  for (i = 1; i <= cRay; i++) {
    rgkRay[i] = NColorFromComboQt(rgpcbRay[i]);
    fOK &= FValidColorA(rgkRay[i]);
  }
  kPen = NColorFromComboQt(pcbPen);
  fOK &= FValidColorA(kPen);
  kDeca = NColorFromComboQt(pcbDeca);
  fOK &= (FValidColorA(kDeca) || kDeca == kMax);
  if (!fOK) {
    QMessageBox::warning(gi.qwind, szAppName,
      "One or more colors were not understood.");
    return;
  }

  for (i = 0; i < cColor; i++) {
    j = ikPalette[i];
    if (j <= 0)
      kMainA[-j] = rgkPalette[i];
    else
      kRainbowA[j] = rgkPalette[i];
  }
  for (i = 0; i < cElem; i++)
    kElemA[i] = rgkElem[i];
  for (i = 1; i <= cRay; i++)
    kRayA[i] = rgkRay[i];
  gi.kiPen = kPen;
  gs.kiDeca = kDeca;
  InitColorsX();
  RedrawQt();
}


// Object display, equivalent to Windows' DlgObject: per object maximum orb,
// orb addition, and color, for the core planets and angles (object indices
// 0 through oCore).

void ShowObjectDialogQt()
{
  QDialog dlg(gi.qwind);
  dlg.setWindowTitle("Objects");
  dlg.resize(500, 500);
  QVBoxLayout *pouter = new QVBoxLayout(&dlg);
  QScrollArea *pscroll = new QScrollArea(&dlg);
  QWidget *pinner = new QWidget();
  QGridLayout *pgrid = new QGridLayout(pinner);
  QLineEdit *rgpeOrb[oCore+1];
  QLineEdit *rgpeAdd[oCore+1];
  QLineEdit *rgpeColor[oCore+1];
  int i;

  pgrid->addWidget(new QLabel("Object"), 0, 0);
  pgrid->addWidget(new QLabel("Max Orb"), 0, 1);
  pgrid->addWidget(new QLabel("Orb Add"), 0, 2);
  pgrid->addWidget(new QLabel("Color"), 0, 3);
  for (i = 0; i <= oCore; i++) {
    pgrid->addWidget(new QLabel(szObjName[i]), i+1, 0);
    rgpeOrb[i] = new QLineEdit(QString::number(rObjOrb[i]));
    pgrid->addWidget(rgpeOrb[i], i+1, 1);
    rgpeAdd[i] = new QLineEdit(QString::number(rObjAdd[i]));
    pgrid->addWidget(rgpeAdd[i], i+1, 2);
    rgpeColor[i] = new QLineEdit(SzColor(kObjU[i]));
    pgrid->addWidget(rgpeColor[i], i+1, 3);
  }
  pscroll->setWidget(pinner);
  pscroll->setWidgetResizable(true);
  pouter->addWidget(pscroll);

  QDialogButtonBox *pbuttons =
    new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  pouter->addWidget(pbuttons);
  QObject::connect(pbuttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(pbuttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  if (dlg.exec() != QDialog::Accepted)
    return;

  for (i = 0; i <= oCore; i++) {
    rObjOrb[i] = rgpeOrb[i]->text().toDouble();
    rObjAdd[i] = rgpeAdd[i]->text().toDouble();
    QByteArray ba = rgpeColor[i]->text().toLocal8Bit();
    kObjU[i] = NParseSz(ba.constData(), pmColor);
  }
  RecastAndRedrawQt();
}


// Object restriction (show/hide), equivalent to Windows' DlgRestrict --
// shared by the main Restrictions, Star Restrictions, and Transit
// Restrictions menu items, which differ only in object range and which
// ignore array they edit, same as Windows' one DlgRestrict does based on
// wi.wCmd.

static void ShowRestrictRangeDialogQt(CONST char *szTitle, int lo, int hi,
  byte *rgignore)
{
  QDialog dlg(gi.qwind);
  dlg.setWindowTitle(szTitle);
  dlg.resize(300, 500);
  QVBoxLayout *pouter = new QVBoxLayout(&dlg);
  QScrollArea *pscroll = new QScrollArea(&dlg);
  QWidget *pinner = new QWidget();
  QVBoxLayout *pinnerlayout = new QVBoxLayout(pinner);
  QVector<QCheckBox *> rgpcb;
  int i;

  for (i = lo; i <= hi; i++) {
    QCheckBox *pcb = new QCheckBox(QString("Show ") + szObjName[i]);
    pcb->setChecked(!rgignore[i]);
    pinnerlayout->addWidget(pcb);
    rgpcb.append(pcb);
  }
  pscroll->setWidget(pinner);
  pscroll->setWidgetResizable(true);
  pouter->addWidget(pscroll);

  QDialogButtonBox *pbuttons =
    new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  pouter->addWidget(pbuttons);
  QObject::connect(pbuttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(pbuttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  if (dlg.exec() != QDialog::Accepted)
    return;

  for (i = lo; i <= hi; i++)
    rgignore[i] = !rgpcb[i-lo]->isChecked();
  AdjustRestrictions();
  RecastAndRedrawQt();
}

void ShowRestrictDialogQt()
{
  ShowRestrictRangeDialogQt("Restrictions", 0, oCore, ignore);
}

void ShowStarRestrictDialogQt()
{
  ShowRestrictRangeDialogQt("Star Restrictions", starLo, starHi, ignore);
}

void ShowTransitRestrictDialogQt()
{
  ShowRestrictRangeDialogQt("Transit Restrictions", 0, oCore, ignore2);
}


// Default chart info, equivalent to Windows' DlgDefault: the location/
// elevation/temperature/etc that a new chart starts out with, and what
// "now" charts use for a location.

void ShowDefaultInfoDialogQt()
{
  QDialog dlg(gi.qwind);
  dlg.setWindowTitle("Default Chart Info");
  QFormLayout *playout = new QFormLayout(&dlg);

  QLineEdit *peName = new QLineEdit(FSzSet(ciDefa.nam) ? ciDefa.nam : "");
  QLineEdit *peLoc  = new QLineEdit(FSzSet(ciDefa.loc) ? ciDefa.loc : "");
  // Formatted the way Windows' DlgDefault does it (SetEditSZOA), not as
  // raw numbers -- see ShowChartInfoForQt() for the same treatment and
  // the reasoning about SzLocation()'s degree byte.
  char sz[cchSzMax];
  QLineEdit *peDst  = new QLineEdit(ciDefa.dst == 0.0 ? "No" :
    (ciDefa.dst == 1.0 ? "Yes" :
    (ciDefa.dst == dstAuto ? "Autodetect" : SzZone(ciDefa.dst))));
  sprintf(sz, "%s", SzZone(ciDefa.zon));
  QLineEdit *peZon  = new QLineEdit(sz[0] == '+' ? &sz[1] : sz);
  int nSavChar = us.fAnsiChar; us.fAnsiChar = fFalse;
  sprintf(sz, "%s", SzLocation(ciDefa.lon, ciDefa.lat));
  us.fAnsiChar = nSavChar;
  sz[is.ichLocSplit] = chNull;
  QLineEdit *peLon  = new QLineEdit(&sz[0]);
  QLineEdit *peLat  = new QLineEdit(&sz[is.ichLocSplit+1]);
  QLineEdit *peElv  = new QLineEdit(SzElevation(us.elvDef));
  QLineEdit *peTmp  = new QLineEdit(SzTemperature(us.tmpDef));
  QLineEdit *peCor  = new QLineEdit(QString::number(us.lTimeAddition));
  peName->setCursorPosition(0);
  peLoc->setCursorPosition(0);

  playout->addRow("Daylight Saving:", peDst);
  playout->addRow("Time Zone:", peZon);
  playout->addRow("Longitude:", peLon);
  playout->addRow("Latitude:", peLat);
  playout->addRow("Elevation:", peElv);
  playout->addRow("Temperature:", peTmp);
  playout->addRow("\"Now\" minute offset:", peCor);
  playout->addRow("Name:", peName);
  playout->addRow("Location:", peLoc);

  QDialogButtonBox *pbuttons =
    new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  playout->addRow(pbuttons);
  QObject::connect(pbuttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(pbuttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  if (dlg.exec() != QDialog::Accepted)
    return;

  CI ci = ciDefa;
  QByteArray ba;
  ba = peDst->text().toLocal8Bit(); ci.dst = RParseSz(ba.constData(), pmDst);
  ba = peZon->text().toLocal8Bit(); ci.zon = RParseSz(ba.constData(), pmZon);
  ba = peLon->text().toLocal8Bit(); ci.lon = RParseSz(ba.constData(), pmLon);
  ba = peLat->text().toLocal8Bit(); ci.lat = RParseSz(ba.constData(), pmLat);

  if (!FValidDst(ci.dst) || !FValidZon(ci.zon) ||
    !FValidLon(ci.lon) || !FValidLat(ci.lat)) {
    QMessageBox::warning(gi.qwind, szAppName,
      "One or more chart info fields are invalid.");
    return;
  }
  ba = peElv->text().toLocal8Bit(); us.elvDef = RParseSz(ba.constData(), pmElv);
  ba = peTmp->text().toLocal8Bit(); us.tmpDef = RParseSz(ba.constData(), pmTmp);
  us.lTimeAddition = peCor->text().toLong();
  ciDefa = ci;
  ba = peName->text().toLocal8Bit(); ciDefa.nam = SzClone((char *)ba.constData());
  ba = peLoc->text().toLocal8Bit();  ciDefa.loc = SzClone((char *)ba.constData());

  RecastAndRedrawQt();
}


// Transits, equivalent to Windows' DlgTransit: which transit chart type
// to show (if any), the date/time to transit to (ciTran), how much time
// the search covers, which kinds of event the search reports, and a few
// display options. Location comes from the default chart info the same
// way Windows does it.

void ShowTransitDialogQt()
{
  QDialog dlg(gi.qwind);
  dlg.setWindowTitle("Transits");
  QVBoxLayout *pouter = new QVBoxLayout(&dlg);
  QHBoxLayout *phTop = new QHBoxLayout();
  QVBoxLayout *pvLeft = new QVBoxLayout();
  QVBoxLayout *pvRight = new QVBoxLayout();
  char sz[cchSzMax];
  int i;

  QGroupBox *pgbType = new QGroupBox("Transit Chart Type");
  QVBoxLayout *pvType = new QVBoxLayout(pgbType);
  QButtonGroup *pgroup = new QButtonGroup(&dlg);
  CONST char *rgszDayType[7] = { "None",
    "Transit to Transit Times", "Transit to Transit Influence",
    "Transit to Transit Graph", "Transit to Natal Times",
    "Transit to Natal Influence", "Transit to Natal Graph" };
  int n1 = us.fInDay ? 1 : (us.fInDayInf ? 2 : (us.fInDayGra ? 3 :
    (us.fTransit ? 4 : (us.fTransitInf ? 5 : (us.fTransitGra ? 6 : 0)))));
  for (i = 0; i < 7; i++) {
    QRadioButton *prb = new QRadioButton(rgszDayType[i]);
    prb->setChecked(i == n1);
    pgroup->addButton(prb, i);
    pvType->addWidget(prb);
  }
  pvLeft->addWidget(pgbType);

  // Same human readable formatting as the chart info dialog -- month by
  // name, time as "9:54pm", zone as "8W". See ShowChartInfoForQt().
  QGroupBox *pgbInfo = new QGroupBox("Transit to Natal Info");
  QFormLayout *pformInfo = new QFormLayout(pgbInfo);
  sprintf(sz, "%.3s", szMonth[FValidMon(ciTran.mon) ? ciTran.mon : 1]);
  QLineEdit *peMon = new QLineEdit(sz);
  QLineEdit *peDay = new QLineEdit(QString::number(ciTran.day));
  QLineEdit *peYea = new QLineEdit(QString::number(ciTran.yea));
  QLineEdit *peTim = new QLineEdit(SzTim(ciTran.tim));
  QLineEdit *peDst = new QLineEdit(ciTran.dst == 0.0 ? "No" :
    (ciTran.dst == 1.0 ? "Yes" :
    (ciTran.dst == dstAuto ? "Autodetect" : SzZone(ciTran.dst))));
  sprintf(sz, "%s", SzZone(ciTran.zon));
  QLineEdit *peZon = new QLineEdit(sz[0] == '+' ? &sz[1] : sz);
  pformInfo->addRow("Month:", peMon);
  pformInfo->addRow("Day:", peDay);
  pformInfo->addRow("Year:", peYea);
  pformInfo->addRow("Time:", peTim);
  pformInfo->addRow("Daylight:", peDst);
  pformInfo->addRow("Zone:", peZon);
  pvLeft->addWidget(pgbInfo);
  phTop->addLayout(pvLeft);

  QGroupBox *pgbCover = new QGroupBox("Times and Graph Cover");
  QVBoxLayout *pvCover = new QVBoxLayout(pgbCover);
  QButtonGroup *pgroupCover = new QButtonGroup(&dlg);
  CONST char *rgszCover[4] =
    { "Given Day", "Given Month", "Given Year", "Range of Years" };
  // Windows derives which of the four is current from two flags plus the
  // magnitude of nEphemYears, rather than storing an index.
  int n2 = us.fInDayMonth + us.fInDayYear +
    us.fInDayYear*(NAbs(us.nEphemYears) > 1);
  for (i = 0; i < 4; i++) {
    QRadioButton *prb = new QRadioButton(rgszCover[i]);
    prb->setChecked(i == n2);
    pgroupCover->addButton(prb, i);
    pvCover->addWidget(prb);
  }
  QFormLayout *pformYears = new QFormLayout();
  QLineEdit *peYears = new QLineEdit(QString::number(us.nEphemYears));
  pformYears->addRow("Years to Span:", peYears);
  pvCover->addLayout(pformYears);
  pvRight->addWidget(pgbCover);

  QGroupBox *pgbRestrict = new QGroupBox("Transit Time Restrictions");
  QVBoxLayout *pvRestrict = new QVBoxLayout(pgbRestrict);
  QCheckBox *pcbSign = new QCheckBox("Sign Changes");
  QCheckBox *pcbDir = new QCheckBox("Direction Changes");
  QCheckBox *pcbDiralt = new QCheckBox("Latitude Dir. Changes");
  QCheckBox *pcbDirlen = new QCheckBox("Distance Dir. Changes");
  QCheckBox *pcbAlt0 = new QCheckBox("Latitude Zero Crossing");
  QCheckBox *pcbDisequ = new QCheckBox("Distances Equal");
  pcbSign->setChecked(us.fIgnoreSign != 0);
  pcbDir->setChecked(us.fIgnoreDir != 0);
  pcbDiralt->setChecked(us.fIgnoreDiralt != 0);
  pcbDirlen->setChecked(us.fIgnoreDirlen != 0);
  pcbAlt0->setChecked(us.fIgnoreAlt0 != 0);
  pcbDisequ->setChecked(us.fIgnoreDisequ != 0);
  for (QCheckBox *pcb : { pcbSign, pcbDir, pcbDiralt, pcbDirlen, pcbAlt0,
    pcbDisequ })
    pvRestrict->addWidget(pcb);
  pvRight->addWidget(pgbRestrict);

  QCheckBox *pcbProgress = new QCheckBox("Progress Instead of Transit");
  QCheckBox *pcbReturn = new QCheckBox("Display Transit Returns Only");
  QCheckBox *pcbListAuto = new QCheckBox("Times Populate Chart List");
  QCheckBox *pcbGraphAll = new QCheckBox("Graphs Include All Objects");
  pcbProgress->setChecked(is.fProgress != 0);
  pcbReturn->setChecked(is.fReturn != 0);
  pcbListAuto->setChecked(us.fListAuto != 0);
  pcbGraphAll->setChecked(us.fGraphAll != 0);
  for (QCheckBox *pcb : { pcbProgress, pcbReturn, pcbListAuto, pcbGraphAll })
    pvRight->addWidget(pcb);
  QFormLayout *pformDiv = new QFormLayout();
  QLineEdit *peDiv = new QLineEdit(QString::number(us.nDivision));
  pformDiv->addRow("Searching Divisions:", peDiv);
  pvRight->addLayout(pformDiv);
  pvRight->addStretch(1);
  phTop->addLayout(pvRight);
  pouter->addLayout(phTop);

  QDialogButtonBox *pbuttons = new QDialogButtonBox(
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  QPushButton *pbNow = pbuttons->addButton("Now",
    QDialogButtonBox::ActionRole);
  pouter->addWidget(pbuttons);
  QObject::connect(pbuttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(pbuttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
  // "Now" refills the date/time fields from the current clock, exactly as
  // Windows' dbTr_tn button does -- it doesn't accept the dialog.
  QObject::connect(pbNow, &QPushButton::clicked, &dlg,
    [peMon, peDay, peYea, peTim, peDst, peZon]() {
      char szT[cchSzMax];
      int mon, day, yea;
      real tim;
      GetTimeNow(&mon, &day, &yea, &tim, ciDefa.dst, ciDefa.zon);
      sprintf(szT, "%.3s", szMonth[FValidMon(mon) ? mon : 1]);
      peMon->setText(szT);
      peDay->setText(QString::number(day));
      peYea->setText(QString::number(yea));
      peTim->setText(SzTim(tim));
      peDst->setText(ciDefa.dst == 0.0 ? "No" :
        (ciDefa.dst == 1.0 ? "Yes" :
        (ciDefa.dst == dstAuto ? "Autodetect" : SzZone(ciDefa.dst))));
      sprintf(szT, "%s", SzZone(ciDefa.zon));
      peZon->setText(szT[0] == '+' ? &szT[1] : szT);
    });

  if (dlg.exec() != QDialog::Accepted)
    return;

  int mon, day, yea;
  real tim, dst, zon;
  QByteArray ba;
  ba = peMon->text().toLocal8Bit(); mon = NParseSz(ba.constData(), pmMon);
  ba = peDay->text().toLocal8Bit(); day = NParseSz(ba.constData(), pmDay);
  ba = peYea->text().toLocal8Bit(); yea = NParseSz(ba.constData(), pmYea);
  ba = peTim->text().toLocal8Bit(); tim = RParseSz(ba.constData(), pmTim);
  ba = peDst->text().toLocal8Bit(); dst = RParseSz(ba.constData(), pmDst);
  ba = peZon->text().toLocal8Bit(); zon = RParseSz(ba.constData(), pmZon);
  int nty = peYears->text().toInt();
  int nd = peDiv->text().toInt();
  if (!FValidMon(mon) || !FValidYea(yea) || !FValidDay(day, mon, yea) ||
    !FValidTim(tim) || !FValidDst(dst) || !FValidZon(zon) ||
    !FValidDivision(nd)) {
    QMessageBox::warning(gi.qwind, szAppName,
      "One or more transit fields are invalid.");
    return;
  }
  SetCI(ciTran, mon, day, yea, tim, dst, zon, ciDefa.lon, ciDefa.lat);

  us.nEphemYears = nty;
  us.fIgnoreSign   = pcbSign->isChecked();
  us.fIgnoreDir    = pcbDir->isChecked();
  us.fIgnoreDiralt = pcbDiralt->isChecked();
  us.fIgnoreDirlen = pcbDirlen->isChecked();
  us.fIgnoreAlt0   = pcbAlt0->isChecked();
  us.fIgnoreDisequ = pcbDisequ->isChecked();
  is.fProgress = pcbProgress->isChecked();
  is.fReturn   = pcbReturn->isChecked();
  us.fListAuto = pcbListAuto->isChecked();
  us.fGraphAll = pcbGraphAll->isChecked();
  us.nDivision = nd;

  int n1sel = pgroup->checkedId();
  int n2sel = pgroupCover->checkedId();
  us.fInDayMonth = n2sel >= 1;
  us.fInDayYear = us.fInDayMonth && n2sel >= 2;
  if (n2sel == 2 && NAbs(us.nEphemYears) > 1)
    us.nEphemYears = 0;
  if (n1sel == 3 || n1sel == 6)
    us.nEphemYears = (n2sel <= 2 ? 1 : (nty <= 1 ? 5 : nty));
  else if (n1sel > 0) {
    // The non graphical transit chart types are text listings.
    us.fGraphics = fFalse;
    if (n1sel == 2)
      us.fProgress = is.fProgress;
  }

  flag fRecast = (n1sel == 2 || n1sel == 5);
  switch (n1sel) {
  case 1: SetChartModeQt(gTraTraTim); break;
  case 2: SetChartModeQt(gTraTraInf); break;
  case 3: SetChartModeQt(gTraTraGra); break;
  case 4: SetChartModeQt(gTraNatTim); break;
  case 5: SetChartModeQt(gTraNatInf); break;
  case 6: SetChartModeQt(gTraNatGra); break;
  default:
    if (n1 != 0)  // Was showing a transit chart; go back to a normal one.
      SetChartModeQt(gWheel);
  }
  if (fRecast)
    RecastAndRedrawQt();
  else
    RedrawQt();
}


// Progressions, equivalent to Windows' DlgProgress: whether to show a
// progressed chart, what kind, how fast it progresses, and the date to
// progress to (ciTran, shared with the Transits dialog).

// Preset progression rates, as offered by Windows' two dropdowns. Those
// live in wdialog.cpp, which isn't compiled into the QT build, so they're
// duplicated here.
static CONST char *rgszProgQt[4] =
  {"Primary", "Secondary", "Tertiary2", "Tertiary1"};
static CONST real rgrProgQt[4] =
  {rDayInYear * rDegMax, rDayInYear, 29.530588, 27.321661};
static CONST char *rgszProgCuspQt[2] = {"Quotidian", "Solar"};
static CONST real rgrProgCuspQt[2] = {1.0, rDayInYear};

void ShowProgressDialogQt()
{
  QDialog dlg(gi.qwind);
  dlg.setWindowTitle("Progressions");
  QVBoxLayout *pouter = new QVBoxLayout(&dlg);
  char sz[cchSzMax], szT[cchSzDef];
  int i;

  QCheckBox *pcbEnable = new QCheckBox("Do Progression");
  pcbEnable->setChecked(us.fProgress != 0);
  pouter->addWidget(pcbEnable);

  QGroupBox *pgbType = new QGroupBox("Progression Type");
  QVBoxLayout *pvType = new QVBoxLayout(pgbType);
  QButtonGroup *pgroup = new QButtonGroup(&dlg);
  CONST char *rgszType[3] = { "Secondary (Calculated Cusps)",
    "Secondary (Solar Arc Cusps)", "Solar Arc" };
  int nTypeCur = us.nProgress == ptCast ? 0 :
    (us.nProgress == ptMixed ? 1 : 2);
  for (i = 0; i < 3; i++) {
    QRadioButton *prb = new QRadioButton(rgszType[i]);
    prb->setChecked(i == nTypeCur);
    pgroup->addButton(prb, i);
    pvType->addWidget(prb);
  }
  pouter->addWidget(pgbType);

  QFormLayout *pform = new QFormLayout();
  // Editable combos, matching Windows: the presets are suggestions, and
  // an arbitrary rate can still be typed. Each entry is the raw number
  // followed by its name, which is why the parse below only reads the
  // leading number and ignores the rest.
  // Every addItem() must happen before setEditText(): adding the first
  // item to an editable combo sets its current index to 0, which
  // overwrites whatever text is in the line edit. Setting the text first
  // silently loses any value that doesn't happen to match a preset.
  QComboBox *pcbDay = new QComboBox();
  pcbDay->setEditable(true);
  FormatR(szT, us.rProgDay, -6);
  QString qsDay = szT;
  for (i = 0; i < 4; i++) {
    FormatR(szT, rgrProgQt[i], -6);
    sprintf(sz, "%s %s", szT, rgszProgQt[i]);
    pcbDay->addItem(sz);
    if (us.rProgDay == rgrProgQt[i])
      qsDay = sz;
  }
  pcbDay->setEditText(qsDay);
  QComboBox *pcbCusp = new QComboBox();
  pcbCusp->setEditable(true);
  FormatR(szT, us.rProgCusp, -6);
  QString qsCusp = szT;
  for (i = 0; i < 2; i++) {
    FormatR(szT, rgrProgCuspQt[i], -6);
    sprintf(sz, "%s %s", szT, rgszProgCuspQt[i]);
    pcbCusp->addItem(sz);
    if (us.rProgCusp == rgrProgCuspQt[i])
      qsCusp = sz;
  }
  pcbCusp->setEditText(qsCusp);
  QLineEdit *peArc = new QLineEdit(us.objProgArc >= 0 ?
    szObjName[us.objProgArc] : "None");
  pform->addRow("Degrees Per Day:", pcbDay);
  pform->addRow("Cusp Move Ratio:", pcbCusp);
  pform->addRow("Solar Arc Based on This Planet:", peArc);
  pouter->addLayout(pform);

  QCheckBox *pcbRAMC =
    new QCheckBox("Solar Arc Cusps Recalculated with New MC");
  pcbRAMC->setChecked(us.fProgRAMC != 0);
  pouter->addWidget(pcbRAMC);

  // Same human readable formatting as the chart info dialog.
  QGroupBox *pgbDate = new QGroupBox("Progress To");
  QFormLayout *pformDate = new QFormLayout(pgbDate);
  sprintf(sz, "%.3s", szMonth[FValidMon(ciTran.mon) ? ciTran.mon : 1]);
  QLineEdit *peMon = new QLineEdit(sz);
  QLineEdit *peDay = new QLineEdit(QString::number(ciTran.day));
  QLineEdit *peYea = new QLineEdit(QString::number(ciTran.yea));
  QLineEdit *peTim = new QLineEdit(SzTim(ciTran.tim));
  QLineEdit *peDst = new QLineEdit(ciTran.dst == 0.0 ? "No" :
    (ciTran.dst == 1.0 ? "Yes" :
    (ciTran.dst == dstAuto ? "Autodetect" : SzZone(ciTran.dst))));
  sprintf(sz, "%s", SzZone(ciTran.zon));
  QLineEdit *peZon = new QLineEdit(sz[0] == '+' ? &sz[1] : sz);
  pformDate->addRow("Month:", peMon);
  pformDate->addRow("Day:", peDay);
  pformDate->addRow("Year:", peYea);
  pformDate->addRow("Time:", peTim);
  pformDate->addRow("Daylight:", peDst);
  pformDate->addRow("Zone:", peZon);
  pouter->addWidget(pgbDate);

  QDialogButtonBox *pbuttons = new QDialogButtonBox(
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  QPushButton *pbNow = pbuttons->addButton("Now",
    QDialogButtonBox::ActionRole);
  pouter->addWidget(pbuttons);
  QObject::connect(pbuttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(pbuttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
  QObject::connect(pbNow, &QPushButton::clicked, &dlg,
    [peMon, peDay, peYea, peTim, peDst, peZon]() {
      char szN[cchSzMax];
      int mon, day, yea;
      real tim;
      GetTimeNow(&mon, &day, &yea, &tim, ciDefa.dst, ciDefa.zon);
      sprintf(szN, "%.3s", szMonth[FValidMon(mon) ? mon : 1]);
      peMon->setText(szN);
      peDay->setText(QString::number(day));
      peYea->setText(QString::number(yea));
      peTim->setText(SzTim(tim));
      peDst->setText(ciDefa.dst == 0.0 ? "No" :
        (ciDefa.dst == 1.0 ? "Yes" :
        (ciDefa.dst == dstAuto ? "Autodetect" : SzZone(ciDefa.dst))));
      sprintf(szN, "%s", SzZone(ciDefa.zon));
      peZon->setText(szN[0] == '+' ? &szN[1] : szN);
    });

  if (dlg.exec() != QDialog::Accepted)
    return;

  QByteArray ba;
  // A leading "X" means the value is a divisor of a year rather than a
  // rate, e.g. "X12" for a twelfth of a year per day. Windows' DlgProgress
  // accepts the same prefix here.
  ba = pcbDay->currentText().toLocal8Bit();
  int iX = ChCap(ba.constData()[0]) == 'X';
  real rd = RFromSz(ba.constData() + iX);
  if (iX != 0 && rd != 0.0)
    rd = rDayInYear / rd;
  ba = pcbCusp->currentText().toLocal8Bit();
  real rC = RFromSz(ba.constData());
  ba = peArc->text().toLocal8Bit();
  int npO = NParseSz(ba.constData(), pmObject);
  int mon, day, yea;
  real tim, dst, zon;
  ba = peMon->text().toLocal8Bit(); mon = NParseSz(ba.constData(), pmMon);
  ba = peDay->text().toLocal8Bit(); day = NParseSz(ba.constData(), pmDay);
  ba = peYea->text().toLocal8Bit(); yea = NParseSz(ba.constData(), pmYea);
  ba = peTim->text().toLocal8Bit(); tim = RParseSz(ba.constData(), pmTim);
  ba = peDst->text().toLocal8Bit(); dst = RParseSz(ba.constData(), pmDst);
  ba = peZon->text().toLocal8Bit(); zon = RParseSz(ba.constData(), pmZon);
  if (rd == 0.0 || rC == 0.0 || !FValidProgArc(npO) ||
    !FValidMon(mon) || !FValidYea(yea) || !FValidDay(day, mon, yea) ||
    !FValidTim(tim) || !FValidDst(dst) || !FValidZon(zon)) {
    QMessageBox::warning(gi.qwind, szAppName,
      "One or more progression fields are invalid.");
    return;
  }

  us.fProgress = pcbEnable->isChecked();
  us.nProgress = pgroup->checkedId() == 0 ? ptCast :
    (pgroup->checkedId() == 1 ? ptMixed : ptSolarArc);
  us.rProgDay = rd;
  us.rProgCusp = rC;
  us.objProgArc = npO;
  us.fProgRAMC = pcbRAMC->isChecked();
  SetCI(ciTran, mon, day, yea, tim, dst, zon, ciDefa.lon, ciDefa.lat);
  is.JDp = MdytszToJulian(ciTran.mon, ciTran.day, ciTran.yea, ciTran.tim,
    ciDefa.dst, ciDefa.zon);
  RecastAndRedrawQt();
}


// Chart settings, equivalent to Windows' DlgChart: a grab bag of per chart
// type display options. A curated subset of Windows' full field list --
// the astrocartography step/distance, star/Arabic part sort order, aspect
// sort order, and decan display fields are left for a later pass.

void ShowChartSettingsDialogQt()
{
  QDialog dlg(gi.qwind);
  dlg.setWindowTitle("Chart Settings");
  dlg.resize(400, 500);
  QVBoxLayout *pouter = new QVBoxLayout(&dlg);
  QScrollArea *pscroll = new QScrollArea(&dlg);
  QWidget *pinner = new QWidget();
  QVBoxLayout *pinnerlayout = new QVBoxLayout(pinner);

  QCheckBox *pcbVelocity = new QCheckBox("Show Object Velocities");
  QCheckBox *pcbWheelReverse = new QCheckBox("Reverse Wheel Direction");
  QCheckBox *pcbGridConfig = new QCheckBox("Show Grid Configurations");
  QCheckBox *pcbGridMidpoint = new QCheckBox("Show Grid Midpoints");
  QCheckBox *pcbAspSummary = new QCheckBox("Show Aspect Summary");
  QCheckBox *pcbMidSummary = new QCheckBox("Show Midpoint Summary");
  QCheckBox *pcbMidAspect = new QCheckBox("Show Midpoint Aspects");
  QCheckBox *pcbPrimeVert = new QCheckBox("Prime Vertical Horizon Chart");
  QCheckBox *pcbSectorApprox = new QCheckBox("Approximate Sectors");
  QCheckBox *pcbCalendarYear = new QCheckBox("Calendar Covers Full Year");
  QCheckBox *pcbInfluenceSign = new QCheckBox("Influence Chart By Sign");
  QCheckBox *pcbArabicFlip = new QCheckBox("Flip Arabic Parts At Night");
  pcbVelocity->setChecked(us.fVelocity != 0);
  pcbWheelReverse->setChecked(us.fWheelReverse != 0);
  pcbGridConfig->setChecked(us.fGridConfig != 0);
  pcbGridMidpoint->setChecked(us.fGridMidpoint != 0);
  pcbAspSummary->setChecked(us.fAspSummary != 0);
  pcbMidSummary->setChecked(us.fMidSummary != 0);
  pcbMidAspect->setChecked(us.fMidAspect != 0);
  pcbPrimeVert->setChecked(us.fPrimeVert != 0);
  pcbSectorApprox->setChecked(us.fSectorApprox != 0);
  pcbCalendarYear->setChecked(us.fCalendarYear != 0);
  pcbInfluenceSign->setChecked(us.fInfluenceSign != 0);
  pcbArabicFlip->setChecked(us.fArabicFlip != 0);
  for (QCheckBox *pcb : { pcbVelocity, pcbWheelReverse, pcbGridConfig,
    pcbGridMidpoint, pcbAspSummary, pcbMidSummary, pcbMidAspect,
    pcbPrimeVert, pcbSectorApprox, pcbCalendarYear, pcbInfluenceSign,
    pcbArabicFlip })
    pinnerlayout->addWidget(pcb);

  QFormLayout *pform = new QFormLayout();
  QLineEdit *peWheelRows = new QLineEdit(QString::number(us.nWheelRows));
  QLineEdit *peArabicParts = new QLineEdit(QString::number(us.nArabicParts));
  QLineEdit *peAtlasList = new QLineEdit(QString::number(us.nAtlasList));
  QLineEdit *peBioday = new QLineEdit(QString::number(us.nBioday));
  QLineEdit *peRatio = new QLineEdit(QString::number(us.rRatio));
  pform->addRow("Wheel rows per house:", peWheelRows);
  pform->addRow("Arabic parts to include:", peArabicParts);
  pform->addRow("Nearest cities row count:", peAtlasList);
  pform->addRow("Biorhythm days:", peBioday);
  pform->addRow("Chart proportion ratio:", peRatio);
  pinnerlayout->addLayout(pform);

  pscroll->setWidget(pinner);
  pscroll->setWidgetResizable(true);
  pouter->addWidget(pscroll);

  QDialogButtonBox *pbuttons =
    new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  pouter->addWidget(pbuttons);
  QObject::connect(pbuttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(pbuttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  if (dlg.exec() != QDialog::Accepted)
    return;

  int nWheelRows = peWheelRows->text().toInt();
  int nArabicParts = peArabicParts->text().toInt();
  int nAtlasList = peAtlasList->text().toInt();
  int nBioday = peBioday->text().toInt();
  if (!FValidWheel(nWheelRows) || !FValidPart(nArabicParts) ||
    nAtlasList < 0 || !FValidBioday(nBioday)) {
    QMessageBox::warning(gi.qwind, szAppName,
      "One or more chart settings fields are invalid.");
    return;
  }
  us.fVelocity = pcbVelocity->isChecked();
  us.fWheelReverse = pcbWheelReverse->isChecked();
  us.fGridConfig = pcbGridConfig->isChecked();
  us.fGridMidpoint = pcbGridMidpoint->isChecked();
  us.fAspSummary = pcbAspSummary->isChecked();
  us.fMidSummary = pcbMidSummary->isChecked();
  us.fMidAspect = pcbMidAspect->isChecked();
  us.fPrimeVert = pcbPrimeVert->isChecked();
  us.fSectorApprox = pcbSectorApprox->isChecked();
  us.fCalendarYear = pcbCalendarYear->isChecked();
  us.fInfluenceSign = pcbInfluenceSign->isChecked();
  us.fArabicFlip = pcbArabicFlip->isChecked();
  us.nWheelRows = nWheelRows;
  us.nArabicParts = nArabicParts;
  us.nAtlasList = nAtlasList;
  us.nBioday = nBioday;
  us.rRatio = peRatio->text().toDouble();
  RecastAndRedrawQt();
}


// Command line entry, equivalent to Windows' DlgCommand / X11's
// CommandLineX() (xscreen.cpp): type any switch(es) Astrolog understands
// and apply them immediately, the same parser command line invocation
// uses. Simplified from CommandLineX() by not saving/restoring
// us.fLoop/is.fMult around the call -- those matter for a line that
// itself starts a new multi chart sequence, an edge case skipped here.

void ShowCommandLineDialogQt()
{
  QDialog dlg(gi.qwind);
  dlg.setWindowTitle("Enter Command Line");
  dlg.resize(500, 100);
  QVBoxLayout *pouter = new QVBoxLayout(&dlg);
  pouter->addWidget(new QLabel(
    QString("Enter any %1 switch(es), e.g. \"-n -zw\":").arg(szAppName)));
  QLineEdit *peLine = new QLineEdit();
  pouter->addWidget(peLine);

  QDialogButtonBox *pbuttons =
    new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  pouter->addWidget(pbuttons);
  QObject::connect(pbuttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(pbuttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  if (dlg.exec() != QDialog::Accepted || peLine->text().trimmed().isEmpty())
    return;

  char szLine[cchSzMax];
  QByteArray ba = peLine->text().toLocal8Bit();
  strncpy(szLine, ba.constData(), cchSzMax-1);
  szLine[cchSzMax-1] = chNull;
  char *rgsz[MAXSWITCHES];
  int argc = NParseCommandLine(szLine, rgsz);
  ciCore = ciMain;
  if (argc <= 0 || !FProcessSwitches(argc, rgsz))
    QMessageBox::warning(gi.qwind, szAppName,
      "One or more switches were not understood.");
  ciMain = ciCore;
  InitColorsX();
  RecastAndRedrawQt();
}


// About, equivalent to Windows' DlgAbout.

void ShowAboutDialogQt()
{
  QDialog dlg(gi.qwind);
  dlg.setWindowTitle("About Astrolog");
  QVBoxLayout *playout = new QVBoxLayout(&dlg);
  playout->addWidget(new QLabel(
    QString("%1 version %2 for Linux (Qt)").arg(szAppName, szVersionCore)));
  playout->addWidget(new QLabel(QString("Released %1").arg(szDateCore)));
  QDialogButtonBox *pbuttons = new QDialogButtonBox(QDialogButtonBox::Ok);
  playout->addWidget(pbuttons);
  QObject::connect(pbuttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  dlg.exec();
}


// Aspect settings, equivalent to Windows' DlgAspect: per aspect maximum
// orb, exact angle, influence, color, and restriction, for all cAspect
// aspects.

void ShowAspectDialogQt()
{
  QDialog dlg(gi.qwind);
  dlg.setWindowTitle("Aspect Settings");
  dlg.resize(500, 500);
  QVBoxLayout *pouter = new QVBoxLayout(&dlg);
  QScrollArea *pscroll = new QScrollArea(&dlg);
  QWidget *pinner = new QWidget();
  QGridLayout *pgrid = new QGridLayout(pinner);
  QVector<QCheckBox *> rgpcbShow;
  QVector<QLineEdit *> rgpeOrb, rgpeAngle, rgpeInf, rgpeColor;
  int i;

  pgrid->addWidget(new QLabel("Aspect"), 0, 0);
  pgrid->addWidget(new QLabel("Show"), 0, 1);
  pgrid->addWidget(new QLabel("Max Orb"), 0, 2);
  pgrid->addWidget(new QLabel("Angle"), 0, 3);
  pgrid->addWidget(new QLabel("Influence"), 0, 4);
  pgrid->addWidget(new QLabel("Color"), 0, 5);
  for (i = 1; i <= cAspect; i++) {
    pgrid->addWidget(new QLabel(szAspectName[i]), i, 0);
    QCheckBox *pcb = new QCheckBox();
    pcb->setChecked(!ignorea[i]);
    pgrid->addWidget(pcb, i, 1);
    rgpcbShow.append(pcb);
    QLineEdit *peOrb = new QLineEdit(QString::number(rAspOrb[i]));
    pgrid->addWidget(peOrb, i, 2);
    rgpeOrb.append(peOrb);
    QLineEdit *peAngle = new QLineEdit(QString::number(rAspAngle[i]));
    pgrid->addWidget(peAngle, i, 3);
    rgpeAngle.append(peAngle);
    QLineEdit *peInf = new QLineEdit(QString::number(rAspInf[i]));
    pgrid->addWidget(peInf, i, 4);
    rgpeInf.append(peInf);
    QLineEdit *peColor = new QLineEdit(SzColor(kAspA[i]));
    pgrid->addWidget(peColor, i, 5);
    rgpeColor.append(peColor);
  }
  pscroll->setWidget(pinner);
  pscroll->setWidgetResizable(true);
  pouter->addWidget(pscroll);

  QDialogButtonBox *pbuttons =
    new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  pouter->addWidget(pbuttons);
  QObject::connect(pbuttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(pbuttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  if (dlg.exec() != QDialog::Accepted)
    return;

  for (i = 1; i <= cAspect; i++) {
    ignorea[i] = !rgpcbShow[i-1]->isChecked();
    rAspOrb[i] = rgpeOrb[i-1]->text().toDouble();
    rAspAngle[i] = rgpeAngle[i-1]->text().toDouble();
    rAspInf[i] = rgpeInf[i-1]->text().toDouble();
    QByteArray ba = rgpeColor[i-1]->text().toLocal8Bit();
    kAspA[i] = NParseSz(ba.constData(), pmColor);
  }
  AdjustAspectCount();
  RecastAndRedrawQt();
}


// More Object Settings, equivalent to Windows' DlgObject2: extends
// ShowObjectDialogQt() to the object range beyond the core planets (cusps
// through dwarf planets), plus one extra row applying to all fixed stars
// collectively, matching how Windows treats that whole range as a single
// "stars" row instead of listing each star.

void ShowObject2DialogQt()
{
  QDialog dlg(gi.qwind);
  dlg.setWindowTitle("More Object Settings");
  dlg.resize(500, 500);
  QVBoxLayout *pouter = new QVBoxLayout(&dlg);
  QScrollArea *pscroll = new QScrollArea(&dlg);
  QWidget *pinner = new QWidget();
  QGridLayout *pgrid = new QGridLayout(pinner);
  QVector<int> rgi;
  QVector<QLineEdit *> rgpeOrb, rgpeAdd, rgpeInf, rgpeColor;
  int i0, i, row = 1;

  pgrid->addWidget(new QLabel("Object"), 0, 0);
  pgrid->addWidget(new QLabel("Max Orb"), 0, 1);
  pgrid->addWidget(new QLabel("Orb Add"), 0, 2);
  pgrid->addWidget(new QLabel("Influence"), 0, 3);
  pgrid->addWidget(new QLabel("Color"), 0, 4);
  for (i0 = oAsc; i0 <= dwarfHi+1; i0++) {
    i = (i0 <= dwarfHi ? i0 : starLo);
    rgi.append(i);
    pgrid->addWidget(new QLabel(i0 <= dwarfHi ? szObjName[i] : "Stars"),
      row, 0);
    QLineEdit *peOrb = new QLineEdit(QString::number(rObjOrb[i]));
    pgrid->addWidget(peOrb, row, 1);
    rgpeOrb.append(peOrb);
    QLineEdit *peAdd = new QLineEdit(QString::number(rObjAdd[i]));
    pgrid->addWidget(peAdd, row, 2);
    rgpeAdd.append(peAdd);
    QLineEdit *peInf = new QLineEdit(QString::number(rObjInf[i]));
    pgrid->addWidget(peInf, row, 3);
    rgpeInf.append(peInf);
    QLineEdit *peColor = new QLineEdit(SzColor(kObjU[i]));
    pgrid->addWidget(peColor, row, 4);
    rgpeColor.append(peColor);
    row++;
  }
  pscroll->setWidget(pinner);
  pscroll->setWidgetResizable(true);
  pouter->addWidget(pscroll);

  QDialogButtonBox *pbuttons =
    new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  pouter->addWidget(pbuttons);
  QObject::connect(pbuttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(pbuttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  if (dlg.exec() != QDialog::Accepted)
    return;

  for (row = 0; row < rgi.size(); row++) {
    i = rgi[row];
    rObjOrb[i] = rgpeOrb[row]->text().toDouble();
    rObjAdd[i] = rgpeAdd[row]->text().toDouble();
    rObjInf[i] = rgpeInf[row]->text().toDouble();
    QByteArray ba = rgpeColor[row]->text().toLocal8Bit();
    kObjU[i] = NParseSz(ba.constData(), pmColor);
  }
  RecastAndRedrawQt();
}


// Calculation settings, equivalent to Windows' DlgCalc: ephemeris source,
// zodiac offset, house system, central planet, harmonic/dwad chart
// factors, and a grab bag of position calculation toggles.

void ShowCalcDialogQt()
{
  QDialog dlg(gi.qwind);
  dlg.setWindowTitle("Calculation Settings");
  dlg.resize(450, 600);
  QVBoxLayout *pouter = new QVBoxLayout(&dlg);
  QScrollArea *pscroll = new QScrollArea(&dlg);
  QWidget *pinner = new QWidget();
  QVBoxLayout *pinnerlayout = new QVBoxLayout(pinner);
  int i;

  QFormLayout *pform1 = new QFormLayout();
  QComboBox *pcbEphem = new QComboBox();
  QVector<int> rgcm;
  pcbEphem->addItem(szEphem[cmSwiss]);   rgcm.append(cmSwiss);
  pcbEphem->addItem(szEphem[cmMoshier]); rgcm.append(cmMoshier);
  pcbEphem->addItem(szEphem[cmJPL]);     rgcm.append(cmJPL);
  if (!us.fNoNetwork) {
    pcbEphem->addItem(szEphem[cmJPLWeb]);
    rgcm.append(cmJPLWeb);
  }
  pcbEphem->addItem(szEphem[cmPlacalc]); rgcm.append(cmPlacalc);
  pcbEphem->addItem(szEphem[cmMatrix]);  rgcm.append(cmMatrix);
  pcbEphem->addItem(szEphem[cmNone]);    rgcm.append(cmNone);
  int cmCur = FCmSwissEph() ? cmSwiss : (FCmSwissMosh() ? cmMoshier :
    (FCmSwissJPL() ? cmJPL : (FCmPlacalc() ? cmPlacalc :
    (FCmMatrix() ? cmMatrix : (FCmJPLWeb() ? cmJPLWeb : cmNone)))));
  pcbEphem->setCurrentIndex(rgcm.indexOf(cmCur));
  pform1->addRow("Calculation Method:", pcbEphem);

  QLineEdit *peOffset = new QLineEdit(QString::number(us.rZodiacOffset));
  pform1->addRow("Zodiac Offset / Ayanamsa:", peOffset);

  QComboBox *pcbSystem = new QComboBox();
  for (i = 0; i < cSystem; i++)
    pcbSystem->addItem(szSystem[i]);
  pcbSystem->setCurrentIndex(us.nHouseSystem);
  pform1->addRow("House System:", pcbSystem);

  QLineEdit *peCenter = new QLineEdit(szObjName[us.objCenter]);
  pform1->addRow("Central Planet:", peCenter);

  QLineEdit *peHarmonic = new QLineEdit(QString::number(us.rHarmonic));
  pform1->addRow("Harmonic Chart Factor:", peHarmonic);

  QLineEdit *peDwad = new QLineEdit(QString::number(us.nDwad));
  pform1->addRow("Dwad Nesting Level:", peDwad);
  pinnerlayout->addLayout(pform1);

  QCheckBox *pcbBary = new QCheckBox(
    "Compute Solar System Barycenter Instead of Sun");
  QCheckBox *pcbTrueNode = new QCheckBox(
    "Compute True Instead of Mean Nodes and Lilith");
  QCheckBox *pcbHouseAngle = new QCheckBox(
    "Cusp Objects Are House Positions Instead of Angles");
  QCheckBox *pcbRefract = new QCheckBox(
    "Local Horizon Positions Apply Atmospheric Refraction");
  QCheckBox *pcbSidereal2 = new QCheckBox(
    "Sidereal Zodiac in Invariable Plane of Solar System");
  QCheckBox *pcbNoNutation = new QCheckBox("Tropical Zodiac No Nutation");
  QCheckBox *pcbEquator2 = new QCheckBox("Equatorial Latitudes");
  QCheckBox *pcbEquator = new QCheckBox("Equatorial Longitudes");
  QCheckBox *pcbTruePos = new QCheckBox("True Space Positions");
  QCheckBox *pcbTopoPos = new QCheckBox("Topocentric Positions");
  QCheckBox *pcbAspect3D = new QCheckBox("3D Aspects");
  QCheckBox *pcbAspectLat = new QCheckBox("3D Orbs");
  QCheckBox *pcbHouse3D = new QCheckBox("3D Houses");
  QCheckBox *pcbSolarWhole = new QCheckBox("Use Start of Planet's Sign");
  pcbBary->setChecked(us.fBarycenter != 0);
  pcbTrueNode->setChecked(us.fTrueNode != 0);
  pcbHouseAngle->setChecked(us.fHouseAngle != 0);
  pcbRefract->setChecked(us.fRefract != 0);
  pcbSidereal2->setChecked(us.fSidereal2 != 0);
  pcbNoNutation->setChecked(us.fNoNutation != 0);
  pcbEquator2->setChecked(us.fEquator2 != 0);
  pcbEquator->setChecked(us.fEquator != 0);
  pcbTruePos->setChecked(us.fTruePos != 0);
  pcbTopoPos->setChecked(us.fTopoPos != 0);
  pcbAspect3D->setChecked(us.fAspect3D != 0);
  pcbAspectLat->setChecked(us.fAspectLat != 0);
  pcbHouse3D->setChecked(us.fHouse3D != 0);
  pcbSolarWhole->setChecked(us.fSolarWhole != 0);
  for (QCheckBox *pcb : { pcbBary, pcbTrueNode, pcbHouseAngle, pcbRefract,
    pcbSidereal2, pcbNoNutation, pcbEquator2, pcbEquator, pcbTruePos,
    pcbTopoPos, pcbAspect3D, pcbAspectLat, pcbHouse3D, pcbSolarWhole })
    pinnerlayout->addWidget(pcb);

  QGroupBox *pgroupBox3D = new QGroupBox("3D House Projection");
  QVBoxLayout *pgrouplayout3D = new QVBoxLayout(pgroupBox3D);
  QButtonGroup *pgroup3D = new QButtonGroup(&dlg);
  CONST char *rgsz3D[3] =
    { "Prime Vertical", "Local Horizon", "Celestial Equator" };
  for (i = 0; i < 3; i++) {
    QRadioButton *prb = new QRadioButton(rgsz3D[i]);
    prb->setChecked(i == us.nHouse3D - 1);
    pgroup3D->addButton(prb, i);
    pgrouplayout3D->addWidget(prb);
  }
  pinnerlayout->addWidget(pgroupBox3D);

  QGroupBox *pgroupBoxAsc = new QGroupBox("Object on Angle");
  QVBoxLayout *pgrouplayoutAsc = new QVBoxLayout(pgroupBoxAsc);
  QButtonGroup *pgroupAsc = new QButtonGroup(&dlg);
  CONST char *rgszAsc[3] =
    { "None", "Object on Ascendant", "Object on Midheaven" };
  int nAscCur = us.objOnAsc == 0 ? 0 : (us.objOnAsc > 0 ? 1 : 2);
  for (i = 0; i < 3; i++) {
    QRadioButton *prb = new QRadioButton(rgszAsc[i]);
    prb->setChecked(i == nAscCur);
    pgroupAsc->addButton(prb, i);
    pgrouplayoutAsc->addWidget(prb);
  }
  QLineEdit *peOnAsc = new QLineEdit(
    szObjName[us.objOnAsc == 0 ? oSun : NAbs(us.objOnAsc)-1]);
  QFormLayout *pformAsc = new QFormLayout();
  pformAsc->addRow("Object:", peOnAsc);
  pgrouplayoutAsc->addLayout(pformAsc);
  pinnerlayout->addWidget(pgroupBoxAsc);

  pscroll->setWidget(pinner);
  pscroll->setWidgetResizable(true);
  pouter->addWidget(pscroll);

  QDialogButtonBox *pbuttons =
    new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  pouter->addWidget(pbuttons);
  QObject::connect(pbuttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(pbuttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  if (dlg.exec() != QDialog::Accepted)
    return;

  real rOffset = peOffset->text().toDouble();
  int nSystem = pcbSystem->currentIndex();
  QByteArray baCenter = peCenter->text().toLocal8Bit();
  int nCenter = NParseSz(baCenter.constData(), pmObject);
  real rHarmonic = peHarmonic->text().toDouble();
  int nDwad = peDwad->text().toInt();
  QByteArray baOnAsc = peOnAsc->text().toLocal8Bit();
  int nOnAsc = NParseSz(baOnAsc.constData(), pmObject);
  if (!FValidOffset(rOffset) || !FValidSystem(nSystem) ||
    !FValidCenter(nCenter) || !FValidHarmonic(rHarmonic) ||
    !FValidDwad(nDwad) || !FItem(nOnAsc)) {
    QMessageBox::warning(gi.qwind, szAppName,
      "One or more calculation settings fields are invalid.");
    return;
  }

  int cmSel = rgcm[pcbEphem->currentIndex()];
  us.fEphemFiles = us.fPlacalcPla = us.fMatrixPla = fFalse;
  us.nSwissEph = 0;
  switch (cmSel) {
  case cmSwiss:   us.fEphemFiles = fTrue; us.nSwissEph = 0; break;
  case cmMoshier: us.fEphemFiles = fTrue; us.nSwissEph = 1; break;
  case cmJPL:     us.fEphemFiles = fTrue; us.nSwissEph = 2; break;
  case cmJPLWeb:  us.fEphemFiles = fTrue; us.nSwissEph = 3; break;
  case cmPlacalc: us.fEphemFiles = us.fPlacalcPla = fTrue; break;
  case cmMatrix:  us.fMatrixPla = fTrue; break;
  default: break;  // cmNone
  }
  us.rZodiacOffset = rOffset;
  us.nHouseSystem = nSystem;
  SetCentric(nCenter);
  SyncHelioMenuQt();
  us.rHarmonic = rHarmonic;
  us.nDwad = nDwad;
  us.fBarycenter = pcbBary->isChecked();
  us.fTrueNode = pcbTrueNode->isChecked();
  us.fHouseAngle = pcbHouseAngle->isChecked();
  us.fRefract = pcbRefract->isChecked();
  us.fSidereal2 = pcbSidereal2->isChecked();
  us.fNoNutation = pcbNoNutation->isChecked();
  us.fAspect3D = pcbAspect3D->isChecked();
  us.fAspectLat = pcbAspectLat->isChecked();
  int nAscSel = pgroupAsc->checkedId();
  us.objOnAsc = nAscSel == 0 ? 0 : (nAscSel == 1 ? nOnAsc+1 : -nOnAsc-1);
  us.fSolarWhole = pcbSolarWhole->isChecked();
  us.fEquator2 = pcbEquator2->isChecked();
  us.fEquator = pcbEquator->isChecked();
  us.fTruePos = pcbTruePos->isChecked();
  us.fTopoPos = pcbTopoPos->isChecked();
  us.fHouse3D = pcbHouse3D->isChecked();
  us.nHouse3D = pgroup3D->checkedId() + 1;
  RecastAndRedrawQt();
}


// Display settings, equivalent to Windows' DlgDisplay: date/time/number
// formatting, aspect count and requirements, eclipse display, and the
// angle/rulership restriction checkbox grids.

void ShowDisplayDialogQt()
{
  QDialog dlg(gi.qwind);
  dlg.setWindowTitle("Display Settings");
  dlg.resize(450, 650);
  QVBoxLayout *pouter = new QVBoxLayout(&dlg);
  QScrollArea *pscroll = new QScrollArea(&dlg);
  QWidget *pinner = new QWidget();
  QVBoxLayout *pinnerlayout = new QVBoxLayout(pinner);
  int i;

  QCheckBox *pcbEuroDate = new QCheckBox(
    "Format Dates as D-M-Y Instead of M/D/Y");
  QCheckBox *pcbEuroTime = new QCheckBox(
    "Format Times as 24 Hour Instead of am/pm");
  QCheckBox *pcbOffsetOnly = new QCheckBox(
    "Display Daylight and Time Zone as Single Offset");
  QCheckBox *pcbEuroDist = new QCheckBox(
    "Display Lengths in Metric Instead of Imperial Units");
  QCheckBox *pcbRound = new QCheckBox(
    "Round Positions to Nearest Unit Instead of Crop");
  QCheckBox *pcbSeconds = new QCheckBox("Display Positions to Nearest Second");
  QCheckBox *pcbSecond1K = new QCheckBox(
    "Display Nearest Second to 1/1000th of a Second");
  QCheckBox *pcbSecondHide = new QCheckBox(
    "Don't Display Seconds If They're Exactly :00");
  pcbEuroDate->setChecked(us.fEuroDate != 0);
  pcbEuroTime->setChecked(us.fEuroTime != 0);
  pcbOffsetOnly->setChecked(us.fOffsetOnly != 0);
  pcbEuroDist->setChecked(us.fEuroDist != 0);
  pcbRound->setChecked(us.fRound != 0);
  pcbSeconds->setChecked(us.fSeconds != 0);
  pcbSecond1K->setChecked(us.fSecond1K != 0);
  pcbSecondHide->setChecked(us.fSecondHide != 0);
  for (QCheckBox *pcb : { pcbEuroDate, pcbEuroTime, pcbOffsetOnly,
    pcbEuroDist, pcbRound, pcbSeconds, pcbSecond1K, pcbSecondHide })
    pinnerlayout->addWidget(pcb);

  QFormLayout *pform1 = new QFormLayout();
  QLineEdit *peAsp = new QLineEdit(QString::number(us.nAsp));
  pform1->addRow("Number of Aspects to Include:", peAsp);
  pinnerlayout->addLayout(pform1);

  QCheckBox *pcbSmartCusp = new QCheckBox(
    "Ignore Insignificant House Cusp Aspects");
  pcbSmartCusp->setChecked(us.fSmartCusp != 0);
  pinnerlayout->addWidget(pcbSmartCusp);

  QFormLayout *pform2 = new QFormLayout();
  QLineEdit *peReqObj = new QLineEdit(
    us.objRequire >= 0 ? szObjName[us.objRequire] : "None");
  pform2->addRow("Required Object for Aspects:", peReqObj);
  pinnerlayout->addLayout(pform2);

  QCheckBox *pcbParallel2 = new QCheckBox(
    "Parallel Aspects Based on Ecliptic Not Equator");
  QCheckBox *pcbDistance = new QCheckBox(
    "Aspects Measure Along Distance Axis");
  pcbParallel2->setChecked(us.fParallel2 != 0);
  pcbDistance->setChecked(us.fDistance != 0);
  pinnerlayout->addWidget(pcbParallel2);
  pinnerlayout->addWidget(pcbDistance);

  QFormLayout *pform3 = new QFormLayout();
  QLineEdit *peScreenWidth = new QLineEdit(QString::number(us.nScreenWidth));
  pform3->addRow("Text Columns:", peScreenWidth);
  pinnerlayout->addLayout(pform3);

  QCheckBox *pcbClip80 = new QCheckBox(
    "Clip Text Charts at Rightmost (e.g. 80th) Column");
  QCheckBox *pcbSabian = new QCheckBox("Interpretations Show Sabian Symbols");
  pcbClip80->setChecked(us.fClip80 != 0);
  pcbSabian->setChecked(us.fSabian != 0);
  pinnerlayout->addWidget(pcbClip80);
  pinnerlayout->addWidget(pcbSabian);

  QFormLayout *pform4 = new QFormLayout();
  QLineEdit *peStation = new QLineEdit(QString::number(us.rStation));
  pform4->addRow("Stationary If Less Than This Velocity:", peStation);
  pinnerlayout->addLayout(pform4);

  QCheckBox *pcbEclipse = new QCheckBox("Show Eclipse Information");
  QCheckBox *pcbEclipseAny = new QCheckBox(
    "Eclipses Only at Location (Not Anywhere in World)");
  pcbEclipse->setChecked(us.fEclipse != 0);
  pcbEclipseAny->setChecked(!us.fEclipseAny);
  pinnerlayout->addWidget(pcbEclipse);
  pinnerlayout->addWidget(pcbEclipseAny);

  QGroupBox *pgroupBoxAngle = new QGroupBox("Rising and Setting Restrictions");
  QVBoxLayout *pgrouplayoutAngle = new QVBoxLayout(pgroupBoxAngle);
  CONST char *rgszAngle[arMax] = { "Rising", "Zenith Crossing", "Setting",
    "Nadir Crossing", "Vertex", "Antivertex" };
  QVector<QCheckBox *> rgpcbAngle;
  for (i = 0; i < arMax; i++) {
    QCheckBox *pcb = new QCheckBox(rgszAngle[i]);
    pcb->setChecked(ignorez[i] != 0);
    pgrouplayoutAngle->addWidget(pcb);
    rgpcbAngle.append(pcb);
  }
  pinnerlayout->addWidget(pgroupBoxAngle);

  QGroupBox *pgroupBoxDegForm = new QGroupBox("Display Format");
  QVBoxLayout *pgrouplayoutDegForm = new QVBoxLayout(pgroupBoxDegForm);
  QButtonGroup *pgroupDegForm = new QButtonGroup(&dlg);
  CONST char *rgszDegForm[4] =
    { "Zodiac Position", "Hours & Minutes", "360 Degrees", "27 Nakshatras" };
  for (i = 0; i < 4; i++) {
    QRadioButton *prb = new QRadioButton(rgszDegForm[i]);
    prb->setChecked(i == us.nDegForm);
    pgroupDegForm->addButton(prb, i);
    pgrouplayoutDegForm->addWidget(prb);
  }
  pinnerlayout->addWidget(pgroupBoxDegForm);

  QGroupBox *pgroupBoxCharset = new QGroupBox("Character Encoding");
  QVBoxLayout *pgrouplayoutCharset = new QVBoxLayout(pgroupBoxCharset);
  QButtonGroup *pgroupCharset = new QButtonGroup(&dlg);
  CONST char *rgszCharset[4] =
    { "Default", "IBM / DOS", "Latin-1", "UTF8 Unicode" };
  for (i = 0; i < 4; i++) {
    QRadioButton *prb = new QRadioButton(rgszCharset[i]);
    prb->setChecked(i == us.nCharset);
    pgroupCharset->addButton(prb, i);
    pgrouplayoutCharset->addWidget(prb);
  }
  pinnerlayout->addWidget(pgroupBoxCharset);

  QGroupBox *pgroupBoxRuler = new QGroupBox("Rulership Restrictions");
  QVBoxLayout *pgrouplayoutRuler = new QVBoxLayout(pgroupBoxRuler);
  CONST char *rgszRuler[rrMax] =
    { "Standard", "Esoteric", "Hierarchical", "Exaltations", "Ray Rulerships" };
  QVector<QCheckBox *> rgpcbRuler;
  for (i = 0; i < rrMax; i++) {
    QCheckBox *pcb = new QCheckBox(rgszRuler[i]);
    pcb->setChecked(ignore7[i] != 0);
    pgrouplayoutRuler->addWidget(pcb);
    rgpcbRuler.append(pcb);
  }
  pinnerlayout->addWidget(pgroupBoxRuler);

  QGroupBox *pgroupBoxAspOrb = new QGroupBox("Aspect Orb Type");
  QVBoxLayout *pgrouplayoutAspOrb = new QVBoxLayout(pgroupBoxAspOrb);
  QButtonGroup *pgroupAspOrb = new QButtonGroup(&dlg);
  CONST char *rgszAspOrb[3] =
    { "Positive/Negative", "Applying/Separating", "Waxing/Waning" };
  for (i = 0; i < 3; i++) {
    QRadioButton *prb = new QRadioButton(rgszAspOrb[i]);
    prb->setChecked(i == us.nAppSep);
    pgroupAspOrb->addButton(prb, i);
    pgrouplayoutAspOrb->addWidget(prb);
  }
  pinnerlayout->addWidget(pgroupBoxAspOrb);

  pscroll->setWidget(pinner);
  pscroll->setWidgetResizable(true);
  pouter->addWidget(pscroll);

  QDialogButtonBox *pbuttons =
    new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  pouter->addWidget(pbuttons);
  QObject::connect(pbuttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(pbuttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  if (dlg.exec() != QDialog::Accepted)
    return;

  QByteArray ba;
  ba = peAsp->text().toLocal8Bit();
  int na = NParseSz(ba.constData(), pmAspect);
  ba = peReqObj->text().toLocal8Bit();
  int nro = NParseSz(ba.constData(), pmObject);
  int ni = peScreenWidth->text().toInt();
  real ryw = peStation->text().toDouble();
  if (!FValidAspect(na) || !(FItem(nro) || nro == -1) ||
    !FValidScreen(ni) || ryw < 0.0) {
    QMessageBox::warning(gi.qwind, szAppName,
      "One or more display settings fields are invalid.");
    return;
  }

  us.fEuroDate = pcbEuroDate->isChecked();
  us.fEuroTime = pcbEuroTime->isChecked();
  us.fOffsetOnly = pcbOffsetOnly->isChecked();
  us.fEuroDist = pcbEuroDist->isChecked();
  us.fRound = pcbRound->isChecked();
  us.fSeconds = pcbSeconds->isChecked();
  us.fSecond1K = pcbSecond1K->isChecked();
  us.fSecondHide = pcbSecondHide->isChecked();
  int naOld = us.nAsp;
  us.nAsp = na;
  for (i = naOld + 1; i <= na; i++)
    ignorea[i] = fFalse;
  for (i = na + 1; i <= cAspect; i++)
    ignorea[i] = fTrue;
  us.fSmartCusp = pcbSmartCusp->isChecked();
  us.objRequire = nro;
  us.fParallel2 = pcbParallel2->isChecked();
  us.fDistance = pcbDistance->isChecked();
  us.nScreenWidth = ni;
  us.fClip80 = pcbClip80->isChecked();
  us.fSabian = pcbSabian->isChecked();
  us.rStation = ryw;
  us.fEclipse = pcbEclipse->isChecked();
  us.fEclipseAny = !pcbEclipseAny->isChecked();
  for (i = 0; i < arMax; i++)
    ignorez[i] = rgpcbAngle[i]->isChecked();
  us.nDegForm = pgroupDegForm->checkedId();
  us.nCharset = pgroupCharset->checkedId();
  for (i = 0; i < rrMax; i++)
    ignore7[i] = rgpcbRuler[i]->isChecked();
  if (!ignore7[rrRay])
    EnsureRay();
  us.nAppSep = pgroupAspOrb->checkedId();
  RecastAndRedrawQt();
}


// Moon restrictions, equivalent to Windows' DlgMoons: which individual
// moons and center-of-body points are shown, using the same shared
// checkbox-list dialog the main Restrictions menu items use. Simplified
// from Windows' version by not including its per-planet "toggle this
// group of moons" quick buttons -- the checkbox list alone covers the
// same ground, just one click at a time instead of one per planet.

void ShowMoonRestrictDialogQt()
{
  ShowRestrictRangeDialogQt("Moon Restrictions", moonsLo, cobHi, ignore);
}


// Moon object settings, equivalent to Windows' DlgObjectM: per moon/COB
// object orb/color settings (the same grid shape as
// ShowObject2DialogQt()), plus 3 moon chart display toggles.

void ShowMoonObjectDialogQt()
{
  QDialog dlg(gi.qwind);
  dlg.setWindowTitle("Moon Object Settings");
  dlg.resize(500, 500);
  QVBoxLayout *pouter = new QVBoxLayout(&dlg);
  QScrollArea *pscroll = new QScrollArea(&dlg);
  QWidget *pinner = new QWidget();
  QVBoxLayout *pinnerlayout = new QVBoxLayout(pinner);
  QGridLayout *pgrid = new QGridLayout();
  QVector<QLineEdit *> rgpeOrb, rgpeAdd, rgpeInf, rgpeColor;
  int i, row = 1;

  pgrid->addWidget(new QLabel("Object"), 0, 0);
  pgrid->addWidget(new QLabel("Max Orb"), 0, 1);
  pgrid->addWidget(new QLabel("Orb Add"), 0, 2);
  pgrid->addWidget(new QLabel("Influence"), 0, 3);
  pgrid->addWidget(new QLabel("Color"), 0, 4);
  for (i = moonsLo; i <= cobHi; i++) {
    pgrid->addWidget(new QLabel(szObjName[i]), row, 0);
    QLineEdit *peOrb = new QLineEdit(QString::number(rObjOrb[i]));
    pgrid->addWidget(peOrb, row, 1);
    rgpeOrb.append(peOrb);
    QLineEdit *peAdd = new QLineEdit(QString::number(rObjAdd[i]));
    pgrid->addWidget(peAdd, row, 2);
    rgpeAdd.append(peAdd);
    QLineEdit *peInf = new QLineEdit(QString::number(rObjInf[i]));
    pgrid->addWidget(peInf, row, 3);
    rgpeInf.append(peInf);
    QLineEdit *peColor = new QLineEdit(SzColor(kObjU[i]));
    pgrid->addWidget(peColor, row, 4);
    rgpeColor.append(peColor);
    row++;
  }
  pinnerlayout->addLayout(pgrid);

  QCheckBox *pcbMoonMove = new QCheckBox(
    "Make Moons Orbit Current Central Object");
  QCheckBox *pcbMoonChartSep = new QCheckBox(
    "True Planetcentric Positions in Moons Charts");
  QCheckBox *pcbMoonWheel = new QCheckBox(
    "Wheel Charts Show Moons Orbiting Planet");
  pcbMoonMove->setChecked(us.fMoonMove != 0);
  pcbMoonChartSep->setChecked(us.fMoonChartSep != 0);
  pcbMoonWheel->setChecked(gs.fMoonWheel != 0);
  pinnerlayout->addWidget(pcbMoonMove);
  pinnerlayout->addWidget(pcbMoonChartSep);
  pinnerlayout->addWidget(pcbMoonWheel);

  pscroll->setWidget(pinner);
  pscroll->setWidgetResizable(true);
  pouter->addWidget(pscroll);

  QDialogButtonBox *pbuttons =
    new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  pouter->addWidget(pbuttons);
  QObject::connect(pbuttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(pbuttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  if (dlg.exec() != QDialog::Accepted)
    return;

  for (i = moonsLo; i <= cobHi; i++) {
    row = i - moonsLo;
    rObjOrb[i] = rgpeOrb[row]->text().toDouble();
    rObjAdd[i] = rgpeAdd[row]->text().toDouble();
    rObjInf[i] = rgpeInf[row]->text().toDouble();
    QByteArray ba = rgpeColor[row]->text().toLocal8Bit();
    kObjU[i] = NParseSz(ba.constData(), pmColor);
  }
  us.fMoonMove = pcbMoonMove->isChecked();
  us.fMoonChartSep = pcbMoonChartSep->isChecked();
  gs.fMoonWheel = pcbMoonWheel->isChecked();
  RecastAndRedrawQt();
}


// Object customization, equivalent to Windows' DlgCustom: redefine each of
// the "custom" (Uranian/dwarf) objects to instead track a hypothetical
// point, a JPL/JPL Horizons body, a moon, an existing object's midpoint, or
// an Arabic part, using the same compact definition-string syntax Windows
// uses (e.g. "h120" for hypothetical Vulcan, "2 n" for the Moon's north
// node point). Simplified from Windows' version by leaving out its
// "Lookup Names" button, a convenience that re-resolves already-valid
// definitions into their canonical display name -- typing the display name
// directly works fine without it.

void ShowCustomDialogQt()
{
  QDialog dlg(gi.qwind);
  dlg.setWindowTitle("Object Customization");
  dlg.resize(500, 500);
  QVBoxLayout *pouter = new QVBoxLayout(&dlg);
  QScrollArea *pscroll = new QScrollArea(&dlg);
  QWidget *pinner = new QWidget();
  QGridLayout *pgrid = new QGridLayout(pinner);
  QVector<QLineEdit *> rgpeName, rgpeDef;
  int i, j, k, l, pnt, flg;
  char sz[cchSzMax], *pch, *pchEnd;

  pgrid->addWidget(new QLabel("Display Name"), 0, 0);
  pgrid->addWidget(new QLabel("Definition"), 0, 1);
  for (i = custLo; i <= custHi; i++) {
    j = i - custLo;
    QLineEdit *peName = new QLineEdit(szObjDisp[i]);
    pgrid->addWidget(peName, j+1, 0);
    rgpeName.append(peName);

    k = rgTypSwiss[j];
    l = rgObjSwiss[j];
    if (k != 2)
      sprintf(sz, "%s%d", k <= 0 ? "h" :
        (k == 1 ? "" : (k == 3 ? "m" : (k == 4 ? "j" : "A"))), l);
    else
      sprintf(sz, l < cobLo ? "%.3s" : "%.4s", szObjName[l]);
    for (pch = sz; *pch; pch++)
      ;
    pnt = rgPntSwiss[j];
    flg = rgFlgSwiss[j];
    if (pnt > 0 || flg > 0)
      *pch++ = ' ';
    if (pnt > 0)
      *pch++ = (pnt == 1 ? 'n' : (pnt == 2 ? 's' : (pnt == 3 ? 'p' : 'a')));
    if (flg &  1) *pch++ = 'H';
    if (flg &  2) *pch++ = 'S';
    if (flg &  4) *pch++ = 'B';
    if (flg &  8) *pch++ = 'N';
    if (flg & 16) *pch++ = 'T';
    if (flg & 32) *pch++ = 'V';
    *pch = chNull;
    QLineEdit *peDef = new QLineEdit(sz);
    pgrid->addWidget(peDef, j+1, 1);
    rgpeDef.append(peDef);
  }
  pscroll->setWidget(pinner);
  pscroll->setWidgetResizable(true);
  pouter->addWidget(pscroll);

  QDialogButtonBox *pbuttons =
    new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  pouter->addWidget(pbuttons);
  QObject::connect(pbuttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(pbuttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  if (dlg.exec() != QDialog::Accepted)
    return;

  for (i = custLo; i <= custHi; i++) {
    j = i - custLo;
    QByteArray ba = rgpeDef[j]->text().toLocal8Bit();
    strncpy(sz, ba.constData(), cchSzMax-1);
    sz[cchSzMax-1] = chNull;
    for (pch = sz; *pch; pch++)
      ;
    for (pch--; pch > sz && *pch >= 'A'; pch--)
      ;
    if (pch >= sz && *pch < '0')
      *pch = chNull;
    pch = sz;
    k = (*pch == 'h' ? 0 : (*pch == 'm' ? 3 : (*pch == 'j' ? 4 :
      (*pch == 'A' ? 5 : (FNumCh(*pch) ? 1 : 2)))));
    if (k == 0 || k >= 3)
      pch++;
    l = (k == 2 ? NParseSz(pch, pmObject) : NFromSz(pch));
    if (!FValidCustom(l, k)) {
      QMessageBox::warning(gi.qwind, szAppName,
        "One or more object definitions are invalid.");
      return;
    }
  }

  for (i = custLo; i <= custHi; i++) {
    j = i - custLo;
    QByteArray ba = rgpeDef[j]->text().toLocal8Bit();
    strncpy(sz, ba.constData(), cchSzMax-1);
    sz[cchSzMax-1] = chNull;
    for (pch = sz; *pch; pch++)
      ;
    pchEnd = pch;
    for (pch--; pch > sz && *pch >= 'A'; pch--)
      ;
    if (pch >= sz && *pch < '0')
      *pch = chNull;
    pch = sz;
    k = (*pch == 'h' ? 0 : (*pch == 'm' ? 3 : (*pch == 'j' ? 4 :
      (*pch == 'A' ? 5 : (FNumCh(*pch) ? 1 : 2)))));
    if (k == 0 || k >= 3)
      pch++;
    l = (k == 2 ? NParseSz(pch, pmObject) : NFromSz(pch));
    rgTypSwiss[j] = k;
    rgObjSwiss[j] = l;
    pnt = 0; flg = 0;
    for (pch = pchEnd-1; pch > sz && *pch >= 'A'; pch--) {
      if (*pch == 'n') pnt = 1;
      if (*pch == 's') pnt = 2;
      if (*pch == 'p') pnt = 3;
      if (*pch == 'a') pnt = 4;
      if (*pch == 'H') flg |= 1;
      if (*pch == 'S') flg |= 2;
      if (*pch == 'B') flg |= 4;
      if (*pch == 'N') flg |= 8;
      if (*pch == 'T') flg |= 16;
      if (*pch == 'V') flg |= 32;
    }
    rgPntSwiss[j] = pnt;
    rgFlgSwiss[j] = flg;

    QByteArray baName = rgpeName[j]->text().toLocal8Bit();
    if (!FEqSz(baName.constData(), szObjDisp[i]))
      FCloneSzCore(baName.constData(), (char **)&szObjDisp[i],
        szObjDisp[i] == szObjName[i]);
  }
  RecastAndRedrawQt();
}


// Star customization, equivalent to Windows' DlgCustomS: rename any fixed
// star's display name, and optionally override which catalog star it
// looks up in the Swiss Ephemeris star list. Simplified the same way
// ShowCustomDialogQt() is, by leaving out the "Lookup Names" button.

void ShowCustomStarDialogQt()
{
  QDialog dlg(gi.qwind);
  dlg.setWindowTitle("Star Customization");
  dlg.resize(500, 500);
  QVBoxLayout *pouter = new QVBoxLayout(&dlg);
  QScrollArea *pscroll = new QScrollArea(&dlg);
  QWidget *pinner = new QWidget();
  QGridLayout *pgrid = new QGridLayout(pinner);
  QVector<QLineEdit *> rgpeName, rgpeDef;
  int i, k;

  pgrid->addWidget(new QLabel("Display Name"), 0, 0);
  pgrid->addWidget(new QLabel("Catalog Lookup Name"), 0, 1);
  for (i = starLo; i <= starHi; i++) {
    int row = i - starLo + 1;
    QLineEdit *peName = new QLineEdit(szObjDisp[i]);
    pgrid->addWidget(peName, row, 0);
    rgpeName.append(peName);

    k = i - starLo + 1;
    CONST char *szDef = FSzSet(szStarCustom[k]) ? szStarCustom[k] :
      (*szStarNameSwiss[k] ? szStarNameSwiss[k] : szObjName[i]);
    QLineEdit *peDef = new QLineEdit(szDef);
    pgrid->addWidget(peDef, row, 1);
    rgpeDef.append(peDef);
  }
  pscroll->setWidget(pinner);
  pscroll->setWidgetResizable(true);
  pouter->addWidget(pscroll);

  QDialogButtonBox *pbuttons =
    new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  pouter->addWidget(pbuttons);
  QObject::connect(pbuttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(pbuttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  if (dlg.exec() != QDialog::Accepted)
    return;

  for (i = starLo; i <= starHi; i++) {
    int row = i - starLo;
    k = i - starLo + 1;
    QByteArray baName = rgpeName[row]->text().toLocal8Bit();
    if (!FEqSz(baName.constData(), szObjDisp[i]))
      FCloneSzCore(baName.constData(), (char **)&szObjDisp[i],
        szObjDisp[i] == szObjName[i]);
    QByteArray baDef = rgpeDef[row]->text().toLocal8Bit();
    FCloneSz(FEqSz(baDef.constData(),
      *szStarNameSwiss[k] ? szStarNameSwiss[k] : szObjName[i]) ?
      NULL : baDef.constData(), &szStarCustom[k]);
  }
  RecastAndRedrawQt();
}

#endif // QT

/* qtdialog.cpp */
