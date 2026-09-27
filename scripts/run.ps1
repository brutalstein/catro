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

# Borderless WGC is consent-gated and requires package identity. If Microsoft's optional WinApp CLI
# is available, attach the checked-in sparse debug identity to this exact executable. A failure here
# must never block normal Catro startup; Windows simply keeps its standard capture border.
$winapp = Get-Command winapp -ErrorAction SilentlyContinue
$identityManifest = Join-Path $root 'apps\windows\Catro\sparse.appxmanifest.xml'
if ($winapp -and (Test-Path $identityManifest)) {
    & $winapp.Source create-debug-identity $shell --manifest $identityManifest
    if ($LASTEXITCODE -ne 0) {
        Write-Warning 'Debug identity registration failed; borderless capture may be unavailable.'
    }
} else {
    Write-Warning 'WinApp CLI not found; Catro will run normally, but Windows may keep the capture border.'
}

Start-Process $shell
