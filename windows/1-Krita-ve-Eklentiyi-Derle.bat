@echo off
rem SPDX-FileCopyrightText: 2026 Atherta
rem SPDX-License-Identifier: GPL-3.0-or-later
rem
rem Builds Krita 5.2 together with the Atherta 3D Paint plugin. Nothing is
rem installed on the computer; missing tools are downloaded as portable copies
rem into the work folder (C:\KritaBuild). Run it again to resume after a failure.

if not exist "%~dp0scripts\build-krita-atherta.ps1" (
    echo HATA: scripts klasoru bulunamadi.
    echo Zip dosyasini once bir klasore cikarin ^(sag tik - Tumunu ayikla^)
    echo ve bu dosyayi o klasorden calistirin.
    pause
    exit /b 1
)

echo Not: Derleme surerken bu pencerenin ICINE tiklamayin. Tiklarsaniz Windows
echo pencereyi "secim" moduna alir ve derleme durur; devam etmesi icin Esc'e basin.
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build-krita-atherta.ps1" %*
echo.
pause
