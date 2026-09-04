@echo off
rem Build the Qt port on Windows with MSVC, no make and no moc: both
rem binaries, from ONE compile of the shared core.
rem
rem   tools\msvc-build-qt.cmd            astrolog.exe and astrolog-qt-test.exe
rem
rem QTDIR must point at a Qt for MSVC (the directory holding bin\, lib\
rem and include\). The outputs land in the current directory, which must
rem be the repository root.
rem
rem WHY ONE COMPILE. The two binaries used to be two full cl.exe runs
rem over the same 34 sources -- 61 s and 55 s with /MP, the push lane's
rem long pole. But -DQTTEST, the only thing that distinguishes them,
rem changes three files: qtdriver.cpp and qtdialog.cpp carry the test
rem hooks, and qttest.cpp is the suite. The other 31 -- the core, the
rem graphics code, the vendored Swiss Ephemeris -- compile to the same
rem object code either way ON THIS PLATFORM, and the reason is worth
rem stating rather than assuming: astrolog.h's one core-side use of
rem QTTEST is AssertIndex(), the range guard on the checked tables, and
rem it is an assert(). This build passes /DNDEBUG, so on Windows that
rem guard has been compiled out of the test build since the day the
rem build was written. The Linux test build (Makefile.qt.test, no
rem NDEBUG), where the sanitizers also run, keeps the guard live; here
rem nothing is lost by sharing the core objects, because there was
rem nothing there to lose.
rem
rem So: the 31 shared sources once, into obj\; qtdriver.cpp and
rem qtdialog.cpp twice, into obj-app\ and obj-test\ (the latter with
rem /DQTTEST beside qttest.cpp); one resource; two links. The lists come
rem from tools\qt-srcs.py --only core|qt|test, which reads Makefile.srcs
rem like every other build here.
rem
rem THE SUBSYSTEM IS THE OTHER REAL DIFFERENCE. The program is linked
rem /SUBSYSTEM:WINDOWS with /ENTRY:mainCRTStartup: astrolog.cpp's
rem ordinary main() still runs, because that is inside "#ifndef WIN" and
rem this build does not define WIN, but no console window opens beside
rem the GUI when a user double-clicks it. The test build stays a console
rem program, because it prints PASS/FAIL to stdout and the runner has to
rem see it; a WINDOWS-subsystem process started from cmd is detached
rem from the console and its output goes nowhere.
rem
rem The flags, explained where they were first needed:
rem
rem   /MP               one cl.exe worker per core; measured 109 s -> 61 s
rem                     for the program on a 4-core runner.
rem   /Zc:__cplusplus   MSVC reports __cplusplus as 199711L unless told
rem                     to be accurate, and Qt's headers check it and
rem                     refuse: "Qt requires a C++17 compiler".
rem   /permissive-      the second flag Qt asks for by name, for the same
rem                     reason: MSVC's defaults are not conforming.
rem   /Zc:strictStrings- AFTER /permissive-, which implies the positive
rem                     form. The tree initialises char * from string
rem                     literals in 1,345 places, and every makefile
rem                     passes -Wno-write-strings for the same reason.
rem   /W3               finds signed/unsigned mismatches, truncation and
rem                     uninitialised locals; /W4 is noisier than useful
rem                     on a codebase this old, /W1 found nothing.
rem   _CRT_SECURE_NO_WARNINGS  silences C4996, which deprecates fopen,
rem                     getenv, strcpy and sscanf for _s variants that
rem                     exist only on this compiler -- 850 of them at /W3.
rem   /DQT /DPC         the Qt backend, and what astrolog.h wants on
rem                     Windows for <io.h> and backslash separators. No
rem                     /DWIN: this is Windows without the Windows backend.
rem
rem NOTE: rem cannot appear between "cl" and its last "^" continuation --
rem cmd passes the word to the compiler as a source file and the link
rem dies with "LNK1181: cannot open input file 'rem.obj'". It did.

setlocal
if not defined QTDIR (
  echo QTDIR is not set -- point it at a Qt for MSVC
  exit /b 2
)
if not exist Makefile.srcs (
  echo run this from the repository root
  exit /b 2
)

python tools\qt-srcs.py --only core > srcs-core.txt || exit /b 1
python tools\qt-srcs.py --only qt   > srcs-qt.txt   || exit /b 1
python tools\qt-srcs.py --only test > srcs-test.txt || exit /b 1

rem vswhere, not a hardcoded edition path: the runner image moves and the
rem edition is not something to guess at. vswhere ships with every
rem installer since 2017 and answers where the toolset actually is.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * ^
  -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 ^
  -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH (
  echo No MSVC toolset found
  exit /b 1
)
echo Using %VSPATH%
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

rem The icon and version resource, so the .exe has a face in Explorer
rem and a Properties tab that says what it is. Version from astrolog.h
rem by way of tools\version.py, never typed here. One resource, both
rem binaries.
for /f "usebackq tokens=*" %%v in (`python tools\version.py --rc`) do set "VERRC=%%v"
for /f "usebackq tokens=*" %%v in (`python tools\version.py`) do set "VERSTR=%%v"
if not defined VERRC exit /b 1
rc /nologo /I. /DVERSIONRC=%VERRC% /DVERSIONSTR=\"%VERSTR%\" /fo astrolog-qt.res tools\astrolog-qt.rc || exit /b 1

set CXX=/nologo /MP /std:c++17 /Zc:__cplusplus /permissive- /Zc:strictStrings- /EHsc /MD /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /DQT /DPC /DWIN32 /D_WINDOWS /DNDEBUG
set INC=/I"%QTDIR%\include" /I"%QTDIR%\include\QtCore" /I"%QTDIR%\include\QtGui" /I"%QTDIR%\include\QtWidgets" /I"%QTDIR%\include\QtNetwork" /I"%QTDIR%\include\QtPrintSupport"
set LIBS=/LIBPATH:"%QTDIR%\lib" Qt6Widgets.lib Qt6Gui.lib Qt6Core.lib Qt6Network.lib Qt6PrintSupport.lib user32.lib gdi32.lib shell32.lib advapi32.lib ole32.lib

if not exist obj mkdir obj
if not exist obj-app mkdir obj-app
if not exist obj-test mkdir obj-test

echo == the shared core, once
cl %CXX% %INC% /c /Fo"obj\\" @srcs-core.txt || exit /b 1

echo == the Qt sources, for the program
cl %CXX% %INC% /c /Fo"obj-app\\" @srcs-qt.txt || exit /b 1

echo == the Qt sources and the suite, for the test build
cl %CXX% /DQTTEST %INC% /c /Fo"obj-test\\" @srcs-qt.txt @srcs-test.txt || exit /b 1

echo == link astrolog.exe
link /nologo /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup /OUT:astrolog.exe obj\*.obj obj-app\*.obj astrolog-qt.res %LIBS% || exit /b 1

echo == link astrolog-qt-test.exe
link /nologo /SUBSYSTEM:CONSOLE /OUT:astrolog-qt-test.exe obj\*.obj obj-test\*.obj astrolog-qt.res %LIBS% || exit /b 1

for %%f in (astrolog.exe astrolog-qt-test.exe) do (
  if not exist %%f (
    echo No %%f produced
    exit /b 1
  )
  echo built %%f, %%~zf bytes
)
endlocal
exit /b 0
