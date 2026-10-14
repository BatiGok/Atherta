# SPDX-FileCopyrightText: 2026 Atherta
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Starts the Krita build made by build-krita-atherta.ps1: the portable
# package if it exists, otherwise Krita straight from the install folder.

$ErrorActionPreference = 'Stop'
$PackageDir = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$InfoFile = Join-Path $PackageDir 'krita-konumu.txt'

if (-not (Test-Path $InfoFile)) {
    Write-Host 'Krita henuz derlenmemis. Once 1-Krita-ve-Eklentiyi-Derle.bat dosyasini calistirin.' -ForegroundColor Red
    exit 1
}

$info = @{}
Get-Content $InfoFile | ForEach-Object {
    $k, $v = $_ -split '=', 2
    if ($k) { $info[$k] = $v }
}

$portableExe = Join-Path $info['Portable'] 'bin\krita.exe'
if (Test-Path $portableExe) {
    Start-Process -FilePath $portableExe -WorkingDirectory (Split-Path $portableExe)
    exit 0
}

# Fallback: run from the install folder with the toolchain's runtime DLLs
# (libc++, libunwind, ...) on PATH.
$installExe = Join-Path $info['Install'] 'bin\krita.exe'
if (-not (Test-Path $installExe)) {
    Write-Host "krita.exe bulunamadi: $installExe" -ForegroundColor Red
    exit 1
}
$env:Path = "$(Join-Path $info['Install'] 'bin');$(Join-Path $info['Toolchain'] 'x86_64-w64-mingw32\bin');$(Join-Path $info['Toolchain'] 'bin');$env:Path"
Start-Process -FilePath $installExe -WorkingDirectory (Split-Path $installExe)
