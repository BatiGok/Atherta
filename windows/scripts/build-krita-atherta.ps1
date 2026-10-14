# SPDX-FileCopyrightText: 2026 Atherta
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds Krita 5.2 with the Atherta 3D Paint plugin on Windows.
#
# It follows Krita's own CI (build-tools/ci-scripts/windows.yml):
#   1. clone Krita 5.2 and KDE's krita-deps-management / krita-ci-utilities
#   2. download KDE's prebuilt Windows dependencies (Qt, KDE Frameworks, ...)
#   3. pick the llvm-mingw (LLVM 18) toolchain matching those dependencies
#   4. build and install Krita with the plugin injected through
#      -DCMAKE_PROJECT_krita_INCLUDE (Krita's sources stay untouched)
#   5. make a portable Krita folder with build-windows-package.py
#
# Every step is skipped when it already finished, so the script can simply
# be run again after a failure. A log is written next to the .bat files.

[CmdletBinding()]
param(
    # Must be a short path without spaces (Krita's build needs that).
    [string]$BuildRoot = 'C:\KritaBuild',
    [string]$KritaBranch = 'krita/5.2',
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'   # makes Invoke-WebRequest much faster
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$PackageDir = Split-Path -Parent $ScriptDir          # the KritaFiles folder
$LogFile = Join-Path $PackageDir 'derleme-kaydi.txt'

# llvm-mingw 20240619 = LLVM 18.1.8, the compiler generation Krita 5.2 uses
# on Windows (CI image "krita-windows-clang18").
$LlvmMingwVersion = '20240619'

function Step([string]$Message) {
    Write-Host ''
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Fail([string]$Message) {
    Write-Host ''
    Write-Host "HATA: $Message" -ForegroundColor Red
    Write-Host "Ayrintilar: $LogFile" -ForegroundColor Red
    throw $Message
}

function Invoke-Checked {
    param([string]$Exe, [string[]]$Arguments, [string]$What)
    Write-Host "   > $Exe $($Arguments -join ' ')" -ForegroundColor DarkGray
    # Route the program's output (stdout and stderr) through PowerShell so it
    # lands in the transcript log; stderr lines must not abort the script.
    $ErrorActionPreference = 'Continue'
    & $Exe @Arguments 2>&1 | ForEach-Object { Write-Host "$_" }
    $code = $LASTEXITCODE
    if ($code -ne 0) {
        Fail "$What basarisiz oldu (cikis kodu $code)."
    }
}

function Refresh-Path {
    # Tools installed by winget only appear in new processes; read the
    # registry so this window sees them too.
    $machine = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $user = [Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = "$machine;$user"
    $links = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Links'
    if (Test-Path $links) { $env:Path = "$links;$env:Path" }
    foreach ($dir in @("$env:ProgramFiles\Git\cmd", "$env:ProgramFiles\CMake\bin", "$env:ProgramFiles\7-Zip")) {
        if (Test-Path $dir) { $env:Path = "$dir;$env:Path" }
    }
}

function Require-Tool([string]$Name) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        Fail "'$Name' bulunamadi. Once 1-Gereksinimleri-Kur.bat dosyasini calistirin, sonra yeni bir pencerede tekrar deneyin."
    }
}

function Find-Python {
    # Probing writes to stderr when a version is missing; that must not stop
    # the script. Results are returned with a leading comma so PowerShell
    # does not unroll one-element arrays into a plain string.
    $ErrorActionPreference = 'Continue'
    # Prefer the py launcher with a known-good version.
    if (Get-Command py -ErrorAction SilentlyContinue) {
        foreach ($v in @('3.12', '3.13', '3.11', '3.10')) {
            & py "-$v" -c "import sys" 2>$null | Out-Null
            if ($LASTEXITCODE -eq 0) { return , @('py', "-$v") }
        }
    }
    if (Get-Command python -ErrorAction SilentlyContinue) {
        $ver = & python -c "import sys; print('%d.%d' % sys.version_info[:2])" 2>$null
        if ($LASTEXITCODE -eq 0 -and "$ver" -match '^3\.(1[0-3])$') { return , @('python') }
    }
    Fail 'Python 3.10 - 3.13 bulunamadi. 1-Gereksinimleri-Kur.bat ile Python 3.12 kurun.'
}

function Get-CrtFlavor([string]$Dll) {
    # The prebuilt dependencies decide which C runtime the toolchain must use.
    $text = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($Dll))
    if ($text -match 'api-ms-win-crt-') { return 'ucrt' }
    if ($text -match '(?i)msvcrt\.dll') { return 'msvcrt' }
    Fail "C calisma zamani tespit edilemedi: $Dll"
}

Start-Transcript -Path $LogFile -Append | Out-Null
try {
    $started = Get-Date
    Write-Host '============================================================'
    Write-Host ' Atherta 3D Paint - Krita 5.2 derlemesi'
    Write-Host " Calisma klasoru: $BuildRoot"
    Write-Host ' Ilk calistirma internet hizina ve bilgisayara gore 1-3 saat surer.'
    Write-Host '============================================================'

    # --- 0. Checks ---------------------------------------------------------
    Step 'Araclar kontrol ediliyor'
    Refresh-Path
    if ($BuildRoot -match '\s') { Fail "Calisma klasoru bosluk iceremez: $BuildRoot" }
    foreach ($tool in @('git', 'cmake', 'ninja')) { Require-Tool $tool }
    $SevenZip = (Get-Command 7z -ErrorAction SilentlyContinue).Source
    if (-not $SevenZip -and (Test-Path "$env:ProgramFiles\7-Zip\7z.exe")) { $SevenZip = "$env:ProgramFiles\7-Zip\7z.exe" }
    if (-not $SevenZip) { Fail '7-Zip bulunamadi. 1-Gereksinimleri-Kur.bat dosyasini calistirin.' }
    $Python = Find-Python
    # cmake_language(DEFER) in cmake/KritaInject.cmake needs CMake 3.19+.
    $cmakeLine = (& cmake --version | Select-Object -First 1) -as [string]
    if ($cmakeLine -match '(\d+)\.(\d+)') {
        if ([int]$Matches[1] -lt 3 -or ([int]$Matches[1] -eq 3 -and [int]$Matches[2] -lt 19)) {
            Fail "CMake 3.19 veya ustu gerekli (bulunan: $cmakeLine). Guncellemek icin: winget upgrade -e --id Kitware.CMake"
        }
    }
    Write-Host "   git:    $((Get-Command git).Source)"
    Write-Host "   cmake:  $((Get-Command cmake).Source)"
    Write-Host "   ninja:  $((Get-Command ninja).Source)"
    Write-Host "   python: $($Python -join ' ')"
    Write-Host "   7-Zip:  $SevenZip"

    $drive = Split-Path -Qualifier $BuildRoot
    $free = (Get-PSDrive ($drive.TrimEnd(':'))).Free
    if ($free -lt 30GB) {
        Write-Host ("   UYARI: {0} surucusunde {1:N0} GB bos alan var; en az 30 GB onerilir." -f $drive, ($free / 1GB)) -ForegroundColor Yellow
    }

    if ($Clean -and (Test-Path $BuildRoot)) {
        Step "Eski derleme siliniyor: $BuildRoot"
        Remove-Item -Recurse -Force $BuildRoot
    }
    New-Item -ItemType Directory -Force -Path $BuildRoot | Out-Null
    $Krita = Join-Path $BuildRoot 'krita'
    $Atherta = Join-Path $BuildRoot 'Atherta'
    $Tools = Join-Path $BuildRoot 'tools'
    New-Item -ItemType Directory -Force -Path $Tools | Out-Null

    # --- 1. Sources ---------------------------------------------------------
    Step 'Atherta kaynaklari kopyalaniyor'
    $AthertaSrc = Join-Path $PackageDir 'Atherta'
    if (-not (Test-Path (Join-Path $AthertaSrc 'cmake\KritaInject.cmake'))) {
        Fail "Atherta kaynaklari bulunamadi: $AthertaSrc"
    }
    if (Test-Path $Atherta) { Remove-Item -Recurse -Force $Atherta }
    Copy-Item -Recurse -Path $AthertaSrc -Destination $Atherta

    git config --global core.longpaths true | Out-Null

    if (-not (Test-Path (Join-Path $Krita 'CMakeLists.txt'))) {
        Step "Krita kaynak kodu indiriliyor ($KritaBranch)"
        Invoke-Checked git @('clone', '--depth', '1', '--branch', $KritaBranch,
                             'https://invent.kde.org/graphics/krita.git', $Krita) 'Krita indirme'
    } else {
        Step 'Krita kaynak kodu zaten var'
    }
    Set-Location $Krita

    if (-not (Test-Path 'krita-deps-management\tools')) {
        Step 'Krita bagimlilik araclari indiriliyor'
        Invoke-Checked git @('clone', '--depth', '1', '--branch', 'krita/5.2',
                             'https://invent.kde.org/packaging/krita-deps-management.git',
                             'krita-deps-management') 'krita-deps-management indirme'
    }
    if (-not (Test-Path 'krita-deps-management\ci-utilities\run-ci-build.py')) {
        Invoke-Checked git @('clone', '--depth', '1',
                             'https://invent.kde.org/packaging/krita-ci-utilities.git',
                             'krita-deps-management\ci-utilities') 'krita-ci-utilities indirme'
    }

    # --- 2. Python environment ---------------------------------------------
    $VenvPy = Join-Path $Krita 'venv\Scripts\python.exe'
    if (-not (Test-Path $VenvPy)) {
        Step 'Python sanal ortami olusturuluyor'
        $py = $Python[0]
        $pyArgs = @()
        if ($Python.Count -gt 1) { $pyArgs += $Python[1..($Python.Count - 1)] }
        Invoke-Checked $py ($pyArgs + @('-m', 'venv', 'venv', '--upgrade-deps')) 'venv olusturma'
        Invoke-Checked $VenvPy @('-m', 'pip', 'install', '-r', 'krita-deps-management\requirements.txt') 'pip install'
    }
    $env:Path = "$(Join-Path $Krita 'venv\Scripts');$env:Path"

    # --- 3. Dependency list (as on CI) -------------------------------------
    Step 'Bagimlilik listesi hazirlaniyor'
    Invoke-Checked $VenvPy @('-u', 'krita-deps-management\tools\replace-branch-in-seed-file.py',
                             'krita-deps-management\latest\krita-deps.yml', '-p',
                             '-o', 'branch-corrected-deps.yml', '-d', 'master') 'seed dosyasi'
    Invoke-Checked $VenvPy @('-u', 'krita-deps-management\tools\generate-deps-file.py',
                             '-s', 'branch-corrected-deps.yml', '-o', '.kde-ci.yml') '.kde-ci.yml olusturma'
    # Skip the G'MIC plugin build that runs after Krita's install step: it is
    # not needed here and takes long.
    Invoke-Checked $VenvPy @('-c', "import yaml; p='.kde-ci.yml'; c=yaml.safe_load(open(p)); c['PostInstallScripts']={}; yaml.safe_dump(c, open(p,'w'))") '.kde-ci.yml duzenleme'

    # Environment used by KDE's CI scripts (see windows.yml and tools/setup-env.py).
    $env:PYTHONUTF8 = '1'
    $env:KDECI_BUILD_TYPE = 'Release'
    $env:KDECI_CACHE_PATH = Join-Path $BuildRoot 'cache'
    $env:KDECI_GITLAB_SERVER = 'https://invent.kde.org/'
    $env:KDECI_PACKAGE_PROJECT = 'teams/ci-artifacts/krita-windows'
    $env:KDECI_REPO_METADATA_PATH = Join-Path $Krita 'krita-deps-management\repo-metadata'
    $env:KDECI_COMPRESS_PACKAGES_ON_DOWNLOAD = 'False'
    $env:KDECI_ONLY_BUILD = 'True'
    $env:CI_COMMIT_REF_PROTECTED = 'true'
    # Lets CMake 4 configure older dependency config files.
    $env:CMAKE_POLICY_VERSION_MINIMUM = '3.5'
    New-Item -ItemType Directory -Force -Path $env:KDECI_CACHE_PATH | Out-Null

    $RunCi = 'krita-deps-management\ci-utilities\run-ci-build.py'
    $CiArgs = @('--project', 'krita', '--branch', $KritaBranch, '--platform', 'Windows/Qt5/Shared')

    # --- 4. Prebuilt dependencies ------------------------------------------
    $DepsMarker = Join-Path $Krita '.atherta-deps-ok'
    if (-not (Test-Path $DepsMarker)) {
        Step 'KDE hazir Windows bagimliliklari indiriliyor (birkac GB)'
        Invoke-Checked $VenvPy (@('-u', $RunCi) + $CiArgs + @('--only-deps')) 'bagimlilik indirme'
        Set-Content -Path $DepsMarker -Value (Get-Date)
    } else {
        Step 'Bagimliliklar zaten indirilmis'
    }

    # --- 5. Compiler ----------------------------------------------------------
    $QtCore = Join-Path $Krita '_install\bin\Qt5Core.dll'
    if (-not (Test-Path $QtCore)) { Fail "Bagimliliklar eksik gorunuyor ($QtCore yok). -Clean ile tekrar deneyin." }
    $Crt = Get-CrtFlavor $QtCore
    $LlvmName = "llvm-mingw-$LlvmMingwVersion-$Crt-x86_64"
    $LlvmDir = Join-Path $Tools $LlvmName
    Step "Derleyici: $LlvmName (bagimliliklar $Crt ile derlenmis)"
    if (-not (Test-Path (Join-Path $LlvmDir 'bin\clang++.exe'))) {
        $zip = Join-Path $Tools "$LlvmName.zip"
        $url = "https://github.com/mstorsjo/llvm-mingw/releases/download/$LlvmMingwVersion/$LlvmName.zip"
        Write-Host "   indiriliyor: $url"
        Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing
        Invoke-Checked $SevenZip @('x', $zip, "-o$Tools", '-y', '-bso0', '-bsp0') 'derleyici acma'
        Remove-Item $zip
    }
    $env:Path = "$(Join-Path $LlvmDir 'bin');$env:Path"
    $env:MINGW_BIN_DIR = Join-Path $LlvmDir 'bin'
    Invoke-Checked 'clang++' @('--version') 'derleyici kontrolu'

    # --- 6. Build Krita + plugin ----------------------------------------------
    $Inject = (Join-Path $Atherta 'cmake\KritaInject.cmake') -replace '\\', '/'
    $env:KDECI_EXTRA_CMAKE_ARGS = "-DBUILD_TESTING=OFF -DHIDE_SAFE_ASSERTS=ON -DCMAKE_PROJECT_krita_INCLUDE=$Inject"
    Step 'Krita ve Atherta eklentisi derleniyor (en uzun adim)'
    Invoke-Checked $VenvPy (@('-u', $RunCi) + $CiArgs + @('--skip-publishing', '--skip-dependencies-fetch')) 'Krita derlemesi'

    $Plugin = Join-Path $Krita '_install\lib\kritaplugins\kritaatherta3d.dll'
    if (-not (Test-Path $Plugin)) { Fail "Eklenti olusmadi: $Plugin" }
    Write-Host "   eklenti: $Plugin" -ForegroundColor Green

    # --- 7. Portable package -----------------------------------------------
    Step 'Tasinabilir Krita klasoru olusturuluyor'
    $env:CI_COMMIT_SHORT_SHA = 'atherta'
    $env:KRITACI_SKIP_DEBUG_PACKAGE = 'True'
    Get-ChildItem -Path $Krita -Filter 'krita-x64-*-atherta*' -ErrorAction SilentlyContinue |
        ForEach-Object { Remove-Item -Recurse -Force $_.FullName }
    $ErrorActionPreference = 'Continue'
    & $VenvPy -u 'build-tools\ci-scripts\build-windows-package.py' --skip-debug-package 2>&1 |
        ForEach-Object { Write-Host "$_" }
    $packageExit = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    $Portable = Join-Path $BuildRoot 'Krita-Atherta'
    $pkg = Get-ChildItem -Path $Krita -Directory -Filter 'krita-x64-*-atherta' -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($packageExit -eq 0 -and $pkg -and (Test-Path (Join-Path $pkg.FullName 'bin\krita.exe'))) {
        if (Test-Path $Portable) { Remove-Item -Recurse -Force $Portable }
        Move-Item $pkg.FullName $Portable
        Write-Host "   tasinabilir Krita: $Portable" -ForegroundColor Green
    } else {
        Write-Host '   UYARI: paketleme basarisiz; Krita dogrudan derleme klasorunden calistirilacak.' -ForegroundColor Yellow
    }

    Set-Content -Path (Join-Path $PackageDir 'krita-konumu.txt') -Value @(
        "BuildRoot=$BuildRoot",
        "Portable=$Portable",
        "Install=$(Join-Path $Krita '_install')",
        "Toolchain=$LlvmDir"
    )

    $elapsed = (Get-Date) - $started
    Write-Host ''
    Write-Host ('BITTI ({0:hh\:mm} surdu). Simdi 3-Krita-Calistir.bat dosyasini calistirin.' -f $elapsed) -ForegroundColor Green
}
catch {
    Write-Host ''
    Write-Host "Derleme durdu: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host "Lutfen $LogFile dosyasini paylasin." -ForegroundColor Red
    exit 1
}
finally {
    Stop-Transcript | Out-Null
}
