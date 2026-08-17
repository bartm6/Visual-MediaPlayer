@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title Visual MediaPlayer - Portable Build

echo.
echo Building Visual MediaPlayer Portable...
echo.

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo ERROR: Visual Studio C++ compiler was not found.
  echo Install the Desktop development with C++ workload, then run this again.
  pause
  exit /b 1
)

set "VS="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS=%%i"
if not defined VS (
  echo ERROR: Visual Studio C++ x64 tools were not found.
  pause
  exit /b 1
)

call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
  echo ERROR: Could not initialize the Visual Studio x64 compiler.
  pause
  exit /b 1
)

set "PROJECT=%~dp0Source\VisualMediaPlayer.vcxproj"
set "BUILT_EXE=%~dp0Source\x64\Release\VisualMediaPlayer.exe"

if exist "%~dp0VisualMediaPlayer.exe" del /q "%~dp0VisualMediaPlayer.exe"

msbuild "%PROJECT%" /t:Clean /m /nologo /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 >nul
msbuild "%PROJECT%" /t:Build /m /nologo /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145

if errorlevel 1 (
  echo.
  echo BUILD FAILED. Send me a screenshot of the error text above.
  echo.
  pause
  exit /b 1
)

if not exist "%BUILT_EXE%" (
  echo.
  echo ERROR: Build completed but VisualMediaPlayer.exe was not found.
  echo.
  pause
  exit /b 1
)

copy /y "%BUILT_EXE%" "%~dp0VisualMediaPlayer.exe" >nul
if errorlevel 1 (
  echo.
  echo ERROR: Could not copy the finished app into this folder.
  pause
  exit /b 1
)

if exist "%~dp0Source\x64" rmdir /s /q "%~dp0Source\x64" 2>nul
for /d %%D in ("%~dp0Source\VisualMe.*") do rmdir /s /q "%%~fD" 2>nul

echo.
echo ========================================
echo DONE
 echo ========================================
echo.
echo Portable VisualMediaPlayer.exe is ready next to Build.bat.
echo.
pause
exit /b 0
