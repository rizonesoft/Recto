@echo off
rem Recto's one-command build: restores packages and builds Recto.sln
rem for x64 and ARM64 in Debug and Release, then runs the x64 tests.
rem All logic lives in build\build.ps1; pass its options through here,
rem for example: build.cmd -Platform x64
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build\build.ps1" %*
exit /b %ERRORLEVEL%
