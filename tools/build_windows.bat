@echo off
rem Builds the Windows version (wwf.exe) natively with Visual Studio (or the Build Tools), CMake and Python 3,
rem and collects what it needs to run into dist-windows\.
rem
rem   Needs: Visual Studio 2019/2022 with "Desktop development with C++" (or the Build Tools), CMake and Python 3
rem   on the PATH (python), and orig\ (see the README). Downloads the SDL2 Visual C++ package once.
rem   Sounds are not included: extract them with dcsrip and put the sounds\ folder next to wwf.exe.
rem
rem   tools\build_windows.bat
rem
rem NOT TESTED: the author has only run the program on macOS, Android and Linux. This script follows the
rem project's MinGW build (tools/build_windows.sh), which is the Windows route that has been written down and
rem cross-compiled; check the output before trusting it.
setlocal
cd /d "%~dp0\.."
set SDL_VER=2.30.9
set WORK=build-windows-msvc

where cmake >nul 2>nul || (echo cmake is missing & exit /b 1)
where python >nul 2>nul || (echo python is missing & exit /b 1)
if not exist orig\IMG (echo orig\IMG is missing: see the README & exit /b 1)

if not exist "%WORK%" mkdir "%WORK%"
if not exist "%WORK%\SDL2-%SDL_VER%" (
    powershell -NoProfile -Command "Invoke-WebRequest -Uri https://github.com/libsdl-org/SDL/releases/download/release-%SDL_VER%/SDL2-devel-%SDL_VER%-VC.zip -OutFile %WORK%\sdl2.zip" || exit /b 1
    powershell -NoProfile -Command "Expand-Archive -Force %WORK%\sdl2.zip %WORK%" || exit /b 1
)
set SDL2_ROOT=%CD%\%WORK%\SDL2-%SDL_VER%

cmake -S . -B "%WORK%\out" -A x64 -DSDL2_DIR="%SDL2_ROOT%\cmake" || exit /b 1
cmake --build "%WORK%\out" --config Release --target wwf || exit /b 1

set DIST=dist-windows
if exist "%DIST%" rmdir /s /q "%DIST%"
mkdir "%DIST%\orig"
copy "%WORK%\out\Release\wwf.exe" "%DIST%\" >nul
copy "%SDL2_ROOT%\lib\x64\SDL2.dll" "%DIST%\" >nul
xcopy /e /i /q "%WORK%\out\gen" "%DIST%\gen" >nul
if exist "%DIST%\gen\c" rmdir /s /q "%DIST%\gen\c"
xcopy /e /i /q orig\IMG "%DIST%\orig\IMG" >nul
(
echo @echo off
echo rem Change the options to taste: --res 1920x1080 --scale 4 --classic
echo wwf.exe --gen gen --img orig\IMG --res 1920x1080 %%*
) > "%DIST%\wwf.bat"
echo Done: %DIST%\ (run wwf.bat; put sounds\ next to it for sound)
