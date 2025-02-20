:: SPDX-FileCopyrightText: Stone Tickle <lattis@mochiro.moe>
:: SPDX-License-Identifier: GPL-3.0-only

@echo off
setlocal
cd /D "%~dp0"

if "%~1" == "" goto :usage

where cl >nul 2>nul
if %ERRORLEVEL%==0 goto :build

:: Attempt to run vcvarsall if cl was not found.
for /f "tokens=*" %%g in (
'"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath'
) do (set installation_path=%%g)
call "%installation_path%\VC\Auxiliary\Build\vcvarsall" x64

:build

set dir=%1
if not exist "%dir%" mkdir "%dir%"

call cl /nologo /Zi /std:c11 /Iinclude src/amalgam.c /link /out:"%dir%/muon-bootstrap.exe"
goto :eof

:usage
echo usage: %0 build_dir
