@echo off
rem Build the Qt port on Windows with MSVC, no make and no moc.
rem
rem   tools\msvc-build-qt.cmd astrolog.exe            the program
rem   tools\msvc-build-qt.cmd astrolog-qt-test.exe --test   the -DQTTEST suite
rem
rem QTDIR must point at a Qt for MSVC (the directory holding bin\, lib\
rem and include\). The output goes in the current directory, which must
rem be the repository root.
rem
rem This was two near-identical cl.exe invocations inline in a workflow
rem file, one per binary, which is the shape that drifts: a flag added
rem to one and not the other is invisible until the suite passes on a
rem build the program was not made with. One script, one flag list, and
rem the difference between the two binaries is exactly what the argument
rem says it is -- the -DQTTEST define, and the subsystem.
rem
rem THE SUBSYSTEM IS THE ONE REAL DIFFERENCE. The program is linked
rem /SUBSYSTEM:WINDOWS with /ENTRY:mainCRTStartup: astrolog.cpp's
rem ordinary main() still runs, because that is inside "#ifndef WIN" and
rem this build does not define WIN, but no console window opens beside
rem the GUI when a user double-clicks it -- which is what a console
rem subsystem binary does on Windows, and what made the first packaged
rem build look like a DOS program. The test build stays a console
rem program, because it prints PASS/FAIL to stdout and the runner has to
rem see it; a WINDOWS-subsystem process started from cmd is detached
rem from the console and its output goes nowhere.
rem
rem The source list is tools\qt-srcs.py's reading of Makefile.srcs, the
rem same list every other build uses, so a new source file is remembered
rem once. The flags are explained where they were first needed:
rem
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
rem   /MP               compile the source files in parallel, one cl.exe
rem                     worker per core. Without it cl.exe takes the 34
rem                     files one at a time: measured 109 s for the
rem                     program and 106 s for the test build on a 4-core
rem                     runner, 215 of the job's 311 seconds, on every
rem                     push. Safe here because nothing uses /Yc, /Yu,
rem                     #import or /showIncludes, which are the things
rem                     /MP cannot combine with.
rem
rem NOTE: rem cannot appear between "cl" and its last "^" continuation --
rem cmd passes the word to the compiler as a source file and the link
rem dies with "LNK1181: cannot open input file 'rem.obj'". It did.

setlocal
set "OUT=%~1"
if "%OUT%"=="" (
  echo usage: msvc-build-qt.cmd ^<output.exe^> [--test]
  exit /b 2
)
if not defined QTDIR (
  echo QTDIR is not set -- point it at a Qt for MSVC
  exit /b 2
)
if not exist Makefile.srcs (
  echo run this from the repository root
  exit /b 2
)

set "TESTDEF="
set "SUBSYS=/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup"
set "SRCARGS="
if "%~2"=="--test" (
  set "TESTDEF=/DQTTEST"
  set "SUBSYS=/SUBSYSTEM:CONSOLE"
  set "SRCARGS=--test"
)

python tools\qt-srcs.py %SRCARGS% > "%OUT%.srcs" || exit /b 1

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
rem by way of tools\version.py, never typed here.
for /f "usebackq tokens=*" %%v in (`python tools\version.py --rc`) do set "VERRC=%%v"
for /f "usebackq tokens=*" %%v in (`python tools\version.py`) do set "VERSTR=%%v"
if not defined VERRC exit /b 1
rc /nologo /I. /DVERSIONRC=%VERRC% /DVERSIONSTR=\"%VERSTR%\" /fo "%OUT%.res" tools\astrolog-qt.rc || exit /b 1

cl /nologo /MP /std:c++17 /Zc:__cplusplus /permissive- /Zc:strictStrings- /EHsc /MD /O2 /W3 ^
   /D_CRT_SECURE_NO_WARNINGS ^
   /DQT %TESTDEF% /DPC /DWIN32 /D_WINDOWS /DNDEBUG ^
   /I"%QTDIR%\include" /I"%QTDIR%\include\QtCore" ^
   /I"%QTDIR%\include\QtGui" /I"%QTDIR%\include\QtWidgets" ^
   /I"%QTDIR%\include\QtNetwork" /I"%QTDIR%\include\QtPrintSupport" ^
   @"%OUT%.srcs" "%OUT%.res" ^
   /Fe:"%OUT%" ^
   /link /LIBPATH:"%QTDIR%\lib" %SUBSYS% ^
   Qt6Widgets.lib Qt6Gui.lib Qt6Core.lib Qt6Network.lib Qt6PrintSupport.lib ^
   user32.lib gdi32.lib shell32.lib advapi32.lib ole32.lib
if errorlevel 1 exit /b 1

if not exist "%OUT%" (
  echo No %OUT% produced
  exit /b 1
)
for %%f in ("%OUT%") do echo built %OUT%, %%~zf bytes
endlocal
exit /b 0
