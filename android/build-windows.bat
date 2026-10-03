@echo off
rem Builds the Android APK on Windows: generates the game (Python), fetches SDL2 if needed, runs Gradle.
rem The images (orig\IMG), the generated data, art\ and sounds\ are put into the APK if they are there.
rem
rem   android\build-windows.bat              build
rem   android\build-windows.bat install      build and install on the connected device (adb)
rem   android\build-windows.bat regen        generate the game again first
setlocal enabledelayedexpansion
cd /d "%~dp0"
set ROOT=%~dp0..

set DOINSTALL=0
set DOREGEN=0
for %%a in (%*) do (
    if /i "%%a"=="install" set DOINSTALL=1
    if /i "%%a"=="regen" set DOREGEN=1
)

if not exist local.properties if "%ANDROID_HOME%"=="" (
    echo No Android SDK is set up: run android\setup-windows.bat first
    exit /b 1
)

if "%DOREGEN%"=="1" goto :regen
if not exist "%ROOT%\build\gen\c\modules.cmake" goto :regen
goto :sdl
:regen
python regen_gen.py "%ROOT%\build\gen"
if errorlevel 1 exit /b 1

:sdl
if not exist SDL\android-project (
    if not exist SDL mkdir SDL
    curl -L -o "%TEMP%\SDL2.tar.gz" https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9.tar.gz
    if errorlevel 1 exit /b 1
    tar -xzf "%TEMP%\SDL2.tar.gz" -C SDL --strip-components=1
)

call gradlew.bat assembleRelease
if errorlevel 1 exit /b 1

set APK=app\build\outputs\apk\release\app-release.apk
echo.
echo APK: %CD%\%APK%
if "%DOINSTALL%"=="1" (
    where adb >nul 2>nul && (adb install -r "%APK%") || (
        for /f "tokens=2 delims==" %%s in ('findstr "sdk.dir" local.properties') do "%%s\platform-tools\adb.exe" install -r "%APK%"
    )
)
