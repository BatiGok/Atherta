@echo off
rem SPDX-FileCopyrightText: 2026 Atherta
rem SPDX-License-Identifier: GPL-3.0-or-later
rem
rem Installs the tools needed to build Krita with the Atherta 3D Paint plugin.
rem Uses winget (comes with Windows 10/11 as "App Installer").
rem
rem Tools that are already present are skipped: winget would otherwise try to
rem upgrade them, which is not needed and can hang on "Waiting for another
rem install/uninstall to complete" while Windows Update or another installer
rem holds the Windows Installer lock.

setlocal EnableExtensions

echo.
echo === Atherta 3D Paint: gerekli araclar kontrol ediliyor ===
echo Gerekenler: Git, Python 3.10-3.13, CMake, Ninja, 7-Zip.
echo Kurulu olanlar atlanir, eksik olanlar winget ile kurulur.
echo.

where winget >nul 2>nul
if errorlevel 1 (
    echo HATA: winget bulunamadi. Microsoft Store'dan "App Installer" uygulamasini kurun.
    pause
    exit /b 1
)

call :ensure_tool git Git.Git
call :ensure_python
call :ensure_tool cmake Kitware.CMake
call :ensure_tool ninja Ninja-build.Ninja
call :ensure_7zip

echo.
echo === Bitti ===
echo Simdi bu pencereyi kapatip "2-Krita-ve-Eklentiyi-Derle.bat" dosyasini calistirin.
echo.
echo Not: Bir kurulum "Waiting for another install/uninstall to complete" satirinda
echo uzun sure beklerse, Windows Update veya Microsoft Store baska bir kurulum
echo yapiyor demektir. Onun bitmesini bekleyin ya da bilgisayari yeniden baslatip
echo bu dosyayi tekrar calistirin.
pause
exit /b 0


rem ---------------------------------------------------------------------------
rem :ensure_tool <command> <winget id>
:ensure_tool
where %1 >nul 2>nul
if not errorlevel 1 (
    echo [tamam] %1 zaten kurulu, atlaniyor.
    exit /b 0
)
echo [kur]   %2 kuruluyor...
winget install -e --id %2 --accept-source-agreements --accept-package-agreements --disable-interactivity
exit /b 0

rem ---------------------------------------------------------------------------
rem Any Python 3.10 - 3.13 is fine; the build script prefers 3.12.
:ensure_python
for %%v in (3.12 3.13 3.11 3.10) do (
    py -%%v -c "import sys" >nul 2>nul && (
        echo [tamam] Python %%v zaten kurulu, atlaniyor.
        exit /b 0
    )
)
echo [kur]   Python 3.12 kuruluyor...
winget install -e --id Python.Python.3.12 --accept-source-agreements --accept-package-agreements --disable-interactivity
exit /b 0

rem ---------------------------------------------------------------------------
:ensure_7zip
if exist "%ProgramFiles%\7-Zip\7z.exe" (
    echo [tamam] 7-Zip zaten kurulu, atlaniyor.
    exit /b 0
)
where 7z >nul 2>nul
if not errorlevel 1 (
    echo [tamam] 7-Zip zaten kurulu, atlaniyor.
    exit /b 0
)
echo [kur]   7-Zip kuruluyor...
winget install -e --id 7zip.7zip --accept-source-agreements --accept-package-agreements --disable-interactivity
exit /b 0
