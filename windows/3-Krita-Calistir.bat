@echo off
rem SPDX-FileCopyrightText: 2026 Atherta
rem SPDX-License-Identifier: GPL-3.0-or-later
rem
rem Starts the Krita build that contains the Atherta 3D Paint plugin.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\run-krita.ps1" %*
if errorlevel 1 pause
