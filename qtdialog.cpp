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
#include <QtWidgets/QStyle>
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
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QAbstractSpinBox>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QPlainTextEdit>
#include <QtGui/QKeyEvent>
#include <QtWidgets/QAbstractItemView>
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

// Fill a combo the resource already placed, the counterpart of
// NewComboQt() for the transcribed dialogs.
static void FillComboQt(QComboBox *pcb, CONST QString &strCur,
  CONST QStringList &rgstr)
{
  if (pcb == NULL)
    return;
  pcb->setEditable(fTrue);
  // addItem() before setEditText(): see the Progressions dialog.
  for (int i = 0; i < rgstr.size(); i++)
    pcb->addItem(rgstr[i]);
  pcb->setEditText(strCur);
}

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

// The mnemonic letter of a label, or a null character if it has none.
// "&&" is a literal ampersand, not a marker, so it is skipped.

static QChar ChMnemonicQt(CONST QString &str)
{
  for (int i = 0; i < str.size() - 1; i++) {
    if (str[i] != QChar('&'))
      continue;
    if (str[i+1] == QChar('&')) {
      i++;
      continue;
    }
    return str[i+1].toLower();
  }
  return QChar();
}


// Windows dialogs activate a control from its mnemonic letter alone --
// "s" ticks "&Sun" -- while Qt wants Alt held down. Both builds take the
// mnemonics from the same "&" in astrolog.rc, so the only difference is
// how the keystroke is routed; on a grid of 52 restriction checkboxes it
// is the difference between the dialog being usable from the keyboard and
// not being usable at all.
//
// Installed on the dialog rather than on each control, so it sees the key
// after the focused widget has declined it. A field that takes typing
// therefore keeps its letters -- otherwise typing a chart name into Set
// Chart Info would tick boxes across the dialog.
//
// Duplicated mnemonics cycle, the way Windows cycles rather than always
// firing the first: dlgRestrict alone has several letters used twice.

// Does this control want the raw keystroke for itself? A text field takes
// letters, and a combo or list takes the arrow keys, so neither can have
// them stolen for navigation.

static flag FWidgetTakesKeysQt(QWidget *pw, flag fArrow)
{
  if (qobject_cast<QLineEdit *>(pw) != NULL ||
    qobject_cast<QAbstractSpinBox *>(pw) != NULL ||
    qobject_cast<QTextEdit *>(pw) != NULL ||
    qobject_cast<QPlainTextEdit *>(pw) != NULL)
    return fTrue;
  QComboBox *pcb = qobject_cast<QComboBox *>(pw);
  if (pcb != NULL)
    return fArrow || pcb->isEditable();
  return fArrow && qobject_cast<QAbstractItemView *>(pw) != NULL;
}


// The focusable control nearest to pwFrom in the given direction, by where
// the controls actually sit rather than by the order they were built in.
//
// Qt moves focus on an arrow key by walking the tab chain, so in Object
// Restrictions -- where the resource lists OK and Cancel before all 52
// checkboxes -- pressing Up on the OK button, which is where focus starts,
// wrapped to the end of the chain and landed on "Recall" at the far side
// of the dialog. Cancel is sitting directly above OK. Windows' dialogs
// walk their tab order too, so this is a divergence, and a deliberate one:
// the layout is a 2D grid of columns and the arrows should follow it.
//
// Scored so the control most nearly straight ahead wins: distance along
// the direction pressed, plus a heavy penalty for drifting sideways.

static QWidget *PwArrowTargetQt(QDialog *pdlg, QWidget *pwFrom, int key)
{
  QWidget *pwBest = NULL;
  long lBest = 0;

  if (pwFrom == NULL)
    return NULL;
  QPoint ptFrom = pwFrom->mapTo(pdlg, pwFrom->rect().center());
  for (QWidget *pw : pdlg->findChildren<QWidget *>()) {
    if (pw == pwFrom || pw->focusPolicy() == Qt::NoFocus ||
      !pw->isEnabled() || !pw->isVisibleTo(pdlg))
      continue;
    QPoint pt = pw->mapTo(pdlg, pw->rect().center());
    int dx = pt.x() - ptFrom.x(), dy = pt.y() - ptFrom.y();
    int nAhead, nSide;
    switch (key) {
    case Qt::Key_Up:    nAhead = -dy; nSide = dx; break;
    case Qt::Key_Down:  nAhead =  dy; nSide = dx; break;
    case Qt::Key_Left:  nAhead = -dx; nSide = dy; break;
    default:            nAhead =  dx; nSide = dy; break;
    }
    if (nAhead <= 0)
      continue;
    long l = (long)nAhead + 4L*(long)NAbs(nSide);
    if (pwBest == NULL || l < lBest) {
      pwBest = pw;
      lBest = l;
    }
  }
  return pwBest;
}


// Keyboard behaviour every dialog built from the resource shares: bare
// mnemonic letters, and arrow keys that follow the layout.

class MnemonicKeysQt : public QObject
{
public:
  bool eventFilter(QObject *pobj, QEvent *pev) override
  {
    QDialog *pdlg;
    QWidget *pwFocus;
    QChar ch;

    if (pev->type() != QEvent::KeyPress)
      return QObject::eventFilter(pobj, pev);
    QKeyEvent *pke = (QKeyEvent *)pev;
    if ((pke->modifiers() & ~Qt::KeypadModifier) != Qt::NoModifier)
      return QObject::eventFilter(pobj, pev);
    pdlg = qobject_cast<QDialog *>(pobj);
    if (pdlg == NULL)
      return QObject::eventFilter(pobj, pev);
    pwFocus = pdlg->focusWidget();

    int key = pke->key();
    if (key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_Left ||
      key == Qt::Key_Right) {
      if (FWidgetTakesKeysQt(pwFocus, fTrue))
        return QObject::eventFilter(pobj, pev);
      QWidget *pwTo = PwArrowTargetQt(pdlg, pwFocus, key);
      if (pwTo == NULL)
        return QObject::eventFilter(pobj, pev);
      pwTo->setFocus(Qt::TabFocusReason);
      return true;
    }

    QString str = pke->text().toLower();
    if (str.size() != 1 || !str[0].isLetterOrNumber())
      return QObject::eventFilter(pobj, pev);
    // Anything that accepts typed text keeps the keystroke.
    if (FWidgetTakesKeysQt(pwFocus, fFalse))
      return QObject::eventFilter(pobj, pev);

    // findChildren() walks in creation order, which is the order the
    // resource lists the controls in, which is the tab order.
    QList<QAbstractButton *> rgpb;
    for (QAbstractButton *pb : pdlg->findChildren<QAbstractButton *>()) {
      if (!pb->isEnabled() || !pb->isVisibleTo(pdlg))
        continue;
      ch = ChMnemonicQt(pb->text());
      if (!ch.isNull() && ch == str[0])
        rgpb.append(pb);
    }
    if (rgpb.isEmpty())
      return QObject::eventFilter(pobj, pev);
    int i = rgpb.indexOf(qobject_cast<QAbstractButton *>(pwFocus));
    QAbstractButton *pb = rgpb[(i + 1) % rgpb.size()];
    pb->setFocus(Qt::ShortcutFocusReason);
    pb->click();
    return true;
  }
};


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


// Put initial focus where Windows puts it. Seven of its dialogs call
// SetFocus() from WM_INITDIALOG (the list is in wdialog.cpp: DlgCommand,
// DlgInfo, DlgDefault, DlgTransit, DlgProgress, DlgList, DlgGraphics)
// rather than let the dialog manager leave it on the first tab stop,
// which in every one of them is the OK button. Qt's default is the same
// as Win32's, so it needs the same override -- without it the Enter
// Command Line box opens with OK focused and you have to Tab into the
// field before typing, where on Windows you just type.
//
// Note Windows does not select the existing text when it does this (its
// handlers return fFalse, so no EM_SETSEL follows), and neither does
// this: the caret lands at the head of the field, which is where
// PrepareDialogQt() has already put it.
static void FocusDialogQt(QWidget *pw)
{
  if (pw != NULL)
    pw->setFocus();
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
  ctlRadio, ctlList
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

// Find a built control by the symbol the resource gave it -- the whole
// symbol, carrying no index.
//
// rc2qt.py splits a trailing run of digits off a resource symbol into
// nIdx, so "dbRe_R0" arrives here as szId "dbRe_R" with nIdx 0, and only a
// symbol that ended in no digits at all gets nIdx -1. Matching on szId
// alone therefore returned whichever of "dbRe_R0", "dbRe_R1" and "dbRe_R"
// the generated table listed first -- a different control the moment the
// resource is reordered, and the wrong one in three dialogs already:
// "Toggle Minors" and "Toggle &Majors" were dead while their Restrict All
// buttons silently ran the toggle as well, and "E&quatorial Longitudes"
// loaded and stored through the "&Equatorial Latitudes" box beside it.
//
// Matching nIdx too makes a bare lookup mean what it says. Callers that
// want one of the indexed controls ask for it by index instead, with
// PwRcFindIdxQt(). See work log item 52.
static QWidget *PwRcFindQt(CONST QVector<RCBUILT> &rgbuilt, CONST char *szId)
{
  for (int i = 0; i < rgbuilt.size(); i++)
    if (rgbuilt[i].pw != NULL && rgbuilt[i].pctl->nIdx < 0 &&
      strcmp(rgbuilt[i].pctl->szId, szId) == 0)
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


static CONST RCCTL *PctlBuiltQt(CONST QVector<RCBUILT> *prg, int i)
{
  return (*prg)[i].pctl;
}


// Shrink one control's own font until its text fits the box the resource
// gave it. Only the text changes size; the box stays where it is, so the
// dialog keeps Windows' proportions.
static void RcFitTextQt(QWidget *pw, CONST QString &str, int dxBox)
{
  if (str.isEmpty() || dxBox <= 0)
    return;
  QFont font = pw->font();
  real rPt = font.pointSizeF();

  for (int i = 0; i < 8; i++) {
    QFontMetrics fm(font);
    if (fm.horizontalAdvance(str) + 4 <= dxBox || font.pointSizeF() < rPt*0.7)
      break;
    font.setPointSizeF(font.pointSizeF() - 0.5);
  }
  if (font.pointSizeF() < rPt)
    pw->setFont(font);

  // Still too wide even shrunk: let it wrap, which is what Windows' static
  // text does. "Atlas City Coloring:" in its 35 unit box is the one case.
  QFontMetrics fmFinal(font);
  QLabel *pl = qobject_cast<QLabel *>(pw);
  if (pl != NULL && fmFinal.horizontalAdvance(str) + 4 > dxBox)
    pl->setWordWrap(fTrue);
}


// Lay a transcribed dialog out. Widgets are positioned absolutely rather
// than through a layout, which is the whole point -- the resource has
// already decided where everything goes.
static void RcBuildDialogQt(QDialog *pdlg, CONST RCCTL *rgctl, int cctl,
  int dxDlg, int dyDlg, QVector<RCBUILT> *prgbuilt)
{
  // Windows' own units: one horizontal unit is a quarter of the dialog
  // font's average character width, one vertical an eighth of its line
  // height. Keeping them exactly that keeps the resource's proportions.
  //
  // Two things were tried here and are wrong. Widening the units so the
  // longest label fits nearly doubles the dialog: "Atlas City Coloring:"
  // sits in a 35 unit box and demands a base of 19 against a natural 9.
  // Shrinking the whole dialog font does nothing at all, because the base
  // unit is derived from that same font, so the space available shrinks
  // exactly as fast as the text in it.
  //
  // What does work is leaving the boxes where the resource put them and
  // shrinking the text of just the control that overflows -- see
  // RcFitTextQt() below. The layout stays Windows', and the strings that
  // this font renders wider than MS Shell Dlg did simply come out a point
  // or so smaller.
  QFontMetrics fm(pdlg->font());
  int dxBase = fm.averageCharWidth(), dyBase = fm.height();
  int i, iPass;

  // Every dialog built from the resource answers bare mnemonic letters,
  // which is where Windows' dialogs act on them. Installed here rather
  // than in PrepareDialogQt() because the restriction dialogs never call
  // that, and they are the ones with 52 checkboxes to reach.
  static MnemonicKeysQt filterMnemonic;
  pdlg->installEventFilter(&filterMnemonic);

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
      case ctlLabel: {
        // A static label renders "&" literally, so drop it -- the
        // resource only uses it on controls that take focus. Wrapping
        // matches Windows, whose static text wraps inside its rectangle.
        QLabel *pl = new QLabel(str.remove(QChar('&')), pdlg);
        pl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        pw = pl;
        break; }
      case ctlButton:
        pw = new QPushButton(str, pdlg);
        break;
      case ctlEdit:
        pw = new QLineEdit(pdlg);
        break;
      case ctlCombo:
        pw = new QComboBox(pdlg);
        break;
      case ctlList:
        pw = new QListWidget(pdlg);
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

  // Keep the base units Windows' own: one horizontal unit is a quarter of
  // the font's average character width, one vertical an eighth of its line
  // height. Widening them so the longest label fits was tried and is
  // wrong -- "Atlas City Coloring:" sits in a 35 unit box and would have
  // demanded a base of 19 against the natural 9, very nearly doubling the
  // width of the whole dialog for one label. Windows just wraps that label
  // onto two lines, which is what static text does there.
  //
  // So labels wrap, and any other text control that doesn't fit its box in
  // this font grows on its own rather than dragging the dialog with it.
  // The proportions then stay exactly the resource's.
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
    int dxCtl = pctl->dx * dxBase / 4;
    if (pctl->nType == ctlLabel || pctl->nType == ctlCheck ||
      pctl->nType == ctlRadio || pctl->nType == ctlButton) {
      // A checkbox or radio spends part of its width on the indicator.
      int dxInd = (pctl->nType == ctlCheck || pctl->nType == ctlRadio) ?
        pw->style()->pixelMetric(QStyle::PM_IndicatorWidth) + 6 : 0;
      RcFitTextQt(pw, QString(pctl->szText).remove(QChar('&')),
        dxCtl - dxInd);
    }
    QLabel *plWrap = qobject_cast<QLabel *>(pw);
    if (plWrap != NULL && plWrap->wordWrap())
      dyCtl = Max(dyCtl, plWrap->heightForWidth(dxCtl));
    pw->setGeometry(pctl->x * dxBase / 4, pctl->y * dyBase / 8,
      dxCtl, dyCtl);
    if (pctl->nType == ctlIcon) {
      QPixmap pix = PixAstrologIconQt();
      if (!pix.isNull())
        ((QLabel *)pw)->setPixmap(pix.scaled(pctl->dx * dxBase / 4,
          pctl->dy * dyBase / 8, Qt::KeepAspectRatio,
          Qt::SmoothTransformation));
    }
  }
}

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





#ifdef QTTEST
// Every text control in every transcribed dialog, with the width the
// resource gives it, for the font fitting diagnostic in qttest.cpp.
void RcAllTablesTestQt(QVector<QPair<QString,int> > *prg)
{
  struct { CONST RCCTL *rgctl; int cctl; } rgtab[] = {
    {rgctlRestrict, cctlRestrict}, {rgctlStar, cctlStar},
    {rgctlMoons, cctlMoons}, {rgctlObject, cctlObject},
    {rgctlObject2, cctlObject2}, {rgctlObjectM, cctlObjectM},
    {rgctlAspect, cctlAspect}, {rgctlColor, cctlColor},
    {rgctlCustom, cctlCustom}, {rgctlCustomS, cctlCustomS},
    {rgctlCalc, cctlCalc}, {rgctlDisplay, cctlDisplay},
    {rgctlChart, cctlChart}, {rgctlGraphics, cctlGraphics} };
  prg->clear();
  for (int i = 0; i < (int)(sizeof(rgtab)/sizeof(rgtab[0])); i++)
    for (int j = 0; j < rgtab[i].cctl; j++) {
      CONST RCCTL *pctl = &rgtab[i].rgctl[j];
      if (pctl->szText[0] == 0 || pctl->dx <= 0)
        continue;
      if (pctl->nType != ctlLabel && pctl->nType != ctlCheck &&
        pctl->nType != ctlRadio && pctl->nType != ctlButton)
        continue;
      prg->append(qMakePair(QString(pctl->szText).remove(QChar('&')),
        pctl->dx));
    }
}
#endif

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


// Windows refuses before it ever puts up a file picker: DlgOpenChart and
// DlgSaveChart test these first and say so. The shared write path in
// io.cpp honours us.fNoWrite too, so nothing was actually written here
// either -- but only after the user had picked a name and been told
// nothing, which reads as the save silently failing. Refuse up front,
// with the wording Windows uses.

static flag FNoReadQt()
{
  if (!us.fNoRead)
    return fFalse;
  PrintWarning("File input is disabled.");
  return fTrue;
}


static flag FNoWriteQt()
{
  if (!us.fNoWrite)
    return fFalse;
  PrintWarning("File output is disabled.");
  return fTrue;
}


// Load a chart file chosen via a standard file picker, exactly as Windows'
// DlgOpenChart does via the stock Windows file dialog -- no custom dialog
// is needed here either, just FInputData() doing the actual work.

void ShowOpenChartDialogQt()
{
  if (FNoReadQt())
    return;
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
  if (FNoWriteQt())
    return;
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
  if (FNoWriteQt())
    return;
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
  if (FNoWriteQt())
    return;
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
  if (FNoWriteQt())
    return;
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
  if (FNoWriteQt())
    return;
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
  if (FNoWriteQt())
    return;
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
  if (FNoReadQt())
    return;
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
  if (FNoReadQt())
    return;
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
  if (FNoWriteQt())
    return;
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
  if (FNoWriteQt())
    return;
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

// File Settings, transcribed from dlgFile.

void ShowFileSettingsDialogQt()
{
  CONST RCFLAG rgflag[] = {
    {"dxFi_YOO", -1, &us.fSmartSave, fFalse},
    {"dxFi_kh",  -1, &us.fTextHTML,  fFalse},
    {"dxFi_Yo",  -1, &us.fWriteOld,  fFalse} };
  QDialog dlg(gi.qwind);
  QVector<RCBUILT> rgbuilt;
  char sz[cchSzMax];
  int nwx, i;
  real rI;

  dlg.setWindowTitle(szTitleFile);
  RcBuildDialogQt(&dlg, rgctlFile, cctlFile, dxFile, dyFile, &rgbuilt);
  RcLoadFlagsQt(rgbuilt, rgflag, CRcFlag(rgflag));

  // These three read something other than a plain flag.
  QCheckBox *pcbBmpP = (QCheckBox *)PwRcFindQt(rgbuilt, "dxFi_Xbp");
  QCheckBox *pcbPS = (QCheckBox *)PwRcFindIdxQt(rgbuilt, "dxFi_Xp", 0);
  QCheckBox *pcbFont = (QCheckBox *)PwRcFindQt(rgbuilt, "dxFi_YXf");
  QCheckBox *pcbBack = (QCheckBox *)PwRcFindIdxQt(rgbuilt, "dxFi_XI", 0);
  QCheckBox *pcbBmpWin = (QCheckBox *)PwRcFindQt(rgbuilt, "dxFi_Wb");
  QCheckBox *pcbNoPop = (QCheckBox *)PwRcFindQt(rgbuilt, "dxFi_Wt");
  if (pcbBmpP != NULL)  pcbBmpP->setChecked(gs.chBmpMode == 'P');
  if (pcbPS != NULL)    pcbPS->setChecked(!gs.fPSComplete);
  if (pcbFont != NULL)  pcbFont->setChecked(gs.nFontAll > 0);
  if (pcbBack != NULL)  pcbBack->setChecked(!gs.fBackDraw);
  if (pcbBmpWin != NULL) pcbBmpWin->setChecked(FBmpWindowQt());
  if (pcbNoPop != NULL) pcbNoPop->setChecked(FNoPopupQt());

  QLineEdit *peAnti = (QLineEdit *)PwRcFindQt(rgbuilt, "deFi_Wx");
  QLineEdit *peThick = (QLineEdit *)PwRcFindQt(rgbuilt, "deFi_YXx");
  QLineEdit *peADB = (QLineEdit *)PwRcFindQt(rgbuilt, "deFi_Y5i");
  QLineEdit *peInchX = (QLineEdit *)PwRcFindQt(rgbuilt, "deFi_YXp0_x");
  QLineEdit *peInchY = (QLineEdit *)PwRcFindQt(rgbuilt, "deFi_YXp0_y");
  QComboBox *pcbPct = (QComboBox *)PwRcFindIdxQt(rgbuilt, "dcFi_XI", 1);
  if (peAnti != NULL)  peAnti->setText(QString::number(NAntialiasQt()));
  if (peThick != NULL) peThick->setText(QString::number(gs.nThickAdjust));
  if (peADB != NULL)   peADB->setText(FSzSet(us.szADB) ? us.szADB : "");
  if (peInchX != NULL) peInchX->setText(SzLength(gs.xInch));
  if (peInchY != NULL) peInchY->setText(SzLength(gs.yInch));
  if (pcbPct != NULL) {
    QStringList rgstr;
    for (i = 25; i <= 100; i += 25)
      rgstr << QString::number(i);
    FillComboQt(pcbPct, SzFormatRQt(gs.rBackPct, -3), rgstr);
  }
  RcLoadRadioQt(rgbuilt, 1, 3,
    gs.nOrient == 0 ? 2 : (gs.nOrient > 0 ? 0 : 1));

  RcWireOkCancelQt(&dlg, rgbuilt);
  PrepareDialogQt(&dlg);
  if (dlg.exec() != QDialog::Accepted)
    return;

  nwx = peAnti != NULL ? peAnti->text().toInt() : NAntialiasQt();
  rI = pcbPct != NULL ?
    RFromSz(pcbPct->currentText().toLocal8Bit().constData()) : gs.rBackPct;
  if (!FValidAntialias(nwx)) { ErrorEnsureQt(&dlg, nwx, "antialias"); return; }
  if (!FValidBackPct(rI))    { ErrorEnsureQt(&dlg, (int)rI, "background transparency"); return; }

  RcStoreFlagsQt(rgbuilt, rgflag, CRcFlag(rgflag));
  if (pcbBmpP != NULL) {
    if (pcbBmpP->isChecked())
      gs.chBmpMode = 'P';
    else if (gs.chBmpMode == 'P')
      gs.chBmpMode = 'B';
  }
  if (pcbPS != NULL)     gs.fPSComplete = !pcbPS->isChecked();
  if (pcbBack != NULL)   gs.fBackDraw = !pcbBack->isChecked();
  if (pcbBmpWin != NULL) SetBmpWindowQt(pcbBmpWin->isChecked());
  if (pcbNoPop != NULL)  SetNoPopupQt(pcbNoPop->isChecked());
  // One box turns every font choice on or off together, remembering what
  // they were so it can put them back.
  if (pcbFont != NULL) {
    gs.nFontAll = pcbFont->isChecked() * gi.nFontPrev;
    gs.nFontTxt = gs.nFontAll / 0x100000;
    gs.nFontSig = (gs.nFontAll / 0x10000) % 0x10;
    gs.nFontHou = (gs.nFontAll / 0x1000) % 0x10;
    gs.nFontObj = (gs.nFontAll / 0x100) % 0x10;
    gs.nFontAsp = (gs.nFontAll / 0x10) % 0x10;
    gs.nFontNak = gs.nFontAll % 0x10;
  }
  if (nwx <= 0)
    gs.fAntialias = fFalse;
  else
    SetAntialiasQt(nwx);
  if (peThick != NULL) gs.nThickAdjust = peThick->text().toInt();
  gs.rBackPct = rI;
  if (peADB != NULL) {
    sprintf(sz, "%.*s", cchSzMax-1,
      peADB->text().toLocal8Bit().constData());
    FCloneSz(sz, &us.szADB);
  }
  if (peInchX != NULL)
    gs.xInch = RParseSz(peInchX->text().toLocal8Bit().constData(), pmLength);
  if (peInchY != NULL)
    gs.yInch = RParseSz(peInchY->text().toLocal8Bit().constData(), pmLength);
  i = NRcStoreRadioQt(rgbuilt, 1, 3, 2);
  gs.nOrient = (i == 2 ? 0 : (i == 0 ? 1 : -1));
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

  dlg.setWindowTitle(szTitleGraphics);
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
  FocusDialogQt(PwRcFindQt(rgbuilt, "deGr_Xw_x"));
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

// The four atlas buttons Windows' chart info dialogs carry, wired to the
// lookups in atlas.cpp. Those fill a Win32 listbox directly; the Qt build
// receives their rows through pfnAtlasRowQt instead (see atlas.cpp), which
// is set only while one of these runs.

#ifdef ATLAS
static QListWidget *s_plistAtlasQt = NULL;
static QVector<int> s_rgiaeQt;

static void AtlasRowQt(CONST char *sz, int iae)
{
  if (s_plistAtlasQt == NULL)
    return;
  // fromLatin1, not the implicit UTF-8: these rows carry Astrolog's degree
  // sign as a single high byte, which decoded as UTF-8 is invalid and comes
  // out as a replacement character.
  s_plistAtlasQt->addItem(QString::fromLatin1(sz));
  s_rgiaeQt.append(iae);
}

// Run one lookup with the rows going to "plist".
static void RcAtlasRunQt(QListWidget *plist, int nWhich, QLineEdit *peLoc,
  QComboBox *pcbLon, QComboBox *pcbLat, CI *pci)
{
  char sz[cchSzMax];
  int ilist = 12, i;
  real lon, lat;

  if (plist == NULL)
    return;
  plist->clear();
  s_plistAtlasQt = plist;
  s_rgiaeQt.clear();
  pfnAtlasRowQt = AtlasRowQt;

  if (nWhich == 0) {                     // Lookup City
    sprintf(sz, "%.*s", cchSzMax-1,
      peLoc->text().toLocal8Bit().constData());
    if (!DisplayAtlasLookup(sz, 1, &ilist))
      plist->addItem("Couldn't get atlas data!");
  } else if (nWhich == 1) {              // Nearby Cities
    lon = RParseSz(pcbLon->currentText().toLocal8Bit().constData(), pmLon);
    lat = RParseSz(pcbLat->currentText().toLocal8Bit().constData(), pmLat);
    if (!DisplayAtlasNearby(lon, lat, 1, &ilist, fFalse))
      plist->addItem("Couldn't get atlas data!");
  } else {                               // Time Changes
    sprintf(sz, "%.*s", cchSzMax-1,
      peLoc->text().toLocal8Bit().constData());
    pfnAtlasRowQt = NULL;
    if (!DisplayAtlasLookup(sz, 0, &i)) {
      pfnAtlasRowQt = NULL;
      s_plistAtlasQt = NULL;
      plist->addItem("Put a valid city in the Location field first.");
      return;
    }
    pfnAtlasRowQt = AtlasRowQt;
    if (!DisplayTimezoneChanges(is.rgae[i].izn, 1, pci))
      plist->addItem("Couldn't get time zone data!");
  }
  pfnAtlasRowQt = NULL;
  s_plistAtlasQt = NULL;
}

// "Apply Info": copy the highlighted atlas row into the location fields.
static void RcAtlasApplyQt(QListWidget *plist, QComboBox *pcbLon,
  QComboBox *pcbLat, QComboBox *pcbZon, QLineEdit *peLoc)
{
  char sz[cchSzMax];
  int iRow, iae, nSav;

  if (plist == NULL)
    return;
  iRow = plist->currentRow();
  if (iRow < 0 || iRow >= s_rgiaeQt.size() || s_rgiaeQt[iRow] < 0)
    return;
  iae = s_rgiaeQt[iRow];
  nSav = us.fAnsiChar; us.fAnsiChar = fFalse;
  sprintf(sz, "%s", SzLocation(is.rgae[iae].lon, is.rgae[iae].lat));
  us.fAnsiChar = nSav;
  sz[is.ichLocSplit] = chNull;
  if (pcbLon != NULL) pcbLon->setEditText(&sz[0]);
  if (pcbLat != NULL) pcbLat->setEditText(&sz[is.ichLocSplit+1]);
  if (pcbZon != NULL)
    pcbZon->setEditText(SzZone(ZondefFromIzn(is.rgae[iae].izn)));
  if (peLoc != NULL) peLoc->setText(SzCity(iae));
}
#endif // ATLAS


// Chart info, transcribed from dlgInfo: the date, time and place fields,
// Windows' Now and Recall buttons, and the four atlas lookups with their
// results list. The same dialog serves chart #2 with a different title,
// as it does on Windows.

static void RcLoadChartInfoQt(CONST QVector<RCBUILT> &rgbuilt, CONST CI *pci)
{
  char sz[cchSzMax];
  int nSavChar;

  sprintf(sz, "%.3s", szMonth[FValidMon(pci->mon) ? pci->mon : 1]);
  FillComboQt((QComboBox *)PwRcFindQt(rgbuilt, "dcInMon"), sz,
    RgstrMonthQt());
  FillComboQt((QComboBox *)PwRcFindQt(rgbuilt, "dcInDay"),
    QString::number(pci->day), RgstrDayQt());
  FillComboQt((QComboBox *)PwRcFindQt(rgbuilt, "dcInYea"),
    QString::number(pci->yea), RgstrYearQt());
  FillComboQt((QComboBox *)PwRcFindQt(rgbuilt, "dcInTim"), SzTim(pci->tim),
    RgstrTimeQt());
  FillComboQt((QComboBox *)PwRcFindQt(rgbuilt, "dcInDst"),
    pci->dst == 0.0 ? "No" : (pci->dst == 1.0 ? "Yes" :
    (pci->dst == dstAuto ? "Autodetect" : SzZone(pci->dst))), RgstrDstQt());
  sprintf(sz, "%s", SzZone(pci->zon));
  FillComboQt((QComboBox *)PwRcFindQt(rgbuilt, "dcInZon"),
    sz[0] == '+' ? &sz[1] : sz, RgstrZoneQt());
  // SzLocation()'s degree byte confuses the parse back, so ask for it
  // without, the way Windows' SetEditSZOA does.
  nSavChar = us.fAnsiChar; us.fAnsiChar = fFalse;
  sprintf(sz, "%s", SzLocation(pci->lon, pci->lat));
  us.fAnsiChar = nSavChar;
  sz[is.ichLocSplit] = chNull;
  FillComboQt((QComboBox *)PwRcFindQt(rgbuilt, "dcInLon"), &sz[0],
    RgstrLonQt());
  FillComboQt((QComboBox *)PwRcFindQt(rgbuilt, "dcInLat"),
    &sz[is.ichLocSplit+1], RgstrLatQt());
  QLineEdit *peName = (QLineEdit *)PwRcFindQt(rgbuilt, "deInNam");
  QLineEdit *peLoc = (QLineEdit *)PwRcFindQt(rgbuilt, "deInLoc");
  if (peName != NULL)
    peName->setText(FSzSet(pci->nam) ? pci->nam : "");
  if (peLoc != NULL)
    peLoc->setText(FSzSet(pci->loc) ? pci->loc : "");
}


static void ShowChartInfoForQt(CI *pci, CONST char *szTitle)
{
  QDialog dlg(gi.qwind);
  QVector<RCBUILT> rgbuilt;
  CI ci = *pci;

  dlg.setWindowTitle(szTitle);
  RcBuildDialogQt(&dlg, rgctlInfo, cctlInfo, dxInfo, dyInfo, &rgbuilt);
  RcLoadChartInfoQt(rgbuilt, pci);

  QComboBox *pcbLon = (QComboBox *)PwRcFindQt(rgbuilt, "dcInLon");
  QComboBox *pcbLat = (QComboBox *)PwRcFindQt(rgbuilt, "dcInLat");
  QComboBox *pcbZon = (QComboBox *)PwRcFindQt(rgbuilt, "dcInZon");
  QLineEdit *peLoc = (QLineEdit *)PwRcFindQt(rgbuilt, "deInLoc");
  QListWidget *plist = (QListWidget *)PwRcFindQt(rgbuilt, "dlIn");

  // Now: today's date and time at the default location. Recall: whatever
  // was last saved. Both refill the fields rather than closing.
  QPushButton *ppbNow = (QPushButton *)PwRcFindQt(rgbuilt, "dbInNow");
  QPushButton *ppbSet = (QPushButton *)PwRcFindQt(rgbuilt, "dbInSet");
  if (ppbNow != NULL)
    QObject::connect(ppbNow, &QPushButton::clicked, &dlg, [&rgbuilt]() {
      CI ciT;
#ifdef TIME
      GetTimeNow(&ciT.mon, &ciT.day, &ciT.yea, &ciT.tim, ciDefa.dst,
        ciDefa.zon);
      ciT.dst = ciDefa.dst; ciT.zon = ciDefa.zon;
      ciT.lon = ciDefa.lon; ciT.lat = ciDefa.lat;
      ciT.nam = ciDefa.nam; ciT.loc = ciDefa.loc;
      RcLoadChartInfoQt(rgbuilt, &ciT);
#endif
    });
  if (ppbSet != NULL)
    QObject::connect(ppbSet, &QPushButton::clicked, &dlg, [&rgbuilt]() {
      RcLoadChartInfoQt(rgbuilt, &ciSave);
    });

#ifdef ATLAS
  QPushButton *ppbCity = (QPushButton *)PwRcFindQt(rgbuilt, "dbInCity");
  QPushButton *ppbCoor = (QPushButton *)PwRcFindQt(rgbuilt, "dbInCoor");
  QPushButton *ppbChan = (QPushButton *)PwRcFindQt(rgbuilt, "dbInChan");
  QPushButton *ppbAppl = (QPushButton *)PwRcFindQt(rgbuilt, "dbInAppl");
  if (ppbCity != NULL)
    QObject::connect(ppbCity, &QPushButton::clicked, &dlg,
      [plist, peLoc, pcbLon, pcbLat, &ci]() {
        RcAtlasRunQt(plist, 0, peLoc, pcbLon, pcbLat, &ci); });
  if (ppbCoor != NULL)
    QObject::connect(ppbCoor, &QPushButton::clicked, &dlg,
      [plist, peLoc, pcbLon, pcbLat, &ci]() {
        RcAtlasRunQt(plist, 1, peLoc, pcbLon, pcbLat, &ci); });
  if (ppbChan != NULL)
    QObject::connect(ppbChan, &QPushButton::clicked, &dlg,
      [plist, peLoc, pcbLon, pcbLat, &ci, &rgbuilt]() {
        // Windows takes the year from the dialog for this one.
        QComboBox *pcbYea = (QComboBox *)PwRcFindQt(rgbuilt, "dcInYea");
        if (pcbYea != NULL)
          ci.yea = NParseSz(pcbYea->currentText().toLocal8Bit().constData(),
            pmYea);
        RcAtlasRunQt(plist, 2, peLoc, pcbLon, pcbLat, &ci); });
  if (ppbAppl != NULL)
    QObject::connect(ppbAppl, &QPushButton::clicked, &dlg,
      [plist, pcbLon, pcbLat, pcbZon, peLoc]() {
        RcAtlasApplyQt(plist, pcbLon, pcbLat, pcbZon, peLoc); });
#endif

  RcWireOkCancelQt(&dlg, rgbuilt);
  PrepareDialogQt(&dlg);
  FocusDialogQt(PwRcFindQt(rgbuilt, "dcInMon"));
  if (dlg.exec() != QDialog::Accepted)
    return;

  QByteArray ba;
  QComboBox *pcb;
  ci = *pci;
  if ((pcb = (QComboBox *)PwRcFindQt(rgbuilt, "dcInMon")) != NULL) {
    ba = pcb->currentText().toLocal8Bit();
    ci.mon = NParseSz(ba.constData(), pmMon); }
  if ((pcb = (QComboBox *)PwRcFindQt(rgbuilt, "dcInDay")) != NULL) {
    ba = pcb->currentText().toLocal8Bit();
    ci.day = NParseSz(ba.constData(), pmDay); }
  if ((pcb = (QComboBox *)PwRcFindQt(rgbuilt, "dcInYea")) != NULL) {
    ba = pcb->currentText().toLocal8Bit();
    ci.yea = NParseSz(ba.constData(), pmYea); }
  if ((pcb = (QComboBox *)PwRcFindQt(rgbuilt, "dcInTim")) != NULL) {
    ba = pcb->currentText().toLocal8Bit();
    ci.tim = RParseSz(ba.constData(), pmTim); }
  if ((pcb = (QComboBox *)PwRcFindQt(rgbuilt, "dcInDst")) != NULL) {
    ba = pcb->currentText().toLocal8Bit();
    ci.dst = RParseSz(ba.constData(), pmDst); }
  if (pcbZon != NULL) { ba = pcbZon->currentText().toLocal8Bit();
    ci.zon = RParseSz(ba.constData(), pmZon); }
  if (pcbLon != NULL) { ba = pcbLon->currentText().toLocal8Bit();
    ci.lon = RParseSz(ba.constData(), pmLon); }
  if (pcbLat != NULL) { ba = pcbLat->currentText().toLocal8Bit();
    ci.lat = RParseSz(ba.constData(), pmLat); }

  if (!FValidMon(ci.mon) || !FValidDay(ci.day, ci.mon, ci.yea) ||
    !FValidYea(ci.yea) || !FValidTim(ci.tim) || !FValidDst(ci.dst) ||
    !FValidZon(ci.zon) || !FValidLon(ci.lon) || !FValidLat(ci.lat)) {
    QMessageBox::warning(gi.qwind, szAppName,
      "One or more chart info fields are invalid.");
    return;
  }
  QLineEdit *peName = (QLineEdit *)PwRcFindQt(rgbuilt, "deInNam");
  if (peName != NULL)
    FCloneSz(peName->text().toLocal8Bit().constData(), &ci.nam);
  if (peLoc != NULL)
    FCloneSz(peLoc->text().toLocal8Bit().constData(), &ci.loc);
  *pci = ci;
  ciSave = ci;
  RecastAndRedrawQt();
}

void ShowChartInfoDialogQt()
{
  ShowChartInfoForQt(&ciCore, szTitleInfo);
}

void ShowChartInfo2DialogQt()
{
  ShowChartInfoForQt(&ciTwin, "Set Chart #2 Info");
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
  if (FNoReadQt())
    return;
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

// Charts #1 through #6, transcribed from dlgInfoAll. Two lines of summary
// per chart in static text, an Open and a Set Info button for each, the
// relationship type as a radio group, and which charts progress.

static void RcLoadInfoAllQt(CONST QVector<RCBUILT> &rgbuilt)
{
  char sz[cchSzMax];
  int i, n, nSav;
  flag fSav;
  CI *pci;

  // Windows asks for the degree sign here (fAnsiChar 2) since these are
  // display only, and the fields are never parsed back.
  nSav = us.fAnsiChar; us.fAnsiChar = 2;
  fSav = us.fGraphics; us.fGraphics = fTrue;
  for (i = 1; i <= cRing; i++) {
    pci = rgpci[i];
    n = DayOfWeek(pci->mon, pci->day, pci->yea);
    sprintf(sz, "%.3s %s %s (%cT Zone %s) %s", szDay[n],
      SzDate(pci->mon, pci->day, pci->yea, 3), SzTim(pci->tim),
      ChDst(pci->dst), SzZone(pci->zon), SzLocation(pci->lon, pci->lat));
    QLabel *pl = (QLabel *)PwRcFindIdxQt(rgbuilt, "ds", (i-1)*2 + 1);
    if (pl != NULL)
      pl->setText(QString::fromLatin1(sz));
    sprintf(sz, "%s%s%s", pci->nam, FSzSet(pci->nam) && FSzSet(pci->loc) ?
      "; " : "", pci->loc);
    pl = (QLabel *)PwRcFindIdxQt(rgbuilt, "ds", (i-1)*2 + 2);
    if (pl != NULL)
      pl->setText(QString::fromLatin1(sz));
  }
  us.fAnsiChar = nSav; us.fGraphics = fSav;
}


void ShowChartsAllDialogQt()
{
  QDialog dlg(gi.qwind);
  QVector<RCBUILT> rgbuilt;
  int i;

  dlg.setWindowTitle(szTitleInfoAll);
  RcBuildDialogQt(&dlg, rgctlInfoAll, cctlInfoAll, dxInfoAll, dyInfoAll,
    &rgbuilt);
  RcLoadInfoAllQt(rgbuilt);

  for (i = 1; i <= cRing; i++) {
    QPushButton *ppbOpen =
      (QPushButton *)PwRcFindIdxQt(rgbuilt, "dbIa_o", i);
    QPushButton *ppbInfo =
      (QPushButton *)PwRcFindIdxQt(rgbuilt, "dbIa_i", i);
    if (ppbOpen != NULL)
      QObject::connect(ppbOpen, &QPushButton::clicked, &dlg,
        [i, &rgbuilt]() {
          ShowOpenChartIntoDialogQt(i);
          RcLoadInfoAllQt(rgbuilt);   // Its summary line just changed.
        });
    if (ppbInfo != NULL)
      QObject::connect(ppbInfo, &QPushButton::clicked, &dlg,
        [i, &rgbuilt]() {
          // Windows titles these "Set Chart #N Info", the plain caption
          // only for chart one (wdialog.cpp:1142).
          char szT[cchSzDef];
          if (i <= 1)
            sprintf(szT, "%s", szTitleInfo);
          else
            sprintf(szT, "Set Chart #%d Info", i);
          ShowChartInfoForQt(rgpci[i], szT);
          RcLoadInfoAllQt(rgbuilt);
        });
  }

  // Windows numbers these backwards: radio 0 is the relationship value 0,
  // and each one after it is the next negative value (rcDual downwards).
  i = us.nRel;
  if (i > rcDual)
    i = 0;
  else if (i < rcHexaWheel)
    i = rcDual;
  RcLoadRadioQt(rgbuilt, 1, 6, -i);
  for (i = 2; i <= 5; i++) {
    QCheckBox *pcb = (QCheckBox *)PwRcFindIdxQt(rgbuilt, "dx", i);
    if (pcb != NULL)
      pcb->setChecked(rgfProg[i] != 0);
  }

  RcWireOkCancelQt(&dlg, rgbuilt);
  PrepareDialogQt(&dlg);
  if (dlg.exec() != QDialog::Accepted)
    return;

  SetRelQt(-NRcStoreRadioQt(rgbuilt, 1, 6, 0));
  for (i = 2; i <= 5; i++) {
    QCheckBox *pcb = (QCheckBox *)PwRcFindIdxQt(rgbuilt, "dx", i);
    if (pcb != NULL)
      rgfProg[i] = pcb->isChecked();
  }
  RecastAndRedrawQt();
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

// Chart List, transcribed from dlgList. The list holds every chart read in
// this session; the buttons down the right sort it, move a chart into one
// of the six slots or out of one, edit or delete entries, and the two at
// the bottom filter what's shown by name and location.

// Refill the list, optionally keeping only rows matching the filter
// fields, and return how many were shown. rgiciQt maps row to chart.
static QVector<int> s_rgiciQt;

static void RcFillChartListQt(QListWidget *plist, QLabel *plSize,
  QLineEdit *peName, QLineEdit *peLoc, flag fFilter)
{
  char sz[cchSzLine], sz1[cchSzDef], sz2[cchSzDef];
  int i, i2 = 0, j, nSav;
  flag fSav;
  CI *pci;
#ifdef EXPRESS
  CI ciT;
  CP cpT;
  flag fRet;
#endif

  if (plist == NULL)
    return;
  plist->clear();
  s_rgiciQt.clear();
  sz1[0] = sz2[0] = chNull;
  if (fFilter) {
    if (peName != NULL)
      sprintf(sz1, "%.*s", cchSzDef-1,
        peName->text().toLocal8Bit().constData());
    if (peLoc != NULL)
      sprintf(sz2, "%.*s", cchSzDef-1,
        peLoc->text().toLocal8Bit().constData());
  }
  for (i = 0; i < is.cci; i++) {
    pci = &is.rgci[i];
    if (fFilter) {
      if (*sz1) {
        for (j = 0; pci->nam[j]; j++)
          if (FEqSzSubI(sz1, &pci->nam[j]))
            break;
        if (!pci->nam[j])
          continue;
      }
      if (*sz2) {
        for (j = 0; pci->loc[j]; j++)
          if (FEqSzSubI(sz2, &pci->loc[j]))
            break;
        if (!pci->loc[j])
          continue;
      }
#ifdef EXPRESS
      // May want to skip current chart if AstroExpression says to do so.
      // Windows' DlgList has always done this and this list never did, so
      // an expression that narrowed the chart list there did nothing here.
      // Cast the candidate chart so the expression can see its positions,
      // then put the real one back: this runs once per chart in the list.
      if (!us.fExpOff && FSzSet(us.szExpListF)) {
        cpT = cp0; ciT = ciCore;
        ciCore = *pci;
        CastChart(-1);
        ExpSetN(iLetterZ, i);
        fRet = !NParseExpression(us.szExpListF);
        ciCore = ciT;
        cp0 = cpT;
        if (fRet)
          continue;
      }
#endif
    }
    j = DayOfWeek(pci->mon, pci->day, pci->yea);
    nSav = us.fAnsiChar; us.fAnsiChar = 2;
    fSav = us.fGraphics; us.fGraphics = fTrue;
    sprintf(sz, "%.3s %s %s (%cT Zone %s) %s %s%s%s", szDay[j],
      SzDate(pci->mon, pci->day, pci->yea, 3), SzTim(pci->tim),
      ChDst(pci->dst), SzZone(pci->zon), SzLocation(pci->lon, pci->lat),
      pci->nam, FSzSet(pci->nam) && FSzSet(pci->loc) ? "; " : "",
      pci->loc);
    us.fAnsiChar = nSav; us.fGraphics = fSav;
    plist->addItem(QString::fromLatin1(sz));
    s_rgiciQt.append(i);
    i2++;
  }
  if (i2 <= 0) {
    plist->addItem("(No charts in list)");
    s_rgiciQt.append(-1);
  }
  if (plSize != NULL)
    plSize->setText(QString("List size: %1").arg(i2));
}


void ShowChartListDialogQt()
{
  QDialog dlg(gi.qwind);
  QVector<RCBUILT> rgbuilt;

  dlg.setWindowTitle(szTitleList);
  RcBuildDialogQt(&dlg, rgctlList, cctlList, dxList, dyList, &rgbuilt);

  QListWidget *plist = (QListWidget *)PwRcFindQt(rgbuilt, "dlLi");
  QLabel *plSize = (QLabel *)PwRcFindIdxQt(rgbuilt, "ds", 1);
  QLineEdit *peName = (QLineEdit *)PwRcFindQt(rgbuilt, "deLi_n");
  QLineEdit *peLoc = (QLineEdit *)PwRcFindQt(rgbuilt, "deLi_l");
  RcFillChartListQt(plist, plSize, peName, peLoc, fFalse);
  RcLoadRadioQt(rgbuilt, 1, 5, 0);    // Sort by: date
  RcLoadRadioQt(rgbuilt, 6, 6, 0);    // Slot: 1st

  // The selected chart, or -1 when the row is the "no charts" placeholder.
  auto iciSel = [plist]() -> int {
    int iRow = plist != NULL ? plist->currentRow() : -1;
    return (iRow >= 0 && iRow < s_rgiciQt.size()) ? s_rgiciQt[iRow] : -1;
  };
  auto iSlot = [&rgbuilt]() -> int {
    return NRcStoreRadioQt(rgbuilt, 6, 6, 0) + 1;
  };
  auto refill = [plist, plSize, peName, peLoc](flag fFilter) {
    RcFillChartListQt(plist, plSize, peName, peLoc, fFilter);
  };

  struct { CONST char *szId; int nAct; } rgbut[] = {
    {"dbLi_sl", 0}, {"dbLi_da", 1}, {"dbLi_f", 2}, {"dbLi_fr", 3},
    {"dbLi_st", 4}, {"dbLi_cf", 5}, {"dbLi_ec", 6}, {"dbLi_dc", 7} };
  for (int i = 0; i < (int)(sizeof(rgbut)/sizeof(rgbut[0])); i++) {
    QPushButton *ppb = (QPushButton *)PwRcFindQt(rgbuilt, rgbut[i].szId);
    if (ppb == NULL)
      continue;
    int nAct = rgbut[i].nAct;
    QObject::connect(ppb, &QPushButton::clicked, &dlg,
      [nAct, &rgbuilt, plist, iciSel, iSlot, refill]() {
        int ici = iciSel(), i2;
        switch (nAct) {
        case 0:                                   // Sort List
          FSortCIList(NRcStoreRadioQt(rgbuilt, 1, 5, 0));
          refill(fFalse);
          return;
        case 1:                                   // Delete All
          is.cci = 0; refill(fFalse); return;
        case 2: refill(fTrue); return;            // Filter
        case 3:                                   // Remove Filter
          refill(fFalse); return;
        }
        if (ici < 0 && nAct != 5) {
          QMessageBox::warning(gi.qwind, szAppName,
            "Can't do operation because no chart in list is selected.");
          return;
        }
        switch (nAct) {
        case 4:                                   // Set To Slot
          *rgpci[iSlot()] = is.rgci[ici];
          is.iciCur = ici;
          if (iSlot() == 1)
            ciCore = is.rgci[ici];
          RecastAndRedrawQt();
          break;
        case 5:                                   // Copy From slot
          FAppendCIList(rgpci[iSlot()]);
          break;
        case 6:                                   // Edit Chart
          ShowChartInfoForQt(&is.rgci[ici], "Set Chart List Info");
          break;
        case 7:                                   // Delete Chart
          i2 = is.cci - 1 - ici;
          if (i2 > 0)
            CopyRgb((pbyte)&is.rgci[ici+1], (pbyte)&is.rgci[ici],
              i2*sizeof(CI));
          is.cci--;
          break;
        }
        refill(fFalse);
      });
  }

  RcWireOkCancelQt(&dlg, rgbuilt);
  PrepareDialogQt(&dlg);
  FocusDialogQt(PwRcFindQt(rgbuilt, "dbLi_sl"));
  if (dlg.exec() != QDialog::Accepted)
    return;

  // OK loads the highlighted chart into the chosen slot, as Windows does.
  int ici = iciSel();
  if (ici >= 0) {
    *rgpci[iSlot()] = is.rgci[ici];
    is.iciCur = ici;
    if (iSlot() == 1)
      ciCore = is.rgci[ici];
    RecastAndRedrawQt();
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
  if (FNoWriteQt())
    return;
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

  ShowRcRestrictQt(szTitleRestrict, rgctlRestrict, cctlRestrict,
    dxRestrict, dyRestrict, 0, dwarfHi, ignore,
    rgbut, (int)(sizeof(rgbut)/sizeof(RCRESBUT)));
}


void ShowStarRestrictDialogQt()
{
  CONST RCRESBUT rgbut[] = {
    {"dbSt_RU", 0, resSet,   starLo, starHi, NULL},
    {"dbSt_RU", 1, resClear, starLo, starHi, NULL} };

  ShowRcRestrictQt(szTitleStar, rgctlStar, cctlStar,
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
  QVector<RCBUILT> rgbuilt;
  char sz[cchSzMax];
  int nSavChar;

  dlg.setWindowTitle(szTitleDefault);
  RcBuildDialogQt(&dlg, rgctlDefault, cctlDefault, dxDefault, dyDefault,
    &rgbuilt);

  QComboBox *pcbDst = (QComboBox *)PwRcFindQt(rgbuilt, "dcDeDst");
  QComboBox *pcbZon = (QComboBox *)PwRcFindQt(rgbuilt, "dcDeZon");
  QComboBox *pcbCor = (QComboBox *)PwRcFindQt(rgbuilt, "dcDeCor");
  QComboBox *pcbLon = (QComboBox *)PwRcFindQt(rgbuilt, "dcDeLon");
  QComboBox *pcbLat = (QComboBox *)PwRcFindQt(rgbuilt, "dcDeLat");
  QComboBox *pcbElv = (QComboBox *)PwRcFindQt(rgbuilt, "dcDeElv");
  QComboBox *pcbTmp = (QComboBox *)PwRcFindQt(rgbuilt, "dcDeTmp");
  QLineEdit *peName = (QLineEdit *)PwRcFindQt(rgbuilt, "deDeNam");
  QLineEdit *peLoc = (QLineEdit *)PwRcFindQt(rgbuilt, "deDeLoc");
  QListWidget *plist = (QListWidget *)PwRcFindQt(rgbuilt, "dlIn");

  // Formatted the way Windows' DlgDefault does it (SetEditSZOA), not as
  // raw numbers -- see ShowChartInfoForQt() for the same treatment and
  // the reasoning about SzLocation()'s degree byte.
  FillComboQt(pcbDst, ciDefa.dst == 0.0 ? "No" :
    (ciDefa.dst == 1.0 ? "Yes" :
    (ciDefa.dst == dstAuto ? "Autodetect" : SzZone(ciDefa.dst))),
    RgstrDstQt());
  sprintf(sz, "%s", SzZone(ciDefa.zon));
  FillComboQt(pcbZon, sz[0] == '+' ? &sz[1] : sz, RgstrZoneQt());
  nSavChar = us.fAnsiChar; us.fAnsiChar = fFalse;
  sprintf(sz, "%s", SzLocation(ciDefa.lon, ciDefa.lat));
  us.fAnsiChar = nSavChar;
  sz[is.ichLocSplit] = chNull;
  FillComboQt(pcbLon, &sz[0], RgstrLonQt());
  FillComboQt(pcbLat, &sz[is.ichLocSplit+1], RgstrLatQt());
  FillComboQt(pcbElv, SzElevation(us.elvDef),
    QStringList() << "0m" << "1000ft");
  FillComboQt(pcbTmp, SzTemperature(us.tmpDef),
    QStringList() << "0C" << "32F");
  FillComboQt(pcbCor, QString::number(us.lTimeAddition),
    QStringList() << "60" << "0" << "-60");
  if (peName != NULL)
    peName->setText(FSzSet(ciDefa.nam) ? ciDefa.nam : "");
  if (peLoc != NULL)
    peLoc->setText(FSzSet(ciDefa.loc) ? ciDefa.loc : "");

#ifdef ATLAS
  // The atlas buttons, which this port didn't have at all.
  CI ciT = ciDefa;
  QPushButton *ppbCity = (QPushButton *)PwRcFindQt(rgbuilt, "dbInCity");
  QPushButton *ppbCoor = (QPushButton *)PwRcFindQt(rgbuilt, "dbInCoor");
  QPushButton *ppbChan = (QPushButton *)PwRcFindQt(rgbuilt, "dbInChan");
  QPushButton *ppbAppl = (QPushButton *)PwRcFindQt(rgbuilt, "dbInAppl");
  if (ppbCity != NULL)
    QObject::connect(ppbCity, &QPushButton::clicked, &dlg,
      [plist, peLoc, pcbLon, pcbLat, &ciT]() {
        RcAtlasRunQt(plist, 0, peLoc, pcbLon, pcbLat, &ciT); });
  if (ppbCoor != NULL)
    QObject::connect(ppbCoor, &QPushButton::clicked, &dlg,
      [plist, peLoc, pcbLon, pcbLat, &ciT]() {
        RcAtlasRunQt(plist, 1, peLoc, pcbLon, pcbLat, &ciT); });
  if (ppbChan != NULL)
    QObject::connect(ppbChan, &QPushButton::clicked, &dlg,
      [plist, peLoc, pcbLon, pcbLat, &ciT]() {
        RcAtlasRunQt(plist, 2, peLoc, pcbLon, pcbLat, &ciT); });
  if (ppbAppl != NULL)
    QObject::connect(ppbAppl, &QPushButton::clicked, &dlg,
      [plist, pcbLon, pcbLat, pcbZon, peLoc]() {
        RcAtlasApplyQt(plist, pcbLon, pcbLat, pcbZon, peLoc); });
#endif

  RcWireOkCancelQt(&dlg, rgbuilt);
  PrepareDialogQt(&dlg);
  FocusDialogQt(PwRcFindQt(rgbuilt, "dcDeDst"));
  if (dlg.exec() != QDialog::Accepted)
    return;

  CI ci = ciDefa;
  QByteArray ba;
  if (pcbDst != NULL) { ba = pcbDst->currentText().toLocal8Bit();
    ci.dst = RParseSz(ba.constData(), pmDst); }
  if (pcbZon != NULL) { ba = pcbZon->currentText().toLocal8Bit();
    ci.zon = RParseSz(ba.constData(), pmZon); }
  if (pcbLon != NULL) { ba = pcbLon->currentText().toLocal8Bit();
    ci.lon = RParseSz(ba.constData(), pmLon); }
  if (pcbLat != NULL) { ba = pcbLat->currentText().toLocal8Bit();
    ci.lat = RParseSz(ba.constData(), pmLat); }
  if (!FValidDst(ci.dst) || !FValidZon(ci.zon) ||
    !FValidLon(ci.lon) || !FValidLat(ci.lat)) {
    QMessageBox::warning(gi.qwind, szAppName,
      "One or more chart info fields are invalid.");
    return;
  }
  if (pcbElv != NULL) { ba = pcbElv->currentText().toLocal8Bit();
    us.elvDef = RParseSz(ba.constData(), pmElv); }
  if (pcbTmp != NULL) { ba = pcbTmp->currentText().toLocal8Bit();
    us.tmpDef = RParseSz(ba.constData(), pmTmp); }
  if (pcbCor != NULL)
    us.lTimeAddition = pcbCor->currentText().toLong();
  if (peName != NULL)
    FCloneSz(peName->text().toLocal8Bit().constData(), &ci.nam);
  if (peLoc != NULL)
    FCloneSz(peLoc->text().toLocal8Bit().constData(), &ci.loc);
  ciDefa = ci;
  RecastAndRedrawQt();
}


// Transits, equivalent to Windows' DlgTransit: which transit chart type
// to show (if any), the date/time to transit to (ciTran), how much time
// the search covers, which kinds of event the search reports, and a few
// display options. Location comes from the default chart info the same
// way Windows does it.

// Transits, transcribed from dlgTransit. The first radio group picks
// which kind of transit chart to cast, the second how far the search runs,
// and the six "ignore" boxes filter what a search reports.

void ShowTransitDialogQt()
{
  CONST RCFLAG rgflag[] = {
    {"dxTr_YR0_s", -1, &us.fIgnoreSign,   fFalse},
    {"dxTr_YR0_d", -1, &us.fIgnoreDir,    fFalse},
    {"dxTr_YR1_l", -1, &us.fIgnoreDiralt, fFalse},
    {"dxTr_YR1_d", -1, &us.fIgnoreDirlen, fFalse},
    {"dxTr_YR2_",   0, &us.fIgnoreAlt0,   fFalse},
    {"dxTr_YR2_d", -1, &us.fIgnoreDisequ, fFalse},
    {"dxTr_p",     -1, &is.fProgress,     fFalse},
    {"dxTr_r",     -1, &is.fReturn,       fFalse},
    {"dxTr_",       5, &us.fListAuto,     fFalse},
    {"dxTr_g",     -1, &us.fGraphAll,     fFalse} };
  QDialog dlg(gi.qwind);
  QVector<RCBUILT> rgbuilt;
  char sz[cchSzMax];
  int mon, day, yea, nty, nd, n1, n2;
  real tim, dst, zon;

  dlg.setWindowTitle(szTitleTransit);
  RcBuildDialogQt(&dlg, rgctlTransit, cctlTransit, dxTransit, dyTransit,
    &rgbuilt);
  RcLoadFlagsQt(rgbuilt, rgflag, CRcFlag(rgflag));

  n1 = us.fInDay ? 1 : (us.fInDayInf ? 2 : (us.fInDayGra ? 3 :
    (us.fTransit ? 4 : (us.fTransitInf ? 5 : (us.fTransitGra ? 6 : 0)))));
  RcLoadRadioQt(rgbuilt, 1, 7, n1);
  n2 = us.fInDayMonth + us.fInDayYear +
    us.fInDayYear*(NAbs(us.nEphemYears) > 1);
  RcLoadRadioQt(rgbuilt, 8, 4, n2);

  QComboBox *pcbMon = (QComboBox *)PwRcFindQt(rgbuilt, "dcTrMon");
  QComboBox *pcbDay = (QComboBox *)PwRcFindQt(rgbuilt, "dcTrDay");
  QComboBox *pcbYea = (QComboBox *)PwRcFindQt(rgbuilt, "dcTrYea");
  QComboBox *pcbTim = (QComboBox *)PwRcFindQt(rgbuilt, "dcTrTim");
  QComboBox *pcbDst = (QComboBox *)PwRcFindQt(rgbuilt, "dcTrDst");
  QComboBox *pcbZon = (QComboBox *)PwRcFindQt(rgbuilt, "dcTrZon");
  QLineEdit *peYears = (QLineEdit *)PwRcFindQt(rgbuilt, "deTr_tY");
  QLineEdit *peDiv = (QLineEdit *)PwRcFindQt(rgbuilt, "deTr_d");

  sprintf(sz, "%.3s", szMonth[FValidMon(MonT) ? MonT : 1]);
  FillComboQt(pcbMon, sz, RgstrMonthQt());
  FillComboQt(pcbDay, QString::number(DayT), RgstrDayQt());
  FillComboQt(pcbYea, QString::number(YeaT), RgstrYearQt());
  FillComboQt(pcbTim, SzTim(TimT), RgstrTimeQt());
  FillComboQt(pcbDst, DstT == 0.0 ? "No" : (DstT == 1.0 ? "Yes" :
    (DstT == dstAuto ? "Autodetect" : SzZone(DstT))), RgstrDstQt());
  sprintf(sz, "%s", SzZone(ZonT));
  FillComboQt(pcbZon, sz[0] == '+' ? &sz[1] : sz, RgstrZoneQt());
  if (peYears != NULL)
    peYears->setText(QString::number(us.nEphemYears));
  if (peDiv != NULL)
    peDiv->setText(QString::number(us.nDivision));

  QPushButton *ppbNow = (QPushButton *)PwRcFindQt(rgbuilt, "dbTr_tn");
  if (ppbNow != NULL)
    QObject::connect(ppbNow, &QPushButton::clicked, &dlg,
      [pcbMon, pcbDay, pcbYea, pcbTim]() {
#ifdef TIME
        char szN[cchSzMax];
        int monN, dayN, yeaN;
        real timN;
        GetTimeNow(&monN, &dayN, &yeaN, &timN, ciDefa.dst, ciDefa.zon);
        sprintf(szN, "%.3s", szMonth[FValidMon(monN) ? monN : 1]);
        if (pcbMon != NULL) pcbMon->setEditText(szN);
        if (pcbDay != NULL) pcbDay->setEditText(QString::number(dayN));
        if (pcbYea != NULL) pcbYea->setEditText(QString::number(yeaN));
        if (pcbTim != NULL) pcbTim->setEditText(SzTim(timN));
#endif
      });

  RcWireOkCancelQt(&dlg, rgbuilt);
  PrepareDialogQt(&dlg);
  FocusDialogQt(PwRcFindQt(rgbuilt, "dcTrMon"));
  if (dlg.exec() != QDialog::Accepted)
    return;

  mon = pcbMon != NULL ?
    NParseSz(pcbMon->currentText().toLocal8Bit().constData(), pmMon) : MonT;
  day = pcbDay != NULL ?
    NParseSz(pcbDay->currentText().toLocal8Bit().constData(), pmDay) : DayT;
  yea = pcbYea != NULL ?
    NParseSz(pcbYea->currentText().toLocal8Bit().constData(), pmYea) : YeaT;
  tim = pcbTim != NULL ?
    RParseSz(pcbTim->currentText().toLocal8Bit().constData(), pmTim) : TimT;
  dst = pcbDst != NULL ?
    RParseSz(pcbDst->currentText().toLocal8Bit().constData(), pmDst) : DstT;
  zon = pcbZon != NULL ?
    RParseSz(pcbZon->currentText().toLocal8Bit().constData(), pmZon) : ZonT;
  nty = peYears != NULL ? peYears->text().toInt() : us.nEphemYears;
  nd = peDiv != NULL ? peDiv->text().toInt() : us.nDivision;

  if (!FValidMon(mon))           { ErrorEnsureQt(&dlg, mon, "month"); return; }
  if (!FValidYea(yea))           { ErrorEnsureQt(&dlg, yea, "year"); return; }
  if (!FValidDay(day, mon, yea)) { ErrorEnsureQt(&dlg, day, "day"); return; }
  if (!FValidTim(tim))           { ErrorEnsureQt(&dlg, (int)tim, "time"); return; }
  if (!FValidDst(dst))           { ErrorEnsureQt(&dlg, (int)dst, "daylight saving"); return; }
  if (!FValidZon(zon))           { ErrorEnsureQt(&dlg, (int)zon, "time zone"); return; }
  if (!FValidDivision(nd))       { ErrorEnsureQt(&dlg, nd, "searching divisions"); return; }

  SetCI(ciTran, mon, day, yea, tim, dst, zon, ciDefa.lon, ciDefa.lat);
  us.nEphemYears = nty;
  RcStoreFlagsQt(rgbuilt, rgflag, CRcFlag(rgflag));
  us.nDivision = nd;

  n1 = NRcStoreRadioQt(rgbuilt, 1, 7, 0);
  n2 = NRcStoreRadioQt(rgbuilt, 8, 4, 0);
  us.fInDayMonth = (n2 >= 1);
  us.fInDayYear = us.fInDayMonth && n2 >= 2;
  if (n2 == 2 && NAbs(us.nEphemYears) > 1)
    us.nEphemYears = 0;

  // The chart type flags are set by switching to the mode the first radio
  // group chose, which is what routes through SetChartModeQt() here where
  // Windows sets wi.nMode and lets ProcessState() sort the flags out.
  switch (n1) {
  case 1: SetChartModeQt(gTraTraTim); break;
  case 2: SetChartModeQt(gTraTraInf); break;
  case 3: SetChartModeQt(gTraTraGra); break;
  case 4: SetChartModeQt(gTraNatTim); break;
  case 5: SetChartModeQt(gTraNatInf); break;
  case 6: SetChartModeQt(gTraNatGra); break;
  default:
    if (us.fInDay || us.fInDayInf || us.fInDayGra ||
      us.fTransit || us.fTransitInf || us.fTransitGra)
      SetChartModeQt(gWheel);
  }

  // Windows' tail (wdialog.cpp, DlgTransit's IDOK). Only the two "graph"
  // types are drawn; the rest are lists, and a list rendered as graphics
  // comes out as a wheel with nothing cast into it -- every object sitting
  // at 0 Aries. SetChartModeQt() turns graphics on, so this has to follow
  // it rather than precede it.
  if (n1 == 3 || n1 == 6)
    us.nEphemYears = (n2 <= 2 ? 1 : (nty <= 1 ? 5 : nty));
  else {
    if (n1 > 0)
      us.fGraphics = fFalse;
    if (n1 == 2)
      us.fProgress = is.fProgress;
  }
  SyncGraphicsMenuQt();
  RecastAndRedrawQt();
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

// Progressions, transcribed from dlgProgress.

void ShowProgressDialogQt()
{
  QDialog dlg(gi.qwind);
  QVector<RCBUILT> rgbuilt;
  char sz[cchSzMax], szT[cchSzDef];
  int npO, mon, day, yea, i;
  real tim, dst, zon, rd, rC;

  dlg.setWindowTitle(szTitleProgress);
  RcBuildDialogQt(&dlg, rgctlProgress, cctlProgress, dxProgress, dyProgress,
    &rgbuilt);

  QCheckBox *pcbOn = (QCheckBox *)PwRcFindQt(rgbuilt, "dxPr_p");
  QCheckBox *pcbRAMC = (QCheckBox *)PwRcFindQt(rgbuilt, "dxPr_pc");
  QComboBox *pcbRate = (QComboBox *)PwRcFindQt(rgbuilt, "dcPr_pd");
  QComboBox *pcbCusp = (QComboBox *)PwRcFindQt(rgbuilt, "dcPr_pC");
  QLineEdit *peArc = (QLineEdit *)PwRcFindQt(rgbuilt, "dePr_pO");
  QComboBox *pcbMon = (QComboBox *)PwRcFindQt(rgbuilt, "dcPrMon");
  QComboBox *pcbDay = (QComboBox *)PwRcFindQt(rgbuilt, "dcPrDay");
  QComboBox *pcbYea = (QComboBox *)PwRcFindQt(rgbuilt, "dcPrYea");
  QComboBox *pcbTim = (QComboBox *)PwRcFindQt(rgbuilt, "dcPrTim");
  QComboBox *pcbDst = (QComboBox *)PwRcFindQt(rgbuilt, "dcPrDst");
  QComboBox *pcbZon = (QComboBox *)PwRcFindQt(rgbuilt, "dcPrZon");

  if (pcbOn != NULL)
    pcbOn->setChecked(us.fProgress != 0);
  if (pcbRAMC != NULL)
    pcbRAMC->setChecked(us.fProgRAMC != 0);
  RcLoadRadioQt(rgbuilt, 1, 3, us.nProgress == ptCast ? 0 :
    (us.nProgress == ptMixed ? 1 : 2));

  // The rate and cusp lists offer named presets with their values, while
  // the field itself takes a number, as Windows fills them.
  if (pcbRate != NULL) {
    QStringList rgstr;
    QString strCur = SzFormatRQt(us.rProgDay, -6);
    for (i = 0; i < 4; i++) {
      FormatR(szT, rgrProgQt[i], -6);
      sprintf(sz, "%s %s", szT, rgszProgQt[i]);
      rgstr << sz;
      if (us.rProgDay == rgrProgQt[i])
        strCur = sz;
    }
    FillComboQt(pcbRate, strCur, rgstr);
  }
  if (pcbCusp != NULL) {
    QStringList rgstr;
    QString strCur = SzFormatRQt(us.rProgCusp, -6);
    for (i = 0; i < 2; i++) {
      FormatR(szT, rgrProgCuspQt[i], -6);
      sprintf(sz, "%s %s", szT, rgszProgCuspQt[i]);
      rgstr << sz;
      if (us.rProgCusp == rgrProgCuspQt[i])
        strCur = sz;
    }
    FillComboQt(pcbCusp, strCur, rgstr);
  }
  if (peArc != NULL)
    peArc->setText(us.objProgArc >= 0 ? szObjName[us.objProgArc] : "None");

  sprintf(sz, "%.3s", szMonth[FValidMon(MonT) ? MonT : 1]);
  FillComboQt(pcbMon, sz, RgstrMonthQt());
  FillComboQt(pcbDay, QString::number(DayT), RgstrDayQt());
  FillComboQt(pcbYea, QString::number(YeaT), RgstrYearQt());
  FillComboQt(pcbTim, SzTim(TimT), RgstrTimeQt());
  FillComboQt(pcbDst, DstT == 0.0 ? "No" : (DstT == 1.0 ? "Yes" :
    (DstT == dstAuto ? "Autodetect" : SzZone(DstT))), RgstrDstQt());
  sprintf(sz, "%s", SzZone(ZonT));
  FillComboQt(pcbZon, sz[0] == '+' ? &sz[1] : sz, RgstrZoneQt());

  // "Now" fills the date and time with the current moment.
  QPushButton *ppbNow = (QPushButton *)PwRcFindQt(rgbuilt, "dbPr_pn");
  if (ppbNow != NULL)
    QObject::connect(ppbNow, &QPushButton::clicked, &dlg,
      [pcbMon, pcbDay, pcbYea, pcbTim]() {
#ifdef TIME
        char szN[cchSzMax];
        int monN, dayN, yeaN;
        real timN;
        GetTimeNow(&monN, &dayN, &yeaN, &timN, ciDefa.dst, ciDefa.zon);
        sprintf(szN, "%.3s", szMonth[FValidMon(monN) ? monN : 1]);
        if (pcbMon != NULL) pcbMon->setEditText(szN);
        if (pcbDay != NULL) pcbDay->setEditText(QString::number(dayN));
        if (pcbYea != NULL) pcbYea->setEditText(QString::number(yeaN));
        if (pcbTim != NULL) pcbTim->setEditText(SzTim(timN));
#endif
      });

  RcWireOkCancelQt(&dlg, rgbuilt);
  PrepareDialogQt(&dlg);
  FocusDialogQt(PwRcFindQt(rgbuilt, "dcPrMon"));
  if (dlg.exec() != QDialog::Accepted)
    return;

  // A leading "X" on the rate means "this many years per day", inverted.
  rd = us.rProgDay;
  if (pcbRate != NULL) {
    sprintf(sz, "%.*s", cchSzMax-1,
      pcbRate->currentText().toLocal8Bit().constData());
    i = (ChCap(sz[0]) == 'X');
    rd = RFromSz(sz + i);
    if (i != 0 && rd != 0.0)
      rd = rDayInYear / rd;
  }
  rC = pcbCusp != NULL ?
    RFromSz(pcbCusp->currentText().toLocal8Bit().constData()) : us.rProgCusp;
  npO = us.objProgArc;
  if (peArc != NULL) {
    sprintf(sz, "%.*s", cchSzMax-1,
      peArc->text().toLocal8Bit().constData());
    npO = NParseSz(sz, pmObject);
  }
  mon = pcbMon != NULL ?
    NParseSz(pcbMon->currentText().toLocal8Bit().constData(), pmMon) : MonT;
  day = pcbDay != NULL ?
    NParseSz(pcbDay->currentText().toLocal8Bit().constData(), pmDay) : DayT;
  yea = pcbYea != NULL ?
    NParseSz(pcbYea->currentText().toLocal8Bit().constData(), pmYea) : YeaT;
  tim = pcbTim != NULL ?
    RParseSz(pcbTim->currentText().toLocal8Bit().constData(), pmTim) : TimT;
  dst = pcbDst != NULL ?
    RParseSz(pcbDst->currentText().toLocal8Bit().constData(), pmDst) : DstT;
  zon = pcbZon != NULL ?
    RParseSz(pcbZon->currentText().toLocal8Bit().constData(), pmZon) : ZonT;

  if (rd == 0.0)                { ErrorEnsureQt(&dlg, 0, "degree per day"); return; }
  if (rC == 0.0)                { ErrorEnsureQt(&dlg, 0, "cusp move ratio"); return; }
  if (!FValidProgArc(npO))      { ErrorEnsureQt(&dlg, npO, "solar arc planet"); return; }
  if (!FValidMon(mon))          { ErrorEnsureQt(&dlg, mon, "month"); return; }
  if (!FValidYea(yea))          { ErrorEnsureQt(&dlg, yea, "year"); return; }
  if (!FValidDay(day, mon, yea)) { ErrorEnsureQt(&dlg, day, "day"); return; }
  if (!FValidTim(tim))          { ErrorEnsureQt(&dlg, (int)tim, "time"); return; }
  if (!FValidDst(dst))          { ErrorEnsureQt(&dlg, (int)dst, "daylight saving"); return; }
  if (!FValidZon(zon))          { ErrorEnsureQt(&dlg, (int)zon, "time zone"); return; }

  if (pcbOn != NULL)
    us.fProgress = pcbOn->isChecked();
  us.nProgress = NRcStoreRadioQt(rgbuilt, 1, 3, 0) == 0 ? ptCast :
    (NRcStoreRadioQt(rgbuilt, 1, 3, 0) == 1 ? ptMixed : ptSolarArc);
  us.rProgDay = rd;
  us.rProgCusp = rC;
  us.objProgArc = npO;
  if (pcbRAMC != NULL)
    us.fProgRAMC = pcbRAMC->isChecked();
  SetCI(ciTran, mon, day, yea, tim, dst, zon, ciDefa.lon, ciDefa.lat);
  is.JDp = MdytszToJulian(MonT, DayT, YeaT, TimT, ciDefa.dst, ciDefa.zon);
  SyncProgressMenuQt();
  RecastAndRedrawQt();
}


// Chart settings, equivalent to Windows' DlgChart: per chart type display
// options, sort orders, and a few counts.

// Aspect list sort orders. Windows keeps this in wdialog.cpp, which isn't
// compiled into the QT build.
static CONST char *rgszSortQt[asMax] = {"Power", "Orb Magnitude",
  "Orb Value", "1st Object Index", "2nd Object Index",
  "Aspect", "1st Object Position", "2nd Object Position", "Midpoint"};

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

  dlg.setWindowTitle(szTitleChart);
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
  QVector<RCBUILT> rgbuilt;

  dlg.setWindowTitle(szTitleCommand);
  RcBuildDialogQt(&dlg, rgctlCommand, cctlCommand, dxCommand, dyCommand,
    &rgbuilt);
  QLineEdit *peLine = (QLineEdit *)PwRcFindQt(rgbuilt, "deCo");
  // Windows' dxCo_e, applied before running the switches so a line can be
  // tried with expressions off. The box reads the inverse of the flag.
  QCheckBox *pcbExp = (QCheckBox *)PwRcFindQt(rgbuilt, "dxCo_e");
  if (pcbExp != NULL)
    pcbExp->setChecked(!us.fExpOff);
  RcWireOkCancelQt(&dlg, rgbuilt);
  PrepareDialogQt(&dlg);
  FocusDialogQt(PwRcFindQt(rgbuilt, "deCo"));
  if (dlg.exec() != QDialog::Accepted || peLine == NULL ||
    peLine->text().trimmed().isEmpty())
    return;

  if (pcbExp != NULL)
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

  dlg.setWindowTitle(szTitleAspect);
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

  dlg.setWindowTitle(szTitleColor);
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

  dlg.setWindowTitle(szTitleObject);
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
      rgpeOrb[i]->setText(SzFormatRQt(rgobjset[i].orb, -2));
    if (rgpeAdd[i] != NULL)
      rgpeAdd[i]->setText(SzFormatRQt(rgobjset[i].add, -1));
    if (rgpeInf[i] != NULL)
      rgpeInf[i]->setText(SzFormatRQt(rgobjset[i].inf, -2));
    FillColorComboQt(rgpcbColor[i], rgobjset[i].kolor, 1);
  }
  RcWireOkCancelQt(&dlg, rgbuilt);
  PrepareDialogQt(&dlg);
  if (dlg.exec() != QDialog::Accepted)
    return;

  for (i = 0; i <= oCore; i++) {
    if (rgpeOrb[i] == NULL)
      continue;
    rgobjset[i].orb = rgpeOrb[i]->text().toDouble();
    rgobjset[i].add = rgpeAdd[i]->text().toDouble();
    rgobjset[i].inf = rgpeInf[i]->text().toDouble();
    rgobjset[i].kolor = NColorFromComboQt(rgpcbColor[i]);
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

  dlg.setWindowTitle(szTitleObject2);
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
      peOrb->setText(SzFormatRQt(rgobjset[i].orb, -2));
    if (peAdd != NULL)
      peAdd->setText(SzFormatRQt(rgobjset[i].add, -1));
    if (peInf != NULL)
      peInf->setText(SzFormatRQt(rgobjset[i].inf, -2));
    // Windows widens the color list by one on the collective stars row.
    FillColorComboQt(pcbColor, rgobjset[i].kolor, 1 + (i == starLo));
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
    rgobjset[i].orb = rgpeOrb[j]->text().toDouble();
    rgobjset[i].add = rgpeAdd[j]->text().toDouble();
    rgobjset[i].inf = rgpeInf[j]->text().toDouble();
    rgobjset[i].kolor = NColorFromComboQt(rgpcbColor[j]);
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

  dlg.setWindowTitle(szTitleCalc);
  RcBuildDialogQt(&dlg, rgctlCalc, cctlCalc, dxCalc, dyCalc, &rgbuilt);
  RcLoadFlagsQt(rgbuilt, rgflag, CRcFlag(rgflag));

  // Ephemeris, ayanamsa and house system are editable combos, as on
  // Windows, so a value can be typed as well as picked.
  QComboBox *pcbEphem = (QComboBox *)PwRcFindQt(rgbuilt, "dcSe_b");
  QComboBox *pcbAyan = (QComboBox *)PwRcFindQt(rgbuilt, "dcSe_s");
  QComboBox *pcbHouse = (QComboBox *)PwRcFindQt(rgbuilt, "dcSe_c");
  if (pcbEphem != NULL) {
    pcbEphem->setEditable(fTrue);
    // Built in Windows' order and with Windows' omissions, rather than by
    // running the cm* constants in numeric order: JPL Web sits after the
    // three Swiss entries there, not last, and an ephemeris the user has
    // switched off is left out entirely -- no JPL Web under "-0n", no
    // Placalc or Matrix under "-0p". Offering one that is disabled invites
    // a lookup that cannot happen. The selection is read back by name
    // below rather than by index, so a shorter list needs nothing else.
#ifdef SWISS
    pcbEphem->addItem(szEphem[cmSwiss]);
    pcbEphem->addItem(szEphem[cmMoshier]);
    pcbEphem->addItem(szEphem[cmJPL]);
#endif
#ifdef JPLWEB
    if (!us.fNoNetwork)
      pcbEphem->addItem(szEphem[cmJPLWeb]);
#endif
#ifdef PLACALC
    if (!us.fNoPlacalc)
      pcbEphem->addItem(szEphem[cmPlacalc]);
#endif
#ifdef MATRIX
    if (!us.fNoPlacalc)
      pcbEphem->addItem(szEphem[cmMatrix]);
#endif
    pcbEphem->addItem(szEphem[cmNone]);
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

  dlg.setWindowTitle(szTitleDisplay);
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

  ShowRcRestrictQt(szTitleMoons, rgctlMoons, cctlMoons,
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

  dlg.setWindowTitle(szTitleObjectM);
  RcBuildDialogQt(&dlg, rgctlObjectM, cctlObjectM, dxObjectM, dyObjectM,
    &rgbuilt);
  for (i = moonsLo; i <= cobHi; i++) {
    j = i - moonsLo;
    QLineEdit *peOrb = (QLineEdit *)PwRcFindIdxQt(rgbuilt, "deo", j+1);
    QLineEdit *peAdd = (QLineEdit *)PwRcFindIdxQt(rgbuilt, "dea", j+1);
    QLineEdit *peInf = (QLineEdit *)PwRcFindIdxQt(rgbuilt, "dei", j+1);
    QComboBox *pcbColor = (QComboBox *)PwRcFindIdxQt(rgbuilt, "dck", j);
    if (peOrb != NULL)
      peOrb->setText(SzFormatRQt(rgobjset[i].orb, -2));
    if (peAdd != NULL)
      peAdd->setText(SzFormatRQt(rgobjset[i].add, -1));
    if (peInf != NULL)
      peInf->setText(SzFormatRQt(rgobjset[i].inf, -2));
    FillColorComboQt(pcbColor, rgobjset[i].kolor, 3);
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
    rgobjset[i].orb = rgpeOrb[j]->text().toDouble();
    rgobjset[i].add = rgpeAdd[j]->text().toDouble();
    rgobjset[i].inf = rgpeInf[j]->text().toDouble();
    rgobjset[i].kolor = NColorFromComboQt(rgpcbColor[j]);
  }
  RecastAndRedrawQt();
}


// Split one Object Customization definition string ("h120", "Mar",
// "2 n", "j2 nHS") into the four values Astrolog keeps for it. Windows
// open-codes this parse twice inside DlgCustom; it is wanted in three
// places here -- validate, apply, and Lookup Names -- so it lives in one
// function rather than being copied a third and fourth time.

// Parse one Object Customization definition string into an OBJDEF. The
// parse itself is FObjDefParse() in calc.cpp -- this used to be a third
// copy of it, and the copies had drifted: this one lacked the
// FObjSelFlagRun() guard, so a definition ending in a body's name would
// read the name's letters as point and flag markers. An empty field now
// comes back as an invalid object rather than quietly parsing as object
// zero, so the dialog's own validation message fires instead.
static void ParseCustomDefQt(CONST QString &str, OBJDEF *pod)
{
  QByteArray ba = str.toLocal8Bit();

  if (!FObjDefParse(ba.constData(), pod)) {
    pod->nTyp = 2; pod->nObj = -1;
    pod->nPnt = pod->nFlg = 0;
  }
}


// "Lookup Names" (dbCu_l in Windows' DlgCustom): fill in every display
// name still left blank or "???" by resolving its definition string to
// the canonical name. Names already filled in are left alone.

static void LookupCustomNamesQt(QVector<QLineEdit *> &rgpeName,
  QVector<QLineEdit *> &rgpeDef)
{
  char sz[cchSzMax];
  OBJDEF od;
  int i;

  for (i = 0; i < rgpeName.size(); i++) {
    QString strName = rgpeName[i]->text();
    if (!strName.isEmpty() && strName != szObjUnknown)
      continue;
    ParseCustomDefQt(rgpeDef[i]->text(), &od);
    // SzObjSelName() covers every definition type now, the JPL Horizons
    // web query included, and remembers the name it answers -- so a name
    // this button shows is a name the program accepts back.
    SzObjSelName(sz, od.nTyp, od.nObj);
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
  OBJDEF od;
  int i, j;
  char sz[cchSzMax];

  dlg.setWindowTitle(szTitleCustom);
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

    // The definition formatter lives in SzObjDefFormat() now; this used
    // to be its second open-coded copy.
    ObjDefGet(custLo + j, &od);
    SzObjDefFormat(sz, &od);
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
    ParseCustomDefQt(rgpeDef[j]->text(), &od);
    if (!FValidCustom(od.nObj, od.nTyp)) {
      QMessageBox::warning(gi.qwind, szAppName,
        "One or more object definitions are invalid.");
      return;
    }
  }

  for (i = custLo; i <= custHi; i++) {
    j = i - custLo;
    ParseCustomDefQt(rgpeDef[j]->text(), &od);
    // The store and the glyph rule are ObjDefSet()'s.
    ObjDefSet(i, &od);

    QByteArray baName = rgpeName[j]->text().toLocal8Bit();
    SetObjDisp(i, baName.constData());
  }
  RecastAndRedrawQt();
}


// Object selections, this port's half of Windows' DlgObjectSel. Both are
// built from the same dlgObjectSel block in astrolog.rc, and both go
// through the shared SzObjSelDef()/SzObjSelName()/FObjSelParse() in
// calc.cpp, so the two builds cannot drift on what a field means.
//
// Every write below is bounded to uranLo..dwarfHi, and nothing is cleared
// wholesale. force[] and ignore[] are full range globals and this dialog
// shows a slice of them, so a forced midpoint on some object it does not
// display -- the Part of Fortune, say -- has to come through untouched.

void ShowObjectSelDialogQt()
{
  QDialog dlg(gi.qwind);
  QVector<RCBUILT> rgbuilt;
  QVector<QCheckBox *> rgpcbShow;
  QVector<QComboBox *> rgpcbDef;
  QVector<QLineEdit *> rgpeName;
  QVector<QString> rgstrName0;
  real rgforceSav[cObjSelRow];
  int rgTypSwissSav[cObjSelRow], rgObjSwissSav[cObjSelRow];
  // One OBJDEF a row rather than four arrays that have to stay aligned.
  OBJDEF rgod[cObjSelRow];
  real rgforce[cObjSelRow];
  char sz[cchSzMax];
  int i, j, k, iobj;

  dlg.setWindowTitle(szTitleObjectSel);
  RcBuildDialogQt(&dlg, rgctlObjectSel, cctlObjectSel, dxObjectSel,
    dyObjectSel, &rgbuilt);

  for (i = 0; i < cObjSelRow; i++) {
    iobj = uranLo + i;
    QCheckBox *pcbShow = (QCheckBox *)PwRcFindIdxQt(rgbuilt, "dxOs", i+1);
    QComboBox *pcbDef = (QComboBox *)PwRcFindIdxQt(rgbuilt, "dcOs", i+1);
    QLineEdit *peName = (QLineEdit *)PwRcFindIdxQt(rgbuilt, "deOs", i+1);
    rgpcbShow.append(pcbShow); rgpcbDef.append(pcbDef);
    rgpeName.append(peName);

    if (pcbShow != NULL)
      pcbShow->setChecked(!ignore[iobj]);
    if (peName != NULL)
      peName->setText(szObjDisp[iobj]);
    // Remember it, so OK can tell a name the user typed from one that is
    // simply the old body's name sitting there untouched.
    rgstrName0.append(peName != NULL ? peName->text() : QString());

    // Every addItem() has to precede setEditText(): adding the first item
    // resets the current index and silently overwrites the line edit.
    if (pcbDef != NULL) {
      pcbDef->setEditable(fTrue);
      for (j = 0; j < cObjSel; j++)
        pcbDef->addItem(rgObjSel[j].szName);
      // A slot forced to a midpoint shows it as A/B, which is both what
      // the user typed and what an astrologer would write. Otherwise the
      // field shows the body, as before.
      if (FForceMid(force[iobj])) {
        sprintf(sz, "%s/%s", szObjDisp[ObjForceMid1(force[iobj])],
          szObjDisp[ObjForceMid2(force[iobj])]);
      } else
        SzObjSelDef(sz, iobj);
      pcbDef->setEditText(sz);
    }
  }

  QPushButton *ppbLookup = (QPushButton *)PwRcFindQt(rgbuilt, "dbOs_l");
  if (ppbLookup != NULL)
    QObject::connect(ppbLookup, &QPushButton::clicked, &dlg,
      [&rgpeName, &rgpcbDef]() {
      char szT[cchSzMax];
      OBJDEF od2;
      int i2, j2, k2;

      for (i2 = 0; i2 < cObjSelRow; i2++) {
        if (rgpeName[i2] == NULL || rgpcbDef[i2] == NULL)
          continue;
        // Every row, not just the blank ones. Windows' Custom Objects
        // dialog only fills a name that is empty or "???", because there
        // the name column is the user's own label. Here the button's
        // whole point is turning what is in the definition box into a
        // name -- type 52872, press it, get Okyrhoe -- so it resolves
        // every row and overwrites. Skipping the ones that already had a
        // name made it look like the button did nothing at all.
        QByteArray baDef = rgpcbDef[i2]->currentText().toLocal8Bit();

        // A midpoint names itself after its two halves; -Fm moves a
        // position but never touches a name.
        if (FObjSelMidPair(baDef.constData(), &j2, &k2)) {
          if (FItem(j2) && FItem(k2))
            sprintf(szT, "%.3s/%.3s", szObjDisp[j2], szObjDisp[k2]);
          else
            sprintf(szT, "%s", szObjUnknown);
          rgpeName[i2]->setText(szT);
          continue;
        }

        // Otherwise ask the ephemeris, and fall back to the name the
        // offered-body list already knows. Without that fallback a body
        // whose .se1 file isn't installed comes back "???" even though
        // the Contains field beside it is displaying its name.
        if (FObjSelParse(baDef.constData(), &od2)) {
          SzObjSelName(szT, od2.nTyp, od2.nObj);
          if (FEqSz(szT, szObjUnknown))
            for (int iSel = 0; iSel < cObjSel; iSel++)
              if (rgObjSel[iSel].nTyp == od2.nTyp &&
                rgObjSel[iSel].nObj == od2.nObj) {
                sprintf(szT, "%s", rgObjSel[iSel].szName);
                break;
              }
          // Put the number and the name in the body field together, so a
          // row that has been looked up stops reading as a bare catalogue
          // number: "10199" becomes "10199 Chariklo". Only for a plain
          // MPC number with nothing else on it -- the other definition
          // forms have no number to pair a name with, and a point or flag
          // suffix already occupies the space after it. FObjSelParse()
          // reads the pair back, since a trailing run is only taken for
          // flags when every letter in it is one.
          if (od2.nTyp == 1 && od2.nPnt <= 0 && od2.nFlg <= 0 &&
            !FEqSz(szT, szObjUnknown)) {
            char szD[cchSzMax];
            sprintf(szD, "%d %s", od2.nObj, szT);
            rgpcbDef[i2]->setEditText(szD);
          }
        } else
          sprintf(szT, "%s", szObjUnknown);
        rgpeName[i2]->setText(szT);
      }
    });

  RcWireOkCancelQt(&dlg, rgbuilt);
  PrepareDialogQt(&dlg);
  FocusDialogQt(PwRcFindIdxQt(rgbuilt, "dcOs", 1));
  if (dlg.exec() != QDialog::Accepted)
    return;

  // Validate every row before writing any of it, so a bad entry leaves the
  // previous settings alone rather than applying half of them.
  for (i = 0; i < cObjSelRow; i++) {
    if (rgpcbDef[i] == NULL)
      continue;
    QByteArray ba = rgpcbDef[i]->currentText().toLocal8Bit();

    // A slash means a midpoint, and the slot keeps whatever body it had:
    // -Fm moves a position, it does not change what the object is.
    rgforce[i] = 0.0;
    if (FObjSelMidPair(ba.constData(), &j, &k)) {
      if (!FItem(j) || !FItem(k)) {
        QMessageBox::warning(gi.qwind, szAppName,
          "A midpoint needs two objects this chart has, written as "
          "\"Sun/Moo\" or \"7066/90482\".");
        return;
      }
      rgforce[i] = ForceMid(j, k);
      rgod[i].nTyp = rgTypSwiss[uranLo + i - custLo];
      rgod[i].nObj = rgObjSwiss[uranLo + i - custLo];
      rgod[i].nPnt = rgPntSwiss[uranLo + i - custLo];
      rgod[i].nFlg = rgFlgSwiss[uranLo + i - custLo];
      continue;
    }

    if (!FObjSelParse(ba.constData(), &rgod[i]) ||
      !FValidCustom(rgod[i].nObj, rgod[i].nTyp)) {
      QMessageBox::warning(gi.qwind, szAppName,
        "One or more object definitions are invalid.");
      return;
    }
  }

  for (i = 0; i < cObjSelRow; i++) {
    iobj = uranLo + i;
    rgTypSwissSav[i] = rgTypSwiss[iobj - custLo];
    rgObjSwissSav[i] = rgObjSwiss[iobj - custLo];
    rgforceSav[i] = force[iobj];
    // The store and the glyph rule are ObjDefSet()'s.
    ObjDefSet(iobj, &rgod[i]);
    force[iobj] = rgforce[i];
    if (rgpcbShow[i] != NULL)
      ignore[iobj] = !rgpcbShow[i]->isChecked();
    if (rgpeName[i] != NULL) {
      QString str = rgpeName[i]->text();
      // A name the user did not touch is the *old* body's name, and
      // leaving it there is what made picking a new body look like it did
      // nothing: the slot really did change -- a different position, a
      // different ephemeris number -- while every label in the chart still
      // read the body it used to be. So when the definition changed and
      // the name was not edited by hand, the name follows the definition.
      // A name the user did type is theirs and is kept.
      if (i < rgstrName0.size() && str == rgstrName0[i]) {
        char szT[cchSzDef];
        int j2, k2;
        if (rgforce[i] != rgforceSav[i] && FForceMid(rgforce[i])) {
          // Forced to a midpoint: name it after its two halves, the same
          // way Lookup Names does. Left alone, the slot sits at the
          // midpoint under the name of the body it used to be, which is
          // the same confusion as picking a new body and keeping the old
          // name.
          j2 = ObjForceMid1(rgforce[i]); k2 = ObjForceMid2(rgforce[i]);
          if (FItem(j2) && FItem(k2)) {
            sprintf(szT, "%.3s/%.3s", szObjDisp[j2], szObjDisp[k2]);
            str = QString(szT);
          }
        } else if (rgod[i].nTyp != rgTypSwissSav[i] ||
          rgod[i].nObj != rgObjSwissSav[i]) {
          SzObjSelName(szT, rgod[i].nTyp, rgod[i].nObj);
          if (!FEqSz(szT, szObjUnknown))
            str = QString(szT);
        }
      }
      QByteArray ba = str.toLocal8Bit();
      SetObjDisp(iobj, ba.constData());
    }
  }

  // The Show boxes just changed which categories have anything in them,
  // so re-derive us.fUranian/us.fDwarf and their menu checks the way the
  // restriction dialogs do.
  AdjustRestrictions();
  SyncRestrictMenuQt();
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

  dlg.setWindowTitle(szTitleCustomS);
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
    SetObjDisp(i, baName.constData());
    QByteArray baDef = rgpeDef[row]->text().toLocal8Bit();
    FCloneSz(FEqSz(baDef.constData(),
      *szStarNameSwiss[k] ? szStarNameSwiss[k] : szObjName[i]) ?
      NULL : baDef.constData(), &szStarCustom[k]);
  }
  RecastAndRedrawQt();
}

#endif // QT

/* qtdialog.cpp */
