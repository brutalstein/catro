$ErrorActionPreference = 'Stop'

$assetName = 'Catro-windows-x64.zip'
$checksumName = "$assetName.sha256"
$releaseBaseOverride = $env:CATRO_RELEASE_BASE_URL
$installRootOverride = $env:CATRO_INSTALL_ROOT
$requestedVersion = $env:CATRO_VERSION
$architectureOverride = $env:CATRO_TEST_ARCHITECTURE

$architecture = if ([string]::IsNullOrWhiteSpace($architectureOverride)) {
    [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
} else {
    $architectureOverride
}
if (-not $architecture.Equals('x64', [StringComparison]::OrdinalIgnoreCase)) {
    throw "Catro for Windows supports x64 only; detected architecture '$architecture'."
}

$releaseBase = if ([string]::IsNullOrWhiteSpace($releaseBaseOverride)) {
    'https://github.com/brutalstein/catro/releases'
} else {
    $releaseBaseOverride.TrimEnd('/')
}

$releaseUri = $null
if (-not [Uri]::TryCreate($releaseBase, [UriKind]::Absolute, [ref]$releaseUri)) {
    throw "Invalid Catro release base URL: $releaseBase"
}
if (-not [string]::IsNullOrEmpty($releaseUri.UserInfo) -or
    -not [string]::IsNullOrEmpty($releaseUri.Query) -or
    -not [string]::IsNullOrEmpty($releaseUri.Fragment)) {
    throw 'Catro release base URL must not contain credentials, query parameters, or fragments.'
}
if ($releaseUri.Scheme -ne 'https') {
    $loopbackTestUrl = -not [string]::IsNullOrWhiteSpace($releaseBaseOverride) -and
        $releaseUri.Scheme -eq 'http' -and
        $releaseUri.IsLoopback
    if (-not $loopbackTestUrl) {
        throw 'Catro release downloads require HTTPS; HTTP is allowed only for an explicit loopback test URL.'
    }
}

$installRoot = if ([string]::IsNullOrWhiteSpace($installRootOverride)) {
    Join-Path (Join-Path $env:LOCALAPPDATA 'Programs') 'Catro'
} else {
    [IO.Path]::GetFullPath($installRootOverride)
}

$selector = if ([string]::IsNullOrWhiteSpace($requestedVersion)) {
    'latest/download'
} else {
    "download/$([Uri]::EscapeDataString($requestedVersion))"
}
$archiveUrl = "$releaseBase/$selector/$assetName"
$checksumUrl = "$releaseBase/$selector/$checksumName"

$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) ("catro-install-" + [Guid]::NewGuid().ToString('N'))
$archivePath = Join-Path $temporaryRoot $assetName
$checksumPath = Join-Path $temporaryRoot $checksumName
$stagingRoot = Join-Path $temporaryRoot 'staging'
$backupPath = "$installRoot.backup-$([Guid]::NewGuid().ToString('N'))"

try {
    New-Item -ItemType Directory -Force -Path $temporaryRoot, $stagingRoot | Out-Null

    Invoke-WebRequest -UseBasicParsing -Uri $archiveUrl -OutFile $archivePath
    Invoke-WebRequest -UseBasicParsing -Uri $checksumUrl -OutFile $checksumPath

    $checksumText = (Get-Content -LiteralPath $checksumPath -Raw).Trim()
    $checksumMatch = [Text.RegularExpressions.Regex]::Match(
        $checksumText,
        '^([0-9A-Fa-f]{64})(?:[ \t]+[^\r\n]+)?$')
    if (-not $checksumMatch.Success) {
        throw 'Release checksum must contain exactly one 64-character SHA-256 digest.'
    }

    $expectedDigest = $checksumMatch.Groups[1].Value
    $actualDigest = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
    if (-not $actualDigest.Equals($expectedDigest, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Release checksum mismatch for $assetName."
    }

    Expand-Archive -LiteralPath $archivePath -DestinationPath $stagingRoot
    $stagedExecutable = Join-Path $stagingRoot 'Catro.exe'
    if (-not (Test-Path -LiteralPath $stagedExecutable -PathType Leaf)) {
        throw 'Release package does not contain Catro.exe at its root.'
    }

    $installParent = Split-Path -Parent $installRoot
    New-Item -ItemType Directory -Force -Path $installParent | Out-Null

    $backupCreated = $false
    try {
        if (Test-Path -LiteralPath $installRoot) {
            Move-Item -LiteralPath $installRoot -Destination $backupPath
            $backupCreated = $true
        }

        if ($backupCreated -and $env:CATRO_TEST_FAIL_AFTER_BACKUP -eq '1') {
            throw 'Forced installer failure after backup.'
        }

        Move-Item -LiteralPath $stagingRoot -Destination $installRoot
    } catch {
        if (Test-Path -LiteralPath $installRoot) {
            Remove-Item -LiteralPath $installRoot -Recurse -Force
        }
        if ($backupCreated -and (Test-Path -LiteralPath $backupPath)) {
            Move-Item -LiteralPath $backupPath -Destination $installRoot
        }
        throw
    }

    if ($backupCreated -and (Test-Path -LiteralPath $backupPath)) {
        Remove-Item -LiteralPath $backupPath -Recurse -Force
    }

    if ([string]::IsNullOrWhiteSpace($installRootOverride)) {
        try {
            $programs = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs'
            New-Item -ItemType Directory -Force -Path $programs | Out-Null
            $shell = New-Object -ComObject WScript.Shell
            $shortcut = $shell.CreateShortcut((Join-Path $programs 'Catro.lnk'))
            $shortcut.TargetPath = Join-Path $installRoot 'Catro.exe'
            $shortcut.WorkingDirectory = $installRoot
            $shortcut.Save()
        } catch {
            Write-Warning "Catro installed, but the Start Menu shortcut could not be created: $($_.Exception.Message)"
        }
    }

    $installedVersion = if ([string]::IsNullOrWhiteSpace($requestedVersion)) { 'latest' } else { $requestedVersion }
    Write-Host "[catro] Installed version: $installedVersion"
    Write-Host "[catro] Executable: $(Join-Path $installRoot 'Catro.exe')"
} finally {
    if (Test-Path -LiteralPath $temporaryRoot) {
        Remove-Item -LiteralPath $temporaryRoot -Recurse -Force
    }
}
