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
#include <QtCore/QFile>
#include <QtGui/QPixmap>
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
// Astrolog's planet icon, the astrlog3.ico that every Windows dialog
// shows. Looked for next to the binary and in the working directory, the
// same two places the bundled fonts are.
static QPixmap PixAstrologIconQt()
{
  static QPixmap pix;
  static flag fTried = fFalse;

  if (!fTried) {
    fTried = fTrue;
    QStringList rgstr;
    rgstr << QCoreApplication::applicationDirPath() + "/astrlog3.ico"
          << QDir::currentPath() + "/astrlog3.ico";
    for (int i = 0; i < rgstr.size(); i++)
      if (QFile::exists(rgstr[i])) {
        pix = QPixmap(rgstr[i]);
        if (!pix.isNull() && pix.height() > 24)
          pix = pix.scaledToHeight(24, Qt::SmoothTransformation);
        break;
      }
  }
  return pix;
}

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
  // Windows shows its planet icon (astrlog3.ico, the "icon3" resource) in
  // every one of these dialogs, and in nearly all of them it sits just to
  // the left of the Cancel button -- dlgAspect has it at x 235 with Cancel
  // at 320, dlgObject at 200 with Cancel at 280, and so on. Rather than
  // place it by hand in twenty-odd dialogs, find each button box and slot
  // the icon in ahead of it.
  QPixmap pixIcon = PixAstrologIconQt();
  if (!pixIcon.isNull())
    for (QDialogButtonBox *pbb : pdlg->findChildren<QDialogButtonBox *>()) {
      QBoxLayout *pblOwner = NULL;
      int iItem = -1;

      // Find the box layout holding this button box, and where in it.
      QList<QBoxLayout *> rgpbl = pdlg->findChildren<QBoxLayout *>();
      QBoxLayout *pblTop = qobject_cast<QBoxLayout *>(pdlg->layout());
      if (pblTop != NULL)
        rgpbl.prepend(pblTop);
      for (QBoxLayout *pbl : rgpbl) {
        for (int i = 0; i < pbl->count(); i++)
          if (pbl->itemAt(i)->widget() == pbb) {
            pblOwner = pbl; iItem = i; break;
          }
        if (pblOwner != NULL)
          break;
      }
      if (pblOwner == NULL)
        continue;
      QLabel *plIcon = new QLabel();
      plIcon->setPixmap(pixIcon);
      if (pblOwner->direction() == QBoxLayout::LeftToRight ||
        pblOwner->direction() == QBoxLayout::RightToLeft)
        pblOwner->insertWidget(iItem, plIcon);
      else {
        // A vertical layout: put the icon and the buttons on one row, so
        // the icon ends up beside Cancel rather than above it.
        pblOwner->removeWidget(pbb);
        QHBoxLayout *prow = new QHBoxLayout();
        prow->addStretch(1);
        prow->addWidget(plIcon);
        prow->addSpacing(8);
        prow->addWidget(pbb);
        pblOwner->insertLayout(iItem, prow);
      }
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


/*
******************************************************************************
** Dialogs transcribed from astrolog.rc.
******************************************************************************
*/

// Windows measures dialogs in "dialog units": one horizontal unit is a
// quarter of the dialog font's average character width, one vertical unit
// an eighth of its line height. Placing controls at the resource's own
// coordinates through that conversion reproduces the original layout
// exactly, and scales with whatever font the desktop is set to.
//
// This exists because rebuilding these dialogs out of nested Qt layouts
// does not. Inferring "two columns here, a group box there" from the
// resource gets the structure right but not the density: every layout
// level contributes its own margins and spacing, and the result came out
// close to twice as tall and wide as the original. Transcribing the
// numbers avoids the guesswork entirely.

enum {
  ctlCheck, ctlLabel, ctlGroup, ctlButton, ctlIcon, ctlEdit, ctlCombo,
  ctlRadio
};

// Each control carries the symbol the resource gave it, split into a
// prefix and any trailing number: "deo07" arrives as szId "deo", nIdx 7.
// A dialog then asks for what it needs by name -- PwRcFindQt("IDOK") -- or
// by prefix and index -- PwRcFindIdxQt("deo", 7).
// How many font choices dlgGraphics offers (text, signs, houses, objects,
// aspects, nakshatras).
#define cFontEntry 6

typedef struct {
  int nType;
  CONST char *szText;
  CONST char *szId;
  int nIdx;
  int x, y, dx, dy;   // In dialog units, as the resource gives them.
} RCCTL;

#include "qtrcdlg.h"

// One built control, so a caller can find what it needs to wire up.
typedef struct {
  CONST RCCTL *pctl;
  QWidget *pw;
} RCBUILT;

static QWidget *PwRcFindQt(CONST QVector<RCBUILT> &, CONST char *);

static void RcWireOkCancelQt(QDialog *pdlg, CONST QVector<RCBUILT> &rgbuilt)
{
  QPushButton *ppbOK = (QPushButton *)PwRcFindQt(rgbuilt, "IDOK");
  QPushButton *ppbCancel = (QPushButton *)PwRcFindQt(rgbuilt, "IDCANCEL");

  if (ppbOK != NULL) {
    ppbOK->setDefault(fTrue);
    QObject::connect(ppbOK, &QPushButton::clicked, pdlg, &QDialog::accept);
  }
  if (ppbCancel != NULL)
    QObject::connect(ppbCancel, &QPushButton::clicked, pdlg,
      &QDialog::reject);
}


static CONST RCCTL *PctlBuiltQt(CONST QVector<RCBUILT> *prg, int i)
{
  return (*prg)[i].pctl;
}


// Lay a transcribed dialog out. Widgets are positioned absolutely rather
// than through a layout, which is the whole point -- the resource has
// already decided where everything goes.
static void RcBuildDialogQt(QDialog *pdlg, CONST RCCTL *rgctl, int cctl,
  int dxDlg, int dyDlg, QVector<RCBUILT> *prgbuilt)
{
  QFontMetrics fm(pdlg->font());
  int dxBase = fm.averageCharWidth(), dyBase = fm.height();
  int i, iPass;

  prgbuilt->clear();
  prgbuilt->resize(cctl);

  // Group boxes first: they are frames the other controls sit inside, and
  // sibling widgets paint in creation order, so building them later would
  // draw the frames over their own contents.
  for (iPass = 0; iPass < 2; iPass++)
    for (i = 0; i < cctl; i++) {
      CONST RCCTL *pctl = &rgctl[i];
      if ((pctl->nType == ctlGroup) != (iPass == 0))
        continue;
      QString str = QString(pctl->szText);
      QWidget *pw = NULL;

      switch (pctl->nType) {
      case ctlGroup:
        pw = new QGroupBox(str, pdlg);
        break;
      case ctlCheck:
        pw = new QCheckBox(str, pdlg);
        break;
      case ctlRadio:
        // Left ungrouped on purpose: Qt would otherwise make every radio
        // in the dialog mutually exclusive, where the resource has
        // several independent groups. Each dialog groups its own.
        pw = new QRadioButton(str, pdlg);
        ((QRadioButton *)pw)->setAutoExclusive(fFalse);
        break;
      case ctlLabel:
        // A static label renders "&" literally, so drop it -- the
        // resource only uses it on controls that take focus.
        pw = new QLabel(str.remove(QChar('&')), pdlg);
        break;
      case ctlButton:
        pw = new QPushButton(str, pdlg);
        break;
      case ctlEdit:
        pw = new QLineEdit(pdlg);
        break;
      case ctlCombo:
        pw = new QComboBox(pdlg);
        break;
      case ctlIcon: {
        QLabel *plIcon = new QLabel(pdlg);
        QPixmap pix = PixAstrologIconQt();
        // The resource sizes the icon too, so it scales with the rest.
        if (!pix.isNull())
          plIcon->setPixmap(pix.scaled(pctl->dx * dxBase / 4,
            pctl->dy * dyBase / 8, Qt::KeepAspectRatio,
            Qt::SmoothTransformation));
        plIcon->setAlignment(Qt::AlignCenter);
        pw = plIcon;
        break; }
      }
      if (pw == NULL)
        continue;
      (*prgbuilt)[i].pctl = pctl;
      (*prgbuilt)[i].pw = pw;
    }

  // Widen the base unit until every label fits the space the resource
  // allotted it, then lay the whole dialog out at that scale. Windows'
  // formula uses the font's average character width, but a given string
  // takes more of that average in one typeface than another -- this font
  // needs noticeably more than the MS Shell Dlg the resource was drawn
  // against, so at the nominal scale the longer labels clip. Growing the
  // individual controls instead would break the alignment the resource
  // spent its coordinates establishing; scaling the unit keeps every
  // proportion intact and simply renders the same dialog larger.
  for (i = 0; i < cctl; i++) {
    CONST RCCTL *pctl = PctlBuiltQt(prgbuilt, i);
    QWidget *pw = (*prgbuilt)[i].pw;
    if (pw == NULL || pctl == NULL || pctl->dx <= 0)
      continue;
    if (pctl->nType != ctlCheck && pctl->nType != ctlLabel &&
      pctl->nType != ctlButton && pctl->nType != ctlRadio)
      continue;
    int dxWant = pw->sizeHint().width();
    if (dxWant > 0)
      dxBase = Max(dxBase, (dxWant * 4 + pctl->dx - 1) / pctl->dx);
  }

  pdlg->setFixedSize(dxDlg * dxBase / 4, dyDlg * dyBase / 8);
  for (i = 0; i < cctl; i++) {
    CONST RCCTL *pctl = PctlBuiltQt(prgbuilt, i);
    QWidget *pw = (*prgbuilt)[i].pw;
    if (pw == NULL || pctl == NULL)
      continue;
    int dyCtl = pctl->dy * dyBase / 8;
    // A COMBOBOX's height in a resource is how far its list drops down
    // when opened, not how tall the closed control is -- taking it
    // literally gives a combo box a couple of hundred pixels high and
    // shoves everything below it off the dialog. Qt sizes its own.
    if (pctl->nType == ctlCombo)
      dyCtl = pw->sizeHint().height();
    pw->setGeometry(pctl->x * dxBase / 4, pctl->y * dyBase / 8,
      pctl->dx * dxBase / 4, dyCtl);
    if (pctl->nType == ctlIcon) {
      QPixmap pix = PixAstrologIconQt();
      if (!pix.isNull())
        ((QLabel *)pw)->setPixmap(pix.scaled(pctl->dx * dxBase / 4,
          pctl->dy * dyBase / 8, Qt::KeepAspectRatio,
          Qt::SmoothTransformation));
    }
  }
}

// Find a built control by the symbol the resource gave it.
static QWidget *PwRcFindQt(CONST QVector<RCBUILT> &rgbuilt, CONST char *szId)
{
  for (int i = 0; i < rgbuilt.size(); i++)
    if (rgbuilt[i].pw != NULL && strcmp(rgbuilt[i].pctl->szId, szId) == 0)
      return rgbuilt[i].pw;
  return NULL;
}

// Wire the OK and Cancel a transcribed dialog got from its resource.
static void RcWireOkCancelQt(QDialog *pdlg, CONST QVector<RCBUILT> &rgbuilt);

// Same, for the numbered runs of controls: PwRcFindIdxQt(rg, "deo", 7).
static QWidget *PwRcFindIdxQt(CONST QVector<RCBUILT> &rgbuilt,
  CONST char *szId, int nIdx)
{
  for (int i = 0; i < rgbuilt.size(); i++)
    if (rgbuilt[i].pw != NULL && rgbuilt[i].pctl->nIdx == nIdx &&
      strcmp(rgbuilt[i].pctl->szId, szId) == 0)
      return rgbuilt[i].pw;
  return NULL;
}


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


// The restriction dialogs, transcribed from dlgRestrict, dlgStar and
// dlgMoons: every checkbox, group box, button and the icon sit at the
// coordinates the resource gives them.
//
// Checkbox polarity follows Windows: the label is the object's name and a
// *checked* box means restricted, i.e. hidden. That reads backwards at
// first glance, but it is what a Windows user expects, and inverting it is
// the one difference that would silently produce the opposite chart.

// What one of a restriction dialog's quick buttons does to its range.
enum { resSet, resClear, resToggle, resCopy };

typedef struct {
  CONST char *szId;   // Resource symbol of the button.
  int nIdx;           // Its index, or -1 when the symbol carries none.
  int nAction, lo, hi;
  CONST byte *rgSource;
} RCRESBUT;

static void ShowRcRestrictQt(CONST char *szTitle, CONST RCCTL *rgctl,
  int cctl, int dxDlg, int dyDlg, int lo, int hi, byte *rgignore,
  CONST RCRESBUT *rgbut, int cbut)
{
  QDialog dlg(gi.qwind);
  QVector<RCBUILT> rgbuilt;
  QVector<QCheckBox *> rgpcb;
  int i;

  dlg.setWindowTitle(szTitle);
  RcBuildDialogQt(&dlg, rgctl, cctl, dxDlg, dyDlg, &rgbuilt);

  // The checkbox numbering is one based over the dialog's object range.
  rgpcb.resize(hi - lo + 1);
  for (i = 0; i < rgbuilt.size(); i++) {
    CONST RCCTL *pctl = rgbuilt[i].pctl;
    if (rgbuilt[i].pw == NULL || pctl == NULL || pctl->nType != ctlCheck)
      continue;
    int iObj = lo + pctl->nIdx - 1;
    if (pctl->nIdx < 1 || !FBetween(iObj, lo, hi))
      continue;
    QCheckBox *pcb = (QCheckBox *)rgbuilt[i].pw;
    pcb->setChecked(rgignore[iObj] != 0);
    rgpcb[iObj - lo] = pcb;
  }

  for (i = 0; i < cbut; i++) {
    CONST RCRESBUT *pbut = &rgbut[i];
    QPushButton *ppb = (QPushButton *)(pbut->nIdx >= 0 ?
      PwRcFindIdxQt(rgbuilt, pbut->szId, pbut->nIdx) :
      PwRcFindQt(rgbuilt, pbut->szId));
    if (ppb == NULL)
      continue;
    QObject::connect(ppb, &QPushButton::clicked, &dlg,
      [&rgpcb, pbut, lo, hi]() {
        int jlo = Max(pbut->lo, lo), jhi = Min(pbut->hi, hi);
        for (int j = jlo; j <= jhi; j++) {
          QCheckBox *pcb = rgpcb[j - lo];
          if (pcb == NULL)
            continue;
          switch (pbut->nAction) {
          case resSet:    pcb->setChecked(fTrue);              break;
          case resClear:  pcb->setChecked(fFalse);             break;
          case resToggle: pcb->setChecked(!pcb->isChecked());  break;
          case resCopy:   pcb->setChecked(pbut->rgSource[j] != 0); break;
          }
        }
      });
  }

  QPushButton *ppbOK = (QPushButton *)PwRcFindQt(rgbuilt, "IDOK");
  QPushButton *ppbCancel = (QPushButton *)PwRcFindQt(rgbuilt, "IDCANCEL");
  if (ppbOK != NULL) {
    ppbOK->setDefault(fTrue);
    QObject::connect(ppbOK, &QPushButton::clicked, &dlg, &QDialog::accept);
  }
  if (ppbCancel != NULL)
    QObject::connect(ppbCancel, &QPushButton::clicked, &dlg,
      &QDialog::reject);

  if (dlg.exec() != QDialog::Accepted)
    return;
  for (i = lo; i <= hi; i++)
    if (rgpcb[i - lo] != NULL)
      rgignore[i] = rgpcb[i - lo]->isChecked();
  AdjustRestrictions();
  // Windows re-derives the Setting menu's category checkmarks here.
  SyncRestrictMenuQt();
  RecastAndRedrawQt();
}


void ShowRestrictDialogQt()
{
  CONST RCRESBUT rgbut[] = {
    {"dbRe_R",    0, resSet,    0,       dwarfHi, NULL},
    {"dbRe_R",    1, resClear,  0,       dwarfHi, NULL},
    {"dbRe_R",   -1, resToggle, oMain+1, oCore,   NULL},
    {"dbRe_RC",  -1, resToggle, cuspLo,  cuspHi,  NULL},
    {"dbRe_Ru",  -1, resToggle, uranLo,  uranHi,  NULL},
    {"dbRe_Ry",  -1, resToggle, dwarfLo, dwarfHi, NULL},
    {"dbRT",     -1, resCopy,   0,       dwarfHi, ignore2},
    {"dbRe_YRi", -1, resCopy,   0,       dwarfHi, ignoreMem} };

  ShowRcRestrictQt("Object Restrictions", rgctlRestrict, cctlRestrict,
    dxRestrict, dyRestrict, 0, dwarfHi, ignore,
    rgbut, (int)(sizeof(rgbut)/sizeof(RCRESBUT)));
}


void ShowStarRestrictDialogQt()
{
  CONST RCRESBUT rgbut[] = {
    {"dbSt_RU", 0, resSet,   starLo, starHi, NULL},
    {"dbSt_RU", 1, resClear, starLo, starHi, NULL} };

  ShowRcRestrictQt("Fixed Star Restrictions", rgctlStar, cctlStar,
    dxStar, dyStar, starLo, starHi, ignore,
    rgbut, (int)(sizeof(rgbut)/sizeof(RCRESBUT)));
}

// Windows drives this from the same dlgRestrict, only editing ignore2 and
// offering a copy from the standard set rather than the transit one.
void ShowTransitRestrictDialogQt()
{
  CONST RCRESBUT rgbut[] = {
    {"dbRe_R",    0, resSet,    0,       dwarfHi, NULL},
    {"dbRe_R",    1, resClear,  0,       dwarfHi, NULL},
    {"dbRe_R",   -1, resToggle, oMain+1, oCore,   NULL},
    {"dbRe_RC",  -1, resToggle, cuspLo,  cuspHi,  NULL},
    {"dbRe_Ru",  -1, resToggle, uranLo,  uranHi,  NULL},
    {"dbRe_Ry",  -1, resToggle, dwarfLo, dwarfHi, NULL},
    {"dbRT",     -1, resCopy,   0,       dwarfHi, ignore},
    {"dbRe_YRi", -1, resCopy,   0,       dwarfHi, ignoreMem} };

  ShowRcRestrictQt("Transit Object Restrictions", rgctlRestrict,
    cctlRestrict, dxRestrict, dyRestrict, 0, dwarfHi, ignore2,
    rgbut, (int)(sizeof(rgbut)/sizeof(RCRESBUT)));
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

// A checkbox in a transcribed dialog and the flag it edits. Extracted from
// the SetCheck/GetCheck pairs in Windows' own dialog handlers rather than
// worked out by hand, so the two builds cannot drift apart over which box
// controls what.
typedef struct {
  CONST char *szId;
  int nIdx;
  flag *pf;
  flag fInvert;   // The box shows the opposite of the flag.
} RCFLAG;

static void RcLoadFlagsQt(CONST QVector<RCBUILT> &rgbuilt,
  CONST RCFLAG *rgflag, int cflag)
{
  for (int i = 0; i < cflag; i++) {
    QCheckBox *pcb = (QCheckBox *)(rgflag[i].nIdx >= 0 ?
      PwRcFindIdxQt(rgbuilt, rgflag[i].szId, rgflag[i].nIdx) :
      PwRcFindQt(rgbuilt, rgflag[i].szId));
    if (pcb != NULL)
      pcb->setChecked((*rgflag[i].pf != 0) != (rgflag[i].fInvert != 0));
  }
}

static void RcStoreFlagsQt(CONST QVector<RCBUILT> &rgbuilt,
  CONST RCFLAG *rgflag, int cflag)
{
  for (int i = 0; i < cflag; i++) {
    QCheckBox *pcb = (QCheckBox *)(rgflag[i].nIdx >= 0 ?
      PwRcFindIdxQt(rgbuilt, rgflag[i].szId, rgflag[i].nIdx) :
      PwRcFindQt(rgbuilt, rgflag[i].szId));
    if (pcb != NULL)
      *rgflag[i].pf = (pcb->isChecked() != (rgflag[i].fInvert != 0));
  }
}

#define CRcFlag(rg) (int)(sizeof(rg) / sizeof(RCFLAG))


// Windows' EnsureN(): complain about one out of range field and leave the
// dialog's values alone.
static void ErrorEnsureQt(QWidget *pw, int n, CONST char *szField)
{
  QMessageBox::warning(pw, szAppName,
    QString("The value %1 is not valid for the %2 field.")
    .arg(n).arg(szField));
}


// One of the resource's radio groups: a run of drNN controls that are
// mutually exclusive and together pick a value. They are built ungrouped
// (see RcBuildDialogQt), so exclusivity is enforced here.
static void RcLoadRadioSzQt(CONST QVector<RCBUILT> &rgbuilt,
  CONST char *szId, int nFirst, int cRadio, int nValue)
{
  QButtonGroup *pgroup = NULL;

  for (int i = 0; i < cRadio; i++) {
    QRadioButton *prb =
      (QRadioButton *)PwRcFindIdxQt(rgbuilt, szId, nFirst + i);
    if (prb == NULL)
      continue;
    if (pgroup == NULL)
      pgroup = new QButtonGroup(prb->parentWidget());
    prb->setAutoExclusive(fTrue);
    pgroup->addButton(prb, i);
    prb->setChecked(i == nValue);
  }
}

static int NRcStoreRadioSzQt(CONST QVector<RCBUILT> &rgbuilt,
  CONST char *szId, int nFirst, int cRadio, int nDefault)
{
  for (int i = 0; i < cRadio; i++) {
    QRadioButton *prb =
      (QRadioButton *)PwRcFindIdxQt(rgbuilt, szId, nFirst + i);
    if (prb != NULL && prb->isChecked())
      return i;
  }
  return nDefault;
}

// Most groups are the plain "dr" run; Graphics Settings also has "drg".
static void RcLoadRadioQt(CONST QVector<RCBUILT> &rgbuilt, int nFirst,
  int cRadio, int nValue)
{
  RcLoadRadioSzQt(rgbuilt, "dr", nFirst, cRadio, nValue);
}

static int NRcStoreRadioQt(CONST QVector<RCBUILT> &rgbuilt, int nFirst,
  int cRadio, int nDefault)
{
  return NRcStoreRadioSzQt(rgbuilt, "dr", nFirst, cRadio, nDefault);
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

// Graphics Settings, transcribed from dlgGraphics. The largest of them:
// six font lists, six glyph variant radio groups on their own "drg" run,
// and two checkboxes that are bits of one value rather than flags.

void ShowGraphicsSettingsDialogQt()
{
  CONST RCFLAG rgflag[] = {
    {"dxGr_XQ", -1, &gs.fKeepSquare, fFalse},
    {"dxGr_XQ",  0, &gs.fAutoScale,  fFalse},
    {"dxGr_XP",  0, &gs.fSouth,      fFalse},
    {"dxGr_XW",  0, &gs.fMollweide,  fFalse},
    {"dxGr_XN", -1, &gs.fAnimMap,    fFalse} };
  // Each glyph choice is a run on the "drg" numbering: Capricorn and
  // Uranus have two variants, Pluto three, then Lilith, Vertex and Eris
  // two each. The stored value is one based.
  struct { int nFirst, cRadio; int *pn; } rgglyph[] = {
    { 1, 2, &gs.nGlyphCap}, { 3, 2, &gs.nGlyphUra},
    { 5, 3, &gs.nGlyphPlu}, { 8, 2, &gs.nGlyphLil},
    {10, 2, &gs.nGlyphVer}, {12, 2, &gs.nGlyphEri} };
  CONST int cglyph = (int)(sizeof(rgglyph)/sizeof(rgglyph[0]));
  int *rgpnFont[cFontEntry] = {&gs.nFontTxt, &gs.nFontSig, &gs.nFontHou,
    &gs.nFontObj, &gs.nFontAsp, &gs.nFontNak};
  QDialog dlg(gi.qwind);
  QVector<RCBUILT> rgbuilt;
  QComboBox *rgpcbFont[cFontEntry];
  char sz[cchSzMax];
  int i, j;

  dlg.setWindowTitle("Graphics Settings");
  RcBuildDialogQt(&dlg, rgctlGraphics, cctlGraphics, dxGraphics, dyGraphics,
    &rgbuilt);
  RcLoadFlagsQt(rgbuilt, rgflag, CRcFlag(rgflag));

  // Windows' "do not automatically update screen".
  QCheckBox *pcbNoUpd = (QCheckBox *)PwRcFindQt(rgbuilt, "dxGr_Wn");
  if (pcbNoUpd != NULL)
    pcbNoUpd->setChecked(FNoUpdateQt());

  // gs.nAllStar is a pair of bits, not two flags.
  QCheckBox *pcbStar1 = (QCheckBox *)PwRcFindIdxQt(rgbuilt, "dxGr_XU", 1);
  QCheckBox *pcbStar2 = (QCheckBox *)PwRcFindIdxQt(rgbuilt, "dxGr_XU", 2);
  if (pcbStar1 != NULL)
    pcbStar1->setChecked(FOdd(gs.nAllStar));
  if (pcbStar2 != NULL)
    pcbStar2->setChecked((gs.nAllStar & 2) != 0);

  QLineEdit *peX = (QLineEdit *)PwRcFindQt(rgbuilt, "deGr_Xw_x");
  QLineEdit *peY = (QLineEdit *)PwRcFindQt(rgbuilt, "deGr_Xw_y");
  QLineEdit *peGrid = (QLineEdit *)PwRcFindQt(rgbuilt, "deGr_YXg");
  QLineEdit *peSpace = (QLineEdit *)PwRcFindQt(rgbuilt, "deGr_YXj");
  QLineEdit *peTrack = (QLineEdit *)PwRcFindQt(rgbuilt, "deGr_XZ");
  QLineEdit *peAU = (QLineEdit *)PwRcFindQt(rgbuilt, "deGr_YXS");
  QLineEdit *peRot = (QLineEdit *)PwRcFindQt(rgbuilt, "deGr_XW");
  QLineEdit *peTilt = (QLineEdit *)PwRcFindQt(rgbuilt, "deGr_XG");
  QLineEdit *peDelay = (QLineEdit *)PwRcFindQt(rgbuilt, "deGr_WN");
  QLineEdit *peLeft = (QLineEdit *)PwRcFindIdxQt(rgbuilt, "deGr_X", 1);
  QLineEdit *peDeca = (QLineEdit *)PwRcFindQt(rgbuilt, "deGr_YXv");
  if (peX != NULL)     peX->setText(QString::number(gs.xWin));
  if (peY != NULL)     peY->setText(QString::number(gs.yWin));
  if (peGrid != NULL)  peGrid->setText(QString::number(gs.nGridCell));
  if (peSpace != NULL) peSpace->setText(QString::number(gs.cspace));
  if (peTrack != NULL) peTrack->setText(gs.objTrack >= 0 ?
    szObjName[gs.objTrack] : "None");
  if (peAU != NULL)    peAU->setText(SzFormatRQt(gs.rspace, -6));
  if (peRot != NULL)   peRot->setText(SzFormatRQt(gs.rRot, -3));
  if (peTilt != NULL)  peTilt->setText(SzFormatRQt(gs.rTilt, -3));
  if (peDelay != NULL) peDelay->setText(QString::number(NAnimDelayQt()));
  if (peLeft != NULL)  peLeft->setText(szObjName[gs.objLeft == 0 ? oSun :
    NAbs(gs.objLeft)-1]);
  if (peDeca != NULL)  peDeca->setText(QString::number(gs.nDecaSize));

  QComboBox *pcbScale = (QComboBox *)PwRcFindQt(rgbuilt, "dcGr_Xs");
  QComboBox *pcbScaleT = (QComboBox *)PwRcFindQt(rgbuilt, "dcGr_XSS");
  QComboBox *pcbCorner = (QComboBox *)PwRcFindQt(rgbuilt, "dcGr_YXv");
  QComboBox *pcbFill = (QComboBox *)PwRcFindQt(rgbuilt, "dcGr_Xv");
  QComboBox *pcbCity = (QComboBox *)PwRcFindQt(rgbuilt, "dcGr_XL");
  if (pcbScale != NULL) {
    pcbScale->setEditable(fTrue);
    for (i = 100; i <= MAXSCALE; i += 100)
      pcbScale->addItem(QString::number(i));
    pcbScale->setEditText(QString::number(gs.nScale));
  }
  if (pcbScaleT != NULL) {
    pcbScaleT->setEditable(fTrue);
    for (i = 100; i <= MAXSCALE; i += 50)
      pcbScaleT->addItem(QString::number(i));
    pcbScaleT->setEditText(QString::number(gs.nScaleText));
  }
  if (pcbCorner != NULL) {
    pcbCorner->setEditable(fTrue);
    // Windows lists these in its own order, not array order.
    for (i = 0; i < 7; i++)
      pcbCorner->addItem(rgszWheelCornerQt[rgiWheelCornerOrderQt[i]]);
    pcbCorner->setEditText(rgszWheelCornerQt[gs.nDecaType]);
  }
  if (pcbFill != NULL) {
    pcbFill->setEditable(fTrue);
    for (i = 0; i < 8; i++)
      pcbFill->addItem(rgszDecaFillQt[i]);
    pcbFill->setEditText(rgszDecaFillQt[gs.nDecaFill]);
  }
  if (pcbCity != NULL) {
    pcbCity->setEditable(fTrue);
    for (i = 0; i < 6; i++)
      pcbCity->addItem(rgszCityColorQt[i]);
    pcbCity->setEditText(rgszCityColorQt[gs.fLabelAsp ? gs.nLabelCity : 0]);
  }
  for (i = 0; i < cFontEntry; i++) {
    rgpcbFont[i] = (QComboBox *)PwRcFindIdxQt(rgbuilt, "dcGr_Xf", i);
    if (rgpcbFont[i] == NULL)
      continue;
    rgpcbFont[i]->setEditable(fTrue);
    for (j = 0; j < cFont; j++)
      rgpcbFont[i]->addItem(rgszFontDispQt[j]);
    rgpcbFont[i]->setEditText(rgszFontDispQt[*rgpnFont[i]]);
  }

  RcLoadRadioQt(rgbuilt, 1, 3,
    gs.objLeft > 0 ? 1 : (gs.objLeft < 0 ? 2 : 0));
  for (i = 0; i < cglyph; i++)
    RcLoadRadioSzQt(rgbuilt, "drg", rgglyph[i].nFirst, rgglyph[i].cRadio,
      *rgglyph[i].pn - 1);

  RcWireOkCancelQt(&dlg, rgbuilt);
  PrepareDialogQt(&dlg);
  if (dlg.exec() != QDialog::Accepted)
    return;

  int nx = peX != NULL ? peX->text().toInt() : gs.xWin;
  int ny = peY != NULL ? peY->text().toInt() : gs.yWin;
  if (!FValidGraphX(nx)) { ErrorEnsureQt(&dlg, nx, "horizontal size"); return; }
  if (!FValidGraphY(ny)) { ErrorEnsureQt(&dlg, ny, "vertical size"); return; }

  RcStoreFlagsQt(rgbuilt, rgflag, CRcFlag(rgflag));
  if (pcbNoUpd != NULL)
    SetNoUpdateQt(pcbNoUpd->isChecked());
  gs.nAllStar = (pcbStar1 != NULL && pcbStar1->isChecked()) |
    ((pcbStar2 != NULL && pcbStar2->isChecked()) << 1);
  flag fResize = (gs.xWin != nx || gs.yWin != ny);
  gs.xWin = nx; gs.yWin = ny;
  if (peGrid != NULL)  gs.nGridCell = peGrid->text().toInt();
  if (peSpace != NULL) gs.cspace = peSpace->text().toInt();
  if (peAU != NULL)    gs.rspace = peAU->text().toDouble();
  if (peRot != NULL)   gs.rRot = peRot->text().toDouble();
  if (peTilt != NULL)  gs.rTilt = peTilt->text().toDouble();
  if (peDeca != NULL)  gs.nDecaSize = peDeca->text().toInt();
  if (peDelay != NULL) SetAnimDelayQt(peDelay->text().toInt());
  if (peTrack != NULL) {
    sprintf(sz, "%.*s", cchSzMax-1,
      peTrack->text().toLocal8Bit().constData());
    gs.objTrack = FMatchSz(sz, "None") ? -1 : NParseSz(sz, pmObject);
  }
  if (pcbScale != NULL)  gs.nScale = pcbScale->currentText().toInt();
  if (pcbScaleT != NULL) gs.nScaleText = pcbScaleT->currentText().toInt();
  if (pcbCorner != NULL) {
    sprintf(sz, "%.*s", cchSzMax-1,
      pcbCorner->currentText().toLocal8Bit().constData());
    for (i = 0; i < 7; i++)
      if (FMatchSz(sz, rgszWheelCornerQt[i]))
        gs.nDecaType = i;
  }
  if (pcbFill != NULL) {
    sprintf(sz, "%.*s", cchSzMax-1,
      pcbFill->currentText().toLocal8Bit().constData());
    for (i = 0; i < 8; i++)
      if (FMatchSz(sz, rgszDecaFillQt[i]))
        gs.nDecaFill = i;
  }
  if (pcbCity != NULL) {
    sprintf(sz, "%.*s", cchSzMax-1,
      pcbCity->currentText().toLocal8Bit().constData());
    for (i = 0; i < 6; i++)
      if (FMatchSz(sz, rgszCityColorQt[i])) {
        gs.fLabelAsp = (i > 0);
        if (i > 0)
          gs.nLabelCity = i;
      }
  }
  for (i = 0; i < cFontEntry; i++) {
    if (rgpcbFont[i] == NULL)
      continue;
    sprintf(sz, "%.*s", cchSzMax-1,
      rgpcbFont[i]->currentText().toLocal8Bit().constData());
    for (j = 0; j < cFont; j++)
      if (FMatchSz(sz, rgszFontDispQt[j]))
        *rgpnFont[i] = j;
  }
  i = NRcStoreRadioQt(rgbuilt, 1, 3, 0);
  if (peLeft != NULL) {
    sprintf(sz, "%.*s", cchSzMax-1,
      peLeft->text().toLocal8Bit().constData());
    j = NParseSz(sz, pmObject);
    gs.objLeft = (i == 0 ? 0 : (i == 1 ? j+1 : -j-1));
  }
  for (i = 0; i < cglyph; i++)
    *rgglyph[i].pn = NRcStoreRadioSzQt(rgbuilt, "drg", rgglyph[i].nFirst,
      rgglyph[i].cRadio, *rgglyph[i].pn - 1) + 1;
  if (fResize && gs.xWin > 0 && gs.yWin > 0)
    gi.qwind->resize(gs.xWin, gs.yWin);
  RecastAndRedrawQt();
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
  CONST char *rgszSortQt[5] =
    { "Date", "Longitude", "Latitude", "Name", "Location" };
  for (i = 0; i < 5; i++) {
    QRadioButton *prb = new QRadioButton(rgszSortQt[i]);
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
// Populate a color list that already exists, which is what the transcribed
// dialogs need -- the resource placed the combo box, this just fills it.
static void FillColorComboQt(QComboBox *pcb, KI ki, int nExtra)
{
  int i, iMax = cColor2 + (nExtra > 0)*(nExtra + 1);

  if (pcb == NULL)
    return;
  pcb->setEditable(true);
  // addItem() before setEditText(): see the Progressions dialog.
  for (i = 0; i < iMax; i++)
    pcb->addItem(szColor[i]);
  pcb->setEditText(SzColor(ki));
}

static QComboBox *NewColorComboQt(KI ki, int nExtra)
{
  QComboBox *pcb = new QComboBox();

  FillColorComboQt(pcb, ki, nExtra);
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

// Chart Settings, transcribed from dlgChart. Two things here don't follow
// the usual shape: the star and Arabic part sort orders store a character
// code rather than an index, and the decan list folds two settings into
// one -- whether decans are listed at all, and which kind.

void ShowChartSettingsDialogQt()
{
  // The character each star sort radio stands for, dr01 through dr07.
  static CONST char rgchStarSort[] = {0, 'z', 'l', 'n', 'b', 'd', 'v'};
  // And dr08 through dr11 for Arabic parts.
  static CONST char rgchArabicSort[] = {0, 'z', 'n', 'f'};
  CONST RCFLAG rgflag[] = {
    {"dxCh_v",  0, &us.fVelocity,      fFalse},
    {"dxCh_w",  0, &us.fWheelReverse,  fFalse},
    {"dxCh_g",  0, &us.fGridConfig,    fFalse},
    {"dxCh_gm",-1, &us.fGridMidpoint,  fFalse},
    {"dxCh_a",  0, &us.fAspSummary,    fFalse},
    {"dxCh_m",  0, &us.fMidSummary,    fFalse},
    {"dxCh_ma",-1, &us.fMidAspect,     fFalse},
    {"dxCh_Z",  0, &us.fPrimeVert,     fFalse},
    {"dxCh_l", -1, &us.fSectorApprox,  fFalse},
    {"dxCh_Ky",-1, &us.fCalendarYear,  fFalse},
    {"dxCh_j",  0, &us.fInfluenceSign, fFalse},
    {"dxCh_L",  0, &us.fLatitudeCross, fFalse},
    {"dxCh_P",  0, &us.fArabicFlip,    fFalse} };
  QDialog dlg(gi.qwind);
  QVector<RCBUILT> rgbuilt;
  char sz[cchSzMax];
  int i, nw, nl, nl2, np, nn, yb;

  dlg.setWindowTitle("Chart Settings");
  RcBuildDialogQt(&dlg, rgctlChart, cctlChart, dxChart, dyChart, &rgbuilt);
  RcLoadFlagsQt(rgbuilt, rgflag, CRcFlag(rgflag));

  // us.nEphemYears is a count, but the box only asks whether it's on.
  QCheckBox *pcbEphYears = (QCheckBox *)PwRcFindQt(rgbuilt, "dxCh_Ey");
  if (pcbEphYears != NULL)
    pcbEphYears->setChecked(us.nEphemYears != 0);

  QLineEdit *peWheel = (QLineEdit *)PwRcFindQt(rgbuilt, "deCh_w");
  QLineEdit *peStep = (QLineEdit *)PwRcFindQt(rgbuilt, "deCh_L");
  QLineEdit *peDist = (QLineEdit *)PwRcFindIdxQt(rgbuilt, "deCh_L", 2);
  QLineEdit *pePart = (QLineEdit *)PwRcFindQt(rgbuilt, "deCh_P");
  QLineEdit *peCity = (QLineEdit *)PwRcFindQt(rgbuilt, "deCh_Nl");
  QLineEdit *peBio = (QLineEdit *)PwRcFindQt(rgbuilt, "deCh_Yb");
  QLineEdit *peRatio = (QLineEdit *)PwRcFindIdxQt(rgbuilt, "deCh_rc", 0);
  if (peWheel != NULL) peWheel->setText(QString::number(us.nWheelRows));
  if (peStep != NULL)  peStep->setText(QString::number(us.nAstroGraphStep));
  if (peDist != NULL)  peDist->setText(QString::number(us.nAstroGraphDist));
  if (pePart != NULL)  pePart->setText(QString::number(us.nArabicParts));
  if (peCity != NULL)  peCity->setText(QString::number(us.nAtlasList));
  if (peBio != NULL)   peBio->setText(QString::number(us.nBioday));
  if (peRatio != NULL) peRatio->setText(SzFormatRQt(us.rRatio, 6));

  QComboBox *pcbSort = (QComboBox *)PwRcFindQt(rgbuilt, "dcCh_a");
  QComboBox *pcbDecan = (QComboBox *)PwRcFindIdxQt(rgbuilt, "dcCh_v", 3);
  if (pcbSort != NULL) {
    pcbSort->setEditable(fTrue);
    for (i = 0; i < asMax; i++)
      pcbSort->addItem(rgszSortQt[i]);
    pcbSort->setEditText(rgszSortQt[us.nAspectSort]);
  }
  if (pcbDecan != NULL) {
    pcbDecan->setEditable(fTrue);
    for (i = 0; i < ddMax; i++)
      pcbDecan->addItem(rgszDecan[i]);
    pcbDecan->setEditText(rgszDecan[us.fListDecan ? us.nDecanType : 0]);
  }

  for (i = 0; i < (int)sizeof(rgchStarSort); i++)
    if (rgchStarSort[i] == us.nStarSort)
      RcLoadRadioQt(rgbuilt, 1, (int)sizeof(rgchStarSort), i);
  for (i = 0; i < (int)sizeof(rgchArabicSort); i++)
    if (rgchArabicSort[i] == us.nArabicSort)
      RcLoadRadioQt(rgbuilt, 8, (int)sizeof(rgchArabicSort), i);

  RcWireOkCancelQt(&dlg, rgbuilt);
  PrepareDialogQt(&dlg);
  if (dlg.exec() != QDialog::Accepted)
    return;

  nw = peWheel != NULL ? peWheel->text().toInt() : us.nWheelRows;
  nl = peStep != NULL ? peStep->text().toInt() : us.nAstroGraphStep;
  nl2 = peDist != NULL ? peDist->text().toInt() : us.nAstroGraphDist;
  np = pePart != NULL ? pePart->text().toInt() : us.nArabicParts;
  nn = peCity != NULL ? peCity->text().toInt() : us.nAtlasList;
  yb = peBio != NULL ? peBio->text().toInt() : us.nBioday;
  if (!FValidWheel(nw))       { ErrorEnsureQt(&dlg, nw, "wheel row"); return; }
  if (!FValidAstrograph(nl))  { ErrorEnsureQt(&dlg, nl, "astrocartography step"); return; }
  if (nl2 < 0)                { ErrorEnsureQt(&dlg, nl2, "latitude crossing count"); return; }
  if (!FValidPart(np))        { ErrorEnsureQt(&dlg, np, "Arabic part"); return; }
  if (nn < 0)                 { ErrorEnsureQt(&dlg, nn, "nearest city count"); return; }
  if (!FValidBioday(yb))      { ErrorEnsureQt(&dlg, yb, "Biorhythm days"); return; }

  RcStoreFlagsQt(rgbuilt, rgflag, CRcFlag(rgflag));
  us.nWheelRows = nw;
  us.nAstroGraphStep = nl;
  us.nAstroGraphDist = nl2;
  us.nArabicParts = np;
  us.nAtlasList = nn;
  us.nBioday = yb;
  if (pcbEphYears != NULL)
    us.nEphemYears = pcbEphYears->isChecked();
  us.nStarSort =
    rgchStarSort[NRcStoreRadioQt(rgbuilt, 1, (int)sizeof(rgchStarSort), 0)];
  us.nArabicSort =
    rgchArabicSort[NRcStoreRadioQt(rgbuilt, 8,
    (int)sizeof(rgchArabicSort), 0)];
  if (pcbSort != NULL) {
    sprintf(sz, "%.*s", cchSzMax-1,
      pcbSort->currentText().toLocal8Bit().constData());
    for (i = 1; i < asMax; i++)
      if (FMatchSz(sz, rgszSortQt[i]))
        us.nAspectSort = i;
  }
  if (peRatio != NULL)
    us.rRatio = peRatio->text().toDouble();
  if (pcbDecan != NULL) {
    sprintf(sz, "%.*s", cchSzMax-1,
      pcbDecan->currentText().toLocal8Bit().constData());
    for (i = 0; i < ddMax; i++)
      if (FMatchSz(sz, rgszDecan[i]))
        break;
    if (i >= ddMax)
      i = 0;
    us.fListDecan = (i > ddNone);
    if (i > ddNone)
      us.nDecanType = i;
  }
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

// Aspect Settings, transcribed from dlgAspect. The checkbox is the
// aspect's name and means *restricted*, as on Windows: checked hides it.

void ShowAspectDialogQt()
{
  QDialog dlg(gi.qwind);
  QVector<RCBUILT> rgbuilt;
  QVector<QCheckBox *> rgpcbRes;
  QVector<QLineEdit *> rgpeOrb, rgpeAngle, rgpeInf;
  QVector<QComboBox *> rgpcbColor;
  int i;

  dlg.setWindowTitle("Aspect Settings");
  RcBuildDialogQt(&dlg, rgctlAspect, cctlAspect, dxAspect, dyAspect,
    &rgbuilt);
  rgpcbRes.resize(cAspect+1); rgpeOrb.resize(cAspect+1);
  rgpeAngle.resize(cAspect+1); rgpeInf.resize(cAspect+1);
  rgpcbColor.resize(cAspect+1);
  for (i = 1; i <= cAspect; i++) {
    rgpcbRes[i] = (QCheckBox *)PwRcFindIdxQt(rgbuilt, "dxa", i);
    rgpeOrb[i] = (QLineEdit *)PwRcFindIdxQt(rgbuilt, "deo", i);
    rgpeAngle[i] = (QLineEdit *)PwRcFindIdxQt(rgbuilt, "dea", i);
    rgpeInf[i] = (QLineEdit *)PwRcFindIdxQt(rgbuilt, "dei", i);
    rgpcbColor[i] = (QComboBox *)PwRcFindIdxQt(rgbuilt, "dck", i);
    if (rgpcbRes[i] != NULL)
      rgpcbRes[i]->setChecked(ignorea[i] != 0);
    if (rgpeOrb[i] != NULL)
      rgpeOrb[i]->setText(SzFormatRQt(rAspOrb[i], -6));
    if (rgpeAngle[i] != NULL)
      rgpeAngle[i]->setText(SzFormatRQt(rAspAngle[i], -6));
    if (rgpeInf[i] != NULL)
      rgpeInf[i]->setText(SzFormatRQt(rAspInf[i], 2));
    FillColorComboQt(rgpcbColor[i], kAspA[i], 0);
  }

  // Restrict All, Unrestrict All, and Toggle Majors, which covers the
  // first five (wdialog.cpp:1380).
  struct { int nIdx, nAction, hi; } rgact[] =
    {{0, resSet, cAspect}, {1, resClear, cAspect}, {-1, resToggle, 5}};
  for (i = 0; i < 3; i++) {
    QPushButton *ppb = (QPushButton *)(rgact[i].nIdx >= 0 ?
      PwRcFindIdxQt(rgbuilt, "dbAs_RA", rgact[i].nIdx) :
      PwRcFindQt(rgbuilt, "dbAs_RA"));
    if (ppb == NULL)
      continue;
    int nAction = rgact[i].nAction, hi = rgact[i].hi;
    QObject::connect(ppb, &QPushButton::clicked, &dlg,
      [&rgpcbRes, nAction, hi]() {
        for (int j = 1; j <= hi; j++) {
          if (rgpcbRes[j] == NULL)
            continue;
          switch (nAction) {
          case resSet:    rgpcbRes[j]->setChecked(fTrue);  break;
          case resClear:  rgpcbRes[j]->setChecked(fFalse); break;
          case resToggle: rgpcbRes[j]->setChecked(!rgpcbRes[j]->isChecked());
            break;
          }
        }
      });
  }

  RcWireOkCancelQt(&dlg, rgbuilt);
  PrepareDialogQt(&dlg);
  if (dlg.exec() != QDialog::Accepted)
    return;

  for (i = 1; i <= cAspect; i++) {
    if (rgpcbRes[i] == NULL)
      continue;
    ignorea[i] = rgpcbRes[i]->isChecked();
    rAspOrb[i] = rgpeOrb[i]->text().toDouble();
    rAspAngle[i] = rgpeAngle[i]->text().toDouble();
    rAspInf[i] = rgpeInf[i]->text().toDouble();
    kAspA[i] = NColorFromComboQt(rgpcbColor[i]);
  }
  AdjustAspectCount();
  RecastAndRedrawQt();
}


// Colors, transcribed from dlgColor. The palette slots are dck00 through
// dck15, the elements dce0 through dce3, the seven rays dcr1 through dcr7,
// and the two loose lists along the bottom dca1 (scribble) and dca2
// (corners).
//
// The palette slots are named for the color they nominally represent, but
// each one maps through ikPalette[] to an entry in either kMainA[] or
// kRainbowA[] -- the dialog remaps which actual color each slot renders
// as, so the label and the current value need not agree.

void ShowColorDialogQt()
{
  QDialog dlg(gi.qwind);
  QVector<RCBUILT> rgbuilt;
  QComboBox *rgpcbPalette[cColor];
  QComboBox *rgpcbElem[cElem];
  QComboBox *rgpcbRay[cRay+1];
  int i, j;

  dlg.setWindowTitle("Colors");
  RcBuildDialogQt(&dlg, rgctlColor, cctlColor, dxColor, dyColor, &rgbuilt);
  for (i = 0; i < cColor; i++) {
    j = ikPalette[i];
    rgpcbPalette[i] = (QComboBox *)PwRcFindIdxQt(rgbuilt, "dck", i);
    FillColorComboQt(rgpcbPalette[i],
      j <= 0 ? kMainA[-j] : kRainbowA[j], 0);
  }
  for (i = 0; i < cElem; i++) {
    rgpcbElem[i] = (QComboBox *)PwRcFindIdxQt(rgbuilt, "dce", i);
    FillColorComboQt(rgpcbElem[i], kElemA[i], 0);
  }
  for (i = 1; i <= cRay; i++) {
    rgpcbRay[i] = (QComboBox *)PwRcFindIdxQt(rgbuilt, "dcr", i);
    FillColorComboQt(rgpcbRay[i], kRayA[i], 0);
  }
  QComboBox *pcbPen = (QComboBox *)PwRcFindIdxQt(rgbuilt, "dca", 1);
  QComboBox *pcbDeca = (QComboBox *)PwRcFindIdxQt(rgbuilt, "dca", 2);
  FillColorComboQt(pcbPen, gi.kiPen, 0);
  FillColorComboQt(pcbDeca, gs.kiDeca, 4);

  RcWireOkCancelQt(&dlg, rgbuilt);
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

// Objects, transcribed from dlgObject. The orb, add and influence fields
// are deoNN/deaNN/deiNN and the color list dckNN, all numbered from one
// over the object range, so they load and store by index.

void ShowObjectDialogQt()
{
  QDialog dlg(gi.qwind);
  QVector<RCBUILT> rgbuilt;
  QLineEdit *rgpeOrb[oCore+1], *rgpeAdd[oCore+1], *rgpeInf[oCore+1];
  QComboBox *rgpcbColor[oCore+1];
  int i;

  dlg.setWindowTitle("Objects");
  RcBuildDialogQt(&dlg, rgctlObject, cctlObject, dxObject, dyObject,
    &rgbuilt);
  for (i = 0; i <= oCore; i++) {
    rgpeOrb[i] = (QLineEdit *)PwRcFindIdxQt(rgbuilt, "deo", i+1);
    rgpeAdd[i] = (QLineEdit *)PwRcFindIdxQt(rgbuilt, "dea", i+1);
    rgpeInf[i] = (QLineEdit *)PwRcFindIdxQt(rgbuilt, "dei", i+1);
    // The color lists are numbered from dck00, unlike the edit fields
    // which start at 01, so this index is the object number itself.
    rgpcbColor[i] = (QComboBox *)PwRcFindIdxQt(rgbuilt, "dck", i);
    if (rgpeOrb[i] != NULL)
      rgpeOrb[i]->setText(SzFormatRQt(rObjOrb[i], -2));
    if (rgpeAdd[i] != NULL)
      rgpeAdd[i]->setText(SzFormatRQt(rObjAdd[i], -1));
    if (rgpeInf[i] != NULL)
      rgpeInf[i]->setText(SzFormatRQt(rObjInf[i], -2));
    FillColorComboQt(rgpcbColor[i], kObjU[i], 1);
  }
  RcWireOkCancelQt(&dlg, rgbuilt);
  PrepareDialogQt(&dlg);
  if (dlg.exec() != QDialog::Accepted)
    return;

  for (i = 0; i <= oCore; i++) {
    if (rgpeOrb[i] == NULL)
      continue;
    rObjOrb[i] = rgpeOrb[i]->text().toDouble();
    rObjAdd[i] = rgpeAdd[i]->text().toDouble();
    rObjInf[i] = rgpeInf[i]->text().toDouble();
    kObjU[i] = NColorFromComboQt(rgpcbColor[i]);
  }
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
  QVector<RCBUILT> rgbuilt;
  QVector<int> rgi;
  QVector<QLineEdit *> rgpeOrb, rgpeAdd, rgpeInf;
  QVector<QComboBox *> rgpcbColor;
  int i0, i, j = 0;

  dlg.setWindowTitle("More Object Settings");
  RcBuildDialogQt(&dlg, rgctlObject2, cctlObject2, dxObject2, dyObject2,
    &rgbuilt);
  for (i0 = oAsc; i0 <= dwarfHi+1; i0++) {
    i = (i0 <= dwarfHi ? i0 : starLo);
    rgi.append(i);
    QLineEdit *peOrb = (QLineEdit *)PwRcFindIdxQt(rgbuilt, "deo", j+1);
    QLineEdit *peAdd = (QLineEdit *)PwRcFindIdxQt(rgbuilt, "dea", j+1);
    QLineEdit *peInf = (QLineEdit *)PwRcFindIdxQt(rgbuilt, "dei", j+1);
    QComboBox *pcbColor = (QComboBox *)PwRcFindIdxQt(rgbuilt, "dck", j);
    if (peOrb != NULL)
      peOrb->setText(SzFormatRQt(rObjOrb[i], -2));
    if (peAdd != NULL)
      peAdd->setText(SzFormatRQt(rObjAdd[i], -1));
    if (peInf != NULL)
      peInf->setText(SzFormatRQt(rObjInf[i], -2));
    // Windows widens the color list by one on the collective stars row.
    FillColorComboQt(pcbColor, kObjU[i], 1 + (i == starLo));
    rgpeOrb.append(peOrb); rgpeAdd.append(peAdd);
    rgpeInf.append(peInf); rgpcbColor.append(pcbColor);
    j++;
  }
  RcWireOkCancelQt(&dlg, rgbuilt);
  PrepareDialogQt(&dlg);
  if (dlg.exec() != QDialog::Accepted)
    return;

  for (j = 0; j < rgi.size(); j++) {
    if (rgpeOrb[j] == NULL)
      continue;
    i = rgi[j];
    rObjOrb[i] = rgpeOrb[j]->text().toDouble();
    rObjAdd[i] = rgpeAdd[j]->text().toDouble();
    rObjInf[i] = rgpeInf[j]->text().toDouble();
    kObjU[i] = NColorFromComboQt(rgpcbColor[j]);
  }
  RecastAndRedrawQt();
}


// Calculation settings, equivalent to Windows' DlgCalc: ephemeris source,
// zodiac offset, house system, central planet, harmonic/dwad chart
// factors, and a grab bag of position calculation toggles.

// Calculation Settings, transcribed from dlgCalc. The checkbox mapping
// again comes from Windows' own DlgCalc handler.

void ShowCalcDialogQt()
{
  CONST RCFLAG rgflag[] = {
    {"dxSe_Yh", -1, &us.fBarycenter,  fFalse},
    {"dxSe_Yn", -1, &us.fTrueNode,    fFalse},
    {"dxSe_Yc",  0, &us.fHouseAngle,  fFalse},
    {"dxSe_Yf", -1, &us.fRefract,     fFalse},
    {"dxSe_ys", -1, &us.fSidereal2,   fFalse},
    {"dxSe_Yn",  0, &us.fNoNutation,  fFalse},
    {"dxSe_",   10, &us.fSolarWhole,  fFalse},
    {"dxSe_sr",  0, &us.fEquator2,    fFalse},
    {"dxSe_sr", -1, &us.fEquator,     fFalse},
    {"dxSe_yt", -1, &us.fTruePos,     fFalse},
    {"dxSe_yv", -1, &us.fTopoPos,     fFalse},
    {"dxSe_A",   3, &us.fAspect3D,    fFalse},
    {"dxSe_Ap", -1, &us.fAspectLat,   fFalse},
    {"dxSe_c",   3, &us.fHouse3D,     fFalse} };
  QDialog dlg(gi.qwind);
  QVector<RCBUILT> rgbuilt;
  char sz[cchSzMax];
  int i, nc, nh, n4, n1;
  real rs, rx;

  dlg.setWindowTitle("Calculation Settings");
  RcBuildDialogQt(&dlg, rgctlCalc, cctlCalc, dxCalc, dyCalc, &rgbuilt);
  RcLoadFlagsQt(rgbuilt, rgflag, CRcFlag(rgflag));

  // Ephemeris, ayanamsa and house system are editable combos, as on
  // Windows, so a value can be typed as well as picked.
  QComboBox *pcbEphem = (QComboBox *)PwRcFindQt(rgbuilt, "dcSe_b");
  QComboBox *pcbAyan = (QComboBox *)PwRcFindQt(rgbuilt, "dcSe_s");
  QComboBox *pcbHouse = (QComboBox *)PwRcFindQt(rgbuilt, "dcSe_c");
  if (pcbEphem != NULL) {
    pcbEphem->setEditable(fTrue);
    for (i = 0; i <= cmNone; i++)
      pcbEphem->addItem(szEphem[i]);
    pcbEphem->setEditText(szEphem[!us.fEphemFiles ?
      (us.fMatrixPla ? cmMatrix : cmNone) :
      (us.fPlacalcPla ? cmPlacalc : us.nSwissEph)]);
  }
  if (pcbAyan != NULL) {
    // The list offers the named ayanamsas with their offsets, and the
    // field itself takes a raw number, exactly as Windows fills it.
    pcbAyan->setEditable(fTrue);
    QString strCur = SzFormatRQt(us.rZodiacOffset, 6);
    for (i = 0; *rgZodiacOffset[i].sz; i++) {
      QString str = SzFormatRQt(rgZodiacOffset[i].r, 6) + " " +
        rgZodiacOffset[i].sz;
      pcbAyan->addItem(str);
      if (us.rZodiacOffset == rgZodiacOffset[i].r)
        strCur = str;
    }
    pcbAyan->setEditText(strCur);
  }
  if (pcbHouse != NULL) {
    pcbHouse->setEditable(fTrue);
    for (i = 0; i < cSystem; i++)
      pcbHouse->addItem(szSystem[i]);
    pcbHouse->setEditText(szSystem[us.nHouseSystem]);
  }

  QLineEdit *peCentral = (QLineEdit *)PwRcFindQt(rgbuilt, "deSe_h");
  QLineEdit *peHarmonic = (QLineEdit *)PwRcFindQt(rgbuilt, "deSe_x");
  QLineEdit *peDwad = (QLineEdit *)PwRcFindIdxQt(rgbuilt, "deSe_", 4);
  QLineEdit *peSolar = (QLineEdit *)PwRcFindIdxQt(rgbuilt, "deSe_", 1);
  if (peCentral != NULL)
    peCentral->setText(szObjName[us.objCenter]);
  if (peHarmonic != NULL)
    peHarmonic->setText(SzFormatRQt(us.rHarmonic, -6));
  if (peDwad != NULL)
    peDwad->setText(QString::number(us.nDwad));
  if (peSolar != NULL)
    peSolar->setText(szObjName[us.objOnAsc == 0 ? oSun :
      NAbs(us.objOnAsc)-1]);

  // dr01..dr03 pick what sits on the Ascendant; dr04..dr06 the 3D house
  // frame of reference.
  RcLoadRadioQt(rgbuilt, 1, 3,
    us.objOnAsc == 0 ? 0 : (us.objOnAsc > 0 ? 1 : 2));
  RcLoadRadioQt(rgbuilt, 4, 3, us.nHouse3D - 1);

  RcWireOkCancelQt(&dlg, rgbuilt);
  PrepareDialogQt(&dlg);
  if (dlg.exec() != QDialog::Accepted)
    return;

  rs = us.rZodiacOffset; nc = us.nHouseSystem; nh = us.objCenter;
  rx = us.rHarmonic; n4 = us.nDwad; n1 = oSun;
  if (pcbAyan != NULL)
    rs = RFromSz(pcbAyan->currentText().toLocal8Bit().constData());
  if (pcbHouse != NULL) {
    sprintf(sz, "%.*s", cchSzMax-1,
      pcbHouse->currentText().toLocal8Bit().constData());
    nc = NParseSz(sz, pmSystem);
  }
  if (peCentral != NULL) {
    sprintf(sz, "%.*s", cchSzMax-1,
      peCentral->text().toLocal8Bit().constData());
    nh = NParseSz(sz, pmObject);
  }
  if (peHarmonic != NULL) {
    // A leading "D" means the field gives a divisor, not a multiplier.
    sprintf(sz, "%.*s", cchSzMax-1,
      peHarmonic->text().toLocal8Bit().constData());
    i = (ChCap(sz[0]) == 'D');
    rx = RFromSz(sz + i);
    if (i != 0 && rx != 0.0)
      rx = rDegMax / rx;
  }
  if (peDwad != NULL)
    n4 = peDwad->text().toInt();
  if (peSolar != NULL) {
    sprintf(sz, "%.*s", cchSzMax-1,
      peSolar->text().toLocal8Bit().constData());
    n1 = NParseSz(sz, pmObject);
  }
  if (!FValidOffset(rs))      { ErrorEnsureQt(&dlg, (int)rs, "zodiac offset"); return; }
  if (!FValidSystem(nc))      { ErrorEnsureQt(&dlg, nc, "house system"); return; }
  if (!FValidCenter(nh))      { ErrorEnsureQt(&dlg, nh, "central planet"); return; }
  if (!FValidHarmonic(rx))    { ErrorEnsureQt(&dlg, (int)rx, "harmonic factor"); return; }
  if (!FValidDwad(n4))        { ErrorEnsureQt(&dlg, n4, "dwad nesting"); return; }
  if (!FItem(n1))             { ErrorEnsureQt(&dlg, n1, "Solar chart planet"); return; }

  if (pcbEphem != NULL) {
    sprintf(sz, "%.*s", cchSzMax-1,
      pcbEphem->currentText().toLocal8Bit().constData());
    us.fEphemFiles = us.fPlacalcPla = us.fMatrixPla = fFalse;
    us.nSwissEph = 0;
#ifdef SWISS
    if (FMatchSz(sz, szEphem[cmSwiss]))        { us.fEphemFiles = fTrue; us.nSwissEph = 0; }
    else if (FMatchSz(sz, szEphem[cmMoshier])) { us.fEphemFiles = fTrue; us.nSwissEph = 1; }
    else if (FMatchSz(sz, szEphem[cmJPL]))     { us.fEphemFiles = fTrue; us.nSwissEph = 2; }
    else if (FMatchSz(sz, szEphem[cmJPLWeb]))  { us.fEphemFiles = fTrue; us.nSwissEph = 3; }
#endif
#ifdef PLACALC
    if (FMatchSz(sz, szEphem[cmPlacalc]))
      us.fEphemFiles = us.fPlacalcPla = fTrue;
#endif
#ifdef MATRIX
    if (FMatchSz(sz, szEphem[cmMatrix]))
      us.fMatrixPla = fTrue;
#endif
  }
  us.rZodiacOffset = rs;
  us.nHouseSystem = nc;
  SetCentric(nh);
  us.rHarmonic = rx;
  us.nDwad = n4;
  RcStoreFlagsQt(rgbuilt, rgflag, CRcFlag(rgflag));
  i = NRcStoreRadioQt(rgbuilt, 1, 3, 0);
  us.objOnAsc = (i == 0 ? 0 : (i == 1 ? n1+1 : -n1-1));
  us.nHouse3D = NRcStoreRadioQt(rgbuilt, 4, 3, us.nHouse3D - 1) + 1;
  SyncHouseSetMenuQt();
  SyncHelioMenuQt();
  RecastAndRedrawQt();
}


// Display settings, equivalent to Windows' DlgDisplay: date/time/number
// formatting, aspect count and requirements, eclipse display, and the
// angle/rulership restriction checkbox grids.

// Display Settings, transcribed from dlgDisplay. The checkbox to flag
// mapping is taken from Windows' own DlgDisplay handler.

void ShowDisplayDialogQt()
{
  CONST RCFLAG rgflag[] = {
    {"dxDi_Yd", -1, &us.fEuroDate,   fFalse},
    {"dxDi_Yt", -1, &us.fEuroTime,   fFalse},
    {"dxDi_Yz",  1, &us.fOffsetOnly, fFalse},
    {"dxDi_Yv", -1, &us.fEuroDist,   fFalse},
    {"dxDi_Yr", -1, &us.fRound,      fFalse},
    {"dxDi_b",   0, &us.fSeconds,    fFalse},
    {"dxDi_b",   1, &us.fSecond1K,   fFalse},
    {"dxDi_b",   2, &us.fSecondHide, fFalse},
    {"dxDi_YC", -1, &us.fSmartCusp,  fFalse},
    {"dxDi_AP", -1, &us.fParallel2,  fFalse},
    {"dxDi_gd", -1, &us.fDistance,   fFalse},
    {"dxDi_Y",   8, &us.fClip80,     fFalse},
    {"dxDi_I",   0, &us.fSabian,     fFalse},
    {"dxDi_Yu", -1, &us.fEclipse,    fFalse},
    // Windows' box reads "only at location", the flag "anywhere".
    {"dxDi_Yu",  0, &us.fEclipseAny, fTrue} };
  QDialog dlg(gi.qwind);
  QVector<RCBUILT> rgbuilt;
  char sz[cchSzMax];
  int na, nro, ni;
  real ryw;

  dlg.setWindowTitle("Display Settings");
  RcBuildDialogQt(&dlg, rgctlDisplay, cctlDisplay, dxDisplay, dyDisplay,
    &rgbuilt);
  RcLoadFlagsQt(rgbuilt, rgflag, CRcFlag(rgflag));
  // The two arrays of restriction boxes, dxDi_z0.. and dxDi_r0..
  for (int i = 0; i < 6; i++) {
    QCheckBox *pcb = (QCheckBox *)PwRcFindIdxQt(rgbuilt, "dxDi_z", i);
    if (pcb != NULL)
      pcb->setChecked(ignorez[i] != 0);
  }
  for (int i = 0; i < 5; i++) {
    QCheckBox *pcb = (QCheckBox *)PwRcFindIdxQt(rgbuilt, "dxDi_r", i);
    if (pcb != NULL)
      pcb->setChecked(ignore7[i] != 0);
  }
  RcLoadRadioQt(rgbuilt, 4, 4, us.nDegForm);
  RcLoadRadioQt(rgbuilt, 8, 4, us.nCharset);
  RcLoadRadioQt(rgbuilt, 12, 3, us.nAppSep);

  QLineEdit *peAsp = (QLineEdit *)PwRcFindQt(rgbuilt, "deDi_A");
  QLineEdit *peReq = (QLineEdit *)PwRcFindQt(rgbuilt, "deDi_RO");
  QLineEdit *peWid = (QLineEdit *)PwRcFindQt(rgbuilt, "deDi_I");
  QLineEdit *peSta = (QLineEdit *)PwRcFindQt(rgbuilt, "deDi_Yw");
  if (peAsp != NULL)
    peAsp->setText(QString::number(us.nAsp));
  if (peReq != NULL)
    peReq->setText(us.objRequire >= 0 ? szObjName[us.objRequire] : "None");
  if (peWid != NULL)
    peWid->setText(QString::number(us.nScreenWidth));
  if (peSta != NULL)
    peSta->setText(SzFormatRQt(us.rStation, -6));

  RcWireOkCancelQt(&dlg, rgbuilt);
  PrepareDialogQt(&dlg);
  if (dlg.exec() != QDialog::Accepted)
    return;

  // Validate before writing anything, as Windows does, so one bad field
  // can't leave the settings half applied.
  na = us.nAsp; nro = us.objRequire;
  ni = us.nScreenWidth; ryw = us.rStation;
  if (peAsp != NULL) {
    sprintf(sz, "%.*s", cchSzMax-1, peAsp->text().toLocal8Bit().constData());
    na = NParseSz(sz, pmAspect);
  }
  if (peReq != NULL) {
    sprintf(sz, "%.*s", cchSzMax-1, peReq->text().toLocal8Bit().constData());
    nro = NParseSz(sz, pmObject);
  }
  if (peWid != NULL)
    ni = peWid->text().toInt();
  if (peSta != NULL)
    ryw = peSta->text().toDouble();
  if (!FValidAspect(na)) {
    ErrorEnsureQt(&dlg, na, "aspect count");
    return;
  }
  if (!(FItem(nro) || nro == -1)) {
    ErrorEnsureQt(&dlg, nro, "required object");
    return;
  }
  if (!FValidScreen(ni)) {
    ErrorEnsureQt(&dlg, ni, "text columns");
    return;
  }
  if (ryw < 0.0) {
    ErrorEnsureQt(&dlg, (int)ryw, "stationary velocity");
    return;
  }

  RcStoreFlagsQt(rgbuilt, rgflag, CRcFlag(rgflag));
  for (int i = 0; i < 6; i++) {
    QCheckBox *pcb = (QCheckBox *)PwRcFindIdxQt(rgbuilt, "dxDi_z", i);
    if (pcb != NULL)
      ignorez[i] = pcb->isChecked();
  }
  for (int i = 0; i < 5; i++) {
    QCheckBox *pcb = (QCheckBox *)PwRcFindIdxQt(rgbuilt, "dxDi_r", i);
    if (pcb != NULL)
      ignore7[i] = pcb->isChecked();
  }
  us.nDegForm = NRcStoreRadioQt(rgbuilt, 4, 4, us.nDegForm);
  us.nCharset = NRcStoreRadioQt(rgbuilt, 8, 4, us.nCharset);
  us.nAppSep = NRcStoreRadioQt(rgbuilt, 12, 3, us.nAppSep);
  for (int i = na + 1; i <= cAspect; i++)
    ignorea[i] = fTrue;
  us.nAsp = na;
  us.objRequire = nro;
  us.nScreenWidth = ni;
  us.rStation = ryw;
  AdjustAspectCount();
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
  // The per planet toggles cover the moon ranges dlgMoons groups them by.
  CONST RCRESBUT rgbut[] = {
    {"dbMo_Rm",  0, resSet,    moonsLo,    cobHi,      NULL},
    {"dbMo_Rm",  1, resClear,  moonsLo,    cobHi,      NULL},
    {"dbMo_Mar",-1, resToggle, moonsLo+0,  moonsLo+1,  NULL},
    {"dbMo_Jup",-1, resToggle, moonsLo+2,  moonsLo+5,  NULL},
    {"dbMo_Sat",-1, resToggle, moonsLo+6,  moonsLo+13, NULL},
    {"dbMo_Ura",-1, resToggle, moonsLo+14, moonsLo+18, NULL},
    {"dbMo_Nep",-1, resToggle, moonsLo+19, moonsLo+21, NULL},
    {"dbMo_Plu",-1, resToggle, moonsLo+22, moonsLo+26, NULL},
    {"dbMo_COB",-1, resToggle, cobLo,      cobHi,      NULL} };

  ShowRcRestrictQt("Planetary Moon Restrictions", rgctlMoons, cctlMoons,
    dxMoons, dyMoons, moonsLo, cobHi, ignore,
    rgbut, (int)(sizeof(rgbut)/sizeof(RCRESBUT)));
}


// Moon object settings, equivalent to Windows' DlgObjectM: per moon/COB
// object orb/color settings (the same grid shape as
// ShowObject2DialogQt()), plus 3 moon chart display toggles.

void ShowMoonObjectDialogQt()
{
  QDialog dlg(gi.qwind);
  QVector<RCBUILT> rgbuilt;
  QVector<QLineEdit *> rgpeOrb, rgpeAdd, rgpeInf;
  QVector<QComboBox *> rgpcbColor;
  int i, j;

  dlg.setWindowTitle("Moon Object Settings");
  RcBuildDialogQt(&dlg, rgctlObjectM, cctlObjectM, dxObjectM, dyObjectM,
    &rgbuilt);
  for (i = moonsLo; i <= cobHi; i++) {
    j = i - moonsLo;
    QLineEdit *peOrb = (QLineEdit *)PwRcFindIdxQt(rgbuilt, "deo", j+1);
    QLineEdit *peAdd = (QLineEdit *)PwRcFindIdxQt(rgbuilt, "dea", j+1);
    QLineEdit *peInf = (QLineEdit *)PwRcFindIdxQt(rgbuilt, "dei", j+1);
    QComboBox *pcbColor = (QComboBox *)PwRcFindIdxQt(rgbuilt, "dck", j);
    if (peOrb != NULL)
      peOrb->setText(SzFormatRQt(rObjOrb[i], -2));
    if (peAdd != NULL)
      peAdd->setText(SzFormatRQt(rObjAdd[i], -1));
    if (peInf != NULL)
      peInf->setText(SzFormatRQt(rObjInf[i], -2));
    FillColorComboQt(pcbColor, kObjU[i], 3);
    rgpeOrb.append(peOrb); rgpeAdd.append(peAdd);
    rgpeInf.append(peInf); rgpcbColor.append(pcbColor);
  }
  // The three settings dlgObjectM carries below its grid.
  QCheckBox *pcbMove = (QCheckBox *)PwRcFindQt(rgbuilt, "dxMo_Ym");
  QCheckBox *pcbSep = (QCheckBox *)PwRcFindIdxQt(rgbuilt, "dxMo_", 80);
  QCheckBox *pcbWheel = (QCheckBox *)PwRcFindIdxQt(rgbuilt, "dxMo_X", 8);
  if (pcbMove != NULL)
    pcbMove->setChecked(us.fMoonMove != 0);
  if (pcbSep != NULL)
    pcbSep->setChecked(us.fMoonChartSep != 0);
  if (pcbWheel != NULL)
    pcbWheel->setChecked(gs.fMoonWheel != 0);

  RcWireOkCancelQt(&dlg, rgbuilt);
  PrepareDialogQt(&dlg);
  if (dlg.exec() != QDialog::Accepted)
    return;

  if (pcbMove != NULL)
    us.fMoonMove = pcbMove->isChecked();
  if (pcbSep != NULL)
    us.fMoonChartSep = pcbSep->isChecked();
  if (pcbWheel != NULL)
    gs.fMoonWheel = pcbWheel->isChecked();
  for (i = moonsLo; i <= cobHi; i++) {
    j = i - moonsLo;
    if (rgpeOrb[j] == NULL)
      continue;
    rObjOrb[i] = rgpeOrb[j]->text().toDouble();
    rObjAdd[i] = rgpeAdd[j]->text().toDouble();
    rObjInf[i] = rgpeInf[j]->text().toDouble();
    kObjU[i] = NColorFromComboQt(rgpcbColor[j]);
  }
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

// Object Customization, transcribed from dlgCustom: denNN is the display
// name and dedNN the definition, both numbered from one over the range.

void ShowCustomDialogQt()
{
  QDialog dlg(gi.qwind);
  QVector<RCBUILT> rgbuilt;
  QVector<QLineEdit *> rgpeName, rgpeDef;
  int i, j, k, l, pnt, flg;
  char sz[cchSzMax], *pch;

  dlg.setWindowTitle("Object Customization");
  RcBuildDialogQt(&dlg, rgctlCustom, cctlCustom, dxCustom, dyCustom,
    &rgbuilt);
  for (i = custLo; i <= custHi; i++) {
    j = i - custLo;
    QLineEdit *peName = (QLineEdit *)PwRcFindIdxQt(rgbuilt, "den", j+1);
    QLineEdit *peDef = (QLineEdit *)PwRcFindIdxQt(rgbuilt, "ded", j+1);
    rgpeName.append(peName);
    rgpeDef.append(peDef);
    if (peName != NULL)
      peName->setText(szObjDisp[i]);
    if (peDef == NULL)
      continue;

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
    peDef->setText(sz);
  }

  QPushButton *ppbLookup = (QPushButton *)PwRcFindQt(rgbuilt, "dbCu_l");
  if (ppbLookup != NULL)
    QObject::connect(ppbLookup, &QPushButton::clicked, &dlg,
      [&rgpeName, &rgpeDef]() { LookupCustomNamesQt(rgpeName, rgpeDef); });
  RcWireOkCancelQt(&dlg, rgbuilt);
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

// "Lookup Names" in the star dialog (dbCu_l in Windows' DlgCustomS): for
// every row whose display name is blank or still the unknown placeholder,
// check the definition against the ephemeris and name the row after what
// came back, or mark it unknown. The object dialog's equivalent is
// LookupCustomNamesQt() above; this one tests a star catalog name rather
// than an object definition.
static void LookupStarNamesQt(QVector<QLineEdit *> &rgpeName,
  QVector<QLineEdit *> &rgpeDef)
{
#ifdef SWISS
  char sz[cchSzMax];
  int i;

  for (i = 0; i < rgpeName.size() && i < rgpeDef.size(); i++) {
    if (rgpeName[i] == NULL || rgpeDef[i] == NULL)
      continue;
    QByteArray baName = rgpeName[i]->text().toLocal8Bit();
    if (baName.size() > 0 && !FEqSz(baName.constData(), szObjUnknown))
      continue;
    QByteArray baDef = rgpeDef[i]->text().toLocal8Bit();
    sprintf(sz, "%.*s", cchSzMax-1, baDef.constData());
    if (!SwissTestStar(sz))
      sprintf(sz, "%s", szObjUnknown);
    rgpeName[i]->setText(sz);
  }
#endif
}


// Fixed Star Customization, transcribed from dlgCustomS.

void ShowCustomStarDialogQt()
{
  QDialog dlg(gi.qwind);
  QVector<RCBUILT> rgbuilt;
  QVector<QLineEdit *> rgpeName, rgpeDef;
  int i, k;

  dlg.setWindowTitle("Fixed Star Customization");
  RcBuildDialogQt(&dlg, rgctlCustomS, cctlCustomS, dxCustomS, dyCustomS,
    &rgbuilt);
  for (i = starLo; i <= starHi; i++) {
    k = i - starLo + 1;
    QLineEdit *peName = (QLineEdit *)PwRcFindIdxQt(rgbuilt, "den", k);
    QLineEdit *peDef = (QLineEdit *)PwRcFindIdxQt(rgbuilt, "ded", k);
    rgpeName.append(peName);
    rgpeDef.append(peDef);
    if (peName != NULL)
      peName->setText(szObjDisp[i]);
    if (peDef != NULL)
      peDef->setText(FSzSet(szStarCustom[k]) ? szStarCustom[k] :
        (*szStarNameSwiss[k] ? szStarNameSwiss[k] : szObjName[i]));
  }

  QPushButton *ppbLookup = (QPushButton *)PwRcFindQt(rgbuilt, "dbCu_l");
  if (ppbLookup != NULL)
    QObject::connect(ppbLookup, &QPushButton::clicked, &dlg,
      [&rgpeName, &rgpeDef]() { LookupStarNamesQt(rgpeName, rgpeDef); });
  RcWireOkCancelQt(&dlg, rgbuilt);
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
