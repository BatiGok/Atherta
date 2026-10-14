# SPDX-FileCopyrightText: 2026 Atherta
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds Krita 5.2 with the Atherta 3D Paint plugin on Windows.
#
# Nothing is installed on the computer and no administrator rights are
# needed: tools that are missing (Git, Python, CMake, Ninja, 7-Zip) are
# downloaded as portable copies into the work folder and used from there.
# Tools that already exist on the PATH are used as they are.
#
# The build follows Krita's own CI (build-tools/ci-scripts/windows.yml):
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
    # Work folder; must be a short path without spaces (Krita's build needs
    # that). Default: C:\KritaBuild, or C:\Users\Public\KritaBuild when the
    # drive root is not writable.
    [string]$BuildRoot = '',
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

# Portable tools, used only when the tool is not already available.
$PortableGitTag = 'v2.55.0.windows.5'
$PortableGitFile = 'PortableGit-2.55.0.5-64-bit.7z.exe'
$PythonNugetVersion = '3.12.10'
$CMakeVersion = '3.31.12'
$NinjaVersion = '1.12.1'
$SevenZipVersion = '2409'

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

function Get-File([string]$Url, [string]$OutFile, [switch]$Optional) {
    # Returns $true/$false only with -Optional; otherwise nothing (any value a
    # PowerShell function emits becomes part of its caller's result).
    Write-Host "   indiriliyor: $Url"
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        try {
            Invoke-WebRequest -Uri $Url -OutFile $OutFile -UseBasicParsing
            if ((Test-Path $OutFile) -and (Get-Item $OutFile).Length -gt 0) {
                if ($Optional) { return $true }
                return
            }
        } catch {
            Write-Host "   deneme $attempt basarisiz: $($_.Exception.Message)" -ForegroundColor Yellow
        }
        Start-Sleep -Seconds (3 * $attempt)
    }
    if ($Optional) { return $false }
    Fail "Indirilemedi: $Url"
}

function Expand-ZipFile([string]$Zip, [string]$Destination) {
    # .NET's extractor is much faster than Expand-Archive on PowerShell 5.1.
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    if (Test-Path $Destination) { Remove-Item -Recurse -Force $Destination }
    [System.IO.Compression.ZipFile]::ExtractToDirectory($Zip, $Destination)
}

function Add-ToPath([string]$Dir) {
    $env:Path = "$Dir;$env:Path"
}

function Refresh-Path {
    # Pick up tools installed earlier (e.g. by winget) that this window has
    # not seen yet.
    $machine = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $user = [Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = "$machine;$user"
    $links = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Links'
    if (Test-Path $links) { Add-ToPath $links }
    foreach ($dir in @("$env:ProgramFiles\Git\cmd", "$env:ProgramFiles\CMake\bin", "$env:ProgramFiles\7-Zip")) {
        if (Test-Path $dir) { Add-ToPath $dir }
    }
}

function Select-BuildRoot([string]$Requested) {
    $candidates = @()
    if ($Requested) { $candidates += $Requested }
    else { $candidates += @("$env:SystemDrive\KritaBuild", "$env:SystemDrive\Users\Public\KritaBuild") }
    foreach ($dir in $candidates) {
        if ($dir -match '\s') {
            Write-Host "   atlandi (bosluk iceriyor): $dir" -ForegroundColor Yellow
            continue
        }
        try {
            New-Item -ItemType Directory -Force -Path $dir -ErrorAction Stop | Out-Null
            $probe = Join-Path $dir '.yazma-testi'
            Set-Content -Path $probe -Value 'ok' -ErrorAction Stop
            Remove-Item $probe -ErrorAction SilentlyContinue
            return $dir
        } catch {
            Write-Host "   yazilamiyor: $dir" -ForegroundColor Yellow
        }
    }
    Fail 'Yazilabilir, bosluk icermeyen bir calisma klasoru bulunamadi. -BuildRoot ile bir klasor verin (ornek: D:\KritaBuild).'
}

function Ensure-Git([string]$Tools) {
    if (Get-Command git -ErrorAction SilentlyContinue) { return }
    $dir = Join-Path $Tools 'PortableGit'
    if (-not (Test-Path (Join-Path $dir 'cmd\git.exe'))) {
        Step 'Tasinabilir Git indiriliyor'
        $sfx = Join-Path $Tools $PortableGitFile
        Get-File "https://github.com/git-for-windows/git/releases/download/$PortableGitTag/$PortableGitFile" $sfx
        # A 7-Zip self-extractor: -o<dir> -y extracts without questions. It is a
        # GUI program, so wait for it explicitly.
        $p = Start-Process -FilePath $sfx -ArgumentList @("-o$dir", '-y') -Wait -PassThru
        if ($p.ExitCode -ne 0 -or -not (Test-Path (Join-Path $dir 'cmd\git.exe'))) {
            Fail "Tasinabilir Git acilamadi (cikis kodu $($p.ExitCode))."
        }
        Remove-Item $sfx -ErrorAction SilentlyContinue
    }
    Add-ToPath (Join-Path $dir 'cmd')
}

function Find-SystemPython {
    # Probing writes to stderr when a version is missing; that must not stop
    # the script. Results are returned with a leading comma so PowerShell
    # does not unroll one-element arrays into a plain string.
    $ErrorActionPreference = 'Continue'
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
    return $null
}

function Ensure-Python([string]$Tools) {
    $system = Find-SystemPython
    if ($system) { return , $system }
    $dir = Join-Path $Tools 'python'
    $exe = Join-Path $dir 'tools\python.exe'
    if (-not (Test-Path $exe)) {
        Step "Tasinabilir Python $PythonNugetVersion indiriliyor"
        # The python.org NuGet package is a complete Python (with venv and pip)
        # that runs without installation.
        $pkg = Join-Path $Tools "python.$PythonNugetVersion.zip"
        Get-File "https://api.nuget.org/v3-flatcontainer/python/$PythonNugetVersion/python.$PythonNugetVersion.nupkg" $pkg
        Expand-ZipFile $pkg $dir
        Remove-Item $pkg -ErrorAction SilentlyContinue
        if (-not (Test-Path $exe)) { Fail "Tasinabilir Python acilamadi: $exe yok." }
    }
    return , @($exe)
}

function Test-CMakeVersion {
    $ErrorActionPreference = 'Continue'
    $line = (& cmake --version 2>$null | Select-Object -First 1) -as [string]
    if ($line -match '(\d+)\.(\d+)') {
        $major = [int]$Matches[1]; $minor = [int]$Matches[2]
        # cmake_language(DEFER) in cmake/KritaInject.cmake needs CMake 3.19+.
        return ($major -gt 3) -or ($major -eq 3 -and $minor -ge 19)
    }
    return $false
}

function Ensure-CMake([string]$Tools) {
    if ((Get-Command cmake -ErrorAction SilentlyContinue) -and (Test-CMakeVersion)) { return }
    $name = "cmake-$CMakeVersion-windows-x86_64"
    $dir = Join-Path $Tools $name
    if (-not (Test-Path (Join-Path $dir 'bin\cmake.exe'))) {
        Step "Tasinabilir CMake $CMakeVersion indiriliyor"
        $zip = Join-Path $Tools "$name.zip"
        Get-File "https://github.com/Kitware/CMake/releases/download/v$CMakeVersion/$name.zip" $zip
        # The archive contains the $name folder itself.
        $tmp = Join-Path $Tools 'cmake-unpack'
        Expand-ZipFile $zip $tmp
        if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
        Move-Item (Join-Path $tmp $name) $dir
        Remove-Item -Recurse -Force $tmp
        Remove-Item $zip -ErrorAction SilentlyContinue
    }
    Add-ToPath (Join-Path $dir 'bin')
}

function Ensure-Ninja([string]$Tools) {
    if (Get-Command ninja -ErrorAction SilentlyContinue) { return }
    $dir = Join-Path $Tools 'ninja'
    if (-not (Test-Path (Join-Path $dir 'ninja.exe'))) {
        Step "Tasinabilir Ninja $NinjaVersion indiriliyor"
        $zip = Join-Path $Tools 'ninja-win.zip'
        Get-File "https://github.com/ninja-build/ninja/releases/download/v$NinjaVersion/ninja-win.zip" $zip
        Expand-ZipFile $zip $dir
        Remove-Item $zip -ErrorAction SilentlyContinue
    }
    Add-ToPath $dir
}

function Ensure-SevenZip([string]$Tools) {
    # Only the final packaging step needs 7-Zip. When it cannot be found or
    # downloaded, packaging is skipped and Krita runs from the build folder.
    foreach ($candidate in @((Get-Command 7z -ErrorAction SilentlyContinue).Source,
                             "$env:ProgramFiles\7-Zip\7z.exe",
                             "${env:ProgramFiles(x86)}\7-Zip\7z.exe",
                             (Join-Path $Tools '7zip\7za.exe'))) {
        if ($candidate -and (Test-Path $candidate)) {
            Add-ToPath (Split-Path -Parent $candidate)
            return $candidate
        }
    }
    Step '7-Zip (tasinabilir) indiriliyor'
    $dir = Join-Path $Tools '7zip'
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $sevenZr = Join-Path $dir '7zr.exe'
    $extra = Join-Path $dir "7z$SevenZipVersion-extra.7z"
    if ((Get-File 'https://www.7-zip.org/a/7zr.exe' $sevenZr -Optional) -and
        (Get-File "https://www.7-zip.org/a/7z$SevenZipVersion-extra.7z" $extra -Optional)) {
        # 7zr.exe only reads .7z archives; it unpacks the full 7za.exe.
        $ErrorActionPreference = 'Continue'
        & $sevenZr x $extra "-o$dir" -y 2>&1 | Out-Null
        Remove-Item $extra -ErrorAction SilentlyContinue
        $exe = Join-Path $dir '7za.exe'
        if (Test-Path $exe) {
            Add-ToPath $dir
            return $exe
        }
    }
    Write-Host '   UYARI: 7-Zip hazirlanamadi; paketleme atlanacak (derleme icin gerekli degil).' -ForegroundColor Yellow
    return $null
}

function Disable-QuickEdit {
    # In a classic console window a single click starts "QuickEdit" selection,
    # which blocks all console output and with it the whole build until Esc is
    # pressed. Switch it off for this window.
    try {
        Add-Type -Namespace Atherta -Name NativeConsole -MemberDefinition @'
[DllImport("kernel32.dll", SetLastError = true)]
public static extern System.IntPtr GetStdHandle(int nStdHandle);
[DllImport("kernel32.dll", SetLastError = true)]
public static extern bool GetConsoleMode(System.IntPtr hConsoleHandle, out uint lpMode);
[DllImport("kernel32.dll", SetLastError = true)]
public static extern bool SetConsoleMode(System.IntPtr hConsoleHandle, uint dwMode);
'@
        $handle = [Atherta.NativeConsole]::GetStdHandle(-10)   # STD_INPUT_HANDLE
        $mode = [uint32]0
        if ([Atherta.NativeConsole]::GetConsoleMode($handle, [ref]$mode)) {
            # Clear ENABLE_QUICK_EDIT_MODE (0x40), keep ENABLE_EXTENDED_FLAGS (0x80).
            $newMode = [uint32](([int64]$mode -band 0xFFFFFFBFL) -bor 0x80)
            [void][Atherta.NativeConsole]::SetConsoleMode($handle, $newMode)
        }
    } catch {
        # Not a classic console (e.g. Windows Terminal): nothing to do.
    }
}

function Get-CrtFlavor([string]$Dll) {
    # The prebuilt dependencies decide which C runtime the toolchain must use.
    $text = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($Dll))
    if ($text -match 'api-ms-win-crt-') { return 'ucrt' }
    if ($text -match '(?i)msvcrt\.dll') { return 'msvcrt' }
    Fail "C calisma zamani tespit edilemedi: $Dll"
}

Disable-QuickEdit
Start-Transcript -Path $LogFile -Append | Out-Null
try {
    $started = Get-Date
    Write-Host '============================================================'
    Write-Host ' Atherta 3D Paint - Krita 5.2 derlemesi'
    Write-Host ' Bilgisayara hicbir sey kurulmaz; eksik araclar tasinabilir'
    Write-Host ' olarak calisma klasorune indirilir.'
    Write-Host ' Ilk calistirma internet hizina ve bilgisayara gore 1-3 saat surer.'
    Write-Host '============================================================'

    # --- 0. Work folder and tools -------------------------------------------
    Step 'Calisma klasoru ve araclar hazirlaniyor'
    Refresh-Path
    $BuildRoot = Select-BuildRoot $BuildRoot
    Write-Host "   calisma klasoru: $BuildRoot"

    $drive = Split-Path -Qualifier $BuildRoot
    $free = (Get-PSDrive ($drive.TrimEnd(':'))).Free
    if ($free -lt 30GB) {
        Write-Host ("   UYARI: {0} surucusunde {1:N0} GB bos alan var; en az 30 GB onerilir." -f $drive, ($free / 1GB)) -ForegroundColor Yellow
    }

    if ($Clean -and (Test-Path $BuildRoot)) {
        Step "Eski derleme siliniyor: $BuildRoot"
        Remove-Item -Recurse -Force $BuildRoot
        New-Item -ItemType Directory -Force -Path $BuildRoot | Out-Null
    }
    $Krita = Join-Path $BuildRoot 'krita'
    $Atherta = Join-Path $BuildRoot 'Atherta'
    $Tools = Join-Path $BuildRoot 'tools'
    New-Item -ItemType Directory -Force -Path $Tools | Out-Null

    Ensure-Git $Tools
    $Python = Ensure-Python $Tools
    Ensure-CMake $Tools
    Ensure-Ninja $Tools
    $SevenZip = Ensure-SevenZip $Tools
    Write-Host "   git:    $((Get-Command git).Source)"
    Write-Host "   python: $($Python -join ' ')"
    Write-Host "   cmake:  $((Get-Command cmake).Source)"
    Write-Host "   ninja:  $((Get-Command ninja).Source)"
    Write-Host "   7-Zip:  $(if ($SevenZip) { $SevenZip } else { 'yok (paketleme atlanacak)' })"

    # --- 1. Sources ---------------------------------------------------------
    Step 'Atherta kaynaklari kopyalaniyor'
    $AthertaSrc = Join-Path $PackageDir 'Atherta'
    if (-not (Test-Path (Join-Path $AthertaSrc 'cmake\KritaInject.cmake'))) {
        Fail "Atherta kaynaklari bulunamadi: $AthertaSrc"
    }
    if (Test-Path $Atherta) { Remove-Item -Recurse -Force $Atherta }
    Copy-Item -Recurse -Path $AthertaSrc -Destination $Atherta

    if (-not (Test-Path (Join-Path $Krita 'CMakeLists.txt'))) {
        Step "Krita kaynak kodu indiriliyor ($KritaBranch)"
        Invoke-Checked git @('-c', 'core.longpaths=true', 'clone', '--depth', '1', '--branch', $KritaBranch,
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
    Add-ToPath (Join-Path $Krita 'venv\Scripts')

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
        Get-File "https://github.com/mstorsjo/llvm-mingw/releases/download/$LlvmMingwVersion/$LlvmName.zip" $zip
        # The archive contains the $LlvmName folder itself.
        $tmp = Join-Path $Tools 'llvm-unpack'
        Expand-ZipFile $zip $tmp
        if (Test-Path $LlvmDir) { Remove-Item -Recurse -Force $LlvmDir }
        Move-Item (Join-Path $tmp $LlvmName) $LlvmDir
        Remove-Item -Recurse -Force $tmp
        Remove-Item $zip -ErrorAction SilentlyContinue
    }
    Add-ToPath (Join-Path $LlvmDir 'bin')
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
    $Portable = Join-Path $BuildRoot 'Krita-Atherta'
    if ($SevenZip) {
        Step 'Tasinabilir Krita klasoru olusturuluyor'
        $env:CI_COMMIT_SHORT_SHA = 'atherta'
        $env:KRITACI_SKIP_DEBUG_PACKAGE = 'True'
        $env:SEVENZIP_EXE = $SevenZip
        Get-ChildItem -Path $Krita -Filter 'krita-x64-*-atherta*' -ErrorAction SilentlyContinue |
            ForEach-Object { Remove-Item -Recurse -Force $_.FullName }
        $ErrorActionPreference = 'Continue'
        & $VenvPy -u 'build-tools\ci-scripts\build-windows-package.py' --skip-debug-package 2>&1 |
            ForEach-Object { Write-Host "$_" }
        $packageExit = $LASTEXITCODE
        $ErrorActionPreference = 'Stop'
        $pkg = Get-ChildItem -Path $Krita -Directory -Filter 'krita-x64-*-atherta' -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($packageExit -eq 0 -and $pkg -and (Test-Path (Join-Path $pkg.FullName 'bin\krita.exe'))) {
            if (Test-Path $Portable) { Remove-Item -Recurse -Force $Portable }
            Move-Item $pkg.FullName $Portable
            Write-Host "   tasinabilir Krita: $Portable" -ForegroundColor Green
        } else {
            Write-Host '   UYARI: paketleme basarisiz; Krita dogrudan derleme klasorunden calistirilacak.' -ForegroundColor Yellow
        }
    } else {
        Write-Host '   7-Zip olmadigi icin paketleme atlandi; Krita derleme klasorunden calistirilacak.' -ForegroundColor Yellow
    }

    Set-Content -Path (Join-Path $PackageDir 'krita-konumu.txt') -Value @(
        "BuildRoot=$BuildRoot",
        "Portable=$Portable",
        "Install=$(Join-Path $Krita '_install')",
        "Toolchain=$LlvmDir"
    )

    $elapsed = (Get-Date) - $started
    Write-Host ''
    Write-Host ('BITTI ({0:hh\:mm} surdu). Simdi 2-Krita-Calistir.bat dosyasini calistirin.' -f $elapsed) -ForegroundColor Green
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
