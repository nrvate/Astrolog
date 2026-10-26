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
rem graphics code, the vendored Swiss Ephemeris -- are compiled ONCE,
rem without /DQTTEST, and linked into both binaries. What that costs is
rem worth stating rather than glossing: astrolog.h's one core-side use
rem of QTTEST is AssertIndex(), the range guard on the checked tables,
rem so the core's 178 checked subscripts are unguarded in the Windows
rem test binary. They are guarded where that buys something: the Linux
rem test build (Makefile.qt.test) and the ASan console build compile the
rem same core WITH -DQTTEST and no NDEBUG, and those tables are shared
rem core -- a bad index there is a shared-core bug, which those builds
rem catch. What Windows adds to the suite is platform behaviour, not a
rem second look at the same tables.
rem
rem The three files that ARE compiled with /DQTTEST here do get the
rem guard, and did not until 2026-09-05: it was an assert() and this
rem build passes /DNDEBUG. It calls FailIndexQt() now, which does not
rem consult NDEBUG. That function is therefore defined UNCONDITIONALLY
rem in general.cpp -- guarded by "#ifdef QTTEST" it is absent from the
rem core compiled here without it, and the first run after the change
rem said so in three LNK2019s.
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
rem EPHEMERIS PROMETHEIA is compiled in, from the submodule at prometheia\,
rem as every make build does through Makefile.prometheia. Its makefile
rem fragment is GNU make's, so this reads the same source list,
rem prometheia\embed\sources.txt, and honours the same contract
rem (Prometheia's docs/EMBEDDING.md): ISO C++20, and /fp:precise, which is
rem MSVC's default and from Visual Studio 2022 generates no contractions --
rem so no /fp flag is passed at all. The library gets its own flags, not
rem this tree's: the core stays C++17 with its permissive string
rem literals, and only ephprom.cpp and qttest.cpp include the library's C
rem header. Its one dependency is zstd, from vcpkg, named by ZSTD_DIR: a
rem static library against the dynamic CRT, so nothing more ships.
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
findstr /x /c:"contract 1" prometheia\embed\sources.txt >nul || (
  echo prometheia\ is not a Prometheia checkout with embed contract 1 -- git submodule update --init
  exit /b 2
)
if not defined ZSTD_DIR (
  echo ZSTD_DIR is not set -- point it at a zstd for MSVC, e.g. vcpkg's x64-windows-static-md
  exit /b 2
)
set "ZSTDLIB=%ZSTD_DIR%\lib\zstd.lib"
if not exist "%ZSTDLIB%" set "ZSTDLIB=%ZSTD_DIR%\lib\zstd_static.lib"
if not exist "%ZSTDLIB%" (
  echo no zstd.lib or zstd_static.lib under %ZSTD_DIR%\lib
  exit /b 2
)

python tools\qt-srcs.py --only core > srcs-core.txt || exit /b 1
python tools\qt-srcs.py --only qt   > srcs-qt.txt   || exit /b 1
python tools\qt-srcs.py --only test > srcs-test.txt || exit /b 1
if exist srcs-prom.txt del srcs-prom.txt
for /f "usebackq tokens=*" %%s in (`findstr /b /c:"src/" prometheia\embed\sources.txt`) do echo prometheia/%%s>>srcs-prom.txt
if not exist srcs-prom.txt (
  echo prometheia\embed\sources.txt lists no sources
  exit /b 1
)

rem The git sha the About dialog shows. Same FORCE-plus-content-diff
rem generator the makefiles run on Linux; a checkout without .git answers
rem empty and the About line shows no bracket.
python tools\gitsha.py gitsha.h || exit /b 1

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

set CXX=/nologo /MP /std:c++17 /Zc:__cplusplus /permissive- /Zc:strictStrings- /EHsc /MD /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /DQT /DPC /DWIN32 /D_WINDOWS /DNDEBUG /DPROMETHEIA
set PROMCXX=/nologo /MP /std:c++20 /Zc:__cplusplus /permissive- /EHsc /MD /O2 /W3 /Iprometheia\include /I"%ZSTD_DIR%\include"
set INC=/I"%QTDIR%\include" /I"%QTDIR%\include\QtCore" /I"%QTDIR%\include\QtGui" /I"%QTDIR%\include\QtWidgets" /I"%QTDIR%\include\QtNetwork" /I"%QTDIR%\include\QtPrintSupport" /I"%QTDIR%\include\QtWebSockets" /Iephsrv /Iprometheia\include
set LIBS=/LIBPATH:"%QTDIR%\lib" Qt6Widgets.lib Qt6Gui.lib Qt6Core.lib Qt6Network.lib Qt6PrintSupport.lib Qt6WebSockets.lib user32.lib gdi32.lib shell32.lib advapi32.lib ole32.lib obj-prom\*.obj "%ZSTDLIB%"

if not exist obj mkdir obj
if not exist obj-app mkdir obj-app
if not exist obj-test mkdir obj-test
if not exist obj-prom mkdir obj-prom

echo == Ephemeris Prometheia, from prometheia\embed\sources.txt
cl %PROMCXX% /c /Fo"obj-prom\\" @srcs-prom.txt || exit /b 1

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
