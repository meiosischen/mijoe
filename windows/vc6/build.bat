@echo off
setlocal
cd /d "%~dp0"
rem Optional: VC6_ROOT may name the Visual Studio directory or its VC98 folder.
if "%VC6_ROOT%"=="" goto build
set "VC6_DIR="
if exist "%VC6_ROOT%\VC98\bin\cl.exe" set "VC6_DIR=%VC6_ROOT%\VC98"
if exist "%VC6_ROOT%\bin\cl.exe" set "VC6_DIR=%VC6_ROOT%"
if not defined VC6_DIR goto badroot
set "PATH=%VC6_DIR%\bin;%VC6_DIR%\..\Common\MSDev98\bin;%PATH%"
set "INCLUDE=%VC6_DIR%\include"
set "LIB=%VC6_DIR%\lib"
:build
set "VC6_CFG=%~1"
if "%VC6_CFG%"=="" set "VC6_CFG=Release"
nmake /nologo /f joewin.mak CFG="%VC6_CFG%" %2
exit /b %errorlevel%
:badroot
echo ERROR: VC6_ROOT does not contain VC98\bin\cl.exe or bin\cl.exe.
exit /b 1
