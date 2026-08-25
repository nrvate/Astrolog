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
#include <QtWidgets/QLabel>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QScrollArea>
#include <QtCore/QVector>

#include "astrolog.h"
#include "qtdriver.h"

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


// Chart info entry, equivalent to Windows' DlgInfo. This is the dialog that
// lets someone actually create a chart interactively instead of only ever
// loading one from disk.

void ShowChartInfoDialogQt()
{
  QDialog dlg(gi.qwind);
  dlg.setWindowTitle("Chart Info");
  QFormLayout *playout = new QFormLayout(&dlg);

  QLineEdit *peName = new QLineEdit(FSzSet(ciCore.nam) ? ciCore.nam : "");
  QLineEdit *peLoc  = new QLineEdit(FSzSet(ciCore.loc) ? ciCore.loc : "");
  QLineEdit *peMon  = new QLineEdit(QString::number(ciCore.mon));
  QLineEdit *peDay  = new QLineEdit(QString::number(ciCore.day));
  QLineEdit *peYea  = new QLineEdit(QString::number(ciCore.yea));
  QLineEdit *peTim  = new QLineEdit(QString::number(ciCore.tim));
  // Daylight offset has special sentinel values (see astrolog.h's dstAuto);
  // show and accept them as the same "ST"/"DT"/"Autodetect" text the rest
  // of Astrolog uses (e.g. when saving a chart file), not a raw number.
  QLineEdit *peDst  = new QLineEdit(ciCore.dst == 0.0 ? "ST" :
    (ciCore.dst == 1.0 ? "DT" :
    (ciCore.dst == dstAuto ? "Autodetect" : SzZone(ciCore.dst))));
  QLineEdit *peZon  = new QLineEdit(QString::number(ciCore.zon));
  QLineEdit *peLon  = new QLineEdit(QString::number(ciCore.lon));
  QLineEdit *peLat  = new QLineEdit(QString::number(ciCore.lat));
  // Long names/locations otherwise show their tail end, not their start.
  peName->setCursorPosition(0);
  peLoc->setCursorPosition(0);

  playout->addRow("Name:", peName);
  playout->addRow("Location:", peLoc);
  playout->addRow("Month (1-12):", peMon);
  playout->addRow("Day:", peDay);
  playout->addRow("Year:", peYea);
  playout->addRow("Time (decimal hours):", peTim);
  playout->addRow("Daylight offset (hours):", peDst);
  playout->addRow("Zone (hours west of UTC):", peZon);
  playout->addRow("Longitude (degrees east):", peLon);
  playout->addRow("Latitude (degrees north):", peLat);

  QDialogButtonBox *pbuttons =
    new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  playout->addRow(pbuttons);
  QObject::connect(pbuttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(pbuttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  if (dlg.exec() != QDialog::Accepted)
    return;

  CI ci = ciCore;
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

  ciCore = ci;
  RecastAndRedrawQt();
}


// Colors, a curated subset of Windows' DlgColor covering the main palette
// and element colors (the ones most visibly used in every chart), letting
// each be set by color name (e.g. "Red") the same way SzColor()/NParseSz()
// format and parse them elsewhere in Astrolog.

void ShowColorDialogQt()
{
  static CONST char *rgszMain[9] = { "Black", "White", "Light Gray",
    "Dark Gray", "Maroon", "Dark Green", "Dark Cyan", "Dark Blue",
    "Magenta" };
  static CONST char *rgszElem[4] = { "Fire", "Earth", "Air", "Water" };
  QDialog dlg(gi.qwind);
  dlg.setWindowTitle("Colors");
  QFormLayout *playout = new QFormLayout(&dlg);
  QLineEdit *rgpeMain[9];
  QLineEdit *rgpeElem[4];
  int i;

  for (i = 0; i < 9; i++) {
    rgpeMain[i] = new QLineEdit(SzColor(kMainA[i]));
    playout->addRow(QString(rgszMain[i]) + ":", rgpeMain[i]);
  }
  for (i = 0; i < cElem; i++) {
    rgpeElem[i] = new QLineEdit(SzColor(kElemA[i]));
    playout->addRow(QString(rgszElem[i]) + ":", rgpeElem[i]);
  }

  QDialogButtonBox *pbuttons =
    new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  playout->addRow(pbuttons);
  QObject::connect(pbuttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(pbuttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  if (dlg.exec() != QDialog::Accepted)
    return;

  for (i = 0; i < 9; i++) {
    QByteArray ba = rgpeMain[i]->text().toLocal8Bit();
    kMainA[i] = NParseSz(ba.constData(), pmColor);
  }
  for (i = 0; i < cElem; i++) {
    QByteArray ba = rgpeElem[i]->text().toLocal8Bit();
    kElemA[i] = NParseSz(ba.constData(), pmColor);
  }
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
  QLineEdit *peDst  = new QLineEdit(ciDefa.dst == 0.0 ? "ST" :
    (ciDefa.dst == 1.0 ? "DT" :
    (ciDefa.dst == dstAuto ? "Autodetect" : SzZone(ciDefa.dst))));
  QLineEdit *peZon  = new QLineEdit(QString::number(ciDefa.zon));
  QLineEdit *peLon  = new QLineEdit(QString::number(ciDefa.lon));
  QLineEdit *peLat  = new QLineEdit(QString::number(ciDefa.lat));
  QLineEdit *peElv  = new QLineEdit(SzElevation(us.elvDef));
  QLineEdit *peTmp  = new QLineEdit(SzTemperature(us.tmpDef));
  QLineEdit *peCor  = new QLineEdit(QString::number(us.lTimeAddition));
  peName->setCursorPosition(0);
  peLoc->setCursorPosition(0);

  playout->addRow("Name:", peName);
  playout->addRow("Location:", peLoc);
  playout->addRow("Daylight offset:", peDst);
  playout->addRow("Zone (hours west of UTC):", peZon);
  playout->addRow("Longitude (degrees east):", peLon);
  playout->addRow("Latitude (degrees north):", peLat);
  playout->addRow("Elevation:", peElv);
  playout->addRow("Temperature:", peTmp);
  playout->addRow("\"Now\" minute offset:", peCor);

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


// Transits, equivalent to Windows' DlgTransit: which day/transit chart mode
// to show (if any) and the date/time to transit to (ciTran), using the
// default location (Info / Default Chart Info) the same way Windows does.
// The ephemeris search range and search filter options DlgTransit also has
// are not included here -- they only matter for the search chart types,
// which aren't in the Chart menu yet either.

void ShowTransitDialogQt()
{
  QDialog dlg(gi.qwind);
  dlg.setWindowTitle("Transits");
  QVBoxLayout *pouter = new QVBoxLayout(&dlg);

  QGroupBox *pgroupBox = new QGroupBox("Show");
  QVBoxLayout *pgrouplayout = new QVBoxLayout(pgroupBox);
  QButtonGroup *pgroup = new QButtonGroup(&dlg);
  CONST char *rgszDayType[7] = { "Normal chart (no transits)",
    "In-Day Transits: Timeline", "In-Day Transits: Influence",
    "In-Day Transits: Graphic", "Transit to Natal: Timeline",
    "Transit to Natal: Influence", "Transit to Natal: Graphic" };
  int n1 = us.fInDay ? 1 : (us.fInDayInf ? 2 : (us.fInDayGra ? 3 :
    (us.fTransit ? 4 : (us.fTransitInf ? 5 : (us.fTransitGra ? 6 : 0)))));
  int i;
  for (i = 0; i < 7; i++) {
    QRadioButton *prb = new QRadioButton(rgszDayType[i]);
    prb->setChecked(i == n1);
    pgroup->addButton(prb, i);
    pgrouplayout->addWidget(prb);
  }
  pouter->addWidget(pgroupBox);

  QFormLayout *pform = new QFormLayout();
  QLineEdit *peMon = new QLineEdit(QString::number(ciTran.mon));
  QLineEdit *peDay = new QLineEdit(QString::number(ciTran.day));
  QLineEdit *peYea = new QLineEdit(QString::number(ciTran.yea));
  QLineEdit *peTim = new QLineEdit(QString::number(ciTran.tim));
  QLineEdit *peDst = new QLineEdit(ciTran.dst == 0.0 ? "ST" :
    (ciTran.dst == 1.0 ? "DT" :
    (ciTran.dst == dstAuto ? "Autodetect" : SzZone(ciTran.dst))));
  QLineEdit *peZon = new QLineEdit(QString::number(ciTran.zon));
  pform->addRow("Month (1-12):", peMon);
  pform->addRow("Day:", peDay);
  pform->addRow("Year:", peYea);
  pform->addRow("Time (decimal hours):", peTim);
  pform->addRow("Daylight offset:", peDst);
  pform->addRow("Zone (hours west of UTC):", peZon);
  pouter->addLayout(pform);

  QDialogButtonBox *pbuttons =
    new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  pouter->addWidget(pbuttons);
  QObject::connect(pbuttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(pbuttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

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
  if (!FValidMon(mon) || !FValidYea(yea) || !FValidDay(day, mon, yea) ||
    !FValidTim(tim) || !FValidDst(dst) || !FValidZon(zon)) {
    QMessageBox::warning(gi.qwind, szAppName,
      "One or more transit date fields are invalid.");
    return;
  }
  SetCI(ciTran, mon, day, yea, tim, dst, zon, ciDefa.lon, ciDefa.lat);

  int n1sel = pgroup->checkedId();
  flag fRecast = fFalse;
  switch (n1sel) {
  case 1: SetChartModeQt(gTraTraTim); break;
  case 2: SetChartModeQt(gTraTraInf); fRecast = fTrue; break;
  case 3: SetChartModeQt(gTraTraGra); break;
  case 4: SetChartModeQt(gTraNatTim); break;
  case 5: SetChartModeQt(gTraNatInf); fRecast = fTrue; break;
  case 6: SetChartModeQt(gTraNatGra); break;
  default:
    if (n1 != 0)  // Was showing a transit chart; go back to a normal one.
      SetChartModeQt(gWheel);
  }
  if (n1sel > 0)
    us.fGraphics = fFalse;
  if (fRecast)
    RecastAndRedrawQt();
  else
    RedrawQt();
}


// Progressions, equivalent to Windows' DlgProgress: whether to show a
// progressed chart, what kind, and the date to progress to (ciTran, shared
// with the Transits dialog above). Simplified from Windows' version by
// treating the progression rate/cusp ratio as plain numbers instead of also
// offering preset dropdown values (Primary/Secondary/etc), and the solar
// arc planet by name instead of a special "X" reciprocal-rate prefix.

void ShowProgressDialogQt()
{
  QDialog dlg(gi.qwind);
  dlg.setWindowTitle("Progressions");
  QVBoxLayout *pouter = new QVBoxLayout(&dlg);

  QCheckBox *pcbEnable = new QCheckBox("Show Progressed Chart");
  pcbEnable->setChecked(us.fProgress != 0);
  pouter->addWidget(pcbEnable);

  QGroupBox *pgroupBox = new QGroupBox("Progression Type");
  QHBoxLayout *pgrouplayout = new QHBoxLayout(pgroupBox);
  QButtonGroup *pgroup = new QButtonGroup(&dlg);
  CONST char *rgszType[3] = { "Cast", "Mixed", "Solar Arc" };
  int nTypeCur = us.nProgress == ptCast ? 0 :
    (us.nProgress == ptMixed ? 1 : 2);
  int i;
  for (i = 0; i < 3; i++) {
    QRadioButton *prb = new QRadioButton(rgszType[i]);
    prb->setChecked(i == nTypeCur);
    pgroup->addButton(prb, i);
    pgrouplayout->addWidget(prb);
  }
  pouter->addWidget(pgroupBox);

  QFormLayout *pform = new QFormLayout();
  QLineEdit *peRate = new QLineEdit(QString::number(us.rProgDay));
  QLineEdit *peCusp = new QLineEdit(QString::number(us.rProgCusp));
  QLineEdit *peArc = new QLineEdit(us.objProgArc >= 0 ?
    szObjName[us.objProgArc] : "None");
  QCheckBox *pcbRAMC = new QCheckBox("Progress RAMC/Houses Too");
  pcbRAMC->setChecked(us.fProgRAMC != 0);
  pform->addRow("Degrees per day:", peRate);
  pform->addRow("Cusp move ratio:", peCusp);
  pform->addRow("Solar arc planet:", peArc);
  pform->addRow(pcbRAMC);

  QLineEdit *peMon = new QLineEdit(QString::number(ciTran.mon));
  QLineEdit *peDay = new QLineEdit(QString::number(ciTran.day));
  QLineEdit *peYea = new QLineEdit(QString::number(ciTran.yea));
  QLineEdit *peTim = new QLineEdit(QString::number(ciTran.tim));
  QLineEdit *peDst = new QLineEdit(ciTran.dst == 0.0 ? "ST" :
    (ciTran.dst == 1.0 ? "DT" :
    (ciTran.dst == dstAuto ? "Autodetect" : SzZone(ciTran.dst))));
  QLineEdit *peZon = new QLineEdit(QString::number(ciTran.zon));
  pform->addRow("Month (1-12):", peMon);
  pform->addRow("Day:", peDay);
  pform->addRow("Year:", peYea);
  pform->addRow("Time (decimal hours):", peTim);
  pform->addRow("Daylight offset:", peDst);
  pform->addRow("Zone (hours west of UTC):", peZon);
  pouter->addLayout(pform);

  QDialogButtonBox *pbuttons =
    new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  pouter->addWidget(pbuttons);
  QObject::connect(pbuttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(pbuttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  if (dlg.exec() != QDialog::Accepted)
    return;

  real rDay = peRate->text().toDouble();
  real rCusp = peCusp->text().toDouble();
  QByteArray ba;
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
  if (rDay == 0.0 || rCusp == 0.0 || !FValidProgArc(npO) ||
    !FValidMon(mon) || !FValidYea(yea) || !FValidDay(day, mon, yea) ||
    !FValidTim(tim) || !FValidDst(dst) || !FValidZon(zon)) {
    QMessageBox::warning(gi.qwind, szAppName,
      "One or more progression fields are invalid.");
    return;
  }

  us.fProgress = pcbEnable->isChecked();
  us.nProgress = pgroup->checkedId() == 0 ? ptCast :
    (pgroup->checkedId() == 1 ? ptMixed : ptSolarArc);
  us.rProgDay = rDay;
  us.rProgCusp = rCusp;
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

#endif // QT

/* qtdialog.cpp */
