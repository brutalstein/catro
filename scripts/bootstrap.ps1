# Checks Windows build prerequisites. Reports what is missing; never installs or changes anything.
$ErrorActionPreference = 'Stop'
$missing = @()

$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmake) {
    $missing += 'CMake 3.28 or newer on PATH (https://cmake.org/download/)'
} else {
    $version = [version]((cmake --version | Select-Object -First 1) -replace '[^0-9.]', '')
    if ($version -lt [version]'3.28') { $missing += "CMake 3.28 or newer (found $version)" }
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuild = $null
if (Test-Path $vswhere) {
    $msbuild = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'MSBuild\**\Bin\MSBuild.exe' |
        Select-Object -First 1
}
if (-not $msbuild) {
    $missing += 'Visual Studio 2022 or newer with the "Desktop development with C++" workload (MSVC x64 tools)'
}

$sdk = Get-ChildItem 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots' -ErrorAction SilentlyContinue |
    Where-Object { $_.PSChildName -like '10.0.2*' }
if (-not $sdk) { $missing += 'Windows 11 SDK (10.0.22621 or newer)' }

if ($missing.Count -gt 0) {
    Write-Host 'Missing prerequisites:'
    $missing | ForEach-Object { Write-Host "  - $_" }
    Write-Host 'Install them yourself; these scripts never install tools or change system settings.'
    exit 1
}
Write-Host "CMake $version, MSBuild $msbuild"
Write-Host 'Prerequisites present. NuGet packages for the shell are restored into apps/windows/packages by build.ps1.'
