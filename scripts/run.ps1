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

# A live process is not enough for a GUI app: wait until WinUI has actually created a top-level
# window. This catches startup hangs where the process survives but XAML initialization never
# reaches Window::Activate().
$deadline = [DateTime]::UtcNow.AddSeconds(8)
$windowReady = $false
do {
    Start-Sleep -Milliseconds 150
    $process.Refresh()
    if ($process.HasExited) {
        $exitCode = $process.ExitCode
        throw "Catro exited during startup with code $exitCode. Rebuild with scripts/build.ps1 and retry."
    }
    if ($process.MainWindowHandle -ne 0) {
        $windowReady = $true
        break
    }
} while ([DateTime]::UtcNow -lt $deadline)

if (-not $windowReady) {
    throw "Catro process is alive (PID $($process.Id)) but no top-level window was created within 8 seconds."
}

Write-Host "[catro] Started Catro (PID $($process.Id), HWND 0x$('{0:X}' -f $process.MainWindowHandle))."
