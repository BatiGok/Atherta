# SPDX-FileCopyrightText: 2026 Atherta
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Entry point for the single copy-paste .bat (windows/Atherta-Krita.bat).
#
# The .bat downloads this script from GitHub and runs it. It:
#   - keeps the log and notes in Desktop\KritaFiles
#   - downloads the latest Atherta sources (branch zip, no Git needed)
#   - runs scripts\build-krita-atherta.ps1 from those sources
#   - starts the built Krita
# Once Krita is built, running the .bat again offers to start it directly.
#
# Nothing is installed on the computer; see build-krita-atherta.ps1.

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'   # makes Invoke-WebRequest much faster
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$Repo = 'BatiGok/Atherta'
$Branch = 'claude/nice-babbage-lgnynz'
$Desktop = [Environment]::GetFolderPath('Desktop')   # follows OneDrive redirection
if (-not $Desktop) { $Desktop = Join-Path $env:USERPROFILE 'Desktop' }
$Data = Join-Path $Desktop 'KritaFiles'
$InfoFile = Join-Path $Data 'krita-konumu.txt'
$LogFile = Join-Path $Data 'derleme-kaydi.txt'

function Start-BuiltKrita {
    # Returns $true when Krita was started.
    if (-not (Test-Path $InfoFile)) { return $false }
    $info = @{}
    foreach ($line in Get-Content $InfoFile) {
        $k, $v = $line -split '=', 2
        if ($k) { $info[$k] = $v }
    }
    if ($info['Portable']) {
        $exe = Join-Path $info['Portable'] 'bin\krita.exe'
        if (Test-Path $exe) {
            Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe)
            return $true
        }
    }
    if ($info['Install'] -and $info['Toolchain']) {
        $exe = Join-Path $info['Install'] 'bin\krita.exe'
        if (Test-Path $exe) {
            # Not packaged: the toolchain's runtime DLLs (libc++, libunwind, ...)
            # must be found on PATH.
            $env:Path = "$(Join-Path $info['Install'] 'bin');$(Join-Path $info['Toolchain'] 'x86_64-w64-mingw32\bin');$(Join-Path $info['Toolchain'] 'bin');$env:Path"
            Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe)
            return $true
        }
    }
    return $false
}

function Get-AthertaSources {
    # Downloads the branch as a zip and returns the folder that contains
    # cmake\KritaInject.cmake.
    $work = Join-Path $env:TEMP 'atherta-kaynak'
    if (Test-Path $work) { Remove-Item -Recurse -Force $work }
    New-Item -ItemType Directory -Force -Path $work | Out-Null
    $zip = Join-Path $work 'atherta.zip'
    $url = "https://github.com/$Repo/archive/refs/heads/$Branch.zip"
    Write-Host "   indiriliyor: $url"
    $done = $false
    for ($attempt = 1; $attempt -le 3 -and -not $done; $attempt++) {
        try {
            Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing
            $done = (Test-Path $zip) -and (Get-Item $zip).Length -gt 0
        } catch {
            Write-Host "   deneme $attempt basarisiz: $($_.Exception.Message)" -ForegroundColor Yellow
            Start-Sleep -Seconds (3 * $attempt)
        }
    }
    if (-not $done) { throw "Kaynak kod indirilemedi: $url" }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $unpacked = Join-Path $work 'src'
    [System.IO.Compression.ZipFile]::ExtractToDirectory($zip, $unpacked)
    $root = Get-ChildItem -Path $unpacked -Directory |
        Where-Object { Test-Path (Join-Path $_.FullName 'cmake\KritaInject.cmake') } |
        Select-Object -First 1
    if (-not $root) { throw 'Indirilen kaynak kodda cmake\KritaInject.cmake bulunamadi.' }
    return $root.FullName
}

try {
    New-Item -ItemType Directory -Force -Path $Data | Out-Null

    Write-Host ''
    Write-Host '============================================================'
    Write-Host ' Atherta 3D Paint - Krita 5.2'
    Write-Host " Kayitlar: $Data"
    Write-Host '============================================================'
    Write-Host ''

    $clean = $false
    if (Test-Path $InfoFile) {
        Write-Host ' Krita daha once derlenmis.'
        Write-Host ''
        Write-Host '   Enter      Krita''yi ac'
        Write-Host '   G + Enter  Eklentinin en son halini indir ve yeniden derle'
        Write-Host '              (yalnizca degisen dosyalar derlenir)'
        Write-Host '   T + Enter  Her seyi silip bastan derle (1-3 saat)'
        Write-Host ''
        $answer = "$(Read-Host ' Seciminiz')".Trim()
        if ($answer -match '^[tT]') {
            $clean = $true
        } elseif ($answer -notmatch '^[gG]') {
            if (Start-BuiltKrita) {
                Write-Host ''
                Write-Host ' Krita aciliyor. Bu pencere birazdan kapanacak.' -ForegroundColor Green
                Start-Sleep -Seconds 3
                exit 0
            }
            Write-Host ''
            Write-Host ' Derlenmis Krita bulunamadi (calisma klasoru silinmis olabilir).' -ForegroundColor Yellow
            Write-Host ' Devam edilirse yeniden derlenecek.' -ForegroundColor Yellow
            [void](Read-Host ' Devam etmek icin Enter, vazgecmek icin pencereyi kapatin')
        }
    } else {
        Write-Host ' Krita, Atherta 3D Paint eklentisiyle birlikte derlenecek.'
        Write-Host ' - Bilgisayara hicbir sey kurulmaz, yonetici izni gerekmez.'
        Write-Host ' - Calisma klasoru: C:\KritaBuild (diskte en az 30 GB bos alan).'
        Write-Host ' - Ilk derleme internet hizina ve bilgisayara gore 1-3 saat surer.'
        Write-Host ' - Yarida kalirsa bu dosyayi tekrar calistirin; kaldigi yerden devam eder.'
        Write-Host ' - Derleme surerken pencerenin ICINE tiklamayin.'
        Write-Host ''
        Write-Host '   Enter      Basla'
        Write-Host '   T + Enter  Onceki yarim derlemeyi silip bastan basla'
        Write-Host ''
        $answer = "$(Read-Host ' Seciminiz')".Trim()
        if ($answer -match '^[tT]') { $clean = $true }
    }

    Write-Host ''
    Write-Host '==> Atherta kaynak kodu indiriliyor' -ForegroundColor Cyan
    $source = Get-AthertaSources
    Write-Host "   kaynak: $source"

    $samples = Join-Path $source 'windows\ornek-modeller'
    if (Test-Path $samples) {
        $sampleDir = Join-Path $Data 'ornek-modeller'
        New-Item -ItemType Directory -Force -Path $sampleDir | Out-Null
        Copy-Item -Force -Path (Join-Path $samples '*') -Destination $sampleDir
    }

    $buildScript = Join-Path $source 'windows\scripts\build-krita-atherta.ps1'
    $buildArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $buildScript,
                   '-AthertaSource', $source, '-LogDir', $Data)
    if ($clean) { $buildArgs += '-Clean' }
    & (Join-Path $PSHOME 'powershell.exe') @buildArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Host ''
        Write-Host ' Derleme tamamlanamadi.' -ForegroundColor Red
        Write-Host " Lutfen su dosyayi paylasin: $LogFile" -ForegroundColor Red
        exit 1
    }

    Write-Host ''
    if (Start-BuiltKrita) {
        Write-Host ' Krita aciliyor.' -ForegroundColor Green
        Write-Host ' Krita''da: Settings > Dockers > 3D Paint' -ForegroundColor Green
        Write-Host " Ornek model: $(Join-Path $Data 'ornek-modeller\kure.obj')" -ForegroundColor Green
        Write-Host ' Sonraki seferlerde bu .bat dosyasini calistirip Enter''a basmaniz yeterli.' -ForegroundColor Green
        Write-Host ''
        [void](Read-Host ' Bu pencereyi kapatmak icin Enter')
        exit 0
    }
    Write-Host ' Derleme bitti ama krita.exe bulunamadi.' -ForegroundColor Red
    Write-Host " Lutfen su dosyayi paylasin: $LogFile" -ForegroundColor Red
    exit 1
}
catch {
    Write-Host ''
    Write-Host "HATA: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host ' Bu pencerenin ekran goruntusunu ya da derleme-kaydi.txt dosyasini paylasin.' -ForegroundColor Red
    exit 1
}
