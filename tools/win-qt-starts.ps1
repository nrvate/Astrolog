# The staged Windows program starts, on Windows, and opens a window.
#
#   powershell -File tools\win-qt-starts.ps1 dist [seconds]
#
# The Linux packages are installed into a clean container and asked for
# Chiron. The Win32 build got tools/ci-verify-windows-starts.sh, which
# runs it under Wine and waits for a window titled "Astrolog". This is
# the same question asked of the Qt build, on the platform it ships for,
# with no Wine in between: start dist\astrolog.exe with the REAL windows
# platform plugin -- not offscreen, which the suite uses and which proves
# nothing about qwindows.dll being present or loadable -- and require a
# top-level window whose title begins "Astrolog".
#
# That covers the failures a file listing cannot see: a Qt DLL
# windeployqt did not bring, a missing platform plugin (Qt then exits at
# once with "could not find the Qt platform plugin"), a CRT DLL the
# runner had and the user will not, and a crash on startup. Each is an
# exit without a window, and each is reported with what the process
# said.
#
# GitHub's Windows runners have an interactive desktop, unlike a
# VirtualBox guestcontrol session (which has none, and where the windows
# plugin blocks forever -- see tools/win-vm-suite.sh). If this is ever
# run somewhere without one, it fails by timeout rather than by hanging.
#
# Killed by the process object it started, never by name: a name match
# would take down any other Astrolog on the machine.

param(
  [string]$Dir = "dist",
  [int]$Seconds = 60
)

$exe = Join-Path $Dir "astrolog.exe"
if (-not (Test-Path $exe)) { Write-Host "no such file: $exe"; exit 2 }

# Nothing inherited from the suite's environment: the point is the
# default platform plugin.
Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue

$out = Join-Path $env:TEMP "astrolog-starts-out.txt"
$err = Join-Path $env:TEMP "astrolog-starts-err.txt"
$p = Start-Process -FilePath (Resolve-Path $exe) -WorkingDirectory (Resolve-Path $Dir) `
       -PassThru -RedirectStandardOutput $out -RedirectStandardError $err

$title = ""
$deadline = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $deadline) {
  Start-Sleep -Milliseconds 250
  $p.Refresh()
  if ($p.HasExited) { break }
  if ($p.MainWindowTitle -like "Astrolog*") { $title = $p.MainWindowTitle; break }
}

if ($title) {
  Write-Host "windows program starts: window titled '$title'"
  Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
  exit 0
}

Write-Host "THE STAGED PROGRAM NEVER OPENED A WINDOW."
if ($p.HasExited) {
  Write-Host "== The process EXITED, code $($p.ExitCode), without mapping a window."
  Write-Host "== A Qt program that exits at once is usually missing a DLL or the"
  Write-Host "== platform plugin; a code of -1073741515 (0xC0000135) is the"
  Write-Host "== loader saying so before main() ran."
} else {
  Write-Host "== Still running after ${Seconds}s with no window: started and hung."
  Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
}
Write-Host "== What the program said:"
foreach ($f in @($out, $err)) {
  if ((Test-Path $f) -and ((Get-Item $f).Length -gt 0)) {
    Get-Content $f -TotalCount 40 | ForEach-Object { "     $_" }
  }
}
exit 1
