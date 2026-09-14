/*
** Astrolog (Version 8.00) File: xdevice.cpp
**
** IMPORTANT NOTICE: Astrolog and all chart display routines and anything
** not enumerated below used in this program are Copyright (C) 1991-2026 by
** Walter D. Pullen (Astara@msn.com, http://www.astrolog.org/astrolog.htm).
** Permission is granted to freely use, modify, and distribute these
** routines provided these credits and notices remain unmodified with any
** altered or distributed versions of the program.
**
** The main ephemeris databases and calculation routines are from the
** library SWISS EPHEMERIS and are programmed and copyright 1997-2008 by
** Astrodienst AG. Use of that source code is subject to license for Swiss
** Ephemeris Free Edition at https://www.astro.com/swisseph/swephinfo_e.htm.
** This copyright notice must not be changed or removed by any user of this
** program.
**
** Additional ephemeris databases and formulas are from the calculation
** routines in the program PLACALC and are programmed and Copyright (C)
** 1989,1991,1993 by Astrodienst AG and Alois Treindl (alois@astro.ch). The
** use of that source code is subject to regulations made by Astrodienst
** Zurich, and the code is not in the public domain. This copyright notice
** must not be changed or removed by any user of this program.
**
** The original planetary calculation routines used in this program have
** been copyrighted and the initial core of this program was mostly a
** conversion to C of the routines created by James Neely as listed in
** 'Manual of Computer Programming for Astrologers', by Michael Erlewine,
** available from Matrix Software.
**
** Atlas composed using data from https://www.geonames.org/ licensed under a
** Creative Commons Attribution 4.0 License. Time zone changes composed using
** public domain TZ database: https://data.iana.org/time-zones/tz-link.html
**
** The PostScript code within the core graphics routines are programmed
** and Copyright (C) 1992-1993 by Brian D. Willoughby (brianw@sounds.wa.com).
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
** Initial programming 8/28-30/1991.
** X Window graphics initially programmed 10/23-29/1991.
** PostScript graphics initially programmed 11/29-30/1992.
** Last code change made 5/28/2026.
*/

// The animated GIF writer's worker threads; see FWriteGifFrame(). mingw-w64
// built with the win32 thread model, which is Makefile.win's compiler, has
// no std::thread before GCC 13, and there a GIF is written on one thread.
#include <cstddef>
#include <cstring>
#if !defined(__GLIBCXX__) || defined(_GLIBCXX_HAS_GTHREADS)
#define GIFTHREADS
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#endif
#include <deque>
#include <memory>
#include <vector>

#include "astrolog.h"
#include "cgif.h"
#include "cgif_raw.h"


#ifdef GRAPH
/*
******************************************************************************
** Windows Bitmap Routines.
******************************************************************************
*/

#define cbPixelK 3
#define CbColmapRow(x) (((x)*cbPixelK + 3) & ~3)
#define CbColmap(x, y) ((y) * CbColmapRow(x))
#define zColmap 65535

// Functions to set or get a pixel within a 24 bit color bitmap.

INLINE long _IbXY(CONST Bitmap *b, int x, int y)
  { return y*(b->clRow << 2) + (x * cbPixelK); }
INLINE byte *_PbXY(CONST Bitmap *b, int x, int y)
  { return &(b->rgb)[_IbXY(b, x, y)]; }
INLINE void _SetRGB(byte *pb, int r, int g, int b)
  { *pb = b; *(pb+1) = g; *(pb+2) = r; }
INLINE KV _GetP(CONST byte *pb)
  { return (*pb << 16) | (*(pb+1) << 8) | *(pb+2); }
INLINE KV _GetXY(CONST Bitmap *b, int x, int y)
  { return _GetP(_PbXY(b, x, y)); }
INLINE void _GetRGB(CONST byte *pb, int *r, int *g, int *b)
  { *b = *pb; *g = *(pb+1); *r = *(pb+2); }
void BmpSetXY(Bitmap *b, int x, int y, KV kv)
  { _SetRGB(_PbXY(b, x, y), RgbR(kv), RgbG(kv), RgbB(kv)); }
KV BmpGetXY(CONST Bitmap *b, int x, int y)
  { return _GetXY(b, x, y); }
void SetXY(int x, int y, KV ki)
  { if (!gi.fBmp) BmSet(gi.bm, x, y, ki); else BmpSetXY(&gi.bmp, x, y, ki); }
KV GetXY(int x, int y)
  { return !gi.fBmp ? FBmGet(gi.bm, x, y) : _GetXY(&gi.bmp, x, y); }
KI BmGetXY(int x, int y)
  { return !gi.fBmp ? FBmGet(gi.bm, x, y) : (_GetXY(&gi.bmp, x, y) > 0)*15; }


// Allocate or reallocate a 24 bit color bitmap to have a given size.

flag FAllocateBmp(Bitmap *b, int x, int y)
{
  char sz[cchSzDef];
  long cb;
  byte *rgb;

  if (x == b->x && y == b->y)
    return fTrue;
  if (x < 0 || y < 0 || x > zColmap || y > zColmap) {
    sprintf2(S(sz), "Can't create color bitmap larger than %d by %d!\n",
      zColmap, zColmap);
    PrintError(sz);
    return fFalse;
  }
  cb = CbColmap(x, y);
  if (cb < 0) {
    sprintf2(S(sz), "Can't allocate color bitmap of size %d by %d!\n", x, y);
    PrintError(sz);
    return fFalse;
  }
  if (cb != CbColmap(b->x, b->y)) {
    rgb = (byte *)PAllocate(cb, "color bitmap");
    if (rgb == NULL)
      return fFalse;
    DeallocatePIf(b->rgb);
    b->rgb = rgb;
  }
  b->x = x; b->y = y;
  b->clRow = CbColmapRow(x) >> 2;
  return fTrue;
}


// Load a bitmap from file into a 24 bit bitmap structure. This supports
// Windows bitmap files stored with 4, 8, 16, 24, or 32 bits per pixel.

flag FReadBmp(Bitmap *b, FILE *file, flag fNoHeader)
{
  byte *pb, bR, bG, bB, ch, ch2;
  KV rgkv[256], kv;
  int cbExtra, cb, x, y, z, k, i, m, n;
  long l;

  // BitmapFileHeader
  if (!fNoHeader) {
    ch = getbyte(); ch2 = getbyte();
    if (ch != 'B' || ch2 != 'M') {
      PrintError("This file does not look like a Windows bitmap.\n");
      return fFalse;
    }
    skiplong();
    skipword(); skipword();
    skiplong();
  }

  // BitmapInfo / BitmapInfoHeader
  cbExtra = getlong();
  x = NAbs((int)getlong()); y = NAbs((int)getlong());
  skipword(); z = getword();
  l = getlong();
  if (l != 0/*BI_RGB*/ && l != 3/*BI_BITFIELDS*/) {
    PrintError("This Windows bitmap can't be uncompressed.\n");
    return fFalse;
  }
  for (i = 0; i < 3; i++) {
    skiplong();
  }
  k = getlong(); skiplong();
  for (cbExtra -= 40; cbExtra > 0; cbExtra--)
    skipbyte();
  if (l == 3/*BI_BITFIELDS*/)
    for (i = 0; i < 3; i++) {
      skiplong();
    }
  if (!(z == 4 || z == 8 || z == 16 || z == 24 || z == 32)) {
    PrintError("This Windows bitmap has bad number of bits per pixel.\n");
    return fFalse;
  }

  // RgbQuad
  // Data
  if (!FAllocateBmp(b, x, y))
    return fFalse;

  // Figure out the bytes per row in this color bitmap.
  if (z == 32)
    cb = x << 2;
  else if (z == 24)
    cb = x * 3;
  else if (z == 16) {
    for (i = 0; i < 12; i++)
      skipbyte();
    cb = x << 1;
  } else {
    // Read in the color palette to translate indexes to RGB values.
    Assert(k <= 256);
    if (k == 0)
      k = (z == 8) ? 256 : 16;
    for (i = 0; i < k; i++) {
      bB = getbyte(); bG = getbyte(); bR = getbyte();
      skipbyte();
      rgkv[i] = Rgb(bR, bG, bB);
    }
    if (z == 8)
      cb = x;
    else
      cb = (x + 1) >> 1;
  }
  // Figure out the number of padding bytes after each row.
  cb = (4 - (cb & 3)) & 3;

  for (n = y-1; n >= 0; n--) {
    pb = _PbXY(b, 0, n);
    for (m = 0; m < x; m++) {
      if (z >= 24) {
        bB = getbyte(); bG = getbyte(); bR = getbyte();
        if (z == 32)
          skipbyte();
      } else {
        if (z == 16) {
          i = WRead(file);
          kv = Rgb((i >> 11) << 3, (i >> 5 & 63) << 2, (i & 31) << 3);
        } else if (z == 8) {
          ch = getbyte();
          kv = rgkv[ch];
        } else {
          if (!FOdd(m))
            ch = getbyte();
          i = FOdd(m) ? (ch & 15) : (ch >> 4);
          kv = rgkv[i];
        }
        bR = RgbR(kv); bG = RgbG(kv); bB = RgbB(kv);
      }
      _SetRGB(pb, bR, bG, bB);
      pb += cbPixelK;
    }
    for (m = 0; m < cb; m++)
      skipbyte();
  }
  return fTrue;
}


// Load a 24 bit bitmap given a filename.

flag FLoadBmp(CONST char *szFile, Bitmap *bmp, flag fNoHeader)
{
  FILE *file;
  flag fRet;

  file = FileOpen(szFile, 3, NULL, 0);
  if (file == NULL)
    return fFalse;
  fRet = FReadBmp(bmp, file, fNoHeader);
  fclose(file);
  return fRet;
}


// Write a 24 bit bitmap to a previously opened file, in the 24 bit bitmap
// format used by Microsoft Windows for its .bmp extension files.

void WriteBmp2(CONST Bitmap *b, FILE *file)
{
  int x, y, cb;
  dword dw;

  cb = (4 - ((b->x*3) & 3)) & 3;
  // BitmapFileHeader
  PutByte('B'); PutByte('M');
  dw = 14+40 + 0 + b->y*(b->x*3+cb);
  PutLong(dw);
  PutWord(0); PutWord(0);
  PutLong(14+40 + 0);
  // BitmapInfo / BitmapInfoHeader
  PutLong(40);
  PutLong(b->x); PutLong(b->y);
  PutWord(1); PutWord(24);
  PutLong(0 /*BI_RGB*/); PutLong(0);
  PutLong(0); PutLong(0);
  PutLong(0); PutLong(0);
  // RgbQuad
  // Data
  for (y = b->y-1; y >= 0; y--) {
    for (x = 0; x < b->x; x++) {
      dw = _GetXY(b, x, y);
      PutByte(RgbB(dw)); PutByte(RgbG(dw)); PutByte(RgbR(dw));
    }
    for (x = 0; x < cb; x++)
      PutByte(0);
  }
}


// Set all pixels in a 24 bit bitmap to the specified RGB color value.

void BmpSetAll(Bitmap *b, KV kv)
{
  int x, y, nR, nG, nB;
  byte *pb;

  nR = RgbR(kv); nG = RgbG(kv); nB = RgbB(kv);
  for (y = 0; y < b->y; y++) {
    pb = _PbXY(b, 0, y);
    for (x = 0; x < b->x; x++) {
      _SetRGB(pb, nR, nG, nB);
      pb += cbPixelK;
    }
  }
}


// Copy a rectangle from one bitmap structure to a different rectangle in
// another, stretching pixels as needed. Like the Windows StretchBlt() API.

void BmpCopyBlock(CONST Bitmap *bs, int x1, int y1, int x2, int y2,
  Bitmap *bd, int x3, int y3, int x4, int y4)
{
  int xs = x2-x1+1, ys = y2-y1+1, xd = x4-x3+1, yd = y4-y3+1,
    x, y, xT, yT, nR, nG, nB;
  byte *pbDst;

  // Sanity checks of coordinate bounds, which shouldn't ever fail.
  Assert(FBetween(x1, 0, bs->x-1));
  Assert(FBetween(y1, 0, bs->y-1));
  Assert(FBetween(x2, 0, bs->x-1));
  Assert(FBetween(y2, 0, bs->y-1));
  Assert(FBetween(x3, 0, bd->x-1));
  Assert(FBetween(y3, 0, bd->y-1));
  Assert(FBetween(x4, 0, bd->x-1));
  Assert(FBetween(y4, 0, bd->y-1));

  for (y = y3; y <= y4; y++) {
    pbDst = _PbXY(bd, x3, y);
    yT = y1 + (y-y3) * ys / yd;
    for (x = x3; x <= x4; x++) {
      xT = x1 + (x-x3) * xs / xd;
      _GetRGB(_PbXY(bs, xT, yT), &nR, &nG, &nB);
      _SetRGB(pbDst, nR, nG, nB);
      pbDst += cbPixelK;
    }
  }
}


// Adjust the color of a pixel on a world map, based on whether the location
// is at night time, or whether the location is under a solar eclipse.

void BmpDarkenKv(real lon, real lat, real lonS, real latS, flag fDoEclipse,
  KV *pkv)
{
  KV kv = *pkv;
#ifdef SWISS
  int et;
  real rEclipse;
#endif

  // Check whether the location is in the night time half of the world.
  if (SphDistance(lonS, latS, lon, rDegQuad - lat) > rDegQuad) {
    *pkv = Rgb(RgbR(kv) / 3, RgbG(kv) / 3, RgbB(kv) / 3);
    return;
  }

#ifdef SWISS
  // Check for a partial or annular/total solar eclipse at the location.
  if (!fDoEclipse)
    return;
  et = NCheckEclipseSolarLoc(rDegHalf - lon, rDegQuad - lat, &rEclipse);
  if (et <= etNone)
    return;
  if (et <= etPartial) {
    *pkv = KvBlend(kv, kBlack, 0.33 + rEclipse/100.0*0.33);
    return;
  }
  *pkv = KvBlend(kv, rgbbmp[kRed], 0.5);
#endif
}


// Like BmpCopyBlock() but the source rectangle coordinates are reals instead
// of integers. Can be used to render only subsections of source pixels.

void BmpCopyBlock2(CONST Bitmap *bs, real x1, real y1, real x2, real y2,
  Bitmap *bd, int x3, int y3, int x4, int y4)
{
  int xd = x4-x3+1, yd = y4-y3+1, x, y, xT, yT, nR, nG, nB;
  real xs, ys, lonS, latS, rx, ry;
  byte *pbDst;
  flag fDoEclipse = fFalse;
  KV kv;

  // Sanity checks of coordinate bounds, which shouldn't ever fail.
  Assert(FBetween(x1, 0.0, (real)bs->x - rSmall));
  Assert(FBetween(y1, 0.0, (real)bs->y - rSmall));
  Assert(FBetween(x2, 0.0, (real)bs->x - rSmall));
  Assert(FBetween(y2, 0.0, (real)bs->y - rSmall));
  Assert(FBetween(x3, 0, bd->x-1));
  Assert(FBetween(y3, 0, bd->y-1));
  Assert(FBetween(x4, 0, bd->x-1));
  Assert(FBetween(y4, 0, bd->y-1));

  if (gs.fMollweide) {
    lonS = Tropical(planet[oSun]);
    latS = planetalt[oSun];
    EclToEqu(&lonS, &latS);
    lonS = Mod(lonS - cp0.lonMC + rDegHalf - Lon);
    if (us.fEclipse &&
      NCheckEclipseSolar(oEar, oMoo, oSun, NULL) > etNone)
      fDoEclipse = fTrue;
  }

  xs = (x2-x1) / (real)xd;
  ys = (y2-y1) / (real)yd;
  for (y = y3; y <= y4; y++) {
    pbDst = _PbXY(bd, x3, y);
    ry = y1 + (real)(y-y3) * ys;
    yT = (int)ry;
    if (!gs.fMollweide) {
      // Fast loop that just copies pixels.
      for (x = x3; x <= x4; x++) {
        xT = (int)(x1 + (real)(x-x3) * xs);
        _GetRGB(_PbXY(bs, xT, yT), &nR, &nG, &nB);
        _SetRGB(pbDst, nR, nG, nB);
        pbDst += cbPixelK;
      }
    } else {
      // Slower loop that may modify colors based on position on Earth.
      ry = ry * rDegMax / (real)bs->x;
      for (x = x3; x <= x4; x++) {
        rx = x1 + (real)(x-x3) * xs;
        xT = (int)rx;
        rx = rx * rDegMax / (real)bs->x;
        kv = _GetXY(bs, xT, yT);
        BmpDarkenKv(rx, ry, lonS, latS, fDoEclipse, &kv);
        BmpSetXY(bd, x, y, kv);
      }
    }
  }
}


#ifdef WINANY
// Copy a 24 bit bitmap structure to a Windows DC. Since Astrolog's internal
// bitmap structure is the same as Windows, it can be done all at once.

void BmpCopyToWin(CONST Bitmap *b, HDC hdc, int x, int y)
{
  BITMAPINFO bi;

  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = cbPixelK << 3;
  bi.bmiHeader.biCompression = BI_RGB;
  bi.bmiHeader.biSizeImage = 0;
  bi.bmiHeader.biXPelsPerMeter = bi.bmiHeader.biYPelsPerMeter = 1000;
  bi.bmiHeader.biClrUsed = 0;
  bi.bmiHeader.biClrImportant = 0;
  bi.bmiColors[0].rgbBlue = bi.bmiColors[0].rgbGreen =
    bi.bmiColors[0].rgbRed = bi.bmiColors[0].rgbReserved = 0;
  bi.bmiHeader.biWidth  =  (b->x);
  bi.bmiHeader.biHeight = -(b->y);

#ifdef WIN
  if (hdc == wi.hdcPrint) {
    // When printing the destination area is differently scaled.
    StretchDIBits(hdc, x, y, b->x, b->y, 0, 0, b->x, b->y,
      b->rgb, (BITMAPINFO *)&bi, DIB_RGB_COLORS, SRCCOPY);
  } else
#endif
  {
    // Fast direct 1:1 pixel memory copy.
    SetDIBitsToDevice(hdc, x, y, b->x, b->y, 0, 0, 0, b->y,
      b->rgb, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
  }
}


// This is the reverse of BmpCopyToWin(). Copy the pixels from a Windows DC to
// a 24 bit bitmap structure. Again since Astrolog's internal bitmap structure
// is the same as Windows, it can be done all at once.

flag FBmpCopyFromWin(Bitmap *b, HDC hdc, HBITMAP hbmp)
{
  BITMAPINFO bi;

  if (!FAllocateBmp(b, wi.xClient, wi.yClient))
    return fFalse;
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = cbPixelK << 3;
  bi.bmiHeader.biCompression = BI_RGB;
  bi.bmiHeader.biSizeImage = 0;
  bi.bmiHeader.biXPelsPerMeter = bi.bmiHeader.biYPelsPerMeter = 1000;
  bi.bmiHeader.biClrUsed = 0;
  bi.bmiHeader.biClrImportant = 0;
  bi.bmiColors[0].rgbBlue = bi.bmiColors[0].rgbGreen =
    bi.bmiColors[0].rgbRed = bi.bmiColors[0].rgbReserved = 0;
  bi.bmiHeader.biWidth  =  (b->x);
  bi.bmiHeader.biHeight = -(b->y);

  GetDIBits(hdc, hbmp, 0, wi.yClient, b->rgb, &bi, DIB_RGB_COLORS);
  return fTrue;
}


#ifdef WIN
// Copy pixels from a large Windows DC to a proportionally smaller one, where
// each destination pixel is a blend of the colors in the NxN square of pixels
// composing the source one. This is done to produce an antialiasing effect.

flag FBmpShrinkToWin(HDC hdc, HBITMAP hbmp, HDC hdcWin)
{
  Bitmap *b = &wi.bmpSmooth, *b2 = &wi.bmpWin;
  int x, y, cR, cG, cB, nR, nG, nB, x1, y1, x2, y2, sq, sq2;
  flag fRet;
  byte *pb, *pb2;

  if (us.fWriteOld) {
    // Windows can quickly do the shrinking and blending, but not as well.
    SetStretchBltMode(hdcWin, HALFTONE);
    SetBrushOrgEx(hdcWin, 0, 0, NULL);
    StretchBlt(hdcWin, 0, 0, wi.xClient, wi.yClient,
      hdc, 0, 0, wi.xClient*wi.nScaleWin, wi.yClient*wi.nScaleWin, SRCCOPY);
    return fTrue;
  }

  // First copy to or allocate internal bitmap structures to make use of.
  wi.xClient *= wi.nScaleWin; wi.yClient *= wi.nScaleWin;
  fRet = FBmpCopyFromWin(b, hdc, wi.hbmp);
  wi.xClient /= wi.nScaleWin; wi.yClient /= wi.nScaleWin;
  if (!fRet)
    return fFalse;

  if (!FAllocateBmp(b2, wi.xClient, wi.yClient))
    return fFalse;
  sq = Sq(wi.nScaleWin); sq2 = sq >> 1;

  // Loop over each pixel in the destination bitmap.
  for (y = 0; y < wi.yClient; y++) {
    pb2 = _PbXY(b2, 0, y);
    for (x = 0; x < wi.xClient; x++) {
      // Loop over the current NxN square of pixels in the source bitmap.
      cR = cG = cB = 0;
      for (y1 = y * wi.nScaleWin, y2 = y1 + wi.nScaleWin; y1 < y2; y1++) {
        pb = _PbXY(b, x * wi.nScaleWin, y1);
        for (x1 = x * wi.nScaleWin, x2 = x1 + wi.nScaleWin; x1 < x2; x1++) {
          _GetRGB(pb, &nR, &nG, &nB);
          cR += nR; cG += nG; cB += nB;
          pb += cbPixelK;
        }
      }
      _SetRGB(pb2, (cR + sq2) / sq, (cG + sq2) / sq, (cB + sq2) / sq);
      pb2 += cbPixelK;
    }
  }

  BmpCopyToWin(b2, hdcWin, 0, 0);
  return fTrue;
}
#endif
#endif // WINANY


// Draw the background bitmap onto the specified 24 bit bitmap. Implements the
// -XI switch features.

flag FBmpDrawBack(Bitmap *bDest)
{
  Bitmap *b = &gi.bmpBack, *b2 = &gi.bmpBack2;
  int nTrans, x, y, x1, y1, x2, y2, x3, y3, x4, y4, nR, nG, nB, nRT, nGT, nBT;
  byte *pb, *pb2;
  KV kv;
  static KV kvLast = -1;
  static int nTransLast = 0;
#ifdef WINANY
  // Only the Windows path reads these back, to notice the background
  // bitmap changing size; elsewhere they were set and never used.
  static int xLast = 0, yLast = 0;
#endif

  // Don't draw background if user doesn't want to.
  if (!gs.fBackDraw || !gi.fBmp)
    return fFalse;

  // Don't draw background if entire chart will be covered with world map.
  if (gi.bmpWorld.rgb != NULL &&
    (gi.nMode == gAstroGraph || (gi.nMode == gWorldMap && !gs.fMollweide)))
    return fFalse;

  // Don't do anything if bitmap empty or transparent enough to be invisible.
  nTrans = (int)(gs.rBackPct * 256.0 / 100.0);
  if (b->rgb == NULL || nTrans <= 0)
    return fFalse;

  // Cache bitmap with proper percentage blend with current background color.
  kv = KvFromKi(gi.kiOff);
  if (b2->x != b->x || b2->y != b->y || kv != kvLast || nTrans != nTransLast) {
    if (!FAllocateBmp(b2, b->x, b->y))
      return fFalse;
    kvLast = kv;
    nTransLast = nTrans;
    nR = RgbR(kv); nG = RgbG(kv); nB = RgbB(kv);
    for (y = 0; y < b->y; y++) {
      pb = _PbXY(b, 0, y);
      pb2 = _PbXY(b2, 0, y);
      for (x = 0; x < b->x; x++) {
        _GetRGB(pb, &nRT, &nGT, &nBT);
        nRT = nR + ((nRT - nR) * nTrans >> 8);
        nGT = nG + ((nGT - nG) * nTrans >> 8);
        nBT = nB + ((nBT - nB) * nTrans >> 8);
        _SetRGB(pb2, nRT, nGT, nBT);
        pb += cbPixelK; pb2 += cbPixelK;
      }
    }
#ifdef WINANY
    xLast = yLast = 0;
#endif
  }

  // Determine source (on bitmap) and destination (on chart) rectangles.
  x1 = y1 = x3 = y3 = 0;
  x2 = gs.xWin; y2 = gs.yWin;
  x4 = b2->x; y4 = b2->y;
  if (gs.nBackOrient < 0) {
    if ((real)x2 / (real)y2 > (real)x4 / (real)y4) {
      x2 = y2 * x4 / y4;
      x1 = (gs.xWin - x2) >> 1;
    } else {
      y2 = x2 * y4 / x4;
      y1 = (gs.yWin - y2) >> 1;
    }
  } else if (gs.nBackOrient > 0) {
    if ((real)x4 / (real)y4 > (real)x2 / (real)y2) {
      x4 = y4 * x2 / y2;
      x3 = (b2->x - x4) >> 1;
    } else {
      y4 = x4 * y2 / x2;
      y3 = (b2->y - y4) >> 1;
    }
  }

  // For bitmaps, copy background to chart bitmap manually.
  if (gi.fFile || bDest != NULL) {
    if ((gs.ft == ftBmp && gi.fBmp) || bDest != NULL)
      BmpCopyBlock(&gi.bmpBack2, x3, y3, x3+x4-1, y3+y4-1,
        bDest != NULL ? bDest : &gi.bmp, x1, y1, x1+x2-1, y1+y2-1);
    return fTrue;
  }

#ifdef QT
  // Interactively on Qt, blit the blended cache straight onto the chart
  // image. The only other non-file path here is the WINANY one below, and
  // this function returns fTrue either way -- so without this a caller
  // believes a background was drawn and skips its own erase (the dtErase
  // test in xcharts0.cpp), leaving the backdrop blank.
  //
  // A Bitmap row is 3 bytes per pixel in B,G,R order (see _SetRGB above)
  // padded out to a long boundary, which is exactly Format_BGR888 with a
  // stride of clRow*4 -- so this can wrap the existing buffer rather than
  // copy it.
  if (gi.qpaint == NULL)
    return fFalse;
  {
    QImage qimBack((CONST uchar *)b2->rgb, b2->x, b2->y,
      b2->clRow << 2, QImage::Format_BGR888);
    gi.qpaint->drawImage(QRect(x1, y1, x2, y2), qimBack,
      QRect(x3, y3, x4, y4));
  }
  return fTrue;
#endif

#ifdef WINANY
  // For Windows, draw background bitmap on window using Windows API.
  if (wi.hdcBack == NULL) {
    wi.hdcBack = CreateCompatibleDC(wi.hdc);
    SetMapMode(wi.hdcBack, MM_TEXT);
    SetWindowOrg(wi.hdcBack, 0, 0); SetViewportOrg(wi.hdcBack, 0, 0);
  }

  if (b2->x != xLast || b2->y != yLast) {
    // If background has changed size, create a new Windows Bitmap for it.
    SelectObject(wi.hdcBack, wi.hbmpPrev);
    if (wi.hbmpBack != NULL)
      DeleteObject(wi.hbmpBack);
    wi.hbmpBack = CreateCompatibleBitmap(wi.hdc, b2->x, b2->y);
    if (wi.hbmpBack == NULL) {
      PrintError("Failed to create color background bitmap.");
      return fFalse;
    }
    wi.hbmpPrev = (HBITMAP)SelectObject(wi.hdcBack, wi.hbmpBack);
    xLast = b2->x; yLast = b2->y;
    BmpCopyToWin(b2, wi.hdcBack, 0, 0);
  }
  SetStretchBltMode(wi.hdc, COLORONCOLOR);
  StretchBlt(wi.hdc, x1, y1, x2, y2, wi.hdcBack, x3, y3, x4, y4, SRCCOPY);
#endif
  return fTrue;
}


// Draw the world map bitmap upon the specified 24 bit bitmap. This draws the
// world in the appropriate projection for various Astrolog charts.

flag FBmpDrawMap()
{
  Bitmap *bmp = &gi.bmp;
  int nScl = 1, yWin2, xc, yc, zc, x1, x2, y1, y2, xi, yi, n, n2;
  real deg = Mod(rDegMax - gs.rRot), lonS, latS, rxc, ryc, rzc,
    lon, lat, lat0, rT, rLen, sint = 0.0, cost = 0.0, sina, cosa;
  KV kv;
  flag fDoEclipse = fFalse;

  // Do nothing if not drawing bitmaps, or if the Earth bitmap fails to load.
  if (!gi.fBmp || (gi.fFile && gs.ft != ftBmp))
    return fFalse;
  if (gi.bmpWorld.rgb == NULL && !FLoadBmp(BITMAP_EARTH, &gi.bmpWorld, fFalse))
    return fFalse;
  yWin2 = gs.yWin;
  if (gs.fText && fMap)
    yWin2 -= (yFont2*gi.nScaleText + 2)*gi.nScaleT;
#ifdef WINANY
  if (!gi.fFile) {
    if (!FAllocateBmp(&wi.bmpWin, gs.xWin, yWin2))
      return fFalse;
    bmp = &wi.bmpWin;
  }
#endif
#ifdef QT
  // The same thing WINANY does just above, for the same reason: the
  // portable composition below needs a real Bitmap to draw into, and
  // gi.bmp is the file export buffer, which nothing allocates on the
  // screen path. Without it the caller falls back to its vector map, so
  // "Use Detailed World Map" does nothing on screen.
  //
  // gi.bmp is the right buffer to borrow rather than a new one: the
  // export path allocates it itself whenever it runs (xscreen.cpp),
  // FAllocateBmp() resizes rather than leaks when the dimensions differ,
  // and astrolog.cpp already frees it at exit beside its three siblings.
  int xWinSavQt = gs.xWin, yWinSavQt = gs.yWin;
  if (!gi.fFile) {
    // Compose at the same 2:1 rectangle the VECTOR map uses, not at the
    // whole canvas. The window is whatever shape the user drags it to,
    // and stretching the Earth to fill it distorts the geography.
    // Windows never meets this: FActionX has already fitted a map chart
    // to 2:1 before it gets here, and the screen path does not pass
    // through FActionX at all. rScaleMap is read here while gs.xWin and
    // gs.yWin still hold the window, and read again by the caller's
    // vector overlay after they are put back, so both get the same
    // number and the coastlines line up with the lines drawn over them.
    int nFit = (int)(rDegHalf * rScaleMap);
    gs.xWin = nFit*2; gs.yWin = nFit; yWin2 = nFit;
    if (gi.qpaint == NULL || !FAllocateBmp(&gi.bmp, gs.xWin, yWin2)) {
      gs.xWin = xWinSavQt; gs.yWin = yWinSavQt;
      return fFalse;
    }
    bmp = &gi.bmp;
  }
#endif
  if (gs.fMollweide && us.fEclipse &&
    NCheckEclipseSolar(oEar, oMoo, oSun, NULL) > etNone)
    fDoEclipse = fTrue;

  // Compute center coordinates and horizontal map dimensions.
  xc = (gs.xWin >> 1) - !FOdd(gs.xWin); yc = (gs.yWin >> 1) - !FOdd(gs.yWin);
  zc = Max(xc, yc);
  rxc = (real)xc; ryc = (real)yc; rzc = (real)zc;
  x1 = (int)((real)gi.bmpWorld.x * deg / rDegMax);
  x2 = (int)((real)gs.xWin       * deg / rDegMax);

  // Draw map on a -XW rectangular world map.
  if (gi.nMode == gAstroGraph || (gi.nMode == gWorldMap && !gs.fMollweide)) {
    if (x1 == 0 || x2 == 0)
      BmpCopyBlock(&gi.bmpWorld, 0, 0, gi.bmpWorld.x-1, gi.bmpWorld.y-1,
        bmp, 0, 0, gs.xWin-1, yWin2-1);
    else {
      BmpCopyBlock(&gi.bmpWorld, 0, 0, x1-1, gi.bmpWorld.y-1,
        bmp, gs.xWin-x2-1, 0, gs.xWin-1, yWin2-1);
      BmpCopyBlock(&gi.bmpWorld, x1, 0, gi.bmpWorld.x-1, gi.bmpWorld.y-1,
        bmp, 0, 0, gs.xWin-x2, yWin2-1);
    }

  // Draw map on a -XW0 Mollweide projection world map.
  } else if (gi.nMode == gWorldMap && gs.fMollweide) {
    if (!FBmpDrawBack(bmp))
      BmpSetAll(bmp, KvFromKi(gi.kiOff));
    for (y2 = 0; y2 < yWin2; y2++) {
      y1 = y2 * gi.bmpWorld.y / yWin2;
      rT = RMollweide((real)y2 * rDegHalf / (real)yWin2 - rDegQuad,
        (real)nScl);
      n = (gs.xWin - (int)(rT * (real)gs.xWin / rDegHalf)) >> 1;
      if (x1 == 0 || x2 == 0)
        BmpCopyBlock(&gi.bmpWorld, 0, y1, gi.bmpWorld.x-1, y1,
          bmp, n, y2, gs.xWin-1-n, y2);
      else {
        n2 = (int)(rT * (real)(x2 - xc) / rDegHalf);
        n2 += xc;
        BmpCopyBlock(&gi.bmpWorld, 0, y1, x1-1, y1,
          bmp, gs.xWin-n2-1, y2, gs.xWin-1-n, y2);
        BmpCopyBlock(&gi.bmpWorld, x1, y1, gi.bmpWorld.x-1, y1,
          bmp, n, y2, gs.xWin-n2, y2);
      }
    }

  // Draw map on a -XP polar globe.
  } else if (gi.nMode == gPolar) {
    if (!FBmpDrawBack(bmp))
      BmpSetAll(bmp, KvFromKi(gi.kiOff));
    lonS = Tropical(planet[oSun]);
    latS = planetalt[oSun];
    EclToEqu(&lonS, &latS);
    lonS = Mod(lonS - cp0.lonMC + rDegHalf - Lon);
    for (y1 = 0; y1 < gs.yWin; y1++) {
      yi = !FOdd(gs.yWin) && y1 > yc;
      for (x1 = 0; x1 < gs.xWin; x1++) {
        xi = !FOdd(gs.xWin) && x1 > xc;
        n  = xc - x1 + xi;
        n2 = yc - y1 + yi;
        if (xc > yc)
          n2 = n2 * xc / yc;
        else if (yc > xc)
          n = n * yc / xc;
        n = Sq(n) + Sq(n2);
        if (n > Sq(zc))
          continue;
        lat = RAsinD(RSqr((real)n) / rzc) * 2.0;
        if (gs.fSouth)
          lat = rDegHalf - lat;
        lon = RAngleD(x1 - xc, y1 - yc);
        lon = Mod(270.0 - gs.rRot + (!gs.fSouth ? -lon : lon));
        if (gs.fEcliptic) {
          lon = Tropical(lon);
          lat = rDegQuad - lat;
          EclToEqu(&lon, &lat);
          lon = Mod(lon - cp0.lonMC + rDegHalf - Lon);
          lat = rDegQuad - lat;
        }
        x2 = (int)(lon * ((real)gi.bmpWorld.x - rSmall) / rDegMax);
        y2 = (int)(lat * ((real)gi.bmpWorld.y - rSmall) / rDegHalf);
        kv = _GetXY(&gi.bmpWorld, x2, y2);
        if (gs.fMollweide)
          BmpDarkenKv(lon, lat, lonS, latS, fDoEclipse, &kv);
        BmpSetXY(bmp, x1, y1, kv);
      }
    }

  // Draw map on a -XG globe.
  } else if (gi.nMode == gGlobe) {
    if (!FBmpDrawBack(bmp))
      BmpSetAll(bmp, KvFromKi(gi.kiOff));
    if (gs.rTilt != 0.0) {
      sint = RSinD(-gs.rTilt);
      cost = RCosD(-gs.rTilt);
    }
    lonS = Tropical(planet[oSun]);
    latS = planetalt[oSun];
    EclToEqu(&lonS, &latS);
    lonS = Mod(lonS - cp0.lonMC + rDegHalf - Lon);
    for (y1 = 0; y1 < gs.yWin; y1++) {
      yi = !FOdd(gs.yWin) && y1 > yc;
      rT = (ryc - (real)y1) / ryc;
      if (rT < -1.0)    // Roundoff may put it slightly outside Acos range.
        rT = -1.0;
      else if (rT > 1.0)
        rT = 1.0;
      lat0 = RAcosD(rT);
      n = xc; n2 = yc - y1;
      if (xc > yc)
        n2 = n2 * xc / yc;
      else if (yc > xc)
        n = n * yc / xc;
      rT = (real)(Sq(n) - Sq(n2));
      rLen = rT >= 0.0 ? RSqr(rT) : rSmall;
      if (rLen < rSmall)
        rLen = 1.0;
      sina = RSinD(rDegQuad - lat0);
      cosa = RCosD(rDegQuad - lat0);
      for (x1 = 0; x1 < gs.xWin; x1++) {
        xi = !FOdd(gs.xWin) && x1 > xc;
        n  = xc - x1 + xi;
        n2 = yc - y1 + yi;
        if (xc > yc)
          n2 = n2 * xc / yc;
        else if (yc > xc)
          n = n * yc / xc;
        n = Sq(n) + Sq(n2);
        if (n > Sq(zc))
          continue;
        lon = (rxc - (real)x1) / rxc;
        rT = lon / rLen * rzc;
        if (rT < -1.0)    // Roundoff may put it slightly outside Acos range.
          rT = -1.0;
        else if (rT > 1.0)
          rT = 1.0;
        lon = Mod(RAcosD(rT));
        lat = lat0;
        if (gs.rTilt != 0.0) {
          lat = rDegQuad - lat;
          CoorXformFast(&lon, &lat, RSinD(lon), RCosD(lon),
            sina, cosa, sint, cost);
          lat = rDegQuad - lat;
        }
        lon = Mod(lon - gs.rRot);
        if (gs.fEcliptic) {
          lon = Tropical(lon);
          lat = rDegQuad - lat;
          EclToEqu(&lon, &lat);
          lon = Mod(lon - cp0.lonMC + rDegHalf - Lon);
          lat = rDegQuad - lat;
        }
        x2 = (int)(lon * ((real)gi.bmpWorld.x - rSmall) / rDegMax);
        y2 = (int)(lat * ((real)gi.bmpWorld.y - rSmall) / rDegHalf);
        kv = _GetXY(&gi.bmpWorld, x2, y2);
#if FALSE
        // Test: Empirically highlight Vertex areas for a particular planet.
        if (us.fWriteOld) {
          real tmp, mc, vtx, lon2, lat2, azi, alt;
          lon2 = rDegHalf - lon; lat2 = rDegQuad - lat;
          SwissHouse(is.T, lon2, lat2, us.nHouseSystem, &tmp, &mc, &tmp, &vtx,
            &tmp, &tmp, &tmp, &tmp);
          if (us.fHouse3D) {
            if (MinDistance(planet[us.objRequire], vtx) < 2.0 ||
              MinDistance(planet[us.objRequire], Mod(vtx+rDegHalf)) < 2.0)
              kv = rgbbmp[kRed];
          } else {
            azi = planet[us.objRequire]; alt = planetalt[us.objRequire];
            EclToEqu(&azi, &alt);
            tmp = 0.0; EclToEqu(&mc, &tmp);
            azi = Mod(mc - azi + rDegQuad);
            EquToLocal(&azi, &alt, -lat2);
            azi = rDegMax - azi;
            if (RAbs(alt) < 2.0)
              kv = rgbbmp[kRed];
          }
        }
#endif
        if (gs.fMollweide)
          BmpDarkenKv(lon, lat, lonS, latS, fDoEclipse, &kv);
        BmpSetXY(bmp, x1, y1, kv);
      }
    }
  }

#ifdef WINANY
  if (!gi.fFile)
    BmpCopyToWin(bmp, wi.hdc, 0, 0);
#endif
#ifdef QT
  // Blit what the code above composed onto the chart image, the
  // counterpart of BmpCopyToWin() just above. Same wrap FBmpDrawBack()
  // uses: a Bitmap row is 3 bytes per pixel in B,G,R order padded out to
  // a long boundary, which is exactly Format_BGR888 with a stride of
  // clRow*4, so this borrows the buffer rather than copying it.
  if (!gi.fFile) {
    QImage qimMap((CONST uchar *)bmp->rgb, bmp->x, bmp->y,
      bmp->clRow << 2, QImage::Format_BGR888);
    gi.qpaint->drawImage(0, 0, qimMap);
    gs.xWin = xWinSavQt; gs.yWin = yWinSavQt;
  }
#endif
  return fTrue;
}


// Draw a subsection of the world map bitmap upon a section of the specified
// 24 bit bitmap. Called from the graphic -Nl switch local space chart.

flag FBmpDrawMap2(int x1, int y1, int x2, int y2,
  real rx1, real ry1, real rx2, real ry2)
{
  Bitmap *bmp = &gi.bmp;
  int x12;
  real rx, ry, x3, y3, x4, y4, x34;

  if (!gi.fBmp || (gi.fFile && gs.ft != ftBmp))
    return fFalse;
  if (gi.bmpWorld.rgb == NULL && !FLoadBmp(BITMAP_EARTH, &gi.bmpWorld, fFalse))
    return fFalse;
#ifdef WINANY
  if (!gi.fFile) {
    if (!FAllocateBmp(&wi.bmpWin, gs.xWin, gs.yWin))
      return fFalse;
    bmp = &wi.bmpWin;
  }
#endif
#ifdef QT
  // The same thing WINANY does just above, and for the reason
  // FBmpDrawMap() needs it: "bmp" starts out pointing at gi.bmp, the file
  // export buffer, which nothing allocates on the screen path -- so the
  // BmpSetAll() below would write through a null pointer.
  //
  // No 2:1 fit here, unlike FBmpDrawMap(): this draws into a rectangle
  // the CALLER chose inside a full canvas bitmap, so the canvas is the
  // right size to compose at, exactly as WINANY composes at gs.xWin by
  // gs.yWin.
  if (!gi.fFile) {
    if (gi.qpaint == NULL || !FAllocateBmp(&gi.bmp, gs.xWin, gs.yWin))
      return fFalse;
    bmp = &gi.bmp;
  }
#endif

  rx = (real)gi.bmpWorld.x / rDegMax; ry = (real)gi.bmpWorld.y / rDegHalf;
  BmpSetAll(bmp, KvFromKi(gi.kiOff));
  rx1 = Mod(rx1); rx2 = Mod(rx2);
  x3 = rx1 * rx; y3 = ry1 * ry;
  x4 = rx2 * rx; y4 = ry2 * ry;
  if (x3 < x4 && x4-x3 > rSmall) {
    // In most cases, just copy the entire rectangle all at once.
    BmpCopyBlock2(&gi.bmpWorld, x3, y3, x4, y4, bmp, x1, y1, x2, y2);
  } else {
    // If viewport spans 180W/E, then have to copy twice from Earth bitmap.
    x34 = (real)gi.bmpWorld.x - rSmall;
    x12 = x1 + (int)((real)(x2-x1+1) * (x34 - x3) / (x34 - x3 + x4));
    BmpCopyBlock2(&gi.bmpWorld, x3, y3, x34, y4, bmp, x1,    y1, x12, y2);
    BmpCopyBlock2(&gi.bmpWorld, 0,  y3, x4,  y4, bmp, x12+1, y1, x2,  y2);
  }

#ifdef WINANY
  if (!gi.fFile)
    BmpCopyToWin(bmp, wi.hdc, 0, 0);
#endif
#ifdef QT
  if (!gi.fFile) {
    QImage qimMap((CONST uchar *)bmp->rgb, bmp->x, bmp->y,
      bmp->clRow << 2, QImage::Format_BGR888);
    gi.qpaint->drawImage(0, 0, qimMap);
  }
#endif
  return fTrue;
}


// Adjust the window or bitmap's content to be smoother, and look antialiased.
// This is a simple method different from actually zooming down thicker lines.

flag FBmpAntialias()
{
  Bitmap *bmp = &gi.bmp;
  int xmax = gs.xWin, ymax = gs.yWin, x, y, n1, n2, n3, n4;
  KV kv1, kv2, kv3, kv4;
  real rBlend = !gs.fInverse ? 0.55 : 0.67;

  if (!gi.fBmp || (gi.fFile && gs.ft != ftBmp))
    return fTrue;
#ifdef QT
  // The third function in this file with this shape, after FBmpDrawMap()
  // and FBmpDrawMap2(): everything below works on gi.bmp, which only the
  // file export path allocates, and the WINANY block just below is what
  // would otherwise fill it in from the window. On Qt neither applies,
  // and the chart lives in gi.qim instead, so there is nothing here to
  // antialias -- reading gi.bmp would be a null dereference.
  if (!gi.fFile)
    return fTrue;
#endif
#ifdef WINANY
  // Copy the contents of the window about to be displayed to a bitmap.
  if (!gi.fFile) {
#ifdef WIN
    if (!wi.fBuffer || wi.nScaleWin > 1)
      return fTrue;
#endif
    if (!FBmpCopyFromWin(bmp, wi.hdc, wi.hbmp))
      return fFalse;
    xmax = wi.xClient; ymax = wi.yClient;
  }
#endif

  // Antialias the content on the bitmap.
  for (y = 0; y < ymax - 1; y++)
    for (x = 0; x < xmax - 1; x++) {
      // Check each 2x2 pixel section.
      kv1 = BmpGetXY(bmp, x, y);
      kv2 = BmpGetXY(bmp, x+1, y);
      kv3 = BmpGetXY(bmp, x, y+1);
      kv4 = BmpGetXY(bmp, x+1, y+1);
      // If all four pixels the same, skip this block.
      if (kv1 == kv2 && kv2 == kv3 && kv3 == kv4)
        continue;
      // If there isn't any diagonal of pixels the same, skip.
      if (kv1 != kv4 && kv2 != kv3)
        continue;
      n1 = RgbR(kv1) + RgbG(kv1) + RgbB(kv1);
      n2 = RgbR(kv2) + RgbG(kv2) + RgbB(kv2);
      n3 = RgbR(kv3) + RgbG(kv3) + RgbB(kv3);
      n4 = RgbR(kv4) + RgbG(kv4) + RgbB(kv4);
      if (gs.fInverse) {
        n1 = 768 - n1;
        n2 = 768 - n2;
        n3 = 768 - n3;
        n4 = 768 - n4;
      }
      // If a diagonal of pixels is brigher than the other two, blend.
      if (kv1 == kv4 && n1 >= n2 && n1 >= n3) {
        BmpSetXY(bmp, x+1, y, KvBlend(kv1, kv2, rBlend));
        BmpSetXY(bmp, x, y+1, KvBlend(kv1, kv3, rBlend));
      }
      if (kv2 == kv3 && n2 >= n1 && n2 >= n4) {
        BmpSetXY(bmp, x, y,     KvBlend(kv2, kv1, rBlend));
        BmpSetXY(bmp, x+1, y+1, KvBlend(kv2, kv4, rBlend));
      }
    }

#ifdef WINANY
  if (!gi.fFile)
    BmpCopyToWin(bmp, wi.hdc, -gi.xOffset, -gi.yOffset);
#endif
  return fTrue;
}


/*
******************************************************************************
** Bitmap File Routines.
******************************************************************************
*/

// Write the bitmap array to a previously opened file in a format that can be
// read in by the Unix X11 commands bitmap and xsetroot. The 'mode' parameter
// defines how much white space is put in the file.

void WriteXBitmap(FILE *file, CONST char *szName, char mode)
{
  int x, y, i, temp = 0;
  uint value;
  char szT[cchSzDef], *pchStart, *pchEnd;

  // Determine variable name from filename. szName is the whole output
  // path and has no length limit, so find its last component in szName
  // itself and copy only that: sprintf'ing the path into szT overflowed
  // it for any ordinary deep path (a 95-byte write into 80 bytes, work
  // log item 134).
  for (pchEnd = (char *)szName; *pchEnd != chNull; pchEnd++)
    ;
  for (pchStart = pchEnd; pchStart > szName &&
    *(pchStart-1) != '/' && *(pchStart-1) != '\\'; pchStart--)
    ;
  sprintf2(S(szT), "%s", pchStart);
  pchStart = szT;
  for (pchEnd = pchStart; *pchEnd != chNull && *pchEnd != '.'; pchEnd++)
    ;
  *pchEnd = chNull;

  // Output file header.
  fprintf(file, "#define %s_width %d\n" , pchStart, gs.xWin);
  fprintf(file, "#define %s_height %d\n", pchStart, gs.yWin);
  fprintf(file, "static %s %s_bits[] = {",
    mode != 'V' ? "char" : "short", pchStart);
  for (y = 0; y < gs.yWin; y++) {
    x = 0;
    do {

      // Process each row, eight columns at a time.
      if (y + x > 0)
        fprintf(file, ",");
      if (temp == 0)
        fprintf(file, "\n%s",
          mode == 'N' ? "  " : (mode == 'C' ? " " : ""));
      value = 0;
      // The width test comes first so it actually guards the read: "^"
      // binds tighter than "&&", so with it last the pixel was fetched
      // and only then discarded, running off the end of the final group
      // of columns in every row.
      for (i = (mode != 'V' ? 7 : 15); i >= 0; i--)
        value = (value << 1) + ((x + i < gs.xWin) &&
          ((!(BmGetXY(x+i, y)^(gs.fInverse*15)))^gs.fInverse));
      if (mode == 'N')
        putc(' ', file);
      fprintf(file, "0x");
      if (mode == 'V')
        fprintf(file, "%c%c",
          ChHex(value >> 12), ChHex((value >> 8) & 15));
      fprintf(file, "%c%c",
        ChHex((value >> 4) & 15), ChHex(value & 15));
      temp++;

      // Is it time to skip to the next line while writing the file yet?
      if ((mode == 'N' && temp >= 12) ||
          (mode == 'C' && temp >= 15) ||
          (mode == 'V' && temp >= 11))
        temp = 0;
      x += (mode != 'V' ? 8 : 16);
    } while (x < gs.xWin);
  }
  fprintf(file, "};\n");
}


// Write the bitmap array to a previously opened file in a simple boolean
// Ascii rectangle, one char per pixel, in which '#' represents an off bit and
// '-' an on bit. The output format is identical to the format generated by
// the Unix bmtoa command, and it can be converted into a bitmap with atobm.

void WriteAscii(FILE *file)
{
  int x, y, i;

  for (y = 0; y < gs.yWin; y++) {
    for (x = 0; x < gs.xWin; x++) {
      i = BmGetXY(x, y);
      if (gs.fColor)
        putc(ChHex(i), file);
      else
        putc(i ? '-' : '#', file);
    }
    putc('\n', file);
  }
}


// Write the bitmap array to a previously opened file in the bitmap format
// used in Microsoft Windows for its .bmp extension files. This is a pretty
// efficient format, only requiring a small header, and one bit per pixel
// for monochrome graphics, or four bits per pixel for 16 color bitmaps.

void WriteBmp(FILE *file)
{
  int x, y;
  dword value;

  // BitmapFileHeader
  PutByte('B'); PutByte('M');
  PutLong(14+40 + (gs.fColor ? 64 : 8) +
    (long)4*gs.yWin*(((gs.xWin-1) >> (gs.fColor ? 3 : 5))+1));
  PutWord(0); PutWord(0);
  PutLong(14+40 + (gs.fColor ? 64 : 8));
  // BitmapInfo / BitmapInfoHeader
  PutLong(40);
  PutLong(gs.xWin); PutLong(gs.yWin);
  PutWord(1); PutWord(gs.fColor ? 4 : 1);
  PutLong(0 /*BI_RGB*/); PutLong(0);
  PutLong(0); PutLong(0);
  PutLong(0); PutLong(0);
  // RgbQuad
  if (gs.fColor)
    for (x = 0; x < 16; x++) {
      PutByte(RgbB(rgbbmp[x])); PutByte(RgbG(rgbbmp[x]));
      PutByte(RgbR(rgbbmp[x])); PutByte(0);
    }
  else {
    PutLong(0);
    PutByte(255); PutByte(255); PutByte(255); PutByte(0);
  }
  // Data
  for (y = gs.yWin-1; y >= 0; y--) {
    value = 0;
    for (x = 0; x < gs.xWin; x++) {
      if ((x & (gs.fColor ? 7 : 31)) == 0 && x > 0) {
        PutLong(value);
        value = 0;
      }
      if (gs.fColor)
        value |= (dword)FBmGet(gi.bm, x, y) << (((x & 7) ^ 1) << 2);
      else
        if (FBmGet(gi.bm, x, y))
          value |= (dword)1 << ((x & 31) ^ 7);
    }
    PutLong(value);
  }
}


// Animated GIF output, one frame per chart render: FGenerateGif() in
// xscreen.cpp opens the file and sets gi.fileGif, BeginFileX() hands every
// render that handle instead of opening a file of its own, EndFileX()
// appends the render here as the next frame, and FEndGif() finishes the
// file. Each frame carries its own palette -- the chart's 16 colours
// exactly, the two of a monochrome chart, or for a 24 bit bitmap the
// colours actually used when there are 256 or fewer. Only a frame with
// more, which takes a photographic background, falls back to a fixed 3-3-2
// palette.
//
// The GIF encoding is the vendored cgif library (cgif.cpp, cgif_raw.cpp;
// MIT, see cgif-license.txt), which replaced a hand-written LZW coder here
// because generating thousands of frames was too slow. Every frame after
// the first is sent as a delta: a pixel already showing its colour is
// given a spare transparent index, and the frame is cropped to the
// rectangle holding the rest. Every frame is still lossless: what a viewer
// shows after frame N is exactly the chart rendered for frame N.
//
// FRAMES ARE ENCODED ON WORKER THREADS, gs.nGifThread of them (-YXgt, 0 for
// every core). Rendering cannot move: it is FActionX() over global state.
// So the main thread renders a frame, copies the rows the GIF needs, and
// queues the copy; workers map it to palette indexes, mark and crop the
// delta, and LZW encode it into a buffer of its own; and the main thread
// writes finished frames strictly in order between renders. The output is
// the same bytes at any thread count, which the suite checks.
//
// What a frame depends on is the whole design. Its delta compares it with
// what is SHOWING, and that is the previous frame's render -- the same size
// as the screen and not quantized -- except in two cases: a frame drawn
// smaller than the screen leaves older pixels showing around it, and a
// quantized frame shows its 3-3-2 colours. A worker therefore rebuilds what
// is showing from the previous render's copy, GUESSING whether that frame
// was quantized, and checks the guess once the previous frame is mapped,
// redoing the frame when it was wrong; a successor of a smaller frame, or a
// smaller frame itself, waits for its predecessor instead. When the
// predecessor is already mapped the worker takes what it left showing
// outright, which is also all the single threaded path ever does. Cropping
// a frame that has no transparent index needs the previous frame's
// transparency too -- cgif's rule, kept so the bytes did not change -- and
// waits for it the same way.
//
// The queue is bounded by memory, not frames: renders may run ahead of the
// workers until what is queued holds cbGifQueueMax bytes, and at least two
// frames a thread are always allowed. A short animation is so rendered
// first and compressed after, and FGenerateGif() reports both counts.

#define cGifSlot 1024

// Most bytes of copied renders, palette indexes and encoded frames the
// queue may hold before rendering waits for the workers. 5000 frames of a
// 1600x1360 24 bit chart would be 32 GB unbounded; this is about 55 of them.
#define cbGifQueueMax (512L << 20)

typedef struct _GifPalette {
  KV rgkv[256];
  int rgnSlot[cGifSlot];
  int cKv;
} GIFPAL;

#ifdef GIFTHREADS
typedef std::atomic<long long> GIFCOUNT;
#else
typedef long long GIFCOUNT;
#endif

// A render as a worker sees it: its rows over the screen, copied.
struct GIFSNAP {
  byte *rgb;         // The rows, as the bitmap holds them.
  long cb;           // Bytes in rgb, when this owns them.
  int xs, ys;        // Its size, clipped to the screen.
  long cbRow;        // Bytes in a row.
  flag fBmp;         // 24 bit rows, rather than 4 bits a pixel?
  int cKvFix;        // 16 colour: palette size, 16 or 2 for monochrome,
  KV rgkvFix[16];    // and the colour of each index.
  GIFCOUNT *pcbMem;  // What is counted against cbGifQueueMax.
  GIFSNAP() : rgb(NULL), cb(0), pcbMem(NULL) {}
  ~GIFSNAP() { if (cb > 0) { free(rgb); *pcbMem -= cb; } }
};

// One frame of the GIF, from its render to its encoded bytes.
struct GIFJOB {
  int iFrame;                        // 0 for the first.
  std::shared_ptr<GIFSNAP> psnap;    // Its render.
  std::shared_ptr<GIFSNAP> psnapPrev;  // The previous frame's render.
  std::shared_ptr<GIFJOB> pjobPrev;  // The previous frame, till done.
  flag fShort;       // Smaller than the screen somewhere?
  flag fQuantGuess;  // Was the previous frame quantized, as guessed?
  int nDelay;        // Hundredths of a second.
  // Set by whoever maps and encodes it, read under the pipe's lock.
  flag fMapped;      // Are the palette indexes below final?
  flag fDone;        // Is the encoded frame below final?
  flag fError;       // Did mapping or encoding fail?
  flag fDropShow;    // Its successor rebuilt what shows: free plShow.
  byte *pbPix;       // A palette index for each screen pixel,
  long cbPix;        // which is this many.
  uint32_t *plShow;  // The screen after this frame, for its successor.
  KV rgkv[256];      // The palette,
  int cKv;           // its size,
  int iTrans;        // and its transparent index, or -1 for none.
  flag fQuant;       // Is it the 3-3-2 palette?
  flag fRect;        // Did mapping find the rectangle below? Only a frame
  int xl, yt, xr, yb;  // with no transparent index needs it to.
  byte *pbOut;       // The encoded frame, as it goes in the file.
  long cbOut, cbOutMax;
  GIFCOUNT *pcbMem;
  GIFJOB() : iFrame(0), fShort(fFalse), fQuantGuess(fFalse), nDelay(0),
    fMapped(fFalse), fDone(fFalse), fError(fFalse), fDropShow(fFalse),
    pbPix(NULL), cbPix(0), plShow(NULL), cKv(0), iTrans(-1), fQuant(fFalse),
    fRect(fFalse), xl(0), yt(0), xr(-1), yb(-1), pbOut(NULL), cbOut(0),
    cbOutMax(0), pcbMem(NULL) {}
  ~GIFJOB();
};

// The GIF being written: its screen, its workers and its queue.
struct GIFPIPE {
  int xGif, yGif;          // The logical screen, fixed by frame 1.
  CGIFRaw *praw;           // cgif's stream; header and trailer only.
  CGIFRaw_Config rc;       // What the encoder reads of it.
  FILE *file;
  int cThread;             // Workers; 0 means frames encode inline.
  int cWritten;            // Frames in the file so far.
  flag fQuantLast;         // Was the last frame written quantized?
  flag fError;             // Did writing or encoding a frame fail?
  std::shared_ptr<GIFSNAP> psnapLast;   // Frame N-1, for frame N.
  std::shared_ptr<GIFJOB> pjobLast;
  std::deque<std::shared_ptr<GIFJOB>> qjobFile;  // Queued, not written.
  GIFCOUNT cbMem;          // Bytes queued: renders, indexes, encodings.
#ifdef GIFTHREADS
  std::deque<std::shared_ptr<GIFJOB>> qjobWork;  // Not yet started.
  std::vector<std::thread> rgthread;
  std::mutex mtx;
  std::condition_variable cvWork;   // A job queued, or stop.
  std::condition_variable cvState;  // A job mapped or done, or stop.
  flag fStop;
  flag fHold;    // fGifWorkerHold, until rendering is done.
#endif
  GIFPIPE() : xGif(0), yGif(0), praw(NULL), file(NULL), cThread(0),
    cWritten(0), fQuantLast(fFalse), fError(fFalse), cbMem(0)
#ifdef GIFTHREADS
    , fStop(fFalse), fHold(fFalse)
#endif
    {}
};

GIFJOB::~GIFJOB()
{
  if (pcbMem == NULL)
    return;
  if (pbPix != NULL) { free(pbPix); *pcbMem -= cbPix; }
  if (plShow != NULL) { free(plShow); *pcbMem -= cbPix * sizeof(uint32_t); }
  if (pbOut != NULL) { free(pbOut); *pcbMem -= cbOutMax; }
}

static GIFPIPE *s_ppipeGif = NULL;

// A test's hook on each frame as it reaches the GIF writer, to draw what no
// chart draws. Not under QTTEST: the MSVC build compiles the core once
// without it, and the suite that sets it would not link.
void (*pfnGifFrameHook)(int) = NULL;

// A test's switch, unconditional for the same reason: hold the workers until
// every frame is rendered, so frames are mapped side by side and on guesses.
// A GIF of small frames is otherwise each frame done before the next is
// rendered, and the paths that depend on timing never run.
flag fGifWorkerHold = fFalse;

// Allocate and free what counts against cbGifQueueMax. malloc() rather
// than PAllocate(), whose counters are not the workers' to touch.

static void *PvGifAlloc(GIFCOUNT *pcb, long cb)
{
  void *pv = malloc(cb);

  if (pv != NULL)
    *pcb += cb;
  return pv;
}

static void GifFree(GIFCOUNT *pcb, void *pv, long cb)
{
  if (pv != NULL) {
    free(pv);
    *pcb -= cb;
  }
}

// Output callback for cgif's header and trailer: to the file.

static int NGifWrite(void *pContext, CONST uint8_t *pData, CONST size_t cb)
{
  return fwrite(pData, 1, cb, (FILE *)pContext) == cb ? 0 : -1;
}

// Output callback for an encoded frame: to the job's buffer.

static int NGifBuffer(void *pContext, CONST uint8_t *pData, CONST size_t cb)
{
  GIFJOB *pj = (GIFJOB *)pContext;
  long cbNew;
  byte *pb;

  if (pj->cbOut + (long)cb > pj->cbOutMax) {
    cbNew = Max(pj->cbOutMax * 2, pj->cbOut + (long)cb + 4096);
    pb = (byte *)realloc(pj->pbOut, cbNew);
    if (pb == NULL)
      return -1;
    *pj->pcbMem += cbNew - pj->cbOutMax;
    pj->pbOut = pb;
    pj->cbOutMax = cbNew;
  }
  memcpy(pj->pbOut + pj->cbOut, pData, cb);
  pj->cbOut += (long)cb;
  return 0;
}

// The palette index of a colour, adding it if new. Returns -1 when the
// palette already holds 256 other colours.

static int IGifColor(GIFPAL *pgp, KV kv)
{
  int h = (int)((((dword)kv * 2654435761u) & 0xffffffffu) >> 22);

  while (pgp->rgnSlot[h] >= 0 && pgp->rgkv[pgp->rgnSlot[h]] != kv)
    h = (h + 1) & (cGifSlot-1);
  if (pgp->rgnSlot[h] < 0) {
    if (pgp->cKv >= 256)
      return -1;
    pgp->rgkv[pgp->cKv] = kv;
    pgp->rgnSlot[h] = pgp->cKv++;
  }
  return pgp->rgnSlot[h];
}

#define BGifQuant(kv) ((byte)((RgbR(kv) >> 5) << 5 | (RgbG(kv) >> 5) << 2 | \
  RgbB(kv) >> 6))
#define KvGifQuant(i) Rgb(((i) >> 5)*255/7, (((i) >> 2) & 7)*255/7, \
  ((i) & 3)*255/3)

// What the screen shows after a frame the size of the screen: its render,
// or the 3-3-2 colours of it when the frame was quantized.

static void GifShowFromSnap(CONST GIFSNAP *ps, flag fQuant, uint32_t *rgl,
  int xGif, int yGif)
{
  CONST byte *pbRow;
  uint32_t *pl;
  int x, y, i;

  for (y = 0; y < yGif; y++) {
    pbRow = ps->rgb + (long)y * ps->cbRow;
    pl = rgl + (long)y * xGif;
    if (!ps->fBmp) {
      for (x = 0; x < xGif; x++) {
        i = (pbRow[x >> 1] >> ((x & 1) ? 0 : 4)) & 15;
        if (ps->cKvFix < 16)
          i = (i != 0);
        pl[x] = (uint32_t)ps->rgkvFix[i];
      }
    } else if (!fQuant) {
      for (x = 0; x < xGif; x++, pbRow += cbPixelK)
        pl[x] = (uint32_t)_GetP(pbRow);
    } else {
      for (x = 0; x < xGif; x++, pbRow += cbPixelK) {
        i = BGifQuant(_GetP(pbRow));
        pl[x] = (uint32_t)KvGifQuant(i);
      }
    }
  }
}

#ifdef GIFTHREADS
// Wait for a job's predecessor to be mapped. Returns fFalse on stop.

static flag FGifWaitMapped(GIFPIPE *pp, GIFJOB *pjPrev,
  std::unique_lock<std::mutex> &lk)
{
  pp->cvState.wait(lk, [pp, pjPrev]{ return pp->fStop || pjPrev->fMapped; });
  return !pp->fStop;
}
#endif

// The previous frame's palette indexes and transparent index, waiting for
// it to be mapped if it is not yet.

static flag FGifPrevMask(GIFPIPE *pp, GIFJOB *pj, CONST byte **ppb,
  int *piTrans)
{
  GIFJOB *pjPrev = pj->pjobPrev.get();

#ifdef GIFTHREADS
  std::unique_lock<std::mutex> lk(pp->mtx);
  if (!FGifWaitMapped(pp, pjPrev, lk))
    return fFalse;
#endif
  *ppb = pjPrev->pbPix;
  *piTrans = pjPrev->iTrans;
  return pjPrev->pbPix != NULL;
}

// Map a render to palette indexes and mark what is already showing
// transparent, updating rglShow, what shows, to what shows after it. This is
// the writer as it was when frames were encoded one after another, byte for
// byte, apart from where it is noted.

static flag FGifMapFrame(GIFPIPE *pp, GIFJOB *pj, uint32_t *rglShow)
{
  CONST GIFSNAP *ps = pj->psnap.get();
  GIFPAL gp;
  CONST byte *pbRow, *pbPrev = NULL;
  byte *pb, *rgbPix = pj->pbPix;
  uint32_t *pl;
  KV kv, kvLast = 0;
  uint32_t rglFix[16];
  int x, y, xs, ys, xGif = pp->xGif, yGif = pp->yGif, i, iLast = -1,
    iTrans = -1, iTransPrev = -1;
  flag fFirst = pj->iFrame == 0, fQuant = fFalse, fShort, fDone = fFalse,
    fShowOk = fTrue;

  xs = ps->xs; ys = ps->ys;
  fShort = pj->fShort;
  pj->fRect = fFalse;

  // The usual frame: the same size as the screen and not the first. One
  // pass, a row at a time, both maps each pixel to its palette index and
  // compares it with what is showing -- a pixel already showing its colour
  // is not looked up in the palette at all. In the 24 bit bitmap its index
  // is not known until the palette is, so it is marked 255 meanwhile, and
  // a frame needing a 256th colour gives up here for the full pass below.
  if (!fFirst && !fShort) {
    if (!ps->fBmp) {
      gp.cKv = ps->cKvFix;
      for (i = 0; i < gp.cKv; i++) {
        gp.rgkv[i] = ps->rgkvFix[i];
        rglFix[i] = (uint32_t)gp.rgkv[i];
      }
      iTrans = gp.cKv;
      for (y = 0; y < ys; y++) {
        pbRow = ps->rgb + (long)y * ps->cbRow;
        pb = rgbPix + (long)y * xGif;
        pl = rglShow + (long)y * xGif;
        for (x = 0; x < xs; x++) {
          i = (pbRow[x >> 1] >> ((x & 1) ? 0 : 4)) & 15;
          if (gp.cKv < 16)
            i = (i != 0);
          if (pl[x] == rglFix[i])
            pb[x] = (byte)iTrans;
          else {
            pb[x] = (byte)i;
            pl[x] = rglFix[i];
          }
        }
      }
      fDone = fTrue;
    } else {
      for (i = 0; i < cGifSlot; i++)
        gp.rgnSlot[i] = -1;
      gp.cKv = 0;
      fDone = fTrue;
      // What shows is left alone until the pass is known to finish: the
      // full pass below, and the rectangle of a frame with no transparent
      // index, compare with it as it was. (When this pass updated it as it
      // went, a frame giving up here compared with a screen half moved on.)
      for (y = 0; y < ys && fDone; y++) {
        pbRow = ps->rgb + (long)y * ps->cbRow;
        pb = rgbPix + (long)y * xGif;
        pl = rglShow + (long)y * xGif;
        for (x = 0; x < xs; x++, pbRow += cbPixelK) {
          kv = _GetP(pbRow);
          if (pl[x] == (uint32_t)kv) {
            pb[x] = 255;
            continue;
          }
          if (kv != kvLast || iLast < 0) {
            iLast = IGifColor(&gp, kv);
            kvLast = kv;
            if (iLast < 0 || iLast >= 255) {
              fDone = fShowOk = fFalse;
              break;
            }
          }
          pb[x] = (byte)iLast;
        }
      }
      if (fDone) {
        iTrans = gp.cKv;
        pl = rglShow;
        for (pb = rgbPix; pb < rgbPix + (long)xGif * yGif; pb++, pl++) {
          if (*pb == 255)
            *pb = (byte)iTrans;
          else
            *pl = (uint32_t)gp.rgkv[*pb];
        }
      }
    }
  }

  // Every other frame: map every pixel of the render to a palette index,
  // a row at a time, then compare the frame with what is showing.
  if (!fDone && !ps->fBmp) {
    gp.cKv = ps->cKvFix;
    for (i = 0; i < gp.cKv; i++)
      gp.rgkv[i] = ps->rgkvFix[i];
    for (y = 0; y < ys; y++) {
      pbRow = ps->rgb + (long)y * ps->cbRow;
      pb = rgbPix + (long)y * xGif;
      for (x = 0; x < xs; x++) {
        i = (pbRow[x >> 1] >> ((x & 1) ? 0 : 4)) & 15;
        pb[x] = (byte)(gp.cKv == 16 ? i : (i != 0));
      }
    }
  } else if (!fDone) {
    for (i = 0; i < cGifSlot; i++)
      gp.rgnSlot[i] = -1;
    gp.cKv = 0;
    // The palette starts over, so must the last lookup. Without this, a
    // frame that gave up above on the colour at its top left corner mapped
    // that corner's run to an index the new palette never gave it.
    iLast = -1;
    for (y = 0; y < ys && !fQuant; y++) {
      pbRow = ps->rgb + (long)y * ps->cbRow;
      pb = rgbPix + (long)y * xGif;
      for (x = 0; x < xs; x++, pbRow += cbPixelK) {
        kv = _GetP(pbRow);
        if (kv != kvLast || iLast < 0) {
          iLast = IGifColor(&gp, kv);
          kvLast = kv;
          if (iLast < 0) {
            fQuant = fTrue;
            break;
          }
        }
        pb[x] = (byte)iLast;
      }
    }
  }
  // What a smaller frame leaves showing of the last one, in this frame's
  // palette. A 16 colour palette is the same every frame, so every such
  // colour is in it already.
  for (y = 0; y < yGif && fShort && !fDone && !fQuant; y++) {
    pb = rgbPix + (long)y * xGif;
    pl = rglShow + (long)y * xGif;
    for (x = y < ys ? xs : 0; x < xGif; x++) {
      if (ps->fBmp)
        iLast = IGifColor(&gp, pl[x]);
      else
        for (iLast = gp.cKv-1; iLast > 0 && gp.rgkv[iLast] != pl[x]; iLast--)
          ;
      if (iLast < 0) {
        fQuant = fTrue;
        break;
      }
      pb[x] = (byte)iLast;
    }
  }
  if (fQuant && !fDone) {
    for (i = 0; i < 256; i++)
      gp.rgkv[i] = KvGifQuant(i);
    gp.cKv = 256;
    for (y = 0; y < yGif; y++) {
      pbRow = ps->rgb + (long)y * ps->cbRow;
      pb = rgbPix + (long)y * xGif;
      pl = rglShow + (long)y * xGif;
      for (x = 0; x < xGif; x++, pbRow += cbPixelK)
        if (x < xs && y < ys)
          pb[x] = BGifQuant(_GetP(pbRow));
        else
          pb[x] = BGifQuant(pl[x]);
    }
  }

  // A viewer leaves each frame in place, so a pixel already showing its
  // colour need not be sent again: it gets the spare index just past the
  // palette, which is transparent. The first frame sends everything, and
  // so does one whose palette is full and has no index to spare -- or
  // whose comparison above gave up part way, which only a frame of 255 or
  // more changed colours does. Such a frame is cropped to what differs
  // from the screen or was transparent in the frame before, as cgif did.
  if (!fDone && !fFirst && gp.cKv < 256 && fShowOk)
    iTrans = gp.cKv;
  if (!fDone && !fFirst && iTrans < 0) {
    if (!FGifPrevMask(pp, pj, &pbPrev, &iTransPrev))
      return fFalse;
    pj->fRect = fTrue;
    pj->xl = xGif; pj->yt = yGif; pj->xr = pj->yb = -1;
  }
  for (y = 0; y < yGif && !fDone; y++) {
    pb = rgbPix + (long)y * xGif;
    pl = rglShow + (long)y * xGif;
    for (x = 0; x < xGif; x++) {
      kv = gp.rgkv[pb[x]];
      if (iTrans >= 0 && pl[x] == (uint32_t)kv)
        pb[x] = (byte)iTrans;
      else {
        if (pj->fRect && (pl[x] != (uint32_t)kv ||
          (iTransPrev >= 0 && pbPrev[(long)y * xGif + x] == iTransPrev))) {
          pj->xl = Min(pj->xl, x); pj->xr = Max(pj->xr, x);
          pj->yt = Min(pj->yt, y); pj->yb = y;
        }
        pl[x] = (uint32_t)kv;
      }
    }
  }

  for (i = 0; i < gp.cKv; i++)
    pj->rgkv[i] = gp.rgkv[i];
  pj->cKv = gp.cKv;
  pj->iTrans = iTrans;
  pj->fQuant = fQuant;
  return fTrue;
}

// Crop a mapped frame to what it changes and encode it into the job's
// buffer: graphic control extension, image descriptor, local colour table
// and LZW data, the bytes cgif_raw_addframe() would write.

static flag FGifEncodeFrame(GIFPIPE *pp, GIFJOB *pj)
{
  CGIFRaw_FrameConfig fc;
  byte rgbPal[3*256], *pbCrop = NULL, *pb;
  KV kv;
  int x, y, xl, yt, xr, yb, i;
  int xGif = pp->xGif, yGif = pp->yGif;
  long cbCrop = 0;
  cgif_result r;

  if (pj->iFrame == 0) {
    xl = yt = 0; xr = xGif-1; yb = yGif-1;
  } else if (pj->fRect) {
    xl = pj->xl; yt = pj->yt; xr = pj->xr; yb = pj->yb;
  } else {
    // The rectangle of what is not transparent, each edge found by working
    // in from it and stopping at the first pixel sent, as cgif does.
    xl = xGif; xr = -1;
    for (yt = 0; yt < yGif; yt++) {
      pb = pj->pbPix + (long)yt * xGif;
      for (x = 0; x < xGif && pb[x] == pj->iTrans; x++)
        ;
      if (x < xGif) {
        xl = x;
        for (x = xGif-1; pb[x] == pj->iTrans; x--)
          ;
        xr = x;
        break;
      }
    }
    for (yb = yGif-1; yb > yt; yb--) {
      pb = pj->pbPix + (long)yb * xGif;
      for (x = 0; x < xGif && pb[x] == pj->iTrans; x++)
        ;
      if (x < xGif) {
        xl = Min(xl, x);
        for (x = xGif-1; pb[x] == pj->iTrans; x--)
          ;
        xr = Max(xr, x);
        break;
      }
    }
    for (y = yt+1; y < yb; y++) {
      pb = pj->pbPix + (long)y * xGif;
      for (x = 0; x < xl && pb[x] == pj->iTrans; x++)
        ;
      xl = x;
      for (x = xGif-1; x > xr && pb[x] == pj->iTrans; x--)
        ;
      xr = x;
    }
    if (yt >= yGif)
      xr = -1;
  }
  if (xr < 0) {    // Nothing changed: one pixel, as cgif sends.
    xl = xr = yt = yb = 0;
  }

  ClearB((pbyte)&fc, sizeof(fc));
  if (pj->iFrame == 0)
    fc.pImageData = pj->pbPix;
  else {
    cbCrop = (long)(xr - xl + 1) * (yb - yt + 1);
    pbCrop = (byte *)PvGifAlloc(pj->pcbMem, cbCrop);
    if (pbCrop == NULL)
      return fFalse;
    for (y = yt; y <= yb; y++)
      memcpy(pbCrop + (long)(y - yt) * (xr - xl + 1),
        pj->pbPix + (long)y * xGif + xl, xr - xl + 1);
    fc.pImageData = pbCrop;
  }
  // The local colour table, with the transparent index's entry if used.
  for (i = 0; i < pj->cKv + (pj->iTrans >= 0); i++) {
    kv = i < pj->cKv ? pj->rgkv[i] : 0;
    rgbPal[3*i] = (byte)RgbR(kv); rgbPal[3*i+1] = (byte)RgbG(kv);
    rgbPal[3*i+2] = (byte)RgbB(kv);
  }
  fc.pLCT = rgbPal;
  fc.sizeLCT = (uint16_t)(pj->cKv + (pj->iTrans >= 0));
  if (pj->iTrans >= 0) {
    fc.attrFlags = CGIF_RAW_FRAME_ATTR_HAS_TRANS;
    fc.transIndex = (uint8_t)pj->iTrans;
  }
  fc.width = (uint16_t)(xr - xl + 1); fc.height = (uint16_t)(yb - yt + 1);
  fc.left = (uint16_t)xl; fc.top = (uint16_t)yt;
  fc.delay = (uint16_t)pj->nDelay;
  fc.disposalMethod = DISPOSAL_METHOD_LEAVE;
  r = cgif_raw_encodeframe(&pp->rc, &fc, NGifBuffer, pj);
  GifFree(pj->pcbMem, pbCrop, cbCrop);
  return r == CGIF_OK;
}

// Map and encode one frame: on a worker, or inline when there are none.

static void GifRunJob(GIFPIPE *pp, GIFJOB *pj)
{
  GIFJOB *pjPrev = pj->pjobPrev.get();
  long cbShow = (long)pp->xGif * pp->yGif * sizeof(uint32_t);
  uint32_t *rglShow = NULL;
  flag fOk = fTrue, fKnown = fTrue, fQuantPrev = pj->fQuantGuess;

  // What shows before this frame. Taken from the previous frame when it is
  // mapped; rebuilt from its render, on a guess, when it is not; waited for
  // when a frame smaller than the screen is involved, since then only the
  // previous frame knows.
  if (pjPrev != NULL) {
#ifdef GIFTHREADS
    std::unique_lock<std::mutex> lk(pp->mtx);
    if (!pjPrev->fMapped && (pjPrev->fShort || pj->fShort) &&
      !FGifWaitMapped(pp, pjPrev, lk))
      fOk = fFalse;
#endif
    if (fOk && pjPrev->fMapped) {
      fQuantPrev = pjPrev->fQuant;
      rglShow = pjPrev->plShow;
      pjPrev->plShow = NULL;
    } else if (fOk) {
      pjPrev->fDropShow = fTrue;
      fKnown = fFalse;
    }
  }
  if (fOk && rglShow == NULL && pjPrev != NULL && pj->psnapPrev == NULL)
    fOk = fFalse;    // A predecessor that failed, encoding inline.
  if (fOk && rglShow == NULL) {
    rglShow = (uint32_t *)PvGifAlloc(pj->pcbMem, cbShow);
    if (rglShow == NULL)
      fOk = fFalse;
    else if (pjPrev != NULL)
      GifShowFromSnap(pj->psnapPrev.get(), fQuantPrev, rglShow, pp->xGif,
        pp->yGif);
  }
  while (fOk) {
    fOk = FGifMapFrame(pp, pj, rglShow);
    if (!fOk || fKnown)
      break;
    // Check the guess, and do the frame again if it was wrong.
    {
#ifdef GIFTHREADS
      std::unique_lock<std::mutex> lk(pp->mtx);
      if (!FGifWaitMapped(pp, pjPrev, lk)) {
        fOk = fFalse;
        break;
      }
#endif
      fKnown = fTrue;
      if (pjPrev->fQuant == fQuantPrev)
        break;
      fQuantPrev = pjPrev->fQuant;
    }
    GifShowFromSnap(pj->psnapPrev.get(), fQuantPrev, rglShow, pp->xGif,
      pp->yGif);
  }
  pj->psnap.reset();
  pj->psnapPrev.reset();
  {
#ifdef GIFTHREADS
    std::unique_lock<std::mutex> lk(pp->mtx);
#endif
    if (fOk && !pj->fDropShow)
      pj->plShow = rglShow;
    else
      GifFree(pj->pcbMem, rglShow, cbShow);
    pj->fMapped = fTrue;
    pj->fError = !fOk;
#ifdef GIFTHREADS
    pp->cvState.notify_all();
#endif
  }
  if (fOk)
    fOk = FGifEncodeFrame(pp, pj);
  {
#ifdef GIFTHREADS
    std::unique_lock<std::mutex> lk(pp->mtx);
#endif
    pj->fError = !fOk;
    pj->fDone = fTrue;
    pj->pjobPrev.reset();
#ifdef GIFTHREADS
    pp->cvState.notify_all();
#endif
  }
}

#ifdef GIFTHREADS
static void GifWorker(GIFPIPE *pp)
{
  std::unique_lock<std::mutex> lk(pp->mtx);
  std::shared_ptr<GIFJOB> pj;

  loop {
    pp->cvWork.wait(lk, [pp]{ return pp->fStop ||
      (!pp->fHold && !pp->qjobWork.empty()); });
    if (pp->fStop)
      break;
    pj = pp->qjobWork.front();
    pp->qjobWork.pop_front();
    lk.unlock();
    GifRunJob(pp, pj.get());
    pj.reset();
    lk.lock();
  }
}
#endif

// How many threads encode an animated GIF, as gs.nGifThread asks: a count,
// or 0 for one a core. 1 encodes on the main thread, as does any build whose
// compiler has no std::thread.

int NGifThreadMax()
{
#ifdef GIFTHREADS
  return Min(Max((int)std::thread::hardware_concurrency(), 1),
    cGifThreadMax);
#else
  return 1;
#endif
}

int NGifThreads()
{
#ifdef GIFTHREADS
  return gs.nGifThread > 0 ? Min(gs.nGifThread, cGifThreadMax) :
    NGifThreadMax();
#else
  return 1;
#endif
}

// Write the frames at the head of the queue that are finished, in order.
// Returns fFalse once any frame has failed.

static flag FGifWriteDone(GIFPIPE *pp)
{
  std::shared_ptr<GIFJOB> pj;

  loop {
    {
#ifdef GIFTHREADS
      std::unique_lock<std::mutex> lk(pp->mtx);
#endif
      if (pp->qjobFile.empty() || !pp->qjobFile.front()->fDone)
        break;
      pj = pp->qjobFile.front();
      pp->qjobFile.pop_front();
    }
    if (pj->fError || fwrite(pj->pbOut, 1, pj->cbOut, pp->file) !=
      (size_t)pj->cbOut)
      pp->fError = fTrue;
    pp->fQuantLast = pj->fQuant;
    pp->cWritten++;
    pj.reset();
  }
  return !pp->fError && !ferror(pp->file);
}

// Append the chart just rendered to the open animated GIF as its next
// frame, starting the file if this is the first one. The first frame fixes
// the logical screen. A later frame of another size is clipped to it, and
// where it is smaller the rest of the screen keeps showing the frame
// before, as a viewer shows it, since no frame is ever cleared. The frame
// is queued for the workers, or encoded here if there are none, and every
// finished frame is written.

static flag FWriteGifFrame(FILE *file)
{
  GIFPIPE *pp = s_ppipeGif;
  std::shared_ptr<GIFSNAP> psnap;
  std::shared_ptr<GIFJOB> pj;
  int xs, ys, i, cThread;
  long cb;

  if (pfnGifFrameHook != NULL)
    (*pfnGifFrameHook)(gi.cGifFrame);
  if (gi.fBmp) {
    xs = gi.bmp.x; ys = gi.bmp.y;
  } else {
    xs = gs.xWin; ys = gs.yWin;
  }
  if (gi.cGifFrame == 0) {
    if (xs <= 0 || ys <= 0 || xs > 0xFFFF || ys > 0xFFFF || pp != NULL)
      return fFalse;
    pp = new GIFPIPE;
    s_ppipeGif = pp;
    pp->xGif = xs; pp->yGif = ys;
    pp->file = file;
    ClearB((pbyte)&pp->rc, sizeof(pp->rc));
    pp->rc.attrFlags = CGIF_RAW_ATTR_IS_ANIMATED |
      (gi.fGifLoop ? 0 : CGIF_RAW_ATTR_NO_LOOP);
    pp->rc.width = (uint16_t)xs; pp->rc.height = (uint16_t)ys;
    pp->rc.numLoops = CGIF_INFINITE_LOOP;
    pp->rc.pWriteFn = NGifWrite;
    pp->rc.pContext = file;
    pp->praw = cgif_raw_newgif(&pp->rc);    // The header, written now.
    if (pp->praw == NULL)
      return fFalse;
    gi.xGif = xs; gi.yGif = ys;
    cThread = NGifThreads();
#ifdef GIFTHREADS
    pp->fHold = fGifWorkerHold;
    // A thread that cannot be started leaves fewer; none, and frames are
    // encoded inline. The vector is sized first, so growing it can never
    // throw with a running thread in hand.
    try {
      pp->rgthread.reserve(cThread > 1 ? cThread : 0);
      for (i = 0; cThread > 1 && i < cThread; i++)
        pp->rgthread.emplace_back(GifWorker, pp);
    } catch (...) {
    }
    pp->cThread = (int)pp->rgthread.size();
#else
    (void)cThread; (void)i;
#endif
  }
  if (pp == NULL || pp->praw == NULL || pp->fError)
    return fFalse;
  xs = Min(xs, pp->xGif); ys = Min(ys, pp->yGif);
  if (xs <= 0 || ys <= 0)
    return fFalse;

  // Copy what the GIF needs of the render. Encoding inline, it is read
  // before the next render, so the bitmap is used where it lies.
  psnap = std::make_shared<GIFSNAP>();
  psnap->pcbMem = &pp->cbMem;
  psnap->xs = xs; psnap->ys = ys;
  psnap->fBmp = gi.fBmp;
  psnap->cbRow = gi.fBmp ? (long)gi.bmp.clRow << 2 : (long)gi.cbBmpRow;
  psnap->cKvFix = gs.fColor ? 16 : 2;
  for (i = 0; i < psnap->cKvFix; i++)
    psnap->rgkvFix[i] = gs.fColor ? rgbbmp[i] : (i ? Rgb(255, 255, 255) : 0);
  if (pp->cThread == 0)
    psnap->rgb = gi.fBmp ? gi.bmp.rgb : gi.bm;
  else {
    cb = psnap->cbRow * ys;
    psnap->rgb = (byte *)PvGifAlloc(&pp->cbMem, cb);
    if (psnap->rgb == NULL)
      return fFalse;
    psnap->cb = cb;
    memcpy(psnap->rgb, gi.fBmp ? gi.bmp.rgb : gi.bm, cb);
  }

  pj = std::make_shared<GIFJOB>();
  pj->pcbMem = &pp->cbMem;
  pj->iFrame = gi.cGifFrame;
  pj->fShort = gi.cGifFrame > 0 && (xs < pp->xGif || ys < pp->yGif);
  pj->fQuantGuess = pp->fQuantLast;
  pj->nDelay = gi.nGifDelay;
  pj->psnap = psnap;
  pj->cbPix = (long)pp->xGif * pp->yGif;
  pj->pbPix = (byte *)PvGifAlloc(&pp->cbMem, pj->cbPix);
  if (pj->pbPix == NULL)
    return fFalse;
  if (pp->cThread > 0) {
    pj->psnapPrev = pp->psnapLast;
    pp->psnapLast = psnap;
  }
  pj->pjobPrev = pp->pjobLast;
  pp->pjobLast = pj;
  gi.cGifFrame++;
#ifdef GIFTHREADS
  if (pp->cThread > 0) {
    std::unique_lock<std::mutex> lk(pp->mtx);
    pp->qjobFile.push_back(pj);
    pp->qjobWork.push_back(pj);
    lk.unlock();
    pp->cvWork.notify_one();
    return !pp->fError && !ferror(file);
  }
#endif
  pp->qjobFile.push_back(pj);
  GifRunJob(pp, pj.get());
  return FGifWriteDone(pp);
}

// Frames of the animated GIF being written that are in the file so far.

int NGifWritten()
{
  return s_ppipeGif != NULL ? s_ppipeGif->cWritten : 0;
}

// Write what the workers have finished. With gpWaitRoom, first wait (a
// little) for room in the queue if it is full; with gpWaitAll, for any frame
// still being encoded. *pfMore says whether there is still no room, or
// still a frame to come. Returns fFalse once a frame has failed.

flag FGifPump(int gp, flag *pfMore)
{
  GIFPIPE *pp = s_ppipeGif;
  flag fMore = fFalse;

  if (pp == NULL) {
    *pfMore = fFalse;
    return fTrue;
  }
#ifdef GIFTHREADS
  if (pp->cThread > 0 && gp != gpNoWait) {
    std::unique_lock<std::mutex> lk(pp->mtx);
    if (pp->fHold) {
      pp->fHold = fFalse;
      pp->cvWork.notify_all();
    }
    auto fnWaiting = [pp, gp]() {
      if (pp->qjobFile.empty() || pp->qjobFile.front()->fDone)
        return fFalse;
      return gp == gpWaitAll ? fTrue :
        (flag)((int)pp->qjobFile.size() >= 2*pp->cThread &&
        pp->cbMem >= cbGifQueueMax);
    };
    if (fnWaiting())
      pp->cvState.wait_for(lk, std::chrono::milliseconds(50),
        [&]() { return !fnWaiting(); });
  }
#endif
  if (!FGifWriteDone(pp))
    return fFalse;
#ifdef GIFTHREADS
  if (pp->cThread > 0) {
    std::unique_lock<std::mutex> lk(pp->mtx);
    fMore = gp == gpWaitAll ? !pp->qjobFile.empty() :
      ((int)pp->qjobFile.size() >= 2*pp->cThread &&
      pp->cbMem >= cbGifQueueMax);
  }
#endif
  *pfMore = fMore;
  return fTrue;
}

// Finish the animated GIF FGenerateGif() is writing: with fFinish, every
// frame still queued and the trailer; without it, stop the workers and
// drop what they hold, which a cancel wants done at once. Frees what the
// frames used. Returns whether all of it was written; the caller still
// closes the file.

flag FEndGif(flag fFinish)
{
  GIFPIPE *pp = s_ppipeGif;
  flag fOk = fTrue, fMore;

  if (pp == NULL)
    return fTrue;
  while (fFinish && fOk && !pp->fError) {
    fOk = FGifPump(gpWaitAll, &fMore);
    if (!fMore)
      break;
  }
#ifdef GIFTHREADS
  {
    std::unique_lock<std::mutex> lk(pp->mtx);
    pp->fStop = fTrue;
    pp->qjobWork.clear();
    pp->cvWork.notify_all();
    pp->cvState.notify_all();
  }
  for (auto &th : pp->rgthread)
    th.join();
#endif
  if (fFinish && (!pp->qjobFile.empty() || pp->cWritten != gi.cGifFrame))
    fOk = fFalse;
  pp->qjobFile.clear();
  pp->psnapLast.reset();
  pp->pjobLast.reset();
  if (pp->praw != NULL) {
    // The trailer. cgif's raw stream says PENDING, never OK, when every
    // frame reached the file some other way than its own addframe.
    if (fFinish) {
      if (cgif_raw_close(pp->praw) == CGIF_EWRITE)
        fOk = fFalse;
    } else
      free(pp->praw);    // What cgif_raw_close() frees, without a trailer.
  }
  if (pp->fError)
    fOk = fFalse;
  s_ppipeGif = NULL;
  delete pp;
  return fOk;
}


#define LFlipB(l) ((((l) & 0xff) << 24) | (((l) & 0xff00) << 8) | \
  (((l) & 0xff0000) >> 8) | (((l) & 0xff000000) >> 24))

// Output a PNG chunk to a PNG file being composed.

void WritePNGChunk(FILE *file, CONST char szType[5], byte *rgb, dword cb,
  CONST dword rglCrc[256])
{
  dword dwT, crc, n;

  dwT = LFlipB(cb);
  fwrite(&dwT, 1, 4, file);
  fwrite(szType, 1, 4, file);
  if (cb > 0)
    fwrite(rgb, 1, cb, file);

  // Compute CRC for this chunk.
  crc = 0xffffffffL;
  for (n = 0; n < 4; n++)
    crc = rglCrc[(crc ^ szType[n]) & 0xff] ^ (crc >> 8);
  for (n = 0; n < cb; n++)
    crc = rglCrc[(crc ^ rgb[n])    & 0xff] ^ (crc >> 8);
  crc ^= 0xffffffffL;
  crc = LFlipB(crc);
  fwrite(&crc, 1, 4, file);
}


// Output a bitmap to file in Portable Network Graphics (PNG) format. Note
// that the PNG file produced will not be compressed any.

flag WritePNG(CONST Bitmap *b, FILE *file)
{
  dword rglCrc[256], crc, dwT, dwAdler;
  int n, k, ib = 0, x, y, s1 = 1, s2 = 0, i;
  byte rgbHead[13], *rgb;
  KV kv;

  // Initialize CRC table for chunk CRC's.
  for (n = 0; n < 256; n++) {
    crc = n;
    for (k = 0; k < 8; k++)
      crc = (crc & 1) ? (0xEDB88320L ^ (crc >> 1)) : (crc >> 1);
    rglCrc[n] = crc;
  }

  // PNG signature
  CONST byte png_signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  fwrite(png_signature, 1, 8, file);

  // IHDR chunk
  dwT = LFlipB(b->x);
  CopyRgb((pbyte)&dwT, rgbHead, 4);
  dwT = LFlipB(b->y);
  CopyRgb((pbyte)&dwT, rgbHead + 4, 4);
  rgbHead[8]  = 8;  // Bit depth
  rgbHead[9]  = 2;  // Color type: Truecolor (RGB)
  rgbHead[10] = 0;  // Compression method
  rgbHead[11] = 0;  // Filter method
  rgbHead[12] = 0;  // Interlace method
  WritePNGChunk(file, "IHDR", rgbHead, 13, rglCrc);

  // Deflate block (no compression)
  int cbRow, cbDataBmp, cRowBlock, cBlock, iBlock, cbBlock;
  cbRow = b->x*cbPixelK + 1;
  cbDataBmp = cbRow * b->y;
  // One deflate block header holds up to 65535 bytes; a window wider
  // than ~21,845 pixels would zero the divisor. FValidGraphX caps the
  // width far below that, and this pins it.
  Assert(cbRow <= 65535);
  cRowBlock = 65535 / cbRow;
  cBlock = (b->y + (cRowBlock - 1)) / cRowBlock;

  rgb = PAllocate(2 + 5*cBlock + cbDataBmp + 4,
    "PNG data"); // zlib header + deflate + data + adler32
  if (rgb == NULL)
    return fFalse;
  rgb[ib++] = 0x78;  // Zlib header for no compression
  rgb[ib++] = 0x01;

  for (iBlock = 0; iBlock < cBlock; iBlock++) {
    // Uncompressed deflate block
    cbBlock = cbRow * ((iBlock+1)*cRowBlock <= b->y ? cRowBlock :
      b->y - iBlock*cRowBlock);
    rgb[ib++] = (iBlock >= cBlock-1);  // 0x1 for final block, no compression
    rgb[ib++] = cbBlock & 0xff;
    rgb[ib++] = (cbBlock >> 8) & 0xff;
    rgb[ib++] = ~cbBlock & 0xff;
    rgb[ib++] = (~cbBlock >> 8) & 0xff;

    for (y = iBlock*cRowBlock; y < (iBlock+1)*cRowBlock && y < b->y; y++) {
      rgb[ib++] = 0;
      for (x = 0; x < b->x; x++) {
        kv = BmpGetXY(b, x, y);
        rgb[ib++] = RgbR(kv);
        rgb[ib++] = RgbG(kv);
        rgb[ib++] = RgbB(kv);
      }
    }
    // Compute Adler-32 checksum for this block
    for (i = 0; i < cbBlock; i++) {
      s1 = (s1 + rgb[ib - cbBlock + i]) % 65521;
      s2 = (s2 + s1) % 65521;
    }
  }

  // Output Adler-32 checksum
  dwAdler = (s2 << 16) + s1;
  rgb[ib++] = (dwAdler >> 24) & 0xff;
  rgb[ib++] = (dwAdler >> 16) & 0xff;
  rgb[ib++] = (dwAdler >> 8) & 0xff;
  rgb[ib++] = dwAdler & 0xff;
  Assert(ib == 2 + 5*cBlock + cbDataBmp + 4);

  // IDAT chunk
  WritePNGChunk(file, "IDAT", rgb, ib, rglCrc);
  DeallocateP(rgb);

  // IEND chunk
  WritePNGChunk(file, "IEND", NULL, 0, rglCrc);
  return fTrue;
}


// Begin the work of creating a graphics file. Prompt for a filename if need
// be, and if valid, create the file and open it for writing.

flag BeginFileX()
{
  char sz[cchSzMax];

  if (us.fNoWrite)
    return fFalse;
  if (gi.fileGif != NULL) {    // An animated GIF is open: render into it.
    gi.file = gi.fileGif;
    return fTrue;
  }
#ifdef WIN
  if (gi.szFileOut == NULL)
    return fFalse;
#endif

#ifndef WIN
  if (gi.szFileOut == NULL && (gs.ft != ftBmp || (gs.ft == ftBmp &&
    (gs.chBmpMode == 'B' || gs.chBmpMode == 'P')))) {
    sprintf2(S(sz), "(It is recommended to specify an extension of '.%s'.)\n",
      gs.ft == ftBmp ? (gs.chBmpMode == 'B' ? "bmp" : "png") :
      gs.ft == ftWmf ? "wmf" : (gs.ft == ftSVG ? "svg" : (gs.ft == ftPS ?
#ifdef PSCRIPT
      (gs.fPSComplete ? "ps" : "eps")
#else
      "ps"
#endif
      : "dw")));
    PrintSzScreen(sz);
  }
#endif // WIN

  loop {
#ifndef WIN
    if (gi.szFileOut == NULL) {
      sprintf2(S(sz), "Enter name of file to write %s to", gs.ft == ftBmp ?
        "bitmap" : (gs.ft == ftPS ? "PostScript" : (gs.ft == ftWmf ?
        "metafile" : (gs.ft == ftSVG ? "SVG" : "wireframe"))));
      InputString(sz, S(sz));
      FCloneSz(sz, &gi.szFileOut);
   }
#else
    // If autosaving in potentially rapid succession, ensure the file isn't
    // being opened by some other application before saving over it again.
    if (wi.fAutoSave) {
      if (wi.hMutex == NULL)
        wi.hMutex = CreateMutex(NULL, fFalse, szAppName);
      if (wi.hMutex != NULL)
        WaitForSingleObject(wi.hMutex, 1000);
    }
#endif
    gi.file = fopen(gi.szFileOut, (gs.ft == ftBmp && (gs.chBmpMode == 'B' ||
      gs.chBmpMode == 'P')) || gs.ft == ftWmf ? "wb" : "w");
    if (gi.file != NULL)
      break;
#ifdef WIN
    if (wi.fAutoSave)
      break;
#endif
    sprintf2(S(sz), "Couldn't create output file: %s", gi.szFileOut);
    PrintWarning(sz);
    FCloneSz(NULL, &gi.szFileOut);
#ifdef WIN
    break;
#endif
  }
  return gi.file != NULL;
}


// Finish up the work of creating a graphics file. This basically consists of
// just calling the appropriate routine to actually write the data in memory
// to a file for bitmaps and metafiles, although for PostScript just close the
// file as were already writing while creating the chart.

void EndFileX()
{
  if (gi.file == NULL)
    return;
  if (gi.file == gi.fileGif) {    // The next frame of an animated GIF.
    if (gs.ft != ftBmp || !FWriteGifFrame(gi.file))
      gi.fGifError = fTrue;
    gi.file = NULL;
    return;
  }
  if (gs.ft == ftBmp) {
    PrintProgress("Writing chart bitmap to file.");
    if (gs.chBmpMode == 'B') {
      if (!gi.fBmp)
        WriteBmp(gi.file);
      else
        WriteBmp2(&gi.bmp, gi.file);
    } else if (gs.chBmpMode == 'A')
      WriteAscii(gi.file);
    else if (gs.chBmpMode == 'P')
      WritePNG(&gi.bmp, gi.file);
    else
      WriteXBitmap(gi.file, gi.szFileOut, gs.chBmpMode);
  }
#ifdef PSCRIPT
  else if (gs.ft == ftPS)
    PsEnd();
#endif
#ifdef METAFILE
  else if (gs.ft == ftWmf) {
    PrintProgress("Writing metafile to file.");
    WriteMeta(gi.file);
  }
#endif
#ifdef SVG
  else if (gs.ft == ftSVG) {
    if (gi.kiSvgAct != kMax)
      fprintf(gi.file, "</g>\n");
    fprintf(gi.file, "</g>\n</svg>\n");
  }
#endif
#ifdef WIRE
  else if (gs.ft == ftWire) {
    PrintProgress("Writing wireframe to file.");
    WriteWire(gi.file);
  }
#endif
  fclose(gi.file);
#ifdef WIN
  if (wi.fAutoSave && wi.hMutex != NULL)
    ReleaseMutex(wi.hMutex);
  if (wi.wCmd == cmdSaveWallTile || wi.wCmd == cmdSaveWallCenter ||
    wi.wCmd == cmdSaveWallStretch || wi.wCmd == cmdSaveWallFit ||
    wi.wCmd == cmdSaveWallFill) {
    WriteProfileString("Desktop", "TileWallpaper",
      wi.wCmd == cmdSaveWallTile ? "1" : "0");
    WriteProfileString("Desktop", "WallpaperStyle",
      wi.wCmd == cmdSaveWallStretch ? "2" : (wi.wCmd == cmdSaveWallFit ? "6" :
      (wi.wCmd == cmdSaveWallFill ? "10" : "0")));
    SystemParametersInfo(SPI_SETDESKWALLPAPER, 0, gi.szFileOut,
      SPIF_UPDATEINIFILE | SPIF_SENDWININICHANGE);
    wi.wCmd = 0;
  }
#endif
}


#ifdef PSCRIPT
/*
******************************************************************************
** PostScript File Routines.
******************************************************************************
*/

// Table of PostScript header alias lines used by the program.

CONST char szPsFunctions[] =
"/languagelevel where{pop languagelevel}{1}ifelse"
" 2 lt{\n"
"/sf{exch findfont exch"
" dup type/arraytype eq{makefont}{scalefont}ifelse setfont}bind def\n"
"/rf{gsave newpath\n"
"4 -2 roll moveto"
" dup 0 exch rlineto exch 0 rlineto neg 0 exch rlineto closepath\n"
"fill grestore}bind def\n"
"/rc{newpath\n"
"4 -2 roll moveto"
" dup 0 exch rlineto exch 0 rlineto neg 0 exch rlineto closepath\n"
"clip newpath}bind def\n"
"}{/sf/selectfont load def/rf/rectfill load def"
"/rc/rectclip load def}ifelse\n"
"/center{0 begin gsave dup 4 2 roll"
" translate newpath 0 0 moveto"
" false charpath flattenpath pathbbox"
" /URy exch def/URx exch def/LLy exch def/LLx exch def"
" URx LLx sub 0.5 mul LLx add neg URy LLy sub 0.5 mul LLy add neg"
" 0 0 moveto rmoveto"
" show grestore end}bind def\n"
"/center load 0 4 dict put\n"
"/c{setrgbcolor}bind def\n"
"/d{moveto 0 0 rlineto}bind def\n"
"/l{4 2 roll moveto lineto}bind def\n"
"/t{lineto}bind def\n"
"/el{newpath matrix currentmatrix 5 1 roll translate scale"
" 0 0 1 0 360 arc setmatrix stroke}bind def\n"
"/ef{newpath matrix currentmatrix 5 1 roll translate scale"
" 0 0 1 0 360 arc fill setmatrix stroke}bind def\n";


// Write a command to flush the PostScript buffer.

void PsStrokeForce()
{
  if (gi.cStroke > 0) {              // Render any existing path
    fprintf(gi.file, "stroke\n");
    gi.cStroke = 0;
    gi.xPen = -1;                    // Invalidate PolyLine cache
  }
}


// Indicate that a certain number of PostScript commands have been done.

void PsStroke(int n)
{
  gi.cStroke += n;
  if (gi.cStroke > 2000)    // Whenever we reach a certain limit, flush.
    PsStrokeForce();
}


// Set the type of line end to be used by PostScript commands. If linecap is
// true, then the line ends are rounded, otherwise they are squared.

void PsLineCap(flag fLineCap)
{
  if (fLineCap != gi.fLineCap) {
    PsStrokeForce();
    fprintf(gi.file, "%d setlinecap\n", fLineCap);
    gi.fLineCap = fLineCap;
  }
}


// Set the dash length to be used by PostScript line commands.

void PsDash(int dashoff)
{
  if (dashoff != gi.nDash) {
    PsStrokeForce();
    if (dashoff)
      fprintf(gi.file, "[%d %d", PSMUL, dashoff * PSMUL);
    else
      fprintf(gi.file, "[");
    fprintf(gi.file, "]0 setdash\n");
    gi.nDash = dashoff;
  }
}


// Set a linewidth size to be used by PostScript figure primitive commands.

void PsLineWidth(int nWidth)
{
  nWidth += gs.nThickAdjust;
  if (nWidth < 1)
    nWidth = 1;
  if (nWidth != gi.nLineWid) {
    PsStrokeForce();
    fprintf(gi.file, "%d setlinewidth\n", nWidth);
    gi.nLineWid = nWidth;
  }
}


// Set a system font and size to be used by PostScript text commands.

void PsFont(int nFont)
{
  CONST char *szFont;
  int z;

  if (nFont == gi.nFontPS || gs.nFontAll == 0)
    return;
  szFont = rgszFontName[nFont];
  z = PSMUL*gi.nScale;
  if (nFont == fiArial) {
    szFont = "Times-Roman"; z = 4*PSMUL*gi.nScaleText;
  } else if (nFont == fiCourier || nFont == fiAstrolog) {
    szFont = "Courier"; z = 5*PSMUL*gi.nScaleText;
  }
  fprintf(gi.file, "/%s[%d 0 0 -%d 0 0]sf\n", szFont, z, z);
  gi.nFontPS = nFont;
}


// Write out initial file header information to the PostScript file.

void PsBegin()
{
  fprintf(gi.file, "%%!PS-Adobe-2.0");
  if (!gs.fPSComplete)
    fprintf(gi.file, " EPSF-2.0");
  fprintf(gi.file, "\n%%%%Title: %s\n", gi.szFileOut);
  fprintf(gi.file, "%%%%Creator: %s %s\n", szAppName, szVersionCore);
  fprintf(gi.file, "%%%%CreationDate: %s\n", szDateCore);
  if (!gs.fPSComplete) {
    fprintf(gi.file, "%%%%BoundingBox: 0 0 %d %d\n", gs.xWin, gs.yWin);
    fprintf(gi.file, "%%%%EndComments\n");
    fprintf(gi.file, "%%%%BeginSetup\n");
    fprintf(gi.file, szPsFunctions);
    fprintf(gi.file, "%%%%EndSetup\n");
    fprintf(gi.file, "0 0 %d %d rc\n", gs.xWin, gs.yWin);
  } else {
    fprintf(gi.file, "%%%%Pages: 1 1\n");
    fprintf(gi.file, "%%%%DocumentFonts: (atend)\n");
    fprintf(gi.file, "%%%%BoundingBox: %d %d %d %d\n", PSGUTTER, PSGUTTER,
      (int)(gs.xInch*72.0+rRound)-PSGUTTER,
      (int)(gs.yInch*72.0+rRound)-PSGUTTER);
    fprintf(gi.file, "%%%%EndComments\n");
    fprintf(gi.file, "%%%%BeginProcSet: common\n");
    fprintf(gi.file, szPsFunctions);
    fprintf(gi.file, "%%%%EndProcSet\n");
    fprintf(gi.file, "%%%%Page: 1 1\n");
  }
  PsFont(fiAstrolog);
  fprintf(gi.file, "gsave\n");
  PsLineWidth(!gs.fThick ? gi.nPenWid/2 : gi.nPenWid*2);
  gi.xPen = -1;
  PrintProgress("Creating PostScript chart file.");
}


// Write out trailing information to the PostScript file.

void PsEnd()
{
  int i;

  PsStrokeForce();
  if (!gs.fPSComplete)
    fprintf(gi.file, "%%%%EOF\n");
  else {
    fprintf(gi.file, "showpage\n");
    fprintf(gi.file, "%%%%PageTrailer\n");
    fprintf(gi.file, "%%%%Trailer\n");
    fprintf(gi.file, "%%%%DocumentFonts: Times-Roman\n");
    if (gs.nFontAll > 0)
      for (i = 1; i < cFont; i++)
        fprintf(gi.file, "%%%%+ %s\n",
          i != fiCourier ? rgszFontName[i] : "Courier");
  }
}
#endif // PSCRIPT


#ifdef METAFILE
/*
******************************************************************************
** Metafile Routines.
******************************************************************************
*/

// Output one 16 bit or 32 bit value into the metafile buffer stream.

void MetaWord(word w)
{
  char sz[cchSzDef];

  if ((pbyte)gi.pwMetaCur - gi.bm >= gi.cbMeta) {
    sprintf2(S(sz), "Metafile would be more than %ld bytes.", gi.cbMeta);
    PrintError(sz);
    Terminate(tcFatal);
    // Terminate() RETURNS when us.fNoQuit is set (-0q, general.cpp:3377),
    // and without this the guard would report the overflow and then
    // perform it -- one word past the end of gi.bm per call, for the rest
    // of the drawing. Truncating is what the message above already claims
    // happens.
    return;
  }
  *gi.pwMetaCur = w;
  gi.pwMetaCur++;
}

void MetaLong(long l)
{
  MetaWord(WLo(l));
  MetaWord(WHi(l));
}


// Output a string into the metafile buffer stream.

void MetaSz(CONST char *sz)
{
  while (*sz) {
    MetaWord(WFromBB(sz[0], sz[1]));
    if (sz[1] == chNull)
      break;
    sz += 2;
  }
}


// Output any necessary metafile records to make the current actual settings
// of line color, fill color, etc, be those that are desired. This is
// generally called by the primitives routines before any figure record is
// actually written into a metafile. Wait until the last moment before
// changing any settings to ensure that unnecessary records aren't output,
// e.g. two select colors in a row.

void MetaSelect()
{
  if (gi.pwPoly != NULL) {    // Invalidate PolyLine cache
    gi.pwPoly = NULL;
    MetaPoint(gi.xPen, gi.yPen, rgbbmp[gi.kiPoly]);
    gi.xPen = -1;
  }
  if (gi.kiLineDes != gi.kiLineAct) {
    MetaSelectObject(gi.kiLineDes);
    gi.kiLineAct = gi.kiLineDes;
  }
  if (gi.kiFillDes != gi.kiFillAct) {
    MetaSelectObject(16*4 + gi.kiFillDes);
    gi.kiFillAct = gi.kiFillDes;
  }
  if (gi.nFontDes != gi.nFontAct) {
    MetaSelectObject(16*5 + gi.nFontDes);
    gi.nFontAct = gi.nFontDes;
  }
  if (gi.kiTextDes != gi.kiTextAct) {
    MetaTextColor(rgbbmp[gi.kiTextDes]);
    gi.kiTextAct = gi.kiTextDes;
  }
  if (gi.nAlignDes != gi.nAlignAct) {
    MetaTextAlign(gi.nAlignDes);
    gi.nAlignAct = gi.nAlignDes;
  }
}


// Output initial metafile header information into the metafile buffer. Also
// setup and create all pen, brush, and font objects that may possibly be used
// in the generation and playing of the picture.

void MetaInit()
{
  int i, j, k;

  gi.pwMetaCur = (word *)gi.bm;
  // Placeable Metaheader
  MetaLong(0x9ac6cdd7);
  MetaWord(0);                             // Not used
  MetaWord(0); MetaWord(0);
  MetaWord(gs.xWin); MetaWord(gs.yWin);
  MetaWord(gs.xWin/6);                     // Units per inch
  MetaLong(0L);                            // Not used
  MetaWord(0x9ac6 ^ 0xcdd7 ^ gs.xWin ^ gs.yWin ^ gs.xWin/6);  // Checksum
  // Metaheader
  MetaWord(1);                             // Metafile type
  MetaWord(9);                             // Size of header in words
  MetaWord(0x300);                         // Windows version
  MetaLong(0L);                            // Size of entire metafile in words
  MetaWord(16*5+1+(gs.nFontAll>0)*(cFont-1)); // Number of objects in metafile
  MetaLong(17L);                           // Size of largest record in words
  MetaWord(0);                             // Not used
  // Setup
  MetaEscape(17);
  MetaLong(LFromBB('A', 's', 't', 'r'));  // "Astr"
  MetaWord(4);                            // Creator
  MetaLong(14L);                          // Bytes in string
  MetaLong(LFromBB('A', 's', 't', 'r'));  // "Astr"
  MetaLong(LFromBB('o', 'l', 'o', 'g'));  // "olog"
  MetaLong(LFromBB(' ', szVerCore[0], '.', szVerCore[1]));  // Version
  MetaWord(WFromBB(szVerCore[2], 0));     // Version part 2
  MetaSaveDc();
  MetaWindowOrg(0, 0);
  MetaWindowExt(gs.xWin, gs.yWin);
  MetaBkMode(1 /* Transparent */);
  // Colors
  for (j = 1; j <= 4; j++)
    for (i = 0; i < 16; i++) {
      k = (j <= 1 ? gi.nPenWid * (1 + gs.fThick) : 0);
      k += gs.nThickAdjust;
      if (k < 0)
        k = 0;
      MetaCreatePen(j <= 2 ? 0 : j-2 /* PS_SOLID; PS_DASH; PS_DOT */,
        k, rgbbmp[i]);
    }
  for (i = 0; i < cColor; i++) {
    MetaCreateBrush(0 /* BS_SOLID */, rgbbmp[i]);
  }
  MetaCreateBrush(1 /* BS_NULL */, 0L);
  // Fonts
  if (gs.nFontAll > 0)
    for (i = 1; i < cFont; i++) {
      j = (CchSz(rgszFontName[i]) + 1) >> 1;
      MetaCreateFont(j, 0, i < fiArial ? -METAMUL*gi.nScale : yFontT,
        i == fiWingding ? 2 /* Symbol Charset */ : 0 /* Ansi Charset */);
      j = (i == fiWingding || i >= fiArial ?
        1 /* Draft */ : 0 /* Default */);
      k = (i == fiWingding || i >= fiCourier ?
          1 /* Fixed */ : 2 /* Variable */) |
        (i <= fiWingding ? 0x10 /* Roman */ :
          (i >= fiCourier ? 0x30 /* Modern */ : 0 /* Don't Care */));
      MetaWord(WFromBB(j, k));
      MetaSz(rgszFontName[i]);
    }
  gi.kiLineAct = gi.kiFillAct = gi.nFontAct = gi.kiTextAct = gi.nAlignAct = -1;
}


// Output trailing records to indicate the end of the metafile and then
// actually write out the entire buffer to the specifed file.

void WriteMeta(FILE *file)
{
  word *w;
#if FALSE
  int i;

  for (i = 16*5+1+(gs.nFontAll > 0)*4; i >= 0; i--) {
    MetaDeleteObject(i);
  }
#endif
  MetaRestoreDc();
  MetaRecord(3, 0);    // End record
  // Two words, not *(long *). A metafile's mtSize is a 32 bit field, and
  // "long" is 32 bits only on Windows: on every 64 bit Unix this wrote
  // EIGHT bytes into a four byte slot and took the two fields after it
  // with it. Measured on a real file before the fix -- mtNoObjects read 0
  // where MetaInit had written 81, and mtMaxRecord read 0 where it had
  // written 17. Every .wmf this program produced on Linux or macOS
  // carried that.
  //
  // Found by UBSan, which called it a misaligned store: gi.bm+28 is four
  // byte aligned and an eight byte type wants eight. The alignment was
  // the symptom; the width was the bug. AddressSanitizer never saw it,
  // because the write stays inside gi.bm's own allocation.
  //
  // WLo/WHi in sequence is how MetaLong() writes every other long in this
  // file, so this now matches the format it is writing.
  {
    word *pwSize = (word *)(gi.bm + 22 + 6);
    long lSize = ((long)((pbyte)gi.pwMetaCur - gi.bm) - 22) / 2;

    pwSize[0] = WLo(lSize);
    pwSize[1] = WHi(lSize);
  }
  for (w = (word *)gi.bm; w < gi.pwMetaCur; w++) {
    PutWord(*w);
  }
}
#endif // METAFILE


#ifdef SVG
/*
******************************************************************************
** Scalable Vector Graphics File Routines.
******************************************************************************
*/

// Output a SVG color change if needed. It's needed only when the color we
// want to use is different from the current color in the SVG file.

void SvgSetColor()
{
  if (gi.kiSvgDes == gi.kiSvgAct)
    return;
  if (gi.kiSvgAct != kMax)
    fprintf(gi.file, "</g>\n");
  fprintf(gi.file, "<g style=\"stroke:%s\">\n", SzColorHTML(gi.kiSvgDes));
  gi.kiSvgAct = gi.kiSvgDes;
}


// Output a string of text to an SVG file.

void SvgText(CONST char *sz, int ch, int x, int y, int nFont, int nScale,
  flag fCenter)
{
  CONST char *pch;
  wchar wch;
  int n;

  SvgSetColor();
  fprintf(gi.file, "<text x=\"%d\" y=\"%d\" fill=\"%s\" stroke=\"none\"",
    x, y, SzColorHTML(gi.kiSvgAct));
  if (fCenter)
    fprintf(gi.file, " text-anchor=\"middle\" dominant-baseline=\"middle\"");
  fprintf(gi.file, " style=\"font-family:%s; font-size:%dpx;",
    rgszFontName[nFont], nScale);
  if (sz != NULL)
    fprintf(gi.file, " white-space:pre");
  fprintf(gi.file, "\">");
  if (sz == NULL) {
    // Empty input string means to use the input char as a length one string.
    wch = ch;
    pch = "";
    n = 0;
    goto LChar;
  } else
    pch = sz;
  while (*pch) {
    n = PchToWch((uchar *)pch, &wch);
LChar:
    // Encode certain Unicode characters that need to be in XML files.
    if (wch == '&')
      fprintf(gi.file, "&amp;");
    else if (wch == '<')
      fprintf(gi.file, "&lt;");
    else if (wch == '>')
      fprintf(gi.file, "&gt;");
    else if (wch < 128)
      fprintf(gi.file, "%c", (uchar)wch);
    else
      fprintf(gi.file, "&#%d;", (int)wch);
    pch += n;
  }
  fprintf(gi.file, "</text>\n");
}
#endif // SVG


#ifdef WIRE
/*
******************************************************************************
** Daedalus Wireframe File Routines.
******************************************************************************
*/

// Write the wireframe file in memory to a previously opened file in the
// Daedalus wireframe format. This usually consists of coordinates for each
// line segment, but can also include changes to the default color.

void WriteWire(FILE *file)
{
  word *pw = (word *)gi.bm;
  int x1, y1, z1, x2, y2, z2, n, nR, nG, nB;
  flag fRgb;
  KV kv;

  if (file == NULL)
    return;
  fprintf(file, "DW#\n%d\n", gi.cWire);
  while (pw < gi.pwWireCur) {
    if (*pw != 32768) {

      // Output one line segment. Six words, and the loop condition only
      // knows that ONE remains -- so check before reading the other five
      // rather than after.
      if (pw + 6 > gi.pwWireCur)
        break;
      x1 = (short)pw[0]; y1 = (short)pw[1]; z1 = (short)pw[2];
      x2 = (short)pw[3]; y2 = (short)pw[4]; z2 = (short)pw[5];
      fprintf(file, "%d %d %d %d %d %d\n", x1, y1, z1, x2, y2, z2);
      pw += 6;
    } else {

      // Output a color change. The record is two words for a palette
      // index and three when the index is invalid and an RGB triple
      // follows (WireLine() writes it that way).
      //
      // **Its length has to be read whether or not the color is being
      // written out.** Advancing by two unconditionally leaves a
      // three-word record one word short, and everything after it is read
      // at the wrong offset.
      if (pw + 2 > gi.pwWireCur)
        break;
      n = BLo(pw[1]);
      fRgb = (n >= cColor2);
      if (fRgb && pw + 3 > gi.pwWireCur)
        break;
      if (gs.fColor) {
        if (!fRgb) {
          kv = rgbbmp[n];
          if (kv != rgbbmpDef[n] || n >= kIndigo)
            fprintf(file, "Rgb %d %d %d\n", RgbR(kv), RgbG(kv), RgbB(kv));
          else
            fprintf(file, "%s\n", szColor[n]);
        } else {
          // Invalid color index means RGB stored in current and next word.
          nR = BHi(pw[1]); nG = BLo(pw[2]); nB = BHi(pw[2]);
          if (nR == nG && nG == nB)
            fprintf(file, "GrayN %d\n", nR);
          else
            fprintf(file, "Rgb %d %d %d\n", nR, nG, nB);
        }
      }
      pw += 2 + fRgb;
    }
  }
}


// Add a single 16 bit number to the current wireframe file.

void WireNum(int n)
{
  char sz[cchSzDef];

  if ((pbyte)gi.pwWireCur - gi.bm >= gi.cbWire) {
    sprintf2(S(sz), "Wireframe would be more than %ld bytes.", gi.cbWire);
    PrintError(sz);
    Terminate(tcFatal);
    return;      // Terminate() returns under -0q; see WriteMetaWord().
  }
  *gi.pwWireCur = (word)n;
  gi.pwWireCur++;
}


// Add a solid line to current wireframe file, specified by its endpoints.

void WireLine(int x1, int y1, int z1, int x2, int y2, int z2)
{
  KV kv;

  if (!FBetween(x1, -32767, 32767) || !FBetween(x2, -32767, 32767) ||
      !FBetween(y1, -32767, 32767) || !FBetween(y2, -32767, 32767) ||
      !FBetween(z1, -32767, 32767) || !FBetween(z2, -32767, 32767))
    return;

  if (gi.kiInFile != gi.kiCur) {
    gi.kiInFile = gi.kiCur;
    WireNum(32768);
    if (gi.kiCur >= 0)
      WireNum(gi.kiCur);
    else {
      kv = -gi.kiCur;
      WireNum(WFromBB(kMax,     RgbR(kv)));
      WireNum(WFromBB(RgbG(kv), RgbB(kv)));
    }
  }
  WireNum(x1); WireNum(y1); WireNum(z1);
  WireNum(x2); WireNum(y2); WireNum(z2);
  gi.cWire++;
}


// Add a small but still visible dot to the current wireframe file.

void WireSpot(int x, int y, int z)
{
  WireLine(x-1, y, z, x+1, y, z);
  WireLine(x, y-1, z, x, y+1, z);
  WireLine(x, y, z-1, x, y, z+1);
}


// Add a circle to the current wireframe file. The circle may be tilted and
// rotated in any configuration. Used when drawing rings of planets.

void WireCircle(int x, int y, int z, real r, real tilt, real rot)
{
  int m = 0, n = 0, o = 0, u, v, w, i, di;
  real alt, azi;

  i = NAbs((int)r) << 1;
  di = 2 + (i < 90) + (i < 72) + (i < 60) + (i < 40);
  for (i = 0; i <= nDegMax; i += di) {
    azi = (real)i; alt = 0.0;
    if (tilt != 0.0)
      CoorXform(&azi, &alt, tilt);
    azi += rot;
    u = x + (int)(r * RCosD(alt) * RSinD(azi));
    v = y + (int)(r * RCosD(alt) * RCosD(azi));
    w = z + (int)(r * RSinD(alt));
    if (i > 0)
      WireLine(m, n, o, u, v, w);
    m = u; n = v; o = w;
  }
}


// Add an octahedron of a given radius to the current wireframe file. These
// shapes are used to mark the exact locations of planets in the scene.

void WireOctahedron(int x, int y, int z, int r)
{
  int rgx[4], rgy[4], i;

  rgx[0] = rgx[3] = x-r; rgx[1] = rgx[2] = x+r;
  rgy[0] = rgy[1] = y-r; rgy[2] = rgy[3] = y+r;
  for (i = 0; i < 4; i++) {
    WireLine(rgx[i], rgy[i], z, x, y, z-r);
    WireLine(rgx[i], rgy[i], z, x, y, z+r);
    WireLine(rgx[i], rgy[i], z, rgx[(i+1) & 3], rgy[(i+1) & 3], z);
  }
}


// Add a sphere of a given radius to the current wireframe file. These shapes
// are used to mark the exact locations of zoomed in planets in the scene.

void WireSphere(int x, int y, int z, int r)
{
  real rgx[12+1], rgy[12+1], rgr[6+1];
  int rgz[6+1], i, j, x1, y1, x2, y2;

  for (i = 0; i < 12; i++) {
    rgx[i] = RCosD((real)(i * nDegMax / 12));
    rgy[i] = RSinD((real)(i * nDegMax / 12));
  }
  rgx[12] = rgx[0]; rgy[12] = rgy[0];
  for (i = 0; i <= 6; i++) {
    rgz[i] = z + (int)((real)r * rgx[i]);
    rgr[i] = (real)r * rgy[i];
  }
  for (j = 1; j <= 6; j++) {
    x2 = x + (int)rgr[j]; y2 = y;
    for (i = 0; i < 12; i++) {
      x1 = x2; y1 = y2;
      x2 = x + (int)(rgr[j] * rgx[i+1]);
      y2 = y + (int)(rgr[j] * rgy[i+1]);
      if (j < 6)
        WireLine(x1, y1, rgz[j], x2, y2, rgz[j]);
      x1 = x + (int)(rgr[j-1] * rgx[i+1]);
      y1 = y + (int)(rgr[j-1] * rgy[i+1]);
      WireLine(x2, y2, rgz[j], x1, y1, rgz[j-1]);
    }
  }
}


// Add a fixed star to the current wireframe file.

void WireStar(int x, int y, int z, ES *pes)
{
  int n;

  // Determine star color.
  if (gs.fColor) {
    if (pes->ki != kDefault)
      n = pes->ki;
    else {
      n = 255 - (int)((pes->mag - rStarLite) / rStarSpan * 224.0);
      n = Min(n, 255); n = Max(n, 32);
      n = -(int)Rgb(n, n, n);
    }
    DrawColor(n);
  }

  // Draw star point.
  if (!FOdd(gs.nAllStar))
    WireSpot(x, y, z);
  else
    WireOctahedron(x, y, z, 3 * gi.nScaleT);

  // Draw star's name label.
  if (!gs.fLabel || gs.nAllStar < 2)
    return;
  gi.zDefault = z + 8*gi.nScaleT;
  DrawSz(pes->pchBest, x, y, dtCent);
}


// Given longitude and latitude values on a globe, return the 3D pixel
// coordinates corresponding to them. In other words, project the globe in
// the 3D environment, and return where our coordinates got projected to.
// Like FGlobeCalc() except for 3D wireframe format.

void WireGlobeCalc(real x1, real y1, int *u, int *v, int *w, int rz, real deg)
{
  // Compute coordinates for a general globe invoked with -XG switch.

  if (gi.nMode == gSphere) {
    // Chart sphere coordinates are relative to the local horizon.
    x1 = Mod(rDegMax - (x1 + cp0.lonMC) + rDegQuad);
    y1 = rDegQuad - y1;
    EquToLocal(&x1, &y1, rDegQuad - Lat);
    y1 = rDegQuad - y1;
  }

  x1 = Mod(x1+deg);       // Shift by current globe rotation value.
  if (gs.rTilt != 0.0) {
    // Do another coordinate shift if the globe's equator is tilted any.
    y1 = rDegQuad - y1;
    CoorXform(&x1, &y1, gs.rTilt);
    x1 = Mod(x1); y1 = rDegQuad - y1;
  }
  *u = (int)((real)rz*RSinD(y1)*RSinD(x1)-rRound);
  *v = (int)((real)rz*RSinD(y1)*RCosD(x1)-rRound);
  *w = (int)((real)rz*RCosD(y1)-rRound);
}


// Given longitude and latitude values, return the 3D pixel coordinates
// corresponding to them. Like FMapCalc() except for 3D wireframe format.

void WireMapCalc(real x1, real y1, int *xp, int *yp, int *zp, flag fSky,
  real rT, int rz, real deg)
{
  if (!fSky)
    x1 = cp0.lonMC - x1;
  if (x1 < 0.0)
    x1 += rDegMax;
  if (x1 > rDegHalf)
    x1 -= rDegMax;
  x1 = Mod(rDegHalf - rT - x1);
  y1 = rDegQuad - y1;
  WireGlobeCalc(x1, y1, xp, yp, zp, rz, deg);
}


// Draw a globe, for either the world or the constellations. Shift the chart
// by specified rotational and tilt values, and may plot on the chart each
// planet at its zenith position on Earth or location in constellations. Like
// DrawMap() except for 3D wireframe format.

void WireDrawGlobe(flag fSky, real deg)
{
  int rz, unit = 12*gi.nScale, x, y, z, xold, yold, zold = 0,
    m, n, o, u, v, w, i, j, k, l;
  real planet1[objMax], planet2[objMax], x1, y1, rT;
  ES es;

  // Set up some variables.
  rz = Min(gs.xWin/2, gs.yWin/2);
  if (gi.nMode == gSphere)
    rz -= 7*gi.nScale;

  // Draw the map (either a constellation map, or a world map).

  rT = fSky ? rDegMax - deg : deg;
#ifdef CONSTEL
  if (fSky)
    EnumConstelLines(NULL, NULL, NULL, NULL, NULL);
  else
#endif
    EnumWorldLines(NULL, NULL, NULL, NULL, NULL);
  while (
#ifdef CONSTEL
    fSky ? EnumConstelLines(&xold, &yold, &x, &y, &i) :
#endif
    EnumWorldLines(&xold, &yold, &x, &y, &i)) {
    if (fSky) {
      if (i > 0)
        DrawColor(gi.nMode == gSphere || !gs.fAlt ? gi.kiGray : kDkGreenB);
      else
        DrawColor(gi.nMode == gSphere ? kPurpleB :
          (gs.fAlt ? kBlueB : kDkBlueB));
    } else {
      if (i >= 0)
        DrawColor(!gs.fAlt && !gs.fColorHouse ? gi.kiGray :
          (i ? kRainbowB[i] : kDkBlueB));
    }
    // For globes, have to do a complicated transformation.
    WireGlobeCalc((real)xold, (real)yold, &m, &n, &o, rz, rT);
#ifdef CONSTEL
    if (fSky && i > 0) {
      gi.zDefault = o;
      DrawSz(szCnstlAbbrev[i], m, n, dtCent | dtScale2);
      continue;
    }
#endif
    WireGlobeCalc((real)x, (real)y, &u, &v, &w, rz, rT);
    WireLine(m, n, o, u, v, w);
  }

  // Now, if in an appropriate bonus chart mode, draw each planet at its
  // zenith or visible location on the globe, if not hidden.

  if (gs.fAlt || gi.nMode == gAstroGraph || gi.nMode == gSphere)
    return;
  rT = gs.fConstel ? rDegHalf : Lon;
  if (rT < 0.0)
    rT += rDegMax;
  j = Max(is.nObj, oMC);
  for (i = 0; i <= j; i++) {
    planet1[i] = Tropical(planet[i]);
    planet2[i] = planetalt[i];
    EclToEqu(&planet1[i], &planet2[i]);    // Calculate zenith long. & lat.
  }

  // Draw ecliptic equator and zodiac sign wedges.

  if (gs.fHouseExtra) {
    if (!gs.fColorSign)
      DrawColor(kDkGreenB);
    for (l = -2; l < cSign; l++) {
      if (gs.fColorSign && l >= 0)
        DrawColor(kSignB(l+1));
      for (i = -90; i <= 90; i++) {
        if (gs.fColorSign && l < 0 && i % 30 == 0)
          DrawColor(kSignB((i+90)/30 + (l < -1)*6 + 1));
        if (l >= 0) {
          // Coordinates for zodiac sign wedge longitude lines.
          j = l*30; k = i;
        } else {
          // Coordinates for ecliptic equator latitude line.
          j = i+90 + (l < -1)*180; k = 0;
        }
        x1 = Tropical((real)j);
        y1 = (real)k;
        EclToEqu(&x1, &y1);
        WireMapCalc(x1, y1, &u, &v, &w, fSky, rT, rz, deg);
        WireSpot(u, v, w);
      }
    }
  }

  // Draw Earth's equator.

  if (gs.fEquator) {
    DrawColor(kPurpleB);
    for (i = 0; i < nDegMax; i++) {
      x1 = (real)i; y1 = 90.0;
      WireGlobeCalc(x1, y1, &j, &k, &l, rz, deg);
      WireSpot(j, k, l);
    }
  }

  // Draw chart latitude and plot chart location.

  if (us.fLatitudeCross && !fSky) {
    DrawColor(kMagentaB);
    for (i = 0; i < nDegMax; i++) {
      x1 = (real)i; y1 = rDegQuad - Lat;
      WireGlobeCalc(x1, y1, &j, &k, &l, rz, deg);
      WireSpot(j, k, l);
    }
    x1 = Mod(rDegHalf - Lon); y1 = rDegQuad - Lat;
    WireGlobeCalc(x1, y1, &j, &k, &l, rz, deg);
    WireOctahedron(j, k, l, gi.nScale);
  }

#ifdef SWISS
  // Draw extra stars.

  if (gs.fAllStar) {
    DrawColor(gi.kiGray);
    SwissComputeStar(0.0, NULL);
    while (SwissComputeStar(is.T, &es)) {
      x1 = es.lon; y1 = es.lat;
      x1 = Tropical(x1);
      EclToEqu(&x1, &y1);
      WireMapCalc(x1, y1, &j, &k, &l, fSky, rT, rz, deg);
      WireStar(j, k, l, &es);
    }
  }

  // Draw extra asteroids.

  if (gs.nAstLo > 0) {
    DrawColor(gi.kiGray);
    SwissComputeAsteroid(0.0, NULL, fFalse);
    while (SwissComputeAsteroid(is.T, &es, fFalse)) {
      x1 = es.lon; y1 = es.lat;
      x1 = Tropical(x1);
      EclToEqu(&x1, &y1);
      WireMapCalc(x1, y1, &j, &k, &l, fSky, rT, rz, deg);
      WireStar(j, k, l, &es);
    }
  }
#endif

  // Draw exoplanets.

  if (gs.fAllExo) {
    EnumExoplanets(NULL);
    while (EnumExoplanets(&es)) {
      x1 = es.lon; y1 = es.lat;
      WireMapCalc(x1, y1, &j, &k, &l, fSky, rT, rz, deg);
      WireStar(j, k, l, &es);
    }
  }

  // Draw grid of triangles or squares over the planet.

  DrawMapTriangles(fTrue, (real)rz, NULL, deg);   // globe: integer radius

#ifdef ATLAS
  // Draw locations of cities from atlas.

  if (gs.fLabelCity && !fSky && FEnsureAtlas()) {
    if (!gs.fLabelAsp)
      DrawColor(kOrangeB);
    KiCity(-1);
    for (i = 0; i < is.cae; i++) {
      x1 = nDegHalf - is.rgae[i].lon;
      y1 = rDegQuad - is.rgae[i].lat;
      WireGlobeCalc(x1, y1, &u, &v, &w, rz, deg);
      if (gs.fLabelAsp) {
        j = KiCity(i);
        if (j == ~0)
          continue;
        DrawColor(j);
      }
      WireSpot(u, v, w);
    }
  }
#endif

  // Draw MC, IC, Asc, and Des lines for each object, as great circles around
  // the globe. The result is a 3D astrocartography chart.

  if (!fSky) for (i = 0; i <= is.nObj; i++) if (FProper(i)) {
    for (k = 0; k < arMax; k++) {
      if (ignorez[!k ? arDes : (k == 1 ? arIC : (k == 2 ? arAsc :
        (k == 3 ? arMC : k)))])
        continue;
      DrawColor(k < arDir ? kObjB[!k ? oDes : (k == 1 ? oNad :
        (k == 2 ? oAsc : oMC))] : (k == arVer ? kCyanB : kDkCyanB));
      for (j = 0; j <= nDegHalf; j++) {
        if (k == 1 || k == 3) {
          // MC and IC lines
          x1 = planet1[i] + (k == 3 ? 0.0 : rDegHalf);
          y1 = (real)(j - 90);
        } else if (k < arDir) {
          // Asc and Des lines
          l = j + (k == 0)*nDegHalf + 90;
          if (l >= nDegMax)
            l -= nDegMax;
          x1 = (real)l; y1 = 0.0;
          CoorXform(&x1, &y1, rDegQuad - planet2[i]);
          x1 += planet1[i] + rDegQuad;
        } else {
          // Vertex and Antivertex lines
          l = j + (k == arVer)*nDegHalf + 90;
          if (l >= nDegMax)
            l -= nDegMax;
          x1 = (real)l; y1 = 0.0;
          CoorXform(&x1, &y1, rDegQuad - planet2[i]);
          x1 += planet1[i] + rDegQuad;
          if (y1 < 0.0)
            y1 += rDegQuad;
          else
            y1 -= rDegQuad;
        }
        WireMapCalc(x1, y1, &x, &y, &z, fSky, rT, rz, deg);
        if (j > 0 && (k < arDir || NAbs(zold-z) < rz))
          WireLine(xold, yold, zold, x, y, z);
        xold = x; yold = y; zold = z;
      }
    }
    // Draw astrocartography lines for minor house cusps, if unrestricted.
    for (k = 1; k <= cSign; k++) {
      if (FIgnore(cuspLo-1 + k) ||
        (k == sAri && chouse[k] == is.Asc) ||
        (k == sCap && chouse[k] == is.MC)  ||
        (k == sLib && chouse[k] == Mod(is.Asc + rDegHalf)) ||
        (k == sCan && chouse[k] == Mod(is.MC  + rDegHalf)))
        continue;
      DrawColor(kSignB(k));
      for (j = 0; j <= nDegHalf; j++) {
        x1 = 0.0; y1 = (real)j;
        CoorXform(&x1, &y1, rDegQuad - chouse3[k]);
        x1 = Mod(x1 + rDegQuad);
        CoorXform(&x1, &y1, rDegQuad + planet2[i]);
        x1 = Mod(x1 - rDegQuad + planet1[i]);
        WireMapCalc(x1, y1, &x, &y, &z, fSky, rT, rz, deg);
        if (j > 0)
          WireLine(xold, yold, zold, x, y, z);
        if (j == 90) {
          // Draw glyph to label minor house cusp line.
          gi.zDefault = z - unit/2;
          DrawObject(cuspLo-1 + k, x, y);
          WireOctahedron(x, y, z, gi.nScale);
        }
        xold = x; yold = y; zold = z;
      }
    }
  } // i

  // Compute coordinates of each object, and draw their glyphs.

  for (j = 0; j < arDir; j++) {
    k = (!j ? arDes : (k == 1 ? arIC : (k == 2 ? arAsc : arMC)));
    if (k != arMC && (fSky || ignorez[k]))
      continue;
    for (i = 0; i <= is.nObj; i++) if (FProper(i)) {
      x1 = planet1[i]; y1 = planet2[i];
      if (k == arIC) {
        x1 = Mod(x1 + rDegHalf);
        neg(y1);
      } else if (k == arAsc) {
        x1 = Mod(x1 - rDegQuad);
        y1 = 0.0;
      } else if (k == arDes) {
        x1 = Mod(x1 + rDegQuad);
        y1 = 0.0;
      }
      WireMapCalc(x1, y1, &u, &v, &w, fSky, rT, rz, deg);
      gi.zDefault = w - unit/2;
      if (k > arMC) {
        l = kObjB[i]; kObjB[i] = gi.kiLite;
      }
      DrawObject(i, u, v);
      if (k > arMC)
        kObjB[i] = l;
      WireOctahedron(u, v, w, gi.nScale);
    }
  }
}


// Generate a chart depicting an aerial view of the solar system in space,
// with all the planets drawn around the Sun, and the specified central body
// in the middle. Like XChartOrbit() except for 3D wireframe format.

void WireChartOrbit()
{
  int x[objMax], y[objMax], z[objMax], zWin, xd, yd, i, j, k;
  real sx, sz, xp, yp, zp, xp2, yp2, zp2, rT;
#ifdef SWISS
  ES es, *pes1, *pes2;
  int xT, yT, zT, x2, y2, z2;
#endif
  PT3R vCross, ptCen;
  real tilt, rot;

  // Compute coordinates of planets.
  i = gi.nScale/gi.nScaleT;
  sz = gs.rspace > 0.0 ? gs.rspace : (i <= 1 ? 90.0 : (i == 2 ? 30.0 :
    (i == 3 ? 6.0 : (gi.nScaleText/2 <= 1 ? 1.0 : 0.006))));
  zWin = Min(gs.xWin, gs.yWin);
  sx = (real)zWin/sz;
  for (i = 0; i <= is.nObj; i++) if (FProper(i)) {
    xp = space[i].x; yp = space[i].y; zp = space[i].z;
    if (us.fStar || gs.fAllStar) {
      xp /= rLYToAU; yp /= rLYToAU; zp /= rLYToAU;
    }
    if (us.fHouse3D)
      OrbitPlot(&xp, &yp, &zp, sz, i, space);
    x[i] = -(int)(xp*sx); y[i] = (int)(yp*sx); z[i] = (int)(zp*sx);
  }

  // Draw zodiac lines.
  if (!gs.fHouseExtra) {
    k = zWin;
    if (!gs.fColorSign)
      DrawColor(kLtGrayB);
    for (i = 0; i < cSign; i++) {
      j = i+1;
      if (gs.fColorSign)
        DrawColor(kSignB(j));
      xd = NCosD(k, nDegHalf-i*30); yd = NSinD(k, nDegHalf-i*30);
      WireLine(0, 0, 0, xd, yd, 0);
      xd = NCosD(k, nDegHalf-i*30-15); yd = NSinD(k, nDegHalf-i*30-15);
      DrawSign(j, xd, yd);
    }
  }

  // Draw lines connecting planets which have aspects between them.
  if (gs.fEquator && us.nAsp > 0) {
    if (!FCreateGrid(fFalse))
      return;
    for (j = oNorm; j >= 1; j--)
      for (i = j-1; i >= 0; i--)
        if (grid->n[i][j] && FProper(i) && FProper(j)) {
          DrawColor(kAspB[ASPT(grid->n[i][j])]);
          WireLine(x[i], y[i], z[i], x[j], y[j], z[j]);
          if (gs.fLabelAsp) {
            gi.zDefault = (z[i] + z[j]) >> 1;
            DrawAspect2(grid->n[i][j],
              (x[i] + x[j]) >> 1, (y[i] + y[j]) >> 1, i, j);
          }
        }
  }

  // Prepare to draw orbital trails.
  if (gs.cspace > 0 && gi.rgspace == NULL) {
    gi.rgspace = RgAllocate(oNorm1*gs.cspace, PT3R, "orbits");
    if (gi.rgspace == NULL)
      return;
    gi.cspace = gi.ispace = 0;
  }

  // Draw planets.
  for (i = 0; i <= is.nObj; i++) if (FProper(i)) {
    DrawColor(kObjB[i]);
    if (!gs.fAlt || i > oVes)
      j = 0;
    else
      j = (int)(RLog10(RObjDiam(i)) * (real)gi.nScaleT);
    j = Max(j, 3 * gi.nScaleT);
    rT = RObjDiam(i) / 2.0 / rAUToKm;
    k = (int)(rT * sx);
    j = Max(j, k);
    if (j < 12 * gi.nScaleT) {
      WireOctahedron(x[i], y[i], z[i], j);
      gi.zDefault = z[i] + (j + 5*gi.nScaleT);
    } else {
      WireSphere(x[i], y[i], z[i], k);
      gi.zDefault = z[i];
      DrawColor(kDkGreenB);

      // Draw rings around Saturn or other planet.
      j = FBetween(i, oJuC, oNeC) ? i - oJuC + oJup :
        (FBetween(i, oJup, oNep) && ignore[i + oJuC - oJup] ? i :
        (i == oHau || i == oQua ? i : -1));
      if (j >= 0) {
        j = IObjRing(j);
        vCross = rgvObjRing[j];
        // Adjust ring vector appropriately if in sidereal zodiac.
        if (is.rSid != 0.0) {
          rT = RLength2(vCross.x, vCross.y);
          rot = RAngleD(vCross.x, vCross.y) + is.rSid;
          vCross.x = RCosD(rot) * rT;
          vCross.y = RSinD(rot) * rT;
        }
        // Calculate tilt of Saturn's rings up or down.
        PtSet(ptCen, 0.0, 0.0, 1.0);
        tilt = VAngleD(&vCross, &ptCen);
        vCross.z = 0.0;
        PtSet(ptCen, 1.0, 0.0, 0.0);
        rot = VAngleD(&vCross, &ptCen);
        if (rgvObjRing[j].y < 0.0)
          rot = rDegMax - rot;
        rT = rgrObjRing[j][0] / rAUToKm * sx;
        WireCircle(x[i], y[i], z[i], rT, tilt, rot);
        if (rgrObjRing[j][1] > 0.0) {
          rT = rgrObjRing[j][1] / rAUToKm * sx;
          WireCircle(x[i], y[i], z[i], rT, tilt, rot);
        }
      }
      DrawColor(kObjB[i]);
    }
    DrawObject(i, x[i], y[i]);

    // Draw orbital trails.
    for (j = 0; j < gi.cspace-1; j++) {
      k = (gi.ispace - gi.cspace + j + gs.cspace) % gs.cspace;
      k = k*oNorm1 + i;
      xp = gi.rgspace[k].x; yp = gi.rgspace[k].y; zp = gi.rgspace[k].z;
      if (us.fHouse3D)
        OrbitPlot(&xp, &yp, &zp, sz, i, &gi.rgspace[k - i]);

      k = (gi.ispace - gi.cspace + j + 1 + gs.cspace) % gs.cspace;
      k = k*oNorm1 + i;
      xp2 = gi.rgspace[k].x; yp2 = gi.rgspace[k].y; zp2 = gi.rgspace[k].z;
      if (us.fHouse3D)
        OrbitPlot(&xp2, &yp2, &zp2, sz, i, &gi.rgspace[k - i]);

      k = gi.cspace - j;
      WireLine(-(int)(xp*sx), (int)(yp*sx), (int)(zp*sx) - k*gs.zspace,
        -(int)(xp2*sx), (int)(yp2*sx), (int)(zp2*sx) - (k-1)*gs.zspace);
    }
    if (gs.cspace < 0 && i <= oNep) {
      j = !FGeo(i) ? oSun : oEar;
      if (i == j || FIgnore(j))
        continue;
      k = (int)RLength2((real)(x[i] - x[j]), (real)(y[i] - y[j]));
      DrawColor(kObjB[i]);
      gi.zDefault = z[i];
      DrawCircle(x[j], y[j], k, k);
    }
  }
  OrbitRecord();

  // Draw planetary moons in orbit around their planet glyphs.
  if (gs.fMoonWheel) {
    char sz[cchSzDef], chT;
    EnumMoonsRing(NULL, NULL, NULL, NULL, NULL, fFalse);
    while (EnumMoonsRing(&i, &j, &rT, &k, &chT, fFalse)) {
      rT = rT - planet[j] + rDegHalf;
      xd = x[j] + NCosD(7*gi.nScale, rT);
      yd = y[j] + NSinD(7*gi.nScale, rT);
      sprintf2(S(sz), "%c", chT);
      DrawColor(k);
      if (!gs.fAlt || j > oVes)
        i = 0;
      else
        i = (int)(RLog10(RObjDiam(j)) * (real)gi.nScaleT);
      i = Max(i, 3 * gi.nScaleT);
      rT = RObjDiam(j) / 2.0 / rAUToKm;
      k = (int)(rT * sx);
      i = Max(i, k);
      if (i < 12 * gi.nScaleT)
        gi.zDefault = z[j] + (i + 5*gi.nScaleT);
      else
        gi.zDefault = z[j];
      DrawSz(sz, xd, yd, dtScale2);
    }
  }

#ifdef SWISS
  // Draw extra stars.
  if (gs.fAllStar) {
    DrawColor(gi.kiGray);
    SwissComputeStar(0.0, NULL);
    while (SwissComputeStar(is.T, &es)) {
      xp = es.pt.x / rLYToAU; yp = es.pt.y / rLYToAU; zp = es.pt.z / rLYToAU;
      if (us.fHouse3D)
        OrbitPlot(&xp, &yp, &zp, sz, -1, NULL);
      xT = -(int)(xp*sx); yT = (int)(yp*sx); zT = (int)(zp*sx);
      WireStar(xT, yT, zT, &es);
    }

    // Draw constellation lines between stars.
    DrawColor(gi.kiLite);
    EnumStarsLines(fTrue, NULL, NULL);
    while (EnumStarsLines(fFalse, &pes1, &pes2)) {
      xp = pes1->pt.x / rLYToAU; yp = pes1->pt.y / rLYToAU;
      zp = pes1->pt.z / rLYToAU;
      if (us.fHouse3D)
        OrbitPlot(&xp, &yp, &zp, sz, -1, NULL);
      xT = -(int)(xp*sx); yT = (int)(yp*sx); zT = (int)(zp*sx);
      xp = pes2->pt.x / rLYToAU; yp = pes2->pt.y / rLYToAU;
      zp = pes2->pt.z / rLYToAU;
      if (us.fHouse3D)
        OrbitPlot(&xp, &yp, &zp, sz, -1, NULL);
      x2 = -(int)(xp*sx); y2 = (int)(yp*sx); z2 = (int)(zp*sx);
      WireLine(xT, yT, zT, x2, y2, z2);
    }
  }

  // Draw extra asteroids.
  if (!gs.fAllStar && gs.nAstLo > 0) {
    DrawColor(gi.kiGray);
    SwissComputeAsteroid(0.0, NULL, fFalse);
    while (SwissComputeAsteroid(is.T, &es, fFalse)) {
      xp = es.pt.x; yp = es.pt.y; zp = es.pt.z;
      if (us.fHouse3D)
        OrbitPlot(&xp, &yp, &zp, sz, -1, NULL);
      xT = -(int)(xp*sx); yT = (int)(yp*sx); zT = (int)(zp*sx);
      WireStar(xT, yT, zT, &es);
    }
  }
#endif
}


// Translate to chart 3D coordinates, that indicate how to compose a 3D chart
// sphere, for the -XX wireframe chart. Inputs may be local horizon altitude
// and azimuth coordinates, local horizon prime vertical, local horizon
// meridian, zodiac position and latitude, or Earth coordinates.

void WireSphereLocal(real azi, real alt, int zr, int *xp, int *yp, int *zp)
{
  if (gs.fEcliptic) {
    azi = Mod(azi - rDegQuad); neg(alt);
    CoorXform(&azi, &alt, Lat - rDegQuad);
    azi = Mod(cp0.lonMC - azi + rDegQuad);
    EquToEcl(&azi, &alt);
    azi = rDegMax - Untropical(azi); neg(alt);
  }
  azi = Mod(rDegQuad*3 - (azi + gs.rRot));
  if (gs.rTilt != 0.0)
    CoorXform(&azi, &alt, gs.rTilt);
  *xp = (int)((real)zr * RCosD(alt) * RSinD(azi) - rRound);
  *yp = (int)((real)zr * RCosD(alt) * RCosD(azi) - rRound);
  *zp = -(int)((real)zr * RSinD(alt) - rRound);
}

void WireSpherePrime(real azi, real alt, int zr, int *xp, int *yp, int *zp)
{
  CoorXform(&azi, &alt, rDegQuad);
  WireSphereLocal(azi + rDegQuad, alt, zr, xp, yp, zp);
}

void WireSphereMeridian(real azi, real alt, int zr, int *xp, int *yp, int *zp)
{
  azi = Mod(azi + rDegQuad);
  CoorXform(&azi, &alt, rDegQuad);
  WireSphereLocal(azi, alt, zr, xp, yp, zp);
}

void WireSphereZodiac(real lon, real lat, int zr, int *xp, int *yp, int *zp)
{
  real lonT, latT;

  lonT = Tropical(lon); latT = lat;
  EclToEqu(&lonT, &latT);
  lonT = Mod(cp0.lonMC - lonT + rDegQuad);
  EquToLocal(&lonT, &latT, rDegQuad - Lat);
  WireSphereLocal(lonT + rDegQuad, -latT, zr, xp, yp, zp);
}

void WireSphereEarth(real azi, real alt, int zr, int *xp, int *yp, int *zp)
{
  azi = Mod(-azi);
  CoorXform(&azi, &alt, rDegQuad - Lat);
  WireSphereLocal(azi + rDegQuad, -alt, zr, xp, yp, zp);
}


// Draw a chart sphere (like a chart wheel but in 3D) as done with the -XX
// switch. Like XChartSphere() except for 3D wireframe format.

void WireChartSphere()
{
  char sz[cchSzDef], chT;
  int rgx[objMax], rgy[objMax], rgz[objMax], zGlyph,
    cChart, iChart, zr, xo = 0, yo = 0, zo = 0, xp, yp, zp, i, j, k, k2, nSav;
  flag fHouse3D = !us.fHouse3D, fNoHorizon;
  real rT;
  CONST CP *pcp;
  CP cpSav;
  byte ignoreSav[objMax];
  ES es;
#ifdef SWISS
  ES *pes1, *pes2;
#endif

  // Initialize variables.
  if (gs.fText && gs.fDoSidebar)
    gs.xWin -= xSideT;

  fNoHorizon = ignorez[0] && ignorez[1] && ignorez[2] && ignorez[3];
  zGlyph = 7*gi.nScale;
  zr = Min(gs.xWin >> 1, gs.yWin >> 1) - zGlyph;
  if (!(us.nRel <= rcTransit))
    cChart = 1 - (FBetween(us.nRel, rcHexaWheel, rcDual) ? us.nRel : 0);
  else {
    cChart = 2;
    CopyRgb(ignore.rgn, ignoreSav, sizeof(ignore.rgn));
  }

  // Draw constellations.
  if (gs.fConstel) {
    neg(gs.rTilt);
    WireDrawGlobe(fTrue, gs.rRot);
    neg(gs.rTilt);
  }

  // Draw horizon.
  if (!fNoHorizon || (!gs.fHouseExtra && fHouse3D)) {
    if (!gs.fColorHouse)
      DrawColor(gi.kiOn);
    for (i = 0; i <= nDegMax; i++) {
      if (gs.fColorHouse && (i == 0 || i == nDegHalf))
        DrawColor(kSignB(i ? sLib : sAri));
      WireSphereLocal((real)i, 0.0, zr, &xp, &yp, &zp);
      if (i > 0) {
        WireLine(xo, yo, zo, xp, yp, zp);
        k = i % 10 == 0 ? 3 : (i % 5 == 0 ? 2 : 1);
        for (j = -k; j <= k; j += (k << 1)) {
          WireSphereLocal((real)i, (real)j / 2.0, zr, &xo, &yo, &zo);
          WireLine(xo, yo, zo, xp, yp, zp);
        }
      }
      xo = xp; yo = yp; zo = zp;
    }
  }

  // Draw Earth's equator.
  if (gs.fEquator) {
    DrawColor(kPurpleB);
    for (i = 0; i <= nDegMax; i++) {
      WireSphereEarth((real)i, 0.0, zr, &xp, &yp, &zp);
      if (i > 0)
        WireLine(xo, yo, zo, xp, yp, zp);
      xo = xp; yo = yp; zo = zp;
    }
  }

  // Draw prime vertical.
  if (!fNoHorizon) {
    if (!gs.fColorHouse)
      DrawColor(gi.kiGray);
    for (i = 0; i <= nDegMax; i++) {
      if (gs.fColorHouse)
        DrawColor(kSignB((i-1)/30 + 1));
      WireSpherePrime((real)i, 0.0, zr, &xp, &yp, &zp);
      if (i > 0) {
        WireLine(xo, yo, zo, xp, yp, zp);
        k = i % 10 == 0 ? 3 : (i % 5 == 0 ? 2 : 1);
        for (j = -k; j <= k; j += (k << 1)) {
          WireSpherePrime((real)i, (real)j / 2.0, zr, &xo, &yo, &zo);
          WireLine(xo, yo, zo, xp, yp, zp);
        }
      }
      xo = xp; yo = yp; zo = zp;
    }
  }

  // Draw 3D house wedges and meridian.
  if (!gs.fColorHouse)
    DrawColor(kDkGreenB);
  for (j = -1; j <= cSign; j++) {
    if (!(!gs.fHouseExtra && fHouse3D) && !(j <= 0 && !fNoHorizon))
      continue;
    if (fHouse3D &&
       ((j == sCap && chouse[j] == is.MC)  ||
        (j == sCan && chouse[j] == Mod(is.MC  + rDegHalf))))
      continue;
    if (fHouse3D && us.nHouse3D == hmPrime &&
       ((j == sAri && chouse[j] == is.Asc) ||
        (j == sLib && chouse[j] == Mod(is.Asc + rDegHalf))))
      continue;
    if (fHouse3D && us.nHouse3D == hmHorizon &&
       ((j == sAri && chouse[j] == Mod(is.Vtx + rDegHalf)) ||
        (j == sLib && chouse[j] == is.Vtx)))
      continue;
    if (gs.fColorHouse) {
      k = j > 0 ? j : (j < 0 ? sCan : sCap);
      DrawColor(kSignB(k));
    }
    rT = j > 0 ? chouse3[j] : (j < 0 ? rDegQuad : 270.0);
    for (i = -90; i <= 90; i++) {
      if (j <= 0 || us.nHouse3D == hmPrime)
        WireSpherePrime(rT, (real)i, zr, &xp, &yp, &zp);
      else if (us.nHouse3D == hmHorizon)
        WireSphereLocal(rDegQuad - rT, (real)i, zr, &xp, &yp, &zp);
      else
        WireSphereEarth(rT, (real)i, zr, &xp, &yp, &zp);
      if (i > -90) {
        WireLine(xo, yo, zo, xp, yp, zp);
        if (j <= 0) {
          k = i % 10 == 0 ? 3 : (i % 5 == 0 ? 2 : 1);
          for (k2 = -k; k2 <= k; k2 += (k << 1)) {
            WireSphereMeridian((real)(j == 0 ? i+180 : 360-i), (real)k2 / 2.0,
              zr, &xo, &yo, &zo);
            WireLine(xo, yo, zo, xp, yp, zp);
          }
        }
      }
      xo = xp; yo = yp; zo = zp;
    }
  }

  // Draw 2D house wedges.
  if (!gs.fHouseExtra && !fHouse3D)
    for (i = 1; i <= cSign; i++) {
      if (gs.fColorHouse)
        DrawColor(kSignB(i));
      for (j = -90; j <= 90; j++) {
        WireSphereZodiac(chouse[i], (real)j, zr, &xp, &yp, &zp);
        if (j > -90)
          WireLine(xo, yo, zo, xp, yp, zp);
        xo = xp; yo = yp; zo = zp;
      }
    }

  // Draw sign wedges.
  if (!us.fIndian) {
    if (!gs.fColorSign)
      DrawColor(kDkBlueB);
    for (i = 0; i <= nDegMax; i++) {
      if (gs.fColorSign)
        DrawColor(kSignB((i-1)/30 + 1));
      WireSphereZodiac((real)i, 0.0, zr, &xp, &yp, &zp);
      if (i > 0) {
        WireLine(xo, yo, zo, xp, yp, zp);
        if (i % 30 != 0) {
          k = i % 10 == 0 ? 3 : (i % 5 == 0 ? 2 : 1);
          for (j = -k; j <= k; j += (k << 1)) {
            WireSphereZodiac((real)i, (real)j / 2.0, zr, &xo, &yo, &zo);
            WireLine(xo, yo, zo, xp, yp, zp);
          }
        }
      }
      xo = xp; yo = yp; zo = zp;
    }
    for (i = 0; i < nDegMax; i += 30) {
      if (gs.fColorSign)
        DrawColor(kSignB(i/30 + 1));
      for (j = -90; j <= 90; j++) {
        WireSphereZodiac((real)i, (real)j, zr, &xp, &yp, &zp);
        if (j > -90)
          WireLine(xo, yo, zo, xp, yp, zp);
        xo = xp; yo = yp; zo = zp;
      }
    }
  }

  // Label signs.
  if (!us.fIndian) {
    nSav = gi.nScale;
    gi.nScale = gi.nScaleTextT;
    for (j = -80; j <= 80; j += 160)
      for (i = 1; i <= cSign; i++) {
        WireSphereZodiac((real)(i*30-15), (real)j, zr, &xp, &yp, &zp);
        DrawColor(gs.fColorSign ? kSignB(i) : kDkBlueB);
        gi.zDefault = zp;
        DrawSign(i, xp, yp);
      }
    gi.nScale = nSav;
  }

  // Label houses.
  if (!gs.fHouseExtra) {
    nSav = gi.nScale;
    gi.nScale = gi.nScaleTextT;
    for (j = -82; j <= 82; j += 164)
      for (i = 1; i <= cSign; i++) {
        WireSpherePrime((real)(i*30-15), (real)j, zr, &xp, &yp, &zp);
        DrawColor(gs.fColorHouse ? kSignB(i) : kDkGreenB);
        gi.zDefault = zp;
        DrawHouse(i, xp, yp);
      }
    gi.nScale = nSav;
  }

  // Label directions.
  if (!fNoHorizon) {
    k = zGlyph >> 1;
    for (i = 0; i < nDegMax; i += 90) {
      j = i / 90;
      WireSphereLocal((real)i, 0.0, zr, &xp, &yp, &zp);
      DrawColor(kObjB[oAsc + ((j + 3) & 3)*3]);
      if (!ignorez[(1 - j) & 3])
        WireLine(0, 0, 0, xp, yp, zp);
      if (gs.fColorHouse)
        DrawColor(gi.kiOn);
      WireSphereLocal((real)i, 0.0, zr + k, &xp, &yp, &zp);
      sprintf2(S(sz), "%c", rgszDir[j][0]);
      gi.zDefault = zp;
      DrawSz(sz, xp, yp, dtCent);
    }
    for (j = -90; j <= 90; j += nDegHalf) {
      WireSphereLocal(0.0, (real)j, zr + k, &xp, &yp, &zp);
      DrawColor(gs.fColorHouse ? gi.kiOn : (kObjB[j <= 0 ? oMC : oNad]));
      sprintf2(S(sz), "%c", j <= 0 ? 'Z' : 'N');
      gi.zDefault = zp;
      DrawSz(sz, xp, yp, dtCent);
    }
  }

#ifdef SWISS
  // Draw extra stars.
  if (gs.fAllStar) {
    DrawColor(gi.kiGray);
    SwissComputeStar(0.0, NULL);
    while (SwissComputeStar(is.T, &es)) {
      WireSphereZodiac(es.lon, es.lat, zr, &xp, &yp, &zp);
      WireStar(xp, yp, zp, &es);
    }

    // Draw constellation lines between stars.
    DrawColor(gi.kiLite);
    EnumStarsLines(fTrue, NULL, NULL);
    while (EnumStarsLines(fFalse, &pes1, &pes2)) {
      WireSphereZodiac(pes1->lon, pes1->lat, zr, &xo, &yo, &zo);
      WireSphereZodiac(pes2->lon, pes2->lat, zr, &xp, &yp, &zp);
      WireLine(xo, yo, zo, xp, yp, zp);
    }
  }

  // Draw extra asteroids.
  if (gs.nAstLo > 0) {
    DrawColor(gi.kiGray);
    SwissComputeAsteroid(0.0, NULL, fFalse);
    while (SwissComputeAsteroid(is.T, &es, fFalse)) {
      WireSphereZodiac(es.lon, es.lat, zr, &xp, &yp, &zp);
      WireStar(xp, yp, zp, &es);
    }
  }
#endif

  // Draw exoplanets.
  if (gs.fAllExo) {
    EnumExoplanets(NULL);
    while (EnumExoplanets(&es)) {
      EquToEcl(&es.lon, &es.lat);
      WireSphereZodiac(es.lon, es.lat, zr, &xp, &yp, &zp);
      WireStar(xp, yp, zp, &es);
    }
  }

  // Determine set of planet data to use.
  for (iChart = 1; iChart <= cChart; iChart++) {
    FProcessCommandLine(szWheelX[iChart]);
    if (iChart == 2 && us.nRel <= rcTransit)
      CopyRgb(ignore2.rgn, ignore.rgn, sizeof(ignore.rgn));
    if (iChart <= 1)
      pcp = rgpcp[us.nRel <= rcDual];
    else
      pcp = rgpcp[iChart];
    cpSav = cp0;
    cp0 = *pcp;

    // Draw planet glyphs, and spots for actual local location.
    for (i = 0; i <= is.nObj; i++) if (FProper(i)) {
      WireSphereZodiac(planet[i], planetalt[i], zr, &xp, &yp, &zp);
      rgx[i] = xp; rgy[i] = yp; rgz[i] = zp;
      gi.zDefault = rgz[i] - zGlyph;
      DrawObject(i, rgx[i], rgy[i]);
      DrawColor(kObjB[i]);
      WireOctahedron(rgx[i], rgy[i], rgz[i], gi.nScale);
    }

    // Draw planetary moons in orbit around their planet glyphs.
    if (gs.fMoonWheel) {
      EnumMoonsRing(NULL, NULL, NULL, NULL, NULL, fTrue);
      while (EnumMoonsRing(&i, &j, &rT, &k, &chT, fTrue)) {
        xp = rgx[j] + NCosD(zGlyph, rT);
        yp = rgy[j] + NSinD(zGlyph, rT);
        sprintf2(S(sz), "%c", chT);
        DrawColor(k);
        gi.zDefault = rgz[j] - zGlyph;
        DrawSz(sz, xp, yp, dtScale2);
      }
    }

    // Draw lines connecting planets which have aspects between them.
    if (!FCreateGrid(fFalse))
      return;
    for (j = is.nObj; j >= 1; j--)
      for (i = j-1; i >= 0; i--)
        if (grid->n[i][j] && FProper(i) && FProper(j)) {
          DrawColor(kAspB[ASPT(grid->n[i][j])]);
          WireLine(rgx[i], rgy[i], rgz[i], rgx[j], rgy[j], rgz[j]);
          if (gs.fLabelAsp) {
            gi.zDefault = (rgz[i] + rgz[j]) >> 1;
            DrawAspect2(grid->n[i][j],
              (rgx[i] + rgx[j]) >> 1, (rgy[i] + rgy[j]) >> 1, i, j);
          }
        }

    cp0 = cpSav;
    if (iChart == 2 && us.nRel <= rcTransit)
      CopyRgb(ignoreSav, ignore.rgn, sizeof(ignore.rgn));
  } // iChart
  FProcessCommandLine(szWheelX[0]);

  // Draw center point.
  DrawColor(gi.kiOn);
  WireOctahedron(0, 0, 0, gi.nScale * 2);
}
#endif // WIRE
#endif // GRAPH

/* xdevice.cpp */
