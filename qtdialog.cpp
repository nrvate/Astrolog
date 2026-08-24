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
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QScrollArea>

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
  QLineEdit *peDst  = new QLineEdit(QString::number(ciCore.dst));
  QLineEdit *peZon  = new QLineEdit(QString::number(ciCore.zon));
  QLineEdit *peLon  = new QLineEdit(QString::number(ciCore.lon));
  QLineEdit *peLat  = new QLineEdit(QString::number(ciCore.lat));

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


// Object restriction (show/hide), equivalent to Windows' DlgRestrict, for
// the core planets and angles (object indices 0 through oCore).

void ShowRestrictDialogQt()
{
  QDialog dlg(gi.qwind);
  dlg.setWindowTitle("Restrictions");
  dlg.resize(300, 500);
  QVBoxLayout *pouter = new QVBoxLayout(&dlg);
  QScrollArea *pscroll = new QScrollArea(&dlg);
  QWidget *pinner = new QWidget();
  QVBoxLayout *pinnerlayout = new QVBoxLayout(pinner);
  QCheckBox *rgpcb[oCore+1];
  int i;

  for (i = 0; i <= oCore; i++) {
    rgpcb[i] = new QCheckBox(QString("Show ") + szObjName[i]);
    rgpcb[i]->setChecked(!ignore[i]);
    pinnerlayout->addWidget(rgpcb[i]);
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

  for (i = 0; i <= oCore; i++)
    ignore[i] = !rgpcb[i]->isChecked();
  AdjustRestrictions();
  RecastAndRedrawQt();
}

#endif // QT

/* qtdialog.cpp */
