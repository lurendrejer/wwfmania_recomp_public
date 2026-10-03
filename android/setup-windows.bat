@echo off
rem One-time set-up on Windows for building the Android app: Java 17, Python 3 and the Android command line tools
rem with the SDK/NDK packages the build asks for. Uses winget (Windows 10/11). Written without a Windows machine to
rem run it on: tell me what fails.
rem
rem   android\setup-windows.bat
setlocal enabledelayedexpansion
cd /d "%~dp0"

echo == Java 17
where java >nul 2>nul || winget install -e --id EclipseAdoptium.Temurin.17.JDK --accept-package-agreements --accept-source-agreements
echo == Python 3
where python >nul 2>nul || winget install -e --id Python.Python.3.12 --accept-package-agreements --accept-source-agreements

set SDK=%LOCALAPPDATA%\Android\Sdk
if not exist "%SDK%\cmdline-tools\latest\bin\sdkmanager.bat" (
    echo == Android command line tools
    mkdir "%SDK%\cmdline-tools" 2>nul
    curl -L -o "%TEMP%\cmdline-tools.zip" https://dl.google.com/android/repository/commandlinetools-win-11076708_latest.zip
    if errorlevel 1 goto :fail
    tar -xf "%TEMP%\cmdline-tools.zip" -C "%SDK%\cmdline-tools"
    if exist "%SDK%\cmdline-tools\latest" rmdir /s /q "%SDK%\cmdline-tools\latest"
    ren "%SDK%\cmdline-tools\cmdline-tools" latest
)

echo == SDK and NDK packages (a big download the first time)
(for /l %%i in (1,1,30) do @echo y) | call "%SDK%\cmdline-tools\latest\bin\sdkmanager.bat" --sdk_root="%SDK%" --licenses >nul
call "%SDK%\cmdline-tools\latest\bin\sdkmanager.bat" --sdk_root="%SDK%" "platforms;android-34" "build-tools;34.0.0" "platform-tools" "ndk;26.1.10909125" "cmake;3.22.1"
if errorlevel 1 goto :fail

set SDKF=%SDK:\=/%
echo sdk.dir=%SDKF%> local.properties
echo == done: android\local.properties points at %SDK%
echo If Java or Python was just installed, open a new terminal window before the next step.
echo Next: android\build-windows.bat
exit /b 0

:fail
echo Something failed above.
exit /b 1
