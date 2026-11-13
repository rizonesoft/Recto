@echo off
setlocal
if defined RECTO_TOOLS_DIR (
    set "_RECTO_TOOLS=%RECTO_TOOLS_DIR%"
) else (
    set "_RECTO_TOOLS=%~dp0..\.tools"
)
set "_RECTO_LOCK=%~dp0tools.lock.json"
set "_RECTO_VERIFIED=%_RECTO_TOOLS%\tools.lock.verified"
set "_RECTO_PY_PTR=%_RECTO_TOOLS%\cpython-exe.txt"

if not exist "%_RECTO_VERIFIED%" goto bootstrap
if not exist "%_RECTO_PY_PTR%" goto bootstrap
fc /b "%_RECTO_LOCK%" "%_RECTO_VERIFIED%" >nul 2>&1
if errorlevel 1 goto bootstrap
set /p _RECTO_PY_EXE=<"%_RECTO_PY_PTR%"
if not defined _RECTO_PY_EXE goto bootstrap
if not exist "%_RECTO_PY_EXE%" goto bootstrap
goto exec

:bootstrap
set "_RECTO_PS=powershell.exe"
where powershell.exe >nul 2>&1
if errorlevel 1 if exist "%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" set "_RECTO_PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
"%_RECTO_PS%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0bootstrap-tools.ps1" 1>&2
if errorlevel 1 exit /b %ERRORLEVEL%
set "_RECTO_PY_EXE="
if exist "%_RECTO_PY_PTR%" set /p _RECTO_PY_EXE=<"%_RECTO_PY_PTR%"
if not defined _RECTO_PY_EXE (
    echo build\python.cmd: cpython executable pointer missing in "%_RECTO_PY_PTR%" 1>&2
    exit /b 1
)
if not exist "%_RECTO_PY_EXE%" (
    echo build\python.cmd: pinned CPython not found at "%_RECTO_PY_EXE%" 1>&2
    exit /b 1
)

:exec
endlocal & "%_RECTO_PY_EXE%" %*
exit /b %ERRORLEVEL%
