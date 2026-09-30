$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$installer = Join-Path $root 'scripts\install-windows.ps1'
if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) {
    throw "Windows installer does not exist: $installer"
}

function Assert-True {
    param([bool] $Condition, [string] $Message)
    if (-not $Condition) { throw $Message }
}

function Assert-Equal {
    param($Expected, $Actual, [string] $Message)
    if ($Expected -ne $Actual) {
        throw "$Message Expected '$Expected', got '$Actual'."
    }
}

$testRoot = Join-Path ([IO.Path]::GetTempPath()) ("catro-windows-installer-test-" + [Guid]::NewGuid().ToString('N'))
$fixtureRoot = Join-Path $testRoot 'fixtures'
$installParent = Join-Path $testRoot 'install-parent'
$installRoot = Join-Path $installParent 'Catro'
$sentinel = Join-Path $installParent 'sibling-sentinel.txt'
$launchMarker = Join-Path $testRoot 'app-launched.txt'
$server = $null

function New-TestPackage {
    param(
        [Parameter(Mandatory = $true)][string] $Version,
        [switch] $MissingExecutable,
        [ValidateSet('Valid', 'Malformed', 'Mismatch')][string] $Checksum = 'Valid'
    )

    $releaseDirectory = Join-Path $fixtureRoot "download\$Version"
    $packageDirectory = Join-Path $testRoot "package-$Version"
    New-Item -ItemType Directory -Force -Path $releaseDirectory, $packageDirectory | Out-Null

    if (-not $MissingExecutable) {
        Copy-Item -LiteralPath $script:probeExecutable -Destination (Join-Path $packageDirectory 'Catro.exe')
    }
    Set-Content -LiteralPath (Join-Path $packageDirectory 'version.txt') -Value $Version -Encoding ASCII

    $archive = Join-Path $releaseDirectory 'Catro-windows-x64.zip'
    Compress-Archive -Path (Join-Path $packageDirectory '*') -DestinationPath $archive
    $checksumPath = "$archive.sha256"

    switch ($Checksum) {
        'Valid' {
            $digest = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
            Set-Content -LiteralPath $checksumPath -Value "$digest  Catro-windows-x64.zip" -Encoding ASCII
        }
        'Malformed' {
            Set-Content -LiteralPath $checksumPath -Value 'not-a-sha256-digest' -Encoding ASCII
        }
        'Mismatch' {
            Set-Content -LiteralPath $checksumPath -Value "$('0' * 64)  Catro-windows-x64.zip" -Encoding ASCII
        }
    }
}

function Invoke-TestInstaller {
    param(
        [Parameter(Mandatory = $true)][string] $Version,
        [hashtable] $Overrides = @{}
    )

    $environment = @{
        CATRO_RELEASE_BASE_URL = $script:baseUrl
        CATRO_INSTALL_ROOT = $installRoot
        CATRO_VERSION = $Version
        CATRO_TEST_ARCHITECTURE = 'x64'
        CATRO_TEST_FAIL_AFTER_BACKUP = $null
        CATRO_TEST_LAUNCH_MARKER = $launchMarker
    }
    foreach ($entry in $Overrides.GetEnumerator()) {
        $environment[$entry.Key] = $entry.Value
    }

    $previous = @{}
    foreach ($name in $environment.Keys) {
        $previous[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
        [Environment]::SetEnvironmentVariable($name, $environment[$name], 'Process')
    }

    try {
        $oldPreference = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        $output = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $installer 2>&1 | Out-String
        $exitCode = $LASTEXITCODE
        $ErrorActionPreference = $oldPreference
        return [pscustomobject]@{
            ExitCode = $exitCode
            Output = $output
        }
    } finally {
        $ErrorActionPreference = 'Stop'
        foreach ($name in $previous.Keys) {
            [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process')
        }
    }
}

function Assert-InstalledVersion {
    param([string] $Expected)
    Assert-True (Test-Path -LiteralPath (Join-Path $installRoot 'Catro.exe') -PathType Leaf) 'Installed Catro.exe is missing.'
    $actual = (Get-Content -LiteralPath (Join-Path $installRoot 'version.txt') -Raw).Trim()
    Assert-Equal $Expected $actual 'Installed version marker is wrong.'
}

try {
    New-Item -ItemType Directory -Force -Path $fixtureRoot, $installParent | Out-Null
    Set-Content -LiteralPath $sentinel -Value 'untouched' -Encoding ASCII

    $probeSource = @'
using System;
using System.IO;
public static class CatroLaunchProbe
{
    public static void Main()
    {
        var marker = Environment.GetEnvironmentVariable("CATRO_TEST_LAUNCH_MARKER");
        if (!String.IsNullOrEmpty(marker))
        {
            File.WriteAllText(marker, "launched");
        }
    }
}
'@
    $script:probeExecutable = Join-Path $testRoot 'launch-probe.exe'
    Add-Type -TypeDefinition $probeSource -Language CSharp -OutputAssembly $script:probeExecutable -OutputType ConsoleApplication

    New-TestPackage -Version 'v1'
    New-TestPackage -Version 'v2'
    New-TestPackage -Version 'malformed' -Checksum Malformed
    New-TestPackage -Version 'mismatch' -Checksum Mismatch
    New-TestPackage -Version 'missing-executable' -MissingExecutable

    $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $listener.Start()
    $port = ([Net.IPEndPoint]$listener.LocalEndpoint).Port
    $listener.Stop()

    $python = (Get-Command python -ErrorAction Stop).Source
    $server = Start-Process -FilePath $python `
        -ArgumentList @('-m', 'http.server', $port, '--bind', '127.0.0.1', '--directory', "`"$fixtureRoot`"") `
        -WindowStyle Hidden -PassThru
    $script:baseUrl = "http://127.0.0.1:$port"

    $ready = $false
    for ($attempt = 0; $attempt -lt 50 -and -not $ready; $attempt++) {
        try {
            Invoke-WebRequest -UseBasicParsing -Uri "$script:baseUrl/" -TimeoutSec 1 | Out-Null
            $ready = $true
        } catch {
            Start-Sleep -Milliseconds 100
        }
    }
    Assert-True $ready 'Loopback fixture server did not become ready.'

    $result = Invoke-TestInstaller -Version 'v1'
    Assert-Equal 0 $result.ExitCode "First install failed. Output: $($result.Output)"
    Assert-InstalledVersion 'v1'

    $result = Invoke-TestInstaller -Version 'v2'
    Assert-Equal 0 $result.ExitCode "Upgrade failed. Output: $($result.Output)"
    Assert-InstalledVersion 'v2'

    $result = Invoke-TestInstaller -Version 'v1' -Overrides @{ CATRO_TEST_FAIL_AFTER_BACKUP = '1' }
    Assert-True ($result.ExitCode -ne 0) 'Forced replacement failure unexpectedly succeeded.'
    Assert-InstalledVersion 'v2'
    Assert-Equal 0 @(Get-ChildItem -LiteralPath $installParent -Directory -Filter 'Catro.backup-*').Count 'Rollback left a backup directory behind.'

    foreach ($rejectedVersion in @('malformed', 'mismatch', 'missing-executable')) {
        $result = Invoke-TestInstaller -Version $rejectedVersion
        Assert-True ($result.ExitCode -ne 0) "Invalid package '$rejectedVersion' unexpectedly succeeded."
        Assert-InstalledVersion 'v2'
    }

    $result = Invoke-TestInstaller -Version 'v2' -Overrides @{ CATRO_TEST_ARCHITECTURE = 'x86' }
    Assert-True ($result.ExitCode -ne 0) 'Non-x64 installation unexpectedly succeeded.'
    Assert-InstalledVersion 'v2'

    Assert-True (-not (Test-Path -LiteralPath $launchMarker)) 'Installer launched Catro automatically.'
    Assert-Equal 'untouched' ((Get-Content -LiteralPath $sentinel -Raw).Trim()) 'Installer modified a sibling path.'

    Write-Host 'Windows installer tests passed.'
} finally {
    if ($server -and -not $server.HasExited) {
        Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
        $server.WaitForExit()
    }
    if (Test-Path -LiteralPath $testRoot) {
        Remove-Item -LiteralPath $testRoot -Recurse -Force
    }
}

exit 0
