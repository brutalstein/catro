# Runs the CTest suite of a build made by build.ps1. -Filter runs a focused subset by regex.
param(
    [ValidateSet('Debug', 'Release')] [string] $Configuration = 'Debug',
    [string] $Filter
)
$ErrorActionPreference = 'Stop'
$buildDir = Join-Path (Split-Path -Parent $PSScriptRoot) 'out\build\windows-msvc'
$arguments = @('--test-dir', $buildDir, '-C', $Configuration, '--output-on-failure')
if ($Filter) { $arguments += @('-R', $Filter) }
ctest @arguments
exit $LASTEXITCODE
