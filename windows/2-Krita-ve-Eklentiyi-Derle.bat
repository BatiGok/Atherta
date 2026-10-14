@echo off
rem SPDX-FileCopyrightText: 2026 Atherta
rem SPDX-License-Identifier: GPL-3.0-or-later
rem
rem Builds Krita 5.2 together with the Atherta 3D Paint plugin.
rem All heavy work happens in C:\KritaBuild (a short path without spaces,
rem which Krita's build needs). Run it again to resume after a failure.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build-krita-atherta.ps1" %*
echo.
pause
