# Launches Catro, lets it settle, closes the main window like a user would, and requires a clean
# exit. Catches shutdown-only failures (for example XAML access after the window closed), which
# surface as 0xC000027B stowed exceptions and never show up in unit tests.
param(
    [string] $Executable,
    [int] $SettleSeconds = 15
)

$ErrorActionPreference = 'Stop'

if (-not $Executable) {
    $Executable = Join-Path (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path 'out\apps\windows\x64\Debug\Catro.exe'
}
$process = Start-Process -FilePath (Resolve-Path $Executable).Path -PassThru
try {
    Start-Sleep -Seconds $SettleSeconds
    $process.Refresh()
    if ($process.HasExited) {
        throw "Catro exited during startup with code $($process.ExitCode)."
    }
    if (-not $process.CloseMainWindow()) {
        throw 'Catro has no main window to close.'
    }
    if (-not $process.WaitForExit(15000)) {
        throw 'Catro did not exit within 15 seconds of closing its window.'
    }
    if ($process.ExitCode -ne 0) {
        throw ('Catro exited with code 0x{0:X8} after its window closed.' -f $process.ExitCode)
    }
    Write-Host 'Windows lifecycle smoke passed.'
} finally {
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
    }
}
