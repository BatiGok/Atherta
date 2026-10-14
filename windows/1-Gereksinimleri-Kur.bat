@echo off
rem SPDX-FileCopyrightText: 2026 Atherta
rem SPDX-License-Identifier: GPL-3.0-or-later
rem
rem Installs the tools needed to build Krita with the Atherta 3D Paint plugin.
rem Uses winget (comes with Windows 10/11 as "App Installer").

echo.
echo === Atherta 3D Paint: gerekli araclar kuruluyor ===
echo Git, Python 3.12, CMake 3.31, Ninja ve 7-Zip kurulacak.
echo Her biri icin onay penceresi cikabilir.
echo.

where winget >nul 2>nul
if errorlevel 1 (
    echo HATA: winget bulunamadi. Microsoft Store'dan "App Installer" uygulamasini kurun.
    pause
    exit /b 1
)

winget install -e --id Git.Git --accept-source-agreements --accept-package-agreements
winget install -e --id Python.Python.3.12 --accept-source-agreements --accept-package-agreements
winget install -e --id Kitware.CMake --version 3.31.6 --accept-source-agreements --accept-package-agreements
if errorlevel 1 winget install -e --id Kitware.CMake --accept-source-agreements --accept-package-agreements
winget install -e --id Ninja-build.Ninja --accept-source-agreements --accept-package-agreements
winget install -e --id 7zip.7zip --accept-source-agreements --accept-package-agreements

echo.
echo === Bitti ===
echo Simdi bu pencereyi kapatip "2-Krita-ve-Eklentiyi-Derle.bat" dosyasini calistirin.
echo (Zaten kurulu olan araclar icin winget "zaten kurulu" diyebilir, sorun degil.)
pause
