#Requires -Version 5.1
param(
    [string]$ServerHost,
    [int]$ServerPort = 0,
    # Skip building the InnoSetup installer after the build.
    [switch]$NoInstaller,
    # Release is the default: Debug builds run 5-15x slower (no optimization,
    # MSVC debug iterators, assertions) and feel laggy when opening chats etc.
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = (Resolve-Path (Join-Path $ScriptDir '..')).Path
$TelegramDir = Join-Path $RepoRoot 'Telegram'
$DcOptionsFile = Join-Path $RepoRoot 'Telegram\SourceFiles\mtproto\mtproto_dc_options.cpp'
$OfficialServerFile = Join-Path $RepoRoot 'Telegram\SourceFiles\owpengram\owpengram_servers.cpp'
$SolutionPath = Join-Path $RepoRoot 'out\Telegram.sln'
$LibrariesRoot = (Resolve-Path (Join-Path $RepoRoot '..')).Path
$LibrariesMarker = Join-Path $LibrariesRoot 'Libraries\win64\local'

$TestApiId = '17349'
$TestApiHash = '344583e45741c457fe1862106095a5eb'

$script:VcVars = $null

function Get-ApiCredentials {
    $candidates = @(
        (Join-Path $RepoRoot 'api_credentials.local.ps1'),
        (Join-Path (Split-Path $RepoRoot -Parent) 'api_credentials.local.ps1')
    )
    foreach ($file in $candidates) {
        if (-not (Test-Path $file)) { continue }
        . $file
        if ([string]::IsNullOrWhiteSpace($TDESKTOP_API_ID) -or [string]::IsNullOrWhiteSpace($TDESKTOP_API_HASH)) {
            throw "$file must set `$TDESKTOP_API_ID and `$TDESKTOP_API_HASH"
        }
        return @{
            Id = $TDESKTOP_API_ID.Trim()
            Hash = $TDESKTOP_API_HASH.Trim()
            Source = (Split-Path $file -Leaf)
        }
    }

    Write-Host '[WARN] api_credentials.local.ps1 not found — using TEST credentials.' -ForegroundColor Yellow
    Write-Host '       Copy api_credentials.local.ps1.example and add your api_id/api_hash for Telegram login.' -ForegroundColor Yellow
    return @{
        Id = $TestApiId
        Hash = $TestApiHash
        Source = 'test defaults'
    }
}

function Write-Step([string]$Text) { Write-Host "`n>> $Text" -ForegroundColor Yellow }
function Write-Ok([string]$Text)   { Write-Host "[OK] $Text" -ForegroundColor Green }
function Write-Err([string]$Text)   { Write-Host "[X] $Text" -ForegroundColor Red }

function Read-Line([string]$Prompt, [string]$Default) {
    $suffix = if ([string]::IsNullOrWhiteSpace($Default)) { '' } else { " [$Default]" }
    $value = Read-Host "$Prompt$suffix"
    if ([string]::IsNullOrWhiteSpace($value)) { return $Default }
    return $value.Trim()
}

function Patch-Server([string]$Address, [int]$Port) {
    # The binary is now server-agnostic: kBuiltInDcs keeps the real Telegram data
    # centers and self-hosted servers are chosen at runtime (no rebuild needed to
    # switch). This step only updates the default address of the built-in
    # "owpengram" profile (kOfficialDefaultHost/Port) as a convenience.
    $ip = $Address
    $portNum = $Port

    if (-not (Test-Path $OfficialServerFile)) {
        throw "File not found: $OfficialServerFile"
    }
    $officialContent = Get-Content -Path $OfficialServerFile -Raw -Encoding UTF8
    $hostPattern = '(const auto kOfficialDefaultHost = u")([^"]*)("_q;)'
    $portPattern = '(constexpr auto kOfficialDefaultPort = )(\d+)(;)'
    if (-not ([regex]::IsMatch($officialContent, $hostPattern) -and [regex]::IsMatch($officialContent, $portPattern))) {
        throw 'kOfficialDefaultHost/kOfficialDefaultPort not found in owpengram_servers.cpp'
    }
    $officialContent = [regex]::Replace($officialContent, $hostPattern, { param($m) $m.Groups[1].Value + $ip + $m.Groups[3].Value })
    $officialContent = [regex]::Replace($officialContent, $portPattern, "`${1}$portNum`${3}")
    [System.IO.File]::WriteAllText($OfficialServerFile, $officialContent, (New-Object System.Text.UTF8Encoding $false))

    Write-Ok "owpengram default profile set: ${ip}:$portNum (built-in DCs unchanged)"
}

function Find-VcVars {
    if ($env:OWPNG_VCVARS64 -and (Test-Path $env:OWPNG_VCVARS64)) {
        return (Resolve-Path $env:OWPNG_VCVARS64).Path
    }

    $roots = @(
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio'),
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio')
    )
    $found = @()
    foreach ($root in $roots) {
        if (-not (Test-Path $root)) { continue }
        Get-ChildItem -Path $root -Filter 'vcvars64.bat' -Recurse -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\VC\\Auxiliary\\Build\\vcvars64\.bat$' } |
            ForEach-Object { $found += $_.FullName }
    }

    if ($found.Count -eq 0) { return $null }

    $preferred = $found | Where-Object { $_ -match '\\2022\\' } | Select-Object -First 1
    if ($preferred) { return $preferred }
    return ($found | Sort-Object -Descending | Select-Object -First 1)
}

function Invoke-Vs([string]$Command, [string]$WorkingDirectory, [string]$Label) {
    if (-not $script:VcVars) { throw 'vcvars64 not resolved' }

    $logDir = Join-Path $RepoRoot 'logs'
    New-Item -ItemType Directory -Force -Path $logDir | Out-Null
    $logFile = Join-Path $logDir ("{0}-{1}.log" -f $Label, (Get-Date -Format 'yyyyMMdd-HHmmss'))

    $escapedWd = $WorkingDirectory.Replace('"', '""')
    $escapedLog = $logFile.Replace('"', '""')
    $full = "call `"$($script:VcVars)`" >nul 2>&1 && cd /d `"$escapedWd`" && $Command >> `"$escapedLog`" 2>&1"

    Write-Host "cmd: $Command" -ForegroundColor DarkGray
    Write-Host "log: $logFile" -ForegroundColor DarkGray

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = 'cmd.exe'
    $psi.Arguments = "/c `"$full`""
    $psi.WorkingDirectory = $WorkingDirectory
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $psi
    $null = $process.Start()

    $offset = 0L
    $lastBeat = [datetime]::UtcNow
    while (-not $process.HasExited) {
        if (Test-Path $logFile) {
            $stream = [System.IO.File]::Open($logFile, 'Open', 'Read', 'ReadWrite')
            try {
                $stream.Seek($offset, 'Begin') | Out-Null
                $reader = New-Object System.IO.StreamReader($stream, [System.Text.Encoding]::UTF8)
                while (-not $reader.EndOfStream) {
                    $line = $reader.ReadLine()
                    if (-not [string]::IsNullOrWhiteSpace($line)) { Write-Host $line }
                }
                $offset = $stream.Position
            }
            finally { $stream.Dispose() }
        }
        if (([datetime]::UtcNow - $lastBeat).TotalSeconds -ge 30) {
            $lastBeat = [datetime]::UtcNow
            Write-Host "[$(Get-Date -Format 'HH:mm:ss')] $Label - still running..." -ForegroundColor Cyan
        }
        Start-Sleep -Milliseconds 500
    }

    if ($process.ExitCode -ne 0) {
        throw "Failed: $Command (exit $($process.ExitCode)). Log: $logFile"
    }
    Write-Ok "Finished: $Label"
}

try {
    Write-Host ''
    Write-Host 'OwpenGram Desktop build' -ForegroundColor Cyan
    Write-Host "Repo: $RepoRoot"
    Write-Host 'Tip: run build-desktop.cmd from cmd or PowerShell (not Git Bash).' -ForegroundColor DarkGray
    Write-Host ''

    # Choose build type at startup (unless passed explicitly via -Configuration).
    # Release is recommended for everyday use; Debug is much slower but has
    # verbose logging/assertions for hunting bugs.
    if (-not $PSBoundParameters.ContainsKey('Configuration')) {
        # Use a plain variable for the loop: assigning $null/'' to $Configuration
        # would trip its [ValidateSet] attribute (it re-validates on assignment).
        $chosen = $null
        do {
            $answer = (Read-Line 'Build type: [R]elease or [D]ebug' 'R').Trim().ToLower()
            if ($answer -eq 'r' -or $answer -eq 'release') {
                $chosen = 'Release'
            } elseif ($answer -eq 'd' -or $answer -eq 'debug') {
                $chosen = 'Debug'
            } else {
                Write-Host '  Please enter R or D.' -ForegroundColor Yellow
            }
        } while (-not $chosen)
        $Configuration = $chosen
    }
    Write-Ok "Build type: $Configuration"

    # Servers are now chosen at runtime, so no address/port input is needed.
    # Patching the owpengram default profile is optional: it only runs when both
    # -ServerHost and -ServerPort are passed explicitly.
    if (-not [string]::IsNullOrWhiteSpace($ServerHost) -and $ServerPort -gt 0) {
        Write-Step "Set owpengram default profile -> ${ServerHost}:$ServerPort (optional)"
        Patch-Server -Address $ServerHost -Port $ServerPort
    } else {
        Write-Ok 'Server is selected at runtime; skipping default-profile patch.'
    }

    Write-Step 'Check tools'
    foreach ($tool in @('git', 'python')) {
        if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
            throw "Missing in PATH: $tool"
        }
    }
    $script:VcVars = Find-VcVars
    if (-not $script:VcVars) {
        throw 'vcvars64.bat not found. Install VS 2022 with C++ build tools, or set OWPNG_VCVARS64.'
    }
    Write-Ok "VS: $script:VcVars"

    Write-Step 'Git submodules'
    Push-Location $RepoRoot
    try {
        $env:GIT_TERMINAL_PROMPT = '0'
        & git -c advice.detachedHead=false submodule update --init --recursive --quiet
        if ($LASTEXITCODE -ne 0) { throw "git submodule failed ($LASTEXITCODE)" }
    }
    finally { Pop-Location }
    Write-Ok 'Submodules OK'

    if (-not (Test-Path $LibrariesMarker)) {
        Write-Step 'Prepare libraries (first time, long)'
        # qt6: v6.9.x (layer 227) dropped Qt 5.15 support — lib_ui uses Qt 6.8+
        # accessibility APIs unconditionally, so x64 must build against Qt 6.
        # qt_version.py only picks Qt 6 when 'qt6' is literally in argv.
        Invoke-Vs -Command 'Telegram\build\prepare\win.bat silent qt6' -WorkingDirectory $RepoRoot -Label 'prepare'
    }
    else {
        Write-Ok 'Libraries present, skip prepare'
    }

    Write-Step 'CMake configure'
    $api = Get-ApiCredentials
    Write-Ok "API credentials: $($api.Source) (id $($api.Id))"
    # Embedded debug info (/Z7) instead of a shared .pdb: avoids the mspdbsrv
    # "C1090 PDB API call failed" / "C2471 cannot update program database" crashes
    # on this toolchain. -D overrides cmake_helpers' non-FORCE cache default.
    # qt6 arg (see prepare step above) + .\ prefix so configure.bat resolves even
    # when CWD isn't on the executable search path in this invocation context.
    $configure = ".\configure.bat x64 qt6 -D TDESKTOP_API_ID=$($api.Id) -D TDESKTOP_API_HASH=$($api.Hash) -D CMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded -D DESKTOP_APP_ENABLE_LTO=ON"
    Invoke-Vs -Command $configure -WorkingDirectory $TelegramDir -Label 'configure'

    Write-Step "MSBuild $Configuration"
    $build = "msbuild `"$SolutionPath`" /t:Telegram /p:Configuration=$Configuration /m /nr:false /v:minimal"
    Invoke-Vs -Command $build -WorkingDirectory $RepoRoot -Label 'build'

    foreach ($name in @('ilyshaGram.exe', 'OwpenGram.exe', 'Telegram.exe')) {
        $exe = Join-Path $RepoRoot "out\$Configuration\$name"
        if (Test-Path $exe) {
            Write-Ok "EXE: $exe"

            if (-not $NoInstaller) {
                Write-Step 'Build installer (InnoSetup)'
                $iss = Join-Path $RepoRoot 'installer\ilyshagram_setup.iss'
                $iscc = $null
                $candidates = @(
                    Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'
                    Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe'
                    Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'
                )
                foreach ($cand in $candidates) {
                    if ($cand -and (Test-Path $cand)) {
                        $iscc = $cand
                        break
                    }
                }
                if (-not $iscc) {
                    Write-Err "ISCC not found (searched AppData, Program Files) — skipping installer. Install Inno Setup 6 or pass -NoInstaller."
                } elseif (-not (Test-Path $iss)) {
                    Write-Err "Installer script not found: $iss — skipping installer."
                } else {
                    $psi = New-Object System.Diagnostics.ProcessStartInfo
                    $psi.FileName = $iscc
                    $psi.Arguments = "`"$iss`""
                    $psi.WorkingDirectory = Split-Path $iss -Parent
                    $psi.UseShellExecute = $false
                    $p = New-Object System.Diagnostics.Process
                    $p.StartInfo = $psi
                    $null = $p.Start()
                    $p.WaitForExit()
                    if ($p.ExitCode -ne 0) {
                        Write-Err "InnoSetup failed (exit $($p.ExitCode)). Installer not produced."
                    } else {
                        $setup = Join-Path $RepoRoot 'installer\out\ilyshagram-setup.exe'
                        Write-Ok "Installer: $setup"
                    }
                }
            }

            exit 0
        }
    }
    throw "Build finished but EXE not found in out\$Configuration"
}
catch {
    Write-Err $_.Exception.Message
    exit 1
}
