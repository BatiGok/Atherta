@echo off
rem SPDX-FileCopyrightText: 2026 Atherta
rem SPDX-License-Identifier: GPL-3.0-or-later
rem
rem Starts the Krita build that contains the Atherta 3D Paint plugin.

if not exist "%~dp0scripts\run-krita.ps1" (
    echo HATA: scripts klasoru bulunamadi.
    echo Bu dosyayi KritaFiles klasorunun icinden calistirin.
    pause
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\run-krita.ps1" %*
if not "%errorlevel%"=="0" pause
