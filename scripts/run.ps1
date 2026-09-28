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
if (-not (Test-Path $shell)) {
    throw "Shell not built: $shell. Run scripts/build.ps1."
}

# IMPORTANT:
# Catro is currently a self-contained, unpackaged WinUI 3 application. Do not call
# 'winapp create-debug-identity' here: that command embeds sparse-package identity metadata directly
# into the already-built executable's SxS manifest. The self-contained shell also carries Windows
# App SDK reg-free activation metadata, and mutating the executable after MSBuild can make startup
# fail before the first window is created.
#
# Borderless WGC will move to a proper packaged/loose-layout development launch path. Until that
# packaging gate is complete, normal startup is more important than silently mutating the binary.
$process = Start-Process -FilePath $shell -PassThru

# Detect immediate startup failures instead of returning a misleading successful prompt.
Start-Sleep -Milliseconds 1200
if ($process.HasExited) {
    $exitCode = $process.ExitCode
    throw "Catro exited during startup with code $exitCode. Rebuild with scripts/build.ps1 and retry."
}

Write-Host "[catro] Started Catro (PID $($process.Id))."
