@echo off
@rem The pinned CMake (build\tools.lock.json, D00 T04 sections 6 and 8): Recto's CMakeLists.txt refuses any other.
setlocal
if defined RECTO_TOOLS_DIR (
    set "_RECTO_TOOLS=%RECTO_TOOLS_DIR%"
) else (
    set "_RECTO_TOOLS=%~dp0..\.tools"
)
set "_RECTO_LOCK=%~dp0tools.lock.json"
set "_RECTO_VERIFIED=%_RECTO_TOOLS%\tools.lock.verified"
set "_RECTO_CMAKE_PTR=%_RECTO_TOOLS%\cmake-exe.txt"

if not exist "%_RECTO_VERIFIED%" goto bootstrap
if not exist "%_RECTO_CMAKE_PTR%" goto bootstrap
fc /b "%_RECTO_LOCK%" "%_RECTO_VERIFIED%" >nul 2>&1
if errorlevel 1 goto bootstrap
2>nul set /p _RECTO_CMAKE_EXE=<"%_RECTO_CMAKE_PTR%"
if not defined _RECTO_CMAKE_EXE goto bootstrap
if not exist "%_RECTO_CMAKE_EXE%" goto bootstrap
goto exec

:bootstrap
set "_RECTO_PS=powershell.exe"
where powershell.exe >nul 2>&1
if errorlevel 1 if exist "%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" set "_RECTO_PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
"%_RECTO_PS%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0bootstrap-tools.ps1" 1>&2
if errorlevel 1 exit /b %ERRORLEVEL%
set "_RECTO_CMAKE_EXE="
if exist "%_RECTO_CMAKE_PTR%" 2>nul set /p _RECTO_CMAKE_EXE=<"%_RECTO_CMAKE_PTR%"
if not defined _RECTO_CMAKE_EXE (
    echo build\cmake.cmd: cmake executable pointer missing in "%_RECTO_CMAKE_PTR%" 1>&2
    exit /b 1
)
if not exist "%_RECTO_CMAKE_EXE%" (
    echo build\cmake.cmd: pinned CMake not found at "%_RECTO_CMAKE_EXE%" 1>&2
    exit /b 1
)

:exec
endlocal & "%_RECTO_CMAKE_EXE%" %*
exit /b %ERRORLEVEL%
