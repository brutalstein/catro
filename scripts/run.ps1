# Launches the WinUI diagnostics shell, or with -Report writes a capability report instead.
# Extra arguments pass to the report tool, e.g. ./scripts/run.ps1 -Report --format json --output out/report.json
[CmdletBinding(PositionalBinding = $false)]
param(
    [ValidateSet('Debug', 'Release')] [string] $Configuration = 'Debug',
    [switch] $Report,
    [Parameter(ValueFromRemainingArguments = $true)] [string[]] $ReportArguments
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if ($Report) {
    $tool = Join-Path $root "out\build\windows-msvc\$Configuration\catro-capability-report.exe"
    if (-not (Test-Path $tool)) { throw "Report tool not built: $tool. Run scripts/build.ps1." }
    & $tool @ReportArguments
    exit $LASTEXITCODE
}
$shell = Join-Path $root "out\apps\windows\x64\$Configuration\Catro.exe"
if (-not (Test-Path $shell)) { throw "Shell not built: $shell. Run scripts/build.ps1." }
Start-Process $shell
