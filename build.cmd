@echo off
rem Recto's one-command build: restores packages and builds Recto.sln
rem for x64 in Debug and Release, then runs the x64 tests (ARM64 is built
rem in CI; pass -Platform x64,ARM64 or -Platform ARM64 to build ARM64 locally).
rem All logic lives in build\build.ps1; pass its options through here
rem (-MaxCpuCount 0 selects automatic memory-bounded sizing; RECTO_BUILD_JOBS
rem overrides the compiler count), for example: build.cmd -Platform x64,ARM64
setlocal
set "_RECTO_PS=powershell.exe"
where powershell.exe >nul 2>&1
if errorlevel 1 if exist "%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" set "_RECTO_PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
"%_RECTO_PS%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0build\bootstrap-tools.ps1"
if errorlevel 1 exit /b %ERRORLEVEL%
endlocal & "%_RECTO_PS%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0build\build.ps1" %*
exit /b %ERRORLEVEL%
