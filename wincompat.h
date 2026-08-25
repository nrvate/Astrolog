/*
** Compatibility shims for cross compiling the Windows build of Astrolog
** with mingw-w64 instead of Visual Studio (see Makefile.win). Force
** included ahead of every translation unit, so no shared source needs
** editing. Not used by the Visual Studio build.
*/

#ifndef __WINCOMPAT_H
#define __WINCOMPAT_H

/* Visual Studio's windows.h defines these; mingw's does not in C++ mode,   */
/* and wdriver.cpp uses them.                                              */
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

#endif /* __WINCOMPAT_H */
