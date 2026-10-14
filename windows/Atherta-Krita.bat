@echo off
setlocal
title Atherta 3D Paint - Krita
set "ATHERTA_URL=https://raw.githubusercontent.com/BatiGok/Atherta/claude/nice-babbage-lgnynz/windows/bootstrap.ps1"
set "ATHERTA_PS1=%TEMP%\atherta-bootstrap.ps1"
echo Atherta 3D Paint: guncel betik indiriliyor...
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; [Net.ServicePointManager]::SecurityProtocol='Tls12'; Invoke-WebRequest -UseBasicParsing -Uri $env:ATHERTA_URL -OutFile $env:ATHERTA_PS1"
if not "%errorlevel%"=="0" goto indirilemedi
powershell -NoProfile -ExecutionPolicy Bypass -File "%ATHERTA_PS1%"
if not "%errorlevel%"=="0" pause
exit /b

:indirilemedi
echo.
echo Betik indirilemedi. Internet baglantinizi kontrol edip tekrar deneyin.
pause
