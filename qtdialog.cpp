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
#include <QtCore/QStringList>
#include <QtCore/QEvent>
#include <QtCore/QCoreApplication>
#include <QtCore/QVector>
#include <QtCore/QMimeData>
#include <QtGui/QClipboard>

#include "astrolog.h"
#include "qtdriver.h"

#include <unistd.h>

#ifdef QT

// An editable combo showing "strCur" with "rgstr" as dropdown suggestions:
// the Qt counterpart of the SetEdit() + SetCombo() pairs Windows uses on
// most of its text fields. Order matters -- every addItem() has to precede
// setEditText(), because adding the first item resets the current index
// and would overwrite the edit text.

static QComboBox *NewComboQt(CONST QString &strCur, CONST QStringList &rgstr)
{
  QComboBox *pcb = new QComboBox();

  pcb->setEditable(true);
  pcb->addItems(rgstr);
  pcb->setEditText(strCur);
  return pcb;
}


// The suggestion lists Windows puts on the chart info fields, from
// SetEditMDYT() and SetEditSZOA() in wdialog.cpp. Kept identical to
// Windows including the year range, which upstream hardcodes.

static QStringList RgstrMonthQt()
{
  QStringList rgstr;
  int i;

  for (i = 1; i <= cSign; i++)
    rgstr.append(szMonth[i]);
  return rgstr;
}

static QStringList RgstrDayQt()
{
  QStringList rgstr;
  int i;

  for (i = 0; i <= 25; i += 5)
    rgstr.append(QString::number(Max(i, 1)));
  return rgstr;
}

static QStringList RgstrYearQt()
{
  QStringList rgstr;
  int i;

  for (i = 2020; i <= 2030; i++)
    rgstr.append(QString::number(i));
  return rgstr;
}

static QStringList RgstrTimeQt()
{
  QStringList rgstr;

  rgstr.append("Midnight");
  rgstr.append(us.fEuroTime ? "6:00" : "6:00am");
  rgstr.append("Noon");
  rgstr.append(us.fEuroTime ? "18:00" : "6:00pm");
  return rgstr;
}

// Windows' SetEditSZOA() offers only No/Yes here, and its individual
// dialogs append "Autodetect" where they want it. This port shows
// Autodetect in every chart info dialog rather than resolving it away
// (see ShowChartInfoForQt), so it belongs in every list -- a value you
// can see but not pick would be worse than the small divergence.
static QStringList RgstrDstQt()
{
  QStringList rgstr;

  rgstr.append("No");
  rgstr.append("Yes");
  rgstr.append("Autodetect");
  return rgstr;
}

// Every three letter zone abbreviation that isn't a daylight or war time
// variant, shown as "<offset> <abbreviation>" -- except LMT and LAT,
// which have no fixed offset to print.
static QStringList RgstrZoneQt()
{
  QStringList rgstr;
  char sz[cchSzDef];
  int i;

  for (i = 0; i < cZone; i++) {
    if (szZon[i][1] && szZon[i][1] != 'D' && szZon[i][1] != 'W' &&
      szZon[i][2] && szZon[i][2] != 'D') {
      if (rZon[i] != zonLMT && rZon[i] != zonLAT)
        sprintf(sz, "%s %s", SzZone(rZon[i]), szZon[i]);
      else
        sprintf(sz, "%s", SzZone(rZon[i]));
      rgstr.append(sz);
    }
  }
  return rgstr;
}

static QStringList RgstrLonQt()
{
  QStringList rgstr;

  rgstr.append("122W20");
  rgstr.append("0E00");
  return rgstr;
}

static QStringList RgstrLatQt()
{
  QStringList rgstr;

  rgstr.append("47N36");
  rgstr.append("0S00");
  return rgstr;
}


// Qt delivers wheel events to whatever widget sits under the pointer, so
// scrolling one of the tall settings dialogs silently changes any combo
// box that slides past the cursor -- Wheel Fill, Character Scale, a color
// picker. Windows' dialogs are fixed size with nothing to scroll, so the
// hazard is specific to this port. Swallow the wheel on any combo that
// isn't focused and hand it to the enclosing scroll area instead, so the
// dialog scrolls and the value stays put. Clicking a combo still focuses
// it, after which the wheel adjusts it as usual.

class NoComboWheelQt : public QObject
{
public:
  bool eventFilter(QObject *pobj, QEvent *pev) override
  {
    QWidget *pw;

    if (pev->type() != QEvent::Wheel)
      return QObject::eventFilter(pobj, pev);
    pw = qobject_cast<QWidget *>(pobj);
    if (pw == NULL || pw->hasFocus())
      return QObject::eventFilter(pobj, pev);
    for (QWidget *ppar = pw->parentWidget(); ppar != NULL;
      ppar = ppar->parentWidget()) {
      QScrollArea *psa = qobject_cast<QScrollArea *>(ppar);
      if (psa != NULL) {
        QCoreApplication::sendEvent(psa->viewport(), pev);
        break;
      }
    }
    return fTrue;
  }
};

// Apply the above to every combo in a dialog. Call just before exec().
static void PrepareDialogQt(QDialog *pdlg)
{
  static NoComboWheelQt filter;

  for (QComboBox *pcb : pdlg->findChildren<QComboBox *>()) {
    pcb->setFocusPolicy(Qt::StrongFocus);
    pcb->installEventFilter(&filter);
  }

  // Show the *start* of every field's text. Qt leaves a QLineEdit's cursor
  // at the end of text set programmatically, so a value too long for its
  // box is scrolled to show its tail: the septile's 51.428571 degrees came
  // up reading "8571", which looks like a corrupt number rather than a
  // field that needs widening. Windows' edit controls show the head.
  for (QLineEdit *pe : pdlg->findChildren<QLineEdit *>())
    pe->setCursorPosition(0);

  // Size the dialog to what it holds. Every Windows dialog is a fixed size
  // that fits its contents exactly; a Qt dialog left at whatever size it
  // was given crops its content behind a scrollbar instead. Any dialog
  // still using a scroll area is asked for the size of the widget inside
  // it, capped so a very long list can't grow taller than a screen.
  for (QScrollArea *psa : pdlg->findChildren<QScrollArea *>()) {
    QWidget *pw = psa->widget();
    if (pw == NULL)
      continue;
    QSize siz = pw->sizeHint();
    psa->setMinimumSize(QSize(Min(siz.width() + 32, 1500),
      Min(siz.height() + 8, 900)));
  }
  pdlg->adjustSize();
}


// Format a real the way Windows' SetEditR() does, so the orb and
// influence grids show the same number of decimals Windows does rather
// than QString::number()'s full precision.
static QString SzFormatRQt(real r, int n)
{
  char sz[cchSzDef];

  FormatR(sz, r, n);
  return QString(sz);
}

// The object labels Windows' dialogs actually show, taken from the CONTROL
// entries of dlgRestrict in astrolog.rc. They aren't szObjName[]: Windows
// spells out "Pallas Athena" and "Part of Fortune" where the internal name
// is the abbreviated "Pallas" and "Fortune". The "&" mnemonics are the ones
// Windows assigns, and Qt reads them the same way, so this brings the
// keyboard shortcuts across too.

static CONST char *rgszObjDlgQt[] = {
  "&Earth", "&Sun", "M&oon",
  "Mercur&y", "&Venus", "&Mars",
  "&Jupiter", "Sa&turn", "Ur&anus",
  "&Neptune", "&Pluto", "&Chiron",
  "Ceres", "Pallas Athena", "Juno",
  "Vesta", "North Node", "South Node",
  "Lilith", "Part of Fortune", "Vertex",
  "East Point", "Ascendant", "&2nd Cusp",
  "&3rd Cusp", "Nadir", "&5th Cusp",
  "&6th Cusp", "&Descendant", "&8th Cusp",
  "&9th Cusp", "Mid&heaven", "&11th Cusp",
  "12th Cusp", "Vu&lcan", "Cupido",
  "Hades", "Zeus", "Kronos",
  "Apollon", "Admetos", "Vulkanus",
  "Poseidon", "Hygiea", "Pholus",
  "Eris", "Haumea", "Makemake",
  "Gonggong", "Quaoar", "Sedna",
  "Orcus" };
#define cszObjDlgQt (int)(sizeof(rgszObjDlgQt) / sizeof(char *))

// And the moon and COB labels, from dlgMoons the same way. Windows writes
// "Jupiter COB" where the internal name is the abbreviated "JupCOB".
static CONST char *rgszMoonDlgQt[] = {
  "P&hobos",
  "&Deimos",
  "&Ganymede",
  "&Callisto",
  "&Io",
  "&Europa",
  "&Titan",
  "Rhea",
  "Iapetus",
  "Dione",
  "Tethys",
  "Enceladus",
  "Mimas",
  "Hyperion",
  "Titania",
  "Oberon",
  "Umbriel",
  "Ariel",
  "Miranda",
  "Triton",
  "Proteus",
  "Nereid",
  "Char&on",
  "Hydra",
  "Nix",
  "Kerberos",
  "Styx",
  "Jupiter COB",
  "Saturn COB",
  "Uranus COB",
  "Neptune COB",
  "Pluto COB" };
#define cszMoonDlgQt (int)(sizeof(rgszMoonDlgQt) / sizeof(char *))

// The aspect labels from dlgAspect, likewise. Windows writes out
// "Conjunction" and "Opposition" where szAspectName[] has the short
// "Conjunct" and "Opposite", and spells TriDecile with an i.
static CONST char *rgszAspDlgQt[] = {
  "&Conjunction",
  "&Opposition",
  "&Square",
  "&Trine",
  "Se&xtile",
  "&Inconjunct",
  "S&emiSextile",
  "SemiSquare",
  "SesquiQuadrate",
  "&Quintile",
  "&BiQuintile",
  "SemiQuintile",
  "Se&ptile",
  "&Novile",
  "BiNovile",
  "BiSeptile",
  "TriSeptile",
  "QuatroNovile",
  "TriDecile",
  "Undecile",
  "BiUndecile",
  "TriUndecile",
  "QuadUndecile",
  "QuinUndecile" };
#define cszAspDlgQt (int)(sizeof(rgszAspDlgQt) / sizeof(char *))

// The label for aspect "i" (1 based, as the aspect arrays are).
static QString SzAspDlgQt(int i)
{
  if (i >= 1 && i - 1 < cszAspDlgQt && rgszAspDlgQt[i-1][0] != chNull)
    return QString(rgszAspDlgQt[i-1]);
  return QString(szAspectName[i]);
}

// And the fixed star labels from dlgStar. Windows writes "Rigil
// Kentaurus", "Kaus Australis" and "Great Attractor" in full where the
// internal names are truncated to fit a text column.
static CONST char *rgszStarDlgQt[] = {
  "&Sirius",
  "Canopus",
  "Rigil &Kentaurus",
  "Arcturus",
  "&Vega",
  "Capella",
  "Rigel",
  "Procyon",
  "&Betelgeuse",
  "Achernar",
  "Agena",
  "Altair",
  "Acrux",
  "Aldebaran",
  "Antares",
  "Spica",
  "Pollux",
  "Fomalhaut",
  "Deneb",
  "Becrux",
  "Regulus",
  "Adara",
  "Castor",
  "Shaula",
  "Bellatrix",
  "Gacrux",
  "Alnath",
  "Alnilam",
  "Miaplacidus",
  "Alnair",
  "Alioth",
  "Dubhe",
  "Wezen",
  "Kaus Australis",
  "Alkaid",
  "Sargas",
  "Menkalinan",
  "Peacock",
  "Alhena",
  "Avior",
  "Murzim",
  "Alphard",
  "Polaris",
  "Algol",
  "Suhail",
  "Alcyone",
  "&Andromeda",
  "&Zeta Reticuli",
  "Galactic &Center",
  "&Great Attractor" };
#define cszStarDlgQt (int)(sizeof(rgszStarDlgQt) / sizeof(char *))

// The label for object "i" in a dialog: the Windows one where there is
// one, otherwise the internal name. The Windows strings carry "&"
// mnemonics, which is right for a checkbox -- Qt reads them the same way
// -- but a QLabel renders the "&" literally, giving "&2nd Cusp". The
// grids that label a row with static text want SzObjDlgPlainQt().
static QString SzObjDlgQt(int i)
{
  if (i >= starLo && i - starLo < cszStarDlgQt &&
    rgszStarDlgQt[i - starLo][0] != chNull)
    return QString(rgszStarDlgQt[i - starLo]);
  if (i >= moonsLo && i - moonsLo < cszMoonDlgQt &&
    rgszMoonDlgQt[i - moonsLo][0] != chNull)
    return QString(rgszMoonDlgQt[i - moonsLo]);
  if (i >= 0 && i < cszObjDlgQt && rgszObjDlgQt[i][0] != chNull)
    return QString(rgszObjDlgQt[i]);
  return QString(szObjName[i]);
}

// Same label with the mnemonic marker removed, for static text.
static QString SzObjDlgPlainQt(int i)
{
  return SzObjDlgQt(i).remove(QChar('&'));
}

// Windows lays its per-row setting grids out in several columns side by
// side, sized to fit, rather than one tall scrolling list -- see the
// repeated control X positions in each dlg* block of astrolog.rc. These
// two put a Qt QGridLayout into the same shape.
//
// PlaceRowQt() says where row "i" lands given how many rows go in a
// column and how many fields a row has. Grid row 0 is left for the column
// headers, and one grid column between blocks acts as a gutter.

static void PlaceRowQt(int i, int cRowPerCol, int cField, int *pnRow,
  int *pnCol)
{
  *pnRow = i % cRowPerCol + 1;
  *pnCol = (i / cRowPerCol) * (cField + 2);
}

// Add the header labels above every column block. "rgsz" lists the field
// headings, left to right, not counting the row's name column.
static void HeadersQt(QGridLayout *pgrid, int cCol, int cField,
  CONST char **rgsz)
{
  int iCol, iField, nBase;

  for (iCol = 0; iCol < cCol; iCol++) {
    nBase = iCol * (cField + 2);
    for (iField = 0; iField < cField; iField++)
      pgrid->addWidget(new QLabel(rgsz[iField]), 0, nBase + 1 + iField);
    if (iCol < cCol-1)
      pgrid->setColumnMinimumWidth(nBase + cField + 1, 12);
  }
}


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
// in the Win32-only WI struct.

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
  // Windows' dxFi_YXf: a master switch for the six Graphics Settings font
  // pickers. Turning it off zeroes them and remembers the set in
  // gi.nFontPrev; turning it back on restores that set.
  QCheckBox *pcbFontAll = new QCheckBox("Use Real System Fonts in Graphic Charts");
  pcbFontAll->setChecked(gs.nFontAll > 0);
  QCheckBox *pcbNoBackDraw = new QCheckBox("Don't Show Background Bitmap");
  pcbSmartSave->setChecked(us.fSmartSave != 0);
  pcbTextHTML->setChecked(us.fTextHTML != 0);
  pcbBmpPNG->setChecked(gs.chBmpMode == 'P');
  pcbPSComplete->setChecked(!gs.fPSComplete);
  pcbWriteOld->setChecked(us.fWriteOld != 0);
  pcbNoBackDraw->setChecked(!gs.fBackDraw);
  for (QCheckBox *pcb : { pcbSmartSave, pcbTextHTML, pcbBmpPNG,
    pcbPSComplete, pcbWriteOld, pcbFontAll })
    pouter->addWidget(pcb);

  QFormLayout *pformThick = new QFormLayout();
  QLineEdit *peThickAdjust = new QLineEdit(QString::number(gs.nThickAdjust));
  pformThick->addRow("Line Thickness Adjustment:", peThickAdjust);
  pouter->addLayout(pformThick);

  // Windows puts this checkbox directly above the transparency it relates
  // to, at the head of the dialog's right hand column.
  pouter->addWidget(pcbNoBackDraw);

  QFormLayout *pform = new QFormLayout();
  // Editable with 25/50/75/100 offered, as Windows' dcFi_XI1 does.
  QComboBox *pcbBackPct = new QComboBox();
  pcbBackPct->setEditable(true);
  for (i = 25; i <= 100; i += 25)
    pcbBackPct->addItem(QString::number(i));
  pcbBackPct->setEditText(SzFormatRQt(gs.rBackPct, -3));
  QLineEdit *peADB = new QLineEdit(FSzSet(us.szADB) ? us.szADB : "");
  QLineEdit *pePaperX = new QLineEdit(SzLength(gs.xInch));
  QLineEdit *pePaperY = new QLineEdit(SzLength(gs.yInch));
  pform->addRow("Background Transparency Percent:", pcbBackPct);
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

  PrepareDialogQt(&dlg);
  if (dlg.exec() != QDialog::Accepted)
    return;

  int nThickAdjust = peThickAdjust->text().toInt();
  QByteArray baBackPct = pcbBackPct->currentText().toLocal8Bit();
  real rBackPct = RFromSz(baBackPct.constData());
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
  gs.nFontAll = pcbFontAll->isChecked() * gi.nFontPrev;
  gs.nFontTxt = gs.nFontAll / 0x100000;
  gs.nFontSig = (gs.nFontAll / 0x10000) % 0x10;
  gs.nFontHou = (gs.nFontAll / 0x1000) % 0x10;
  gs.nFontObj = (gs.nFontAll / 0x100) % 0x10;
  gs.nFontAsp = (gs.nFontAll / 0x10) % 0x10;
  gs.nFontNak = gs.nFontAll % 0x10;
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
// Mirrors rgszFontDisp[] in wdialog.cpp, which is Windows-only. These are
// the names shown to the user; rgszFontName[] (xdata.cpp, portable) holds
// the family names actually looked up when drawing.
static CONST char *rgszFontDispQt[cFont] = {szAppNameCore, "Wingdings",
  "Astro", "Enigma", "Hamburg", "Astronomicon", "StarFont",
  "StarFont Serif", "Hank's Nakshatra", "Arial", "Courier New", "Consolas",
  "Lucida", "Cascadia"};

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
// Skipped, as Win32-only (it lives in the WI struct): "Don't
// Automatically Redraw Screen" (wi.fNoUpdate).

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

  // Windows offers both as editable dropdowns stepping 100..MAXSCALE by
  // 50, except character scale which only accepts whole multiples of 100
  // (FValidScale vs FValidScaleText). addItem() before setEditText().
  QFormLayout *pformScale = new QFormLayout();
  QComboBox *pcbScale = new QComboBox();
  QComboBox *pcbScaleText = new QComboBox();
  pcbScale->setEditable(true);
  pcbScaleText->setEditable(true);
  for (i = 100; i <= MAXSCALE; i += 50) {
    if (i % 100 == 0)
      pcbScale->addItem(QString::number(i));
    pcbScaleText->addItem(QString::number(i));
  }
  pcbScale->setEditText(QString::number(gs.nScale));
  pcbScaleText->setEditText(QString::number(gs.nScaleText));
  pformScale->addRow("Character Scale:", pcbScale);
  pformScale->addRow("Text Scale:", pcbScaleText);
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
  QLineEdit *peZoom = new QLineEdit(SzFormatRQt(gs.rspace, -6));
  pformMisc->addRow("Number of Cells in Graphics Aspect Grid:", peGridCell);
  pformMisc->addRow("Solar System Orbit Trail Length:", peSpace);
  pformMisc->addRow("Telescope Focuses on This Object:", peTrack);
  pformMisc->addRow("Telescope and Orbit Zoom Scale:", peZoom);
  pin->addLayout(pformMisc);

  QGroupBox *pgbMap = new QGroupBox("Map and Globe");
  QVBoxLayout *pvMap = new QVBoxLayout(pgbMap);
  QFormLayout *pformMap = new QFormLayout();
  QLineEdit *peRot = new QLineEdit(SzFormatRQt(gs.rRot, -3));
  QLineEdit *peTilt = new QLineEdit(SzFormatRQt(gs.rTilt, -3));
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

  // Windows' Fonts group: one combo per glyph category, each offering only
  // the fonts that category allows (rgszFontAllow[] in xdata.cpp -- not
  // every font has glyphs for every category).
  QGroupBox *pgbFont = new QGroupBox("Fonts");
  QFormLayout *pformFont = new QFormLayout(pgbFont);
  CONST char *rgszFontLabel[6] = { "Text:", "Signs:", "Houses:", "Objects:",
    "Aspects:", "Nakshat.:" };
  int *rgpnFont[6] = { &gs.nFontTxt, &gs.nFontSig, &gs.nFontHou,
    &gs.nFontObj, &gs.nFontAsp, &gs.nFontNak };
  QComboBox *rgpcbFont[6];
  QVector<int> rgrgiFont[6];
  for (i = 0; i < 6; i++) {
    rgpcbFont[i] = new QComboBox();
    for (int j = 0; j < cFont; j++) {
      if (rgszFontAllow[i][j] < '0')
        continue;
      rgrgiFont[i].append(j);
      rgpcbFont[i]->addItem(rgszFontDispQt[j]);
    }
    rgpcbFont[i]->setCurrentIndex(Max(rgrgiFont[i].indexOf(*rgpnFont[i]), 0));
    pformFont->addRow(rgszFontLabel[i], rgpcbFont[i]);
  }
  pin->addWidget(pgbFont);

  QGroupBox *pgbAnim = new QGroupBox("Animation");
  QVBoxLayout *pvAnim = new QVBoxLayout(pgbAnim);
  // Windows edits wi.nTimerDelay here; the Qt build keeps the equivalent
  // in qtdriver.cpp, since wi is Win32-only.
  QFormLayout *pformAnim = new QFormLayout();
  QLineEdit *peAnimDelay = new QLineEdit(QString::number(NAnimDelayQt()));
  pformAnim->addRow("Update Delay in Milliseconds:", peAnimDelay);
  pvAnim->addLayout(pformAnim);
  QCheckBox *pcbAnimMap = new QCheckBox("Animate Map Instead of Time");
  pcbAnimMap->setChecked(gs.fAnimMap != 0);
  pvAnim->addWidget(pcbAnimMap);
  pin->addWidget(pgbAnim);

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

  PrepareDialogQt(&dlg);
  if (dlg.exec() != QDialog::Accepted)
    return;

  int nWinX = peWinX->text().toInt();
  int nWinY = peWinY->text().toInt();
  int nScale = pcbScale->currentText().toInt();
  int nScaleText = pcbScaleText->currentText().toInt();
  int nGridCell = peGridCell->text().toInt();
  int cspace = peSpace->text().toInt();
  int nDecaSize = peDecaSize->text().toInt();
  int nAnimDelay = peAnimDelay->text().toInt();
  QByteArray ba;
  ba = peRot->text().toLocal8Bit();  real rRot = RFromSz(ba.constData());
  ba = peTilt->text().toLocal8Bit(); real rTilt = RFromSz(ba.constData());
  ba = peZoom->text().toLocal8Bit(); real rZoom = RFromSz(ba.constData());
  QByteArray baTrack = peTrack->text().toLocal8Bit();
  int objTrack = NParseSz(baTrack.constData(), pmObject);
  QByteArray baObjLeft = peObjLeft->text().toLocal8Bit();
  int objLeft = NParseSz(baObjLeft.constData(), pmObject);
  if (!FValidGraphX(nWinX) || !FValidGraphY(nWinY) ||
    !FValidScale(nScale) || !FValidScaleText(nScaleText) ||
    !FValidGrid(nGridCell) || !FValidDecaSize(nDecaSize) ||
    !FValidTimer(nAnimDelay) ||
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
  SetAnimDelayQt(nAnimDelay);
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
  for (i = 0; i < 6; i++)
    *rgpnFont[i] = rgrgiFont[i].value(rgpcbFont[i]->currentIndex(), 0);
  // Windows keeps the six packed into gs.nFontAll as well, which is what
  // File Settings' "Use Real System Fonts" toggles off and back on.
  gs.nFontAll = gs.nFontTxt*0x100000 + gs.nFontSig*0x10000 +
    gs.nFontHou*0x1000 + gs.nFontObj*0x100 + gs.nFontAsp*0x10 + gs.nFontNak;
  if (gs.nFontAll > 0)
    gi.nFontPrev = gs.nFontAll;

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
  QComboBox *peMon  = NewComboQt(sz, RgstrMonthQt());
  QComboBox *peDay  = NewComboQt(QString::number(pci->day), RgstrDayQt());
  QComboBox *peYea  = NewComboQt(QString::number(pci->yea), RgstrYearQt());
  QComboBox *peTim  = NewComboQt(SzTim(pci->tim), RgstrTimeQt());
  // Daylight saving has sentinel values (see astrolog.h's dstAuto) rather
  // than being a plain offset. Windows resolves dstAuto to its concrete
  // Yes/No via DstReal() before display, which silently discards the
  // user's "work it out for me" choice on the next OK -- show it as
  // "Autodetect" instead so it survives a round trip.
  QComboBox *peDst  = NewComboQt(pci->dst == 0.0 ? "No" :
    (pci->dst == 1.0 ? "Yes" :
    (pci->dst == dstAuto ? "Autodetect" : SzZone(pci->dst))), RgstrDstQt());
  sprintf(sz, "%s", SzZone(pci->zon));
  QComboBox *peZon  = NewComboQt(sz[0] == '+' ? &sz[1] : sz, RgstrZoneQt());
  // SzLocation() returns longitude and latitude in one string split at
  // is.ichLocSplit. Force plain ASCII while formatting: otherwise it uses
  // a Latin-1/IBM degree byte that isn't valid UTF-8 (see the Charts
  // dialog for where that matters), and these fields want the compact
  // "122W19" form anyway.
  int nSavChar = us.fAnsiChar; us.fAnsiChar = fFalse;
  sprintf(sz, "%s", SzLocation(pci->lon, pci->lat));
  us.fAnsiChar = nSavChar;
  sz[is.ichLocSplit] = chNull;
  QComboBox *peLon  = NewComboQt(&sz[0], RgstrLonQt());
  QComboBox *peLat  = NewComboQt(&sz[is.ichLocSplit+1], RgstrLatQt());
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

  PrepareDialogQt(&dlg);
  if (dlg.exec() != QDialog::Accepted)
    return;

  CI ci = *pci;
  QByteArray ba;
  ba = peMon->currentText().toLocal8Bit(); ci.mon = NParseSz(ba.constData(), pmMon);
  ba = peDay->currentText().toLocal8Bit(); ci.day = NParseSz(ba.constData(), pmDay);
  ba = peYea->currentText().toLocal8Bit(); ci.yea = NParseSz(ba.constData(), pmYea);
  ba = peTim->currentText().toLocal8Bit(); ci.tim = RParseSz(ba.constData(), pmTim);
  ba = peDst->currentText().toLocal8Bit(); ci.dst = RParseSz(ba.constData(), pmDst);
  ba = peZon->currentText().toLocal8Bit(); ci.zon = RParseSz(ba.constData(), pmZon);
  ba = peLon->currentText().toLocal8Bit(); ci.lon = RParseSz(ba.constData(), pmLon);
  ba = peLat->currentText().toLocal8Bit(); ci.lat = RParseSz(ba.constData(), pmLat);

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
  dlg.setWindowTitle("Charts #3 through #6");
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

  PrepareDialogQt(&dlg);
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

  PrepareDialogQt(&dlg);
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

// One color field: an editable combo listing the color names, since that
// is what Windows offers. "nExtra" follows SetEditColor()'s convention --
// 0 for plain colors only, higher values progressively admitting the
// symbolic entries past them (Element, Ray, Star, Planet, Auto), which
// only some fields accept.
static QComboBox *NewColorComboQt(KI ki, int nExtra)
{
  QComboBox *pcb = new QComboBox();
  int i, iMax = cColor2 + (nExtra > 0)*(nExtra + 1);

  pcb->setEditable(true);
  // addItem() before setEditText(): see the Progressions dialog.
  for (i = 0; i < iMax; i++)
    pcb->addItem(szColor[i]);
  pcb->setEditText(SzColor(ki));
  return pcb;
}

static QComboBox *AddColorComboQt(QFormLayout *pform, CONST char *szLabel,
  KI ki, int nExtra)
{
  QComboBox *pcb = NewColorComboQt(ki, nExtra);

  pform->addRow(QString(szLabel) + ":", pcb);
  return pcb;
}

static int NColorFromComboQt(QComboBox *pcb);

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
      j <= 0 ? kMainA[-j] : kRainbowA[j], 0);
  }
  pvLeft->addWidget(pgbPalette);

  QGroupBox *pgbExtra = new QGroupBox("Other");
  QFormLayout *pformExtra = new QFormLayout(pgbExtra);
  QComboBox *pcbPen = AddColorComboQt(pformExtra, "Scribble", gi.kiPen, 0);
  QComboBox *pcbDeca = AddColorComboQt(pformExtra, "Corners", gs.kiDeca, 4);
  pvLeft->addWidget(pgbExtra);
  pvLeft->addStretch(1);

  QGroupBox *pgbElem = new QGroupBox("Elements");
  QFormLayout *pformElem = new QFormLayout(pgbElem);
  for (i = 0; i < cElem; i++)
    rgpcbElem[i] = AddColorComboQt(pformElem, rgszElem[i], kElemA[i], 0);
  pvRight->addWidget(pgbElem);

  QGroupBox *pgbRay = new QGroupBox("Seven Rays");
  QFormLayout *pformRay = new QFormLayout(pgbRay);
  for (i = 1; i <= cRay; i++)
    rgpcbRay[i] = AddColorComboQt(pformRay, rgszRay[i-1], kRayA[i], 0);
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

  PrepareDialogQt(&dlg);
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
  // Two columns, as Windows' dlgObject has (astrolog.rc column X 5 and 190).
  CONST int cCol = 2, cField = 4;
  CONST int cRowPerCol = (oCore + 1 + cCol - 1) / cCol;
  CONST char *rgszHead[] = {"Max Orb", "Add", "Influence", "Color"};
  QDialog dlg(gi.qwind);
  dlg.setWindowTitle("Objects");
  QVBoxLayout *pouter = new QVBoxLayout(&dlg);
  QGridLayout *pgrid = new QGridLayout();
  QLineEdit *rgpeOrb[oCore+1];
  QLineEdit *rgpeAdd[oCore+1];
  QLineEdit *rgpeInf[oCore+1];
  QComboBox *rgpcbColor[oCore+1];
  int i, nRow, nCol;

  pgrid->setHorizontalSpacing(4);
  pgrid->setVerticalSpacing(2);
  HeadersQt(pgrid, cCol, cField, rgszHead);
  for (i = 0; i <= oCore; i++) {
    PlaceRowQt(i, cRowPerCol, cField, &nRow, &nCol);
    pgrid->addWidget(new QLabel(SzObjDlgPlainQt(i)), nRow, nCol);
    rgpeOrb[i] = new QLineEdit(SzFormatRQt(rObjOrb[i], -2));
    pgrid->addWidget(rgpeOrb[i], nRow, nCol+1);
    rgpeAdd[i] = new QLineEdit(SzFormatRQt(rObjAdd[i], -1));
    pgrid->addWidget(rgpeAdd[i], nRow, nCol+2);
    rgpeInf[i] = new QLineEdit(SzFormatRQt(rObjInf[i], -2));
    pgrid->addWidget(rgpeInf[i], nRow, nCol+3);
    rgpcbColor[i] = NewColorComboQt(kObjU[i], 1);
    pgrid->addWidget(rgpcbColor[i], nRow, nCol+4);
  }
  pouter->addLayout(pgrid);

  QDialogButtonBox *pbuttons =
    new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  pouter->addWidget(pbuttons);
  QObject::connect(pbuttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(pbuttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  PrepareDialogQt(&dlg);
  if (dlg.exec() != QDialog::Accepted)
    return;

  for (i = 0; i <= oCore; i++) {
    rObjOrb[i] = rgpeOrb[i]->text().toDouble();
    rObjAdd[i] = rgpeAdd[i]->text().toDouble();
    rObjInf[i] = rgpeInf[i]->text().toDouble();
    kObjU[i] = NColorFromComboQt(rgpcbColor[i]);
  }
  RecastAndRedrawQt();
}


// Object restriction (show/hide), equivalent to Windows' DlgRestrict --
// shared by the main Restrictions, Star Restrictions, and Transit
// Restrictions menu items, which differ only in object range and which
// ignore array they edit, same as Windows' one DlgRestrict does based on
// wi.wCmd.

// One button in the quick-action column Windows runs down the right side
// of each restriction dialog (dlgRestrict/dlgStar/dlgMoons in
// astrolog.rc). They come in four shapes, and "lo"/"hi" are absolute
// object indexes clamped to whatever range the dialog is showing.
#define resSet    0   // check every box in the range (restrict them)
#define resClear  1   // clear every box in the range (unrestrict them)
#define resToggle 2   // flip every box in the range
#define resCopy   3   // reload the range from another restriction array

typedef struct {
  CONST char *szLabel;
  int nAction;
  int lo, hi;
  CONST byte *rgSource;   // resCopy only
} RESBUT;

#define CButRes(rg) (int)(sizeof(rg) / sizeof(RESBUT))

// Object restriction (show/hide), equivalent to Windows' DlgRestrict,
// DlgStar, and DlgMoons -- one implementation for all of them, since they
// differ only in title, object range, which ignore array they edit, and
// which quick buttons they offer. (Windows likewise drives the first two
// with a single DlgRestrict keyed off wi.wCmd.)
//
// Checkbox polarity follows Windows: the label is the bare object name
// and a *checked* box means restricted, i.e. hidden. That reads backwards
// at first glance, but it's what a Windows user's muscle memory expects,
// and inverting it is the one difference that would silently produce the
// opposite chart.

// Windows doesn't lay these boxes out as one long list, nor as columns of
// equal length: dlgRestrict puts them in five labelled group boxes, by what
// kind of object each is (astrolog.rc GROUPBOX "Planets", "Minor Objects",
// "House Cusps", "Uranians", "Dwarfs"). Splitting the same objects into six
// even columns would put Lilith, a cusp and an asteroid in one column, so
// the grouping is what has to be matched, not the column count.

typedef struct {
  CONST char *szLabel;
  int lo, hi;
} OBJGROUPQT;

static CONST OBJGROUPQT rgGroupRestrictQt[] = {
  {"Planets",       0,       oChi-1},
  {"Minor Objects", oChi,    oCore},
  {"House Cusps",   cuspLo,  cuspHi},
  {"Uranians",      uranLo,  uranHi},
  {"Dwarfs",        dwarfLo, dwarfHi} };
#define cGroupRestrictQt \
  (int)(sizeof(rgGroupRestrictQt) / sizeof(OBJGROUPQT))

// dlgMoons groups its moons by the planet they orbit, seven boxes of it
// (astrolog.rc GROUPBOX "Mars" through "Center of Body (COB)"). The counts
// come from that dialog's checkbox blocks: Mars 2, Jupiter 4, Saturn 8,
// Uranus 5, Neptune 3, Pluto 5, then the five COB points.
static CONST OBJGROUPQT rgGroupMoonQt[] = {
  {"Mars",     moonsLo+0,  moonsLo+1},
  {"Jupiter",  moonsLo+2,  moonsLo+5},
  {"Saturn",   moonsLo+6,  moonsLo+13},
  {"Uranus",   moonsLo+14, moonsLo+18},
  {"Neptune",  moonsLo+19, moonsLo+21},
  {"Pluto",    moonsLo+22, moonsLo+26},
  {"Center of Body (COB)", cobLo, cobHi} };
#define cGroupMoonQt (int)(sizeof(rgGroupMoonQt) / sizeof(OBJGROUPQT))

static void ShowRestrictRangeDialogQt(CONST char *szTitle, int lo, int hi,
  byte *rgignore, CONST RESBUT *rgbut, int cbut, int cCol,
  CONST OBJGROUPQT *rggroup, int cgroup)
{
  // With a group table the range goes into labelled boxes; otherwise it's
  // laid out in cCol plain columns, which is what the fixed stars want
  // since dlgStar has no groups either.
  CONST int cRowPerCol = (cCol > 0 ? (hi - lo + 1 + cCol - 1) / cCol : 0);
  QDialog dlg(gi.qwind);
  dlg.setWindowTitle(szTitle);
  QVBoxLayout *pouter = new QVBoxLayout(&dlg);
  QHBoxLayout *pmiddle = new QHBoxLayout();
  QVector<QCheckBox *> rgpcb;
  int i, iGroup, jlo, jhi;

  rgpcb.resize(hi - lo + 1);
  if (cgroup > 0) {
    for (iGroup = 0; iGroup < cgroup; iGroup++) {
      CONST OBJGROUPQT *pgrp = &rggroup[iGroup];
      jlo = Max(pgrp->lo, lo); jhi = Min(pgrp->hi, hi);
      if (jlo > jhi)
        continue;
      QGroupBox *pgb = new QGroupBox(pgrp->szLabel);
      QVBoxLayout *pcol = new QVBoxLayout(pgb);
      pcol->setSpacing(1);
      for (i = jlo; i <= jhi; i++) {
        QCheckBox *pcb = new QCheckBox(SzObjDlgQt(i));
        pcb->setChecked(rgignore[i] != 0);
        pcol->addWidget(pcb);
        rgpcb[i - lo] = pcb;
      }
      pcol->addStretch(1);
      pmiddle->addWidget(pgb, 0, Qt::AlignTop);
    }
  } else {
    QGridLayout *pgrid = new QGridLayout();
    pgrid->setHorizontalSpacing(10);
    pgrid->setVerticalSpacing(1);
    for (i = lo; i <= hi; i++) {
      QCheckBox *pcb = new QCheckBox(SzObjDlgQt(i));
      pcb->setChecked(rgignore[i] != 0);
      pgrid->addWidget(pcb, (i - lo) % cRowPerCol, (i - lo) / cRowPerCol);
      rgpcb[i - lo] = pcb;
    }
    pmiddle->addLayout(pgrid);
  }

  // Windows' button placement (astrolog.rc dlgRestrict): the set/toggle
  // buttons are a column of uniform 50 unit wide buttons down the right
  // hand side, with Cancel and OK stacked at the bottom of that same
  // column. The two "copy a whole set in" buttons are not in that column
  // at all -- they sit along the bottom edge, the wide one at far left
  // (125 units) and Recall in the middle. Leaving them in the column is
  // what stretched every button to the width of the longest label.
  QVBoxLayout *pcol = new QVBoxLayout();
  QHBoxLayout *pbottom = new QHBoxLayout();
  QVector<QPushButton *> rgpbCol;

  for (i = 0; i < cbut; i++) {
    CONST RESBUT *pbut = &rgbut[i];
    QPushButton *ppb = new QPushButton(pbut->szLabel);
    // rgpcb outlives every click: the lambda only runs inside exec().
    QObject::connect(ppb, &QPushButton::clicked, &dlg,
      [&rgpcb, pbut, lo, hi]() {
        int j, jlo = Max(pbut->lo, lo), jhi = Min(pbut->hi, hi);
        for (j = jlo; j <= jhi; j++) {
          QCheckBox *pcb = rgpcb[j - lo];
          switch (pbut->nAction) {
          case resSet:    pcb->setChecked(fTrue);              break;
          case resClear:  pcb->setChecked(fFalse);             break;
          case resToggle: pcb->setChecked(!pcb->isChecked());  break;
          case resCopy:   pcb->setChecked(pbut->rgSource[j] != 0); break;
          }
        }
      });
    if (pbut->nAction == resCopy) {
      // Along the bottom, in table order: the wide one then Recall.
      if (pbottom->count() > 0)
        pbottom->addSpacing(20);
      pbottom->addWidget(ppb);
    } else {
      pcol->addWidget(ppb);
      rgpbCol.append(ppb);
    }
  }

  QPushButton *pbCancel = new QPushButton("Cancel");
  QPushButton *pbOk = new QPushButton("OK");
  pbOk->setDefault(fTrue);
  rgpbCol.append(pbCancel);
  rgpbCol.append(pbOk);
  QObject::connect(pbOk, &QPushButton::clicked, &dlg, &QDialog::accept);
  QObject::connect(pbCancel, &QPushButton::clicked, &dlg, &QDialog::reject);
  pcol->addStretch(1);
  pcol->addWidget(pbCancel);
  pcol->addWidget(pbOk);

  // One width for the whole column, as Windows gives them, rather than
  // letting the longest label set it.
  int dxBut = 0;
  for (QPushButton *ppb : rgpbCol)
    dxBut = Max(dxBut, ppb->sizeHint().width());
  for (QPushButton *ppb : rgpbCol)
    ppb->setFixedWidth(dxBut);

  pmiddle->addStretch(1);
  pmiddle->addLayout(pcol);
  pouter->addLayout(pmiddle);
  pbottom->addStretch(1);
  pouter->addLayout(pbottom);

  PrepareDialogQt(&dlg);
  if (dlg.exec() != QDialog::Accepted)
    return;

  for (i = lo; i <= hi; i++)
    rgignore[i] = rgpcb[i-lo]->isChecked();
  AdjustRestrictions();
  // Windows re-derives the Setting menu's category checkmarks here (the
  // us.fCusp/fUranian/fDwarf block at the end of DlgRestrict, and the
  // equivalents in DlgStar and DlgMoons).
  SyncRestrictMenuQt();
  RecastAndRedrawQt();
}

// Windows' dlgRestrict shows objects 0 through dwarfHi -- the core bodies
// plus all the cusps, Uranians, and dwarfs -- not just 0 through oCore.
static CONST RESBUT rgbutRestrict[] = {
  {"&Restrict All",   resSet,    0,       dwarfHi},
  {"&Unrestrict All", resClear,  0,       dwarfHi},
  {"Toggle Minors",   resToggle, oMain+1, oCore},
  {"Toggle Cusps",    resToggle, cuspLo,  cuspHi},
  {"Toggle Uran.",    resToggle, uranLo,  uranHi},
  {"Toggle Dwarfs",   resToggle, dwarfLo, dwarfHi},
  {"Copy &from Transit Restriction Set", resCopy, 0, dwarfHi, ignore2},
  {"Recall",          resCopy,   0,       dwarfHi, ignoreMem},
};

static CONST RESBUT rgbutTransit[] = {
  {"&Restrict All",   resSet,    0,       dwarfHi},
  {"&Unrestrict All", resClear,  0,       dwarfHi},
  {"Toggle Minors",   resToggle, oMain+1, oCore},
  {"Toggle Cusps",    resToggle, cuspLo,  cuspHi},
  {"Toggle Uran.",    resToggle, uranLo,  uranHi},
  {"Toggle Dwarfs",   resToggle, dwarfLo, dwarfHi},
  {"Copy &From Standard Restriction Set", resCopy, 0, dwarfHi, ignore},
  {"Recall",          resCopy,   0,       dwarfHi, ignore2Mem},
};

static CONST RESBUT rgbutStar[] = {
  {"&Restrict All",   resSet,   starLo, starHi},
  {"&Unrestrict All", resClear, starLo, starHi},
};

// Windows' moon toggles are written as offsets from the dialog's first
// checkbox (dbMo_Mar covers 0..1, dbMo_Jup 2..5, and so on in DlgMoons);
// they're absolute object indexes here.
static CONST RESBUT rgbutMoons[] = {
  {"&Restrict All",   resSet,    moonsLo,    cobHi},
  {"&Unrestrict All", resClear,  moonsLo,    cobHi},
  {"Toggle &Mars",    resToggle, moonsLo+0,  moonsLo+1},
  {"Toggle &Jupiter", resToggle, moonsLo+2,  moonsLo+5},
  {"Toggle &Saturn",  resToggle, moonsLo+6,  moonsLo+13},
  {"Toggle Ur&anus",  resToggle, moonsLo+14, moonsLo+18},
  {"Tog. &Neptune",   resToggle, moonsLo+19, moonsLo+21},
  {"Toggle &Pluto",   resToggle, moonsLo+22, moonsLo+26},
  {"Toggle CO&B",     resToggle, moonsLo+27, moonsLo+31},
};

void ShowRestrictDialogQt()
{
  // Wide enough for the "Copy from ... Restriction Set" button.
  ShowRestrictRangeDialogQt("Object Restrictions", 0, dwarfHi, ignore,
    rgbutRestrict, CButRes(rgbutRestrict), 0,
    rgGroupRestrictQt, cGroupRestrictQt);
}

void ShowStarRestrictDialogQt()
{
  ShowRestrictRangeDialogQt("Fixed Star Restrictions", starLo, starHi,
    ignore, rgbutStar, CButRes(rgbutStar), 4, NULL, 0);
}

void ShowTransitRestrictDialogQt()
{
  ShowRestrictRangeDialogQt("Transit Object Restrictions", 0, dwarfHi,
    ignore2, rgbutTransit, CButRes(rgbutTransit), 0,
    rgGroupRestrictQt, cGroupRestrictQt);
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
  QComboBox *peDst  = NewComboQt(ciDefa.dst == 0.0 ? "No" :
    (ciDefa.dst == 1.0 ? "Yes" :
    (ciDefa.dst == dstAuto ? "Autodetect" : SzZone(ciDefa.dst))),
    RgstrDstQt());
  sprintf(sz, "%s", SzZone(ciDefa.zon));
  QComboBox *peZon  = NewComboQt(sz[0] == '+' ? &sz[1] : sz, RgstrZoneQt());
  int nSavChar = us.fAnsiChar; us.fAnsiChar = fFalse;
  sprintf(sz, "%s", SzLocation(ciDefa.lon, ciDefa.lat));
  us.fAnsiChar = nSavChar;
  sz[is.ichLocSplit] = chNull;
  QComboBox *peLon  = NewComboQt(&sz[0], RgstrLonQt());
  QComboBox *peLat  = NewComboQt(&sz[is.ichLocSplit+1], RgstrLatQt());
  // Windows offers these three presets apiece (dcDeElv/dcDeTmp/dcDeCor).
  QComboBox *peElv  = NewComboQt(SzElevation(us.elvDef),
    QStringList() << "0m" << "1000ft");
  QComboBox *peTmp  = NewComboQt(SzTemperature(us.tmpDef),
    QStringList() << "0C" << "32F");
  QComboBox *peCor  = NewComboQt(QString::number(us.lTimeAddition),
    QStringList() << "60" << "0" << "-60");
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

  PrepareDialogQt(&dlg);
  if (dlg.exec() != QDialog::Accepted)
    return;

  CI ci = ciDefa;
  QByteArray ba;
  ba = peDst->currentText().toLocal8Bit(); ci.dst = RParseSz(ba.constData(), pmDst);
  ba = peZon->currentText().toLocal8Bit(); ci.zon = RParseSz(ba.constData(), pmZon);
  ba = peLon->currentText().toLocal8Bit(); ci.lon = RParseSz(ba.constData(), pmLon);
  ba = peLat->currentText().toLocal8Bit(); ci.lat = RParseSz(ba.constData(), pmLat);

  if (!FValidDst(ci.dst) || !FValidZon(ci.zon) ||
    !FValidLon(ci.lon) || !FValidLat(ci.lat)) {
    QMessageBox::warning(gi.qwind, szAppName,
      "One or more chart info fields are invalid.");
    return;
  }
  ba = peElv->currentText().toLocal8Bit(); us.elvDef = RParseSz(ba.constData(), pmElv);
  ba = peTmp->currentText().toLocal8Bit(); us.tmpDef = RParseSz(ba.constData(), pmTmp);
  us.lTimeAddition = peCor->currentText().toLong();
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
  QComboBox *peMon = NewComboQt(sz, RgstrMonthQt());
  QComboBox *peDay = NewComboQt(QString::number(ciTran.day),
    RgstrDayQt());
  QComboBox *peYea = NewComboQt(QString::number(ciTran.yea),
    RgstrYearQt());
  QComboBox *peTim = NewComboQt(SzTim(ciTran.tim), RgstrTimeQt());
  QComboBox *peDst = NewComboQt(ciTran.dst == 0.0 ? "No" :
    (ciTran.dst == 1.0 ? "Yes" :
    (ciTran.dst == dstAuto ? "Autodetect" : SzZone(ciTran.dst))),
    RgstrDstQt());
  sprintf(sz, "%s", SzZone(ciTran.zon));
  QComboBox *peZon = NewComboQt(sz[0] == '+' ? &sz[1] : sz,
    RgstrZoneQt());
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
      peMon->setEditText(szT);
      peDay->setEditText(QString::number(day));
      peYea->setEditText(QString::number(yea));
      peTim->setEditText(SzTim(tim));
      peDst->setEditText(ciDefa.dst == 0.0 ? "No" :
        (ciDefa.dst == 1.0 ? "Yes" :
        (ciDefa.dst == dstAuto ? "Autodetect" : SzZone(ciDefa.dst))));
      sprintf(szT, "%s", SzZone(ciDefa.zon));
      peZon->setEditText(szT[0] == '+' ? &szT[1] : szT);
    });

  PrepareDialogQt(&dlg);
  if (dlg.exec() != QDialog::Accepted)
    return;

  int mon, day, yea;
  real tim, dst, zon;
  QByteArray ba;
  ba = peMon->currentText().toLocal8Bit(); mon = NParseSz(ba.constData(), pmMon);
  ba = peDay->currentText().toLocal8Bit(); day = NParseSz(ba.constData(), pmDay);
  ba = peYea->currentText().toLocal8Bit(); yea = NParseSz(ba.constData(), pmYea);
  ba = peTim->currentText().toLocal8Bit(); tim = RParseSz(ba.constData(), pmTim);
  ba = peDst->currentText().toLocal8Bit(); dst = RParseSz(ba.constData(), pmDst);
  ba = peZon->currentText().toLocal8Bit(); zon = RParseSz(ba.constData(), pmZon);
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
  QComboBox *peMon = NewComboQt(sz, RgstrMonthQt());
  QComboBox *peDay = NewComboQt(QString::number(ciTran.day),
    RgstrDayQt());
  QComboBox *peYea = NewComboQt(QString::number(ciTran.yea),
    RgstrYearQt());
  QComboBox *peTim = NewComboQt(SzTim(ciTran.tim), RgstrTimeQt());
  QComboBox *peDst = NewComboQt(ciTran.dst == 0.0 ? "No" :
    (ciTran.dst == 1.0 ? "Yes" :
    (ciTran.dst == dstAuto ? "Autodetect" : SzZone(ciTran.dst))),
    RgstrDstQt());
  sprintf(sz, "%s", SzZone(ciTran.zon));
  QComboBox *peZon = NewComboQt(sz[0] == '+' ? &sz[1] : sz,
    RgstrZoneQt());
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
      peMon->setEditText(szN);
      peDay->setEditText(QString::number(day));
      peYea->setEditText(QString::number(yea));
      peTim->setEditText(SzTim(tim));
      peDst->setEditText(ciDefa.dst == 0.0 ? "No" :
        (ciDefa.dst == 1.0 ? "Yes" :
        (ciDefa.dst == dstAuto ? "Autodetect" : SzZone(ciDefa.dst))));
      sprintf(szN, "%s", SzZone(ciDefa.zon));
      peZon->setEditText(szN[0] == '+' ? &szN[1] : szN);
    });

  PrepareDialogQt(&dlg);
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
  ba = peMon->currentText().toLocal8Bit(); mon = NParseSz(ba.constData(), pmMon);
  ba = peDay->currentText().toLocal8Bit(); day = NParseSz(ba.constData(), pmDay);
  ba = peYea->currentText().toLocal8Bit(); yea = NParseSz(ba.constData(), pmYea);
  ba = peTim->currentText().toLocal8Bit(); tim = RParseSz(ba.constData(), pmTim);
  ba = peDst->currentText().toLocal8Bit(); dst = RParseSz(ba.constData(), pmDst);
  ba = peZon->currentText().toLocal8Bit(); zon = RParseSz(ba.constData(), pmZon);
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


// Chart settings, equivalent to Windows' DlgChart: per chart type display
// options, sort orders, and a few counts.

// Aspect list sort orders. Windows keeps this in wdialog.cpp, which isn't
// compiled into the QT build.
static CONST char *rgszSortQt[asMax] = {"Power", "Orb Magnitude",
  "Orb Value", "1st Object Index", "2nd Object Index",
  "Aspect", "1st Object Position", "2nd Object Position", "Midpoint"};

void ShowChartSettingsDialogQt()
{
  QDialog dlg(gi.qwind);
  dlg.setWindowTitle("Chart Settings");
  dlg.resize(760, 620);
  QVBoxLayout *pouter = new QVBoxLayout(&dlg);
  QScrollArea *pscroll = new QScrollArea(&dlg);
  QWidget *pinner = new QWidget();
  QHBoxLayout *phInner = new QHBoxLayout(pinner);
  QVBoxLayout *pvLeft = new QVBoxLayout();
  QVBoxLayout *pvRight = new QVBoxLayout();
  char sz[cchSzMax];
  int i;

  QFormLayout *pformTop = new QFormLayout();
  QComboBox *pcbDecan = new QComboBox();
  for (i = 0; i < ddMax; i++)
    pcbDecan->addItem(rgszDecan[i]);
  pcbDecan->setCurrentIndex(us.fListDecan ? us.nDecanType : ddNone);
  QLineEdit *peWheelRows = new QLineEdit(QString::number(us.nWheelRows));
  pformTop->addRow("Wheel Sign Subdivision Type:", pcbDecan);
  pformTop->addRow("Text House Wheel Rows:", peWheelRows);
  pvLeft->addLayout(pformTop);

  QCheckBox *pcbVelocity = new QCheckBox(
    "Text Listing Velocities Relative to Average Speed");
  QCheckBox *pcbWheelReverse = new QCheckBox(
    "Text House Wheel Reverses Object Order");
  QCheckBox *pcbGridConfig = new QCheckBox(
    "Text Aspect Grid Shows Aspect Configurations");
  QCheckBox *pcbGridMidpoint = new QCheckBox(
    "Relationship Aspect Grid Shows Midpoints Instead");
  QCheckBox *pcbAspSummary = new QCheckBox(
    "Text Aspect List Shows Aspect Summary");
  QCheckBox *pcbMidSummary = new QCheckBox(
    "Text Midpoint List Shows Midpoint Summary");
  QCheckBox *pcbMidAspect = new QCheckBox(
    "Text Midpoint List Includes Aspects to Midpoints");
  QCheckBox *pcbPrimeVert = new QCheckBox(
    "Horizon Chart Displays with Polar Center");
  QCheckBox *pcbSectorApprox = new QCheckBox(
    "Sector Chart Approximated with Placidus Cusps");
  QCheckBox *pcbCalendarYear = new QCheckBox("Calendar Is for Entire Year");
  QCheckBox *pcbInfluenceSign = new QCheckBox(
    "Text Influence Chart Shows Sign Influences Too");
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
  for (QCheckBox *pcb : { pcbVelocity, pcbWheelReverse, pcbGridConfig,
    pcbGridMidpoint, pcbAspSummary, pcbMidSummary, pcbMidAspect,
    pcbPrimeVert, pcbSectorApprox, pcbCalendarYear, pcbInfluenceSign })
    pvLeft->addWidget(pcb);

  QFormLayout *pformStep = new QFormLayout();
  QLineEdit *peAstroStep =
    new QLineEdit(QString::number(us.nAstroGraphStep));
  pformStep->addRow("Text Astrocartography Degree Step Rate:", peAstroStep);
  pvLeft->addLayout(pformStep);
  QCheckBox *pcbLatCross = new QCheckBox(
    "Text Astrocartography Shows Latitude Crossings");
  pcbLatCross->setChecked(us.fLatitudeCross != 0);
  pvLeft->addWidget(pcbLatCross);

  QFormLayout *pformCounts = new QFormLayout();
  // Windows shows this one with its unit appended; the parse below only
  // reads the leading number, so the suffix is cosmetic either way.
  sprintf(sz, "%d%s", us.nAstroGraphDist, us.fEuroDist ? "km" : "mi");
  QLineEdit *peAstroDist = new QLineEdit(sz);
  QLineEdit *peArabicParts =
    new QLineEdit(QString::number(us.nArabicParts));
  QLineEdit *peAtlasList = new QLineEdit(QString::number(us.nAtlasList));
  QLineEdit *peBioday = new QLineEdit(QString::number(us.nBioday));
  pformCounts->addRow("Latitude Crossings Show Cities Within:", peAstroDist);
  pformCounts->addRow("Number of Arabic Parts to Display:", peArabicParts);
  pformCounts->addRow("Nearest Cities Lists This Many Cities:", peAtlasList);
  pformCounts->addRow("Number of Days Biorhythm Chart Covers:", peBioday);
  pvLeft->addLayout(pformCounts);
  QCheckBox *pcbEphemYears = new QCheckBox("Ephemeris Is for Entire Year");
  QCheckBox *pcbArabicFlip = new QCheckBox(
    "Display Arabic Part Formulas with Terms Reversed");
  pcbEphemYears->setChecked(us.nEphemYears != 0);
  pcbArabicFlip->setChecked(us.fArabicFlip != 0);
  pvLeft->addWidget(pcbEphemYears);
  pvLeft->addWidget(pcbArabicFlip);
  pvLeft->addStretch(1);

  // The two sort orders are stored as switch letters rather than indexes,
  // so each radio carries its letter alongside.
  QGroupBox *pgbStar = new QGroupBox("Fixed Stars Sort By");
  QVBoxLayout *pvStar = new QVBoxLayout(pgbStar);
  QButtonGroup *pgroupStar = new QButtonGroup(&dlg);
  CONST char *rgszStarSort[7] = { "Object Index", "Longitude", "Latitude",
    "Name", "Brightness", "Distance", "Velocity" };
  CONST char rgchStarSort[7] = { 0, 'z', 'l', 'n', 'b', 'd', 'v' };
  for (i = 0; i < 7; i++) {
    QRadioButton *prb = new QRadioButton(rgszStarSort[i]);
    prb->setChecked(us.nStarSort == rgchStarSort[i]);
    pgroupStar->addButton(prb, i);
    pvStar->addWidget(prb);
  }
  if (pgroupStar->checkedButton() == NULL)
    pgroupStar->button(0)->setChecked(true);
  pvRight->addWidget(pgbStar);

  QGroupBox *pgbArabic = new QGroupBox("Arabic Parts Sort By");
  QVBoxLayout *pvArabic = new QVBoxLayout(pgbArabic);
  QButtonGroup *pgroupArabic = new QButtonGroup(&dlg);
  CONST char *rgszArabicSort[4] =
    { "Category Index", "Position", "Name", "Formula" };
  CONST char rgchArabicSort[4] = { 0, 'z', 'n', 'f' };
  for (i = 0; i < 4; i++) {
    QRadioButton *prb = new QRadioButton(rgszArabicSort[i]);
    prb->setChecked(us.nArabicSort == rgchArabicSort[i]);
    pgroupArabic->addButton(prb, i);
    pvArabic->addWidget(prb);
  }
  if (pgroupArabic->checkedButton() == NULL)
    pgroupArabic->button(0)->setChecked(true);
  pvRight->addWidget(pgbArabic);

  QFormLayout *pformRight = new QFormLayout();
  QComboBox *pcbAspSort = new QComboBox();
  for (i = 0; i < asMax; i++)
    pcbAspSort->addItem(rgszSortQt[i]);
  pcbAspSort->setCurrentIndex(FBetween(us.nAspectSort, 0, asMax-1) ?
    us.nAspectSort : 0);
  QLineEdit *peRatio = new QLineEdit(QString::number(us.rRatio));
  pformRight->addRow("Aspect List Sort By:", pcbAspSort);
  pformRight->addRow("Midpoint Proportion:", peRatio);
  pvRight->addLayout(pformRight);
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

  PrepareDialogQt(&dlg);
  if (dlg.exec() != QDialog::Accepted)
    return;

  int nWheelRows = peWheelRows->text().toInt();
  int nAstroStep = peAstroStep->text().toInt();
  int nAstroDist = peAstroDist->text().toInt();
  int nArabicParts = peArabicParts->text().toInt();
  int nAtlasList = peAtlasList->text().toInt();
  int nBioday = peBioday->text().toInt();
  if (!FValidWheel(nWheelRows) || !FValidAstrograph(nAstroStep) ||
    nAstroDist < 0 || !FValidPart(nArabicParts) || nAtlasList < 0 ||
    !FValidBioday(nBioday)) {
    QMessageBox::warning(gi.qwind, szAppName,
      "One or more chart settings fields are invalid.");
    return;
  }

  us.fVelocity = pcbVelocity->isChecked();
  us.nWheelRows = nWheelRows;
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
  us.nAstroGraphStep = nAstroStep;
  us.fLatitudeCross = pcbLatCross->isChecked();
  us.nAstroGraphDist = nAstroDist;
  us.nEphemYears = pcbEphemYears->isChecked();
  us.nArabicParts = nArabicParts;
  us.fArabicFlip = pcbArabicFlip->isChecked();
  us.nAtlasList = nAtlasList;
  us.nBioday = nBioday;
  us.nStarSort = rgchStarSort[pgroupStar->checkedId()];
  us.nArabicSort = rgchArabicSort[pgroupArabic->checkedId()];
  us.nAspectSort = pcbAspSort->currentIndex();
  us.rRatio = peRatio->text().toDouble();
  i = pcbDecan->currentIndex();
  us.fListDecan = (i > ddNone);
  if (i > ddNone)
    us.nDecanType = i;
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
  dlg.resize(500, 130);
  QVBoxLayout *pouter = new QVBoxLayout(&dlg);
  pouter->addWidget(new QLabel("Enter command switches below:"));
  QLineEdit *peLine = new QLineEdit();
  pouter->addWidget(peLine);
  // Windows has this checkbox too (dxCo_e), and applies it before running
  // the switches so a line can be tried with expressions off.
  QCheckBox *pcbExp = new QCheckBox("Enable AstroExpression hooks");
  pcbExp->setChecked(!us.fExpOff);
  pouter->addWidget(pcbExp);

  QDialogButtonBox *pbuttons =
    new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  pouter->addWidget(pbuttons);
  QObject::connect(pbuttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(pbuttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  PrepareDialogQt(&dlg);
  if (dlg.exec() != QDialog::Accepted || peLine->text().trimmed().isEmpty())
    return;

  us.fExpOff = !pcbExp->isChecked();
  char szLine[cchSzMax];
  QByteArray ba = peLine->text().toLocal8Bit();
  strncpy(szLine, ba.constData(), cchSzMax-1);
  szLine[cchSzMax-1] = chNull;
  char *rgsz[MAXSWITCHES];
  int argc = NParseCommandLine(szLine, rgsz);
  ciCore = ciMain;
  // A chart-type switch has to be routed back through SetChartModeQt(),
  // or gi.nMode and the Chart menu stay where the menus last put them.
  QVector<flag> rgfMode(CChartModeQt());
  SnapChartModeQt(rgfMode.data());
  if (argc <= 0 || !FProcessSwitches(argc, rgsz))
    QMessageBox::warning(gi.qwind, szAppName,
      "One or more switches were not understood.");
  ciMain = ciCore;
  InitColorsX();
  SyncChartModeFromFlagsQt(rgfMode.constData());
  RecastAndRedrawQt();
}


// About, equivalent to Windows' DlgAbout.

// The credits and license text Windows' dlgAbout carries, verbatim.
// Astrolog's own license requires these notices stay with the program, so
// they belong here rather than being trimmed to a version number -- which
// is all this dialog used to show.

static CONST char *rgszAboutQt[] = {
  "By Walter D. Pullen (Astara@msn.com)",
  "Astrolog Website: http://www.astrolog.org/astrolog.htm",
  "Astrolog Website mirror: http://www.magitech.com/astrolog/astrolog.htm",
  "",
  "Main ephemeris databases and calculation routines are from the library",
  "'Swiss Ephemeris' by Astrodienst AG, subject to license for Swiss",
  "Ephemeris Free Edition at http://www.astro.com/swisseph. Old 'Placalc'",
  "library and formulas are by Alois Treindl, also from Astrodienst AG.",
  "",
  "Original planetary calculation formulas were converted from",
  "routines by James Neely, as listed in 'Manual of Computer Programming",
  "for Astrologers' by Michael Erlewine, available from Matrix Software.",
  "",
  "Atlas list of city locations from GeoNames: https://www.geonames.org/",
  "Timezone and Daylight Saving Time date changes converted from",
  "TZ database: https://data.iana.org/time-zones/tz-link.html",
  "PostScript graphics routines by Brian D. Willoughby.",
  "",
  "IMPORTANT: Astrolog is free software. You can distribute and/or modify",
  "it under the terms of the GNU General Public License, as described at",
  "http://www.gnu.org and in the license.htm file included with the",
  "program. Astrolog is distributed without any warranty expressed or",
  "implied of any kind. These license and copyright notices must not be",
  "changed or removed by any user or editor of the program.",
  "",
  "Special thanks to all those unmentioned, seen and",
  "unseen, who have pointed out problems, suggested",
  "features, and sent many positive vibes! :-)" };

void ShowAboutDialogQt()
{
  QDialog dlg(gi.qwind);
  int i;

  dlg.setWindowTitle("About Astrolog");
  QVBoxLayout *playout = new QVBoxLayout(&dlg);
  // Windows' first two lines say "for <arch> Windows"; say what this
  // build actually is instead.
  QLabel *plabelVer = new QLabel(
    QString("%1 version %2 for Linux (Qt)").arg(szAppName, szVersionCore));
  QFont fontBold = plabelVer->font();
  fontBold.setBold(true);
  plabelVer->setFont(fontBold);
  playout->addWidget(plabelVer);
  playout->addWidget(new QLabel(QString("Released %1").arg(szDateCore)));
  playout->addSpacing(8);
  for (i = 0; i < (int)(sizeof(rgszAboutQt)/sizeof(char *)); i++) {
    if (!*rgszAboutQt[i]) {
      playout->addSpacing(6);
      continue;
    }
    QLabel *plabel = new QLabel(rgszAboutQt[i]);
    // Makes the URLs clickable, and lets the text be selected and copied.
    plabel->setTextFormat(Qt::PlainText);
    plabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    plabel->setOpenExternalLinks(true);
    playout->addWidget(plabel);
  }
  playout->addSpacing(8);
  QDialogButtonBox *pbuttons = new QDialogButtonBox(QDialogButtonBox::Ok);
  playout->addWidget(pbuttons);
  QObject::connect(pbuttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  PrepareDialogQt(&dlg);
  dlg.exec();
}


// Aspect settings, equivalent to Windows' DlgAspect: per aspect maximum
// orb, exact angle, influence, color, and restriction, for all cAspect
// aspects.

void ShowAspectDialogQt()
{
  // Laid out to match Windows' dlgAspect (astrolog.rc): two columns of 12
  // aspects side by side, sized to fit, with no scrolling. Astrolog's
  // Windows dialogs are all dense like this -- a single tall scrolling
  // column is the wrong shape for them however well it scales.
  //
  // The checkbox is the aspect's name and means *restricted*, as it does on
  // Windows: checked hides the aspect. It used to be a separate "Show"
  // column with the opposite sense, which both took an extra grid column
  // and inverted the meaning of every box relative to the Windows build.
  CONST int cCol = 2, cRow = cAspect / 2;
  QDialog dlg(gi.qwind);
  dlg.setWindowTitle("Aspect Settings");
  QVBoxLayout *pouter = new QVBoxLayout(&dlg);
  QGridLayout *pgrid = new QGridLayout();
  QVector<QCheckBox *> rgpcbRes;
  QVector<QLineEdit *> rgpeOrb, rgpeAngle, rgpeInf;
  QVector<QComboBox *> rgpcbColor;
  int i, j, iCol, iRow, nBase;

  rgpcbRes.resize(cAspect); rgpeOrb.resize(cAspect);
  rgpeAngle.resize(cAspect); rgpeInf.resize(cAspect);
  rgpcbColor.resize(cAspect);
  pgrid->setHorizontalSpacing(4);
  pgrid->setVerticalSpacing(2);
  for (iCol = 0; iCol < cCol; iCol++) {
    nBase = iCol * 6;
    pgrid->addWidget(new QLabel("Orb"), 0, nBase+1);
    pgrid->addWidget(new QLabel("Angle"), 0, nBase+2);
    pgrid->addWidget(new QLabel("Influence"), 0, nBase+3);
    pgrid->addWidget(new QLabel("Color"), 0, nBase+4);
    // A little air between the two blocks, as the Windows dialog has.
    if (iCol < cCol-1)
      pgrid->setColumnMinimumWidth(nBase+5, 10);
    for (iRow = 0; iRow < cRow; iRow++) {
      i = iCol*cRow + iRow + 1;
      QCheckBox *pcb = new QCheckBox(SzAspDlgQt(i));
      pcb->setChecked(ignorea[i] != 0);
      pgrid->addWidget(pcb, iRow+1, nBase+0);
      rgpcbRes[i-1] = pcb;
      QLineEdit *peOrb = new QLineEdit(SzFormatRQt(rAspOrb[i], -6));
      pgrid->addWidget(peOrb, iRow+1, nBase+1);
      rgpeOrb[i-1] = peOrb;
      QLineEdit *peAngle = new QLineEdit(SzFormatRQt(rAspAngle[i], -6));
      pgrid->addWidget(peAngle, iRow+1, nBase+2);
      rgpeAngle[i-1] = peAngle;
      QLineEdit *peInf = new QLineEdit(SzFormatRQt(rAspInf[i], 2));
      pgrid->addWidget(peInf, iRow+1, nBase+3);
      rgpeInf[i-1] = peInf;
      QComboBox *pcbColor = NewColorComboQt(kAspA[i], 0);
      pgrid->addWidget(pcbColor, iRow+1, nBase+4);
      rgpcbColor[i-1] = pcbColor;
    }
  }
  pouter->addLayout(pgrid);

  // Windows puts Restrict All / Unrestrict All / Toggle Majors along the
  // bottom left, opposite Cancel and OK.
  QDialogButtonBox *pbuttons =
    new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  QPushButton *pbRes = new QPushButton("&Restrict All");
  QPushButton *pbUnres = new QPushButton("&Unrestrict All");
  QPushButton *pbMajor = new QPushButton("Toggle &Majors");
  pbuttons->addButton(pbRes, QDialogButtonBox::ResetRole);
  pbuttons->addButton(pbUnres, QDialogButtonBox::ResetRole);
  pbuttons->addButton(pbMajor, QDialogButtonBox::ResetRole);
  pouter->addWidget(pbuttons);
  QObject::connect(pbRes, &QPushButton::clicked, &dlg, [&rgpcbRes]() {
    for (int k = 0; k < cAspect; k++) rgpcbRes[k]->setChecked(true); });
  QObject::connect(pbUnres, &QPushButton::clicked, &dlg, [&rgpcbRes]() {
    for (int k = 0; k < cAspect; k++) rgpcbRes[k]->setChecked(false); });
  // Windows toggles just the five majors here (wdialog.cpp:1380).
  QObject::connect(pbMajor, &QPushButton::clicked, &dlg, [&rgpcbRes]() {
    for (int k = 0; k < 5; k++)
      rgpcbRes[k]->setChecked(!rgpcbRes[k]->isChecked()); });
  QObject::connect(pbuttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(pbuttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  PrepareDialogQt(&dlg);
  if (dlg.exec() != QDialog::Accepted)
    return;

  for (i = 1; i <= cAspect; i++) {
    ignorea[i] = rgpcbRes[i-1]->isChecked();
    rAspOrb[i] = rgpeOrb[i-1]->text().toDouble();
    rAspAngle[i] = rgpeAngle[i-1]->text().toDouble();
    rAspInf[i] = rgpeInf[i-1]->text().toDouble();
    kAspA[i] = NColorFromComboQt(rgpcbColor[i-1]);
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
  // Three columns, as Windows' dlgObject2 has (astrolog.rc column X 5,
  // 175 and 335).
  CONST int cCol = 3, cField = 4;
  CONST int cRowTotal = dwarfHi + 1 - oAsc + 1;
  CONST int cRowPerCol = (cRowTotal + cCol - 1) / cCol;
  CONST char *rgszHead[] = {"Max Orb", "Add", "Influence", "Color"};
  QDialog dlg(gi.qwind);
  dlg.setWindowTitle("More Object Settings");
  QVBoxLayout *pouter = new QVBoxLayout(&dlg);
  QGridLayout *pgrid = new QGridLayout();
  QVector<int> rgi;
  QVector<QLineEdit *> rgpeOrb, rgpeAdd, rgpeInf;
  QVector<QComboBox *> rgpcbColor;
  int i0, i, iRow = 0, nRow, nCol;

  pgrid->setHorizontalSpacing(4);
  pgrid->setVerticalSpacing(2);
  HeadersQt(pgrid, cCol, cField, rgszHead);
  for (i0 = oAsc; i0 <= dwarfHi+1; i0++) {
    i = (i0 <= dwarfHi ? i0 : starLo);
    rgi.append(i);
    PlaceRowQt(iRow, cRowPerCol, cField, &nRow, &nCol);
    pgrid->addWidget(new QLabel(i0 <= dwarfHi ? SzObjDlgPlainQt(i) :
      QString("Stars")), nRow, nCol);
    QLineEdit *peOrb = new QLineEdit(SzFormatRQt(rObjOrb[i], -2));
    pgrid->addWidget(peOrb, nRow, nCol+1);
    rgpeOrb.append(peOrb);
    QLineEdit *peAdd = new QLineEdit(SzFormatRQt(rObjAdd[i], -1));
    pgrid->addWidget(peAdd, nRow, nCol+2);
    rgpeAdd.append(peAdd);
    QLineEdit *peInf = new QLineEdit(SzFormatRQt(rObjInf[i], -2));
    pgrid->addWidget(peInf, nRow, nCol+3);
    rgpeInf.append(peInf);
    // Windows widens the color list by one on the collective stars row.
    QComboBox *pcbColor = NewColorComboQt(kObjU[i], 1 + (i == starLo));
    pgrid->addWidget(pcbColor, nRow, nCol+4);
    rgpcbColor.append(pcbColor);
    iRow++;
  }
  pouter->addLayout(pgrid);

  QDialogButtonBox *pbuttons =
    new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  pouter->addWidget(pbuttons);
  QObject::connect(pbuttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(pbuttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  PrepareDialogQt(&dlg);
  if (dlg.exec() != QDialog::Accepted)
    return;

  for (iRow = 0; iRow < rgi.size(); iRow++) {
    i = rgi[iRow];
    rObjOrb[i] = rgpeOrb[iRow]->text().toDouble();
    rObjAdd[i] = rgpeAdd[iRow]->text().toDouble();
    rObjInf[i] = rgpeInf[iRow]->text().toDouble();
    kObjU[i] = NColorFromComboQt(rgpcbColor[iRow]);
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
  // Wide enough for "Local Horizon Positions Apply Atmospheric Refraction".
  dlg.resize(530, 600);
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

  // Windows offers the named ayanamsas as dropdown entries formatted
  // "<offset> <name>", and reads the field back with atof(), which stops
  // at the space -- so picking an entry and typing a bare number both
  // work. addItem() before setEditText(): see the Progressions dialog.
  QComboBox *pcbOffset = new QComboBox();
  pcbOffset->setEditable(true);
  QString strOffset = SzFormatRQt(us.rZodiacOffset, 6);
  for (i = 0; *rgZodiacOffset[i].sz; i++) {
    QString str = SzFormatRQt(rgZodiacOffset[i].r, 6) + " " +
      rgZodiacOffset[i].sz;
    pcbOffset->addItem(str);
    if (us.rZodiacOffset == rgZodiacOffset[i].r)
      strOffset = str;
  }
  pcbOffset->setEditText(strOffset);
  pform1->addRow("Zodiac Offset / Ayanamsa:", pcbOffset);

  // Editable, like Windows' dcSe_c, so a house system can be typed as
  // well as picked; NParseSz() accepts either the name or its index.
  QComboBox *pcbSystem = new QComboBox();
  pcbSystem->setEditable(true);
  for (i = 0; i < cSystem; i++)
    pcbSystem->addItem(szSystem[i]);
  pcbSystem->setEditText(szSystem[us.nHouseSystem]);
  pform1->addRow("House System:", pcbSystem);

  QLineEdit *peCenter = new QLineEdit(szObjName[us.objCenter]);
  pform1->addRow("Central Planet:", peCenter);

  QLineEdit *peHarmonic = new QLineEdit(SzFormatRQt(us.rHarmonic, -6));
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
  // Windows' dlgCalc reads top to bottom as: the four coordinate-system
  // boxes beside the form fields, then the wide ones, then the 3D pair,
  // then "3D Houses" immediately above its plane group. "Use Start of
  // Planet's Sign" belongs to the Solar Chart group and is added there.
  for (QCheckBox *pcb : { pcbEquator2, pcbEquator, pcbTruePos, pcbTopoPos,
    pcbBary, pcbTrueNode, pcbHouseAngle, pcbRefract, pcbSidereal2,
    pcbNoNutation, pcbAspect3D, pcbAspectLat, pcbHouse3D })
    pinnerlayout->addWidget(pcb);

  QGroupBox *pgroupBox3D = new QGroupBox("3D Houses Plane");
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

  QGroupBox *pgroupBoxAsc = new QGroupBox("Solar Chart Setting");
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
  pformAsc->addRow("Use This Planet:", peOnAsc);
  pgrouplayoutAsc->addLayout(pformAsc);
  pgrouplayoutAsc->addWidget(pcbSolarWhole);
  pinnerlayout->addWidget(pgroupBoxAsc);

  pscroll->setWidget(pinner);
  pscroll->setWidgetResizable(true);
  pouter->addWidget(pscroll);

  QDialogButtonBox *pbuttons =
    new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  pouter->addWidget(pbuttons);
  QObject::connect(pbuttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(pbuttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  PrepareDialogQt(&dlg);
  if (dlg.exec() != QDialog::Accepted)
    return;

  // RFromSz() rather than toDouble(): it is what Windows reads these
  // fields with, it stops at the space in "24.0 Lahiri", and it also
  // accepts a leading "~" AstroExpression where that's compiled in.
  QByteArray ba;
  ba = pcbOffset->currentText().toLocal8Bit();
  real rOffset = RFromSz(ba.constData());
  ba = pcbSystem->currentText().toLocal8Bit();
  int nSystem = NParseSz(ba.constData(), pmSystem);
  QByteArray baCenter = peCenter->text().toLocal8Bit();
  int nCenter = NParseSz(baCenter.constData(), pmObject);
  // A leading "D" asks for a divisional chart rather than a harmonic:
  // "D9" is the navamsa, i.e. 360/9, not a harmonic factor of 9.
  ba = peHarmonic->text().toLocal8Bit();
  CONST char *szHarmonic = ba.constData();
  int fDivisional = (ChCap(szHarmonic[0]) == 'D');
  real rHarmonic = RFromSz(szHarmonic + fDivisional);
  if (fDivisional && rHarmonic != 0.0)
    rHarmonic = rDegMax / rHarmonic;
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
  // Windows re-syncs the House Settings submenu here (the WiCheckMenu
  // calls for cmdHouseSetSolar/cmdHouseSet3D/cmdHouseSetDwad in DlgCalc).
  SyncHouseSetMenuQt();
  RecastAndRedrawQt();
}


// Display settings, equivalent to Windows' DlgDisplay: date/time/number
// formatting, aspect count and requirements, eclipse display, and the
// angle/rulership restriction checkbox grids.

void ShowDisplayDialogQt()
{
  QDialog dlg(gi.qwind);
  dlg.setWindowTitle("Display Settings");
  // Wide enough for "Eclipses Only at Location (Not Anywhere in World)".
  dlg.resize(530, 650);
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
  QLineEdit *peStation = new QLineEdit(SzFormatRQt(us.rStation, -6));
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
    "Nadir Crossing", "Vertex", "Antivert" };
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
  // Listed in Windows' on-screen order, which is not value order: it
  // shows Nakshatras (3) above 360 Degrees (2). The button id is the
  // us.nDegForm value, so the order here is presentation only.
  CONST char *rgszDegForm[4] =
    { "Zodiac Position", "Hours & Minutes", "27 Nakshatras", "360 Degrees" };
  CONST int rgnDegForm[4] = { 0, 1, 3, 2 };
  for (i = 0; i < 4; i++) {
    QRadioButton *prb = new QRadioButton(rgszDegForm[i]);
    prb->setChecked(rgnDegForm[i] == us.nDegForm);
    pgroupDegForm->addButton(prb, rgnDegForm[i]);
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

  PrepareDialogQt(&dlg);
  if (dlg.exec() != QDialog::Accepted)
    return;

  QByteArray ba;
  ba = peAsp->text().toLocal8Bit();
  int na = NParseSz(ba.constData(), pmAspect);
  ba = peReqObj->text().toLocal8Bit();
  int nro = NParseSz(ba.constData(), pmObject);
  int ni = peScreenWidth->text().toInt();
  ba = peStation->text().toLocal8Bit();
  real ryw = RFromSz(ba.constData());
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
  // Windows re-syncs the View menu here (WiCheckMenu for cmdSecond and
  // cmdApplying in DlgDisplay).
  SyncDisplayMenuQt();
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
  ShowRestrictRangeDialogQt("Planetary Moon Restrictions", moonsLo, cobHi,
    ignore, rgbutMoons, CButRes(rgbutMoons), 0,
    rgGroupMoonQt, cGroupMoonQt);
}


// Moon object settings, equivalent to Windows' DlgObjectM: per moon/COB
// object orb/color settings (the same grid shape as
// ShowObject2DialogQt()), plus 3 moon chart display toggles.

void ShowMoonObjectDialogQt()
{
  QDialog dlg(gi.qwind);
  dlg.setWindowTitle("Moon Object Settings");
  QVBoxLayout *pouter = new QVBoxLayout(&dlg);
  QGridLayout *pgrid = new QGridLayout();
  QVector<QLineEdit *> rgpeOrb, rgpeAdd, rgpeInf;
  QVector<QComboBox *> rgpcbColor;
  // Three columns, as Windows' dlgObjectM has (astrolog.rc X 5, 171, 330).
  CONST int cCol = 3, cField = 4;
  CONST int cRowPerCol = (cobHi - moonsLo + 1 + cCol - 1) / cCol;
  CONST char *rgszHead[] = {"Max Orb", "Add", "Influence", "Color"};
  int i, iRow = 0, nRow, nCol;

  pgrid->setHorizontalSpacing(4);
  pgrid->setVerticalSpacing(2);
  HeadersQt(pgrid, cCol, cField, rgszHead);
  for (i = moonsLo; i <= cobHi; i++) {
    PlaceRowQt(iRow, cRowPerCol, cField, &nRow, &nCol);
    pgrid->addWidget(new QLabel(szObjName[i]), nRow, nCol+0);
    QLineEdit *peOrb = new QLineEdit(SzFormatRQt(rObjOrb[i], -2));
    pgrid->addWidget(peOrb, nRow, nCol+1);
    rgpeOrb.append(peOrb);
    QLineEdit *peAdd = new QLineEdit(SzFormatRQt(rObjAdd[i], -1));
    pgrid->addWidget(peAdd, nRow, nCol+2);
    rgpeAdd.append(peAdd);
    QLineEdit *peInf = new QLineEdit(SzFormatRQt(rObjInf[i], -2));
    pgrid->addWidget(peInf, nRow, nCol+3);
    rgpeInf.append(peInf);
    QComboBox *pcbColor = NewColorComboQt(kObjU[i], 3);
    pgrid->addWidget(pcbColor, nRow, nCol+4);
    rgpcbColor.append(pcbColor);
    iRow++;
  }
  pouter->addLayout(pgrid);

  QCheckBox *pcbMoonMove = new QCheckBox(
    "Make Moons Orbit Current Central Object");
  QCheckBox *pcbMoonChartSep = new QCheckBox(
    "True Planetcentric Positions in Moons Charts");
  QCheckBox *pcbMoonWheel = new QCheckBox(
    "Wheel Charts Show Moons Orbiting Planet");
  pcbMoonMove->setChecked(us.fMoonMove != 0);
  pcbMoonChartSep->setChecked(us.fMoonChartSep != 0);
  pcbMoonWheel->setChecked(gs.fMoonWheel != 0);
  pouter->addWidget(pcbMoonMove);
  pouter->addWidget(pcbMoonChartSep);
  pouter->addWidget(pcbMoonWheel);

  pouter->addLayout(pgrid);

  QDialogButtonBox *pbuttons =
    new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  pouter->addWidget(pbuttons);
  QObject::connect(pbuttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(pbuttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  PrepareDialogQt(&dlg);
  if (dlg.exec() != QDialog::Accepted)
    return;

  for (i = moonsLo; i <= cobHi; i++) {
    iRow = i - moonsLo;
    rObjOrb[i] = rgpeOrb[iRow]->text().toDouble();
    rObjAdd[i] = rgpeAdd[iRow]->text().toDouble();
    rObjInf[i] = rgpeInf[iRow]->text().toDouble();
    kObjU[i] = NColorFromComboQt(rgpcbColor[iRow]);
  }
  us.fMoonMove = pcbMoonMove->isChecked();
  us.fMoonChartSep = pcbMoonChartSep->isChecked();
  gs.fMoonWheel = pcbMoonWheel->isChecked();
  RecastAndRedrawQt();
}


// Split one Object Customization definition string ("h120", "Mar",
// "2 n", "j2 nHS") into the four values Astrolog keeps for it. Windows
// open-codes this parse twice inside DlgCustom; it is wanted in three
// places here -- validate, apply, and Lookup Names -- so it lives in one
// function rather than being copied a third and fourth time.

static void ParseCustomDefQt(CONST QString &str, char *sz, int *pk, int *pl,
  int *ppnt, int *pflg)
{
  char *pch, *pchEnd;
  int k, l, pnt = 0, flg = 0;

  QByteArray ba = str.toLocal8Bit();
  strncpy(sz, ba.constData(), cchSzMax-1);
  sz[cchSzMax-1] = chNull;
  for (pch = sz; *pch; pch++)
    ;
  pchEnd = pch;
  // Any trailing point/flag letters are separated from the definition
  // proper by a space; snip them off before reading the definition.
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

  // Only read point/flag letters when the walk back stops short of the
  // start of the string. An all-letter definition is an object name, not
  // a run of flags -- without this guard (which Windows has, at the
  // rgPntSwiss assignment in DlgCustom) "Mar" reads its own 'a' as the
  // apsis marker, "Ven" and "Sun" read 'n' as the node marker, and so on.
  for (pch = pchEnd-1; pch > sz && *pch >= 'A'; pch--)
    ;
  if (pch > sz)
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
  *pk = k; *pl = l; *ppnt = pnt; *pflg = flg;
}


// "Lookup Names" (dbCu_l in Windows' DlgCustom): fill in every display
// name still left blank or "???" by resolving its definition string to
// the canonical name. Names already filled in are left alone.

static void LookupCustomNamesQt(QVector<QLineEdit *> &rgpeName,
  QVector<QLineEdit *> &rgpeDef)
{
  char sz[cchSzMax];
  int i, k, l, pnt, flg;

  for (i = 0; i < rgpeName.size(); i++) {
    QString strName = rgpeName[i]->text();
    if (!strName.isEmpty() && strName != szObjUnknown)
      continue;
    ParseCustomDefQt(rgpeDef[i]->text(), sz, &k, &l, &pnt, &flg);
    switch (k) {
    case 0:
      SwissGetObjName(sz, -l);
      break;
    case 1:
      SwissGetObjName(sz, l);
      break;
    case 2:
      sprintf(sz, "%s", FValidObj(l) ? szObjName[l] : szObjUnknown);
      break;
    case 3:
      l = ObjMoons(l);
      sprintf(sz, "%s", FItem(l) ? szObjName[l] : szObjUnknown);
      break;
    case 4:
#ifdef JPLWEB
      {
        // Same as Windows: this one goes out to JPL Horizons over the
        // network, synchronously, while the dialog sits there.
        real rT;
        if (!GetJPLHorizons(l, &rT, &rT, &rT, &rT, &rT, &rT, sz))
          sprintf(sz, "%s", szObjUnknown);
      }
#else
      sprintf(sz, "%s", szObjUnknown);
#endif
      break;
    case 5:
      sprintf(sz, "%s", FValidPart(l) ? ai[l-1].name : szObjUnknown);
      break;
    }
    rgpeName[i]->setText(sz);
  }
}


// Object customization, equivalent to Windows' DlgCustom: redefine each of
// the "custom" (Uranian/dwarf) objects to instead track a hypothetical
// point, a JPL/JPL Horizons body, a moon, an existing object's midpoint, or
// an Arabic part, using the same compact definition-string syntax Windows
// uses (e.g. "h120" for hypothetical Vulcan, "2 n" for the Moon's north
// node point).

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
  char sz[cchSzMax], *pch;

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
  QPushButton *ppbLookup =
    pbuttons->addButton("&Lookup Names", QDialogButtonBox::ActionRole);
  QObject::connect(ppbLookup, &QPushButton::clicked, &dlg,
    [&rgpeName, &rgpeDef]() { LookupCustomNamesQt(rgpeName, rgpeDef); });
  pouter->addWidget(pbuttons);
  QObject::connect(pbuttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(pbuttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  PrepareDialogQt(&dlg);
  if (dlg.exec() != QDialog::Accepted)
    return;

  for (i = custLo; i <= custHi; i++) {
    j = i - custLo;
    ParseCustomDefQt(rgpeDef[j]->text(), sz, &k, &l, &pnt, &flg);
    if (!FValidCustom(l, k)) {
      QMessageBox::warning(gi.qwind, szAppName,
        "One or more object definitions are invalid.");
      return;
    }
  }

  for (i = custLo; i <= custHi; i++) {
    j = i - custLo;
    ParseCustomDefQt(rgpeDef[j]->text(), sz, &k, &l, &pnt, &flg);
    rgTypSwiss[j] = k;
    rgObjSwiss[j] = l;
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
// looks up in the Swiss Ephemeris star list.

void ShowCustomStarDialogQt()
{
  QDialog dlg(gi.qwind);
  dlg.setWindowTitle("Fixed Star Customization");
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
  // "Lookup Names" (dbCu_l in DlgCustomS): for every display name still
  // blank or "???", ask the Swiss Ephemeris star catalog what the entry
  // in the lookup column actually resolves to. SwissTestStar() rewrites
  // its argument in place with the catalog's own spelling.
  QPushButton *ppbLookup =
    pbuttons->addButton("&Lookup Names", QDialogButtonBox::ActionRole);
  QObject::connect(ppbLookup, &QPushButton::clicked, &dlg,
    [&rgpeName, &rgpeDef]() {
      char sz[cchSzMax];
      int row;
      for (row = 0; row < rgpeName.size(); row++) {
        QString strName = rgpeName[row]->text();
        if (!strName.isEmpty() && strName != szObjUnknown)
          continue;
        QByteArray ba = rgpeDef[row]->text().toLocal8Bit();
        strncpy(sz, ba.constData(), cchSzMax-1);
        sz[cchSzMax-1] = chNull;
        if (!SwissTestStar(sz))
          sprintf(sz, "%s", szObjUnknown);
        rgpeName[row]->setText(sz);
      }
    });
  pouter->addWidget(pbuttons);
  QObject::connect(pbuttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(pbuttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  PrepareDialogQt(&dlg);
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
