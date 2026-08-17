@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title Visual MediaPlayer - Build Installer

echo.
echo Building Visual MediaPlayer installer...
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

set "APP_PROJECT=%~dp0Source\VisualMediaPlayer.vcxproj"
set "APP_EXE=%~dp0Source\x64\Release\VisualMediaPlayer.exe"
set "PAYLOAD_DIR=%~dp0Installer\payload"
set "PAYLOAD_EXE=%PAYLOAD_DIR%\VisualMediaPlayer.exe"
set "SETUP_PROJECT=%~dp0Installer\VisualMediaPlayerSetup.vcxproj"
set "BUILT_SETUP=%~dp0Installer\x64\Release\VisualMediaPlayerSetup.exe"
set "FINAL_SETUP=%~dp0VisualMediaPlayerSetup.exe"

if exist "%FINAL_SETUP%" del /q "%FINAL_SETUP%"
if not exist "%PAYLOAD_DIR%" mkdir "%PAYLOAD_DIR%"
if exist "%PAYLOAD_EXE%" del /q "%PAYLOAD_EXE%"

echo [1/2] Building Visual MediaPlayer...
msbuild "%APP_PROJECT%" /t:Clean /m /nologo /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 >nul
msbuild "%APP_PROJECT%" /t:Build /m /nologo /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145
if errorlevel 1 goto :failed

if not exist "%APP_EXE%" (
  echo ERROR: VisualMediaPlayer.exe was not produced.
  goto :failed
)
copy /y "%APP_EXE%" "%PAYLOAD_EXE%" >nul
if errorlevel 1 goto :failed

echo.
echo [2/2] Building VisualMediaPlayerSetup.exe...
msbuild "%SETUP_PROJECT%" /t:Clean /m /nologo /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 >nul
msbuild "%SETUP_PROJECT%" /t:Build /m /nologo /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145
if errorlevel 1 goto :failed

if not exist "%BUILT_SETUP%" (
  echo ERROR: VisualMediaPlayerSetup.exe was not produced.
  goto :failed
)
copy /y "%BUILT_SETUP%" "%FINAL_SETUP%" >nul
if errorlevel 1 goto :failed

call :cleanup

echo.
echo ========================================
echo DONE
echo ========================================
echo.
echo VisualMediaPlayerSetup.exe is ready.
echo Upload this ONE file to GitHub Releases.
echo.
pause
exit /b 0

:failed
echo.
echo BUILD FAILED. Send me a screenshot of the red/error text above.
echo.
call :cleanup
pause
exit /b 1

:cleanup
if exist "%PAYLOAD_EXE%" del /q "%PAYLOAD_EXE%" 2>nul
if exist "%~dp0Source\x64" rmdir /s /q "%~dp0Source\x64" 2>nul
if exist "%~dp0Installer\x64" rmdir /s /q "%~dp0Installer\x64" 2>nul
for /d %%D in ("%~dp0Source\VisualMe.*") do rmdir /s /q "%%~fD" 2>nul
for /d %%D in ("%~dp0Installer\VisualMe.*") do rmdir /s /q "%%~fD" 2>nul
exit /b 0
