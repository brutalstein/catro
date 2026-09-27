# Runs the local Windows verification gate and stores a transcript under out/logs/.
# Nothing is installed or system settings changed. Use -AudioMeter only when you explicitly
# want to exercise the microphone path for a few seconds.
param(
    [ValidateSet('Debug', 'Release')] [string] $Configuration = 'Debug',
    [switch] $SkipShell,
    [switch] $AudioMeter
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$logDir = Join-Path $root "out\logs\verify-$stamp"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$transcript = Join-Path $logDir 'session.log'

Start-Transcript -Path $transcript -Force | Out-Null
try {
    Push-Location $root
    try {
        Write-Host '=== CATRO LOCAL VERIFICATION ==='
        Write-Host "[catro] Time: $(Get-Date -Format o)"
        Write-Host "[catro] PowerShell: $($PSVersionTable.PSVersion)"
        Write-Host "[catro] OS: $([System.Environment]::OSVersion.VersionString)"
        Write-Host "[catro] Architecture: $env:PROCESSOR_ARCHITECTURE"
        Write-Host "[catro] Git: $(git --version)"
        Write-Host "[catro] Commit: $(git rev-parse HEAD)"
        Write-Host '[catro] Working tree:'
        git status --short

        Write-Host '\n=== PREREQUISITES ==='
        & (Join-Path $PSScriptRoot 'bootstrap.ps1')
        if ($LASTEXITCODE -ne 0) { throw 'Prerequisite check failed.' }

        Write-Host '\n=== BUILD ==='
        $buildArgs = @{ Configuration = $Configuration }
        if ($SkipShell) { $buildArgs.SkipShell = $true }
        & (Join-Path $PSScriptRoot 'build.ps1') @buildArgs
        if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }

        Write-Host '\n=== TESTS ==='
        & (Join-Path $PSScriptRoot 'test.ps1') -Configuration $Configuration
        if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }

        Write-Host '\n=== REDACTED CAPABILITY REPORT ==='
        $reportPath = Join-Path $logDir 'capability-report.txt'
        & (Join-Path $PSScriptRoot 'run.ps1') -Configuration $Configuration -Report --format human --output $reportPath
        if ($LASTEXITCODE -ne 0) { throw 'Capability report failed.' }
        Get-Content $reportPath

        if ($AudioMeter) {
            Write-Host '\n=== AUDIO METER (3 seconds) ==='
            $audioTool = Join-Path $root "out\build\windows-msvc\$Configuration\catro-audio-check.exe"
            if (-not (Test-Path $audioTool)) { throw "Audio tool not built: $audioTool" }
            & $audioTool --mode meter --seconds 3
            if ($LASTEXITCODE -ne 0) { throw 'Audio meter check failed.' }
        }

        Write-Host "\n[catro] Verification completed successfully."
        Write-Host "[catro] Logs: $logDir"
    } finally {
        Pop-Location
    }
} catch {
    Write-Host "\n[catro] VERIFICATION FAILED: $($_.Exception.Message)"
    Write-Host "[catro] Logs: $logDir"
    throw
} finally {
    Stop-Transcript | Out-Null
}
