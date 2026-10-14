@echo off
rem Recto's one-command build: fetches submodules, restores packages and
rem builds Recto.sln for x64 and ARM64 in Debug and Release, then runs
rem the x64 tests. All logic lives in build\build.ps1; pass its options
rem through here, for example: build.cmd -Platform x64
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build\build.ps1" %*
exit /b %ERRORLEVEL%
