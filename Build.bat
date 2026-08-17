@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title Visual MediaPlayer - Build

if exist "VisualMediaPlayer.exe" (
  echo VisualMediaPlayer.exe already exists.
  echo.
  choice /C RB /N /M "[R]un it or [B]uild it again? "
  if errorlevel 2 goto :build
  if errorlevel 1 (
    start "" "%~dp0VisualMediaPlayer.exe"
    exit /b 0
  )
)

:build
echo.
echo Building Visual MediaPlayer...
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

set "PROJECT=%~dp0_source\VisualMediaPlayer.vcxproj"
set "BUILT_EXE=%~dp0_source\x64\Release\VisualMediaPlayer.exe"

if exist "%~dp0VisualMediaPlayer.exe" del /q "%~dp0VisualMediaPlayer.exe"

rem Use the project's normal output paths. Avoid overriding OutDir/IntDir;
rem trailing path separators in command-line MSBuild properties can be misparsed.
msbuild "%PROJECT%" /t:Clean /m /nologo /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 >nul
msbuild "%PROJECT%" /t:Build /m /nologo /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145

if errorlevel 1 (
  echo.
  echo BUILD FAILED. Send me a screenshot of the red/error text above.
  echo.
  pause
  exit /b 1
)

if not exist "%BUILT_EXE%" (
  echo.
  echo ERROR: Build completed but VisualMediaPlayer.exe was not found at:
  echo %BUILT_EXE%
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

rem Remove compiler output so the extracted folder stays simple.
if exist "%~dp0_source\x64" rmdir /s /q "%~dp0_source\x64" 2>nul
for /d %%D in ("%~dp0_source\VisualMe.*") do rmdir /s /q "%%~fD" 2>nul

if not exist "%~dp0VisualMediaPlayer.exe" (
  echo.
  echo ERROR: The final EXE could not be verified.
  pause
  exit /b 1
)

echo.
echo ========================================
echo DONE
echo ========================================
echo.
echo VisualMediaPlayer.exe is ready.
echo.
start "" "%~dp0VisualMediaPlayer.exe"
exit /b 0
