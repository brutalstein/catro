# Configures and builds the core, tools, tests, and the WinUI shell. Writes only under out/ and
# apps/windows/packages (NuGet restore of the pinned shell packages).
param(
    [ValidateSet('Debug', 'Release')] [string] $Configuration = 'Debug',
    [switch] $SkipShell
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root 'out\build\windows-msvc'

Push-Location $root
try {
    # Let CMake select the newest installed Visual Studio. The local baseline is VS 2022,
    # while current windows-2025 hosted runners provide VS 2026.
    cmake -S $root -B $buildDir -A x64 -D CATRO_BUILD_TESTS=ON
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
    cmake --build $buildDir --config $Configuration
    if ($LASTEXITCODE -ne 0) { throw 'CMake build failed.' }
    if ($SkipShell) { return }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $msbuild = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'MSBuild\**\Bin\MSBuild.exe' |
        Select-Object -First 1
    if (-not $msbuild) { throw 'MSBuild not found; run scripts/bootstrap.ps1.' }
    & $msbuild (Join-Path $root 'apps\windows\Catro.sln') -restore -p:RestorePackagesConfig=true `
        "-p:Configuration=$Configuration" -p:Platform=x64 "-p:CatroCoreRoot=$buildDir" -m -nologo -v:m
    if ($LASTEXITCODE -ne 0) { throw 'WinUI shell build failed.' }
    Write-Host "Shell: out\apps\windows\x64\$Configuration\Catro.exe"
} finally {
    Pop-Location
}
