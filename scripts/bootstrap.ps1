# Checks Windows build prerequisites. Reports what is missing; never installs or changes anything.
$ErrorActionPreference = 'Stop'
$missing = @()

$cmake = Get-Command cmake -ErrorAction SilentlyContinue
$cmakeVersion = $null
if (-not $cmake) {
    $missing += 'CMake 3.28 or newer on PATH (https://cmake.org/download/)'
} else {
    $cmakeVersion = [version]((cmake --version | Select-Object -First 1) -replace '[^0-9.]', '')
    if ($cmakeVersion -lt [version]'3.28') { $missing += "CMake 3.28 or newer (found $cmakeVersion)" }
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = $null
$msbuild = $null
if (Test-Path $vswhere) {
    $vsJson = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json
    if ($LASTEXITCODE -eq 0 -and $vsJson) {
        $vs = ($vsJson | ConvertFrom-Json | Select-Object -First 1)
    }
    $msbuild = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'MSBuild\**\Bin\MSBuild.exe' |
        Select-Object -First 1
}
if (-not $vs -or -not $msbuild) {
    $missing += 'Visual Studio 2022 or newer with the "Desktop development with C++" workload (MSVC x64 tools)'
}

$sdkVersion = $null
try {
    $kitsRoot = (Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots' -ErrorAction Stop).KitsRoot10
    if ($kitsRoot) {
        $sdkVersion = Get-ChildItem (Join-Path $kitsRoot 'Include') -Directory -ErrorAction Stop |
            ForEach-Object {
                try { [version]$_.Name } catch { $null }
            } |
            Where-Object { $_ -and $_ -ge [version]'10.0.22621.0' } |
            Sort-Object -Descending |
            Select-Object -First 1
    }
} catch {
    $sdkVersion = $null
}
if (-not $sdkVersion) { $missing += 'Windows 11 SDK (10.0.22621.0 or newer)' }

if ($missing.Count -gt 0) {
    Write-Host 'Missing prerequisites:'
    $missing | ForEach-Object { Write-Host "  - $_" }
    Write-Host 'Install them yourself; these scripts never install tools or change system settings.'
    exit 1
}

Write-Host "[catro] CMake: $cmakeVersion"
Write-Host "[catro] Visual Studio: $($vs.displayName) $($vs.installationVersion)"
Write-Host "[catro] MSBuild: $msbuild"
Write-Host "[catro] Windows SDK: $sdkVersion"
Write-Host 'Prerequisites present. NuGet packages for the shell are restored into apps/windows/packages by build.ps1.'
