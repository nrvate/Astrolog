; Windows installer for Astrolog, built with NSIS.
;
; NSIS rather than Inno Setup or WiX for one reason that matters here:
; makensis runs natively on LINUX, so this is built by the same
; ubuntu-22.04 job that already cross-compiles astrolog.exe with mingw.
; Inno Setup needs Wine, WiX needs Windows and .NET, and a 7-Zip SFX is
; an extractor rather than an installer -- no Start Menu entry, no
; uninstaller, no Add/Remove Programs row.
;
; Built by tools/package-windows-installer.sh, which passes VERSION and
; SRCDIR on the command line. Do not run makensis on this directly.

!ifndef VERSION
  !error "VERSION not defined -- use tools/package-windows-installer.sh"
!endif
!ifndef SRCDIR
  !error "SRCDIR not defined -- use tools/package-windows-installer.sh"
!endif

Name "Astrolog ${VERSION}"
OutFile "${OUTFILE}"
Unicode true
; 64-bit: the binary is x86_64, so it belongs in Program Files, not the
; (x86) tree that $PROGRAMFILES points at on a 64-bit Windows.
InstallDir "$PROGRAMFILES64\Astrolog"
InstallDirRegKey HKLM "Software\Astrolog" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

!include "MUI2.nsh"
!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_LICENSE "${SRCDIR}\license.htm"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "Astrolog" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"
  ; Everything the package stages. Astrolog resolves ephem/, font/ and
  ; its data files relative to its own executable, so a flat install
  ; directory is what it wants -- no registry paths, no environment.
  File /r "${SRCDIR}\*.*"

  WriteRegStr HKLM "Software\Astrolog" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "Software\Astrolog" "Version" "${VERSION}"

  ; The Add/Remove Programs row, so this uninstalls like anything else.
  !define UNKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Astrolog"
  WriteRegStr HKLM "${UNKEY}" "DisplayName"     "Astrolog ${VERSION}"
  WriteRegStr HKLM "${UNKEY}" "DisplayVersion"  "${VERSION}"
  WriteRegStr HKLM "${UNKEY}" "Publisher"       "Astrolog"
  WriteRegStr HKLM "${UNKEY}" "URLInfoAbout"    "https://github.com/nrvate/Astrolog"
  WriteRegStr HKLM "${UNKEY}" "DisplayIcon"     "$INSTDIR\astrolog.exe"
  WriteRegStr HKLM "${UNKEY}" "UninstallString" "$INSTDIR\uninstall.exe"
  WriteRegDWORD HKLM "${UNKEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNKEY}" "NoRepair" 1

  CreateDirectory "$SMPROGRAMS\Astrolog"
  CreateShortCut "$SMPROGRAMS\Astrolog\Astrolog.lnk" "$INSTDIR\astrolog.exe"
  CreateShortCut "$SMPROGRAMS\Astrolog\Uninstall Astrolog.lnk" "$INSTDIR\uninstall.exe"

  WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd

Section "Uninstall"
  Delete "$SMPROGRAMS\Astrolog\Astrolog.lnk"
  Delete "$SMPROGRAMS\Astrolog\Uninstall Astrolog.lnk"
  RMDir "$SMPROGRAMS\Astrolog"
  ; RMDir /r on $INSTDIR, which is only safe because InstallDir is ours
  ; and the uninstaller lives inside it. It is still worth being explicit
  ; that this removes the whole directory, charts the user saved there
  ; included.
  RMDir /r "$INSTDIR"
  DeleteRegKey HKLM "Software\Astrolog"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Astrolog"
SectionEnd
