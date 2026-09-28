param(
    [ValidateSet('Release','Debug')]
    [string] $Configuration = 'Release',
    [switch] $SkipBuild,
    [string] $ServiceUrl = $env:CATRO_SERVICE_URL,
    [switch] $AllowInsecureService,
    [switch] $Engineering
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$arch = 'x64'
$source = Join-Path $root "out\apps\windows\$arch\$Configuration"
$distRoot = Join-Path $root 'out\dist'
$staging = Join-Path $distRoot "Catro-windows-$arch"
$zip = Join-Path $distRoot "Catro-windows-$arch.zip"
$sha = "$zip.sha256"

Push-Location $root
try {
    if (-not $SkipBuild) {
        & (Join-Path $PSScriptRoot 'build.ps1') -Configuration $Configuration
        if ($LASTEXITCODE -ne 0) {
            throw "Catro build failed with code $LASTEXITCODE."
        }
    }

    $exe = Join-Path $source 'Catro.exe'
    $voice = Join-Path $source 'catro-voice-runtime.dll'
    $room = Join-Path $source 'catro-room-runtime.dll'
    if (-not (Test-Path $exe)) {
        throw "Catro executable not found: $exe"
    }
    if (-not (Test-Path $voice)) {
        throw "Voice runtime not found: $voice"
    }
    if (-not (Test-Path $room)) {
        throw "Room runtime not found: $room"
    }

    # Self-contained Windows App SDK output must contain its native runtime beside the executable.
    # Match by the stable Microsoft.WindowsAppRuntime prefix instead of pinning a package build path.
    $appRuntime = Get-ChildItem $source -File -ErrorAction Stop |
        Where-Object { $_.Name -like 'Microsoft.WindowsAppRuntime*.dll' } |
        Select-Object -First 1
    if (-not $appRuntime) {
        throw 'Self-contained Windows App SDK runtime DLLs are missing from the shell output.'
    }

    New-Item -ItemType Directory -Force -Path $distRoot | Out-Null
    if (Test-Path $staging) {
        Remove-Item -Recurse -Force $staging
    }
    New-Item -ItemType Directory -Force -Path $staging | Out-Null

    Copy-Item -Path (Join-Path $source '*') -Destination $staging -Recurse -Force

    if (-not $Engineering) {
        if ([string]::IsNullOrWhiteSpace($ServiceUrl)) {
            throw 'Production package requires -ServiceUrl https://your-catro-service (or CATRO_SERVICE_URL). Use -Engineering only for local direct-peer validation packages.'
        }

        $serviceUri = $null
        if (-not [Uri]::TryCreate($ServiceUrl, [UriKind]::Absolute, [ref]$serviceUri)) {
            throw "Invalid Catro service URL: $ServiceUrl"
        }
        if ($serviceUri.Scheme -ne 'https' -and
            -not ($AllowInsecureService -and $serviceUri.Scheme -eq 'http')) {
            throw 'Production Catro service URL must use HTTPS. -AllowInsecureService is engineering/LAN-only.'
        }

        $networkConfig = [ordered]@{
            api_base_url = $serviceUri.AbsoluteUri.TrimEnd('/')
            allow_insecure_http = [bool]$AllowInsecureService
        } | ConvertTo-Json -Depth 4

        $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
        [IO.File]::WriteAllText(
            (Join-Path $staging 'catro-network.json'),
            $networkConfig,
            $utf8NoBom)
    }

    $readme = @"
Catro Windows x64

Run Catro.exe.

This build is self-contained: the Windows App SDK and MSVC runtime are shipped with the app.
No Visual Studio, CMake, Windows App SDK runtime installer, or VC++ Redistributable is required
on the target computer.

Minimum OS: Windows 10 version 2004 (build 19041) or newer.
For screen sharing and hardware video acceleration, current Windows 11 and current GPU drivers are
recommended.

Production RTC uses the Catro service configured in catro-network.json. The client registers its
stable local identity, joins shared servers with invite codes, requests short-lived room tokens,
and receives WSS + ICE/STUN/TURN provisioning from the service automatically. No end-user
CATRO_ROOM_TOKEN, CATRO_SERVER_ID, or CATRO_ICE_SERVERS variables are required.

The per-install directory credential is generated on first run and stored under LocalAppData with
Windows DPAPI protection. Room tokens are short-lived and are never written to the package.

Engineering CATRO_* direct-peer variables remain available only for local validation.
"@
    Set-Content -Path (Join-Path $staging 'README.txt') -Value $readme -Encoding UTF8

    if (Test-Path $zip) {
        Remove-Item -Force $zip
    }
    Compress-Archive -Path (Join-Path $staging '*') -DestinationPath $zip -CompressionLevel Optimal

    $digest = (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLowerInvariant()
    Set-Content -Path $sha -Value "$digest  $([IO.Path]::GetFileName($zip))" -Encoding ASCII

    Write-Host "[catro] Portable package: $zip"
    Write-Host "[catro] SHA-256: $digest"
} finally {
    Pop-Location
}
