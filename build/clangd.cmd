@echo off
setlocal EnableExtensions DisableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

set "VS_INSTALL=%RECTO_VS_INSTALL%"
if defined VS_INSTALL goto :have_vs

set "FOUND_VS="
setlocal EnableDelayedExpansion
set "VSWHERE=!ProgramFiles(x86)!\Microsoft Visual Studio\Installer\vswhere.exe"
set "VS_VER=[18.0,19.0)"
if exist "!VSWHERE!" (
    for /f "usebackq delims=" %%I in (`""!VSWHERE!" -version "!VS_VER!" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>nul"`) do set "FOUND_VS=%%I"
)
endlocal & set "VS_INSTALL=%FOUND_VS%"

:have_vs
set "CLANGD_EXE="
if not defined VS_INSTALL goto :check_clangd
if "%VS_INSTALL:~-1%"=="\" set "VS_INSTALL=%VS_INSTALL:~0,-1%"
if /I "%PROCESSOR_ARCHITECTURE%"=="ARM64" if exist "%VS_INSTALL%\VC\Tools\Llvm\ARM64\bin\clangd.exe" set "CLANGD_EXE=%VS_INSTALL%\VC\Tools\Llvm\ARM64\bin\clangd.exe"
if not defined CLANGD_EXE if exist "%VS_INSTALL%\VC\Tools\Llvm\x64\bin\clangd.exe" set "CLANGD_EXE=%VS_INSTALL%\VC\Tools\Llvm\x64\bin\clangd.exe"
if not defined CLANGD_EXE if exist "%VS_INSTALL%\VC\Tools\Llvm\ARM64\bin\clangd.exe" set "CLANGD_EXE=%VS_INSTALL%\VC\Tools\Llvm\ARM64\bin\clangd.exe"
if not defined CLANGD_EXE if exist "%VS_INSTALL%\VC\Tools\Llvm\bin\clangd.exe" set "CLANGD_EXE=%VS_INSTALL%\VC\Tools\Llvm\bin\clangd.exe"

:check_clangd
if defined CLANGD_EXE goto :dispatch
echo clangd.cmd: clangd.exe not found; install Visual Studio component Microsoft.VisualStudio.Component.VC.Llvm.Clang 1>&2
exit /b 2

:dispatch
if /I "%~1"=="--version" goto :passthrough
if /I "%~1"=="--help" goto :passthrough
if /I "%~1"=="-h" goto :passthrough
if /I "%~1"=="--help-hidden" goto :passthrough
if /I "%~1"=="--help-list-hidden" goto :passthrough

set "HAS_J="
set "HAS_PCH="
set "HAS_BG="
set "HAS_CCDB="
set "HAS_CHECK="
set "HAS_CHECK_LINES="
set "HAS_TWEAKS="

:parse_flags
if "%~1"=="" goto :run_clangd
set "ARG=%~1"
if /I "%ARG:~0,2%"=="-j" set "HAS_J=1"
if /I "%ARG:~0,13%"=="--pch-storage" set "HAS_PCH=1"
if /I "%ARG:~0,18%"=="--background-index" set "HAS_BG=1"
if /I "%ARG:~0,22%"=="--compile-commands-dir" set "HAS_CCDB=1"
if /I "%ARG:~0,13%"=="--check-lines" set "HAS_CHECK_LINES=1"
if /I "%ARG:~0,7%"=="--check" if /I not "%ARG:~0,13%"=="--check-lines" set "HAS_CHECK=1"
if /I "%ARG:~0,8%"=="--tweaks" set "HAS_TWEAKS=1"
shift
goto :parse_flags

:run_clangd
set "EXTRA_FLAGS="
if not defined HAS_J set "EXTRA_FLAGS=%EXTRA_FLAGS% -j=2"
if not defined HAS_PCH set "EXTRA_FLAGS=%EXTRA_FLAGS% --pch-storage=disk"
if not defined HAS_BG if not defined HAS_CHECK set "EXTRA_FLAGS=%EXTRA_FLAGS% --background-index"
if not defined HAS_CCDB set "EXTRA_FLAGS=%EXTRA_FLAGS% --compile-commands-dir="%SCRIPT_DIR%""
rem --check still builds the full preamble, AST, diagnostics, and index across every line while bounding the O(tokens) tweak/UI-feature fuzzer on 400KB+ translation units and LLVM 22 macro tweak bugs.
if defined HAS_CHECK if not defined HAS_CHECK_LINES set "EXTRA_FLAGS=%EXTRA_FLAGS% --check-lines=1-1"
if defined HAS_CHECK if not defined HAS_TWEAKS set "EXTRA_FLAGS=%EXTRA_FLAGS% --tweaks=-"

"%CLANGD_EXE%" %EXTRA_FLAGS% %*
exit /b %ERRORLEVEL%

:passthrough
"%CLANGD_EXE%" %*
exit /b %ERRORLEVEL%
